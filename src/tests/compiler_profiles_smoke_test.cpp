#include "core/compiler_profiles.h"
#include "core/compiler_runner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>

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

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	const QStringList appArgs = QCoreApplication::arguments();
	if (appArgs.contains(QStringLiteral("--fake-compiler"))) {
		const QString outputPath = appArgs.last();
		if (!outputPath.endsWith(QStringLiteral("--fake-compiler"))) {
			touchFile(outputPath);
		}
		std::cout << "maps/start.map:7: warning: fake compiler warning\n";
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
	const QString mapPath = root.filePath(QStringLiteral("maps/start.map"));
	if (!touchFile(mapPath)) {
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
	if (!plan.expectedOutputPath.endsWith(QStringLiteral("start.bsp"))) {
		return fail("Expected qbsp default output path.");
	}
	if (!plan.commandLine.contains(QStringLiteral("start.map"))) {
		return fail("Expected command line to include map input.");
	}
	const vibestudio::CompilerCommandManifest manifest = vibestudio::compilerCommandManifestFromPlan(plan);
	if (manifest.taskLog.isEmpty() || manifest.expectedOutputPaths.isEmpty()) {
		return fail("Expected manifest task log and output paths.");
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

	vibestudio::CompilerRunRequest runRequest;
	runRequest.command = request;
	runRequest.command.outputPath = root.filePath(QStringLiteral("maps/start-run.bsp"));
	runRequest.command.extraArguments = {QStringLiteral("--fake-compiler")};
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
	if (runResult.registeredOutputPaths.isEmpty() || !QFile::exists(runRequest.command.outputPath)) {
		return fail("Expected compiler output registration.");
	}
	if (!QFile::exists(runRequest.manifestPath)) {
		return fail("Expected compiler run manifest save.");
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
	if (lightPlan.warnings.isEmpty()) {
		return fail("Expected in-place output warning for light.");
	}
	if (!lightPlan.commandLine.contains(QStringLiteral("start.bsp"))) {
		return fail("Expected light command line to include BSP input.");
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
