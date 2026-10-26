#include "core/level_placement.h"
#include "core/level_repeat.h"
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
	case LevelPlacementPhase::Deleting:
		return QCoreApplication::translate("VibeStudioLevelPlacement", "Preparing deletion…");
	case LevelPlacementPhase::Connecting:
		return QCoreApplication::translate("VibeStudioLevelPlacement", "Connecting entities…");
	case LevelPlacementPhase::Visibility:
		return QCoreApplication::translate("VibeStudioLevelPlacement", "Updating object visibility…");
	case LevelPlacementPhase::SelectingObjects:
		return QCoreApplication::translate("VibeStudioLevelPlacement", "Finding selectable objects…");
	case LevelPlacementPhase::Selecting:
		return QCoreApplication::translate("VibeStudioLevelPlacement", "Testing selection volumes…");
	case LevelPlacementPhase::Copying:
		return QCoreApplication::translate("VibeStudioLevelPlacement", "Preparing copied objects…");
	case LevelPlacementPhase::Cutting:
		return QCoreApplication::translate("VibeStudioLevelPlacement", "Preparing cut objects…");
	case LevelPlacementPhase::Preparing:
		return QCoreApplication::translate("VibeStudioLevelPlacement", "Preparing map edit…");
	case LevelPlacementPhase::Extracting:
		return QCoreApplication::translate("VibeStudioLevelPlacement", "Splitting linked groups…");
	case LevelPlacementPhase::Repeating:
		return QCoreApplication::translate("VibeStudioLevelPlacement", "Repeating commands…");
	case LevelPlacementPhase::Building:
		return QCoreApplication::translate("VibeStudioLevelPlacement", "Building brush geometry…");
	case LevelPlacementPhase::Arraying:
		return QCoreApplication::translate("VibeStudioLevelPlacement", "Preparing array copies…");
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
		case LevelPlacementOperation::DeleteObjects:
			detail::placementCheckpoint(LevelPlacementPhase::Deleting);
			applied = deleteLevelMapSelection(&candidate, &result.error);
			break;
		case LevelPlacementOperation::RemoveComponents:
			applied = removeLevelMapComponents(&candidate, request.components, &result.componentReport, &result.error);
			break;
		case LevelPlacementOperation::RoundToGrid:
			detail::placementCheckpoint(LevelPlacementPhase::Transforming);
			applied = roundLevelMapSelectionToGrid(&candidate, request.gridRound, &result.gridRoundReport, &result.error);
			break;
		case LevelPlacementOperation::SelectTextured:
			applied = selectLevelMapTextured(&candidate,request.texturedSelection,&result.texturedSelectionReport,&result.error);
			break;
		case LevelPlacementOperation::ConnectEntities:
			detail::placementCheckpoint(LevelPlacementPhase::Connecting);
			applied = connectLevelMapEntities(&candidate,request.connection,&result.connectionReport,&result.error);
			break;
		case LevelPlacementOperation::Visibility:
			applied = changeLevelMapVisibility(&candidate, request.visibility, &result.visibilityReport, &result.error);
			break;
		case LevelPlacementOperation::SelectObjects:
			applied = selectLevelMapObjects(&candidate, request.objectSelection, &result.objectSelectionReport, &result.error);
			break;
		case LevelPlacementOperation::SelectRegion:
			applied = selectLevelMapByRegion(&candidate, request.region, &result.regionReport, &result.error);
			break;
		case LevelPlacementOperation::Copy:
		case LevelPlacementOperation::Cut:
			detail::placementCheckpoint(LevelPlacementPhase::Copying);
			result.clipboardText = levelMapSelectionText(candidate, &result.error);
			applied = !result.clipboardText.isEmpty();
			if (applied && request.operation == LevelPlacementOperation::Cut) {
				detail::placementCheckpoint(LevelPlacementPhase::Cutting);
				applied = deleteLevelMapSelection(&candidate, &result.error);
			}
			break;
		case LevelPlacementOperation::Repeat:
			applied = repeatLevelMapCommands(&candidate, request.textures, &result.error);
			break;
		case LevelPlacementOperation::RepeatTransforms:
			applied = repeatLevelMapTransforms(&candidate, request.textures, request.repeatPivot, &result.repeatPivot, &result.error);
			break;
		case LevelPlacementOperation::MoveToCamera:
			detail::placementCheckpoint(LevelPlacementPhase::Transforming);
			applied = moveLevelMapSelectionToCamera(&candidate, request.paste, request.textures, &result.pasteOffset, &result.error);
			break;
		case LevelPlacementOperation::ExtractLinkedGroups:
			applied = extractLevelLinkedGroups(&candidate, &result.extractionReport, &result.error);
			break;
		case LevelPlacementOperation::Sweep:
			applied = sweepLevelMap(&candidate,request.sweep,&result.sweepReport,&result.error);
			break;
		case LevelPlacementOperation::WrapMerge:
			applied = wrapLevelMap(&candidate,request.wrap,&result.wrapReport,&result.error);
			break;
		case LevelPlacementOperation::TransformComponents:
			applied = request.surfaceFaces.faces.isEmpty()
				? transformLevelMapComponents(&candidate, request.components, request.componentTransform, &result.componentReport, &result.error)
				: transformLevelMapSurfaceFaces(&candidate, request.surfaceFaces, request.components, request.componentTransform,
					&result.surfaceFaceReport, &result.error);
			if (applied && !request.surfaceFaces.faces.isEmpty()) { result.componentReport = result.surfaceFaceReport.components; }
			break;
		case LevelPlacementOperation::AddBrush:
			applied = addLevelMapBrushPrimitive(&candidate, request.primitive, nullptr, &result.error);
			break;
		case LevelPlacementOperation::AddEntity:
			applied = addLevelMapEntity(&candidate, request.className, request.offset, request.properties, nullptr, &result.error);
			break;
		case LevelPlacementOperation::CreateEntity:
			detail::placementCheckpoint(LevelPlacementPhase::Inserting);
			applied = createLevelMapEntityOfClass(&candidate, request.entityCreate, &result.entityCreateReport, &result.error);
			break;
		case LevelPlacementOperation::AddThing:
			applied = addLevelMapDoomThing(&candidate, request.thingType, request.offset.x, request.offset.y, request.thingAngle, nullptr, &result.error);
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
		case LevelPlacementOperation::Scale:
			applied = scaleLevelMapSelection(&candidate, request.scale, &result.error);
			break;
		case LevelPlacementOperation::Snap:
			applied = snapLevelMapSelectionToGrid(&candidate, request.grid, request.textures, &result.error);
			break;
		case LevelPlacementOperation::Duplicate:
			applied = arrayLevelMapSelection(&candidate, request.offset, request.copies, request.textures, &result.error, request.duplicateMode);
			break;
		case LevelPlacementOperation::Paste: {
			auto placement = request.paste;
			if (placement.mode == LevelPasteMode::Offset) { placement.offset = request.offset; }
			applied = pasteLevelMapTextAt(&candidate, request.text, placement, request.textures, &result.error, &result.pasteOffset);
			break;
		}
		case LevelPlacementOperation::Mirror:
			applied = (!request.connectedGeometry || selectConnectedLevelMapDoomGeometry(&candidate, &result.error)) &&
					  flipLevelMapSelection(&candidate, request.axis, request.textures, request.mirrorPivot, &result.error);
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
	if (!result.succeeded) { result.clipboardText.clear(); result.regionReport = {}; result.objectSelectionReport = {}; result.texturedSelectionReport = {}; result.visibilityReport = {}; result.connectionReport = {}; result.componentReport = {}; result.gridRoundReport = {}; result.surfaceFaceReport = {}; result.entityCreateReport = {}; }
	return result;
}
} // namespace vibestudio
