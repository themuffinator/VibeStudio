#pragma once
#include "core/level_linked_extraction.h"

#include "core/level_map.h"
#include "core/level_primitive.h"
#include "core/level_brush.h"
#include "core/level_grid_round.h"
#include "core/level_wrap.h"
#include "core/level_sweep.h"
#include "core/level_paste.h"
#include "core/level_region_selection.h"
#include "core/level_object_selection.h"
#include "core/level_textured_selection.h"
#include "core/level_visibility.h"
#include "core/level_entity_connections.h"
#include "core/level_entity_create.h"
#include <functional>

namespace vibestudio {
enum class LevelPlacementOperation { Snap, Duplicate, Paste, Mirror, SelectConnectedDoom, Move, Rotate, Resize, AddBrush, AddEntity, AddThing, TransformComponents, WrapMerge, Sweep, ExtractLinkedGroups, Repeat, Copy, Cut, SelectRegion, SelectObjects, Visibility, ConnectEntities, SelectTextured, DeleteObjects, RemoveComponents, RoundToGrid, CreateEntity, MoveToCamera, RepeatTransforms };
enum class LevelPlacementPhase { Preparing, Parsing, Transforming, Inserting, Finalizing, Complete, Building, Arraying, Extracting, Repeating, Copying, Cutting, Selecting, SelectingObjects, Visibility, Connecting, Deleting };

struct LevelPlacementRequest {
	LevelPlacementOperation operation = LevelPlacementOperation::Duplicate;
	LevelMapVec3 offset{0, 0, 0, true};
	double grid = 16;
	QString text;
	LevelMapTextureLockOptions textures;
	// Additional copies, with cumulative offsets from the original selection.
	int copies = 1;
	LevelDuplicateMode duplicateMode = LevelDuplicateMode::Ordinary;
	int axis = 0;
	bool connectedGeometry = false;
	// Mirror: the plane passes through this point when it is valid (only its
	// `axis` coordinate matters), otherwise through the centre of what moves.
	LevelMapVec3 mirrorPivot;
	LevelMapRotationRequest rotation;
	LevelMapVec3 mins, maxs;
	bool snapMoveDelta = false;
	LevelBrushPrimitiveRequest primitive;
	QString className;
	QVector<LevelMapProperty> properties;
	int thingType = 0, thingAngle = 0;
	QVector<LevelMapComponentSet> components;
	LevelComponentTransformRequest componentTransform;
	LevelWrapRequest wrap;
	LevelSweepRequest sweep;
	LevelPastePlacement paste;
	LevelRegionSelectionRequest region;
	LevelObjectSelectionRequest objectSelection;
	LevelTexturedSelectionRequest texturedSelection;
	// TransformComponents only: independent face picks transformed with `components`.
	LevelSurfaceFacePicks surfaceFaces;
	LevelVisibilityRequest visibility;
	LevelEntityConnectionRequest connection;
	LevelMapGridRoundRequest gridRound;
	// CreateEntity: a generated Create <class> command (core/level_entity_create.h).
	LevelEntityCreateRequest entityCreate;
	// RepeatTransforms: the pivot of NetRadiant Custom's Repeat Transforms; invalid
	// for the centre of the copies (core/level_repeat.h). MoveToCamera uses `paste`.
	LevelMapVec3 repeatPivot;
};
struct LevelPlacementProgress {
	LevelPlacementPhase phase = LevelPlacementPhase::Preparing;
	qint64 completed = 0, total = 0;
};
struct LevelPlacementControl {
	// Invoked synchronously on the calling worker. Callbacks must not mutate
	// the source or access GUI objects. Cancellation discards the whole candidate.
	std::function<bool()> isCancelled;
	std::function<void(const LevelPlacementProgress&)> progress;
};
struct LevelPlacementResult {
	LevelMapDocument document;
	LevelWrapReport wrapReport;
	LevelSweepReport sweepReport;
	LevelLinkedExtractionReport extractionReport;
	LevelRegionSelectionReport regionReport;
	LevelObjectSelectionReport objectSelectionReport;
	LevelTexturedSelectionReport texturedSelectionReport;
	LevelSurfaceFaceTransformReport surfaceFaceReport;
	LevelVisibilityReport visibilityReport;
	LevelEntityConnectionReport connectionReport;
	LevelMapComponentReport componentReport;
	LevelMapGridRoundReport gridRoundReport;
	LevelEntityCreateReport entityCreateReport;
	// Paste and MoveToCamera: the translation applied.
	LevelMapVec3 pasteOffset;
	// RepeatTransforms: the pivot the copies turned and scaled about.
	LevelMapVec3 repeatPivot;
	// Copy/Cut only, and only on success. Callers publish this before adopting
	// a Cut candidate so failed clipboard publication cannot delete objects.
	QString clipboardText;
	QString error;
	bool succeeded = false, cancelled = false;
};

// Runs the normal authoring/scene-lock services on a private candidate. Source
// and history remain untouched, including cancellation during finalization.
// No files or package drafts are modified. Publication is the caller's job.
LevelPlacementResult prepareLevelPlacement(const LevelMapDocument& source, const LevelPlacementRequest& request,
										   const LevelPlacementControl& control = {});
QString levelPlacementPhaseName(LevelPlacementPhase phase);
} // namespace vibestudio
