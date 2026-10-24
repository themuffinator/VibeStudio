#include "core/level_brush.h"
#include "core/level_texture_mapping.h"
#include <QCoreApplication>
#include <cmath>
#include <algorithm>
#include <limits>

namespace vibestudio {
namespace {
bool fail(QString* error, const char* message)
{
	if (error) { *error = QCoreApplication::translate("LevelBrushExtrusion",message); }
	return false;
}
bool finish(LevelMapBrush* brush, QString* error)
{
	LevelBrushTopology topology;
	if (!levelBrushTopology(*brush,&topology,error)) { return false; }
	brush->mins = topology.geometry.mins; brush->maxs = topology.geometry.maxs;
	brush->boundsSolved = true; brush->faceCount = static_cast<int>(brush->faces.size()); brush->geometryDirty = true;
	return true;
}
bool reverseFace(LevelMapBrushFace* face, QString* error)
{
	const auto projection = levelTextureProjection(*face);
	std::swap(face->p0,face->p2);
	if (face->explicitPlane) {
		face->planeNormal.x = -face->planeNormal.x; face->planeNormal.y = -face->planeNormal.y; face->planeNormal.z = -face->planeNormal.z;
		face->planeDistance = -face->planeDistance;
	}
	// The two sides of a split retain the same world projection even when
	// brush primitives need a different matrix for the reversed normal.
	if (!setLevelTextureProjection(face,projection,false,error)) { return false; }
	face->textureParametersDirty = true;
	return true;
}
}

double snapLevelBrushExtrusionDistance(double distance, const LevelMapVec3& normal,
	const QVector<LevelMapVec3>& vertices, double grid)
{
	const double length = normal.x*normal.x + normal.y*normal.y + normal.z*normal.z;
	if (!normal.valid || !std::isfinite(distance) || !std::isfinite(grid) || grid < 0 || grid > 32768
		|| !std::isfinite(length) || std::abs(length-1) > 1e-5) { return std::numeric_limits<double>::quiet_NaN(); }
	if (grid == 0) { return distance; }
	double best = std::round(distance/grid)*grid;
	const double axes[]{normal.x,normal.y,normal.z};
	for (const auto& vertex : vertices) {
		const double point[]{vertex.x,vertex.y,vertex.z};
		for (int axis = 0; axis < 3; ++axis) {
			if (!vertex.valid || !std::isfinite(point[axis])) { return std::numeric_limits<double>::quiet_NaN(); }
			if (std::abs(axes[axis]) < 1e-6) { continue; }
			const double target = std::round((point[axis]+axes[axis]*distance)/grid)*grid;
			const double candidate = (target-point[axis])/axes[axis];
			if (std::abs(candidate-distance) < std::abs(best-distance)-1e-9) { best = candidate; }
		}
	}
	return best;
}

// Original VibeStudio plane-operation implementation. Behaviour reference:
// TrenchBroom 2026.2 manual, Extrusion (reviewed 2026-10-18); see CREDITS.md.
bool prepareLevelBrushExtrusion(const LevelMapBrush& brush, int face, double distance, bool split, bool textureLock,
	LevelBrushExtrusion* result, QString* error)
{
	if (error) { error->clear(); }
	if (result) { *result = {}; }
	if (!result || !std::isfinite(distance) || std::abs(distance) > 32768 || face < 0 || face >= brush.faces.size()) {
		return fail(error,QT_TRANSLATE_NOOP("LevelBrushExtrusion","Choose an existing brush face and a finite extrusion from −32768 to 32768 units."));
	}
	LevelBrushTopology before;
	if (!levelBrushTopology(brush,&before,error)) { return false; }
	if (distance == 0) { result->brushes = {brush}; result->activeFace = face; return true; }
	if (split && std::abs(distance) < 0.02) {
		return fail(error,QT_TRANSLATE_NOOP("LevelBrushExtrusion","A split needs at least 0.02 units between its cap planes."));
	}
	const auto plane = before.geometry.faces[face].plane;
	const LevelMapVec3 offset{plane.normalX*distance,plane.normalY*distance,plane.normalZ*distance,true};
	auto moved = brush;
	auto& target = moved.faces[face];
	for (auto* p : {&target.p0,&target.p1,&target.p2}) { p->x += offset.x; p->y += offset.y; p->z += offset.z; }
	if (target.explicitPlane) {
		target.planeDistance -= target.planeNormal.x*offset.x + target.planeNormal.y*offset.y + target.planeNormal.z*offset.z;
	}
	if (textureLock) {
		LevelTextureAffine transform; transform.translation = offset;
		if (!lockTransformedLevelTexture(brush.faces[face],&target,transform,false,error)) { return false; }
		target.textureParametersDirty = true;
	}
	// This intentionally does not rebuild a hull or discard inactive planes:
	// extrusion stops before any original face disappears.
	if (!finish(&moved,error)) { return false; }
	LevelBrushExtrusion made; made.changed = true; made.activeFace = face;
	if (!split) { made.brushes = {moved}; *result = std::move(made); return true; }
	auto slab = distance > 0 ? moved : brush;
	auto cap = distance > 0 ? brush.faces[face] : moved.faces[face];
	if (!reverseFace(&cap,error)) { return false; }
	slab.faces.append(cap);
	const auto solved = solveBrushGeometry(slab.faces);
	if (!solved.solved || solved.faces.size() != slab.faces.size() || !solved.faces.last().isValid()) {
		return fail(error,QT_TRANSLATE_NOOP("LevelBrushExtrusion","The extrusion could not form a closed split brush."));
	}
	QVector<LevelMapBrushFace> active;
	int activeFace = -1;
	for (int i = 0; i < slab.faces.size(); ++i) {
		if (!solved.faces[i].isValid()) { continue; }
		if (i == face) { activeFace = static_cast<int>(active.size()); }
		active.append(slab.faces[i]);
	}
	slab.faces = std::move(active);
	if (activeFace < 0 || !finish(&slab,error)) { return false; }
	made.brushes = {distance > 0 ? brush : moved,slab};
	made.activeBrush = distance > 0 ? 1 : 0;
	made.activeFace = distance > 0 ? activeFace : face;
	*result = std::move(made);
	return true;
}
} // namespace vibestudio
