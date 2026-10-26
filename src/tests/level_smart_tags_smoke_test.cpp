// Original fixtures exercise smart-tag authoring, matching, undo, refusal and
// cancellation through the same service the GUI worker and CLI use.
#include "core/level_document.h"
#include "core/level_linked_groups.h"
#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_smart_tags.h"
#include "core/studio_semantics.h"
#include "tests/level_view_filter_test_helpers.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
void check(bool passed, const char* name, const QString& reason = {})
{
	if (!passed) { ok = false; std::cerr << name << ": " << reason.toUtf8().constData() << '\n'; }
}
LevelMapDocument fresh()
{
	LevelMapDocument doc;
	QString error;
	check(loadLevelMapBytes({QStringLiteral("tags.map"), {}, QStringLiteral("idTech3")}, tests::viewFilterTagsFixture(), &doc, &error), "fixture", error);
	check(setLevelMapSelection(&doc, {{LevelMapSelectionKind::QuakeBrush, 5}}, &error), "selection", error);
	return doc;
}
QByteArray bytes(const LevelMapDocument& doc) { return serializeLevelMap(doc).bytes; }
bool write(const QString& path, const QByteArray& data) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size(); }
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	LevelFilterTagSet tags;
	check(parseLevelFilterTagText(tests::viewFilterTagLayout(), QStringLiteral("test"), &tags, &error), "tags", error);
	if (!tags.find(QStringLiteral("tag:water"))) { return 1; }
	LevelSmartTagRequest request;
	request.tag = *tags.find(QStringLiteral("tag:water"));
	request.choice = QStringLiteral("liquids/water_new");
	auto doc = fresh();
	const auto original = bytes(doc);
	check(applyLevelSmartTag(&doc, request, &error), "texture tag", error);
	check(doc.undoStack.size() == 1 && doc.brushes[5].faces[0].textureName == request.choice, "texture applied once");
	const auto changed = bytes(doc);
	check(undoLevelMapEdit(&doc, &error) && bytes(doc) == original, "texture undo", error);
	check(redoLevelMapEdit(&doc, &error) && bytes(doc) == changed, "texture redo", error);
	request.enabled = false;
	check(!applyLevelSmartTag(&doc, request, &error) && bytes(doc) == changed, "texture disable refused");
	request.enabled = true;
	request.choice = QStringLiteral("base_wall/wall");
	check(!applyLevelSmartTag(&doc, request, &error) && bytes(doc) == changed, "nonmatching material refused");

	// Independent faces leave the other faces and patch untouched.
	doc = fresh();
	request.choice = QStringLiteral("liquids/water_new");
	request.surfaceTargets = true;
	request.faces = {{5, 0}};
	check(applyLevelSmartTag(&doc, request, &error) && doc.brushes[5].faces[0].textureName == request.choice
		&& doc.brushes[5].faces[2].textureName == QStringLiteral("base_wall/wall"), "one face", error);
	request.faces.clear(); request.patches = {0};
	check(applyLevelSmartTag(&doc, request, &error) && doc.patches[0].textureName == request.choice, "patch material", error);

	// Flag edits preserve unrelated bits; detail + another bit still one step.
	doc = fresh(); request = {};
	request.tag = *tags.find(QStringLiteral("tag:detail"));
	request.tag.flagBits = 0x40;
	check(applyLevelSmartTag(&doc, request, &error), "detail plus fog", error);
	check(doc.undoStack.size() == 1 && doc.brushes[5].faces[0].contentFlags == (0x8000000 | 0x40), "flag batch");
	check(undoLevelMapEdit(&doc, &error) && bytes(doc) == original, "batch undo", error);
	check(redoLevelMapEdit(&doc, &error), "batch redo", error);
	request.tag = *tags.find(QStringLiteral("tag:mist")); request.enabled = false;
	check(applyLevelSmartTag(&doc, request, &error) && doc.brushes[5].faces[0].contentFlags == 0x8000000, "clear preserves detail", error);
	request.tag.match = LevelFilterTagMatch::SurfaceFlag; request.tag.flagBits = 0x80000000; request.enabled = true;
	check(applyLevelSmartTag(&doc, request, &error) && doc.brushes[5].faces[0].surfaceFlags == -2147483648LL, "signed high bit", error);
	// Inferred Quake II maps may omit every optional face flag number. They
	// still use detail bits, while Quake must refuse a face-flag tag.
	const auto q1 = tests::viewFilterQuakeLiquidFixture();
	check(loadLevelMapBytes({QStringLiteral("q1.map"), {}, QStringLiteral("idTech2")}, q1, &doc, &error), "Quake fixture", error);
	check(setLevelMapSelection(&doc, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error), "Quake selection", error);
	request = {}; request.tag = *tags.find(QStringLiteral("tag:detail"));
	check(!applyLevelSmartTag(&doc, request, &error) && doc.undoStack.isEmpty(), "Quake flag tag refused");
	QByteArray modernQuake = q1;
	modernQuake.replace("\"wad\" \"gfx/base.wad\"\n", "");
	modernQuake.replace("*water1", "studio/water");
	modernQuake.replace("*lava1", "studio/lava");
	modernQuake.replace("ground1_6", "studio/ground");
	check(loadLevelMapBytes({QStringLiteral("modern-quake.map"), {}, QStringLiteral("idTech2")}, modernQuake, &doc, &error)
		&& setLevelMapSelection(&doc, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error), "modern Quake fixture", error);
	check(!applyLevelSmartTag(&doc, request, &error) && doc.undoStack.isEmpty(), "texture folders alone do not enable flag edits");
	QByteArray q2 = q1;
	q2.replace("\"wad\" \"gfx/base.wad\"\n", "");
	q2.replace("\"classname\" \"worldspawn\"", "\"classname\" \"worldspawn\"\n\"_q2_marker\" \"1\"");
	q2 += "{\n\"classname\" \"misc_deadsoldier\"\n\"origin\" \"0 0 128\"\n}\n";
	check(loadLevelMapBytes({QStringLiteral("q2.map"), {}, QStringLiteral("idTech2")}, q2, &doc, &error)
		&& setLevelMapSelection(&doc, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error), "Quake II fixture", error);
	check(applyLevelSmartTag(&doc, request, &error) && doc.brushes[0].entityId == 0 && doc.brushes[0].faces[0].contentFlags == 0x8000000,
		"Quake II inferred detail flags", error);
	q2 = QByteArray(kQuake2MapTargetHeader) + modernQuake;
	check(loadLevelMapBytes({QStringLiteral("q2-header.map"), {}, QStringLiteral("idTech2")}, q2, &doc, &error)
		&& setLevelMapSelection(&doc, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error), "Quake II header fixture", error);
	check(applyLevelSmartTag(&doc, request, &error) && doc.brushes[0].entityId == 0 && doc.brushes[0].faces[0].contentFlags == 0x8000000,
		"Quake II header detail flags", error);
	request.enabled = false;
	check(applyLevelSmartTag(&doc, request, &error) && doc.brushes[0].faces[0].contentFlags == 0, "Quake II make structural after detail", error);

	// Class tags use their configured texture and one transaction; disabling
	// moves matching brush entities back to world, retaining the material.
	doc = fresh(); request = {};
	request.tag = *tags.find(QStringLiteral("tag:triggers")); request.choice = QStringLiteral("trigger_multiple");
	check(applyLevelSmartTag(&doc, request, &error), "tie classname", error);
	const auto tiedFaces = levelMapSelectedSurfaces(doc);
	const auto tiedBrush = std::find_if(doc.brushes.cbegin(), doc.brushes.cend(), [&](const auto& brush) { return !tiedFaces.isEmpty() && brush.id == tiedFaces.first().brushId; });
	check(doc.undoStack.size() == 1 && tiedBrush != doc.brushes.cend() && tiedBrush->entityId != 0
		&& tiedBrush->faces[0].textureName == QStringLiteral("common/trigger"), "tie and material atomic");
	check(undoLevelMapEdit(&doc, &error) && bytes(doc) == original, "tie undo", error);
	check(redoLevelMapEdit(&doc, &error), "tie redo", error);
	request.enabled = false; request.choice.clear();
	check(applyLevelSmartTag(&doc, request, &error) && doc.brushes[5].entityId == 0, "untie", error);
	const auto untied = bytes(doc);
	check(!applyLevelSmartTag(&doc, request, &error) && bytes(doc) == untied, "nonmatching owner refused");

	// Shader choices come from declared surfaceparms, never the filename.
	request = {}; request.tag = *tags.find(QStringLiteral("tag:fog-volume"));
	LevelViewFilterShader shader; shader.surfaceParms = {QStringLiteral("fog")};
	request.shaders.shaders.insert(QStringLiteral("textures/sfx/mist"), shader);
	request.shaders.shaders.insert(QStringLiteral("textures/sfx/fog_fake"), {});
	check(levelSmartTagChoices(request.tag, {}, {}, request.shaders) == QStringList{QStringLiteral("textures/sfx/mist")}, "surfaceparm choices");
	request.choice = QStringLiteral("textures/sfx/mist"); doc = fresh();
	check(applyLevelSmartTag(&doc, request, &error), "shader tag", error);
	request.choice = QStringLiteral("textures/sfx/fog_fake");
	check(!applyLevelSmartTag(&doc, request, &error), "unverified shader refused");

	// All targets must pass guards before any write or undo change.
	request = {}; request.tag = *tags.find(QStringLiteral("tag:water")); request.choice = QStringLiteral("liquids/water_new");
	doc = fresh(); doc.hiddenObjects.insert(QStringLiteral("brush:5"));
	check(!applyLevelSmartTag(&doc, request, &error) && doc.undoStack.isEmpty() && bytes(doc) == original, "hidden guard");
	doc = fresh(); request.excludedObjects.insert(QStringLiteral("brush:5"));
	check(!applyLevelSmartTag(&doc, request, &error) && doc.undoStack.isEmpty(), "view filter guard"); request.excludedObjects.clear();
	QString group;
	check(createLevelSceneNode(&doc, LevelSceneNodeKind::Group, QStringLiteral("Guard"), {}, &group, &error)
		&& assignLevelSceneObjects(&doc, group, {QStringLiteral("brush:5")}, &error) && setLevelSceneLocked(&doc, group, true, &error), "locked setup", error);
	const auto locked = bytes(doc); const auto history = doc.undoStack.size();
	check(!applyLevelSmartTag(&doc, request, &error) && bytes(doc) == locked && doc.undoStack.size() == history, "locked guard");
	check(setLevelSceneLocked(&doc, group, false, &error) && openLevelSceneGroup(&doc, group, &error), "context setup", error);
	request.surfaceTargets = true; request.faces = {{0, 0}};
	check(!applyLevelSmartTag(&doc, request, &error), "open group guard");
	// Class removal affects every sibling owned by the selected entity. A
	// hidden sibling must veto the transaction even when it was not selected.
	doc = fresh(); request = {}; request.tag = *tags.find(QStringLiteral("tag:triggers")); request.enabled = false;
	check(setLevelMapSelection(&doc, {{LevelMapSelectionKind::QuakeBrush, 7}, {LevelMapSelectionKind::QuakeBrush, 8}}, &error)
		&& tieLevelMapSelectionToEntity(&doc, QStringLiteral("trigger_multiple"), {}, nullptr, &error), "class sibling setup", error);
	const auto siblingFaces = levelMapSelectedSurfaces(doc);
	QList<int> siblingIds;
	for (const auto& face : siblingFaces) { if (!siblingIds.contains(face.brushId)) { siblingIds << face.brushId; } }
	check(siblingIds.size() == 2, "class sibling ids");
	if (siblingIds.size() != 2) { return 1; }
	check(setLevelMapSelection(&doc, {{LevelMapSelectionKind::QuakeBrush, siblingIds[0]}}, &error), "class guard selection", error);
	const QString siblingId = QStringLiteral("brush:%1").arg(siblingIds[1]);
	doc.hiddenObjects.insert(siblingId);
	const auto classBefore = bytes(doc);
	const auto classInitialDepth = doc.undoStack.size();
	check(!applyLevelSmartTag(&doc, request, &error) && bytes(doc) == classBefore && doc.undoStack.size() == classInitialDepth, "hidden class sibling guard");
	doc.hiddenObjects.clear(); group.clear();
	check(createLevelSceneNode(&doc, LevelSceneNodeKind::Group, QStringLiteral("Class guard"), {}, &group, &error)
		&& assignLevelSceneObjects(&doc, group, {siblingId}, &error) && setLevelSceneLocked(&doc, group, true, &error), "class lock setup", error);
	const auto classLocked = bytes(doc); const auto classDepth = doc.undoStack.size();
	check(!applyLevelSmartTag(&doc, request, &error) && bytes(doc) == classLocked && doc.undoStack.size() == classDepth, "locked class sibling guard");
	// Flag tags use the same exclusions and reject partial detail changes.
	doc = fresh(); request = {}; request.tag = *tags.find(QStringLiteral("tag:mist")); request.excludedObjects.insert(QStringLiteral("brush:5"));
	check(!applyLevelSmartTag(&doc, request, &error) && bytes(doc) == original && doc.undoStack.isEmpty(), "flag filter guard");
	request = {}; request.tag = *tags.find(QStringLiteral("tag:detail")); request.surfaceTargets = true; request.faces = {{5, 0}};
	check(!applyLevelSmartTag(&doc, request, &error) && bytes(doc) == original && doc.undoStack.isEmpty(), "partial detail refused");

	// A face edit inside an open linked group propagates through the normal
	// surface service, with its copies and original restored by one Undo.
	doc = fresh(); group.clear(); QString linked;
	check(createLevelSceneNode(&doc, LevelSceneNodeKind::Group, QStringLiteral("Linked"), {}, &group, &error)
		&& assignLevelSceneObjects(&doc, group, {QStringLiteral("brush:5")}, &error)
		&& createLinkedLevelGroup(&doc, group, {0, 256, 0, true}, &linked, &error)
		&& openLevelSceneGroup(&doc, group, &error), "linked setup", error);
	const auto linkedBefore = bytes(doc); const auto linkedDepth = doc.undoStack.size();
	request = {}; request.tag = *tags.find(QStringLiteral("tag:water")); request.choice = QStringLiteral("liquids/water_new");
	request.surfaceTargets = true; request.faces = {{5, 0}};
	check(applyLevelSmartTag(&doc, request, &error) && doc.undoStack.size() == linkedDepth + 1, "linked tag one step", error);
	int appliedCopies = 0;
	for (const auto& brush : doc.brushes) { if (brush.faces[0].textureName == request.choice) { ++appliedCopies; } }
	check(appliedCopies == 2, "linked material propagated");
	check(undoLevelMapEdit(&doc, &error) && bytes(doc) == linkedBefore, "linked undo", error);

	LevelPlacementRequest job; job.operation = LevelPlacementOperation::SmartTag;
	job.smartTag.tag = *tags.find(QStringLiteral("tag:water")); job.smartTag.choice = QStringLiteral("liquids/water_new");
	doc = fresh();
	LevelPlacementControl control; control.isCancelled = [] { return true; };
	const auto cancelled = prepareLevelPlacement(doc, job, control);
	check(cancelled.cancelled && !cancelled.succeeded && bytes(doc) == original && doc.undoStack.isEmpty(), "cancellation");
	check(shellCommandIdExists(QStringLiteral("map.makeTag.water")) && shellCommandIdExists(QStringLiteral("map.make-non-tag.detail")), "dynamic command ids");
	// A composed tag transaction obeys the normal history cap and save marker.
	doc = fresh(); doc.undoLimit = 1; doc.savedUndoDepth = 0;
	request = {}; request.tag = *tags.find(QStringLiteral("tag:mist"));
	check(applyLevelSmartTag(&doc, request, &error), "history first tag", error);
	request.enabled = false;
	check(applyLevelSmartTag(&doc, request, &error) && doc.undoStack.size() == 1 && doc.undoLimit == 1 && doc.savedUndoDepth == -1, "history eviction", error);

	if (argc > 1) {
		QTemporaryDir temp;
		const QString map = temp.filePath(QStringLiteral("input.map")), tagFile = temp.filePath(QStringLiteral("tags.json")), output = temp.filePath(QStringLiteral("output.map"));
		check(write(map, tests::viewFilterTagsFixture()) && write(tagFile, tests::viewFilterTagLayout()), "CLI fixtures");
		const auto cli = [&](const QStringList& extra) {
			QProcess process;
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--settings-file", temp.filePath("settings.ini"), "map", "smart-tag", map,
				"--tags", tagFile, "--tag", "water", "--json"} + extra);
			if (!process.waitForFinished(30000)) { process.kill(); process.waitForFinished(); return false; }
			const auto out = process.readAllStandardOutput();
			if (process.exitCode() != 0) { std::cerr << out.constData() << process.readAllStandardError().constData(); }
			return process.exitCode() == 0 && !QJsonDocument::fromJson(out).isNull();
		};
		check(cli({"--choices"}), "CLI choices");
		const QString package = temp.filePath(QStringLiteral("package"));
		check(QDir().mkpath(package + QStringLiteral("/textures/liquids"))
			&& write(package + QStringLiteral("/textures/liquids/water_from_package.wal"), QByteArray(100, '\0')), "raw package texture fixture");
		QProcess query;
		query.start(QString::fromLocal8Bit(argv[1]), {"--cli", "--settings-file", temp.filePath("settings.ini"), "map", "smart-tag", map, "--tags", tagFile, "--tag", "water", "--package", package, "--choices", "--json"});
		check(query.waitForFinished(30000) && query.exitCode() == 0, "package choices query");
		const auto choices = QJsonDocument::fromJson(query.readAllStandardOutput()).object().value(QStringLiteral("choices")).toArray();
		check(choices.contains(QStringLiteral("liquids/water_from_package")), "package-only raw texture choice");
		check(cli({"--face", "5:1", "--choice", "liquids/water_new", "--output", output, "--dry-run"}) && !QFileInfo::exists(output), "CLI dry run");
		check(cli({"--face", "5:1", "--choice", "liquids/water_new", "--output", output}) && QFileInfo::exists(output), "CLI write");
	}
	return ok ? 0 : 1;
}
