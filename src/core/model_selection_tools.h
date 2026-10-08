#pragma once

// Edit-mode selection operators for model surfaces.
//
// The operators follow Blender's Select menu
// (https://docs.blender.org/manual/en/latest/modeling/meshes/selecting/index.html):
// select all, invert, linked, more/less, loops and rings, shortest path,
// similar, non-manifold, loose, boundary loop, sharp edges, random, checker
// deselect, side of an axis, facing direction and mirror. No Blender code is
// used. Connectivity is geometric (see core/model_geometric_topology.h), so
// selections cross UV seams unless delimitSeams asks for indexed (UV island)
// connectivity. Results are ordinary ModelSelection values for the active
// surface; the GUI and the CLI apply them through the same document services.

#include "core/model_document.h"

namespace vibestudio
{
enum class ModelSelectionMode
{
	Faces,
	Vertices,
	Edges
};
enum class ModelSelectOperation
{
	All,
	None,
	Invert,
	Linked,
	More,
	Less,
	Loop,
	Ring,
	ShortestPath,
	Similar,
	NonManifold,
	Loose,
	Boundary,
	Sharp,
	Random,
	Checker,
	Side,
	Facing,
	Mirror
};
enum class ModelSimilarity
{
	// Faces and vertices: normal direction within threshold degrees.
	Normal,
	// Faces: area within a relative threshold.
	Area,
	// Faces: same plane (normal within threshold degrees, distance within 0.1%).
	Coplanar,
	// Edges: length within a relative threshold.
	Length,
	// Edges: direction within threshold degrees, either way round.
	Direction,
	// Edges: angle between the two faces within threshold degrees.
	FaceAngle,
	// Edges: same UV seam marking.
	Seam,
	// Vertices: same number of connected edges.
	Valence
};
struct ModelSelectRequest
{
	ModelSelectOperation operation = ModelSelectOperation::All;
	ModelSelectionMode mode = ModelSelectionMode::Faces;
	// Pose used for positions, normals and distances.
	int frame = 0;
	// Loop and ring seed (an indexed edge of the active surface).
	ModelEdge edge{-1, -1};
	// Shortest path endpoints: vertices in vertex and edge modes, faces in face mode.
	int from = -1, to = -1;
	// Add the result to the current selection instead of replacing it.
	bool extend = false;
	// Linked selection stops at UV seams (indexed connectivity).
	bool delimitSeams = false;
	ModelSimilarity similarity = ModelSimilarity::Normal;
	// Similar tolerance; Sharp angle in degrees; Side margin; Facing cone in degrees.
	double threshold = 5;
	// Random: fraction selected and repeatable seed.
	double ratio = 0.5;
	quint32 seed = 0;
	// Checker: deselect every nth element, starting after offset.
	int nth = 2, offset = 0;
	// Side, Facing and Mirror axis (0 X, 1 Y, 2 Z) and direction.
	int axis = 0;
	bool positive = true;
	// Largest normal difference when pairing triangles into quads for loops.
	double maxAngle = 40;
};
bool selectModelComponents(const ModelMesh &mesh, const ModelSelection &current, const ModelSelectRequest &request, ModelSelection *result,
						   QString *error = nullptr, const ModelWorkControl &control = {});
QString modelSelectOperationName(ModelSelectOperation operation);
} // namespace vibestudio
