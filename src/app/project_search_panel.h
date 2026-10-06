#pragma once

#include "core/project_text_search.h"
#include "core/language_references.h"

#include <QWidget>

#include <functional>
#include <memory>
#include <optional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTimer;

namespace vibestudio {

class ProjectSearchPanel final : public QWidget {
	Q_OBJECT
public:
	explicit ProjectSearchPanel(QWidget* parent = nullptr);
	~ProjectSearchPanel() override;
	void setRootPath(const QString& path);
	void startSearch();
	void startSearch(const AssetTextSearchRequest& request);
	void cancel();
	// Protocol phase followed by background preparation of source previews.
	// The token prevents an obsolete reply from replacing a newer search.
	quint64 beginReferences(const QString& root, const QString& symbol, const QString& provider, std::function<void()> cancel, bool rename = false);
	void finishReferences(quint64 token, LanguageReferenceRequest request);
	void finishRename(quint64 token, LanguageRenameRequest request);
	quint64 beginCodeActions(const QString& root, const QString& provider, std::function<void()> cancel);
	void finishLanguageEdits(quint64 token, LanguageWorkspaceEditRequest request);
	bool semanticRequestCurrent(quint64 token) const { return m_waitingReferences && token == m_generation; }
	void invalidateReferences(const QString& reason);
	[[nodiscard]] bool referencesActive() const;
	// Apply is an explicit action over the visible, current preview.
	void applyPreview();
	[[nodiscard]] bool busy() const;
	[[nodiscard]] const AssetTextSearchReport& report() const;
	[[nodiscard]] QLineEdit* findField() const;
	[[nodiscard]] QLineEdit* replaceField() const;
	[[nodiscard]] QListWidget* resultsList() const;

	std::function<void(const AssetTextMatch&)> openMatch;
	// Captured on the GUI thread before the worker starts.
	std::function<QVector<AssetTextBuffer>()> captureBuffers;
	// Finish deferred editor edits on the GUI thread while the progress dialog
	// still protects the batch. The host revalidates every snapshot first.
	std::function<void(AssetTextSearchReport&)> applyBuffers;
	// Runs on the GUI thread immediately before writing; returns why a batch
	// is blocked (changed snapshots, a different project), or an empty string.
	std::function<QString(const AssetTextSearchReport&)> beforeApply;
	std::function<void(bool applying)> operationStarted;
	std::function<void(const AssetTextSearchReport&)> operationFinished;

private:
	struct Work;
	quint64 beginLanguageRequest(const QString& root, const QString& symbol, const QString& provider, std::function<void()> cancel, bool rename, const QString& actionTitle);
	void launch(AssetTextSearchRequest request, bool applying, std::optional<LanguageReferenceRequest> references = {}, std::optional<LanguageWorkspaceEditRequest> edits = {});
	void setSearchControlsEnabled(bool enabled);
	void invalidatePreview();
	void updateButtons();
	void presentReport();
	void showMatchDetails();

	QString m_root;
	AssetTextSearchReport m_report;
	std::shared_ptr<Work> m_work;
	quint64 m_generation = 0;
	bool m_previewCurrent = false;
	bool m_waitingReferences = false;
	std::function<void()> m_cancelReferences;
	QLineEdit* m_find = nullptr;
	QLineEdit* m_replace = nullptr;
	QCheckBox* m_replaceEnabled = nullptr;
	QCheckBox* m_caseSensitive = nullptr;
	QCheckBox* m_wholeWords = nullptr;
	QLineEdit* m_include = nullptr;
	QLineEdit* m_exclude = nullptr;
	QLabel* m_context = nullptr;
	QLabel* m_status = nullptr;
	QProgressBar* m_progress = nullptr;
	QPushButton* m_search = nullptr;
	QPushButton* m_cancel = nullptr;
	QPushButton* m_apply = nullptr;
	QListWidget* m_results = nullptr;
	QPlainTextEdit* m_details = nullptr;
	QTimer* m_progressTimer = nullptr;
};

} // namespace vibestudio
