// Reveal Material and Find and Replace Textures: the shared core service
// (core/level_material_find.h), Find within the selection and the CLI routes.
#include "core/level_material_find.h"
#include "core/level_document.h"
#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_textured_selection.h"
#include "tests/level_type_selection_test_helpers.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) { ok = false; std::cerr << "FAIL: " << label << ": " << detail.toStdString() << std::endl; }
	return value;
}
LevelMapSelectionRef entity(int id) { return {LevelMapSelectionKind::Entity, id}; }
LevelMapSelectionRef brush(int id) { return {LevelMapSelectionKind::QuakeBrush, id}; }
LevelMapSelectionRef patch(int id) { return {LevelMapSelectionKind::QuakePatch, id}; }
// Brush 0: face 1 metal, faces 2-6 studio/checker; brush 1 and patches 0-2:
// studio/checker. Entity 1 owns both brushes and patch 1.
LevelMapDocument fixture()
{
	LevelMapDocument map; QString error;
	expect(loadLevelMapBytes({"find.map", {}, {}}, tests::typeSelectionFixture(), &map, &error), "parse fixture", error);
	return map;
}
LevelMapDocument doomFixture()
{
	LevelMapDocument map; QString error; LevelMapCreateRequest create; create.game = QStringLiteral("doom");
	expect(createLevelMap(create, &map, &error), "Doom fixture", error);
	return map;
}
bool lockEntityOne(LevelMapDocument* map)
{
	QString group, error;
	return expect(createLevelSceneNode(map, LevelSceneNodeKind::Layer, "Locked", {}, &group, &error)
		&& assignLevelSceneObjects(map, group, {"entity:1"}, &error) && setLevelSceneLocked(map, group, true, &error), "lock fixture", error);
}

void reveal()
{
	auto map = fixture();
	auto result = levelSelectionMaterial(map);
	expect(result.state == LevelSelectionMaterial::State::None && result.surfaces == 0 && !result.fromPicks, "nothing selected reveals nothing");
	setLevelMapSelection(&map, {brush(1)});
	result = levelSelectionMaterial(map);
	expect(result.state == LevelSelectionMaterial::State::Single && result.material == "studio/checker" && result.surfaces == 6 && !result.fromPicks,
		"one brush's shared material", result.material);
	setLevelMapSelection(&map, {brush(0)});
	result = levelSelectionMaterial(map);
	expect(result.state == LevelSelectionMaterial::State::Mixed && result.material == "metal" && result.other == "studio/checker" && result.surfaces == 2,
		"a mixed brush stops at its second material");
	result = levelSelectionMaterial(map, {{0, 0}});
	expect(result.state == LevelSelectionMaterial::State::Single && result.material == "metal" && result.fromPicks, "picked faces win over the selected brush");
	result = levelSelectionMaterial(map, {{0, 1}, {1, 3}}, {2});
	expect(result.state == LevelSelectionMaterial::State::Single && result.material == "studio/checker" && result.surfaces == 3, "picked faces and whole patches");
	result = levelSelectionMaterial(map, {{0, 0}}, {0});
	expect(result.state == LevelSelectionMaterial::State::Mixed, "a picked patch with another material is mixed");
	result = levelSelectionMaterial(map, {{999, 0}, {0, 99}});
	expect(result.state == LevelSelectionMaterial::State::None && result.fromPicks, "stale picks reveal nothing and never widen to the selection");
	map.brushes[1].faces[2].textureName = QStringLiteral("STUDIO/CHECKER");
	setLevelMapSelection(&map, {brush(1), patch(0)});
	result = levelSelectionMaterial(map);
	expect(result.state == LevelSelectionMaterial::State::Single && result.material == "studio/checker" && result.surfaces == 7,
		"names compare without case and keep the first spelling");
	setLevelMapSelection(&map, {entity(1)});
	expect(levelSelectionMaterial(map).state == LevelSelectionMaterial::State::Mixed, "a selected entity reads its brushes and patches");

	// Doom: a sector's flats, a line's sides without "-" placeholders.
	auto doom = doomFixture();
	if (!expect(!doom.doomSectors.isEmpty() && !doom.doomLinedefs.isEmpty(), "Doom fixture geometry")) { return; }
	const auto& sector = doom.doomSectors.first();
	setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomSector, sector.id}});
	result = levelSelectionMaterial(doom);
	const bool sameFlat = sector.floorTexture.compare(sector.ceilingTexture, Qt::CaseInsensitive) == 0;
	expect(result.state == (sameFlat ? LevelSelectionMaterial::State::Single : LevelSelectionMaterial::State::Mixed) && result.flat && !result.wall,
		"sector flats", sector.floorTexture + QLatin1Char('/') + sector.ceilingTexture);
	const auto& line = doom.doomLinedefs.first();
	QSet<QString> names;
	for (const auto& side : doom.doomSidedefs) {
		if (side.id != line.frontSidedef && side.id != line.backSidedef) { continue; }
		for (const auto& name : {side.upperTexture, side.lowerTexture, side.middleTexture}) {
			if (!name.trimmed().isEmpty() && name.trimmed() != QStringLiteral("-")) { names.insert(name.trimmed().toCaseFolded()); }
		}
	}
	setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomLinedef, line.id}});
	result = levelSelectionMaterial(doom);
	expect(names.size() == 1 ? result.state == LevelSelectionMaterial::State::Single && result.wall && !result.flat
							 : result.state != LevelSelectionMaterial::State::Single, "line sides ignore the - placeholder");
}

void preview()
{
	auto map = fixture();
	LevelMaterialReplaceRequest request; request.find = QStringLiteral(" STUDIO/CHECKER ");
	LevelMaterialReplaceReport report; QString error;
	expect(previewLevelMaterialReplace(map, request, &report, &error) && report.targets.size() == 14 && report.changes == 0 && report.skipped == 0,
		"Find preview counts every face and patch", error);
	expect(levelMaterialTargetId(report.targets.first()) == "face:0:2" && levelMaterialTargetId(report.targets.last()) == "patch:2", "targets in map order");
	request.replace = QStringLiteral("base/metal");
	expect(previewLevelMaterialReplace(map, request, &report, &error) && report.changes == 14, "Replace preview counts changes", error);
	request.replace = QStringLiteral("studio/checker");
	expect(previewLevelMaterialReplace(map, request, &report, &error) && report.targets.size() == 14 && report.changes == 0, "the same name changes nothing");

	auto hidden = fixture(); hidden.hiddenObjects = {"brush:0"}; request.replace = QStringLiteral("base/metal");
	expect(previewLevelMaterialReplace(hidden, request, &report, &error) && report.targets.size() == 9 && report.skipped == 5, "hidden objects are skipped and counted");
	request.excludedObjects = {"patch:1"};
	expect(previewLevelMaterialReplace(map, request, &report, &error) && report.targets.size() == 13 && report.skipped == 1, "view-hidden objects are skipped");
	request.excludedObjects = {"entity:1"};
	expect(previewLevelMaterialReplace(map, request, &report, &error) && report.targets.size() == 2 && report.skipped == 12, "a hidden owner hides its primitives");
	request.excludedObjects.clear();

	auto grouped = fixture(); QString group;
	expect(createLevelSceneNode(&grouped, LevelSceneNodeKind::Group, "Closed", {}, &group, &error) && assignLevelSceneObjects(&grouped, group, {"entity:1"}, &error), "group fixture", error);
	expect(previewLevelMaterialReplace(grouped, request, &report, &error) && report.targets.size() == 14 && report.inClosedGroups == 12 && report.skipped == 0,
		"closed group members stay editable as wholes", error);
	expect(openLevelSceneGroup(&grouped, group, &error) && previewLevelMaterialReplace(grouped, request, &report, &error)
		&& report.targets.size() == 12 && report.inClosedGroups == 0 && report.skipped == 2, "an open group leaves outside objects alone", error);
}

void replace()
{
	auto map = fixture(); const auto original = serializeLevelMap(map).bytes; QString error;
	LevelMaterialReplaceRequest request; request.find = QStringLiteral("studio/checker"); request.replace = QStringLiteral("base/metal");
	LevelMaterialReplaceReport report;
	const auto undo = map.undoStack.size();
	expect(replaceLevelMapMaterial(&map, request, &report, &error) && report.replaced == 14 && map.undoStack.size() == undo + 1, "whole-map replace is one undo step", error);
	expect(map.brushes[0].faces[0].textureName == "metal" && map.brushes[1].faces[5].textureName == "base/metal" && map.patches[2].textureName == "base/metal",
		"only matching surfaces change");
	const auto replaced = serializeLevelMap(map).bytes;
	expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == original, "exact undo", error);
	expect(redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == replaced && undoLevelMapEdit(&map, &error), "exact redo", error);

	setLevelMapSelection(&map, {brush(1), patch(0)}); request.scope = LevelMaterialFindScope::Selection;
	expect(replaceLevelMapMaterial(&map, request, &report, &error) && report.replaced == 7 && map.brushes[0].faces[1].textureName == "studio/checker"
		&& map.patches[0].textureName == "base/metal" && map.patches[1].textureName == "studio/checker", "selection scope", error);
	expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == original, "selection undo", error);

	request.scope = LevelMaterialFindScope::Picks; request.faces = {{0, 0}, {0, 1}, {1, 2}, {0, 1}}; request.patches = {2, 2};
	expect(previewLevelMaterialReplace(map, request, &report, &error) && report.targets.size() == 3 && report.changes == 3, "picks deduplicate and skip other materials", error);
	expect(replaceLevelMapMaterial(&map, request, &report, &error) && report.replaced == 3 && map.brushes[0].faces[0].textureName == "metal"
		&& map.brushes[0].faces[1].textureName == "base/metal" && map.brushes[0].faces[2].textureName == "studio/checker"
		&& map.brushes[1].faces[2].textureName == "base/metal" && map.patches[2].textureName == "base/metal" && map.patches[0].textureName == "studio/checker",
		"picked faces scope changes exactly the picks", error);
	expect(map.selection == QVector<LevelMapSelectionRef>{brush(1), patch(0)}, "replace leaves the selection alone");
	expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == original, "picks undo", error);

	// Locked uses are skipped rather than refusing the whole edit.
	auto locked = fixture(); lockEntityOne(&locked); request = {}; request.find = QStringLiteral("studio/checker"); request.replace = QStringLiteral("base/metal");
	expect(replaceLevelMapMaterial(&locked, request, &report, &error) && report.replaced == 2 && report.skipped == 12
		&& locked.patches[0].textureName == "base/metal" && locked.patches[1].textureName == "studio/checker" && locked.brushes[1].faces[0].textureName == "studio/checker",
		"locked uses are skipped and counted", error);

	// Doom walls and flats share the service.
	auto doom = doomFixture();
	if (expect(!doom.doomSectors.isEmpty(), "Doom sectors")) {
		const auto flat = doom.doomSectors.first().floorTexture; int uses = 0;
		for (const auto& sector : doom.doomSectors) { uses += (sector.floorTexture.compare(flat, Qt::CaseInsensitive) == 0) + (sector.ceilingTexture.compare(flat, Qt::CaseInsensitive) == 0); }
		request = {}; request.find = flat; request.replace = QStringLiteral("FLAT5");
		expect(replaceLevelMapMaterial(&doom, request, &report, &error) && report.replaced == uses && doom.doomSectors.first().floorTexture == "FLAT5", "Doom flats", error);
		request.scope = LevelMaterialFindScope::Picks; request.faces = {{0, 0}};
		expect(!previewLevelMaterialReplace(doom, request, &report, &error) && !error.isEmpty(), "picks are refused on Doom maps");
	}
}

void failures()
{
	struct Case { const char* label; std::function<void(LevelMapDocument&, LevelMaterialReplaceRequest&)> setup; };
	const QVector<Case> cases = {
		{"empty find", [](auto&, auto& r) { r.find = QStringLiteral("  "); }},
		{"empty replacement", [](auto&, auto& r) { r.replace.clear(); }},
		{"empty selection", [](auto&, auto& r) { r.scope = LevelMaterialFindScope::Selection; }},
		{"no picks", [](auto&, auto& r) { r.scope = LevelMaterialFindScope::Picks; }},
		{"missing pick", [](auto&, auto& r) { r.scope = LevelMaterialFindScope::Picks; r.faces = {{0, 1}, {9, 0}}; }},
		{"missing patch pick", [](auto&, auto& r) { r.scope = LevelMaterialFindScope::Picks; r.patches = {44}; }},
		{"too many picks", [](auto&, auto& r) { r.scope = LevelMaterialFindScope::Picks; r.faces.fill({0, 1}, 16385); }},
		{"no match", [](auto&, auto& r) { r.find = QStringLiteral("absent"); }},
		{"already the replacement", [](auto&, auto& r) { r.replace = QStringLiteral("studio/checker"); }},
		{"unwritable name", [](auto&, auto& r) { r.replace = QStringLiteral("bad name"); }},
		{"every use protected", [](auto& m, auto& r) { r.excludedObjects = {"patch:0", "patch:2"}; lockEntityOne(&m); }},
		{"unknown format", [](auto& m, auto&) { m.format = LevelMapFormat::Unknown; }},
	};
	for (const auto& test : cases) {
		auto map = fixture();
		LevelMaterialReplaceRequest request; request.find = QStringLiteral("studio/checker"); request.replace = QStringLiteral("base/metal");
		test.setup(map, request);
		const auto bytes = serializeLevelMap(map).bytes; const auto undo = map.undoStack.size(); const auto revision = map.revision;
		LevelMaterialReplaceReport sentinel; sentinel.replaced = 77; QString error;
		expect(!replaceLevelMapMaterial(&map, request, &sentinel, &error) && !error.isEmpty() && sentinel.replaced == 77
			&& serializeLevelMap(map).bytes == bytes && map.undoStack.size() == undo && map.revision == revision, test.label, error);
	}
	auto map = fixture(); QString error; int replaced = 5; const auto bytes = serializeLevelMap(map).bytes;
	expect(!replaceLevelMapTextureUses(&map, "metal", "other", {{QStringLiteral("face"), 0, 99, {}, {}}}, &replaced, &error) && !error.isEmpty()
		&& serializeLevelMap(map).bytes == bytes && map.undoStack.isEmpty(), "an explicit surface that no longer exists refuses the edit", error);
	expect(replaceLevelMapTextureUses(&map, "METAL", "other", {{QStringLiteral("face"), 0, 0, QStringLiteral("ignored"), {}}, {QStringLiteral("face"), 0, 0, {}, {}}}, &replaced, &error)
		&& replaced == 1 && map.brushes[0].faces[0].textureName == "other", "explicit surfaces read their names from the document", error);
}

void findWithinSelection()
{
	auto map = fixture(); setLevelMapSelection(&map, {brush(1)});
	LevelTexturedSelectionRequest request; request.material = QStringLiteral("studio/checker"); request.withinSelection = true;
	LevelTexturedSelectionReport report; QString error;
	expect(selectLevelMapTextured(&map, request, &report, &error) && report.faces.size() == 6 && report.matchedPatches.isEmpty()
		&& std::all_of(report.faces.cbegin(), report.faces.cend(), [](const auto& face) { return face.brushId == 1; }), "Find within one selected brush", error);
	map = fixture(); setLevelMapSelection(&map, {entity(1)});
	expect(selectLevelMapTextured(&map, request, &report, &error) && report.faces.size() == 11 && report.matchedPatches == QVector<int>{1},
		"a selected entity's brushes and patches", error);
	map = fixture(); const auto bytes = serializeLevelMap(map).bytes;
	expect(!selectLevelMapTextured(&map, request, &report, &error) && !error.isEmpty() && serializeLevelMap(map).bytes == bytes, "nothing selected to search within");
	request.withinSelection = false;
	expect(selectLevelMapTextured(&map, request, &report, &error) && report.faces.size() == 11 && report.matchedPatches.size() == 3, "the whole map without the flag", error);
}

void cli(const QString& executable)
{
	QTemporaryDir temp; if (!expect(temp.isValid(), "CLI temporary directory")) { return; }
	const auto path = temp.filePath("find.map"), output = temp.filePath("replaced.map");
	const auto source = tests::typeSelectionFixture(); QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(), "CLI fixture")) { return; }
	file.close();
	const auto run = [&](const QStringList& args, int code = 0) {
		QProcess process; process.setWorkingDirectory(temp.path());
		process.start(executable, QStringList{"--cli", "--json", "--settings-file", temp.filePath("settings.ini"), "map"} + args);
		const bool done = process.waitForStarted() && process.waitForFinished(30000);
		const auto out = process.readAllStandardOutput(), err = process.readAllStandardError();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code, "CLI status", args.join(' ') + QLatin1Char('\n') + QString::fromUtf8(out + err));
		if (!done) { process.kill(); process.waitForFinished(); }
		return QJsonDocument::fromJson(out).object();
	};
	auto report = run({"replace-texture", path, "--from", "STUDIO/CHECKER", "--to", "base/metal", "--face", "face:1:1", "--face", "face:0:1", "--output", output});
	expect(report["replaced"].toInt() == 1 && report["skipped"].toInt() == 0 && report["scope"].toString() == "picks"
		&& report["targets"].toArray() == QJsonArray{"face:1:1"}, "CLI picked-face replace", QString::fromUtf8(QJsonDocument(report).toJson()));
	LevelMapDocument result; QString error;
	if (expect(loadLevelMap({output, {}, {}}, &result, &error), "CLI output", error)) {
		expect(result.brushes[1].faces[0].textureName == "base/metal" && result.brushes[1].faces[1].textureName == "studio/checker"
			&& result.brushes[0].faces[0].textureName == "metal", "CLI changes exactly the picked face");
	}
	report = run({"replace-texture", path, "--from", "studio/checker", "--to", "base/metal", "--output", output, "--dry-run", "--overwrite"});
	expect(report["replaced"].toInt() == 14 && report["scope"].toString() == "map", "CLI whole map");
	report = run({"replace-texture", path, "--from", "studio/checker", "--to", "base/metal", "--object", "brush:1", "--output", output, "--overwrite"});
	expect(report["replaced"].toInt() == 6 && report["scope"].toString() == "selection", "CLI selected objects");
	run({"replace-texture", path, "--from", "studio/checker", "--to", "base/metal", "--face", "patch:0", "--output", output, "--overwrite"}, 2);
	run({"replace-texture", path, "--from", "studio/checker", "--to", "base/metal", "--face", "face:1:1", "--object", "brush:1", "--output", output, "--overwrite"}, 2);
	run({"replace-texture", path, "--from", "studio/checker", "--to", "base/metal", "--face", "face:9:1", "--output", output, "--overwrite"}, 1);
	run({"replace-texture", path, "--from", "absent", "--to", "base/metal", "--output", output, "--overwrite"}, 1);
	report = run({"select-textured", path, "--material", "studio/checker", "--object", "brush:1", "--within-selection"});
	expect(report["faces"].toArray().size() == 6 && report["matchedPatches"].toArray().isEmpty() && report["withinSelection"].toBool(),
		"CLI Find within the selection", QString::fromUtf8(QJsonDocument(report).toJson()));
	run({"select-textured", path, "--material", "studio/checker", "--within-selection"}, 2);
	run({"select-type", path, "--within-selection"}, 2);
	expect(file.open(QIODevice::ReadOnly) && file.readAll() == source, "CLI leaves the source map intact");
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	reveal(); preview(); replace(); failures(); findWithinSelection();
	if (argc == 2) { cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath()); }
	std::cout << (ok ? "Material find and replace checks passed.\n" : "Material find and replace checks failed.\n");
	return ok ? 0 : 1;
}
