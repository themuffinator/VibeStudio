#pragma once

#include "core/model_boundary_fill.h"

namespace vibestudio
{
// Internal shared budget for lookup, triangulation and every-pose validation.
struct ModelBoundaryWork
{
	ModelWorkProgress progress;
	QString *error;
	qint64 checks = 0;
	bool stopped = false;
	bool step();
};
struct ModelBoundaryLoops
{
	// Directed opposite to the incident faces; canonical lowest vertex first.
	QVector<QVector<int>> loops;
	QVector<ModelEdge> edges;
	QSet<ModelEdge> existing;
};
bool findModelBoundaryLoops(const ModelSurface &source, const QSet<ModelEdge> &selected,
	ModelBoundaryLoops *result, ModelBoundaryWork &work);
// Proposed indices must be valid for the source. Checks geometric collapse,
// new/new and new/existing intersections, including distinct seam indices.
bool validateModelBoundaryFaces(const ModelSurface &source, const QVector<ModelTriangle> &added, ModelBoundaryWork &work);
} // namespace vibestudio
