#pragma once

#include "core/advanced_studio.h"
#include "core/ai_transport.h"
#include "core/asset_tools.h"
#include "core/bsp_inspect.h"
#include "core/build_pipeline.h"
#include "core/compiler_runner.h"
#include "core/deflate.h"
#include "core/document_watch.h"
#include "core/entity_definitions.h"
#include "core/idtech_image.h"
#include "core/level_editor_controls.h"
#include "core/level_navigation.h"
#include "core/level_map.h"
#include "core/level_document.h"
#include "core/level_materials.h"
#include "core/level_surface.h"
#include "core/level_surface_clipboard.h"
#include "core/level_material_paint.h"
#include "core/level_primitive.h"
#include "core/level_merge.h"
#include "core/level_patch_stitch.h"
#include "core/level_patch_cap.h"
#include "core/model_mesh.h"
#include "core/model_appearance.h"
#include "core/operation_state.h"
#include "core/package_archive.h"
#include "core/package_compare.h"
#include "core/package_staging.h"
#include "core/project_manifest.h"
#include "core/studio_settings.h"
#include "core/text_document.h"
#include "app/texture_thumbnail_cache.h"
#include "app/audio_playback.h"

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QPersistentModelIndex>
#include <QSet>
#include <QStyle>
#include <QVector>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>

class QAbstractButton;
class QAction;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QMenuBar;
class QPlainTextEdit;
class QProcess;
class QProgressBar;
class QProgressDialog;
class QPushButton;
class QSlider;
class QSplitter;
class QTabBar;
class QTextBrowser;
class QTextDocument;
class QStackedWidget;
class QTabWidget;
class QTemporaryDir;
class QTextEdit;
class QThread;
class QTimer;
class QToolBar;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace vibestudio {

struct LevelPlacementRequest;
struct LevelBuildWorkspace;

class PackageEntryView;
class PackageStagingView;
class PackageFolderView;
struct PackageStageFileRequest;
struct PackageStageFilesResult;

class ActivityTimelineChart;
class AudioEditorDialog;
class AudioSessionDialog;
class AudioRecoveryDialog;
class AudioRecoveryDiscovery;
class AudioBrowserWorker;
struct AudioBrowserResult;
class CodeFindBar;
class CommandPaletteDialog;
class CompositionChart;
class DetailDrawer;
class ElidedLabel;
class EmptyStateView;
class ImagePreviewView;
class TexturePreviewWorker;
class PackagePreviewWorker;
class LevelTextureAuditPanel;
class LevelGenerationDialog;
class TextureGenerationDialog;
class LevelAiEditDialog;
class SoundGenerationDialog;
struct PackagePreviewResult;
struct PackageCopyResult;
struct TexturePreviewSource;
struct TexturePreviewResult;
class LoadingPane;
class MapViewport;
class LevelScenePanel;
class LevelObjectList;
class ModeRail;
class ModelViewport;
class ModelDesignDialog;
class ModelEditorDialog;
class ModelAssemblyDialog;
class ModelPreviewWorker;
class LevelRecoveryWriter;
class LevelPreviewWorker;
class LevelSurfaceTools;
class LevelSurfaceWorker;
struct LevelSurfaceResult;
struct LevelSurfaceStrokeState;
class CodeRecoveryWriter;
class PackageRecoveryWriter;
class PackageRecoveryDialog;
class PackageCopyBudget;
class PackageCopySession;
class CodeIndexWorker;
class CodeFilesPanel;
class CodeLanguagePanel;
class CodeQuickInfoPanel;
class CodeSignaturePanel;
struct StudioDiagnosticMarker;
class TextureEditorDialog;
class NavigationTile;
class NoticeBar;
class PageHeader;
class PageTransition;
class PaletteSwatchView;
class PipelineChart;
class QuickOpenDialog;
class ProjectSearchPanel;
class StudioCommandRegistry;
class StudioCodeEditor;
class StudioSyntaxHighlighter;
class WaveformView;
struct CrashReportInfo;

// The studio work surfaces the mode rail switches between. Each value maps to
// exactly one page in the mode stack.
enum class StudioMode {
	Workspace,
	Levels,
	Models,
	Textures,
	Audio,
	Packages,
	Code,
	Shaders,
	Build,
	Settings,
};

// One row per work surface, shared by the mode rail and the View menu so their
// labels, hints, and order cannot drift apart.
struct StudioModeDescriptor {
	StudioMode mode = StudioMode::Workspace;
	QString label;
	QString hint;
	QString iconName; // A studio_icons.h glyph name.
};

QVector<StudioModeDescriptor> studioModeDescriptors();

class ApplicationShell final : public QMainWindow {
	Q_OBJECT

public:
	explicit ApplicationShell(QWidget* parent = nullptr, std::unique_ptr<AudioPlaybackBackend> audioBackend = {});
	~ApplicationShell() override;

	// Exercises every work surface once so an offscreen CI run touches the real
	// widgets, refresh paths, and paint code rather than only the CLI.
	void runSelfTest();

	// Opens a map, package, project folder, or text file the same way a drop
	// onto the window would, so command-line paths behave like drag and drop.
	void openPathFromCommandLine(const QString& path);
	bool saveWorkspaceTo(const QString& path, bool overwrite = false, QString* error = nullptr);
	bool openWorkspaceFrom(const QString& path, QString* error = nullptr);
	void openWorkspaceFile();
	void saveWorkspaceFile();
	// Export the currently displayed frame through the shared cancellable model
	// service. The selected mesh and package protection are immutable snapshots.
	bool exportSelectedModelTo(const QString& path, bool overwrite, QString* error = nullptr);
	// A draft has no disk identity until Save As adopts a reviewed destination.
	void createCodeDocument();
	bool saveCodeDocumentAs(const TextFileWriteTarget& target, QString* error = nullptr);
	bool recoverCodeDocument(const QString& path, QString* error = nullptr);
	void checkpointCodeDocuments();
	void checkpointPackageDocument();
	bool recoverPackageDocument(const QString& directory, const QString& id, const QByteArray& manifestSha256,
		const QString& destination, QString* error = nullptr);
	// Shared document operations also used by the New/Save/Recover controls.
	bool createLevelDocument(const LevelMapCreateRequest& request, QString* error = nullptr);
	bool saveLevelDocument(const QString& path, bool overwrite, QString* error = nullptr);
	bool recoverLevelDocument(const QString& path, QString* error = nullptr);
	const LevelMapDocument& levelDocument() const { return m_levelMapDocument; }
	void checkpointLevelDocument();
	bool applyLevelPatch(const LevelMapPatch& patch, int patchId = -1, QString* error = nullptr);
	bool applyLevelPatchStitch(int first, int second, const LevelPatchStitchRequest& request, QString* error = nullptr);
	bool applyLevelPatchCaps(int patchId, const LevelPatchCapRequest& request, QString* error = nullptr);
	bool applyLevelBrushGeometry(const LevelMapBrush& brush, int brushId, QString* error = nullptr);
	bool applyLevelRotation(const LevelMapRotationRequest& request, QString* error = nullptr);
	bool applyLevelSurfaceEdit(const LevelSurfaceEditPlan& plan, QString* error = nullptr);
	bool queueLevelSurfaceEdit(const LevelSurfaceRequest& request, QString* error = nullptr);
	bool copyLevelSurfaceSettings(LevelSurfaceFace face, QString* error = nullptr);
	bool pasteLevelSurfaceSettings(const QVector<LevelSurfaceFace>& faces, const LevelSurfacePasteOptions& options = {}, QString* error = nullptr);
	bool pasteLevelSurfaceTargets(const QVector<LevelMaterialTarget>& targets, const LevelSurfacePasteOptions& options = {}, QString* error = nullptr);
	const LevelSurfaceClipboard& levelSurfaceClipboard() const { return m_levelSurfaceClipboard; }
	void cancelLevelSurfaceEdits();
	bool levelSurfaceEditsPending() const;
	bool applyLevelMaterialPaint(const QVector<LevelMaterialTarget>& targets, const QString& material, QString* error = nullptr);
	bool sampleLevelMaterialTarget(const LevelMaterialTarget& target, QString* error = nullptr);
	void chooseLevelPaintMaterial(const QString& material);
	[[nodiscard]] QString levelPaintMaterial() const { return m_levelPaintMaterial; }
	bool applyLevelBrushPrimitive(const LevelBrushPrimitiveRequest& request, QString* error = nullptr);
	bool applyLevelBrushMerge(const LevelBrushMergePlan& plan, QString* error = nullptr);
	bool captureLevelViewState(LevelViewState* state, QString* error = nullptr);
	bool restoreLevelViewState(const LevelViewState& state, QString* error = nullptr);
	bool captureLevelBookmark(const QString& name, QString* error = nullptr);
	bool restoreLevelBookmark(const QString& id, QString* error = nullptr);
	bool replaceLevelBookmarks(const LevelViewBookmarks& bookmarks, QString* error = nullptr);
	bool reloadLevelBookmarks(QString* error = nullptr);
	[[nodiscard]] const LevelViewBookmarks& levelBookmarks() const { return m_levelBookmarks; }
	// Reopens what was open when the studio last closed, while the preference
	// is on: the package, the map, and the code editor's tabs. Files gone since
	// are skipped and counted in the status message.
	void restoreLastSession();
	// Starts the session once the window is up. After a session that ended
	// without shutting down, it offers that session and its crash report
	// instead of reopening it, since one of its files may be what crashed it;
	// otherwise it reopens the last session when `reopenLast` is set. From
	// then on what is open is recorded as it changes, so a crash loses none
	// of it.
	void beginSession(bool reopenLast);

	// Renders every work surface to <directory>/NN-<mode>.png. Documentation
	// screenshots and visual review use it to see the real widgets without a
	// person driving the window. Returns the files written.
	QStringList captureUiSnapshots(const QString& directory);

protected:
	void closeEvent(QCloseEvent* event) override;
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dragMoveEvent(QDragMoveEvent* event) override;
	void dropEvent(QDropEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void buildUi();
	void registerAssetWorkbenchCommands();
	// Generative AI: the Level, Texture, and Sound Generators, Edit Map with
	// AI, and the image and sound model settings (ai_generation_actions.cpp).
	void registerAiGenerationCommands();
	void showLevelGenerator();
	void showTextureGenerator();
	void showLevelAiEditor();
	void showSoundGenerator();
	[[nodiscard]] QString generationGame() const;
	[[nodiscard]] QStringList generationTextureNames(const QString& game) const;
	// `explanation`, when given, says what the request carries in place of
	// the generators' "the description and settings".
	bool confirmGenerationSend(const QString& connectorId, const QString& displayName, const QString& endpoint, const AiHttpRequest& request,
		const QString& explanation = QString());
	// A generator's request or build as an Activity row.
	QString beginGenerationTask(const QString& title, const QString& detail);
	void endGenerationTask(const QString& id, bool succeeded, bool cancelled, const QString& summary);
	QWidget* buildAiImageSettingsGroup();
	void refreshAiImageModelFields();
	void saveAiImageModelFields();
	QWidget* buildAiSoundSettingsGroup();
	void refreshAiSoundModelFields();
	void saveAiSoundModelFields();
	void refreshAssetWorkbenchCommands();
	QString selectedWorkbenchAssetPath(StudioMode mode) const;
	void revealWorkbenchAsset(StudioMode mode);
	void addAssetPackageMenu(PageHeader* header, StudioMode mode);
	void buildCommands();
	void buildMenuBar();
	void buildToolBar();
	void buildStatusBar();
	QWidget* buildWorkspacePage();
	QWidget* buildLevelsPage();
	QWidget* buildModelsPage();
	void showModelDesign();
	void showModelEditor(const ModelMesh* initial = nullptr);
	void showModelAssembly();
	QWidget* buildTexturesPage();
	void showTextureEditor(bool editSelected = false);
	QWidget* buildAudioPage();
	QWidget* buildPackagesPage();
	QWidget* buildCodePage();
	QWidget* buildShadersPage();
	QWidget* buildBuildPage();
	QWidget* buildSettingsPage();
	QWidget* buildSidePanel();
	void setMode(StudioMode mode);
	[[nodiscard]] StudioMode currentMode() const;
	// "<page> — <project> — VibeStudio", for the task bar and window switchers.
	void refreshWindowTitle();
	// Copies each page header's context line onto its Workspace tile.
	void refreshWorkspaceTiles();
	// Sizes the Workspace page's short lists (recent projects, installations)
	// to their rows.
	void fitWorkspaceLists();
	void refreshModeAvailability();
	// Updates every page header's context line and swaps each work surface
	// between its empty state and its workbench to match what is open.
	void refreshSurfaceStates();
	// Restores every work-surface splitter to its built-in proportions, closes
	// the panels, and expands the rail.
	void resetLayout();
	void showCommandPalette();
	// Go to File (Ctrl+P): recent files, the project's files, and the open
	// package's entries, each opened on the surface that shows it.
	void showQuickOpen();
	void openQuickOpenKey(const QString& key);
	void runCommand(const QString& commandId);
	// Page-specific keys (Del, F2, Ctrl+Z, Ctrl+S, Escape, ...) only fire while
	// keyboard focus is inside their page; see StudioCommandRegistry.
	void installShortcutScopes();
	// Ctrl+F: the current page's search or filter field, the code editor's find
	// bar on the Code page, and the Workspace search everywhere else.
	void focusCurrentPageSearch();
	// After a mode switch, keyboard focus moves into the new page so its keys
	// work straight away -- unless the keyboard is in the rail, moving from
	// page to page with the arrow keys; `leavingRail` takes it out of there.
	void focusPagePrimaryWidget(StudioMode mode, bool leavingRail = false);
	void copyProjectManifest();
	void validateProject();
	void showLocalizationReport();
	void reviewAiProposal();

	// Navigation between surfaces. Every list row that names a file, a
	// package entry, a map object, or a task can take the user to it.
	// Opens a package entry where it belongs: textures, models, and sounds on
	// their own surfaces; maps, shaders, and scripts from a temporary copy.
	void openPackageEntry(const QString& virtualPath, qsizetype entryIndex = -1);
	// Shows the entry selected in the Packages browser, inside its folder.
	void revealPackageEntry(const QString& virtualPath, qint64 sourceOrdinal = -1);
	bool showAssetEntry(StudioMode mode, const QString& virtualPath);
	// Finds the image a shader or map reference names; references usually
	// omit the extension. Empty when the open package has no such image.
	[[nodiscard]] QString resolveTextureReference(const QString& reference) const;
	void showTextureReference(const QString& reference);
	// Verified immutable batches stay owned for the studio session; each
	// request gets a fresh namespace and never replaces another open copy.
	PackageCopyResult preparePackageCopies(const QVector<qsizetype>& entryIndexes);
	QString extractPackageEntryCopy(const QString& virtualPath, qsizetype entryIndex = -1);
	bool selectLevelMapObjectBySelector(const QString& selector);
	void openWorkspaceRow(const QListWidgetItem* item);
	void activateActivityTask(const QString& taskId);
	[[nodiscard]] StudioMode modeForActivitySource(const QString& source) const;
	void showSettingsCategory(int index);
	void activateStatusChip(QAbstractButton* chip);
	void showAssetContextMenu(QListWidget* list, StudioMode mode, const QPoint& position);
	void showShaderGraphContextMenu(const QPoint& position);
	void activateShaderGraphItem(QTreeWidgetItem* item);
	void showBuildStageDetails(const QString& stageId);
	void refreshCommandEnablement();
	void refreshStatusChips();
	// Coalesces chip refreshes: many state changes in one event-loop pass cost
	// one refresh. Compiler discovery is cached for a short while on top.
	void scheduleStatusChipRefresh();
	void openDroppedPath(const QString& path);
	void loadShellState();
	void saveShellState();
	void refreshWorkspaceDashboard();
	void initializeCurrentProjectManifest();
	void refreshRecentProjects();
	void openProjectFolder();
	void addGameInstallationProfile();
	void detectGameInstallationProfiles();
	void importSelectedDetectedInstallation();
	void removeSelectedGameInstallation();
	void selectCurrentGameInstallation();
	void refreshGameInstallations();
	// The Allow test maps check box follows the installation selected in the
	// list and sets that installation's read-only flag.
	void refreshInstallTestMapsToggle();
	void setSelectedInstallationTestMaps(bool allowed);
	QString selectedGameInstallationId() const;
	int selectedDetectedInstallationIndex() const;
	void openPackageFile();
	void openPackageFolder();
	void closePackage();
	void loadPackagePath(const QString& path);
	// Opening another package replaces the staged plan against this one; this
	// asks first when there is one. True means go ahead.
	bool confirmStagedPackageChangesHandled();
	// A loose texture, model, or sound: its folder is browsed as a folder
	// package and the file selected on its surface.
	void showLooseAsset(const QString& path, AssetPreviewKind kind);
	void refreshPackageBrowser();
	void refreshWorkspacePackageContext();
	void refreshLevelTextureAudit();
	void applyLevelTextureAudit();
	void refreshWorkspaceContextPanels();
	void refreshProjectProblemsPanel();
	void refreshWorkspaceSearch();
	void scheduleWorkspaceSearch();
	void refreshChangedFilesPanel();
	void refreshProjectDependencyGraph();
	void refreshRecentActivityTimeline();
	void openLevelMapFile();
	void loadLevelMapPath(const QString& path);
	// Closes the open map, asking about unsaved edits first, and shows the
	// empty page with its recent maps.
	void closeLevelMap();
	void refreshLevelsRecentMaps();
	// Lists the maps of the WAD in the path field and shows the chooser only
	// when the path is a WAD.
	void refreshLevelMapNameChoices();
	void refreshLevelMapWorkbench();
	// The Objects filter: plain words narrow the list by name; a term that
	// tests a property (tag=3, class:light, light>200) queries every object's
	// properties, all terms together.
	void filterLevelMapObjects();
	void updateLevelObjectFilterState();
	// Selects every object the Objects filter keeps (Enter in the filter).
	void selectMatchingLevelMapObjects();
	// `objects` less those hidden in the view, which every Select command
	// leaves out: selecting one would show it again, and a Delete would then
	// reach it. `leftHidden` counts them.
	[[nodiscard]] QVector<LevelMapSelectionRef> visibleLevelMapObjects(const QVector<LevelMapSelectionRef>& objects, int* leftHidden) const;
	// Whether the open map names its textures with their "textures/" folder,
	// as most Quake III maps do; worked out once for a pass over many names.
	[[nodiscard]] bool mapTextureNamesPrefixed() const;
	[[nodiscard]] QString mapTextureNameForEntry(const QString& virtualPath, bool prefixed) const;
	// A list of package entries (Models, Sounds) filtered by its field: plain
	// words as every filter does, or a query over each entry's path, name,
	// ext, folder, type, and size, whether in the package or a loose file.
	void filterEntryList(QListWidget* list, QLineEdit* filter);
	// Says in the status bar when a query tests a key none of the list's items
	// has, which is usually a typing slip rather than a real empty result.
	// Says once for each query typed in `field` which of its keys nothing in
	// the list has, not again each time a refresh filters with it.
	void reportUnknownQueryKeys(QLineEdit* field, const StudioQuery& query, const QSet<QString>& known);
	// Remembers the property queries typed into `field` (when it is left or
	// Enter is pressed) under `filterId`, and offers them again: typing narrows
	// them, and Down in the field lists them all.
	void rememberFilterQueries(QLineEdit* field, const QString& filterId);
	// The Shaders stage graph filtered by its field: plain words as every tree
	// filter does, or a query over each shader's name, counts of stages,
	// textures, and missing textures, directives (cull=none, surfaceparm), and
	// stages' blend, map, and rgbgen, keeping a matching shader's whole tree.
	void filterShaderGraph();
	// The Code page's file tree filtered by its field: plain words as every
	// tree filter does, or a query over each file's name, ext, folder (from the
	// project root), size, and language, keeping the folders of the matches.
	void filterCodeTree();
	QHash<QString, StudioQueryProperties> m_shaderQueryProperties;
	// The file browser owns its asynchronous catalog and metadata filters.
	CodeFilesPanel* m_codeFilesPanel = nullptr;
	QString m_codeFilesActivityId;
	// The query finding the objects that share an inspector row's value, such
	// as light=300 or tag=3; empty for a row no query can test.
	[[nodiscard]] QString inspectorRowQuery(const QTreeWidgetItem* item) const;
	// Shows the Objects tab filtered by `query`, focused so Enter selects them.
	void findLevelMapObjectsLike(const QString& query);
	void refreshLevelMapSelection(const LevelMapInspectionSummary* summary = nullptr);
	// Level editor profiles: which views the Levels page shows, how they
	// answer the mouse and keys, and the 3D camera's link to the 2D view.
	[[nodiscard]] bool levelMap3DShowing() const;
	[[nodiscard]] bool levelMap2DShowing() const;
	void applyLevelEditorProfile(bool profileChanged, bool preserveWorkspace = false);
	void refreshLevelViewportShortcuts();
	[[nodiscard]] QString levelControlsAccessibleText(const QString& section, const QString& view) const;
	void applyLevelViewLayout(LevelViewLayout layout);
	MapViewport* createLevelPlanViewport(int index);
	void activateLevelPlanViewport(MapViewport* view);
	void synchronizeLevelPlanViews(bool frame = false);
	void synchronizeLevelViewNavigation(MapViewport* source, bool fit = false);
	void frameLevelPlanViews(bool selection = true);
	void followLevelCamera(bool force = false);
	void chooseLevelViewLinks(const LevelViewLinks& links);
	void refreshLevelViewLinkActions();
	void chooseLevelViewLayout(const QString& preference);
	void saveLevelViewSplitters();
	void toggleLevelViewMaximized();
	void restoreLevelViewWorkspace(bool focus = false);
	void equalizeLevelViews();
	void refreshLevelViewWorkspaceActions();
	void refreshLevelBookmarkContext();
	void copyLevelBookmarksAfterSave();
	void populateLevelBookmarkMenu(QMenu* menu);
	void showLevelBookmarks();
	void captureLevelBookmarkFromUi();
	void stepLevelBookmark(int direction);
	void applyPendingLevelBookmarkCamera();
	void cycleLevelMapView();
	void stepLevelMapProjection();
	void showLevelMapProjection(int index);
	void setLevelMapGridUnits(int units);
	void frameLevelMapSelection();
	void isolateLevelMapSelection();
	void drawLevelMapBrushFromViewport(const LevelMapVec3& mins, const LevelMapVec3& maxs);
	void levelBrushPrimitiveAdded(const LevelBrushPrimitiveRequest& request);
	[[nodiscard]] QString levelBrushMaterial() const;
	void pickLevelMap3D(int triangle, int pick);
	void showLevelMap3DContextMenu(const QPoint& position);
	void refreshLevelCameraMarker();
	void aimLevelCameraAt(const QPointF& planePoint, bool place);
	void showLevelControlsReference();
	void showLevelGesturePreferences();
	void refreshLevelMapViewport();
	QWidget* buildLevelMaterialTools();
	void refreshLevelMaterialTools(const QVector<LevelMapTextureUse>* textureUses = nullptr);
	void beginLevelMaterialStroke();
	void touchLevelMaterialSurface(int triangle);
	void finishLevelMaterialStroke(bool commit);
	void beginLevelSurfaceStroke();
	void touchLevelSurfaceStroke(int triangle, CameraMaterialGesture action);
	void finishLevelSurfaceStroke(bool commit);
	void cancelLevelSurfaceStroke();
	void startLevelSurfaceStrokeStep();
	void completeLevelSurfaceStroke(LevelSurfaceResult result);
	void setLevelMaterialTool(int tool);
	void showLevelMaterialPaintMessage(const QString& message);
	void rebuildLevelMaterialTargets(const QVector<LevelMaterialTarget>& targets);
	void rememberLevelMaterial(const QString& material);
	void reloadEntityDefinitions();
	void chooseEntityDefinitionPath();
	void refreshEntityInspector(const QStringList* textureNames = nullptr);
	// The inspector for several selected entities: every key any of them
	// sets, a value that differs across them marked as such; an edit sets it
	// on all of them. Called by refreshEntityInspector() on a cleared grid.
	void refreshMultiEntityInspector(const QVector<int>& entityIds, const QString& previousKey);
	// The Quake-family entities selected in Levels, the primary last.
	[[nodiscard]] QVector<int> selectedLevelMapEntityIds() const;
	// What the inspector is, for screen readers, while one object is shown.
	[[nodiscard]] QString entityInspectorDescription() const;
	// Applies an inline edit from the entity property grid: a key's value or
	// a spawnflag check box.
	void applyEntityInspectorEdit(QTreeWidgetItem* item, int column);
	QString selectedLevelMapObjectSelector() const;
	// True when the selection is something the entity inspector edits: an
	// entity, or a Doom thing through the entity it is mirrored into.
	[[nodiscard]] bool levelMapSelectionHasKeys() const;
	void selectLevelMapObjectFromViewport(int selectionKind, int objectId);
	void syncLevelMapSelectionFromViewport(const QVector<LevelMapSelectionRef>& selection);
	void moveLevelMapSelectionFromViewport(double dx, double dy, double dz);
	// Dims and marks an objects-list row whose object the viewport hides, or
	// clears the mark once it is shown again.
	void refreshLevelMapObjectHiddenMarks();
	// Where a new object goes along the axis the view hides: level with the
	// primary entity or thing, else the middle of the map.
	[[nodiscard]] double levelMapHiddenAxisValue() const;
	// Copies the text selected in a focused read-only text view, which does not
	// claim Ctrl+C for itself. False when there is none.
	bool copyFocusedTextSelection();
	// Right-click menu for the map, from the viewport or the objects list:
	// framing, adding and deleting, the inspector's edits, the selection's
	// selector, and the leak trail. Opens at `globalPosition`; from the
	// viewport a new entity goes where the menu was opened.
	void showLevelMapContextMenu(const QPoint& globalPosition, bool fromViewport);
	void editSelectedLevelMapProperty();
	// Values a key of an entity of `className` is likely to take, for the
	// inspector's completer: another entity's targetname for a target-style
	// key, a name something targets but no entity carries for targetname, or
	// the choices the class's definition lists. Empty when there is no hint.
	[[nodiscard]] QStringList entityValueSuggestions(const QString& className, const QString& key) const;
	void moveSelectedLevelMapObject();
	// Asks for a class and adds a point entity at `viewPoint` in the viewport,
	// or at the centre of the view when it is null.
	void addLevelMapEntityFromUi(const QPointF& viewPoint = QPointF(-1.0, -1.0));
	// The Doom and Hexen counterpart: asks for a DoomEd type number.
	void addLevelMapThingFromUi(const QPointF& viewPoint = QPointF(-1.0, -1.0));
	// Asks for a size and a texture and adds a box brush centred on
	// `viewPoint`, or on the centre of the view.
	void addLevelMapBrushFromUi(const QPointF& viewPoint = QPointF(-1.0, -1.0));
	void showLevelPrefab(bool capture, bool stage = false, const QString& path = {}, bool packageEntry = false);
	void editLevelMapPatchFromUi(bool creating);
	void stitchLevelMapPatchesFromUi();
	void capLevelMapPatchFromUi();
	void editLevelMapBrushComponentsFromUi();
	void deleteLevelMapSelectionFromUi();
	// Selects every entity of the primary entity's class, or every thing of
	// the primary thing's type.
	void selectSimilarLevelMapObjects();
	// What "similar" means for the current primary selection, for menu text;
	// empty when nothing selected has a class or type.
	[[nodiscard]] QString similarLevelMapObjectsLabel() const;
	// Selects every object using `texture`; with none given, asks which of the
	// map's textures.
	void selectLevelMapObjectsByTexture(const QString& texture = QString());
	// The texture the primary selection shows: a brush's first face, a
	// patch's shader, or a sector's floor. Empty otherwise.
	[[nodiscard]] QString selectedLevelMapTexture() const;
	// Right-click menu on a build problem: show it, copy it, copy them all.
	void showBuildProblemMenu(const QPoint& globalPosition, QListWidgetItem* item);
	// Copies the selection one grid step right and down in the view.
	void duplicateLevelMapSelectionFromUi();
	void placeLevelMapObjectsFromUi(bool paste);
	std::function<bool(const LevelMapDocument&, QString*)> levelPlacementCommitter();
	bool runLevelPlacementFromUi(const LevelPlacementRequest& request, QString* error);
	// Snaps each selected object to the viewport's grid.
	void snapLevelMapSelectionFromUi();
	// Turns the selection a quarter turn as the view shows it: about z in the
	// top view, and about the axis each side view looks along.
	void rotateLevelMapSelectionFromUi(bool counterclockwise);
	void rotateLevelMapSelectionPreciselyFromUi();
	void editLevelUdmfPropertiesFromUi();
	void editLevelMapSurfacesFromUi();
	LevelSurfaceFace inspectedLevelMapSurface() const;
	QWidget* buildLevelSurfaceTools();
	void registerLevelSurfaceCommands();
	void refreshLevelSurfaceTools();
	void startLevelSurfaceBatch();
	std::function<bool()> levelSurfaceContextGuard() const;
	// Mirrors the selection along the view's horizontal or vertical axis.
	void flipLevelMapSelectionFromUi(bool horizontal);
	// Asks for the selection's new size along each axis, kept against its
	// lower corner, its centre, or its upper corner.
	void resizeLevelMapSelectionFromUi();
	// A drag on one of the viewport's resize handles.
	void resizeLevelMapSelectionFromViewport(const LevelMapVec3& mins, const LevelMapVec3& maxs);
	// True when the selection has a size to change; on a Doom map only
	// things resize.
	[[nodiscard]] bool levelMapSelectionResizable() const;
	// Fits the selection to `mins`-`maxs` as one undo step and reports it.
	bool applyLevelMapResize(const LevelMapVec3& mins, const LevelMapVec3& maxs);
	// Select All, or with `invert` the objects Select All would pick less the
	// ones selected now: every entity but worldspawn, brush, and patch, or
	// every Doom thing, leaving hidden objects out.
	void selectAllLevelMapObjects(bool invert = false);
	void selectNoLevelMapObjects();
	// Puts a texture on every face of the selected brushes and on the selected
	// patches; with none given, asks for one, offering the texture current on
	// the Textures surface first.
	void applyLevelMapTextureFromUi(const QString& texture = QString());
	// " 2 brushes left as they were; the first because: …" after a brush
	// operation that left some of the selection alone, or nothing.
	[[nodiscard]] QString levelMapSkippedNote(const QStringList& skipped) const;
	// Splits or flips the selected Doom linedefs.
	void editLevelMapLinedefsFromUi(bool split);
	// True when the selection holds linedefs of a Doom map that can be written.
	[[nodiscard]] bool levelMapSelectionHasLinedefs() const;
	// True when a Doom or Hexen map is open that can be written back.
	[[nodiscard]] bool levelMapDoomEditable() const;
	// Draw Sector on or off in the viewport, and the sector a closed shape asks
	// for; Add Sector… takes the corners typed, for the keyboard.
	void setLevelMapDrawMode(bool enabled);
	bool applyLevelMapSectorDrawing(const QVector<LevelMapVec3>& corners);
	void drawLevelMapSectorFromViewport(const QVector<QPointF>& corners);
	void addLevelMapSectorFromUi();
	// Joins the selected Doom vertices into the last one picked.
	void mergeLevelMapVerticesFromUi();
	// Connect Entities: the selection's entities target the primary.
	void connectLevelMapEntitiesFromUi();
	// Make Door: asks for the door, track, and ceiling textures, keeping the
	// last answers for the next door.
	void makeLevelMapDoorsFromUi();
	// Raise, lower, and grade the selected Doom sectors; a raise or lower steps
	// by 8 (16 for light), or by 1 while Shift is held.
	void shiftLevelMapSectorsFromUi(LevelMapSectorField field, int direction);
	void gradientLevelMapSectorsFromUi(LevelMapSectorField field);
	// Selects what the selection's entities target, or what targets them.
	void selectLinkedLevelMapEntities(bool targets);
	// Places an entity class or a Doom thing type at a point in the view, or
	// in its middle for a negative point, snapped to the grid; the palette,
	// its drops, and the Add…Here dialogs all end here.
	void placeLevelMapEntity(const QString& className, const QPointF& viewPoint);
	void placeLevelMapThing(int type, const QPointF& viewPoint);
	// A palette entry, "entity:<class>" or "thing:<type>", placed at a point.
	void placeFromLevelMapPalette(const QString& payload, const QPointF& viewPoint);
	// Lists what the open map can place: its definitions' point classes, or
	// Doom thing types by category.
	void refreshLevelMapPalette();
	// Joins the selected Doom sectors into the last one picked; `merge` takes
	// the lines between them away too.
	void joinLevelMapSectorsFromUi(bool merge);
	// How many sectors the selection holds, on a Doom map that can be edited.
	[[nodiscard]] int levelMapSelectedSectorCount() const;
	// Fills the inspector with the fields of a selected Doom sector, linedef
	// (its flags and both sides), or vertex, editable in place. False when
	// the primary selection is none of those.
	bool refreshDoomInspector();
	// The same for a selected brush: each face, named by the way it faces, with
	// its texture, shift, rotation, and scale editable in place.
	bool refreshBrushInspector(const QStringList* textureNames = nullptr);
	// A Doom row edited in the inspector: `field` names the record and field,
	// as refreshDoomInspector() tagged the row.
	void applyDoomInspectorEdit(QTreeWidgetItem* item, int column, const QString& field);
	// Carves the selected brushes out of every brush they overlap.
	void carveLevelMapSelectionFromUi();
	void mergeLevelMapBrushesFromUi();
	// Asks for a wall thickness, a grid step to begin with, and hollows the
	// selected brushes.
	void hollowLevelMapSelectionFromUi();
	// Clip tool on or off in the viewport, and the cut it asks for.
	void setLevelMapClipMode(bool enabled);
	void clipLevelMapSelectionFromViewport(const LevelMapVec3& a, const LevelMapVec3& b, const LevelMapVec3& c, int keep);
	// Clip Selection…: a plane square to an axis, at a position, and what
	// stays, for the keyboard.
	void clipLevelMapSelectionFromUi();
	// True when the selection holds brushes or a brush entity to clip.
	[[nodiscard]] bool levelMapSelectionClippable() const;
	bool applyLevelMapClip(const LevelMapVec3& a, const LevelMapVec3& b, const LevelMapVec3& c, LevelMapClipKeep keep);
	// The Levels view's grid, snap, projection, and Show toggles, kept from one
	// session to the next the way Radiant and TrenchBroom keep them.
	void restoreLevelMapViewState();
	void saveLevelMapViewState();
	// The Levels 3D preview: shown in place of the 2D view, rebuilt from the
	// document after each edit while it is showing.
	void setLevelMap3D(bool enabled);
	void connectLevelCameraResize();
	void refreshLevelCameraResize();
	QWidget* buildLevelBrushTools();
	void connectLevelCameraBrush();
	void refreshLevelCameraBrush();
	void useLevelBrushWorkZone();
	void refreshLevelMap3D();
	QWidget* buildLevelPreviewStatus();
	void showLevelMaterialDetails();
	// Marks the selected objects' triangles in the 3D preview.
	void refreshLevelMap3DHighlight();
	// Steps the Levels grid one size smaller or larger: 1 to 256 units.
	void stepLevelMapGrid(int direction);
	// Hide the selection from the view, or bring every hidden object back.
	void hideLevelMapSelection();
	void showAllLevelMapObjects();
	// Asks which texture to replace with which, across the map or only the
	// selection, showing how many uses will change.
	// Every command with its current keys and where they work, filterable.
	void showKeyboardShortcuts();
	// `replacement` pre-fills the texture to use instead, as when a texture is
	// chosen on the Textures surface.
	void showReplaceTextureDialog(const QString& replacement = QString());
	// The name the open map would use for a package texture entry: without
	// its extension, with or without the "textures/" folder as the map's own
	// names have it, and a bare lump name for a Doom map.
	[[nodiscard]] QString mapTextureNameForEntry(const QString& virtualPath) const;
	// Map objects on the clipboard as .map text, interchangeable with other
	// Quake-family editors.
	bool copyLevelMapSelectionToClipboard();
	void cutLevelMapSelection();
	void pasteLevelMapFromClipboard();
	// Removes the key on the inspector's current row from the selected entity.
	void removeEntityInspectorKey();
	void undoLevelMapEditFromUi();
	void redoLevelMapEditFromUi();
	// The History tab: every edit, oldest first, with the undone ones after
	// the current step. Activating a row undoes or redoes until `depth` edits
	// are applied.
	void refreshLevelMapHistory();
	void jumpToLevelMapHistory(int depth);
	bool saveLevelMapAsFromUi();
	bool saveLevelMapFromUi();
	void newLevelMapFromUi();
	void recoverLevelMapFromUi();
	void adoptLevelMapDocument(LevelMapDocument document);
	void retireLevelRecovery();
	[[nodiscard]] bool levelMapHasUnsavedEdits() const;
	// Asks what to do with unsaved map edits before something replaces or
	// closes the map. True means go ahead: the edits were saved or
	// knowingly discarded, or there were none. `proceedLabel` names the button
	// that goes ahead without saving when that keeps the edits open instead of
	// discarding them (a compile of the saved file).
	bool confirmLevelMapEditsHandled(const QString& question, const QString& proceedLabel = QString());
	void exportLevelMapImage();
	void planLevelMapCompile();
	void copyLevelMapCliEquivalent();
	void refreshAdvancedStudioSurface();
	void refreshShaderDetailSections(const QString& shaderName = QString(), int stageIndex = -1);
	void inspectAdvancedShaderScript();
	void createAdvancedSpritePlan();
	void indexAdvancedCodeWorkspace();
	void createAdvancedAiProposal();
	void discoverAdvancedExtensions();
	void refreshTextureBrowser();
	// Puts the active palette in the swatch grid and says where it came from.
	IdTechPaletteResolution refreshTexturePaletteView();
	IdTechPaletteResolution refreshTexturePaletteView(const IdTechPaletteResolution& resolution);
	void filterTextureEntries();
	// Tiles show decoded thumbnails like idStudio's asset browser; List shows
	// paths and sizes. Thumbnails decode a few at a time in the background.
	void applyTextureViewMode();
	void fitTextureGrid();
	void queueTextureThumbnails();
	void scheduleTextureThumbnails();
	void setTextureThumbnailIcons(const QString& path, const QIcon& icon);
	void generateTextureThumbnailBatch();
	void acceptTextureThumbnail(const TexturePreviewResult& result);
	void acceptTexturePreview(const TexturePreviewResult& result);
	[[nodiscard]] TexturePreviewSource texturePreviewSource() const;
	void updateTexturePreviewState();
	void showSelectedTexture();
	void exportSelectedTexture();
	void refreshModelBrowser();
	void refreshModelMaterials();
	void showSelectedModel();
	void exportSelectedModel();
	void refreshModelPlaybackControls();
	QWidget* createModelAppearancePanel();
	void refreshModelAppearanceControls();
	void refreshModelAppearanceImage();
	void refreshAudioBrowser();
	void showSelectedAudioEntry();
	void exportSelectedAudio();
	void showAudioEditor(bool selectedEntry, const QString& localPath = QString());
	AudioEditorDialog* ensureAudioEditor();
	AudioSessionDialog* ensureAudioSession();
	void showAudioSession(const QString& path = {});
	QWidget* buildAudioRecoveryPreferences();
	void recoverAudioFromUi();
	void discoverAudioRecoveryAtStartup();
	void offerAudioRecoveryNotice();
	void initializeAudioBrowser(std::unique_ptr<AudioPlaybackBackend> backend);
	void acceptAudioPreview(const AudioBrowserResult& result);
	[[nodiscard]] bool audioPlaybackAvailable() const;
	void toggleAudioPlayback();
	void stopAudioPlayback();
	void seekAudioPlayback(qint64 positionMs);
	void setAudioLoop(bool enabled);
	void refreshAudioTransport();
	void unloadAudioPlayback();
	void refreshCodeWorkspaceTree(bool force = false);
	// Opening the file already in the editor keeps it as it is, unsaved edits
	// included, unless `forceReload` asks for the disk copy. False when the
	// file could not be read or the user cancelled the unsaved-changes prompt.
	bool openCodeFile(const QString& path, bool forceReload = false);
	bool openCodeFileAt(const QString& path, int line, int column = 0);
	void moveCodeCaretTo(int line, int column = 0);
	void goToCodeLine();
	// Lists the open file's symbols in a type-to-find picker; choosing one
	// moves the caret to its line.
	void showCodeSymbolPicker();
	// Opens where the name at the caret is defined: the open file's own
	// definitions first, then the project's, a picker when there are several.
	void goToCodeDefinition();
	// Go Back and Go Forward (Alt+Left, Alt+Right, the mouse's side buttons)
	// step through the places the user has been: each page left, and on Code
	// each jump to another file or line. rememberPlace() records where the
	// user is just before such a move.
	void navigateBack();
	void navigateForward();
	void rememberPlace();
	// Every use of the name at the caret across the project, as whole words
	// and matching case, under Search Results.
	void findCodeReferences();
	[[nodiscard]] QString codeIdentifierAtCaret() const;
	// The open file's functions, shaders, or classes, rebuilt a moment after
	// each edit; followCodeOutline() makes the one holding the caret current.
	void refreshCodeOutline();
	void followCodeOutline();
	// Where the open file is: its folders from the project root, each a menu
	// of what that folder holds; the file, a menu of the files beside it; and
	// the function, shader, or class holding the caret, which opens Go to
	// Symbol.
	void refreshCodeBreadcrumb();
	// Moves the caret to the open file's next problem after the caret's line
	// (or the one before it, direction < 0), wrapping at either end, and says
	// which it is and what it reports.
	void goToCodeProblem(int direction);
	// Shows the last build's next problem after the one shown last (or the
	// one before it, direction < 0), wrapping, from any page: a map object,
	// a file line, a leak trail, or the stage that printed it.
	void goToBuildProblem(int direction);
	// The problem F8 last went to, and where that left the caret, so problems
	// sharing a line are stepped through one by one.
	int m_lastCodeProblem = -1;
	int m_lastCodeProblemPosition = -1;
	void showCodeFolderMenu(QToolButton* crumb, const QString& folder);
	// Requests the current project's index without blocking the editor.
	QString ensureDefinitionIndex();
	QString codeIndexRoot() const;
	QWidget* buildCodeIndexPanel();
	void refreshCodeIndexResults();
	void startCodeIndex();
	void invalidateDefinitionIndex();
	void cancelCodeIndex();
	// The names completion offers: the language's keywords, the open file's
	// names, and the project's symbols.
	[[nodiscard]] QStringList codeCompletionNames(const QString& prefix);
	// One zoom step up (direction > 0) or down, or back to 100% (0).
	void zoomCodeEditor(int direction);
	void refreshCodeZoomReadout();
	// Asks for new keys for a command, warning of the command they would be
	// taken from; true when the user's keys changed.
	bool captureCommandKeys(const QString& commandId, QWidget* parent);
	// Installs the user's own keys, keeps them in settings, and refreshes
	// what names keys (the mode rail's hints).
	void applyUserShortcuts(const QHash<QString, QStringList>& shortcuts);
	void refreshModeRailHints();
	// The Levels Textures tab: the textures applied lately, then the map's own
	// by how much they are used, each with its thumbnail from the package.
	void refreshLevelMapTextures(const QVector<LevelMapTextureUse>* textureUses = nullptr);
	void showLevelMapTextureMenu(const QPoint& position);
	void selectLevelMapObjectsUsingTexture(const QString& texture, const QString& materialKey = {});
	// Sizes the Settings category list to its longest name at the current
	// text scale, so none is cut off or scrolls sideways.
	void fitSettingsCategories();
	// Keeps the Settings categories whose settings mention the search text,
	// and brings the first match on the shown category into view.
	void filterSettings();
	void pollWatchedDocuments();
	void handleExternalDocumentChange(const DocumentChangeEvent& event);
	void registerWatchedDocument(const QString& path, DocumentWatchRole role, const QString& documentId = QString(), bool adoptContents = true);
	// Whether the map, the package, the project manifest, or (unless
	// `countCodeTabs` is false) a code tab still has `normalizedPath` open.
	// One file can be open as more than one of them, and the watcher keeps
	// one role per path, so a watch goes only when nothing holds the path.
	[[nodiscard]] bool watchedPathHeld(const QString& normalizedPath, bool countCodeTabs = true) const;
	// Where Automatic finds palettes moved (an installation chosen, the
	// project's palette changed): the texture preview, its palette view, and
	// the thumbnails decoded with the old palette follow.
	void refreshTexturePaletteSources();
	void offerCrashRecovery(const CrashReportInfo& report, const StudioSession& last);
	// The crash reports kept on this machine, newest first, with the full
	// text of the chosen one: every kept report, or the ones given.
	void showCrashReports();
	void showCrashReports(const QVector<CrashReportInfo>& reports);
	void reopenSession(const StudioSession& session, const std::function<bool()>& packageReviewed = {}, const std::function<bool()>& mapReviewed = {});
	// What is open now, as the next start would reopen it: temporary copies
	// of package entries die with this run, so they are left out.
	[[nodiscard]] StudioSession currentSession() const;
	void recordSessionIfChanged();
	void openSelectedCodeFile();
	bool saveCodeFile();
	bool saveCodeFileAsFromUi();
	void initializeCodeRecovery();
	void retireCodeRecovery(int index);
	void recoverCodeDocumentFromUi();
	bool addCodeTab(const QString& path, const QString& draftName = {});
	[[nodiscard]] bool hasCodeDocument() const;
	[[nodiscard]] QString codeTabName(int index) const;
	[[nodiscard]] int codeTabIndexOf(const QTextDocument* document) const;
	void refreshCodeFileStatus(const QString& state = {});
	void refreshCodeDiagnostics();
	// Open files, one tab each (see CodeTab). Activating a tab swaps its
	// document into the editor and makes it the file m_codeFilePath names.
	[[nodiscard]] int codeTabIndexOf(const QString& path) const;
	void activateCodeTab(int index);
	void followCodeFileInTree(const QString& path);
	void storeCodeTabView(int index);
	bool closeCodeTab(int index);
	void closeOtherCodeTabs(int index);
	void cycleCodeTab(int step);
	void refreshCodeTabLabels();
	void codeDocumentModificationChanged(QTextDocument* document, bool modified);
	bool reloadCodeTab(int index);
	// Asks about every open file with unsaved changes; false when the user
	// cancels, or a save fails.
	bool confirmCodeTabsHandled(const QString& question);
	void showCodeTabMenu(const QPoint& position);
	void runCodeFindReplace();
	void configureCodeProjectSearch();
	QWidget* buildCodeLanguagePanel();
	void scheduleCodeLanguageSync();
	bool requestCodeLanguageDefinition();
	bool requestCodeLanguageCompletions(int triggerKind, const QString& trigger);
	bool requestCodeLanguageReferences(const QString& word);
	QWidget* buildCodeQuickInfoPanel();
	QWidget* buildCodeSignaturePanel();
	QWidget* buildCodeSnippetBar();
	bool queueCodeSignatureHelp(int triggerKind, const QString& trigger = {});
	void requestCodeSignatureHelp();
	void codeSignatureContextChanged();
	bool retireCodeSignatureHelp();
	bool requestCodeQuickInfo(int offset, bool persistent, const QPoint& globalPosition);
	void retireCodeQuickInfo(const QString& reason, bool clearPane);
	void formatCodeDocument(bool selection);
	void renameCodeSymbol();
	void showCodeActions();
	void retireCodeFormatting(const QString& reason, OperationState state = OperationState::Cancelled);
	void showCodeDefinitions(const QVector<CodeSymbol>& matches, const QString& word);
	void appendCodeLanguageDiagnostics(QVector<StudioDiagnosticMarker>* markers);
	bool activateCodeLanguageDiagnostic(QListWidgetItem* item);
	void refreshBuildSurface();
	void runSelectedBuildPipeline();
	void cancelBuildPipeline();
	void finishBuildPipeline(const BuildPipelineResult& result, const QString& taskId, const QString& projectPath);
	void copyBuildPipelineCommands();
	void inspectCompiledArtifacts();
	void launchConfiguredGame();
	// One request for the plan shown, the commands copied, and the run, so the
	// three never disagree about tools or paths.
	[[nodiscard]] BuildPipelineRequest currentBuildPipelineRequest(bool dryRun) const;
	void prepareLevelBuildWorkspaceFromUi();
	void openLevelBuildWorkspaceAssets();
	void publishLevelBuildWorkspacePackage();
	void deployLevelBuildWorkspace(bool launchAfter = false);
	[[nodiscard]] bool preparedLevelBuildCanLaunch() const;
	[[nodiscard]] bool preparedLevelBuildWorkspaceActive() const;
	// The file a build of the open map reads: the last Save As copy when there
	// is one, else the map's own file. Empty with no map open.
	[[nodiscard]] QString openLevelMapBuildPath() const;
	// Points the build input at the open map while the user has not chosen a
	// file of their own, and switches to a pipeline for that map's game when
	// the current one cannot build it. True when the input changed.
	bool followOpenMapInBuild();
	[[nodiscard]] bool selectedGameInstallation(GameInstallationProfile* out) const;
	[[nodiscard]] GameLaunchRequest currentGameLaunchRequest(const GameInstallationProfile& installation, const QString& bspPath) const;
	// The map a launch loads when the Map field is empty: the compiled map's
	// file name, or the open Doom map's lump.
	[[nodiscard]] QString derivedLaunchMapName(const QString& bspPath) const;
	// Lists the last run's compiler warnings and errors; `showRun` is false
	// while the page shows a plan rather than that run.
	void refreshBuildProblems(bool showRun);
	// A problem on a line of the open map selects the object written there;
	// one in another text file opens it at the line. One that can be shown
	// nowhere else is shown on the Build page, and the reason is returned.
	QString activateBuildProblem(QListWidgetItem* item);
	void revealBuildOutput();
	// Stages the built map (and its .lit and .lux) into the open package under
	// maps/, replacing an older build there; Save As then writes it.
	void addBuildOutputToPackage();
	// Runs the pipeline and, once it succeeds, launches the game with the map
	// it built (F5).
	void buildAndLaunch();
	// Leak trails: the point file a leaking qbsp or q3map2 run writes, drawn
	// over the open map. A build of the open map shows or clears its trail by
	// itself; the Build menu loads one by hand.
	bool showLeakTrail(const QString& pointFilePath);
	void clearLeakTrailFromUi();
	void loadLeakTrailFromFile();
	void syncLeakTrailWithLastBuild();
	// Shows the trail in Levels and frames it; opens the build's map first
	// when another map is open. False when the trail could not be read.
	bool revealLeakTrail(const QString& pointFilePath);
	// Asks once per installation whether test maps may be copied into it, and
	// records the answer on the profile. True when the copy may go ahead.
	bool allowTestMaps(GameInstallationProfile* installation, const GameMapDeployPlan& plan);
	// Shows the game folder the installation was last launched into, once per
	// change of installation, so a mod's folder is not retyped every session.
	void syncLaunchGameDirectory(const GameInstallationProfile& installation);
	QString selectedWorkspaceFilePath() const;
	QString selectedWorkspaceVirtualPath() const;
	void revealSelectedWorkspacePath();
	void copySelectedWorkspaceVirtualPath();
	void refreshPackageTree();
	void refreshPackageCompositionSummary();
	void refreshPackageStagingSummary();
	void initializePackageRecovery();
	void retirePackageRecovery();
	void refreshPackageRecoveryStatus();
	void recoverPackagesFromUi();
	[[nodiscard]] QString packageViewKey() const;
	[[nodiscard]] const PackageArchive& packageViewArchive() const;
	void refreshPackageEntryDetails(const QString& virtualPath);
	void filterPackageEntries();
	// Shows one package folder's contents in the entry list, the way a file
	// browser does. `selectPath` is selected once the folder is listed.
	void navigatePackageFolder(const QString& folder, const QString& selectPath = QString(), bool recordHistory = true);
	void stepPackageFolderHistory(int delta);
	void rebuildPackageBreadcrumb();
	void refreshCompilerPipelineSummary();
	void refreshCompilerToolTable(const CompilerRegistrySummary& registry);
	void refreshCompilerToolActions();
	[[nodiscard]] QString selectedCompilerToolId() const;
	// Saves the executable the user picks for the selected tool as a user
	// override: the same setting `compiler set-path` writes.
	void locateSelectedCompilerTool();
	void clearSelectedCompilerToolPath();
	// Re-reads the tools everywhere they are shown after a path changed.
	void compilerToolPathsChanged(const QString& message);
	QString selectedCompilerProfileId() const;
	void runSelectedCompilerProfile();
	void finishCompilerRun(const CompilerRunResult& result, const QString& taskId, const QString& projectPath);
	void copySelectedCompilerCliEquivalent();
	void copySelectedCompilerManifest();
	QString selectedPackageEntryPath() const;
	QStringList selectedPackageEntryPaths() const;
	qsizetype selectedPackageEntryIndex() const;
	QVector<qsizetype> selectedPackageEntryIndexes() const;
	QString selectedPackageTreeEntryPath() const;
	void selectPackageEntryPath(const QString& virtualPath);
	void selectPackageTreeEntryPath(const QString& virtualPath);
	void showPackageEntryContextMenu(const QPoint& position);
	void extractSelectedPackageEntries();
	void extractAllPackageEntries();
	void extractPackageEntriesToDirectory(const QStringList& virtualPaths, bool extractAll, const QVector<PackageExtractionSelection>& entrySelections = {});
	void showPackageExtractionReport(const PackageExtractionReport& report);
	bool choosePackageStageResolution(const QString& title, PackageStageConflictResolution* resolution);
	void stagePackageAddFile();
	PackageStageFilesResult stagePackageFilesWithProgress(const QVector<PackageStageFileRequest>& files);
	// Files dropped on an open package's entry list or folder tree are staged
	// into it, in the folder under the pointer; `onPackage` says whether the
	// point is over those views at all.
	[[nodiscard]] QString packageDropFolderAt(const QPoint& windowPoint, bool* onPackage) const;
	void stageDroppedFiles(const QStringList& paths, const QString& folder);
	// Entries dragged out of the package list: each file, or each file under a
	// folder, copied into the session's temporary folder, as the URLs a
	// desktop drop copies from. Empty, with the reason shown, when too much.
	[[nodiscard]] QList<QUrl> extractPackageEntriesForDrag(const QVector<qsizetype>& entryIndexes);
	void stagePackageReplaceSelected();
	void stagePackageRenameSelected();
	void stagePackageRenameEntry(const PackageEntry& selected);
	void stagePackageDeleteEntries(const QVector<PackageEntry>& selectedEntries);
	void stagePackageFolder(bool rename, const QString& path, const QString& revision);
	void editPackageWadGroups();
	void stagePackageDeleteSelected();
	void saveStagedPackageAs();
	void newPackage();
	void createPackageDirectory();
	void showPackageTreeContextMenu(const QPoint& position);
	void openPackageDraft();
	bool savePackageDraft(bool saveAs = false);
	bool applyPackageEdit(const QString& label,
		const std::function<bool(PackageStagingModel&, QString*, const PackageReadControl&)>& edit);
	void undoPackageEdit(bool redo = false);
	void refreshPackageCommandEnablement();
	QString packageOpenPath() const;
	QString packageWatchPath() const;
	void comparePackageWithFile(bool staged = false);
	void validateOpenPackage();
	void activateRecentProject(QListWidgetItem* item);
	// Makes a folder the current project and refreshes everything that reads it.
	void openProjectPath(const QString& path);
	void closeProject();
	void rebuildRecentMenu();
	void unstagePackageOperations(const QStringList& operationIds);
	void showPackageStagingContextMenu(const QPoint& position);
	void removeSelectedRecentProject();
	void clearRecentProjects();
	void refreshSetupPanel();
	void startOrResumeSetup();
	void advanceSetup();
	void skipSetup();
	void completeSetup();
	void resetSetup();
	void refreshPreferenceControls();
	void savePreferenceControls();
	void applyPreferencesToUi();
	void applyPreferencesToWidgets();
	// Re-applies per-item state colours in the lists without touching the
	// application stylesheet.
	void applyStateColors();
	void scheduleThemeRefresh();
	void seedActivityCenter();
	void recordActivity(const QString& title, const QString& detail, const QString& source, OperationState state, const QString& resultSummary = QString(), const QStringList& warnings = {});
	// The Assistant: questions to a text model, with the context the user ticks.
	void buildAssistantDock();
	void showAssistant(const QString& prompt = QString(), const QStringList& includeContext = QStringList());
	void refreshAssistant();
	void refreshAssistantContext(const QStringList& forceChecked = QStringList());
	[[nodiscard]] AiContextItem assistantContextItem(const QString& id) const;
	[[nodiscard]] QVector<AiContextItem> assistantContextItems() const;
	[[nodiscard]] AiChatRequest assistantRequest(const AiTextConnection& connection, const QString& question) const;
	bool confirmAssistantConsent(const AiTextConnection& connection, const AiHttpRequest& http);
	void showAssistantRequestPreview();
	void sendAssistantQuestion();
	void cancelAssistantQuestion();
	void finishAssistantQuestion(const AiChatResponse& response);
	void renderAssistantTranscript();
	void setAssistantStatus(const QString& text);
	void explainBuildWithAssistant();
	void askAssistantAboutCode();
	// Settings > AI and Automation > Assistant Model.
	void refreshAiTextModelFields();
	void saveAiTextModelFields();
	void testAiTextConnection();
	void persistActivityTask(const QString& taskId);
	void updateSetupActivity();
	void refreshActivityCenter(const QString& preferredTaskId = QString());
	void refreshActivityDetails(const QString& taskId);
	QString selectedActivityTaskId() const;
	void cancelSelectedActivityTask();
	void clearFinishedActivityTasks();
	void copyDiagnosticBundle();
	void refreshInspectorDrawerForSettings();
	void refreshInspectorDrawerForProject(const QString& path);
	void updateInspector();
	void updateInspectorForProject(const QString& path);
	void showAboutDialog();
	[[nodiscard]] bool confirmDestructiveAction(const QString& title, const QString& message, const QString& acceptLabel);
	[[nodiscard]] QString activePaletteId() const;
	void invalidatePaletteResolution();

	StudioSettings m_settings;
	OperationStateModel m_activity;
	PackageArchive m_packageArchive;
	PackageStagingModel m_packageStaging;
	mutable PackageArchive m_packageViewArchive;
	mutable QString m_packageViewKey;
	QString m_packageBrowserKey;
	LevelMapDocument m_levelMapDocument;
	LevelRecoveryWriter* m_levelRecoveryWriter = nullptr;
	ShaderDocument m_advancedShaderDocument;
	QVector<ShaderReferenceValidation> m_advancedShaderValidation;
	QStringList m_advancedShaderValidationWarnings;
	SpriteWorkflowPlan m_advancedSpritePlan;
	CodeWorkspaceIndex m_advancedCodeIndex;
	ExtensionDiscoveryResult m_advancedExtensionDiscovery;
	AiWorkflowResult m_advancedAiProposal;
	std::unique_ptr<StudioCommandRegistry> m_ownedCommands;
	StudioCommandRegistry* m_commands = nullptr;
	CommandPaletteDialog* m_commandPalette = nullptr;
	QuickOpenDialog* m_quickOpen = nullptr;
	QString m_quickOpenActivityId;
	QStackedWidget* m_modeStack = nullptr;
	// The studio bar: the menus, history, open commands, the command search
	// centred on the window, and the build and launch commands, in one row.
	QToolBar* m_toolBar = nullptr;
	QMenuBar* m_menuBar = nullptr;
	LoadingPane* m_inspectorState = nullptr;
	DetailDrawer* m_inspectorDrawer = nullptr;
	LoadingPane* m_workspaceState = nullptr;
	DetailDrawer* m_workspaceDrawer = nullptr;
	ModeRail* m_modeRail = nullptr;
	QComboBox* m_railBehaviourCombo = nullptr;
	QDockWidget* m_activityDock = nullptr;
	QDockWidget* m_inspectorDock = nullptr;
	QDockWidget* m_assistantDock = nullptr;
	QLabel* m_assistantConnection = nullptr;
	QToolButton* m_assistantSettingsButton = nullptr;
	QListWidget* m_assistantContext = nullptr;
	QTextBrowser* m_assistantTranscript = nullptr;
	QLabel* m_assistantStatus = nullptr;
	QPlainTextEdit* m_assistantPrompt = nullptr;
	QPushButton* m_assistantSend = nullptr;
	QPushButton* m_assistantCancel = nullptr;
	QPushButton* m_assistantPreview = nullptr;
	QPushButton* m_assistantCopy = nullptr;
	QPushButton* m_assistantClear = nullptr;
	QTimer* m_assistantTicker = nullptr;
	std::unique_ptr<AiChatClient> m_assistantClient;
	// One exchange as the transcript shows it: "user", "assistant", or a
	// question that got no answer ("unanswered"), which the model never sees.
	struct AssistantTurn {
		QString role;
		QString shownText;
		QString sentText;
		// A question's context labels; an answer's connector; an unanswered
		// question's reason.
		QStringList contextLabels;
	};
	QVector<AssistantTurn> m_assistantTurns;
	QString m_assistantTaskId;
	QString m_assistantPendingName;
	qint64 m_assistantStartedMs = 0;
	QToolButton* m_buildExplain = nullptr;
	QComboBox* m_aiTextConnector = nullptr;
	QLineEdit* m_aiTextModel = nullptr;
	QLineEdit* m_aiTextEndpoint = nullptr;
	QLabel* m_aiTextCredential = nullptr;
	QPushButton* m_aiTextTest = nullptr;
	QLabel* m_aiTextTestResult = nullptr;
	std::unique_ptr<AiChatClient> m_aiTestClient;
	QPointer<LevelGenerationDialog> m_levelGenerationDialog;
	QPointer<TextureGenerationDialog> m_textureGenerationDialog;
	QPointer<LevelAiEditDialog> m_levelAiEditDialog;
	QPointer<SoundGenerationDialog> m_soundGenerationDialog;
	QComboBox* m_aiImageConnector = nullptr;
	QLineEdit* m_aiImageModel = nullptr;
	QLineEdit* m_aiImageEndpoint = nullptr;
	QLabel* m_aiImageStatus = nullptr;
	QComboBox* m_aiSoundConnector = nullptr;
	QLineEdit* m_aiSoundModel = nullptr;
	QLineEdit* m_aiSoundEndpoint = nullptr;
	QLabel* m_aiSoundStatus = nullptr;
	QToolButton* m_activityToggle = nullptr;
	QToolButton* m_inspectorToggle = nullptr;
	QMenu* m_viewMenu = nullptr;
	QMenu* m_recentMenu = nullptr;
	// Filters for the lists that had none: each hides rows as the user types.
	QLineEdit* m_modelFilter = nullptr;
	QLineEdit* m_audioFilter = nullptr;
	QLineEdit* m_levelObjectFilter = nullptr;
	// What decoding each image found, for the Textures filter's w, h, and
	// format terms: known as each thumbnail is made.
	QHash<QString, QSize> m_textureSizes;
	QHash<QString, QString> m_textureFormats;
	// A filter on what decoding finds is run again a moment after thumbnails
	// arrive, not after every batch of them.
	QTimer* m_textureRefilterTimer = nullptr;
	// The Objects tab, which counts what the filter keeps while one is typed.
	QTabWidget* m_levelOutlinerTabs = nullptr;
	QWidget* m_levelObjectsPanel = nullptr;
	QLineEdit* m_shaderFilter = nullptr;
	QLineEdit* m_codeTreeFilter = nullptr;
	// Package, entry, and palette of the model on screen, so returning to the
	// Models surface does not decode it again and reset the camera.
	QString m_modelShownKey;
	// Page chrome keyed by StudioMode, and the splitters whose sizes persist.
	QHash<int, PageHeader*> m_pageHeaders;
	// The Workspace page's tiles, keyed by the StudioMode each leads to; each
	// shows the context line of its page's header.
	QHash<int, NavigationTile*> m_workspaceTiles;
	QHash<int, QStackedWidget*> m_surfaceStacks;
	QHash<QString, QSplitter*> m_layoutSplitters;
	QHash<QString, QByteArray> m_defaultLayoutStates;
	QHash<int, EmptyStateView*> m_emptyStates;
	QListWidget* m_recentProjects = nullptr;
	QListWidget* m_gameInstallations = nullptr;
	QTextEdit* m_inspector = nullptr;
	ElidedLabel* m_recentSummary = nullptr;
	ElidedLabel* m_installSummary = nullptr;
	QLabel* m_packageSummary = nullptr;
	QToolButton* m_projectChip = nullptr;
	QToolButton* m_packageChip = nullptr;
	QToolButton* m_installChip = nullptr;
	QToolButton* m_compilerChip = nullptr;
	QToolButton* m_aiChip = nullptr;
	QListWidget* m_settingsCategories = nullptr;
	QStackedWidget* m_settingsPages = nullptr;
	QLineEdit* m_settingsSearch = nullptr;
	QLabel* m_settingsNoMatch = nullptr;
	QTabWidget* m_buildSections = nullptr;
	// Session folder for temporary copies of package entries opened in a
	// surface that reads files from disk (maps, shaders, scripts).
	std::shared_ptr<PackageCopyBudget> m_packageCopyBudget;
	std::shared_ptr<PackageCopySession> m_sessionCopies;
	QVector<std::shared_ptr<const QTemporaryDir>> m_packageCopyLeases;
	bool m_codeFileIsPackageCopy = false;
	QListWidget* m_projectProblems = nullptr;
	QTabWidget* m_workspaceContextTabs = nullptr;
	QWidget* m_workspaceSearchPanel = nullptr;
	QLineEdit* m_workspaceSearch = nullptr;
	QListWidget* m_workspaceSearchResults = nullptr;
	QListWidget* m_changedFiles = nullptr;
	// Git status runs asynchronously; a newer request makes older results stale.
	QProcess* m_gitStatusProcess = nullptr;
	int m_gitStatusGeneration = 0;
	QListWidget* m_dependencyGraph = nullptr;
	QListWidget* m_recentActivityTimeline = nullptr;
	ActivityTimelineChart* m_activityTimelineChart = nullptr;
	QLineEdit* m_levelMapPath = nullptr;
	QComboBox* m_levelMapName = nullptr;
	// The toolbar holds the WAD map chooser through this action; it shows only
	// for a WAD path.
	QAction* m_levelMapNameAction = nullptr;
	// The WAD whose maps the chooser lists, so it is read once per WAD.
	QString m_levelMapNamesPath;
	QComboBox* m_levelMapEngine = nullptr;
	QComboBox* m_levelMapCompilerProfile = nullptr;
	LoadingPane* m_levelMapState = nullptr;
	MapViewport* m_levelMapViewport = nullptr;
	QComboBox* m_levelMapProjection = nullptr;
	QComboBox* m_levelMapGrid = nullptr;
	QCheckBox* m_levelMapSnap = nullptr;
	// Display switches, checkable items of the view bar's Show menu.
	QAction* m_levelMapShowThings = nullptr;
	QAction* m_levelMapShowSectors = nullptr;
	QAction* m_levelMapShowGrid = nullptr;
	QAction* m_levelMapShowVertices = nullptr;
	QAction* m_levelMapShowLabels = nullptr;
	QAction* m_levelMapShowLinks = nullptr;
	// The toolbar holds the sector switch through this action; only Doom maps
	// have sectors, so it is hidden for the others.
	ElidedLabel* m_levelMapHover = nullptr;
	QLineEdit* m_entityDefinitionPath = nullptr;
	QLabel* m_entityDefinitionSummary = nullptr;
	QTreeWidget* m_entityInspector = nullptr;
	bool m_refreshingEntityInspector = false;
	EntityDefinitionCatalogue m_entityDefinitions;
	EntityValidationReport m_entityValidation;
	LevelObjectList* m_levelMapObjects = nullptr;
	bool m_selectMatchingWhenReady = false;
	LevelScenePanel* m_levelScenePanel = nullptr;
	QListWidget* m_levelMapStatistics = nullptr;
	QListWidget* m_levelMapView = nullptr;
	QListWidget* m_levelMapValidation = nullptr;
	LevelTextureAuditPanel* m_levelTextureAudit = nullptr;
	DetailDrawer* m_levelMapDrawer = nullptr;
	QAbstractButton* m_levelMapSaveAs = nullptr;
	QAbstractButton* m_levelMapEditProperty = nullptr;
	QAbstractButton* m_levelMapMoveSelection = nullptr;
	QAbstractButton* m_levelMapPlanCompile = nullptr;
	QAbstractButton* m_levelMapCopyCli = nullptr;
	QLineEdit* m_advancedShaderPath = nullptr;
	QLineEdit* m_advancedSpriteName = nullptr;
	QLineEdit* m_advancedSpriteFrames = nullptr;
	QLineEdit* m_advancedSpriteRotations = nullptr;
	QLineEdit* m_advancedAiPrompt = nullptr;
	QLineEdit* m_advancedExtensionRoot = nullptr;
	QComboBox* m_advancedSpriteEngine = nullptr;
	QComboBox* m_advancedAiKind = nullptr;
	LoadingPane* m_advancedStudioState = nullptr;
	QTreeWidget* m_advancedShaderGraph = nullptr;
	QListWidget* m_advancedSpriteSequence = nullptr;
	QListWidget* m_advancedCodeTree = nullptr;
	QListWidget* m_advancedAiProposalList = nullptr;
	QListWidget* m_advancedExtensions = nullptr;
	DetailDrawer* m_advancedStudioDrawer = nullptr;
	QAbstractButton* m_advancedShaderInspect = nullptr;
	QAbstractButton* m_advancedSpritePlanButton = nullptr;
	QAbstractButton* m_advancedCodeIndexButton = nullptr;
	QAbstractButton* m_advancedAiCreate = nullptr;
	QAbstractButton* m_advancedExtensionDiscover = nullptr;
	QLineEdit* m_textureFilter = nullptr;
	QToolButton* m_textureTilesButton = nullptr;
	QToolButton* m_textureListButton = nullptr;
	bool m_textureTileMode = true;
	TextureThumbnailCache m_textureThumbnails;
	QHash<QString, QList<QPersistentModelIndex>> m_textureThumbnailItems;
	QSet<QString> m_textureThumbnailWanted, m_textureMetadataKnown;
	// Each listed image's decoder path (see assetDetectionPath), by virtual path.
	QHash<QString, QString> m_textureDecodePaths;
	quint64 m_texturePaletteRevision = 0;
	TexturePreviewWorker* m_texturePreviewWorker = nullptr;
	TexturePreviewWorker* m_textureThumbnailWorker = nullptr;
	TexturePreviewWorker* m_packageTexturePreviewWorker = nullptr;
	PackagePreviewWorker* m_packagePreviewWorker = nullptr;
	QString m_packageMetadataPreviewKey;
	std::shared_ptr<const PackagePreviewResult> m_packageMetadataPreview;
	bool m_packagePreviewCancelled = false;
	QString m_packageTexturePreviewKey;
	std::shared_ptr<const TexturePreviewResult> m_packageTexturePreview;
	QString m_texturePreviewKey;
	IdTechImageDecodeResult m_textureDecoded;
	IdTechPaletteResolution m_textureDecodedPalette;
	QPushButton* m_textureEditSelected = nullptr;
	QToolButton* m_textureCancelPreview = nullptr;
	bool m_texturePreviewPending = false;
	bool m_textureThumbnailPending = false;
	bool m_textureThumbnailsPaused = false;
	QString m_textureThumbnailPendingPath;
	QString m_textureThumbnailKey;
	QStringList m_textureThumbnailQueue;
	QTimer* m_textureThumbnailTimer = nullptr;
	QTimer* m_textureDemandTimer = nullptr;
	QListWidget* m_textureEntries = nullptr;
	ImagePreviewView* m_texturePreview = nullptr;
	PaletteSwatchView* m_texturePalette = nullptr;
	QComboBox* m_texturePaletteChoice = nullptr;
	QComboBox* m_textureMipLevel = nullptr;
	QComboBox* m_textureFrame = nullptr;
	QLabel* m_texturePaletteSource = nullptr;
	LoadingPane* m_textureState = nullptr;
	DetailDrawer* m_textureDrawer = nullptr;
	QListWidget* m_modelEntries = nullptr;
	ModelViewport* m_modelViewport = nullptr;
	QComboBox* m_modelRenderMode = nullptr;
	QComboBox* m_modelAnimation = nullptr;
	QAbstractButton* m_modelPlayPause = nullptr;
	ElidedLabel* m_modelHover = nullptr;
	ImagePreviewView* m_modelSkinPreview = nullptr;
	ModelAppearance m_modelAppearance;
	QString m_modelAppearanceContext;
	LevelPreviewAssets m_modelAppearanceAssets;
	QFormLayout* m_modelAppearanceForm = nullptr;
	QComboBox* m_modelAppearanceSurface = nullptr;
	QComboBox* m_modelAppearanceSlot = nullptr;
	QComboBox* m_modelAppearanceMdlSkin = nullptr;
	QComboBox* m_modelAppearanceMdlMember = nullptr;
	QPushButton* m_modelAppearanceChoose = nullptr;
	QPushButton* m_modelAppearanceReset = nullptr;
	QPlainTextEdit* m_modelAppearanceDetails = nullptr;
	QTreeWidget* m_modelDetails = nullptr;
	ModelMesh m_modelMesh;
	ModelPreviewWorker* m_modelPreview = nullptr;
	QPushButton* m_modelPreviewCancel = nullptr;
	QPushButton* m_modelExport = nullptr;
	bool m_modelExportRunning = false, m_closeAfterModelExport = false;
	std::function<void()> m_cancelModelExport;
	QPointer<ModelDesignDialog> m_modelDesignDialog;
	QPointer<ModelEditorDialog> m_modelEditorDialog;
	QPointer<ModelAssemblyDialog> m_modelAssemblyDialog;
	QPointer<TextureEditorDialog> m_textureEditorDialog;
	QString m_levelPackagePreviewKey;
	LoadingPane* m_modelState = nullptr;
	DetailDrawer* m_modelDrawer = nullptr;
	QListWidget* m_audioEntries = nullptr;
	PackageArchive m_audioArchive;
	QPointer<AudioEditorDialog> m_audioEditorDialog;
	QPointer<AudioSessionDialog> m_audioSessionDialog;
	QPointer<AudioRecoveryDialog> m_audioRecoveryDialog;
	AudioRecoveryDiscovery* m_audioRecoveryDiscovery = nullptr;
	QCheckBox* m_audioRecoveryEnabled = nullptr;
	QCheckBox* m_audioRecoveryNotify = nullptr;
	QString m_audioRecoveryNotice;
	bool m_audioRecoveryDiscoveryStarted = false;
	bool m_audioRecoveryNoticeVisible = false;
	WaveformView* m_audioWaveform = nullptr;
	QTreeWidget* m_audioDetails = nullptr;
	LoadingPane* m_audioState = nullptr;
	DetailDrawer* m_audioDrawer = nullptr;
	QLabel* m_audioTimeLabel = nullptr;
	QSlider* m_audioVolume = nullptr;
	QSlider* m_audioSeek = nullptr;
	AudioPlayback* m_audioPlayback = nullptr;
	AudioBrowserWorker* m_audioPreviewWorker = nullptr;
	AudioBrowserWorker* m_audioAuditionWorker = nullptr;
	quint64 m_audioRevision = 0, m_audioShownRevision = 0;
	qsizetype m_audioShownIndex = -1;
	bool m_audioPreviewPending = false, m_audioAuditionPending = false;
	// The entry the player holds, the entry on show, and whether it can play.
	QString m_audioPlaybackPath;
	QString m_audioShownPath;
	QString m_audioBrowserKey;
	bool m_audioSelectedPlayable = false;
	bool m_audioSelectedEditable = false;
	qint64 m_audioDurationMs = 0;
	// Requested start position, retained through pause/stop and preparation.
	qint64 m_audioCursorMs = 0;
	QTreeWidget* m_codeTree = nullptr;
	StudioCodeEditor* m_codeEditor = nullptr;
	CodeFindBar* m_codeFindBar = nullptr;
	StudioSyntaxHighlighter* m_codeHighlighter = nullptr;
	// One open file on the Code page: its own document (text, undo history,
	// modified state) and highlighter, and where its caret and view were.
	struct CodeTab {
		QString path;
		QString draftName;
		QString suggestedName;
		QString recoveryId;
		// Stable for this tab's lifetime, including saves and reloads.
		QString searchId;
		QString recoveryStatus;
		QString recoveryError;
		TextFileDocument source;
		QTextDocument* document = nullptr;
		StudioSyntaxHighlighter* highlighter = nullptr;
		bool truncated = false;
		bool packageCopy = false;
		QString packageNote;
		QString packageTooltip;
		int anchor = 0;
		int position = 0;
		int scroll = 0;
		int horizontalScroll = 0;
	};
	QVector<CodeTab> m_codeTabs;
	QTabBar* m_codeTabBar = nullptr;
	int m_codeTab = -1;
	int m_codeDraftSerial = 0;
	PackageRecoveryWriter* m_packageRecoveryWriter = nullptr;
	QPointer<PackageRecoveryDialog> m_packageRecoveryDialog;
	QTimer* m_packageRecoveryTimer = nullptr;
	QLabel* m_packageRecoveryLabel = nullptr;
	QString m_packageRecoveryId, m_packageRecoveryError;
	QDateTime m_packageRecoverySaved;
	quint64 m_packageRecoveryRevision = 0;
	bool m_packageRecoveryClosing = false;
	CodeRecoveryWriter* m_codeRecoveryWriter = nullptr;
	bool m_codeRecoveryClosing = false;
	// What the editor shows with no file open; the shell owns it, so swapping
	// a tab's document in never deletes it.
	QTextDocument* m_codeEmptyDocument = nullptr;
	StudioSyntaxHighlighter* m_codeEmptyHighlighter = nullptr;
	ElidedLabel* m_codeStatus = nullptr;
	QLineEdit* m_codeFind = nullptr;
	QLineEdit* m_codeReplace = nullptr;
	QListWidget* m_codeDiagnostics = nullptr;
	QString m_levelMapViewportKey;
	QString m_levelMapViewportSourceKey;
	bool m_syncingLevelMapSelection = false;
	// The status message last shown about the Levels selection ("7 objects
	// selected."). It is taken down when the selection changes, so the bar
	// never reports a selection that is gone.
	QString m_levelSelectionMessage;
	void showLevelSelectionMessage(const QString& message);
	QString m_codeFilePath;
	// External change detection for everything the shell holds open. The
	// watcher does no work of its own: this timer drives poll(), which is
	// the authoritative check.
	DocumentWatcher m_documentWatcher;
	QTimer* m_documentWatchTimer = nullptr;
	// Every manifest write the studio makes goes through here, so the watcher
	// adopts the new bytes instead of reporting the save as an outside edit.
	bool saveOwnProjectManifest(const ProjectManifest& manifest, QString* error = nullptr);
	// The session as last written to settings. Recording starts with
	// beginSession(), so a self-test or a snapshot run never overwrites it.
	QString m_workspaceDocumentPath;
	QByteArray m_workspaceRevision;
	QJsonObject m_workspaceExtensions;
	StudioSession m_recordedSession;
	bool m_recordingSession = false;
	// The crash notice offers the last session back: until it is answered the
	// record keeps that session, so a second crash or a close cannot lose it.
	bool m_sessionOfferPending = false;
	// Another studio was running at start and owns the recorded session; this
	// one neither reopens nor records it until that one has closed.
	bool m_secondaryInstance = false;
	NoticeBar* m_noticeBar = nullptr;
	// Fades a newly chosen page in over the page stack.
	PageTransition* m_pageTransition = nullptr;
	// Paths the user has told us to stop asking about for the current load.
	// Outside changes the user chose not to take, by path, as the file looked
	// then: that change is never asked about twice, but a later one is.
	QHash<QString, DocumentFingerprint> m_ignoredExternalChanges;
	bool m_codeDirty = false;
	QLineEdit* m_packageFilter = nullptr;
	QWidget* m_packageBreadcrumb = nullptr;
	QToolButton* m_packageBack = nullptr;
	QToolButton* m_packageForward = nullptr;
	QToolButton* m_packageUp = nullptr;
	QString m_packageBrowseFolder;
	QString m_packageBrowseSource;
	QStringList m_packageFolderHistory;
	int m_packageFolderHistoryIndex = -1;
	LoadingPane* m_packageState = nullptr;
	QListWidget* m_packageComposition = nullptr;
	CompositionChart* m_packageCompositionChart = nullptr;
	PackageStagingView* m_packageStagingSummary = nullptr;
	QAbstractButton* m_packageUndo = nullptr;
	QAbstractButton* m_packageRedo = nullptr;
	QAbstractButton* m_packageDraftSave = nullptr;
	PackageFolderView* m_packageTree = nullptr;
	QAction* m_packageFolderRename = nullptr;
	QAction* m_packageFolderDelete = nullptr;
	PackageEntryView* m_packageEntries = nullptr;
	DetailDrawer* m_packageDrawer = nullptr;
	ImagePreviewView* m_packageImagePreview = nullptr;
	QStackedWidget* m_packagePreviewStack = nullptr;
	QPlainTextEdit* m_packageTextPreview = nullptr;
	QLabel* m_packagePreviewNote = nullptr;
	QLabel* m_packagePreviewStatus = nullptr;
	QProgressBar* m_packagePreviewProgress = nullptr;
	QPushButton* m_packagePreviewCancel = nullptr;
	QAbstractButton* m_packageNew = nullptr;
	QAbstractButton* m_packageCreateDirectory = nullptr;
	QAbstractButton* m_packageWadGroups = nullptr;
	QAbstractButton* m_packageStageAdd = nullptr;
	QAbstractButton* m_packageStageReplace = nullptr;
	QAbstractButton* m_packageStageRename = nullptr;
	QAbstractButton* m_packageStageDelete = nullptr;
	QAbstractButton* m_packageStageSaveAs = nullptr;
	QAbstractButton* m_packageCompare = nullptr;
	QAbstractButton* m_packageValidate = nullptr;
	QAbstractButton* m_packageReview = nullptr;
	QPointer<QDialog> m_packageCompareDialog;
	QPointer<QDialog> m_packageValidationDialog;
	QPointer<QDialog> m_packageSaveDialog;
	bool m_packageSaveRunning = false;
	bool m_packageReadRunning = false;
	QString m_packageCompareTaskId;
	QString m_packageValidationTaskId;
	QString m_packageSaveTaskId;
	QComboBox* m_packageCompression = nullptr;
	QAction* m_packageWriteManifest = nullptr;
	QAbstractButton* m_packageExtractSelected = nullptr;
	QAbstractButton* m_packageExtractAll = nullptr;
	QAbstractButton* m_packageExtractCancel = nullptr;
	QAbstractButton* m_importDetectedInstall = nullptr;
	QCheckBox* m_installTestMaps = nullptr;
	QAbstractButton* m_revealWorkspacePath = nullptr;
	QAbstractButton* m_copyWorkspaceVirtualPath = nullptr;
	QListWidget* m_compilerPipeline = nullptr;
	QComboBox* m_buildPipelineChoice = nullptr;
	QLineEdit* m_buildPipelineInput = nullptr;
	QListWidget* m_buildPipelineStages = nullptr;
	PipelineChart* m_buildPipelineChart = nullptr;
	LoadingPane* m_buildPipelineState = nullptr;
	DetailDrawer* m_buildPipelineDrawer = nullptr;
	QAbstractButton* m_buildPipelineRun = nullptr;
	QAbstractButton* m_buildPipelineCancel = nullptr;
	QAbstractButton* m_buildPipelineCopy = nullptr;
	QAbstractButton* m_buildInspectArtifacts = nullptr;
	QComboBox* m_launchProfileChoice = nullptr;
	QLineEdit* m_launchMapName = nullptr;
	QLineEdit* m_launchGameDirectory = nullptr;
	QCheckBox* m_launchDeploy = nullptr;
	QAbstractButton* m_buildAndLaunch = nullptr;
	// Set by Build and Launch; the finished build then starts the launch.
	bool m_launchAfterBuild = false;
	// The leak point file drawn over the open map; empty when none is.
	QString m_leakTrailPath;
	// The class last added from the Levels surface, offered first next time.
	QString m_lastAddedEntityClass;
	// Textures surface: show only the textures the open map uses.
	QCheckBox* m_textureInMapOnly = nullptr;
	QWidget* m_levelsRecentBox = nullptr;
	QListWidget* m_levelsRecentMaps = nullptr;
	QuickOpenDialog* m_symbolPicker = nullptr;
	QListWidget* m_levelMapHistory = nullptr;
	int m_lastAddedThingType = 0;
	QString m_lastAppliedTexture;
	QWidget* m_levelMaterialTools = nullptr;
	LevelSurfaceTools* m_levelSurfaceTools = nullptr;
	LevelSurfaceWorker* m_levelSurfaceWorker = nullptr;
	std::shared_ptr<LevelSurfaceStrokeState> m_levelSurfaceStroke;
	LevelPreviewWorker* m_levelSurfaceStrokePreview = nullptr;
	QTimer* m_levelSurfaceDebounce = nullptr;
	QVector<LevelSurfaceRequest> m_levelSurfacePending;
	QVector<LevelSurfaceFace> m_levelSurfaceFaces;
	QHash<QString, QSize> m_levelSurfaceSizes;
	std::function<bool()> m_levelSurfaceCurrent;
	quint64 m_levelSurfaceToken = 0;
	bool m_levelSurfacePublishing = false;
	bool m_levelSurfacePasting = false;
	LevelSurfaceClipboard m_levelSurfaceClipboard;
	std::shared_ptr<const PackageArchiveReader> m_levelSurfaceClipboardArchive;
	std::shared_ptr<const PackageStagingModel> m_levelSurfaceClipboardStaging;
	QString m_levelSurfaceClipboardPalette;
	QString m_levelSurfaceClipboardEngineFamily;
	LevelMapFormat m_levelSurfaceClipboardFormat = LevelMapFormat::Unknown;
	QComboBox* m_levelMaterialPicker = nullptr;
	QComboBox* m_levelMaterialTool = nullptr;
	QWidget* m_levelBrushTools = nullptr;
	QComboBox* m_levelBrushPlane = nullptr;
	QDoubleSpinBox* m_levelBrushBase = nullptr;
	QDoubleSpinBox* m_levelBrushDepth = nullptr;
	quint64 m_levelBrushWorkSerial = 0;
	QVector<LevelMapSelectionRef> m_levelBrushWorkSelection;
	ElidedLabel* m_levelMaterialPaintStatus = nullptr;
	QLineEdit* m_levelMaterialTargetInput = nullptr;
	QToolButton* m_levelMaterialStrokeCancel = nullptr;
	QString m_levelPaintMaterial, m_levelStrokeMaterial;
	quint64 m_levelPaintContextSerial = 0, m_levelStrokeSerial = 0, m_levelStrokeRevision = 0;
	bool m_levelMaterialStroke = false;
	QSet<LevelMaterialTarget> m_levelStrokeTargets;
	QVector<int> m_levelStrokeTriangles;
	QVector<LevelMaterialTarget> m_levelPreviewMaterialTargets;
	QHash<LevelMaterialTarget, QVector<int>> m_levelMaterialTriangles;
	// Textures applied to the map this session, newest first, which lead the
	// Levels Textures tab.
	QStringList m_recentMapTextures;
	QListWidget* m_levelMapTextures = nullptr;
	QLineEdit* m_levelMapTextureFilter = nullptr;
	// The 3D camera and the 2D view side by side; a layout shows one or both.
	QSplitter* m_levelMapViews = nullptr;
	QSplitter* m_levelMapUpperViews = nullptr;
	QSplitter* m_levelMapLowerViews = nullptr;
	QVector<MapViewport*> m_levelPlanViews;
	LevelViewLayout m_levelViewLayout = LevelViewLayout::Single2D;
	bool m_applyingLevelViewLayout = false;
	bool m_applyingLevelViewNavigation = false;
	// Temporary expansion keeps the ordinary layout and its splitter state.
	struct LevelViewMaximizeState {
		QWidget* view = nullptr;
		QList<QWidget*> visible;
		QByteArray root, upper, lower;
	};
	LevelViewMaximizeState m_levelMaximized;
	QWidget* m_levelLastFocusedView = nullptr;
	LevelViewLinks m_levelViewLinks;
	QToolButton* m_levelViewLayoutButton = nullptr;
	QToolButton* m_levelBookmarksButton = nullptr;
	LevelViewBookmarks m_levelBookmarks;
	QString m_levelBookmarkStore;
	QString m_levelBookmarkError;
	QString m_levelBookmarkCursor;
	QByteArray m_levelBookmarkRevision;
	quint64 m_levelBookmarkLoadSerial = 0;
	quint64 m_levelBookmarkGeneration = 0;
	bool m_levelBookmarkLayout = false;
	bool m_levelBookmarkCameraPending = false;
	CameraViewState m_levelBookmarkCamera;
	// The chosen editor profile's level controls (core/level_editor_controls.h).
	LevelEditorControls m_levelControls;
	QString m_levelControlsProfileName;
	bool m_levelControlsApplied = false;
	QString m_levelControlsProfileId;
	QToolButton* m_levelControlsButton = nullptr;
	QDialog* m_levelControlsDialog = nullptr;
	QString m_lastDrawnBrushTexture;
	ModelViewport* m_levelMap3D = nullptr;
	QToolButton* m_levelMap3DButton = nullptr;
	// Which map the 3D preview last showed, so an edit keeps its camera and
	// another map is framed afresh.
	QString m_levelMap3DSource;
	LevelPreviewWorker* m_levelPreviewWorker = nullptr;
	QWidget* m_levelPreviewStatus = nullptr;
	QLabel* m_levelPreviewLabel = nullptr;
	QProgressBar* m_levelPreviewProgress = nullptr;
	QToolButton* m_levelPreviewCancel = nullptr;
	QAction* m_levelPreviewTextured = nullptr;
	LevelPreviewAssets m_levelPreviewAssets;
	QString m_levelPreviewRequestKey;
	quint64 m_levelPreviewReload = 0;
	// Which object each triangle of the 3D preview came from.
	QVector<LevelMapSelectionRef> m_levelMap3DOwners;
	QVector<int> m_levelMap3DFaces;
	QTreeWidget* m_levelMapPalette = nullptr;
	QLineEdit* m_levelMapPaletteFilter = nullptr;
	// What the palette was last filled for, so it is refilled only when that
	// changes.
	QString m_levelMapPaletteKey;
	// A record field the Inspector should open to on its next rebuild, as
	// "face:<brush>:<face>" after a face is picked in the 3D preview.
	QString m_inspectorFocus;
	QTabWidget* m_levelMapInspectorTabs = nullptr;
	QWidget* m_levelMapInspectorPanel = nullptr;
	// The installation whose remembered game folder the launch form shows.
	QString m_launchGameDirectoryInstallation;
	QAbstractButton* m_launchGame = nullptr;
	QTreeWidget* m_launchSummary = nullptr;
	QAbstractButton* m_launchCopyCommand = nullptr;
	// The command line the launch preview planned, for Copy Command Line.
	QString m_launchPreviewCommandLine;
	QAbstractButton* m_compilerRunSelected = nullptr;
	QAbstractButton* m_compilerCopyCli = nullptr;
	QAbstractButton* m_compilerCopyManifest = nullptr;
	QTreeWidget* m_compilerTools = nullptr;
	QAbstractButton* m_compilerLocate = nullptr;
	QAbstractButton* m_compilerUseAutomatic = nullptr;
	QAbstractButton* m_buildUseOpenMap = nullptr;
	QAbstractButton* m_buildRevealOutput = nullptr;
	QAbstractButton* m_buildAddToPackage = nullptr;
	QTabWidget* m_buildResultTabs = nullptr;
	QListWidget* m_buildProblems = nullptr;
	// Whether the Problems list shows a finished run of the chosen pipeline
	// and input, rather than the note that one appears there after a run.
	bool m_buildProblemsShowRun = false;
	// Counts finished builds, so the Problems list, rebuilt each time the
	// Build page is shown, keeps its current row while it shows the same run.
	int m_buildRunSerial = 0;
	int m_buildProblemsSerial = -1;
	// True until the user picks a build input of their own; while it is true
	// the input follows the map open in Levels.
	bool m_buildInputFollowsMap = true;
	std::shared_ptr<const LevelBuildWorkspace> m_levelBuildWorkspace;
	QLabel* m_setupStatus = nullptr;
	QLabel* m_setupStep = nullptr;
	QLabel* m_setupNextAction = nullptr;
	QProgressBar* m_setupProgress = nullptr;
	QListWidget* m_setupSummary = nullptr;
	QAbstractButton* m_setupStartResume = nullptr;
	QAbstractButton* m_setupNext = nullptr;
	QAbstractButton* m_setupSkip = nullptr;
	QAbstractButton* m_setupComplete = nullptr;
	QAbstractButton* m_setupReset = nullptr;
	QLabel* m_activitySummary = nullptr;
	LoadingPane* m_activityState = nullptr;
	QListWidget* m_activityTasks = nullptr;
	DetailDrawer* m_activityDrawer = nullptr;
	QAbstractButton* m_activityCancel = nullptr;
	QAbstractButton* m_activityClearFinished = nullptr;
	QComboBox* m_localeCombo = nullptr;
	QComboBox* m_themeCombo = nullptr;
	QComboBox* m_textScaleCombo = nullptr;
	QComboBox* m_densityCombo = nullptr;
	QComboBox* m_editorProfileCombo = nullptr;
	QComboBox* m_aiReasoningConnectorCombo = nullptr;
	QComboBox* m_aiCodingConnectorCombo = nullptr;
	QComboBox* m_aiVisionConnectorCombo = nullptr;
	QComboBox* m_aiImageConnectorCombo = nullptr;
	QComboBox* m_aiAudioConnectorCombo = nullptr;
	QComboBox* m_aiVoiceConnectorCombo = nullptr;
	QComboBox* m_aiThreeDConnectorCombo = nullptr;
	QComboBox* m_aiEmbeddingsConnectorCombo = nullptr;
	QComboBox* m_aiLocalConnectorCombo = nullptr;
	QCheckBox* m_reducedMotion = nullptr;
	QCheckBox* m_restoreSession = nullptr;
	QCheckBox* m_crashReports = nullptr;
	QToolButton* m_codeZoomReadout = nullptr;
	QListWidget* m_codeOutline = nullptr;
	QWidget* m_codeBreadcrumb = nullptr;
	// The open file's problem count beside the readout; a click goes to the
	// next one, as F8 does.
	QToolButton* m_codeProblems = nullptr;
	// The file and symbol the breadcrumb shows, so a caret move that stays in
	// one symbol leaves it be.
	QString m_codeBreadcrumbKey;
	QLineEdit* m_codeOutlineFilter = nullptr;
	QTimer* m_codeOutlineTimer = nullptr;
	// The project's symbols for Go to Definition, gathered once per project
	// root and again after a save; the open file's come from its text.
	CodeWorkspaceIndex m_definitionIndex;
	bool m_definitionIndexValid = false;
	CodeIndexWorker* m_codeIndexWorker = nullptr;
	QWidget* m_codeIndexPanel = nullptr;
	QLabel* m_codeIndexStatus = nullptr;
	QLineEdit* m_codeIndexFilter = nullptr;
	QProgressBar* m_codeIndexProgress = nullptr;
	QPushButton* m_codeIndexCancel = nullptr;
	QTimer* m_codeIndexRefreshTimer = nullptr;
	QString m_codeIndexActivityId;
	bool m_codeIndexRequested = false;
	QPointer<QTextDocument> m_pendingDefinitionDocument;
	int m_pendingDefinitionRevision = -1;
	int m_pendingDefinitionPosition = -1;
	QVector<CodeSymbol> m_definitionMatches;
	QuickOpenDialog* m_definitionPicker = nullptr;
	// Where the user was: a page, and on it the open file and caret (Code),
	// the selection while the map is unchanged (Levels), or the current row
	// (Textures, Models, Audio).
	struct StudioPlace {
		StudioMode mode = StudioMode::Workspace;
		QString path;
		int line = 0;
		int column = 0;
		QVector<LevelMapSelectionRef> selection;
		quint64 revision = 0;
		// Which load of the map it was: maps of one WAD share its path, and a
		// reload starts the revision over.
		quint64 load = 0;
		QString entry;
	};
	[[nodiscard]] StudioPlace currentPlace() const;
	[[nodiscard]] bool samePlace(const StudioPlace& first, const StudioPlace& second) const;
	// Goes back to `place`; false when it has gone, as a deleted file has.
	bool restorePlace(const StudioPlace& place);
	// Takes the nearest place off `from` that still exists and goes there,
	// leaving the place it came from on `to`.
	bool stepToPlace(QVector<StudioPlace>* from, QVector<StudioPlace>* to);
	// "Levels", or "place_sample.qc, line 12" on Code: what a place is called
	// on the toolbar's arrows and in the status bar.
	[[nodiscard]] QString placeName(const StudioPlace& place) const;
	// The toolbar's arrows are enabled while there is somewhere to go, and
	// their tips say where.
	void refreshPlaceButtons();
	QVector<StudioPlace> m_backPlaces;
	QVector<StudioPlace> m_forwardPlaces;
	// Counts map loads and closes, for StudioPlace::load.
	quint64 m_levelMapLoadSerial = 0;
	bool m_levelMapLoadRunning = false;
	std::shared_ptr<MapBrushGeometryCache> m_levelPreparedBrushGeometry;
	QToolButton* m_navigateBackButton = nullptr;
	QToolButton* m_navigateForwardButton = nullptr;
	// Set while a place is being gone back to, or a session reopened, so the
	// moves that makes are not remembered as the user's.
	bool m_restoringPlace = false;
	// Find in Project's matches, apart from the open file's problems, so
	// opening a match does not replace the list it came from.
	QListWidget* m_codeSearchResults = nullptr;
	ProjectSearchPanel* m_codeSearchPanel = nullptr;
	CodeLanguagePanel* m_codeLanguagePanel = nullptr;
	QString m_codeLanguageActivityId;
	quint64 m_codeLanguageEpoch = 0;
	int m_codeCompletionRequest = -1;
	CodeQuickInfoPanel* m_codeQuickInfo = nullptr;
	CodeSignaturePanel* m_codeSignature = nullptr;
	QTimer* m_codeSignatureTimer = nullptr;
	QPointer<QTextDocument> m_codeSignatureDocument;
	QString m_codeSignaturePath, m_codeSignatureTrigger;
	int m_codeSignatureRequest = -1, m_codeSignatureTriggerKind = 1;
	quint64 m_codeSignatureToken = 0;
	bool m_codeSignatureActive = false, m_codeSignatureRetrigger = false;
	int m_codeQuickInfoRequest = -1;
	quint64 m_codeQuickInfoToken = 0;
	bool m_codeQuickInfoPersistent = false;
	QProgressDialog* m_codeFormattingProgress = nullptr;
	QString m_codeFormattingActivityId;
	int m_codeFormattingRequest = -1;
	quint64 m_codeFormattingToken = 0;
	QString m_codeSearchActivityId;
	bool m_codeProjectReplaceRunning = false;
	bool m_codeSaveRunning = false;
	QTabWidget* m_codeOutputTabs = nullptr;
	LevelMapDoorOptions m_levelMapDoorOptions;
	QCheckBox* m_textToSpeech = nullptr;
	QCheckBox* m_aiFreeMode = nullptr;
	QCheckBox* m_aiCloudConnectors = nullptr;
	QCheckBox* m_aiAgenticWorkflows = nullptr;
	QString m_setupActivityId;
	QString m_settingsActivityId;
	QString m_packageActivityId;
	QString m_compilerRunActivityId;
	QString m_buildPipelineActivityId;
	QThread* m_compilerRunThread = nullptr;
	QThread* m_buildPipelineThread = nullptr;
	QVector<GameInstallationDetectionCandidate> m_detectedInstallationCandidates;
	IdTechPaletteResolution m_paletteResolution;
	bool m_paletteResolutionValid = false;
	std::atomic_bool m_packageExtractionCancelRequested {false};
	// Extraction pumps the event loop so its Cancel button stays clickable;
	// while it runs, anything that would replace or close the archive waits.
	bool m_packageExtractionRunning = false;
	QString m_packageExtractionTaskId;
	bool m_statusChipRefreshScheduled = false;
	QString m_compilerChipKey;
	qint64 m_compilerChipProbedAtMs = -1;
	int m_compilerChipAvailable = 0;
	int m_compilerChipTotal = 0;
	// The last pipeline run, kept so its per-stage results survive the next
	// refresh of the Build page while the same pipeline and input are shown.
	BuildPipelineResult m_lastBuildResult;
	QString m_lastBuildKey;
	bool m_themeRefreshScheduled = false;
	bool m_workspaceSearchScheduled = false;
	bool m_buildingUi = false;
	std::atomic_bool m_compilerRunCancelRequested = false;
	std::atomic_bool m_buildPipelineCancelRequested = false;
};

} // namespace vibestudio
