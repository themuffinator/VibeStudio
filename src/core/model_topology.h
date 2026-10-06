#pragma once

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QHash>
#include <QPair>
#include <QSet>
#include <algorithm>

namespace vibestudio
{
// Undirected indexed edges. Canonical endpoint pairs survive position edits;
// topology operations explicitly remap them. UV seams remain separate edges.
using ModelEdge = QPair<int, int>;
inline ModelEdge modelEdge(int a, int b) { return {std::min(a, b), std::max(a, b)}; }
QVector<ModelEdge> modelSurfaceEdges(const ModelSurface &surface, const ModelWorkControl &control = {});

// Prepared with the document on its worker, then shared by selection validation
// and the component inspector. Retaining triangles makes the identity check
// safe across edits, undo/redo, and allocator address reuse.
struct ModelSurfaceTopology
{
	QVector<ModelTriangle> triangles;
	int vertexCount = -1;
	QVector<ModelEdge> edges;
	QHash<ModelEdge, int> faceUses;
	QSet<ModelEdge> allEdges;
	QSet<int> edgeVertices;
	qint64 storageBytes() const;
};
bool prepareModelSurfaceTopology(const ModelSurface &surface, ModelSurfaceTopology *index, QString *error = nullptr,
								 const ModelWorkControl &control = {});

// Lowest-index compatible anchors keep their positions, UVs and normals. All
// poses must satisfy the distance bound. Collapsed faces are removed, but new
// duplicate faces, inconsistent winding and nonmanifold edges are refused.
// Input must already pass editable-model validation. Failure leaves it intact.
bool weldModelSurface(ModelSurface *surface, const QSet<int> &vertices, double distance, bool preserveSeams, QVector<int> *vertexMap,
					  QVector<int> *faceMap, QString *error, const ModelWorkControl &control = {});
} // namespace vibestudio
