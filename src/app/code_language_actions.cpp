#include "app/application_shell.h"
#include "app/code_editor.h"
#include "app/code_language_panel.h"
#include "app/project_search_panel.h"
#include "app/syntax_highlight.h"

#include <QDir>
#include <QFileInfo>
#include <QListWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QToolButton>

namespace vibestudio {

QWidget* ApplicationShell::buildCodeLanguagePanel()
{
	m_codeLanguagePanel = new CodeLanguagePanel;
	m_codeLanguagePanel->setPreferences(m_settings.languageServerPreferences());
	m_codeLanguagePanel->setRootPath(m_settings.currentProjectPath());
	m_codeEditor->languageCompletionsRequested = [this](int kind, const QString& trigger) { return requestCodeLanguageCompletions(kind, trigger); };
	m_codeEditor->languageCompletionTrigger = [this](const QString& trigger) { return m_codeLanguagePanel->client()->ready() && m_codeLanguagePanel->client()->completionTriggers().contains(trigger); };
	m_codeEditor->languageCompletionCancelled = [this]() {
		if (m_codeCompletionRequest >= 0) { m_codeLanguagePanel->client()->cancelRequest(m_codeCompletionRequest); m_codeCompletionRequest = -1; }
	};
	m_codeLanguagePanel->preferencesChanged = [this](const QJsonObject& preferences) { m_settings.setLanguageServerPreferences(preferences); m_settings.sync(); };
	m_codeLanguagePanel->snapshots = [this](const QString& language, const QStringList& extensions, QString* error) {
		QVector<LanguageDocument> documents;
		const QDir root(m_settings.currentProjectPath());
		qint64 total = 0;
		for (const auto& tab : m_codeTabs) {
			if (tab.path.isEmpty() || tab.packageCopy || !extensions.contains(QFileInfo(tab.path).suffix().toLower())) { continue; }
			const QString relative = root.relativeFilePath(tab.path);
			if (QDir::isAbsolutePath(relative) || relative == QStringLiteral("..") || relative.startsWith(QStringLiteral("../"))) { continue; }
			const qint64 chars = tab.document->characterCount() - 1;
			total += chars * 2;
			if (!tab.source.editable() || chars > textDocumentByteLimit || total > 64ll * 1024 * 1024 || documents.size() >= 128) {
				*error = tr("A matching open document is read-only, incomplete or exceeds the language service limits: %1").arg(QDir::toNativeSeparators(tab.path));
				return QVector<LanguageDocument> {};
			}
			const QString id = language == QStringLiteral("cpp") && QFileInfo(tab.path).suffix().toLower() == QStringLiteral("c") ? QStringLiteral("c") : language;
			documents << LanguageDocument {tab.path, id, tab.document->toRawText().replace(QChar(0x2029), QLatin1Char('\n'))};
		}
		return documents;
	};
	m_codeLanguagePanel->connectionChanged = [this]() {
		const auto* client = m_codeLanguagePanel->client();
		const QString state = client->state();
		if (!client->ready() && m_codeEditor) { m_codeEditor->dismissCompletions(); }
		if (!client->ready()) { retireCodeQuickInfo(tr("The language connection ended. Request Quick Info after reconnecting."), true); }
		if (!client->ready()) { retireCodeSignatureHelp(); }
		if (!client->ready()) { retireCodeFormatting(tr("The language connection ended. Request formatting after reconnecting.")); }
		if (!client->ready() && m_codeSearchPanel) { m_codeSearchPanel->invalidateReferences(tr("The language connection ended. Reconnect and request the language operation again.")); }
		if (state == QStringLiteral("starting")) {
			m_codeLanguageActivityId = m_activity.createTask(tr("Language Server"), client->rootPath(), QStringLiteral("code"), OperationState::Running, false);
		} else if (state == QStringLiteral("ready") && !m_codeLanguageActivityId.isEmpty()) {
			m_activity.completeTask(m_codeLanguageActivityId, tr("Connected to %1. Matching open documents are shared with the local server.").arg(client->serverName()));
		} else if (state == QStringLiteral("failed") && !m_codeLanguageActivityId.isEmpty()) { m_activity.failTask(m_codeLanguageActivityId, client->error()); }
		else if (state == QStringLiteral("stopped") && !m_codeLanguageActivityId.isEmpty()) { m_activity.completeTask(m_codeLanguageActivityId, tr("Language server disconnected.")); }
		if (!m_codeLanguageActivityId.isEmpty()) { persistActivityTask(m_codeLanguageActivityId); refreshActivityCenter(m_codeLanguageActivityId); }
		refreshCodeDiagnostics();
	};
	m_codeLanguagePanel->client()->diagnosticsChanged = [this](const LanguageDiagnostics&) { refreshCodeDiagnostics(); };
	return m_codeLanguagePanel;
}

void ApplicationShell::scheduleCodeLanguageSync()
{
	++m_codeLanguageEpoch;
	retireCodeFormatting(tr("The source context changed. Request formatting again."));
	retireCodeQuickInfo(tr("The source context changed. Request Quick Info again."), true);
	if (m_codeEditor) { m_codeEditor->dismissLanguageCompletions(); }
	if (m_codeSearchPanel) { m_codeSearchPanel->invalidateReferences(tr("The source context changed. Request the language operation again.")); }
	if (!m_codeLanguagePanel) { return; }
	m_codeLanguagePanel->setRootPath(m_settings.currentProjectPath());
	m_codeLanguagePanel->scheduleSync();
	// Retire live server locations immediately, before the debounce elapses.
	if (m_codeLanguagePanel->client()->ready() && m_codeDiagnostics && hasCodeDocument()
		&& !m_codeLanguagePanel->client()->matchesDocument(m_codeFilePath, m_codeEditor->document()->toRawText().replace(QChar(0x2029), QLatin1Char('\n')))) {
		for (int row = m_codeDiagnostics->count() - 1; row >= 0; --row) {
			if (m_codeDiagnostics->item(row)->data(Qt::UserRole + 8).isValid()) { delete m_codeDiagnostics->takeItem(row); }
		}
		if (m_codeHighlighter) { m_codeHighlighter->clearDiagnostics(); }
	}
}

void ApplicationShell::appendCodeLanguageDiagnostics(QVector<StudioDiagnosticMarker>* markers)
{
	if (!m_codeLanguagePanel || !m_codeLanguagePanel->client()->ready() || !hasCodeDocument() || m_codeFilePath.isEmpty()) { return; }
	const auto* client = m_codeLanguagePanel->client();
	const auto report = client->diagnostics(m_codeFilePath);
	if (!report.error.isEmpty()) {
		auto* failed = new QListWidgetItem(tr("Language diagnostics failed: %1").arg(report.error), m_codeDiagnostics); failed->setFlags(Qt::ItemIsEnabled); return;
	}
	if (!report.received) {
		if (client->documentVersion(m_codeFilePath) > 0) {
			auto* waiting = new QListWidgetItem(tr("Waiting for language server diagnostics…"), m_codeDiagnostics); waiting->setFlags(Qt::ItemIsEnabled);
		}
		return;
	}
	const auto& tab = m_codeTabs[m_codeTab];
	// A report can arrive during the GUI debounce. Only publish locations for
	// a version that is still the text currently visible to the user.
	if (!client->matchesDocument(tab.path, tab.document->toRawText().replace(QChar(0x2029), QLatin1Char('\n')))) { return; }
	const int revision = tab.document->revision();
	const bool versioned = report.versioned && report.version == client->documentVersion(tab.path);
	for (const auto& diagnostic : report.items) {
		const auto& at = diagnostic.location;
		const QString severity = diagnostic.severity == 1 ? QStringLiteral("error") : diagnostic.severity == 2 ? QStringLiteral("warning") : QStringLiteral("info");
		const QString severityText = diagnostic.severity == 1 ? tr("Error") : diagnostic.severity == 2 ? tr("Warning") : diagnostic.severity == 4 ? tr("Hint") : tr("Information");
		const QString label = tr("Language server · %1 · %2:%3 · %4").arg(severityText).arg(at.line + 1).arg(at.character + 1).arg(diagnostic.message)
			+ (versioned ? QString() : tr(" (unversioned report; location unavailable)"));
		auto* item = new QListWidgetItem(label, m_codeDiagnostics);
		item->setToolTip(diagnostic.source + QLatin1Char(' ') + diagnostic.code + QLatin1Char('\n') + diagnostic.message);
		item->setData(Qt::UserRole + 8, report.version);
		const QTextBlock block = tab.document->findBlockByNumber(at.line);
		if (!versioned || !block.isValid() || at.character > block.length() - 1) { item->setFlags(Qt::ItemIsEnabled); continue; }
		item->setData(Qt::UserRole, at.line + 1);
		item->setData(Qt::UserRole + 4, at.character + 1);
		item->setData(Qt::UserRole + 9, tab.searchId);
		item->setData(Qt::UserRole + 10, revision);
		markers->push_back({at.line + 1, at.character + 1, at.endLine == at.line ? at.endCharacter - at.character : 0, severity, diagnostic.message});
	}
	if (report.limited) { m_codeDiagnostics->addItem(tr("Some language diagnostics were omitted or shortened because their ranges, content or size were invalid.")); }
}

bool ApplicationShell::activateCodeLanguageDiagnostic(QListWidgetItem* item)
{
	if (!item || !item->data(Qt::UserRole + 8).isValid()) { return false; }
	if (!hasCodeDocument() || !m_codeLanguagePanel || !m_codeLanguagePanel->client()->ready()
		|| item->data(Qt::UserRole + 9).toString() != m_codeTabs[m_codeTab].searchId
		|| item->data(Qt::UserRole + 10).toInt() != m_codeEditor->document()->revision()
		|| item->data(Qt::UserRole + 8).toInt() != m_codeLanguagePanel->client()->documentVersion(m_codeFilePath)) {
		statusBar()->showMessage(tr("The diagnostic has no current document version. Wait for updated language diagnostics.")); return true;
	}
	rememberPlace(); moveCodeCaretTo(item->data(Qt::UserRole).toInt(), item->data(Qt::UserRole + 4).toInt()); return true;
}

bool ApplicationShell::requestCodeLanguageDefinition()
{
	if (!m_codeLanguagePanel || !m_codeLanguagePanel->client()->ready() || !m_codeLanguagePanel->client()->supportsDefinition()) { return false; }
	if (!m_codeLanguagePanel->synchronizeNow() || m_codeLanguagePanel->client()->documentVersion(m_codeFilePath) == 0) { return false; }
	const QPointer<QTextDocument> document = m_codeEditor->document();
	const int revision = document->revision(), position = m_codeEditor->textCursor().position();
	const quint64 epoch = m_codeLanguageEpoch;
	const QString word = codeIdentifierAtCaret();
	const QTextCursor cursor = m_codeEditor->textCursor();
	const int request = m_codeLanguagePanel->client()->definition(m_codeFilePath, cursor.blockNumber(), cursor.positionInBlock(),
		[this, document, revision, position, word, epoch](const QVector<LanguageLocation>& locations, const QString& error) {
			if (!document || m_codeLanguageEpoch != epoch || m_codeEditor->document() != document || document->revision() != revision || m_codeEditor->textCursor().position() != position || currentMode() != StudioMode::Code) { return; }
			if (!error.isEmpty()) { statusBar()->showMessage(error); return; }
			if (locations.isEmpty()) { statusBar()->showMessage(tr("The language server returned no definition at this position.")); return; }
			QVector<CodeSymbol> matches;
			for (const auto& at : locations) { matches << CodeSymbol {word, tr("language server"), at.filePath, QDir(m_settings.currentProjectPath()).relativeFilePath(at.filePath), at.line + 1, at.character + 1}; }
			showCodeDefinitions(matches, word);
		});
	if (request < 0) { return false; }
	statusBar()->showMessage(tr("Resolving definition with the language server… Keep the caret here to open the result."), 15000);
	return true;
}

bool ApplicationShell::requestCodeLanguageCompletions(int triggerKind, const QString& trigger)
{
	if (!m_codeLanguagePanel || !m_codeEditor || !hasCodeDocument() || m_codeEditor->isReadOnly() || currentMode() != StudioMode::Code
		|| !m_codeLanguagePanel->client()->ready() || !m_codeLanguagePanel->client()->supportsCompletion()) { return false; }
	if (!m_codeLanguagePanel->synchronizeNow() || m_codeLanguagePanel->client()->documentVersion(m_codeFilePath) == 0) { return false; }
	const auto token = m_codeEditor->beginLanguageCompletions();
	const QPointer<QTextDocument> document = m_codeEditor->document();
	const QString path = m_codeFilePath;
	const int revision = document->revision(), position = m_codeEditor->textCursor().position();
	const quint64 epoch = m_codeLanguageEpoch;
	const auto cursor = m_codeEditor->textCursor();
	m_codeCompletionRequest = m_codeLanguagePanel->client()->completion(path, cursor.blockNumber(), cursor.positionInBlock(), triggerKind, trigger,
		[this, token, document, path, revision, position, epoch](const LanguageCompletions& result) {
			if (!m_codeEditor->languageCompletionCurrent(token) || m_codeLanguageEpoch != epoch || currentMode() != StudioMode::Code) { return; }
			m_codeCompletionRequest = -1;
			m_codeEditor->finishLanguageCompletions(token, result, [this, document, path, revision, position, epoch, version = result.version](const LanguageCompletionItem& item) {
				if (!document || m_codeEditor->document() != document || m_codeFilePath != path || document->revision() != revision
					|| m_codeEditor->textCursor().position() != position || m_codeEditor->isReadOnly() || m_codeLanguageEpoch != epoch || currentMode() != StudioMode::Code
					|| !m_codeLanguagePanel->client()->ready() || m_codeLanguagePanel->client()->documentVersion(path) != version) {
					statusBar()->showMessage(tr("This completion is out of date. Request suggestions again.")); return false;
				}
				QString error;
				if (!m_codeEditor->applyLanguageCompletion(item, &error)) { statusBar()->showMessage(error); return false; }
				statusBar()->showMessage(tr("Completion applied to the unsaved document. Undo reverts its related edits too."), 5000); return true;
			}, [this, token, path, epoch, version = result.version](int choice, const LanguageCompletionItem& item) {
				if (m_codeCompletionRequest >= 0) { m_codeLanguagePanel->client()->cancelRequest(m_codeCompletionRequest); }
				m_codeCompletionRequest = m_codeLanguagePanel->client()->resolveCompletion(path, version, item,
					[this, token, choice, epoch](const LanguageCompletionItem& resolved, const QString& error) {
						if (m_codeLanguageEpoch != epoch || currentMode() != StudioMode::Code || !m_codeEditor->languageCompletionCurrent(token)) { return; }
						m_codeCompletionRequest = -1;
						if (!m_codeEditor->finishLanguageCompletionResolve(token, choice, resolved, error)) { return; }
						if (!error.isEmpty()) { statusBar()->showMessage(error, 15000); }
						else if (m_codeEditor->languageCompletionCurrent(token)) { statusBar()->showMessage(tr("Completion ready: %1").arg(resolved.label), 15000); }
					});
				if (m_codeCompletionRequest < 0) {
					const auto error = tr("The language server could not resolve this suggestion. Request completions again.");
					m_codeEditor->finishLanguageCompletionResolve(token, choice, {}, error); statusBar()->showMessage(error);
				} else { statusBar()->showMessage(tr("Resolving %1… Escape cancels.").arg(item.label), 15000); }
			});
			QString status = result.error.isEmpty() ? tr("Language server suggestions: %1").arg(result.items.size()) : result.error;
			if (result.incomplete || result.limited) { status += tr(" · Partial list; keep typing to request updated suggestions."); }
			if (result.skipped > 0) { status += tr(" · Unsupported or invalid suggestions skipped: %1").arg(result.skipped); }
			if (!result.error.isEmpty() || result.items.isEmpty()) { status += tr(" · Local completion remains available."); }
			if (m_codeCompletionRequest >= 0) { status += tr(" · Resolving selected suggestion… Escape cancels."); }
			statusBar()->showMessage(status, 15000);
		});
	if (m_codeCompletionRequest < 0) { m_codeEditor->dismissCompletions(); return false; }
	statusBar()->showMessage(tr("Loading language server completions… Escape cancels."), 15000); return true;
}

bool ApplicationShell::requestCodeLanguageReferences(const QString& word)
{
	if (!m_codeLanguagePanel || !m_codeSearchPanel || !m_codeEditor || !hasCodeDocument()
		|| !m_codeLanguagePanel->client()->ready() || !m_codeLanguagePanel->client()->supportsReferences()) { return false; }
	if (m_codeSearchPanel->busy()) { statusBar()->showMessage(tr("Wait for the current search, or cancel it before finding references.")); return true; }
	if (!m_codeLanguagePanel->synchronizeNow() || m_codeLanguagePanel->client()->documentVersion(m_codeFilePath) == 0) { return false; }
	const QPointer<LanguageServerClient> client = m_codeLanguagePanel->client();
	const auto requestId = std::make_shared<int>(-1);
	const auto cursor = m_codeEditor->textCursor();
	const quint64 epoch = m_codeLanguageEpoch;
	const auto buffers = m_codeSearchPanel->captureBuffers ? m_codeSearchPanel->captureBuffers() : QVector<AssetTextBuffer> {};
	const auto token = m_codeSearchPanel->beginReferences(client->rootPath(), word.isEmpty() ? tr("Symbol at caret") : word, client->serverName(),
		[client, requestId]() { if (client && *requestId >= 0) { client->cancelRequest(*requestId); } });
	if (token == 0) { return true; }
	if (m_codeOutputTabs) { m_codeOutputTabs->setCurrentWidget(m_codeSearchPanel); }
	*requestId = client->references(m_codeFilePath, cursor.blockNumber(), cursor.positionInBlock(), true,
		[this, token, epoch, buffers](const LanguageReferences& result) {
			if (m_codeLanguageEpoch != epoch) { m_codeSearchPanel->invalidateReferences(tr("The source context changed. Find references again.")); return; }
			LanguageReferenceRequest request; request.references = result; request.buffers = buffers;
			m_codeSearchPanel->finishReferences(token, std::move(request));
		});
	if (*requestId < 0) {
		LanguageReferenceRequest request; request.references.error = tr("The language server could not start the reference request.");
		m_codeSearchPanel->finishReferences(token, std::move(request));
	}
	return true;
}

} // namespace vibestudio
