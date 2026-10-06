#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/level_surface_test_helpers.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* message, const QString& error = {})
{
	if (!value) { std::cerr << message << ": " << error.toStdString() << '\n'; ok = false; }
	return value;
}
void verifyPreviewPrefixes()
{
	// Compare each displayed group with a complete small reference document.
	// Exercise both sides of every cap, sparse IDs and unrelated object fields.
	for (const bool doom : {false, true}) {
		for (const int count : {0, 11, 12, 13, 15, 16, 17, 23, 24, 25, 10000}) {
			LevelMapDocument document;
			document.format = doom ? LevelMapFormat::DoomWad : LevelMapFormat::Quake3Map;
			for (int i = 0; i < count; ++i) {
				if (doom) {
					LevelMapDoomLinedef line; line.id = 3*i; line.tag = i; document.doomLinedefs << line;
					LevelMapDoomThing thing; thing.id = 3*i; thing.x = i; document.doomThings << thing;
					LevelMapDoomSector sector; sector.id = 3*i; sector.floorHeight = i; document.doomSectors << sector;
				} else {
					LevelMapBrush brush; brush.id = 3*i; brush.faceCount = i; document.brushes << brush;
					LevelMapPatch patch; patch.id = 3*i; patch.textureName = QString::number(i); document.patches << patch;
					LevelMapEntity entity; entity.id = 3*i; entity.className = QString::number(i); document.entities << entity;
				}
			}
			auto prefix = document;
			const std::array<int,3> limits = doom ? std::array<int,3>{24,16,12} : std::array<int,3>{24,12,16};
			const QStringList labels = doom ? QStringList{"linedefs","things","sectors"} : QStringList{"brushes","patches","entities"};
			if (doom) {
				prefix.doomLinedefs.resize(std::min(count,24)); prefix.doomThings.resize(std::min(count,16));
				prefix.doomSectors.resize(std::min(count,12));
			} else {
				prefix.brushes.resize(std::min(count,24)); prefix.patches.resize(std::min(count,12));
				prefix.entities.resize(std::min(count,16));
			}
			const auto reference = levelMapViewLines(prefix);
			QStringList expected{reference.first()}; int from = 1;
			for (int group = 0; group < 3; ++group) {
				const auto shown = std::min(count, limits[group]);
				expected << reference.mid(from, shown); from += shown;
				if (count > limits[group]) {
					expected << QStringLiteral("... showing %1 of %2 %3.").arg(limits[group]).arg(count).arg(labels[group]);
				}
			}
			if (count == 0) { expected = reference; }
			QElapsedTimer elapsed; elapsed.start();
			expect(levelMapViewLines(document) == expected, "bounded preview retains exact prefixes and total counts");
			if (count == 10000) {
				std::cout << QJsonDocument(QJsonObject{{"preview_records_per_kind",count}, {"doom",doom},
					{"format_ms",elapsed.nsecsElapsed()/1e6}}).toJson(QJsonDocument::Compact).constData() << '\n';
			}
		}
	}
}
} // namespace
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	verifyPreviewPrefixes();
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::AddBrush;
	request.primitive.mins = {220, -96, 16, true};
	request.primitive.maxs = {348, 96, 80, true};
	request.primitive.texture = QStringLiteral("studio/new");
	for (const auto* dialect : {"classic", "valve220", "brushDef", "brushDef3"}) {
		LevelMapDocument source;
		if (!expect(loadLevelMapBytes({"jobs.map", {}, "idtech3"}, tests::surfaceFixture(dialect), &source, &error), "fixture", error)) {
			return 1;
		}
		QString layer;
		expect(createLevelSceneNode(&source, LevelSceneNodeKind::Layer, "New brushes", {}, &layer, &error), "creation layer", error);
		source.activeSceneNode = layer;
		selectLevelMapObject(&source, "brush:0");
		const auto original = serializeLevelMap(source).bytes;
		for (const auto* shape : {"box", "wedge", "cylinder", "cone", "sphere"}) {
			request.primitive.shape = QString::fromLatin1(shape);
			auto direct = source;
			int id = -1;
			expect(addLevelMapBrushPrimitive(&direct, request.primitive, &id, &error), "direct authoring", error);
			auto result = prepareLevelPlacement(source, request);
			if (!expect(result.succeeded && !result.cancelled, "prepared primitive", result.error)) { continue; }
			const auto saved = serializeLevelMap(result.document).bytes;
			expect(saved == serializeLevelMap(direct).bytes && result.document.selection == direct.selection &&
				result.document.undoStack.size() == source.undoStack.size() + 1 &&
				levelSceneMembership(result.document.scene, QStringLiteral("brush:%1").arg(id)) == layer,
				"worker matches direct/CLI semantics, destination and single undo");
			const auto& brush = result.document.brushes.last();
			expect(brush.mins.x == 220 && brush.maxs.y == 96 && brush.maxs.z == 80 && brush.primitiveKind == dialect,
				"independent bounds and native dialect oracle");
			expect(undoLevelMapEdit(&result.document, &error) && serializeLevelMap(result.document).bytes == original &&
				redoLevelMapEdit(&result.document, &error) && serializeLevelMap(result.document).bytes == saved, "exact history", error);
			LevelMapDocument reloaded;
			expect(loadLevelMapBytes({"saved.map", {}, "idtech3"}, saved, &reloaded, &error) && reloaded.brushes.size() == 2 &&
				levelSceneMembership(reloaded.scene, "brush:1") == layer, "saved candidate preserves destination", error);
			expect(serializeLevelMap(source).bytes == original && source.selection.size() == 1 && source.selectedObjectId == 0,
				"source remains unchanged");
		}
	}
	LevelMapDocument source;
	if (!expect(tests::createGeometryFixture(512, &source, &error), "cancellation fixture", error)) { return 1; }
	// Retain a redo branch and a nonempty selection, so cancellation must preserve both.
	selectLevelMapObject(&source, "brush:0");
	expect(moveLevelMapSelection(&source, 3, 0, 0, &error) && undoLevelMapEdit(&source, &error), "redo fixture", error);
	const auto original = serializeLevelMap(source).bytes;
	const auto revision = source.revision;
	for (const auto phase : {LevelPlacementPhase::Preparing, LevelPlacementPhase::Building, LevelPlacementPhase::Inserting,
		LevelPlacementPhase::Finalizing, LevelPlacementPhase::Complete}) {
		bool stopped = false;
		LevelPlacementControl control;
		control.progress = [&](const auto& progress) { if (progress.phase == phase) { stopped = true; } };
		control.isCancelled = [&] { return stopped; };
		const auto result = prepareLevelPlacement(source, request, control);
		expect(stopped && result.cancelled && !result.succeeded && result.document.format == LevelMapFormat::Unknown,
			"phase cancellation discards candidate", result.error);
		expect(serializeLevelMap(source).bytes == original && source.revision == revision && source.redoStack.size() == 1 &&
			source.selectedObjectId == 0, "cancel preserves bytes, history and selection");
	}
	bool building = false;
	int probes = 0;
	LevelPlacementControl control;
	control.progress = [&](const auto& progress) { building = progress.phase == LevelPlacementPhase::Building; };
	control.isCancelled = [&] { return building && ++probes > 60; };
	expect(prepareLevelPlacement(source, request, control).cancelled && probes > 60, "mid-geometry cancellation");
	request.primitive.maxs = request.primitive.mins;
	expect(!prepareLevelPlacement(source, request).succeeded && serializeLevelMap(source).bytes == original, "invalid request rollback");
	request.primitive.maxs = {348, 96, 80, true};
	QString locked, destination;
	expect(createLevelSceneNode(&source, LevelSceneNodeKind::Layer, "Locked", {}, &locked, &error) &&
		assignLevelSceneObjects(&source, locked, {"brush:0"}, &error) &&
		createLevelSceneNode(&source, LevelSceneNodeKind::Group, "Destination", locked, &destination, &error) &&
		setLevelSceneLocked(&source, locked, true, &error), "locked hierarchy", error);
	source.activeSceneNode = destination;
	const auto refused = prepareLevelPlacement(source, request);
	expect(!refused.succeeded && !refused.cancelled && refused.error.contains("Unlock"), "inherited creation lock", refused.error);
	source.activeSceneNode.clear();
	expect(prepareLevelPlacement(source, request).succeeded, "unrelated locked geometry stays intact");
	LevelMapDocument unsupported; unsupported.format = LevelMapFormat::DoomWad;
	expect(!prepareLevelPlacement(unsupported, request).succeeded, "unsupported map refuses brush creation");
	// Preparation timings exclude publication, widgets, file I/O and real-map complexity.
	QJsonArray samples;
	request.primitive.shape = QStringLiteral("box");
	for (const int count : {100, 1000, 10000}) {
		LevelMapDocument large;
		if (!expect(tests::createGeometryFixture(count, &large, &error), "benchmark fixture", error)) { continue; }
		for (int repeat = 0; repeat < 3; ++repeat) {
			QElapsedTimer timer; timer.start();
			const auto result = prepareLevelPlacement(large, request);
			const auto ms = timer.nsecsElapsed() / 1e6;
			expect(result.succeeded && result.document.brushes.size() == count + 1 && large.brushes.size() == count,
				"large-map preparation", result.error);
			samples.append(QJsonObject{{"brushes", count}, {"repeat", repeat}, {"prepare_ms", ms}});
		}
	}
	std::cout << QJsonDocument(QJsonObject{{"samples", samples}}).toJson(QJsonDocument::Compact).constData() << '\n';
	return ok ? 0 : 1;
}
