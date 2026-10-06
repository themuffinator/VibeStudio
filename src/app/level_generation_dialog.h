#pragma once

// The Level Generator: a prompt and options in, a sealed level out, planned
// by the rules or by the configured text model (core/level_generation.h).
// The layout preview, plan, and notes are reviewed before anything is opened
// or written; the shell supplies consent, the editor, and activity through
// hooks, so the dialog never edits shell state itself.

#include "core/ai_transport.h"
#include "core/level_generation.h"

#include <QCoreApplication>
#include <QDialog>
#include <QImage>

#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTabWidget;
class QToolButton;

namespace vibestudio {

struct LevelGenerationDialogHooks {
	// The game the open map or project targets, or empty.
	std::function<QString()> defaultGame;
	// Texture names the open package and project offer, for matching roles.
	std::function<QStringList(const QString& game)> availableTextures;
	// Asks before a request leaves the machine; false keeps it here.
	std::function<bool(const QString& connectorId, const QString& displayName, const QString& endpoint, const AiHttpRequest& request)> confirmSend;
	// Opens the result in the Levels editor as a new, unsaved map.
	std::function<bool(const LevelGenerationResult& result, QString* error)> openInLevels;
	// The Activity panel: a task starts, then ends.
	std::function<QString(const QString& title, const QString& detail)> beginTask;
	std::function<void(const QString& task, bool succeeded, bool cancelled, const QString& summary)> endTask;
	// Where Save As starts.
	std::function<QString()> saveFolder;
};

class LevelGenerationDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioLevelGenerationDialog)

public:
	LevelGenerationDialog(QWidget* parent, LevelGenerationDialogHooks hooks);
	~LevelGenerationDialog() override;

	void setPrompt(const QString& prompt);
	// Builds from the controls: the rules' plan, or the text model's.
	void generate();
	void cancelGeneration();
	[[nodiscard]] bool busy() const;
	[[nodiscard]] const LevelGenerationResult& result() const;
	// Re-reads the AI settings into the planner status line.
	void refreshPlannerStatus();

protected:
	void resizeEvent(QResizeEvent* event) override;
	void reject() override;
	void keyPressEvent(QKeyEvent* event) override;

private:
	struct Work;

	LevelGenerationSpec specFromControls() const;
	void askModel(const LevelGenerationSpec& spec, const AiTextConnection& connection, const QString& previousAnswer, const QStringList& problems, int attempt);
	void build(const LevelGenerationSpec& spec, const LevelSemanticPlan* plan, const QStringList& planWarnings);
	void showResult(const LevelGenerationResult& result, const QStringList& planWarnings);
	void setBusy(bool busy, const QString& status);
	void finishTask(bool succeeded, bool cancelled, const QString& summary);
	void updatePreview();
	void showRequestPreview();
	void saveAs();
	void savePlan();
	void openInLevels();

	LevelGenerationDialogHooks m_hooks;
	std::shared_ptr<Work> m_work;
	std::unique_ptr<AiChatClient> m_client;
	LevelGenerationResult m_result;
	QImage m_preview;
	QString m_task;
	bool m_busy = false;

	QPlainTextEdit* m_prompt = nullptr;
	QComboBox* m_game = nullptr;
	QComboBox* m_mode = nullptr;
	QComboBox* m_theme = nullptr;
	QComboBox* m_verticality = nullptr;
	QComboBox* m_liquid = nullptr;
	QComboBox* m_monsters = nullptr;
	QSpinBox* m_rooms = nullptr;
	QSpinBox* m_players = nullptr;
	QSpinBox* m_seed = nullptr;
	QCheckBox* m_seedFromPrompt = nullptr;
	QCheckBox* m_projectTextures = nullptr;
	QRadioButton* m_rulesPlanner = nullptr;
	QRadioButton* m_modelPlanner = nullptr;
	QLabel* m_plannerStatus = nullptr;
	QPushButton* m_previewRequest = nullptr;
	QPushButton* m_generate = nullptr;
	QPushButton* m_newSeed = nullptr;
	QPushButton* m_cancel = nullptr;
	QLabel* m_status = nullptr;
	QProgressBar* m_progress = nullptr;
	QLabel* m_previewImage = nullptr;
	QLabel* m_legend = nullptr;
	QLabel* m_summary = nullptr;
	QToolButton* m_details = nullptr;
	QTabWidget* m_detailTabs = nullptr;
	QPlainTextEdit* m_planText = nullptr;
	QPlainTextEdit* m_notesText = nullptr;
	QPlainTextEdit* m_mapText = nullptr;
	QPushButton* m_open = nullptr;
	QPushButton* m_save = nullptr;
	QPushButton* m_savePlan = nullptr;
};

} // namespace vibestudio
