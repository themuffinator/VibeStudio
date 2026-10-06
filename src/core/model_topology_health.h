#pragma once

#include "core/model_topology.h"

namespace vibestudio
{
struct ModelHealthEdge
{
	ModelEdge vertices;
	QVector<int> faces;
	bool inconsistentWinding = false;
};
struct ModelVertexFans
{
	int vertex = -1;
	QVector<QVector<int>> faces;
};
struct ModelTopologyHealth
{
	QVector<ModelHealthEdge> edges;
	QVector<ModelEdge> boundaryEdges, nonmanifoldEdges, windingEdges;
	QVector<QVector<int>> duplicateFaces;
	QVector<int> unusedVertices;
	QVector<ModelVertexFans> disconnectedFans;
	int faceComponents = 0;
};
enum class ModelTopologyRepair
{
	RemoveDuplicateFaces,
	RemoveUnusedVertices,
	SplitDisconnectedFans,
	OrientFaces,
	SplitNonmanifoldEdges
};

// Indexed topology only: coincident seam copies stay distinct, and boundaries
// are informational. Validated editable geometry is required by repair callers.
// All results and maps remain untouched on cancellation or failure.
bool inspectModelTopology(const ModelSurface &surface, ModelTopologyHealth *result, QString *error = nullptr,
						  const ModelWorkControl &control = {});
// Repairs cover the complete surface. Maps relate old vertices to all surviving
// copies, and old faces to their surviving representative (-1 if removed).
// Positions, UVs and every pose's normals are preserved exactly. Orienting faces
// retains each edge-connected component's lowest face as its orientation seed;
// it neither infers outward direction nor regenerates authored normals.
// Nonmanifold splitting connects face corners only through two-face edges at
// affected endpoints. It preserves those unambiguous connections, creates
// boundary copies where needed, and leaves unrelated disconnected fans alone.
bool repairModelTopology(const ModelSurface &source, ModelTopologyRepair repair, int vertexLimit, ModelSurface *result,
						 QVector<QVector<int>> *vertexMap, QVector<int> *faceMap, QString *error = nullptr,
						 const ModelWorkControl &control = {});
} // namespace vibestudio
