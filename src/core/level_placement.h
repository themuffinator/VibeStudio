#pragma once

#include "core/level_map.h"
#include "core/level_primitive.h"
#include <functional>

namespace vibestudio {
enum class LevelPlacementOperation { Snap, Duplicate, Paste, Mirror, SelectConnectedDoom, Move, Rotate, Resize, AddBrush };
enum class LevelPlacementPhase { Preparing, Parsing, Transforming, Inserting, Finalizing, Complete, Building };

struct LevelPlacementRequest {
	LevelPlacementOperation operation = LevelPlacementOperation::Duplicate;
	LevelMapVec3 offset{0, 0, 0, true};
	double grid = 16;
	QString text;
	LevelMapTextureLockOptions textures;
	int axis = 0;
	bool connectedGeometry = false;
	LevelMapRotationRequest rotation;
	LevelMapVec3 mins, maxs;
	bool snapMoveDelta = false;
	LevelBrushPrimitiveRequest primitive;
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
