#include "app/application_shell.h"

#include "app/code_editor.h"
#include "app/code_index_worker.h"
#include "app/studio_actions.h"
#include "app/studio_runtime.h"
#include "app/syntax_highlight.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace vibestudio {
namespace {

bool sameIndexPath(const QString& left, const QString& right)
{
	return normalizeDocumentWatchPath(left) == normalizeDocumentWatchPath(right);
}

} // namespace

QString ApplicationShell::codeIndexRoot() const
{
	const QString project = m_settings.currentProjectPath();
	if (!project.isEmpty()) { return QFileInfo(project).absoluteFilePath(); }
	return m_codeFilePath.isEmpty() || m_codeFileIsPackageCopy ? QString() : QFileInfo(m_codeFilePath).absolutePath();
}

QWidget* ApplicationShell::buildCodeIndexPanel()
{
	m_codeIndexPanel = new QWidget;
	m_codeIndexPanel->setObjectName(QStringLiteral("codeIndexPanel"));
	m_codeIndexPanel->setAccessibleName(tr("Project source index"));
	auto* layout = new QVBoxLayout(m_codeIndexPanel);
	layout->setContentsMargins(0, 0, 0, 0);
	m_codeIndexStatus = new QLabel(tr("Choose Index Code to scan project definitions."));
	m_codeIndexStatus->setObjectName(QStringLiteral("codeIndexStatus"));
	m_codeIndexStatus->setTextFormat(Qt::PlainText);
	m_codeIndexStatus->setWordWrap(true);
	m_codeIndexStatus->setAccessibleName(tr("Source index status"));
	layout->addWidget(m_codeIndexStatus);
	auto* controls = new QHBoxLayout;
	auto* refresh = new QPushButton(tr("Refresh Index"));
	refresh->setObjectName(QStringLiteral("codeIndexRefresh"));
	refresh->setAccessibleName(tr("Refresh project source index"));
	connect(refresh, &QPushButton::clicked, this, [this]() { startCodeIndex(); });
	controls->addWidget(refresh);
	m_codeIndexProgress = new QProgressBar;
	m_codeIndexProgress->setAccessibleName(tr("Indexing source files"));
	m_codeIndexProgress->setRange(0, 0);
	m_codeIndexProgress->setTextVisible(false);
	m_codeIndexProgress->hide();
	controls->addWidget(m_codeIndexProgress, 1);
	m_codeIndexCancel = new QPushButton(tr("Cancel"));
	m_codeIndexCancel->setObjectName(QStringLiteral("codeIndexCancel"));
	m_codeIndexCancel->setAccessibleName(tr("Cancel source indexing"));
	m_codeIndexCancel->setEnabled(false);
	connect(m_codeIndexCancel, &QPushButton::clicked, this, [this]() { cancelCodeIndex(); });
	controls->addWidget(m_codeIndexCancel);
	layout->addLayout(controls);
	m_codeIndexFilter = new QLineEdit;
	m_codeIndexFilter->setObjectName(QStringLiteral("codeIndexFilter"));
	m_codeIndexFilter->setPlaceholderText(tr("Filter symbols and source paths"));
	m_codeIndexFilter->setAccessibleName(tr("Filter indexed symbols and files"));
	m_codeIndexFilter->setClearButtonEnabled(true);
	connect(m_codeIndexFilter, &QLineEdit::textChanged, this, [this]() { refreshCodeIndexResults(); });
	layout->addWidget(m_codeIndexFilter);
	layout->addWidget(m_advancedCodeTree, 1);
	m_advancedCodeTree->setAccessibleName(tr("Indexed symbols and source files"));
	m_advancedCodeTree->setAccessibleDescription(tr("Activate a symbol to open its source location. The status reports cancelled or incomplete scans."));
	m_codeIndexRefreshTimer = new QTimer(this);
	m_codeIndexRefreshTimer->setSingleShot(true);
	m_codeIndexRefreshTimer->setInterval(400);
	connect(m_codeIndexRefreshTimer, &QTimer::timeout, this, [this]() { ensureDefinitionIndex(); });
	m_codeIndexWorker = new CodeIndexWorker(this);
	m_codeIndexWorker->started = [this]() {
		m_codeIndexProgress->show();
		m_codeIndexCancel->setEnabled(true);
		m_codeIndexStatus->setText(tr("Indexing %1…").arg(QDir::toNativeSeparators(codeIndexRoot())));
		m_codeIndexActivityId = m_activity.createTask(tr("Index Code"), QDir::toNativeSeparators(codeIndexRoot()), QStringLiteral("code"), OperationState::Running, true);
		refreshActivityCenter(m_codeIndexActivityId);
	};
	m_codeIndexWorker->progress = [this](int files, int symbols) {
		const QString status = tr("Indexing: %1 file(s), %2 symbol(s)…").arg(files).arg(symbols);
		m_codeIndexStatus->setText(status);
		m_activity.setProgress(m_codeIndexActivityId, files, 0, status);
	};
	m_codeIndexWorker->completed = [this](const CodeWorkspaceIndex& index) {
		m_codeIndexProgress->hide();
		m_codeIndexCancel->setEnabled(false);
		if (!sameIndexPath(index.rootPath, codeIndexRoot())) {
			m_activity.cancelTask(m_codeIndexActivityId, tr("Source context changed; this result was discarded."));
			persistActivityTask(m_codeIndexActivityId);
			invalidateDefinitionIndex();
			return;
		}
		m_definitionIndex = index;
		// A cancelled/limited result is retained with its state so completion
		// cannot restart the same scan on every keystroke. Refresh is explicit.
		m_definitionIndexValid = true;
		m_advancedCodeIndex = index;
		const QString counts = tr("%1 file(s), %2 symbol(s), %3 skipped").arg(index.files.size()).arg(index.symbols.size()).arg(index.filesSkipped);
		const QString status = index.cancelled ? tr("Index cancelled — %1. Refresh to try again.").arg(counts)
			: index.state == OperationState::Failed ? tr("Index failed — %1").arg(index.warnings.join(QLatin1Char('\n')))
			: !index.complete ? tr("Partial index — %1. Inspect warnings below.").arg(counts)
			: tr("Index ready — %1").arg(counts);
		m_codeIndexStatus->setText(status);
		m_codeIndexStatus->setAccessibleDescription(status);
		for (const auto& warning : index.warnings) { m_activity.appendWarning(m_codeIndexActivityId, warning); }
		if (index.cancelled) { m_activity.cancelTask(m_codeIndexActivityId, status); }
		else if (index.state == OperationState::Failed) { m_activity.failTask(m_codeIndexActivityId, status); }
		else { m_activity.completeTask(m_codeIndexActivityId, status); }
		persistActivityTask(m_codeIndexActivityId);
		refreshActivityCenter(m_codeIndexActivityId);
		refreshAdvancedStudioSurface();
		const bool resume = !index.cancelled && m_pendingDefinitionDocument
			&& m_codeEditor->document() == m_pendingDefinitionDocument
			&& m_pendingDefinitionDocument->revision() == m_pendingDefinitionRevision
			&& m_codeEditor->textCursor().position() == m_pendingDefinitionPosition
			&& currentMode() == StudioMode::Code;
		m_pendingDefinitionDocument.clear();
		if (resume) { goToCodeDefinition(); }
	};
	return m_codeIndexPanel;
}

void ApplicationShell::refreshCodeIndexResults()
{
	if (!m_advancedCodeTree) { return; }
	m_advancedCodeTree->clear();
	const QString query = m_codeIndexFilter ? m_codeIndexFilter->text().trimmed() : QString();
	const auto note = [this](const QString& text) {
		auto* item = new QListWidgetItem(text, m_advancedCodeTree);
		item->setFlags(Qt::ItemIsEnabled);
	};
	const auto location = [this](const QString& label, const QString& path, int line, int column) {
		auto* item = new QListWidgetItem(label, m_advancedCodeTree);
		item->setData(Qt::UserRole, line);
		item->setData(Qt::UserRole + 3, path);
		item->setData(Qt::UserRole + 4, column);
		item->setToolTip(tr("%1\nActivate to open line %2.").arg(QDir::toNativeSeparators(path)).arg(line));
	};
	int matches = 0;
	for (const auto& symbol : m_advancedCodeIndex.symbols) {
		if (!query.isEmpty() && !symbol.name.contains(query, Qt::CaseInsensitive) && !symbol.relativePath.contains(query, Qt::CaseInsensitive)) { continue; }
		if (++matches > 1000) { continue; }
		location(QStringLiteral("%1 %2  %3:%4").arg(symbol.kind, symbol.name, symbol.relativePath).arg(symbol.line), symbol.filePath, symbol.line, symbol.column);
	}
	if (matches == 0) { note(tr("No matching symbols in the available index.")); }
	else if (matches > 1000) { note(tr("Showing 1000 of %1 matching symbols. Narrow the filter to see others.").arg(matches)); }
	note(tr("Files"));
	int files = 0;
	for (const auto& file : m_advancedCodeIndex.files) {
		if (!query.isEmpty() && !file.relativePath.contains(query, Qt::CaseInsensitive)) { continue; }
		if (++files > 200) { continue; }
		location(file.fromBuffer ? tr("%1 (open buffer)").arg(file.relativePath) : file.relativePath, file.filePath, 1, 1);
	}
	if (files > 200) { note(tr("Showing 200 of %1 matching files. Narrow the filter to see others.").arg(files)); }
	for (const auto& warning : m_advancedCodeIndex.warnings) { note(warning); }
	for (const auto& diagnostic : m_advancedCodeIndex.diagnostics.mid(0, 200)) {
		location(tr("%1:%2 — %3").arg(diagnostic.relativePath).arg(diagnostic.line).arg(diagnostic.message), diagnostic.filePath, std::max(1, diagnostic.line), diagnostic.column);
	}
	if (m_advancedCodeIndex.diagnostics.size() > 200) { note(tr("More diagnostics are available from code index on the CLI.")); }
	if (query.isEmpty() && !m_advancedCodeIndex.rootPath.isEmpty()) {
		note(tr("Build tasks"));
		for (const auto& task : m_advancedCodeIndex.buildTaskLines) { note(task); }
		note(tr("Launch profiles"));
		for (const auto& profile : m_advancedCodeIndex.launchProfileLines) { note(profile); }
	}
}

void ApplicationShell::cancelCodeIndex()
{
	if (!m_codeIndexWorker) { return; }
	m_pendingDefinitionDocument.clear();
	m_codeIndexRefreshTimer->stop();
	m_codeIndexStatus->setText(tr("Cancelling source indexing…"));
	m_codeIndexCancel->setEnabled(false);
	m_codeIndexWorker->cancel();
}

void ApplicationShell::invalidateDefinitionIndex()
{
	scheduleCodeLanguageSync();
	m_definitionIndexValid = false;
	m_definitionIndex = {};
	m_advancedCodeIndex = {};
	m_pendingDefinitionDocument.clear();
	if (!m_codeIndexWorker) { return; }
	if (m_codeIndexWorker->busy() && !m_codeIndexActivityId.isEmpty()) {
		m_activity.cancelTask(m_codeIndexActivityId, tr("Source context changed; the index will be refreshed."));
		persistActivityTask(m_codeIndexActivityId);
	}
	m_codeIndexWorker->reset();
	m_codeIndexRefreshTimer->stop();
	m_codeIndexProgress->hide();
	m_codeIndexCancel->setEnabled(false);
	m_advancedCodeTree->clear();
	m_codeIndexStatus->setText(tr("Source context changed. The index needs a refresh."));
	if (m_codeIndexRequested && !codeIndexRoot().isEmpty()) { m_codeIndexRefreshTimer->start(); }
}

void ApplicationShell::startCodeIndex()
{
	const QString root = codeIndexRoot();
	if (root.isEmpty()) {
		statusBar()->showMessage(tr("Open a project or a saved source file to index its definitions."));
		return;
	}
	invalidateDefinitionIndex();
	m_codeIndexRefreshTimer->stop();
	m_codeIndexRequested = true;
	CodeWorkspaceIndexRequest request;
	request.rootPath = root;
	qint64 snapshotBytes = 0;
	for (const auto& tab : m_codeTabs) {
		if (tab.path.isEmpty() || tab.packageCopy || !tab.source.editable()) { continue; }
		const QString relative = QDir(root).relativeFilePath(tab.path);
		if (QDir::isAbsolutePath(relative) || relative == QStringLiteral("..") || relative.startsWith(QStringLiteral("../"))) { continue; }
		// Snapshot bounded text on the GUI thread; all parsing and disk reads
		// take place in the worker. Even clean tabs can retain a declined disk edit.
		const qint64 bytes = qint64(tab.document->characterCount()) * sizeof(QChar);
		if (bytes > request.maxFileBytes * 2 || snapshotBytes + bytes > request.maxTotalBytes) {
			request.buffers.push_back({tab.path, QString(), tr("The open buffer exceeds the source index snapshot limit.")});
			continue;
		}
		snapshotBytes += bytes;
		request.buffers.push_back({tab.path, tab.document->toRawText().replace(QChar(0x2029), QLatin1Char('\n')), {}});
	}
	m_codeIndexWorker->start(std::move(request));
}

QString ApplicationShell::ensureDefinitionIndex()
{
	const QString root = codeIndexRoot();
	if (root.isEmpty()) {
		if (!m_definitionIndex.rootPath.isEmpty() || (m_codeIndexWorker && !m_codeIndexWorker->requestedRoot().isEmpty())) { invalidateDefinitionIndex(); }
		return {};
	}
	if (m_definitionIndexValid && sameIndexPath(m_definitionIndex.rootPath, root)) { return root; }
	if (m_codeIndexWorker && (!m_codeIndexWorker->busy() || !sameIndexPath(m_codeIndexWorker->requestedRoot(), root))) { startCodeIndex(); }
	return root;
}

} // namespace vibestudio
