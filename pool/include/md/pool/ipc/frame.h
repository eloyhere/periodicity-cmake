// Periodicity — length-prefixed message framing (pool IPC).
//
// A pipe is a byte stream, not a message queue: one read can carry half a
// message, three messages, or a message and the first byte of the next. Every
// IPC channel in this layer therefore runs the same framing:
//
//   [ uint32 little-endian payload length ][ payload bytes ]
//
// `FrameDecoder` is the reassembly half. Feed it whatever the transport
// delivered; ask it for whole frames until it returns nullopt. A length above
// the limit is a protocol error, not an allocation request, so a corrupt or
// malicious peer cannot make the reader reserve a gigabyte.

#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace md::pool::ipc {

inline constexpr std::size_t kFrameHeaderSize = 4;
inline constexpr std::size_t kMaxFrameSize = std::size_t{1} << 20; // 1 MiB

// Encodes one frame. A payload above kMaxFrameSize has no legal encoding — the
// decoder rejects the length — so this returns a null QByteArray instead of a
// message the far end can never accept. An empty payload is still a valid
// frame (a 4-byte header), so callers must test isNull(), not isEmpty().
inline QByteArray encodeFrame(QByteArrayView payload)
{
    const std::size_t length = payload.size();
    if (length > kMaxFrameSize)
        return QByteArray();

    QByteArray frame;
    frame.resize(static_cast<qsizetype>(kFrameHeaderSize + length));

    auto* bytes = reinterpret_cast<unsigned char*>(frame.data());
    bytes[0] = static_cast<unsigned char>((length >> 0) & 0xFFu);
    bytes[1] = static_cast<unsigned char>((length >> 8) & 0xFFu);
    bytes[2] = static_cast<unsigned char>((length >> 16) & 0xFFu);
    bytes[3] = static_cast<unsigned char>((length >> 24) & 0xFFu);

    if (length != 0)
        std::memcpy(frame.data() + kFrameHeaderSize, payload.data(), length);

    return frame;
}

class FrameDecoder
{
public:
    void append(QByteArrayView chunk)
    {
        if (!error_.isEmpty())
            return;

        if (chunk.isEmpty())
            return;

        const std::size_t consumed = static_cast<std::size_t>(offset_);
        if (consumed != 0 && consumed == static_cast<std::size_t>(buffer_.size())) {
            buffer_.clear();
            offset_ = 0;
        } else if (consumed >= 64 * 1024) {
            buffer_.remove(0, static_cast<qsizetype>(consumed));
            offset_ = 0;
        }

        buffer_.append(chunk.data(), static_cast<qsizetype>(chunk.size()));
    }

    // Returns the next whole frame, or nullopt when more bytes are needed.
    // Once hasError() is true the decoder stays failed until reset().
    std::optional<QByteArray> next()
    {
        if (!error_.isEmpty())
            return std::nullopt;

        const std::size_t available =
            static_cast<std::size_t>(buffer_.size()) - static_cast<std::size_t>(offset_);
        if (available < kFrameHeaderSize)
            return std::nullopt;

        const auto* bytes = reinterpret_cast<const unsigned char*>(buffer_.constData()) + offset_;
        const std::size_t length = static_cast<std::size_t>(bytes[0])
            | (static_cast<std::size_t>(bytes[1]) << 8)
            | (static_cast<std::size_t>(bytes[2]) << 16)
            | (static_cast<std::size_t>(bytes[3]) << 24);

        if (length > kMaxFrameSize) {
            error_ = QStringLiteral("frame of %1 bytes exceeds the %2 byte limit")
                         .arg(length)
                         .arg(kMaxFrameSize);
            return std::nullopt;
        }

        if (available < kFrameHeaderSize + length)
            return std::nullopt;

        const qsizetype begin = offset_ + static_cast<qsizetype>(kFrameHeaderSize);
        QByteArray payload = buffer_.mid(begin, static_cast<qsizetype>(length));
        offset_ = begin + static_cast<qsizetype>(length);

        if (static_cast<std::size_t>(offset_) == static_cast<std::size_t>(buffer_.size())) {
            buffer_.clear();
            offset_ = 0;
        }

        return payload;
    }

    [[nodiscard]] bool hasError() const noexcept { return !error_.isEmpty(); }
    [[nodiscard]] QString error() const { return error_; }
    [[nodiscard]] std::size_t buffered() const noexcept
    {
        return static_cast<std::size_t>(buffer_.size()) - static_cast<std::size_t>(offset_);
    }

    void reset()
    {
        buffer_.clear();
        offset_ = 0;
        error_.clear();
    }

private:
    QByteArray buffer_;
    qsizetype offset_ = 0;
    QString error_;
};

} // namespace md::pool::ipc
