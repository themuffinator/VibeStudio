// Crash capture is tested through the pieces that can be called directly:
// report formatting, report parsing, and the previous-session bookkeeping.
// Nothing here crashes the process on purpose. The interface language's
// fonts and the untranslated-message log are tested here too.

#include "app/studio_runtime.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QVector>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	const qint64 written = file.write(bytes);
	file.close();
	return written == bytes.size();
}

CrashReportInfo sampleReport()
{
	CrashReportInfo info;
	info.valid = true;
	info.formatVersion = 1;
	info.version = QStringLiteral("0.9.9-test");
	info.qtVersion = QStringLiteral("6.10.1");
	info.platform = QStringLiteral("Test Platform (x86_64)");
	info.updateChannel = QStringLiteral("dev");
	info.sessionId = QStringLiteral("0123456789abcdef0123456789abcdef");
	info.processId = 4321;
	info.sessionStarted = QDateTime::fromSecsSinceEpoch(1'700'000'000, QTimeZone::utc());
	info.crashedAt = QDateTime::fromSecsSinceEpoch(1'700'000'500, QTimeZone::utc());
	info.sessionLogPath = QStringLiteral("/tmp/vibestudio/logs/vibestudio-20260101.log");
	info.reasonId = QStringLiteral("signal");
	info.reasonDetail = QStringLiteral("SIGSEGV");
	info.exceptionCode = QStringLiteral("0xc0000005");
	info.faultAddress = QStringLiteral("0x00007ff6deadbeef");
	info.backtrace << QStringLiteral("0x00007ff600001000")
		<< QStringLiteral("0x00007ff600002000")
		<< QStringLiteral("0x00007ff600003000");
	info.logLines << QStringLiteral("2026-01-01T00:00:01Z [warning] texture wad missing")
		<< QStringLiteral("2026-01-01T00:00:02Z [critical] compile step failed");
	return info;
}

bool runFormatRoundTripSmoke()
{
	bool ok = true;
	const CrashReportInfo original = sampleReport();
	const QByteArray text = formatCrashReportText(original);

	ok &= expect(text.startsWith("VibeStudio crash report\n"), "A report should start with its banner.");
	ok &= expect(text.contains("\nreasonId: signal\n"), "The reason id should be written as a plain field.");
	ok &= expect(text.contains("\n[backtrace]\n"), "The backtrace section marker should be present.");
	ok &= expect(text.contains("\n[log]\n"), "The log section marker should be present.");
	// Format tokens are never translated, so a report written under any locale
	// parses the same way.
	ok &= expect(text.contains("\nupdateChannel: dev\n"), "The update channel should be recorded.");
	ok &= expect(text.contains("\nqtVersion: 6.10.1\n"), "The Qt version should be recorded.");
	ok &= expect(text.contains("\nplatform: Test Platform (x86_64)\n"), "The platform should be recorded.");

	const CrashReportInfo parsed = parseCrashReportText(text, QStringLiteral("/tmp/report.txt"));
	ok &= expect(parsed.valid, "A formatted report should parse.");
	ok &= expect(parsed.path == QStringLiteral("/tmp/report.txt"), "The parser should keep the supplied path.");
	ok &= expect(parsed.formatVersion == original.formatVersion, "The format version should round-trip.");
	ok &= expect(parsed.version == original.version, "The build version should round-trip.");
	ok &= expect(parsed.qtVersion == original.qtVersion, "The Qt version should round-trip.");
	ok &= expect(parsed.platform == original.platform, "The platform should round-trip.");
	ok &= expect(parsed.updateChannel == original.updateChannel, "The update channel should round-trip.");
	ok &= expect(parsed.sessionId == original.sessionId, "The session id should round-trip.");
	ok &= expect(parsed.processId == original.processId, "The process id should round-trip.");
	ok &= expect(parsed.sessionStarted == original.sessionStarted, "The session start should round-trip.");
	ok &= expect(parsed.crashedAt == original.crashedAt, "The crash time should round-trip.");
	ok &= expect(parsed.sessionLogPath == original.sessionLogPath, "The session log path should round-trip.");
	ok &= expect(parsed.reasonId == original.reasonId, "The reason id should round-trip.");
	ok &= expect(parsed.reasonDetail == original.reasonDetail, "The reason detail should round-trip.");
	ok &= expect(parsed.exceptionCode == original.exceptionCode, "The exception code should round-trip.");
	ok &= expect(parsed.faultAddress == original.faultAddress, "The fault address should round-trip.");
	ok &= expect(parsed.backtrace == original.backtrace, "The backtrace should round-trip.");
	ok &= expect(parsed.logLines == original.logLines, "The log tail should round-trip.");

	// A report with nothing optional set must still be well formed.
	CrashReportInfo bare;
	bare.version = QStringLiteral("0.0.1");
	const CrashReportInfo bareParsed = parseCrashReportText(formatCrashReportText(bare));
	ok &= expect(bareParsed.valid, "A minimal report should still parse.");
	ok &= expect(bareParsed.version == QStringLiteral("0.0.1"), "A minimal report should keep its version.");
	ok &= expect(bareParsed.backtrace.isEmpty() && bareParsed.logLines.isEmpty(), "A minimal report has no sections.");
	ok &= expect(!bareParsed.crashedAt.isValid(), "A report with no crash time should not invent one.");
	return ok;
}

bool runParseRobustnessSmoke()
{
	bool ok = true;

	ok &= expect(!parseCrashReportText(QByteArray()).valid, "Empty input should not parse as a report.");
	ok &= expect(!parseCrashReportText(QByteArrayLiteral("not a report at all")).valid, "Unrelated text should not parse.");
	ok &= expect(!parseCrashReportText(QByteArrayLiteral("VibeStudio session marker\nprocessId: 1\n")).valid,
		"A session marker should not parse as a crash report.");

	// A banner with nothing after it, and a banner followed by junk.
	ok &= expect(parseCrashReportText(QByteArrayLiteral("VibeStudio crash report\n")).valid, "A bare banner is still a report.");
	const CrashReportInfo junk = parseCrashReportText(QByteArrayLiteral(
		"VibeStudio crash report\n"
		"garbage without a separator\n"
		": leading colon\n"
		"processId: not-a-number\n"
		"crashedAtUnix: -5\n"
		"[log]\n"
		"one line\n"));
	ok &= expect(junk.valid, "A report with malformed fields should still parse.");
	ok &= expect(junk.processId == 0, "An unparseable process id should fall back to zero.");
	ok &= expect(!junk.crashedAt.isValid(), "A negative crash timestamp should be rejected.");
	ok &= expect(junk.logLines.size() == 1, "The log section should still be read.");

	// Binary noise must not be able to steer the parser.
	QByteArray hostile = QByteArrayLiteral("VibeStudio crash report\n");
	hostile.append(QByteArray(4096, '\0'));
	hostile.append("\n[backtrace]\n");
	hostile.append(QByteArray(1024, '\xff'));
	ok &= expect(parseCrashReportText(hostile).valid, "Binary noise after the banner should parse without failing.");

	ok &= expect(!readCrashReport(QString()).valid, "An empty path should not read a report.");
	ok &= expect(!readCrashReport(QStringLiteral("/definitely/not/here/crash.txt")).valid, "A missing file should not read a report.");
	return ok;
}

bool runSummarySmoke()
{
	bool ok = true;

	const QStringList none = crashReportSummaryLines(CrashReportInfo());
	ok &= expect(none.size() == 1, "An invalid report should summarise in one line.");

	const CrashReportInfo info = sampleReport();
	const QStringList lines = crashReportSummaryLines(info);
	ok &= expect(lines.size() >= 6, "A full report should summarise in several lines.");
	ok &= expect(lines.first().contains(info.version), "The summary should lead with the build version.");
	ok &= expect(crashReportSummaryText(info).contains(QLatin1Char('\n')), "The summary text should be multi-line.");

	const QJsonObject json = crashReportJson(info);
	ok &= expect(json.value(QStringLiteral("valid")).toBool(), "Report JSON should carry validity.");
	ok &= expect(json.value(QStringLiteral("reasonId")).toString() == QStringLiteral("signal"), "Report JSON should carry the reason id.");
	ok &= expect(json.value(QStringLiteral("backtrace")).toArray().size() == 3, "Report JSON should carry the backtrace.");
	ok &= expect(json.value(QStringLiteral("logLines")).toArray().size() == 2, "Report JSON should carry the log tail.");
	ok &= expect(json.value(QStringLiteral("exceptionCode")).toString() == info.exceptionCode, "Report JSON should carry the exception code.");
	return ok;
}

// Installs crash capture exactly once, against a throwaway directory that has
// been seeded to look like a previous session that died.
bool runPreviousSessionSmoke(const QString& directory)
{
	bool ok = true;

	const QString staleSessionId = QStringLiteral("ffffffffffffffffffffffffffffffff");
	CrashReportInfo stale = sampleReport();
	stale.sessionId = staleSessionId;
	// A process id of zero is never a live process, so the marker reads as
	// abandoned without depending on which pids happen to exist.
	stale.processId = 0;

	const QString reportPath = QDir(directory).filePath(
		QStringLiteral("crash-20260101T000000-0-%1.txt").arg(staleSessionId));
	ok &= expect(writeFile(reportPath, formatCrashReportText(stale)), "The seeded crash report should be written.");

	QByteArray marker = QByteArrayLiteral("VibeStudio session marker\n");
	marker.append("formatVersion: 1\n");
	marker.append("version: 0.9.9-test\n");
	marker.append("sessionId: ");
	marker.append(staleSessionId.toUtf8());
	marker.append("\nprocessId: 0\n");
	marker.append("sessionStarted: 2026-01-01T00:00:00Z\n");
	const QString markerPath = QDir(directory).filePath(
		QStringLiteral("session-0-%1.marker").arg(staleSessionId));
	ok &= expect(writeFile(markerPath, marker), "The seeded session marker should be written.");

	CrashHandlerOptions options;
	options.directory = directory;
	// Nothing process-wide is installed: this test only exercises bookkeeping.
	options.installOsHandlers = false;
	options.keepReports = 20;
	options.maxReportAgeDays = 0;

	const CrashHandlerStatus status = installCrashHandling(options);
	ok &= expect(status.installed, "Crash capture should install.");
	ok &= expect(!status.osHandlersInstalled, "No OS handlers should be installed in the test.");
	ok &= expect(QDir(status.directory) == QDir(directory), "The status should report the requested directory.");
	ok &= expect(!status.sessionId.isEmpty(), "A session id should be allocated.");
	ok &= expect(!status.reportPath.isEmpty(), "A report path should be reserved up front.");
	ok &= expect(status.reportPath.contains(status.sessionId), "The reserved report path should carry the session id.");
	ok &= expect(!QFileInfo::exists(status.reportPath), "Nothing crashed, so no report should exist.");
	ok &= expect(!status.markerPath.isEmpty() && QFileInfo::exists(status.markerPath), "A session marker should be written.");

	// The marker is what the next launch reads, so its shape matters: same
	// field layout as a report, different banner.
	QFile markerFile(status.markerPath);
	ok &= expect(markerFile.open(QIODevice::ReadOnly), "The session marker should be readable.");
	const QByteArray markerText = markerFile.readAll();
	markerFile.close();
	ok &= expect(markerText.startsWith("VibeStudio session marker\n"), "The marker should carry the marker banner.");
	ok &= expect(markerText.contains(QStringLiteral("sessionId: %1\n").arg(status.sessionId).toUtf8()), "The marker should carry this session's id.");
	ok &= expect(markerText.contains("\nprocessId: "), "The marker should carry the owning process id.");
	ok &= expect(markerText.contains("\nsessionStarted: "), "The marker should carry the session start time.");
	ok &= expect(!parseCrashReportText(markerText).valid, "The marker must not be mistaken for a crash report.");
	ok &= expect(!status.platform.isEmpty(), "The platform should be described.");
	ok &= expect(crashHandlingEnabled(), "Crash capture should be enabled after installing.");

	ok &= expect(previousSessionCrashed(), "The seeded marker should be read as a previous crash.");
	const CrashReportInfo previous = previousSessionCrashReport();
	ok &= expect(previous.valid, "The previous session's report should be found.");
	ok &= expect(previous.sessionId == staleSessionId, "The report should belong to the seeded session.");
	ok &= expect(QFileInfo(previous.path) == QFileInfo(reportPath), "The report path should point at the seeded file.");
	ok &= expect(previous.reasonDetail == QStringLiteral("SIGSEGV"), "The report's reason should survive.");
	ok &= expect(previous.logLines.size() == 2, "The report's log tail should survive.");
	ok &= expect(!QFileInfo::exists(markerPath), "A consumed stale marker should be removed.");

	// Installing again is a no-op that returns the same status.
	CrashHandlerOptions second;
	second.directory = QStringLiteral("/somewhere/else");
	const CrashHandlerStatus repeated = installCrashHandling(second);
	ok &= expect(repeated.sessionId == status.sessionId, "Installing twice should not start a new session.");
	ok &= expect(repeated.directory == status.directory, "Installing twice should not move the directory.");

	const QVector<CrashReportInfo> recent = recentCrashReports(10);
	ok &= expect(recent.size() == 1, "The seeded report should be listed.");
	if (recent.size() == 1) {
		ok &= expect(recent.first().sessionId == staleSessionId, "The listed report should be the seeded one.");
	}
	ok &= expect(recentCrashReports(0).isEmpty(), "Asking for no reports should return none.");

	// Opting out removes the marker, so the next launch sees a clean exit.
	setCrashHandlingEnabled(false);
	ok &= expect(!crashHandlingEnabled(), "Crash capture should be disableable.");
	ok &= expect(!QFileInfo::exists(status.markerPath), "Disabling crash capture should drop the session marker.");
	setCrashHandlingEnabled(true);
	ok &= expect(crashHandlingEnabled(), "Crash capture should be re-enableable.");
	ok &= expect(QFileInfo::exists(status.markerPath), "Re-enabling crash capture should restore the session marker.");

	markSessionEndedCleanly();
	ok &= expect(!QFileInfo::exists(status.markerPath), "A clean shutdown should leave no marker behind.");

	ok &= expect(pruneCrashReports(0, 0) == 1, "Pruning to nothing should delete the seeded report.");
	ok &= expect(recentCrashReports(10).isEmpty(), "Pruning should leave no reports.");
	ok &= expect(pruneCrashReports(0, 0) == 0, "Pruning an empty directory should delete nothing.");
	return ok;
}

// Han-script interfaces put their own fonts first; every other language
// leaves the platform's fallback alone.
bool runInterfaceFontSmoke()
{
	bool ok = true;
	ok &= expect(interfaceLanguageFontFamilies(QStringLiteral("de")).isEmpty(), "A Latin-script interface should add no fonts.");
	ok &= expect(interfaceLanguageFontFamilies(QStringLiteral("ar")).isEmpty(), "An Arabic interface should add no fonts.");
	for (const QString& language : {QStringLiteral("ja"), QStringLiteral("zh-Hans"), QStringLiteral("zh-Hant"), QStringLiteral("ko")}) {
		QStringList families = interfaceLanguageFontFamilies(language);
		for (const QString& family : std::as_const(families)) {
			ok &= expect(QFontDatabase::hasFamily(family), "Only installed families should be offered.");
		}
		ok &= expect(families.removeDuplicates() == 0, "Each family should be offered once.");
	}
	ok &= expect(interfaceLanguageFontFamilies(QStringLiteral("zh-TW")) == interfaceLanguageFontFamilies(QStringLiteral("zh-Hant")),
		"A regional tag should get its target's fonts.");
	return ok;
}

// The reviewer's untranslated-message log: each studio message without a
// translation once, escaped, and nothing from Qt's own contexts or from the
// source language.
bool runUntranslatedLogSmoke(QCoreApplication& app, const QString& directory)
{
	bool ok = true;
	const QString logPath = QDir(directory).filePath(QStringLiteral("untranslated.tsv"));
	qputenv("VIBESTUDIO_UNTRANSLATED_LOG", QFile::encodeName(logPath));
	const TranslationLoadResult german = installStudioTranslations(app, QStringLiteral("de"));
	const QString germanName = QCoreApplication::translate("VibeStudioLocalization", "German");
	QCoreApplication::translate("VibeStudioRuntimeTest", "Not in any catalog");
	QCoreApplication::translate("VibeStudioRuntimeTest", "Not in any catalog");
	QCoreApplication::translate("vibestudio::RuntimeTest", "Two\nlines");
	QCoreApplication::translate("QDialogButtonBox", "Not ours");
	installStudioTranslations(app, QStringLiteral("en"));
	QCoreApplication::translate("VibeStudioRuntimeTest", "Asked in English");
	qunsetenv("VIBESTUDIO_UNTRANSLATED_LOG");

	QFile file(logPath);
	ok &= expect(file.open(QIODevice::ReadOnly | QIODevice::Text), "The untranslated log should be written.");
	const QList<QByteArray> lines = file.readAll().split('\n');
	ok &= expect(lines.count(QByteArray("VibeStudioRuntimeTest\tNot in any catalog\t")) == 1, "An untranslated message should be logged once.");
	ok &= expect(lines.contains(QByteArray("vibestudio::RuntimeTest\tTwo\\nlines\t")), "A line break should be logged escaped.");
	ok &= expect(!lines.contains(QByteArray("QDialogButtonBox\tNot ours\t")), "Qt's own contexts should not be logged.");
	ok &= expect(!lines.contains(QByteArray("VibeStudioRuntimeTest\tAsked in English\t")), "The source language should log nothing.");
	if (german.installed && germanName != QStringLiteral("German")) {
		ok &= expect(!lines.contains(QByteArray("VibeStudioLocalization\tGerman\t")), "A translated message should not be logged.");
	}
	return ok;
}

} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	bool ok = true;
	ok &= runFormatRoundTripSmoke();
	ok &= runParseRobustnessSmoke();
	ok &= runSummarySmoke();
	ok &= runInterfaceFontSmoke();

	QTemporaryDir temporary;
	ok &= expect(temporary.isValid(), "Crash bookkeeping needs a temporary directory.");
	if (temporary.isValid()) {
		ok &= runPreviousSessionSmoke(temporary.path());
		ok &= runUntranslatedLogSmoke(app, temporary.path());
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
