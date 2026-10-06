#pragma once

#include "core/model_topology.h"

namespace vibestudio
{
enum class ModelUvPivot
{
	Origin,
	SelectionCentre,
	Custom,
	IndividualIslands
};

struct ModelUvEdge
{
	ModelEdge vertices;
	QVector<int> faces;
	bool seam = false;
};
struct ModelUvIsland
{
	QVector<int> faces, vertices;
	ModelTexCoord mins, maxs;
};
struct ModelUvTopology
{
	QVector<ModelUvIsland> islands;
	QVector<int> faceIsland;
	QVector<ModelUvEdge> edges;
};

// Deterministic charts across shared indexed edges. Marked seams, boundaries,
// and edges with more than two incident faces stop chart traversal. Touching at
// a vertex alone does not join charts. Failure leaves the output unchanged.
bool buildModelUvTopology(const ModelSurface &surface, ModelUvTopology *result, QString *error = nullptr,
						  const ModelWorkControl &control = {});
bool expandModelUvIslands(const ModelSurface &surface, const ModelUvTopology &topology, const QSet<int> &faces, const QSet<int> &vertices,
						  const QSet<ModelEdge> &edges, QSet<int> *result, QString *error = nullptr, const ModelWorkControl &control = {});

struct ModelUvIslandTransform
{
	ModelTexCoord scale{1, 1}, offset;
	double rotation = 0;
	// -1 uses existing UVs; 0/1/2 project XY/XZ/YZ in referenceFrame.
	int projection = -1;
	int referenceFrame = 0;
	int vertexLimit = 65536;
};

// Complete selected islands transform about their own coordinate bounds. Shared
// corners split deterministically; unselected UVs and all pose attributes survive.
// The caller validates the mesh and supplies its remaining vertex capacity.
// Failure/cancellation leaves result unchanged.
bool transformModelUvIslands(const ModelSurface &surface, const QSet<int> &faces, const ModelUvIslandTransform &transform,
							 ModelSurface *result, QString *error = nullptr, const ModelWorkControl &control = {});
} // namespace vibestudio
