#pragma once

#include "core/operation_state.h"
#include "core/package_archive.h"
#include "core/compiler_runner.h"
#include "core/studio_settings.h"

#include <QMainWindow>

#include <atomic>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;
class QTabWidget;
class QTextEdit;
class QThread;
class QTreeWidget;
class QTreeWidgetItem;

namespace vibestudio {

class DetailDrawer;
class LoadingPane;

class ApplicationShell final : public QMainWindow {
public:
	explicit ApplicationShell(QWidget* parent = nullptr);
	~ApplicationShell() override;

private:
	void buildUi();
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
	void loadPackagePath(const QString& path);
	void refreshPackageBrowser();
	void refreshWorkspaceContextPanels();
	void refreshProjectProblemsPanel();
	void refreshWorkspaceSearch();
	void refreshChangedFilesPanel();
	void refreshProjectDependencyGraph();
	void refreshRecentActivityTimeline();
	QString selectedWorkspaceFilePath() const;
	QString selectedWorkspaceVirtualPath() const;
	void revealSelectedWorkspacePath();
	void copySelectedWorkspaceVirtualPath();
	void refreshPackageTree();
	void refreshPackageCompositionSummary();
	void refreshPackageEntryDetails(const QString& virtualPath);
	void filterPackageEntries();
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
	void extractSelectedPackageEntries();
	void extractAllPackageEntries();
	void extractPackageEntriesToDirectory(const QStringList& virtualPaths, bool extractAll);
	void showPackageExtractionReport(const PackageExtractionReport& report);
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
	void seedActivityCenter();
	void recordActivity(const QString& title, const QString& detail, const QString& source, OperationState state, const QString& resultSummary = QString(), const QStringList& warnings = {});
	void persistActivityTask(const QString& taskId);
	void updateSetupActivity();
	void refreshActivityCenter(const QString& preferredTaskId = QString());
	void refreshActivityDetails(const QString& taskId);
	QString selectedActivityTaskId() const;
	void cancelSelectedActivityTask();
	void clearFinishedActivityTasks();
	void refreshInspectorDrawerForSettings();
	void refreshInspectorDrawerForProject(const QString& path);
	void updateInspector();
	void updateInspectorForProject(const QString& path);

	StudioSettings m_settings;
	OperationStateModel m_activity;
	PackageArchive m_packageArchive;
	LoadingPane* m_inspectorState = nullptr;
	DetailDrawer* m_inspectorDrawer = nullptr;
	LoadingPane* m_workspaceState = nullptr;
	DetailDrawer* m_workspaceDrawer = nullptr;
	QListWidget* m_modeRail = nullptr;
	QListWidget* m_recentProjects = nullptr;
	QListWidget* m_gameInstallations = nullptr;
	QTextEdit* m_inspector = nullptr;
	QLabel* m_recentSummary = nullptr;
	QLabel* m_installSummary = nullptr;
	QLabel* m_packageSummary = nullptr;
	QListWidget* m_projectProblems = nullptr;
	QLineEdit* m_workspaceSearch = nullptr;
	QListWidget* m_workspaceSearchResults = nullptr;
	QListWidget* m_changedFiles = nullptr;
	QListWidget* m_dependencyGraph = nullptr;
	QListWidget* m_recentActivityTimeline = nullptr;
	QLineEdit* m_packageFilter = nullptr;
	LoadingPane* m_packageState = nullptr;
	QListWidget* m_packageComposition = nullptr;
	QTreeWidget* m_packageTree = nullptr;
	QListWidget* m_packageEntries = nullptr;
	DetailDrawer* m_packageDrawer = nullptr;
	QPushButton* m_packageExtractSelected = nullptr;
	QPushButton* m_packageExtractAll = nullptr;
	QPushButton* m_packageExtractCancel = nullptr;
	QPushButton* m_importDetectedInstall = nullptr;
	QPushButton* m_revealWorkspacePath = nullptr;
	QPushButton* m_copyWorkspaceVirtualPath = nullptr;
	QListWidget* m_compilerPipeline = nullptr;
	QPushButton* m_compilerRunSelected = nullptr;
	QPushButton* m_compilerCopyCli = nullptr;
	QPushButton* m_compilerCopyManifest = nullptr;
	QLabel* m_setupStatus = nullptr;
	QLabel* m_setupStep = nullptr;
	QLabel* m_setupNextAction = nullptr;
	QProgressBar* m_setupProgress = nullptr;
	QListWidget* m_setupSummary = nullptr;
	QPushButton* m_setupStartResume = nullptr;
	QPushButton* m_setupNext = nullptr;
	QPushButton* m_setupSkip = nullptr;
	QPushButton* m_setupComplete = nullptr;
	QPushButton* m_setupReset = nullptr;
	QLabel* m_activitySummary = nullptr;
	LoadingPane* m_activityState = nullptr;
	QListWidget* m_activityTasks = nullptr;
	DetailDrawer* m_activityDrawer = nullptr;
	QPushButton* m_activityCancel = nullptr;
	QPushButton* m_activityClearFinished = nullptr;
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
	QThread* m_compilerRunThread = nullptr;
	QVector<GameInstallationDetectionCandidate> m_detectedInstallationCandidates;
	bool m_packageExtractionCancelRequested = false;
	std::atomic_bool m_compilerRunCancelRequested = false;
};

} // namespace vibestudio
