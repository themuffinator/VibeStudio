#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/level_placement_test_helpers.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <future>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool valid, const char* message, const QString& error = {}) {
	if (!valid) {
		std::cerr << message << ": " << error.toStdString() << '\n';
		ok = false;
	}
	return valid;
}
} // namespace
int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	LevelMapDocument source;
	QString error;
	if (!tests::createGeometryFixture(512, &source, &error)) {
		return 1;
	}
	QVector<LevelMapSelectionRef> selected;
	for (const auto& brush : source.brushes) {
		selected << LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush, brush.id};
	}
	setLevelMapSelection(&source, selected);
	expect(moveLevelMapSelection(&source, 3, 3, 3, &error), "off-grid source", error);
	const auto original = serializeLevelMap(source).bytes;
	const auto revision = source.revision;
	const auto text = levelMapSelectionText(source);
	for (auto operation : {LevelPlacementOperation::Snap, LevelPlacementOperation::Duplicate, LevelPlacementOperation::Paste}) {
		LevelPlacementRequest request;
		request.operation = operation;
		request.offset = {16, 0, 0, true};
		request.text = text;
		for (auto phase : {LevelPlacementPhase::Preparing, LevelPlacementPhase::Parsing, LevelPlacementPhase::Transforming,
						   LevelPlacementPhase::Inserting, LevelPlacementPhase::Finalizing, LevelPlacementPhase::Complete}) {
			if (phase == LevelPlacementPhase::Parsing && operation != LevelPlacementOperation::Paste) {
				continue;
			}
			if (phase == LevelPlacementPhase::Inserting && operation == LevelPlacementOperation::Snap) {
				continue;
			}
			bool stopped = false;
			LevelPlacementControl control;
			control.isCancelled = [&] { return stopped; };
			control.progress = [&](const auto& progress) {
				if (progress.phase == phase && (phase != LevelPlacementPhase::Transforming || progress.completed >= 32)) {
					stopped = true;
				}
			};
			auto cancelled = prepareLevelPlacement(source, request, control);
			expect(stopped && cancelled.cancelled && !cancelled.succeeded && cancelled.document.format == LevelMapFormat::Unknown,
				   "cancellation discards candidate at every phase", cancelled.error);
			expect(source.revision == revision && source.selection == selected && serializeLevelMap(source).bytes == original,
				   "cancelled preparation leaves source and selection unchanged");
		}
		LevelPlacementPhase last = LevelPlacementPhase::Preparing;
		LevelPlacementControl control;
		control.progress = [&](const auto& progress) { last = progress.phase; };
		auto result = prepareLevelPlacement(source, request, control);
		expect(result.succeeded && !result.cancelled && last == LevelPlacementPhase::Complete, "completed candidate", result.error);
		const auto delta = operation == LevelPlacementOperation::Snap ? LevelMapVec3{-3, -3, -3, true} : request.offset;
		const auto index = operation == LevelPlacementOperation::Snap ? 0 : source.brushes.size();
		if (result.succeeded) {
			expect(tests::placementUvsMatch(source.brushes.first(), result.document.brushes[index], delta), "candidate UVs");
			const auto after = serializeLevelMap(result.document).bytes;
			expect(undoLevelMapEdit(&result.document, &error) && serializeLevelMap(result.document).bytes == original &&
					   redoLevelMapEdit(&result.document, &error) && serializeLevelMap(result.document).bytes == after,
				   "candidate exact history", error);
		}
	}
	// Cancellation inside a long parser pass, not just at a phase transition.
	LevelPlacementRequest paste;
	paste.operation = LevelPlacementOperation::Paste;
	paste.text = text;
	paste.offset = {16, 0, 0, true};
	bool parsing = false;
	int probes = 0;
	LevelPlacementControl parseControl;
	parseControl.progress = [&](const auto& p) { parsing = p.phase == LevelPlacementPhase::Parsing; };
	parseControl.isCancelled = [&] { return parsing && ++probes > 100; };
	QElapsedTimer timer;
	timer.start();
	const auto cancelledParse = prepareLevelPlacement(source, paste, parseControl);
	expect(cancelledParse.cancelled && probes > 100 && timer.elapsed() < 2000, "bounded mid-parser cancellation", cancelledParse.error);
	std::cout << "Parser cancellation ms: " << timer.elapsed() << '\n';
	// Simultaneous workers must not inherit each other's cancellation state.
	LevelPlacementRequest duplicate;
	duplicate.offset = {16, 0, 0, true};
	auto stoppedWorker = std::async(std::launch::async, [&] {
		LevelPlacementControl c;
		c.isCancelled = [] { return true; };
		return prepareLevelPlacement(source, duplicate, c);
	});
	auto liveWorker = std::async(std::launch::async, [&] { return prepareLevelPlacement(source, duplicate); });
	expect(stoppedWorker.get().cancelled && liveWorker.get().succeeded, "independent worker controls");
	// A nested preparation restores the caller's scope, including on cancellation.
	bool nested = false;
	LevelPlacementControl outer;
	outer.progress = [&](const auto&) {
		if (nested) {
			return;
		}
		nested = true;
		LevelPlacementControl inner;
		inner.isCancelled = [] { return true; };
		expect(prepareLevelPlacement(source, duplicate, inner).cancelled, "nested cancellation");
	};
	expect(prepareLevelPlacement(source, duplicate, outer).succeeded, "outer control restored");
	auto legacy = source;
	expect(duplicateLevelMapSelection(&legacy, 16, 0, 0, {true, false}, &error), "no control leaks into direct authoring", error);
	// Read-only source-binding checks must survive successive dirty edits and a
	// map-wide classic -> Valve conversion before anything has been saved.
	LevelMapDocument chained;
	expect(loadLevelMapBytes({"chain.map", {}, "idtech3"}, tests::placementFixture("classic"), &chained, &error), "chain fixture", error);
	selectLevelMapObject(&chained, "brush:0");
	LevelMapRotationRequest rotation;
	rotation.axis = 2;
	rotation.degrees = 37;
	rotation.textureLock = true;
	rotation.allowValve220 = true;
	expect(rotateLevelMapSelection(&chained, rotation, &error), "convert dirty source", error);
	const auto beforeMove = chained.brushes.first();
	expect(moveLevelMapSelection(&chained, 13.5, -7.25, 4, {true, true}, &error), "move converted unsaved source", error);
	expect(tests::placementUvsMatch(beforeMove, chained.brushes.first(), {13.5, -7.25, 4, true}), "dirty conversion UVs");
	expect(duplicateLevelMapSelection(&chained, 64, 0, 0, {true, true}, &error), "copy converted source", error);
	const auto firstCopy = chained.brushes.last();
	expect(duplicateLevelMapSelection(&chained, 64, 0, 0, {true, true}, &error), "copy of dirty copy", error);
	LevelMapDocument reloaded;
	expect(loadLevelMapBytes({"chain.map", {}, "idtech3"}, serializeLevelMap(chained).bytes, &reloaded, &error), "reload edit chain",
		   error);
	expect(tests::placementUvsMatch(firstCopy, reloaded.brushes.last(), {64, 0, 0, true}), "saved chained UV oracle");
	QString node;
	expect(createLevelSceneNode(&source, LevelSceneNodeKind::Group, "Locked", {}, &node, &error) &&
			   assignLevelSceneObjects(&source, node, {"brush:0"}, &error) && setLevelSceneLocked(&source, node, true, &error),
		   "locked source", error);
	const auto protectedBytes = serializeLevelMap(source).bytes;
	const auto refused = prepareLevelPlacement(source, duplicate);
	expect(!refused.succeeded && !refused.cancelled && refused.error.contains("Unlock") &&
			   serializeLevelMap(source).bytes == protectedBytes,
		   "scene lock refusal survives preparation", refused.error);
	return ok ? 0 : 1;
}
