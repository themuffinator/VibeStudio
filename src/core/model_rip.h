#pragma once

#include "core/model_document.h"
#include "core/model_geometric_topology.h"

namespace vibestudio
{
// A deterministic side of a connected vertex/edge rip. Planning uses one pose;
// generated topology and skin bindings retain every pose. Faces are never
// implicitly selected as the object of a rip.
struct ModelRipPlan
{
	ModelGeometricTopology geometry;
	QSet<int> groups, movingFaces;
	QSet<ModelEdge> selectedEdges, cuts;
	int sideFace = -1;
	bool vertexSelection = false;
};
bool planModelRip(const ModelSurface &surface, const ModelSelection &selection, const ModelMeshToolOptions &options, ModelRipPlan *result,
				  QString *error = nullptr, const ModelWorkControl &control = {});
} // namespace vibestudio
