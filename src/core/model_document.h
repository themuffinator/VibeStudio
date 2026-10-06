#pragma once

#include "core/model_file_io.h"
#include "core/model_mdl.h"
#include "core/model_mesh.h"
#include "core/model_topology.h"
#include "core/model_transform.h"
#include "core/model_uv.h"

#include <QByteArray>
#include <QJsonObject>
#include <QSet>

namespace vibestudio
{

// Authoring limits are independent of individual game formats. Validation and
// exporters enforce their narrower limits without silently trimming geometry.
inline constexpr int modelDocumentMaxVertices = 65536;
inline constexpr int modelDocumentMaxSurfaces = 32;
inline constexpr int modelDocumentMaxTriangles = 131072;
inline constexpr int modelDocumentMaxFrames = 1024;
inline constexpr int modelDocumentMaxAnimations = 1024;
inline constexpr qint64 modelDocumentMaxFrameVertices = 1048576;
inline constexpr qint64 modelDocumentMaxSourceBytes = 64 * 1024 * 1024;

struct ModelSelection
{
	int surface = 0;
	QSet<int> vertices;
	QSet<int> faces;
	QSet<ModelEdge> edges{};
	// One named attachment may be selected instead of mesh components.
	QString tag{};
	QString collision{};
	// Whole-surface selection includes unused vertices; surface is the active member.
	QSet<int> surfaces{};
	bool operator==(const ModelSelection &) const = default;
};

enum class ModelEditKind
{
	Transform,
	Extrude,
	Subdivide,
	DuplicateFaces,
	DeleteFaces,
	FlipFaces,
	RecalculateNormals,
	TransformUv,
	ProjectUv,
	SetMaterial,
	DuplicateFrame,
	DeleteFrame,
	RenameFrame,
	SplitEdges,
	WeldVertices,
	MarkUvSeams,
	ClearUvSeams,
	DetachUv,
	SetMd2SkinSize,
	AddTag,
	DuplicateTag,
	RenameTag,
	DeleteTag,
	SetTagOrigin,
	ResetTagOrientation,
	TransformTag,
	CopyTagPose,
	UnwrapUv,
	PackUv,
	RemoveDuplicateFaces,
	RemoveUnusedVertices,
	SplitDisconnectedFans,
	OrientFaces,
	AddAnimation,
	RenameAnimation,
	SetAnimationRange,
	DeleteAnimation,
	InsertInbetweens,
	CopyFramePose,
	AddMdlSkin,
	ReplaceMdlSkinMember,
	AppendMdlSkinMember,
	RemoveMdlSkin,
	RemoveMdlSkinMember,
	SetMdlSkinDuration,
	SetMdlHeader,
	SetMdlPalette,
	GroupMdlFrames,
	UngroupMdlFrames,
	SetMdlFrameDuration,
	AddCollisionBox,
	FitCollisionBox,
	UpdateCollisionBox,
	DuplicateCollisionBox,
	DeleteCollisionBox,
	TransformCollisionBox,
	ApplySkinBindings,
	FillBoundaryLoops,
	RenameSurface,
	SeparateFaces,
	MoveFacesToSurface,
	DuplicateSurface,
	DeleteSurface,
	JoinSurfaces,
	SetMaterialSlots,
	SetAnimationRate,
	BridgeBoundaryLoops,
	SplitNonmanifoldEdges,
	PackUvAround,
	AnimateCollisionBox,
	FreezeCollisionBox,
	FitAnimatedCollisionBox,
};

struct ModelEdit
{
	ModelEditKind kind = ModelEditKind::Transform;
	ModelSelection selection;
	// -1 applies position transforms to all frames. Topology always spans all
	// frames. Frame operations require one explicit frame index.
	int frame = -1;
	ModelVec3 translation;
	ModelVec3 rotation;
	ModelVec3 scale{1, 1, 1};
	ModelVec3 pivot;
	ModelTexCoord uvScale{1, 1};
	ModelTexCoord uvOffset;
	double uvRotation = 0;
	// XY=0, XZ=1, YZ=2, projected in model units before the UV transform.
	int projection = 0;
	QString text;
	double weldDistance = 0.001;
	bool preserveSeams = true;
	double translationGrid = 0;
	ModelTexCoord uvPivot;
	ModelUvPivot uvPivotMode = ModelUvPivot::Origin;
	bool uvIslands = false;
	double uvTranslationGrid = 0;
	QSize md2SkinSize{256, 256};
	double rotationGrid = 0, scaleGrid = 0;
	ModelTransformPivot pivotMode = ModelTransformPivot::Custom;
	// Selection-centre pivots use this pose for all-frame edits; a frame-local
	// edit uses its own pose. The resolved pivot stays fixed across every pose.
	int pivotFrame = 0;
	ModelVec3 tagOrigin;
	int sourceFrame = 0;
	int bridgeTwist = 0;
	int uvAtlasResolution = 512, uvAtlasPadding = 4;
	int uvAtlasHeight = 0;
	bool uvPreserveScale = false;
	int animationIndex = -1, rangeFirst = 0, rangeLast = 0;
	double animationRate = 0;
	int inbetweenCount = 1;
	ModelMdlSkinInput mdlSkin;
	int mdlSkinSlot = 0, mdlSkinMember = 0;
	double mdlDuration = .1;
	// Header edits use eyePosition, flags, syncType and size only. Palette
	// edits use palette and paletteGenerated; native ranges remain independent.
	ModelMdlSettings mdlSettings;
	ModelCollisionBox collisionBox;
	// UpdateCollisionBox field mask: centre=1, size=2, rotation=4. A zero
	// mask renames without changing any pose; GUI Apply Box supplies all fields.
	int collisionFields = 7;
	QByteArray skinBindings;
	// Surface operations span every pose. Join requires the target in surfaces;
	// move uses selection.faces. Material replacement must be explicit.
	QSet<int> surfaces;
	int targetSurface = -1;
	bool adoptTargetMaterials = false;
	// Complete ordered external binding list, including an explicit empty list.
	QStringList materialSlots;
	ModelTransformSpace transformSpace = ModelTransformSpace::World;
	ModelVec3 axisRotation;
	// -1 uses the edited frame, or pivotFrame for an all-frame operation.
	int axesFrame = -1;
};

struct ModelExportReport
{
	int storedVertices = 0;
	double maxPositionError = 0;
	double maxUvError = 0;
	double maxNormalAngleDegrees = 0;
	QStringList notes;
};

// Both parsers and mutations leave outputs untouched on failure. No incomplete
// native decode (including warnings) is accepted as an editable source.
QStringList validateEditableModel(const ModelMesh &mesh, const ModelWorkControl &control = {});
void updateEditableModelMetadata(ModelMesh *mesh);
bool importEditableModel(const QString &virtualPath, const QByteArray &bytes, ModelMesh *mesh, QString *error = nullptr,
						 const IdTechPalette *palette = nullptr, const ModelWorkControl &control = {});
QJsonObject editableModelJson(const ModelMesh &mesh, QString *error = nullptr, const ModelWorkControl &control = {});
bool parseEditableModel(const QByteArray &bytes, ModelMesh *mesh, QString *error = nullptr, const ModelWorkControl &control = {});
bool applyModelEdit(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *resultingSelection = nullptr, QString *error = nullptr,
					const ModelWorkControl &control = {});
QByteArray exportEditableModel(const ModelMesh &mesh, const QString &format, int frame = 0, QString *error = nullptr,
							   const ModelWorkControl &control = {}, ModelExportReport *report = nullptr);

// Selection-aware history, bounded by count and estimated geometry/selection size. The
// source fingerprint guards saves against an external writer; imported game
// files are never the writable authoring source.
class ModelDocument
{
  public:
	bool setMesh(const ModelMesh &mesh, QString *error = nullptr, const ModelWorkControl &control = {});
	// A recovered copy has no writable source binding and always needs saving.
	bool restoreDraft(const ModelMesh &mesh, const ModelSelection &selection, QString *error = nullptr,
					  const ModelWorkControl &control = {});
	bool load(const QString &path, QString *error = nullptr, const ModelWorkControl &control = {});
	bool save(const QString &path, bool overwrite = false, QString *error = nullptr, const ModelWorkControl &control = {});
	bool edit(const ModelEdit &operation, QString *error = nullptr, const ModelWorkControl &control = {});
	bool undo();
	bool redo();
	[[nodiscard]] bool canUndo() const;
	[[nodiscard]] bool canRedo() const;
	[[nodiscard]] bool isModified() const;
	[[nodiscard]] const ModelMesh &mesh() const;
	[[nodiscard]] const ModelSelection &selection() const;
	[[nodiscard]] const ModelSurfaceTopology &surfaceTopology(int surface) const;
	void setSelection(const ModelSelection &selection);
	[[nodiscard]] QString path() const;
	[[nodiscard]] QByteArray sourceFingerprint() const;
	[[nodiscard]] QByteArray revisionFingerprint() const;
	[[nodiscard]] qint64 historyBytes() const;

  private:
	struct State
	{
		ModelMesh mesh;
		ModelSelection selection;
		QByteArray digest;
		qint64 bytes = 0;
		QVector<ModelSurfaceTopology> topology;
		qint64 estimatedBytes() const;
	};
	static bool prepareTopology(State *state, QString *error, const ModelWorkControl &control);
	State m_state;
	QVector<State> m_undo, m_redo;
	QByteArray m_savedDigest, m_sourceFingerprint;
	QString m_path, m_resolvedPath;
	void trimHistory();
};

} // namespace vibestudio
