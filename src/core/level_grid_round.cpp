// Round to Grid: Radiant's Snap To Grid, where every brush vertex, patch
// control point and point-entity origin rounds on its own, or only the picked
// components in component mode. Workflow reference: NetRadiant Custom
// Selection_SnapToGrid, Scene_SnapToGrid_Selected and
// Scene_SnapToGrid_Component_Selected in radiant/select.cpp at 449778b3
// (GPL-2.0-or-later). Original implementation over VibeStudio's component
// transform service; see docs/CREDITS.md.

#include "core/level_grid_round.h"
#include "core/level_history_p.h"
#include "core/level_patch.h"
#include "core/level_placement_control_p.h"
#include "core/level_scene_locks.h"
#include "core/level_visibility.h"

#include <QCoreApplication>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace vibestudio
{
namespace
{
QString coordinate(double value)
{
	// Grid steps can be fractional (0.125); a negative zero reads as zero.
	return QString::number(value == 0 ? 0.0 : value, 'g', 15);
}

bool samePoint(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool finitePoint(const LevelMapVec3& p)
{
	return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x) <= 32768
		&& std::abs(p.y) <= 32768 && std::abs(p.z) <= 32768;
}
} // namespace

bool roundLevelMapSelectionToGrid(LevelMapDocument* document, const LevelMapGridRoundRequest& request,
	LevelMapGridRoundReport* report, QString* error)
{
	LevelMapGridRoundReport staged;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
			return roundLevelMapSelectionToGrid(candidate, request, &staged, error);
		});
		guarded.has_value()) {
		if (*guarded && report) {
			*report = staged;
		}
		return *guarded;
	}
	if (report) {
		*report = {};
	}
	if (error) {
		error->clear();
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document || (document->format != LevelMapFormat::QuakeMap && document->format != LevelMapFormat::Quake3Map)) {
		return fail(QCoreApplication::translate("VibeStudioLevelGridRound",
			"Open a Quake-family map to round brush vertices, patch control points and entity origins to the grid."));
	}
	if (!std::isfinite(request.grid) || request.grid <= 0 || request.grid > 4096) {
		return fail(QCoreApplication::translate("VibeStudioLevelGridRound", "Round to a grid from more than 0 to 4096 units."));
	}
	LevelComponentTransformRequest transform;
	transform.grid = request.grid;
	transform.textureLock = request.textureLock;
	transform.fitUvLock = request.fitUvLock;
	transform.allowValve220 = request.allowValve220;
	LevelMapGridRoundReport result;
	if (!request.components.isEmpty()) {
		// Component mode rounds only the picks, through the shared transform:
		// one all-or-nothing undo step that names a refused object.
		transform.allowCollapse = request.allowCollapse;
		LevelMapComponentReport components;
		if (!transformLevelMapComponents(document, request.components, transform, &components, error)) {
			return false;
		}
		result.brushes = components.brushes;
		result.patches = components.patches;
		result.collapsedVertices = components.collapsedVertices;
		result.fittedFaces = components.fittedFaces;
		if (report) {
			*report = result;
		}
		return true;
	}
	if (document->selection.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelGridRound", "Select brushes, patches or entities to round them to the grid."));
	}

	// Gather the objects whose points round: selected brushes and patches,
	// the primitives of selected brush entities, and point-entity origins.
	const QSet<QString> hidden = levelMapHiddenObjects(*document);
	QStringList skipped;
	int considered = 0;
	QVector<int> brushIds, patchIds, entityIds;
	QSet<int> brushSeen, patchSeen, entitySeen;
	const auto take = [&](LevelMapSelectionKind kind, int id) {
		auto& seen = kind == LevelMapSelectionKind::QuakeBrush ? brushSeen : patchSeen;
		if (seen.contains(id)) {
			return;
		}
		seen.insert(id);
		++considered;
		const QString name = levelMapSelectionRefId({kind, id});
		if (hidden.contains(name)) {
			skipped << QCoreApplication::translate("VibeStudioLevelGridRound", "%1 is hidden; show it to round it.").arg(name);
			return;
		}
		(kind == LevelMapSelectionKind::QuakeBrush ? brushIds : patchIds) << id;
	};
	for (const LevelMapSelectionRef& ref : std::as_const(document->selection)) {
		detail::placementCancellationCheckpoint();
		if (ref.kind == LevelMapSelectionKind::QuakeBrush || ref.kind == LevelMapSelectionKind::QuakePatch) {
			take(ref.kind, ref.objectId);
			continue;
		}
		const QString name = levelMapSelectionRefId(ref);
		if (ref.kind != LevelMapSelectionKind::Entity) {
			++considered;
			skipped << QCoreApplication::translate("VibeStudioLevelGridRound", "%1 has no points to round.").arg(name);
			continue;
		}
		if (entitySeen.contains(ref.objectId)) {
			continue;
		}
		entitySeen.insert(ref.objectId);
		const auto entity = std::find_if(document->entities.cbegin(), document->entities.cend(), [&ref](const auto& e) { return e.id == ref.objectId; });
		if (entity == document->entities.cend()) {
			++considered;
			skipped << QCoreApplication::translate("VibeStudioLevelGridRound", "%1 is no longer in the map.").arg(name);
			continue;
		}
		if (entity->className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0) {
			++considered;
			skipped << QCoreApplication::translate("VibeStudioLevelGridRound", "Select worldspawn's brushes or patches to round them.");
			continue;
		}
		bool owns = false;
		for (const LevelMapBrush& brush : std::as_const(document->brushes)) {
			if (brush.entityId == ref.objectId) {
				owns = true;
				take(LevelMapSelectionKind::QuakeBrush, brush.id);
			}
		}
		for (const LevelMapPatch& patch : std::as_const(document->patches)) {
			if (patch.entityId == ref.objectId) {
				owns = true;
				take(LevelMapSelectionKind::QuakePatch, patch.id);
			}
		}
		if (owns) {
			// A brush entity's primitives round; its origin key keeps its meaning.
			continue;
		}
		++considered;
		if (hidden.contains(name)) {
			skipped << QCoreApplication::translate("VibeStudioLevelGridRound", "%1 is hidden; show it to round it.").arg(name);
		} else if (!entity->origin.valid) {
			skipped << QCoreApplication::translate("VibeStudioLevelGridRound", "%1 has no origin to round.").arg(name);
		} else {
			entityIds << ref.objectId;
		}
	}

	// Check every object on its own first, so one brush the grid would
	// collapse is reported and left as it was rather than refusing the rest.
	const LevelTextureAffine identity;
	QVector<LevelMapComponentSet> sets;
	for (const int id : std::as_const(brushIds)) {
		detail::placementCancellationCheckpoint();
		const auto found = std::find_if(document->brushes.cbegin(), document->brushes.cend(), [id](const auto& b) { return b.id == id; });
		if (found == document->brushes.cend()) {
			skipped << QCoreApplication::translate("VibeStudioLevelGridRound", "Brush %1 is no longer in the map.").arg(id);
			continue;
		}
		LevelBrushTopology topology;
		QString problem;
		if (!levelBrushTopology(*found, &topology, &problem)) {
			skipped << QCoreApplication::translate("VibeStudioLevelGridRound", "Brush %1: %2").arg(id).arg(problem);
			continue;
		}
		QVector<int> all(topology.vertices.size());
		std::iota(all.begin(), all.end(), 0);
		LevelMapBrush draft = *found;
		LevelBrushEditReport edit;
		if (!transformLevelBrushComponents(&draft, LevelBrushComponent::Vertex, all, identity, request.grid, false, request.textureLock,
				request.allowValve220, &edit, &problem, request.fitUvLock)) {
			skipped << QCoreApplication::translate("VibeStudioLevelGridRound", "Brush %1: %2").arg(id).arg(problem);
			continue;
		}
		if (edit.changed) {
			sets.append({{LevelMapSelectionKind::QuakeBrush, id}, LevelBrushComponent::Vertex, all});
		}
	}
	for (const int id : std::as_const(patchIds)) {
		detail::placementCancellationCheckpoint();
		const auto found = std::find_if(document->patches.cbegin(), document->patches.cend(), [id](const auto& p) { return p.id == id; });
		if (found == document->patches.cend()) {
			skipped << QCoreApplication::translate("VibeStudioLevelGridRound", "Patch %1 is no longer in the map.").arg(id);
			continue;
		}
		LevelMapPatch draft = *found;
		bool changed = false, inside = true;
		for (LevelMapVec3& point : draft.controlPoints) {
			const LevelMapVec3 rounded = transformedLevelComponentPoint(point, identity, request.grid);
			inside = inside && finitePoint(rounded);
			changed = changed || !samePoint(rounded, point);
			point = rounded;
		}
		QString problem;
		if (!inside) {
			skipped << QCoreApplication::translate("VibeStudioLevelGridRound", "Patch %1 would leave the map's coordinate range.").arg(id);
			continue;
		}
		if (!validateLevelPatch(draft, &problem)) {
			skipped << QCoreApplication::translate("VibeStudioLevelGridRound", "Patch %1: %2").arg(id).arg(problem);
			continue;
		}
		if (changed) {
			QVector<int> all(draft.controlPoints.size());
			std::iota(all.begin(), all.end(), 0);
			sets.append({{LevelMapSelectionKind::QuakePatch, id}, LevelBrushComponent::Vertex, all});
		}
	}
	QVector<int> originIds;
	QStringList originValues;
	for (const int id : std::as_const(entityIds)) {
		const auto entity = std::find_if(document->entities.cbegin(), document->entities.cend(), [id](const auto& e) { return e.id == id; });
		const LevelMapVec3 rounded = snapLevelMapPosition(entity->origin, request.grid);
		if (!finitePoint(rounded)) {
			skipped << QCoreApplication::translate("VibeStudioLevelGridRound", "Entity %1 would leave the map's coordinate range.").arg(id);
			continue;
		}
		if (samePoint(rounded, entity->origin)) {
			continue;
		}
		originIds << id;
		originValues << QStringLiteral("%1 %2 %3").arg(coordinate(rounded.x), coordinate(rounded.y), coordinate(rounded.z));
	}
	if (sets.isEmpty() && originIds.isEmpty()) {
		if (considered > 0 && skipped.size() >= considered) {
			return fail(skipped.first());
		}
		result.skipped = skipped;
		if (report) {
			*report = result;
		}
		return true;
	}

	LevelMapDocument working = *document;
	{
		// Keep both edits until they fold into one step, whatever the limit.
		const detail::LevelMapHistoryScope history(working);
		const qsizetype before = working.undoStack.size();
		LevelMapComponentReport components;
		if (!sets.isEmpty() && !transformLevelMapComponents(&working, sets, transform, &components, error)) {
			return false;
		}
		if (!originIds.isEmpty() && !setLevelMapEntitiesProperty(&working, originIds, QStringLiteral("origin"), originValues, error)) {
			return false;
		}
		result.brushes = components.brushes;
		result.patches = components.patches;
		result.entities = static_cast<int>(originIds.size());
		result.collapsedVertices = components.collapsedVertices;
		result.fittedFaces = components.fittedFaces;
		const int objects = result.brushes + result.patches + result.entities;
		const int steps = static_cast<int>(working.undoStack.size() - before);
		if (steps > 0
			&& !collapseLevelMapUndoSteps(&working, steps,
				QCoreApplication::translate("VibeStudioLevelGridRound", "Round %n object(s) to the %1-unit grid", nullptr, objects).arg(coordinate(request.grid)),
				QCoreApplication::translate("VibeStudioLevelGridRound", "Put the rounded objects back"), error)) {
			return false;
		}
	}
	*document = std::move(working);
	result.skipped = skipped;
	if (report) {
		*report = result;
	}
	return true;
}

} // namespace vibestudio
