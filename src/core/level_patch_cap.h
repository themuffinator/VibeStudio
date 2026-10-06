#pragma once

#include "core/level_patch_stitch.h"
#include <QJsonObject>

namespace vibestudio
{

enum class LevelPatchCapUv { Planar, Boundary };

struct LevelPatchCapRequest {
	QVector<LevelPatchBoundary> boundaries{LevelPatchBoundary::FirstRow, LevelPatchBoundary::LastRow};
	QString texture; // Empty retains the source material.
	LevelPatchCapUv uv = LevelPatchCapUv::Planar;
	double unitsPerTile = 128;
	bool customCenter = false; // One boundary only; open arcs always close on their chord.
	LevelMapVec3 center{0, 0, 0, true};
	bool invert = false;
};

struct LevelPatchCapResult {
	QVector<LevelMapPatch> caps;
	QVector<LevelPatchBoundary> boundaries;
	QVector<LevelMapVec3> centers;
	QVector<bool> closed;
	QVector<bool> reversed;
	QStringList warnings;
};

// Original radial quadratic construction. The outer row retains the exact
// boundary curve. Planarity and monotone polar control directions certify that
// the fan cannot fold or cover itself. No source patch is modified.
bool prepareLevelPatchCaps(const LevelMapPatch &source, const LevelPatchCapRequest &request, LevelPatchCapResult *result,
						   QString *error = nullptr, const std::function<bool()> &cancelled = {});
bool capLevelMapPatch(LevelMapDocument *document, int patchId, const LevelPatchCapRequest &request, LevelPatchCapResult *result = nullptr,
					  QString *error = nullptr);
QJsonObject levelPatchCapReportJson(const LevelPatchCapResult &result);

} // namespace vibestudio
