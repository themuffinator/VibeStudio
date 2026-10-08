#pragma once

// Skeleton maths, validation, skinning and baking for the skeletal model
// representation declared in core/model_mesh.h (ModelSkeleton, ModelJoint,
// ModelSurfaceSkinning and friends).
//
// Decoders fill the joints, influences and clips, then call
// bakeModelSkeleton, which writes the bind pose and every clip frame into the
// mesh's ordinary per-surface frames, frame list, animations and tags. The
// rest of the studio (viewport, mesh tools, vertex-animation exports) reads
// those frames; skeletal exports read the skeleton itself.
//
// Conventions: matrices are row-major 3x4 acting on column vectors
// (p' = M * p); quaternions are Hamilton (x, y, z, w) and rotate with
// q * v * conj(q). Joint parents always precede their children.

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

// --- Maths -----------------------------------------------------------------

[[nodiscard]] ModelJointMatrix modelJointIdentity();
[[nodiscard]] ModelJointMatrix modelJointMatrix(const ModelQuat& rotation, const ModelVec3& translation,
	const ModelVec3& scale = ModelVec3{1.0f, 1.0f, 1.0f});
// a * b: b is applied first.
[[nodiscard]] ModelJointMatrix modelJointMultiply(const ModelJointMatrix& a, const ModelJointMatrix& b);
// The inverse of an affine matrix. A singular matrix yields the identity and
// sets *ok to false.
[[nodiscard]] ModelJointMatrix modelJointInverse(const ModelJointMatrix& matrix, bool* ok = nullptr);
[[nodiscard]] ModelVec3 modelJointTransformPoint(const ModelJointMatrix& matrix, const ModelVec3& point);
// Applies only the 3x3 part.
[[nodiscard]] ModelVec3 modelJointTransformVector(const ModelJointMatrix& matrix, const ModelVec3& vector);
// Transforms a normal by the inverse transpose of the 3x3 part and
// normalizes it, so non-uniform scale keeps normals perpendicular.
[[nodiscard]] ModelVec3 modelJointTransformNormal(const ModelJointMatrix& matrix, const ModelVec3& normal);
[[nodiscard]] ModelVec3 modelJointTranslation(const ModelJointMatrix& matrix);
// The rotation of the 3x3 part with any scale divided out, as a unit
// quaternion with w >= 0.
[[nodiscard]] ModelQuat modelJointRotation(const ModelJointMatrix& matrix);
// The length of each column of the 3x3 part.
[[nodiscard]] ModelVec3 modelJointScale(const ModelJointMatrix& matrix);
[[nodiscard]] bool modelJointMatrixIsFinite(const ModelJointMatrix& matrix);
// The largest absolute difference between corresponding entries.
[[nodiscard]] double modelJointMatrixDistance(const ModelJointMatrix& a, const ModelJointMatrix& b);

[[nodiscard]] ModelQuat modelQuatNormalized(const ModelQuat& q);
[[nodiscard]] ModelQuat modelQuatMultiply(const ModelQuat& a, const ModelQuat& b);
[[nodiscard]] ModelQuat modelQuatConjugate(const ModelQuat& q);
[[nodiscard]] ModelVec3 modelQuatRotate(const ModelQuat& q, const ModelVec3& v);
// Shortest-path spherical interpolation.
[[nodiscard]] ModelQuat modelQuatSlerp(const ModelQuat& a, const ModelQuat& b, float t);
// Rebuilds a unit quaternion from the three components an MD5 file stores,
// taking w = -sqrt(1 - x*x - y*y - z*z). Doom 3 rebuilds w >= 0 and applies the
// matrix with its row-vector convention (idMat3 * idVec3 sums the rows'
// components column by column), which is the conjugate rotation in the
// column-vector maths used here; the negative w is that conjugate.
[[nodiscard]] ModelQuat modelQuatFromXyzNegativeW(float x, float y, float z);
// The three components an MD5 file stores for a rotation: the conjugate
// form above with w <= 0, so modelQuatFromXyzNegativeW reads it back.
[[nodiscard]] ModelVec3 modelQuatToXyzNegativeW(const ModelQuat& q);
// Quake's AnglesToAxis in degrees (pitch, yaw, roll, q_math.c): the matrix
// columns are the forward, left and up axes, so a local point (x, y, z) lands
// at x * forward + y * left + z * up + translation.
[[nodiscard]] ModelJointMatrix modelJointFromQuakeAngles(float pitch, float yaw, float roll, const ModelVec3& translation);

// --- Hierarchy -------------------------------------------------------------

// Concatenates parent-relative transforms into model space. `parents[i]` is
// -1 or less than i; anything else yields an empty result.
[[nodiscard]] QVector<ModelJointMatrix> modelJointsToModelSpace(const QVector<int>& parents, const QVector<ModelJointMatrix>& local);
// The inverse: each joint relative to its parent.
[[nodiscard]] QVector<ModelJointMatrix> modelJointsToLocalSpace(const QVector<int>& parents, const QVector<ModelJointMatrix>& modelSpace);
[[nodiscard]] QVector<int> modelJointParents(const ModelSkeleton& skeleton);
[[nodiscard]] int modelJointIndex(const ModelSkeleton& skeleton, const QString& name, Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive);
// The bind pose of every joint, in joint order.
[[nodiscard]] QVector<ModelJointMatrix> modelSkeletonBindPose(const ModelSkeleton& skeleton);

// --- Validation ------------------------------------------------------------

// Parents precede children, names are unique, matrices are finite, every
// clip frame has one matrix per joint, every skinned surface has one
// influence range per vertex naming existing joints with finite weights, and
// tags name existing joints. False with the first problem otherwise.
[[nodiscard]] bool validateModelSkeleton(const ModelMesh& mesh, QString* error = nullptr);
// Scales each vertex's weights to sum to one and drops zero-weight
// influences. Returns how many vertices needed it.
int normalizeModelSkinningWeights(ModelSurfaceSkinning* skinning);

// --- Skinning --------------------------------------------------------------

// Positions (and normals) of a skinned surface under `pose` (one model-space
// matrix per joint). Influences without a normal offset contribute none; when
// a vertex has no normal at all the normals are rebuilt from the faces.
[[nodiscard]] bool skinModelSurface(const ModelSurface& surface, const QVector<ModelJointMatrix>& pose, ModelFrameGeometry* out,
	QString* error = nullptr);
// Area-weighted vertex normals from the faces of one frame's positions, using
// counter-clockwise front faces.
[[nodiscard]] QVector<ModelVec3> modelFaceNormals(const ModelSurface& surface, const QVector<ModelVec3>& positions);
// For formats that store bind-pose vertices: expresses each vertex position
// and normal in the bind space of each of its joints. `joints`/`weights` hold
// `perVertex` entries per vertex; weights at or below zero are skipped.
[[nodiscard]] bool modelSkinningFromBindPose(const ModelSkeleton& skeleton, const QVector<ModelVec3>& positions,
	const QVector<ModelVec3>& normals, const QVector<int>& joints, const QVector<float>& weights, int perVertex,
	ModelSurfaceSkinning* out, QString* error = nullptr);

// --- Baking ----------------------------------------------------------------

struct ModelSkeletonBakeOptions {
	// Write the bind pose as the first frame. Formats with no separate bind
	// pose (their bind is the first clip frame) turn this off.
	bool includeBindPose = true;
	// Clip names to bake, in skeleton order; empty bakes every clip.
	QStringList clips;
	// The most frames to write; clips past it are left out with a warning.
	int maxFrames = 8192;
	// The most vertex positions (vertices times frames) to write, which bounds
	// memory for long clip lists on dense meshes; clips past it are left out
	// with a warning too.
	qint64 maxVertexSlots = 4LL * 1024LL * 1024LL;
};

// Replaces the mesh's frames, per-surface frame geometry, animations and
// tags with the baked bind pose and clips. Surfaces without skinning keep
// their first frame's geometry in every baked frame. Fills
// skeleton.bakedClipForFrame and notes clips left out over the limits in
// mesh->detailLines (the skeleton keeps them).
[[nodiscard]] bool bakeModelSkeleton(ModelMesh* mesh, const ModelSkeletonBakeOptions& options = {}, QString* error = nullptr,
	const ModelWorkControl& control = {});

// The frame name a baked clip frame receives: the clip name and the frame
// number, cut to the 16 bytes MD3/MD2 frame names hold.
[[nodiscard]] QString modelBakedFrameName(const QString& clipName, int frame);

// One line per joint, indented by depth, for summaries and the CLI.
[[nodiscard]] QStringList modelSkeletonSummaryLines(const ModelSkeleton& skeleton);

// --- Editing ---------------------------------------------------------------
//
// An editable skeletal model keeps its joints, clips and influences while
// the mesh tools work on its frames. The bind pose is the frame
// modelBindPoseFrame names; editing it and then rebinding (or re-baking)
// carries the edit into the skeletal exports and the animation.

// The baked frame that shows the bind pose: the frame baked from no clip,
// else frame 0 when every joint's bind equals the first clip's first frame
// (formats whose bind is that frame). -1 when there is none.
[[nodiscard]] int modelBindPoseFrame(const ModelMesh& mesh);
// Recomputes the offsets of every vertex whose position in `frame` no longer
// matches its skinned bind position, so the frame becomes the bind pose;
// joints and weights stay, and untouched vertices keep their offsets exactly.
// Returns how many vertices were rebound.
int rebindModelSkinning(ModelMesh* mesh, int frame, double tolerance = 1e-4);
// After an edit that changed a skinned surface's vertices: each vertex of
// `after` takes the influences of the `before` vertex at the same bind
// position, or else of the nearest one, then is rebound to its own position.
// Surfaces are matched by index. Also keeps skeleton.bakedClipForFrame in
// step with the frame list (frames the edit added are marked -2: edited, not
// from a clip). False only when the result cannot be made consistent.
bool reconcileModelSkinning(const ModelMesh& before, ModelMesh* after, QString* error = nullptr);
// Rebinds from the bind frame, then bakes the bind pose and the same clips
// again, so every frame follows the edited bind pose.
bool rebakeModelSkeleton(ModelMesh* mesh, QString* error = nullptr, const ModelWorkControl& control = {},
	int maxFrames = 8192);

// The skeleton and each surface's influences as JSON for editable sources
// (.mesh.json version 8), and back. Reading checks counts and indexes and
// leaves `mesh` unchanged on failure.
[[nodiscard]] QJsonObject modelSkeletonJson(const ModelMesh& mesh);
bool modelSkeletonFromJson(const QJsonObject& object, ModelMesh* mesh, QString* error = nullptr);

} // namespace vibestudio
