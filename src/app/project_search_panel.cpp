#include "app/project_search_panel.h"

#include "app/studio_icons.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <atomic>
#include <algorithm>

namespace vibestudio {
namespace {

// Keep the editor unavailable until the writer has stopped, including after
// Escape or the title-bar close button asks it to cancel.
class ReplacementProgress final : public QDialog {
public:
	using QDialog::QDialog;
	std::function<void()> cancelWork;
	void reject() override { if (cancelWork) { cancelWork(); } }
};

QStringList patterns(const QString& text)
{
	QStringList result;
	for (const auto& part : text.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
		if (!part.trimmed().isEmpty()) { result << part.trimmed(); }
	}
	return result;
}

} // namespace

struct ProjectSearchPanel::Work {
	std::atomic_bool cancel {false};
	std::atomic_int files {0};
	std::atomic_int matches {0};
	bool applying = false;
	bool references = false;
	QString cancellationReason;
	AssetTextSearchReport result;
};

ProjectSearchPanel::ProjectSearchPanel(QWidget* parent) : QWidget(parent)
{
	setObjectName(QStringLiteral("projectSearchPanel"));
	setAccessibleName(tr("Project search and replacement preview"));
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	auto* fields = new QGridLayout;
	m_find = new QLineEdit;
	m_find->setObjectName(QStringLiteral("codeProjectFind"));
	m_find->setAccessibleName(tr("Find text"));
	m_find->setClearButtonEnabled(true);
	m_find->setPlaceholderText(tr("Find in project files and open documents"));
	auto* findLabel = new QLabel(tr("&Find"));
	findLabel->setBuddy(m_find);
	m_search = new QPushButton(studioIcon(QStringLiteral("search")), tr("Search"));
	m_search->setObjectName(QStringLiteral("projectSearchStart"));
	m_search->setAccessibleName(tr("Search project files and open documents"));
	m_search->setAutoDefault(false);
	fields->addWidget(findLabel, 0, 0);
	fields->addWidget(m_find, 0, 1);
	fields->addWidget(m_search, 0, 2);
	m_replaceEnabled = new QCheckBox(tr("&Replace with"));
	m_replaceEnabled->setObjectName(QStringLiteral("projectReplaceEnabled"));
	m_replaceEnabled->setAccessibleName(tr("Preview replacements"));
	m_replaceEnabled->setToolTip(tr("Enable replacement preview. An empty replacement deletes the matched text."));
	m_replace = new QLineEdit;
	m_replace->setObjectName(QStringLiteral("projectReplaceText"));
	m_replace->setAccessibleName(tr("Replace text"));
	m_replace->setPlaceholderText(tr("Empty text removes matches"));
	m_replace->setClearButtonEnabled(true);
	m_replace->setEnabled(false);
	m_apply = new QPushButton(tr("Apply Preview…"));
	m_apply->setObjectName(QStringLiteral("projectSearchApply"));
	m_apply->setAccessibleName(tr("Apply the reviewed file and document replacements"));
	m_apply->setToolTip(tr("Apply exactly the replacements shown here. Open documents keep undoable edits; unopened files are saved. Changed snapshots block the batch."));
	m_apply->setAutoDefault(false);
	fields->addWidget(m_replaceEnabled, 1, 0);
	fields->addWidget(m_replace, 1, 1);
	fields->addWidget(m_apply, 1, 2);
	fields->setColumnStretch(1, 1);
	layout->addLayout(fields);
	auto* options = new QHBoxLayout;
	m_caseSensitive = new QCheckBox(tr("Match &case"));
	m_caseSensitive->setObjectName(QStringLiteral("projectSearchCase"));
	m_caseSensitive->setAccessibleName(tr("Case-sensitive project search"));
	m_wholeWords = new QCheckBox(tr("Whole &words"));
	m_wholeWords->setObjectName(QStringLiteral("projectSearchWords"));
	m_wholeWords->setAccessibleName(tr("Match whole identifiers"));
	auto* filterToggle = new QToolButton;
	filterToggle->setText(tr("File filters"));
	filterToggle->setAccessibleName(tr("Show file include and exclude filters"));
	filterToggle->setCheckable(true);
	filterToggle->setObjectName(QStringLiteral("projectSearchFilters"));
	options->addWidget(m_caseSensitive);
	options->addWidget(m_wholeWords);
	options->addWidget(filterToggle);
	options->addStretch();
	m_cancel = new QPushButton(tr("Cancel"));
	m_cancel->setObjectName(QStringLiteral("projectSearchCancel"));
	m_cancel->setAccessibleName(tr("Cancel project search"));
	m_cancel->setAutoDefault(false);
	options->addWidget(m_cancel);
	layout->addLayout(options);
	auto* filters = new QWidget;
	auto* filterLayout = new QGridLayout(filters);
	filterLayout->setContentsMargins(0, 0, 0, 0);
	m_include = new QLineEdit;
	m_include->setObjectName(QStringLiteral("projectSearchInclude"));
	m_include->setAccessibleName(tr("Include file patterns"));
	m_include->setPlaceholderText(tr("*.qc; scripts/*.cfg"));
	m_exclude = new QLineEdit;
	m_exclude->setObjectName(QStringLiteral("projectSearchExclude"));
	m_exclude->setAccessibleName(tr("Exclude file patterns"));
	m_exclude->setPlaceholderText(tr("generated/*; *_backup.cfg"));
	m_include->setToolTip(tr("Semicolon-separated wildcards. A pattern with a slash matches the relative path; otherwise it matches the filename."));
	m_exclude->setToolTip(m_include->toolTip());
	auto* includeLabel = new QLabel(tr("&Include"));
	includeLabel->setBuddy(m_include);
	auto* excludeLabel = new QLabel(tr("&Exclude"));
	excludeLabel->setBuddy(m_exclude);
	filterLayout->addWidget(includeLabel, 0, 0);
	filterLayout->addWidget(m_include, 0, 1);
	filterLayout->addWidget(excludeLabel, 1, 0);
	filterLayout->addWidget(m_exclude, 1, 1);
	filterLayout->setColumnStretch(1, 1);
	filters->hide();
	connect(filterToggle, &QToolButton::toggled, filters, &QWidget::setVisible);
	layout->addWidget(filters);
	m_context = new QLabel;
	m_context->setTextFormat(Qt::PlainText);
	m_context->setWordWrap(true);
	m_context->setAccessibleName(tr("Search project root"));
	layout->addWidget(m_context);
	m_status = new QLabel(tr("Search project files and open documents to see matches here."));
	m_status->setObjectName(QStringLiteral("projectSearchStatus"));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setAccessibleName(tr("Project search status"));
	layout->addWidget(m_status);
	m_progress = new QProgressBar;
	m_progress->setAccessibleName(tr("Project search progress"));
	m_progress->setTextVisible(false);
	m_progress->hide();
	layout->addWidget(m_progress);
	auto* split = new QSplitter;
	split->setAccessibleName(tr("Search results and replacement details"));
	m_results = new QListWidget;
	// File paths, source coordinates and code retain their reading order in RTL layouts.
	m_results->setLayoutDirection(Qt::LeftToRight);
	m_results->setObjectName(QStringLiteral("codeSearchResults"));
	m_results->setAccessibleName(tr("Project search results"));
	m_results->setAccessibleDescription(tr("Activate a match to open its document and line. Selecting a match shows its source and before and after text."));
	m_results->setTextElideMode(Qt::ElideRight);
	m_details = new QPlainTextEdit;
	m_details->setObjectName(QStringLiteral("projectSearchDetails"));
	m_details->setReadOnly(true);
	m_details->setAccessibleName(tr("Replacement preview and search details"));
	split->addWidget(m_results);
	split->addWidget(m_details);
	split->setStretchFactor(0, 3);
	split->setStretchFactor(1, 2);
	layout->addWidget(split, 1);
	connect(m_results, &QListWidget::currentRowChanged, this, [this]() { showMatchDetails(); });
	connect(m_results, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
		const int row = m_results->row(item);
		if (openMatch && row >= 0 && row < m_report.matches.size()) { openMatch(m_report.matches.at(row)); }
	});
	connect(m_search, &QPushButton::clicked, this, [this]() { startSearch(); });
	connect(m_find, &QLineEdit::returnPressed, this, [this]() { startSearch(); });
	connect(m_replace, &QLineEdit::returnPressed, this, [this]() { startSearch(); });
	connect(m_cancel, &QPushButton::clicked, this, &ProjectSearchPanel::cancel);
	connect(m_apply, &QPushButton::clicked, this, &ProjectSearchPanel::applyPreview);
	for (auto* field : {m_find, m_replace, m_include, m_exclude}) {
		connect(field, &QLineEdit::textChanged, this, [this]() { invalidatePreview(); });
	}
	for (auto* option : {m_replaceEnabled, m_caseSensitive, m_wholeWords}) {
		connect(option, &QCheckBox::toggled, this, [this]() { invalidatePreview(); });
	}
	connect(m_replaceEnabled, &QCheckBox::toggled, m_replace, &QWidget::setEnabled);
	m_progressTimer = new QTimer(this);
	m_progressTimer->setInterval(100);
	connect(m_progressTimer, &QTimer::timeout, this, [this]() {
		if (!m_work || m_work->cancel) { return; }
		m_status->setText(m_work->references ? tr("Preparing language result previews… %1 files, %2 locations.").arg(m_work->files.load()).arg(m_work->matches.load())
			: m_work->applying ? tr("Applying reviewed replacements…")
			: tr("Searching files and open documents… %1 scanned, %2 matches.").arg(m_work->files.load()).arg(m_work->matches.load()));
	});
	updateButtons();
}

ProjectSearchPanel::~ProjectSearchPanel()
{
	if (m_work) { m_work->cancel = true; }
	if (m_cancelReferences) { m_cancelReferences(); }
}

bool ProjectSearchPanel::busy() const { return m_waitingReferences || static_cast<bool>(m_work); }
bool ProjectSearchPanel::referencesActive() const { return m_waitingReferences || (m_work && m_work->references); }
const AssetTextSearchReport& ProjectSearchPanel::report() const { return m_report; }
QLineEdit* ProjectSearchPanel::findField() const { return m_find; }
QLineEdit* ProjectSearchPanel::replaceField() const { return m_replace; }
QListWidget* ProjectSearchPanel::resultsList() const { return m_results; }

void ProjectSearchPanel::setRootPath(const QString& path)
{
	const QString root = path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
	if (m_root == root) { return; }
	m_root = root;
	++m_generation;
	cancel();
	m_report = {};
	m_results->clear();
	m_details->clear();
	m_context->setText(root.isEmpty() ? tr("Open a project folder to search its files.") : QDir::toNativeSeparators(root));
	m_status->setText(tr("Project changed. Search to refresh the results."));
	invalidatePreview();
}

void ProjectSearchPanel::invalidatePreview()
{
	m_previewCurrent = false;
	if (!busy() && !m_report.rootPath.isEmpty()) { m_status->setText(tr("Search options changed. Search again to refresh the preview.")); }
	updateButtons();
}

void ProjectSearchPanel::updateButtons()
{
	m_search->setEnabled(!busy() && !m_root.isEmpty() && !m_find->text().isEmpty());
	m_cancel->setEnabled(m_waitingReferences || (m_work && !m_work->cancel));
	m_apply->setEnabled(!busy() && m_previewCurrent && m_report.canApply());
}

void ProjectSearchPanel::startSearch()
{
	if (busy()) { return; }
	AssetTextSearchRequest request;
	request.rootPath = m_root;
	request.findText = m_find->text();
	request.replace = m_replaceEnabled->isChecked();
	request.replaceText = m_replace->text();
	request.caseSensitive = m_caseSensitive->isChecked();
	request.wholeWords = m_wholeWords->isChecked();
	request.includeGlobs = patterns(m_include->text());
	request.excludeGlobs = patterns(m_exclude->text());
	launch(request, false);
}

void ProjectSearchPanel::startSearch(const AssetTextSearchRequest& request)
{
	if (busy()) { return; }
	setRootPath(request.rootPath);
	m_find->setText(request.findText);
	m_replaceEnabled->setChecked(request.replace);
	m_replace->setText(request.replaceText);
	m_caseSensitive->setChecked(request.caseSensitive);
	m_wholeWords->setChecked(request.wholeWords);
	m_include->setText(request.includeGlobs.join(QStringLiteral("; ")));
	m_exclude->setText(request.excludeGlobs.join(QStringLiteral("; ")));
	launch(request, false);
}

void ProjectSearchPanel::cancel()
{
	if (referencesActive()) { invalidateReferences(tr("Language request cancelled.")); return; }
	if (!m_work) { return; }
	m_work->cancel = true;
	m_status->setText(tr("Cancelling… Completed writes, if any, will be listed."));
	updateButtons();
}

void ProjectSearchPanel::setSearchControlsEnabled(bool enabled)
{
	for (QWidget* control : QVector<QWidget*> {m_find, m_replace, m_include, m_exclude, m_replaceEnabled, m_caseSensitive, m_wholeWords}) { control->setEnabled(enabled); }
	m_replace->setEnabled(enabled && m_replaceEnabled->isChecked());
}

quint64 ProjectSearchPanel::beginReferences(const QString& root, const QString& symbol, const QString& provider, std::function<void()> cancel, bool rename)
{
	return beginLanguageRequest(root, symbol, provider, std::move(cancel), rename, {});
}

quint64 ProjectSearchPanel::beginCodeActions(const QString& root, const QString& provider, std::function<void()> cancel)
{
	return beginLanguageRequest(root, {}, provider, std::move(cancel), false, tr("Code Actions"));
}

quint64 ProjectSearchPanel::beginLanguageRequest(const QString& root, const QString& symbol, const QString& provider, std::function<void()> cancel, bool rename, const QString& actionTitle)
{
	if (busy()) { return 0; }
	setRootPath(root); ++m_generation;
	m_report = {}; m_report.rootPath = m_root; m_report.findText = symbol.left(256); m_report.referenceProvider = provider.isEmpty() ? tr("Language server") : provider;
	m_report.semanticRename = rename; m_report.codeActionTitle = actionTitle;
	m_previewCurrent = false; m_find->setText(m_report.findText); m_replaceEnabled->setChecked(false); m_search->setText(tr("Search Text"));
	m_waitingReferences = true; m_cancelReferences = std::move(cancel);
	m_results->clear(); m_details->clear(); setSearchControlsEnabled(false); updateButtons();
	m_progress->setRange(0, 0); m_progress->show();
	m_status->setText((!actionTitle.isEmpty() ? tr("Finding code actions with %1…") : rename ? tr("Preparing rename with %1…") : tr("Finding references with %1…")).arg(m_report.referenceProvider));
	if (operationStarted) { operationStarted(false); }
	return m_generation;
}

void ProjectSearchPanel::finishReferences(quint64 token, LanguageReferenceRequest request)
{
	if (!m_waitingReferences || token != m_generation) { return; }
	m_waitingReferences = false; m_cancelReferences = {};
	request.rootPath = m_root; request.symbol = m_report.findText; request.provider = m_report.referenceProvider;
	launch({}, false, std::move(request));
}

void ProjectSearchPanel::finishRename(quint64 token, LanguageRenameRequest request)
{
	finishLanguageEdits(token, languageRenameWorkspaceRequest(request));
}

void ProjectSearchPanel::finishLanguageEdits(quint64 token, LanguageWorkspaceEditRequest request)
{
	if (!semanticRequestCurrent(token)) { return; }
	m_waitingReferences = false; m_cancelReferences = {};
	request.rootPath = m_report.rootPath; request.provider = m_report.referenceProvider;
	m_report.findText = request.findText; m_report.replaceText = request.replaceText;
	m_find->setText(request.rename ? request.findText : QString()); m_replace->setText(request.rename ? request.replaceText : QString()); m_replaceEnabled->setChecked(request.rename);
	launch({}, false, {}, std::move(request));
}

void ProjectSearchPanel::invalidateReferences(const QString& reason)
{
	if (m_waitingReferences) {
		m_waitingReferences = false; ++m_generation;
		const auto cancel = std::move(m_cancelReferences); m_cancelReferences = {};
		if (cancel) { cancel(); }
		m_report.complete = false; m_report.cancelled = true; m_report.saveState = QStringLiteral("cancelled"); m_report.warnings << reason;
		m_progress->hide(); setSearchControlsEnabled(true); presentReport(); updateButtons();
		if (operationFinished) { operationFinished(m_report); }
	} else if (m_work && m_work->references) {
		m_work->cancel = true; m_work->cancellationReason = reason; m_status->setText(reason); updateButtons();
	} else if (!busy() && m_report.hasLanguageEdits() && m_report.dryRun && m_previewCurrent) {
		m_previewCurrent = false; m_status->setText(reason); updateButtons();
	}
}

void ProjectSearchPanel::applyPreview()
{
	if (busy() || !m_previewCurrent || !m_report.canApply()) { return; }
	if (!applyBuffers && std::any_of(m_report.changes.cbegin(), m_report.changes.cend(), [](const auto& change) { return !change.bufferId.isEmpty(); })) {
		m_status->setText(tr("Open-document replacements require an editor host. Search again from the Code workspace."));
		return;
	}
	if (beforeApply) {
		const QString error = beforeApply(m_report);
		if (!error.isEmpty()) {
			m_status->setText(error);
			m_details->setPlainText(error);
			return;
		}
	}
	QMessageBox confirmation(QMessageBox::Question, !m_report.codeActionTitle.isEmpty() ? tr("Apply Code Action") : m_report.semanticRename ? tr("Apply Symbol Rename") : tr("Apply Project Replacements"), tr("Apply the reviewed replacements?"),
		QMessageBox::Apply | QMessageBox::Cancel, this);
	confirmation.setTextFormat(Qt::PlainText);
	confirmation.setInformativeText(tr("Open documents to edit: %1. Unopened files to save: %2.")
			.arg(std::count_if(m_report.changes.cbegin(), m_report.changes.cend(), [](const auto& change) { return !change.bufferId.isEmpty(); }))
			.arg(std::count_if(m_report.changes.cbegin(), m_report.changes.cend(), [](const auto& change) { return change.bufferId.isEmpty(); }))
		+ QStringLiteral("\n\n") + tr("Open-document edits remain unsaved and can be undone.")
		+ QLatin1Char('\n') + tr("Disk changes cannot be undone in the editor."));
	confirmation.setDefaultButton(QMessageBox::Cancel);
	if (confirmation.exec() != QMessageBox::Apply) { return; }
	// A modal question can process timers and external-change notifications.
	if (beforeApply) {
		const QString error = beforeApply(m_report);
		if (!error.isEmpty()) { m_status->setText(error); return; }
	}
	if (!m_previewCurrent) { return; }
	launch({}, true);
}

void ProjectSearchPanel::launch(AssetTextSearchRequest request, bool applying, std::optional<LanguageReferenceRequest> references, std::optional<LanguageWorkspaceEditRequest> languageEdits)
{
	if (busy()) { return; }
	if (!references && !languageEdits && !applying) { m_search->setText(tr("Search")); }
	const auto state = std::make_shared<Work>();
	state->applying = applying;
	state->references = references.has_value() || languageEdits.has_value();
	m_work = state;
	m_previewCurrent = false;
	const quint64 generation = m_generation;
	const AssetTextSearchReport preview = m_report;
	if (!applying && !references && !languageEdits) { m_report = {}; }
	request.dryRun = true;
	if (!applying && !references && !languageEdits && captureBuffers) { request.buffers = captureBuffers(); }
	request.isCancelled = [state]() { return state->cancel.load(); };
	request.progress = [state](int files, int matches) { state->files = files; state->matches = matches; };
	if (references) { references->isCancelled = request.isCancelled; references->progress = request.progress; }
	if (languageEdits) { languageEdits->isCancelled = request.isCancelled; languageEdits->progress = request.progress; }
	// Search controls are frozen during the scan, so the visible controls
	// always describe the preview returned by this worker.
	setSearchControlsEnabled(false);
	updateButtons();
	m_progress->setRange(0, 0);
	m_progress->show();
	m_status->setText(languageEdits ? tr("Validating language edits and preparing the review…") : references ? tr("Preparing reference source previews…") : applying ? tr("Applying reviewed replacements…") : tr("Searching project files and open documents…"));
	if (!applying) { m_results->clear(); m_details->clear(); }
	m_progressTimer->start();
	QPointer<ReplacementProgress> writeDialog;
	if (applying) {
		writeDialog = new ReplacementProgress(window());
		writeDialog->setWindowTitle(tr("Apply Project Replacements"));
		writeDialog->setWindowModality(Qt::WindowModal);
		writeDialog->setAccessibleName(tr("Project replacement progress"));
		auto* layout = new QVBoxLayout(writeDialog);
		auto* label = new QLabel(tr("Applying reviewed replacements (%1)…").arg(preview.changes.size()));
		label->setWordWrap(true);
		layout->addWidget(label);
		auto* progress = new QProgressBar;
		progress->setRange(0, 0);
		progress->setAccessibleName(tr("Replacement write progress"));
		layout->addWidget(progress);
		auto* stop = new QPushButton(tr("Cancel"));
		stop->setAccessibleName(tr("Cancel remaining replacements"));
		layout->addWidget(stop);
		writeDialog->cancelWork = [state, label, stop]() {
			state->cancel = true;
			label->setText(tr("Cancelling… Waiting for the current file."));
			stop->setEnabled(false);
		};
		connect(stop, &QPushButton::clicked, writeDialog, &QDialog::reject);
		writeDialog->show();
	}
	if (!references && !languageEdits && operationStarted) { operationStarted(applying); }
	const bool deferBuffers = bool(applyBuffers);
	auto* worker = QThread::create([state, request, preview, applying, deferBuffers, references = std::move(references), languageEdits = std::move(languageEdits)]() {
		state->result = languageEdits ? prepareLanguageWorkspaceEdit(*languageEdits) : references ? prepareLanguageReferences(*references) : applying ? applyProjectTextReplacements(preview, [state]() { return state->cancel.load(); }, {}, deferBuffers)
			: findReplaceProjectText(request);
	});
	connect(worker, &QThread::finished, this, [this, state, generation, writeDialog]() {
		m_progressTimer->stop();
		m_progress->hide();
		m_work.reset();
		setSearchControlsEnabled(true);
		if (state->references && state->cancel) {
			state->result.cancelled = true; state->result.complete = false; state->result.saveState = QStringLiteral("cancelled");
			state->result.matches.clear(); state->result.matchCount = 0; state->result.filesWithMatches = 0;
			state->result.changes.clear(); state->result.replacementCount = 0;
			if (!state->cancellationReason.isEmpty()) { state->result.warnings << state->cancellationReason; }
		}
		if (state->applying && state->result.bufferEditsPending) {
			if (generation == m_generation && !state->cancel && applyBuffers) { applyBuffers(state->result); }
			else {
				state->result.bufferEditsPending = false;
				state->result.saveState = QStringLiteral("failed");
				state->result.cancelled = state->cancel;
				state->result.warnings << tr("The editor context changed or replacement was cancelled. Pending open-document edits were not applied.");
			}
		}
		if (writeDialog) { writeDialog->accept(); writeDialog->deleteLater(); }
		if (generation == m_generation) {
			m_report = state->result;
			m_previewCurrent = !state->applying && (!state->references || m_report.hasLanguageEdits());
			presentReport();
		} else {
			m_status->setText(tr("Project changed. Search to refresh the results."));
		}
		updateButtons();
		if (operationFinished) { operationFinished(state->result); }
	});
	connect(this, &QObject::destroyed, worker, [state]() { state->cancel = true; });
	connect(qApp, &QCoreApplication::aboutToQuit, worker, [worker, state]() { state->cancel = true; worker->wait(); });
	connect(worker, &QThread::finished, worker, &QObject::deleteLater);
	worker->start();
}

void ProjectSearchPanel::presentReport()
{
	m_results->clear();
	for (const auto& match : m_report.matches) {
		auto* item = new QListWidgetItem(QStringLiteral("%1:%2:%3  %4").arg(QDir(m_report.rootPath).relativeFilePath(match.filePath))
			.arg(match.line).arg(match.column).arg(match.lineText.left(400))
			+ (match.bufferId.isEmpty() ? QString() : tr(" [open document]")), m_results);
		item->setData(Qt::UserRole, match.line);
		item->setData(Qt::UserRole + 3, match.filePath);
		item->setData(Qt::UserRole + 4, match.column);
		item->setToolTip(QDir::toNativeSeparators(match.filePath));
	}
	QString status;
	if (!m_report.codeActionTitle.isEmpty() && m_report.dryRun) {
		status = tr("%1 · Edits: %2 · Files: %3 · Provider: %4.").arg(m_report.codeActionTitle).arg(m_report.replacementCount).arg(m_report.filesWithMatches).arg(m_report.referenceProvider);
		status += QLatin1Char(' ') + (!m_report.succeeded() ? tr("The action is blocked. See details.") : m_report.canApply() ? tr("Review the edits, then apply the preview.") : tr("No changes proposed."));
	} else if (m_report.semanticRename && m_report.dryRun) {
		status = tr("Rename %1 → %2 · Edits: %3 · Files: %4 · Provider: %5.").arg(m_report.findText, m_report.replaceText).arg(m_report.replacementCount).arg(m_report.filesWithMatches).arg(m_report.referenceProvider);
		status += QLatin1Char(' ') + (!m_report.succeeded() ? tr("Rename is blocked. See details.") : m_report.canApply() ? tr("Review the edits, then apply the preview.") : tr("No changes proposed."));
	} else if (!m_report.referenceProvider.isEmpty() && !m_report.hasLanguageEdits()) {
		status = tr("References: %1 · Files: %2 · Provider: %3 · Omitted: %4.").arg(m_report.matchCount).arg(m_report.filesWithMatches).arg(m_report.referenceProvider).arg(m_report.referenceLocationsSkipped);
		if (m_report.cancelled) { status += QLatin1Char(' ') + tr("Cancelled. Find references again to refresh."); }
		else if (!m_report.succeeded()) { status += QLatin1Char(' ') + tr("Partial or failed lookup. See details."); }
	} else if (!m_report.dryRun) {
		status = tr("Replacements applied: %1 · Files saved: %2 · Open documents edited: %3.").arg(m_report.replacementsApplied).arg(m_report.writtenFiles.size()).arg(m_report.editedBuffers.size());
		if (m_report.cancelled) { status += QLatin1Char(' ') + tr("Cancelled; remaining files were not written."); }
		else if (!m_report.succeeded()) { status += QLatin1Char(' ') + tr("Replacement stopped. See details, then search again."); }
		else { status += QLatin1Char(' ') + tr("Search again to refresh locations."); }
	} else {
		status = tr("Matches: %1 · Files: %2 · Scanned: %3 · Skipped: %4 · Open documents: %5.")
			.arg(m_report.matchCount).arg(m_report.filesWithMatches).arg(m_report.filesScanned).arg(m_report.filesSkipped).arg(m_report.buffersScanned);
		if (m_report.cancelled) { status += QLatin1Char(' ') + tr("Cancelled. Results are incomplete."); }
		else if (!m_report.succeeded()) { status += QLatin1Char(' ') + tr("Incomplete search. Replacement is blocked; see details."); }
		else if (m_report.canApply()) { status += QLatin1Char(' ') + tr("Review the before and after text, then apply the preview."); }
		else if (m_report.replace && m_report.matchCount > 0) { status += QLatin1Char(' ') + tr("The replacement would leave these files unchanged."); }
	}
	m_status->setText(status);
	if (!m_report.warnings.isEmpty() || !m_report.dryRun) {
		m_details->setPlainText(m_report.warnings.join(QLatin1Char('\n')) + QLatin1Char('\n')
			+ (m_report.writtenFiles.isEmpty() ? tr("No files were written.")
				: tr("Saved files:\n%1").arg(m_report.writtenFiles.join(QLatin1Char('\n')))));
		if (!m_report.editedBuffers.isEmpty()) { m_details->appendPlainText(tr("Edited, not saved:\n%1").arg(m_report.editedBuffers.join(QLatin1Char('\n')))); }
	} else if (!m_report.matches.isEmpty()) {
		m_results->setCurrentRow(0);
	} else {
		m_details->setPlainText(m_report.hasLanguageEdits() ? tr("The language server proposed no text changes.") : m_report.referenceProvider.isEmpty() ? tr("No matches in the selected project files and open documents. Binary and unsupported text encodings are excluded.")
			: tr("The language server returned no usable references in this project."));
	}
}

void ProjectSearchPanel::showMatchDetails()
{
	const int row = m_results->currentRow();
	if (row < 0 || row >= m_report.matches.size()) { return; }
	const auto& match = m_report.matches.at(row);
	QString details = (m_report.referenceProvider.isEmpty() || m_report.hasLanguageEdits() ? tr("%1\nLine %2, column %3\n\nBefore:\n%4") : tr("%1\nLine %2, column %3\n\nSource:\n%4"))
		.arg(QDir::toNativeSeparators(match.filePath)).arg(match.line).arg(match.column).arg(match.rawLine.left(8192));
	details += tr("\n\nSource: %1 · Encoding: %2").arg(match.bufferId.isEmpty() ? tr("Saved file") : tr("Open document snapshot"), match.encoding);
	if (m_report.replace) { details += tr("\n\nAfter:\n%1").arg(match.replacementLine.left(8192)); }
	if (match.rawLine.size() > 8192 || match.replacementLine.size() > 8192) { details += m_report.referenceProvider.isEmpty() ? tr("\n\nPreview shortened. Replacements use the complete line.") : tr("\n\nSource preview shortened."); }
	if (!m_report.referenceProvider.isEmpty()) { details += (m_report.hasLanguageEdits() ? tr("\n\nEdit provider: %1. Each edit uses the reviewed source snapshot.") : tr("\n\nReference provider: %1. Locations are checked against this source snapshot.")).arg(m_report.referenceProvider); }
	if (!m_report.dryRun) { details += tr("\n\nThese locations are from before replacement. Search again to refresh them."); }
	if (!m_report.warnings.isEmpty()) { details += QLatin1Char('\n') + m_report.warnings.join(QLatin1Char('\n')); }
	m_details->setPlainText(details);
}

} // namespace vibestudio
