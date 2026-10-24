#include "core/level_brush.h"
#include "core/level_document.h"
#include "core/level_linked_groups.h"
#include "core/level_placement.h"
#include "core/level_protected_properties.h"
#include "core/level_repeat.h"
#include "core/level_repeat_p.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_texture_mapping.h"
#include "tests/level_prefab_test_helpers.h"
#include "tests/level_udmf_test_helpers.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool condition, const char* description, const QString& error = {})
{
	if (!condition) {
		std::cerr << "FAIL: " << description << ": " << error.toStdString() << '\n';
	}
	return condition;
}
bool fixture(LevelMapDocument* document, QString* error)
{
	LevelMapCreateRequest create;
	create.starterRoom = false;
	LevelMapDocument draft;
	return createLevelMap(create, &draft, error) &&
	       addLevelMapBoxBrush(&draft, {0, 0, 0, true}, {16, 16, 16, true}, "repeat/first", nullptr, error) &&
	       addLevelMapBoxBrush(&draft, {128, 0, 0, true}, {144, 16, 16, true}, "repeat/second", nullptr, error) &&
	       loadLevelMapBytes({"repeat.map", {}, "idTech3"}, serializeLevelMap(draft).bytes, document, error);
}
void pick(LevelMapDocument* document, int brush)
{
	setLevelMapSelection(document, {{LevelMapSelectionKind::QuakeBrush, brush}});
}
bool equivalentBrushes(const LevelMapDocument& actual, const LevelMapDocument& expected)
{
	if (actual.brushes.size() != expected.brushes.size()) {
		return false;
	}
	const auto near = [](double a, double b) { return std::abs(a - b) < 1e-7; };
	for (qsizetype i = 0; i < actual.brushes.size(); ++i) {
		const auto& a = actual.brushes[i];
		const auto& b = expected.brushes[i];
		if (a.id != b.id || a.entityId != b.entityId || a.faces.size() != b.faces.size()) {
			return false;
		}
		for (qsizetype j = 0; j < a.faces.size(); ++j) {
			const auto& x = a.faces[j];
			const auto& y = b.faces[j];
			if (x.textureName != y.textureName || x.contentFlags != y.contentFlags || x.surfaceFlags != y.surfaceFlags ||
			    x.surfaceValue != y.surfaceValue) {
				return false;
			}
			const auto u = levelTextureProjection(x), v = levelTextureProjection(y);
			if (!u.valid || !v.valid || u.normalizedCoordinates != v.normalizedCoordinates) {
				return false;
			}
			const std::array<LevelMapVec3, 3> xp{x.p0, x.p1, x.p2}, yp{y.p0, y.p1, y.p2};
			for (int k = 0; k < 3; ++k) {
				if (!near(xp[k].x, yp[k].x) || !near(xp[k].y, yp[k].y) || !near(xp[k].z, yp[k].z)) {
					return false;
				}
				const auto uv = u.at(xp[k]), expectedUv = v.at(yp[k]);
				if (!near(uv.x(), expectedUv.x()) || !near(uv.y(), expectedUv.y())) {
					return false;
				}
			}
		}
	}
	return true;
}
} // namespace
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	LevelMapDocument initial;
	if (!fixture(&initial, &error)) {
		return expect(false, "fixture", error) ? 0 : 1;
	}
	const LevelMapTextureLockOptions textures{true, true};
	bool ok = true;
	{
		LevelMapDocument document;
		ok &= expect(loadLevelMapBytes({"repeat.wad", "MAP01", {}}, tests::udmf::fixture(), &document, &error) &&
		                 setLevelMapSelection(&document, {{LevelMapSelectionKind::DoomThing, 0}}, &error) &&
		                 moveLevelMapSelection(&document, 1.25, 0.5, 2.5, textures, &error) &&
		                 duplicateLevelMapObjects(&document, document.selection, 8, 16, 4, textures, &error) &&
		                 document.repeat.actions.size() == 2,
		             "record UDMF fractional thing moves and duplication", error);
		const auto before = serializeLevelMap(document).bytes;
		auto expected = document;
		ok &= expect(moveLevelMapSelection(&expected, 1.25, 0.5, 2.5, textures, &error) &&
		                 duplicateLevelMapObjects(&expected, expected.selection, 8, 16, 4, textures, &error) &&
		                 repeatLevelMapCommands(&document, textures, &error) &&
		                 serializeLevelMap(document).bytes == serializeLevelMap(expected).bytes,
		             "UDMF replay retains fractional values, source trivia and inserted selection", error);
		ok &= expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == before, "UDMF exact replay undo", error);
	}
	for (const QString& game : {QStringLiteral("doom"), QStringLiteral("hexen")}) {
		LevelMapDocument document;
		LevelMapCreateRequest request;
		request.game = game;
		ok &= expect(createLevelMap(request, &document, &error) && setLevelMapSelection(&document,
		                                                                                {{LevelMapSelectionKind::DoomSector, 0},
		                                                                                 {LevelMapSelectionKind::DoomLinedef, 0},
		                                                                                 {LevelMapSelectionKind::DoomVertex, 0},
		                                                                                 {LevelMapSelectionKind::DoomThing, 0}},
		                                                                                &error),
		             "Doom repeat fixture", error);
		const double height = game == "hexen" ? 4 : 0;
		ok &= expect(moveLevelMapSelection(&document, 16, 0, height, textures, &error) &&
		                 rotateLevelMapSelection(&document, {2, 90, {0, 0, 0, true}, false, false}, &error),
		             "record Doom shared geometry and thing sequence", error);
		auto expected = document;
		const auto before = serializeLevelMap(document).bytes;
		ok &= expect(moveLevelMapSelection(&expected, 16, 0, height, textures, &error) &&
		                 rotateLevelMapSelection(&expected, {2, 90, {0, 0, 0, true}, false, false}, &error) &&
		                 repeatLevelMapCommands(&document, textures, &error) &&
		                 serializeLevelMap(document).bytes == serializeLevelMap(expected).bytes,
		             "Doom replay preserves topology, winding and thing height", error);

		ok &= expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == before, "Doom repeat exact undo", error);
	}
	{
		auto document = initial;
		pick(&document, 0);
		ok &= expect(moveLevelMapSelection(&document, 8, 0, 0, textures, &error), "array recording fixture", error);
		const auto recorded = document.repeat.actions;
		ok &= expect(arrayLevelMapSelection(&document, {32, 0, 0, true}, 3, textures, &error) && document.repeat.actions == recorded &&
		                 document.repeat.clearOnNextAction,
		             "multi-copy arrays do not leak internal duplication into repeat", error);
	}
	{
		auto document = initial;
		ok &= expect(moveLevelMapObject(&document, "brush", 0, 8, 0, 0, &error) && document.repeat.actions.size() == 1,
		             "direct legacy object movement records", error);
		ok &= expect(moveLevelMapObject(&document, "brush", 1, 0, 8, 0, &error) && document.repeat.actions.size() == 1 &&
		                 document.repeat.actions.first().matrix[7] == 8,
		             "implicit object selection starts a new sequence", error);
		const auto recorded = document.repeat;
		LevelComponentTransformRequest request;
		request.delta = {0, 0, 4, true};
		ok &= expect(transformLevelMapComponents(&document, {{{LevelMapSelectionKind::QuakeBrush, 1}, LevelBrushComponent::Face, {5}}},
		                                         request, nullptr, &error) &&
		                 document.repeat.actions == recorded.actions,
		             "component transformations do not enter object repeat history", error);
	}
	for (int axis : {0, 1, 2}) {
		LevelMapDocument document;
		ok &= expect(tests::loadPrefabFixture(&document, &error), "mixed-object fixture", error);
		const auto selection = document.selection;
		auto fixtureBytes = tests::prefabFixture();
		fixtureBytes.replace("\"angle\" \"45\"", "\"angles\" \"0 45 0\"");
		ok &= expect(loadLevelMapBytes({"angles.map", {}, "idTech3"}, fixtureBytes, &document, &error) &&
		                 setLevelMapSelection(&document, selection, &error),
		             "3D entity angle fixture", error);
		const LevelMapRotationRequest rotation{axis, 30, {0, 0, 0, true}, true, true};
		ok &= expect(rotateLevelMapSelection(&document, rotation, &error) && document.repeat.actions.size() == 1,
		             "record arbitrary 3D rotation", error);
		auto expected = document;
		ok &= expect(rotateLevelMapSelection(&expected, rotation, &error) && repeatLevelMapCommands(&document, {true, true}, &error),
		             "replay arbitrary 3D rotation", error);
		ok &= expect(equivalentBrushes(document, expected), "mixed-object brush geometry and UVs match direct rotation",
		             QString::number(axis));
		for (qsizetype i = 0; i < document.entities.size(); ++i) {
			for (const auto& property : expected.entities[i].properties) {
				ok &= expect(tests::prefabProperty(document.entities[i], property.key) == property.value,
				             "entity origin, heading and properties match direct rotation", property.key);
			}
		}
		for (qsizetype i = 0; i < document.patches.size(); ++i) {
			const auto& a = document.patches[i];
			const auto& b = expected.patches[i];
			ok &= expect(a.controlU == b.controlU && a.controlV == b.controlV, "patch UVs survive repeat");
			for (qsizetype j = 0; j < a.controlPoints.size(); ++j) {
				const auto p = a.controlPoints[j], q = b.controlPoints[j];
				ok &= expect(std::hypot(p.x - q.x, p.y - q.y, p.z - q.z) < 1e-7, "patch control points match direct rotation");
			}
		}
	}
	{
		LevelMapDocument document;
		QString group, copy;
		ok &= expect(tests::loadPrefabFixture(&document, &error) &&
		                 createLevelSceneNode(&document, LevelSceneNodeKind::Group, "Assembly", {}, &group, &error) &&
		                 assignLevelSceneObjects(&document, group, {"entity:1", "entity:2", "patch:0"}, &error) &&
		                 createLinkedLevelGroup(&document, group, {256, 0, 0, true}, &copy, &error),
		             "linked assembly fixture", error);
		const auto ownerIdentity = levelSceneNode(document.scene, group)->linkObjects.value("entity:1");
		int peerOwner = -1;
		for (auto it = levelSceneNode(document.scene, copy)->linkObjects.cbegin();
		     it != levelSceneNode(document.scene, copy)->linkObjects.cend(); ++it) {
			if (it.value() == ownerIdentity) {
				peerOwner = it.key().section(':', 1).toInt();
			}
		}
		ok &= expect(peerOwner >= 0 && setLevelProtectedEntityProperty(&document, {peerOwner}, "speed", {"333"}, &error),
		             "protect linked owner", error);
		ok &= expect(openLevelSceneGroup(&document, group, &error), "open repeat group", error);
		const auto sourceSelector = levelSceneNode(document.scene, group)->linkObjects.key(ownerIdentity);
		const auto sourceOwner = sourceSelector.section(':', 1).toInt();
		peerOwner = levelSceneNode(document.scene, copy)->linkObjects.key(ownerIdentity).section(':', 1).toInt();
		ok &= expect(!sourceSelector.isEmpty() && setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, sourceOwner}}, &error),
		             "select repeat owner", error);
		ok &= expect(moveLevelMapSelection(&document, 0, 0, 8, textures, &error), "record linked assembly transform", error);
		const auto before = serializeLevelMap(document).bytes;
		const auto depth = document.undoStack.size();
		const auto recorded = document.repeat.actions;
		auto expected = document;
		ok &= expect(moveLevelMapSelection(&expected, 0, 0, 8, textures, &error) && repeatLevelMapCommands(&document, textures, &error) &&
		                 serializeLevelMap(document).bytes == serializeLevelMap(expected).bytes && document.repeat.actions == recorded &&
		                 document.undoStack.size() == depth + 1 && tests::prefabProperty(document.entities[peerOwner], "speed") == "333",
		             "repeat updates linked counterparts once and preserves instance-local properties", error);
		ok &= expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == before,
		             "undo restores every linked instance", error);
	}
	{
		auto document = initial;
		pick(&document, 0);
		ok &= expect(moveLevelMapSelection(&document, 16, 0, 0, textures, &error) &&
		                 duplicateLevelMapObjects(&document, document.selection, 0, 64, 0, textures, &error) &&
		                 rotateLevelMapSelection(&document, {2, 90, {0, 0, 0, true}, true, true}, &error),
		             "record move, duplication and fixed-pivot rotation", error);
		ok &= expect(document.repeat.actions.size() == 3 && !document.repeat.clearOnNextAction,
		             "duplicate selection remains inside the sequence");
		pick(&document, 1);
		const auto sequence = document.repeat;
		ok &= expect(sequence.actions.size() == 3 && sequence.clearOnNextAction, "selection change retains the repeat sequence");
		const auto before = serializeLevelMap(document).bytes;
		auto expected = document;
		{
			const detail::LevelRepeatCaptureScope suppress(&expected, false);
			ok &= expect(moveLevelMapSelection(&expected, 16, 0, 0, textures, &error) &&
			                 duplicateLevelMapObjects(&expected, expected.selection, 0, 64, 0, textures, &error) &&
			                 rotateLevelMapSelection(&expected, {2, 90, {0, 0, 0, true}, true, true}, &error),
			             "independent expected sequence", error);
		}
		document.undoLimit = 1;
		ok &= expect(repeatLevelMapCommands(&document, textures, &error) &&
		                 serializeLevelMap(document).bytes == serializeLevelMap(expected).bytes && document.repeat == sequence &&
		                 document.undoLimit == 1 && document.undoStack.size() == 1,
		             "repeat targets the current selection atomically without recording itself", error);
		const auto after = serializeLevelMap(document).bytes;
		ok &=
		    expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == before && document.repeat.actions.isEmpty(),
		           "one undo restores the sequence and clears repeat history", error);
		ok &= expect(redoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == after && document.repeat.actions.isEmpty(),
		             "redo remains exact without rearming repeat", error);
	}
	for (int kind = 0; kind < 4; ++kind) {
		auto document = initial;
		pick(&document, 0);
		bool changed = false;
		if (kind == 0) {
			changed = rotateLevelMapSelection(&document, {2, 90, {}, true, true}, &error);
		}
		if (kind == 1) {
			changed = resizeLevelMapSelection(&document, {10, 20, 30, true}, {42, 52, 62, true}, textures, &error);
		}
		if (kind == 2) {
			changed = shearLevelMapSelectionAbout(&document, 0, 2, 0.25, 8, textures, &error);
		}
		if (kind == 3) {
			changed = flipLevelMapSelection(&document, 0, textures, &error);
		}
		ok &= expect(changed && document.repeat.actions.size() == 1, "record each affine object transform", error);
		pick(&document, 1);
		auto expected = document;
		{
			const detail::LevelRepeatCaptureScope suppress(&expected, false);
			if (kind == 0) {
				changed = rotateLevelMapSelection(&expected, {2, 90, {8, 8, 8, true}, true, true}, &error);
			}
			if (kind == 1) {
				changed = resizeLevelMapSelection(&expected, {266, 20, 30, true}, {298, 52, 62, true}, textures, &error);
			}
			if (kind == 2) {
				changed = shearLevelMapSelectionAbout(&expected, 0, 2, 0.25, 8, textures, &error);
			}
			if (kind == 3) {
				changed =
				    flipLevelMapSelection(&expected, 0, textures, &error) && moveLevelMapSelection(&expected, -256, 0, 0, textures, &error);
			}
		}
		// Equivalent classic mappings can spell the same angle as -180 or 180.
		// Check the world geometry and UV function rather than that spelling.
		ok &= expect(changed && repeatLevelMapCommands(&document, textures, &error) && equivalentBrushes(document, expected),
		             "replay retains source world pivot, scale origin, shear plane and mirror plane", QString::number(kind) + ": " + error);
	}
	{
		auto document = initial;
		pick(&document, 0);
		ok &= expect(moveLevelMapSelection(&document, 8, 0, 0, &error) && document.repeat.actions.size() == 1, "legacy move route records",
		             error);
		ok &= expect(moveLevelMapSelection(&document, 0, 8, 0, &error) && document.repeat.actions.size() == 2,
		             "consecutive moves accumulate", error);
		addLevelMapSelection(&document, LevelMapSelectionKind::QuakeBrush, 1);
		removeLevelMapSelection(&document, LevelMapSelectionKind::QuakeBrush, 0);
		ok &= expect(document.repeat.actions.size() == 2 && document.repeat.clearOnNextAction, "add/remove selection arms replacement");
		ok &= expect(moveLevelMapSelection(&document, 0, 0, 8, &error) && document.repeat.actions.size() == 1 &&
		                 !document.repeat.clearOnNextAction,
		             "the next new repeatable edit replaces the old sequence", error);
		const auto bytes = serializeLevelMap(document).bytes;
		const auto depth = document.undoStack.size();
		const auto revision = document.revision, repeatRevision = document.repeat.revision;
		clearLevelMapRepeatCommands(&document);
		ok &= expect(document.repeat.actions.isEmpty() && serializeLevelMap(document).bytes == bytes &&
		                 document.undoStack.size() == depth && document.revision == revision && document.repeat.revision > repeatRevision,
		             "clear changes only transient repeat state");
	}
	{
		auto document = initial;
		pick(&document, 0);
		moveLevelMapSelection(&document, 8, 0, 0, textures, &error);
		moveLevelMapSelection(&document, 0, 8, 0, textures, &error);
		const auto json = levelRepeatCommandsJson(document);
		QVector<LevelRepeatAction> parsed;
		ok &= expect(parseLevelRepeatCommands(json, &parsed, &error) && parsed == document.repeat.actions, "recipe round trip", error);
		auto invalid = json;
		invalid["version"] = 2;
		const auto original = parsed;
		ok &= expect(!parseLevelRepeatCommands(invalid, &parsed, &error) && parsed == original, "invalid recipe leaves caller unchanged");
		LevelPlacementRequest request;
		request.operation = LevelPlacementOperation::Repeat;
		request.textures = textures;
		bool cancel = false;
		LevelPlacementControl control;
		control.isCancelled = [&] { return cancel; };
		control.progress = [&](const auto& progress) {
			cancel |= progress.phase == LevelPlacementPhase::Repeating && progress.completed == 1;
		};
		const auto bytes = serializeLevelMap(document).bytes;
		const auto state = document.repeat;
		const auto cancelled = prepareLevelPlacement(document, request, control);
		ok &= expect(cancelled.cancelled && !cancelled.succeeded && serializeLevelMap(document).bytes == bytes && document.repeat == state,
		             "cancellation after a completed action publishes nothing");
		document.repeat.actions.last().matrix[7] = 1e20;
		const auto failedBefore = document.repeat;
		const auto depth = document.undoStack.size();
		ok &= expect(!repeatLevelMapCommands(&document, textures, &error) && serializeLevelMap(document).bytes == bytes &&
		                 document.repeat == failedBefore && document.undoStack.size() == depth,
		             "late transform failure rolls back the complete repeat", error);
	}
	{
		auto document = initial;
		QString group, copy;
		pick(&document, 0);
		moveLevelMapSelection(&document, 8, 0, 0, textures, &error);
		const auto actions = document.repeat.actions;
		ok &= expect(createLevelSceneNode(&document, LevelSceneNodeKind::Group, "Assembly", {}, &group, &error) &&
		                 assignLevelSceneObjects(&document, group, {"brush:0"}, &error) &&
		                 createLinkedLevelGroup(&document, group, {0, 64, 0, true}, &copy, &error) && document.repeat.actions == actions,
		             "linked-copy internal transforms do not enter repeat history", error);
		pick(&document, 0);
		ok &= expect(setLevelSceneLocked(&document, group, true, &error), "lock repeat target", error);
		const auto bytes = serializeLevelMap(document).bytes;
		ok &= expect(!repeatLevelMapCommands(&document, textures, &error) && serializeLevelMap(document).bytes == bytes,
		             "repeat obeys scene locks", error);
	}
	if (ok) {
		std::cout
		    << "Repeat recording, world transforms, duplication, selection boundaries, undo, recipes, locks and cancellation passed.\n";
	}
	return ok ? 0 : 1;
}
