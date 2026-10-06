#include "core/level_placement.h"
#include "core/level_doom_selection.h"
#include "core/level_placement_control_p.h"
#include <QCoreApplication>
#include <exception>

namespace vibestudio {
namespace detail {
namespace {
thread_local const LevelPlacementControl* activeControl = nullptr;
}
PlacementControlScope::PlacementControlScope(const LevelPlacementControl& control) : m_previous(activeControl) { activeControl = &control; }
PlacementControlScope::~PlacementControlScope() { activeControl = m_previous; }
void placementCancellationCheckpoint() {
	if (activeControl && activeControl->isCancelled && activeControl->isCancelled()) {
		throw PlacementCancelled{};
	}
}
void placementCheckpoint(LevelPlacementPhase phase, qint64 completed, qint64 total) {
	placementCancellationCheckpoint();
	if (activeControl && activeControl->progress) {
		activeControl->progress({phase, completed, total});
	}
	placementCancellationCheckpoint();
}
} // namespace detail

QString levelPlacementPhaseName(LevelPlacementPhase phase) {
	switch (phase) {
	case LevelPlacementPhase::Preparing:
		return QCoreApplication::translate("VibeStudioLevelPlacement", "Preparing map edit…");
	case LevelPlacementPhase::Building:
		return QCoreApplication::translate("VibeStudioLevelPlacement", "Building brush geometry…");
	case LevelPlacementPhase::Parsing:
		return QCoreApplication::translate("VibeStudioLevelPlacement", "Reading pasted geometry…");
	case LevelPlacementPhase::Transforming:
		return QCoreApplication::translate("VibeStudioLevelPlacement", "Transforming objects…");
	case LevelPlacementPhase::Inserting:
		return QCoreApplication::translate("VibeStudioLevelPlacement", "Preparing inserted objects…");
	case LevelPlacementPhase::Finalizing:
		return QCoreApplication::translate("VibeStudioLevelPlacement", "Preparing history and scene…");
	case LevelPlacementPhase::Complete:
		return QCoreApplication::translate("VibeStudioLevelPlacement", "Placement ready.");
	}
	return {};
}
LevelPlacementResult prepareLevelPlacement(const LevelMapDocument& source, const LevelPlacementRequest& request,
										   const LevelPlacementControl& control) {
	LevelPlacementResult result;
	detail::PlacementControlScope scope(control);
	try {
		detail::placementCheckpoint(LevelPlacementPhase::Preparing);
		auto candidate = source;
		bool applied = false;
		switch (request.operation) {
		case LevelPlacementOperation::AddBrush:
			applied = addLevelMapBrushPrimitive(&candidate, request.primitive, nullptr, &result.error);
			break;
		case LevelPlacementOperation::Move:
			applied = request.snapMoveDelta ? moveLevelMapSelectionSnapped(&candidate, request.offset.x, request.offset.y, request.offset.z,
																		   request.grid, request.textures, &result.error)
											: moveLevelMapSelection(&candidate, request.offset.x, request.offset.y, request.offset.z,
																	request.textures, &result.error);
			break;
		case LevelPlacementOperation::Rotate:
			applied = rotateLevelMapSelection(&candidate, request.rotation, &result.error);
			break;
		case LevelPlacementOperation::Resize:
			applied = resizeLevelMapSelection(&candidate, request.mins, request.maxs, request.textures, &result.error);
			break;
		case LevelPlacementOperation::Snap:
			applied = snapLevelMapSelectionToGrid(&candidate, request.grid, request.textures, &result.error);
			break;
		case LevelPlacementOperation::Duplicate:
			applied = duplicateLevelMapSelection(&candidate, request.offset.x, request.offset.y, request.offset.z, request.textures,
												 &result.error);
			break;
		case LevelPlacementOperation::Paste:
			applied = pasteLevelMapText(&candidate, request.text, request.offset, request.textures, &result.error);
			break;
		case LevelPlacementOperation::Mirror:
			applied = (!request.connectedGeometry || selectConnectedLevelMapDoomGeometry(&candidate, &result.error)) &&
					  flipLevelMapSelection(&candidate, request.axis, request.textures, &result.error);
			break;
		case LevelPlacementOperation::SelectConnectedDoom:
			applied = selectConnectedLevelMapDoomGeometry(&candidate, &result.error);
			break;
		}
		detail::placementCancellationCheckpoint();
		if (applied) {
			detail::placementCheckpoint(LevelPlacementPhase::Complete, 1, 1);
			result.document = std::move(candidate);
			result.succeeded = true;
		}
	} catch (const detail::PlacementCancelled&) {
		result.cancelled = true;
		result.error = QCoreApplication::translate("VibeStudioLevelPlacement", "Placement cancelled. The map was not changed.");
	} catch (const std::exception&) {
		result.error = QCoreApplication::translate("VibeStudioLevelPlacement", "Placement could not be prepared. The map was not changed.");
	}
	return result;
}
} // namespace vibestudio
