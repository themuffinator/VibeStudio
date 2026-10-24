#pragma once

// Edit-mode mesh tools for idTech models.
//
// Each tool is a candidate-only operation dispatched by applyModelEdit, which
// validates and atomically commits the whole document as one undo step. Tools
// keep indexed triangles but reason about geometric topology (see
// core/model_geometric_topology.h): coincident UV and hard-normal seam copies
// act as one vertex, as a Blender vertex does with per-corner UVs. New
// vertices keep each side's own UVs, so UV seams survive every edit.
//
// Tool names, defaults and behaviour follow Blender's documented edit-mode
// operators (https://docs.blender.org/manual/en/latest/modeling/meshes/editing/index.html).
// No Blender code is used. Decimation implements the published quadric error
// metric of Garland and Heckbert, "Surface Simplification Using Quadric Error
// Metrics" (SIGGRAPH 1997), as half-edge collapses so every animation pose
// keeps stored positions. Polygon filling uses deterministic ear clipping.
//
// Topology tools span every animation pose. Position-only tools (shrink and
// fatten, smoothing and weighted transforms) honour the edit's frame scope.
// Geometric choices such as convexity, cut positions and inset directions use
// the options' reference pose; every other pose follows the same topology.

#include "core/model_mesh.h"
#include "core/model_knife.h"
#include "core/model_vertex_origins.h"
#include "core/model_work.h"

#include <QMap>
#include <QString>

#include <array>

namespace vibestudio
{
enum class ModelEditKind;
struct ModelEdit;
struct ModelSelection;
struct ModelEditReport;

enum class ModelMergeMode
{
	Centre,
	// The reference pose meets the given point; other poses keep their offset.
	Point,
	// Each connected group of selected vertices collapses to its own centre.
	Collapse
};
enum class ModelInsetMode
{
	Region,
	Individual
};
enum class ModelPrimitive
{
	Plane,
	Cube,
	Circle,
	Grid,
	Cylinder,
	Cone,
	UvSphere,
	IcoSphere,
	Torus
};
// Proportional editing falloff curves, matching Blender's names.
enum class ModelFalloff
{
	Smooth,
	Sphere,
	Root,
	InverseSquare,
	Sharp,
	Linear,
	Constant
};
// UV projections, as Blender's Cube, Project From View, Cylinder and Sphere
// Projection. Cube maps each face by its dominant axis.
enum class ModelUvProjection
{
	Cube,
	View,
	Cylinder,
	Sphere
};
enum class ModelBisectKeep
{
	Both,
	// Delete the selected faces behind the plane (opposite its normal).
	Front,
	Back
};

struct ModelMeshToolOptions
{
	// Pose used for geometric choices; new geometry follows every pose.
	int referenceFrame = 0;
	// Inset: border width and lift of the inner region along its normal.
	double insetThickness = 2, insetDepth = 0;
	ModelInsetMode inset = ModelInsetMode::Region;
	// Inset and shrink/fatten keep a constant shell width at sharp corners.
	bool evenThickness = true;
	// Shrink/Fatten distance along vertex normals; Poke centre lift.
	double offset = 0;
	// Smooth Vertices: factor 0-1 per iteration, 1-100 iterations.
	double smoothFactor = 0.5;
	int iterations = 1;
	bool pinBoundary = false;
	ModelMergeMode merge = ModelMergeMode::Centre;
	std::array<double, 3> point{0, 0, 0};
	// Loop Cut: 1-64 parallel cuts; one cut may slide between -1 and 1.
	int cuts = 1;
	double slide = 0;
	// Edge Slide: offset is a signed distance in the reference pose instead
	// of slide's signed fraction. Other poses follow the same rail fraction.
	bool slideByDistance = false;
	// Symmetrize mirror axis (0 X, 1 Y, 2 Z) and which half is copied.
	int axis = 0;
	bool positiveToNegative = true;
	// Vertices this close to the mirror plane join both halves.
	double mergeThreshold = 0.001;
	// Symmetrize on skinned surfaces: mirror partners by joint name (both
	// directions apply; unlisted joints on the mirror plane keep their name).
	// Off-centre joints without a partner are refused unless keepJointNames
	// binds the mirrored half to the same joints. See core/model_vertex_origins.h.
	QMap<QString, QString> mirrorJoints{};
	bool keepJointNames = false;
	// Bisect plane through point with this normal.
	std::array<double, 3> normal{0, 0, 1};
	ModelBisectKeep keep = ModelBisectKeep::Both;
	// Add Primitive: shape, centre (point), full extents and local up axis.
	ModelPrimitive primitive = ModelPrimitive::Cube;
	std::array<double, 3> size{32, 32, 32};
	int segments = 16, rings = 8;
	// Add Primitive on a skeletal model: how the new vertices follow the
	// skeleton (rigidly to bindJoint, or the nearest bones). Without a binding,
	// adding to a skinned surface is refused. See core/model_vertex_origins.h.
	ModelNewGeometryBinding newGeometryBinding = ModelNewGeometryBinding::None;
	QString bindJoint{};
	// Add Primitive into a new surface with this name (using the active
	// surface's materials) instead of the active surface.
	QString newSurface{};
	// Beautify, edge rotation and quad pairing: largest normal difference.
	double maxAngle = 40;
	// Auto Smooth: edges sharper than this angle keep hard normals.
	double smoothAngle = 30;
	// Weighted transforms: proportional radius (0 = off), falloff, connected
	// geodesic distance and mirrored axes (bit mask X = 1, Y = 2, Z = 4).
	double proportionalRadius = 0;
	ModelFalloff falloff = ModelFalloff::Smooth;
	bool connectedOnly = false;
	int mirrorAxes = 0;
	// Decimate: fraction of selected triangles to keep, or an explicit count.
	double ratio = 0.5;
	int targetTriangles = 0;
	bool preserveBoundary = true;
	// Bevel Vertices: distance of the new corner points along each edge.
	double bevelWidth = 2;
	// Edge bevel: neighbouring-edge offset, 1-32 cross-section segments and
	// quadratic roundness (0 straight chamfer, 1 rounded). New bands use tile size.
	int bevelSegments = 1;
	double bevelRoundness = 1;
	// Solidify: shell thickness behind the faces (negative grows outward).
	double solidifyThickness = 4;
	// UV projection: model units per texture repeat (64 matches a 64-unit
	// texture tile), the view's right and up directions for View, the round
	// projections' axis (axis), and fitting the result into the 0-1 square.
	ModelUvProjection uvProjection = ModelUvProjection::Cube;
	double uvTileSize = 64;
	std::array<double, 3> uvAxisU{1, 0, 0}, uvAxisV{0, 0, 1};
	bool uvFit = false;
	ModelKnifeOptions knife;
	// Spin around point/normal: extrude a face region or boundary edges, or
	// duplicate faces. Close joins a complete turn (and omits its duplicate cap).
	double spinAngle = 90;
	int spinSteps = 8;
	bool spinDuplicate = false, spinClose = true;
	// Rip selected vertices/edges toward the incident side face, optionally
	// bridge the opening. The translation is carried by ModelEdit::translation.
	int ripFace = -1;
	bool ripFill = false, ripOpposite = false;
	bool operator==(const ModelMeshToolOptions &) const = default;
};

bool isModelMeshToolEdit(ModelEditKind kind);
// Tools that move existing vertices without changing topology.
bool isModelMeshShapeEdit(ModelEditKind kind);
// Candidate-only operation; applyModelEdit validates and commits the document.
bool applyModelMeshToolEdit(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error,
							const ModelWorkControl &control = {}, ModelEditReport *report = nullptr);

// Proportional falloff weight for a normalized distance (0 at the selection,
// 1 at the radius). Shared by the viewport preview and the committed edit.
double modelFalloffWeight(ModelFalloff falloff, double distance);
QString modelPrimitiveName(ModelPrimitive primitive);
} // namespace vibestudio
