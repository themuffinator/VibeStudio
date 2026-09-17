#include "core/build_pipeline.h"
#include "core/compiler_profiles.h"
#include "core/game_installation.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QtEndian>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace vibestudio;

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

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
	return file.write(bytes) == bytes.size();
}

bool samePath(const QString& left, const QString& right)
{
	if (left.isEmpty() || right.isEmpty()) {
		return false;
	}
	return QDir::cleanPath(QFileInfo(left).absoluteFilePath()) == QDir::cleanPath(QFileInfo(right).absoluteFilePath());
}

QByteArray minimalQuakeMap()
{
	return QByteArrayLiteral(
		"{\n"
		"\"classname\" \"worldspawn\"\n"
		"\"wad\" \"fixture.wad\"\n"
		"{\n"
		"( -64 -64 -16 ) ( -64 -63 -16 ) ( -64 -64 -15 ) TESTTEX 0 0 0 1 1\n"
		"( -64 -64 -16 ) ( -64 -64 -15 ) ( -63 -64 -16 ) TESTTEX 0 0 0 1 1\n"
		"( -64 -64 -16 ) ( -63 -64 -16 ) ( -64 -63 -16 ) TESTTEX 0 0 0 1 1\n"
		"( 64 64 16 ) ( 64 65 16 ) ( 65 64 16 ) TESTTEX 0 0 0 1 1\n"
		"( 64 64 16 ) ( 65 64 16 ) ( 64 64 17 ) TESTTEX 0 0 0 1 1\n"
		"( 64 64 16 ) ( 64 64 17 ) ( 64 65 16 ) TESTTEX 0 0 0 1 1\n"
		"}\n"
		"}\n"
		"{\n"
		"\"classname\" \"info_player_start\"\n"
		"\"origin\" \"0 0 24\"\n"
		"}\n");
}

QByteArray minimalQuakeBsp()
{
	QByteArray bytes(4 + 15 * 8, '\0');
	qToLittleEndian<qint32>(29, reinterpret_cast<uchar*>(bytes.data()));
	return bytes;
}

// Stands in for a compiler stage that produces its artifact, prints an error-shaped but non-fatal
// notice, and exits 0 - exactly what ericw-tools does for data it cannot parse
// (external/compilers/ericw-tools/common/bspfile_common.cc, common/bspxfile.cc).
int runFakeStageCompiler(const QStringList& appArgs)
{
	QString outputPath;
	for (const QString& argument : appArgs) {
		if (argument.endsWith(QStringLiteral(".bsp"), Qt::CaseInsensitive)) {
			outputPath = argument;
		}
	}
	if (outputPath.isEmpty()) {
		// With no destination argument qbsp writes "<map>.bsp" beside its input.
		for (const QString& argument : appArgs) {
			if (argument.endsWith(QStringLiteral(".map"), Qt::CaseInsensitive)) {
				const QFileInfo info(argument);
				outputPath = info.absolutePath() + QStringLiteral("/") + info.completeBaseName() + QStringLiteral(".bsp");
			}
		}
	}
	if (!outputPath.isEmpty()) {
		writeFile(outputPath, minimalQuakeBsp());
	}
	std::cout << "ERROR: malformed extended content flags file\n" << std::flush;
	if (appArgs.contains(QStringLiteral("--fake-slow-stage"))) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1500));
	}
	return EXIT_SUCCESS;
}

const BuildPipelineStageResult* stageById(const BuildPipelineResult& result, const QString& stageId)
{
	for (const BuildPipelineStageResult& stage : result.stages) {
		if (stage.stage.id == stageId) {
			return &stage;
		}
	}
	return nullptr;
}

bool runDescriptorSmoke()
{
	bool ok = true;
	const QStringList ids = buildPipelineIds();
	for (const char* expected : {"quake-full", "quake-fast", "quake-bsp-only", "quake3-full", "quake3-bsp-only", "doom-zdbsp", "doom-zokumbsp"}) {
		ok &= expect(ids.contains(QString::fromLatin1(expected)), "Expected build pipeline id to be registered.");
	}

	BuildPipelineDescriptor quakeFull;
	ok &= expect(buildPipelineForId(QStringLiteral("quake-full"), &quakeFull), "quake-full pipeline should resolve.");
	ok &= expect(quakeFull.engineFamily == QStringLiteral("idTech2"), "quake-full should be an idTech2 pipeline.");
	ok &= expect(quakeFull.inputExtensions == QStringList{QStringLiteral("map")}, "quake-full should consume .map sources.");
	ok &= expect(quakeFull.stages.size() == 3, "quake-full should declare three stages.");
	if (quakeFull.stages.size() == 3) {
		ok &= expect(quakeFull.stages.at(0).id == QStringLiteral("qbsp") && quakeFull.stages.at(0).profileId == QStringLiteral("ericw-qbsp"), "quake-full stage 1 should be ericw qbsp.");
		ok &= expect(quakeFull.stages.at(0).inputFromStageId.isEmpty(), "quake-full qbsp should consume the pipeline input.");
		ok &= expect(quakeFull.stages.at(1).id == QStringLiteral("vis") && quakeFull.stages.at(1).profileId == QStringLiteral("ericw-vis"), "quake-full stage 2 should be ericw vis.");
		ok &= expect(quakeFull.stages.at(1).inputFromStageId == QStringLiteral("qbsp"), "quake-full vis should chain from qbsp.");
		ok &= expect(quakeFull.stages.at(1).optional && quakeFull.stages.at(1).enabledByDefault, "quake-full vis should be optional but enabled.");
		ok &= expect(quakeFull.stages.at(2).id == QStringLiteral("light") && quakeFull.stages.at(2).profileId == QStringLiteral("ericw-light"), "quake-full stage 3 should be ericw light.");
		ok &= expect(quakeFull.stages.at(2).inputFromStageId == QStringLiteral("vis"), "quake-full light should chain from vis.");
	}
	for (const BuildPipelineStage& stage : quakeFull.stages) {
		ok &= expect(!stage.displayName.isEmpty() && !stage.description.isEmpty(), "Every stage needs a display name and description.");
	}

	BuildPipelineDescriptor quakeFast;
	ok &= expect(buildPipelineForId(QStringLiteral("quake-fast"), &quakeFast), "quake-fast pipeline should resolve.");
	const BuildPipelineStage* fastVis = nullptr;
	for (const BuildPipelineStage& stage : quakeFast.stages) {
		if (stage.id == QStringLiteral("vis")) {
			fastVis = &stage;
		}
	}
	ok &= expect(fastVis != nullptr && fastVis->optional && !fastVis->enabledByDefault, "quake-fast should disable vis by default.");

	BuildPipelineDescriptor quake3Full;
	ok &= expect(buildPipelineForId(QStringLiteral("quake3-full"), &quake3Full), "quake3-full pipeline should resolve.");
	ok &= expect(quake3Full.engineFamily == QStringLiteral("idTech3"), "quake3-full should be an idTech3 pipeline.");
	ok &= expect(quake3Full.stages.size() == 3, "quake3-full should declare three stages.");
	if (quake3Full.stages.size() == 3) {
		ok &= expect(quake3Full.stages.at(0).profileId == QStringLiteral("q3map2-bsp"), "quake3-full should start with q3map2-bsp.");
		ok &= expect(quake3Full.stages.at(1).profileId == QStringLiteral("q3map2-vis"), "quake3-full should continue with q3map2-vis.");
		ok &= expect(quake3Full.stages.at(2).profileId == QStringLiteral("q3map2-light"), "quake3-full should finish with q3map2-light.");
	}

	BuildPipelineDescriptor doomZokum;
	ok &= expect(buildPipelineForId(QStringLiteral("doom-zokumbsp"), &doomZokum), "doom-zokumbsp pipeline should resolve.");
	ok &= expect(doomZokum.stages.size() == 1 && doomZokum.stages.at(0).profileId == QStringLiteral("zokumbsp-nodes"), "doom-zokumbsp should run zokumbsp nodes.");
	ok &= expect(doomZokum.inputExtensions == QStringList{QStringLiteral("wad")}, "doom pipelines should consume .wad input.");

	ok &= expect(buildPipelineIdsForInput(QStringLiteral("/tmp/example.map")).contains(QStringLiteral("quake-full")), "A .map source should offer the Quake pipelines.");
	ok &= expect(!buildPipelineIdsForInput(QStringLiteral("/tmp/example.map")).contains(QStringLiteral("doom-zdbsp")), "A .map source should not offer the Doom pipelines.");
	ok &= expect(buildPipelineIdsForInput(QStringLiteral("/tmp/example.wad")).contains(QStringLiteral("doom-zdbsp")), "A .wad source should offer the Doom pipelines.");
	ok &= expect(!buildPipelineForId(QStringLiteral("nope-not-a-pipeline")), "Unknown pipeline ids should not resolve.");
	return ok;
}

bool runPlanSmoke(const QDir& root)
{
	bool ok = true;
	const QString mapPath = root.filePath(QStringLiteral("pipeline.map"));
	if (!writeFile(mapPath, minimalQuakeMap())) {
		return expect(false, "Failed to write the .map fixture.");
	}

	BuildPipelineRequest request;
	request.pipelineId = QStringLiteral("quake-full");
	request.inputPath = mapPath;
	request.workspaceRootPath = root.path();
	request.stageExtraArguments.insert(QStringLiteral("light"), {QStringLiteral("-extra4")});

	const BuildPipelineResult planned = planBuildPipeline(request);
	ok &= expect(planned.pipeline.id == QStringLiteral("quake-full"), "Planned pipeline should be quake-full.");
	ok &= expect(planned.plannedStageCount == 3 && planned.skippedStageCount == 0, "All three quake-full stages should be planned.");
	ok &= expect(planned.stages.size() == 3, "Planned result should carry one entry per stage.");
	ok &= expect(planned.durationMs < 0, "Planning must not report a run duration.");

	const BuildPipelineStageResult* qbsp = stageById(planned, QStringLiteral("qbsp"));
	const BuildPipelineStageResult* vis = stageById(planned, QStringLiteral("vis"));
	const BuildPipelineStageResult* light = stageById(planned, QStringLiteral("light"));
	if (!qbsp || !vis || !light) {
		return expect(false, "Expected qbsp, vis, and light stage results.");
	}
	ok &= expect(samePath(qbsp->inputPath, mapPath), "qbsp should consume the .map source.");
	ok &= expect(QFileInfo(qbsp->outputPath).suffix().compare(QStringLiteral("bsp"), Qt::CaseInsensitive) == 0, "qbsp should produce a .bsp artifact.");
	ok &= expect(vis->inputPath == qbsp->outputPath, "vis should consume the qbsp output.");
	ok &= expect(light->inputPath == qbsp->outputPath, "light should consume the same .bsp as vis.");
	ok &= expect(planned.finalOutputPath == light->outputPath, "The final output should be the last stage's artifact.");
	ok &= expect(light->plan.arguments.contains(QStringLiteral("-extra4")), "Stage extra arguments should reach the stage command.");
	ok &= expect(!qbsp->plan.arguments.contains(QStringLiteral("-extra4")), "Stage extra arguments must not leak into other stages.");
	ok &= expect(planned.errors.isEmpty(), "A chained stage whose input does not exist yet must not be a planning error.");

	BuildPipelineRequest withoutVis = request;
	withoutVis.disabledStageIds << QStringLiteral("vis");
	const BuildPipelineResult repointed = planBuildPipeline(withoutVis);
	const BuildPipelineStageResult* skippedVis = stageById(repointed, QStringLiteral("vis"));
	const BuildPipelineStageResult* repointedLight = stageById(repointed, QStringLiteral("light"));
	const BuildPipelineStageResult* repointedQbsp = stageById(repointed, QStringLiteral("qbsp"));
	if (!skippedVis || !repointedLight || !repointedQbsp) {
		return expect(false, "Expected all stage entries to survive as results even when skipped.");
	}
	ok &= expect(skippedVis->skipped && !skippedVis->skipReason.isEmpty(), "A disabled stage should be skipped with a reason.");
	ok &= expect(repointed.plannedStageCount == 2 && repointed.skippedStageCount == 1, "Disabling vis should leave two planned stages.");
	ok &= expect(!repointedLight->skipped, "light must still run when vis is disabled.");
	ok &= expect(repointedLight->inputPath == repointedQbsp->outputPath, "light should be re-pointed at the qbsp output when vis is skipped.");

	BuildPipelineRequest explicitOutput = request;
	explicitOutput.outputPath = root.filePath(QStringLiteral("custom/output.bsp"));
	const BuildPipelineResult explicitPlan = planBuildPipeline(explicitOutput);
	const BuildPipelineStageResult* explicitQbsp = stageById(explicitPlan, QStringLiteral("qbsp"));
	const BuildPipelineStageResult* explicitLight = stageById(explicitPlan, QStringLiteral("light"));
	if (!explicitQbsp || !explicitLight) {
		return expect(false, "Expected the explicit-output plan to resolve its stages.");
	}
	ok &= expect(samePath(explicitQbsp->outputPath, explicitOutput.outputPath), "The pipeline output path should be applied to the last stage that accepts one.");
	ok &= expect(explicitLight->inputPath == explicitQbsp->outputPath, "Later in-place stages should follow the explicit output path.");
	ok &= expect(samePath(explicitPlan.finalOutputPath, explicitOutput.outputPath), "The final output path should be the requested output path.");

	BuildPipelineRequest unknown;
	unknown.pipelineId = QStringLiteral("not-a-pipeline");
	unknown.inputPath = mapPath;
	const BuildPipelineResult unknownPlan = planBuildPipeline(unknown);
	ok &= expect(!unknownPlan.errors.isEmpty(), "An unknown pipeline id should produce an error.");
	ok &= expect(unknownPlan.state == OperationState::Failed, "An unknown pipeline id should fail.");
	ok &= expect(!unknownPlan.succeeded(), "An unknown pipeline id must not report success.");
	ok &= expect(unknownPlan.stages.isEmpty(), "An unknown pipeline should not resolve any stage.");

	BuildPipelineRequest missingInput;
	missingInput.pipelineId = QStringLiteral("quake-full");
	missingInput.inputPath = root.filePath(QStringLiteral("absent.map"));
	ok &= expect(!planBuildPipeline(missingInput).errors.isEmpty(), "A missing pipeline input should produce an error.");

	// The q3map2 vis/light profiles may not be registered yet; the pipeline must
	// degrade to a skipped stage instead of failing.
	BuildPipelineRequest quake3;
	quake3.pipelineId = QStringLiteral("quake3-full");
	quake3.inputPath = mapPath;
	quake3.workspaceRootPath = root.path();
	const BuildPipelineResult quake3Plan = planBuildPipeline(quake3);
	const BuildPipelineStageResult* q3vis = stageById(quake3Plan, QStringLiteral("vis"));
	if (!q3vis) {
		return expect(false, "Expected a quake3-full vis stage result.");
	}
	if (compilerProfileForId(QStringLiteral("q3map2-vis"))) {
		ok &= expect(!q3vis->skipped, "q3map2-vis should be planned once the profile exists.");
	} else {
		ok &= expect(q3vis->skipped && !q3vis->skipReason.isEmpty(), "A missing compiler profile should skip its stage with a reason.");
		const BuildPipelineStageResult* q3light = stageById(quake3Plan, QStringLiteral("light"));
		const BuildPipelineStageResult* q3bsp = stageById(quake3Plan, QStringLiteral("bsp"));
		if (q3light && q3bsp && q3light->skipped) {
			ok &= expect(!q3bsp->skipped, "The q3map2 bsp stage should still be planned.");
		}
	}
	return ok;
}

bool runDryRunSmoke(const QDir& root)
{
	bool ok = true;
	const QString mapPath = root.filePath(QStringLiteral("dryrun.map"));
	if (!writeFile(mapPath, minimalQuakeMap())) {
		return expect(false, "Failed to write the dry-run .map fixture.");
	}
	const QString manifestDirectory = root.filePath(QStringLiteral("manifests"));

	BuildPipelineRequest request;
	request.pipelineId = QStringLiteral("quake-full");
	request.inputPath = mapPath;
	request.workspaceRootPath = root.path();
	request.manifestDirectory = manifestDirectory;
	request.dryRun = true;

	int startedCount = 0;
	int finishedCount = 0;
	BuildPipelineCallbacks callbacks;
	callbacks.stageStarted = [&startedCount](int, const BuildPipelineStage&) { ++startedCount; };
	callbacks.stageFinished = [&finishedCount](int, const BuildPipelineStageResult&) { ++finishedCount; };

	const BuildPipelineResult result = runBuildPipeline(request, callbacks);
	ok &= expect(result.dryRun, "A dry run should be reported as a dry run.");
	ok &= expect(result.durationMs >= 0, "A run should report a duration.");
	ok &= expect(startedCount == 3 && finishedCount == 3, "Every planned stage should report start and finish.");
	ok &= expect(result.stages.size() == 3, "A dry run should still report every stage.");
	ok &= expect(!result.cancelled, "An uncancelled dry run should not report cancellation.");

	bool anyStarted = false;
	bool everyPlanReported = true;
	for (const BuildPipelineStageResult& stage : result.stages) {
		anyStarted = anyStarted || stage.run.started;
		everyPlanReported = everyPlanReported && stage.plan.profileFound && !stage.plan.commandLine.isEmpty();
	}
	ok &= expect(!anyStarted, "A dry run must not start any compiler process.");
	ok &= expect(everyPlanReported, "A dry run must still report a resolved plan per stage.");
	ok &= expect(result.failedStageCount == 0, "A dry run of a valid pipeline should not fail a stage.");
	ok &= expect(!QFileInfo::exists(root.filePath(QStringLiteral("dryrun.bsp"))), "A dry run must not produce artifacts.");
	ok &= expect(QFileInfo(QDir(manifestDirectory).filePath(QStringLiteral("quake-full.qbsp.json"))).isFile(), "Each stage should write its manifest into the manifest directory.");

	const QJsonObject json = buildPipelineResultJson(result);
	const QJsonObject jsonAgain = buildPipelineResultJson(result);
	ok &= expect(QJsonDocument(json).toJson() == QJsonDocument(jsonAgain).toJson(), "The JSON serializer should be stable.");
	ok &= expect(json.value(QStringLiteral("pipelineId")).toString() == QStringLiteral("quake-full"), "JSON should carry the pipeline id.");
	ok &= expect(json.value(QStringLiteral("dryRun")).toBool(), "JSON should carry the dry-run flag.");
	ok &= expect(json.value(QStringLiteral("stages")).toArray().size() == 3, "JSON should carry every stage.");
	ok &= expect(json.contains(QStringLiteral("warnings")) && json.contains(QStringLiteral("errors")), "JSON should carry warnings and errors.");

	const QString text = buildPipelineResultText(result);
	ok &= expect(text == buildPipelineResultText(result), "The text serializer should be stable.");
	ok &= expect(text.contains(QStringLiteral("quake-full")) && text.contains(QStringLiteral("qbsp")), "The text report should name the pipeline and its stages.");

	BuildPipelineCallbacks cancelCallbacks;
	cancelCallbacks.cancellationRequested = []() { return true; };
	const BuildPipelineResult cancelled = runBuildPipeline(request, cancelCallbacks);
	ok &= expect(cancelled.cancelled, "A cancelled pipeline should report cancellation.");
	ok &= expect(cancelled.state == OperationState::Cancelled, "A cancelled pipeline should end in the cancelled state.");
	ok &= expect(!cancelled.succeeded(), "A cancelled pipeline must not report success.");
	bool everyStageSkipped = true;
	for (const BuildPipelineStageResult& stage : cancelled.stages) {
		everyStageSkipped = everyStageSkipped && stage.skipped && !stage.skipReason.isEmpty() && !stage.run.started;
	}
	ok &= expect(everyStageSkipped, "Cancelling should skip the remaining stages with a reason.");

	BuildPipelineRequest unknown;
	unknown.pipelineId = QStringLiteral("not-a-pipeline");
	unknown.inputPath = mapPath;
	unknown.dryRun = true;
	const BuildPipelineResult unknownRun = runBuildPipeline(unknown);
	ok &= expect(!unknownRun.errors.isEmpty() && unknownRun.stages.isEmpty(), "Running an unknown pipeline should error without stages.");
	return ok;
}

// A stage that exits 0 has produced its artifact: an error-shaped output line is a report, not a
// pipeline failure, and it must not mask a cancellation either.
bool runNonFatalStageErrorSmoke(const QDir& root)
{
	bool ok = true;
	const QString mapPath = root.filePath(QStringLiteral("nonfatal.map"));
	if (!writeFile(mapPath, minimalQuakeMap())) {
		return expect(false, "Failed to write the non-fatal .map fixture.");
	}

	BuildPipelineRequest request;
	request.pipelineId = QStringLiteral("quake-bsp-only");
	request.inputPath = mapPath;
	request.workspaceRootPath = root.path();
	request.registerOutputs = true;
	request.executableOverrides.push_back({QStringLiteral("ericw-qbsp"), QCoreApplication::applicationFilePath()});
	request.stageExtraArguments.insert(QStringLiteral("qbsp"), {QStringLiteral("--fake-stage-compiler")});

	const BuildPipelineResult result = runBuildPipeline(request);
	ok &= expect(result.failedStageCount == 0, "A stage that exited 0 must not be counted as a failed stage.");
	ok &= expect(result.completedStageCount == 1, "A stage that exited 0 should be counted as completed.");
	ok &= expect(result.errors.isEmpty(), "A non-fatal compiler notice must not become a pipeline error.");
	ok &= expect(result.state == OperationState::Warning, "A pipeline whose every stage exited 0 must not be reported as failed.");
	ok &= expect(result.succeeded(), "A pipeline whose every stage exited 0 should report success.");
	ok &= expect(result.warnings.join('\n').contains(QStringLiteral("malformed extended content flags")), "The compiler notice should still be reported as a warning.");
	const BuildPipelineStageResult* stage = stageById(result, QStringLiteral("qbsp"));
	ok &= expect(stage != nullptr && stage->state == OperationState::Warning, "The stage itself should read Warning rather than Completed.");

	BuildPipelineRequest cancelRequest = request;
	cancelRequest.stageExtraArguments.insert(QStringLiteral("qbsp"), {QStringLiteral("--fake-stage-compiler"), QStringLiteral("--fake-slow-stage")});
	bool sawStageOutput = false;
	BuildPipelineCallbacks cancelCallbacks;
	cancelCallbacks.logEntry = [&sawStageOutput](const CompilerTaskLogEntry& entry) {
		if (entry.message.contains(QStringLiteral("malformed extended content flags"))) {
			sawStageOutput = true;
		}
	};
	cancelCallbacks.cancellationRequested = [&sawStageOutput]() { return sawStageOutput; };
	const BuildPipelineResult cancelled = runBuildPipeline(cancelRequest, cancelCallbacks);
	ok &= expect(sawStageOutput, "Expected the stage output to stream before cancellation was requested.");
	ok &= expect(cancelled.cancelled, "A stage cancelled mid-run should report cancellation.");
	ok &= expect(cancelled.failedStageCount == 0, "Cancelling a stage must not count it as failed.");
	ok &= expect(cancelled.state == OperationState::Cancelled, "Error-shaped partial output must not mask a cancellation.");
	return ok;
}

bool runLaunchSmoke(const QDir& root)
{
	bool ok = true;
	const QString quakeExe = root.filePath(QStringLiteral("quakespasm.exe"));
	const QString bspPath = root.filePath(QStringLiteral("launch.bsp"));
	const QString wadPath = root.filePath(QStringLiteral("launch.wad"));
	if (!writeFile(quakeExe, QByteArrayLiteral("fixture")) || !writeFile(bspPath, QByteArrayLiteral("fixture")) || !writeFile(wadPath, QByteArrayLiteral("fixture"))) {
		return expect(false, "Failed to write the launch fixtures.");
	}

	ok &= expect(gameLaunchProfileIds().contains(QStringLiteral("custom")), "A custom launch profile should exist.");
	ok &= expect(defaultGameLaunchProfileId(GameEngineFamily::IdTech1) == QStringLiteral("doom-source-port"), "idTech1 should default to the Doom launch profile.");
	ok &= expect(defaultGameLaunchProfileId(GameEngineFamily::IdTech2) == QStringLiteral("quake-source-port"), "idTech2 should default to the Quake launch profile.");
	ok &= expect(defaultGameLaunchProfileId(GameEngineFamily::IdTech3) == QStringLiteral("quake3-source-port"), "idTech3 should default to the Quake III launch profile.");
	ok &= expect(defaultGameLaunchProfileId(GameEngineFamily::Unknown) == QStringLiteral("custom"), "An unknown engine family should default to the custom profile.");
	for (const QString& id : gameLaunchProfileIds()) {
		GameLaunchProfile profile;
		ok &= expect(gameLaunchProfileForId(id, &profile) && !profile.displayName.isEmpty(), "Every launch profile should resolve with a display name.");
	}

	GameInstallationProfile installation;
	installation.id = QStringLiteral("fixture");
	installation.gameKey = QStringLiteral("quake");
	installation.engineFamily = GameEngineFamily::IdTech2;
	installation.rootPath = root.path();
	installation.executablePath = quakeExe;

	GameLaunchRequest quakeRequest;
	quakeRequest.mapName = QStringLiteral("e1m1");
	quakeRequest.modDirectory = QStringLiteral("id1");
	const GameLaunchPlan quakePlan = buildGameLaunchPlan(quakeRequest, installation);
	ok &= expect(quakePlan.profileFound && quakePlan.profile.id == QStringLiteral("quake-source-port"), "An empty profile id should fall back to the installation's engine family.");
	ok &= expect(quakePlan.runnable && quakePlan.errors.isEmpty(), "A complete Quake launch plan should be runnable.");
	ok &= expect(quakePlan.arguments.contains(QStringLiteral("-basedir")) && quakePlan.arguments.contains(root.path()), "The Quake plan should pass the base directory.");
	ok &= expect(quakePlan.arguments.contains(QStringLiteral("-game")) && quakePlan.arguments.contains(QStringLiteral("id1")), "The Quake plan should pass the mod directory.");
	ok &= expect(quakePlan.arguments.contains(QStringLiteral("+map")) && quakePlan.arguments.contains(QStringLiteral("e1m1")), "The Quake plan should pass the map name.");
	ok &= expect(quakePlan.workingDirectory == QDir::cleanPath(root.path()), "The working directory should default to the executable's directory.");
	ok &= expect(!quakePlan.commandLine.isEmpty(), "The launch plan should expose a command line.");

	GameLaunchRequest noModRequest = quakeRequest;
	noModRequest.modDirectory.clear();
	const GameLaunchPlan noModPlan = buildGameLaunchPlan(noModRequest, installation);
	ok &= expect(!noModPlan.arguments.contains(QStringLiteral("-game")), "An argument template with an unresolved token should be dropped.");
	ok &= expect(!noModPlan.warnings.isEmpty(), "Dropping an argument template should warn.");
	ok &= expect(noModPlan.runnable, "A dropped optional argument should not block the launch.");

	GameLaunchRequest quake2Request;
	quake2Request.launchProfileId = QStringLiteral("quake2-source-port");
	quake2Request.mapName = QStringLiteral("base1");
	quake2Request.modDirectory = QStringLiteral("baseq2");
	const GameLaunchPlan quake2Plan = buildGameLaunchPlan(quake2Request, installation);
	ok &= expect(quake2Plan.runnable, "A complete Quake II launch plan should be runnable.");
	ok &= expect(quake2Plan.arguments.indexOf(QStringLiteral("+set")) >= 0 && quake2Plan.arguments.contains(QStringLiteral("game")) && quake2Plan.arguments.contains(QStringLiteral("baseq2")), "Quake II should pass +set game <mod>.");
	ok &= expect(quake2Plan.arguments.contains(QStringLiteral("+map")) && quake2Plan.arguments.contains(QStringLiteral("base1")), "Quake II should pass the map name.");

	GameInstallationProfile quake3Installation = installation;
	quake3Installation.engineFamily = GameEngineFamily::IdTech3;
	GameLaunchRequest quake3Request;
	quake3Request.mapName = QStringLiteral("q3dm1");
	quake3Request.modDirectory = QStringLiteral("baseq3");
	const GameLaunchPlan quake3Plan = buildGameLaunchPlan(quake3Request, quake3Installation);
	ok &= expect(quake3Plan.profile.id == QStringLiteral("quake3-source-port"), "idTech3 installs should default to the Quake III profile.");
	ok &= expect(quake3Plan.arguments.contains(QStringLiteral("fs_basepath")) && quake3Plan.arguments.contains(QStringLiteral("fs_game")), "Quake III should pass fs_basepath and fs_game.");
	ok &= expect(quake3Plan.arguments.contains(QStringLiteral("+devmap")) && quake3Plan.arguments.contains(QStringLiteral("q3dm1")), "Quake III should devmap the requested map.");
	ok &= expect(quake3Plan.runnable, "A complete Quake III launch plan should be runnable.");

	GameInstallationProfile doomInstallation = installation;
	doomInstallation.engineFamily = GameEngineFamily::IdTech1;
	GameLaunchRequest doomRequest;
	doomRequest.mapName = QStringLiteral("MAP01");
	doomRequest.bspPath = wadPath;
	const GameLaunchPlan doomPlan = buildGameLaunchPlan(doomRequest, doomInstallation);
	ok &= expect(doomPlan.profile.id == QStringLiteral("doom-source-port"), "idTech1 installs should default to the Doom profile.");
	ok &= expect(doomPlan.arguments.contains(QStringLiteral("-iwad")), "The Doom plan should pass -iwad.");
	ok &= expect(doomPlan.arguments.contains(QStringLiteral("-file")) && doomPlan.arguments.contains(wadPath), "The Doom plan should pass the built PWAD.");
	ok &= expect(doomPlan.arguments.contains(QStringLiteral("-warp")) && doomPlan.arguments.contains(QStringLiteral("MAP01")), "The Doom plan should warp to the requested map.");
	ok &= expect(doomPlan.runnable, "A complete Doom launch plan should be runnable.");

	GameLaunchRequest customRequest;
	customRequest.launchProfileId = QStringLiteral("custom");
	customRequest.extraArguments << QStringLiteral("-window");
	const GameLaunchPlan customPlan = buildGameLaunchPlan(customRequest, installation);
	ok &= expect(customPlan.runnable, "The custom profile should not require a map name.");
	ok &= expect(customPlan.arguments == QStringList{QStringLiteral("-window")}, "The custom profile should pass only the caller's arguments.");

	GameLaunchRequest noMapRequest;
	noMapRequest.launchProfileId = QStringLiteral("quake-source-port");
	const GameLaunchPlan noMapPlan = buildGameLaunchPlan(noMapRequest, installation);
	ok &= expect(!noMapPlan.runnable && !noMapPlan.errors.isEmpty(), "A missing required map name should block the launch.");

	GameInstallationProfile missingExecutable = installation;
	missingExecutable.executablePath = root.filePath(QStringLiteral("absent-engine.exe"));
	const GameLaunchPlan missingPlan = buildGameLaunchPlan(quakeRequest, missingExecutable);
	ok &= expect(!missingPlan.runnable, "A missing executable should not be runnable.");
	ok &= expect(!missingPlan.errors.isEmpty(), "A missing executable should report an error.");
	ok &= expect(missingPlan.state() == OperationState::Failed, "A missing executable should fail the plan state.");

	GameInstallationProfile directoryExecutable = installation;
	directoryExecutable.executablePath = root.path();
	const GameLaunchPlan directoryPlan = buildGameLaunchPlan(quakeRequest, directoryExecutable);
	ok &= expect(!directoryPlan.runnable && !directoryPlan.errors.isEmpty(), "A directory used as the executable should not be runnable.");

	GameLaunchRequest unknownProfile;
	unknownProfile.launchProfileId = QStringLiteral("not-a-launch-profile");
	const GameLaunchPlan unknownPlan = buildGameLaunchPlan(unknownProfile, installation);
	ok &= expect(!unknownPlan.profileFound && !unknownPlan.runnable && !unknownPlan.errors.isEmpty(), "An unknown launch profile should error.");

	// Never actually launch anything from a test; only prove the refusal path.
	qint64 pid = -1;
	QString launchError;
	ok &= expect(!startGameLaunch(missingPlan, &pid, &launchError), "startGameLaunch must refuse a plan that is not runnable.");
	ok &= expect(pid == 0 && !launchError.isEmpty(), "A refused launch should report no pid and an error.");
	ok &= expect(!startGameLaunch(unknownPlan), "startGameLaunch must refuse an unresolved profile.");

	const QJsonObject json = gameLaunchPlanJson(quakePlan);
	ok &= expect(QJsonDocument(json).toJson() == QJsonDocument(gameLaunchPlanJson(quakePlan)).toJson(), "The launch JSON serializer should be stable.");
	ok &= expect(json.value(QStringLiteral("profileId")).toString() == QStringLiteral("quake-source-port"), "Launch JSON should carry the profile id.");
	ok &= expect(json.value(QStringLiteral("runnable")).toBool(), "Launch JSON should carry the runnable flag.");
	ok &= expect(json.value(QStringLiteral("arguments")).toArray().size() == quakePlan.arguments.size(), "Launch JSON should carry every argument.");

	const QString text = gameLaunchPlanText(quakePlan);
	ok &= expect(text == gameLaunchPlanText(quakePlan), "The launch text serializer should be stable.");
	ok &= expect(text.contains(QStringLiteral("quake-source-port")), "The launch report should name the profile.");
	return ok;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	const QStringList appArgs = QCoreApplication::arguments();
	if (appArgs.contains(QStringLiteral("--fake-stage-compiler"))) {
		return runFakeStageCompiler(appArgs);
	}

	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected a valid temporary directory.");
	}
	const QDir root(tempDir.path());
	bool ok = true;
	ok &= runDescriptorSmoke();
	ok &= runPlanSmoke(root);
	ok &= runDryRunSmoke(root);
	ok &= runNonFatalStageErrorSmoke(root);
	ok &= runLaunchSmoke(root);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
