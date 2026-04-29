#pragma once

#include "core/studio_settings.h"

#include <QMainWindow>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QTextEdit;

namespace vibestudio {

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
	void updateInspector();
	void updateInspectorForProject(const QString& path);

	StudioSettings m_settings;
	QListWidget* m_modeRail = nullptr;
	QListWidget* m_recentProjects = nullptr;
	QTextEdit* m_inspector = nullptr;
	QLabel* m_recentSummary = nullptr;
};

} // namespace vibestudio
