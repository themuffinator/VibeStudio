#pragma once

#include "app/model_design_dialog.h"
#include "app/model_document_work.h"
#include "app/model_material_worker.h"
#include "core/model_document.h"
#include "core/model_collision.h"
#include "core/model_editor_controls.h"
#include "core/model_skin_source.h"
#include "core/model_skin_bindings.h"
#include "core/model_topology_health.h"
#include "core/model_intersections.h"

#include <QDialog>
#include <QHash>
#include <QPointer>

#include <functional>

class QAction;
class QCheckBox;
class QGridLayout;
class QFrame;
class QToolButton;
class QTreeWidget;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
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

class ModelEditorDialog final : public QDialog
{
	// The Blender-style interaction layer drives the editor's own controls.
	friend class ModelEditorTools;

  public:
	explicit ModelEditorDialog(QWidget *parent = nullptr);
	~ModelEditorDialog() override;
	[[nodiscard]] const ModelDocument &document() const;
	bool setMesh(const ModelMesh &mesh, QString *error = nullptr);
	bool openSource(const QString &path, QString *error = nullptr);
	bool repairSource(const QString &path, QString *error = nullptr);
	bool applyEdit(const ModelEdit &operation, QString *error = nullptr);
	bool importMdlSkin(const QString &path, ModelEditKind kind, QString *error = nullptr);
	bool importMdlSkinFromPackage(const ModelSkinSourceReference &reference, ModelEditKind kind, QString *error = nullptr);
	bool importMdlPalette(const QString &path, QString *error = nullptr);
	bool importSkinBindings(const QString &path, QString *error = nullptr);
	bool importSkinBindingsFromPackage(const ModelSkinSourceReference &reference, QString *error = nullptr);
	bool inspectTopology(QString *error = nullptr);
	bool inspectIntersections(QString *error = nullptr);
	bool exportCollision(const QString &path, const ModelCollisionExport &request, bool overwrite, QString *error = nullptr);
	bool placeCollision(const ModelCollisionExport &request, QString *error = nullptr);
	std::function<ModelCollisionDestination()> collisionDestination;
	void refreshContext();
	std::function<ModelDesignContext()> context;
	std::function<bool(const QByteArray &, const QString &, bool, const LevelMapVec3 &, bool, QString *)> handoff;
	std::function<void(const QString &)> showMaterial;
	// Runs after the user picks another controls profile, so other views
	// (the Models page preview) can follow it.
	std::function<void()> profileChanged;
	void setMaterialSource(ModelMaterialSource source);
	void reloadMaterials();
	[[nodiscard]] bool materialLoading() const;
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
	void activatePane(int quadrant);
	[[nodiscard]] int activePane() const { return m_activePane; }
	// Copies what the preview shows into the other panes.
	void syncPanes();

  protected:
	void closeEvent(QCloseEvent *event) override;
	void reject() override;
	bool eventFilter(QObject *watched, QEvent *event) override;

  private:
	ModelDocument m_document;
	ModelEditorTools *m_tools = nullptr;
	QByteArray m_presentedMeshRevision;
	ModelSelection m_presentedSelection, m_highlightedSelection;
	QByteArray m_highlightedRevision;
	QHash<int, qint64> m_previewImageKeys;
	bool m_refreshing = false;
	bool m_highContrast = false;
	bool m_working = false, m_closeAfterWork = false;
	std::function<void()> m_cancelWork;
	std::function<void()> m_afterDeferredClose;
	bool m_decidingClose = false;
	QToolBar *m_toolbar = nullptr;
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
	QLabel *m_status = nullptr;
	QLabel *m_context = nullptr;
	QLineEdit *m_material = nullptr;
	QComboBox *m_materialSlot = nullptr;
	QPushButton *m_manageMaterialSlots = nullptr, *m_assignMaterial = nullptr, *m_showMaterial = nullptr;
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
	QLabel *m_tagSummary = nullptr;
	QComboBox *m_tagFrameScope = nullptr;
	QSpinBox *m_tagCopyFrame = nullptr;
	QList<QPushButton *> m_tagSelectionButtons;
	QString m_displayedTag;
	QComboBox *m_collisionBoxes = nullptr, *m_collisionFitScope = nullptr, *m_collisionTarget = nullptr;
	QComboBox *m_collisionPoseScope = nullptr;
	QLineEdit *m_collisionName = nullptr, *m_collisionMaterial = nullptr;
	QDoubleSpinBox *m_collisionCentre[3]{}, *m_collisionSize[3]{}, *m_collisionRotation[3]{}, *m_collisionOrigin[3]{};
	QCheckBox *m_showCollision = nullptr;
	QLabel *m_collisionSummary = nullptr;
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
	QLabel *m_materialStatus = nullptr;
	QProgressBar *m_materialProgress = nullptr;
	QPushButton *m_materialCancel = nullptr;
	bool m_materialRefreshPending = false;
	ModelRecoveryWriter *m_recovery = nullptr;
	QTimer *m_recoveryTimer = nullptr;
	QCheckBox *m_recoveryEnabled = nullptr;
	QLabel *m_recoveryStatus = nullptr;
	QString m_recoveryId, m_recoveryDirectory, m_recoverySourcePath;
	QByteArray m_recoverySourceHash;
	quint64 m_recoveryRevision = 1, m_recoveryRequestedRevision = 0;
	bool m_recoveryActive = false, m_approvedClose = false;
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
	QLabel *m_skeletonSummary = nullptr;
	QPushButton *m_exportMd5 = nullptr, *m_exportIqm = nullptr;
	SidebarPage *createSidebarPage(const QString &pageId);
	QFormLayout *addSidebarForm(const QString &pageId, StudioSidebar *sidebar);
	void buildSidebarPages();
	void buildToolPages();
	void buildPaneGrid(QWidget *viewport);
	void arrangeSidebars();
	// Each sidebar page is at least as wide as its widest control, so the
	// sidebars never clip a form, whichever side the profile puts them on.
	void fitSidebarPages();
	void saveSidebarState();
	void refreshProfileControls();
	void placePanes();
	void configurePane(ModelViewport *pane, ModelPaneView view, bool frame);
	void addSkeletonControls(QFormLayout *form);
	void refreshSkeleton();
	void showControlsReference();
	void customiseControls();
	void exportControls();
	void importControls();
	bool performWork(const QString &title, ModelDocumentJob job, QString *error, bool durableWrite = false);
	void chooseRecovery();
	void retireRecovery(const QString &preservedId = {});
	void refresh(bool keepView = true);
	void refreshSelection();
	void refreshTransformAxes();
	void configureTransformAxes(ModelEdit &edit) const;
	void refreshUv();
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
	void stage(bool place);
};
} // namespace vibestudio
