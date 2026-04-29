#pragma once

#include "core/operation_state.h"
#include "core/studio_settings.h"

#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;
class QTabWidget;
class QTextEdit;

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
	void refreshRecentProjects();
	void openProjectFolder();
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
	LoadingPane* m_inspectorState = nullptr;
	DetailDrawer* m_inspectorDrawer = nullptr;
	QListWidget* m_modeRail = nullptr;
	QListWidget* m_recentProjects = nullptr;
	QTextEdit* m_inspector = nullptr;
	QLabel* m_recentSummary = nullptr;
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
	QCheckBox* m_reducedMotion = nullptr;
	QCheckBox* m_textToSpeech = nullptr;
	QString m_setupActivityId;
	QString m_settingsActivityId;
};

} // namespace vibestudio
