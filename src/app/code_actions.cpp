#include "app/application_shell.h"
#include "app/code_actions_dialog.h"
#include "app/code_editor.h"
#include "app/code_language_panel.h"
#include "app/project_search_panel.h"
#include <QStatusBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

namespace vibestudio {
void ApplicationShell::showCodeActions()
{
	if (!m_codeEditor || !hasCodeDocument() || m_codeEditor->isReadOnly() || m_codeSaveRunning || m_codeProjectReplaceRunning || !m_codeSearchPanel) {
		statusBar()->showMessage(tr("Open an editable Code document and finish any pending save before requesting code actions.")); return;
	}
	setMode(StudioMode::Code);
	if (m_codeSearchPanel->busy()) { statusBar()->showMessage(tr("Finish or cancel the current search before requesting code actions.")); return; }
	if (!m_codeLanguagePanel || !m_codeLanguagePanel->client()->ready() || !m_codeLanguagePanel->client()->supportsCodeActions()
		|| !m_codeLanguagePanel->synchronizeNow() || m_codeLanguagePanel->client()->documentVersion(m_codeFilePath) == 0) {
		statusBar()->showMessage(tr("Connect a language server with code action support for this document.")); return;
	}
	const QPointer<LanguageServerClient> client = m_codeLanguagePanel->client(); const auto requestId = std::make_shared<int>(-1);
	const QString path = m_codeFilePath; const auto cursor = m_codeEditor->textCursor(); const quint64 epoch = m_codeLanguageEpoch;
	const QPointer<QTextDocument> document = m_codeEditor->document(); const int revision = document->revision();
	const auto buffers = m_codeSearchPanel->captureBuffers ? m_codeSearchPanel->captureBuffers() : QVector<AssetTextBuffer> {};
	QHash<QString, int> versions; for (const auto& buffer : buffers) { const int version = client->documentVersion(buffer.filePath); if (version > 0) { versions.insert(buffer.filePath, version); } }
	const auto token = m_codeSearchPanel->beginCodeActions(client->rootPath(), client->serverName(), [client, requestId]() { if (client && *requestId >= 0) { client->cancelRequest(*requestId); } });
	if (!token) { return; } if (m_codeOutputTabs) { m_codeOutputTabs->setCurrentWidget(m_codeSearchPanel); }
	const auto current = [this, client, path, document, revision, epoch, token]() {
		return client && client->ready() && document && document == m_codeEditor->document() && document->revision() == revision && path == m_codeFilePath
			&& epoch == m_codeLanguageEpoch && currentMode() == StudioMode::Code && !m_codeSaveRunning && !m_codeProjectReplaceRunning && !m_codeEditor->isReadOnly()
			&& m_codeSearchPanel->semanticRequestCurrent(token);
	};
	const auto finish = [this, token, buffers, versions, current](const LanguageCodeAction& action, const QString& error) {
		if (!current()) { if (m_codeSearchPanel->semanticRequestCurrent(token)) { m_codeSearchPanel->invalidateReferences(tr("The source context changed. Request code actions again.")); } return; }
		LanguageWorkspaceEditRequest request; request.title = action.title.isEmpty() ? tr("Code Actions") : action.title; request.findText = request.title;
		request.workspaceEdit = action.wire.value(QStringLiteral("edit")); request.buffers = buffers; request.versions = versions;
		request.error = !error.isEmpty() ? error : action.disabledReason;
		if (action.title.isEmpty() && request.error.isEmpty()) { request.workspaceEdit = QJsonObject {}; }
		m_codeSearchPanel->finishLanguageEdits(token, request);
	};
	*requestId = client->codeActions(path, cursor.selectionStart(), cursor.selectionEnd() - cursor.selectionStart(),
		[this, client, path, requestId, current, finish](const LanguageCodeActions& actions) {
			*requestId = -1;
			if (actions.items.isEmpty() && (actions.limited || actions.skipped > 0)) { finish({}, tr("The provider returned only invalid or oversized actions. No edit preview was prepared.")); return; }
			if (!current() || !actions.error.isEmpty() || actions.items.isEmpty()) { finish({}, actions.error); return; }
			CodeActionsDialog dialog(actions, client->serverName(), this);
			QTimer guard; connect(&guard, &QTimer::timeout, &dialog, [&dialog, current]() { if (!current()) { dialog.reject(); } }); guard.start(100);
			const int outcome = dialog.exec(); guard.stop();
			if (!current()) { finish({}, {}); return; }
			if (outcome != QDialog::Accepted) { m_codeSearchPanel->invalidateReferences(tr("Code action cancelled.")); return; }
			const auto action = actions.items[dialog.selectedAction()];
			if (!action.needsResolve) { finish(action, {}); return; }
			statusBar()->showMessage(tr("Resolving %1…").arg(action.title));
			*requestId = client->resolveCodeAction(path, actions.version, action, [finish, requestId](const LanguageCodeAction& resolved, const QString& error) { *requestId = -1; finish(resolved, error); });
			if (*requestId < 0) { finish(action, tr("The language server could not resolve this action.")); }
		});
	if (*requestId < 0) { finish({}, tr("The language server could not request code actions.")); }
}
} // namespace vibestudio
