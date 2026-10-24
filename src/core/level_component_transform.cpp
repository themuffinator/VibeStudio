#include "core/level_brush.h"
#include "core/level_patch.h"
#include "core/level_component_selection.h"
#include "core/level_scene_locks.h"
#include "core/level_placement_control_p.h"
#include "core/level_repeat_p.h"
#include <QCoreApplication>
#include <algorithm>
#include <cmath>

namespace vibestudio {
namespace {
bool fail(QString* error, const char* text)
{
	if (error) { *error = QCoreApplication::translate("LevelComponents",text); } return false;
}
bool finite(const LevelMapVec3& p, double limit = 32768)
{
	return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
		std::abs(p.x) <= limit && std::abs(p.y) <= limit && std::abs(p.z) <= limit;
}
bool selectedPoints(const LevelMapDocument& document, const LevelMapComponentSet& set, QVector<LevelMapVec3>* points, QString* error)
{
	if (set.components.isEmpty()) { return fail(error,QT_TRANSLATE_NOOP("LevelComponents","Select existing components to transform.")); }
	if (set.object.kind == LevelMapSelectionKind::QuakeBrush) {
		const auto found = std::find_if(document.brushes.cbegin(),document.brushes.cend(),[&](const auto& b) { return b.id == set.object.objectId; });
		LevelBrushTopology topology;
		if (found == document.brushes.cend() || !levelBrushTopology(*found,&topology,error)) { return false; }
		const auto vertices = levelBrushComponentVertices(topology,set.kind,set.components);
		if (vertices.isEmpty()) { return fail(error,QT_TRANSLATE_NOOP("LevelComponents","A selected brush component no longer exists.")); }
		for (int vertex : vertices) { *points << topology.vertices[vertex]; }
		return true;
	}
	if (set.object.kind == LevelMapSelectionKind::QuakePatch && set.kind == LevelBrushComponent::Vertex) {
		const auto found = std::find_if(document.patches.cbegin(),document.patches.cend(),[&](const auto& p) { return p.id == set.object.objectId; });
		if (found == document.patches.cend() || !validateLevelPatch(*found,error)) { return false; }
		for (int vertex : set.components) {
			if (vertex < 0 || vertex >= found->controlPoints.size()) { return fail(error,QT_TRANSLATE_NOOP("LevelComponents","A selected patch control point no longer exists.")); }
			*points << found->controlPoints[vertex];
		}
		return true;
	}
	return fail(error,QT_TRANSLATE_NOOP("LevelComponents","Choose brush vertices, edges or faces, or patch control points."));
}
}

LevelMapVec3 transformedLevelComponentPoint(const LevelMapVec3& p, const LevelTextureAffine& matrix, double grid)
{
	const auto& a = matrix.columns;
	return snapLevelMapPosition({a[0].x*p.x+a[1].x*p.y+a[2].x*p.z+matrix.translation.x,
		a[0].y*p.x+a[1].y*p.y+a[2].y*p.z+matrix.translation.y,a[0].z*p.x+a[1].z*p.y+a[2].z*p.z+matrix.translation.z,true},grid);
}

bool levelMapComponentBounds(const LevelMapDocument& document, const QVector<LevelMapComponentSet>& sets,
	LevelMapVec3* mins, LevelMapVec3* maxs, QString* error)
{
	if (error) { error->clear(); }
	if (!mins || !maxs || sets.isEmpty() || sets.size() > 256) { return fail(error,QT_TRANSLATE_NOOP("LevelComponents","Choose components of 1 to 256 brushes and patches.")); }
	QSet<QString> seen; LevelMapVec3 lo,hi;
	for (const auto& set : sets) {
		detail::placementCancellationCheckpoint();
		const auto id = levelMapSelectionRefId(set.object);
		if (seen.contains(id) || set.components.size() > 4096) { return fail(error,QT_TRANSLATE_NOOP("LevelComponents","An object is named twice or has too many selected components.")); }
		seen.insert(id);
		QVector<LevelMapVec3> points;
		if (!selectedPoints(document,set,&points,error)) {
			if (error) { *error = QCoreApplication::translate("LevelComponents","%1: %2").arg(id,error->isEmpty() ? QCoreApplication::translate("LevelComponents","Object not found.") : *error); }
			return false;
		}
		for (const auto& p : points) {
			if (!finite(p)) { return fail(error,QT_TRANSLATE_NOOP("LevelComponents","A component is outside the map's finite coordinate range.")); }
			if (!lo.valid) { lo = hi = p; }
			else { lo.x = std::min(lo.x,p.x); lo.y = std::min(lo.y,p.y); lo.z = std::min(lo.z,p.z);
				hi.x = std::max(hi.x,p.x); hi.y = std::max(hi.y,p.y); hi.z = std::max(hi.z,p.z); }
		}
	}
	*mins = lo; *maxs = hi; return lo.valid;
}

bool levelMapVertexSnapDelta(const LevelMapDocument& document,const QVector<LevelMapComponentSet>& sets,
	const LevelMapSelectionRef& target,int vertex,LevelMapVec3* delta,QString* error)
{
	// Original implementation of the quick-snap behaviour in TrenchBroom
	// v2026.2 VertexToolController.cpp (GPL-3.0-or-later); see docs/CREDITS.md.
	if (error) { error->clear(); }
	if (!delta || target.kind != LevelMapSelectionKind::QuakeBrush || vertex < 0 || sets.isEmpty()
		|| std::any_of(sets.cbegin(),sets.cend(),[](const auto& set) { return set.object.kind != LevelMapSelectionKind::QuakeBrush || set.kind != LevelBrushComponent::Vertex; })) {
		return fail(error,QT_TRANSLATE_NOOP("LevelComponents","Pick one brush vertex and choose an existing brush vertex as the snap target."));
	}
	LevelMapVec3 lo,hi,targetLo,targetHi;
	if (!levelMapComponentBounds(document,sets,&lo,&hi,error)) { return false; }
	if (levelComponentGeometryKey({lo}) != levelComponentGeometryKey({hi})) {
		return fail(error,QT_TRANSLATE_NOOP("LevelComponents","Pick exactly one geometric vertex. Coincident vertices in different brushes may be picked together."));
	}
	if (!levelMapComponentBounds(document,{{target,LevelBrushComponent::Vertex,{vertex}}},&targetLo,&targetHi,error)) { return false; }
	const LevelMapVec3 result{targetLo.x-lo.x,targetLo.y-lo.y,targetLo.z-lo.z,true};
	if (!finite(result,65536)) { return fail(error,QT_TRANSLATE_NOOP("LevelComponents","The vertex snap displacement is outside the map's coordinate range.")); }
	*delta = result; return true;
}

bool levelComponentTransformMatrix(const LevelComponentTransformRequest& request, const LevelMapVec3& defaultPivot,
	LevelTextureAffine* matrix, QString* error)
{
	if (error) { error->clear(); }
	const auto pivot = request.pivot.valid ? request.pivot : defaultPivot;
	if (!matrix || !finite(pivot) || !std::isfinite(request.grid) || request.grid < 0 || request.grid > 32768) {
		return fail(error,QT_TRANSLATE_NOOP("LevelComponents","Use a finite pivot within ±32768 units and a grid from 0 to 32768."));
	}
	LevelTextureAffine result;
	switch (request.operation) {
	case LevelComponentTransformKind::Move:
		if (!finite(request.delta,65536)) { return fail(error,QT_TRANSLATE_NOOP("LevelComponents","Use finite movement within ±65536 units; resulting corners must remain within ±32768.")); }
		result.translation = request.delta; *matrix = result; return true;
	case LevelComponentTransformKind::Rotate:
		if (request.axis < 0 || request.axis > 2 || !std::isfinite(request.amount) || std::abs(request.amount) > 360000) {
			return fail(error,QT_TRANSLATE_NOOP("LevelComponents","Choose a rotation axis and a finite angle within ±360000 degrees."));
		}
		for (auto& column : result.columns) { column = rotateLevelVector(column,request.axis,std::remainder(request.amount,360.0)); }
		break;
	case LevelComponentTransformKind::Scale:
		if (!finite(request.scale,1024)) { return fail(error,QT_TRANSLATE_NOOP("LevelComponents","Use finite scale factors within ±1024.")); }
		result.columns[0].x = request.scale.x; result.columns[1].y = request.scale.y; result.columns[2].z = request.scale.z;
		break;
	case LevelComponentTransformKind::Shear:
		if (request.axis < 0 || request.axis > 2 || request.along < 0 || request.along > 2 || request.axis == request.along ||
			!std::isfinite(request.amount) || std::abs(request.amount) > 1024) {
			return fail(error,QT_TRANSLATE_NOOP("LevelComponents","Choose different shear axes and a finite factor within ±1024."));
		}
		if (request.axis == 0) { result.columns[request.along].x = request.amount; }
		else if (request.axis == 1) { result.columns[request.along].y = request.amount; }
		else { result.columns[request.along].z = request.amount; }
		break;
	case LevelComponentTransformKind::Affine: {
		const auto& c = request.linear;
		const double determinant = c[0].x*(c[1].y*c[2].z-c[2].y*c[1].z) - c[1].x*(c[0].y*c[2].z-c[2].y*c[0].z) + c[2].x*(c[0].y*c[1].z-c[1].y*c[0].z);
		if (!finite(c[0],1024) || !finite(c[1],1024) || !finite(c[2],1024) || !std::isfinite(determinant) || std::abs(determinant) < 1e-9
			|| !finite(request.delta,65536)) {
			return fail(error,QT_TRANSLATE_NOOP("LevelComponents","Use an invertible linear part with finite terms within ±1024 and a finite translation within ±65536 units."));
		}
		result.columns = c;
		break;
	}
	default: return fail(error,QT_TRANSLATE_NOOP("LevelComponents","Unknown component transform."));
	}
	const auto movedPivot = transformedLevelComponentPoint(pivot,result);
	result.translation = {pivot.x-movedPivot.x,pivot.y-movedPivot.y,pivot.z-movedPivot.z,true};
	if (request.operation == LevelComponentTransformKind::Affine) {
		result.translation.x += request.delta.x; result.translation.y += request.delta.y; result.translation.z += request.delta.z;
	}
	*matrix = result; return true;
}

namespace {
// NetRadiant Custom records each component move, turn, scale and skew for
// Repeat Transforms (core/level_repeat.h). Snapping (a grid) and the Affine
// replay itself record nothing.
LevelRepeatAction repeatableComponentTransform(const LevelComponentTransformRequest& request, bool* recordable)
{
	LevelRepeatAction action;
	*recordable = request.grid == 0;
	switch (request.operation) {
	case LevelComponentTransformKind::Move:
		action.kind = LevelRepeatAction::Kind::Move;
		action.matrix[3] = request.delta.x; action.matrix[7] = request.delta.y; action.matrix[11] = request.delta.z;
		break;
	case LevelComponentTransformKind::Rotate:
		action.rotationAxis = request.axis; action.rotationDegrees = request.amount;
		break;
	case LevelComponentTransformKind::Scale:
		action.matrix[0] = request.scale.x; action.matrix[5] = request.scale.y; action.matrix[10] = request.scale.z;
		break;
	case LevelComponentTransformKind::Shear:
		action.matrix[4*request.axis+request.along] = request.amount;
		break;
	default:
		*recordable = false;
		break;
	}
	return action;
}
}

// Original component-transform implementation. Behaviour references: TrenchBroom
// 2026.2 vertex editing and NRC selection transforms; see docs/CREDITS.md.
bool transformLevelMapComponents(LevelMapDocument* document, const QVector<LevelMapComponentSet>& sets,
	const LevelComponentTransformRequest& request, LevelMapComponentReport* report, QString* error)
{
	if (error) { error->clear(); }
	LevelMapComponentReport result;
	if (const auto guarded = guardLevelSceneEdit(document,error,[&](auto* candidate) {
		return transformLevelMapComponents(candidate,sets,request,&result,error);
	}); guarded.has_value()) { if (*guarded && report) { *report = result; } return *guarded; }
	if (!document || (document->format != LevelMapFormat::QuakeMap && document->format != LevelMapFormat::Quake3Map)) {
		return fail(error,QT_TRANSLATE_NOOP("LevelComponents","Open a Quake-family map to transform brush and patch components."));
	}
	bool recordable = false;
	const LevelRepeatAction repeated = repeatableComponentTransform(request,&recordable);
	detail::LevelRepeatCaptureScope repeat(document,recordable);
	LevelMapVec3 lo,hi;
	if (!levelMapComponentBounds(*document,sets,&lo,&hi,error)) { return false; }
	LevelTextureAffine matrix;
	if (!levelComponentTransformMatrix(request,{(lo.x+hi.x)/2,(lo.y+hi.y)/2,(lo.z+hi.z)/2,true},&matrix,error)) { return false; }
	QMap<int,LevelMapBrush> brushes;
	QMap<int,LevelMapPatch> patches;
	int done = 0;
	for (const auto& set : sets) {
		detail::placementCheckpoint(LevelPlacementPhase::Transforming,done++,sets.size());
		QString problem;
		if (set.object.kind == LevelMapSelectionKind::QuakeBrush) {
			const auto found = std::find_if(document->brushes.cbegin(),document->brushes.cend(),[&](const auto& b) { return b.id == set.object.objectId; });
			auto brush = *found; LevelBrushEditReport edited;
			if (!transformLevelBrushComponents(&brush,set.kind,set.components,matrix,request.grid,request.allowCollapse,
				request.textureLock,request.allowValve220,&edited,&problem,request.fitUvLock)) {
				if (error) { *error = QCoreApplication::translate("LevelComponents","Brush %1: %2").arg(set.object.objectId).arg(problem); } return false;
			}
			result.brushes += edited.changed; result.collapsedVertices += edited.collapsedVertices; result.fittedFaces += edited.fittedFaces;
			if (edited.changed) { brushes.insert(brush.id,brush); }
		} else {
			const auto found = std::find_if(document->patches.cbegin(),document->patches.cend(),[&](const auto& p) { return p.id == set.object.objectId; });
			auto patch = *found;
			const QSet<int> selected(set.components.cbegin(),set.components.cend());
			for (int vertex : selected) {
				patch.controlPoints[vertex] = transformedLevelComponentPoint(found->controlPoints[vertex],matrix,request.grid);
				if (!finite(patch.controlPoints[vertex])) { return fail(error,QT_TRANSLATE_NOOP("LevelComponents","The transformed control point would leave the component coordinate range of ±32768.")); }
			}
			if (!validateLevelPatch(patch,&problem)) { if (error) { *error = QCoreApplication::translate("LevelComponents","Patch %1: %2").arg(set.object.objectId).arg(problem); } return false; }
			if (!std::equal(patch.controlPoints.cbegin(),patch.controlPoints.cend(),found->controlPoints.cbegin(),[](const auto& a, const auto& b) {
				return a.x == b.x && a.y == b.y && a.z == b.z;
			})) {
				refreshLevelPatchBounds(&patch); patch.definitionDirty = patch.geometryDirty = true;
				patches.insert(patch.id,patch); ++result.patches;
			}
		}
	}
	// q3map2 selects classic/Valve syntax from the first face. A conversion
	// must cover the map, including untouched brushes, in this same transaction.
	bool convertsClassic = false;
	for (const auto& source : document->brushes) {
		if (!brushes.contains(source.id)) { continue; }
		const auto& changed = brushes[source.id];
		for (const auto& face : changed.faces) {
			const auto donor = std::find_if(source.faces.cbegin(),source.faces.cend(),[&](const auto& f) { return f.line == face.line; });
			convertsClassic |= donor != source.faces.cend() && !donor->explicitTextureAxes && face.explicitTextureAxes;
		}
	}
	if (convertsClassic) {
		for (const auto& source : document->brushes) {
			detail::placementCancellationCheckpoint();
			auto converted = brushes.value(source.id,source); bool changed = false;
			for (auto& face : converted.faces) {
				if (face.explicitTextureMatrix) { return fail(error,QT_TRANSLATE_NOOP("LevelComponents","Valve 220 conversion cannot be mixed with brush-primitive matrices in the same map.")); }
				if (face.explicitTextureAxes) { continue; }
				const auto projection = levelTextureProjection(face);
				if (!projection.valid) { return fail(error,QT_TRANSLATE_NOOP("LevelComponents","An existing face has an invalid UV projection; map-wide Valve 220 conversion was cancelled.")); }
				face.explicitTextureAxes = true; face.uAxis = projection.u; face.vAxis = projection.v;
				face.uOffset = projection.offsetU; face.vOffset = projection.offsetV;
				face.scaleX = face.scaleY = 1; face.rotation = 0; face.textureParametersDirty = true; changed = true;
			}
			if (changed) { converted.primitiveKind = QStringLiteral("valve220"); converted.textureParametersDirty = true; brushes.insert(source.id,converted); }
		}
	}
	if (!replaceLevelMapComponentGeometry(document,brushes,patches,
		QCoreApplication::translate("LevelComponents","Transform components of %n object(s)",nullptr,result.brushes+result.patches),error)) { return false; }
	repeat.commitTransform(repeated);
	if (report) { *report = result; }
	return true;
}
} // namespace vibestudio
