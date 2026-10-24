#pragma once
#include "core/level_map.h"
#include "core/model_transform.h"
#include <array>

namespace vibestudio {
// NRC TranslateFreeXY_Z in selection_mtable_translate.h at 449778b3840f8f2062daab15c87cb012f2ac945c
// (GPL-2.0-or-later), independently implemented; see docs/CREDITS.md.
// Pure point tracking: fixed press anchors, relative grid, live plane/axis choice.
// Neither scene picking nor document mutation belongs to this controller.
class LevelClipPointDrag {
public:
	bool begin(LevelMapVec3 point, ModelPickRay ray, int planeAxis, double grid);
	bool update(ModelPickRay ray, bool vertical, bool axisOnly, LevelMapVec3* point) const;
	[[nodiscard]] int planeAxis() const { return m_axis; }
private:
	bool pick(ModelPickRay ray, const std::array<double,3>& normal, LevelMapVec3* point) const;
	LevelMapVec3 m_origin, m_planeStart, m_axisStart;
	std::array<double,3> m_planeNormal{}, m_axisNormal{};
	double m_grid = 0;
	int m_axis = 2;
	bool m_started = false;
};
} // namespace vibestudio
