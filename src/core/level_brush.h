#pragma once

#include "core/map_geometry.h"
#include "core/level_texture_mapping.h"

namespace vibestudio
{

// Component IDs describe the current solved brush, not the arbitrary three
// plane points in a .map file. Re-query after a topology-changing edit.
enum class LevelBrushComponent { Vertex, Edge, Face };

inline constexpr int kLevelBrushComponentMaxFaces = 128;
inline constexpr int kLevelBrushComponentMaxVertices = 256;

struct LevelBrushTopology {
	QVector<LevelMapVec3> vertices;	   // lexicographic XYZ order
	QVector<std::array<int, 2>> edges; // ascending endpoint IDs, sorted
	QVector<QVector<int>> faces;	   // source face order, winding vertex IDs
	MapBrushGeometry geometry;
};

struct LevelBrushEditReport {
	int verticesBefore = 0;
	int verticesAfter = 0;
	int facesBefore = 0;
	int facesAfter = 0;
	int collapsedVertices = 0;
	int insertedVertices = 0;
	int removedVertices = 0;
	// Faces whose UVs were fitted rather than kept exactly (fitUvLock).
	int fittedFaces = 0;
	bool changed = false;
};

bool levelBrushTopology(const LevelMapBrush &brush, LevelBrushTopology *topology, QString *error = nullptr);
// Original shared convex-hull builder used by primitive creation. Points must
// be finite world coordinates; the result is a closed classic geometry draft.
// Document insertion supplies the target texture dialect and source bindings.
bool createLevelBrushHull(const QVector<LevelMapVec3> &points, const QString &texture, LevelMapBrush *brush, QString *error = nullptr);
// Convex envelope permits duplicate, interior and coplanar input points. Up to
// 4,096 inputs; the result retains the 128-face / 256-vertex editor limits.
bool createLevelBrushEnvelope(const QVector<LevelMapVec3> &points, const QString &texture, LevelMapBrush *brush, QString *error = nullptr);
QVector<int> levelBrushComponentVertices(const LevelBrushTopology &topology, LevelBrushComponent kind, const QVector<int> &components);
// Rebuilds the convex hull after moving the requested components. Coplanar
// triangles merge; bent faces split. New faces inherit mapping/flags from the
// best matching source face. Texture coordinates stay in their stored frame;
// this operation does not promise texture lock on a deformed face.
// A disappearing/merged vertex requires allowCollapse. Flat, open, non-finite
// and out-of-world candidates are always rejected without mutating the brush.
bool moveLevelBrushComponents(LevelMapBrush *brush, LevelBrushComponent kind, const QVector<int> &components, const LevelMapVec3 &delta,
							  double grid = 0, bool allowCollapse = false, LevelBrushEditReport *report = nullptr,
							  QString *error = nullptr);
// Explicit topology edits. Removing an edge/face removes its incident vertices;
// the remaining points must still form a closed, three-dimensional brush.
// Insertion may split edges/faces by adding an exterior corner. Coplanar or
// interior points cannot survive as corners in a convex plane-based .map brush
// and are refused. All requested new points must survive, even with collapse
// enabled for existing corners. Failures leave the input brush unchanged.
bool removeLevelBrushComponents(LevelMapBrush *brush, LevelBrushComponent kind, const QVector<int> &components,
								LevelBrushEditReport *report = nullptr, QString *error = nullptr);
bool insertLevelBrushVertices(LevelMapBrush *brush, const QVector<LevelMapVec3> &positions, double grid = 0,
							  bool allowCollapse = false, LevelBrushEditReport *report = nullptr, QString *error = nullptr);
// Add a corner at each selected edge midpoint or face centroid plus delta.
// The new corners must survive the convex rebuild. A zero unsnapped movement
// is a no-op, so interactive previews can return to their starting position.
bool splitLevelBrushComponents(LevelMapBrush *brush, LevelBrushComponent kind, const QVector<int> &components,
	const LevelMapVec3 &delta, double grid = 0, bool allowCollapse = false,
	LevelBrushEditReport *report = nullptr, QString *error = nullptr);
// Commits a geometry draft through the map's shared undo/persistence service.
// Face source bindings must originate from this brush; material syntax, flags,
// comments and primitive dialect belong to the document and are retained.
bool replaceLevelMapBrushGeometry(LevelMapDocument *document, int brushId, const LevelMapBrush &replacement, QString *error = nullptr);
// Validate independent drafts against one source, then publish one batch before
// linked-instance propagation. Prevents early followers from replacing later
// selected objects, and preserves undo limits and save points.
bool replaceLevelMapComponentGeometry(LevelMapDocument* document, const QMap<int,LevelMapBrush>& brushes,
	const QMap<int,LevelMapPatch>& patches, const QString& description, QString* error = nullptr);

// Components of one brush (ids as levelBrushTopology() numbers them for
// `kind`) or control points of one patch (row * width + column), for moving
// the parts of several objects as one edit, as the 2D views' vertex editing
// does.
struct LevelMapComponentSet {
	LevelMapSelectionRef object;
	LevelBrushComponent kind = LevelBrushComponent::Vertex;
	QVector<int> components;
};
struct LevelMapComponentReport {
	int brushes = 0;
	int patches = 0;
	int collapsedVertices = 0;
	int removedVertices = 0;
	int insertedVertices = 0;
	int fittedFaces = 0;
};

// Affine: `linear` about the pivot, then `delta`, as Repeat Transforms
// composes NetRadiant Custom's recorded scale, skew, turn and translation
// (core/level_repeat.h). It is never recorded as a repeatable transform.
enum class LevelComponentTransformKind { Move, Rotate, Scale, Shear, Affine };
struct LevelComponentTransformRequest {
	LevelComponentTransformKind operation = LevelComponentTransformKind::Move;
	// Move: the displacement. Affine: the translation after the linear part.
	LevelMapVec3 delta{0,0,0,true};
	// Affine only: the columns of the linear part.
	std::array<LevelMapVec3,3> linear{{{1,0,0,true},{0,1,0,true},{0,0,1,true}}};
	LevelMapVec3 scale{1,1,1,true};
	// Invalid selects the centre of the picked vertices' bounds, shared across
	// all brushes and patches. Rotation is right-handed around axis (0=X).
	LevelMapVec3 pivot;
	int axis = 2, along = 0;
	// Degrees for Rotate; displacement per unit along `along` for Shear.
	double amount = 0, grid = 0;
	bool allowCollapse = false, textureLock = false, allowValve220 = false;
	// With textureLock: where no single planar projection keeps every corner's
	// UVs, fit the face to three of its corners (preferring corners the edit
	// did not move) and let the others drift, as NetRadiant Custom's texture
	// vertex lock does, instead of refusing; a classic face that cannot hold the
	// fit takes the nearest classic mapping unless Valve 220 is allowed.
	// LevelMapComponentReport::fittedFaces counts such faces.
	bool fitUvLock = false;
};
bool levelMapComponentBounds(const LevelMapDocument& document, const QVector<LevelMapComponentSet>& sets,
	LevelMapVec3* mins, LevelMapVec3* maxs, QString* error = nullptr);
// Resolve one geometric brush vertex (possibly shared by several selected
// brushes) onto an existing target vertex. Exact target: no grid rounding.
// Read-only preparation; apply through the ordinary component transform service.
bool levelMapVertexSnapDelta(const LevelMapDocument& document, const QVector<LevelMapComponentSet>& sets,
	const LevelMapSelectionRef& target, int vertex, LevelMapVec3* delta, QString* error = nullptr);
bool levelComponentTransformMatrix(const LevelComponentTransformRequest& request, const LevelMapVec3& defaultPivot,
	LevelTextureAffine* matrix, QString* error = nullptr);
LevelMapVec3 transformedLevelComponentPoint(const LevelMapVec3& point, const LevelTextureAffine& matrix, double grid = 0);
bool transformLevelBrushComponents(LevelMapBrush* brush, LevelBrushComponent kind, const QVector<int>& components,
	const LevelTextureAffine& matrix, double grid = 0, bool allowCollapse = false, bool textureLock = false,
	bool allowValve220 = false, LevelBrushEditReport* report = nullptr, QString* error = nullptr, bool fitUvLock = false);
// One shared pivot/matrix, one atomic undo step. Exact UV lock preserves every
// retained corner's coordinates; mappings the native dialect cannot represent
// are refused unless explicit Valve 220 conversion is allowed, or fitted when
// request.fitUvLock is set. Patch UVs stay attached to their control points.
bool transformLevelMapComponents(LevelMapDocument* document, const QVector<LevelMapComponentSet>& sets,
	const LevelComponentTransformRequest& request, LevelMapComponentReport* report = nullptr, QString* error = nullptr);
// Moves every set by `delta` as one undo step, rebuilding each brush's
// convex hull as moveLevelBrushComponents() does. All or nothing: a brush or
// patch that cannot take the move leaves the map as it was, named in `error`.
bool moveLevelMapComponents(LevelMapDocument *document, const QVector<LevelMapComponentSet> &sets, const LevelMapVec3 &delta,
							bool allowCollapse, LevelMapComponentReport *report = nullptr, QString *error = nullptr);
// Removes vertices incident to the selected brush components in one atomic
// undo step. A refused brush, patch selection or scene lock rejects all sets.
bool removeLevelMapComponents(LevelMapDocument *document, const QVector<LevelMapComponentSet> &sets,
							  LevelMapComponentReport *report = nullptr, QString *error = nullptr);
bool splitLevelMapComponents(LevelMapDocument *document, const QVector<LevelMapComponentSet> &sets,
	const LevelMapVec3 &delta, bool allowCollapse = false, LevelMapComponentReport *report = nullptr, QString *error = nullptr);

struct LevelMapVertexInsertion {
	int brushId = -1;
	QVector<LevelMapVec3> positions;
};
// Insert into up to 256 distinct brushes as one checked scene transaction.
// Every requested corner must survive the hull; a refused or locked brush
// leaves all geometry, linked instances, selection and undo history untouched.
bool insertLevelMapVertices(LevelMapDocument* document, const QVector<LevelMapVertexInsertion>& insertions,
	double grid = 0, bool allowCollapse = false, LevelMapComponentReport* report = nullptr, QString* error = nullptr);

struct LevelBrushExtrusion {
	QVector<LevelMapBrush> brushes;
	int activeBrush = 0;
	int activeFace = -1;
	bool changed = false;
};
// Snap normal travel to the nearest distance step or vertex/grid-plane
// crossing, whichever is nearer. Invalid inputs return NaN; zero grid is free.
double snapLevelBrushExtrusionDistance(double distance, const LevelMapVec3& normal,
	const QVector<LevelMapVec3>& vertices, double grid);
// Move one supporting plane along its outward normal. Every original face
// must still bound a polygon. Split keeps two solids separated at the old
// plane (outward) or the moved plane (inward). Donor face bindings survive;
// document persistence must rewrite the geometry and dirty texture fields.
bool prepareLevelBrushExtrusion(const LevelMapBrush& brush, int face, double distance, bool split, bool textureLock,
	LevelBrushExtrusion* result, QString* error = nullptr);
struct LevelMapExtrusionReport {
	int changedBrushes = 0;
	int createdBrushes = 0;
	QVector<int> brushIds;
	QVector<LevelMapComponentSet> faces;
};
// One face per brush, parallel to the first face; distance follows that first
// face's normal. Opposing normals are allowed when resizing, not splitting.
// An invalid/locked brush refuses the entire transaction. New pieces inherit
// their original owner and scene membership, never the current creation layer.
bool extrudeLevelMapFaces(LevelMapDocument* document, const QVector<LevelMapComponentSet>& faces,
	double distance, bool split, bool textureLock, LevelMapExtrusionReport* report = nullptr, QString* error = nullptr);
// Rounds every vertex of the brushes onto the grid and rebuilds them, as
// TrenchBroom's Snap Vertices does, to clean up brushes left off the grid
// by rotation, shear or other editors; one undo step. A brush the snap would
// collapse, or that cannot be rewritten in place, is left as it was with its
// reason in `skipped`.
bool snapLevelMapBrushVertices(LevelMapDocument *document, const QVector<int> &brushIds, double grid, int *snapped = nullptr,
							   QStringList *skipped = nullptr, QString *error = nullptr);

} // namespace vibestudio
