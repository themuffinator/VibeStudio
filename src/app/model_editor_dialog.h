#pragma once

#include "app/model_design_dialog.h"
#include "app/model_document_work.h"
#include "app/model_material_worker.h"
#include "app/status_line_label.h"
#include "core/model_document.h"
#include "core/model_collision.h"
#include "core/model_delivery.h"
#include "core/model_editor_controls.h"
#include "core/model_skin_source.h"
#include "core/model_skin_bindings.h"
#include "core/model_topology_health.h"
#include "core/model_intersections.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>

#include <functional>

class QAction;
class QBoxLayout;
class QCheckBox;
class QGridLayout;
class QFrame;
class QToolButton;
class QTreeWidget;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QTableView;
class QTabWidget;
class QTimer;
class QToolBar;
class QProgressBar;
class QPushButton;
class QSpinBox;

namespace vibestudio
{
class ModelComponentTable;
class ModelEditorTools;
class ModelIntersectionList;
class ModelViewport;
class ModelUvView;
class ModelRecoveryWriter;
class SidebarPage;
class StudioSidebar;

struct ModelCollisionDestination
{
	LevelMapDocument document;
	std::function<bool(const LevelMapDocument &, QString *)> publish;
};

// Where the editor's delivery targets come from when none is chosen in the
// session (core/model_delivery.h): the open project's saved choices and what
// its game installation suggests.
struct ModelEditorDeliveryContext
{
	QString projectRoot;
	ModelDeliveryPreferences project;
	ModelDeliveryPreferences installation;
	QString installationName;
};

// One file staged with others in one package operation (a base MD3 and its
// detail levels).
struct ModelStagedFile
{
	QString path;
	QByteArray bytes;
};

class ModelEditorDialog final : public QDialog
{
	// The Blender-style interaction layer drives the editor's own controls.
	friend class ModelEditorTools;
	friend class ModelLodDialog;
	friend class ModelSkinVariantsDialog;

  public:
	explicit ModelEditorDialog(QWidget *parent = nullptr);
	~ModelEditorDialog() override;
	[[nodiscard]] const ModelDocument &document() const;
	bool setMesh(const ModelMesh &mesh, QString *error = nullptr);
	bool openSource(const QString &path, QString *error = nullptr);
	bool repairSource(const QString &path, QString *error = nullptr);
	bool applyEdit(const ModelEdit &operation, QString *error = nullptr, ModelEditReport *report = nullptr);
	bool importMdlSkin(const QString &path, ModelEditKind kind, QString *error = nullptr);
	bool importMdlSkinFromPackage(const ModelSkinSourceReference &reference, ModelEditKind kind, QString *error = nullptr);
	bool importMdlPalette(const QString &path, QString *error = nullptr);
	bool importSkinBindings(const QString &path, QString *error = nullptr);
	bool importSkinBindingsFromPackage(const ModelSkinSourceReference &reference, QString *error = nullptr);
	bool inspectTopology(QString *error = nullptr);
	bool inspectIntersections(QString *error = nullptr);
	bool exportCollision(const QString &path, const ModelCollisionExport &request, bool overwrite, QString *error = nullptr);
	bool placeCollision(const ModelCollisionExport &request, QString *error = nullptr);
	// Native export (model_editor_export.cpp). The export actions choose the
	// destination, then call these; each shows its report under Export Report.
	bool exportToPath(const QString &path, const QString &format, const ModelExportOptions &options, QString *error = nullptr);
	// Every skeletal clip as `<clip>.md5anim` in `directory`, reviewed together:
	// nothing is written when a file exists and `overwrite` is false.
	bool exportMd5Clips(const QString &directory, const ModelExportOptions &options, bool overwrite, QString *error = nullptr);
	[[nodiscard]] const ModelExportReport &lastExportReport() const { return m_lastExport; }
	// The delivery target (core/model_targets.h) each format's exports, stages
	// and detail levels are checked against; the format's default until chosen.
	[[nodiscard]] QString deliveryTarget(const QString &format) const;
	bool setDeliveryTarget(const QString &format, const QString &id);
	// Every selected delivery target's full preflight, on the worker; shows the
	// results in a dialog when `show` is set.
	bool checkDeliveryTargets(QVector<ModelTargetReport> *reports = nullptr, bool show = true, QString *error = nullptr);
	// Selects what a delivery-target issue names: its faces or vertices when
	// it lists them, else its surface in surface mode; false for an issue
	// about the whole model.
	bool selectTargetIssue(const ModelTargetIssue &issue);
	// Prepare for target (core/model_target_prepare.h): prepares the fixes for
	// `format`'s delivery target on the worker and, after the review is
	// accepted (at once when `review` is false), adopts the prepared model as
	// one undo step.
	bool prepareForTarget(const QString &format, bool review = true, QString *error = nullptr);
	// Where `format`'s target comes from: chosen in this session, saved with
	// the project, suggested by its installation, or the format's default.
	[[nodiscard]] ModelDeliveryOrigin deliveryTargetOrigin(const QString &format) const;
	// Saves every format's chosen target and the strict setting with the open
	// project (saveDeliveryPreferences); false with the reason otherwise.
	bool saveDeliveryTargetsToProject(QString *error = nullptr);
	std::function<ModelEditorDeliveryContext()> deliveryContext;
	std::function<bool(const ModelDeliveryPreferences &, QString *)> saveDeliveryPreferences;
	// Detail Levels… (model_lod_dialog.cpp): reviews, stores and exports the
	// source's detail levels.
	bool reviewDetailLevels(QString *error = nullptr);
	// Skin Variants… (model_skin_variants_dialog.cpp): edits the named skins
	// the source owns as one undo step and writes their skin files.
	bool editSkinVariants(QString *error = nullptr);
	// Shows `mesh` in the viewport in place of the document, changing nothing;
	// nullptr shows the document again.
	void previewTransientMesh(const ModelMesh *mesh);
	// Refuse exports that leave data out (the Export page's setting, as
	// model build --strict): every export, stage and detail set reads it.
	[[nodiscard]] bool strictExport() const;
	std::function<ModelCollisionDestination()> collisionDestination;
	void refreshContext();
	std::function<ModelDesignContext()> context;
	std::function<bool(const QByteArray &, const QString &, bool, const LevelMapVec3 &, bool, QString *)> handoff;
	// Stages several files in one package operation, placing the first when
	// placing; Stage in Package uses it for an MD3 with stored detail levels.
	std::function<bool(const QVector<ModelStagedFile> &, bool, const LevelMapVec3 &, bool, QString *)> handoffFiles;
	// The active package's files at these paths (core/model_design.h
	// readStagedModelFiles), so staging an MD3 can review the detail files
	// already beside it; read on the GUI thread.
	std::function<QHash<QString, QByteArray>(const QStringList &)> readPackageFiles;
	std::function<void(const QString &)> showMaterial;
	// Runs after the user picks another controls profile, so other views
	// (the Models page preview) can follow it.
	std::function<void()> profileChanged;
	void setMaterialSource(ModelMaterialSource source);
	void reloadMaterials();
	[[nodiscard]] bool materialLoading() const;
	// High contrast and reduced motion for every view; reduced motion also
	// turns off Play / Pause and says why.
	void setAccessibility(bool highContrast, bool reducedMotion);
	void checkpointRecovery();
	[[nodiscard]] bool recoveryBusy() const;
	[[nodiscard]] QString recoveryPath() const;
	[[nodiscard]] bool operationBusy() const;
	void cancelOperation();
	// A busy document first cancels its worker and resolves unsaved changes.
	// The optional continuation runs once after an accepted deferred close.
	// Capture the caller with a lifetime guard when it is a QObject.
	bool requestClose(std::function<void()> afterDeferredClose = {});
	[[nodiscard]] ModelEditorTools *tools() const { return m_tools; }

	// Layout and controls profiles (model_editor_layout.cpp). The editor's
	// panels live on two tabbed sidebars arranged by the profile's family, as
	// the Levels page arranges its own; the views are one or four panes.
	void applyProfile(const QString &profileId, bool save = true);
	[[nodiscard]] QString profileId() const { return m_controls.profileId; }
	[[nodiscard]] const ModelEditorControls &controls() const { return m_controls; }
	// Applies controls that may carry the user's overrides for the profile.
	void applyControls(const ModelEditorControls &controls);
	// Import Controls… after its file dialog: saves the file's changes for
	// the profile it names and switches to it. The status line names what was
	// skipped, such as a key that always moves focus between panels.
	bool importControlsFile(const QString &path, QString *error = nullptr);
	[[nodiscard]] StudioSidebar *leadingSidebar() const { return m_leadingSidebar; }
	[[nodiscard]] StudioSidebar *trailingSidebar() const { return m_trailingSidebar; }
	[[nodiscard]] SidebarPage *sidebarPage(const QString &pageId) const { return m_sidebarPages.value(pageId); }
	// Shows the page on whichever sidebar holds it, opening a folded sidebar.
	bool showSidebarPage(const QString &pageId);
	void setViewLayout(ModelViewLayout layout);
	[[nodiscard]] ModelViewLayout viewLayout() const { return m_viewLayout; }
	void toggleFourViews();
	void toggleMaximisedView();
	void toggleSidebars();
	// Every view pane in reading order; the interactive one is the preview.
	[[nodiscard]] QVector<ModelViewport *> panes() const;
	[[nodiscard]] ModelPaneView paneView(int quadrant) const { return m_paneViews.value(quadrant); }
	// Makes the pane in `quadrant` the interactive one, as a click does.
	void activatePane(int quadrant, Qt::FocusReason reason = Qt::MouseFocusReason);
	// Next View and Previous View: the pane `step` places on in reading order
	// becomes active and takes keyboard focus, and its name is announced.
	// Outside four views it only says how to open them.
	bool stepActivePane(int step);
	[[nodiscard]] int activePane() const { return m_activePane; }
	// Copies what the preview shows into the other panes.
	void syncPanes();

  protected:
	void closeEvent(QCloseEvent *event) override;
	void reject() override;
	bool eventFilter(QObject *watched, QEvent *event) override;
	// A new text scale or theme refits the sidebars; showing or narrowing the
	// editor makes room for the views (model_editor_layout.cpp).
	void changeEvent(QEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

  private:
	ModelDocument m_document;
	ModelEditorTools *m_tools = nullptr;
	QByteArray m_presentedMeshRevision;
	ModelSelection m_presentedSelection, m_highlightedSelection;
	QByteArray m_highlightedRevision;
	QHash<int, qint64> m_previewImageKeys;
	bool m_refreshing = false;
	bool m_highContrast = false;
	QAction *m_play = nullptr;
	void refreshPlaybackActions();
	bool m_working = false, m_closeAfterWork = false;
	std::function<void()> m_cancelWork;
	std::function<void()> m_afterDeferredClose;
	bool m_decidingClose = false;
	// The document commands of the Model menu (open, save, export, stage),
	// disabled together while document work runs.
	QMenu *m_modelMenu = nullptr;
	QList<QAction *> m_documentActions;
	QWidget *m_editingControls = nullptr;
	ModelViewport *m_preview = nullptr;
	ModelComponentTable *m_components = nullptr;
	QTableView *m_table = nullptr;
	QComboBox *m_surface = nullptr;
	QComboBox *m_selectionMode = nullptr;
	QComboBox *m_frame = nullptr;
	QComboBox *m_frameScope = nullptr;
	QComboBox *m_renderMode = nullptr;
	QComboBox *m_viewPreset = nullptr;
	QCheckBox *m_moveGizmo = nullptr;
	QCheckBox *m_snapTranslation = nullptr;
	QCheckBox *m_xrayVertices = nullptr;
	QCheckBox *m_showTags = nullptr;
	QDoubleSpinBox *m_translationGrid = nullptr;
	QComboBox *m_transformTool = nullptr;
	QComboBox *m_transformSpace = nullptr;
	QDoubleSpinBox *m_axisRotation[3]{};
	QComboBox *m_pivotMode = nullptr;
	QDoubleSpinBox *m_rotationGrid = nullptr;
	QDoubleSpinBox *m_scaleGrid = nullptr;
	ModelUvView *m_uv = nullptr;
	QAction *m_uvPickIslands = nullptr;
	QAction *m_uvMove = nullptr;
	QComboBox *m_uvPivotMode = nullptr;
	QDoubleSpinBox *m_uvPivot[2]{};
	QCheckBox *m_uvSnap = nullptr;
	QDoubleSpinBox *m_uvGrid = nullptr;
	QSpinBox *m_uvAtlasResolution = nullptr, *m_uvAtlasHeight = nullptr, *m_uvAtlasPadding = nullptr;
	QCheckBox *m_uvAtlasSquare = nullptr;
	QPushButton *m_uvUnwrap = nullptr, *m_uvPack = nullptr, *m_uvPackAround = nullptr;
	QComboBox *m_uvObstacleScale = nullptr;
	QLabel *m_healthStatus = nullptr, *m_healthDetail = nullptr;
	QComboBox *m_healthFinding = nullptr;
	QPushButton *m_healthInspect = nullptr, *m_healthSelect = nullptr, *m_healthRepair = nullptr;
	ModelTopologyHealth m_health;
	QByteArray m_healthRevision;
	int m_healthSurface = -1;
	QComboBox *m_intersectionScope = nullptr, *m_intersectionFinding = nullptr;
	QLabel *m_intersectionStatus = nullptr, *m_intersectionDetail = nullptr;
	QPushButton *m_intersectionInspect = nullptr, *m_intersectionFirst = nullptr, *m_intersectionSecond = nullptr;
	ModelIntersectionList *m_intersectionList = nullptr;
	ModelIntersectionReport m_intersections;
	QByteArray m_intersectionRevision;
	int m_intersectionFrame = -2;
	// The status strip's lines read their text to screen readers too.
	StatusLineLabel *m_status = nullptr;
	StatusLineLabel *m_context = nullptr;
	QLineEdit *m_material = nullptr;
	QComboBox *m_materialSlot = nullptr;
	QPushButton *m_manageMaterialSlots = nullptr, *m_assignMaterial = nullptr, *m_showMaterial = nullptr, *m_skinVariants = nullptr;
	QHash<QString, int> m_previewMaterialSlots;
	void addMaterialSlotControls(QFormLayout *surface);
	void refreshMaterialSlots();
	QHash<int, int> materialPreviewSlots() const;
	QPushButton *m_skinBindingsFile = nullptr, *m_skinBindingsPackage = nullptr, *m_skinBindingsDetails = nullptr;
	QStringList m_lastSkinBindings;
	void addSkinBindingControls(QFormLayout *surface);
	void addSurfaceControls(QFormLayout *surface);
	void choosePackageSkinBindings();
	bool importSkinBindingsWork(std::function<bool(ModelSkinBindingInput *, QString *, const ModelWorkControl &)> read,
							   QString *error);
	QLineEdit *m_frameName = nullptr;
	QComboBox *m_animationClip = nullptr;
	QLineEdit *m_animationName = nullptr, *m_inbetweenPrefix = nullptr;
	QSpinBox *m_animationFirst = nullptr, *m_animationLast = nullptr;
	QDoubleSpinBox *m_animationRate = nullptr, *m_clipRate = nullptr;
	QSpinBox *m_inbetweenCount = nullptr, *m_copyPoseSource = nullptr;
	QLabel *m_animationSummary = nullptr;
	QPushButton *m_addAnimation = nullptr, *m_insertInbetweens = nullptr, *m_copyFramePose = nullptr;
	QList<QPushButton *> m_animationSelectionButtons;
	QByteArray m_animationRevision;
	int m_displayedAnimation = -2;
	QLineEdit *m_tagName = nullptr;
	QDoubleSpinBox *m_tagOrigin[3]{};
	LiveStatusLabel *m_tagSummary = nullptr;
	QComboBox *m_tagFrameScope = nullptr;
	QSpinBox *m_tagCopyFrame = nullptr;
	QList<QPushButton *> m_tagSelectionButtons;
	QString m_displayedTag;
	QComboBox *m_collisionBoxes = nullptr, *m_collisionFitScope = nullptr, *m_collisionTarget = nullptr;
	QComboBox *m_collisionPoseScope = nullptr;
	QLineEdit *m_collisionName = nullptr, *m_collisionMaterial = nullptr;
	QDoubleSpinBox *m_collisionCentre[3]{}, *m_collisionSize[3]{}, *m_collisionRotation[3]{}, *m_collisionOrigin[3]{};
	QCheckBox *m_showCollision = nullptr;
	LiveStatusLabel *m_collisionSummary = nullptr;
	QPushButton *m_collisionExport = nullptr, *m_collisionPlace = nullptr;
	QPushButton *m_collisionAnimate = nullptr, *m_collisionFreeze = nullptr;
	QList<QPushButton *> m_collisionSelectedButtons;
	QByteArray m_collisionRevision;
	QString m_displayedCollision;
	int m_displayedCollisionFrame = -1;
	QLineEdit *m_virtualPath = nullptr;
	QSpinBox *m_md2Width = nullptr;
	QSpinBox *m_md2Height = nullptr;
	QComboBox *m_mdlSkin = nullptr, *m_mdlMember = nullptr, *m_mdlGroup = nullptr, *m_mdlSync = nullptr;
	QLineEdit *m_mdlFlags = nullptr, *m_mdlEye = nullptr, *m_mdlSize = nullptr;
	QSpinBox *m_mdlFirst = nullptr, *m_mdlLast = nullptr;
	QDoubleSpinBox *m_mdlSkinDuration = nullptr, *m_mdlPoseDuration = nullptr;
	QLabel *m_mdlSummary = nullptr, *m_mdlTiming = nullptr;
	QComboBox *m_mdlPlaybackTiming = nullptr;
	QDoubleSpinBox *m_mdlPlaybackTime = nullptr, *m_mdlSyncPhase = nullptr;
	QLabel *m_mdlPlaybackStatus = nullptr;
	QByteArray m_mdlRevision;
	QImage m_mdlPreview;
	int m_displayedMdlSkin = -2, m_displayedMdlMember = -2, m_displayedMdlGroup = -2, m_displayedMdlPose = -2;
	QList<QPushButton *> m_mdlEnabledButtons, m_mdlSkinButtons;
	QPushButton *m_mdlPackageSkin = nullptr;
	QCheckBox *m_replace = nullptr;
	QCheckBox *m_strictExport = nullptr;
	QDoubleSpinBox *m_translation[3]{};
	QDoubleSpinBox *m_rotation[3]{};
	QDoubleSpinBox *m_scale[3]{};
	QDoubleSpinBox *m_pivot[3]{};
	QDoubleSpinBox *m_uvScale[2]{};
	QDoubleSpinBox *m_uvOffset[2]{};
	QDoubleSpinBox *m_uvRotation = nullptr;
	QDoubleSpinBox *m_weldDistance = nullptr;
	QSpinBox *m_bridgeTwist = nullptr;
	QCheckBox *m_preserveSeams = nullptr;
	QComboBox *m_projection = nullptr;
	QDoubleSpinBox *m_placement[3]{};
	QAction *m_undo = nullptr;
	QAction *m_redo = nullptr;
	QAction *m_stage = nullptr;
	QAction *m_place = nullptr;
	QImage m_checker;
	QHash<int, QImage> m_surfaceImages;
	ModelMaterialSource m_materialSource;
	LevelPreviewAssets m_materialAssets;
	ModelMaterialWorker *m_materialLoader = nullptr;
	QWidget *m_materialRow = nullptr;
	StatusLineLabel *m_materialStatus = nullptr;
	// When material loading started, so only a long load is announced.
	QElapsedTimer m_materialLoadClock;
	QProgressBar *m_materialProgress = nullptr;
	QToolButton *m_materialCancel = nullptr;
	bool m_materialRefreshPending = false;
	ModelRecoveryWriter *m_recovery = nullptr;
	QTimer *m_recoveryTimer = nullptr;
	QAction *m_recoveryEnabled = nullptr;
	StatusLineLabel *m_recoveryStatus = nullptr;
	QString m_recoveryId, m_recoveryDirectory, m_recoverySourcePath;
	QByteArray m_recoverySourceHash;
	// The recovery failure last announced, so a checkpoint that keeps failing
	// the same way every few seconds is read on its line, not announced again.
	QString m_recoveryProblem;
	quint64 m_recoveryRevision = 1, m_recoveryRequestedRevision = 0;
	bool m_recoveryActive = false, m_approvedClose = false;
	// Source size and undo history (model_editor_budget.cpp): the summary in
	// the status strip, the history limit setting in Model > Undo History
	// Limit…, and whether this
	// document has shown the notice that history discarded old steps.
	QLabel *m_documentBudget = nullptr;
	QSpinBox *m_historyLimit = nullptr;
	bool m_historyNoticeShown = false;
	void createDocumentBudget(QBoxLayout *row);
	void refreshDocumentBudget();
	// Appends the once-per-document notice to the status line when history
	// discarded steps since `discardedBefore`. A handler that replaces the
	// status afterwards adds historyTrimNotice() itself.
	void noteHistoryTrim(int discardedBefore);
	[[nodiscard]] QString historyTrimNotice() const;
	// The status after opening or receiving a model: the re-bake receipt of a
	// skeletal model fitted to the editable budget, and a source over the limit.
	void reportOpenedModel(const ModelImportReport &imported);
	// Layout (model_editor_layout.cpp).
	ModelEditorControls m_controls = modelEditorControlsForProfile(defaultModelEditorProfileId());
	StudioSidebar *m_leadingSidebar = nullptr, *m_trailingSidebar = nullptr;
	QHash<QString, SidebarPage *> m_sidebarPages;
	QToolButton *m_profileButton = nullptr;
	QComboBox *m_profileCombo = nullptr, *m_layoutCombo = nullptr;
	QComboBox *m_paneCombos[4]{};
	QWidget *m_paneGrid = nullptr;
	QGridLayout *m_paneLayout = nullptr;
	QFrame *m_paneFrames[4]{};
	QVector<ModelViewport *> m_mirrors;
	QVector<ModelPaneView> m_paneViews;
	// Which quadrant the preview sits in, and each quadrant's camera.
	int m_activePane = 3;
	ModelViewLayout m_viewLayout = ModelViewLayout::Single;
	bool m_maximised = false;
	bool m_sidebarsHidden = false;
	bool m_forwardingPress = false;
	QPointer<ModelViewport> m_pressForwardSource;
	QTimer *m_paneSync = nullptr;
	QTreeWidget *m_skeletonTree = nullptr;
	LiveStatusLabel *m_skeletonSummary = nullptr;
	// The skeletal working set: what it bakes, its share of the frame budget
	// and the dialog that changes it.
	QLabel *m_workingSetSummary = nullptr;
	QProgressBar *m_workingSetBudget = nullptr;
	QPushButton *m_workingSetButton = nullptr;
	QCheckBox *m_showJoints = nullptr, *m_xrayJoints = nullptr, *m_jointDescendants = nullptr;
	QPushButton *m_exportMd5 = nullptr, *m_exportIqm = nullptr;
	SidebarPage *createSidebarPage(const QString &pageId);
	QFormLayout *addSidebarForm(const QString &pageId, StudioSidebar *sidebar);
	void buildSidebarPages();
	void buildToolPages();
	void buildPaneGrid(QWidget *viewport);
	void arrangeSidebars();
	// Each sidebar page is as wide as what cannot wrap on it, and about
	// eighteen characters at least, whatever the text scale: buttons, check
	// boxes, notes and summaries wrap, and choices shrink, rather than
	// widening the sidebar.
	void fitSidebarPages();
	// When the views would keep less than 45% of the width, the sidebars give
	// back what they have beyond their minimum, then one folds to its tabs
	// (with a status message saying so): the properties sidebar, unless it is
	// the one just opened, which stays open while the other folds.
	void fitViewsToWidth();
	// Fits once the event loop has laid the editor out at its new size;
	// `opened` is a sidebar the user has just opened.
	void queueViewsFit(StudioSidebar *opened = nullptr);
	// Opening a sidebar where both no longer fit beside the views folds the
	// other one, so the editor never outgrows its page.
	void keepSidebarsInRoom(StudioSidebar *opened);
	// Folds a sidebar to its tabs, gives the width it frees to the views and
	// says so in the status strip and to the screen reader.
	void foldSidebarForViews(StudioSidebar *sidebar, const QString &message);
	bool m_viewsFitQueued = false;
	StudioSidebar *m_viewsFitOpened = nullptr;
	void saveSidebarState();
	void refreshProfileControls();
	void placePanes();
	void configurePane(ModelViewport *pane, ModelPaneView view, bool frame);
	void addSkeletonControls(QFormLayout *form);
	void refreshSkeleton();
	void selectJoint(const QString &name, bool toggle = false);
	void refreshJointSelection();
	void applyJointTransform(const ModelTransform &transform);
	void executeJointTransform();
	void showControlsReference();
	void customiseControls();
	void exportControls();
	void importControls();
	// What a document operation is for: an edit, an export or a save, whose
	// refusal or long run is announced, or a modal tool's live preview, which
	// is not (it runs at every pointer move or typed digit, and the tool
	// announces its refusal when the user confirms).
	enum class WorkKind
	{
		Operation,
		Preview
	};
	bool performWork(const QString &title, ModelDocumentJob job, QString *error, bool durableWrite = false,
					 WorkKind kind = WorkKind::Operation);
	void chooseRecovery();
	void retireRecovery(const QString &preservedId = {});
	void refresh(bool keepView = true);
	void refreshSelection();
	void refreshTransformAxes();
	void configureTransformAxes(ModelEdit &edit) const;
	void refreshUv();
	void editUvDensity();
	QWidget *createUvView();
	void addUvControls(QFormLayout *surface);
	void addTagControls(QFormLayout *animation);
	void addAnimationControls(QFormLayout *animation);
	void addMdlControls(QFormLayout *form);
	void chooseMdlPackageSkin();
	void refreshMdlControls();
	void executeMdl(ModelEditKind kind);
	void previewMdlSkin();
	void previewMdlAnimation(bool play);
	void refreshMdlPlayback();
	void refreshAnimationControls();
	void restoreAnimationPreview(int frame, bool keepView);
	void selectAnimationClip();
	void executeAnimation(ModelEditKind kind);
	void refreshTags();
	void addCollisionControls(QFormLayout *form);
	void refreshCollision();
	void executeCollision(ModelEditKind kind);
	void handoffCollision(bool place);
	ModelCollisionExport collisionRequest() const;
	void addHealthControls(QFormLayout *form);
	void refreshHealth();
	void selectHealthFinding();
	void repairHealthFinding();
	void addIntersectionControls(QFormLayout *form);
	void refreshIntersections();
	void selectIntersectionFace(bool second);
	void executeTag(ModelEditKind kind);
	void selectUvIslands();
	QString nativeUvStatus() const;
	void refreshMaterial();
	void applyMaterialImages();
	void showMaterialDetails();
	void selectFromTable();
	ModelEdit operation(ModelEditKind kind) const;
	void execute(ModelEditKind kind);
	bool maybeSave();
	bool save(bool saveAs = false);
	void open() override;
	void chooseRepairImport();
	void exportModel(const QString &format);
	void exportMd5Animation(bool allClips);
	// The MD5 animation chooser, starting at the clip `preferredClip` names,
	// else the clip of the current frame; fills the clip (unless all clips)
	// and frame rate. False when cancelled or the model has no clip, with the
	// status saying why.
	bool chooseMd5Animation(bool allClips, const QString &preferredClip, ModelExportOptions *options);
	QPushButton *m_exportReport = nullptr;
	ModelExportReport m_lastExport;
	QString m_lastExportSummary;
	QHash<QString, QString> m_deliveryTargets;
	// The delivery context last read, and the project it was applied for.
	ModelEditorDeliveryContext m_deliveryContext;
	QString m_deliveryContextKey;
	// Reads deliveryContext and, for a newly opened project, applies its
	// saved strict setting.
	void syncDeliveryContext();
	void addExportReportButton(QHBoxLayout *row);
	// A refused export passes LiveStatus::Problem, so it is announced as one.
	void presentExportReport(const QString &summary, const ModelExportReport &report, LiveStatus kind = LiveStatus::Result);
	// A refused export whose delivery target listed blockers.
	void presentBlockedExport(const QString &format, const ModelExportReport &report);
	// The status after a refused export: the target's blockers, a strict
	// refusal's omissions, or the error alone.
	void presentRefusedExport(const QString &format, const ModelExportReport &report, const QString &error);
	// Forgets the last report when another model is opened.
	void clearExportReport();
	void showExportReport();
	// Strict export setting on the Export page.
	void addStrictExportControl(QFormLayout *form);
	void stage(bool place);
};
} // namespace vibestudio
