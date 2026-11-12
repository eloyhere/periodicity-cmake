// Periodicity — cross-process ring buffer in named shared memory (pool IPC).
//
// A pipe copies every byte through the kernel twice. When the payload rate is
// the point (telemetry, a job queue, frames), two processes can instead map the
// same pages and pass indices:
//
//   parent: auto ring = SharedRingBuffer::create(name, 1 << 20, &error);
//   child:  auto ring = SharedRingBuffer::open(name, 1 << 20, &error);
//   parent: ring->push(encodeFrame("PING 1"));
//   child:  ring->pop();
//
// The mapping starts with a header — magic, version, capacity and the two
// cursors — followed by the byte storage. Cursors are offsets, never pointers,
// because the two processes may map at different addresses. `push()` writes a
// whole frame or nothing: a partial message can never be observed, so a full
// ring drops the frame and counts it.
//
// As with the in-process ring, the discipline is one producer and one consumer
// per direction. A duplex conversation uses two rings, one per direction.
//
// Names are per-session (`Local\` on Windows) and carry the creating PID, so a
// crashed process cannot collide with the next run; a leftover mapping simply
// disappears when its last handle closes.

#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace md::pool::ipc {

class SharedRingBuffer
{
public:
    // `capacity` is the byte capacity, rounded up to a power of two. Fails when
    // the name already exists (create) or does not (open).
    static std::unique_ptr<SharedRingBuffer> create(const QString& name,
                                                    std::size_t capacity,
                                                    QString* error);
    static std::unique_ptr<SharedRingBuffer> open(const QString& name,
                                                  std::size_t capacity,
                                                  QString* error);

    ~SharedRingBuffer();

    SharedRingBuffer(const SharedRingBuffer&) = delete;
    SharedRingBuffer& operator=(const SharedRingBuffer&) = delete;
    SharedRingBuffer(SharedRingBuffer&&) = delete;
    SharedRingBuffer& operator=(SharedRingBuffer&&) = delete;

    // Frames `payload` and appends it atomically. False when the ring is full or
    // the frame is larger than its capacity; a full ring counts a drop.
    bool push(QByteArrayView payload);

    // Returns the next whole frame, or nullopt when empty. Consumer side only.
    std::optional<QByteArray> pop();

    [[nodiscard]] std::size_t capacity() const;
    [[nodiscard]] std::size_t used() const;
    [[nodiscard]] std::uint64_t dropped() const;

    // "Local\Periodicity.<prefix>.<pid>.<sequence>" on Windows,
    // "/periodicity.<prefix>.<pid>.<sequence>" on POSIX.
    static QString uniqueName(const QString& prefix);

private:
    SharedRingBuffer();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace md::pool::ipc
