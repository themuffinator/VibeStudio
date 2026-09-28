#include "app/studio_runtime.h"

#include "core/localization.h"
#include "core/studio_manifest.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QLinearGradient>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QStandardPaths>
#include <QTextStream>
#include <QTranslator>

#include <QJsonArray>
#include <QSysInfo>
#include <QTimeZone>
#include <QUuid>

// Platform headers for crash capture. Both branches are optional: with neither
// defined the studio installs only std::set_terminate and writes no report.
#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(Q_OS_UNIX)
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define VIBESTUDIO_HAVE_EXECINFO 1
#endif
#endif

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <utility>

namespace vibestudio {

namespace {

QString runtimeText(const char* source)
{
	return QCoreApplication::translate("VibeStudioRuntime", source);
}

QTranslator*& studioTranslator()
{
	static QTranslator* translator = nullptr;
	return translator;
}

QTranslator*& qtBaseTranslator()
{
	static QTranslator* translator = nullptr;
	return translator;
}

struct SessionLogState {
	QMutex mutex;
	QString path;
	QStringList recent;
	QtMessageHandler previousHandler = nullptr;
	bool installed = false;
	int maxRecent = 400;
};

SessionLogState& sessionLogState()
{
	static SessionLogState state;
	return state;
}

QString messageTypeToken(QtMsgType type)
{
	switch (type) {
	case QtDebugMsg:
		return QStringLiteral("debug");
	case QtInfoMsg:
		return QStringLiteral("info");
	case QtWarningMsg:
		return QStringLiteral("warning");
	case QtCriticalMsg:
		return QStringLiteral("critical");
	case QtFatalMsg:
		return QStringLiteral("fatal");
	}
	return QStringLiteral("message");
}

// Slots are fixed so the crash handler can read the log tail without touching
// the heap, a mutex or a QString. `SessionLogState::recent` stays the source of
// truth for the in-process log viewer; this is a byte-for-byte mirror that a
// signal handler is allowed to look at.
constexpr int kCrashLogSlots = 64;
constexpr int kCrashLogSlotBytes = 512;

struct CrashLogRing {
	char lines[kCrashLogSlots][kCrashLogSlotBytes];
	// Total lines ever appended. The reader derives the live window from it, so
	// no index needs to be kept consistent with anything else.
	std::atomic<unsigned> written;
};

// Namespace scope and constant-initialized: a function-local static would have
// a guard variable, and a crash before the first call would read it unguarded.
CrashLogRing g_crashLogRing{};

// Called from the message handler, which already serializes on the session log
// mutex, so the slot is filled before `written` publishes it.
void appendCrashLogLine(const QString& line)
{
	const QByteArray utf8 = line.toUtf8();
	const unsigned index = g_crashLogRing.written.load(std::memory_order_relaxed);
	char* slot = g_crashLogRing.lines[index % kCrashLogSlots];
	const int copied = static_cast<int>(std::min<qsizetype>(utf8.size(), kCrashLogSlotBytes - 2));
	std::memcpy(slot, utf8.constData(), static_cast<size_t>(copied));
	slot[copied] = '\n';
	slot[copied + 1] = '\0';
	g_crashLogRing.written.store(index + 1, std::memory_order_release);
}

void sessionMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
	SessionLogState& state = sessionLogState();

	const QString line = QStringLiteral("%1 [%2] %3")
		.arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate), messageTypeToken(type), message);

	{
		const QMutexLocker locker(&state.mutex);
		appendCrashLogLine(line);
		state.recent.push_back(line);
		while (state.recent.size() > state.maxRecent) {
			state.recent.removeFirst();
		}
		if (!state.path.isEmpty() && type != QtDebugMsg) {
			QFile file(state.path);
			if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
				QTextStream stream(&file);
				stream.setEncoding(QStringConverter::Utf8);
				stream << line << '\n';
			}
		}
	}

	if (state.previousHandler) {
		state.previousHandler(type, context, message);
	} else if (type != QtDebugMsg) {
		std::fputs(qPrintable(line + QLatin1Char('\n')), stderr);
	}
}

void appendUniqueExistingDirectory(QStringList* paths, const QString& candidate)
{
	if (!paths || candidate.isEmpty()) {
		return;
	}
	const QString normalized = QDir::cleanPath(candidate);
	if (normalized.isEmpty() || paths->contains(normalized)) {
		return;
	}
	paths->push_back(normalized);
}

QStringList qmCandidateFileNames(const QString& localeName)
{
	QStringList names;
	const QString normalized = normalizedLocalizationTargetId(localeName);
	const QString requested = localeName.trimmed();

	auto addName = [&names](const QString& locale) {
		if (locale.isEmpty()) {
			return;
		}
		const QString fileName = QStringLiteral("vibestudio_%1.qm").arg(locale);
		if (!names.contains(fileName)) {
			names.push_back(fileName);
		}
	};

	addName(requested);
	addName(normalized);

	// A regional locale falls back to its base language: pt_BR -> pt.
	for (const QString& locale : {requested, normalized}) {
		const qsizetype separator = locale.indexOf(QLatin1Char('_'));
		if (separator > 0) {
			addName(locale.left(separator));
		}
	}

	return names;
}

} // namespace

void configureHighDpiBehavior()
{
	// Qt 6 always enables high-DPI scaling; what still matters for painted
	// surfaces is the rounding policy. PassThrough keeps fractional scale
	// factors (150%, 175%) exact so QPainter geometry, hit-testing, and
	// nearest-neighbour texture previews stay aligned with what is drawn.
	QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
}

QStringList translationSearchPaths()
{
	QStringList paths;

	const QByteArray override = qgetenv("VIBESTUDIO_I18N_DIR");
	if (!override.isEmpty()) {
		appendUniqueExistingDirectory(&paths, QString::fromLocal8Bit(override));
	}

	const QString applicationDir = QCoreApplication::applicationDirPath();
	if (!applicationDir.isEmpty()) {
		appendUniqueExistingDirectory(&paths, applicationDir + QStringLiteral("/i18n"));
		// Development layout: builddir/src/vibestudio -> builddir/i18n.
		appendUniqueExistingDirectory(&paths, applicationDir + QStringLiteral("/../i18n"));
		// Installed layout: prefix/bin/vibestudio -> prefix/share/vibestudio/i18n.
		appendUniqueExistingDirectory(&paths, applicationDir + QStringLiteral("/../share/vibestudio/i18n"));
		// Portable package layout.
		appendUniqueExistingDirectory(&paths, applicationDir + QStringLiteral("/../../i18n"));
	}

	appendUniqueExistingDirectory(&paths, QDir::currentPath() + QStringLiteral("/i18n"));

	return paths;
}

TranslationLoadResult installStudioTranslations(QCoreApplication& app, const QString& localeName)
{
	TranslationLoadResult result;
	result.requestedLocale = localeName.trimmed();
	result.resolvedLocale = normalizedLocalizationTargetId(result.requestedLocale);
	result.rightToLeft = isRightToLeftLocale(result.resolvedLocale);
	result.searchedPaths = translationSearchPaths();

	QTranslator*& translator = studioTranslator();
	if (translator) {
		app.removeTranslator(translator);
		delete translator;
		translator = nullptr;
	}

	QTranslator*& baseTranslator = qtBaseTranslator();
	if (baseTranslator) {
		app.removeTranslator(baseTranslator);
		delete baseTranslator;
		baseTranslator = nullptr;
	}

	// English is the source language, but it still loads its catalog: that
	// catalog holds only plural forms (generated by scripts/english_plurals.py),
	// which is how Qt turns "%n item(s)" into "1 item" and "5 items".
	const bool sourceLanguage = result.resolvedLocale.isEmpty() || result.resolvedLocale == QStringLiteral("en");

	{
		const QStringList fileNames = qmCandidateFileNames(sourceLanguage ? QStringLiteral("en") : result.requestedLocale);
		auto* candidate = new QTranslator(&app);
		bool loaded = false;
		for (const QString& directory : std::as_const(result.searchedPaths)) {
			for (const QString& fileName : std::as_const(fileNames)) {
				const QString path = QDir(directory).filePath(fileName);
				if (!QFileInfo::exists(path)) {
					continue;
				}
				if (candidate->load(path)) {
					result.catalogPath = QDir::cleanPath(path);
					loaded = true;
					break;
				}
				result.warnings.push_back(
					runtimeText("Translation catalog could not be loaded: %1").arg(QDir::cleanPath(path)));
			}
			if (loaded) {
				break;
			}
		}

		if (loaded && app.installTranslator(candidate)) {
			translator = candidate;
			result.installed = true;
		} else {
			delete candidate;
			if (!loaded && !sourceLanguage) {
				result.warnings.push_back(
					runtimeText("No compiled translation catalog (.qm) was found for %1; using the source language.")
						.arg(result.resolvedLocale));
			}
		}
	}

	if (!sourceLanguage) {
		// Qt's own dialogs and standard buttons have their own catalogs.
		auto* qtCandidate = new QTranslator(&app);
		const QString qtCatalogDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
		if (!qtCatalogDir.isEmpty()
			&& qtCandidate->load(QLocale(result.resolvedLocale), QStringLiteral("qtbase"), QStringLiteral("_"), qtCatalogDir)
			&& app.installTranslator(qtCandidate)) {
			baseTranslator = qtCandidate;
		} else {
			delete qtCandidate;
		}
	}

	return result;
}

bool applyLayoutDirectionForLocale(const QString& localeName)
{
	const bool rightToLeft = isRightToLeftLocale(normalizedLocalizationTargetId(localeName));
	const Qt::LayoutDirection desired = rightToLeft ? Qt::RightToLeft : Qt::LeftToRight;
	if (QGuiApplication::layoutDirection() == desired) {
		return false;
	}
	QGuiApplication::setLayoutDirection(desired);
	return true;
}

QIcon studioApplicationIcon()
{
	static QIcon icon = []() {
		QIcon built;
		for (const int size : {16, 24, 32, 48, 64, 128, 256}) {
			QPixmap pixmap(size, size);
			pixmap.fill(Qt::transparent);

			QPainter painter(&pixmap);
			painter.setRenderHint(QPainter::Antialiasing, true);

			const qreal inset = size * 0.06;
			const QRectF plate(inset, inset, size - inset * 2.0, size - inset * 2.0);
			const qreal radius = size * 0.22;

			QLinearGradient gradient(plate.topLeft(), plate.bottomRight());
			gradient.setColorAt(0.0, QColor(0x27, 0x5d, 0x86));
			gradient.setColorAt(1.0, QColor(0x14, 0x1a, 0x21));
			painter.setPen(Qt::NoPen);
			painter.setBrush(gradient);
			painter.drawRoundedRect(plate, radius, radius);

			// A chevron reading as both a "V" and a build-stage arrow.
			QPen stroke(QColor(0xe8, 0xed, 0xf2));
			stroke.setWidthF(std::max(1.0, size * 0.11));
			stroke.setCapStyle(Qt::RoundCap);
			stroke.setJoinStyle(Qt::MiterJoin);
			painter.setPen(stroke);
			painter.setBrush(Qt::NoBrush);

			QPolygonF chevron;
			chevron << QPointF(plate.left() + plate.width() * 0.26, plate.top() + plate.height() * 0.30)
				<< QPointF(plate.center().x(), plate.top() + plate.height() * 0.72)
				<< QPointF(plate.left() + plate.width() * 0.74, plate.top() + plate.height() * 0.30);
			painter.drawPolyline(chevron);

			painter.end();
			built.addPixmap(pixmap);
		}
		return built;
	}();
	return icon;
}

QString installSessionLogging()
{
	SessionLogState& state = sessionLogState();
	{
		const QMutexLocker locker(&state.mutex);
		if (state.installed) {
			return state.path;
		}

		const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
		if (!root.isEmpty()) {
			QDir directory(root);
			if (directory.mkpath(QStringLiteral("logs"))) {
				const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd"));
				state.path = directory.filePath(QStringLiteral("logs/vibestudio-%1.log").arg(stamp));
			}
		}
		state.installed = true;
	}

	state.previousHandler = qInstallMessageHandler(sessionMessageHandler);
	return state.path;
}

QString sessionLogFilePath()
{
	SessionLogState& state = sessionLogState();
	const QMutexLocker locker(&state.mutex);
	return state.path;
}

QStringList recentSessionLogLines(int maxLines)
{
	SessionLogState& state = sessionLogState();
	const QMutexLocker locker(&state.mutex);
	if (maxLines <= 0 || state.recent.size() <= maxLines) {
		return state.recent;
	}
	return state.recent.mid(state.recent.size() - maxLines);
}

// ---------------------------------------------------------------------------
// Crash capture
// ---------------------------------------------------------------------------

namespace {

constexpr int kCrashPathChars = 1024;
constexpr int kCrashPrefixBytes = 8192;
constexpr int kCrashDetailBytes = 384;
constexpr int kCrashBacktraceFrames = 62;
constexpr qint64 kCrashReportReadLimit = 1LL * 1024LL * 1024LL;
constexpr int kCrashReportLineLimit = 20000;

const char* const kCrashReportBanner = "VibeStudio crash report";
const char* const kSessionMarkerBanner = "VibeStudio session marker";
constexpr int kCrashReportFormatVersion = 1;

// Everything the handler needs is rendered here while the process is healthy.
// A signal handler may not allocate, lock, or call into Qt: the crash it is
// reporting may have happened inside the allocator or while the very mutex it
// would need was held, and either would hang the process instead of producing
// a report.
char g_crashPrefix[kCrashPrefixBytes] = {};
int g_crashPrefixLength = 0;
char g_crashReportPathUtf8[kCrashPathChars] = {};
char g_sessionMarkerPathUtf8[kCrashPathChars] = {};
char g_terminateDetail[kCrashDetailBytes] = {};
#if defined(Q_OS_WIN)
wchar_t g_crashReportPathWide[kCrashPathChars] = {};
wchar_t g_sessionMarkerPathWide[kCrashPathChars] = {};
#endif

std::atomic<bool> g_crashHandlingEnabled{false};
std::atomic<bool> g_captureBacktrace{true};
// Set once, never cleared: a second fault while a report is being written must
// not recurse into the handler.
std::atomic_flag g_crashInProgress;

bool tryEnterCrashHandling() noexcept
{
	if (!g_crashHandlingEnabled.load(std::memory_order_acquire)) {
		return false;
	}
	return !g_crashInProgress.test_and_set(std::memory_order_acq_rel);
}

// --- byte buffer helpers, all async-signal-safe ---

int safeStringLength(const char* text, int limit) noexcept
{
	if (!text) {
		return 0;
	}
	int length = 0;
	while (length < limit && text[length] != '\0') {
		++length;
	}
	return length;
}

int safeAppendBytes(char* buffer, int capacity, int offset, const char* data, int length) noexcept
{
	if (!buffer || capacity <= 1) {
		return 0;
	}
	int written = offset < 0 ? 0 : offset;
	if (written >= capacity - 1) {
		return capacity - 1;
	}
	for (int i = 0; i < length && written < capacity - 1; ++i) {
		const char value = data ? data[i] : ' ';
		// Newlines and carriage returns would break the one-field-per-line
		// layout the parser relies on.
		buffer[written++] = (value == '\n' || value == '\r') ? ' ' : value;
	}
	buffer[written] = '\0';
	return written;
}

int safeAppend(char* buffer, int capacity, int offset, const char* text) noexcept
{
	return safeAppendBytes(buffer, capacity, offset, text, safeStringLength(text, capacity));
}

int safeAppendNewline(char* buffer, int capacity, int offset) noexcept
{
	if (!buffer || capacity <= 1) {
		return 0;
	}
	int written = offset < 0 ? 0 : offset;
	if (written >= capacity - 1) {
		return capacity - 1;
	}
	buffer[written++] = '\n';
	buffer[written] = '\0';
	return written;
}

int safeAppendUnsigned(char* buffer, int capacity, int offset, unsigned long long value) noexcept
{
	char digits[24] = {};
	int count = 0;
	do {
		digits[count++] = static_cast<char>('0' + static_cast<int>(value % 10ULL));
		value /= 10ULL;
	} while (value != 0ULL && count < 24);

	char ordered[24] = {};
	for (int i = 0; i < count; ++i) {
		ordered[i] = digits[count - 1 - i];
	}
	return safeAppendBytes(buffer, capacity, offset, ordered, count);
}

int safeAppendHex(char* buffer, int capacity, int offset, unsigned long long value) noexcept
{
	static const char kDigits[] = "0123456789abcdef";
	char ordered[19] = {};
	int count = 0;
	ordered[count++] = '0';
	ordered[count++] = 'x';
	bool started = false;
	for (int shift = 60; shift >= 0; shift -= 4) {
		const int nibble = static_cast<int>((value >> shift) & 0xFULL);
		if (nibble != 0 || started || shift == 0) {
			started = true;
			ordered[count++] = kDigits[nibble];
		}
	}
	return safeAppendBytes(buffer, capacity, offset, ordered, count);
}

// --- low level file output ---

#if defined(Q_OS_WIN)

using CrashFile = HANDLE;

CrashFile openCrashReportFile() noexcept
{
	if (g_crashReportPathWide[0] == L'\0') {
		return INVALID_HANDLE_VALUE;
	}
	return ::CreateFileW(g_crashReportPathWide, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

bool crashFileValid(CrashFile file) noexcept
{
	return file != INVALID_HANDLE_VALUE && file != nullptr;
}

void crashWriteBytes(CrashFile file, const char* data, int length) noexcept
{
	if (!crashFileValid(file) || !data || length <= 0) {
		return;
	}
	DWORD written = 0;
	::WriteFile(file, data, static_cast<DWORD>(length), &written, nullptr);
}

void closeCrashFile(CrashFile file) noexcept
{
	if (crashFileValid(file)) {
		::CloseHandle(file);
	}
}

#elif defined(Q_OS_UNIX)

using CrashFile = int;

CrashFile openCrashReportFile() noexcept
{
	if (g_crashReportPathUtf8[0] == '\0') {
		return -1;
	}
	return ::open(g_crashReportPathUtf8, O_WRONLY | O_CREAT | O_TRUNC, 0600);
}

bool crashFileValid(CrashFile file) noexcept
{
	return file >= 0;
}

void crashWriteBytes(CrashFile file, const char* data, int length) noexcept
{
	if (!crashFileValid(file) || !data || length <= 0) {
		return;
	}
	int offset = 0;
	while (offset < length) {
		const ssize_t written = ::write(file, data + offset, static_cast<size_t>(length - offset));
		if (written <= 0) {
			if (written < 0 && errno == EINTR) {
				continue;
			}
			return;
		}
		offset += static_cast<int>(written);
	}
}

void closeCrashFile(CrashFile file) noexcept
{
	if (crashFileValid(file)) {
		::close(file);
	}
}

#else

// Portable fallback: no report is written. Every caller checks
// `crashFileValid`, so the whole crash path becomes a no-op rather than an
// error.
using CrashFile = int;

CrashFile openCrashReportFile() noexcept
{
	return -1;
}

bool crashFileValid(CrashFile file) noexcept
{
	return file >= 0;
}

void crashWriteBytes(CrashFile, const char*, int) noexcept
{
}

void closeCrashFile(CrashFile) noexcept
{
}

#endif

void crashWriteText(CrashFile file, const char* text) noexcept
{
	crashWriteBytes(file, text, safeStringLength(text, kCrashPrefixBytes));
}

void writeCrashBacktrace(CrashFile file) noexcept
{
	if (!g_captureBacktrace.load(std::memory_order_relaxed)) {
		crashWriteText(file, "disabled\n");
		return;
	}

#if defined(Q_OS_WIN)
	// RtlCaptureStackBackTrace needs no symbol handler and no extra library, so
	// it is safe to call from an exception filter. Raw addresses only; they are
	// resolved offline against the matching build.
	void* frames[kCrashBacktraceFrames] = {};
	const USHORT count = ::RtlCaptureStackBackTrace(0, kCrashBacktraceFrames, frames, nullptr);
	char line[64] = {};
	for (USHORT index = 0; index < count; ++index) {
		int offset = safeAppendHex(line, sizeof(line), 0, reinterpret_cast<unsigned long long>(frames[index]));
		offset = safeAppendNewline(line, sizeof(line), offset);
		crashWriteBytes(file, line, offset);
	}
	if (count == 0) {
		crashWriteText(file, "unavailable\n");
	}
#elif defined(VIBESTUDIO_HAVE_EXECINFO)
	// backtrace_symbols_fd writes straight to the descriptor and, unlike
	// backtrace_symbols, never allocates.
	void* frames[kCrashBacktraceFrames] = {};
	const int count = ::backtrace(frames, kCrashBacktraceFrames);
	if (count > 0) {
		::backtrace_symbols_fd(frames, count, file);
	} else {
		crashWriteText(file, "unavailable\n");
	}
#else
	crashWriteText(file, "unavailable on this platform\n");
#endif
}

void writeCrashLogTail(CrashFile file) noexcept
{
	const unsigned written = g_crashLogRing.written.load(std::memory_order_acquire);
	if (written == 0) {
		return;
	}
	const unsigned available = written < static_cast<unsigned>(kCrashLogSlots)
		? written
		: static_cast<unsigned>(kCrashLogSlots);
	for (unsigned index = written - available; index < written; ++index) {
		const char* slot = g_crashLogRing.lines[index % kCrashLogSlots];
		const int length = safeStringLength(slot, kCrashLogSlotBytes);
		crashWriteBytes(file, slot, length);
	}
}

struct CrashDetails {
	const char* reasonId = "unknown";
	const char* reasonDetail = "";
	bool hasCode = false;
	unsigned long long code = 0;
	bool hasAddress = false;
	unsigned long long address = 0;
};

// Must be callable from a signal handler: no allocation, no locking, no Qt.
void writeCrashReport(const CrashDetails& details) noexcept
{
	const CrashFile file = openCrashReportFile();
	if (!crashFileValid(file)) {
		return;
	}

	crashWriteBytes(file, g_crashPrefix, g_crashPrefixLength);

	char scratch[kCrashDetailBytes + 256] = {};
	int offset = safeAppend(scratch, sizeof(scratch), 0, "reasonId: ");
	offset = safeAppend(scratch, sizeof(scratch), offset, details.reasonId);
	offset = safeAppendNewline(scratch, sizeof(scratch), offset);
	offset = safeAppend(scratch, sizeof(scratch), offset, "reasonDetail: ");
	offset = safeAppend(scratch, sizeof(scratch), offset, details.reasonDetail);
	offset = safeAppendNewline(scratch, sizeof(scratch), offset);
	if (details.hasCode) {
		offset = safeAppend(scratch, sizeof(scratch), offset, "exceptionCode: ");
		offset = safeAppendHex(scratch, sizeof(scratch), offset, details.code);
		offset = safeAppendNewline(scratch, sizeof(scratch), offset);
	}
	if (details.hasAddress) {
		offset = safeAppend(scratch, sizeof(scratch), offset, "faultAddress: ");
		offset = safeAppendHex(scratch, sizeof(scratch), offset, details.address);
		offset = safeAppendNewline(scratch, sizeof(scratch), offset);
	}
	offset = safeAppend(scratch, sizeof(scratch), offset, "crashedAtUnix: ");
	// time() is on the POSIX async-signal-safe list; on Windows this is not a
	// signal context at all.
	offset = safeAppendUnsigned(scratch, sizeof(scratch), offset, static_cast<unsigned long long>(std::time(nullptr)));
	offset = safeAppendNewline(scratch, sizeof(scratch), offset);
	crashWriteBytes(file, scratch, offset);

	crashWriteText(file, "[backtrace]\n");
	writeCrashBacktrace(file);
	crashWriteText(file, "[log]\n");
	writeCrashLogTail(file);

	closeCrashFile(file);
}

void removeSessionMarkerLowLevel() noexcept
{
#if defined(Q_OS_WIN)
	if (g_sessionMarkerPathWide[0] != L'\0') {
		::DeleteFileW(g_sessionMarkerPathWide);
	}
#elif defined(Q_OS_UNIX)
	if (g_sessionMarkerPathUtf8[0] != '\0') {
		::unlink(g_sessionMarkerPathUtf8);
	}
#endif
}

// --- process-wide handlers ---

void studioTerminateHandler() noexcept
{
	if (!tryEnterCrashHandling()) {
		// Already reporting, or capture is off. Let the default behaviour run.
		std::abort();
	}

	g_terminateDetail[0] = '\0';
	// Recovering the in-flight exception's message is worth one attempt, and it
	// is wrapped so a throwing what() or a rethrow failure cannot escape a
	// noexcept function.
	try {
		const std::exception_ptr current = std::current_exception();
		if (current) {
			std::rethrow_exception(current);
		} else {
			safeAppend(g_terminateDetail, kCrashDetailBytes, 0, "terminate called with no active exception");
		}
	} catch (const std::exception& error) {
		safeAppend(g_terminateDetail, kCrashDetailBytes, 0, error.what());
	} catch (...) {
		safeAppend(g_terminateDetail, kCrashDetailBytes, 0, "unknown exception");
	}

	CrashDetails details;
	details.reasonId = "terminate";
	details.reasonDetail = g_terminateDetail;
	writeCrashReport(details);

	std::abort();
}

#if defined(Q_OS_WIN)

// Windows delivers hardware faults to an unhandled-exception filter rather than
// to a signal handler. Returning EXCEPTION_CONTINUE_SEARCH leaves Windows Error
// Reporting and any attached debugger free to do their normal job afterwards.
LONG WINAPI studioUnhandledExceptionFilter(EXCEPTION_POINTERS* pointers) noexcept
{
	if (!tryEnterCrashHandling()) {
		return EXCEPTION_CONTINUE_SEARCH;
	}

	CrashDetails details;
	details.reasonId = "windows-exception";
	details.reasonDetail = "unhandled structured exception";
	if (pointers && pointers->ExceptionRecord) {
		details.hasCode = true;
		details.code = static_cast<unsigned long long>(pointers->ExceptionRecord->ExceptionCode);
		details.hasAddress = true;
		details.address = reinterpret_cast<unsigned long long>(pointers->ExceptionRecord->ExceptionAddress);
		switch (pointers->ExceptionRecord->ExceptionCode) {
		case EXCEPTION_ACCESS_VIOLATION:
			details.reasonDetail = "access violation";
			break;
		case EXCEPTION_STACK_OVERFLOW:
			details.reasonDetail = "stack overflow";
			break;
		case EXCEPTION_ILLEGAL_INSTRUCTION:
			details.reasonDetail = "illegal instruction";
			break;
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
		case EXCEPTION_FLT_DIVIDE_BY_ZERO:
			details.reasonDetail = "divide by zero";
			break;
		default:
			break;
		}
	}
	writeCrashReport(details);
	return EXCEPTION_CONTINUE_SEARCH;
}

// abort() (which qFatal reaches) bypasses the exception filter entirely.
void studioAbortHandler(int) noexcept
{
	if (!tryEnterCrashHandling()) {
		return;
	}
	CrashDetails details;
	details.reasonId = "signal";
	details.reasonDetail = "SIGABRT";
	writeCrashReport(details);
}

#elif defined(Q_OS_UNIX)

const char* posixSignalName(int number) noexcept
{
	switch (number) {
	case SIGSEGV:
		return "SIGSEGV";
	case SIGBUS:
		return "SIGBUS";
	case SIGFPE:
		return "SIGFPE";
	case SIGILL:
		return "SIGILL";
	case SIGABRT:
		return "SIGABRT";
	default:
		break;
	}
	return "unknown signal";
}

// Installed with SA_RESETHAND, so the disposition is already back to SIG_DFL by
// the time this returns: re-raising produces the normal kill plus core dump.
void studioPosixSignalHandler(int number, siginfo_t* info, void*) noexcept
{
	if (tryEnterCrashHandling()) {
		CrashDetails details;
		details.reasonId = "signal";
		details.reasonDetail = posixSignalName(number);
		details.hasCode = true;
		details.code = static_cast<unsigned long long>(number);
		if (info) {
			details.hasAddress = true;
			details.address = reinterpret_cast<unsigned long long>(info->si_addr);
		}
		writeCrashReport(details);
	}
	::raise(number);
	::_exit(128 + number);
}

#endif

bool installOsCrashHandlers() noexcept
{
	std::set_terminate(&studioTerminateHandler);

#if defined(Q_OS_WIN)
	::SetUnhandledExceptionFilter(&studioUnhandledExceptionFilter);
	std::signal(SIGABRT, &studioAbortHandler);
	return true;
#elif defined(Q_OS_UNIX)
	struct sigaction action;
	std::memset(&action, 0, sizeof(action));
	action.sa_sigaction = &studioPosixSignalHandler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = SA_SIGINFO | SA_RESETHAND;
#if defined(SA_ONSTACK)
	// A stack overflow faults with no usable stack left, so run on the
	// alternate stack when the platform offers one.
	action.sa_flags |= SA_ONSTACK;
#endif
	// Not named `signals`: Qt defines that as a macro unless QT_NO_KEYWORDS.
	const int handledSignals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
	for (const int number : handledSignals) {
		::sigaction(number, &action, nullptr);
	}
	return true;
#else
	// std::set_terminate is portable; nothing else can be installed here, and
	// no report will be produced for a hardware fault.
	return false;
#endif
}

bool backtraceAvailableOnThisPlatform() noexcept
{
#if defined(Q_OS_WIN) || defined(VIBESTUDIO_HAVE_EXECINFO)
	return true;
#else
	return false;
#endif
}

void warmUpBacktrace() noexcept
{
#if defined(VIBESTUDIO_HAVE_EXECINFO)
	// The first backtrace() call loads and initialises the unwinder, which
	// allocates. Doing it now means the handler never has to.
	void* frames[4] = {};
	(void)::backtrace(frames, 4);
#endif
}

bool processIsRunning(qint64 processId) noexcept
{
	if (processId <= 0) {
		return false;
	}
#if defined(Q_OS_WIN)
	const HANDLE handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(processId));
	if (!handle) {
		return false;
	}
	DWORD exitCode = 0;
	const bool running = ::GetExitCodeProcess(handle, &exitCode) != 0 && exitCode == STILL_ACTIVE;
	::CloseHandle(handle);
	return running;
#elif defined(Q_OS_UNIX)
	if (::kill(static_cast<pid_t>(processId), 0) == 0) {
		return true;
	}
	// EPERM means the process exists but belongs to someone else.
	return errno == EPERM;
#else
	// Nothing portable to ask. Treating the owner as gone at worst offers a
	// crash report the user can dismiss.
	return false;
#endif
}

// --- report text ---

QString sanitizeReportValue(const QString& value)
{
	QString cleaned = value.simplified();
	cleaned.remove(QLatin1Char('\n'));
	cleaned.remove(QLatin1Char('\r'));
	return cleaned;
}

void appendReportField(QByteArray* target, const char* key, const QString& value)
{
	if (!target) {
		return;
	}
	target->append(key);
	target->append(": ", 2);
	target->append(sanitizeReportValue(value).toUtf8());
	target->append('\n');
}

// The fixed part of a report: everything known before the crash. The installed
// handler holds exactly these bytes pre-rendered.
QByteArray crashReportHeaderBytes(const CrashReportInfo& info, const char* banner)
{
	QByteArray bytes;
	bytes.append(banner);
	bytes.append('\n');
	appendReportField(&bytes, "formatVersion", QString::number(info.formatVersion > 0 ? info.formatVersion : kCrashReportFormatVersion));
	appendReportField(&bytes, "version", info.version);
	appendReportField(&bytes, "qtVersion", info.qtVersion);
	appendReportField(&bytes, "platform", info.platform);
	appendReportField(&bytes, "updateChannel", info.updateChannel);
	appendReportField(&bytes, "sessionId", info.sessionId);
	appendReportField(&bytes, "processId", QString::number(info.processId));
	appendReportField(&bytes, "sessionStarted",
		info.sessionStarted.isValid() ? info.sessionStarted.toUTC().toString(Qt::ISODate) : QString());
	appendReportField(&bytes, "sessionLog", info.sessionLogPath);
	return bytes;
}

CrashReportInfo parseReportBytes(const QByteArray& text, const QString& path, const char* expectedBanner)
{
	CrashReportInfo info;
	info.path = path;

	const QList<QByteArray> rawLines = text.split('\n');
	if (rawLines.isEmpty()) {
		return info;
	}
	if (QString::fromUtf8(rawLines.first()).trimmed() != QString::fromUtf8(expectedBanner)) {
		return info;
	}
	info.valid = true;

	// 0 = header, 1 = backtrace, 2 = log. The markers are only honoured in
	// order, so a logged line that happens to read "[log]" cannot reopen a
	// section.
	int section = 0;
	const int limit = static_cast<int>(std::min<qsizetype>(rawLines.size(), kCrashReportLineLimit));
	for (int index = 1; index < limit; ++index) {
		const QString line = QString::fromUtf8(rawLines.at(index));
		if (section == 0 && line == QStringLiteral("[backtrace]")) {
			section = 1;
			continue;
		}
		if (section <= 1 && line == QStringLiteral("[log]")) {
			section = 2;
			continue;
		}

		if (section == 1) {
			if (!line.isEmpty()) {
				info.backtrace << line;
			}
			continue;
		}
		if (section == 2) {
			if (!line.isEmpty()) {
				info.logLines << line;
			}
			continue;
		}

		const qsizetype separator = line.indexOf(QStringLiteral(": "));
		if (separator <= 0) {
			continue;
		}
		const QString key = line.left(separator);
		const QString value = line.mid(separator + 2);

		if (key == QLatin1String("formatVersion")) {
			info.formatVersion = value.toInt();
		} else if (key == QLatin1String("version")) {
			info.version = value;
		} else if (key == QLatin1String("qtVersion")) {
			info.qtVersion = value;
		} else if (key == QLatin1String("platform")) {
			info.platform = value;
		} else if (key == QLatin1String("updateChannel")) {
			info.updateChannel = value;
		} else if (key == QLatin1String("sessionId")) {
			info.sessionId = value;
		} else if (key == QLatin1String("processId")) {
			info.processId = value.toLongLong();
		} else if (key == QLatin1String("sessionStarted")) {
			info.sessionStarted = QDateTime::fromString(value, Qt::ISODate);
		} else if (key == QLatin1String("sessionLog")) {
			info.sessionLogPath = value;
		} else if (key == QLatin1String("reasonId")) {
			info.reasonId = value;
		} else if (key == QLatin1String("reasonDetail")) {
			info.reasonDetail = value;
		} else if (key == QLatin1String("exceptionCode")) {
			info.exceptionCode = value;
		} else if (key == QLatin1String("faultAddress")) {
			info.faultAddress = value;
		} else if (key == QLatin1String("crashedAtUnix")) {
			bool parsed = false;
			const qint64 seconds = value.toLongLong(&parsed);
			if (parsed && seconds > 0) {
				info.crashedAt = QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::UTC);
			}
		}
	}

	return info;
}

QByteArray readBoundedFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	// A report is our own file, but it lives in a user-writable directory, so
	// the read is bounded like any other file-driven read.
	return file.read(kCrashReportReadLimit);
}

struct CrashCaptureState {
	bool installed = false;
	bool osHandlersInstalled = false;
	QString directory;
	QString reportPath;
	QString markerPath;
	QString sessionId;
	QString platform;
	// Kept so the marker can be rewritten when crash capture is switched back
	// on mid-session.
	QByteArray markerBytes;
	bool previousCrashed = false;
	CrashReportInfo previousReport;
};

CrashCaptureState& crashCaptureState()
{
	static CrashCaptureState state;
	return state;
}

QString defaultCrashDirectory()
{
	const QString logPath = sessionLogFilePath();
	if (!logPath.isEmpty()) {
		return QFileInfo(logPath).absolutePath();
	}
	const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	if (root.isEmpty()) {
		return QString();
	}
	return QDir(root).filePath(QStringLiteral("logs"));
}

QString describePlatform()
{
	const QString product = QSysInfo::prettyProductName();
	const QString architecture = QSysInfo::currentCpuArchitecture();
	if (product.isEmpty()) {
		return architecture;
	}
	return QStringLiteral("%1 (%2)").arg(product, architecture);
}

void storePathBuffers(const QString& reportPath, const QString& markerPath)
{
	const QByteArray reportUtf8 = reportPath.toUtf8();
	const int reportLength = static_cast<int>(std::min<qsizetype>(reportUtf8.size(), kCrashPathChars - 1));
	std::memcpy(g_crashReportPathUtf8, reportUtf8.constData(), static_cast<size_t>(reportLength));
	g_crashReportPathUtf8[reportLength] = '\0';

	const QByteArray markerUtf8 = markerPath.toUtf8();
	const int markerLength = static_cast<int>(std::min<qsizetype>(markerUtf8.size(), kCrashPathChars - 1));
	std::memcpy(g_sessionMarkerPathUtf8, markerUtf8.constData(), static_cast<size_t>(markerLength));
	g_sessionMarkerPathUtf8[markerLength] = '\0';

#if defined(Q_OS_WIN)
	// The wide path is what CreateFileW needs, and it keeps a non-ASCII profile
	// directory working.
	const int reportChars = static_cast<int>(std::min<qsizetype>(reportPath.size(), kCrashPathChars - 1));
	reportPath.left(reportChars).toWCharArray(g_crashReportPathWide);
	g_crashReportPathWide[reportChars] = L'\0';

	const int markerChars = static_cast<int>(std::min<qsizetype>(markerPath.size(), kCrashPathChars - 1));
	markerPath.left(markerChars).toWCharArray(g_sessionMarkerPathWide);
	g_sessionMarkerPathWide[markerChars] = L'\0';
#endif
}

void storeCrashPrefix(const QByteArray& prefix)
{
	const int length = static_cast<int>(std::min<qsizetype>(prefix.size(), kCrashPrefixBytes - 1));
	std::memcpy(g_crashPrefix, prefix.constData(), static_cast<size_t>(length));
	g_crashPrefix[length] = '\0';
	g_crashPrefixLength = length;
}

QFileInfoList crashReportFiles(const QString& directory)
{
	QDir dir(directory);
	if (directory.isEmpty() || !dir.exists()) {
		return QFileInfoList();
	}
	QFileInfoList files = dir.entryInfoList(QStringList{QStringLiteral("crash-*.txt")}, QDir::Files, QDir::NoSort);
	std::sort(files.begin(), files.end(), [](const QFileInfo& left, const QFileInfo& right) {
		return left.lastModified() > right.lastModified();
	});
	return files;
}

// Finds the stale marker of the most recent session that is no longer running,
// consuming (deleting) every stale marker it sees.
CrashReportInfo consumeStaleSessionMarkers(const QString& directory, qint64 currentProcessId)
{
	CrashReportInfo newest;
	QDir dir(directory);
	if (directory.isEmpty() || !dir.exists()) {
		return newest;
	}

	const QFileInfoList markers = dir.entryInfoList(QStringList{QStringLiteral("session-*.marker")}, QDir::Files, QDir::NoSort);
	QStringList doomed;
	for (const QFileInfo& marker : markers) {
		const CrashReportInfo info = parseReportBytes(readBoundedFile(marker.absoluteFilePath()),
			marker.absoluteFilePath(), kSessionMarkerBanner);
		if (!info.valid) {
			// Not one of ours, or truncated. Leave it alone.
			continue;
		}
		if (info.processId == currentProcessId || processIsRunning(info.processId)) {
			// A live instance owns it.
			continue;
		}
		doomed << marker.absoluteFilePath();
		if (!newest.valid || (info.sessionStarted.isValid() && info.sessionStarted > newest.sessionStarted)) {
			newest = info;
		}
	}

	for (const QString& path : std::as_const(doomed)) {
		QFile::remove(path);
	}
	return newest;
}

void writeSessionMarkerFile()
{
	const CrashCaptureState& state = crashCaptureState();
	if (state.markerPath.isEmpty() || state.markerBytes.isEmpty()) {
		return;
	}
	QFile marker(state.markerPath);
	if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		marker.write(state.markerBytes);
		marker.close();
	}
}

} // namespace

CrashHandlerStatus installCrashHandling(const CrashHandlerOptions& options)
{
	CrashCaptureState& state = crashCaptureState();
	if (state.installed) {
		return crashHandlerStatus();
	}

	state.directory = options.directory.trimmed().isEmpty()
		? defaultCrashDirectory()
		: QDir::cleanPath(options.directory.trimmed());
	if (!state.directory.isEmpty()) {
		QDir().mkpath(state.directory);
	}
	state.platform = describePlatform();

	const qint64 currentProcessId = QCoreApplication::applicationPid();

	// Anything left behind by a session whose process is gone means that session
	// never got to say goodbye.
	const CrashReportInfo stale = consumeStaleSessionMarkers(state.directory, currentProcessId);
	state.previousCrashed = stale.valid;
	if (stale.valid) {
		CrashReportInfo report;
		const QFileInfoList reports = crashReportFiles(state.directory);
		for (const QFileInfo& candidate : reports) {
			if (!stale.sessionId.isEmpty() && candidate.fileName().contains(stale.sessionId)) {
				report = readCrashReport(candidate.absoluteFilePath());
				break;
			}
		}
		if (report.valid) {
			state.previousReport = report;
		} else {
			// The session died without managing to write a report: a kill, an
			// OOM, or a power loss. What the marker knows is still worth
			// offering.
			state.previousReport = stale;
			state.previousReport.path.clear();
			state.previousReport.reasonId = QStringLiteral("unclean-exit");
			state.previousReport.reasonDetail = runtimeText("The previous session ended without writing a crash report.");
		}
	}

	pruneCrashReports(options.keepReports, options.maxReportAgeDays);

	state.installed = true;
	if (!options.enabled || state.directory.isEmpty()) {
		// Opt-out, or nowhere to write. Previous-session detection above has
		// already run, so an earlier report stays available.
		g_crashHandlingEnabled.store(false, std::memory_order_release);
		return crashHandlerStatus();
	}

	state.sessionId = QUuid::createUuid().toString(QUuid::Id128);
	const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss"));
	state.reportPath = QDir(state.directory).filePath(
		QStringLiteral("crash-%1-%2-%3.txt").arg(stamp, QString::number(currentProcessId), state.sessionId));
	state.markerPath = QDir(state.directory).filePath(
		QStringLiteral("session-%1-%2.marker").arg(QString::number(currentProcessId), state.sessionId));

	CrashReportInfo self;
	self.formatVersion = kCrashReportFormatVersion;
	self.version = versionString();
	self.qtVersion = QString::fromLatin1(qVersion());
	self.platform = state.platform;
	self.updateChannel = updateChannel();
	self.sessionId = state.sessionId;
	self.processId = currentProcessId;
	self.sessionStarted = QDateTime::currentDateTimeUtc();
	self.sessionLogPath = sessionLogFilePath();

	storePathBuffers(state.reportPath, state.markerPath);
	storeCrashPrefix(crashReportHeaderBytes(self, kCrashReportBanner));

	// The marker is what the next launch looks for. It carries the same fields
	// as a report so one parser handles both.
	state.markerBytes = crashReportHeaderBytes(self, kSessionMarkerBanner);
	writeSessionMarkerFile();

	g_captureBacktrace.store(options.captureBacktrace, std::memory_order_release);
	g_crashHandlingEnabled.store(true, std::memory_order_release);
	warmUpBacktrace();

	if (options.installOsHandlers) {
		state.osHandlersInstalled = installOsCrashHandlers();
		// A clean return from main() should never look like a crash.
		std::atexit(&removeSessionMarkerLowLevel);
	}

	return crashHandlerStatus();
}

CrashHandlerStatus crashHandlerStatus()
{
	const CrashCaptureState& state = crashCaptureState();
	CrashHandlerStatus status;
	status.installed = state.installed;
	status.osHandlersInstalled = state.osHandlersInstalled;
	status.backtraceAvailable = backtraceAvailableOnThisPlatform() && g_captureBacktrace.load(std::memory_order_acquire);
	status.directory = state.directory;
	status.reportPath = state.reportPath;
	status.markerPath = state.markerPath;
	status.sessionId = state.sessionId;
	status.platform = state.platform;
	return status;
}

void setCrashHandlingEnabled(bool enabled)
{
	g_crashHandlingEnabled.store(enabled, std::memory_order_release);
	if (enabled) {
		// Switching back on has to restore the marker, or a later crash would
		// still be invisible to the next launch.
		writeSessionMarkerFile();
	} else {
		// Leaving the marker behind would report a crash for a session the user
		// asked us to stop watching.
		markSessionEndedCleanly();
	}
}

bool crashHandlingEnabled()
{
	return g_crashHandlingEnabled.load(std::memory_order_acquire);
}

void markSessionEndedCleanly()
{
	const CrashCaptureState& state = crashCaptureState();
	if (!state.markerPath.isEmpty()) {
		QFile::remove(state.markerPath);
	}
	removeSessionMarkerLowLevel();
}

bool previousSessionCrashed()
{
	return crashCaptureState().previousCrashed;
}

CrashReportInfo previousSessionCrashReport()
{
	return crashCaptureState().previousReport;
}

QString crashReportDirectory()
{
	const CrashCaptureState& state = crashCaptureState();
	return state.directory.isEmpty() ? defaultCrashDirectory() : state.directory;
}

QVector<CrashReportInfo> recentCrashReports(int maxReports)
{
	QVector<CrashReportInfo> reports;
	if (maxReports <= 0) {
		return reports;
	}
	const QFileInfoList files = crashReportFiles(crashReportDirectory());
	for (const QFileInfo& file : files) {
		const CrashReportInfo info = readCrashReport(file.absoluteFilePath());
		if (!info.valid) {
			continue;
		}
		reports.push_back(info);
		if (reports.size() >= maxReports) {
			break;
		}
	}
	return reports;
}

int pruneCrashReports(int keepMostRecent, int maxAgeDays)
{
	const QFileInfoList files = crashReportFiles(crashReportDirectory());
	if (files.isEmpty()) {
		return 0;
	}

	const QDateTime cutoff = maxAgeDays > 0
		? QDateTime::currentDateTimeUtc().addDays(-maxAgeDays)
		: QDateTime();

	int removed = 0;
	for (int index = 0; index < files.size(); ++index) {
		const QFileInfo& file = files.at(index);
		bool expired = keepMostRecent >= 0 && index >= keepMostRecent;
		if (!expired && cutoff.isValid() && file.lastModified().toUTC() < cutoff) {
			expired = true;
		}
		if (expired && QFile::remove(file.absoluteFilePath())) {
			++removed;
		}
	}
	return removed;
}

QByteArray formatCrashReportText(const CrashReportInfo& info)
{
	QByteArray bytes = crashReportHeaderBytes(info, kCrashReportBanner);
	appendReportField(&bytes, "reasonId", info.reasonId);
	appendReportField(&bytes, "reasonDetail", info.reasonDetail);
	if (!info.exceptionCode.isEmpty()) {
		appendReportField(&bytes, "exceptionCode", info.exceptionCode);
	}
	if (!info.faultAddress.isEmpty()) {
		appendReportField(&bytes, "faultAddress", info.faultAddress);
	}
	appendReportField(&bytes, "crashedAtUnix",
		QString::number(info.crashedAt.isValid() ? info.crashedAt.toSecsSinceEpoch() : 0));

	bytes.append("[backtrace]\n");
	for (const QString& frame : info.backtrace) {
		bytes.append(sanitizeReportValue(frame).toUtf8());
		bytes.append('\n');
	}
	bytes.append("[log]\n");
	for (const QString& line : info.logLines) {
		bytes.append(sanitizeReportValue(line).toUtf8());
		bytes.append('\n');
	}
	return bytes;
}

CrashReportInfo parseCrashReportText(const QByteArray& text, const QString& path)
{
	return parseReportBytes(text, path, kCrashReportBanner);
}

CrashReportInfo readCrashReport(const QString& path)
{
	if (path.trimmed().isEmpty()) {
		return CrashReportInfo();
	}
	return parseReportBytes(readBoundedFile(path), QDir::cleanPath(path), kCrashReportBanner);
}

QStringList crashReportSummaryLines(const CrashReportInfo& info)
{
	QStringList lines;
	if (!info.valid) {
		lines << runtimeText("No crash report is available for the previous session.");
		return lines;
	}

	lines << runtimeText("VibeStudio %1 closed unexpectedly.").arg(info.version.isEmpty() ? runtimeText("(unknown build)") : info.version);
	if (info.crashedAt.isValid()) {
		lines << runtimeText("Time: %1").arg(info.crashedAt.toUTC().toString(Qt::ISODate));
	}
	if (!info.reasonDetail.isEmpty()) {
		lines << runtimeText("Reason: %1").arg(info.reasonDetail);
	} else if (!info.reasonId.isEmpty()) {
		lines << runtimeText("Reason: %1").arg(info.reasonId);
	}
	if (!info.platform.isEmpty()) {
		lines << runtimeText("Platform: %1").arg(info.platform);
	}
	if (!info.qtVersion.isEmpty()) {
		lines << runtimeText("Qt: %1").arg(info.qtVersion);
	}
	if (!info.updateChannel.isEmpty()) {
		lines << runtimeText("Update channel: %1").arg(info.updateChannel);
	}
	if (!info.path.isEmpty()) {
		lines << runtimeText("Report: %1").arg(info.path);
	}
	if (!info.sessionLogPath.isEmpty()) {
		lines << runtimeText("Session log: %1").arg(info.sessionLogPath);
	}
	if (!info.backtrace.isEmpty()) {
		lines << runtimeText("Captured %1 stack frame(s).").arg(QString::number(info.backtrace.size()));
	}
	if (!info.logLines.isEmpty()) {
		lines << runtimeText("Captured %1 log line(s) from the crashed session.").arg(QString::number(info.logLines.size()));
	}
	return lines;
}

QString crashReportSummaryText(const CrashReportInfo& info)
{
	return crashReportSummaryLines(info).join(QLatin1Char('\n'));
}

QJsonObject crashReportJson(const CrashReportInfo& info)
{
	QJsonObject object;
	object.insert(QStringLiteral("valid"), info.valid);
	object.insert(QStringLiteral("formatVersion"), info.formatVersion);
	object.insert(QStringLiteral("path"), info.path);
	object.insert(QStringLiteral("version"), info.version);
	object.insert(QStringLiteral("qtVersion"), info.qtVersion);
	object.insert(QStringLiteral("platform"), info.platform);
	object.insert(QStringLiteral("updateChannel"), info.updateChannel);
	object.insert(QStringLiteral("sessionId"), info.sessionId);
	object.insert(QStringLiteral("processId"), static_cast<double>(info.processId));
	object.insert(QStringLiteral("sessionStarted"),
		info.sessionStarted.isValid() ? info.sessionStarted.toUTC().toString(Qt::ISODate) : QString());
	object.insert(QStringLiteral("crashedAt"),
		info.crashedAt.isValid() ? info.crashedAt.toUTC().toString(Qt::ISODate) : QString());
	object.insert(QStringLiteral("sessionLog"), info.sessionLogPath);
	object.insert(QStringLiteral("reasonId"), info.reasonId);
	object.insert(QStringLiteral("reasonDetail"), info.reasonDetail);
	if (!info.exceptionCode.isEmpty()) {
		object.insert(QStringLiteral("exceptionCode"), info.exceptionCode);
	}
	if (!info.faultAddress.isEmpty()) {
		object.insert(QStringLiteral("faultAddress"), info.faultAddress);
	}

	QJsonArray backtrace;
	for (const QString& frame : info.backtrace) {
		backtrace.append(frame);
	}
	object.insert(QStringLiteral("backtrace"), backtrace);

	QJsonArray logLines;
	for (const QString& line : info.logLines) {
		logLines.append(line);
	}
	object.insert(QStringLiteral("logLines"), logLines);
	return object;
}

} // namespace vibestudio
