// Periodicity — parent/child process over two pipes (pool IPC).
//
// The child is an ordinary executable: its stdin and stdout are the two pipes,
// and the parent writes frames to one and reads frames from the other. That is
// the smallest same-machine channel there is, and it needs no server, no port
// and no handshake.
//
//   auto child = ChildProcess::spawn({.program = "helper"}, onBytes, &error);
//   child->write(encodeFrame("PING 1"));
//   child->terminate();
//
// Two details are deliberate:
//
//   * only the two pipe ends are inherited (PROC_THREAD_ATTRIBUTE_HANDLE_LIST),
//     so a child can never inherit an unrelated file handle from the UI process;
//   * the output handler runs on an internal reader thread. It receives a view
//     that is only valid for the duration of the call, which is exactly what a
//     FrameDecoder::append() wants; copy it to keep it;
//   * the optional end-of-stream handler runs on that same thread once, after
//     the last output byte, when the child closes its stdout or exits. A
//     consumer reassembling frames needs it: without EOF, a child that dies
//     mid-frame leaves a partial message in the decoder and the consumer waits
//     forever. A handler that throws is contained; it cannot take the reader
//     thread (or the process) down.

#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <memory>

namespace md::pool::ipc {

class ChildProcess
{
public:
    // Called on the reader thread with the bytes as they arrive. The view is
    // ephemeral; copy anything that must outlive the call.
    using OutputHandler = std::function<void(QByteArrayView)>;

    // Called on the reader thread once, after the last output byte. A throwing
    // handler is swallowed: the reader thread and the child are unaffected.
    using EofHandler = std::function<void()>;

    struct Options
    {
        QString program;
        QStringList arguments;
        QString workingDirectory;
    };

    // Returns nullptr and fills `error` when the process could not be started.
    // `onEof` may be empty. Both handlers run on the reader thread and must not
    // outlive the ChildProcess unless they own their captures; the destructor
    // stops the child and joins that thread.
    static std::unique_ptr<ChildProcess> spawn(const Options& options,
                                               OutputHandler onOutput,
                                               QString* error,
                                               EofHandler onEof = {});

    ~ChildProcess();

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&&) = delete;
    ChildProcess& operator=(ChildProcess&&) = delete;

    // Writes all bytes to the child's stdin. False once the pipe is gone.
    bool write(QByteArrayView bytes);

    [[nodiscard]] bool isRunning() const;

    [[nodiscard]] qint64 pid() const;

    // Blocks up to `timeoutMs` (-1 = forever) and returns the exit code, or -1
    // on timeout.
    int waitForFinished(int timeoutMs = -1);

    // Asks the child to exit and waits briefly for it to do so.
    void terminate(int exitCode = 0);

    [[nodiscard]] QString errorString() const;

private:
    ChildProcess();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace md::pool::ipc
