#include "core/compiler_profiles.h"
#include "core/compiler_runner.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

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

// IBSP v46: a 4-byte ident, a 4-byte version and 18 directory entries of 8 bytes
// (external/compilers/vibemap3/tools/quake3/q3map2/q3map2.h, dheader_t).
bool writeMinimalQuake3Bsp(const QString& path)
{
	QByteArray bytes(8 + 18 * 8, '\0');
	bytes.replace(0, 4, "IBSP");
	qToLittleEndian<qint32>(46, reinterpret_cast<uchar*>(bytes.data()) + 4);
	return writeTextFile(path, bytes);
}

bool backdateFile(const QString& path, int secondsAgo)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadWrite)) {
		return false;
	}
	return file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-secondsAgo), QFileDevice::FileModificationTime);
}

QString siblingPath(const QString& path, const QString& suffix)
{
	const QFileInfo info(path);
	return info.absolutePath() + QStringLiteral("/") + info.completeBaseName() + suffix;
}

QString fakeOutputPath(const QStringList& appArgs)
{
	const int outputFlagIndex = appArgs.indexOf(QStringLiteral("-o"));
	if (outputFlagIndex >= 0 && outputFlagIndex + 1 < appArgs.size()) {
		return appArgs.at(outputFlagIndex + 1);
	}
	const QString last = appArgs.last();
	if (last.startsWith(QStringLiteral("--"))) {
		return {};
	}
	return last;
}

int runFakeCompiler(const QStringList& appArgs)
{
	if (appArgs.contains(QStringLiteral("--fake-q3-leak"))) {
		// q3map2 removes "<source>.lin" at startup, LeakFile() rewrites it when the map leaks, and the
		// process still exits 0. The output carries a "******* leaked *******" banner and an
		// "Entity <n>, Brush <m>: Entity leaked" line with no classname and no coordinates
		// (external/compilers/vibemap3/tools/quake3/q3map2/bsp.cpp, leakfile.cpp and
		// tools/quake3/common/inout.cpp).
		QString sourcePath;
		for (const QString& argument : appArgs) {
			if (argument.endsWith(QStringLiteral(".map"), Qt::CaseInsensitive)) {
				sourcePath = argument;
			}
		}
		writeTextFile(siblingPath(sourcePath, QStringLiteral(".lin")), QByteArray("0 0 0\n"));
		writeMinimalQuake3Bsp(siblingPath(sourcePath, QStringLiteral(".bsp")));
		std::cout << "**********************\n";
		std::cout << "******* leaked *******\n";
		std::cout << "**********************\n";
		std::cout << "Entity 3, Brush 0: Entity leaked\n" << std::flush;
		return EXIT_SUCCESS;
	}

	const QString outputPath = fakeOutputPath(appArgs);
	if (!outputPath.isEmpty()) {
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

	if (appArgs.contains(QStringLiteral("--fake-stream"))) {
		std::cout << "STREAM-LINE-1\n" << std::flush;
		std::this_thread::sleep_for(std::chrono::milliseconds(1500));
		std::cout << "STREAM-LINE-2\n" << std::flush;
		return EXIT_SUCCESS;
	}

	if (appArgs.contains(QStringLiteral("--fake-fatal"))) {
		// ericw-tools common/log.cc and q3map2 tools/quake3/common/inout.cpp both print a banner
		// line followed by the real message.
		std::cout << "************ ERROR ************\r\n";
		std::cout << "Failed to open maps/missing.map: No such file\r\n" << std::flush;
		return 1;
	}

	if (appArgs.contains(QStringLiteral("--fake-leak"))) {
		const QString leakPath = QFileInfo(outputPath).absolutePath() + QStringLiteral("/") + QFileInfo(outputPath).completeBaseName() + QStringLiteral(".pts");
		writeTextFile(leakPath, QByteArray("0 0 0\n"));
		std::cout << "WARNING: Reached occupant \"info_player_start\" at (32 64 -16), no filling performed.\n";
		std::cout << "Leak file written to " << leakPath.toLocal8Bit().constData() << "\n" << std::flush;
		return EXIT_SUCCESS;
	}

	if (appArgs.contains(QStringLiteral("--fake-leaktest"))) {
		// With -leaktest qbsp writes the leak files first and then aborts with exit code 1 on purpose
		// (external/compilers/vibemap2/src/qbsp/outside.cc).
		writeTextFile(siblingPath(outputPath, QStringLiteral(".pts")), QByteArray("0 0 0\n"));
		std::cout << "WARNING: Reached occupant \"info_player_start\" at (32 64 -16), no filling performed.\n";
		std::cout << "Aborting because -leaktest was used.\n" << std::flush;
		return 1;
	}

	if (appArgs.contains(QStringLiteral("--fake-ericw-diagnostics"))) {
		std::cout << "---- light / ericw-tools v2.0.0-alpha ----\n";
		std::cout << "WARNING: maps/start.final.map[line 12]: brush bounds out of range\n";
		std::cout << "WARNING: 34: microbrush\n";
		std::cout << "0 errors, 0 warnings\n";
		std::cout << "Error count: 0\n";
		std::cout << "no warnings were produced\n";
		std::cout << "Aborting because -leaktest was used is not what happened here\n" << std::flush;
		std::cerr << "WARNING: stderr side warning\n" << std::flush;
		return EXIT_SUCCESS;
	}

	if (appArgs.contains(QStringLiteral("--fake-colored"))) {
		// ericw-tools 2 colours its console output with ANSI escape sequences.
		std::cout << "\x1b[0m\x1b[33mimg::ConvertTextures: WARNING: invalid size data for brick_wall\x1b[0m\n";
		std::cout << "\x1b[33mWARNING: 34: microbrush\x1b[0m\n";
		std::cout << "\x1b]0;qbsp\x07Processing hull 0...\n" << std::flush;
		return EXIT_SUCCESS;
	}

	if (appArgs.contains(QStringLiteral("--fake-quiet"))) {
		return EXIT_SUCCESS;
	}

	std::cout << "\"maps/start.final.map\":7: warning: fake compiler warning\n";
	return EXIT_SUCCESS;
}

bool hasProfile(const QString& id)
{
	return vibestudio::compilerProfileIds().contains(id);
}

bool joinContains(const QStringList& values, const QString& text)
{
	return values.join('\n').contains(text, Qt::CaseInsensitive);
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	const QStringList appArgs = QCoreApplication::arguments();
	if (appArgs.contains(QStringLiteral("--fake-compiler"))) {
		return runFakeCompiler(appArgs);
	}

	const QVector<vibestudio::CompilerProfileDescriptor> profiles = vibestudio::compilerProfileDescriptors();
	if (profiles.size() < 15) {
		return fail("Expected VibeMap2, helper, Doom node-builder, and VibeMap3 stage profiles.");
	}
	for (const QString& id : {QStringLiteral("vibemap2-bsp"), QStringLiteral("vibemap2-vis"), QStringLiteral("vibemap2-light"),
			QStringLiteral("zdbsp-nodes"), QStringLiteral("zokumbsp-nodes"), QStringLiteral("vibemap3-probe"), QStringLiteral("vibemap3-bsp")}) {
		if (!hasProfile(id)) {
			return fail("Expected existing compiler profile ids to stay stable.");
		}
	}
	for (const QString& id : {QStringLiteral("vibemap3-vis"), QStringLiteral("vibemap3-light"), QStringLiteral("vibemap3-convert"),
			QStringLiteral("vibemap3-pk3"), QStringLiteral("vibemap2-bspinfo"), QStringLiteral("vibemap2-bsputil-check"),
			QStringLiteral("vibemap2-bsputil-extract-entities"), QStringLiteral("vibemap2-bsputil-extract-textures")}) {
		if (!hasProfile(id)) {
			return fail("Expected new stage and helper profiles.");
		}
	}

	vibestudio::CompilerArgumentPreset wadPathPreset;
	if (!vibestudio::compilerArgumentPresetForId(QStringLiteral("vibemap2-bsp"), QStringLiteral("wadpath"), &wadPathPreset)
		|| !wadPathPreset.requiresValue
		|| wadPathPreset.arguments != QStringList{QStringLiteral("-wadpath")}) {
		return fail("Expected a qbsp -wadpath preset that asks for a value.");
	}
	vibestudio::CompilerArgumentPreset litPreset;
	if (!vibestudio::compilerArgumentPresetForId(QStringLiteral("vibemap2-light"), QStringLiteral("lit"), &litPreset)
		|| litPreset.requiresValue
		|| litPreset.arguments != QStringList{QStringLiteral("-lit")}) {
		return fail("Expected a value-free light -lit preset.");
	}
	QStringList qbspPresetArguments;
	for (const vibestudio::CompilerArgumentPreset& preset : vibestudio::compilerArgumentPresetsForProfile(QStringLiteral("vibemap2-bsp"))) {
		qbspPresetArguments += preset.arguments;
	}
	for (const QString& expected : {QStringLiteral("-bsp2"), QStringLiteral("-hlbsp"), QStringLiteral("-qbism"), QStringLiteral("-hexen2"), QStringLiteral("-notex"), QStringLiteral("-leaktest")}) {
		if (!qbspPresetArguments.contains(expected)) {
			return fail("Expected the documented VibeMap2 bsp target presets.");
		}
	}
	QStringList vibemap3PresetArguments;
	for (const vibestudio::CompilerArgumentPreset& preset : vibestudio::compilerArgumentPresetsForProfile(QStringLiteral("vibemap3-bsp"))) {
		vibemap3PresetArguments += preset.arguments;
	}
	for (const QString& expected : {QStringLiteral("-meta"), QStringLiteral("-fast"), QStringLiteral("-fs_basepath"), QStringLiteral("-fs_game"), QStringLiteral("-threads"), QStringLiteral("-v")}) {
		if (!vibemap3PresetArguments.contains(expected)) {
			return fail("Expected the documented VibeMap3 presets.");
		}
	}

	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected temporary workspace.");
	}
	QDir root(tempDir.path());
	if (!root.mkpath(QStringLiteral("external/compilers/vibemap2/build/src/qbsp")) || !root.mkpath(QStringLiteral("maps"))) {
		return fail("Expected fake workspace directories.");
	}

#if defined(Q_OS_WIN)
	const QString qbspName = QStringLiteral("vibemap2-bsp.exe");
#else
	const QString qbspName = QStringLiteral("vibemap2-bsp");
#endif
	if (!touchFile(root.filePath(QStringLiteral("external/compilers/vibemap2/build/src/qbsp/%1").arg(qbspName)))) {
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
	request.profileId = QStringLiteral("vibemap2-bsp");
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
	if (!joinContains(plan.relatedOutputPaths, QStringLiteral("start.final.pts"))
		|| !joinContains(plan.relatedOutputPaths, QStringLiteral("start.final.prt"))) {
		return fail("Expected qbsp leak point and portal files among the related outputs.");
	}
	if (plan.knownIssueNotes.isEmpty()) {
		return fail("Expected informational VibeMap2 known-issue notes in the command plan.");
	}
	if (joinContains(plan.warnings, QStringLiteral("high-value upstream issues are tracked"))) {
		return fail("Expected the known-issue tracking summary to stay informational instead of warning.");
	}
	if (plan.preflightWarnings.isEmpty() || !joinContains(plan.preflightWarnings, QStringLiteral("#194"))) {
		return fail("Expected Quake map preflight warnings in command plan.");
	}
	const vibestudio::CompilerCommandManifest manifest = vibestudio::compilerCommandManifestFromPlan(plan);
	if (manifest.taskLog.isEmpty() || manifest.expectedOutputPaths.isEmpty() || manifest.optionalOutputPaths.isEmpty()) {
		return fail("Expected manifest task log, output paths, and optional outputs.");
	}
	if (manifest.knownIssueNotes.isEmpty() || manifest.preflightWarnings.isEmpty()) {
		return fail("Expected manifest to carry known-issue notes and preflight warnings.");
	}
	if (!vibestudio::compilerCommandManifestJson(manifest).contains(QStringLiteral("taskLog"))
		|| !vibestudio::compilerCommandManifestJson(manifest).contains(QStringLiteral("optionalOutputPaths"))) {
		return fail("Expected manifest JSON task log and optional outputs.");
	}
	const QString manifestPath = root.filePath(QStringLiteral("build/qbsp-manifest.json"));
	QString saveError;
	if (!vibestudio::saveCompilerCommandManifest(manifest, manifestPath, &saveError) || !QFile::exists(manifestPath)) {
		return fail("Expected manifest save to create a JSON file.");
	}
	vibestudio::CompilerCommandManifest loadedManifest;
	if (!vibestudio::loadCompilerCommandManifest(manifestPath, &loadedManifest, &saveError)
		|| loadedManifest.profileId != manifest.profileId
		|| loadedManifest.optionalOutputPaths != manifest.optionalOutputPaths
		|| loadedManifest.knownIssueNotes != manifest.knownIssueNotes) {
		return fail("Expected manifest load to round-trip profile id, optional outputs, and notes.");
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
	bool sawKnownIssueNote = false;
	bool sawCategorizedPreflightWarning = false;
	for (const vibestudio::CompilerTaskLogEntry& entry : dryPreflightLog) {
		sawKnownIssueNote = sawKnownIssueNote || (entry.level == QStringLiteral("info") && entry.message.contains(QStringLiteral("Known issue note")));
		sawCategorizedPreflightWarning = sawCategorizedPreflightWarning || entry.message.contains(QStringLiteral("Preflight warning"));
	}
	if (dryPreflightResult.state != vibestudio::OperationState::Warning || !sawKnownIssueNote || !sawCategorizedPreflightWarning) {
		return fail("Expected categorized VibeMap2 findings to surface before a dry-run process launch.");
	}

	vibestudio::CompilerRunRequest runRequest;
	runRequest.command = request;
	runRequest.command.outputPath = root.filePath(QStringLiteral("maps/start-run.bsp"));
	runRequest.command.extraArguments = {QStringLiteral("--fake-compiler"), QStringLiteral("--print-temp")};
	runRequest.command.executableOverrides.push_back({QStringLiteral("vibemap2-bsp"), QCoreApplication::applicationFilePath()});
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
	if (!runResult.diagnostics.first().filePath.endsWith(QStringLiteral("start.final.map"))
		|| runResult.diagnostics.first().line != 7
		|| runResult.diagnostics.first().channel != QStringLiteral("stdout")) {
		return fail("Expected diagnostics to preserve dotted filenames, line numbers, and channel.");
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
	if (badArtifactResult.state != vibestudio::OperationState::Failed || !joinContains(badArtifactResult.manifest.errors, QStringLiteral("artifact validation"))) {
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

	// qbsp writes "<bsp>.pts" on a leak and still exits 0 (ericw-tools qbsp/outside.cc).
	vibestudio::CompilerRunRequest leakRequest = runRequest;
	leakRequest.command.outputPath = root.filePath(QStringLiteral("maps/leaky.bsp"));
	leakRequest.command.extraArguments = {QStringLiteral("--fake-compiler"), QStringLiteral("--fake-leak")};
	leakRequest.manifestPath.clear();
	const vibestudio::CompilerRunResult leakResult = vibestudio::runCompilerCommand(leakRequest);
	if (!leakResult.leakDetected || leakResult.exitCode != 0) {
		return fail("Expected a leak to be detected after a zero-exit qbsp run.");
	}
	if (leakResult.leakOccupantClassname != QStringLiteral("info_player_start") || leakResult.leakPointText != QStringLiteral("32 64 -16")) {
		return fail("Expected the leaked occupant classname and coordinates.");
	}
	if (!joinContains(leakResult.manifest.warnings, QStringLiteral("LEAK"))
		|| !joinContains(leakResult.manifest.warnings, QStringLiteral("info_player_start"))) {
		return fail("Expected a prominent leak warning naming the occupant.");
	}
	if (!joinContains(leakResult.registeredOutputPaths, QStringLiteral("leaky.pts"))) {
		return fail("Expected the leak point file to be registered as a produced artifact.");
	}

	// "-leaktest" makes qbsp write the leak files and then exit 1 on purpose; the one run the user
	// asked to fail on a leak must still be the run that explains the leak.
	vibestudio::CompilerRunRequest leakTestRequest = runRequest;
	leakTestRequest.command.outputPath = root.filePath(QStringLiteral("maps/leaktest.bsp"));
	leakTestRequest.command.extraArguments = {QStringLiteral("--fake-compiler"), QStringLiteral("--fake-leaktest"), QStringLiteral("-leaktest")};
	leakTestRequest.manifestPath.clear();
	const vibestudio::CompilerRunResult leakTestResult = vibestudio::runCompilerCommand(leakTestRequest);
	if (leakTestResult.state != vibestudio::OperationState::Failed || leakTestResult.exitCode == 0) {
		return fail("Expected the -leaktest abort to stay a failed run.");
	}
	if (!leakTestResult.leakDetected || !leakTestResult.leakPointFilePath.endsWith(QStringLiteral("leaktest.pts"))) {
		return fail("Expected a leak to be diagnosed even when the compile aborts with a non-zero exit code.");
	}
	if (!joinContains(leakTestResult.manifest.warnings, QStringLiteral("is not sealed"))
		|| !joinContains(leakTestResult.manifest.warnings, QStringLiteral("info_player_start"))
		|| !joinContains(leakTestResult.manifest.warnings, QStringLiteral("-leaktest"))) {
		return fail("Expected the -leaktest exit code to be explained beside the leak warning.");
	}

	// qbsp only deletes a stale .pts when neither -onlyents nor -convert is used
	// (external/compilers/vibemap2/src/qbsp/qbsp.cc), so a leftover file is not evidence of a leak.
	vibestudio::CompilerRunRequest staleLeakRequest = runRequest;
	staleLeakRequest.command.outputPath = root.filePath(QStringLiteral("maps/onlyents.bsp"));
	staleLeakRequest.command.extraArguments = {QStringLiteral("--fake-compiler"), QStringLiteral("--fake-quiet"), QStringLiteral("-onlyents")};
	staleLeakRequest.manifestPath.clear();
	const QString stalePointsPath = root.filePath(QStringLiteral("maps/onlyents.pts"));
	if (!writeTextFile(stalePointsPath, QByteArray("0 0 0\n")) || !backdateFile(stalePointsPath, 3600)) {
		return fail("Expected a stale leak point fixture.");
	}
	const vibestudio::CompilerRunResult staleLeakResult = vibestudio::runCompilerCommand(staleLeakRequest);
	if (staleLeakResult.state == vibestudio::OperationState::Failed) {
		return fail("Expected an entity-only recompile beside a stale leak point file to succeed.");
	}
	if (staleLeakResult.leakDetected || joinContains(staleLeakResult.manifest.warnings, QStringLiteral("is not sealed"))) {
		return fail("Expected a leak point file that predates the run not to be reported as a fresh leak.");
	}
	bool sawStaleLeakNote = false;
	for (const vibestudio::CompilerTaskLogEntry& entry : staleLeakResult.manifest.taskLog) {
		sawStaleLeakNote = sawStaleLeakNote || entry.message.contains(QStringLiteral("predates this run"));
	}
	if (!sawStaleLeakNote) {
		return fail("Expected the stale leak point file to be explained in the task log.");
	}

	const QString cleanBspPath = root.filePath(QStringLiteral("maps/quiet.bsp"));
	if (!writeMinimalQuakeBsp(cleanBspPath)) {
		return fail("Expected clean BSP fixture.");
	}
	vibestudio::CompilerCommandRequest lightRequest;
	lightRequest.profileId = QStringLiteral("vibemap2-light");
	lightRequest.inputPath = cleanBspPath;
	lightRequest.workspaceRootPath = tempDir.path();
	lightRequest.executableOverrides.push_back({QStringLiteral("vibemap2-light"), QCoreApplication::applicationFilePath()});
	const vibestudio::CompilerCommandPlan cleanLightPlan = vibestudio::buildCompilerCommandPlan(lightRequest);
	if (!cleanLightPlan.warnings.isEmpty() || !cleanLightPlan.knownIssueWarnings.isEmpty()) {
		return fail("Expected a clean VibeMap2 light plan to carry no warnings.");
	}
	if (cleanLightPlan.knownIssueNotes.isEmpty()) {
		return fail("Expected informational known-issue notes on a clean VibeMap2 light plan.");
	}
	vibestudio::CompilerRunRequest cleanRunRequest;
	cleanRunRequest.command = lightRequest;
	cleanRunRequest.command.extraArguments = {QStringLiteral("--fake-compiler"), QStringLiteral("--fake-quiet")};
	cleanRunRequest.registerOutputs = true;
	const vibestudio::CompilerRunResult cleanRunResult = vibestudio::runCompilerCommand(cleanRunRequest);
	if (cleanRunResult.state != vibestudio::OperationState::Completed) {
		return fail("Expected a warning-free VibeMap2 run to reach Completed.");
	}

	// "-lit" writes a sibling coloured lighting file (ericw-tools light/light.cc).
	vibestudio::CompilerCommandRequest litRequest = lightRequest;
	litRequest.extraArguments = {QStringLiteral("-lit")};
	const vibestudio::CompilerCommandPlan litPlan = vibestudio::buildCompilerCommandPlan(litRequest);
	if (!joinContains(litPlan.additionalExpectedOutputPaths, QStringLiteral("quiet.lit"))) {
		return fail("Expected light -lit to add the sibling .lit file to the expected outputs.");
	}
	const vibestudio::CompilerCommandManifest litManifest = vibestudio::compilerCommandManifestFromPlan(litPlan);
	if (!joinContains(litManifest.expectedOutputPaths, QStringLiteral("quiet.lit")) || litManifest.outputHashes.size() != litManifest.expectedOutputPaths.size()) {
		return fail("Expected the .lit artifact to be hashed with the other expected outputs.");
	}

	// ericw-tools locations are "<source>[line N]"; zero-count summaries are not diagnostics.
	vibestudio::CompilerRunRequest diagnosticsRequest = cleanRunRequest;
	diagnosticsRequest.command.extraArguments = {QStringLiteral("--fake-compiler"), QStringLiteral("--fake-ericw-diagnostics")};
	const vibestudio::CompilerRunResult diagnosticsResult = vibestudio::runCompilerCommand(diagnosticsRequest);
	if (diagnosticsResult.diagnostics.size() != 3) {
		return fail("Expected exactly the three VibeMap2-style diagnostics and no zero-count false positives.");
	}
	for (const vibestudio::CompilerDiagnostic& diagnostic : diagnosticsResult.diagnostics) {
		if (diagnostic.rawLine.contains('\r')) {
			return fail("Expected CRLF output to be normalized before diagnostics are recorded.");
		}
		if (diagnostic.message.contains(QStringLiteral("0 errors"))
			|| diagnostic.message.contains(QStringLiteral("Error count"))
			|| diagnostic.message.contains(QStringLiteral("no warnings"))
			|| diagnostic.message.contains(QStringLiteral("leaktest"))) {
			return fail("Expected status lines not to be classified as diagnostics.");
		}
	}
	if (diagnosticsResult.diagnostics.at(0).line != 12 || !diagnosticsResult.diagnostics.at(0).filePath.endsWith(QStringLiteral("start.final.map"))) {
		return fail("Expected the VibeMap2 <source>[line N] location form to be parsed.");
	}
	if (diagnosticsResult.diagnostics.at(1).line != 34 || !diagnosticsResult.diagnostics.at(1).filePath.isEmpty()) {
		return fail("Expected a bare 'WARNING: <line>:' diagnostic to capture the line number only.");
	}
	if (diagnosticsResult.diagnostics.at(2).channel != QStringLiteral("stderr")
		|| diagnosticsResult.diagnostics.at(0).channel != QStringLiteral("stdout")) {
		return fail("Expected stdout and stderr diagnostics to stay distinguishable.");
	}

	// Colour escapes are taken out before a line is parsed or logged.
	vibestudio::CompilerRunRequest coloredRequest = cleanRunRequest;
	coloredRequest.command.extraArguments = {QStringLiteral("--fake-compiler"), QStringLiteral("--fake-colored")};
	const vibestudio::CompilerRunResult coloredResult = vibestudio::runCompilerCommand(coloredRequest);
	if (coloredResult.diagnostics.size() != 2
		|| coloredResult.diagnostics.at(0).message != QStringLiteral("img::ConvertTextures: WARNING: invalid size data for brick_wall")
		|| coloredResult.diagnostics.at(1).line != 34) {
		return fail("Expected coloured compiler output to be parsed as if it were plain.");
	}
	QStringList coloredLog;
	for (const vibestudio::CompilerTaskLogEntry& entry : coloredResult.manifest.taskLog) {
		coloredLog << entry.message;
	}
	if (coloredLog.join(QLatin1Char('\n')).contains(QChar(0x1b)) || !coloredLog.contains(QStringLiteral("Processing hull 0..."))) {
		return fail("Expected colour escapes to be taken out of the compiler log, and the text around them kept.");
	}

	vibestudio::CompilerRunRequest fatalRequest = cleanRunRequest;
	fatalRequest.command.extraArguments = {QStringLiteral("--fake-compiler"), QStringLiteral("--fake-fatal")};
	const vibestudio::CompilerRunResult fatalResult = vibestudio::runCompilerCommand(fatalRequest);
	if (fatalResult.state != vibestudio::OperationState::Failed || fatalResult.diagnostics.isEmpty()) {
		return fail("Expected a fatal fake compiler run to fail with a diagnostic.");
	}
	const vibestudio::CompilerDiagnostic fatalDiagnostic = fatalResult.diagnostics.first();
	if (fatalDiagnostic.level != QStringLiteral("error")
		|| fatalDiagnostic.message != QStringLiteral("Failed to open maps/missing.map: No such file")
		|| !fatalDiagnostic.rawLine.contains(QStringLiteral("************ ERROR ************"))) {
		return fail("Expected the fatal error banner and its message to be joined into one diagnostic.");
	}

	// Output has to be streamed: cancellation is only requested once the first line arrives.
	vibestudio::CompilerRunRequest streamRequest = cleanRunRequest;
	streamRequest.command.extraArguments = {QStringLiteral("--fake-compiler"), QStringLiteral("--fake-stream")};
	bool sawStreamedLine = false;
	vibestudio::CompilerRunCallbacks streamCallbacks;
	streamCallbacks.logEntry = [&sawStreamedLine](const vibestudio::CompilerTaskLogEntry& entry) {
		if (entry.message.contains(QStringLiteral("STREAM-LINE-1"))) {
			sawStreamedLine = true;
		}
	};
	streamCallbacks.cancellationRequested = [&sawStreamedLine]() {
		return sawStreamedLine;
	};
	const vibestudio::CompilerRunResult streamResult = vibestudio::runCompilerCommand(streamRequest, streamCallbacks);
	if (!sawStreamedLine) {
		return fail("Expected compiler output lines to be streamed while the process is still running.");
	}
	if (!streamResult.cancelled || streamResult.stdoutText.contains(QStringLiteral("STREAM-LINE-2"))) {
		return fail("Expected streamed output to allow cancellation before the process finished.");
	}

	vibestudio::CompilerManifestRerunRequest rerunRequest;
	rerunRequest.registerOutputs = false;
	rerunRequest.timeoutMs = 30000;
	const vibestudio::CompilerRunResult rerunResult = vibestudio::rerunCompilerCommandManifest(cleanRunResult.manifest, rerunRequest);
	if (!rerunResult.started || !rerunResult.registeredOutputPaths.isEmpty()) {
		return fail("Expected the request-shaped manifest rerun to honour registerOutputs=false.");
	}

	// A rerun replays the stored command, never the previous execution's outcome.
	vibestudio::CompilerCommandManifest repairedManifest = fatalResult.manifest;
	if (repairedManifest.errors.isEmpty()) {
		return fail("Expected the failed run manifest to record its errors.");
	}
	repairedManifest.arguments.replaceInStrings(QStringLiteral("--fake-fatal"), QStringLiteral("--fake-quiet"));
	repairedManifest.commandLine = vibestudio::compilerCommandLineText(repairedManifest.program, repairedManifest.arguments);
	vibestudio::CompilerManifestRerunRequest repairedRerunRequest;
	repairedRerunRequest.timeoutMs = 30000;
	QVector<vibestudio::CompilerTaskLogEntry> repairedLog;
	vibestudio::CompilerRunCallbacks repairedCallbacks;
	repairedCallbacks.logEntry = [&repairedLog](const vibestudio::CompilerTaskLogEntry& entry) {
		repairedLog.push_back(entry);
	};
	const vibestudio::CompilerRunResult repairedResult = vibestudio::rerunCompilerCommandManifest(repairedManifest, repairedRerunRequest, repairedCallbacks);
	if (repairedResult.state != vibestudio::OperationState::Completed) {
		return fail("Expected a repaired rerun to reach Completed instead of inheriting the previous run's findings.");
	}
	if (!repairedResult.manifest.errors.isEmpty() || !repairedResult.manifest.warnings.isEmpty()
		|| !repairedResult.manifest.knownIssueWarnings.isEmpty() || !repairedResult.manifest.preflightWarnings.isEmpty()) {
		return fail("Expected a rerun not to carry the previous run's warnings and errors.");
	}
	for (const vibestudio::CompilerTaskLogEntry& entry : repairedLog) {
		if (entry.message.contains(QStringLiteral("Preflight error"))) {
			return fail("Expected a rerun not to replay the previous run's errors into its task log.");
		}
	}

	// q3map2 writes "<source>.lin" on a leak and still exits 0, and names neither a classname nor
	// coordinates (external/compilers/vibemap3/tools/quake3/q3map2/leakfile.cpp).
	const QString q3LeakMapPath = root.filePath(QStringLiteral("maps/q3leak.map"));
	if (!writeTextFile(q3LeakMapPath, QByteArray("{\n\"classname\" \"worldspawn\"\n}\n"))) {
		return fail("Expected a Quake III map fixture.");
	}
	vibestudio::CompilerRunRequest q3LeakRequest;
	q3LeakRequest.command.profileId = QStringLiteral("vibemap3-bsp");
	q3LeakRequest.command.inputPath = q3LeakMapPath;
	q3LeakRequest.command.workspaceRootPath = tempDir.path();
	q3LeakRequest.command.extraArguments = {QStringLiteral("--fake-compiler"), QStringLiteral("--fake-q3-leak")};
	q3LeakRequest.command.executableOverrides.push_back({QStringLiteral("vibemap3"), QCoreApplication::applicationFilePath()});
	const vibestudio::CompilerRunResult q3LeakResult = vibestudio::runCompilerCommand(q3LeakRequest);
	if (!q3LeakResult.leakDetected || !q3LeakResult.leakPointFilePath.endsWith(QStringLiteral("q3leak.lin"))) {
		return fail("Expected a VibeMap3 leak to be detected from its .lin leak file.");
	}
	if (!q3LeakResult.leakOccupantClassname.isEmpty() || !q3LeakResult.leakPointText.isEmpty()) {
		return fail("Expected no invented occupant classname or leak coordinates for a VibeMap3 leak.");
	}
	if (!joinContains(q3LeakResult.manifest.warnings, QStringLiteral("is not sealed"))) {
		return fail("Expected a prominent leak warning for the VibeMap3 BSP stage.");
	}

	// vis needs the portal file qbsp deletes when the map leaks (ericw-tools vis/vis.cc).
	vibestudio::CompilerCommandRequest visRequest;
	visRequest.profileId = QStringLiteral("vibemap2-vis");
	visRequest.inputPath = cleanBspPath;
	visRequest.workspaceRootPath = tempDir.path();
	const vibestudio::CompilerCommandPlan visPlan = vibestudio::buildCompilerCommandPlan(visRequest);
	if (!joinContains(visPlan.warnings, QStringLiteral("quiet.prt")) || !joinContains(visPlan.warnings, QStringLiteral("leak"))) {
		return fail("Expected a clear warning when the vis stage has no portal file.");
	}
	if (!writeTextFile(root.filePath(QStringLiteral("maps/quiet.prt")), QByteArray("PRT1\n"))) {
		return fail("Expected portal fixture.");
	}
	const vibestudio::CompilerCommandPlan visPlanWithPortals = vibestudio::buildCompilerCommandPlan(visRequest);
	if (joinContains(visPlanWithPortals.warnings, QStringLiteral("quiet.prt"))) {
		return fail("Expected the portal warning to disappear once the .prt file exists.");
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
	if (!zdbspPlan.profileFound || !zdbspPlan.toolFound || !zdbspPlan.errors.isEmpty()) {
		return fail("Expected ZDBSP profile and tool to resolve.");
	}
	const int zdbspOutputFlagIndex = zdbspPlan.arguments.indexOf(QStringLiteral("-o"));
	if (zdbspOutputFlagIndex < 0 || zdbspPlan.arguments.value(zdbspOutputFlagIndex + 1) != QDir::cleanPath(zdbspRequest.outputPath)) {
		return fail("Expected ZDBSP to receive its output through -o.");
	}
	if (zdbspPlan.arguments.last() != QDir::cleanPath(wadPath)) {
		return fail("Expected the ZDBSP input WAD to stay the trailing positional argument.");
	}

	vibestudio::CompilerCommandRequest zdbspDefaultRequest = zdbspRequest;
	zdbspDefaultRequest.outputPath.clear();
	zdbspDefaultRequest.workingDirectory = root.filePath(QStringLiteral("maps"));
	const vibestudio::CompilerCommandPlan zdbspDefaultPlan = vibestudio::buildCompilerCommandPlan(zdbspDefaultRequest);
	if (zdbspDefaultPlan.expectedOutputPath == QDir::cleanPath(wadPath)) {
		return fail("Expected the ZDBSP default output never to collapse onto the input WAD.");
	}
	if (!zdbspDefaultPlan.expectedOutputPath.endsWith(QStringLiteral("tmp.wad"))) {
		return fail("Expected the ZDBSP default output to be tmp.wad in the working directory.");
	}

	vibestudio::CompilerCommandRequest zokumRequest = zdbspRequest;
	zokumRequest.profileId = QStringLiteral("zokumbsp-nodes");
	const vibestudio::CompilerCommandPlan zokumPlan = vibestudio::buildCompilerCommandPlan(zokumRequest);
	const int zokumOutputFlagIndex = zokumPlan.arguments.indexOf(QStringLiteral("-o"));
	if (zokumOutputFlagIndex <= zokumPlan.arguments.indexOf(QDir::cleanPath(wadPath))
		|| zokumPlan.arguments.value(zokumOutputFlagIndex + 1) != QDir::cleanPath(zokumRequest.outputPath)) {
		return fail("Expected ZokumBSP to receive -o after the input WAD.");
	}

	const QString q3BspPath = root.filePath(QStringLiteral("maps/arena.bsp"));
	if (!writeMinimalQuakeBsp(q3BspPath)) {
		return fail("Expected fake Quake III BSP input.");
	}
	vibestudio::CompilerCommandRequest q3VisRequest;
	q3VisRequest.profileId = QStringLiteral("vibemap3-vis");
	q3VisRequest.inputPath = q3BspPath;
	q3VisRequest.workspaceRootPath = tempDir.path();
	q3VisRequest.extraArguments = {QStringLiteral("-fast")};
	const vibestudio::CompilerCommandPlan q3VisPlan = vibestudio::buildCompilerCommandPlan(q3VisRequest);
	if (q3VisPlan.arguments.value(0) != QStringLiteral("-vis")) {
		return fail("Expected the VibeMap3 stage token to stay argument 0 ahead of user extras.");
	}
	if (q3VisPlan.arguments.last() != QDir::cleanPath(q3BspPath)) {
		return fail("Expected VibeMap3 to receive the map file as the trailing argument.");
	}
	vibestudio::CompilerCommandRequest q3LightRequest = q3VisRequest;
	q3LightRequest.profileId = QStringLiteral("vibemap3-light");
	if (vibestudio::buildCompilerCommandPlan(q3LightRequest).arguments.value(0) != QStringLiteral("-light")) {
		return fail("Expected the VibeMap3 light stage token first.");
	}
	vibestudio::CompilerCommandRequest q3ConvertRequest = q3VisRequest;
	q3ConvertRequest.profileId = QStringLiteral("vibemap3-convert");
	if (vibestudio::buildCompilerCommandPlan(q3ConvertRequest).arguments.value(0) != QStringLiteral("-convert")) {
		return fail("Expected the VibeMap3 convert stage token first.");
	}
	vibestudio::CompilerCommandRequest q3Pk3Request = q3VisRequest;
	q3Pk3Request.profileId = QStringLiteral("vibemap3-pk3");
	const vibestudio::CompilerCommandPlan q3Pk3Plan = vibestudio::buildCompilerCommandPlan(q3Pk3Request);
	if (q3Pk3Plan.arguments.value(0) != QStringLiteral("-pk3")) {
		return fail("Expected the VibeMap3 pk3 stage token first.");
	}
	if (q3Pk3Plan.expectedOutputKnown || !q3Pk3Plan.expectedOutputPath.isEmpty()) {
		return fail("Expected the VibeMap3 auto-package destination to be reported as unknown.");
	}

	vibestudio::CompilerCommandRequest vibemap3BspRequest;
	vibemap3BspRequest.profileId = QStringLiteral("vibemap3-bsp");
	vibemap3BspRequest.inputPath = mapPath;
	vibemap3BspRequest.workspaceRootPath = tempDir.path();
	vibemap3BspRequest.extraArguments = {QStringLiteral("-fast")};
	const vibestudio::CompilerCommandPlan vibemap3BspPlan = vibestudio::buildCompilerCommandPlan(vibemap3BspRequest);
	if (vibemap3BspPlan.arguments.value(0) != QStringLiteral("-meta")) {
		return fail("Expected the VibeMap3 BSP profile to keep -meta first so BSPMain stays the dispatch target.");
	}

	vibestudio::CompilerCommandRequest bspinfoRequest;
	bspinfoRequest.profileId = QStringLiteral("vibemap2-bspinfo");
	bspinfoRequest.inputPath = cleanBspPath;
	bspinfoRequest.workspaceRootPath = tempDir.path();
	const vibestudio::CompilerCommandPlan bspinfoPlan = vibestudio::buildCompilerCommandPlan(bspinfoRequest);
	if (bspinfoPlan.arguments != QStringList{QDir::cleanPath(cleanBspPath)}) {
		return fail("Expected bspinfo to receive only its BSP path.");
	}
	if (!bspinfoPlan.expectedOutputPath.endsWith(QStringLiteral("quiet.bsp.json"))) {
		return fail("Expected bspinfo to declare its JSON dump as the expected artifact.");
	}
	vibestudio::CompilerCommandRequest bsputilRequest = bspinfoRequest;
	bsputilRequest.profileId = QStringLiteral("vibemap2-bsputil-check");
	const vibestudio::CompilerCommandPlan bsputilPlan = vibestudio::buildCompilerCommandPlan(bsputilRequest);
	if (bsputilPlan.arguments.value(0) != QStringLiteral("--check") || bsputilPlan.arguments.last() != QDir::cleanPath(cleanBspPath)) {
		return fail("Expected bsputil operations before the positional BSP path.");
	}
	if (!bsputilPlan.expectedOutputPath.isEmpty()) {
		return fail("Expected bsputil --check to declare no output artifact.");
	}
	vibestudio::CompilerCommandRequest bsputilEntitiesRequest = bspinfoRequest;
	bsputilEntitiesRequest.profileId = QStringLiteral("vibemap2-bsputil-extract-entities");
	const vibestudio::CompilerCommandPlan bsputilEntitiesPlan = vibestudio::buildCompilerCommandPlan(bsputilEntitiesRequest);
	if (bsputilEntitiesPlan.arguments.value(0) != QStringLiteral("--extract-entities")
		|| !bsputilEntitiesPlan.expectedOutputPath.endsWith(QStringLiteral("quiet.ent"))) {
		return fail("Expected bsputil --extract-entities to write a sibling .ent file.");
	}

	vibestudio::CompilerCommandRequest vibemap3ProbeRequest;
	vibemap3ProbeRequest.profileId = QStringLiteral("vibemap3-probe");
	vibemap3ProbeRequest.workspaceRootPath = tempDir.path();
	const vibestudio::CompilerCommandPlan vibemap3ProbePlan = vibestudio::buildCompilerCommandPlan(vibemap3ProbeRequest);
	if (!vibemap3ProbePlan.profileFound || !vibemap3ProbePlan.toolFound || !vibemap3ProbePlan.errors.isEmpty()) {
		return fail("Expected VibeMap3 probe to allow no input path.");
	}
	if (!vibemap3ProbePlan.arguments.contains(QStringLiteral("-help"))) {
		return fail("Expected VibeMap3 probe command to include help argument.");
	}

	vibestudio::CompilerCommandRequest unknownRequest;
	unknownRequest.profileId = QStringLiteral("unknown");
	const vibestudio::CompilerCommandPlan unknownPlan = vibestudio::buildCompilerCommandPlan(unknownRequest);
	if (unknownPlan.errors.isEmpty()) {
		return fail("Expected unknown profile error.");
	}

	return EXIT_SUCCESS;
}
