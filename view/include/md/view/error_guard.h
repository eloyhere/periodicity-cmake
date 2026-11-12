// Periodicity — the exception boundary of the component kernel.
//
// Qt does not expect a virtual hook to throw. An exception leaving paintEvent()
// unwinds through Qt's own frames, leaves the widget half painted and, on some
// platforms, terminates the process when it reaches foreign code. View therefore
// runs its four entry points — paintEvent(), measure(), arrange() and event() —
// through guard(): one component exception is caught here, reported once and
// turned into a false return, so the widget stays paintable and the event loop
// keeps running.
//
// The happy path allocates nothing: guard() is a try block around a callable
// invocation. The diagnostic QString and the qWarning are built only on the
// failure path. Both overloads are noexcept: a second exception escaping a
// catch handler would terminate the process, which is exactly what a guard must
// not allow.

#pragma once

#include <QString>
#include <QtGlobal>

#include <exception>
#include <functional>
#include <utility>

namespace md::view {

// The diagnostic state of one guarded object (View).
//
// `reportedSites` is a caller-owned bitmask: every entry point claims one bit,
// and a failure is logged only when its bit was still clear. A paint loop that
// throws on every frame therefore warns once instead of flooding the log, while
// a failure at a different entry point still reports. `lastError` is refreshed
// on every failure, including the suppressed ones, so it always describes the
// most recent exception.
struct GuardReport
{
    quint32 reportedSites = 0;
    QString lastError;
};

namespace detail {

[[nodiscard]] inline const char* guardWhere(const char* where) noexcept
{
    return where != nullptr ? where : "unknown entry point";
}

// Builds the diagnostic and reports the failure. This is the only allocating
// path in this header; it runs only after an exception has been caught.
//
// `report == nullptr` means "no bitmask" (the two-argument guard): every
// failure is logged. A zero `site` means the same, because it cannot claim a
// bit of its own.
inline void reportFailure(const char* where, quint32 site, GuardReport* report,
                          const char* what) noexcept
{
    QString text = QStringLiteral("md::view: ");
    text += QString::fromUtf8(guardWhere(where));
    text += QStringLiteral(" failed: ");
    text += what != nullptr ? QString::fromUtf8(what)
                            : QStringLiteral("unknown exception");

    if (report != nullptr)
        report->lastError = text;

    if (report == nullptr || site == 0) {
        qWarning("%s", qUtf8Printable(text));
        return;
    }

    if ((report->reportedSites & site) != 0)
        return;

    report->reportedSites |= site;
    qWarning("%s", qUtf8Printable(text));
}

template <class Fn>
bool run(const char* where, quint32 site, GuardReport* report, Fn&& fn) noexcept
{
    try {
        static_cast<void>(std::invoke(std::forward<Fn>(fn)));
        return true;
    } catch (const std::exception& error) {
        reportFailure(where, site, report, error.what());
        return false;
    } catch (...) {
        reportFailure(where, site, report, nullptr);
        return false;
    }
}

} // namespace detail

// Runs `fn` once. Returns true when it completed; on any exception it logs a
// qWarning naming `where` and returns false. It never rethrows, and it
// allocates only when the call fails.
template <class Fn>
[[nodiscard]] bool guard(const char* where, Fn&& fn) noexcept
{
    return detail::run(where, 0, nullptr, std::forward<Fn>(fn));
}

// The once-per-site form used by View. `report` carries the bitmask and the
// last diagnostic; `site` is one bit of it (the caller's error-site enum).
// Every failure updates report.lastError, but only the first failure at a given
// site is logged.
template <class Fn>
[[nodiscard]] bool guard(const char* where, quint32 site, GuardReport& report,
                         Fn&& fn) noexcept
{
    return detail::run(where, site, &report, std::forward<Fn>(fn));
}

} // namespace md::view
