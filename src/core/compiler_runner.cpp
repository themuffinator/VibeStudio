#include "core/compiler_runner.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>

namespace vibestudio {

namespace {

QString runnerText(const char* source)
{
	return QCoreApplication::translate("VibeStudioCompilerRunner", source);
}

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

QString diagnosticLevelForLine(const QString& line)
{
	const QString lower = line.toLower();
	if (lower.contains(QStringLiteral("fatal")) || lower.contains(QStringLiteral("error"))) {
		return QStringLiteral("error");
	}
	if (lower.contains(QStringLiteral("warn"))) {
		return QStringLiteral("warning");
	}
	return {};
}

CompilerDiagnostic diagnosticFromLine(const QString& line)
{
	CompilerDiagnostic diagnostic;
	diagnostic.level = diagnosticLevelForLine(line);
	diagnostic.rawLine = line.trimmed();
	diagnostic.message = diagnostic.rawLine;
	if (diagnostic.level.isEmpty()) {
		return diagnostic;
	}

	static const QRegularExpression pathPattern(QStringLiteral(R"(((?:[A-Za-z]:)?[^:\n\r\t ]+\.(?:map|bsp|wad|shader|cfg|script|txt|c|cpp|h|hpp))(?:[:(](\d+))?(?:[:,](\d+))?)"));
	const QRegularExpressionMatch match = pathPattern.match(line);
	if (match.hasMatch()) {
		diagnostic.filePath = QDir::cleanPath(match.captured(1));
		diagnostic.line = match.captured(2).toInt();
		diagnostic.column = match.captured(3).toInt();
	}
	return diagnostic;
}

QVector<CompilerDiagnostic> parseCompilerDiagnostics(const QString& stdoutText, const QString& stderrText)
{
	QVector<CompilerDiagnostic> diagnostics;
	const QStringList lines = QStringLiteral("%1\n%2").arg(stdoutText, stderrText).split('\n');
	for (const QString& line : lines) {
		CompilerDiagnostic diagnostic = diagnosticFromLine(line);
		if (!diagnostic.level.isEmpty()) {
			diagnostics.push_back(diagnostic);
		}
	}
	return diagnostics;
}

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

CompilerRunResult runResolvedCommand(CompilerRunResult result, const CompilerRunRequest& request, const CompilerRunCallbacks& callbacks)
{
	result.manifest.startedUtc = QDateTime::currentDateTimeUtc();
	appendLog(&result.manifest, callbacks, QStringLiteral("info"), runnerText("Compiler task started."));

	if (callbacks.cancellationRequested && callbacks.cancellationRequested()) {
		result.cancelled = true;
		result.durationMs = 0;
		appendLog(&result.manifest, callbacks, QStringLiteral("warning"), runnerText("Compiler task cancelled before process start."));
		finishResult(&result, OperationState::Cancelled);
		return result;
	}

	if (request.dryRun) {
		result.durationMs = 0;
		appendLog(&result.manifest, callbacks, QStringLiteral("info"), runnerText("Dry run requested; compiler process was not started."));
		finishResult(&result, result.plan.isRunnable() ? OperationState::Completed : result.plan.state());
		return result;
	}

	if (!result.plan.isRunnable()) {
		result.durationMs = 0;
		appendLog(&result.manifest, callbacks, QStringLiteral("error"), runnerText("Compiler command is not runnable."));
		finishResult(&result, result.plan.state(), runnerText("Compiler command is not runnable."));
		return result;
	}

	QProcess process;
	process.setProgram(result.plan.program);
	process.setArguments(result.plan.arguments);
	if (!result.plan.workingDirectory.trimmed().isEmpty()) {
		process.setWorkingDirectory(result.plan.workingDirectory);
	}

	QElapsedTimer timer;
	timer.start();
	process.start();
	if (!process.waitForStarted(5000)) {
		result.durationMs = timer.elapsed();
		appendLog(&result.manifest, callbacks, QStringLiteral("error"), runnerText("Compiler process could not start."));
		finishResult(&result, OperationState::Failed, process.errorString());
		return result;
	}

	result.started = true;
	appendLog(&result.manifest, callbacks, QStringLiteral("info"), runnerText("Process started: %1").arg(result.plan.commandLine));
	const int timeoutMs = std::max(1000, request.timeoutMs);
	while (!process.waitForFinished(100)) {
		if (callbacks.cancellationRequested && callbacks.cancellationRequested()) {
			result.cancelled = true;
			process.kill();
			process.waitForFinished(1000);
			appendLog(&result.manifest, callbacks, QStringLiteral("warning"), runnerText("Cancellation requested; compiler process was stopped."));
			break;
		}
		if (timer.elapsed() > timeoutMs) {
			result.timedOut = true;
			process.kill();
			process.waitForFinished(1000);
			appendLog(&result.manifest, callbacks, QStringLiteral("error"), runnerText("Compiler process timed out after %1 ms.").arg(timeoutMs));
			break;
		}
	}

	result.durationMs = timer.elapsed();
	result.exitCode = process.exitCode();
	result.stdoutText = QString::fromLocal8Bit(process.readAllStandardOutput());
	result.stderrText = QString::fromLocal8Bit(process.readAllStandardError());
	result.diagnostics = parseCompilerDiagnostics(result.stdoutText, result.stderrText);
	for (const CompilerDiagnostic& diagnostic : result.diagnostics) {
		if (diagnostic.level == QStringLiteral("error")) {
			result.manifest.errors.push_back(diagnostic.message);
		} else {
			result.manifest.warnings.push_back(diagnostic.message);
		}
		appendLog(&result.manifest, callbacks, diagnostic.level, diagnostic.rawLine);
	}
	if (!result.stdoutText.trimmed().isEmpty()) {
		appendLog(&result.manifest, callbacks, QStringLiteral("info"), runnerText("Captured stdout (%1 bytes).").arg(result.stdoutText.toUtf8().size()));
	}
	if (!result.stderrText.trimmed().isEmpty()) {
		appendLog(&result.manifest, callbacks, QStringLiteral("warning"), runnerText("Captured stderr (%1 bytes).").arg(result.stderrText.toUtf8().size()));
	}

	if (result.cancelled) {
		finishResult(&result, OperationState::Cancelled);
		return result;
	}
	if (result.timedOut) {
		finishResult(&result, OperationState::Failed, runnerText("Compiler process timed out."));
		return result;
	}
	if (process.exitStatus() != QProcess::NormalExit || result.exitCode != 0) {
		appendLog(&result.manifest, callbacks, QStringLiteral("error"), runnerText("Compiler process finished with exit code %1.").arg(result.exitCode));
		finishResult(&result, OperationState::Failed);
		return result;
	}

	result.registeredOutputPaths = request.registerOutputs ? existingOutputs(result.manifest.expectedOutputPaths) : QStringList();
	for (const QString& output : result.registeredOutputPaths) {
		appendLog(&result.manifest, callbacks, QStringLiteral("info"), runnerText("Registered output: %1").arg(output));
	}
	appendLog(&result.manifest, callbacks, QStringLiteral("info"), runnerText("Compiler process completed in %1 ms.").arg(result.durationMs));
	finishResult(&result, result.manifest.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning);
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
	CompilerRunResult result;
	result.manifest = manifest;
	result.manifest.manifestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	result.manifest.createdUtc = QDateTime::currentDateTimeUtc();
	result.manifest.taskLog.clear();
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
		result.plan.errors << runnerText("Manifest program no longer exists.");
	}

	CompilerRunRequest request;
	request.dryRun = false;
	request.registerOutputs = true;
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
	lines << runnerText("Compiler run result");
	lines << runnerText("State: %1").arg(operationStateId(result.state));
	lines << runnerText("Started: %1").arg(result.started ? runnerText("yes") : runnerText("no"));
	lines << runnerText("Exit code: %1").arg(result.exitCode >= 0 ? QString::number(result.exitCode) : runnerText("not run"));
	lines << runnerText("Duration: %1 ms").arg(result.durationMs >= 0 ? QString::number(result.durationMs) : runnerText("not run"));
	lines << runnerText("Command line: %1").arg(result.manifest.commandLine);
	if (!result.registeredOutputPaths.isEmpty()) {
		lines << runnerText("Registered outputs");
		for (const QString& output : result.registeredOutputPaths) {
			lines << QStringLiteral("- %1").arg(QDir::toNativeSeparators(output));
		}
	}
	if (!result.error.isEmpty()) {
		lines << runnerText("Error: %1").arg(result.error);
	}
	lines << QString();
	lines << compilerCommandManifestText(result.manifest);
	return lines.join('\n');
}

} // namespace vibestudio
