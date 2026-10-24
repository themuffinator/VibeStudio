#pragma once

// Exact skin-weight provenance for mesh, UV, surface and frame edits.
//
// Skeletal models (MD5, IQM, MDS, MDM, MDR, Ghoul 2, Half-Life) carry joint
// influences per vertex (ModelSurfaceSkinning). When an edit adds, removes,
// reorders, interpolates, mirrors or moves vertices between surfaces, those
// influences must follow what the tool actually did, not where the vertices
// happen to be afterwards.
//
// applyModelEdit seeds every vertex of a skeletal model with an identity
// origin (its own surface and index) before a tool runs. The shared vertex
// helpers carry origins with the vertex data, in the same places that carry
// persistent vertex identities (ModelSurface::topologyVertices):
// - a copy keeps its source's origin (appendModelVertexOriginCopy);
// - an interpolated vertex blends its sources' origins with the same weights
//   as its positions (appendModelVertexOriginBlend);
// - compaction, repairs, UV splits and surface separation rebuild origins
//   through the same vertex map as the vertex data (rebuildModelVertexOrigins);
// - joining surfaces appends the incoming origins (appendModelSurfaceOrigins);
// - a mirrored copy records the mirror plane (mirrorModelVertexOrigin);
// - new geometry has no origin (appendModelVertexOriginNone).
//
// reconcileModelSkinningFromOrigins then derives each resulting vertex's
// influences from its origins. Origins name the surface they came from, so a
// surface split, move or join no longer depends on surface positions.
// - One unmirrored source: the influences are copied verbatim, so untouched
//   vertices stay bit-exact. A copy the tool moved is rebound at its own bind
//   position and its retained skeletal poses are posed from the skeleton.
// - Several sources: blendModelSkinInfluences merges them by joint with
//   weighted offsets, so every posed position is the same blend of the
//   sources' posed positions.
// - Mirrored sources: the joints go through the mirror joint map, the
//   offsets are rebound at the mirrored bind position, and the retained poses
//   are posed from the skeleton. Joints on the mirror plane keep their names
//   whatever the map says; a mapped pair must sit on opposite sides of it.
// - Merged sources (Merge Vertices, Weld by Distance without seams): every
//   merged vertex takes one equal blend of the vertices it joined, is rebound
//   where the tool put it and is posed from the skeleton.
// - No source (Add Primitive): the edit must name a binding for the new
//   vertices (a joint, or the nearest bones); on a skinned surface without
//   one, the edit is refused. A primitive added as a new surface without a
//   binding stays unskinned.
// Blends that give a vertex more joint influences than the surface had are
// counted (ModelSkinProvenance::widened), since formats with a lower limit
// keep only the strongest.
//
// Edge bevel, knife, spin and rip write a surface's complete influences
// themselves and mark its origins authored (markModelSurfaceOriginsAuthored).
// Their influences stand. Where such a tool left a normal in a retained
// skeletal pose that differs from the skin's, the skin's normal is used, but
// only for vertices the tool made or rewrote: a vertex whose position, normal
// and influences match a vertex of the surface before the edit bit for bit
// keeps its normal, including normals edited by hand in one frame.
//
// Nothing is guessed from positions. A skinned surface whose tool kept no
// origins and did not mark them authored, a surface that would mix vertices
// that follow the skeleton with vertices that do not, and a vertex that needs
// rebinding on a model without a retained bind frame are refused instead of
// approximated.
//
// Frame edits (Duplicate Frame, Delete Frame, Insert In-between Frames, Copy
// Full Pose) keep every vertex's influences. While a surface follows the
// skeleton they are refused for the bind frame and frames baked from a
// skeletal clip, so the clip mapping stays intact; skeleton.bakedClipForFrame
// follows the frames that remain (modelSkeletalFrameOrigins). Once every
// surface is unbound they are allowed everywhere: a clip that loses a frame,
// or has one overwritten, no longer matches its poses in order, so its
// remaining frames become independent (-2).
//
// Origins are transient. reconcileModelSkinningFromOrigins clears them, and
// they are never serialised, fingerprinted or kept in history.

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QJsonObject>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio
{

enum ModelVertexOriginFlag : quint8
{
	// Made by this edit (a copy, blend or mirror), not a vertex the model had.
	ModelVertexOriginCreated = 1,
	// The tool moved this vertex in every pose by a rule the skeleton cannot
	// follow: rebind it at its bind position and pose it from the skeleton.
	ModelVertexOriginRepose = 2,
	// The tool recomputed this vertex's normal from the faces in every pose:
	// rebind the normal from the bind pose and pose it from the skeleton.
	ModelVertexOriginNormals = 4,
	// The vertex joined others (Merge Vertices, Weld by Distance) and takes
	// their equal blend: reported as merged rather than interpolated.
	ModelVertexOriginMerged = 8,
};

// Identity origins for every vertex of every surface.
void seedModelVertexOrigins(ModelMesh *mesh);
void clearModelVertexOrigins(ModelMesh *mesh);
// Tools that write complete influences themselves (edge bevel, knife, spin,
// rip) replace the edited surface's origins with this marker, so
// reconciliation keeps their influences. Any other surface that arrives
// without origins is refused.
void markModelSurfaceOriginsAuthored(ModelSurface *surface);

// Each helper describes the vertex just appended to the surface's vertex data
// and does nothing when the surface carries no origins.
void appendModelVertexOriginCopy(ModelSurface *surface, int source);
void appendModelVertexOriginBlend(ModelSurface *surface, const QVector<QPair<int, double>> &weights);
void appendModelVertexOriginNone(ModelSurface *surface);
// Toggles the mirror plane (0 X, 1 Y, 2 Z) on every source of the vertex.
void mirrorModelVertexOrigin(ModelSurface *surface, int vertex, int axis);
void markModelVertexOriginRepose(ModelSurface *surface, int vertex);
void markModelVertexOriginNormals(ModelSurface *surface, int vertex);
// After the vertex data was rebuilt: vertex v now carries previous's origin
// of sourceOf[v], or none for -1. Copies after the first are marked created.
void rebuildModelVertexOrigins(ModelSurface *surface, const ModelSurfaceOrigins &previous, const QVector<int> &sourceOf);
// Appends another surface's origins after the target's own vertices.
void appendModelSurfaceOrigins(ModelSurface *target, const ModelSurfaceOrigins &source);
// Gives every target vertex one blend of the members' origins (vertex,
// weight), marked merged and posed from the skeleton. Call it before the
// vertex data is compacted, while member indices are still valid.
void mergeModelVertexOrigins(ModelSurface *surface, const QVector<QPair<int, double>> &members, const QVector<int> &targets);
// Tracked origins with no vertices, for a surface a tool creates from nothing.
void startModelSurfaceOrigins(ModelSurface *surface);

// Whether two vertices of a skinned surface have the same joints and weights
// (summed per joint, within `tolerance`). Offsets are not compared. True when
// the surface carries no complete skinning.
[[nodiscard]] bool sameModelSkinWeights(const ModelSurface &surface, int a, int b, double tolerance = 1e-6);

// --- Skin influence blending -----------------------------------------------

struct ModelSkinBlendSource
{
	const ModelSurfaceSkinning *skinning = nullptr;
	int vertex = -1;
	double weight = 0;
	// Optional joint renumbering (mirror joint maps); null keeps each joint.
	const QVector<int> *joints = nullptr;
};
enum class ModelSkinBlendStatus
{
	Blended,
	Cancelled,
	NegativeWeight,
	InfluenceCount
};
// Merges the sources' influences by joint. Each joint's weight is the
// weighted sum of the sources' weights and its offsets are averaged by those
// contributions, so in every pose the skinned position is the same weighted
// blend of the sources' skinned positions. Joints are listed in index order.
// Negative source weights and more than 64 merged joints are refused.
ModelSkinBlendStatus blendModelSkinInfluences(const QVector<ModelSkinBlendSource> &sources, QVector<ModelJointInfluence> *out,
	ModelWorkProgress *work = nullptr);

// --- Mirror joint maps ------------------------------------------------------

// Left/right joint pairs found by name: Left/Right words, L/R tokens between
// separators (Bip01 L Thigh, l_hand, hand.L), and a leading capital L or R
// before a lower-case name (Doom 3's Lhand/Rhand). Only pairs whose partner
// exists are listed, each in both directions. Nothing is inferred from joint
// positions.
[[nodiscard]] QMap<QString, QString> detectModelMirrorJointPairs(const ModelSkeleton &skeleton);
// "a=b, c=d", each pair once in joint order.
[[nodiscard]] QString modelMirrorJointMapText(const QMap<QString, QString> &map);
// Parses "a=b,c=d" into a map holding both directions. Refuses empty or
// repeated names and inconsistent pairs.
bool parseModelMirrorJointMap(const QString &text, QMap<QString, QString> *map, QString *error = nullptr);
// The joints that weight the surface and sit off the mirror plane through the
// origin (axis 0 X, 1 Y, 2 Z), in joint order. Symmetrize needs a mirror
// partner for each of them unless joint names are kept.
[[nodiscard]] QStringList modelOffCentreJoints(const ModelMesh &mesh, int surface, int axis, double tolerance = 0.001);
// Whether joints a and b can mirror each other across the planes through the
// origin in `planes` (1 X, 2 Y, 4 Z): on each plane a sits off, b sits off on
// the opposite side; on a plane a sits on, b does too.
[[nodiscard]] bool modelJointsMirrorAcross(const ModelSkeleton &skeleton, int a, int b, quint8 planes, double tolerance = 0.001);
// The pairs of `map` that can mirror across the plane on `axis`
// (modelJointsMirrorAcross, both directions kept as given). Pairs of joints on
// the plane are dropped: those joints keep their names whatever a map says.
// With `surface` >= 0, only pairs with a joint that weights that surface are
// kept. Detect Pairs and `--joint-map detect` use it to propose only pairs
// that apply to the chosen axis.
[[nodiscard]] QMap<QString, QString> modelMirrorJointPairsForAxis(const ModelMesh &mesh, const QMap<QString, QString> &map, int axis,
	int surface = -1, double tolerance = 0.001);
// Checks that every name exists, that the map is symmetric (each pair maps
// both ways, added when only one direction is given) and that no joint maps
// to two partners. Returns the completed map.
bool completeModelMirrorJointMap(const ModelSkeleton &skeleton, const QMap<QString, QString> &map, QMap<QString, QString> *complete,
	QString *error = nullptr);

// --- Reconciliation ---------------------------------------------------------

enum class ModelEditKind;
// How an edit kind keeps skin influences on a skeletal model.
enum class ModelEditSkinHandling
{
	// Skeleton, skin and weight edits write influences directly.
	Direct,
	// The tool writes complete influences itself (edge bevel, knife, spin, rip).
	Authored,
	// Every resulting vertex's influences follow its recorded origin exactly.
	Provenance,
	// New geometry without a source vertex follows the binding the edit
	// names (ModelMeshToolOptions::newGeometryBinding); without one, adding to
	// a skinned surface is refused.
	Binding,
	// Frame edits: vertices keep their influences, frames keep their clip
	// mapping, and frames that follow the skeleton are refused.
	Frames,
};
[[nodiscard]] ModelEditSkinHandling modelEditSkinHandling(ModelEditKind kind);

// How new vertices without a source (Add Primitive) follow the skeleton.
enum class ModelNewGeometryBinding
{
	// No binding: refused on a skinned surface; unskinned anywhere else.
	None,
	// Rigidly to one joint (weight 1), bound at the bind pose.
	Joint,
	// To the nearest bones, as Skin Weights and Binding does (four
	// influences, falloff 2).
	NearestBones,
};

// How a skinned surface's influences were derived by one edit.
struct ModelSkinProvenance
{
	// The tool wrote complete influences itself (edge bevel, knife, spin, rip).
	bool authored = false;
	// Copied unchanged from the vertex's own source.
	int kept = 0;
	// Blended from several source vertices, as their positions were.
	int interpolated = 0;
	// Copied through the mirror joint map, then rebound at the mirrored position.
	int mirrored = 0;
	// Copied from the source vertex, then rebound where the tool moved it.
	int rebound = 0;
	// Given the equal blend of the vertices they merged with.
	int merged = 0;
	// New geometry bound by the edit's binding (a joint or the nearest bones).
	int bound = 0;
	// Interpolated or merged vertices that now have more joint influences
	// than any vertex of the surfaces they came from (previousLimit), up to
	// `widest`. Reduce and normalise weights (PruneSkinWeights) can limit them.
	int widened = 0;
	int widest = 0;
	int previousLimit = 0;
	bool operator==(const ModelSkinProvenance &) const = default;
};

struct ModelSkinReconcileOptions
{
	// Mirror partners by joint name. Joints on the mirror plane keep their
	// name, listed or not; a listed off-centre joint's partner must sit on the
	// other side (modelJointsMirrorAcross). Off-centre joints need a partner
	// unless keepJointNames.
	QMap<QString, QString> mirrorJoints;
	bool keepJointNames = false;
	// How far a joint's bind position may sit from a mirror plane and still
	// count as centred, in model units.
	double centreTolerance = 0.001;
	// How vertices without a source are bound; Joint names bindJoint.
	ModelNewGeometryBinding newGeometry = ModelNewGeometryBinding::None;
	QString bindJoint;
	// For a frame edit: the frame before the edit that each resulting frame
	// came from, or -1 for a new or overwritten frame (independent, -2, in
	// skeleton.bakedClipForFrame). See modelSkeletalFrameOrigins.
	QVector<int> frameOrigins;
};

// Frame edits on skeletal models. Empty when `kind` with these frames keeps
// every frame that follows the skeleton (the bind frame and frames baked from
// a skeletal clip), or when no surface follows the skeleton; otherwise a
// translated reason naming the frame and the skeletal tool to use instead, in
// the editor and on the command line. `frame` is the edited frame (the
// duplicated, deleted or destination frame, or the first in-between endpoint).
[[nodiscard]] QString modelSkeletalFrameEditRefusal(const ModelMesh &mesh, ModelEditKind kind, int frame);
// The resulting frame list of a frame edit, as the frame before the edit each
// resulting frame came from, or -1 for a new frame and for the frame Copy
// Full Pose overwrites. Empty for other edits.
[[nodiscard]] QVector<int> modelSkeletalFrameOrigins(const ModelMesh &mesh, ModelEditKind kind, int frame, int inserted = 1);

// Derives the influences of every surface of `after` from its origins (see
// the top of this file), keeps skeleton.bakedClipForFrame in step with the
// frame list, clears every surface's origins and validates the skeleton.
// `report` receives one entry per resulting skinned surface. Refusals leave
// `after` in an unspecified state; callers restore their own copy.
bool reconcileModelSkinningFromOrigins(const ModelMesh &before, ModelMesh *after, const ModelSkinReconcileOptions &options,
	QMap<int, ModelSkinProvenance> *report, QString *error = nullptr, const ModelWorkControl &control = {});

// Translated one-line summary across surfaces: interpolated, mirrored,
// rebound, merged, bound and widened counts, plus kept counts when
// includeKept. Empty when there is nothing to say.
[[nodiscard]] QString modelSkinProvenanceSummary(const QMap<int, ModelSkinProvenance> &report, bool includeKept = false);
// One translated line per reported surface, for CLI text output.
[[nodiscard]] QStringList modelSkinProvenanceLines(const ModelMesh &mesh, const QMap<int, ModelSkinProvenance> &report);
// {"surfaces": [{index, name, authored, kept, interpolated, mirrored,
// rebound, merged, bound, widened, widest, previousLimit}], plus each count's
// total and the largest widest} in surface order.
[[nodiscard]] QJsonObject modelSkinProvenanceJson(const ModelMesh &mesh, const QMap<int, ModelSkinProvenance> &report);

} // namespace vibestudio
