#pragma once

#include "core/model_boundary_fill.h"

namespace vibestudio
{
struct ModelBoundaryBridgeReport
{
	int firstAnchor = -1, secondAnchor = -1;
	QVector<ModelEdge> boundaryEdges;
	QVector<int> faces;
};

// One or more seed edges from each of exactly two disjoint closed boundaries.
// Unequal vertex counts are supported. The reference pose chooses a closest
// anchor pair and deterministic minimum-cross-edge-cost strips within two
// seam orientations; twist advances the second anchor around its aligned loop. Existing vertex
// attributes stay exact, and the new strip must be valid in every stored pose.
// No source/result/report mutation on failure, including result == &source.
bool bridgeModelBoundaryLoops(const ModelSurface &source, const QSet<ModelEdge> &selected, int referenceFrame, int twist,
	int triangleLimit, ModelSurface *result, ModelBoundaryBridgeReport *report, QString *error,
	const ModelWorkControl &control = {});
} // namespace vibestudio
