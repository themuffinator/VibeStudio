#pragma once

// Edit Map with AI: an instruction in, edits to the open map out. The text
// model proposes actions (core/level_ai_edit.h); each is checked against the
// map and listed with what it does and what, if anything, stops it, and only
// the ones left checked are applied, each as its own undo step. A saved
// proposal loads and applies with no model at all. The shell supplies the
// map, consent, the edit itself, and Activity through hooks, so the dialog
// never changes shell state on its own.

#include "core/ai_transport.h"
#include "core/level_ai_edit.h"

#include <QCoreApplication>
#include <QDialog>

#include <functional>
#include <memory>

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTabWidget;
class QToolButton;
class QTreeWidget;

namespace vibestudio {

struct LevelAiEditDialogHooks {
	// The map open in Levels, or null when none is.
	std::function<const LevelMapDocument*()> document;
	// The project folder, which the map summary shortens to <project>.
	std::function<QString()> projectRoot;
	// Asks before a request leaves the machine; false keeps it here.
	std::function<bool(const QString& connectorId, const QString& displayName, const QString& endpoint, const AiHttpRequest& request)> confirmSend;
	// Applies the enabled actions to the open map and refreshes the editor.
	std::function<LevelAiEditApplyReport(const LevelAiEditProposal& proposal)> apply;
	// The Activity panel: a task starts, then ends.
	std::function<QString(const QString& title, const QString& detail)> beginTask;
	std::function<void(const QString& task, bool succeeded, bool cancelled, const QString& summary)> endTask;
	// Where proposals are saved and loaded.
	std::function<QString()> proposalFolder;
};

class LevelAiEditDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioLevelAiEditDialog)

public:
	LevelAiEditDialog(QWidget* parent, LevelAiEditDialogHooks hooks);
	~LevelAiEditDialog() override;

	void setInstruction(const QString& instruction);
	// Sends the instruction and the map summary to the text model.
	void ask();
	void cancelRequest();
	// Reads a saved proposal and checks it against the open map.
	bool loadProposal(const QString& path, QString* error = nullptr);
	// Lists a proposal for review, checked against the open map.
	void showProposal(const LevelAiEditProposal& proposal);
	// Applies the checked actions through the hooks, once per proposal.
	void applyChecked();
	[[nodiscard]] bool busy() const;
	[[nodiscard]] const LevelAiEditProposal& proposal() const;
	// Re-reads the open map, its selection, and the AI settings.
	void refreshStatus();

protected:
	void reject() override;
	void keyPressEvent(QKeyEvent* event) override;
	void changeEvent(QEvent* event) override;

private:
	[[nodiscard]] const LevelMapDocument* editableDocument() const;
	[[nodiscard]] LevelAiEditProposal checkedProposal() const;
	void setBusy(bool busy, const QString& status);
	void finishTask(bool succeeded, bool cancelled, const QString& summary);
	void showRequestPreview();
	void chooseProposalFile();
	void saveProposal();
	void updateButtons();

	LevelAiEditDialogHooks m_hooks;
	std::unique_ptr<AiChatClient> m_client;
	LevelAiEditProposal m_proposal;
	QString m_proposalMap;
	quint64 m_proposalRevision = 0;
	QString m_task;
	bool m_busy = false;
	bool m_applied = false;

	QLabel* m_mapStatus = nullptr;
	QPlainTextEdit* m_instruction = nullptr;
	QLabel* m_connectionStatus = nullptr;
	QPushButton* m_previewRequest = nullptr;
	QPushButton* m_loadProposal = nullptr;
	QPushButton* m_ask = nullptr;
	QPushButton* m_cancel = nullptr;
	QLabel* m_status = nullptr;
	QProgressBar* m_progress = nullptr;
	QLabel* m_summary = nullptr;
	QTreeWidget* m_actions = nullptr;
	QPushButton* m_apply = nullptr;
	QPushButton* m_saveProposal = nullptr;
	QToolButton* m_details = nullptr;
	QTabWidget* m_detailTabs = nullptr;
	QPlainTextEdit* m_contextText = nullptr;
	QPlainTextEdit* m_answerText = nullptr;
	QPlainTextEdit* m_resultText = nullptr;
};

} // namespace vibestudio
