#include <md/pool/ipc/child_process.h>

#include <QByteArray>
#include <QLatin1Char>
#include <QString>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#ifdef Q_OS_WIN
#  include <windows.h>
#elif defined(Q_OS_UNIX)
#  include <cerrno>
#  include <csignal>
#  include <spawn.h>
#  include <sys/wait.h>
#  include <unistd.h>

extern char** environ;
#endif

namespace md::pool::ipc {
namespace {

#ifdef Q_OS_WIN

QString lastError(DWORD code)
{
    return QStringLiteral("Windows error %1").arg(static_cast<qulonglong>(code));
}

// MSVC's command-line rules: backslashes are literal unless they precede a
// quote, and a quote is escaped by doubling the backslashes before it.
QString quoteArgument(const QString& argument)
{
    const QChar quote = QChar(QLatin1Char('"'));
    const QChar backslash = QChar(QLatin1Char('\\'));

    if (!argument.isEmpty()
        && !argument.contains(QChar(QLatin1Char(' ')))
        && !argument.contains(QChar(QLatin1Char('\t')))
        && !argument.contains(quote)) {
        return argument;
    }

    QString quoted = quote;
    int backslashes = 0;

    for (const QChar character : argument) {
        if (character == backslash) {
            ++backslashes;
            continue;
        }
        if (character == quote) {
            quoted += QString(backslashes * 2 + 1, backslash);
            quoted += character;
            backslashes = 0;
            continue;
        }
        quoted += QString(backslashes, backslash);
        backslashes = 0;
        quoted += character;
    }

    quoted += QString(backslashes * 2, backslash);
    quoted += quote;
    return quoted;
}

QString buildCommandLine(const ChildProcess::Options& options)
{
    QString commandLine = quoteArgument(options.program);
    for (const QString& argument : options.arguments)
        commandLine += QChar(QLatin1Char(' ')) + quoteArgument(argument);
    return commandLine;
}

#endif

} // namespace

struct ChildProcess::Impl
{
#ifdef Q_OS_WIN
    HANDLE process = nullptr;
    HANDLE stdinWrite = nullptr;
    HANDLE stdoutRead = nullptr;
    qint64 pid = 0;
#else
    int pid = 0;
    int stdinWrite = -1;
    int stdoutRead = -1;
#endif
    std::thread reader;
    std::mutex writeMutex;
    OutputHandler output;
    QString error;
    std::atomic<bool> running{false};
    std::atomic<int> exitCode{-1};
};

ChildProcess::ChildProcess()
    : impl_(std::make_unique<Impl>())
{
}

ChildProcess::~ChildProcess()
{
    if (!impl_)
        return;

#ifdef Q_OS_WIN
    if (impl_->process != nullptr) {
        if (::WaitForSingleObject(impl_->process, 0) == WAIT_TIMEOUT) {
            ::TerminateProcess(impl_->process, 0);
            ::WaitForSingleObject(impl_->process, 2000);
        }

        DWORD code = 0;
        if (::GetExitCodeProcess(impl_->process, &code))
            impl_->exitCode.store(static_cast<int>(code), std::memory_order_relaxed);
    }

    if (impl_->reader.joinable()) {
        // ReadFile() is blocking; cancelling it is the documented way to make
        // the reader return without closing a handle it still uses.
        ::CancelSynchronousIo(impl_->reader.native_handle());
        impl_->reader.join();
    }

    if (impl_->stdinWrite != nullptr)
        ::CloseHandle(impl_->stdinWrite);
    if (impl_->stdoutRead != nullptr)
        ::CloseHandle(impl_->stdoutRead);
    if (impl_->process != nullptr)
        ::CloseHandle(impl_->process);
#else
    if (impl_->pid > 0) {
        if (impl_->running.load(std::memory_order_relaxed)) {
            ::kill(impl_->pid, SIGTERM);
            for (int attempt = 0; attempt < 100; ++attempt) {
                int status = 0;
                if (::waitpid(impl_->pid, &status, WNOHANG) == impl_->pid) {
                    impl_->exitCode.store(WIFEXITED(status) ? WEXITSTATUS(status) : -1,
                                          std::memory_order_relaxed);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
    }

    if (impl_->stdinWrite >= 0)
        ::close(impl_->stdinWrite);
    if (impl_->reader.joinable())
        impl_->reader.join();
    if (impl_->stdoutRead >= 0)
        ::close(impl_->stdoutRead);
#endif
}

std::unique_ptr<ChildProcess> ChildProcess::spawn(const Options& options,
                                                  OutputHandler onOutput,
                                                  QString* error)
{
    const auto fail = [error](const QString& message) -> std::unique_ptr<ChildProcess> {
        if (error != nullptr)
            *error = message;
        return nullptr;
    };

    if (options.program.isEmpty())
        return fail(QStringLiteral("ChildProcess: empty program path"));

#ifdef Q_OS_WIN
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    HANDLE inRead = nullptr;
    HANDLE inWrite = nullptr;
    HANDLE outRead = nullptr;
    HANDLE outWrite = nullptr;

    const auto closeAll = [&] {
        if (inRead != nullptr)
            ::CloseHandle(inRead);
        if (inWrite != nullptr)
            ::CloseHandle(inWrite);
        if (outRead != nullptr)
            ::CloseHandle(outRead);
        if (outWrite != nullptr)
            ::CloseHandle(outWrite);
        inRead = inWrite = outRead = outWrite = nullptr;
    };

    if (!::CreatePipe(&inRead, &inWrite, &attributes, 0)) {
        closeAll();
        return fail(QStringLiteral("CreatePipe(stdin) failed: %1").arg(lastError(::GetLastError())));
    }
    if (!::SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0)) {
        closeAll();
        return fail(QStringLiteral("SetHandleInformation failed: %1").arg(lastError(::GetLastError())));
    }
    if (!::CreatePipe(&outRead, &outWrite, &attributes, 0)) {
        closeAll();
        return fail(QStringLiteral("CreatePipe(stdout) failed: %1").arg(lastError(::GetLastError())));
    }
    if (!::SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0)) {
        closeAll();
        return fail(QStringLiteral("SetHandleInformation failed: %1").arg(lastError(::GetLastError())));
    }

    SIZE_T attributeSize = 0;
    ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);

    std::vector<unsigned char> attributeBuffer(attributeSize + 16);
    auto* attributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeBuffer.data());

    if (!::InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeSize)) {
        closeAll();
        return fail(QStringLiteral("InitializeProcThreadAttributeList failed: %1")
                        .arg(lastError(::GetLastError())));
    }

    const HANDLE inherited[] = {inRead, outWrite};
    const bool attributeOk = ::UpdateProcThreadAttribute(attributeList, 0,
                                                         PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                                         const_cast<HANDLE*>(inherited),
                                                         sizeof(inherited), nullptr, nullptr) != FALSE;

    if (!attributeOk) {
        const DWORD code = ::GetLastError();
        ::DeleteProcThreadAttributeList(attributeList);
        closeAll();
        return fail(QStringLiteral("UpdateProcThreadAttribute failed: %1").arg(lastError(code)));
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = inRead;
    startup.StartupInfo.hStdOutput = outWrite;
    startup.StartupInfo.hStdError = outWrite;
    startup.lpAttributeList = attributeList;

    std::wstring commandLine = buildCommandLine(options).toStdWString();
    const std::wstring workingDirectory = options.workingDirectory.toStdWString();

    PROCESS_INFORMATION processInfo{};
    const DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW;

    const BOOL created = ::CreateProcessW(
        nullptr, commandLine.data(), nullptr, nullptr, TRUE, flags, nullptr,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
        &startup.StartupInfo, &processInfo);

    ::DeleteProcThreadAttributeList(attributeList);

    if (!created) {
        const DWORD code = ::GetLastError();
        closeAll();
        return fail(QStringLiteral("CreateProcessW failed: %1").arg(lastError(code)));
    }

    ::CloseHandle(processInfo.hThread);

    // The child owns the other ends now; the parent keeps only its two.
    ::CloseHandle(inRead);
    ::CloseHandle(outWrite);
    inRead = nullptr;
    outWrite = nullptr;

    auto process = std::unique_ptr<ChildProcess>(new ChildProcess());
    process->impl_->process = processInfo.hProcess;
    process->impl_->stdinWrite = inWrite;
    process->impl_->stdoutRead = outRead;
    process->impl_->pid = static_cast<qint64>(processInfo.dwProcessId);
    process->impl_->output = std::move(onOutput);
    process->impl_->running.store(true, std::memory_order_relaxed);

    Impl* impl = process->impl_.get();
    impl->reader = std::thread([impl] {
        std::vector<char> buffer(4096);
        for (;;) {
            DWORD read = 0;
            const BOOL ok = ::ReadFile(impl->stdoutRead, buffer.data(),
                                       static_cast<DWORD>(buffer.size()), &read, nullptr);
            if (ok == FALSE || read == 0)
                break;
            if (impl->output)
                impl->output(QByteArrayView(buffer.data(), static_cast<qsizetype>(read)));
        }
    });

    return process;
#else
    int inPipe[2] = {-1, -1};
    int outPipe[2] = {-1, -1};

    if (::pipe(inPipe) != 0)
        return fail(QStringLiteral("pipe(stdin) failed: %1")
                        .arg(QString::fromLocal8Bit(std::strerror(errno))));

    if (::pipe(outPipe) != 0) {
        ::close(inPipe[0]);
        ::close(inPipe[1]);
        return fail(QStringLiteral("pipe(stdout) failed: %1")
                        .arg(QString::fromLocal8Bit(std::strerror(errno))));
    }

    posix_spawn_file_actions_t actions;
    ::posix_spawn_file_actions_init(&actions);
    ::posix_spawn_file_actions_adddup2(&actions, inPipe[0], STDIN_FILENO);
    ::posix_spawn_file_actions_adddup2(&actions, outPipe[1], STDOUT_FILENO);
    ::posix_spawn_file_actions_adddup2(&actions, outPipe[1], STDERR_FILENO);
    ::posix_spawn_file_actions_addclose(&actions, inPipe[1]);
    ::posix_spawn_file_actions_addclose(&actions, outPipe[0]);

    const QByteArray program = options.program.toLocal8Bit();
    std::vector<QByteArray> arguments;
    arguments.push_back(program);
    for (const QString& argument : options.arguments)
        arguments.push_back(argument.toLocal8Bit());

    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (QByteArray& argument : arguments)
        argv.push_back(argument.data());
    argv.push_back(nullptr);

    pid_t child = 0;
    const int spawnResult =
        ::posix_spawn(&child, program.constData(), &actions, nullptr, argv.data(), environ);

    ::posix_spawn_file_actions_destroy(&actions);
    ::close(inPipe[0]);
    ::close(outPipe[1]);

    if (spawnResult != 0) {
        ::close(inPipe[1]);
        ::close(outPipe[0]);
        return fail(QStringLiteral("posix_spawn failed: %1")
                        .arg(QString::fromLocal8Bit(std::strerror(spawnResult))));
    }

    auto process = std::unique_ptr<ChildProcess>(new ChildProcess());
    process->impl_->pid = child;
    process->impl_->stdinWrite = inPipe[1];
    process->impl_->stdoutRead = outPipe[0];
    process->impl_->output = std::move(onOutput);
    process->impl_->running.store(true, std::memory_order_relaxed);

    Impl* impl = process->impl_.get();
    impl->reader = std::thread([impl] {
        std::vector<char> buffer(4096);
        for (;;) {
            const ssize_t read = ::read(impl->stdoutRead, buffer.data(), buffer.size());
            if (read <= 0)
                break;
            if (impl->output)
                impl->output(QByteArrayView(buffer.data(), static_cast<qsizetype>(read)));
        }
    });

    return process;
#endif
}

bool ChildProcess::write(QByteArrayView bytes)
{
    if (!impl_)
        return false;
    if (bytes.isEmpty())
        return true;

    std::lock_guard<std::mutex> lock(impl_->writeMutex);

#ifdef Q_OS_WIN
    if (impl_->stdinWrite == nullptr)
        return false;

    const char* data = bytes.data();
    std::size_t remaining = bytes.size();
    while (remaining > 0) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, 0x7FFFFFFFu));
        DWORD written = 0;
        if (::WriteFile(impl_->stdinWrite, data, chunk, &written, nullptr) == FALSE || written == 0)
            return false;
        data += written;
        remaining -= written;
    }
    return true;
#else
    if (impl_->stdinWrite < 0)
        return false;

    const char* data = bytes.data();
    std::size_t remaining = bytes.size();
    while (remaining > 0) {
        const ssize_t written = ::write(impl_->stdinWrite, data, remaining);
        if (written <= 0)
            return false;
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }
    return true;
#endif
}

bool ChildProcess::isRunning() const
{
    if (!impl_)
        return false;

#ifdef Q_OS_WIN
    if (impl_->process == nullptr)
        return false;

    if (::WaitForSingleObject(impl_->process, 0) == WAIT_TIMEOUT)
        return true;

    DWORD code = 0;
    if (::GetExitCodeProcess(impl_->process, &code))
        impl_->exitCode.store(static_cast<int>(code), std::memory_order_relaxed);
    impl_->running.store(false, std::memory_order_relaxed);
    return false;
#else
    if (impl_->pid <= 0)
        return false;

    int status = 0;
    const pid_t result = ::waitpid(impl_->pid, &status, WNOHANG);
    if (result == impl_->pid) {
        impl_->exitCode.store(WIFEXITED(status) ? WEXITSTATUS(status) : -1,
                              std::memory_order_relaxed);
        impl_->running.store(false, std::memory_order_relaxed);
        return false;
    }
    return true;
#endif
}

qint64 ChildProcess::pid() const
{
    return impl_ ? static_cast<qint64>(impl_->pid) : 0;
}

int ChildProcess::waitForFinished(int timeoutMs)
{
    if (!impl_)
        return -1;

#ifdef Q_OS_WIN
    if (impl_->process == nullptr)
        return -1;

    const DWORD timeout = timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs);
    if (::WaitForSingleObject(impl_->process, timeout) != WAIT_OBJECT_0)
        return -1;

    DWORD code = 0;
    if (!::GetExitCodeProcess(impl_->process, &code))
        return -1;

    impl_->exitCode.store(static_cast<int>(code), std::memory_order_relaxed);
    impl_->running.store(false, std::memory_order_relaxed);
    return impl_->exitCode.load(std::memory_order_relaxed);
#else
    if (impl_->pid <= 0)
        return -1;

    const auto deadline = timeoutMs < 0
        ? std::chrono::steady_clock::time_point::max()
        : std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    for (;;) {
        int status = 0;
        const pid_t result = ::waitpid(impl_->pid, &status, WNOHANG);
        if (result == impl_->pid) {
            const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            impl_->exitCode.store(code, std::memory_order_relaxed);
            impl_->running.store(false, std::memory_order_relaxed);
            return code;
        }
        if (result < 0)
            return -1;
        if (std::chrono::steady_clock::now() >= deadline)
            return -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
#endif
}

void ChildProcess::terminate(int exitCode)
{
    if (!impl_)
        return;

#ifdef Q_OS_WIN
    if (impl_->process == nullptr)
        return;

    if (::WaitForSingleObject(impl_->process, 0) == WAIT_TIMEOUT) {
        ::TerminateProcess(impl_->process, static_cast<UINT>(exitCode));
        ::WaitForSingleObject(impl_->process, 2000);
    }

    DWORD code = 0;
    if (::GetExitCodeProcess(impl_->process, &code))
        impl_->exitCode.store(static_cast<int>(code), std::memory_order_relaxed);
    impl_->running.store(false, std::memory_order_relaxed);
#else
    Q_UNUSED(exitCode);
    if (impl_->pid <= 0)
        return;

    ::kill(impl_->pid, SIGTERM);
    impl_->running.store(false, std::memory_order_relaxed);
#endif
}

QString ChildProcess::errorString() const
{
    return impl_ ? impl_->error : QString();
}

} // namespace md::pool::ipc
