// Checks the layer commands (core/level_scene_layers.h): NetRadiant Custom's
// Expand Selection to Layers and its layers menu, TrenchBroom's Move to Layer.
// Expansion adds the visible, unlocked objects of the selected objects' layers,
// nested groups included, keeps closed groups whole, follows the open group and
// records one selection step only when asked. Move to Layer is one scene undo
// step back to the exact bytes; closed groups move whole, a brush takes its
// brush entity along, locks, stale metadata and an open group refuse it, and a
// hidden destination drops the moved objects from the selection. Doom maps
// have layers too. With the vibestudio binary as its argument it drives
// `editor scene select --expand-layers` and `editor scene move-to-layer`.

#include "core/level_document.h"
#include "core/level_map.h"
#include "core/level_object_selection.h"
#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_layers.h"
#include "core/level_scene_locks.h"
#include "tests/level_scene_layers_test_helpers.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;

namespace {

bool ok = true;

bool expect(bool condition, const char* label, const QString& detail = QString())
{
	if (!condition) {
		ok = false;
		std::cerr << "FAIL: " << label;
		if (!detail.isEmpty()) {
			std::cerr << " (" << detail.toStdString() << ")";
		}
		std::cerr << std::endl;
	}
	return condition;
}

QByteArray bytesOf(const LevelMapDocument& document)
{
	return serializeLevelMap(document).bytes;
}

QSet<QString> keys(const QVector<LevelMapSelectionRef>& selection)
{
	QSet<QString> result;
	for (const auto& ref : selection) {
		result.insert(levelMapSelectionRefId(ref));
	}
	return result;
}

QString text(const QSet<QString>& values)
{
	QStringList sorted(values.cbegin(), values.cend());
	sorted.sort();
	return sorted.join(QLatin1Char(','));
}

QByteArray readFile(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "temporary directory")) {
		return 1;
	}
	const QByteArray source = tests::sceneLayersFixtureBytes();
	const QString path = temp.filePath(QStringLiteral("layers.map"));
	LevelMapDocument baseline;
	QString error;
	if (!expect(!source.isEmpty() && writeFile(path, source) && loadLevelMap({path, {}, {}}, &baseline, &error), "fixture", error)) {
		return 1;
	}
	const auto brush = [&](const char* name) { return QStringLiteral("brush:%1").arg(tests::sceneLayersBrush(baseline, QLatin1String(name))); };
	const auto entity = [&](const char* className) { return QStringLiteral("entity:%1").arg(tests::sceneLayersEntity(baseline, QLatin1String(className))); };
	const auto ref = [](const QString& key) {
		const auto colon = key.indexOf(QLatin1Char(':'));
		return LevelMapSelectionRef {levelMapSelectionKindFromId(key.left(colon)), key.mid(colon + 1).toInt()};
	};
	const auto refs = [&](const QStringList& list) {
		QVector<LevelMapSelectionRef> result;
		for (const auto& key : list) {
			result << ref(key);
		}
		return result;
	};
	const QString architecture = tests::sceneLayersNode(baseline, QStringLiteral("Architecture"));
	const QString pillars = tests::sceneLayersNode(baseline, QStringLiteral("Pillars"));
	const QString lights = tests::sceneLayersNode(baseline, QStringLiteral("Lights"));
	const QString crates = tests::sceneLayersNode(baseline, QStringLiteral("Crates"));
	expect(!architecture.isEmpty() && !pillars.isEmpty() && !lights.isEmpty() && !crates.isEmpty(), "fixture scene");
	const QString world = QStringLiteral("entity:%1").arg(tests::sceneLayersEntity(baseline, QStringLiteral("worldspawn")));

	// Each object's layer: its own membership, else its entity's, up through groups.
	{
		const auto layers = levelSceneObjectLayers(baseline, baseline.scene);
		expect(layers.value(brush("w1")) == architecture && layers.value(brush("g1")) == architecture && layers.value(brush("d1")) == architecture
				&& layers.value(entity("func_door")) == architecture,
			"Architecture holds its brushes, its group and its door's brushes");
		expect(layers.value(entity("light")) == lights && layers.value(brush("floor")).isEmpty() && layers.value(brush("h1")).isEmpty()
				&& layers.value(entity("info_player_start")).isEmpty() && !layers.contains(world),
			"Lights, Default and its root group; worldspawn has no layer");
		const auto nodes = levelSceneNodeLayers(baseline.scene);
		expect(nodes.value(pillars) == architecture && nodes.value(crates).isEmpty() && nodes.value(lights) == lights, "node layers");
	}

	// Expand Selection to Layers.
	const QSet<QString> architectureUnits {brush("w1"), brush("w2"), brush("g1"), brush("g2"), brush("d1"), brush("d2")};
	{
		auto map = baseline;
		setLevelMapSelection(&map, {ref(brush("w1"))});
		const auto depth = map.undoStack.size();
		const auto revision = map.revision;
		LevelSceneLayerSelectionReport report;
		expect(expandLevelMapSelectionToLayers(&map, {}, &report, &error), "expand from a world brush", error);
		expect(keys(map.selection) == architectureUnits, "the whole layer, its group and its door's brushes", text(keys(map.selection)));
		expect(!keys(map.selection).contains(entity("func_door")), "a brush entity counts through its brushes");
		expect(report.layers == QStringList {architecture} && report.added == 5 && report.skippedGroups == 0, "report");
		expect(map.undoStack.size() == depth && map.revision == revision && bytesOf(map) == bytesOf(baseline), "no history and no map change by default");

		// One selection undo step when asked, as TrenchBroom's selection history.
		map = baseline;
		setLevelMapSelection(&map, {ref(brush("w1"))});
		expect(expandLevelMapSelectionToLayers(&map, {{}, true}, &report, &error) && map.undoStack.size() == depth + 1, "one selection step", error);
		expect(undoLevelMapEdit(&map, &error) && keys(map.selection) == QSet<QString> {brush("w1")}, "undo restores the selection", error);
		expect(redoLevelMapEdit(&map, &error) && keys(map.selection) == architectureUnits, "redo expands again", error);

		// Two layers at once, Default among them, with its root group whole.
		map = baseline;
		setLevelMapSelection(&map, refs({entity("light"), brush("floor")}));
		expect(expandLevelMapSelectionToLayers(&map, {}, &report, &error), "expand Default and Lights", error);
		expect(keys(map.selection) == QSet<QString> {entity("light"), brush("floor"), brush("h1"), entity("info_player_start")},
			"Default, its root group and Lights", text(keys(map.selection)));
		expect(report.layers == QStringList {QString(), lights}, "Default is listed first");

		// Hidden, locked and filtered objects stay out; a closed group goes whole or not at all.
		map = baseline;
		expect(setLevelSceneVisible(&map, pillars, false, &error), "hide Pillars", error);
		setLevelMapSelection(&map, {ref(brush("w1"))});
		expect(expandLevelMapSelectionToLayers(&map, {}, &report, &error)
				&& keys(map.selection) == QSet<QString> {brush("w1"), brush("w2"), brush("d1"), brush("d2")} && report.skippedGroups == 1,
			"a hidden group is left out", text(keys(map.selection)));
		map = baseline;
		expect(setLevelSceneLocked(&map, pillars, true, &error), "lock Pillars", error);
		setLevelMapSelection(&map, {ref(brush("w1"))});
		expect(expandLevelMapSelectionToLayers(&map, {}, &report, &error) && !keys(map.selection).contains(brush("g1")), "a locked group is left out");
		map = baseline;
		setLevelMapSelection(&map, {ref(brush("w1"))});
		expect(expandLevelMapSelectionToLayers(&map, {{brush("g2"), brush("d1")}, false}, &report, &error)
				&& keys(map.selection) == QSet<QString> {brush("w1"), brush("w2"), brush("d2")},
			"a filtered member keeps its closed group out", text(keys(map.selection)));

		// An open group limits the result to its contents.
		map = baseline;
		expect(openLevelSceneGroup(&map, pillars, &error), "open Pillars", error);
		setLevelMapSelection(&map, {ref(brush("g1"))});
		expect(expandLevelMapSelectionToLayers(&map, {}, &report, &error) && keys(map.selection) == QSet<QString> {brush("g1"), brush("g2")},
			"inside the open group only", text(keys(map.selection)));

		// Refusals change nothing.
		map = baseline;
		expect(!expandLevelMapSelectionToLayers(&map, {}, &report, &error) && !error.isEmpty(), "an empty selection is refused");
		setLevelMapSelection(&map, {ref(world)});
		expect(!expandLevelMapSelectionToLayers(&map, {}, &report, &error) && keys(map.selection) == QSet<QString> {world},
			"worldspawn alone has no layer", error);
		map.scene.problem = QStringLiteral("stale");
		setLevelMapSelection(&map, {ref(brush("w1"))});
		expect(!expandLevelMapSelectionToLayers(&map, {}, &report, &error), "stale scene metadata is refused");

		// The cancellable worker path the studio runs.
		map = baseline;
		setLevelMapSelection(&map, {ref(brush("w1"))});
		LevelPlacementRequest request;
		request.operation = LevelPlacementOperation::SelectObjects;
		request.objectSelection.mode = LevelObjectSelectionMode::Layers;
		const auto prepared = prepareLevelPlacement(map, request);
		expect(prepared.succeeded && keys(prepared.document.selection) == architectureUnits
				&& prepared.objectSelectionReport.layers == QStringList {architecture} && keys(map.selection) == QSet<QString> {brush("w1")},
			"the placement worker expands a private copy", prepared.error);
		LevelPlacementControl cancel;
		cancel.isCancelled = [] { return true; };
		const auto cancelled = prepareLevelPlacement(map, request, cancel);
		expect(!cancelled.succeeded && cancelled.cancelled, "cancellation publishes nothing");
	}

	// Move to Layer.
	{
		auto map = baseline;
		const QByteArray original = bytesOf(map);
		const auto depth = map.undoStack.size();
		LevelSceneLayerMoveReport report;
		setLevelMapSelection(&map, {ref(brush("floor"))});
		expect(levelMapSelectionLayerMoveBlockedReason(map, architecture).isEmpty(), "floor can move to Architecture");
		expect(!levelMapSelectionLayerMoveBlockedReason(map, QString()).isEmpty(), "floor is already in Default");
		expect(moveLevelMapSelectionToLayer(&map, architecture, &report, &error) && report.changed && report.objects == QStringList {brush("floor")},
			"move floor to Architecture", error);
		expect(levelSceneMembership(map.scene, brush("floor")) == architecture && map.undoStack.size() == depth + 1, "one scene undo step");
		const QByteArray moved = bytesOf(map);
		expect(undoLevelMapEdit(&map, &error) && bytesOf(map) == original, "undo restores the exact bytes", error);
		expect(redoLevelMapEdit(&map, &error) && bytesOf(map) == moved, "redo moves it again", error);

		// A closed group moves whole.
		map = baseline;
		setLevelMapSelection(&map, refs({brush("g1"), brush("g2")}));
		expect(moveLevelMapSelectionToLayer(&map, lights, &report, &error) && report.groups == QStringList {pillars} && report.objects.isEmpty(),
			"Pillars moves to Lights", error);
		expect(levelSceneNode(map.scene, pillars)->parentId == lights && levelSceneMembership(map.scene, brush("g1")) == pillars,
			"the group keeps its members");
		expect(undoLevelMapEdit(&map, &error) && bytesOf(map) == original, "undo the group move", error);
		map = baseline;
		setLevelMapSelection(&map, {ref(brush("h1"))});
		expect(moveLevelMapSelectionToLayer(&map, architecture, &report, &error) && levelSceneNode(map.scene, crates)->parentId == architecture,
			"a root group moves under a layer", error);
		setLevelMapSelection(&map, {ref(brush("h1"))});
		expect(moveLevelMapSelectionToLayer(&map, QString(), &report, &error) && levelSceneNode(map.scene, crates)->parentId.isEmpty(),
			"and back to Default", error);

		// A brush takes its brush entity along, and stops overriding it.
		map = baseline;
		expect(assignLevelSceneObjects(&map, lights, {brush("d1")}, &error), "pin d1 to Lights", error);
		const auto pinned = map.undoStack.size();
		setLevelMapSelection(&map, {ref(brush("d1"))});
		expect(moveLevelMapSelectionToLayer(&map, architecture, &report, &error) && report.changed && report.released == QStringList {brush("d1")}
				&& report.alreadyThere == 1,
			"d1 follows its door again", error);
		expect(levelSceneMembership(map.scene, brush("d1")).isEmpty() && levelSceneObjectLayers(map, map.scene).value(brush("d1")) == architecture
				&& map.undoStack.size() == pinned + 1,
			"one step, membership through the door");
		map = baseline;
		setLevelMapSelection(&map, {ref(brush("d2"))});
		expect(moveLevelMapSelectionToLayer(&map, lights, &report, &error) && report.objects == QStringList {entity("func_door")}
				&& levelSceneObjectLayers(map, map.scene).value(brush("d1")) == lights,
			"the whole door moves to Lights", error);

		// Already there: success, nothing recorded.
		map = baseline;
		setLevelMapSelection(&map, refs({brush("w1"), brush("w2")}));
		expect(moveLevelMapSelectionToLayer(&map, architecture, &report, &error) && !report.changed && report.alreadyThere == 2
				&& map.undoStack.size() == depth && bytesOf(map) == original,
			"a selection already in the layer changes nothing", error);

		// Worldspawn is skipped, not refused.
		setLevelMapSelection(&map, refs({world, brush("floor")}));
		expect(moveLevelMapSelectionToLayer(&map, lights, &report, &error) && report.worldspawn == 1 && report.objects == QStringList {brush("floor")},
			"worldspawn is skipped", error);

		// A hidden destination drops the moved objects from the selection.
		map = baseline;
		expect(setLevelSceneVisible(&map, lights, false, &error), "hide Lights", error);
		setLevelMapSelection(&map, refs({brush("floor"), brush("w1")}));
		expect(moveLevelMapSelectionToLayer(&map, lights, &report, &error) && report.deselected == 2 && map.selection.isEmpty(),
			"moved into a hidden layer and deselected", error);

		// Refusals leave map and history alone.
		map = baseline;
		expect(setLevelSceneLocked(&map, lights, true, &error), "lock Lights", error);
		const QByteArray locked = bytesOf(map);
		const auto lockedDepth = map.undoStack.size();
		setLevelMapSelection(&map, {ref(brush("floor"))});
		expect(!moveLevelMapSelectionToLayer(&map, lights, &report, &error) && error.contains(QStringLiteral("Lights")) && bytesOf(map) == locked
				&& map.undoStack.size() == lockedDepth,
			"a locked layer refuses new members", error);
		expect(!levelMapSelectionLayerMoveBlockedReason(map, lights).isEmpty(), "and its menu entry says why");
		setLevelMapSelection(&map, {ref(entity("light"))});
		expect(!moveLevelMapSelectionToLayer(&map, QString(), &report, &error) && bytesOf(map) == locked, "locked content cannot leave", error);
		map = baseline;
		setLevelMapSelection(&map, {ref(brush("floor"))});
		expect(!moveLevelMapSelectionToLayer(&map, pillars, &report, &error), "a group is not a layer");
		expect(!moveLevelMapSelectionToLayer(&map, QStringLiteral("00000000-0000-4000-8000-000000000000"), &report, &error), "an unknown layer");
		expect(openLevelSceneGroup(&map, pillars, &error), "open Pillars", error);
		setLevelMapSelection(&map, {ref(brush("g1"))});
		expect(!moveLevelMapSelectionToLayer(&map, lights, &report, &error) && error.contains(QStringLiteral("group")), "refused inside an open group", error);
		map = baseline;
		expect(!moveLevelMapSelectionToLayer(&map, lights, &report, &error), "nothing selected");
		map.scene.problem = QStringLiteral("stale");
		setLevelMapSelection(&map, {ref(brush("floor"))});
		expect(!moveLevelMapSelectionToLayer(&map, lights, &report, &error), "stale scene metadata");
		// Sibling names stay distinct: a second root Crates cannot arrive.
		map = baseline;
		QString otherCrates;
		expect(createLevelSceneNode(&map, LevelSceneNodeKind::Group, QStringLiteral("Crates"), architecture, &otherCrates, &error)
				&& assignLevelSceneObjects(&map, otherCrates, {brush("w2")}, &error),
			"a second Crates in Architecture", error);
		const QByteArray named = bytesOf(map);
		setLevelMapSelection(&map, {ref(brush("w2"))});
		expect(!moveLevelMapSelectionToLayer(&map, QString(), &report, &error) && bytesOf(map) == named, "a name clash is refused whole", error);
	}

	// Doom maps have layers too.
	{
		LevelMapDocument doom;
		LevelMapCreateRequest create;
		create.game = QStringLiteral("doom");
		expect(createLevelMap(create, &doom, &error), "Doom map", error);
		QString layer;
		expect(createLevelSceneNode(&doom, LevelSceneNodeKind::Layer, QStringLiteral("Room"), {}, &layer, &error)
				&& assignLevelSceneObjects(&doom, layer, {QStringLiteral("sector:0"), QStringLiteral("thing:0")}, &error),
			"Doom layer", error);
		setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomSector, 0}});
		LevelSceneLayerSelectionReport expanded;
		expect(expandLevelMapSelectionToLayers(&doom, {}, &expanded, &error) && keys(doom.selection).contains(QStringLiteral("thing:0")),
			"a Doom sector expands to its layer's thing", error);
		setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomThing, 0}});
		LevelSceneLayerMoveReport moved;
		expect(moveLevelMapSelectionToLayer(&doom, QString(), &moved, &error) && levelSceneMembership(doom.scene, QStringLiteral("thing:0")).isEmpty(),
			"a Doom thing moves to Default", error);
	}

	// The command line.
	if (argc > 1) {
		const QString cli = QString::fromLocal8Bit(argv[1]);
		const auto run = [&](const QStringList& arguments, int expected = 0) {
			QProcess process;
			process.setWorkingDirectory(temp.path());
			process.start(cli, QStringList {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--settings-file"),
								   temp.filePath(QStringLiteral("cli.ini")), QStringLiteral("editor"), QStringLiteral("scene")}
					+ arguments);
			const bool ended = process.waitForStarted() && process.waitForFinished(60000);
			const QByteArray out = process.readAllStandardOutput(), err = process.readAllStandardError();
			expect(ended && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected, "CLI exit code",
				arguments.join(QLatin1Char(' ')) + QStringLiteral(" -> ") + QString::number(process.exitCode()) + QLatin1Char(' ') + QString::fromUtf8(out + err));
			if (!ended) {
				process.kill();
				process.waitForFinished();
			}
			return QJsonDocument::fromJson(out).object();
		};
		const auto selectionOf = [](const QJsonObject& result) {
			QSet<QString> values;
			for (const auto& value : result.value(QStringLiteral("selection")).toArray()) {
				values.insert(value.toString());
			}
			return values;
		};
		auto result = run({QStringLiteral("select"), path, QStringLiteral("--objects"), brush("w1"), QStringLiteral("--expand-layers")});
		expect(selectionOf(result) == architectureUnits && result.value(QStringLiteral("layers")).toArray() == QJsonArray {architecture}
				&& readFile(path) == source,
			"select --expand-layers matches the studio and writes nothing", text(selectionOf(result)));
		result = run({QStringLiteral("select"), path, QStringLiteral("--objects"), brush("floor"), QStringLiteral("--expand-layers")});
		expect(result.value(QStringLiteral("layers")).toArray() == QJsonArray {QStringLiteral("default")}
				&& selectionOf(result).contains(brush("h1")),
			"Default is reported as default");
		result = run({QStringLiteral("select"), path, QStringLiteral("--objects"), brush("g1"), QStringLiteral("--context"), pillars, QStringLiteral("--expand-layers")});
		expect(selectionOf(result) == QSet<QString> {brush("g1"), brush("g2")}, "the context limits the expansion");
		// Worldspawn is no scene object; a pick outside the context leaves nothing to expand.
		run({QStringLiteral("select"), path, QStringLiteral("--objects"), world, QStringLiteral("--expand-layers")}, 2);
		run({QStringLiteral("select"), path, QStringLiteral("--objects"), brush("w1"), QStringLiteral("--context"), pillars, QStringLiteral("--expand-layers")}, 4);
		run({QStringLiteral("list"), path, QStringLiteral("--expand-layers")}, 2);

		const QString output = temp.filePath(QStringLiteral("moved.map"));
		const QStringList move {QStringLiteral("move-to-layer"), path, QStringLiteral("--id"), lights, QStringLiteral("--objects"),
			brush("floor") + QLatin1Char(',') + brush("g1"), QStringLiteral("--output"), output};
		result = run(move + QStringList {QStringLiteral("--dry-run")});
		expect(!QFile::exists(output) && result.value(QStringLiteral("dryRun")).toBool(), "a dry run writes nothing");
		result = run(move);
		const QJsonObject movedJson = result.value(QStringLiteral("moved")).toObject();
		expect(movedJson.value(QStringLiteral("changed")).toBool() && movedJson.value(QStringLiteral("groups")).toArray() == QJsonArray {pillars}
				&& movedJson.value(QStringLiteral("objects")).toArray() == QJsonArray {brush("floor")},
			"move-to-layer reports what moved");
		LevelMapDocument written;
		expect(loadLevelMap({output, {}, {}}, &written, &error) && levelSceneMembership(written.scene, brush("floor")) == lights
				&& levelSceneNode(written.scene, pillars)->parentId == lights && readFile(path) == source,
			"the written map has the same scene the studio makes", error);
		LevelMapDocument expectedMove = baseline;
		setLevelMapSelection(&expectedMove, refs({brush("floor"), brush("g1")}));
		moveLevelMapSelectionToLayer(&expectedMove, lights, nullptr, &error);
		expect(readFile(output) == bytesOf(expectedMove), "byte for byte");
		run(move, 4);
		run({QStringLiteral("move-to-layer"), path, QStringLiteral("--id"), QStringLiteral("default"), QStringLiteral("--objects"), brush("w1"),
				QStringLiteral("--output"), temp.filePath(QStringLiteral("default.map"))});
		LevelMapDocument toDefault;
		expect(loadLevelMap({temp.filePath(QStringLiteral("default.map")), {}, {}}, &toDefault, &error)
				&& levelSceneMembership(toDefault.scene, brush("w1")).isEmpty(),
			"--id default moves to Default", error);
		run({QStringLiteral("move-to-layer"), path, QStringLiteral("--id"), lights, QStringLiteral("--objects"), world, QStringLiteral("--output"),
				temp.filePath(QStringLiteral("world.map"))}, 2);
		run({QStringLiteral("move-to-layer"), path, QStringLiteral("--id"), lights, QStringLiteral("--objects"), brush("floor")}, 2);
		run({QStringLiteral("move-to-layer"), path, QStringLiteral("--id"), lights, QStringLiteral("--objects"), brush("floor"), QStringLiteral("--expand-layers"),
				QStringLiteral("--output"), temp.filePath(QStringLiteral("bad.map"))}, 2);
		run({QStringLiteral("move-to-layer"), path, QStringLiteral("--id"), pillars, QStringLiteral("--objects"), brush("floor"), QStringLiteral("--output"),
				temp.filePath(QStringLiteral("group.map"))}, 4);
		expect(!QFile::exists(temp.filePath(QStringLiteral("group.map"))) && !QFile::exists(temp.filePath(QStringLiteral("bad.map"))),
			"refused moves write nothing");
		// A locked layer refuses with the lock's reason and writes nothing.
		LevelMapDocument lockedMap = baseline;
		expect(setLevelSceneLocked(&lockedMap, lights, true, &error), "lock Lights for the CLI", error);
		const QString lockedPath = temp.filePath(QStringLiteral("locked.map"));
		writeFile(lockedPath, bytesOf(lockedMap));
		run({QStringLiteral("move-to-layer"), lockedPath, QStringLiteral("--id"), lights, QStringLiteral("--objects"), brush("floor"), QStringLiteral("--output"),
				temp.filePath(QStringLiteral("locked-out.map"))}, 4);
		expect(!QFile::exists(temp.filePath(QStringLiteral("locked-out.map"))), "a locked layer writes nothing");
	}

	std::cout << (ok ? "Level scene layer checks passed" : "Level scene layer checks failed") << std::endl;
	return ok ? 0 : 1;
}
