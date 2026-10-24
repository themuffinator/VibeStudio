#include "core/level_brush.h"
#include "core/level_scene_locks.h"
#include "core/level_placement_control_p.h"
#include <QCoreApplication>
#include <algorithm>
#include <cmath>

namespace vibestudio {
bool insertLevelMapVertices(LevelMapDocument* document, const QVector<LevelMapVertexInsertion>& insertions,
	double grid, bool allowCollapse, LevelMapComponentReport* report, QString* error)
{
	if (report) { *report = {}; }
	if (error) { error->clear(); }
	LevelMapComponentReport result;
	if (const auto guarded = guardLevelSceneEdit(document,error,[&](LevelMapDocument* candidate) {
		return insertLevelMapVertices(candidate,insertions,grid,allowCollapse,&result,error);
	}); guarded.has_value()) {
		if (*guarded && report) { *report = result; }
		return *guarded;
	}
	const auto fail = [error](const char* message) {
		if (error) { *error = QCoreApplication::translate("LevelBrush",message); }
		return false;
	};
	if (!document || (document->format != LevelMapFormat::QuakeMap && document->format != LevelMapFormat::Quake3Map)) {
		return fail(QT_TRANSLATE_NOOP("LevelBrush","Open a Quake-family map to insert brush vertices."));
	}
	if (insertions.isEmpty() || insertions.size() > 256 || !std::isfinite(grid) || grid < 0 || grid > 32768) {
		return fail(QT_TRANSLATE_NOOP("LevelBrush","Choose 1 to 256 brushes and a finite insertion grid from 0 to 32768 units."));
	}
	QMap<int,LevelMapBrush> drafts;
	for (const auto& insertion : insertions) {
		detail::placementCancellationCheckpoint();
		const auto found = std::find_if(document->brushes.cbegin(),document->brushes.cend(),[&](const auto& brush) { return brush.id == insertion.brushId; });
		if (found == document->brushes.cend() || drafts.contains(insertion.brushId)) {
			return fail(QT_TRANSLATE_NOOP("LevelBrush","A brush to edit is missing or named twice."));
		}
		auto draft = *found; LevelBrushEditReport edit; QString problem;
		if (!insertLevelBrushVertices(&draft,insertion.positions,grid,allowCollapse,&edit,&problem)) {
			if (error) { *error = QCoreApplication::translate("LevelBrush","Brush %1: %2").arg(insertion.brushId).arg(problem); }
			return false;
		}
		drafts.insert(insertion.brushId,std::move(draft));
		++result.brushes; result.insertedVertices += edit.insertedVertices; result.collapsedVertices += edit.collapsedVertices;
	}
	if (!replaceLevelMapComponentGeometry(document,drafts,{},QCoreApplication::translate("LevelBrush","Insert vertices in %n brush(es)",nullptr,result.brushes),error)) { return false; }
	if (report) { *report = result; }
	return true;
}
} // namespace vibestudio
