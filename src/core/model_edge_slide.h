#pragma once

#include "core/model_mesh.h"
#include "core/model_topology.h"
#include "core/model_work.h"

#include <array>

namespace vibestudio
{
// A deterministic rail pair for one geometric vertex. Copies share positions
// in every pose but keep independent UVs, normals and skin influences.
struct ModelEdgeSlideVertex
{
	int vertex = -1;
	std::array<int, 2> rails{-1, -1}; // positive, negative; -1 at a one-sided border
	QVector<int> copies;
};
struct ModelEdgeSlidePlan
{
	QVector<ModelEdgeSlideVertex> vertices;
	int chains = 0;
};
// Selected non-branching chains/loops follow sides of the existing quad view.
// Open chains start at their lowest endpoint; closed loops start at their
// lowest vertex towards its lowest neighbour. The winding side is positive.
// A one-sided chain starts with its available side positive. Each side must
// give the same rail at adjoining selected edges, otherwise the edit refuses.
bool planModelEdgeSlide(const ModelSurface &surface, const QSet<ModelEdge> &edges, int frame, double maxAngle, ModelEdgeSlidePlan *result,
						QString *error = nullptr, const ModelWorkControl &control = {});
} // namespace vibestudio
