#pragma once
#include "core/level_document.h"
#include "core/level_patch_stitch.h"
#include <array>
#include <cmath>

namespace vibestudio::tests
{
inline LevelMapPatch stitchPatch(LevelPatchBoundary edge, int id, bool second = false, bool reversed = false)
{
	LevelPatchCreateRequest create;
	create.texture = QStringLiteral("studio/grid");
	LevelMapPatch p;
	createLevelPatch(create, &p);
	p.id = id;
	p.entityId = 0;
	const bool rows = edge == LevelPatchBoundary::FirstRow || edge == LevelPatchBoundary::LastRow;
	const bool last = edge == LevelPatchBoundary::LastRow || edge == LevelPatchBoundary::LastColumn;
	for (int r = 0; r < 3; ++r) {
		for (int c = 0; c < 3; ++c) {
			const int along = rows ? c : r, across = rows ? r : c, away = last ? 2 - across : across;
			const double y = (along - 1) * 64.0 * (reversed ? -1 : 1);
			p.controlPoints[r * 3 + c] = {(second ? 4.0 : 0.0) + (second ? 1 : -1) * away * 64.0, y, along == 1 ? 48.0 : 0.0, true};
			p.controlU[r * 3 + c] = (second ? 3 : 0) + along * 0.5;
			p.controlV[r * 3 + c] = away * 0.5;
		}
	}
	refreshLevelPatchBounds(&p);
	return p;
}
// Direct tensor-product Bernstein evaluation, independent of the refinement
// implementation's restricted-curve control-point formulas.
inline std::array<double, 5> samplePatch(const LevelMapPatch &p, double u, double v)
{
	const int nx = (p.width - 1) / 2, ny = (p.height - 1) / 2;
	const int sx = std::min(static_cast<int>(u * nx), nx - 1), sy = std::min(static_cast<int>(v * ny), ny - 1);
	const double x = u * nx - sx, y = v * ny - sy;
	const double a[3]{(1 - x) * (1 - x), 2 * x * (1 - x), x * x}, b[3]{(1 - y) * (1 - y), 2 * y * (1 - y), y * y};
	std::array<double, 5> out{};
	for (int r = 0; r < 3; ++r) {
		for (int c = 0; c < 3; ++c) {
			const int i = (sy * 2 + r) * p.width + sx * 2 + c;
			const auto q = p.controlPoints[i];
			const double w = a[c] * b[r];
			out[0] += q.x * w;
			out[1] += q.y * w;
			out[2] += q.z * w;
			out[3] += p.controlU[i] * w;
			out[4] += p.controlV[i] * w;
		}
	}
	return out;
}
inline bool createStitchMap(LevelMapDocument *map, QString *error = nullptr)
{
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake3");
	create.starterRoom = false;
	if (!createLevelMap(create, map, error)) {
		return false;
	}
	auto first = stitchPatch(LevelPatchBoundary::LastColumn, 0), second = stitchPatch(LevelPatchBoundary::FirstColumn, 1, true);
	second.textureName = QStringLiteral("studio/shader");
	if (!refineLevelPatch(&first, false, 2, error) || !refineLevelPatch(&second, false, 3, error) ||
		!addLevelMapPatch(map, first, nullptr, error) || !addLevelMapPatch(map, second, nullptr, error)) {
		return false;
	}
	setLevelMapSelection(map, {{LevelMapSelectionKind::QuakePatch, 0}, {LevelMapSelectionKind::QuakePatch, 1}});
	return true;
}
} // namespace vibestudio::tests
