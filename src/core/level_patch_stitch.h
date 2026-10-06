#pragma once

#include "core/level_patch.h"
#include <QJsonObject>
#include <functional>

namespace vibestudio
{
enum class LevelPatchBoundary { FirstRow, LastRow, FirstColumn, LastColumn };
enum class LevelPatchStitchOrder { Automatic, Forward, Reversed };
enum class LevelPatchStitchTarget { First, Second, Average };
enum class LevelPatchStitchUv { Preserve, First, Second, Average };
struct LevelPatchStitchRequest {
	LevelPatchBoundary first = LevelPatchBoundary::LastColumn;
	LevelPatchBoundary second = LevelPatchBoundary::FirstColumn;
	LevelPatchStitchOrder order = LevelPatchStitchOrder::Automatic;
	LevelPatchStitchTarget target = LevelPatchStitchTarget::Average;
	LevelPatchStitchUv uv = LevelPatchStitchUv::Preserve;
	double maxDistance = 8;
	bool matchTangents = false;
};
struct LevelPatchStitchResult {
	LevelMapPatch first, second;
	int boundaryPoints = 0;
	bool reversed = false;
	double maxGap = 0, maxMovement = 0;
	QStringList warnings;
};
QString levelPatchBoundaryId(LevelPatchBoundary edge);
bool parseLevelPatchBoundary(const QString &id, LevelPatchBoundary *edge);
QVector<int> levelPatchBoundaryIndices(const LevelMapPatch &patch, LevelPatchBoundary edge, bool inner = false);
// Exact uniform quadratic refinement along an axis. The requested segment count
// must be a multiple of the current count, with at most 31 control points.
// Geometry, UVs, other-axis control lines, dialect and source metadata survive.
bool refineLevelPatch(LevelMapPatch *patch, bool columns, int segments, QString *error = nullptr);
// Immutable preparation; all outputs are assigned only on success. Patches must
// be distinct and share an owner. Common segmentation uses the least common
// multiple, never an approximation; an over-limit grid is refused.
bool prepareLevelPatchStitch(const LevelMapPatch &first, const LevelMapPatch &second, const LevelPatchStitchRequest &request,
							 LevelPatchStitchResult *result, QString *error = nullptr, const std::function<bool()> &cancelled = {});
// Shared GUI/CLI document operation. Both patches are replaced in one undo step.
bool stitchLevelMapPatches(LevelMapDocument *document, int firstId, int secondId, const LevelPatchStitchRequest &request,
						   LevelPatchStitchResult *result = nullptr, QString *error = nullptr);
QJsonObject levelPatchStitchReportJson(const LevelPatchStitchResult &result);
} // namespace vibestudio
