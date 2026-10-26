// NetRadiant Custom's CSG Tool in the core and on the command line: the
// Diagonal and Extrude hollows (mitred walls that fill the shell exactly,
// straight slabs), the wrap and pull names, Keep Inner Brush with its caulk
// rule, the face policies (picked, only picked, facing an axis), Shrink and
// Expand as one exact undo step each, their refusals, scene locks and hidden
// brushes, a reload, and `map hollow --mode diagonal|extrude --keep-inner
// --exclude-face --only-face --exclude-facing`, `map shrink-brushes` and
// `map expand-brushes`.

#include "core/level_document.h"
#include "core/level_hollow.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_visibility.h"
#include "core/map_geometry.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>

using namespace vibestudio;

namespace {

int failures = 0;

bool expect(bool condition, const char* message, const QString& detail = {})
{
	if (!condition) {
		++failures;
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty()) {
			std::cerr << " (" << qPrintable(detail) << ")";
		}
		std::cerr << "\n";
	}
	return condition;
}

bool near(double a, double b, double tolerance = 1e-3) { return std::abs(a - b) <= tolerance; }

QByteArray bytesOf(const LevelMapDocument& document) { return serializeLevelMap(document).bytes; }

LevelMapDocument reloaded(const LevelMapDocument& document)
{
	LevelMapDocument loaded;
	QString error;
	loadLevelMapBytes({QStringLiteral("csg.map"), {}, document.format == LevelMapFormat::Quake3Map ? QStringLiteral("idtech3") : QString()},
		bytesOf(document), &loaded, &error);
	return loaded;
}

LevelMapDocument mapWith(const QString& game, const QVector<std::pair<LevelMapVec3, LevelMapVec3>>& boxes, const QString& texture)
{
	LevelMapDocument document;
	LevelMapCreateRequest create;
	create.game = game;
	create.starterRoom = false;
	QString error;
	createLevelMap(create, &document, &error);
	for (const auto& [mins, maxs] : boxes) {
		addLevelMapBoxBrush(&document, mins, maxs, texture, nullptr, &error);
	}
	return reloaded(document);
}

LevelMapDocument cube(const QString& game = QStringLiteral("quake3"))
{
	return mapWith(game, {{{-64, -64, -64, true}, {64, 64, 64, true}}}, QStringLiteral("studio/wall"));
}

void selectBrushes(LevelMapDocument* document, const QVector<int>& ids)
{
	QVector<LevelMapSelectionRef> selection;
	for (const int id : ids) {
		selection.append({LevelMapSelectionKind::QuakeBrush, id});
	}
	setLevelMapSelection(document, selection);
}

// The zero-based face of `brush` whose outward normal is `axis` (0..2) with
// `sign`.
int faceAlong(const LevelMapBrush& brush, int axis, int sign)
{
	const MapBrushGeometry geometry = solveBrushGeometry(brush.faces, brush.id, brush.entityId);
	for (int face = 0; face < geometry.faces.size(); ++face) {
		const MapPlane& plane = geometry.faces.at(face).plane;
		const double along = axis == 0 ? plane.normalX : (axis == 1 ? plane.normalY : plane.normalZ);
		if (geometry.faces.at(face).isValid() && near(along, sign)) {
			return face;
		}
	}
	return -1;
}

// The volume of a solved convex brush: a pyramid from its first corner to
// each face.
double volumeOf(const LevelMapBrush& brush)
{
	const MapBrushGeometry geometry = solveBrushGeometry(brush.faces, brush.id, brush.entityId);
	if (!geometry.solved) {
		return -1;
	}
	LevelMapVec3 origin;
	for (const MapFacePolygon& polygon : geometry.faces) {
		if (polygon.isValid()) {
			origin = polygon.points.first();
			break;
		}
	}
	double volume = 0.0;
	for (const MapFacePolygon& polygon : geometry.faces) {
		if (!polygon.isValid()) {
			continue;
		}
		double ax = 0, ay = 0, az = 0;
		const LevelMapVec3& first = polygon.points.first();
		for (int index = 1; index + 1 < polygon.points.size(); ++index) {
			const LevelMapVec3& b = polygon.points.at(index);
			const LevelMapVec3& c = polygon.points.at(index + 1);
			const double ux = b.x - first.x, uy = b.y - first.y, uz = b.z - first.z;
			const double vx = c.x - first.x, vy = c.y - first.y, vz = c.z - first.z;
			ax += uy * vz - uz * vy;
			ay += uz * vx - ux * vz;
			az += ux * vy - uy * vx;
		}
		const double area = std::sqrt(ax * ax + ay * ay + az * az) / 2.0;
		const double height = polygon.plane.normalX * (first.x - origin.x) + polygon.plane.normalY * (first.y - origin.y)
			+ polygon.plane.normalZ * (first.z - origin.z);
		volume += area * height / 3.0;
	}
	return volume;
}

double totalVolume(const LevelMapDocument& document)
{
	double volume = 0.0;
	for (const LevelMapBrush& brush : document.brushes) {
		volume += volumeOf(brush);
	}
	return volume;
}

// How many faces of `brush` point back at the origin (the room), lie on the
// outside of a box `outer` units from the origin, or neither.
struct FaceRoles {
	int inner = 0;
	int outer = 0;
	int diagonal = 0;
	int other = 0;
	QStringList innerTextures;
	QStringList outerTextures;
	QStringList diagonalTextures;
};

FaceRoles rolesOf(const LevelMapBrush& brush, double outer)
{
	FaceRoles roles;
	const MapBrushGeometry geometry = solveBrushGeometry(brush.faces, brush.id, brush.entityId);
	for (int face = 0; face < geometry.faces.size() && face < brush.faces.size(); ++face) {
		const MapFacePolygon& polygon = geometry.faces.at(face);
		if (!polygon.isValid()) {
			continue;
		}
		const MapPlane& plane = polygon.plane;
		const int axes = (std::abs(plane.normalX) > 1e-6) + (std::abs(plane.normalY) > 1e-6) + (std::abs(plane.normalZ) > 1e-6);
		const QString& texture = brush.faces.at(face).textureName;
		if (axes == 2) {
			++roles.diagonal;
			roles.diagonalTextures << texture;
		} else if (near(plane.distance, outer)) {
			++roles.outer;
			roles.outerTextures << texture;
		} else if (plane.distance < 0.0 || near(plane.distance, -64)) {
			++roles.inner;
			roles.innerTextures << texture;
		} else {
			++roles.other;
		}
	}
	return roles;
}

bool allAre(const QStringList& textures, const QString& texture)
{
	return std::all_of(textures.cbegin(), textures.cend(), [&texture](const QString& name) { return name == texture; });
}

void checkModeIds()
{
	LevelMapHollowMode mode = LevelMapHollowMode::Overlap;
	expect(parseLevelMapHollowMode(QStringLiteral("Diagonal"), &mode) && mode == LevelMapHollowMode::Diagonal
			&& levelMapHollowModeId(mode) == QLatin1String("diagonal"),
		"diagonal parses and prints");
	expect(parseLevelMapHollowMode(QStringLiteral("extrude"), &mode) && mode == LevelMapHollowMode::Extrude
			&& levelMapHollowModeId(mode) == QLatin1String("extrude"),
		"extrude parses and prints");
	expect(parseLevelMapHollowMode(QStringLiteral("wrap"), &mode) && mode == LevelMapHollowMode::Touch, "wrap is NetRadiant Custom's touch");
	expect(parseLevelMapHollowMode(QStringLiteral("pull"), &mode) && mode == LevelMapHollowMode::Room, "pull is NetRadiant Custom's room");
	expect(!parseLevelMapHollowMode(QStringLiteral("diag"), &mode) && mode == LevelMapHollowMode::Room, "an unknown mode changes nothing");
	for (const LevelCsgFacePolicy policy : {LevelCsgFacePolicy::ExcludePicked, LevelCsgFacePolicy::OnlyPicked, LevelCsgFacePolicy::ExcludeFacingView,
			 LevelCsgFacePolicy::ExcludeFacingCamera}) {
		LevelCsgFacePolicy parsed = LevelCsgFacePolicy::ExcludePicked;
		expect(parseLevelCsgFacePolicy(levelCsgFacePolicyId(policy), &parsed) && parsed == policy, "face policy ids round trip",
			levelCsgFacePolicyId(policy));
	}
	expect(!parseLevelCsgFacePolicy(QStringLiteral("-faces"), nullptr), "an unknown face policy is refused");
	const LevelCsgToolPreferences defaults;
	expect(defaults.thickness == 16.0 && defaults.facePolicy == LevelCsgFacePolicy::ExcludePicked && defaults.caulk && !defaults.keepInner
			&& levelCsgToolPreferencesProblem(defaults).isEmpty(),
		"the CSG Tool starts as NetRadiant Custom's: 16 units, picked faces excluded, caulk, inner brush removed");
	expect(!levelCsgToolPreferencesProblem({0.0, LevelCsgFacePolicy::ExcludePicked, true, false}).isEmpty()
			&& !levelCsgToolPreferencesProblem({5000.0, LevelCsgFacePolicy::ExcludePicked, true, false}).isEmpty(),
		"thicknesses out of range cannot be saved");
}

void checkDiagonalAndExtrude()
{
	QString error;
	// Diagonal: six frustums that fill the 16-unit shell exactly.
	LevelMapDocument document = cube();
	const int id = document.brushes.first().id;
	const QByteArray original = bytesOf(document);
	selectBrushes(&document, {id});
	LevelMapHollowReport report;
	if (expect(hollowLevelMapSelection(&document, 16, LevelMapHollowMode::Diagonal, {}, &report, &error), "hollow with diagonal joints", error)) {
		expect(report.hollowed == 1 && report.walls == 6 && document.brushes.size() == 6 && document.undoStack.size() == 1,
			"six diagonal walls in one undo step", QString::number(report.walls));
		const double shell = 160.0 * 160.0 * 160.0 - 128.0 * 128.0 * 128.0;
		expect(near(totalVolume(document), shell, 0.5), "the walls fill the shell with no overlap or gap",
			QString::number(totalVolume(document)));
		bool mitred = true;
		bool inside = true;
		for (const LevelMapBrush& wall : document.brushes) {
			const FaceRoles roles = rolesOf(wall, 80);
			mitred = mitred && roles.inner == 1 && roles.outer == 1 && roles.diagonal == 4 && roles.other == 0;
			inside = inside && wall.mins.x >= -80.001 && wall.maxs.x <= 80.001 && wall.mins.z >= -80.001 && wall.maxs.z <= 80.001;
			expect(near(volumeOf(wall), shell / 6.0, 0.5), "each wall is the same frustum", QString::number(volumeOf(wall)));
		}
		expect(mitred, "each wall has one room face, one outside face and four mitred joints");
		expect(inside, "the walls stay inside the grown box");
		expect(document.selection.size() == 6, "the walls are selected");
		const QByteArray after = bytesOf(document);
		const LevelMapDocument back = reloaded(document);
		expect(back.brushes.size() == 6 && near(totalVolume(back), shell, 0.5), "the diagonal walls survive a reload");
		expect(undoLevelMapEdit(&document, &error) && bytesOf(document) == original, "undo brings the brush back exactly", error);
		expect(redoLevelMapEdit(&document, &error) && bytesOf(document) == after, "redo rebuilds the same walls", error);
	}

	// Extrude: six slabs straight out from the faces, the edges left open.
	document = cube();
	selectBrushes(&document, {id});
	if (expect(hollowLevelMapSelection(&document, 16, LevelMapHollowMode::Extrude, {}, &report, &error), "hollow by extruding faces", error)) {
		expect(report.walls == 6 && document.brushes.size() == 6, "six slabs", QString::number(report.walls));
		bool square = true;
		for (const LevelMapBrush& wall : document.brushes) {
			const FaceRoles roles = rolesOf(wall, 80);
			square = square && roles.diagonal == 0 && near(volumeOf(wall), 128.0 * 128.0 * 16.0, 0.5);
		}
		expect(square, "each slab is the face pushed straight out, its sides square to it");
		expect(near(totalVolume(document), 6.0 * 128.0 * 128.0 * 16.0, 1.0), "the slabs leave the edges open");
		expect(undoLevelMapEdit(&document, &error) && bytesOf(document) == original, "undo restores the brush", error);
	}

	// The NetRadiant Custom names give the same walls.
	for (const auto& [name, mode] : {std::pair {QStringLiteral("wrap"), LevelMapHollowMode::Touch}, std::pair {QStringLiteral("pull"), LevelMapHollowMode::Room}}) {
		LevelMapHollowMode parsed = LevelMapHollowMode::Overlap;
		LevelMapDocument byName = cube();
		LevelMapDocument byMode = cube();
		selectBrushes(&byName, {id});
		selectBrushes(&byMode, {id});
		expect(parseLevelMapHollowMode(name, &parsed) && hollowLevelMapSelection(&byName, 16, parsed, {}, nullptr, &error)
				&& hollowLevelMapSelection(&byMode, 16, mode, {}, nullptr, &error) && bytesOf(byName) == bytesOf(byMode),
			"NetRadiant Custom's names build the same walls", name);
	}
}

void checkKeepInner()
{
	QString error;
	const QString caulk = QStringLiteral("common/caulk");
	// Wrap with the inner brush kept: the core keeps the brush's number,
	// and caulk covers the faces between it and the walls.
	LevelMapDocument document = cube();
	const int id = document.brushes.first().id;
	const QByteArray original = bytesOf(document);
	selectBrushes(&document, {id});
	LevelMapHollowOptions options;
	options.keepInner = true;
	options.caulkTexture = caulk;
	LevelMapHollowReport report;
	if (expect(hollowLevelMapSelection(&document, 16, LevelMapHollowMode::Touch, options, &report, &error), "wrap keeping the inner brush", error)) {
		expect(report.walls == 6 && report.innerKept == 1 && document.brushes.size() == 7 && document.undoStack.size() == 1,
			"six walls and the inner brush in one step", QString::number(document.brushes.size()));
		const LevelMapBrush* core = nullptr;
		for (const LevelMapBrush& brush : document.brushes) {
			if (brush.id == id) {
				core = &brush;
			}
		}
		if (expect(core, "the inner brush keeps the brush's number")) {
			expect(near(core->mins.x, -48) && near(core->maxs.z, 48), "the inner brush is what the walls leave");
			expect(std::all_of(core->faces.cbegin(), core->faces.cend(), [&caulk](const LevelMapBrushFace& face) { return face.textureName == caulk; }),
				"the walls cover every face of the inner brush, so it is all caulk");
		}
		bool outsideKept = true;
		bool insideCaulked = true;
		for (const LevelMapBrush& wall : document.brushes) {
			if (wall.id == id) {
				continue;
			}
			const FaceRoles roles = rolesOf(wall, 64);
			outsideKept = outsideKept && roles.outer >= 1 && allAre(roles.outerTextures, QStringLiteral("studio/wall"));
			insideCaulked = insideCaulked && roles.inner == 1 && allAre(roles.innerTextures, caulk);
		}
		expect(outsideKept, "the walls' outside faces keep their material");
		expect(insideCaulked, "the walls' faces against the inner brush are caulk");
		expect(near(totalVolume(document), 128.0 * 128.0 * 128.0, 1.0), "walls and inner brush fill the brush exactly");
		expect(document.selection.size() == 6
				&& std::none_of(document.selection.cbegin(), document.selection.cend(), [id](const LevelMapSelectionRef& ref) { return ref.objectId == id; }),
			"the walls are selected, the inner brush is not");
		expect(undoLevelMapEdit(&document, &error) && bytesOf(document) == original, "undo restores the brush exactly", error);
	}

	// Diagonal with the inner brush kept: the brush stays as it is, its
	// covered faces caulked.
	document = cube();
	selectBrushes(&document, {id});
	if (expect(hollowLevelMapSelection(&document, 16, LevelMapHollowMode::Diagonal, options, &report, &error), "diagonal keeping the brush", error)) {
		const LevelMapBrush* kept = nullptr;
		for (const LevelMapBrush& brush : document.brushes) {
			if (brush.id == id) {
				kept = &brush;
			}
		}
		expect(kept && near(kept->mins.x, -64) && near(kept->maxs.z, 64) && report.innerKept == 1, "the brush stays where it was");
		expect(kept && std::all_of(kept->faces.cbegin(), kept->faces.cend(), [&caulk](const LevelMapBrushFace& face) { return face.textureName == caulk; }),
			"its faces under the walls are caulk");
		bool roles = true;
		for (const LevelMapBrush& wall : document.brushes) {
			if (wall.id != id) {
				const FaceRoles faces = rolesOf(wall, 80);
				roles = roles && allAre(faces.outerTextures, QStringLiteral("studio/wall")) && allAre(faces.innerTextures, caulk)
					&& allAre(faces.diagonalTextures, caulk) && faces.outer == 1;
			}
		}
		expect(roles, "outside faces keep their material; inner faces and joints are caulk");
		expect(near(totalVolume(document), 160.0 * 160.0 * 160.0, 1.0), "the brush and its walls fill the grown box");
	}

	// Without caulk, Extrude keeps the brush exactly as it was.
	document = cube();
	selectBrushes(&document, {id});
	options.caulkTexture.clear();
	if (expect(hollowLevelMapSelection(&document, 8, LevelMapHollowMode::Extrude, options, &report, &error), "extrude keeping the brush", error)) {
		const LevelMapDocument before = cube();
		bool same = false;
		for (const LevelMapBrush& brush : document.brushes) {
			if (brush.id == id) {
				same = brush.faces.size() == before.brushes.first().faces.size() && near(volumeOf(brush), 128.0 * 128.0 * 128.0);
			}
		}
		expect(same && document.brushes.size() == 7 && report.caulkedFaces == 0, "the brush stays whole beside six slabs");
	}
	// Overlap keeping the inner brush.
	document = cube();
	selectBrushes(&document, {id});
	if (expect(hollowLevelMapSelection(&document, 16, LevelMapHollowMode::Overlap, options, &report, &error), "overlap keeping the brush", error)) {
		expect(document.brushes.size() == 7 && report.innerKept == 1, "six overlapping walls round the inner brush");
	}
	// A brush too thin for two walls cannot keep an inner brush inside them.
	LevelMapDocument thin = mapWith(QStringLiteral("quake3"), {{{-64, -64, -8, true}, {64, 64, 8, true}}}, QStringLiteral("studio/wall"));
	selectBrushes(&thin, {thin.brushes.first().id});
	const QByteArray thinBytes = bytesOf(thin);
	expect(!hollowLevelMapSelection(&thin, 16, LevelMapHollowMode::Touch, options, nullptr, &error) && bytesOf(thin) == thinBytes,
		"a thin brush is refused inwards, changing nothing", error);
}

void checkFacePolicies()
{
	QString error;
	LevelMapDocument document = mapWith(QStringLiteral("quake3"), {{{-64, -64, -64, true}, {64, 64, 64, true}}, {{256, 0, 0, true}, {320, 64, 64, true}}},
		QStringLiteral("studio/wall"));
	const LevelMapBrush brush = document.brushes.first();
	const int id = brush.id;
	const int other = document.brushes.at(1).id;
	const int top = faceAlong(brush, 2, 1);
	const int bottom = faceAlong(brush, 2, -1);
	selectBrushes(&document, {id});
	// -proj and -cam: the faces at either end along the axis.
	QVector<LevelSurfaceFace> facing = levelMapFacesFacingAxis(document, {0, 0, 5, true});
	expect(facing.size() == 2 && facing.contains(LevelSurfaceFace {id, top}) && facing.contains(LevelSurfaceFace {id, bottom}),
		"facing Z leaves out the top and bottom", QString::number(facing.size()));
	facing = levelMapFacesFacingAxis(document, {1, 1, 0, true});
	expect(facing.size() == 4, "a diagonal axis leaves out both faces at each end that face it equally", QString::number(facing.size()));
	expect(levelMapFacesFacingAxis(document, {0, 0, 0, true}).isEmpty(), "a zero axis leaves nothing out");
	// -faces and +faces.
	const QVector<LevelSurfaceFace> picked {{id, top}, {other, 0}, {id, 40}, {id, top}};
	QVector<LevelSurfaceFace> excluded = levelCsgExcludedFaces(document, LevelCsgFacePolicy::ExcludePicked, picked);
	expect(excluded == QVector<LevelSurfaceFace> {{id, top}}, "picks on other brushes, past the faces or repeated are ignored");
	excluded = levelCsgExcludedFaces(document, LevelCsgFacePolicy::OnlyPicked, picked);
	expect(excluded.size() == 5 && !excluded.contains(LevelSurfaceFace {id, top}), "only the picked face is worked on");
	excluded = levelCsgExcludedFaces(document, LevelCsgFacePolicy::ExcludeFacingCamera, picked, {0, 0, -1, true});
	expect(excluded.size() == 2, "the camera policy uses the axis, not the picks");

	// A shaft: diagonal walls round the sides only, flush at top and bottom.
	const QByteArray original = bytesOf(document);
	LevelMapHollowOptions options;
	options.openFaces = levelMapFacesFacingAxis(document, {0, 0, 1, true});
	options.caulkTexture = QStringLiteral("common/caulk");
	LevelMapHollowReport report;
	if (expect(hollowLevelMapSelection(&document, 16, LevelMapHollowMode::Diagonal, options, &report, &error), "a diagonal shaft", error)) {
		expect(report.walls == 4 && report.openFaces == 2, "four walls, two open ends", QString::number(report.walls));
		expect(near(totalVolume(document), (160.0 * 160.0 - 128.0 * 128.0) * 128.0 + 64.0 * 64.0 * 64.0, 1.0),
			"the side walls fill the shell round the shaft", QString::number(totalVolume(document)));
		double lowest = 1e9;
		double highest = -1e9;
		for (const LevelMapBrush& wall : document.brushes) {
			if (wall.id != other) {
				lowest = std::min(lowest, wall.mins.z);
				highest = std::max(highest, wall.maxs.z);
			}
		}
		expect(near(lowest, -64) && near(highest, 64), "the walls stop flush with the open ends");
		expect(undoLevelMapEdit(&document, &error) && bytesOf(document) == original, "undo restores the shaft's brush", error);
	}
	// +faces with one face: one slab.
	selectBrushes(&document, {id});
	options.openFaces = levelCsgExcludedFaces(document, LevelCsgFacePolicy::OnlyPicked, {{id, top}});
	options.caulkTexture.clear();
	if (expect(hollowLevelMapSelection(&document, 8, LevelMapHollowMode::Extrude, options, &report, &error), "extrude one picked face", error)) {
		expect(report.walls == 1 && report.openFaces == 5, "one slab on the picked face");
	}
	// +faces on a brush with nothing picked leaves it alone.
	document = mapWith(QStringLiteral("quake3"), {{{-64, -64, -64, true}, {64, 64, 64, true}}}, QStringLiteral("studio/wall"));
	selectBrushes(&document, {document.brushes.first().id});
	options.openFaces = levelCsgExcludedFaces(document, LevelCsgFacePolicy::OnlyPicked, {});
	const QByteArray untouched = bytesOf(document);
	expect(!hollowLevelMapSelection(&document, 8, LevelMapHollowMode::Diagonal, options, nullptr, &error) && bytesOf(document) == untouched,
		"a brush with no picked face is left as it was", error);
}

void checkShrinkExpand()
{
	QString error;
	for (const QString& game : {QStringLiteral("quake3"), QStringLiteral("quake")}) {
		LevelMapDocument document = cube(game);
		const LevelMapBrush brush = document.brushes.first();
		const int id = brush.id;
		const QByteArray original = bytesOf(document);
		selectBrushes(&document, {id});
		LevelMapBrushOffsetReport report;
		if (!expect(offsetLevelMapSelectionFaces(&document, 8, {}, &report, &error), "expand by 8", game + QLatin1Char(' ') + error)) {
			continue;
		}
		const LevelMapBrush& grown = document.brushes.first();
		expect(report.changed == 1 && report.movedFaces == 6 && document.undoStack.size() == 1, "one brush, six faces, one undo step", game);
		expect(near(grown.mins.x, -72) && near(grown.maxs.y, 72) && near(grown.maxs.z, 72) && grown.faces.size() == 6, "the box grows 8 units each way", game);
		bool texturesKept = true;
		for (int face = 0; face < grown.faces.size(); ++face) {
			texturesKept = texturesKept && grown.faces.at(face).textureName == brush.faces.at(face).textureName
				&& near(grown.faces.at(face).shiftX, brush.faces.at(face).shiftX) && near(grown.faces.at(face).shiftY, brush.faces.at(face).shiftY);
		}
		expect(texturesKept, "faces keep their materials and offsets", game);
		expect(document.selection.size() == 1 && document.selection.first().objectId == id, "the brush stays selected", game);
		const QByteArray grownBytes = bytesOf(document);
		const LevelMapDocument back = reloaded(document);
		expect(back.brushes.size() == 1 && near(back.brushes.first().maxs.z, 72), "the grown brush survives a reload", game);
		expect(undoLevelMapEdit(&document, &error) && bytesOf(document) == original, "undo restores the brush exactly", game + error);
		expect(redoLevelMapEdit(&document, &error) && bytesOf(document) == grownBytes, "redo grows it again", game + error);
		expect(undoLevelMapEdit(&document, &error), "undo once more", error);
		// Shrink, the top face left alone.
		const int top = faceAlong(brush, 2, 1);
		if (expect(offsetLevelMapSelectionFaces(&document, -8, {{id, top}}, &report, &error), "shrink by 8 but the top", game + error)) {
			const LevelMapBrush& shrunk = document.brushes.first();
			expect(report.movedFaces == 5 && near(shrunk.mins.x, -56) && near(shrunk.maxs.x, 56) && near(shrunk.mins.z, -56) && near(shrunk.maxs.z, 64),
				"five faces move in, the top stays", game);
			expect(undoLevelMapEdit(&document, &error) && bytesOf(document) == original, "undo restores it", game);
		}
		// Too far in: refused, nothing changes.
		expect(!offsetLevelMapSelectionFaces(&document, -64, {}, &report, &error) && error.contains(QStringLiteral("too thin")) && bytesOf(document) == original,
			"a shrink that would flatten the brush is refused", error);
		QVector<LevelSurfaceFace> all;
		for (int face = 0; face < 6; ++face) {
			all.append({id, face});
		}
		expect(!offsetLevelMapSelectionFaces(&document, 8, all, &report, &error) && error.contains(QStringLiteral("every face")) && bytesOf(document) == original,
			"a brush with every face left alone is refused", error);
		expect(!offsetLevelMapSelectionFaces(&document, 0, {}, &report, &error) && !offsetLevelMapSelectionFaces(&document, 5000, {}, &report, &error)
				&& bytesOf(document) == original,
			"zero and huge offsets are refused");
	}

	// A wedge: each face's plane moves exactly 4 units out.
	LevelMapDocument document = cube();
	const int id = document.brushes.first().id;
	selectBrushes(&document, {id});
	if (expect(clipLevelMapSelection(&document, {64, -64, 64, true}, {64, 64, 64, true}, {-64, 64, -64, true}, LevelMapClipKeep::Back, nullptr, &error)
				&& document.brushes.size() == 1,
			"cut a wedge", error)) {
		document = reloaded(document);
		const LevelMapBrush wedge = document.brushes.first();
		selectBrushes(&document, {wedge.id});
		const MapBrushGeometry before = solveBrushGeometry(wedge.faces);
		LevelMapBrushOffsetReport report;
		if (expect(offsetLevelMapSelectionFaces(&document, 4, {}, &report, &error), "expand the wedge", error)) {
			const MapBrushGeometry after = solveBrushGeometry(document.brushes.first().faces);
			bool moved = after.faces.size() == before.faces.size();
			for (int face = 0; moved && face < before.faces.size(); ++face) {
				moved = !before.faces.at(face).isValid() || near(after.faces.at(face).plane.distance, before.faces.at(face).plane.distance + 4.0, 1e-3);
			}
			expect(moved, "every plane, the sloped one too, moves 4 units out");
		}
	}

	// Two brushes: the one too small to shrink is skipped, the other changes.
	document = mapWith(QStringLiteral("quake3"), {{{-64, -64, -64, true}, {64, 64, 64, true}}, {{256, 0, 0, true}, {264, 8, 8, true}}},
		QStringLiteral("studio/wall"));
	selectBrushes(&document, {document.brushes.at(0).id, document.brushes.at(1).id});
	LevelMapBrushOffsetReport report;
	expect(offsetLevelMapSelectionFaces(&document, -8, {}, &report, &error) && report.changed == 1 && report.skipped.size() == 1
			&& near(document.brushes.at(0).maxs.x, 56) && near(document.brushes.at(1).maxs.x, 264),
		"a brush too small to shrink is skipped with its reason", report.skipped.join(QLatin1Char('|')));
}

void checkLocksAndHidden()
{
	QString error;
	// A locked brush cannot be shrunk, expanded or hollowed.
	LevelMapDocument document = cube();
	const int id = document.brushes.first().id;
	QString layer;
	if (expect(createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Protected"), {}, &layer, &error), "a layer to lock", error)
		&& expect(assignLevelSceneObjects(&document, layer, {QStringLiteral("brush:%1").arg(id)}, &error), "the brush joins it", error)
		&& expect(setLevelSceneLocked(&document, layer, true, &error), "lock the layer", error)) {
		selectBrushes(&document, {id});
		const QByteArray before = bytesOf(document);
		expect(!offsetLevelMapSelectionFaces(&document, 8, {}, nullptr, &error) && bytesOf(document) == before && error.contains(QStringLiteral("Unlock")),
			"a locked brush is not expanded", error);
		expect(!hollowLevelMapSelection(&document, 8, LevelMapHollowMode::Diagonal, {}, nullptr, &error) && bytesOf(document) == before,
			"nor hollowed", error);
	}
	// A hidden brush of a selected entity is left as it was.
	document = mapWith(QStringLiteral("quake3"), {{{-64, -64, -64, true}, {64, 64, 64, true}}, {{256, 0, 0, true}, {320, 64, 64, true}}},
		QStringLiteral("studio/wall"));
	const int first = document.brushes.at(0).id;
	const int second = document.brushes.at(1).id;
	selectBrushes(&document, {first, second});
	int entity = -1;
	if (!expect(tieLevelMapSelectionToEntity(&document, QStringLiteral("func_group"), {}, &entity, &error), "a brush entity", error)) {
		return;
	}
	selectBrushes(&document, {second});
	LevelVisibilityReport hidden;
	if (!expect(changeLevelMapVisibility(&document, {LevelVisibilityOperation::HideSelected}, &hidden, &error) && hidden.hidden > 0, "hide one brush", error)) {
		return;
	}
	setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, entity}});
	LevelMapDocument shrink = document;
	LevelMapBrushOffsetReport report;
	expect(offsetLevelMapSelectionFaces(&shrink, -8, {}, &report, &error) && report.changed == 1 && report.skipped.size() == 1
			&& report.skipped.first().contains(QStringLiteral("hidden")),
		"Shrink leaves the hidden brush alone", report.skipped.join(QLatin1Char('|')) + error);
	LevelMapHollowReport walls;
	expect(hollowLevelMapSelection(&document, 8, LevelMapHollowMode::Extrude, {}, &walls, &error) && walls.hollowed == 1 && walls.skipped.size() == 1
			&& walls.skipped.first().contains(QStringLiteral("hidden")),
		"hollowing leaves the hidden brush alone", walls.skipped.join(QLatin1Char('|')) + error);
}

void checkCli(const QString& binary)
{
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "temporary folder")) {
		return;
	}
	const auto run = [&](const QStringList& arguments, int expected) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(binary, QStringList {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--settings-file"), temp.filePath(QStringLiteral("settings.ini"))}
				+ arguments);
		const bool finished = process.waitForStarted() && process.waitForFinished(60000);
		const QByteArray out = process.readAllStandardOutput();
		const QByteArray err = process.readAllStandardError();
		if (!finished) {
			process.kill();
			process.waitForFinished();
		}
		expect(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected, "CLI exit code",
			arguments.join(QLatin1Char(' ')) + QStringLiteral(" -> ") + QString::number(process.exitCode()) + QLatin1Char(' ') + QString::fromUtf8(out + err));
		return QJsonDocument::fromJson(out).object();
	};
	const auto field = [](const QJsonObject& result, const char* key) {
		const QJsonObject details = result.value(QStringLiteral("details")).toObject();
		return details.contains(QLatin1String(key)) ? details.value(QLatin1String(key)) : result.value(QLatin1String(key));
	};
	const auto load = [&](const QString& name, LevelMapDocument* document) {
		QString error;
		return loadLevelMap({temp.filePath(name), {}, QStringLiteral("idtech3")}, document, &error);
	};
	const LevelMapDocument fixture = cube();
	const int id = fixture.brushes.first().id;
	const int top = faceAlong(fixture.brushes.first(), 2, 1);
	{
		QFile file(temp.filePath(QStringLiteral("cube.map")));
		if (!expect(file.open(QIODevice::WriteOnly) && file.write(bytesOf(fixture)) > 0, "write the CLI fixture")) {
			return;
		}
	}
	const QString object = QStringLiteral("brush:%1").arg(id);
	const QString topFace = QStringLiteral("face:%1:%2").arg(id).arg(top + 1);
	const QStringList hollow {QStringLiteral("map"), QStringLiteral("hollow"), QStringLiteral("cube.map"), QStringLiteral("--object"), object,
		QStringLiteral("--thickness"), QStringLiteral("16")};

	QJsonObject result = run(hollow + QStringList {QStringLiteral("--mode"), QStringLiteral("diagonal"), QStringLiteral("--keep-inner"),
										  QStringLiteral("--caulk"), QStringLiteral("--output"), QStringLiteral("diagonal.map")},
		0);
	expect(field(result, "mode").toString() == QLatin1String("diagonal") && field(result, "wallCount").toInt() == 6
			&& field(result, "innerKept").toInt() == 1 && field(result, "keepInner").toBool() && field(result, "caulkedFaces").toInt() > 0,
		"map hollow --mode diagonal --keep-inner --caulk", QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
	LevelMapDocument saved;
	expect(load(QStringLiteral("diagonal.map"), &saved) && saved.brushes.size() == 7 && near(totalVolume(saved), 160.0 * 160.0 * 160.0, 1.0),
		"the saved walls and inner brush fill the grown box");

	result = run(hollow + QStringList {QStringLiteral("--mode"), QStringLiteral("extrude"), QStringLiteral("--only-face"), topFace,
							  QStringLiteral("--output"), QStringLiteral("slab.map")},
		0);
	expect(field(result, "wallCount").toInt() == 1 && field(result, "openFaceCount").toInt() == 5, "--only-face keeps one slab",
		QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
	result = run(hollow + QStringList {QStringLiteral("--mode"), QStringLiteral("diagonal"), QStringLiteral("--exclude-facing"), QStringLiteral("z"),
							  QStringLiteral("--output"), QStringLiteral("shaft.map")},
		0);
	expect(field(result, "wallCount").toInt() == 4 && field(result, "openFaceCount").toInt() == 2, "--exclude-facing z makes a shaft");
	result = run(hollow + QStringList {QStringLiteral("--mode"), QStringLiteral("wrap"), QStringLiteral("--exclude-face"), topFace,
							  QStringLiteral("--output"), QStringLiteral("wrap.map")},
		0);
	expect(field(result, "mode").toString() == QLatin1String("touch") && field(result, "wallCount").toInt() == 5, "wrap is touch; --exclude-face opens a face");
	for (const QStringList& bad : {QStringList {QStringLiteral("--mode"), QStringLiteral("mitre")}, QStringList {QStringLiteral("--exclude-facing"), QStringLiteral("w")},
			 QStringList {QStringLiteral("--exclude-facing"), QStringLiteral("0,0,0")}, QStringList {QStringLiteral("--only-face"), QStringLiteral("face:%1:1").arg(id + 9)},
			 QStringList {QStringLiteral("--exclude-face"), QStringLiteral("face:%1:9").arg(id)}}) {
		run(hollow + bad + QStringList {QStringLiteral("--output"), QStringLiteral("bad.map")}, 2);
	}
	expect(!QFile::exists(temp.filePath(QStringLiteral("bad.map"))), "refused hollows write nothing");

	// Shrink and Expand.
	const QStringList expand {QStringLiteral("map"), QStringLiteral("expand-brushes"), QStringLiteral("cube.map"), QStringLiteral("--object"), object};
	const QStringList shrink {QStringLiteral("map"), QStringLiteral("shrink-brushes"), QStringLiteral("cube.map"), QStringLiteral("--object"), object};
	result = run(expand + QStringList {QStringLiteral("--thickness"), QStringLiteral("8"), QStringLiteral("--output"), QStringLiteral("wide.map")}, 0);
	expect(result.value(QStringLiteral("changed")).toInt() == 1 && result.value(QStringLiteral("movedFaces")).toInt() == 6
			&& result.value(QStringLiteral("operation")).toString() == QLatin1String("expand") && result.value(QStringLiteral("offset")).toDouble() == 8.0,
		"map expand-brushes reports the move", QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
	expect(load(QStringLiteral("wide.map"), &saved) && near(saved.brushes.first().maxs.x, 72) && near(saved.brushes.first().mins.z, -72),
		"the expanded brush is saved");
	result = run(shrink + QStringList {QStringLiteral("--thickness"), QStringLiteral("8"), QStringLiteral("--exclude-face"), topFace,
							 QStringLiteral("--output"), QStringLiteral("thin.map")},
		0);
	expect(result.value(QStringLiteral("movedFaces")).toInt() == 5 && result.value(QStringLiteral("excludedFaces")).toArray() == QJsonArray {topFace},
		"map shrink-brushes leaves the excluded face", QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
	expect(load(QStringLiteral("thin.map"), &saved) && near(saved.brushes.first().maxs.x, 56) && near(saved.brushes.first().maxs.z, 64),
		"the shrunk brush is saved");
	result = run(shrink + QStringList {QStringLiteral("--thickness"), QStringLiteral("8"), QStringLiteral("--exclude-facing"), QStringLiteral("0,0,1"),
							 QStringLiteral("--output"), QStringLiteral("dry.map"), QStringLiteral("--dry-run")},
		0);
	expect(result.value(QStringLiteral("movedFaces")).toInt() == 4 && result.value(QStringLiteral("dryRun")).toBool()
			&& !QFile::exists(temp.filePath(QStringLiteral("dry.map"))),
		"--dry-run writes nothing");
	run(shrink + QStringList {QStringLiteral("--thickness"), QStringLiteral("64"), QStringLiteral("--output"), QStringLiteral("flat.map")}, 1);
	expect(!QFile::exists(temp.filePath(QStringLiteral("flat.map"))), "a shrink that flattens the brush writes nothing");
	for (const QStringList& bad : {QStringList {QStringLiteral("--output"), QStringLiteral("bad.map")},
			 QStringList {QStringLiteral("--thickness"), QStringLiteral("0"), QStringLiteral("--output"), QStringLiteral("bad.map")},
			 QStringList {QStringLiteral("--thickness"), QStringLiteral("8")},
			 QStringList {QStringLiteral("--thickness"), QStringLiteral("8"), QStringLiteral("--colour"), QStringLiteral("red"), QStringLiteral("--output"),
				 QStringLiteral("bad.map")},
			 QStringList {QStringLiteral("--thickness"), QStringLiteral("8"), QStringLiteral("--only-face"), QStringLiteral("brush:1"), QStringLiteral("--output"),
				 QStringLiteral("bad.map")}}) {
		run(shrink + bad, 2);
	}
	run({QStringLiteral("map"), QStringLiteral("expand-brushes"), QStringLiteral("cube.map"), QStringLiteral("--thickness"), QStringLiteral("8"),
			QStringLiteral("--output"), QStringLiteral("bad.map")},
		2);
	expect(!QFile::exists(temp.filePath(QStringLiteral("bad.map"))), "refused shrinks write nothing");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	checkModeIds();
	checkDiagonalAndExtrude();
	checkKeepInner();
	checkFacePolicies();
	checkShrinkExpand();
	checkLocksAndHidden();
	if (argc > 1) {
		checkCli(QString::fromLocal8Bit(argv[1]));
	}
	if (failures > 0) {
		std::cerr << failures << " CSG Tool check(s) failed.\n";
		return 1;
	}
	std::cout << "CSG Tool: diagonal and extrude hollows, keep inner, face policies, shrink and expand, locks, hidden brushes"
			  << (argc > 1 ? ", map hollow, shrink-brushes and expand-brushes" : "") << " passed.\n";
	return 0;
}
