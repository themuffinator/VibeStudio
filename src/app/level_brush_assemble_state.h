#pragma once

#include "core/level_brush_assemble.h"

#include <QString>
#include <algorithm>
#include <cmath>

namespace vibestudio {
// The Assemble Brush tool's points, shared by the plan views and the camera
// as the clip tool shares its points (app/level_clip_state.h). Each view
// keeps a copy; the shell propagates every change and checks the hull on a
// worker (app/level_brush_assemble_actions.cpp).
struct LevelBrushAssembleState {
	QVector<LevelMapVec3> points;
	[[nodiscard]] bool valid() const
	{
		return points.size() <= kLevelBrushPointsMaximum && std::all_of(points.cbegin(), points.cend(), [](const LevelMapVec3& p) {
			return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x) <= 32768 && std::abs(p.y) <= 32768
				&& std::abs(p.z) <= 32768;
		});
	}
	bool operator==(const LevelBrushAssembleState& other) const
	{
		return std::equal(points.cbegin(), points.cend(), other.points.cbegin(), other.points.cend(),
			[](const LevelMapVec3& a, const LevelMapVec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.valid == b.valid; });
	}
};

// The worker's verdict on the current points: the hull's wire and counts once
// it is checked, or why no brush can be made yet.
struct LevelBrushAssemblePreview {
	enum class Status { Empty, Pending, Ready, Refused };
	Status status = Status::Empty;
	LevelBrushAssembleShape shape = LevelBrushAssembleShape::Empty;
	QVector<QPair<LevelMapVec3, LevelMapVec3>> edges;
	int corners = 0;
	int faces = 0;
	QString message;
};
} // namespace vibestudio
