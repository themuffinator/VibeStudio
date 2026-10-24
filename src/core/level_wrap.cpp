#include "core/level_wrap.h"
#include "core/level_patch.h"
#include "core/level_primitive.h"
#include "core/level_scene_locks.h"
#include "core/level_placement_control_p.h"
#include <QCoreApplication>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace vibestudio {
// Behaviour reference: NRC radiant/csg.cpp CSG_WrapMerge,
// Scene_gatherSelectedComponents and ultimate_group_path, revision
// 449778b3840f8f2062daab15c87cb012f2ac945c, GPL-2.0-or-later.
// Independent implementation using VibeStudio services; see docs/CREDITS.md.
bool wrapLevelMap(LevelMapDocument* document, const LevelWrapRequest& request, LevelWrapReport* report, QString* error)
{
	if (report) { *report = {}; } if (error) { error->clear(); }
	LevelWrapReport result;
	if (const auto guarded = guardLevelSceneEdit(document,error,[&](auto* candidate) {
		return wrapLevelMap(candidate,request,&result,error);
	}); guarded.has_value()) { if (*guarded && report) { *report = result; } return *guarded; }
	const auto fail = [error](const char* text) { if (error) { *error = QCoreApplication::translate("LevelWrap",text); } return false; };
	const auto finite = [](const LevelMapVec3& p) { return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)
		&& std::abs(p.x) <= 32768 && std::abs(p.y) <= 32768 && std::abs(p.z) <= 32768; };
	if (!document || (document->format != LevelMapFormat::QuakeMap && document->format != LevelMapFormat::Quake3Map)
		|| !validLevelClipTexture(request.texture) || request.points.size() > 3 || std::any_of(request.points.cbegin(),request.points.cend(),[&](const auto& p) { return !finite(p); })) {
		return fail(QT_TRANSLATE_NOOP("LevelWrap","Wrap/Merge needs a Quake-family map, a valid material and up to three finite extra points within ±32768 units."));
	}
	if (!request.componentsOnly) {
		if (!request.components.isEmpty()) { return fail(QT_TRANSLATE_NOOP("LevelWrap","Choose whole-object or component wrapping explicitly; do not mix their selections.")); }
		LevelMapClipReport merged;
		if (!wrapLevelMapSelection(document,request.points,request.texture,&merged,error)) { return false; }
		result.sourceBrushes = merged.changed; result.inheritedFaces = merged.inheritedFaces; result.newFaces = merged.newFaces;
		result.brushId = document->selection.last().objectId;
		result.faces = result.inheritedFaces+result.newFaces;
		for (const auto& brush : document->brushes) { if (brush.id == result.brushId) {
			// The replacement service already validated this persisted topology.
			LevelBrushTopology topology; if (levelBrushTopology(brush,&topology)) { result.vertices = int(topology.vertices.size()); } break;
		} }
		if (report) { *report = result; } return true;
	}
	if (request.components.isEmpty() || request.components.size() > 256) { return fail(QT_TRANSLATE_NOOP("LevelWrap","Pick brush vertices, edges, faces or patch controls before wrapping components.")); }
	QMap<int,LevelBrushTopology> brushes;
	QSet<int> patches;
	QVector<LevelMapVec3> points = request.points;
	for (const auto& set : request.components) {
		detail::placementCheckpoint(LevelPlacementPhase::Building,brushes.size()+patches.size(),0);
		if (set.components.isEmpty() || set.components.size() > 4096) { return fail(QT_TRANSLATE_NOOP("LevelWrap","Each component set must contain between 1 and 4096 component IDs.")); }
		if (set.object.kind == LevelMapSelectionKind::QuakeBrush) {
			if (set.kind != LevelBrushComponent::Vertex && set.kind != LevelBrushComponent::Edge && set.kind != LevelBrushComponent::Face) {
				return fail(QT_TRANSLATE_NOOP("LevelWrap","Choose brush vertices, edges or faces."));
			}
			if (!brushes.contains(set.object.objectId)) {
				const auto it = std::find_if(document->brushes.cbegin(),document->brushes.cend(),[&](const auto& b) { return b.id == set.object.objectId; });
				if (it == document->brushes.cend()) { return fail(QT_TRANSLATE_NOOP("LevelWrap","A picked brush no longer exists.")); }
				LevelBrushTopology topology; if (!levelBrushTopology(*it,&topology,error)) { return false; } brushes.insert(it->id,std::move(topology));
			}
			const auto& topology = brushes[set.object.objectId];
			const auto vertices = levelBrushComponentVertices(topology,set.kind,set.components);
			if (vertices.isEmpty()) { return fail(QT_TRANSLATE_NOOP("LevelWrap","A picked brush component no longer exists.")); }
			for (const int id : vertices) { points << topology.vertices[id]; }
		} else if (set.object.kind == LevelMapSelectionKind::QuakePatch && set.kind == LevelBrushComponent::Vertex) {
			const auto it = std::find_if(document->patches.cbegin(),document->patches.cend(),[&](const auto& p) { return p.id == set.object.objectId; });
			if (it == document->patches.cend()) { return fail(QT_TRANSLATE_NOOP("LevelWrap","A picked patch no longer exists.")); }
			if (!patches.contains(it->id) && !validateLevelPatch(*it,error)) { return false; }
			patches.insert(it->id);
			for (const int id : set.components) {
				if (id < 0 || id >= it->controlPoints.size()) { return fail(QT_TRANSLATE_NOOP("LevelWrap","A picked patch control point no longer exists.")); }
				points << it->controlPoints[id];
			}
		} else { return fail(QT_TRANSLATE_NOOP("LevelWrap","Choose brush components or patch control points to wrap.")); }
		if (brushes.size()+patches.size() > 64 || points.size() > 4096) { return fail(QT_TRANSLATE_NOOP("LevelWrap","Wrap/Merge supports at most 64 source objects and 4096 input points.")); }
	}
	LevelMapBrush hull; LevelBrushTopology topology;
	if (!createLevelBrushEnvelope(points,request.texture,&hull,error) || !levelBrushTopology(hull,&topology,error)) { return false; }
	LevelHullInsertionOptions insertion; insertion.keepSelection = true;
	insertion.destination = document->selection.isEmpty() ? request.components.last().object : document->selection.last();
	if (insertion.destination.kind == LevelMapSelectionKind::Entity) {
		const int owner = insertion.destination.objectId;
		const auto entity = std::find_if(document->entities.cbegin(),document->entities.cend(),[&](const auto& e) { return e.id == owner; });
		if (entity != document->entities.cend() && entity->className != QStringLiteral("worldspawn")
			&& std::none_of(document->brushes.cbegin(),document->brushes.cend(),[&](const auto& b) { return b.entityId == owner; })
			&& std::none_of(document->patches.cbegin(),document->patches.cend(),[&](const auto& p) { return p.entityId == owner; })) {
			// NRC ultimate_group_path falls back to worldspawn for a point entity.
			insertion.destination = {};
		}
	}
	QVector<int> ids;
	if (!addLevelMapBrushHulls(document,{topology.vertices},request.texture,
		QCoreApplication::translate("LevelWrap","Wrap picked components into a new brush"),QCoreApplication::translate("LevelWrap","Remove wrapped component brush"),&ids,error,insertion)) { return false; }
	result.sourcesRetained = true; result.brushId = ids.first(); result.sourceBrushes = int(brushes.size()); result.sourcePatches = int(patches.size());
	result.vertices = int(topology.vertices.size()); result.faces = result.newFaces = int(topology.faces.size());
	if (report) { *report = result; } return true;
}

QJsonObject levelWrapReportJson(const LevelWrapReport& r)
{
	return {{"operation","wrap-merge"},{"sourcesRetained",r.sourcesRetained},{"brushId",r.brushId},
		{"sourceBrushes",r.sourceBrushes},{"sourcePatches",r.sourcePatches},{"vertices",r.vertices},{"faces",r.faces},
		{"inheritedFaces",r.inheritedFaces},{"newFaces",r.newFaces}};
}
} // namespace vibestudio
