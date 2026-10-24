#pragma once

#include "core/model_file_io.h"
#include "core/model_mdl.h"
#include "core/model_mesh_tools.h"
#include "core/model_mesh.h"
#include "core/model_rig.h"
#include "core/model_ik.h"
#include "core/model_animation_curves.h"
#include "core/model_curve_key_tools.h"
#include "core/model_curve_transfer.h"
#include "core/model_document_memory.h"
#include "core/model_skeletal_edit.h"
#include "core/model_skeletal_working_set.h"
#include "core/model_source_encoding.h"
#include "core/model_skinning_tools.h"
#include "core/model_targets.h"
#include "core/model_weight_paint.h"
#include "core/model_weight_transfer.h"
#include "core/model_topology.h"
#include "core/model_transform.h"
#include "core/model_uv.h"
#include "core/model_uv_density.h"
#include "core/model_vertex_origins.h"

#include <QByteArray>
#include <QJsonObject>
#include <QLocale>
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
	// One joint is selected instead of mesh components, tags or collision.
	QString joint{};
	bool operator==(const ModelSelection &) const = default;
};

// Optional transient provenance for editor state. A reported surface has one
// original face index per resulting triangle. Absent surfaces have no mapping;
// callers must not infer identity for operations that do not report provenance.
struct ModelEditReport
{
	QHash<int, QVector<int>> sourceFaces;
	// How each resulting skinned surface's joint influences were derived
	// (core/model_vertex_origins.h). Empty for models without a skeleton and
	// for skeleton, skin and weight edits, which write influences directly.
	QMap<int, ModelSkinProvenance> skin;
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
	// Edit-mode mesh tools (core/model_mesh_tools.h).
	RotateEdges,
	MergeVertices,
	DissolveVertices,
	DissolveFaces,
	PokeFaces,
	BeautifyFaces,
	MakeFace,
	ExtrudeEdges,
	InsetFaces,
	ShrinkFatten,
	SmoothVertices,
	WeightedTransform,
	ShadeFlat,
	ShadeSmooth,
	ShadeAutoSmooth,
	Bisect,
	Symmetrize,
	LoopCut,
	AddPrimitive,
	Decimate,
	BevelVertices,
	Solidify,
	ProjectUvMapping,
	AddJoint,
	RenameJoint,
	ReparentJoint,
	DeleteJoint,
	SetJointWeight,
	TransformBindJoint,
	AddSkeletalClip,
	DuplicateSkeletalClip,
	RenameSkeletalClip,
	DeleteSkeletalClip,
	ResizeSkeletalClip,
	SetSkeletalClipTiming,
	TransformSkeletalPose,
	CopySkeletalPose,
	InterpolateSkeletalPoses,
	BindSkin,
	RebindSkin,
	UnbindSkin,
	PruneSkinWeights,
	PaintSkinWeights,
	TransferSkinWeights,
	SolveTwoBoneIk,
	EditAnimationCurves,
	EditCurveKeys,
	TransferAnimationCurves,
	SlideEdges,
	BevelEdges,
	Knife,
	SetUvDensity,
	Spin,
	Rip,
	// Chooses the skeletal clips baked into editable frames
	// (core/model_skeletal_working_set.h); ModelSkeletalEditOptions::workingSet.
	SetSkeletalWorkingSet,
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
	// Edit-mode tool settings; only mesh tools accept non-default values.
	ModelMeshToolOptions tool{};
	ModelRigOptions rig{};
	ModelSkeletalEditOptions skeletal{};
	ModelIkOptions ik{};
	ModelCurveEditOptions curves{};
	ModelCurveKeyOptions curveKeys{};
	ModelCurveTransferOptions curveTransfer{};
	ModelSkinOptions skin{};
	QVector<ModelWeightStroke> weightStrokes;
	ModelWeightTransferOptions weightTransfer{};
	ModelUvDensityOptions uvDensity{};
};

struct ModelExportReport
{
	int storedVertices = 0;
	double maxPositionError = 0;
	double maxUvError = 0;
	double maxNormalAngleDegrees = 0;
	QStringList notes;
	// The notes about data the target format does not hold, also in `notes`.
	// A strict export is refused while this is not empty.
	QStringList omissions;
	// md5anim: the clip written, its frame count and the frame rate written.
	int clip = -1;
	QString clipName;
	int clipFrames = 0;
	int frameRate = 0;
	// IQM: vertices whose influences past the fourth were dropped, the largest
	// share of one vertex's weight they carried, and the largest weight change
	// from 8-bit storage.
	int reducedInfluenceVertices = 0;
	double maxDroppedWeight = 0;
	double maxWeightError = 0;
	// IQM: the largest joint pose change from its 16-bit pose channels.
	double maxJointTranslationError = 0;
	double maxJointRotationDegrees = 0;
	double maxJointScaleError = 0;
	// IQM: how far a reader's rebuild moves a skinned vertex from the editor's
	// pose with exact weights and channels, so before 8-bit and 16-bit
	// rounding: the layout's own loss (one bind position and four influences
	// per vertex, no joint shear). maxPositionError includes the rounding.
	double maxLayoutError = 0;
	// The delivery target the export was checked against (empty for OBJ and
	// ASE) and every limit the model exceeds for it. An export refused by the
	// preflight keeps this report so callers can list each blocker.
	ModelTargetReport target;
};

struct ModelExportOptions
{
	// OBJ and ASE write this frame. For md5anim, when `clip` is negative, it
	// selects the skeletal clip the frame is a sample of (or the only clip).
	int frame = 0;
	// md5anim: the skeletal clip to write; negative selects it from `frame`.
	int clip = -1;
	// md5anim: a whole frame rate that replaces the clip's own; zero keeps it.
	int md5FrameRate = 0;
	// Refuse, writing nothing, when the report lists an omission. The refused
	// export still fills the report so callers can show what was left out.
	bool strict = false;
	// The delivery target (core/model_targets.h) for MDL, MD2, MD3, IQM and
	// MD5; empty selects the format's default. The export checks every limit
	// of the target first and writes nothing when one is exceeded.
	QString target;
};

// Both parsers and mutations leave outputs untouched on failure. No incomplete
// native decode (including warnings) is accepted as an editable source.
QStringList validateEditableModel(const ModelMesh &mesh, const ModelWorkControl &control = {});
void updateEditableModelMetadata(ModelMesh *mesh);
bool importEditableModel(const QString &virtualPath, const QByteArray &bytes, ModelMesh *mesh, QString *error = nullptr,
						 const IdTechPalette *palette = nullptr, const ModelWorkControl &control = {});
// How importEditableModel treats a skeletal model whose frames are re-baked
// from its skeleton (core/model_skeletal_working_set.h).
struct ModelImportOptions
{
	// Bake exactly the clips `workingSet` names (clip names or zero-based
	// indexes; empty keeps only the bind pose), refusing a choice over the
	// editable budget. Otherwise only a model over the editable budget is
	// re-baked, with the clips that fit the automatic budget in file order.
	bool chooseWorkingSet = false;
	QStringList workingSet;
};
struct ModelImportReport
{
	// True when the frames were re-baked from the skeleton; workingSet then
	// says what was baked and what was left out, and why.
	bool rebaked = false;
	// True when ModelImportOptions::workingSet chose the clips; false when the
	// model was over the editable budget and the import fitted it.
	bool chosen = false;
	// True when the import failed because a chosen clip name or index names
	// no clip, or names one twice: a usage error, not a problem with the model.
	bool unresolvedClips = false;
	ModelSkeletalWorkingSetPlan workingSet;
};
bool importEditableModel(const QString &virtualPath, const QByteArray &bytes, ModelMesh *mesh, const ModelImportOptions &options,
						 ModelImportReport *report, QString *error = nullptr, const IdTechPalette *palette = nullptr,
						 const ModelWorkControl &control = {});
// importEditableModel's fit for an already decoded model, such as a package
// model handed to the Mesh Editor: a skeletal model over the editable frame
// budget is re-baked into the automatic working set, and `report` says so.
// Anything else is left alone. False with an error when the fit fails.
bool fitEditableModelBudget(ModelMesh *mesh, ModelImportReport *report = nullptr, QString *error = nullptr, const ModelWorkControl &control = {});
// The editable source (core/model_source_encoding.h). Automatic writes packed
// arrays when the readable arrays alone would pass the source limit.
QJsonObject editableModelJson(const ModelMesh &mesh, QString *error = nullptr, const ModelWorkControl &control = {},
							  ModelSourceEncoding encoding = ModelSourceEncoding::Readable);
// The compact source bytes every writer of editable sources publishes: saves,
// recovery payloads, CLI outputs and editable bakes. Automatic writes readable
// JSON while it fits modelDocumentMaxSourceBytes, else packed arrays, and
// tries the other form when the first passes the limit. Refuses, naming the
// size, a source over that limit in both. `written` receives the encoding used
// (Readable or Packed).
QByteArray serializeEditableModel(const ModelMesh &mesh, QString *error = nullptr, const ModelWorkControl &control = {},
								  ModelSourceEncoding encoding = ModelSourceEncoding::Automatic, ModelSourceEncoding *written = nullptr);
// The size the source limit applies to: the exact packed size, or, when that
// passes modelDocumentMaxSourceBytes, the exact readable size if it is
// smaller (short decimals can beat base64 for whole-number geometry). A
// source fits the limit exactly when Automatic can save it. -1 on
// cancellation or error.
qint64 projectEditableModelBytes(const ModelMesh &mesh, QString *error = nullptr, const ModelWorkControl &control = {});
// "12.3 MiB", for source and history sizes in messages. The C locale keeps
// CLI and core messages deterministic; the editor passes QLocale().
QString modelMebibytesText(qint64 bytes, const QLocale &locale = QLocale::c());
bool parseEditableModel(const QByteArray &bytes, ModelMesh *mesh, QString *error = nullptr, const ModelWorkControl &control = {});
bool applyModelEdit(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *resultingSelection = nullptr, QString *error = nullptr,
					const ModelWorkControl &control = {}, ModelEditReport *report = nullptr);
// Native export with ModelExportOptions::frame set to `frame`.
QByteArray exportEditableModel(const ModelMesh &mesh, const QString &format, int frame = 0, QString *error = nullptr,
							   const ModelWorkControl &control = {}, ModelExportReport *report = nullptr);
// Each format reports what it leaves out or approximates in `report`
// (notes, omissions among them).
QByteArray exportEditableModel(const ModelMesh &mesh, const QString &format, const ModelExportOptions &options, QString *error = nullptr,
							   const ModelWorkControl &control = {}, ModelExportReport *report = nullptr);
// The format ids exportEditableModel writes, in menu order.
[[nodiscard]] QStringList modelExportFormatIds();

// The memory undo and redo may retain beyond the live document
// (ModelDocument::historyBytes). The Mesh Editor applies the
// model/historyBudgetMiB setting; this default holds elsewhere.
inline constexpr qint64 modelDocumentDefaultHistoryBytes = 64LL * 1024 * 1024;
inline constexpr int modelDocumentMaxHistorySteps = 100;
// The history limit every ModelDocument trims to, at least 1 MiB.
void setModelDocumentHistoryLimit(qint64 bytes);
[[nodiscard]] qint64 modelDocumentHistoryLimit();

// Selection-aware history, bounded by step count and by the memory history
// retains (core/model_document_memory.h). Every state stays saveable: an edit
// that would take the source (projectEditableModelBytes) over modelDocumentMaxSourceBytes is
// refused unless it shrinks a source already over. The source fingerprint
// guards saves against an external writer; imported game files are never the
// writable authoring source.
class ModelDocument
{
  public:
	bool setMesh(const ModelMesh &mesh, QString *error = nullptr, const ModelWorkControl &control = {});
	// A recovered copy has no writable source binding and always needs saving.
	bool restoreDraft(const ModelMesh &mesh, const ModelSelection &selection, QString *error = nullptr,
					  const ModelWorkControl &control = {});
	bool load(const QString &path, QString *error = nullptr, const ModelWorkControl &control = {});
	// Dry runs validate the complete serialised source and destination without
	// creating files/directories or adopting a saved identity/history baseline.
	bool save(const QString &path, bool overwrite = false, QString *error = nullptr, const ModelWorkControl &control = {},
			  bool dryRun = false);
	bool edit(const ModelEdit &operation, QString *error = nullptr, const ModelWorkControl &control = {}, ModelEditReport *report = nullptr);
	// Adopts `mesh`, which a core service built from this document's revision
	// `baseRevision` by applying several edits (Prepare for target, a stored
	// detail-level recipe), as one undo step with `selection`. Refuses,
	// changing nothing, when the document changed since `baseRevision` or
	// `mesh` is not a valid editable model.
	bool adopt(const ModelMesh &mesh, const ModelSelection &selection, const QByteArray &baseRevision, QString *error = nullptr,
			   const ModelWorkControl &control = {});
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
	// The memory undo and redo retain beyond the live document: each block of
	// their states that the live state does not share, counted once however
	// many states share it, plus each state's own record.
	[[nodiscard]] qint64 historyBytes() const;
	[[nodiscard]] int undoSteps() const;
	[[nodiscard]] int redoSteps() const;
	// Steps trimmed to keep within the limits since the mesh was set or loaded.
	[[nodiscard]] int discardedHistorySteps() const;
	// Trims now, after the history limit was lowered. Edits and this trim
	// discard the oldest undo steps first and always keep the step before the
	// current one and every redo step; undo and redo never discard.
	void enforceHistoryLimit();
	// The size of the current state the source limit applies to
	// (projectEditableModelBytes).
	[[nodiscard]] qint64 sourceBytes() const;
	// The size and encoding of the last successful save, dry runs included.
	[[nodiscard]] qint64 savedSourceBytes() const;
	[[nodiscard]] ModelSourceEncoding savedSourceEncoding() const;

  private:
	struct State
	{
		ModelMesh mesh;
		ModelSelection selection;
		QByteArray digest;
		qint64 sourceBytes = 0;
		QVector<ModelSurfaceTopology> topology;
		void visitMemory(ModelMemoryVisitor &visitor) const;
	};
	static bool prepareTopology(State *state, QString *error, const ModelWorkControl &control);
	static bool summarize(State *state, QString *error, const ModelWorkControl &control);
	State m_state;
	QVector<State> m_undo, m_redo;
	QByteArray m_savedDigest, m_sourceFingerprint;
	QString m_path, m_resolvedPath;
	ModelMemoryLedger m_history;
	int m_discarded = 0;
	qint64 m_savedBytes = 0;
	ModelSourceEncoding m_savedEncoding = ModelSourceEncoding::Readable;
	void remember(const State &state);
	void forget(const State &state);
	void updateLive();
	void trimHistory();
};

// What the last save of `document` wrote, for CLI JSON: {bytes, encoding
// ("readable" or "packed"), limitBytes}.
QJsonObject modelSourceSaveJson(const ModelDocument &document);
// One sentence when the last save wrote packed arrays (source version 12),
// which older builds cannot read and which do not diff line by line; empty
// for a readable source.
QString modelSourceSaveNotice(const ModelDocument &document, const QLocale &locale = QLocale::c());

} // namespace vibestudio
