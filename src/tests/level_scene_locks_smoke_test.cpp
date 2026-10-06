#include "core/audio_level.h"
#include "core/level_brush.h"
#include "core/level_material_paint.h"
#include "core/level_merge.h"
#include "core/level_patch_cap.h"
#include "core/level_patch_stitch.h"
#include "core/level_primitive.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_surface.h"
#include "core/model_design.h"
#include "core/package_staging.h"
#include "core/texture_handoff.h"
#include "tests/level_prefab_test_helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
int refusalCases = 0;
bool expect(bool pass, const char* label, const QString& error = {}) {
	if (!pass) {
		std::cerr << label << ": " << error.toStdString() << '\n';
		ok = false;
	}
	return pass;
}
QString lock(LevelMapDocument* map, const QStringList& objects = {}) {
	QString node, error;
	expect(createLevelSceneNode(map, LevelSceneNodeKind::Layer, QStringLiteral("Protected"), {}, &node, &error), "create lock layer",
		   error);
	if (!objects.isEmpty()) {
		expect(assignLevelSceneObjects(map, node, objects, &error), "assign lock members", error);
	}
	expect(setLevelSceneLocked(map, node, true, &error), "lock layer", error);
	return node;
}
using Operation = std::function<bool(LevelMapDocument*, QString*)>;
void blocked(const char* name, const LevelMapDocument& base, const QStringList& objects, const Operation& operation,
			 bool creation = false) {
	auto map = base;
	const auto node = lock(&map, objects);
	if (creation) {
		map.activeSceneNode = node;
	}
	markLevelMapSaved(&map);
	const auto before = map;
	const auto bytes = serializeLevelMap(map);
	expect(bytes.succeeded(), "serializable refusal fixture", bytes.errors.join(';'));
	QString error;
	const bool applied = operation(&map, &error);
	expect(!applied && error.contains(QStringLiteral("Unlock")), name, error);
	expect(map.revision == before.revision && map.selection == before.selection && map.scene == before.scene &&
			   map.undoStack.size() == before.undoStack.size() && map.redoStack.size() == before.redoStack.size() &&
			   map.savedUndoDepth == before.savedUndoDepth && map.editState == before.editState &&
			   map.doomTopologyRevision == before.doomTopologyRevision && map.doomGeometryEdits == before.doomGeometryEdits &&
			   serializeLevelMap(map).bytes == bytes.bytes,
		   "refusal preserves native data, selection, scene and history", QString::fromLatin1(name));
	// A refusal is only meaningful when this same request works after unlocking.
	expect(setLevelSceneLocked(&map, node, false, &error), "unlock control", error);
	expect(operation(&map, &error), "unlocked positive control", QString::fromLatin1(name) + QStringLiteral(": ") + error);
	++refusalCases;
}
QByteArray wav() {
	QByteArray bytes(46, '\0');
	bytes.replace(0, 4, "RIFF");
	qToLittleEndian<quint32>(38, bytes.data() + 4);
	bytes.replace(8, 8, "WAVEfmt ");
	qToLittleEndian<quint32>(16, bytes.data() + 16);
	qToLittleEndian<quint16>(1, bytes.data() + 20);
	qToLittleEndian<quint16>(1, bytes.data() + 22);
	qToLittleEndian<quint32>(22050, bytes.data() + 24);
	qToLittleEndian<quint32>(44100, bytes.data() + 28);
	qToLittleEndian<quint16>(2, bytes.data() + 32);
	qToLittleEndian<quint16>(16, bytes.data() + 34);
	bytes.replace(36, 4, "data");
	qToLittleEndian<quint32>(2, bytes.data() + 40);
	return bytes;
}
} // namespace

int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	QString error;
	LevelMapDocument fixture;
	if (!expect(tests::loadPrefabFixture(&fixture, &error), "fixture", error)) {
		return 1;
	}
	const QStringList brush{QStringLiteral("brush:0")}, entity{QStringLiteral("entity:3")}, patch{QStringLiteral("patch:0")};
	blocked("model reference", fixture, entity,
			[](auto* d, auto* e) { return setLevelMapEntityProperty(d, 3, "model", "models/other.md3", e); });
	blocked("multi entity property", fixture, entity,
			[](auto* d, auto* e) { return setLevelMapEntitiesProperty(d, {2, 3}, "targetname", {"changed"}, e); });
	blocked("remove entity property", fixture, entity, [](auto* d, auto* e) { return removeLevelMapEntityProperty(d, 3, "angle", e); });
	blocked("remove entity properties", fixture, entity,
			[](auto* d, auto* e) { return removeLevelMapEntitiesProperty(d, {2, 3}, "origin", e); });
	blocked("primitive owner properties", fixture, brush,
			[](auto* d, auto* e) { return setLevelMapEntityProperty(d, 1, "speed", "220", e); });
	blocked("move primitive", fixture, brush, [](auto* d, auto* e) { return moveLevelMapObject(d, "brush", 0, 8, 0, 0, e); });
	blocked("move primitive owner", fixture, brush, [](auto* d, auto* e) { return moveLevelMapObject(d, "entity", 1, 8, 0, 0, e); });
	blocked("move primitive texture lock", fixture, brush,
			[](auto* d, auto* e) { return moveLevelMapObject(d, "brush", 0, 8, 0, 0, {true, false}, e); });
	blocked("move model", fixture, entity, [](auto* d, auto* e) { return moveLevelMapObject(d, "entity", 3, 8, 0, 0, e); });
	blocked("brush face material", fixture, brush,
			[](auto* d, auto* e) { return setLevelMapBrushFaceProperty(d, 0, 0, "texture", "changed", e); });
	blocked("brush face mapping", fixture, brush,
			[](auto* d, auto* e) { return setLevelMapBrushFaceProperty(d, 0, 0, "shiftx", "16", e); });
	blocked("surface mapping plan", fixture, brush, [](auto* d, auto* e) {
		LevelSurfaceEditPlan plan;
		LevelSurfaceRequest request;
		request.operation = LevelSurfaceOperation::Shift;
		request.x = 16;
		return prepareLevelSurfaceEdit(*d, {{0, 0}}, request, {}, &plan, e) && commitLevelSurfaceEdit(d, plan, e);
	});
	blocked("mixed material stroke", fixture, patch, [](auto* d, auto* e) {
		LevelMaterialPaintPlan plan;
		return prepareLevelMaterialPaint(*d, {{LevelMaterialKind::BrushFace, 0, 0}, {LevelMaterialKind::Patch, 0, 0}}, "changed", &plan,
										 e) &&
			   commitLevelMaterialPaint(d, plan, e);
	});
	blocked("global material replacement", fixture, brush,
			[](auto* d, auto* e) { return replaceLevelMapTexture(d, "studio/checker", "changed", false, nullptr, e); });
	blocked("brush component commit", fixture, brush, [](auto* d, auto* e) {
		auto replacement = d->brushes.first();
		for (auto& face : replacement.faces) {
			face.p0.x += 8;
			face.p1.x += 8;
			face.p2.x += 8;
		}
		return replaceLevelMapBrushGeometry(d, 0, replacement, e);
	});
	blocked("patch control grid", fixture, patch, [](auto* d, auto* e) {
		auto replacement = d->patches.first();
		replacement.controlPoints[0].z += 8;
		return replaceLevelMapPatches(d, {{0, replacement}}, e);
	});
	auto selected = fixture;
	setLevelMapSelection(&selected, {{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakeBrush, 1}});
	blocked("compound move", selected, brush, [](auto* d, auto* e) { return moveLevelMapSelection(d, 8, 0, 0, e); });
	blocked("snapped move", selected, brush,
			[](auto* d, auto* e) { return moveLevelMapSelectionSnapped(d, 16, 0, 0, 8, {true, false}, e); });
	blocked("resize", selected, brush,
			[](auto* d, auto* e) { return resizeLevelMapSelection(d, {0, 0, 0, true}, {128, 128, 192, true}, e); });
	blocked("rotate", selected, brush, [](auto* d, auto* e) { return rotateLevelMapSelection(d, 2, 1, e); });
	blocked("flip", selected, brush, [](auto* d, auto* e) { return flipLevelMapSelection(d, 0, e); });
	blocked("snap", selected, {QStringLiteral("brush:1")}, [](auto* d, auto* e) { return snapLevelMapSelectionToGrid(d, 128, e); });
	blocked("delete selection", selected, brush, [](auto* d, auto* e) { return deleteLevelMapSelection(d, e); });
	blocked("duplicate locked source", selected, brush, [](auto* d, auto* e) { return duplicateLevelMapSelection(d, 128, 0, 0, e); });
	blocked("apply selection material", selected, brush, [](auto* d, auto* e) { return applyLevelMapTexture(d, "changed", nullptr, e); });
	blocked("convex merge", selected, brush, [](auto* d, auto* e) {
		LevelBrushMergePlan plan;
		return prepareLevelBrushMerge(*d, {}, &plan, e) && commitLevelBrushMerge(d, plan, e);
	});
	setLevelMapSelection(&selected, {{LevelMapSelectionKind::QuakeBrush, 0}});
	blocked("clip", selected, brush, [](auto* d, auto* e) {
		return clipLevelMapSelection(d, {16, 0, 0, true}, {16, 64, 0, true}, {16, 0, 96, true}, LevelMapClipKeep::Both, nullptr, e);
	});
	blocked("hollow", selected, brush, [](auto* d, auto* e) { return hollowLevelMapSelection(d, 4, nullptr, e); });
	LevelMapDocument carving;
	LevelMapCreateRequest empty;
	empty.starterRoom = false;
	createLevelMap(empty, &carving);
	addLevelMapBoxBrush(&carving, {0, 0, 0, true}, {128, 128, 128, true}, "stone");
	addLevelMapBoxBrush(&carving, {32, 32, -16, true}, {96, 96, 144, true}, "stone");
	blocked("carve unselected protected target", carving, brush, [](auto* d, auto* e) { return carveLevelMapSelection(d, nullptr, e); });
	setLevelMapSelection(&selected, {{LevelMapSelectionKind::Entity, 2}, {LevelMapSelectionKind::Entity, 3}});
	blocked("connect entities", selected, entity, [](auto* d, auto* e) { return connectLevelMapEntities(d, nullptr, e); });
	LevelMapPatch newPatch;
	createLevelPatch({}, &newPatch);
	blocked("append primitive to locked owner", fixture, {QStringLiteral("entity:1")},
			[&](auto* d, auto* e) { return addLevelMapPatches(d, {newPatch}, 1, nullptr, e); });
	blocked(
		"new model destination", fixture, {},
		[](auto* d, auto* e) {
			return addLevelMapEntity(d, "misc_model", {128, 0, 0, true}, {{"model", "models/test.md3", 0}}, nullptr, e);
		},
		true);
	blocked(
		"new brush destination", fixture, {},
		[](auto* d, auto* e) { return addLevelMapBoxBrush(d, {128, 0, 0, true}, {160, 32, 32, true}, "stone", nullptr, e); }, true);
	blocked("new patch destination", fixture, {}, [&](auto* d, auto* e) { return addLevelMapPatch(d, newPatch, nullptr, e); }, true);
	const auto copiedText = levelMapSelectionText(fixture, &error);
	blocked("paste destination", fixture, {}, [&](auto* d, auto* e) { return pasteLevelMapText(d, copiedText, e); }, true);
	LevelPrefab prefab;
	LevelPrefabCreateRequest prefabRequest;
	prefabRequest.name = "Assembly";
	expect(createLevelPrefab(fixture, prefabRequest, &prefab, nullptr, &error), "prefab fixture", error);
	blocked("prefab destination", fixture, {}, [&](auto* d, auto* e) { return insertLevelPrefab(d, prefab, {}, nullptr, e); }, true);
	auto stitching = fixture;
	LevelPatchCreateRequest plane;
	plane.center = {162, 32, 100, true};
	plane.texture = "studio/checker";
	LevelMapPatch adjacent;
	createLevelPatch(plane, &adjacent);
	addLevelMapPatch(&stitching, adjacent);
	blocked("patch stitch", stitching, patch, [](auto* d, auto* e) { return stitchLevelMapPatches(d, 0, 1, {}, nullptr, e); });
	LevelPatchCreateRequest cylinder;
	cylinder.shape = "cylinder";
	LevelMapPatch curved;
	createLevelPatch(cylinder, &curved);
	auto capping = fixture;
	addLevelMapPatch(&capping, curved);
	blocked("patch cap destination", capping, {}, [](auto* d, auto* e) { return capLevelMapPatch(d, 1, {}, nullptr, e); }, true);
	// A package-and-sound handoff must not publish half its transaction.
	auto soundMap = fixture;
	soundMap.activeSceneNode = lock(&soundMap);
	PackageStagingModel staging;
	expect(staging.createEmpty(PackageArchiveFormat::Pk3, {}, &error), "pending package", error);
	const auto stagingBefore = staging.plannedEntries().size();
	LevelSoundRequest sound;
	sound.virtualPath = "sound/lock.wav";
	sound.game = "quake3";
	sound.origin = {0, 0, 0, true};
	sound.mode = "loop-on";
	int placed = -1;
	expect(!stageLevelSound(&staging, &soundMap, sound, wav(), false, &placed, &error) && placed == -1 && error.contains("Unlock") &&
			   staging.plannedEntries().size() == stagingBefore && soundMap.entities.size() == fixture.entities.size(),
		   "sound/package atomic lock refusal", error);
	setLevelSceneLocked(&soundMap, soundMap.activeSceneNode, false);
	expect(stageLevelSound(&staging, &soundMap, sound, wav(), false, &placed, &error) && placed >= 0 &&
			   staging.plannedEntries().size() == stagingBefore + 1,
		   "sound/package unlocked handoff", error);
	auto modelMap = fixture;
	const auto modelNode = lock(&modelMap);
	modelMap.activeSceneNode = modelNode;
	ModelDesign design;
	design.parts << ModelDesignPart{};
	const auto packageRevision = staging.revision(), mapRevision = modelMap.revision;
	expect(!stageModelDesign(design, "models/locked.md3", &staging, &modelMap, {256, 0, 0, true}, false, &error) &&
			   error.contains("Unlock") && staging.revision() == packageRevision && modelMap.revision == mapRevision,
		   "model/package atomic lock refusal", error);
	setLevelSceneLocked(&modelMap, modelNode, false);
	expect(stageModelDesign(design, "models/locked.md3", &staging, &modelMap, {256, 0, 0, true}, false, &error),
		   "model/package unlocked handoff", error);
	auto textureMap = fixture;
	const auto textureNode = lock(&textureMap, brush);
	QImage image(16, 16, QImage::Format_ARGB32);
	image.fill(Qt::red);
	TextureExportOptions textureOptions;
	const auto pixels = encodeTextureExport(image, textureOptions);
	const auto texturePackageRevision = staging.revision(), textureMapRevision = textureMap.revision;
	expect(!stageAndApplyTextureExport(pixels, textureOptions, "textures/changed.png", &staging, false, &textureMap, nullptr, &error) &&
			   error.contains("Unlock") && staging.revision() == texturePackageRevision && textureMap.revision == textureMapRevision,
		   "texture/package atomic lock refusal", error);
	// Scene locks protect native map records, not the contents of shared assets.
	expect(
		stageAndApplyTextureExport(pixels, textureOptions, "textures/studio/checker.png", &staging, false, &textureMap, nullptr, &error) &&
			textureMap.revision == textureMapRevision,
		"restaging identical map reference remains available", error);
	setLevelSceneLocked(&textureMap, textureNode, false);
	expect(stageAndApplyTextureExport(pixels, textureOptions, "textures/changed.png", &staging, false, &textureMap, nullptr, &error),
		   "texture/package unlocked handoff", error);
	// Copies are inspection: paste into an unlocked destination remains possible.
	auto copyMap = selected;
	const auto copyNode = lock(&copyMap, entity);
	expect(!levelMapSelectionText(copyMap, &error).isEmpty() && pasteLevelMapText(&copyMap, copiedText, &error),
		   "copy and paste outside lock", error);
	expect(setLevelMapEntityProperty(&copyMap, 2, "target", "elsewhere", &error), "unrelated entity remains editable", error);
	expect(undoLevelMapEdit(&copyMap, &error) && redoLevelMapEdit(&copyMap, &error), "unrelated history replays with locks", error);
	Q_UNUSED(copyNode);
	// An empty locked layer still rejects creation and never leaks output IDs.
	auto outputs = fixture;
	outputs.activeSceneNode = lock(&outputs);
	int created = 731;
	expect(!addLevelMapEntity(&outputs, "light", {0, 0, 0, true}, {}, &created, &error) && created == 731,
		   "refusal preserves caller output", error);
	// Lock state is undoable; nested local unlock cannot bypass an ancestor.
	auto hierarchy = fixture;
	QString parent, child;
	createLevelSceneNode(&hierarchy, LevelSceneNodeKind::Layer, "Root", {}, &parent);
	createLevelSceneNode(&hierarchy, LevelSceneNodeKind::Group, "Child", parent, &child);
	assignLevelSceneObjects(&hierarchy, child, brush);
	setLevelSceneLocked(&hierarchy, parent, true);
	expect(levelSceneLockedNodes(hierarchy.scene).contains(child) && setLevelSceneLocked(&hierarchy, child, false),
		   "inherited lock survives local false");
	expect(!moveLevelMapObject(&hierarchy, "brush", 0, 8, 0, 0, &error), "inherited geometry protection", error);
	const auto hierarchyState = hierarchy.scene;
	expect(!assignLevelSceneObjects(&hierarchy, {}, brush, &error) && !reparentLevelSceneNode(&hierarchy, child, {}, &error) &&
			   !removeLevelSceneNode(&hierarchy, parent, &error) && !resetLevelScene(&hierarchy, &error) &&
			   !createLevelSceneNode(&hierarchy, LevelSceneNodeKind::Group, "New child", child, nullptr, &error) &&
			   hierarchy.scene == hierarchyState,
		   "scene organization cannot escape locks", error);
	expect(renameLevelSceneNode(&hierarchy, parent, "Renamed", &error) && setLevelSceneVisible(&hierarchy, parent, false, &error),
		   "name and visibility remain available", error);
	setLevelSceneVisible(&hierarchy, parent, true);
	setLevelSceneLocked(&hierarchy, parent, false);
	expect(undoLevelMapEdit(&hierarchy) && levelSceneLockedNodes(hierarchy.scene).contains(child) && redoLevelMapEdit(&hierarchy) &&
			   levelSceneLockedNodes(hierarchy.scene).isEmpty(),
		   "lock history exact");
	// Previous version has no locks; version 2 requires a boolean lock field.
	auto persisted = fixture;
	const auto persistedNode = lock(&persisted, brush);
	const auto saved = serializeLevelMap(persisted).bytes;
	LevelMapDocument restored;
	expect(loadLevelMapBytes({"locks.map", {}, {}}, saved, &restored, &error) && restored.scene == persisted.scene &&
			   !moveLevelMapObject(&restored, "brush", 0, 8, 0, 0, &error),
		   "native lock roundtrip", error);
	const QByteArray marker("\n// VibeStudioScene: ");
	const auto offset = saved.lastIndexOf(marker);
	auto json = QJsonDocument::fromJson(QByteArray::fromBase64(saved.mid(offset + marker.size()).trimmed())).object();
	json["version"] = 1;
	auto nodes = json["nodes"].toArray();
	auto oldNode = nodes[0].toObject();
	oldNode.remove("locked");
	nodes[0] = oldNode;
	json["nodes"] = nodes;
	auto legacy = saved.first(offset) + marker + QJsonDocument(json).toJson(QJsonDocument::Compact).toBase64() + '\n';
	expect(loadLevelMapBytes({"legacy.map", {}, {}}, legacy, &restored, &error) && restored.scene.problem.isEmpty() &&
			   !levelSceneNode(restored.scene, persistedNode)->locked && moveLevelMapObject(&restored, "brush", 0, 8, 0, 0, &error),
		   "version 1 compatibility", error);
	json["version"] = 2;
	oldNode["locked"] = "true";
	nodes[0] = oldNode;
	json["nodes"] = nodes;
	legacy = saved.first(offset) + marker + QJsonDocument(json).toJson(QJsonDocument::Compact).toBase64() + '\n';
	expect(loadLevelMapBytes({"bad-lock.map", {}, {}}, legacy, &restored, &error) && !restored.scene.problem.isEmpty() &&
			   serializeLevelMap(restored).bytes == legacy,
		   "malformed lock metadata preserved", error);

	for (const QString& game : {QStringLiteral("doom"), QStringLiteral("hexen")}) {
		LevelMapCreateRequest request;
		request.game = game;
		LevelMapDocument doom;
		createLevelMap(request, &doom);
		const QStringList sector{QStringLiteral("sector:0")}, line{QStringLiteral("linedef:0")}, thing{QStringLiteral("thing:0")};
		blocked("sector height", doom, sector, [](auto* d, auto* e) { return setLevelMapSectorProperty(d, 0, "floorheight", "16", e); });
		blocked("shared sector side", doom, sector,
				[](auto* d, auto* e) { return setLevelMapSidedefProperty(d, 0, "middle", "CHANGED", e); });
		blocked("line endpoint move", doom, line, [](auto* d, auto* e) { return moveLevelMapObject(d, "vertex", 0, 8, 0, 0, e); });
		blocked("sector boundary move", doom, sector, [](auto* d, auto* e) { return moveLevelMapObject(d, "linedef", 0, 8, 0, 0, e); });
		blocked("linedef property", doom, sector, [](auto* d, auto* e) { return setLevelMapLinedefProperty(d, 0, "flags", "3", e); });
		blocked("linedef side property", doom, line,
				[](auto* d, auto* e) { return setLevelMapLinedefSideProperty(d, 0, true, "offsetx", "8", e); });
		blocked("thing alias property", doom, thing, [](auto* d, auto* e) { return setLevelMapEntityProperty(d, 0, "angle", "180", e); });
		blocked("delete thing", doom, thing,
				[](auto* d, auto* e) { return deleteLevelMapObjects(d, {{LevelMapSelectionKind::DoomThing, 0}}, e); });
		blocked("duplicate thing", doom, thing,
				[](auto* d, auto* e) { return duplicateLevelMapObjects(d, {{LevelMapSelectionKind::DoomThing, 0}}, 16, 0, 0, e); });
		blocked("new thing destination", doom, {}, [](auto* d, auto* e) { return addLevelMapDoomThing(d, 1, 0, 0, 0, nullptr, e); }, true);
		blocked("Doom material stroke", doom, sector, [](auto* d, auto* e) {
			LevelMaterialPaintPlan plan;
			return prepareLevelMaterialPaint(*d, {{LevelMaterialKind::WallMiddle, 0, 0}}, "CHANGED", &plan, e) &&
				   commitLevelMaterialPaint(d, plan, e);
		});
		setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomLinedef, 0}});
		blocked("split protected boundary", doom, sector, [](auto* d, auto* e) { return splitLevelMapLinedefs(d, nullptr, e); });
		blocked("flip protected boundary", doom, line, [](auto* d, auto* e) { return flipLevelMapLinedefs(d, nullptr, e); });
		blocked("delete protected boundary", doom, sector, [](auto* d, auto* e) { return deleteLevelMapSelection(d, e); });
		setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomSector, 0}});
		blocked("shift protected sector", doom, sector,
				[](auto* d, auto* e) { return shiftLevelMapSectors(d, LevelMapSectorField::Floor, 16, nullptr, e); });
		// A detached room supplies both a topology refusal and a legal compaction:
		// deleting lower IDs must not falsely modify the protected second room.
		const QVector<LevelMapVec3> room{{1024, 0, 0, true}, {1152, 0, 0, true}, {1152, 128, 0, true}, {1024, 128, 0, true}};
		blocked(
			"draw in locked destination", doom, {}, [&](auto* d, auto* e) { return drawLevelMapDoomSector(d, room, nullptr, e); }, true);
		expect(drawLevelMapDoomSector(&doom, room, nullptr, &error), "detached room", error);
		setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomSector, 0}, {LevelMapSelectionKind::DoomSector, 1}});
		blocked("join protected sector", doom, sector, [](auto* d, auto* e) { return joinLevelMapSectors(d, false, nullptr, e); });
		auto gradient = doom;
		const QVector<LevelMapVec3> thirdRoom{{2048, 0, 0, true}, {2176, 0, 0, true}, {2176, 128, 0, true}, {2048, 128, 0, true}};
		expect(drawLevelMapDoomSector(&gradient, thirdRoom, nullptr, &error), "gradient room", error);
		setLevelMapSectorProperty(&gradient, 0, "lightlevel", "100");
		setLevelMapSectorProperty(&gradient, 1, "lightlevel", "0");
		setLevelMapSectorProperty(&gradient, 2, "lightlevel", "200");
		setLevelMapSelection(
			&gradient,
			{{LevelMapSelectionKind::DoomSector, 0}, {LevelMapSelectionKind::DoomSector, 1}, {LevelMapSelectionKind::DoomSector, 2}});
		blocked("sector gradient", gradient, {QStringLiteral("sector:1")},
				[](auto* d, auto* e) { return gradientLevelMapSectors(d, LevelMapSectorField::Light, nullptr, e); });
		auto welding = doom;
		setLevelMapSelection(&welding, {{LevelMapSelectionKind::DoomVertex, 0}, {LevelMapSelectionKind::DoomVertex, 1}});
		blocked("vertex weld", welding, sector, [](auto* d, auto* e) { return mergeLevelMapVertices(d, nullptr, e); });
		auto doorway = doom;
		const QVector<LevelMapVec3> neighbor{{256, -256, 0, true}, {256, 256, 0, true}, {384, 256, 0, true}, {384, -256, 0, true}};
		blocked("attach geometry to protected sector", doorway, sector,
				[&](auto* d, auto* e) { return drawLevelMapDoomSector(d, neighbor, nullptr, e); });
		expect(drawLevelMapDoomSector(&doorway, neighbor, nullptr, &error), "adjacent room", error);
		setLevelMapSelection(&doorway, {{LevelMapSelectionKind::DoomSector, 2}});
		blocked("door modifies shared protected side", doorway, sector,
				[](auto* d, auto* e) { return makeLevelMapDoors(d, {}, nullptr, e); });
		const auto roomNode = lock(&doom, {QStringLiteral("sector:1")});
		doom.undoLimit = 1;
		expect(deleteLevelMapObjects(&doom, {{LevelMapSelectionKind::DoomSector, 0}}, &error) &&
				   levelSceneMembership(doom.scene, QStringLiteral("sector:0")) == roomNode && doom.doomSectors.size() == 1,
			   "unrelated deletion carries locked geometry through compacted IDs", error);
		expect(undoLevelMapEdit(&doom, &error) && doom.doomSectors.size() == 2 && redoLevelMapEdit(&doom, &error),
			   "locked compacted history", error);
		const auto bytes = serializeLevelMap(doom);
		expect(bytes.succeeded() && loadLevelMapBytes({"locks.wad", doom.mapName, {}}, bytes.bytes, &restored, &error) &&
				   !setLevelMapSectorProperty(&restored, 0, "floorheight", "16", &error),
			   "binary lock roundtrip", error);
	}
	std::cout << refusalCases << " lock refusals with unlocked controls; " << (ok ? "passed\n" : "FAILED\n");
	return ok ? 0 : 1;
}
