#include "core/level_textured_selection.h"
#include "core/level_brush.h"
#include "core/level_history_p.h"
#include "core/level_placement_control_p.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_visibility.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <algorithm>
#include <cmath>

// Original adaptation of NRC Select_FacesAndPatchesByShader and its brush/patch
// walkers at 449778b3840f8f2062daab15c87cb012f2ac945c (GPL-2.0-or-later).
// No source copied. Studio visibility, locks, group boundaries, cancellation
// and atomic publication are intentional adaptations; see docs/CREDITS.md.
namespace vibestudio {
namespace {
struct Text { Q_DECLARE_TR_FUNCTIONS(LevelTexturedSelection) };
QString objectKey(LevelMapSelectionKind kind, int id) { return levelMapSelectionRefId({kind,id}); }
struct FacePlane {
	LevelMapVec3 normal;
	double distance = 0;
	bool valid = false;
};
// Newell's method: exact for planar polygons and orientation-independent when
// compared by absolute normal alignment, so reflections still match.
FacePlane facePlane(const QVector<LevelMapVec3>& points)
{
	double nx = 0, ny = 0, nz = 0, cx = 0, cy = 0, cz = 0;
	for (qsizetype i = 0; i < points.size(); ++i) {
		const auto& a = points[i]; const auto& b = points[(i+1)%points.size()];
		nx += (a.y-b.y)*(a.z+b.z); ny += (a.z-b.z)*(a.x+b.x); nz += (a.x-b.x)*(a.y+b.y);
		cx += a.x; cy += a.y; cz += a.z;
	}
	const double length = std::sqrt(nx*nx+ny*ny+nz*nz);
	if (points.size() < 3 || !std::isfinite(length) || length < 1e-9) { return {}; }
	FacePlane plane; plane.normal = {nx/length,ny/length,nz/length,true}; plane.valid = true;
	plane.distance = (plane.normal.x*cx+plane.normal.y*cy+plane.normal.z*cz)/static_cast<double>(points.size());
	return plane;
}
QVector<LevelMapVec3> facePoints(const LevelBrushTopology& topology, int face)
{
	QVector<LevelMapVec3> points;
	for (int vertex : topology.faces[face]) { points << topology.vertices[vertex]; }
	return points;
}
double alignment(const LevelMapVec3& a, const LevelMapVec3& b) { return std::abs(a.x*b.x+a.y*b.y+a.z*b.z); }
}
QByteArray levelSurfaceSelectionGeometryKey(const LevelMapBrush& brush)
{
	QByteArray bytes;
	QDataStream stream(&bytes,QIODevice::WriteOnly); stream.setVersion(QDataStream::Qt_6_0);
	stream << qint32(brush.id) << qint32(brush.entityId) << qint64(brush.faces.size());
	const auto point = [&](const LevelMapVec3& p) { stream << p.valid << p.x << p.y << p.z; };
	for (const auto& face : brush.faces) {
		detail::placementCancellationCheckpoint();
		stream << face.explicitPlane;
		if (face.explicitPlane) { point(face.planeNormal); stream << face.planeDistance; }
		else { point(face.p0); point(face.p1); point(face.p2); }
	}
	return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
}
bool selectLevelMapTextured(LevelMapDocument* document, const LevelTexturedSelectionRequest& request,
	LevelTexturedSelectionReport* report, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [&](const QString& message) { if (error) { *error = message; } return false; };
	if (!document || (document->format != LevelMapFormat::QuakeMap && document->format != LevelMapFormat::Quake3Map)) {
		return fail(Text::tr("Select Textured requires a Quake-family text map."));
	}
	if (request.material.isEmpty() || request.material.size() > 4096 || request.material.contains(QChar::Null)) {
		return fail(Text::tr("Choose a current material of 1–4,096 characters."));
	}
	if (request.faces.size() > kLevelTexturedSelectionLimit) {
		return fail(Text::tr("Select Textured supports at most 16,384 combined face and matching patch targets."));
	}
	if (!document->scene.opaqueMetadata.isEmpty() || !document->scene.problem.isEmpty()) {
		return fail(Text::tr("Resolve the map's unrecognised scene metadata before selecting textured surfaces."));
	}
	// Within the selection: NRC Scene_BrushFindReplaceShader_Selected's search
	// branch, extended to patches and to a selected entity's primitives.
	QSet<int> scopeEntities, scopeBrushes, scopePatches;
	if (request.withinSelection) {
		for (const auto& ref : document->selection) {
			if (ref.kind == LevelMapSelectionKind::Entity) { scopeEntities.insert(ref.objectId); }
			if (ref.kind == LevelMapSelectionKind::QuakeBrush) { scopeBrushes.insert(ref.objectId); }
			if (ref.kind == LevelMapSelectionKind::QuakePatch) { scopePatches.insert(ref.objectId); }
		}
		if (scopeEntities.isEmpty() && scopeBrushes.isEmpty() && scopePatches.isEmpty()) {
			return fail(Text::tr("Select brushes, patches or entities to search within."));
		}
	}
	const auto inScope = [&](const QSet<int>& primitives, int id, int owner) {
		return !request.withinSelection || primitives.contains(id) || scopeEntities.contains(owner);
	};
	detail::placementCheckpoint(LevelPlacementPhase::SelectingObjects);
	const auto context = levelSceneSelectionContext(*document);
	const auto excluded = levelMapHiddenObjects(*document) | levelSceneLockedObjects(*document) | request.excludedObjects;
	QHash<int,const LevelMapBrush*> brushes;
	QSet<int> owners;
	for (const auto& entity : document->entities) { detail::placementCancellationCheckpoint(); owners.insert(entity.id); }
	for (const auto& brush : document->brushes) { detail::placementCancellationCheckpoint(); brushes.insert(brush.id,&brush); }
	LevelTexturedSelectionReport result; result.selection = document->selection;
	QSet<QPair<int,int>> picked;
	QSet<QString> objects;
	for (const auto& ref : document->selection) { objects.insert(levelMapSelectionRefId(ref)); }
	const auto validateBrush = [&](const LevelMapBrush& brush) {
		if (result.brushGeometry.contains(brush.id)) { return true; }
		LevelBrushTopology topology; QString problem;
		if (!owners.contains(brush.entityId) || !levelBrushTopology(brush,&topology,&problem) || topology.faces.size() != brush.faces.size()) {
			return fail(Text::tr("Cannot select faces of brush %1: %2").arg(brush.id).arg(problem.isEmpty() ? Text::tr("Invalid brush geometry or owner.") : problem));
		}
		result.brushGeometry.insert(brush.id,levelSurfaceSelectionGeometryKey(brush)); return true;
	};
	for (const auto& face : request.faces) {
		detail::placementCancellationCheckpoint(); const auto* brush = brushes.value(face.brushId);
		if (!brush || face.faceIndex < 0 || face.faceIndex >= brush->faces.size()) {
			return fail(Text::tr("An existing face pick is missing: brush %1, face %2.").arg(face.brushId).arg(face.faceIndex+1));
		}
		if (!validateBrush(*brush)) { return false; }
		const QPair<int,int> key{face.brushId,face.faceIndex};
		if (!picked.contains(key)) { picked.insert(key); result.faces.append(face); }
	}
	const auto permitted = [&](LevelMapSelectionKind kind, int id, int owner) {
		const auto key = objectKey(kind,id);
		return owners.contains(owner) && context.allowedObjects.contains(key) && !context.closedGroups.contains(key)
			&& !excluded.contains(key) && !excluded.contains(objectKey(LevelMapSelectionKind::Entity,owner));
	};
	const auto hasRoom = [&] {
		return result.faces.size()+result.matchedPatches.size() < kLevelTexturedSelectionLimit
			|| fail(Text::tr("Select Textured supports at most 16,384 combined face and matching patch targets."));
	};
	qint64 completed = 0; const auto total = document->brushes.size()+document->patches.size();
	for (const auto& brush : document->brushes) {
		detail::placementCheckpoint(LevelPlacementPhase::SelectingObjects,completed++,total);
		if (!permitted(LevelMapSelectionKind::QuakeBrush,brush.id,brush.entityId) || !inScope(scopeBrushes,brush.id,brush.entityId)) { continue; }
		for (int i = 0; i < brush.faces.size(); ++i) {
			detail::placementCancellationCheckpoint();
			const QPair<int,int> key{brush.id,i};
			if (brush.faces[i].textureName.compare(request.material,Qt::CaseInsensitive) != 0 || picked.contains(key)) { continue; }
			if (!hasRoom() || !validateBrush(brush)) { return false; }
			picked.insert(key); result.faces.append({brush.id,i}); ++result.addedFaces;
		}
	}
	for (const auto& patch : document->patches) {
		detail::placementCheckpoint(LevelPlacementPhase::SelectingObjects,completed++,total);
		if (!permitted(LevelMapSelectionKind::QuakePatch,patch.id,patch.entityId) || !inScope(scopePatches,patch.id,patch.entityId)
			|| patch.textureName.compare(request.material,Qt::CaseInsensitive) != 0) { continue; }
		if (!hasRoom()) { return false; }
		result.matchedPatches.append(patch.id);
		const auto key = objectKey(LevelMapSelectionKind::QuakePatch,patch.id);
		if (!objects.contains(key)) {
			objects.insert(key); result.selection.append({LevelMapSelectionKind::QuakePatch,patch.id}); ++result.addedPatches;
		}
	}
	LevelMapDocument candidate = *document;
	if (!setLevelMapSelection(&candidate,result.selection,error)) { return false; }
	if (request.undoSelection && result.selection != document->selection) {
		detail::recordLevelMapSelectionHistory(&candidate,document->selection,
			{document->openSceneGroups,document->activeSceneNode,document->sceneCreationBeforeOpen},Text::tr("Select Textured"));
	}
	detail::placementCheckpoint(LevelPlacementPhase::Finalizing,1,1);
	*document = std::move(candidate); if (report) { *report = std::move(result); } return true;
}

// Independent face manipulation. Workflow reference: NRC
// FaceInstance::transformComponents and Scene_Translate_Component_Selected at
// 449778b3840f8f2062daab15c87cb012f2ac945c (GPL-2.0-or-later), where faces
// picked by Select Textured transform in face mode without selecting their
// brushes. Original implementation over the shared component transform
// service (corner-based, as TrenchBroom's face tool); see docs/CREDITS.md.
bool levelSurfaceFaceComponentSets(const LevelMapDocument& document, const LevelSurfaceFacePicks& picks,
	const QVector<LevelMapComponentSet>& components, QVector<LevelMapComponentSet>* sets, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [&](const QString& message) { if (error) { *error = message; } return false; };
	if (!sets) { return fail(Text::tr("Provide a destination for the merged component sets.")); }
	if (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map) {
		return fail(Text::tr("Transforming picked faces requires a Quake-family text map."));
	}
	if (picks.faces.size() > kLevelTexturedSelectionLimit) {
		return fail(Text::tr("At most 16,384 picked faces can be transformed together."));
	}
	QVector<LevelMapComponentSet> result = components;
	if (picks.faces.isEmpty()) { *sets = std::move(result); return true; }
	const auto context = levelSceneSelectionContext(document);
	const auto excluded = levelMapHiddenObjects(document) | levelSceneLockedObjects(document) | picks.excludedObjects;
	QHash<int,const LevelMapBrush*> brushes;
	for (const auto& brush : document.brushes) { detail::placementCancellationCheckpoint(); brushes.insert(brush.id,&brush); }
	QHash<QString,qsizetype> indices;
	for (qsizetype i = 0; i < result.size(); ++i) { indices.insert(levelMapSelectionRefId(result[i].object),i); }
	QHash<int,LevelBrushTopology> topologies;
	for (const auto& face : picks.faces) {
		detail::placementCancellationCheckpoint();
		const auto* brush = brushes.value(face.brushId);
		if (!brush || face.faceIndex < 0 || face.faceIndex >= brush->faces.size()) {
			return fail(Text::tr("A picked face is missing: brush %1, face %2. Select textured surfaces again.").arg(face.brushId).arg(face.faceIndex+1));
		}
		const auto key = objectKey(LevelMapSelectionKind::QuakeBrush,brush->id);
		if (!context.allowedObjects.contains(key) || context.closedGroups.contains(key) || excluded.contains(key)
			|| excluded.contains(objectKey(LevelMapSelectionKind::Entity,brush->entityId))) {
			return fail(Text::tr("Brush %1 is hidden, filtered, locked or outside the open group, so its picked faces cannot be transformed.").arg(brush->id));
		}
		// The component transform takes at most 256 objects; stop before solving
		// more brushes than it could accept.
		if (!indices.contains(key) && result.size() >= 256) {
			return fail(Text::tr("The picked faces and components span more than 256 brushes and patches. Transform fewer at once."));
		}
		if (!topologies.contains(brush->id)) {
			if (picks.brushGeometry.contains(brush->id) && picks.brushGeometry.value(brush->id) != levelSurfaceSelectionGeometryKey(*brush)) {
				return fail(Text::tr("Brush %1 changed after its faces were picked. Select textured surfaces again.").arg(brush->id));
			}
			LevelBrushTopology topology; QString problem;
			if (!levelBrushTopology(*brush,&topology,&problem) || topology.faces.size() != brush->faces.size()) {
				return fail(Text::tr("Cannot transform faces of brush %1: %2").arg(brush->id)
					.arg(problem.isEmpty() ? Text::tr("Invalid brush geometry.") : problem));
			}
			topologies.insert(brush->id,topology);
		}
		const auto found = indices.constFind(key);
		if (found == indices.cend()) {
			indices.insert(key,result.size());
			result.append({{LevelMapSelectionKind::QuakeBrush,brush->id},LevelBrushComponent::Face,{face.faceIndex}});
			continue;
		}
		auto& set = result[*found];
		if (set.kind == LevelBrushComponent::Face) {
			if (!set.components.contains(face.faceIndex)) { set.components.append(face.faceIndex); }
			continue;
		}
		// Component sets carry one kind per object; the transform moves corners,
		// so vertices express the union exactly.
		const auto& topology = topologies[brush->id];
		auto vertices = levelBrushComponentVertices(topology,set.kind,set.components);
		const auto corners = levelBrushComponentVertices(topology,LevelBrushComponent::Face,{face.faceIndex});
		if (vertices.isEmpty() || corners.isEmpty()) {
			return fail(Text::tr("A picked component of brush %1 no longer exists.").arg(brush->id));
		}
		for (int vertex : corners) { if (!vertices.contains(vertex)) { vertices.append(vertex); } }
		std::sort(vertices.begin(),vertices.end());
		set.kind = LevelBrushComponent::Vertex; set.components = vertices;
	}
	*sets = std::move(result); return true;
}

bool transformLevelMapSurfaceFaces(LevelMapDocument* document, const LevelSurfaceFacePicks& picks,
	const QVector<LevelMapComponentSet>& components, const LevelComponentTransformRequest& request,
	LevelSurfaceFaceTransformReport* report, QString* error)
{
	if (error) { error->clear(); }
	if (!document) { if (error) { *error = Text::tr("Open a map to transform picked faces."); } return false; }
	LevelSurfaceFaceTransformReport result;
	if (!levelSurfaceFaceComponentSets(*document,picks,components,&result.sets,error)) { return false; }
	LevelMapVec3 lo,hi; LevelTextureAffine matrix;
	if (!levelMapComponentBounds(*document,result.sets,&lo,&hi,error)
		|| !levelComponentTransformMatrix(request,{(lo.x+hi.x)/2,(lo.y+hi.y)/2,(lo.z+hi.z)/2,true},&matrix,error)) { return false; }
	// Where each picked face's corners land under the same shared matrix.
	QHash<int,LevelBrushTopology> before;
	QVector<QVector<LevelMapVec3>> expected; expected.reserve(picks.faces.size());
	for (const auto& face : picks.faces) {
		detail::placementCancellationCheckpoint();
		if (!before.contains(face.brushId)) {
			const auto found = std::find_if(document->brushes.cbegin(),document->brushes.cend(),[&](const auto& b) { return b.id == face.brushId; });
			LevelBrushTopology topology;
			if (found == document->brushes.cend() || !levelBrushTopology(*found,&topology,error)) { return false; }
			before.insert(face.brushId,topology);
		}
		const auto& topology = before[face.brushId];
		QVector<LevelMapVec3> points;
		for (int vertex : topology.faces[face.faceIndex]) { points << transformedLevelComponentPoint(topology.vertices[vertex],matrix,request.grid); }
		expected << points;
	}
	if (!transformLevelMapComponents(document,result.sets,request,&result.components,error)) { return false; }
	QHash<int,const LevelMapBrush*> after;
	for (const auto& brush : document->brushes) { after.insert(brush.id,&brush); }
	QHash<int,LevelBrushTopology> topologies;
	QSet<QPair<int,int>> seen;
	for (qsizetype i = 0; i < picks.faces.size(); ++i) {
		detail::placementCancellationCheckpoint();
		const auto* brush = after.value(picks.faces[i].brushId);
		const auto plane = facePlane(expected[i]);
		if (brush && plane.valid && !topologies.contains(brush->id)) {
			LevelBrushTopology topology;
			if (levelBrushTopology(*brush,&topology)) { topologies.insert(brush->id,topology); }
		}
		QVector<int> matches;
		if (brush && plane.valid && topologies.contains(brush->id)) {
			const auto& topology = topologies[brush->id];
			const auto onPlane = [&](const LevelMapVec3& p) {
				return std::abs(p.x*plane.normal.x+p.y*plane.normal.y+p.z*plane.normal.z-plane.distance) <= 1e-3;
			};
			for (int f = 0; f < topology.faces.size() && matches.isEmpty(); ++f) {
				const auto points = facePoints(topology,f); const auto fitted = facePlane(points);
				if (fitted.valid && alignment(fitted.normal,plane.normal) > 0.99999 && std::all_of(points.cbegin(),points.cend(),onPlane)) { matches << f; }
			}
			// Grid rounding can bend a transformed face; the rebuild then splits
			// it into pieces built only from its own transformed corners.
			const auto corner = [&](const LevelMapVec3& p) {
				return std::any_of(expected[i].cbegin(),expected[i].cend(),[&](const auto& q) {
					return (p.x-q.x)*(p.x-q.x)+(p.y-q.y)*(p.y-q.y)+(p.z-q.z)*(p.z-q.z) <= 1e-6;
				});
			};
			if (matches.isEmpty()) {
				for (int piece = 0; piece < topology.faces.size(); ++piece) {
					const auto points = facePoints(topology,piece); const auto fitted = facePlane(points);
					if (fitted.valid && alignment(fitted.normal,plane.normal) > 0.5 && std::all_of(points.cbegin(),points.cend(),corner)) { matches << piece; }
				}
			}
		}
		if (matches.isEmpty()) { ++result.retiredFaces; continue; }
		for (int f : std::as_const(matches)) {
			if (seen.contains({brush->id,f})) { continue; }
			seen.insert({brush->id,f}); result.faces.append({brush->id,f});
			if (!result.brushGeometry.contains(brush->id)) { result.brushGeometry.insert(brush->id,levelSurfaceSelectionGeometryKey(*brush)); }
		}
	}
	if (report) { *report = std::move(result); }
	return true;
}
} // namespace vibestudio
