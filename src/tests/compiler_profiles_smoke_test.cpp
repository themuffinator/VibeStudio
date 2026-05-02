#include "core/compiler_profiles.h"
#include "core/compiler_runner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

bool touchFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
	file.write("fixture");
	return true;
}

bool writeTextFile(const QString& path, const QByteArray& text)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	file.write(text);
	return true;
}

bool writeMinimalQuakeBsp(const QString& path)
{
	QByteArray bytes(4 + 15 * 8, '\0');
	qToLittleEndian<qint32>(29, reinterpret_cast<uchar*>(bytes.data()));
	return writeTextFile(path, bytes);
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	const QStringList appArgs = QCoreApplication::arguments();
	if (appArgs.contains(QStringLiteral("--fake-compiler"))) {
		const QString outputPath = appArgs.last();
		if (!outputPath.endsWith(QStringLiteral("--fake-compiler"))) {
			if (appArgs.contains(QStringLiteral("--fake-empty-output"))) {
				writeTextFile(outputPath, QByteArray());
			} else if (outputPath.endsWith(QStringLiteral(".bsp"), Qt::CaseInsensitive)) {
				writeMinimalQuakeBsp(outputPath);
			} else {
				touchFile(outputPath);
			}
		}
		if (appArgs.contains(QStringLiteral("--print-temp"))) {
			std::cout << "TMP=" << qgetenv("TMP").constData() << "\n";
			std::cout << "TMPDIR=" << qgetenv("TMPDIR").constData() << "\n";
		}
		std::cout << "\"maps/start.final.map\":7: warning: fake compiler warning\n";
		return EXIT_SUCCESS;
	}

	const QVector<vibestudio::CompilerProfileDescriptor> profiles = vibestudio::compilerProfileDescriptors();
	if (profiles.size() < 7) {
		return fail("Expected ericw, Doom node-builder, and q3map2 profiles.");
	}
	if (!vibestudio::compilerProfileIds().contains(QStringLiteral("ericw-qbsp"))) {
		return fail("Expected ericw-qbsp profile id.");
	}
	if (!vibestudio::compilerProfileIds().contains(QStringLiteral("zdbsp-nodes"))) {
		return fail("Expected zdbsp-nodes profile id.");
	}
	if (!vibestudio::compilerProfileIds().contains(QStringLiteral("q3map2-probe"))) {
		return fail("Expected q3map2-probe profile id.");
	}

	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected temporary workspace.");
	}
	QDir root(tempDir.path());
	if (!root.mkpath(QStringLiteral("external/compilers/ericw-tools/build/bin")) || !root.mkpath(QStringLiteral("maps"))) {
		return fail("Expected fake workspace directories.");
	}

#if defined(Q_OS_WIN)
	const QString qbspName = QStringLiteral("qbsp.exe");
#else
	const QString qbspName = QStringLiteral("qbsp");
#endif
	if (!touchFile(root.filePath(QStringLiteral("external/compilers/ericw-tools/build/bin/%1").arg(qbspName)))) {
		return fail("Expected fake qbsp executable.");
	}
	const QString mapPath = root.filePath(QStringLiteral("maps/start.final.map"));
	if (!writeTextFile(mapPath, R"MAP(
{
"classname" "worldspawn"
"wad" "C:\Users\Mapper\quake\id1\gfx.wad"
}
{
"classname" "misc_external_map"
"_external_map" "..\prefabs\room.map"
}
)MAP")) {
		return fail("Expected fake map file.");
	}

	vibestudio::CompilerCommandRequest request;
	request.profileId = QStringLiteral("ericw-qbsp");
	request.inputPath = mapPath;
	request.workspaceRootPath = tempDir.path();
	const vibestudio::CompilerCommandPlan plan = vibestudio::buildCompilerCommandPlan(request);
	if (!plan.profileFound || !plan.toolFound) {
		return fail("Expected profile and tool to resolve.");
	}
	if (!plan.isRunnable()) {
		return fail("Expected fake qbsp plan to be runnable.");
	}
	if (!plan.expectedOutputPath.endsWith(QStringLiteral("start.final.bsp"))) {
		return fail("Expected qbsp default output path to preserve dotted filename stem.");
	}
	if (!plan.commandLine.contains(QStringLiteral("start.final.map"))) {
		return fail("Expected command line to include map input.");
	}
	if (plan.knownIssueWarnings.isEmpty()) {
		return fail("Expected ericw known-issue warnings in command plan.");
	}
	if (plan.preflightWarnings.isEmpty() || !plan.preflightWarnings.join('\n').contains(QStringLiteral("#194"))) {
		return fail("Expected ericw map preflight warnings in command plan.");
	}
	const vibestudio::CompilerCommandManifest manifest = vibestudio::compilerCommandManifestFromPlan(plan);
	if (manifest.taskLog.isEmpty() || manifest.expectedOutputPaths.isEmpty()) {
		return fail("Expected manifest task log and output paths.");
	}
	if (manifest.knownIssueWarnings.isEmpty() || manifest.preflightWarnings.isEmpty()) {
		return fail("Expected manifest to carry known-issue and preflight warnings.");
	}
	if (!vibestudio::compilerCommandManifestJson(manifest).contains(QStringLiteral("taskLog"))) {
		return fail("Expected manifest JSON task log.");
	}
	const QString manifestPath = root.filePath(QStringLiteral("build/qbsp-manifest.json"));
	QString saveError;
	if (!vibestudio::saveCompilerCommandManifest(manifest, manifestPath, &saveError) || !QFile::exists(manifestPath)) {
		return fail("Expected manifest save to create a JSON file.");
	}
	vibestudio::CompilerCommandManifest loadedManifest;
	if (!vibestudio::loadCompilerCommandManifest(manifestPath, &loadedManifest, &saveError) || loadedManifest.profileId != manifest.profileId) {
		return fail("Expected manifest load to round-trip profile id.");
	}

	vibestudio::CompilerRunRequest dryPreflightRequest;
	dryPreflightRequest.command = request;
	dryPreflightRequest.dryRun = true;
	QVector<vibestudio::CompilerTaskLogEntry> dryPreflightLog;
	vibestudio::CompilerRunCallbacks dryPreflightCallbacks;
	dryPreflightCallbacks.logEntry = [&dryPreflightLog](const vibestudio::CompilerTaskLogEntry& entry) {
		dryPreflightLog.push_back(entry);
	};
	const vibestudio::CompilerRunResult dryPreflightResult = vibestudio::runCompilerCommand(dryPreflightRequest, dryPreflightCallbacks);
	bool sawKnownIssueWarning = false;
	bool sawCategorizedPreflightWarning = false;
	for (const vibestudio::CompilerTaskLogEntry& entry : dryPreflightLog) {
		sawKnownIssueWarning = sawKnownIssueWarning || entry.message.contains(QStringLiteral("Known issue warning"));
		sawCategorizedPreflightWarning = sawCategorizedPreflightWarning || entry.message.contains(QStringLiteral("Preflight warning"));
	}
	if (dryPreflightResult.state != vibestudio::OperationState::Warning || !sawKnownIssueWarning || !sawCategorizedPreflightWarning) {
		return fail("Expected categorized ericw warnings to surface before a dry-run process launch.");
	}

	vibestudio::CompilerRunRequest runRequest;
	runRequest.command = request;
	runRequest.command.outputPath = root.filePath(QStringLiteral("maps/start-run.bsp"));
	runRequest.command.extraArguments = {QStringLiteral("--fake-compiler"), QStringLiteral("--print-temp")};
	runRequest.command.executableOverrides.push_back({QStringLiteral("ericw-qbsp"), QCoreApplication::applicationFilePath()});
	runRequest.manifestPath = root.filePath(QStringLiteral("build/qbsp-run-manifest.json"));
	runRequest.registerOutputs = true;
	const vibestudio::CompilerRunResult runResult = vibestudio::runCompilerCommand(runRequest);
	if (runResult.state != vibestudio::OperationState::Warning || !runResult.started) {
		return fail("Expected fake compiler run to complete with parsed warning.");
	}
	if (runResult.stdoutText.isEmpty() || runResult.diagnostics.isEmpty()) {
		return fail("Expected compiler stdout and parsed diagnostics.");
	}
	if (!runResult.stdoutText.contains(QStringLiteral("vibestudio-compiler-"))) {
		return fail("Expected runner to provide an isolated compiler temp directory.");
	}
	if (!runResult.diagnostics.first().filePath.endsWith(QStringLiteral("start.final.map"))) {
		return fail("Expected diagnostics to preserve dotted filenames.");
	}
	if (runResult.registeredOutputPaths.isEmpty() || !QFile::exists(runRequest.command.outputPath)) {
		return fail("Expected compiler output registration.");
	}
	if (!QFile::exists(runRequest.manifestPath)) {
		return fail("Expected compiler run manifest save.");
	}

	vibestudio::CompilerRunRequest badArtifactRequest = runRequest;
	badArtifactRequest.command.outputPath = root.filePath(QStringLiteral("maps/start-empty.bsp"));
	badArtifactRequest.command.extraArguments = {QStringLiteral("--fake-compiler"), QStringLiteral("--fake-empty-output")};
	badArtifactRequest.manifestPath.clear();
	const vibestudio::CompilerRunResult badArtifactResult = vibestudio::runCompilerCommand(badArtifactRequest);
	if (badArtifactResult.state != vibestudio::OperationState::Failed || !badArtifactResult.manifest.errors.join('\n').contains(QStringLiteral("artifact validation"), Qt::CaseInsensitive)) {
		return fail("Expected post-run artifact validation to fail empty BSP outputs after a successful process exit.");
	}

	vibestudio::CompilerRunRequest invalidWorkingDirRequest = runRequest;
	invalidWorkingDirRequest.command.workingDirectory = root.filePath(QStringLiteral("missing-workdir"));
	invalidWorkingDirRequest.manifestPath.clear();
	const vibestudio::CompilerRunResult invalidWorkingDirResult = vibestudio::runCompilerCommand(invalidWorkingDirRequest);
	if (invalidWorkingDirResult.started || invalidWorkingDirResult.state != vibestudio::OperationState::Failed) {
		return fail("Expected missing working directory to stop the process before launch.");
	}
	if (invalidWorkingDirResult.manifest.errors.isEmpty() || !invalidWorkingDirResult.manifest.errors.last().contains(QStringLiteral("working directory"))) {
		return fail("Expected missing working directory error to be surfaced.");
	}

	vibestudio::CompilerCommandRequest lightRequest;
	lightRequest.profileId = QStringLiteral("ericw-light");
	lightRequest.inputPath = root.filePath(QStringLiteral("maps/start.bsp"));
	lightRequest.outputPath = root.filePath(QStringLiteral("maps/start-lit.bsp"));
	lightRequest.workspaceRootPath = tempDir.path();
	if (!touchFile(lightRequest.inputPath)) {
		return fail("Expected fake BSP file.");
	}
	const vibestudio::CompilerCommandPlan lightPlan = vibestudio::buildCompilerCommandPlan(lightRequest);
	if (!lightPlan.profileFound || !lightPlan.toolFound) {
		return fail("Expected light profile and tool to resolve.");
	}
	if (lightPlan.expectedOutputPath != QDir::cleanPath(lightRequest.inputPath)) {
		return fail("Expected in-place light profile to keep the input BSP as the expected artifact.");
	}
	if (lightPlan.commandLine.contains(QStringLiteral("start-lit.bsp"))) {
		return fail("Expected in-place light command line not to include unsupported custom output path.");
	}
	if (lightPlan.warnings.isEmpty()) {
		return fail("Expected in-place output warning for light.");
	}
	if (!lightPlan.commandLine.contains(QStringLiteral("start.bsp"))) {
		return fail("Expected light command line to include BSP input.");
	}

	vibestudio::CompilerRunRequest warningRequest;
	warningRequest.command = lightRequest;
	warningRequest.command.executableOverrides.push_back({QStringLiteral("ericw-light"), QCoreApplication::applicationFilePath()});
	warningRequest.dryRun = true;
	QVector<vibestudio::CompilerTaskLogEntry> warningLog;
	vibestudio::CompilerRunCallbacks warningCallbacks;
	warningCallbacks.logEntry = [&warningLog](const vibestudio::CompilerTaskLogEntry& entry) {
		warningLog.push_back(entry);
	};
	const vibestudio::CompilerRunResult warningResult = vibestudio::runCompilerCommand(warningRequest, warningCallbacks);
	bool sawPlanWarning = false;
	for (const vibestudio::CompilerTaskLogEntry& entry : warningLog) {
		sawPlanWarning = sawPlanWarning || entry.message.contains(QStringLiteral("Plan warning"));
	}
	if (warningResult.started || warningResult.state != vibestudio::OperationState::Warning || !sawPlanWarning) {
		return fail("Expected compiler plan warnings to be surfaced before process launch.");
	}

	vibestudio::CompilerRunRequest lightRunRequest;
	lightRunRequest.command = lightRequest;
	lightRunRequest.command.extraArguments = {QStringLiteral("--fake-compiler")};
	lightRunRequest.command.executableOverrides.push_back({QStringLiteral("ericw-light"), QCoreApplication::applicationFilePath()});
	lightRunRequest.registerOutputs = true;
	const vibestudio::CompilerRunResult lightRunResult = vibestudio::runCompilerCommand(lightRunRequest);
	if (!lightRunResult.started || lightRunResult.state != vibestudio::OperationState::Warning) {
		return fail("Expected in-place light run with unsupported custom output to complete with warnings.");
	}
	if (!lightRunResult.registeredOutputPaths.contains(QDir::cleanPath(lightRequest.inputPath))) {
		return fail("Expected in-place light run to register the updated input BSP.");
	}
	if (QFile::exists(lightRequest.outputPath)) {
		return fail("Expected unsupported custom light output path not to be created or registered.");
	}

	const QString wadPath = root.filePath(QStringLiteral("maps/doom.wad"));
	if (!touchFile(wadPath)) {
		return fail("Expected fake WAD file.");
	}
	vibestudio::CompilerCommandRequest zdbspRequest;
	zdbspRequest.profileId = QStringLiteral("zdbsp-nodes");
	zdbspRequest.inputPath = wadPath;
	zdbspRequest.outputPath = root.filePath(QStringLiteral("maps/doom-nodes.wad"));
	zdbspRequest.workspaceRootPath = tempDir.path();
	const vibestudio::CompilerCommandPlan zdbspPlan = vibestudio::buildCompilerCommandPlan(zdbspRequest);
	if (!zdbspPlan.profileFound || !zdbspPlan.toolFound) {
		return fail("Expected ZDBSP profile and tool to resolve.");
	}
	if (!zdbspPlan.errors.isEmpty()) {
		return fail("Expected ZDBSP plan to be valid even when executable discovery only warns.");
	}
	if (!zdbspPlan.commandLine.contains(QStringLiteral("doom.wad")) || !zdbspPlan.commandLine.contains(QStringLiteral("doom-nodes.wad"))) {
		return fail("Expected ZDBSP command line to include WAD input and output.");
	}

	vibestudio::CompilerCommandRequest q3map2ProbeRequest;
	q3map2ProbeRequest.profileId = QStringLiteral("q3map2-probe");
	q3map2ProbeRequest.workspaceRootPath = tempDir.path();
	const vibestudio::CompilerCommandPlan q3map2ProbePlan = vibestudio::buildCompilerCommandPlan(q3map2ProbeRequest);
	if (!q3map2ProbePlan.profileFound || !q3map2ProbePlan.toolFound) {
		return fail("Expected q3map2 probe profile and tool to resolve.");
	}
	if (!q3map2ProbePlan.errors.isEmpty()) {
		return fail("Expected q3map2 probe to allow no input path.");
	}
	if (!q3map2ProbePlan.arguments.contains(QStringLiteral("-help"))) {
		return fail("Expected q3map2 probe command to include help argument.");
	}

	vibestudio::CompilerCommandRequest unknownRequest;
	unknownRequest.profileId = QStringLiteral("unknown");
	const vibestudio::CompilerCommandPlan unknownPlan = vibestudio::buildCompilerCommandPlan(unknownRequest);
	if (unknownPlan.errors.isEmpty()) {
		return fail("Expected unknown profile error.");
	}

	return EXIT_SUCCESS;
}
