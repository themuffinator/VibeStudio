#pragma once

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QHash>
#include <QSet>
#include <array>

namespace vibestudio
{
// Original implementation. Interaction follows the Blender knife workflow
// (manual, reviewed 2026-10-21); see docs/CREDITS.md. No upstream code is used.
struct ModelKnifeSegment
{
	std::array<double, 2> a{}, b{};
	bool operator==(const ModelKnifeSegment &) const = default;
};
struct ModelKnifeOptions
{
	QVector<ModelKnifeSegment> segments;
	// Orthonormal camera axes. Orthographic coordinates are model units;
	// perspective coordinates are right/depth and up/depth (unit focal plane).
	std::array<double, 3> origin{0, 0, 0}, right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, -1};
	bool perspective = false, through = false, selectedOnly = false;
	// Hidden geometry neither receives cuts nor occludes visible geometry.
	// Boundary subdivisions still propagate to adjacent copies for conformity.
	QHash<int, QSet<int>> hiddenFaces;
	bool operator==(const ModelKnifeOptions &) const = default;
};
struct ModelKnifeCut
{
	int face = -1;
	// Coordinates on the original triangle: weights for B/C, A = 1-B-C.
	std::array<double, 2> a{}, b{};
};
// Plans visibility continuously along each finite stroke, including depth
// crossings. Other surfaces can occlude, but only the active surface is cut.
bool planModelKnife(const ModelMesh &mesh, int surface, int frame, const QSet<int> &selectedFaces, const ModelKnifeOptions &options,
					QVector<ModelKnifeCut> *cuts, QString *error, const ModelWorkControl &control = {});
} // namespace vibestudio
