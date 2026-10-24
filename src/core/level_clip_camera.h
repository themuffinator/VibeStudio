#pragma once
#include "core/level_map.h"

namespace vibestudio {
// NRC selection_mtor_clip.cpp viewdir_set/fixup/make_cut_worthy at
// 449778b3840f8f2062daab15c87cb012f2ac945c (GPL-2.0-or-later).
// Independently implemented; see docs/CREDITS.md. This chooses the synthetic
// third-point extrusion, never changes either actual point or the document.
// preserveAxis retains an existing two-point plane unless it becomes parallel
// to the segment or only touches an axial outer selection face. Camera motion
// alone does not change a stored plane. Invalid input leaves outputs unchanged.
bool levelCameraClipAxis(const QVector<LevelMapVec3>& points,
	const LevelMapVec3& eye, const LevelMapVec3& forward,
	const LevelMapVec3& mins, const LevelMapVec3& maxs,
	int* axis, int* direction, bool preserveAxis = false, QString* error = nullptr);
} // namespace vibestudio
