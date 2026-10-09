#include "core/level_build_workspace.h"
#include "core/package_draft.h"
#include "tests/level_material_test_helpers.h"
#include "tests/package_subset_test_helpers.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* message, const QString& error = {}) {
	if (!value) {
		std::cerr << message << ": " << error.toStdString() << '\n';
		ok = false;
	}
	return value;
}
} // namespace
int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	if (argc > 1) {
		std::cout << "Prepared-workspace compiler discovery fixture\n";
		return 0;
	}
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	QString error;
	LevelMapDocument map;
	if (!expect(tests::createMaterialFixture(temp.path(), &map, &error), "generated asset/map fixture", error)) {
		return 1;
	}
	const auto source = temp.filePath("original.map");
	expect(tests::putMaterialFile(source, serializeLevelMap(map).bytes), "write original map");
	expect(loadLevelMap({source, {}, "idtech3"}, &map, &error), "load original", error);
	const auto sourceBytes = subset_test::get(source);
	selectLevelMapObject(&map, "brush:0");
	expect(moveLevelMapSelection(&map, 3, 4, 5, {true, false}, &error), "unsaved geometry", error);
	const auto currentBytes = serializeLevelMap(map).bytes;
	const auto mapRevision = map.revision;
	PackageArchive archive;
	expect(archive.load(temp.filePath("assets"), &error), "load assets", error);
	PackageStagingModel draft;
	expect(draft.loadBaseArchive(archive, &error), "load package draft", error);
	const auto replacement = tests::materialImage(96, 48, true);
	expect(draft.addBytes(replacement, "textures/studio/grid.png", &error, PackageStageConflictResolution::ReplaceExisting), "staged image",
		   error);
	expect(draft.addBytes("unreferenced but captured", "extra/retained.txt", &error), "complete snapshot member", error);
	expect(draft.addBytes("stale", "maps/studio_build.bsp", &error), "old output", error);
	expect(draft.addBytes("old map", "maps/studio_build.map", &error), "old map", error);
	const auto draftRevision = draft.revision();
	PackageStagingArchive snapshot(draft);
	LevelBuildWorkspaceRequest request;
	request.directory = temp.filePath("prepared");
	request.dryRun = true;
	const auto dry = prepareLevelBuildWorkspace(map, snapshot, request);
	expect(dry.ready && dry.dryRun && !QFileInfo::exists(request.directory), "dry run creates no workspace", dry.error);
	request.dryRun = false;
	const auto prepared = prepareLevelBuildWorkspace(map, snapshot, request);
	if (!expect(prepared.ready && !prepared.dryRun, "prepare workspace", prepared.error)) {
		return 1;
	}
	expect(prepared.inputs.size() == archive.summary().fileCount + 2, "complete file snapshot plus current map");
	expect(prepared.omittedPaths.contains("maps/studio_build.map") && prepared.omittedPaths.contains("maps/studio_build.bsp"),
		   "omit old compiler inputs/outputs");
	expect(subset_test::get(prepared.inputPath()) == currentBytes && subset_test::get(source) == sourceBytes,
		   "current map bytes and source protection");
	expect(map.revision == mapRevision && draft.revision() == draftRevision && serializeLevelMap(map).bytes == currentBytes,
		   "live map/draft unchanged");
	expect(subset_test::get(QDir(prepared.assetsPath()).filePath("textures/studio/grid.png")) == replacement,
		   "staged image bytes captured");
	expect(subset_test::get(QDir(prepared.assetsPath()).filePath("extra/retained.txt")) == QByteArray("unreferenced but captured"),
		   "whole snapshot retained");
	expect(!QFileInfo::exists(QDir(prepared.assetsPath()).filePath("maps/studio_build.bsp")), "stale BSP omitted");
	const auto loaded = readLevelBuildWorkspace(request.directory);
	expect(loaded.ready && loaded.inputs.size() == prepared.inputs.size() && verifyLevelBuildWorkspace(loaded, &error),
		   "persistent inventory verifies", error);
	for (const auto& input : prepared.inputs) {
		const auto bytes = subset_test::get(QDir(prepared.directory).filePath(input.path));
		expect(static_cast<quint64>(bytes.size()) == input.bytes &&
				   QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) == input.sha256,
			   "independent input hash");
	}
	BuildPipelineRequest pipeline;
	pipeline.pipelineId = "quake3-full";
	pipeline.dryRun = true;
	pipeline.executableOverrides = {{"vibemap3", app.applicationFilePath()}};
	pipeline.stageExtraArguments["light"] = {"-fast"};
	expect(configureLevelBuildPipeline(loaded, &pipeline, &error), "configure workspace paths", error);
	const auto configured = pipeline;
	expect(configureLevelBuildPipeline(loaded, &pipeline, &error) && pipeline.stageExtraArguments == configured.stageExtraArguments,
		   "idempotent compiler planning", error);
	const auto plan = runLevelBuildWorkspace(loaded, pipeline);
	expect(plan.succeeded() && plan.dryRun && plan.stages.size() == 3, "shared pipeline dry run", plan.errors.join('\n'));
	for (const auto& stage : plan.stages) {
		expect(stage.plan.arguments.contains("-fs_basepath") && stage.plan.arguments.contains(QDir(loaded.directory).filePath("game")),
			   "each stage uses captured assets");
	}
	auto conflicting = BuildPipelineRequest{};
	conflicting.pipelineId = "quake3-full";
	conflicting.stageExtraArguments["bsp"] = {"-fs_basepath", temp.path()};
	expect(!configureLevelBuildPipeline(loaded, &conflicting, &error), "refuse asset path override");
	for (const auto& flag : QStringList{"-lightmapdir", "-tempname", "-rename"}) {
		conflicting.stageExtraArguments.clear();
		conflicting.stageExtraArguments["light"] = {flag, temp.path()};
		expect(!configureLevelBuildPipeline(loaded, &conflicting, &error), "refuse untracked output path override");
	}
	const auto texture = QDir(loaded.assetsPath()).filePath("textures/studio/grid.png");
	auto damaged = replacement;
	damaged[damaged.size() / 2] ^= 1;
	expect(tests::putMaterialFile(texture, damaged) && !verifyLevelBuildWorkspace(loaded, &error), "reject same-sized asset tampering");
	int started = 0;
	BuildPipelineCallbacks callbacks;
	callbacks.stageStarted = [&](int, const auto&) { ++started; };
	expect(!runLevelBuildWorkspace(loaded, pipeline, callbacks).succeeded() && started == 0,
		   "changed inputs stop before compiler execution");
	expect(tests::putMaterialFile(texture, replacement), "restore test asset");
	const auto extra = QDir(loaded.assetsPath()).filePath("scripts/injected.shader");
	expect(tests::putMaterialFile(extra, "// unrecorded") && !verifyLevelBuildWorkspace(loaded, &error), "reject new shader inputs");
	expect(QFile::remove(extra), "remove owned extra fixture");
	expect(tests::putMaterialFile(QDir(loaded.directory).filePath("home/baseq3/extra.pk3"), "extra") &&
			   !verifyLevelBuildWorkspace(loaded, &error),
		   "reject compiler home injection");
	expect(QFile::remove(QDir(loaded.directory).filePath("home/baseq3/extra.pk3")), "remove owned home fixture");
	expect(verifyLevelBuildWorkspace(loaded, &error), "restored inventory verifies", error);
	bool stopAfterStages = false;
	BuildPipelineCallbacks cancelVerification;
	cancelVerification.logEntry = [&](const auto& entry) { stopAfterStages |= entry.message.contains("Checking that compiler inputs"); };
	cancelVerification.cancellationRequested = [&] { return stopAfterStages; };
	const auto interruptedVerification = runLevelBuildWorkspace(loaded, pipeline, cancelVerification);
	expect(interruptedVerification.cancelled && interruptedVerification.state == OperationState::Cancelled &&
			   interruptedVerification.registeredOutputPaths.isEmpty(),
		   "post-run verification cancellation remains cancelled");
	expect(!prepareLevelBuildWorkspace(map, snapshot, request).ready && subset_test::get(prepared.inputPath()) == currentBytes,
		   "never overwrite existing workspace");
	request.directory = temp.filePath("assets/inside");
	expect(!prepareLevelBuildWorkspace(map, snapshot, request).ready && !QFileInfo::exists(request.directory), "protect asset folder");
	request.directory = temp.filePath("too-small");
	request.maximumBytes = 1;
	expect(!prepareLevelBuildWorkspace(map, snapshot, request).ready && !QFileInfo::exists(request.directory),
		   "bounded snapshot before output");
	request.maximumBytes = 4ULL * 1024 * 1024 * 1024;
	request.directory = temp.filePath("cancelled");
	bool cancel = false;
	PackageReadControl control;
	control.isCancelled = [&] { return cancel; };
	control.progress = [&](const QString& name, qint64, qint64) {
		if (name == "models/studio/prop.md3") {
			cancel = true;
		}
	};
	const auto cancelled = prepareLevelBuildWorkspace(map, snapshot, request, control);
	expect(cancelled.cancelled && !cancelled.ready && !QFileInfo::exists(request.directory), "cancel streamed preparation atomically",
		   cancelled.error);
	expect(QDir(temp.path()).entryList({".vibestudio-build-*"}, QDir::Dirs | QDir::Hidden).isEmpty(), "failed private copies cleaned");
	// Portable manifests reject escaping paths and excessive/invalid inventory data.
	const auto manifestBytes = subset_test::get(loaded.manifestPath());
	auto manifest = QJsonDocument::fromJson(manifestBytes).object();
	auto inputs = manifest.value("inputs").toArray();
	auto badInput = inputs[0].toObject();
	badInput["path"] = "../outside";
	inputs[0] = badInput;
	manifest["inputs"] = inputs;
	expect(tests::putMaterialFile(loaded.manifestPath(), QJsonDocument(manifest).toJson()) &&
			   !readLevelBuildWorkspace(loaded.directory).ready,
		   "reject escaping manifest");
	expect(tests::putMaterialFile(loaded.manifestPath(), manifestBytes), "restore manifest");
	// A retained draft snapshot stays independent of later studio edits.
	expect(draft.addBytes("later", "extra/retained.txt", &error, PackageStageConflictResolution::ReplaceExisting), "later draft change",
		   error);
	expect(verifyLevelBuildWorkspace(loaded, &error), "workspace independent of later draft changes", error);
	return ok ? 0 : 1;
}
