#pragma once

// Internal helpers shared by the mesh tool implementation files. Every helper
// works on a candidate copy; applyModelEdit validates and commits the result.

#include "core/model_document.h"
#include "core/model_geometric_topology.h"
#include "core/model_geometry_helpers.h"
#include "core/model_mesh_tools.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

namespace vibestudio::model_tools
{
using namespace model_geometry;

struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelMeshTools)
};

bool fail(QString *error, const QString &message);
inline P3 position(const ModelSurface &surface, int frame, int vertex)
{
	return point(surface.frames[frame].positions[vertex]);
}
inline ModelVec3 vec(P3 p)
{
	return {float(p.x), float(p.y), float(p.z)};
}
inline P3 unit(P3 p)
{
	const double l = length(p);
	return l > 1e-300 ? p * (1.0 / l) : P3{0, 0, 0};
}
inline P3 arrayPoint(const std::array<double, 3> &values)
{
	return {values[0], values[1], values[2]};
}
// Twice the triangle's area along its winding normal.
P3 faceCross(const ModelSurface &surface, int frame, const ModelTriangle &t);
// Unit face normal weighted by the face's interior angle at corner 0, 1 or 2.
// Angle weighting keeps vertex normals of triangulated quads symmetric.
P3 cornerNormal(const ModelSurface &surface, int frame, const ModelTriangle &t, int corner);
// Angle-weighted normal at the corner(s) of `t` whose vertex satisfies `match`.
template <typename Match> P3 matchingCornerNormal(const ModelSurface &surface, int frame, const ModelTriangle &t, Match match)
{
	P3 sum{0, 0, 0};
	const int corners[3]{t.a, t.b, t.c};
	for (int corner = 0; corner < 3; ++corner)
	{
		if (match(corners[corner]))
			sum = sum + cornerNormal(surface, frame, t, corner);
	}
	return sum;
}
// Smallest interior angle in radians, or zero for a collapsed triangle.
double minimumAngle(P3 a, P3 b, P3 c);
bool collapsedInAnyFrame(const ModelSurface &surface, const ModelTriangle &t);
bool flipsInAnyFrame(const ModelSurface &surface, const ModelTriangle &before, const ModelTriangle &after);

// New vertices copy every pose. vertexCount stays in sync with texCoords.
int appendCopy(ModelSurface *surface, int source);
// Positions and UVs blend linearly, normals blend and renormalize.
int appendBlend(ModelSurface *surface, const QVector<QPair<int, double>> &weights);
// Interpolates from a to b. Pass endpoints in a fixed geometric order so seam
// copies on both sides of an edge receive bit-identical positions.
int appendLerp(ModelSurface *surface, int a, int b, double t);

// Checks document-wide vertex, frame-vertex and triangle storage limits before
// a tool allocates geometry.
bool reserveCapacity(const ModelMesh &mesh, int surface, qint64 addedVertices, qint64 addedTriangles, QString *error);

// Area-weighted normals for these vertices in every pose (or one pose). Seam
// copies that currently share one normal stay smooth across the seam; copies
// with different normals keep their hard edge.
bool refreshNormals(ModelSurface *surface, const QSet<int> &vertices, ModelWorkProgress &work, int onlyFrame = -1);
// Seam marks must name existing edges; marks on removed edges are dropped.
void pruneSeams(ModelSurface *surface);
// Removes candidates that no triangle references any more. Vertices that were
// already unused before the edit are not candidates and remain.
bool compactUnused(ModelSurface *surface, const QSet<int> &candidates, ModelSelection *selection, ModelWorkProgress &work);
QSet<int> usedVertices(const ModelSurface &surface);
QSet<int> selectedVertexSet(const ModelSurface &surface, const ModelSelection &selection);
QSet<int> expandToCopies(const ModelGeometricTopology &topology, const QSet<int> &vertices);
QSet<ModelEdge> surfaceEdgeSet(const ModelSurface &surface);
// Faces whose sorted corners repeat an earlier face among `touched` are removed.
void removeDuplicateFaces(ModelSurface *surface, const QSet<int> &touchedVertices);

// Ear clipping in the plane perpendicular to `normal`. The polygon lists
// vertex indices in winding order (counter-clockwise about the normal).
bool triangulatePolygon(const ModelSurface &surface, int frame, const QVector<int> &polygon, P3 normal, QVector<ModelTriangle> *result);

// Inserts one vertex per listed canonical index edge at t, measured from the
// edge's lower group to its higher group so coincident seam edges agree.
// Every triangle using a split edge is retriangulated without T-junctions.
// created maps each split edge to its new vertex; faceSource maps each new
// triangle to the triangle it came from.
bool splitSurfaceEdges(ModelSurface *surface, const ModelGeometricTopology &topology, const QHash<ModelEdge, double> &cuts, int frame,
					   QHash<ModelEdge, int> *created, QVector<int> *faceSource, ModelWorkProgress &work);

// Tool entry points. selection is updated with the tool's result.
bool rotateEdges(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work);
bool pokeFaces(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work);
bool beautifyFaces(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work);
bool makeFace(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work);
bool mergeVertices(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work);
bool dissolveVertices(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work);
bool dissolveFaces(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work);
bool extrudeEdges(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work);
bool insetFaces(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work);
bool shrinkFatten(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work);
bool smoothVertices(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work);
bool weightedTransform(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work,
					   const ModelWorkControl &control);
bool shadeFaces(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work);
bool bisectFaces(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work);
bool symmetrize(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work);
bool loopCut(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work);
bool addPrimitive(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work);
bool decimate(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work);
bool bevelVertices(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work);
bool solidify(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work);
bool projectUvs(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work);
} // namespace vibestudio::model_tools
