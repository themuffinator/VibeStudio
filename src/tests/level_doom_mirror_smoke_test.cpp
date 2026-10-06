#include "core/doom_preview_geometry.h"
#include "core/level_doom_selection.h"
#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_doom_mirror_test_helpers.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
namespace d = vibestudio::tests::doom;
namespace f = vibestudio::tests::doomMirror;
namespace {
bool ok = true;
bool expect(bool value, const char* message, const QString& detail = {}) {
	if (!value) {
		ok = false;
		std::cerr << message << ": " << detail.toStdString() << '\n';
	}
	return value;
}
double floorArea(const LevelMapDocument& map) {
	double area = 0;
	const auto geometry = buildDoomPreviewGeometry(map);
	expect(geometry.warnings.isEmpty(), "preview closed boundaries", geometry.warnings.join(';'));
	for (const auto& p : geometry.polygons) {
		if (p.target.kind != LevelMaterialKind::SectorFloor) {
			continue;
		}
		double a = 0;
		for (int i = 0; i < p.points.size(); ++i) {
			const auto& x = p.points[i];
			const auto& y = p.points[(i + 1) % p.points.size()];
			a += x.x * y.y - x.y * y.x;
		}
		area += std::abs(a) / 2;
	}
	return area;
}
} // namespace
int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	QString error;
	if (!temp.isValid() || argc < 2) {
		return 1;
	}
	for (bool hexen : {false, true}) {
		const auto bytes = f::fixture(hexen);
		LevelMapDocument source;
		if (!expect(loadLevelMapBytes({"mirror.wad", "MAP01", {}}, bytes, &source, &error), "fixture load", error)) {
			return 1;
		}
		const auto area = floorArea(source);
		expect(area == 38912, "independent two-room + island area");
		expect(setLevelMapSelection(&source, {{LevelMapSelectionKind::DoomLinedef, 0}, {LevelMapSelectionKind::DoomThing, 0}}, &error),
			   "seed selection", error);
		const auto seed = source.selection;
		expect(!flipLevelMapSelection(&source, 0, &error) && error.contains("Connected Geometry") && source.undoStack.isEmpty() &&
				   serializeLevelMap(source).bytes == bytes && source.selection == seed,
			   "attached partial flip refuses atomically", error);
		expect(selectConnectedLevelMapDoomGeometry(&source, &error), "connected expansion", error);
		expect(source.selection.size() == 8 && source.selection.last().kind == LevelMapSelectionKind::DoomThing &&
				   source.undoStack.isEmpty() && serializeLevelMap(source).bytes == bytes,
			   "selection joins shared vertices without changing map or island");
		for (int axis : {0, 1}) {
			auto map = source;
			expect(flipLevelMapSelection(&map, axis, &error), "mirror connected rooms", error);
			expect(map.undoStack.size() == 1 && map.doomGeometryChanged && floorArea(map) == area, "one geometry edit retains floor area");
			for (int i = 0; i < map.doomVertices.size(); ++i) {
				const auto& a = source.doomVertices[i];
				const auto& b = map.doomVertices[i];
				expect(b.x == (i < 6 && axis == 0 ? 256 - a.x : a.x) && b.y == (i < 6 && axis == 1 ? 128 - a.y : a.y),
					   "common pivot, unchanged island");
			}
			const auto saved = serializeLevelMap(map);
			expect(saved.errors.isEmpty() && saved.staleLumps.contains("NODES"), "node invalidation", saved.errors.join(';'));
			for (const auto& name : {"SIDEDEFS", "SECTORS", "BEHAVIOR", "SCRIPTS"}) {
				expect(f::firstLump(saved.bytes, name) == f::firstLump(bytes, name), "native side/sector/script bytes retained");
			}
			const auto oldThings = f::firstLump(bytes, "THINGS"), newThings = f::firstLump(saved.bytes, "THINGS");
			const int thingSize = hexen ? 20 : 10;
			expect(oldThings.mid(thingSize) == newThings.mid(thingSize) &&
					   oldThings.mid(hexen ? 10 : 6, hexen ? 10 : 4) == newThings.mid(hexen ? 10 : 6, hexen ? 10 : 4) &&
					   (!hexen || (oldThings.left(2) == newThings.left(2) && oldThings.mid(6, 2) == newThings.mid(6, 2))),
				   "thing type, flags, height, tid, specials and arguments retained");
			const auto oldLines = f::firstLump(bytes, "LINEDEFS"), newLines = f::firstLump(saved.bytes, "LINEDEFS");
			const int stride = hexen ? 16 : 14;
			for (int i = 0; i < 11; ++i) {
				expect(newLines.mid(i * stride + 4, stride - 4) == oldLines.mid(i * stride + 4, stride - 4),
					   "flags, specials, arguments and sidedef IDs unchanged");
				expect(map.doomLinedefs[i].startVertex == (i < 7 ? source.doomLinedefs[i].endVertex : source.doomLinedefs[i].startVertex) &&
						   map.doomLinedefs[i].endVertex == (i < 7 ? source.doomLinedefs[i].startVertex : source.doomLinedefs[i].endVertex),
					   "reflected endpoints only");
			}
			expect(map.doomThings[0].angle == (axis == 0 ? 150 : 330) && map.doomThings[1].x == 550 && map.doomThings[1].angle == 90,
				   "selected thing heading, unselected thing retained");
			const auto oldLumps = d::lumps(bytes), newLumps = d::lumps(saved.bytes);
			expect(oldLumps.size() == newLumps.size(), "directory count preserved");
			bool foreign = false;
			for (int i = 0; i < oldLumps.size() && i < newLumps.size(); ++i) {
				if (oldLumps[i].name == "PLAYPAL") {
					foreign = true;
				}
				expect(oldLumps[i].name == newLumps[i].name && (!foreign || oldLumps[i].bytes == newLumps[i].bytes),
					   "assets, duplicate entries and second map preserved");
			}
			expect(undoLevelMapEdit(&map, &error) && !map.doomGeometryChanged && serializeLevelMap(map).bytes == bytes,
				   "exact undo restores nodes and source WAD", error);
			expect(redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == saved.bytes, "exact redo", error);
			expect(flipLevelMapSelection(&map, axis, &error), "second reflection", error);
			expect(f::firstLump(serializeLevelMap(map).bytes, "LINEDEFS") == oldLines &&
					   f::firstLump(serializeLevelMap(map).bytes, "VERTEXES") == f::firstLump(bytes, "VERTEXES"),
				   "double reflection restores native geometry");
			LevelMapDocument roundtrip;
			expect(loadLevelMapBytes({"saved.wad", "MAP01", {}}, saved.bytes, &roundtrip, &error) &&
					   roundtrip.doomFormat == source.doomFormat && floorArea(roundtrip) == area,
				   "dialect and orientation survive reparse", error);
		}
		QString group;
		expect(createLevelSceneNode(&source, LevelSceneNodeKind::Group, "locked boundary", {}, &group, &error) &&
				   assignLevelSceneObjects(&source, group, {"linedef:2"}, &error) && setLevelSceneLocked(&source, group, true, &error),
			   "scene lock fixture", error);
		const auto lockedBytes = serializeLevelMap(source).bytes;
		QVector<LevelMapSelectionRef> selectedVertices;
		for (int i = 0; i < 6; ++i) {
			selectedVertices << LevelMapSelectionRef{LevelMapSelectionKind::DoomVertex, i};
		}
		setLevelMapSelection(&source, selectedVertices);
		expect(!flipLevelMapSelection(&source, 0, &error) && error.contains("Unlock") && serializeLevelMap(source).bytes == lockedBytes,
			   "unselected locked boundary refuses reflected vertex selection", error);
		expect(setLevelSceneLocked(&source, group, false, &error), "unlock", error);
		const auto sceneBytes = serializeLevelMap(source).bytes;
		for (auto phase : {LevelPlacementPhase::Preparing, LevelPlacementPhase::Transforming, LevelPlacementPhase::Finalizing,
						   LevelPlacementPhase::Complete}) {
			LevelPlacementRequest request;
			request.operation = LevelPlacementOperation::Mirror;
			bool cancel = false;
			LevelPlacementControl control;
			control.isCancelled = [&] { return cancel; };
			control.progress = [&](const auto& p) { cancel = p.phase == phase; };
			const auto result = prepareLevelPlacement(source, request, control);
			expect(cancel && result.cancelled && !result.succeeded && serializeLevelMap(source).bytes == sceneBytes,
				   "cancel mirror without publishing", result.error);
		}
		LevelPlacementRequest connected;
		connected.operation = LevelPlacementOperation::Mirror;
		connected.connectedGeometry = true;
		setLevelMapSelection(&source, seed);
		expect(prepareLevelPlacement(source, connected).succeeded && source.selection == seed,
			   "combined expansion and reflection private candidate");
		connected.operation = LevelPlacementOperation::SelectConnectedDoom;
		int probes = 0;
		LevelPlacementControl control;
		control.isCancelled = [&] { return ++probes > 10; };
		expect(prepareLevelPlacement(source, connected, control).cancelled && source.selection == seed, "mid-expansion cancellation");
		expect(!flipLevelMapSelection(&source, 2, &error) && serializeLevelMap(source).bytes == sceneBytes, "Doom Z refused", error);
		auto invalid = source;
		invalid.selection << LevelMapSelectionRef{LevelMapSelectionKind::DoomVertex, 999};
		const auto invalidSelection = invalid.selection;
		expect(!selectConnectedLevelMapDoomGeometry(&invalid, &error) && invalid.selection == invalidSelection,
			   "stale seed rejected without selection changes", error);
		invalid = source;
		invalid.doomLinedefs[0].endVertex = 999;
		expect(!selectConnectedLevelMapDoomGeometry(&invalid, &error) && invalid.selection == seed, "missing topology vertex rejected",
			   error);
		invalid = source;
		invalid.doomFormat = LevelMapDoomFormat::Udmf;
		expect(!prepareLevelPlacement(invalid, connected).succeeded && !flipLevelMapSelection(&invalid, 0, &error), "UDMF stays read-only");
		const auto input = temp.filePath(hexen ? "hexen.wad" : "doom.wad"),
				   output = temp.filePath(hexen ? "hexen-flip.wad" : "doom-flip.wad");
		expect(d::write(input, bytes), "CLI input");
		const auto run = [&](bool expand, bool dry) {
			QProcess p;
			QStringList args{"--cli",
							 "--json",
							 "--settings-file",
							 temp.filePath("settings.ini"),
							 "map",
							 "flip",
							 input,
							 "--map",
							 "MAP01",
							 "--object",
							 "linedef:0",
							 "--axis",
							 "x",
							 "--output",
							 output};
			if (expand) {
				args << "--connected";
			}
			if (dry) {
				args << "--dry-run";
			}
			p.start(QString::fromLocal8Bit(argv[1]), args);
			expect(p.waitForFinished(30000), "CLI completed");
			return qMakePair(p.exitCode(), p.readAllStandardOutput());
		};
		expect(run(false, false).first != 0 && !QFile::exists(output), "CLI unsafe flip writes nothing");
		const auto dry = run(true, true);
		expect(dry.first == 0 && !QFile::exists(output), "CLI dry run writes nothing", QString::fromUtf8(dry.second));
		const auto published = run(true, false);
		expect(published.first == 0 && QFile::exists(output), "CLI connected mirror saves", QString::fromUtf8(published.second));
		const auto report = QJsonDocument::fromJson(published.second).object();
		expect(report.value("connectedGeometry").toBool() && report.value("textureLockPolicy").toString() == "native-offsets",
			   "CLI reports native texture policy and explicit expansion");
		expect(report.value("save").toObject().value("staleLumps").toArray().contains("NODES"), "structured node rebuild requirement");
		LevelMapDocument loaded;
		expect(loadLevelMap({output, "MAP01", {}}, &loaded, &error) && floorArea(loaded) == area && loaded.doomLinedefs[0].startVertex == 1,
			   "CLI output orientation", error);
		QFile original(input);
		expect(original.open(QIODevice::ReadOnly), "open unchanged CLI source");
		expect(original.readAll() == bytes, "CLI source unchanged");
	}
	// All boundaries of a sector: a concavity, a hole and a disconnected island.
	auto polygon = d::topology();
	d::loop(polygon, {{0, 0}, {0, 128}, {64, 128}, {64, 96}, {128, 96}, {128, 0}});
	d::loop(polygon, {{16, 16}, {48, 16}, {48, 48}, {16, 48}});
	d::loop(polygon, {{256, 0}, {256, 64}, {320, 64}, {320, 0}});
	setLevelMapSelection(&polygon, {{LevelMapSelectionKind::DoomSector, 0}});
	const auto area = floorArea(polygon);
	expect(flipLevelMapSelection(&polygon, 0, &error) && floorArea(polygon) == area, "sector reflection retains concavity, hole and island",
		   error);
	std::cout << "Doom/Hexen native reflection, history, scene locks, cancellation and CLI checks finished\n";
	return ok ? 0 : 1;
}
