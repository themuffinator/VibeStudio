#include "core/compiler_runner.h"

#include "core/compiler_artifact_validation.h"
#include "core/compiler_known_issues.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTemporaryDir>
#include <QUuid>

#include <algorithm>

namespace vibestudio {

namespace {

CompilerTaskLogEntry logEntry(const QString& level, const QString& message)
{
	return {QDateTime::currentDateTimeUtc(), level, message};
}

void appendLog(CompilerCommandManifest* manifest, const CompilerRunCallbacks& callbacks, const QString& level, const QString& message)
{
	if (!manifest || message.trimmed().isEmpty()) {
		return;
	}
	const CompilerTaskLogEntry entry = logEntry(level, message.trimmed());
	manifest->taskLog.push_back(entry);
	if (callbacks.logEntry) {
		callbacks.logEntry(entry);
	}
}

CompilerFileHash hashFile(const QString& path)
{
	CompilerFileHash hash;
	hash.path = QDir::cleanPath(path);
	const QFileInfo info(hash.path);
	hash.exists = info.isFile();
	hash.sizeBytes = hash.exists ? info.size() : 0;
	if (!hash.exists) {
		return hash;
	}

	QFile file(hash.path);
	if (!file.open(QIODevice::ReadOnly)) {
		return hash;
	}
	QCryptographicHash digest(QCryptographicHash::Sha256);
	while (!file.atEnd()) {
		digest.addData(file.read(64 * 1024));
	}
	hash.sha256 = QString::fromLatin1(digest.result().toHex());
	return hash;
}

// Extensions VibeStudio is willing to treat as a diagnostic's source file.
const char* kDiagnosticPathExtensions = "map|bsp|wad|shader|cfg|script|txt|c|cpp|h|hpp|pts|prt|lin|lit|pk3|mdl|md3|wal|tga|jpg|def|ent";

// Compiler output is decoded once, from a complete byte buffer. VibeMap2 formats its log through
// fmt and emits UTF-8, while VibeMap3 echoes the narrow argv it was handed, which on Windows is the
// ANSI codepage; decoding UTF-8 first with a local-8-bit fallback covers both without forcing either.
QString decodeToolOutput(const QByteArray& bytes)
{
	QStringDecoder utf8(QStringConverter::Utf8);
	const QString text = utf8.decode(bytes);
	if (utf8.hasError()) {
		return QString::fromLocal8Bit(bytes);
	}
	return text;
}

QString stripTrailingCarriageReturn(const QString& line)
{
	QString stripped = line;
	while (stripped.endsWith('\r')) {
		stripped.chop(1);
	}
	return stripped;
}

// Tools that colour their console output, as VibeMap2 does, wrap their
// messages in ECMA-48 escape sequences: "ESC [ ... final byte" (CSI), "ESC ]
// ... BEL" (OSC), or a two-byte escape. Left in, they show as stray
// characters in the Problems list and the logs, and hide a leading level from
// the diagnostic parser.
QString stripTerminalEscapes(const QString& line)
{
	if (!line.contains(QChar(0x1b))) {
		return line;
	}
	static const QRegularExpression escapes(QStringLiteral(R"re(\x1B(?:\[[0-?]*[ -/]*[@-~]|\][^\x07\x1B]*(?:\x07|\x1B\\)|[@-_]))re"));
	return QString(line).remove(escapes);
}

// Both VibeMap2 (src/common/log.cc, exit_on_exception) and VibeMap3
// (tools/quake3/common/inout.cpp, Error) print a banner line and then the message on the next line.
bool isFatalErrorBanner(const QString& trimmedLine)
{
	static const QRegularExpression bannerPattern(QStringLiteral(R"regex(^\*{3,}\s*(?:FATAL\s+)?ERROR\s*\*{3,}$)regex"), QRegularExpression::CaseInsensitiveOption);
	return bannerPattern.match(trimmedLine).hasMatch();
}

// Zero-count summaries such as "0 errors", "no warnings" or "Error count: 0" are status lines, not diagnostics.
bool isZeroCountSummary(const QString& trimmedLine)
{
	static const QRegularExpression leadingZero(QStringLiteral(R"regex(^\s*0\s+(?:errors?|warnings?)\b)regex"), QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression wordZero(QStringLiteral(R"regex(\b(?:no|zero)\s+(?:errors?|warnings?)\b)regex"), QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression countZero(QStringLiteral(R"regex(\b(?:errors?|warnings?)\s*(?:count)?\s*[:=]\s*0\b)regex"), QRegularExpression::CaseInsensitiveOption);
	return leadingZero.match(trimmedLine).hasMatch() || wordZero.match(trimmedLine).hasMatch() || countZero.match(trimmedLine).hasMatch();
}

QString diagnosticLevelForToken(const QString& token)
{
	const QString lower = token.toLower();
	if (lower.startsWith(QStringLiteral("warn"))) {
		return QStringLiteral("warning");
	}
	return QStringLiteral("error");
}

// A plausible diagnostic either starts with a level token or carries "<level>:" somewhere in the line.
// Plain substring matching used to classify "0 errors", "--leaktest" and similar status text.
QString diagnosticLevelForLine(const QString& trimmedLine)
{
	if (trimmedLine.isEmpty() || isZeroCountSummary(trimmedLine)) {
		return {};
	}
	static const QRegularExpression leadingToken(QStringLiteral(R"regex(^\s*(?:\[[^\]\r\n]*\]\s*)?(fatal error|fatal|error|warning|warn)\b)regex"), QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch leading = leadingToken.match(trimmedLine);
	if (leading.hasMatch()) {
		return diagnosticLevelForToken(leading.captured(1));
	}
	static const QRegularExpression colonToken(QStringLiteral(R"regex(\b(fatal error|fatal|error|warning|warn)\s*:)regex"), QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch colon = colonToken.match(trimmedLine);
	if (colon.hasMatch()) {
		return diagnosticLevelForToken(colon.captured(1));
	}
	return {};
}

void applyDiagnosticLocation(CompilerDiagnostic* diagnostic, const QString& text)
{
	if (!diagnostic) {
		return;
	}

	// VibeMap2 reports locations as "<source>[line N]" (src/include/common/parser.hh, the
	// parser_source_location formatter), not as "path:N".
	static const QRegularExpression bracketLinePattern(QStringLiteral(R"regex(\[line\s+(\d+)\])regex"), QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression bracketSourcePattern(QString::fromLatin1(R"regex(((?:[A-Za-z]:)?[^\s\[\]":]+\.(?:%1))\s*\[line\s+\d+\])regex").arg(QString::fromLatin1(kDiagnosticPathExtensions)), QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch bracketLine = bracketLinePattern.match(text);
	if (bracketLine.hasMatch()) {
		diagnostic->line = bracketLine.captured(1).toInt();
		const QRegularExpressionMatch bracketSource = bracketSourcePattern.match(text);
		if (bracketSource.hasMatch()) {
			diagnostic->filePath = QDir::cleanPath(bracketSource.captured(1));
		}
		return;
	}

	// Many qbsp warnings are "WARNING: <line>: message" with no file name at all.
	static const QRegularExpression bareLinePattern(QStringLiteral(R"regex(^\s*(?:fatal error|fatal|error|warning|warn)\s*:\s*(\d+)\s*:)regex"), QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch bareLine = bareLinePattern.match(text);
	if (bareLine.hasMatch()) {
		diagnostic->line = bareLine.captured(1).toInt();
		return;
	}

	static const QRegularExpression pathPattern(QString::fromLatin1(R"regex((?:"([^"\n\r]+?\.(?:%1))"|((?:[A-Za-z]:)?[^:\n\r\t ]+?\.(?:%1)))(?:[:(](\d+))?(?:[:,](\d+))?)regex").arg(QString::fromLatin1(kDiagnosticPathExtensions)), QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch match = pathPattern.match(text);
	if (match.hasMatch()) {
		const QString quotedPath = match.captured(1);
		diagnostic->filePath = QDir::cleanPath(quotedPath.isEmpty() ? match.captured(2) : quotedPath);
		diagnostic->line = match.captured(3).toInt();
		diagnostic->column = match.captured(4).toInt();
	}
}

// Incremental, line-at-a-time diagnostic parser. It keeps the pending fatal-error banner per channel
// so the banner and the message that follows it are reported as one diagnostic.
class CompilerDiagnosticParser
{
public:
	bool consume(const QString& rawLine, const QString& channel, CompilerDiagnostic* out)
	{
		const QString text = stripTrailingCarriageReturn(rawLine).trimmed();
		if (text.isEmpty()) {
			return false;
		}

		QString& pendingBanner = channel == QStringLiteral("stderr") ? m_pendingStderrBanner : m_pendingStdoutBanner;
		if (!pendingBanner.isEmpty()) {
			CompilerDiagnostic diagnostic;
			diagnostic.level = QStringLiteral("error");
			diagnostic.channel = channel;
			diagnostic.rawLine = QStringLiteral("%1\n%2").arg(pendingBanner, text);
			diagnostic.message = text;
			applyDiagnosticLocation(&diagnostic, text);
			pendingBanner.clear();
			if (out) {
				*out = diagnostic;
			}
			return true;
		}

		if (isFatalErrorBanner(text)) {
			pendingBanner = text;
			return false;
		}

		const QString level = diagnosticLevelForLine(text);
		if (level.isEmpty()) {
			return false;
		}

		CompilerDiagnostic diagnostic;
		diagnostic.level = level;
		diagnostic.channel = channel;
		diagnostic.rawLine = text;
		diagnostic.message = text;
		applyDiagnosticLocation(&diagnostic, text);
		if (out) {
			*out = diagnostic;
		}
		return true;
	}

	// A banner with no message after it is still an error worth reporting.
	QVector<CompilerDiagnostic> flush()
	{
		QVector<CompilerDiagnostic> diagnostics;
		for (const QString& channel : {QStringLiteral("stdout"), QStringLiteral("stderr")}) {
			QString& pendingBanner = channel == QStringLiteral("stderr") ? m_pendingStderrBanner : m_pendingStdoutBanner;
			if (pendingBanner.isEmpty()) {
				continue;
			}
			CompilerDiagnostic diagnostic;
			diagnostic.level = QStringLiteral("error");
			diagnostic.channel = channel;
			diagnostic.rawLine = pendingBanner;
			diagnostic.message = pendingBanner;
			diagnostics.push_back(diagnostic);
			pendingBanner.clear();
		}
		return diagnostics;
	}

private:
	QString m_pendingStdoutBanner;
	QString m_pendingStderrBanner;
};

void refreshFileHashes(CompilerCommandManifest* manifest)
{
	if (!manifest) {
		return;
	}
	manifest->inputHashes.clear();
	for (const QString& path : manifest->inputPaths) {
		manifest->inputHashes.push_back(hashFile(path));
	}
	manifest->outputHashes.clear();
	for (const QString& path : manifest->expectedOutputPaths) {
		manifest->outputHashes.push_back(hashFile(path));
	}
}

QStringList existingOutputs(const QStringList& paths)
{
	QStringList outputs;
	for (const QString& path : paths) {
		const QString cleaned = QDir::cleanPath(path);
		if (QFileInfo(cleaned).isFile() && !outputs.contains(cleaned)) {
			outputs.push_back(cleaned);
		}
	}
	return outputs;
}

bool containsTrimmed(const QStringList& values, const QString& candidate)
{
	const QString trimmed = candidate.trimmed();
	for (const QString& value : values) {
		if (value.trimmed() == trimmed) {
			return true;
		}
	}
	return false;
}

void appendWarning(CompilerRunResult* result, const CompilerRunCallbacks& callbacks, const QString& message)
{
	if (!result || message.trimmed().isEmpty()) {
		return;
	}
	const QString warning = message.trimmed();
	if (!containsTrimmed(result->manifest.warnings, warning)) {
		result->manifest.warnings.push_back(warning);
	}
	appendLog(&result->manifest, callbacks, QStringLiteral("warning"), warning);
}

void appendError(CompilerRunResult* result, const CompilerRunCallbacks& callbacks, const QString& message)
{
	if (!result || message.trimmed().isEmpty()) {
		return;
	}
	const QString error = message.trimmed();
	if (!containsTrimmed(result->manifest.errors, error)) {
		result->manifest.errors.push_back(error);
	}
	appendLog(&result->manifest, callbacks, QStringLiteral("error"), error);
}

bool manifestHasWarnings(const CompilerCommandManifest& manifest)
{
	return !manifest.warnings.isEmpty() || !manifest.knownIssueWarnings.isEmpty() || !manifest.preflightWarnings.isEmpty();
}

// A tool that exited 0 produced its artifacts, so an error-shaped output line is reported rather
// than fatal: VibeMap2 prints non-fatal "ERROR: ..." notices (src/common/bspfile_common.cc,
// src/common/bspxfile.cc) and carries on. The run is not clean either, so it lands on Warning.
OperationState successfulRunState(const CompilerCommandManifest& manifest)
{
	return manifestHasWarnings(manifest) || !manifest.errors.isEmpty() ? OperationState::Warning : OperationState::Completed;
}

void surfacePreflightFindings(CompilerRunResult* result, const CompilerRunCallbacks& callbacks)
{
	if (!result) {
		return;
	}
	for (const QString& note : result->manifest.knownIssueNotes) {
		appendLog(&result->manifest, callbacks, QStringLiteral("info"), QCoreApplication::translate("VibeStudioCompilerRunner", "Known issue note: %1").arg(note));
	}
	QStringList categorizedWarnings;
	for (const QString& warning : result->manifest.knownIssueWarnings) {
		categorizedWarnings.push_back(warning.trimmed());
		appendLog(&result->manifest, callbacks, QStringLiteral("warning"), QCoreApplication::translate("VibeStudioCompilerRunner", "Known issue warning: %1").arg(warning));
	}
	for (const QString& warning : result->manifest.preflightWarnings) {
		categorizedWarnings.push_back(warning.trimmed());
		appendLog(&result->manifest, callbacks, QStringLiteral("warning"), QCoreApplication::translate("VibeStudioCompilerRunner", "Preflight warning: %1").arg(warning));
	}
	for (const QString& warning : result->manifest.warnings) {
		if (!containsTrimmed(categorizedWarnings, warning)) {
			appendLog(&result->manifest, callbacks, QStringLiteral("warning"), QCoreApplication::translate("VibeStudioCompilerRunner", "Plan warning: %1").arg(warning));
		}
	}
	for (const QString& error : result->manifest.errors) {
		appendLog(&result->manifest, callbacks, QStringLiteral("error"), QCoreApplication::translate("VibeStudioCompilerRunner", "Preflight error: %1").arg(error));
	}
}

void appendNote(CompilerRunResult* result, const CompilerRunCallbacks& callbacks, const QString& message)
{
	if (!result || message.trimmed().isEmpty()) {
		return;
	}
	const QString note = message.trimmed();
	if (!containsTrimmed(result->manifest.knownIssueNotes, note)) {
		result->manifest.knownIssueNotes.push_back(note);
	}
	appendLog(&result->manifest, callbacks, QStringLiteral("info"), note);
}

// Run the upstream issue catalog over the captured output so real diagnostics gain upstream context.
void enrichWithKnownIssues(CompilerRunResult* result, const CompilerRunCallbacks& callbacks)
{
	if (!result) {
		return;
	}
	const QString output = QStringLiteral("%1\n%2").arg(result->stdoutText, result->stderrText);
	if (output.trimmed().isEmpty()) {
		return;
	}
	for (const CompilerKnownIssueMatch& match : matchCompilerKnownIssues(output, result->manifest.toolId, result->manifest.profileId)) {
		const QString text = QCoreApplication::translate("VibeStudioCompilerRunner", "Compiler output matches upstream issue #%1 (keyword \"%2\"): %3 Action: %4")
			.arg(match.issue.issueId, match.matchedKeyword, match.issue.warningText, match.issue.actionText);
		if (match.issue.highValue) {
			if (!containsTrimmed(result->manifest.knownIssueWarnings, text)) {
				result->manifest.knownIssueWarnings.push_back(text.trimmed());
			}
			appendWarning(result, callbacks, text);
		} else {
			appendNote(result, callbacks, text);
		}
	}
}

bool profileWritesLeakFiles(const CompilerCommandManifest& manifest)
{
	if (manifest.stageId.compare(QStringLiteral("qbsp"), Qt::CaseInsensitive) == 0
		|| manifest.toolId.compare(QStringLiteral("vibemap2-bsp"), Qt::CaseInsensitive) == 0) {
		return true;
	}
	// VibeMap3's BSPMain removes "<source>.lin" at startup and LeakFile() rewrites it when the map
	// leaks, after which the process still exits 0
	// (external/compilers/vibemap3/tools/quake3/q3map2/bsp.cpp and leakfile.cpp). Only the BSP
	// stage does that: -vis and -light never remove the file, so a stale .lin must not be read back
	// as a fresh leak there.
	return manifest.toolId.compare(QStringLiteral("vibemap3"), Qt::CaseInsensitive) == 0
		&& manifest.stageId.compare(QStringLiteral("bsp"), Qt::CaseInsensitive) == 0;
}

// VibeMap2 bsp writes "<bsp>.pts" (plus "<bsp>.leak.prt"); VibeMap3 writes "<source>.lin".
bool isLeakPointFile(const QString& path)
{
	return path.endsWith(QStringLiteral(".pts"), Qt::CaseInsensitive)
		|| path.endsWith(QStringLiteral(".lin"), Qt::CaseInsensitive);
}

struct LeakFileState
{
	QDateTime lastModifiedUtc;
	qint64 sizeBytes = -1;
};

using LeakFileSnapshot = QHash<QString, LeakFileState>;

// Taken immediately before the process starts. A leak point file that was already on disk proves
// nothing on its own: qbsp only removes stale .bsp/.prt/.pts files when neither -onlyents nor
// -convert is in play (external/compilers/vibemap2/src/qbsp/qbsp.cc), so an entity-only recompile of
// a repaired map leaves the old .pts sitting beside the BSP.
LeakFileSnapshot captureLeakFileSnapshot(const CompilerCommandManifest& manifest)
{
	LeakFileSnapshot snapshot;
	if (!profileWritesLeakFiles(manifest)) {
		return snapshot;
	}
	for (const QString& path : manifest.optionalOutputPaths) {
		if (!isLeakPointFile(path)) {
			continue;
		}
		const QString cleaned = QDir::cleanPath(path);
		const QFileInfo info(cleaned);
		if (!info.isFile()) {
			continue;
		}
		LeakFileState state;
		state.lastModifiedUtc = info.lastModified().toUTC();
		state.sizeBytes = info.size();
		snapshot.insert(cleaned, state);
	}
	return snapshot;
}

bool leakFileIsFresh(const LeakFileSnapshot& snapshot, const QString& cleanedPath, const QFileInfo& info, const QDateTime& startedUtc)
{
	const LeakFileSnapshot::const_iterator previous = snapshot.constFind(cleanedPath);
	if (previous == snapshot.constEnd()) {
		return true;
	}
	if (previous->sizeBytes != info.size() || previous->lastModifiedUtc != info.lastModified().toUTC()) {
		return true;
	}
	// An in-place rewrite of identical bytes within one coarse timestamp tick (FAT granularity or a
	// network share) looks unchanged, so fall back to the timestamp. Erring towards reporting a leak
	// is the safe direction; silently dropping a real one is not.
	return startedUtc.isValid() && info.lastModified().toUTC() >= startedUtc.addSecs(-2);
}

void detectLeak(CompilerRunResult* result, const LeakFileSnapshot& preRunLeakFiles, const CompilerRunCallbacks& callbacks)
{
	if (!result || !profileWritesLeakFiles(result->manifest)) {
		return;
	}

	QString freshLeakFilePath;
	QString staleLeakFilePath;
	for (const QString& path : result->manifest.optionalOutputPaths) {
		if (!isLeakPointFile(path)) {
			continue;
		}
		const QString cleaned = QDir::cleanPath(path);
		const QFileInfo info(cleaned);
		if (!info.isFile()) {
			continue;
		}
		if (leakFileIsFresh(preRunLeakFiles, cleaned, info, result->manifest.startedUtc)) {
			freshLeakFilePath = cleaned;
			break;
		}
		if (staleLeakFilePath.isEmpty()) {
			staleLeakFilePath = cleaned;
		}
	}
	if (!freshLeakFilePath.isEmpty()) {
		result->leakDetected = true;
		result->leakPointFilePath = freshLeakFilePath;
	}

	const QString output = QStringLiteral("%1\n%2").arg(result->stdoutText, result->stderrText);

	// VibeMap2 bsp names the entity it reached and where (src/qbsp/outside.cc).
	static const QRegularExpression occupantPattern(QStringLiteral(R"regex(Reached occupant\s+"([^"]*)"\s+at\s+\(([^)]*)\))regex"), QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch occupant = occupantPattern.match(output);
	if (occupant.hasMatch()) {
		result->leakDetected = true;
		result->leakOccupantClassname = occupant.captured(1).trimmed();
		result->leakPointText = occupant.captured(2).trimmed();
	}

	// VibeMap3 prints a "******* leaked *******" banner from Leak_feedback() and
	// "Entity <n>, Brush <m>: Entity leaked" from xml_Select()
	// (external/compilers/vibemap3/tools/quake3/q3map2/leakfile.cpp and common/inout.cpp). It
	// reports no classname and no coordinates, so only the entity index is available.
	static const QRegularExpression q3LeakBannerPattern(QStringLiteral(R"regex(\*{3,}\s*leaked\s*\*{3,})regex"), QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression q3LeakEntityPattern(QStringLiteral(R"regex(Entity\s+(-?\d+),\s*Brush\s+-?\d+:\s*Entity leaked)regex"), QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch q3LeakEntity = q3LeakEntityPattern.match(output);
	if (q3LeakEntity.hasMatch() || q3LeakBannerPattern.match(output).hasMatch()) {
		result->leakDetected = true;
	}

	if (result->leakDetected && result->leakPointFilePath.isEmpty() && !staleLeakFilePath.isEmpty()) {
		// The compiler output proves the leak, so keep a usable path even when the file on disk did
		// not visibly change.
		result->leakPointFilePath = staleLeakFilePath;
	}

	if (!result->leakDetected) {
		if (!staleLeakFilePath.isEmpty()) {
			appendLog(&result->manifest, callbacks, QStringLiteral("info"),
				QCoreApplication::translate("VibeStudioCompilerRunner", "A leak point file (%1) is present but predates this run; it was left by an earlier compile.")
					.arg(QDir::toNativeSeparators(staleLeakFilePath)));
		}
		return;
	}

	QString message = QCoreApplication::translate("VibeStudioCompilerRunner", "LEAK: the map is not sealed, so visibility and lighting data will be wrong.");
	if (!result->leakOccupantClassname.isEmpty()) {
		message += QLatin1Char(' ');
		message += result->leakPointText.isEmpty()
			? QCoreApplication::translate("VibeStudioCompilerRunner", "The compiler reached the entity \"%1\" from the void.").arg(result->leakOccupantClassname)
			: QCoreApplication::translate("VibeStudioCompilerRunner", "The compiler reached the entity \"%1\" at (%2) from the void.").arg(result->leakOccupantClassname, result->leakPointText);
	} else if (q3LeakEntity.hasMatch()) {
		message += QLatin1Char(' ');
		message += QCoreApplication::translate("VibeStudioCompilerRunner", "The compiler reached map entity %1 from the void.").arg(q3LeakEntity.captured(1));
	}
	if (!result->leakPointFilePath.isEmpty()) {
		message += QLatin1Char(' ');
		message += QCoreApplication::translate("VibeStudioCompilerRunner", "Load the leak point file %1 in the editor to follow the leak line.").arg(QDir::toNativeSeparators(result->leakPointFilePath));
	}
	// -leaktest makes qbsp print this and exit 1 on purpose, after the leak files are written
	// (external/compilers/vibemap2/src/qbsp/outside.cc).
	if (result->exitCode != 0 && output.contains(QStringLiteral("Aborting because -leaktest was used"))) {
		message += QLatin1Char(' ');
		message += QCoreApplication::translate("VibeStudioCompilerRunner", "The non-zero exit code is the expected -leaktest behaviour rather than a separate compile error.");
	}
	appendWarning(result, callbacks, message);
}

QString cleanAbsoluteDirectoryPath(const QString& path)
{
	if (path.trimmed().isEmpty()) {
		return {};
	}
	return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString compilerTempTemplatePath()
{
	return QDir(QDir::tempPath()).filePath(QStringLiteral("vibestudio-compiler-XXXXXX"));
}

void applyIsolatedTempEnvironment(QProcess* process, CompilerCommandManifest* manifest, const QString& tempPath)
{
	if (!process || tempPath.trimmed().isEmpty()) {
		return;
	}
	QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	environment.insert(QStringLiteral("TMP"), tempPath);
	environment.insert(QStringLiteral("TEMP"), tempPath);
	environment.insert(QStringLiteral("TMPDIR"), tempPath);
	process->setProcessEnvironment(environment);
	if (manifest) {
		manifest->environmentSubset.insert(QStringLiteral("TMP"), tempPath);
		manifest->environmentSubset.insert(QStringLiteral("TEMP"), tempPath);
		manifest->environmentSubset.insert(QStringLiteral("TMPDIR"), tempPath);
	}
}

void addManifestProvenanceWarnings(CompilerRunResult* result, const CompilerRunCallbacks& callbacks)
{
	if (!result) {
		return;
	}
	if (result->manifest.schemaVersion != CompilerCommandManifest::kSchemaVersion) {
		appendWarning(result, callbacks, QCoreApplication::translate("VibeStudioCompilerRunner", "Manifest schema version differs from the current compiler manifest schema."));
	}
	if (result->manifest.createdUtc.isValid() == false) {
		appendWarning(result, callbacks, QCoreApplication::translate("VibeStudioCompilerRunner", "Manifest creation timestamp is missing or invalid."));
	}
	if (result->manifest.profileId.trimmed().isEmpty()) {
		appendWarning(result, callbacks, QCoreApplication::translate("VibeStudioCompilerRunner", "Manifest profile provenance is missing."));
	} else if (!result->plan.profileFound) {
		appendWarning(result, callbacks, QCoreApplication::translate("VibeStudioCompilerRunner", "Manifest profile is no longer registered in this build."));
	}
	if (result->manifest.toolId.trimmed().isEmpty()) {
		appendWarning(result, callbacks, QCoreApplication::translate("VibeStudioCompilerRunner", "Manifest tool provenance is missing."));
	}
	if (result->manifest.commandLine.trimmed().isEmpty() && !result->manifest.program.trimmed().isEmpty()) {
		appendWarning(result, callbacks, QCoreApplication::translate("VibeStudioCompilerRunner", "Manifest command line was reconstructed from program and arguments."));
	}
}

void finishResult(CompilerRunResult* result, OperationState state, const QString& error = QString())
{
	if (!result) {
		return;
	}
	result->state = state;
	result->manifest.state = state;
	result->manifest.finishedUtc = QDateTime::currentDateTimeUtc();
	result->manifest.durationMs = result->durationMs;
	result->manifest.exitCode = result->exitCode;
	result->manifest.stdoutText = result->stdoutText;
	result->manifest.stderrText = result->stderrText;
	result->manifest.diagnostics = result->diagnostics;
	result->manifest.registeredOutputPaths = result->registeredOutputPaths;
	if (!error.trimmed().isEmpty()) {
		result->error = error.trimmed();
		result->manifest.errors.push_back(result->error);
	}
	refreshFileHashes(&result->manifest);
}

CompilerArtifactValidationReport validateCompletedArtifacts(CompilerRunResult* result, const CompilerRunCallbacks& callbacks)
{
	if (!result) {
		return {};
	}
	appendLog(&result->manifest, callbacks, QStringLiteral("info"), QCoreApplication::translate("VibeStudioCompilerRunner", "Validating compiler output artifacts."));
	const CompilerArtifactValidationReport report = validateCompilerArtifacts(result->manifest, callbacks.cancellationRequested);
	for (const QString& warning : report.warnings) {
		appendWarning(result, callbacks, warning);
	}
	for (const QString& error : report.errors) {
		appendError(result, callbacks, error);
	}
	return report;
}

CompilerRunResult runResolvedCommand(CompilerRunResult result, const CompilerRunRequest& request, const CompilerRunCallbacks& callbacks)
{
	result.manifest.startedUtc = QDateTime::currentDateTimeUtc();
	appendLog(&result.manifest, callbacks, QStringLiteral("info"), QCoreApplication::translate("VibeStudioCompilerRunner", "Compiler task started."));
	surfacePreflightFindings(&result, callbacks);

	if (callbacks.cancellationRequested && callbacks.cancellationRequested()) {
		result.cancelled = true;
		result.durationMs = 0;
		appendLog(&result.manifest, callbacks, QStringLiteral("warning"), QCoreApplication::translate("VibeStudioCompilerRunner", "Compiler task cancelled before process start."));
		finishResult(&result, OperationState::Cancelled);
		return result;
	}

	if (request.dryRun) {
		result.durationMs = 0;
		appendLog(&result.manifest, callbacks, QStringLiteral("info"), QCoreApplication::translate("VibeStudioCompilerRunner", "Dry run requested; compiler process was not started."));
		finishResult(&result, result.plan.isRunnable() ? successfulRunState(result.manifest) : result.plan.state());
		return result;
	}

	if (!result.plan.isRunnable()) {
		result.durationMs = 0;
		appendLog(&result.manifest, callbacks, QStringLiteral("error"), QCoreApplication::translate("VibeStudioCompilerRunner", "Compiler command is not runnable."));
		finishResult(&result, result.plan.state(), QCoreApplication::translate("VibeStudioCompilerRunner", "Compiler command is not runnable."));
		return result;
	}

	const QString workingDirectory = cleanAbsoluteDirectoryPath(result.plan.workingDirectory);
	if (workingDirectory.isEmpty() || !QFileInfo(workingDirectory).isDir()) {
		result.durationMs = 0;
		const QString error = workingDirectory.isEmpty()
			? QCoreApplication::translate("VibeStudioCompilerRunner", "Compiler working directory is required before process start.")
			: QCoreApplication::translate("VibeStudioCompilerRunner", "Compiler working directory does not exist: %1").arg(QDir::toNativeSeparators(workingDirectory));
		appendLog(&result.manifest, callbacks, QStringLiteral("error"), error);
		finishResult(&result, OperationState::Failed, error);
		return result;
	}
	result.plan.workingDirectory = workingDirectory;
	result.manifest.workingDirectory = workingDirectory;

	QTemporaryDir isolatedTempDir(compilerTempTemplatePath());
	if (!isolatedTempDir.isValid()) {
		result.durationMs = 0;
		const QString error = QCoreApplication::translate("VibeStudioCompilerRunner", "Failed to create an isolated compiler temporary directory.");
		appendLog(&result.manifest, callbacks, QStringLiteral("error"), error);
		finishResult(&result, OperationState::Failed, error);
		return result;
	}

	QProcess process;
	process.setProgram(result.plan.program);
	process.setArguments(result.plan.arguments);
	process.setWorkingDirectory(result.plan.workingDirectory);
	applyIsolatedTempEnvironment(&process, &result.manifest, isolatedTempDir.path());
	appendLog(&result.manifest, callbacks, QStringLiteral("info"), QCoreApplication::translate("VibeStudioCompilerRunner", "Using isolated compiler temporary directory: %1").arg(QDir::toNativeSeparators(isolatedTempDir.path())));

	const LeakFileSnapshot preRunLeakFiles = captureLeakFileSnapshot(result.manifest);

	QElapsedTimer timer;
	timer.start();
	process.start();
	if (!process.waitForStarted(5000)) {
		result.durationMs = timer.elapsed();
		appendLog(&result.manifest, callbacks, QStringLiteral("error"), QCoreApplication::translate("VibeStudioCompilerRunner", "Compiler process could not start."));
		finishResult(&result, OperationState::Failed, process.errorString());
		return result;
	}

	result.started = true;
	appendLog(&result.manifest, callbacks, QStringLiteral("info"), QCoreApplication::translate("VibeStudioCompilerRunner", "Process started: %1").arg(result.plan.commandLine));

	CompilerDiagnosticParser diagnosticParser;
	QByteArray stdoutBytes;
	QByteArray stderrBytes;
	QByteArray stdoutPending;
	QByteArray stderrPending;

	// Emit one log entry per complete line as it arrives so watchers see progress live, and parse
	// diagnostics incrementally instead of only after the process has exited.
	const auto handleLine = [&](const QString& rawLine, const QString& channel) {
		const QString line = stripTerminalEscapes(stripTrailingCarriageReturn(rawLine));
		if (line.trimmed().isEmpty()) {
			return;
		}
		CompilerDiagnostic diagnostic;
		const bool isDiagnostic = diagnosticParser.consume(line, channel, &diagnostic);
		if (isDiagnostic) {
			result.diagnostics.push_back(diagnostic);
			if (diagnostic.level == QStringLiteral("error")) {
				result.manifest.errors.push_back(diagnostic.message);
			} else {
				result.manifest.warnings.push_back(diagnostic.message);
			}
		}
		const QString level = isDiagnostic ? diagnostic.level : QStringLiteral("info");
		const QString message = channel == QStringLiteral("stderr") ? QStringLiteral("[stderr] %1").arg(line.trimmed()) : line.trimmed();
		appendLog(&result.manifest, callbacks, level, message);
	};

	// A pipe read returns whatever bytes happened to have arrived, so a chunk boundary can fall in
	// the middle of a multi-byte sequence. Buffer raw bytes and split on the 0x0A byte, which can
	// never appear inside a UTF-8 continuation byte nor inside a Windows DBCS trail byte; decoding
	// happens per complete line, and once more over the whole buffer at the end.
	const auto pumpChannel = [&](QByteArray* pending, QByteArray* captured, const QByteArray& chunk, const QString& channel) {
		if (chunk.isEmpty()) {
			return;
		}
		captured->append(chunk);
		pending->append(chunk);
		int newlineIndex = pending->indexOf('\n');
		while (newlineIndex >= 0) {
			const QByteArray line = pending->left(newlineIndex);
			pending->remove(0, newlineIndex + 1);
			handleLine(decodeToolOutput(line), channel);
			newlineIndex = pending->indexOf('\n');
		}
	};

	const auto pump = [&]() {
		pumpChannel(&stdoutPending, &stdoutBytes, process.readAllStandardOutput(), QStringLiteral("stdout"));
		pumpChannel(&stderrPending, &stderrBytes, process.readAllStandardError(), QStringLiteral("stderr"));
	};

	const int timeoutMs = std::max(1000, request.timeoutMs);
	while (!process.waitForFinished(100)) {
		pump();
		if (callbacks.cancellationRequested && callbacks.cancellationRequested()) {
			result.cancelled = true;
			process.kill();
			process.waitForFinished(1000);
			appendLog(&result.manifest, callbacks, QStringLiteral("warning"), QCoreApplication::translate("VibeStudioCompilerRunner", "Cancellation requested; compiler process was stopped."));
			break;
		}
		if (timer.elapsed() > timeoutMs) {
			result.timedOut = true;
			process.kill();
			process.waitForFinished(1000);
			appendLog(&result.manifest, callbacks, QStringLiteral("error"), QCoreApplication::translate("VibeStudioCompilerRunner", "Compiler process timed out after %1 ms.").arg(timeoutMs));
			break;
		}
	}

	result.durationMs = timer.elapsed();
	result.exitCode = process.exitCode();
	pump();
	if (!stdoutPending.trimmed().isEmpty()) {
		handleLine(decodeToolOutput(stdoutPending), QStringLiteral("stdout"));
	}
	if (!stderrPending.trimmed().isEmpty()) {
		handleLine(decodeToolOutput(stderrPending), QStringLiteral("stderr"));
	}
	stdoutPending.clear();
	stderrPending.clear();
	result.stdoutText = decodeToolOutput(stdoutBytes);
	result.stderrText = decodeToolOutput(stderrBytes);
	for (const CompilerDiagnostic& diagnostic : diagnosticParser.flush()) {
		result.diagnostics.push_back(diagnostic);
		result.manifest.errors.push_back(diagnostic.message);
		appendLog(&result.manifest, callbacks, diagnostic.level, diagnostic.rawLine);
	}
	if (!result.stdoutText.trimmed().isEmpty()) {
		appendLog(&result.manifest, callbacks, QStringLiteral("info"), QCoreApplication::translate("VibeStudioCompilerRunner", "Captured stdout (%1 bytes).").arg(stdoutBytes.size()));
	}
	if (!result.stderrText.trimmed().isEmpty()) {
		appendLog(&result.manifest, callbacks, QStringLiteral("info"), QCoreApplication::translate("VibeStudioCompilerRunner", "Captured stderr (%1 bytes).").arg(stderrBytes.size()));
	}
	enrichWithKnownIssues(&result, callbacks);

	// Diagnose the leak before the exit-code branches. With -leaktest qbsp writes the leak files and
	// then exits 1 on purpose (external/compilers/vibemap2/src/qbsp/outside.cc), so the one run the
	// user explicitly asked to fail on a leak used to be the one run that never explained it.
	detectLeak(&result, preRunLeakFiles, callbacks);

	if (result.cancelled) {
		finishResult(&result, OperationState::Cancelled);
		return result;
	}
	if (result.timedOut) {
		finishResult(&result, OperationState::Failed, QCoreApplication::translate("VibeStudioCompilerRunner", "Compiler process timed out."));
		return result;
	}
	if (process.exitStatus() != QProcess::NormalExit || result.exitCode != 0) {
		appendLog(&result.manifest, callbacks, QStringLiteral("error"), QCoreApplication::translate("VibeStudioCompilerRunner", "Compiler process finished with exit code %1.").arg(result.exitCode));
		finishResult(&result, OperationState::Failed);
		return result;
	}

	const CompilerArtifactValidationReport artifactReport = validateCompletedArtifacts(&result, callbacks);
	if (artifactReport.cancelled) {
		result.cancelled = true;
		finishResult(&result, OperationState::Cancelled);
		return result;
	}
	if (artifactReport.hasErrors()) {
		finishResult(&result, OperationState::Failed, QCoreApplication::translate("VibeStudioCompilerRunner", "Compiler output artifact validation failed."));
		return result;
	}

	result.registeredOutputPaths = request.registerOutputs ? existingOutputs(result.manifest.expectedOutputPaths + result.manifest.optionalOutputPaths) : QStringList();
	for (const QString& output : result.registeredOutputPaths) {
		appendLog(&result.manifest, callbacks, QStringLiteral("info"), QCoreApplication::translate("VibeStudioCompilerRunner", "Registered output: %1").arg(output));
	}
	appendLog(&result.manifest, callbacks, QStringLiteral("info"), QCoreApplication::translate("VibeStudioCompilerRunner", "Compiler process completed in %1 ms.").arg(result.durationMs));
	finishResult(&result, successfulRunState(result.manifest));
	return result;
}

} // namespace

CompilerRunResult runCompilerCommand(const CompilerRunRequest& request, const CompilerRunCallbacks& callbacks)
{
	CompilerRunResult result;
	result.plan = buildCompilerCommandPlan(request.command);
	result.manifest = compilerCommandManifestFromPlan(result.plan);
	result.manifestPath = request.manifestPath;
	result = runResolvedCommand(result, request, callbacks);
	if (!request.manifestPath.trimmed().isEmpty()) {
		QString error;
		if (!saveCompilerCommandManifest(result.manifest, request.manifestPath, &error)) {
			result.error = error;
			result.state = OperationState::Failed;
			result.manifest.state = OperationState::Failed;
			result.manifest.errors.push_back(error);
		}
	}
	return result;
}

CompilerRunResult rerunCompilerCommandManifest(const CompilerCommandManifest& manifest, const CompilerRunCallbacks& callbacks, const QString& manifestPath)
{
	CompilerManifestRerunRequest request;
	request.manifestPath = manifestPath;
	return rerunCompilerCommandManifest(manifest, request, callbacks);
}

CompilerRunResult rerunCompilerCommandManifest(const CompilerCommandManifest& manifest, const CompilerManifestRerunRequest& rerunRequest, const CompilerRunCallbacks& callbacks)
{
	const QString manifestPath = rerunRequest.manifestPath;
	CompilerRunResult result;
	result.manifest = manifest;
	result.manifest.manifestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	result.manifest.createdUtc = QDateTime::currentDateTimeUtc();
	result.manifest.taskLog.clear();
	// A rerun replays the stored command, not its outcome. appendWarning/appendError and finishResult
	// only ever append, so anything the previous execution observed would be re-logged by
	// surfacePreflightFindings, counted by manifestHasWarnings (pinning successfulRunState to
	// Warning) and written back out by saveCompilerCommandManifest as a failure that did not happen
	// in this run. preflightWarnings go too: they describe filesystem state at the original plan
	// time, and the rerun path has no request to re-run the preflight against.
	result.manifest.state = OperationState::Idle;
	result.manifest.startedUtc = {};
	result.manifest.finishedUtc = {};
	result.manifest.exitCode = -1;
	result.manifest.durationMs = -1;
	result.manifest.warnings.clear();
	result.manifest.errors.clear();
	result.manifest.knownIssueNotes.clear();
	result.manifest.knownIssueWarnings.clear();
	result.manifest.preflightWarnings.clear();
	result.manifest.diagnostics.clear();
	result.manifest.stdoutText.clear();
	result.manifest.stderrText.clear();
	result.manifest.registeredOutputPaths.clear();
	result.manifest.outputHashes.clear();
	result.plan.profileFound = compilerProfileForId(manifest.profileId, &result.plan.profile);
	result.plan.toolFound = !manifest.toolId.trimmed().isEmpty();
	result.plan.executableAvailable = QFileInfo(manifest.program).isFile();
	result.plan.program = manifest.program;
	result.plan.arguments = manifest.arguments;
	result.plan.commandLine = manifest.commandLine.isEmpty() ? compilerCommandLineText(manifest.program, manifest.arguments) : manifest.commandLine;
	result.plan.workingDirectory = manifest.workingDirectory;
	result.plan.inputPath = manifest.inputPaths.value(0);
	result.plan.expectedOutputPath = manifest.expectedOutputPaths.value(0);
	if (!result.plan.executableAvailable) {
		result.plan.errors << QCoreApplication::translate("VibeStudioCompilerRunner", "Manifest program no longer exists.");
	}
	addManifestProvenanceWarnings(&result, callbacks);

	CompilerRunRequest request;
	request.dryRun = rerunRequest.dryRun;
	request.registerOutputs = rerunRequest.registerOutputs;
	request.timeoutMs = rerunRequest.timeoutMs;
	request.manifestPath = manifestPath;
	result.manifestPath = manifestPath;
	result = runResolvedCommand(result, request, callbacks);
	if (!manifestPath.trimmed().isEmpty()) {
		QString error;
		if (!saveCompilerCommandManifest(result.manifest, manifestPath, &error)) {
			result.error = error;
			result.state = OperationState::Failed;
			result.manifest.state = OperationState::Failed;
			result.manifest.errors.push_back(error);
		}
	}
	return result;
}

QString compilerRunResultText(const CompilerRunResult& result)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioCompilerRunner", "Compiler run result");
	lines << QCoreApplication::translate("VibeStudioCompilerRunner", "State: %1").arg(operationStateId(result.state));
	lines << QCoreApplication::translate("VibeStudioCompilerRunner", "Started: %1").arg(result.started ? QCoreApplication::translate("VibeStudioCompilerRunner", "yes") : QCoreApplication::translate("VibeStudioCompilerRunner", "no"));
	lines << QCoreApplication::translate("VibeStudioCompilerRunner", "Exit code: %1").arg(result.exitCode >= 0 ? QString::number(result.exitCode) : QCoreApplication::translate("VibeStudioCompilerRunner", "not run"));
	lines << QCoreApplication::translate("VibeStudioCompilerRunner", "Duration: %1 ms").arg(result.durationMs >= 0 ? QString::number(result.durationMs) : QCoreApplication::translate("VibeStudioCompilerRunner", "not run"));
	lines << QCoreApplication::translate("VibeStudioCompilerRunner", "Command line: %1").arg(result.manifest.commandLine);
	if (!result.registeredOutputPaths.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerRunner", "Registered outputs");
		for (const QString& output : result.registeredOutputPaths) {
			lines << QStringLiteral("- %1").arg(QDir::toNativeSeparators(output));
		}
	}
	if (result.leakDetected) {
		lines << QCoreApplication::translate("VibeStudioCompilerRunner", "Leak: yes");
		if (!result.leakOccupantClassname.isEmpty()) {
			lines << QCoreApplication::translate("VibeStudioCompilerRunner", "Leaked entity: %1").arg(result.leakOccupantClassname);
		}
		if (!result.leakPointText.isEmpty()) {
			lines << QCoreApplication::translate("VibeStudioCompilerRunner", "Leak position: %1").arg(result.leakPointText);
		}
		if (!result.leakPointFilePath.isEmpty()) {
			lines << QCoreApplication::translate("VibeStudioCompilerRunner", "Leak point file: %1").arg(QDir::toNativeSeparators(result.leakPointFilePath));
		}
	}
	if (!result.error.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerRunner", "Error: %1").arg(result.error);
	}
	lines << QString();
	lines << compilerCommandManifestText(result.manifest);
	return lines.join('\n');
}

} // namespace vibestudio
