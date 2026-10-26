// Core and command-line checks of NetRadiant Custom's Fit Width and Fit
// Height and its Project Axial, Ortho and Camera (core/level_surface.h):
// LevelSurfaceRequest::keepProportions fits one axis and scales the other by
// the same factor from its minimum, on classic, Valve 220 and brush-primitive
// faces and on a patch, where Width Only and Height Only keep the other axis;
// ProjectAxial gives faces Natural's projection (brush-primitive offsets within
// one repeat) and patches a flat projection along their nearest world axis;
// ProjectDirection lays one flat projection along a direction with q3map2's
// ComputeAxisBase axes in every face format and on patches, leaves faces it
// runs along and classic faces that cannot hold it as they were, counts them,
// and refuses a request that changes nothing but skip; each edit is one exact
// undo step and a locked brush refuses it. map align-textures --fit with
// --keep-proportions, --project and --project-direction write what the shared
// service writes, and refuse bad input without writing.

#include "core/level_document.h"
#include "core/level_nudge.h"
#include "core/level_patch.h"
#include "core/level_patch_tools.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_surface.h"
#include "core/level_texture_mapping.h"
#include "core/map_geometry.h"
#include "tests/level_material_test_helpers.h"
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
#include <limits>

using namespace vibestudio;

namespace {

bool ok = true;
QString phase;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << qPrintable(phase) << ": " << label << ": " << qPrintable(detail) << std::endl;
	}
	return value;
}

bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }
bool same(const LevelMapVec3& a, const LevelMapVec3& b, double tolerance = 1e-5)
{
	return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}
double dot(const LevelMapVec3& a, const LevelMapVec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
LevelMapVec3 scaled(const LevelMapVec3& a, double s) { return {a.x * s, a.y * s, a.z * s, true}; }

bool load(const QByteArray& bytes, LevelMapDocument* document)
{
	QString error;
	return expect(loadLevelMapBytes({QStringLiteral("projection.map"), {}, QStringLiteral("idtech3")}, bytes, document, &error), "load fixture", error);
}

LevelSurfaceRequest fit(double u, double v, bool keep)
{
	LevelSurfaceRequest request;
	request.operation = LevelSurfaceOperation::Fit;
	request.x = u;
	request.y = v;
	request.keepProportions = keep;
	return request;
}

LevelSurfaceRequest project(const LevelMapVec3& direction, double scale)
{
	LevelSurfaceRequest request;
	request.operation = LevelSurfaceOperation::ProjectDirection;
	request.x = request.y = scale;
	request.projectionDirection = direction;
	return request;
}

LevelSurfaceRequest operation(LevelSurfaceOperation kind, double scale)
{
	LevelSurfaceRequest request;
	request.operation = kind;
	request.x = request.y = scale;
	return request;
}

QVector<LevelSurfaceFace> allFaces(const LevelMapDocument& map)
{
	QVector<LevelSurfaceFace> faces;
	for (const auto& brush : map.brushes) {
		for (int i = 0; i < brush.faces.size(); ++i) {
			faces.append({brush.id, i});
		}
	}
	return faces;
}

// The bytes `request` leaves on `faces` (every face when empty) and `patches`,
// or "refused: " and the reason.
QByteArray edited(const QByteArray& source, const LevelSurfaceRequest& request, const QHash<QString, QSize>& sizes, QVector<LevelSurfaceFace> faces = {},
	const QVector<int>& patches = {}, bool brushes = true, LevelSurfaceEditPlan* planOut = nullptr)
{
	LevelMapDocument map;
	load(source, &map);
	if (brushes && faces.isEmpty()) {
		faces = allFaces(map);
	}
	LevelSurfaceEditPlan plan;
	QString error;
	if (!prepareLevelSurfaceEdits(map, faces, patches, {request}, sizes, &plan, &error) || !commitLevelSurfaceEdit(&map, plan, &error)) {
		return QByteArrayLiteral("refused: ") + error.toUtf8();
	}
	if (planOut) {
		*planOut = plan;
	}
	return serializeLevelMap(map).bytes;
}

// A face's texture coordinates over its winding: U and V minimum and maximum,
// in its own units (texels, or repeats on brush-primitive faces).
struct Extent {
	double minU = std::numeric_limits<double>::max(), maxU = -std::numeric_limits<double>::max();
	double minV = std::numeric_limits<double>::max(), maxV = -std::numeric_limits<double>::max();
	double width() const { return maxU - minU; }
	double height() const { return maxV - minV; }
};
Extent extent(const LevelMapBrush& brush, int index)
{
	Extent result;
	const auto geometry = solveBrushGeometry(brush.faces, brush.id, brush.entityId);
	const auto projection = levelTextureProjection(brush.faces[index]);
	if (!geometry.solved || index >= geometry.faces.size()) {
		return result;
	}
	for (const auto& point : geometry.faces[index].points) {
		const QPointF uv = projection.at(point);
		result.minU = std::min(result.minU, uv.x());
		result.maxU = std::max(result.maxU, uv.x());
		result.minV = std::min(result.minV, uv.y());
		result.maxV = std::max(result.maxV, uv.y());
	}
	return result;
}
Extent patchExtent(const LevelMapPatch& patch)
{
	Extent result;
	for (int i = 0; i < patch.controlU.size(); ++i) {
		result.minU = std::min(result.minU, patch.controlU[i]);
		result.maxU = std::max(result.maxU, patch.controlU[i]);
		result.minV = std::min(result.minV, patch.controlV[i]);
		result.maxV = std::max(result.maxV, patch.controlV[i]);
	}
	return result;
}

LevelMapVec3 faceNormal(const LevelMapBrushFace& face)
{
	if (face.explicitPlane) {
		const double length = std::sqrt(dot(face.planeNormal, face.planeNormal));
		return scaled(face.planeNormal, 1 / length);
	}
	const auto plane = planeFromPoints(face.p0, face.p1, face.p2);
	return {plane.normalX, plane.normalY, plane.normalZ, true};
}

// A Quake III map with one patch of `shape`, textured studio/grid.
QByteArray patchFixture(const QString& shape)
{
	LevelMapDocument created;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake3");
	create.starterRoom = false;
	QString error;
	createLevelMap(create, &created, &error);
	LevelPatchCreateRequest request;
	request.shape = shape;
	request.texture = QStringLiteral("studio/grid");
	LevelMapPatch patch;
	expect(createLevelPatch(request, &patch, &error) && addLevelMapPatch(&created, patch, nullptr, &error), "patch fixture", error);
	return serializeLevelMap(created).bytes;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	expect(temp.isValid(), "temporary directory");
	QString error;
	const QSize imageSize(128, 64);
	const QHash<QString, QSize> sizes {{QStringLiteral("studio/grid"), imageSize}};
	const double root3 = 1 / std::sqrt(3.0);
	const LevelMapVec3 diagonal {root3, root3, root3, true};

	for (const auto* dialect : {"classic", "valve220", "brushDef", "brushDef3"}) {
		phase = QStringLiteral("fit ") + QString::fromLatin1(dialect);
		const QByteArray fixture = tests::surfaceFixture(QString::fromLatin1(dialect));
		LevelMapDocument map;
		if (!load(fixture, &map)) {
			return 1;
		}
		const auto source = serializeLevelMap(map).bytes;
		const bool matrix = map.brushes.first().faces.first().explicitTextureMatrix;
		// One repeat is the image's size in texels, or 1 on brush primitives.
		const double repeatU = matrix ? 1.0 : imageSize.width(), repeatV = matrix ? 1.0 : imageSize.height();
		const auto faces = allFaces(map);
		for (const bool width : {true, false}) {
			const auto request = width ? fit(1, 0, true) : fit(0, 1, true);
			LevelSurfaceEditPlan plan;
			LevelMapDocument document = map;
			expect(prepareLevelSurfaceEdits(document, faces, {}, {request}, sizes, &plan, &error) && plan.faceCount() == 6
					&& commitLevelSurfaceEdit(&document, plan, &error) && document.undoStack.size() == 1,
				width ? "Fit Width changes every face in one undo step" : "Fit Height changes every face in one undo step", error);
			const auto only = edited(source, width ? fit(1, 0, false) : fit(0, 1, false), sizes);
			LevelMapDocument onlyMap;
			load(only, &onlyMap);
			for (int i = 0; i < 6; ++i) {
				const auto before = extent(map.brushes.first(), i), after = extent(document.brushes.first(), i), kept = extent(onlyMap.brushes.first(), i);
				const QString detail = QStringLiteral("face %1: %2..%3 x %4..%5, before %6 x %7")
					.arg(i + 1).arg(after.minU).arg(after.maxU).arg(after.minV).arg(after.maxV).arg(before.width()).arg(before.height());
				if (width) {
					expect(near(after.minU, 0, 1e-3) && near(after.width(), repeatU, 1e-3), "one repeat across U from the face's edge", detail);
					expect(near(kept.minV, before.minV, 1e-3) && near(kept.height(), before.height(), 1e-3), "Width Only keeps V", detail);
				} else {
					expect(near(after.minV, 0, 1e-3) && near(after.height(), repeatV, 1e-3), "one repeat down V from the face's edge", detail);
					expect(near(kept.minU, before.minU, 1e-3) && near(kept.width(), before.width(), 1e-3), "Height Only keeps U", detail);
				}
				// The other axis starts at the face's edge too, scaled as much.
				expect(near(width ? after.minV : after.minU, 0, 1e-3)
						&& near(after.height() / after.width(), before.height() / before.width(), 1e-4 * std::max(1.0, before.height() / before.width())),
					"the texture keeps its proportions", detail);
			}
			expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == source, "undo restores the exact source", error);
		}
		// With both counts set, keeping proportions is the plain fit.
		expect(edited(source, fit(2, 3, true), sizes) == edited(source, fit(2, 3, false), sizes), "a fit of both axes ignores keep proportions");
		if (!matrix) {
			expect(edited(source, fit(1, 0, true), {}).startsWith("refused"), "a texel fit needs the image size");
		}
	}

	phase = QStringLiteral("fit patch");
	{
		const QByteArray source = patchFixture(QStringLiteral("cylinder"));
		LevelMapDocument map;
		load(source, &map);
		const int patchId = map.patches.first().id;
		// A spread with V half as dense as U.
		auto stretched = map;
		for (double& v : stretched.patches.first().controlV) {
			v *= 3;
		}
		stretched.patches.first().definitionDirty = stretched.patches.first().geometryDirty = true;
		const auto bytes = serializeLevelMap(stretched).bytes;
		LevelMapDocument start;
		load(bytes, &start);
		const auto before = patchExtent(start.patches.first());
		for (const bool width : {true, false}) {
			LevelMapDocument after;
			load(edited(bytes, width ? fit(2, 0, true) : fit(0, 2, true), {}, {}, {patchId}, false), &after);
			const auto fitted = patchExtent(after.patches.first());
			const QString detail = QStringLiteral("%1..%2 x %3..%4").arg(fitted.minU).arg(fitted.maxU).arg(fitted.minV).arg(fitted.maxV);
			expect(near(fitted.minU, 0, 1e-6) && near(fitted.minV, 0, 1e-6) && near(width ? fitted.width() : fitted.height(), 2, 1e-6)
					&& near(fitted.height() / fitted.width(), before.height() / before.width(), 1e-4),
				"a patch fits two repeats and keeps its proportions, without needing the image", detail);
			LevelMapDocument only;
			load(edited(bytes, width ? fit(2, 0, false) : fit(0, 2, false), {}, {}, {patchId}, false), &only);
			const auto kept = patchExtent(only.patches.first());
			expect(width ? near(kept.minV, before.minV) && near(kept.height(), before.height()) : near(kept.minU, before.minU) && near(kept.width(), before.width()),
				"Width Only and Height Only keep the other axis", detail);
		}
	}

	for (const auto* dialect : {"classic", "valve220", "brushDef", "brushDef3"}) {
		phase = QStringLiteral("project axial ") + QString::fromLatin1(dialect);
		const QByteArray fixture = tests::surfaceFixture(QString::fromLatin1(dialect));
		LevelMapDocument map;
		if (!load(fixture, &map)) {
			return 1;
		}
		const auto source = serializeLevelMap(map).bytes;
		const bool matrix = map.brushes.first().faces.first().explicitTextureMatrix;
		LevelMapDocument axial, natural;
		load(edited(source, operation(LevelSurfaceOperation::ProjectAxial, 0.5), sizes), &axial);
		load(edited(source, operation(LevelSurfaceOperation::Natural, 0.5), sizes), &natural);
		if (!expect(axial.brushes.size() == 1 && natural.brushes.size() == 1, "both edits apply")) {
			continue;
		}
		for (int i = 0; i < 6; ++i) {
			const auto& a = axial.brushes.first().faces[i];
			const auto& n = natural.brushes.first().faces[i];
			if (!matrix) {
				// Classic and Valve 220 faces take Natural's projection exactly.
				expect(a.shiftX == n.shiftX && a.shiftY == n.shiftY && a.rotation == n.rotation && a.scaleX == n.scaleX && a.scaleY == n.scaleY
						&& same(a.uAxis, n.uAxis) && same(a.vAxis, n.vAxis) && a.uOffset == n.uOffset && a.vOffset == n.vOffset,
					"Project Axial is Natural's projection", QString::number(i + 1));
			} else {
				// The same matrix, its offsets within one repeat.
				const double du = a.textureMatrix[2] - n.textureMatrix[2], dv = a.textureMatrix[5] - n.textureMatrix[5];
				expect(near(a.textureMatrix[0], n.textureMatrix[0]) && near(a.textureMatrix[1], n.textureMatrix[1]) && near(a.textureMatrix[3], n.textureMatrix[3])
						&& near(a.textureMatrix[4], n.textureMatrix[4]) && a.textureMatrix[2] >= 0 && a.textureMatrix[2] < 1 && a.textureMatrix[5] >= 0
						&& a.textureMatrix[5] < 1 && near(du, std::round(du), 1e-5) && near(dv, std::round(dv), 1e-5),
					"Project Axial is Natural's matrix with offsets within one repeat",
					QStringLiteral("%1 %2").arg(a.textureMatrix[2]).arg(a.textureMatrix[5]));
			}
		}
		if (matrix) {
			expect(edited(source, operation(LevelSurfaceOperation::ProjectAxial, 0.5), {}).contains("image size"), "brush primitives need the image size");
		}
	}

	phase = QStringLiteral("project axial patch");
	{
		const QByteArray plane = patchFixture(QStringLiteral("plane"));
		LevelMapDocument map;
		load(plane, &map);
		const int patchId = map.patches.first().id;
		LevelMapDocument after;
		load(edited(plane, operation(LevelSurfaceOperation::ProjectAxial, 0.5), sizes, {}, {patchId}, false), &after);
		const auto& patch = after.patches.first();
		bool exact = patch.controlPoints.size() == patch.controlU.size();
		for (int i = 0; i < patch.controlPoints.size() && exact; ++i) {
			// Along Z: U along X and V down Y, 64 map units a repeat of 128 by 64 texels.
			exact = near(patch.controlU[i], patch.controlPoints[i].x / 64) && near(patch.controlV[i], -patch.controlPoints[i].y / 32);
		}
		expect(exact, "a flat patch is projected along the world axis it faces");
		const QByteArray cylinder = patchFixture(QStringLiteral("cylinder"));
		LevelMapDocument round;
		load(cylinder, &round);
		const int roundId = round.patches.first().id;
		const auto axial = edited(cylinder, operation(LevelSurfaceOperation::ProjectAxial, 0.5), sizes, {}, {roundId}, false);
		const auto natural = edited(cylinder, operation(LevelSurfaceOperation::Natural, 0.5), sizes, {}, {roundId}, false);
		expect(!axial.startsWith("refused") && !natural.startsWith("refused") && axial != natural, "on a cylinder Project Axial differs from Natural's spread",
			QString::fromUtf8(axial.left(80)));
		expect(edited(plane, operation(LevelSurfaceOperation::ProjectAxial, 0.5), {}, {}, {patchId}, false).contains("image size"),
			"a patch projection needs the image size");
		LevelSurfaceEditPlan plan;
		const auto source = serializeLevelMap(map).bytes;
		expect(prepareLevelSurfaceEdits(map, {}, {patchId}, {operation(LevelSurfaceOperation::ProjectAxial, 0.5)}, sizes, &plan, &error) && plan.patchCount() == 1
				&& commitLevelSurfaceEdit(&map, plan, &error) && undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == source,
			"the patch projection undoes exactly", error);
	}

	for (const auto* dialect : {"classic", "valve220", "brushDef", "brushDef3"}) {
		phase = QStringLiteral("project direction ") + QString::fromLatin1(dialect);
		const QByteArray fixture = tests::surfaceFixture(QString::fromLatin1(dialect));
		LevelMapDocument map;
		if (!load(fixture, &map)) {
			return 1;
		}
		const auto source = serializeLevelMap(map).bytes;
		const auto& firstFace = map.brushes.first().faces.first();
		const bool matrix = firstFace.explicitTextureMatrix, valve = firstFace.explicitTextureAxes;
		const auto faces = allFaces(map);
		// From the Top view: the floor and ceiling take it, the walls run along it.
		LevelSurfaceEditPlan plan;
		LevelMapDocument document = map;
		expect(prepareLevelSurfaceEdits(document, faces, {}, {project({0, 0, 1, true}, 0.5)}, sizes, &plan, &error) && plan.faceCount() == 2
				&& plan.skippedEdgeOnCount() == 4 && plan.skippedClassicCount() == 0 && commitLevelSurfaceEdit(&document, plan, &error)
				&& document.undoStack.size() == 1,
			"from the top, two faces change and four run along the projection",
			QStringLiteral("%1 changed, %2 edge-on, %3 classic: %4").arg(plan.faceCount()).arg(plan.skippedEdgeOnCount()).arg(plan.skippedClassicCount()).arg(error));
		// Checks the faces `direction` reaches (only floors and ceilings when
		// `floors`) show its projection at `scale`.
		const auto check = [&](const LevelMapDocument& edited, const LevelMapVec3& direction, double scale, bool floors, const char* label) {
			const auto [s, t] = levelTexturePrimitiveBasis(direction);
			const double w = matrix ? imageSize.width() : 1.0, h = matrix ? imageSize.height() : 1.0;
			const auto geometry = solveBrushGeometry(edited.brushes.first().faces, edited.brushes.first().id, edited.brushes.first().entityId);
			for (int i = 0; i < 6; ++i) {
				const auto& face = edited.brushes.first().faces[i];
				if (std::abs(dot(faceNormal(face), direction)) < 1e-6 || (floors && std::abs(faceNormal(face).z) < 0.5)) {
					continue;
				}
				if (valve) {
					expect(same(face.uAxis, s) && same(face.vAxis, t) && face.scaleX == scale && face.scaleY == scale && face.uOffset == 0 && face.vOffset == 0
							&& face.rotation == 0,
						"a Valve 220 face takes the direction's axes at the default scale", QString::number(i + 1));
				}
				// Every format: the texture over the face is the projection along
				// the direction, up to whole repeats on brush primitives.
				const auto projection = levelTextureProjection(face);
				double offsetU = 0, offsetV = 0;
				bool first = true, consistent = true;
				for (const auto& point : geometry.faces[i].points) {
					const QPointF uv = projection.at(point);
					const double du = uv.x() - dot(s, point) / (scale * w), dv = uv.y() - dot(t, point) / (scale * h);
					if (first) {
						offsetU = du;
						offsetV = dv;
						first = false;
					}
					consistent &= near(du, offsetU, 1e-4) && near(dv, offsetV, 1e-4);
				}
				expect(consistent && (matrix ? near(offsetU, std::round(offsetU), 1e-4) && near(offsetV, std::round(offsetV), 1e-4)
											   : near(offsetU, 0, 1e-3) && near(offsetV, 0, 1e-3)),
					label, QStringLiteral("face %1 offsets %2 %3").arg(i + 1).arg(offsetU).arg(offsetV));
			}
		};
		check(document, {0, 0, 1, true}, 0.5, false, "the faces show the projection from the top");
		for (int i = 0; i < 6; ++i) {
			if (std::abs(faceNormal(map.brushes.first().faces[i]).z) < 0.5) {
				const auto& before = map.brushes.first().faces[i];
				const auto& after = document.brushes.first().faces[i];
				expect(levelTextureProjection(before).u.x == levelTextureProjection(after).u.x && before.shiftX == after.shiftX
						&& before.textureMatrix == after.textureMatrix && before.uOffset == after.uOffset,
					"a wall the projection runs along keeps its mapping", QString::number(i + 1));
			}
		}
		expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == source, "undo restores the exact source", error);

		// Along a diagonal: no face runs along it. Classic walls cannot hold it.
		document = map;
		const bool classic = !matrix && !valve;
		expect(prepareLevelSurfaceEdits(document, faces, {}, {project({2, 2, 2, true}, 1)}, sizes, &plan, &error)
				&& plan.faceCount() == (classic ? 2 : 6) && plan.skippedClassicCount() == (classic ? 4 : 0) && plan.skippedEdgeOnCount() == 0
				&& commitLevelSurfaceEdit(&document, plan, &error),
			"along a diagonal every face changes, but classic walls",
			QStringLiteral("%1 changed, %2 classic: %3").arg(plan.faceCount()).arg(plan.skippedClassicCount()).arg(error));
		check(document, diagonal, 1, classic, "the faces show the diagonal projection");

		// Only the walls from the top: nothing to change, so it is refused.
		QVector<LevelSurfaceFace> walls;
		for (int i = 0; i < 6; ++i) {
			if (std::abs(faceNormal(map.brushes.first().faces[i]).z) < 0.5) {
				walls.append({map.brushes.first().id, i});
			}
		}
		const auto refused = edited(source, project({0, 0, 1, true}, 0.5), sizes, walls);
		expect(refused.startsWith("refused") && refused.contains("runs along every"), "walls alone from the top are refused", QString::fromUtf8(refused));
		if (classic) {
			const auto classicRefused = edited(source, project(diagonal, 1), sizes, walls);
			expect(classicRefused.contains("Classic faces cannot hold"), "classic walls alone along the diagonal are refused", QString::fromUtf8(classicRefused));
		}
		if (matrix) {
			expect(edited(source, project({0, 0, 1, true}, 0.5), {}).contains("image size"), "brush primitives need the image size");
		}
		// A locked brush refuses the projection.
		load(source, &document);
		QString layer;
		expect(createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
				&& assignLevelSceneObjects(&document, layer, {QStringLiteral("brush:0")}, &error) && setLevelSceneLocked(&document, layer, true, &error),
			"lock the brush", error);
		const auto locked = serializeLevelMap(document).bytes;
		expect(prepareLevelSurfaceEdits(document, faces, {}, {project({0, 0, 1, true}, 0.5)}, sizes, &plan, &error) && !commitLevelSurfaceEdit(&document, plan, &error)
				&& serializeLevelMap(document).bytes == locked,
			"a locked brush refuses the projection", error);
	}

	phase = QStringLiteral("project direction patch");
	{
		const QByteArray plane = patchFixture(QStringLiteral("plane"));
		LevelMapDocument map;
		load(plane, &map);
		const int patchId = map.patches.first().id;
		LevelMapDocument after;
		LevelSurfaceEditPlan plan;
		load(edited(plane, project({0, 0, 5, true}, 0.5), sizes, {}, {patchId}, false, &plan), &after);
		const auto& patch = after.patches.first();
		bool exact = plan.patchCount() == 1 && patch.controlPoints.size() == patch.controlU.size();
		for (int i = 0; i < patch.controlPoints.size() && exact; ++i) {
			// ComputeAxisBase from above: U along Y and V along X.
			exact = near(patch.controlU[i], patch.controlPoints[i].y / 64) && near(patch.controlV[i], patch.controlPoints[i].x / 32);
		}
		expect(exact, "a flat patch takes the projection from above");
		const auto refused = edited(plane, project({1, 0, 0, true}, 0.5), sizes, {}, {patchId}, false);
		expect(refused.contains("runs along every"), "a flat patch the projection runs along is refused", QString::fromUtf8(refused));
		expect(edited(plane, project({0, 0, 0, true}, 0.5), sizes, {}, {patchId}, false).startsWith("refused"), "a zero direction is refused");
		const QByteArray cylinder = patchFixture(QStringLiteral("cylinder"));
		LevelMapDocument round;
		load(cylinder, &round);
		const int roundId = round.patches.first().id;
		LevelMapDocument roundAfter;
		load(edited(cylinder, project({0, 0, 1, true}, 0.5), sizes, {}, {roundId}, false), &roundAfter);
		const auto& projected = roundAfter.patches.first();
		bool flat = projected.controlU.size() == projected.controlPoints.size();
		for (int i = 0; i < projected.controlPoints.size() && flat; ++i) {
			flat = near(projected.controlU[i], projected.controlPoints[i].y / 64) && near(projected.controlV[i], projected.controlPoints[i].x / 32);
		}
		expect(flat, "a cylinder standing on Z is projected flat from above");
	}

	if (argc > 1) {
		phase = QStringLiteral("cli");
		const QString binary = QString::fromLocal8Bit(argv[1]);
		const auto run = [&](const QString& map, const QStringList& arguments, int* exitCode, bool optionsFirst = false) {
			QProcess process;
			process.start(binary, QStringList {QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("align-textures")}
				+ (optionsFirst ? arguments + QStringList {map} : QStringList {map} + arguments)
				+ QStringList {QStringLiteral("--json")});
			const bool ended = process.waitForFinished(60000);
			*exitCode = ended && process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
			const auto bytes = process.readAllStandardOutput();
			if (*exitCode != 0) {
				std::cerr << bytes.constData() << process.readAllStandardError().constData() << std::endl;
			}
			return QJsonDocument::fromJson(bytes).object();
		};
		const auto loaded = [](const QString& path) {
			LevelMapDocument document;
			QString problem;
			expect(loadLevelMap({path, {}, QStringLiteral("idtech3")}, &document, &problem), "map loads", problem);
			return document;
		};
		const auto out = [&](const char* name) { return QDir(temp.path()).filePath(QString::fromLatin1(name)); };
		const auto valve = out("valve.map"), classic = out("classic.map");
		tests::putMaterialFile(valve, tests::surfaceFixture(QStringLiteral("valve220")));
		tests::putMaterialFile(classic, tests::surfaceFixture(QStringLiteral("classic")));
		const QStringList brush {QStringLiteral("--engine"), QStringLiteral("idTech3"), QStringLiteral("--object"), QStringLiteral("brush:0")};
		// What the shared service writes for `request` on brush 0.
		const auto expected = [&](const QString& path, LevelSurfaceRequest request, bool defaultScale) {
			auto document = loaded(path);
			if (defaultScale) {
				request.x = request.y = levelSurfaceDefaultScale(document);
			}
			setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 0}});
			LevelSurfaceEditPlan plan;
			QString problem;
			expect(prepareLevelSurfaceEdits(document, levelMapSelectedSurfaces(document), {}, {request}, sizes, &plan, &problem)
					&& commitLevelSurfaceEdit(&document, plan, &problem),
				"reference edit", problem);
			return serializeLevelMap(document).bytes;
		};
		const auto saved = [&](const char* name) { return serializeLevelMap(loaded(out(name))).bytes; };
		int code = -1;
		auto json = run(valve, brush + QStringList {QStringLiteral("--fit"), QStringLiteral("1,0"), QStringLiteral("--keep-proportions"), QStringLiteral("--texture-size"),
			QStringLiteral("128,64"), QStringLiteral("--output"), out("fit-width.map")}, &code);
		expect(code == 0 && json.value(QStringLiteral("keepProportions")).toBool() && json.value(QStringLiteral("facesChanged")).toInt() == 6
				&& saved("fit-width.map") == expected(valve, fit(1, 0, true), false),
			"--fit 1,0 --keep-proportions writes Fit Width", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
		json = run(valve, brush + QStringList {QStringLiteral("--keep-proportions"), QStringLiteral("--fit"), QStringLiteral("1,0"),
			QStringLiteral("--texture-size"), QStringLiteral("128,64"), QStringLiteral("--output"), out("fit-options-first.map")}, &code, true);
		expect(code == 0 && saved("fit-options-first.map") == saved("fit-width.map"), "fit options may precede the map path");
		json = run(valve, brush + QStringList {QStringLiteral("--project"), QStringLiteral("z"), QStringLiteral("--output"), out("top.map")}, &code);
		const auto direction = json.value(QStringLiteral("direction")).toArray();
		expect(code == 0 && json.value(QStringLiteral("projection")).toString() == QStringLiteral("z") && json.value(QStringLiteral("facesChanged")).toInt() == 2
				&& json.value(QStringLiteral("skippedEdgeOn")).toInt() == 4 && json.value(QStringLiteral("skippedClassic")).toInt() == 0 && direction.size() == 3
				&& direction.at(2).toDouble() == 1 && saved("top.map") == expected(valve, project({0, 0, 1, true}, 1), true),
			"--project z writes the projection from the top", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
		json = run(valve, brush + QStringList {QStringLiteral("--project"), QStringLiteral("axial"), QStringLiteral("--output"), out("axial.map")}, &code);
		expect(code == 0 && json.value(QStringLiteral("projection")).toString() == QStringLiteral("axial") && json.value(QStringLiteral("facesChanged")).toInt() == 6
				&& saved("axial.map") == expected(valve, operation(LevelSurfaceOperation::ProjectAxial, 1), true),
			"--project axial writes Project Axial", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
		json = run(valve, brush + QStringList {QStringLiteral("--project-direction"), QStringLiteral("1,1,1"), QStringLiteral("--project-scale"), QStringLiteral("0.25,0.5"),
			QStringLiteral("--output"), out("diagonal.map")}, &code);
		auto diagonalRequest = project({1, 1, 1, true}, 0.25);
		diagonalRequest.y = 0.5;
		expect(code == 0 && json.value(QStringLiteral("projection")).toString() == QStringLiteral("direction") && json.value(QStringLiteral("facesChanged")).toInt() == 6
				&& near(json.value(QStringLiteral("scale")).toArray().at(1).toDouble(), 0.5) && saved("diagonal.map") == expected(valve, diagonalRequest, false),
			"--project-direction with --project-scale", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
		json = run(valve, brush + QStringList {QStringLiteral("--project-direction"), QStringLiteral("1,1,1"), QStringLiteral("--project-scale"),
			QStringLiteral("0.25,0.5"), QStringLiteral("--output"), out("project-options-first.map")}, &code, true);
		expect(code == 0 && saved("project-options-first.map") == saved("diagonal.map"), "projection options may precede the map path");
		json = run(valve, brush + QStringList {QStringLiteral("--project"), QStringLiteral("camera"), QStringLiteral("--camera-yaw"), QStringLiteral("30"),
			QStringLiteral("--camera-pitch"), QStringLiteral("-40"), QStringLiteral("--output"), out("camera.map")}, &code);
		LevelMapVec3 forward, right, up;
		levelCameraBasis(30, -40, &forward, &right, &up);
		expect(code == 0 && json.value(QStringLiteral("projection")).toString() == QStringLiteral("camera")
				&& saved("camera.map") == expected(valve, project(scaled(forward, -1), 1), true),
			"--project camera projects back towards the camera", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
		json = run(classic, brush + QStringList {QStringLiteral("--project-direction"), QStringLiteral("1,1,1"), QStringLiteral("--output"), out("classic-diagonal.map")}, &code);
		expect(code == 0 && json.value(QStringLiteral("skippedClassic")).toInt() == 4 && json.value(QStringLiteral("facesChanged")).toInt() == 2,
			"classic walls keep their mapping and are counted", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
		// The walls, as one-based face numbers.
		QStringList walls {QStringLiteral("--engine"), QStringLiteral("idTech3")};
		{
			const auto document = loaded(valve);
			for (int i = 0; i < document.brushes.first().faces.size(); ++i) {
				if (std::abs(faceNormal(document.brushes.first().faces[i]).z) < 0.5) {
					walls << QStringLiteral("--face") << QStringLiteral("0:%1").arg(i + 1);
				}
			}
			expect(walls.size() == 10, "four walls", walls.join(QLatin1Char(' ')));
		}
		const struct {
			QString map;
			QStringList arguments;
			const char* label;
		} refusals[] {
			{valve, brush + QStringList {QStringLiteral("--fit"), QStringLiteral("1,1"), QStringLiteral("--keep-proportions")}, "keep proportions with both counts"},
			{valve, brush + QStringList {QStringLiteral("--shift"), QStringLiteral("1,1"), QStringLiteral("--keep-proportions")}, "keep proportions without --fit"},
			{valve, brush + QStringList {QStringLiteral("--project"), QStringLiteral("sideways")}, "an unknown projection"},
			{valve, brush + QStringList {QStringLiteral("--project-direction"), QStringLiteral("0,0,0")}, "a zero direction"},
			{valve, brush + QStringList {QStringLiteral("--project-direction"), QStringLiteral("1,1")}, "a direction of two values"},
			{valve, brush + QStringList {QStringLiteral("--project"), QStringLiteral("z"), QStringLiteral("--project-scale"), QStringLiteral("0,1")}, "a zero scale"},
			{valve, brush + QStringList {QStringLiteral("--shift"), QStringLiteral("1,1"), QStringLiteral("--project-scale"), QStringLiteral("1,1")}, "a scale without a projection"},
			{valve, brush + QStringList {QStringLiteral("--project"), QStringLiteral("z"), QStringLiteral("--camera-yaw"), QStringLiteral("10")}, "a camera angle with an axis"},
			{valve, brush + QStringList {QStringLiteral("--project"), QStringLiteral("z"), QStringLiteral("--project-direction"), QStringLiteral("1,0,0")}, "two projections"},
			{valve, walls + QStringList {QStringLiteral("--project"), QStringLiteral("z")}, "faces the projection runs along"},
		};
		for (const auto& refusal : refusals) {
			QFile::remove(out("refused.map"));
			run(refusal.map, refusal.arguments + QStringList {QStringLiteral("--output"), out("refused.map")}, &code);
			expect(code != 0 && !QFile::exists(out("refused.map")), "refused", QString::fromLatin1(refusal.label));
		}
	}
	std::cout << (ok ? "Texture projection checks passed" : "Texture projection checks failed") << std::endl;
	return ok ? 0 : 1;
}
