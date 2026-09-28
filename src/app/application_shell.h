#pragma once

#include "core/advanced_studio.h"
#include "core/asset_tools.h"
#include "core/bsp_inspect.h"
#include "core/build_pipeline.h"
#include "core/compiler_runner.h"
#include "core/deflate.h"
#include "core/document_watch.h"
#include "core/entity_definitions.h"
#include "core/idtech_image.h"
#include "core/level_map.h"
#include "core/model_mesh.h"
#include "core/operation_state.h"
#include "core/package_archive.h"
#include "core/package_compare.h"
#include "core/package_staging.h"
#include "core/studio_settings.h"

#include <QHash>
#include <QMainWindow>
#include <QStyle>
#include <QVector>
#include <QStringList>

#include <atomic>
#include <memory>

class QAbstractButton;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QPlainTextEdit;
class QProcess;
class QProgressBar;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QTextEdit;
class QThread;
class QToolBar;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace vibestudio {

class ActivityTimelineChart;
class CommandPaletteDialog;
class CompositionChart;
class DetailDrawer;
class ElidedLabel;
class EmptyStateView;
class ImagePreviewView;
class LoadingPane;
class MapViewport;
class ModeRail;
class ModelViewport;
class PageHeader;
class PaletteSwatchView;
class PipelineChart;
class StudioCommandRegistry;
class StudioSyntaxHighlighter;
class WaveformView;

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
	explicit ApplicationShell(QWidget* parent = nullptr);
	~ApplicationShell() override;

	// Exercises every work surface once so an offscreen CI run touches the real
	// widgets, refresh paths, and paint code rather than only the CLI.
	void runSelfTest();

	// Opens a map, package, project folder, or text file the same way a drop
	// onto the window would, so command-line paths behave like drag and drop.
	void openPathFromCommandLine(const QString& path);

	// Renders every work surface to <directory>/NN-<mode>.png. Documentation
	// screenshots and visual review use it to see the real widgets without a
	// person driving the window. Returns the files written.
	QStringList captureUiSnapshots(const QString& directory);

protected:
	void closeEvent(QCloseEvent* event) override;
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dropEvent(QDropEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void buildUi();
	void buildCommands();
	void buildMenuBar();
	void buildToolBar();
	void buildStatusBar();
	QWidget* buildWorkspacePage();
	QWidget* buildLevelsPage();
	QWidget* buildModelsPage();
	QWidget* buildTexturesPage();
	QWidget* buildAudioPage();
	QWidget* buildPackagesPage();
	QWidget* buildCodePage();
	QWidget* buildShadersPage();
	QWidget* buildBuildPage();
	QWidget* buildSettingsPage();
	QWidget* buildSidePanel();
	void setMode(StudioMode mode);
	[[nodiscard]] StudioMode currentMode() const;
	void refreshModeAvailability();
	// Updates every page header's context line and swaps each work surface
	// between its empty state and its workbench to match what is open.
	void refreshSurfaceStates();
	// Restores every work-surface splitter to its built-in proportions, closes
	// the panels, and expands the rail.
	void resetLayout();
	void showCommandPalette();
	void runCommand(const QString& commandId);
	void refreshCommandEnablement();
	void refreshStatusChips();
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
	QString selectedGameInstallationId() const;
	int selectedDetectedInstallationIndex() const;
	void openPackageFile();
	void openPackageFolder();
	void closePackage();
	void loadPackagePath(const QString& path);
	void refreshPackageBrowser();
	void refreshWorkspaceContextPanels();
	void refreshProjectProblemsPanel();
	void refreshWorkspaceSearch();
	void scheduleWorkspaceSearch();
	void refreshChangedFilesPanel();
	void refreshProjectDependencyGraph();
	void refreshRecentActivityTimeline();
	void openLevelMapFile();
	void loadLevelMapPath(const QString& path);
	void refreshLevelMapWorkbench();
	void refreshLevelMapSelection();
	void refreshLevelMapViewport();
	void reloadEntityDefinitions();
	void chooseEntityDefinitionPath();
	void refreshEntityInspector();
	// Applies an inline edit from the entity property grid: a key's value or
	// a spawnflag check box.
	void applyEntityInspectorEdit(QTreeWidgetItem* item, int column);
	QString selectedLevelMapObjectSelector() const;
	void selectLevelMapObjectFromViewport(int selectionKind, int objectId);
	void syncLevelMapSelectionFromViewport(const QVector<LevelMapSelectionRef>& selection);
	void moveLevelMapSelectionFromViewport(double dx, double dy, double dz);
	void editSelectedLevelMapProperty();
	void moveSelectedLevelMapObject();
	void undoLevelMapEditFromUi();
	void redoLevelMapEditFromUi();
	void saveLevelMapAsFromUi();
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
	void filterTextureEntries();
	// Tiles show decoded thumbnails like idStudio's asset browser; List shows
	// paths and sizes. Thumbnails decode a few at a time in the background.
	void applyTextureViewMode();
	void fitTextureGrid();
	void queueTextureThumbnails();
	void generateTextureThumbnailBatch();
	void showSelectedTexture();
	void exportSelectedTexture();
	void refreshModelBrowser();
	void showSelectedModel();
	void exportSelectedModel();
	void refreshModelPlaybackControls();
	void refreshAudioBrowser();
	void showSelectedAudioEntry();
	void refreshCodeWorkspaceTree();
	void openCodeFile(const QString& path);
	void pollWatchedDocuments();
	void handleExternalDocumentChange(const DocumentChangeEvent& event);
	void registerWatchedDocument(const QString& path, DocumentWatchRole role, const QString& documentId = QString(), bool adoptContents = true);
	void reportPreviousSessionCrash();
	void openSelectedCodeFile();
	void saveCodeFile();
	void refreshCodeDiagnostics();
	void runCodeFindReplace();
	void refreshBuildSurface();
	void runSelectedBuildPipeline();
	void cancelBuildPipeline();
	void finishBuildPipeline(const BuildPipelineResult& result, const QString& taskId, const QString& projectPath);
	void copyBuildPipelineCommands();
	void inspectCompiledArtifacts();
	void launchConfiguredGame();
	QString selectedWorkspaceFilePath() const;
	QString selectedWorkspaceVirtualPath() const;
	void revealSelectedWorkspacePath();
	void copySelectedWorkspaceVirtualPath();
	void refreshPackageTree();
	void refreshPackageCompositionSummary();
	void refreshPackageStagingSummary();
	void refreshPackageEntryDetails(const QString& virtualPath);
	void filterPackageEntries();
	// Shows one package folder's contents in the entry list, the way a file
	// browser does. `selectPath` is selected once the folder is listed.
	void navigatePackageFolder(const QString& folder, const QString& selectPath = QString(), bool recordHistory = true);
	void stepPackageFolderHistory(int delta);
	void rebuildPackageBreadcrumb();
	void refreshCompilerPipelineSummary();
	QString selectedCompilerProfileId() const;
	void runSelectedCompilerProfile();
	void finishCompilerRun(const CompilerRunResult& result, const QString& taskId, const QString& projectPath);
	void copySelectedCompilerCliEquivalent();
	void copySelectedCompilerManifest();
	QString selectedPackageEntryPath() const;
	QStringList selectedPackageEntryPaths() const;
	QString selectedPackageTreeEntryPath() const;
	void selectPackageEntryPath(const QString& virtualPath);
	void selectPackageTreeEntryPath(const QString& virtualPath);
	void showPackageEntryContextMenu(const QPoint& position);
	void extractSelectedPackageEntries();
	void extractAllPackageEntries();
	void extractPackageEntriesToDirectory(const QStringList& virtualPaths, bool extractAll);
	void showPackageExtractionReport(const PackageExtractionReport& report);
	bool choosePackageStageResolution(const QString& title, PackageStageConflictResolution* resolution);
	void stagePackageAddFile();
	void stagePackageReplaceSelected();
	void stagePackageRenameSelected();
	void stagePackageDeleteSelected();
	void saveStagedPackageAs();
	void comparePackageWithFile();
	void activateRecentProject(QListWidgetItem* item);
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
	[[nodiscard]] IdTechPaletteResolution activePaletteResolution();
	[[nodiscard]] QString activePaletteId() const;
	void invalidatePaletteResolution();

	StudioSettings m_settings;
	OperationStateModel m_activity;
	PackageArchive m_packageArchive;
	PackageStagingModel m_packageStaging;
	LevelMapDocument m_levelMapDocument;
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
	QStackedWidget* m_modeStack = nullptr;
	QToolBar* m_toolBar = nullptr;
	LoadingPane* m_inspectorState = nullptr;
	DetailDrawer* m_inspectorDrawer = nullptr;
	LoadingPane* m_workspaceState = nullptr;
	DetailDrawer* m_workspaceDrawer = nullptr;
	ModeRail* m_modeRail = nullptr;
	QDockWidget* m_activityDock = nullptr;
	QDockWidget* m_inspectorDock = nullptr;
	QToolButton* m_activityToggle = nullptr;
	QToolButton* m_inspectorToggle = nullptr;
	QMenu* m_viewMenu = nullptr;
	// Page chrome keyed by StudioMode, and the splitters whose sizes persist.
	QHash<int, PageHeader*> m_pageHeaders;
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
	QLabel* m_projectChip = nullptr;
	QLabel* m_packageChip = nullptr;
	QLabel* m_installChip = nullptr;
	QLabel* m_compilerChip = nullptr;
	QLabel* m_aiChip = nullptr;
	QListWidget* m_projectProblems = nullptr;
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
	QLineEdit* m_levelMapName = nullptr;
	QComboBox* m_levelMapEngine = nullptr;
	QComboBox* m_levelMapCompilerProfile = nullptr;
	LoadingPane* m_levelMapState = nullptr;
	MapViewport* m_levelMapViewport = nullptr;
	QComboBox* m_levelMapProjection = nullptr;
	QComboBox* m_levelMapGrid = nullptr;
	QCheckBox* m_levelMapSnap = nullptr;
	QCheckBox* m_levelMapShowThings = nullptr;
	QCheckBox* m_levelMapShowSectors = nullptr;
	ElidedLabel* m_levelMapHover = nullptr;
	QLineEdit* m_entityDefinitionPath = nullptr;
	QLabel* m_entityDefinitionSummary = nullptr;
	QTreeWidget* m_entityInspector = nullptr;
	bool m_refreshingEntityInspector = false;
	EntityDefinitionCatalogue m_entityDefinitions;
	EntityValidationReport m_entityValidation;
	QListWidget* m_levelMapObjects = nullptr;
	QListWidget* m_levelMapStatistics = nullptr;
	QListWidget* m_levelMapView = nullptr;
	QListWidget* m_levelMapValidation = nullptr;
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
	QHash<QString, QIcon> m_textureThumbnails;
	QString m_textureThumbnailKey;
	QStringList m_textureThumbnailQueue;
	QTimer* m_textureThumbnailTimer = nullptr;
	QListWidget* m_textureEntries = nullptr;
	ImagePreviewView* m_texturePreview = nullptr;
	PaletteSwatchView* m_texturePalette = nullptr;
	QComboBox* m_texturePaletteChoice = nullptr;
	QComboBox* m_textureMipLevel = nullptr;
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
	QTreeWidget* m_modelDetails = nullptr;
	ModelMesh m_modelMesh;
	LoadingPane* m_modelState = nullptr;
	DetailDrawer* m_modelDrawer = nullptr;
	QListWidget* m_audioEntries = nullptr;
	WaveformView* m_audioWaveform = nullptr;
	QTreeWidget* m_audioDetails = nullptr;
	LoadingPane* m_audioState = nullptr;
	DetailDrawer* m_audioDrawer = nullptr;
	QTreeWidget* m_codeTree = nullptr;
	QPlainTextEdit* m_codeEditor = nullptr;
	StudioSyntaxHighlighter* m_codeHighlighter = nullptr;
	ElidedLabel* m_codeStatus = nullptr;
	QLineEdit* m_codeFind = nullptr;
	QLineEdit* m_codeReplace = nullptr;
	QListWidget* m_codeDiagnostics = nullptr;
	QString m_levelMapViewportKey;
	QString m_levelMapViewportSourceKey;
	bool m_syncingLevelMapSelection = false;
	QString m_codeFilePath;
	// External change detection for everything the shell holds open. The
	// watcher does no work of its own: this timer drives poll(), which is
	// the authoritative check.
	DocumentWatcher m_documentWatcher;
	QTimer* m_documentWatchTimer = nullptr;
	// Paths the user has told us to stop asking about for the current load.
	QSet<QString> m_ignoredExternalChanges;
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
	QListWidget* m_packageStagingSummary = nullptr;
	QTreeWidget* m_packageTree = nullptr;
	QListWidget* m_packageEntries = nullptr;
	DetailDrawer* m_packageDrawer = nullptr;
	ImagePreviewView* m_packageImagePreview = nullptr;
	QAbstractButton* m_packageStageAdd = nullptr;
	QAbstractButton* m_packageStageReplace = nullptr;
	QAbstractButton* m_packageStageRename = nullptr;
	QAbstractButton* m_packageStageDelete = nullptr;
	QAbstractButton* m_packageStageSaveAs = nullptr;
	QAbstractButton* m_packageCompare = nullptr;
	QComboBox* m_packageCompression = nullptr;
	QAbstractButton* m_packageExtractSelected = nullptr;
	QAbstractButton* m_packageExtractAll = nullptr;
	QAbstractButton* m_packageExtractCancel = nullptr;
	QAbstractButton* m_importDetectedInstall = nullptr;
	QAbstractButton* m_revealWorkspacePath = nullptr;
	QAbstractButton* m_copyWorkspaceVirtualPath = nullptr;
	QListWidget* m_compilerPipeline = nullptr;
	PipelineChart* m_compilerPipelineChart = nullptr;
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
	QAbstractButton* m_launchGame = nullptr;
	QListWidget* m_launchSummary = nullptr;
	QAbstractButton* m_compilerRunSelected = nullptr;
	QAbstractButton* m_compilerCopyCli = nullptr;
	QAbstractButton* m_compilerCopyManifest = nullptr;
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
	bool m_packageExtractionCancelRequested = false;
	bool m_themeRefreshScheduled = false;
	bool m_workspaceSearchScheduled = false;
	bool m_buildingUi = false;
	std::atomic_bool m_compilerRunCancelRequested = false;
	std::atomic_bool m_buildPipelineCancelRequested = false;
};

} // namespace vibestudio
