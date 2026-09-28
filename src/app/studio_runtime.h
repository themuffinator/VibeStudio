#pragma once

// Application runtime services shared by the GUI entry point and the shell:
// high-DPI setup, runtime translation loading, layout direction, and session
// logging.
//
// These live in src/app rather than src/core because they install process-wide
// Qt application state (translators, message handlers, layout direction) which
// only an application entry point should own.

#include <QDateTime>
#include <QIcon>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QCoreApplication;

namespace vibestudio {

struct TranslationLoadResult {
	bool installed = false;
	QString requestedLocale;
	QString resolvedLocale;
	QString catalogPath;
	bool rightToLeft = false;
	QStringList searchedPaths;
	QStringList warnings;
};

// Applies the high-DPI and rendering attributes the studio's painted widgets
// depend on. Must be called before the QApplication is constructed where Qt
// requires it, and is safe to call once only.
void configureHighDpiBehavior();

// Resolves and installs the .qm catalog for `localeName`, replacing any
// previously installed VibeStudio translator. An empty or unknown locale falls
// back to the source language without reporting failure.
TranslationLoadResult installStudioTranslations(QCoreApplication& app, const QString& localeName);

// Applies the layout direction implied by a locale. Returns true when the
// direction changed.
bool applyLayoutDirectionForLocale(const QString& localeName);

// Directories searched for compiled translation catalogs, in order.
QStringList translationSearchPaths();

// The application/window icon, painted at several sizes rather than shipped as
// a binary asset so the repository stays free of opaque image blobs.
QIcon studioApplicationIcon();

// Installs a Qt message handler that mirrors warnings and above into a
// rotating session log under the user's application data directory, so a crash
// or a failed compile leaves a diagnostic trail. Returns the log file path.
QString installSessionLogging();
QString sessionLogFilePath();
QStringList recentSessionLogLines(int maxLines = 200);

// --- Crash capture -------------------------------------------------------
//
// The session log only survives an orderly shutdown: a hardware fault or an
// uncaught exception takes the process down with the tail of the log still in
// memory. Crash capture turns that tail into a file on disk, next to the
// session log, so the next launch can offer it to the user.
//
// What is installed is platform-specific and every OS call is guarded:
//  - Windows: `SetUnhandledExceptionFilter` for hardware faults plus a
//    `signal(SIGABRT)` handler, because `abort()` (which `qFatal` reaches)
//    never passes through the unhandled-exception filter. Stack frames come
//    from `RtlCaptureStackBackTrace`, which needs no debug-help library.
//  - POSIX: `sigaction` for SIGSEGV, SIGBUS, SIGFPE, SIGILL and SIGABRT, with
//    `SA_SIGINFO | SA_RESETHAND` so the default action still produces a core
//    dump after the report is written. Stack frames come from
//    `backtrace_symbols_fd` when <execinfo.h> exists.
//  - Anywhere else: only `std::set_terminate` is installed and no report is
//    written. Nothing fails; crash capture simply reports itself unavailable.
//
// On POSIX the report is written with async-signal-safe primitives only:
// everything that needs formatting (version, paths, the log tail) is rendered
// into fixed byte buffers while the process is healthy, and the handler does
// nothing but `open`, `write` and `close`. Allocating, locking a mutex or
// touching a QString from a signal handler can deadlock against the very state
// the crash interrupted, which would lose the report entirely.

struct CrashHandlerOptions {
	// Master opt-out. When false nothing is installed, no session marker is
	// written and no report can be produced; previously written reports are
	// still readable.
	bool enabled = true;
	// False installs no process-wide handlers and only does the bookkeeping
	// (previous-session detection, pruning). Used by tests.
	bool installOsHandlers = true;
	// Collect stack frames where the platform can do it cheaply and safely.
	bool captureBacktrace = true;
	// Defaults to the session log's directory, `<AppDataLocation>/logs`.
	QString directory;
	// Reports kept by the prune that runs at install time.
	int keepReports = 20;
	int maxReportAgeDays = 30;
};

struct CrashHandlerStatus {
	bool installed = false;
	bool osHandlersInstalled = false;
	bool backtraceAvailable = false;
	QString directory;
	// Where this session would write its report, reserved up front so the
	// handler never has to build a path.
	QString reportPath;
	QString markerPath;
	QString sessionId;
	QString platform;
};

// One crash report, either parsed from disk or built for formatting.
struct CrashReportInfo {
	// The record was read from disk and carried the expected banner.
	bool valid = false;
	int formatVersion = 0;
	// Empty when the previous session ended abruptly without leaving a report.
	QString path;
	QString version;
	QString qtVersion;
	QString platform;
	QString updateChannel;
	QString sessionId;
	qint64 processId = 0;
	QDateTime sessionStarted;
	QDateTime crashedAt;
	QString sessionLogPath;
	// Untranslated token: "signal", "windows-exception", "terminate",
	// "unclean-exit".
	QString reasonId;
	// Free text, e.g. "SIGSEGV" or an exception's what().
	QString reasonDetail;
	QString exceptionCode;
	QString faultAddress;
	QStringList backtrace;
	QStringList logLines;
};

// Installs crash capture and performs the previous-session check. Safe to call
// once; later calls return the existing status unchanged.
CrashHandlerStatus installCrashHandling(const CrashHandlerOptions& options = CrashHandlerOptions());
CrashHandlerStatus crashHandlerStatus();

// Runtime opt-out. Disabling also removes this session's marker, so the next
// launch will not report a crash for it. The installed handlers stay in place
// but become no-ops.
void setCrashHandlingEnabled(bool enabled);
bool crashHandlingEnabled();

// Call on an orderly shutdown. Also registered with `std::atexit`, so a normal
// return from main() clears the marker even if the shell forgets.
void markSessionEndedCleanly();

// True when a previous session left a session marker behind and the process
// that owned it is no longer running.
bool previousSessionCrashed();
// The report for that session. `path` is empty when the session died without
// managing to write one (a kill, or a power loss).
CrashReportInfo previousSessionCrashReport();

QString crashReportDirectory();
// Newest first.
QVector<CrashReportInfo> recentCrashReports(int maxReports = 20);
// Deletes reports beyond `keepMostRecent` or older than `maxAgeDays`
// (non-positive disables that half of the rule). Returns the number deleted.
int pruneCrashReports(int keepMostRecent = 20, int maxAgeDays = 30);

// Report text is a plain `key: value` header followed by `[backtrace]` and
// `[log]` sections. The keys and section markers are format tokens and are
// never translated, so a report stays readable and parseable whatever locale
// wrote it.
QByteArray formatCrashReportText(const CrashReportInfo& info);
CrashReportInfo parseCrashReportText(const QByteArray& text, const QString& path = QString());
CrashReportInfo readCrashReport(const QString& path);

// Human-facing summary for the "the studio closed unexpectedly" prompt.
QStringList crashReportSummaryLines(const CrashReportInfo& info);
QString crashReportSummaryText(const CrashReportInfo& info);
QJsonObject crashReportJson(const CrashReportInfo& info);

} // namespace vibestudio
