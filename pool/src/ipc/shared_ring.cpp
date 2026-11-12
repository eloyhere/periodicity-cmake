#include <md/pool/ipc/shared_ring.h>

#include <md/pool/ipc/frame.h>

#include <QByteArray>
#include <QString>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <new>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace md::pool::ipc {
namespace {

constexpr std::uint32_t kMagic = 0x5344524Du; // "MDRS"
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kMinCapacity = 4096;
constexpr std::size_t kMaxCapacity = std::size_t{1} << 28; // 256 MiB

// The mapping begins with this header. Cursors are byte offsets into the
// storage that follows: a pointer would be meaningless in the other process.
struct alignas(64) RingHeader
{
    std::uint32_t magic = kMagic;
    std::uint32_t version = kVersion;
    std::uint64_t capacity = 0;
    std::atomic<std::uint64_t> head{0};
    std::atomic<std::uint64_t> tail{0};
    std::atomic<std::uint64_t> dropped{0};
    std::uint64_t reserved[3] = {0, 0, 0};
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "shared cursors must be lock-free");
static_assert(sizeof(RingHeader) == 64, "the header is one cache line");
static_assert(alignof(RingHeader) == 64, "the payload starts on a cache line");

std::size_t roundUpToPowerOfTwo(std::size_t value)
{
    std::size_t result = kMinCapacity;
    while (result < value && result < kMaxCapacity)
        result <<= 1;
    return result;
}

QString lastError()
{
#ifdef Q_OS_WIN
    return QStringLiteral("Windows error %1").arg(static_cast<qulonglong>(::GetLastError()));
#else
    return QString::fromLocal8Bit(std::strerror(errno));
#endif
}

void writeAt(unsigned char* data, std::size_t capacity, std::uint64_t offset,
             const void* source, std::size_t size)
{
    const std::size_t start = static_cast<std::size_t>(offset & (capacity - 1));
    const std::size_t first = std::min(size, capacity - start);
    std::memcpy(data + start, source, first);
    if (size > first)
        std::memcpy(data, static_cast<const unsigned char*>(source) + first, size - first);
}

void readAt(const unsigned char* data, std::size_t capacity, std::uint64_t offset,
            void* destination, std::size_t size)
{
    const std::size_t start = static_cast<std::size_t>(offset & (capacity - 1));
    const std::size_t first = std::min(size, capacity - start);
    std::memcpy(destination, data + start, first);
    if (size > first)
        std::memcpy(static_cast<unsigned char*>(destination) + first, data, size - first);
}

std::uint64_t currentProcessId()
{
#ifdef Q_OS_WIN
    return static_cast<std::uint64_t>(::GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

} // namespace

struct SharedRingBuffer::Impl
{
    RingHeader* header = nullptr;
    unsigned char* data = nullptr;
    std::size_t capacity = 0;
    bool creator = false;

#ifdef Q_OS_WIN
    HANDLE mapping = nullptr;
    void* view = nullptr;
#else
    int fd = -1;
    void* view = nullptr;
#endif
};

SharedRingBuffer::SharedRingBuffer()
    : impl_(std::make_unique<Impl>())
{
}

SharedRingBuffer::~SharedRingBuffer()
{
    if (!impl_)
        return;

    if (impl_->view != nullptr) {
#ifdef Q_OS_WIN
        ::UnmapViewOfFile(impl_->view);
#else
        ::munmap(impl_->view, sizeof(RingHeader) + impl_->capacity);
#endif
        impl_->view = nullptr;
    }

#ifdef Q_OS_WIN
    if (impl_->mapping != nullptr) {
        ::CloseHandle(impl_->mapping);
        impl_->mapping = nullptr;
    }
#else
    if (impl_->fd >= 0) {
        ::close(impl_->fd);
        impl_->fd = -1;
    }
#endif
}

std::unique_ptr<SharedRingBuffer> SharedRingBuffer::create(const QString& name,
                                                           std::size_t capacity,
                                                           QString* error)
{
    const auto fail = [error](const QString& message) -> std::unique_ptr<SharedRingBuffer> {
        if (error != nullptr)
            *error = message;
        return nullptr;
    };

    if (name.isEmpty())
        return fail(QStringLiteral("SharedRingBuffer: empty name"));

    const std::size_t capacityBytes = roundUpToPowerOfTwo(std::max(capacity, kMinCapacity));
    const std::size_t total = sizeof(RingHeader) + capacityBytes;

#ifdef Q_OS_WIN
    if (total > 0xFFFFFFFFull)
        return fail(QStringLiteral("SharedRingBuffer: mapping is too large"));

    const std::wstring wideName = name.toStdWString();
    HANDLE mapping = ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                          static_cast<DWORD>(total), wideName.c_str());
    if (mapping == nullptr)
        return fail(QStringLiteral("CreateFileMapping failed: %1").arg(lastError()));

    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        ::CloseHandle(mapping);
        return fail(QStringLiteral("shared memory '%1' already exists").arg(name));
    }

    void* view = ::MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, total);
    if (view == nullptr) {
        ::CloseHandle(mapping);
        return fail(QStringLiteral("MapViewOfFile failed: %1").arg(lastError()));
    }
#else
    const QByteArray localName = name.toLocal8Bit();
    const int fd = ::shm_open(localName.constData(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0)
        return fail(QStringLiteral("shm_open('%1') failed: %2").arg(name, lastError()));

    if (::ftruncate(fd, static_cast<off_t>(total)) != 0) {
        ::close(fd);
        ::shm_unlink(localName.constData());
        return fail(QStringLiteral("ftruncate failed: %1").arg(lastError()));
    }

    void* view = ::mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (view == MAP_FAILED) {
        ::close(fd);
        ::shm_unlink(localName.constData());
        return fail(QStringLiteral("mmap failed: %1").arg(lastError()));
    }
#endif

    auto ring = std::unique_ptr<SharedRingBuffer>(new SharedRingBuffer());

#ifdef Q_OS_WIN
    ring->impl_->mapping = mapping;
#else
    ring->impl_->fd = fd;
#endif
    ring->impl_->view = view;
    ring->impl_->capacity = capacityBytes;
    ring->impl_->creator = true;

    auto* header = ::new (view) RingHeader{};
    header->magic = kMagic;
    header->version = kVersion;
    header->capacity = static_cast<std::uint64_t>(capacityBytes);
    header->head.store(0, std::memory_order_relaxed);
    header->tail.store(0, std::memory_order_relaxed);
    header->dropped.store(0, std::memory_order_relaxed);

    ring->impl_->header = header;
    ring->impl_->data = reinterpret_cast<unsigned char*>(header) + sizeof(RingHeader);

    std::memset(ring->impl_->data, 0, capacityBytes);
    return ring;
}

std::unique_ptr<SharedRingBuffer> SharedRingBuffer::open(const QString& name,
                                                         std::size_t capacity,
                                                         QString* error)
{
    const auto fail = [error](const QString& message) -> std::unique_ptr<SharedRingBuffer> {
        if (error != nullptr)
            *error = message;
        return nullptr;
    };

    if (name.isEmpty())
        return fail(QStringLiteral("SharedRingBuffer: empty name"));

    const std::size_t capacityBytes = roundUpToPowerOfTwo(std::max(capacity, kMinCapacity));
    const std::size_t total = sizeof(RingHeader) + capacityBytes;

#ifdef Q_OS_WIN
    if (total > 0xFFFFFFFFull)
        return fail(QStringLiteral("SharedRingBuffer: mapping is too large"));

    const std::wstring wideName = name.toStdWString();
    HANDLE mapping = ::OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wideName.c_str());
    if (mapping == nullptr)
        return fail(QStringLiteral("OpenFileMapping('%1') failed: %2").arg(name, lastError()));

    void* view = ::MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, total);
    if (view == nullptr) {
        ::CloseHandle(mapping);
        return fail(QStringLiteral("MapViewOfFile failed: %1").arg(lastError()));
    }
#else
    const QByteArray localName = name.toLocal8Bit();
    const int fd = ::shm_open(localName.constData(), O_RDWR, 0600);
    if (fd < 0)
        return fail(QStringLiteral("shm_open('%1') failed: %2").arg(name, lastError()));

    void* view = ::mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (view == MAP_FAILED) {
        ::close(fd);
        return fail(QStringLiteral("mmap failed: %1").arg(lastError()));
    }
#endif

    auto ring = std::unique_ptr<SharedRingBuffer>(new SharedRingBuffer());

#ifdef Q_OS_WIN
    ring->impl_->mapping = mapping;
#else
    ring->impl_->fd = fd;
#endif
    ring->impl_->view = view;
    ring->impl_->capacity = capacityBytes;

    auto* header = static_cast<RingHeader*>(view);
    if (header->magic != kMagic || header->version != kVersion
        || header->capacity != static_cast<std::uint64_t>(capacityBytes)) {
#ifdef Q_OS_WIN
        ::UnmapViewOfFile(view);
        ::CloseHandle(mapping);
#else
        ::munmap(view, total);
        ::close(fd);
#endif
        return fail(QStringLiteral("shared memory '%1' has an incompatible header").arg(name));
    }

    ring->impl_->header = header;
    ring->impl_->data = reinterpret_cast<unsigned char*>(header) + sizeof(RingHeader);
    return ring;
}

bool SharedRingBuffer::push(QByteArrayView payload)
{
    if (!impl_ || impl_->header == nullptr)
        return false;

    const std::size_t length = payload.size();
    if (length > kMaxFrameSize)
        return false;

    const std::size_t frame = kFrameHeaderSize + length;
    if (frame > impl_->capacity)
        return false;

    RingHeader* header = impl_->header;
    const std::uint64_t head = header->head.load(std::memory_order_relaxed);
    const std::uint64_t tail = header->tail.load(std::memory_order_acquire);
    const std::uint64_t used = head - tail;

    if (static_cast<std::uint64_t>(impl_->capacity) - used < frame) {
        header->dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    const unsigned char lengthBytes[4] = {
        static_cast<unsigned char>((length >> 0) & 0xFFu),
        static_cast<unsigned char>((length >> 8) & 0xFFu),
        static_cast<unsigned char>((length >> 16) & 0xFFu),
        static_cast<unsigned char>((length >> 24) & 0xFFu)};

    writeAt(impl_->data, impl_->capacity, head, lengthBytes, kFrameHeaderSize);
    if (length != 0)
        writeAt(impl_->data, impl_->capacity, head + kFrameHeaderSize, payload.data(), length);

    header->head.store(head + frame, std::memory_order_release);
    return true;
}

std::optional<QByteArray> SharedRingBuffer::pop()
{
    if (!impl_ || impl_->header == nullptr)
        return std::nullopt;

    RingHeader* header = impl_->header;
    const std::uint64_t tail = header->tail.load(std::memory_order_relaxed);
    const std::uint64_t head = header->head.load(std::memory_order_acquire);

    if (head == tail)
        return std::nullopt;

    const std::uint64_t available = head - tail;
    if (available < kFrameHeaderSize)
        return std::nullopt;

    unsigned char lengthBytes[4] = {};
    readAt(impl_->data, impl_->capacity, tail, lengthBytes, kFrameHeaderSize);

    const std::size_t length = static_cast<std::size_t>(lengthBytes[0])
        | (static_cast<std::size_t>(lengthBytes[1]) << 8)
        | (static_cast<std::size_t>(lengthBytes[2]) << 16)
        | (static_cast<std::size_t>(lengthBytes[3]) << 24);

    const std::size_t frame = kFrameHeaderSize + length;
    if (length > kMaxFrameSize || frame > available) {
        // A corrupt frame would otherwise stall the consumer forever: skip it
        // and account for it like a drop.
        header->dropped.fetch_add(1, std::memory_order_relaxed);
        header->tail.store(head, std::memory_order_release);
        return std::nullopt;
    }

    QByteArray payload;
    payload.resize(static_cast<qsizetype>(length));
    if (length != 0)
        readAt(impl_->data, impl_->capacity, tail + kFrameHeaderSize, payload.data(), length);

    header->tail.store(tail + frame, std::memory_order_release);
    return payload;
}

std::size_t SharedRingBuffer::capacity() const
{
    return impl_ ? impl_->capacity : 0;
}

std::size_t SharedRingBuffer::used() const
{
    if (!impl_ || impl_->header == nullptr)
        return 0;

    const std::uint64_t head = impl_->header->head.load(std::memory_order_acquire);
    const std::uint64_t tail = impl_->header->tail.load(std::memory_order_acquire);
    return static_cast<std::size_t>(head - tail);
}

std::uint64_t SharedRingBuffer::dropped() const
{
    if (!impl_ || impl_->header == nullptr)
        return 0;
    return impl_->header->dropped.load(std::memory_order_relaxed);
}

QString SharedRingBuffer::uniqueName(const QString& prefix)
{
    static std::atomic<std::uint64_t> sequence{0};
    const std::uint64_t index = sequence.fetch_add(1, std::memory_order_relaxed) + 1;

#ifdef Q_OS_WIN
    return QStringLiteral("Local\\Periodicity.%1.%2.%3")
        .arg(prefix)
        .arg(currentProcessId())
        .arg(index);
#else
    return QStringLiteral("/periodicity.%1.%2.%3")
        .arg(prefix)
        .arg(currentProcessId())
        .arg(index);
#endif
}

} // namespace md::pool::ipc
