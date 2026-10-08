#include "core/doom_preview_geometry.h"
#include "core/level_doom_nodes.h"
#include "core/level_doom_selection.h"
#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_udmf.h"
#include "tests/level_udmf_test_helpers.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace f = vibestudio::tests::udmf;
namespace d = vibestudio::tests::doom;
namespace {
bool ok = true;
bool expect(bool value, const char* message, const QString& detail = {}) {
	if (!value) {
		ok = false;
		std::cerr << message << ": " << detail.toStdString() << '\n';
	}
	return value;
}
bool near(double a, double b) { return std::abs(a - b) < 1e-8; }
QByteArray untransformedBytes(const LevelMapDocument& map) {
	auto result = map.doomUdmf->source;
	QVector<QPair<qsizetype, qsizetype>> spans;
	for (const auto& block : map.doomUdmf->blocks) {
		for (const auto& p : block.properties) {
			const bool coordinate = (block.type == "vertex" || block.type == "thing") && (p.name == "x" || p.name == "y");
			const bool thing = block.type == "thing" && (p.name == "height" || p.name == "angle");
			const bool line = block.type == "linedef" && (p.name == "v1" || p.name == "v2");
			if (coordinate || thing || line) {
				spans << qMakePair(p.valueBegin, p.valueEnd);
			}
		}
	}
	for (auto it = spans.crbegin(); it != spans.crend(); ++it) {
		result.replace(it->first, it->second - it->first, "<native-value>");
	}
	return result;
}
double floorArea(const LevelMapDocument& map) {
	const auto geometry = buildDoomPreviewGeometry(map);
	expect(geometry.warnings.isEmpty(), "transformed sector remains closed", geometry.warnings.join(';'));
	double result = 0;
	for (const auto& polygon : geometry.polygons) {
		if (polygon.target.kind != LevelMaterialKind::SectorFloor) {
			continue;
		}
		double area = 0;
		for (qsizetype i = 0; i < polygon.points.size(); ++i) {
			const auto& a = polygon.points[i];
			const auto& b = polygon.points[(i + 1) % polygon.points.size()];
			area += a.x * b.y - a.y * b.x;
		}
		result += std::abs(area) * .5;
	}
	return result;
}
void verifyHistoryAndArchive(const LevelMapDocument& source, LevelMapDocument map, bool geometry) {
	QString error;
	const auto before = serializeLevelMap(source), after = serializeLevelMap(map);
	expect(after.errors.isEmpty() && map.undoStack.size() == source.undoStack.size() + 1, "one atomic source edit", after.errors.join(';'));
	expect(map.selection == source.selection && untransformedBytes(map) == untransformedBytes(source),
		   "selection and every non-transform byte retained");
	expect(inspectLevelDoomNodes(map).state == (geometry ? LevelDoomNodeState::Stale : LevelDoomNodeState::Present),
		   "correct node readiness");
	const auto oldLumps = d::lumps(before.bytes), newLumps = d::lumps(after.bytes);
	expect(oldLumps.size() == newLumps.size(), "WAD directory count retained");
	for (int i = 0; i < oldLumps.size() && i < newLumps.size(); ++i) {
		expect(oldLumps[i].name == newLumps[i].name, "WAD directory order retained");
		if (i == 1) {
			continue;
		}
		if (geometry && i == 3) {
			expect(newLumps[i].bytes.isEmpty(), "selected ZNODES invalidated");
			continue;
		}
		expect(oldLumps[i].bytes == newLumps[i].bytes, "other maps, sidecars, assets, duplicate lumps retained");
	}
	LevelMapDocument reopened;
	expect(loadLevelMapBytes({"saved.wad", "MAP01", {}}, after.bytes, &reopened, &error) &&
			   reopened.doomUdmf->source == map.doomUdmf->source,
		   "reopen exact transformed TEXTMAP", error);
	expect(inspectLevelDoomNodes(reopened).state == (geometry ? LevelDoomNodeState::Missing : LevelDoomNodeState::Present),
		   "persisted node readiness");
	expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == before.bytes &&
			   inspectLevelDoomNodes(map).state == LevelDoomNodeState::Present,
		   "undo restores exact original text and node products", error);
	expect(redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == after.bytes, "redo restores exact transformed archive", error);
}
void verifyThingDuplication(const LevelMapDocument& original) {
	QString error;
	auto source = original;
	selectLevelMapObject(&source, "thing:0");
	const auto before = serializeLevelMap(source).bytes;
	expect(levelMapSelectionIsDuplicable(source), "UDMF things enable common duplicate actions");
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::Duplicate;
	request.offset = {.375, -.125, .25, true};
	request.copies = 3;
	auto result = prepareLevelPlacement(source, request);
	if (expect(result.succeeded, "fractional UDMF thing array", result.error)) {
		auto map = result.document;
		expect(map.doomThings.size() == 4 && map.entities.size() == 4 && map.selection.size() == 3 && map.undoStack.size() == 1,
			"array publishes native records, entity mirrors and selection as one undo step");
		for (int i = 0; i < map.doomThings.size(); ++i) {
			const auto& thing = map.doomThings[i];
			expect(thing.x == 64.25 + i * .375 && thing.y == 64.25 - i * .125 && thing.z == .5 + i * .25 && thing.angle == 90 && thing.type == 1,
				"copies derive fractional XYZ from the source and retain heading and type");
			expect(map.entities[i].origin.x == thing.x && map.entities[i].origin.z == thing.z, "mirrors reflect native fractional placement");
		}
		expect(map.doomUdmf->source.startsWith(source.doomUdmf->source)
			&& map.doomUdmf->source.count("user_note = \"untouched\";") == 4
			&& map.doomUdmf->source.count("extension { version = 9007199254740993;") == 1,
			"original bytes and unknown thing fields retained, global extension blocks not copied");
		expect(inspectLevelDoomNodes(map).state == LevelDoomNodeState::Present, "ordinary thing arrays preserve built nodes");
		const auto after = serializeLevelMap(map);
		const auto oldLumps = d::lumps(before), newLumps = d::lumps(after.bytes);
		expect(after.errors.isEmpty() && oldLumps.size() == newLumps.size(), "array WAD remains serializable", after.errors.join(';'));
		for (int i = 0; i < oldLumps.size() && i < newLumps.size(); ++i) {
			expect(oldLumps[i].name == newLumps[i].name && (i == 1 || oldLumps[i].bytes == newLumps[i].bytes),
				"array preserves other map, duplicate resource, sidecar and node lumps");
		}
		LevelMapDocument reopened;
		expect(loadLevelMapBytes({"array.wad", "MAP01", {}}, after.bytes, &reopened, &error)
			&& reopened.doomThings.size() == 4 && reopened.doomUdmf->source == map.doomUdmf->source, "array save/reopen exact", error);
		expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == before && map.selection == source.selection,
			"array undo restores exact bytes and source selection", error);
		expect(redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == after.bytes && map.selection == result.document.selection,
			"array redo restores exact bytes and copy selection", error);
	}
	auto direct = source;
	expect(duplicateLevelMapObjects(&direct, {{LevelMapSelectionKind::DoomThing, 0}, {LevelMapSelectionKind::Entity, 0}}, 0, 0, 0, &error)
		&& direct.doomThings.size() == 2 && direct.doomUdmf->source.count("x = 64.25; y = 64.25; height = 0.5;") == 2,
		"overlapping thing and entity selectors copy once and zero offset preserves lexical content", error);
	auto grouped = source;
	QString group;
	expect(createLevelSceneNode(&grouped, LevelSceneNodeKind::Group, "Thing array", {}, &group, &error)
		&& assignLevelSceneObjects(&grouped, group, {"thing:0"}, &error), "array scene setup", error);
	const auto sceneBefore = grouped.scene;
	auto groupedResult = prepareLevelPlacement(grouped, request);
	if (expect(groupedResult.succeeded, "array copies inherit source scene group", groupedResult.error)) {
		auto map = groupedResult.document;
		expect(levelSceneNode(map.scene, group)->objects.size() == 4 && validateLevelScene(map, map.scene, &error), "array scene metadata valid", error);
		expect(undoLevelMapEdit(&map, &error) && map.scene == sceneBefore && map.selection == grouped.selection, "array scene undo exact", error);
		expect(redoLevelMapEdit(&map, &error) && map.scene == groupedResult.document.scene, "array scene redo exact", error);
	}
	expect(setLevelSceneLocked(&grouped, group, true, &error), "lock array source", error);
	const auto lockedBefore = serializeLevelMap(grouped).bytes;
	auto denied = prepareLevelPlacement(grouped, request);
	expect(!denied.succeeded && denied.error.contains("Unlock") && serializeLevelMap(grouped).bytes == lockedBefore,
		"array cannot add members to a protected source group", denied.error);
	auto optionalText = f::textmap();
	optionalText.replace("height = 0.5; ", "/* keep block comment */ ");
	LevelMapDocument optional;
	expect(loadLevelMapBytes({"optional.wad", "MAP01", {}}, f::fixture(optionalText), &optional, &error), "copy optional height fixture", error);
	selectLevelMapObject(&optional, "thing:0");
	expect(duplicateLevelMapSelection(&optional, .5, 0, .125, &error) && optional.doomThings.last().z == .125
		&& optional.doomUdmf->source.startsWith(optionalText) && optional.doomUdmf->source.count("/* keep block comment */") == 2,
		"copy inserts missing height while retaining comments", error);
	for (int type : {3000, 9303}) {
		auto polyText = f::textmap();
		polyText.replace("type = 1; angle = 90;", QStringLiteral("type = %1; angle = 721;").arg(type).toUtf8());
		LevelMapDocument poly;
		expect(loadLevelMapBytes({"poly.wad", "MAP01", {}}, f::fixture(polyText), &poly, &error), "polyobject copy fixture", error);
		selectLevelMapObject(&poly, "thing:0");
		expect(duplicateLevelMapSelection(&poly, 0, 0, 0, &error) && poly.doomGeometryChanged && poly.doomThings.last().angle == 721,
			"adding polyobject controls invalidates nodes even at zero offset and preserves the polyobject ID", error);
		expect(undoLevelMapEdit(&poly, &error) && !poly.doomGeometryChanged, "polyobject copy undo restores nodes", error);
	}
	for (auto phase : {LevelPlacementPhase::Preparing, LevelPlacementPhase::Arraying, LevelPlacementPhase::Finalizing, LevelPlacementPhase::Complete}) {
		bool stopped = false;
		LevelPlacementControl control;
		control.progress = [&](const auto& p) { stopped |= p.phase == phase; };
		control.isCancelled = [&] { return stopped; };
		const auto cancelled = prepareLevelPlacement(source, request, control);
		expect(stopped && cancelled.cancelled && !cancelled.succeeded && cancelled.document.format == LevelMapFormat::Unknown
			&& serializeLevelMap(source).bytes == before, "array cancellation never publishes partial copies", cancelled.error);
	}
	request.offset = {4000000, 0, 0, true}; // Two fit, the third would exceed the native range.
	const auto overflow = prepareLevelPlacement(source, request);
	expect(!overflow.succeeded && serializeLevelMap(source).bytes == before && source.undoStack.isEmpty(), "late array overflow is atomic", overflow.error);
	auto ambiguousText = f::textmap();
	ambiguousText.replace("thing { x = 64.25;", "thing { x = 1; x = 64.25;");
	LevelMapDocument ambiguous;
	expect(loadLevelMapBytes({"ambiguous.wad", "MAP01", {}}, f::fixture(ambiguousText), &ambiguous, &error), "ambiguous thing fixture", error);
	selectLevelMapObject(&ambiguous, "thing:0");
	expect(!duplicateLevelMapSelection(&ambiguous, 1, 0, 0, &error) && error.contains("ambiguous")
		&& ambiguous.doomUdmf->source == ambiguousText && ambiguous.undoStack.isEmpty(), "ambiguous changed coordinate refuses the entire copy", error);
	auto manyText = f::textmap() + '\n';
	for (int i = 0; i < 64; ++i) { manyText += "thing { x = 1; y = 2; type = 1; }\n"; }
	LevelMapDocument many;
	expect(loadLevelMapBytes({"many.wad", "MAP01", {}}, f::fixture(manyText), &many, &error), "record budget fixture", error);
	QVector<LevelMapSelectionRef> manySelected;
	for (const auto& thing : many.doomThings) { manySelected << LevelMapSelectionRef{LevelMapSelectionKind::DoomThing, thing.id}; }
	setLevelMapSelection(&many, manySelected);
	request.offset = {1, 0, 0, true}; request.copies = 256;
	const auto tooMany = prepareLevelPlacement(many, request);
	expect(!tooMany.succeeded && tooMany.error.contains("32768") && many.doomUdmf->source == manyText && many.undoStack.isEmpty(),
		"UDMF arrays include entity mirrors in the bounded native-record budget", tooMany.error);
	// An oversized selection must remain cancellable while it is validated,
	// before the copy-count budget is reached. This also exercises indexed
	// identities over a large source rather than repeated full-map searches.
	auto largeText = f::textmap() + '\n';
	for (int i = 0; i < 12000; ++i) { largeText += "thing { x = 1; y = 2; type = 1; }\n"; }
	LevelMapDocument large;
	expect(loadLevelMapBytes({"large-things.wad", "MAP01", {}}, f::fixture(largeText), &large, &error), "large thing selection fixture", error);
	QVector<LevelMapSelectionRef> largeSelected;
	for (const auto& thing : large.doomThings) { largeSelected << LevelMapSelectionRef{LevelMapSelectionKind::DoomThing, thing.id}; }
	setLevelMapSelection(&large, largeSelected);
	for (int cancelAfter : {200, 12500}) {
		int checks = 0;
		LevelPlacementControl duringValidation;
		duringValidation.isCancelled = [&] { return ++checks > cancelAfter; };
		QElapsedTimer validationTimer;
		validationTimer.start();
		const auto validationCancelled = prepareLevelPlacement(large, request, duringValidation);
		expect(validationCancelled.cancelled && !validationCancelled.succeeded && checks > cancelAfter && validationTimer.elapsed() < 3000
			&& validationCancelled.document.format == LevelMapFormat::Unknown && large.doomUdmf->source == largeText
			&& large.undoStack.isEmpty() && large.selection == largeSelected,
			"large thing selection cancels during identity indexing or selection validation without publishing any state", validationCancelled.error);
	}
	auto mixed = source;
	setLevelMapSelection(&mixed, {{LevelMapSelectionKind::DoomThing, 0}, {LevelMapSelectionKind::DoomSector, 0}});
	expect(!levelMapSelectionIsDuplicable(mixed) && !duplicateLevelMapSelection(&mixed, 1, 0, 0, &error)
		&& serializeLevelMap(mixed).bytes == before, "mixed UDMF geometry selection cannot partially duplicate", error);
}
} // namespace
int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	QString error;
	LevelMapDocument source;
	if (!expect(loadLevelMapBytes({"source.wad", "MAP01", {}}, f::fixture(), &source, &error), "load generated UDMF", error)) {
		return 1;
	}
	verifyThingDuplication(source);
	// Overlapping selections must move each shared boundary vertex exactly once.
	expect(setLevelMapSelection(
			   &source,
			   {{LevelMapSelectionKind::DoomSector, 0}, {LevelMapSelectionKind::DoomLinedef, 0}, {LevelMapSelectionKind::DoomVertex, 0}},
			   &error),
		   "select shared geometry", error);
	const auto original = serializeLevelMap(source).bytes;
	std::cerr << "UDMF move/history\n";
	LevelPlacementRequest move;
	move.operation = LevelPlacementOperation::Move;
	move.offset = {.375, -.125, 0, true};
	auto moved = prepareLevelPlacement(source, move);
	if (expect(moved.succeeded, "fractional worker move", moved.error)) {
		for (int i = 0; i < 4; ++i) {
			expect(moved.document.doomVertices[i].x == source.doomVertices[i].x + .375 &&
					   moved.document.doomVertices[i].y == source.doomVertices[i].y - .125,
				   "shared vertices translated once without rounding");
		}
		expect(near(floorArea(moved.document), 65536), "translation preserves independent room area");
		verifyHistoryAndArchive(source, moved.document, true);
	}
	LevelPlacementRequest rotate;
	std::cerr << "UDMF rotation/history\n";
	rotate.operation = LevelPlacementOperation::Rotate;
	rotate.rotation = {2, 22.5, {0, 0, 0, true}, true, false};
	auto rotated = prepareLevelPlacement(source, rotate);
	if (expect(rotated.succeeded, "arbitrary UDMF rotation", rotated.error)) {
		constexpr double radians = 22.5 * 3.14159265358979323846 / 180;
		for (int i = 0; i < 4; ++i) {
			const auto& a = source.doomVertices[i];
			const auto& b = rotated.document.doomVertices[i];
			expect(near(b.x, a.x * std::cos(radians) - a.y * std::sin(radians)) &&
					   near(b.y, a.x * std::sin(radians) + a.y * std::cos(radians)),
				   "rotation matches independent affine oracle");
		}
		expect(near(floorArea(rotated.document), 65536), "rotation preserves room area");
		verifyHistoryAndArchive(source, rotated.document, true);
	}
	for (int axis : {0, 1}) {
		std::cerr << "UDMF mirror/history " << axis << '\n';
		auto map = source;
		expect(flipLevelMapSelection(&map, axis, &error), "UDMF mirror", error);
		for (int i = 0; i < 4; ++i) {
			expect(map.doomVertices[i].x == (axis == 0 ? 256.5 - source.doomVertices[i].x : source.doomVertices[i].x) &&
					   map.doomVertices[i].y == (axis == 1 ? 256.5 - source.doomVertices[i].y : source.doomVertices[i].y),
				   "fractional mirror pivot");
			expect(map.doomLinedefs[i].startVertex == source.doomLinedefs[i].endVertex &&
					   map.doomLinedefs[i].endVertex == source.doomLinedefs[i].startVertex && map.doomLinedefs[i].frontSidedef == i,
				   "mirror restores line winding and retains side ownership");
		}
		expect(near(floorArea(map), 65536), "mirror keeps closed floor facing correctly");
		verifyHistoryAndArchive(source, map, true);
	}
	auto distant = source;
	expect(moveLevelMapSelection(&distant, 100000, 0, 0, &error), "large-coordinate UDMF placement", error);
	const auto distantBytes = serializeLevelMap(distant).bytes;
	auto distantRotation = rotate;
	distantRotation.rotation.pivot = {100128.25, 128.25, 0, true};
	auto distantResult = prepareLevelPlacement(distant, distantRotation);
	expect(distantResult.succeeded && distantResult.document.doomVertices[0].x > 99000, "custom pivot uses the UDMF coordinate range",
		   distantResult.error);
	distantRotation.rotation.pivot.x = 1e7 + 1;
	expect(!prepareLevelPlacement(distant, distantRotation).succeeded && serializeLevelMap(distant).bytes == distantBytes,
		   "out-of-range UDMF pivot refused atomically");
	auto partial = source;
	std::cerr << "UDMF connected selection\n";
	selectLevelMapObject(&partial, "linedef:0");
	const auto partialSelection = partial.selection;
	expect(!flipLevelMapSelection(&partial, 0, &error) && error.contains("Connected Geometry") && partial.undoStack.isEmpty() &&
			   partial.selection == partialSelection && serializeLevelMap(partial).bytes == original,
		   "partial mirror fails atomically", error);
	LevelPlacementRequest connected;
	connected.operation = LevelPlacementOperation::Mirror;
	connected.axis = 1;
	connected.connectedGeometry = true;
	auto expanded = prepareLevelPlacement(partial, connected);
	expect(expanded.succeeded && expanded.document.selection.size() == 4 && near(floorArea(expanded.document), 65536),
		   "connected expansion and mirror share worker transaction", expanded.error);
	LevelPlacementRequest snap;
	std::cerr << "UDMF snap/history\n";
	snap.operation = LevelPlacementOperation::Snap;
	snap.grid = .5;
	auto snapped = prepareLevelPlacement(source, snap);
	if (expect(snapped.succeeded, "fractional UDMF grid", snapped.error)) {
		expect(snapped.document.doomVertices[0].x == .5 && snapped.document.doomVertices[0].y == .5, "snap keeps fractional grid");
		verifyHistoryAndArchive(source, snapped.document, true);
	}
	LevelPlacementRequest resize;
	std::cerr << "UDMF resize/history\n";
	resize.operation = LevelPlacementOperation::Resize;
	resize.mins = {-2.125, 4.375, 0, true};
	resize.maxs = {125.875, 388.375, 0, true};
	auto resized = prepareLevelPlacement(source, resize);
	if (expect(resized.succeeded, "UDMF boundary resize", resized.error)) {
		expect(resized.document.doomVertices[0].x == -2.125 && resized.document.doomVertices[2].y == 388.375 &&
				   near(floorArea(resized.document), 128 * 384),
			   "resize has exact requested bounds and independent area");
		verifyHistoryAndArchive(source, resized.document, true);
	}
	auto things = source;
	std::cerr << "UDMF things\n";
	selectLevelMapObject(&things, "thing:0");
	auto thingMove = move;
	thingMove.offset.z = 1.125;
	auto elevated = prepareLevelPlacement(things, thingMove);
	if (expect(elevated.succeeded, "UDMF thing height move", elevated.error)) {
		expect(elevated.document.doomThings[0].z == 1.625 && elevated.document.entities[0].origin.z == 1.625 &&
				   elevated.document.entities[0].origin.x == 64.625,
			   "fractional thing and entity mirror agree");
		verifyHistoryAndArchive(things, elevated.document, false);
	}
	auto turnedThing = prepareLevelPlacement(things, rotate);
	if (expect(turnedThing.succeeded, "thing arbitrary rotation", turnedThing.error)) {
		expect(turnedThing.document.doomThings[0].angle == 113 && turnedThing.document.doomThings[0].z == .5,
			   "whole-degree heading and unchanged height");
		verifyHistoryAndArchive(things, turnedThing.document, false);
	}
	for (qint64 angle : {qint64(std::numeric_limits<int>::min()), qint64(std::numeric_limits<int>::max())}) {
		auto extreme = things;
		expect(editLevelMapUdmfProperties(&extreme, {{"thing:0", "angle", QString::number(angle)}}, &error), "large heading fixture",
			   error);
		auto turned = extreme;
		expect(rotateLevelMapSelection(&turned, 2, 1, &error) && turned.doomThings[0].angle == ((angle + 90) % 360 + 360) % 360,
			   "heading normalizes before integer narrowing", error);
		auto mirrored = extreme;
		expect(flipLevelMapSelection(&mirrored, 0, &error) && mirrored.doomThings[0].angle == ((180 - angle) % 360 + 360) % 360,
			   "extreme reflected heading avoids overflow", error);
	}
	auto snappedThing = prepareLevelPlacement(things, snap);
	// ZDBSP reads these extended-namespace thing angles as identifiers, not yaw.
	// Only XY changes affect its polyobject node inputs; height is independent.
	for (int type : {3000, 3001, 3002, 9300, 9301, 9302, 9303}) {
		auto text = f::textmap();
		text.replace("type = 1;", QByteArray("type = ") + QByteArray::number(type) + ';');
		text.replace("angle = 90;", "angle = 721;");
		LevelMapDocument poly;
		expect(loadLevelMapBytes({"poly.wad", "MAP01", {}}, f::fixture(text), &poly, &error), "polyobject control fixture", error);
		selectLevelMapObject(&poly, "thing:0");
		auto polyTurn = prepareLevelPlacement(poly, rotate);
		if (expect(polyTurn.succeeded, "polyobject control transform", polyTurn.error)) {
			expect(polyTurn.document.doomThings[0].angle == 721 && polyTurn.document.doomGeometryChanged,
				   "polyobject identifiers retained and node inputs invalidated");
			verifyHistoryAndArchive(poly, polyTurn.document, true);
		}
		auto heightOnly = poly;
		expect(moveLevelMapSelection(&heightOnly, 0, 0, .125, &error) && !heightOnly.doomGeometryChanged &&
				   heightOnly.doomThings[0].angle == 721,
			   "polyobject height alone retains identifier and nodes", error);
	}
	expect(snappedThing.succeeded && snappedThing.document.doomThings[0].x == 64.5 && snappedThing.document.doomThings[0].z == .5 &&
			   !snappedThing.document.doomGeometryChanged,
		   "thing snapping is XY and retains nodes", snappedThing.error);
	// A no-op around one vertex must not promise a history entry or publish a candidate.
	auto point = source;
	std::cerr << "UDMF no-op and validation\n";
	selectLevelMapObject(&point, "vertex:0");
	auto noOp = rotate;
	noOp.rotation.pivot = {.25, .25, 0, true};
	auto unchanged = prepareLevelPlacement(point, noOp);
	expect(!unchanged.succeeded && unchanged.error.contains("unchanged") && point.undoStack.isEmpty() && serializeLevelMap(point).bytes == original,
		   "no-op transform leaves source spelling and history exact", unchanged.error);
	// Add a missing optional height only when a thing's elevation actually changes.
	auto noHeightText = f::textmap();
	noHeightText.replace("height = 0.5; ", "");
	LevelMapDocument noHeight;
	expect(loadLevelMapBytes({"optional.wad", "MAP01", {}}, f::fixture(noHeightText), &noHeight, &error), "optional height fixture", error);
	selectLevelMapObject(&noHeight, "thing:0");
	expect(moveLevelMapSelection(&noHeight, 0, 0, .125, &error) && noHeight.doomThings[0].z == .125 &&
			   noHeight.doomUdmf->source.contains("height = 0.125;\r\n"),
		   "missing optional height inserted losslessly", error);
	expect(undoLevelMapEdit(&noHeight, &error) && noHeight.doomUdmf->source == noHeightText, "optional insertion undo exact", error);
	for (const auto& delta :
		 QVector<LevelMapVec3>{{0, 0, 1, true}, {1e8, 0, 0, true}, {std::numeric_limits<double>::infinity(), 0, 0, true}}) {
		auto invalid = source;
		expect(!moveLevelMapSelection(&invalid, delta.x, delta.y, delta.z, &error) && !error.isEmpty() && invalid.undoStack.isEmpty() &&
				   serializeLevelMap(invalid).bytes == original,
			   "invalid geometry move never partially commits", error);
	}
	auto collapsed = source;
	selectLevelMapObject(&collapsed, "vertex:0");
	expect(!moveLevelMapSelection(&collapsed, 0, 256, 0, &error) && error.contains("collapse") &&
			   serializeLevelMap(collapsed).bytes == original,
		   "collapsed line refused before source publication", error);
	auto locked = source;
	std::cerr << "UDMF locks\n";
	QString group;
	expect(createLevelSceneNode(&locked, LevelSceneNodeKind::Group, "Protected boundary", {}, &group, &error) &&
			   assignLevelSceneObjects(&locked, group, {"linedef:2"}, &error) && setLevelSceneLocked(&locked, group, true, &error),
		   "scene lock fixture", error);
	const auto lockedBytes = serializeLevelMap(locked).bytes;
	auto freeThing = locked;
	selectLevelMapObject(&freeThing, "thing:0");
	auto freeMove = prepareLevelPlacement(freeThing, thingMove);
	expect(freeMove.succeeded && freeMove.document.doomThings[0].z == 1.625, "unlocked objects remain editable beside locked geometry",
		   freeMove.error);
	auto freeProperty = locked;
	expect(editLevelMapUdmfProperties(&freeProperty, {{"thing:0", "height", "3.125"}}, &error) && freeProperty.doomThings[0].z == 3.125,
		   "raw property transaction retains scene guard identity", error);
	for (const auto& request : {move, rotate, resize, snap, connected}) {
		auto refused = prepareLevelPlacement(locked, request);
		expect(!refused.succeeded && !refused.cancelled && refused.error.contains("Unlock") &&
				   serializeLevelMap(locked).bytes == lockedBytes,
			   "every native transform respects indirect scene locks", refused.error);
	}
	// Exceeds the manual 4096-property batch cap: geometry operations use the document budget.
	auto largeText = f::textmap() + '\n';
	std::cerr << "UDMF large selection\n";
	for (int i = 0; i < 3000; ++i) {
		largeText += QStringLiteral("vertex { x = %1.125; y = 2.375; user_keep = true; }\n").arg(i).toUtf8();
	}
	LevelMapDocument large;
	expect(loadLevelMapBytes({"large.wad", "MAP01", {}}, f::fixture(largeText), &large, &error), "large fixture", error);
	QVector<LevelMapSelectionRef> selected;
	for (const auto& vertex : large.doomVertices) {
		selected << LevelMapSelectionRef{LevelMapSelectionKind::DoomVertex, vertex.id};
	}
	setLevelMapSelection(&large, selected);
	QElapsedTimer elapsed;
	elapsed.start();
	auto largeMove = prepareLevelPlacement(large, move);
	expect(largeMove.succeeded && largeMove.document.doomVertices.last().x == 2999.5 && largeMove.document.undoStack.size() == 1,
		   "6008 native property changes commit once", largeMove.error);
	std::cout << "3,004-vertex lossless transform: " << elapsed.elapsed() << " ms\n";
	for (auto phase : {LevelPlacementPhase::Preparing, LevelPlacementPhase::Transforming, LevelPlacementPhase::Finalizing,
					   LevelPlacementPhase::Complete}) {
		bool stopped = false;
		LevelPlacementControl control;
		control.progress = [&](const auto& p) { stopped = stopped || p.phase == phase; };
		control.isCancelled = [&] { return stopped; };
		auto cancelled = prepareLevelPlacement(large, move, control);
		expect(stopped && cancelled.cancelled && !cancelled.succeeded && cancelled.document.format == LevelMapFormat::Unknown &&
				   large.undoStack.isEmpty() && large.doomUdmf->source == largeText,
			   "cancellation at every publication phase is atomic", cancelled.error);
	}
	bool writing = false;
	int checks = 0;
	LevelPlacementControl insideWriter;
	insideWriter.progress = [&](const auto& p) { writing = writing || p.phase == LevelPlacementPhase::Finalizing; };
	insideWriter.isCancelled = [&] { return writing && ++checks > 500; };
	elapsed.restart();
	auto cancelled = prepareLevelPlacement(large, move, insideWriter);
	expect(cancelled.cancelled && checks > 500 && elapsed.elapsed() < 3000 && large.doomUdmf->source == largeText,
		   "bounded cancellation inside lossless encoding", cancelled.error);
	expect(serializeLevelMap(source).bytes == original && source.undoStack.isEmpty(), "all preparations leave source immutable");
	return ok ? 0 : 1;
}
