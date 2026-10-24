#pragma once

#include "core/level_map.h"
#include "core/map_geometry.h"
#include <algorithm>
#include <cmath>

namespace vibestudio {
// One world-space plane shared by plan and camera views. Two points extrude
// along the originating view's axis; a third point defines an arbitrary plane.
// Interaction behaviour follows the credited TrenchBroom / Radiant tools;
// geometry is always prepared by clipLevelMapSelection.
struct LevelClipState {
	QVector<LevelMapVec3> points;
	int axis = 2;
	int direction = 1;
	LevelMapClipKeep keep = LevelMapClipKeep::Back;
	static bool finite(const LevelMapVec3& p) {
		return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)
			&& std::abs(p.x) <= 1048576 && std::abs(p.y) <= 1048576 && std::abs(p.z) <= 1048576;
	}
	bool valid() const {
		return points.size() <= 3 && axis >= 0 && axis < 3 && (direction == 1 || direction == -1)
			&& (keep == LevelMapClipKeep::Back || keep == LevelMapClipKeep::Front || keep == LevelMapClipKeep::Both)
			&& std::all_of(points.cbegin(),points.cend(),finite);
	}
	bool plane(LevelMapVec3* a, LevelMapVec3* b, LevelMapVec3* c) const {
		return valid() && levelMapClipPlane(points,axis,direction,a,b,c);
	}
	bool actionable(bool wrapMerge) const { return valid() && !points.isEmpty() && (wrapMerge || plane(nullptr,nullptr,nullptr)); }
	bool operator==(const LevelClipState& other) const {
		return axis == other.axis && direction == other.direction && keep == other.keep && points.size() == other.points.size()
			&& std::equal(points.cbegin(),points.cend(),other.points.cbegin(),[](auto a,auto b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.valid == b.valid; });
	}
};

struct LevelClipPreview {
	enum class Status { Empty, Pending, Ready, Refused };
	Status status = Status::Empty;
	QVector<QPair<LevelMapVec3,LevelMapVec3>> edges;
	QString message;
	bool wrapped = false;
};
} // namespace vibestudio
