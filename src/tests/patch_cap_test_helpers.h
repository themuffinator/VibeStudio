#pragma once
#include "core/level_patch_cap.h"
#include "tests/patch_stitch_test_helpers.h"

namespace vibestudio::tests
{
inline LevelMapPatch capSource(bool columns = false, bool open = false)
{
	LevelMapPatch p;
	LevelPatchCreateRequest create;
	create.shape = QStringLiteral("cylinder");
	create.texture = QStringLiteral("studio/grid");
	createLevelPatch(create, &p);
	p.id = 0;
	p.entityId = 0;
	if (open) {
		p.width = 3;
		p.controlPoints.clear();
		p.controlU.clear();
		p.controlV.clear();
		for (int r = 0; r < 3; ++r) {
			for (int c = 0; c < 3; ++c) {
				p.controlPoints << LevelMapVec3{(c - 1) * 64.0, c == 1 ? 64.0 : 0.0, (r - 1) * 64.0, true};
				p.controlU << c * 0.5;
				p.controlV << r * 0.5;
			}
		}
	}
	if (columns) {
		auto copy = p;
		p.width = copy.height;
		p.height = copy.width;
		for (int r = 0; r < p.height; ++r) {
			for (int c = 0; c < p.width; ++c) {
				const int dest = r * p.width + c, source = c * copy.width + r;
				p.controlPoints[dest] = copy.controlPoints[source];
				p.controlU[dest] = copy.controlU[source];
				p.controlV[dest] = copy.controlV[source];
			}
		}
	}
	refreshLevelPatchBounds(&p);
	return p;
}
inline bool createCapMap(LevelMapDocument *map, QString *error = nullptr)
{
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake3");
	create.starterRoom = false;
	return createLevelMap(create, map, error) && addLevelMapPatch(map, capSource(), nullptr, error);
}
} // namespace vibestudio::tests
