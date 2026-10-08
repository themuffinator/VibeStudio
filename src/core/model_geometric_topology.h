#pragma once

// Geometric adjacency and quad interpretation for indexed model surfaces.
//
// Editable surfaces split a vertex index wherever UVs or hard normals differ,
// so index adjacency stops at every UV seam. Blender instead keeps one vertex
// and stores UVs per face corner. Edit-mode tools that walk loops and rings,
// find region borders or smooth shapes therefore treat indices whose positions
// are equal in every animation pose as one geometric vertex. Tools that create
// geometry still write separate indices on each side, so seams survive.
//
// idTech model formats store triangles only. The quad view pairs neighbouring
// triangles into convex, near-planar quads, which gives triangle meshes the
// edge loops and rings that quad modellers expect. The pairing rules follow
// the documented behaviour of Blender's Triangles to Quads operator
// (https://docs.blender.org/manual/en/latest/modeling/meshes/editing/face/triangles_quads.html);
// no Blender code is used.

#include "core/model_mesh.h"
#include "core/model_topology.h"
#include "core/model_work.h"

#include <QHash>
#include <QPair>
#include <QSet>
#include <QVector>

#include <array>

namespace vibestudio
{
struct ModelGeometricTopology
{
	// The lowest index sharing every pose position with each vertex index.
	// Group ids are vertex indices, so index-space containers can hold them.
	QVector<int> vertexGroup;
	// Index copies of each group (ascending); empty for non-representatives.
	QVector<QVector<int>> copies;
	// Faces touching each group (ascending); empty for non-representatives.
	QVector<QVector<int>> groupFaces;
	// Canonical group pair -> incident faces in ascending order.
	QHash<ModelEdge, QVector<int>> edgeFaces;
	[[nodiscard]] int group(int vertex) const { return vertex >= 0 && vertex < vertexGroup.size() ? vertexGroup[vertex] : -1; }
	[[nodiscard]] ModelEdge geometricEdge(int a, int b) const { return modelEdge(group(a), group(b)); }
	[[nodiscard]] bool boundary(ModelEdge geometric) const { return edgeFaces.value(geometric).size() == 1; }
};
bool buildModelGeometricTopology(const ModelSurface &surface, ModelGeometricTopology *result, QString *error = nullptr,
								 const ModelWorkControl &control = {});

// One element of the quad view: a paired quad (four corners in face winding
// order, with the shared diagonal between corners 0 and 2) or an unpaired
// triangle (corners[3] == -1, faces[1] == -1).
struct ModelQuadElement
{
	std::array<int, 4> corners{-1, -1, -1, -1};
	std::array<int, 2> faces{-1, -1};
	[[nodiscard]] int sides() const { return corners[3] < 0 ? 3 : 4; }
	[[nodiscard]] bool quad() const { return corners[3] >= 0; }
};
struct ModelQuadTopology
{
	ModelGeometricTopology geometry;
	QVector<ModelQuadElement> elements;
	// Element containing each face.
	QVector<int> faceElement;
	// Canonical group pair of each element side -> (element, side) uses. A side
	// runs from corners[side] to corners[(side + 1) % sides()].
	QHash<ModelEdge, QVector<QPair<int, int>>> sideUses;
	// Canonical group pairs hidden inside paired quads.
	QSet<ModelEdge> diagonals;
};
// Greedy, deterministic pairing. Two triangles sharing an indexed edge with
// opposite winding pair when their normals differ by at most maxAngleDegrees in
// the reference frame and their union is a strictly convex quad. Flat,
// rectangular candidates whose shared edge is the longest side of both
// triangles pair first; ties fall to the lower face indices.
bool buildModelQuadTopology(const ModelSurface &surface, int frame, double maxAngleDegrees, ModelQuadTopology *result,
							QString *error = nullptr, const ModelWorkControl &control = {});

// Walks over geometric edges of the quad view. Both return canonical group
// pairs in walk order and never include quad diagonals. A loop crosses a vertex
// only where exactly four quads meet (or follows a boundary through vertices
// with two boundary edges); a ring crosses opposite sides of quads and stops at
// triangles or boundaries. closed reports a loop or ring that returns to its seed.
QVector<ModelEdge> modelEdgeLoop(const ModelQuadTopology &topology, ModelEdge geometric, bool *closed = nullptr);
QVector<ModelEdge> modelEdgeRing(const ModelQuadTopology &topology, ModelEdge geometric, bool *closed = nullptr,
								 QVector<int> *elements = nullptr);
} // namespace vibestudio
