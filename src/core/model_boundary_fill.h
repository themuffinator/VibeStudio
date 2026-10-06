#pragma once

#include "core/model_topology.h"

namespace vibestudio
{
inline constexpr int modelBoundaryMaxLoopVertices = 1024;
inline constexpr qint64 modelBoundaryMaxChecks = 16 * 1024 * 1024;

struct ModelBoundaryFillReport
{
	int loopCount = 0;
	QVector<ModelEdge> boundaryEdges;
	QVector<int> faces;
};

// Select at least one indexed boundary edge per complete loop. The reference
// pose chooses a deterministic triangulation; every pose must support that
// same cap without overlaps with this surface. Coincident seam copies remain
// separate. No positions, normals, UVs, materials or seam marks are changed.
// Input must pass editable-model validation. Failure/cancellation leaves both
// output values untouched, including when result aliases source.
bool fillModelBoundaryLoops(const ModelSurface &source, const QSet<ModelEdge> &selected, int referenceFrame, int triangleLimit,
							ModelSurface *result, ModelBoundaryFillReport *report, QString *error, const ModelWorkControl &control = {});
} // namespace vibestudio
