// NetRadiant Custom's prisms, cones and spheres (core/level_primitive.h,
// core/level_shapes.h): the circle fit's sizes and orientation on every axis,
// raised side limits, in-place rebuilds that keep ids, owners, layers, file
// order and face formats in one exact undo step, hidden, locked, open-group,
// too-small and linked brushes, and `map add-shape --in-place --fit` and
// `map add-brush --fit` through the CLI when the vibestudio binary is the
// first argument. Synthetic maps and owned temporary files only.
#include "core/level_document.h"
#include "core/level_linked_groups.h"
#include "core/level_primitive.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_shapes.h"
#include "core/level_visibility.h"
#include "core/map_geometry.h"
#include "tests/level_surface_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>

using namespace vibestudio;

namespace {

bool ok = true;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << label << ": " << qPrintable(detail) << std::endl;
	}
	return value;
}

bool near(double a, double b, double tolerance = 0.01)
{
	return std::abs(a - b) <= tolerance;
}

QString text(const LevelMapVec3& v)
{
	return QStringLiteral("(%1 %2 %3)").arg(v.x).arg(v.y).arg(v.z);
}

bool bounds(const LevelMapBrush& brush, const LevelMapVec3& mins, const LevelMapVec3& maxs)
{
	return near(brush.mins.x, mins.x) && near(brush.mins.y, mins.y) && near(brush.mins.z, mins.z) && near(brush.maxs.x, maxs.x)
		&& near(brush.maxs.y, maxs.y) && near(brush.maxs.z, maxs.z);
}

// True when the solved brush has a face with this outward normal at this
// distance from the origin.
bool hasPlane(const LevelMapBrush& brush, double nx, double ny, double nz, double distance)
{
	const MapBrushGeometry geometry = solveBrushGeometry(brush.faces);
	for (const MapFacePolygon& face : geometry.faces) {
		const MapPlane& p = face.plane;
		if (face.isValid() && near(p.normalX, nx, 1e-4) && near(p.normalY, ny, 1e-4) && near(p.normalZ, nz, 1e-4) && near(p.distance, distance)) {
			return true;
		}
	}
	return false;
}

int vertexCount(const LevelMapBrush& brush)
{
	LevelBrushTopology topology;
	return levelBrushTopology(brush, &topology) ? static_cast<int>(topology.vertices.size()) : -1;
}

LevelMapBrush primitive(const QString& shape, int sides, int axis, const LevelMapVec3& mins, const LevelMapVec3& maxs, QString* error,
	LevelBrushPrimitiveFit fit = LevelBrushPrimitiveFit::Circle, int bands = 4)
{
	LevelBrushPrimitiveRequest request;
	request.shape = shape;
	request.sides = sides;
	request.bands = bands;
	request.axis = axis;
	request.mins = mins;
	request.maxs = maxs;
	request.fit = fit;
	request.texture = QStringLiteral("studio/new");
	LevelMapBrush brush;
	if (!createLevelBrushPrimitive(request, &brush, error)) {
		return {};
	}
	return brush;
}

// A box brush written as Quake's compilers read one, each face wound out.
QString boxBrush(int number, double x0, double y0, double z0, double x1, double y1, double z1)
{
	const auto p = [](double x, double y, double z) { return QStringLiteral("( %1 %2 %3 )").arg(x).arg(y).arg(z); };
	const auto face = [&](const QString& a, const QString& b, const QString& c) { return QStringLiteral("%1 %2 %3 base/box 0 0 0 1 1").arg(a, b, c); };
	return QStringList {QStringLiteral("// brush %1").arg(number), QStringLiteral("{"),
		face(p(x0 + 1, y0, z1), p(x0, y0, z1), p(x0, y0 + 1, z1)), face(p(x0, y0 + 1, z0), p(x0, y0, z0), p(x0 + 1, y0, z0)),
		face(p(x1, y0 + 1, z0), p(x1, y0, z0), p(x1, y0, z0 + 1)), face(p(x0, y0, z0 + 1), p(x0, y0, z0), p(x0, y0 + 1, z0)),
		face(p(x0, y1, z0 + 1), p(x0, y1, z0), p(x0 + 1, y1, z0)), face(p(x0 + 1, y0, z0), p(x0, y0, z0), p(x0, y0, z0 + 1)),
		QStringLiteral("}")}
		.join(QLatin1Char('\n'));
}

// Worldspawn brushes 0 (a 64 cube), 1 (64 by 32 by 64), 2 and 3, brush 4 in
// a func_door and brush 5, a slab half a unit thick, in a func_group.
QByteArray fixture()
{
	QStringList lines {QStringLiteral("// entity 0"), QStringLiteral("{"), QStringLiteral("\"classname\" \"worldspawn\"")};
	lines << boxBrush(0, 0, 0, 0, 64, 64, 64) << boxBrush(1, 128, 0, 0, 192, 32, 64) << boxBrush(2, 256, 0, 0, 320, 64, 64)
		  << boxBrush(3, 384, 0, 0, 448, 64, 64) << QStringLiteral("}") << QStringLiteral("// entity 1") << QStringLiteral("{")
		  << QStringLiteral("\"classname\" \"func_door\"") << boxBrush(4, 0, 128, 0, 64, 192, 64) << QStringLiteral("}")
		  << QStringLiteral("// entity 2") << QStringLiteral("{") << QStringLiteral("\"classname\" \"func_group\"") << boxBrush(5, 512, 0, 0, 576, 64, 0.5)
		  << QStringLiteral("}");
	return lines.join(QStringLiteral("\r\n")).toUtf8() + "\r\n";
}

QString className(const LevelMapDocument& document, int entityId)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.id == entityId) {
			return entity.className;
		}
	}
	return {};
}

const LevelMapBrush* brushById(const LevelMapDocument& document, int id)
{
	for (const LevelMapBrush& brush : document.brushes) {
		if (brush.id == id) {
			return &brush;
		}
	}
	return nullptr;
}

QJsonObject runCli(const QString& binary, const QStringList& args, int expected)
{
	QProcess process;
	process.start(binary, args);
	const bool finished = process.waitForFinished(60000);
	const QByteArray out = process.readAllStandardOutput();
	expect(finished && process.exitCode() == expected, "CLI exit code",
		QStringLiteral("%1 for %2: %3 %4").arg(process.exitCode()).arg(args.join(QLatin1Char(' ')), QString::fromUtf8(out),
			QString::fromUtf8(process.readAllStandardError())));
	return QJsonDocument::fromJson(out).object();
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QString error;

	// Fit ids and side limits.
	LevelBrushPrimitiveFit fit = LevelBrushPrimitiveFit::Box;
	expect(levelBrushPrimitiveFitFromId(QStringLiteral(" Circle "), &fit) && fit == LevelBrushPrimitiveFit::Circle
			&& levelBrushPrimitiveFitFromId(QStringLiteral("box"), &fit) && fit == LevelBrushPrimitiveFit::Box
			&& !levelBrushPrimitiveFitFromId(QStringLiteral("radiant"), &fit) && levelBrushPrimitiveFitId(LevelBrushPrimitiveFit::Circle) == QStringLiteral("circle"),
		"fit ids");
	expect(levelBrushPrimitiveMaxSides(QStringLiteral("cylinder")) == 126 && levelBrushPrimitiveMaxSides(QStringLiteral("cone")) == 127
			&& levelBrushPrimitiveMaxSides(QStringLiteral("sphere")) == 64 && levelBrushPrimitiveMaxSides(QStringLiteral("box")) == 0,
		"side limits follow the 128-face component editor");
	expect(levelSidedPrismCommandId(3) == QStringLiteral("map.make3SidedPrism") && levelSidedPrismCommandId(10).isEmpty()
			&& levelSidedPrismSides(QStringLiteral("map.make9SidedPrism")) == 9 && levelSidedPrismSides(QStringLiteral("map.makePrism")) == 0,
		"sided prism command ids");

	// The circle fit: an octagon on a square keeps the box's own faces.
	const LevelMapVec3 o {0, 0, 0, true};
	const LevelMapVec3 cube {64, 64, 64, true};
	LevelMapBrush brush = primitive(QStringLiteral("cylinder"), 8, 2, o, cube, &error);
	expect(brush.faceCount == 10 && bounds(brush, o, cube) && hasPlane(brush, 1, 0, 0, 64) && hasPlane(brush, 0, 1, 0, 64) && hasPlane(brush, -1, 0, 0, 0)
			&& hasPlane(brush, 0, -1, 0, 0) && hasPlane(brush, 0, 0, 1, 64) && hasPlane(brush, 0, 0, -1, 0),
		"8-sided prism keeps the box's axial faces", error + text(brush.mins) + text(brush.maxs));
	brush = primitive(QStringLiteral("cylinder"), 8, 2, o, cube, &error, LevelBrushPrimitiveFit::Box);
	expect(brush.faceCount == 10 && bounds(brush, o, cube) && !hasPlane(brush, 1, 0, 0, 64), "the box fit still puts corners on the axes", error);
	// Four sides: a square on the larger extent, overhanging the shorter side.
	brush = primitive(QStringLiteral("cylinder"), 4, 2, o, {64, 32, 64, true}, &error);
	expect(brush.faceCount == 6 && vertexCount(brush) == 8 && bounds(brush, {0, -16, 0, true}, {64, 48, 64, true}),
		"4-sided prism is a square on the larger extent", error + text(brush.mins) + text(brush.maxs));
	brush = primitive(QStringLiteral("cylinder"), 4, 2, o, {64, 32, 64, true}, &error, LevelBrushPrimitiveFit::Box);
	expect(brush.faceCount == 6 && bounds(brush, o, {64, 32, 64, true}) && !hasPlane(brush, 1, 0, 0, 64), "the box fit's four sides make a diamond",
		error);
	// Three sides: a face square to +X on the circle, the corners beyond.
	brush = primitive(QStringLiteral("cylinder"), 3, 2, o, cube, &error);
	const double reach = 32 * std::tan(std::numbers::pi / 3);
	expect(brush.faceCount == 5 && vertexCount(brush) == 6 && hasPlane(brush, 1, 0, 0, 64)
			&& bounds(brush, {-32, 32 - reach, 0, true}, {64, 32 + reach, 64, true}),
		"3-sided prism faces the first axis and overhangs", error + text(brush.mins) + text(brush.maxs));
	// The first axis follows the depth axis: Y for a Side view (X deep), Z for
	// a Front view (Y deep).
	brush = primitive(QStringLiteral("cylinder"), 4, 0, o, {64, 64, 32, true}, &error);
	expect(hasPlane(brush, 0, 1, 0, 64) && hasPlane(brush, 0, 0, 1, 48) && bounds(brush, {0, 0, -16, true}, {64, 64, 48, true}),
		"X depth axis: faces on Y and Z", error + text(brush.mins) + text(brush.maxs));
	brush = primitive(QStringLiteral("cylinder"), 3, 1, o, cube, &error);
	expect(hasPlane(brush, 0, 0, 1, 64) && near(brush.mins.y, 0) && near(brush.maxs.y, 64), "Y depth axis: the first face is on Z",
		error + text(brush.mins) + text(brush.maxs));
	// A cone's base at the low end, its point over the centre of the high end.
	brush = primitive(QStringLiteral("cone"), 8, 2, o, cube, &error);
	expect(brush.faceCount == 9 && vertexCount(brush) == 9 && bounds(brush, o, cube) && hasPlane(brush, 0, 0, -1, 0)
			&& hasPlane(brush, 2 / std::sqrt(5.0), 0, 1 / std::sqrt(5.0), 128 / std::sqrt(5.0)),
		"8-sided cone faces the first axis", error);
	// A sphere is round on the largest extent, poles on the depth axis.
	brush = primitive(QStringLiteral("sphere"), 8, 2, o, {64, 64, 32, true}, &error, LevelBrushPrimitiveFit::Circle, 8);
	expect(brush.faceCount == 64 && vertexCount(brush) == 58 && bounds(brush, {0, 0, -16, true}, {64, 64, 48, true}),
		"8 by 8 sphere on the largest extent", error + text(brush.mins) + text(brush.maxs));
	brush = primitive(QStringLiteral("sphere"), 8, 2, o, {64, 64, 32, true}, &error, LevelBrushPrimitiveFit::Box, 8);
	expect(bounds(brush, o, {64, 64, 32, true}), "the box fit still makes an ellipsoid", error);
	// Raised limits, as far as 128 faces allow.
	const LevelMapVec3 big {256, 256, 256, true};
	for (const auto f : {LevelBrushPrimitiveFit::Circle, LevelBrushPrimitiveFit::Box}) {
		expect(primitive(QStringLiteral("cylinder"), 126, 2, o, big, &error, f).faceCount == 128, "126-sided cylinder", error);
		expect(primitive(QStringLiteral("cone"), 127, 2, o, big, &error, f).faceCount == 128, "127-sided cone", error);
		expect(primitive(QStringLiteral("cylinder"), 127, 2, o, big, &error, f).faces.isEmpty() && error.contains(QStringLiteral("126")),
			"127-sided cylinder refused", error);
		expect(primitive(QStringLiteral("cone"), 128, 2, o, big, &error, f).faces.isEmpty(), "128-sided cone refused", error);
	}
	expect(primitive(QStringLiteral("sphere"), 11, 2, o, big, &error, LevelBrushPrimitiveFit::Circle, 11).faceCount == 121, "11 by 11 sphere", error);

	// A circle-fit brush added to a map is checked against its own bounds.
	{
		LevelMapDocument created;
		LevelMapCreateRequest create;
		create.starterRoom = false;
		createLevelMap(create, &created);
		LevelBrushPrimitiveRequest request;
		request.shape = QStringLiteral("cylinder");
		request.sides = 4;
		request.mins = o;
		request.maxs = {64, 32, 64, true};
		request.fit = LevelBrushPrimitiveFit::Circle;
		int id = -1;
		expect(addLevelMapBrushPrimitive(&created, request, &id, &error) && created.brushes.size() == 1
				&& bounds(created.brushes.first(), {0, -16, 0, true}, {64, 48, 64, true}),
			"add a circle-fit primitive", error);
		LevelShapeRequest shape;
		shape.shape = QStringLiteral("cone");
		shape.fit = LevelBrushPrimitiveFit::Circle;
		shape.mins = {128, 0, 0, true};
		shape.maxs = {192, 32, 64, true};
		QVector<int> ids;
		expect(addLevelMapShape(&created, shape, &ids, &error) && ids.size() == 1 && brushById(created, ids.first())
				&& near(brushById(created, ids.first())->mins.y, -16),
			"Shapes tab cones take the fit", error);
	}

	// In-place rebuilds keep ids, owners, layers, file order and the selection.
	const QByteArray source = fixture();
	LevelMapDocument document;
	if (!expect(loadLevelMapBytes({QStringLiteral("prism.map"), {}, QStringLiteral("idtech2")}, source, &document, &error) && document.brushes.size() == 6,
			"fixture loads", error)) {
		return 1;
	}
	const int door = brushById(document, 4)->entityId;
	QString layer;
	expect(createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Pillars"), {}, &layer, &error)
			&& assignLevelSceneObjects(&document, layer, {QStringLiteral("brush:0")}, &error),
		"layer fixture", error);
	const QVector<LevelMapSelectionRef> selection {{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakeBrush, 1},
		{LevelMapSelectionKind::Entity, door}};
	setLevelMapSelection(&document, selection);
	const QByteArray before = serializeLevelMap(document).bytes;
	const int undoDepth = static_cast<int>(document.undoStack.size());
	LevelShapeRequest prism;
	prism.shape = QStringLiteral("cylinder");
	prism.sides = 6;
	prism.axis = 2;
	prism.fit = LevelBrushPrimitiveFit::Circle;
	prism.texture = QStringLiteral("studio/pillar");
	LevelShapeRebuildReport report;
	expect(rebuildLevelMapBrushesAsShape(&document, {0, 1, 4}, prism, &report, &error) && report.brushIds == QVector<int>({0, 1, 4})
			&& report.skipped.isEmpty(),
		"rebuild three brushes in place", error + report.skipped.join(QLatin1Char(';')));
	const LevelMapBrush* first = brushById(document, 0);
	const LevelMapBrush* owned = brushById(document, 4);
	expect(document.brushes.size() == 6 && first && first->faceCount == 8 && owned && owned->faceCount == 8 && owned->entityId == door
			&& brushById(document, 2)->faceCount == 6,
		"ids, counts and the brush entity's ownership are kept");
	expect(first && hasPlane(*first, 1, 0, 0, 64) && near(first->mins.z, 0) && near(first->maxs.z, 64)
			&& std::all_of(first->faces.cbegin(), first->faces.cend(), [](const LevelMapBrushFace& face) { return face.textureName == QStringLiteral("studio/pillar"); }),
		"each brush fills its own bounds in the current material");
	expect(levelSceneMembership(document.scene, QStringLiteral("brush:0")) == layer, "layer membership kept");
	expect(document.selection == selection && static_cast<int>(document.undoStack.size()) == undoDepth + 1, "selection kept, one undo step");
	const QByteArray after = serializeLevelMap(document).bytes;
	LevelMapDocument reopened;
	expect(loadLevelMapBytes({QStringLiteral("prism.map"), {}, QStringLiteral("idtech2")}, after, &reopened, &error) && reopened.brushes.size() == 6
			&& reopened.brushes.at(0).faceCount == 8 && reopened.brushes.at(1).faceCount == 8 && reopened.brushes.at(2).faceCount == 6
			&& reopened.brushes.at(4).faceCount == 8 && className(reopened, reopened.brushes.at(4).entityId) == QStringLiteral("func_door"),
		"saved in place: file order and owner survive a reopen", error);
	expect(after.contains("// brush 0") && after.indexOf("studio/pillar") < after.indexOf("// brush 2"), "the brush markers and order stay");
	expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == before && document.selection == selection, "exact undo", error);
	expect(redoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == after, "exact redo", error);
	expect(undoLevelMapEdit(&document, &error), "undo for the next checks", error);

	// Refusals leave the map as it was.
	const quint64 revision = document.revision;
	LevelShapeRequest arch = prism;
	arch.shape = QStringLiteral("arch");
	expect(!rebuildLevelMapBrushesAsShape(&document, {0}, arch, &report, &error) && document.revision == revision, "several-brush shapes refused", error);
	expect(!rebuildLevelMapBrushesAsShape(&document, {99}, prism, &report, &error) && error.contains(QStringLiteral("99")), "missing brush refused", error);
	LevelShapeRequest tooMany = prism;
	tooMany.sides = 127;
	expect(!rebuildLevelMapBrushesAsShape(&document, {0}, tooMany, &report, &error) && document.revision == revision, "too many sides refused", error);
	// A brush less than a unit thick is skipped with its reason; 126 sides fit
	// the rest.
	LevelShapeRequest fine = prism;
	fine.sides = 126;
	expect(rebuildLevelMapBrushesAsShape(&document, {0, 5}, fine, &report, &error) && report.brushIds == QVector<int>({0}) && report.skipped.size() == 1
			&& report.skipped.first().contains(QStringLiteral("5")) && brushById(document, 0)->faceCount == 128,
		"a brush too thin for a shape is skipped", error + report.skipped.join(QLatin1Char(';')));
	expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == before, "undo the detailed prism", error);
	const quint64 thinRevision = document.revision;
	expect(!rebuildLevelMapBrushesAsShape(&document, {5}, prism, &report, &error) && error.contains(QStringLiteral("5")) && document.revision == thinRevision,
		"only a thin brush refuses", error);
	// Hidden brushes are skipped, so hiding the only one refuses.
	setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 2}});
	LevelVisibilityRequest hide;
	expect(changeLevelMapVisibility(&document, hide, nullptr, &error) && levelMapHiddenObjects(document).contains(QStringLiteral("brush:2")), "hide brush 2",
		error);
	const QByteArray hiddenBytes = serializeLevelMap(document).bytes;
	expect(!rebuildLevelMapBrushesAsShape(&document, {2}, prism, &report, &error) && error.contains(QStringLiteral("hidden"))
			&& serializeLevelMap(document).bytes == hiddenBytes,
		"a hidden brush is left alone", error);
	expect(rebuildLevelMapBrushesAsShape(&document, {2, 3}, prism, &report, &error) && report.brushIds == QVector<int>({3}) && report.skipped.size() == 1,
		"hidden brushes are skipped beside visible ones", error);
	expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == hiddenBytes, "undo beside hidden", error);
	// A locked layer refuses the whole edit.
	QString locked;
	expect(createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &locked, &error)
			&& assignLevelSceneObjects(&document, locked, {QStringLiteral("brush:3")}, &error) && setLevelSceneLocked(&document, locked, true, &error),
		"lock brush 3", error);
	const QByteArray lockedBytes = serializeLevelMap(document).bytes;
	const quint64 lockedRevision = document.revision;
	expect(!rebuildLevelMapBrushesAsShape(&document, {0, 3}, prism, &report, &error) && document.revision == lockedRevision
			&& serializeLevelMap(document).bytes == lockedBytes,
		"a locked brush refuses the edit", error);
	expect(rebuildLevelMapBrushesAsShape(&document, {0}, prism, &report, &error) && brushById(document, 0)->faceCount == 8,
		"unlocked brushes still rebuild beside locks", error);
	// Inside an open group only its own brushes can change.
	QString group;
	expect(createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("Columns"), {}, &group, &error)
			&& assignLevelSceneObjects(&document, group, {QStringLiteral("brush:1")}, &error) && openLevelSceneGroup(&document, group, &error),
		"open a group", error);
	const quint64 groupRevision = document.revision;
	expect(!rebuildLevelMapBrushesAsShape(&document, {4}, prism, &report, &error) && document.revision == groupRevision,
		"a brush outside the open group refuses", error);
	expect(rebuildLevelMapBrushesAsShape(&document, {1}, prism, &report, &error) && brushById(document, 1)->faceCount == 8
			&& levelSceneMembership(document.scene, QStringLiteral("brush:1")) == group,
		"a brush in the open group rebuilds and stays in it", error);

	// A rebuilt brush in a linked group reaches its linked copies in the same
	// undo step.
	{
		LevelMapDocument linked;
		QString original;
		QString copy;
		expect(loadLevelMapBytes({QStringLiteral("linked.map"), {}, QStringLiteral("idtech2")}, source, &linked, &error)
				&& createLevelSceneNode(&linked, LevelSceneNodeKind::Group, QStringLiteral("Pillar"), {}, &original, &error)
				&& assignLevelSceneObjects(&linked, original, {QStringLiteral("brush:2")}, &error)
				&& createLinkedLevelGroup(&linked, original, {0, 512, 0, true}, &copy, &error) && linked.brushes.size() == 7,
			"linked group fixture", error);
		const QByteArray linkedBefore = serializeLevelMap(linked).bytes;
		const auto linkedDepth = linked.undoStack.size();
		expect(rebuildLevelMapBrushesAsShape(&linked, {2}, prism, &report, &error)
				&& std::count_if(linked.brushes.cbegin(), linked.brushes.cend(), [](const LevelMapBrush& b) { return b.faceCount == 8; }) == 2
				&& linked.undoStack.size() == linkedDepth + 1,
			"the linked copy follows in one undo step", error);
		expect(undoLevelMapEdit(&linked, &error) && serializeLevelMap(linked).bytes == linkedBefore, "linked undo is exact", error);
	}

	// Every face format keeps its own dialect; comments and contents stay.
	for (const QString& dialect : {QStringLiteral("classic"), QStringLiteral("valve220"), QStringLiteral("brushDef"), QStringLiteral("brushDef3")}) {
		LevelMapDocument styled;
		const QByteArray original = tests::surfaceFixture(dialect);
		if (!expect(loadLevelMapBytes({QStringLiteral("styled.map"), {}, QStringLiteral("idtech3")}, original, &styled, &error), "dialect fixture", error)) {
			continue;
		}
		const int id = styled.brushes.first().id;
		LevelShapeRequest cone = prism;
		cone.shape = QStringLiteral("cone");
		cone.sides = 5;
		cone.axis = 1;
		expect(rebuildLevelMapBrushesAsShape(&styled, {id}, cone, &report, &error) && styled.brushes.first().primitiveKind == dialect
				&& styled.brushes.first().faceCount == 6 && styled.brushes.first().faces.first().contentFlags == 2,
			"face format and contents kept", dialect + QStringLiteral(": ") + error);
		const QByteArray saved = serializeLevelMap(styled).bytes;
		expect(saved.contains("// flags") && saved.contains("// untouched point entity"), "comments kept", dialect);
		LevelMapDocument back;
		expect(loadLevelMapBytes({QStringLiteral("styled.map"), {}, QStringLiteral("idtech3")}, saved, &back, &error) && back.brushes.first().faceCount == 6
				&& back.brushes.first().primitiveKind == dialect,
			"reopens in the same format", dialect + QStringLiteral(": ") + error);
		expect(undoLevelMapEdit(&styled, &error) && serializeLevelMap(styled).bytes == original, "byte-exact undo in every format", dialect);
	}

	// The CLI: map add-shape --in-place --fit circle and map add-brush --fit.
	if (argc > 1) {
		const QString binary = QString::fromLocal8Bit(argv[1]);
		QTemporaryDir temporary;
		if (!expect(temporary.isValid(), "temporary folder")) {
			return 1;
		}
		const QString input = QDir(temporary.path()).filePath(QStringLiteral("input.map"));
		const QString output = QDir(temporary.path()).filePath(QStringLiteral("output.map"));
		QFile file(input);
		if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(), "CLI fixture")) {
			return 1;
		}
		file.close();
		const QStringList base {QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("add-shape"), input, QStringLiteral("--shape"),
			QStringLiteral("cylinder"), QStringLiteral("--sides"), QStringLiteral("6"), QStringLiteral("--axis"), QStringLiteral("z"), QStringLiteral("--fit"),
			QStringLiteral("circle"), QStringLiteral("--in-place"), QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--object"),
			QStringLiteral("brush:4"), QStringLiteral("--texture"), QStringLiteral("studio/pillar"), QStringLiteral("--output"), output, QStringLiteral("--json")};
		runCli(binary, base + QStringList {QStringLiteral("--dry-run")}, 0);
		expect(!QFile::exists(output), "dry run writes nothing");
		const QJsonObject result = runCli(binary, base, 0);
		expect(result.value(QStringLiteral("inPlace")).toBool() && result.value(QStringLiteral("fit")).toString() == QStringLiteral("circle")
				&& result.value(QStringLiteral("brushes")).toArray() == QJsonArray({QStringLiteral("brush:0"), QStringLiteral("brush:4")}),
			"structured in-place result", QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
		LevelMapDocument written;
		expect(loadLevelMap({output, {}, QStringLiteral("idtech2")}, &written, &error) && written.brushes.size() == 6 && written.brushes.at(0).faceCount == 8
				&& written.brushes.at(4).faceCount == 8 && className(written, written.brushes.at(4).entityId) == QStringLiteral("func_door"),
			"CLI rebuild keeps ids and owners", error);
		const auto variant = [&](const QString& option, const QString& value) {
			QStringList args = base;
			const qsizetype at = args.indexOf(option);
			if (at >= 0) {
				args[at + 1] = value;
			}
			return args;
		};
		runCli(binary, variant(QStringLiteral("--shape"), QStringLiteral("arch")), 2);
		runCli(binary, variant(QStringLiteral("--fit"), QStringLiteral("radiant")), 2);
		runCli(binary, base + QStringList {QStringLiteral("--mins"), QStringLiteral("0,0,0"), QStringLiteral("--maxs"), QStringLiteral("8,8,8")}, 2);
		QStringList boxFit = base;
		boxFit[boxFit.indexOf(QStringLiteral("cylinder"))] = QStringLiteral("box");
		boxFit.removeAt(boxFit.indexOf(QStringLiteral("--sides")) + 1);
		boxFit.removeAt(boxFit.indexOf(QStringLiteral("--sides")));
		runCli(binary, boxFit, 2);
		const QString brushOutput = QDir(temporary.path()).filePath(QStringLiteral("brush.map"));
		const QJsonObject added = runCli(binary, {QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("add-brush"), input, QStringLiteral("--shape"),
			QStringLiteral("cylinder"), QStringLiteral("--sides"), QStringLiteral("4"), QStringLiteral("--fit"), QStringLiteral("circle"), QStringLiteral("--mins"),
			QStringLiteral("0,0,0"), QStringLiteral("--maxs"), QStringLiteral("64,32,64"), QStringLiteral("--texture"), QStringLiteral("studio/new"),
			QStringLiteral("--output"), brushOutput, QStringLiteral("--json")}, 0);
		LevelMapDocument brushMap;
		expect(added.value(QStringLiteral("faceCount")).toInt() == 6 && loadLevelMap({brushOutput, {}, QStringLiteral("idtech2")}, &brushMap, &error)
				&& std::any_of(brushMap.brushes.cbegin(), brushMap.brushes.cend(),
					[](const LevelMapBrush& made) { return bounds(made, {0, -16, 0, true}, {64, 48, 64, true}); }),
			"map add-brush --fit circle", error);
		runCli(binary, {QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("add-brush"), input, QStringLiteral("--shape"), QStringLiteral("box"),
			QStringLiteral("--fit"), QStringLiteral("circle"), QStringLiteral("--mins"), QStringLiteral("0,0,0"), QStringLiteral("--maxs"),
			QStringLiteral("64,32,64"), QStringLiteral("--texture"), QStringLiteral("studio/new"), QStringLiteral("--output"), brushOutput,
			QStringLiteral("--overwrite")}, 2);
	}
	std::cout << (ok ? "level prism smoke passed" : "level prism smoke FAILED") << std::endl;
	return ok ? 0 : 1;
}
