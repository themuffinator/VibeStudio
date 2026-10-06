#include "app/application_shell.h"
#include "app/code_editor.h"
#include "app/code_language_panel.h"
#include "app/project_search_panel.h"

#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QTextDocument>

namespace vibestudio {

void ApplicationShell::renameCodeSymbol()
{
	if (!m_codeEditor || !hasCodeDocument() || m_codeEditor->isReadOnly() || m_codeSaveRunning || m_codeProjectReplaceRunning || !m_codeSearchPanel) {
		statusBar()->showMessage(tr("Open an editable Code document and finish any pending save before renaming.")); return;
	}
	setMode(StudioMode::Code);
	if (m_codeSearchPanel->busy()) { statusBar()->showMessage(tr("Finish or cancel the current search before renaming.")); return; }
	if (!m_codeLanguagePanel || !m_codeLanguagePanel->client()->ready() || !m_codeLanguagePanel->client()->supportsRename()
		|| !m_codeLanguagePanel->synchronizeNow() || m_codeLanguagePanel->client()->documentVersion(m_codeFilePath) == 0) {
		statusBar()->showMessage(tr("Connect a language server with rename support for this document.")); return;
	}
	const QPointer<LanguageServerClient> client = m_codeLanguagePanel->client();
	const auto requestId = std::make_shared<int>(-1);
	const QString path = m_codeFilePath; const auto cursor = m_codeEditor->textCursor();
	const quint64 epoch = m_codeLanguageEpoch;
	const QPointer<QTextDocument> document = m_codeEditor->document(); const int revision = document->revision();
	QTextCursor wordCursor(cursor); wordCursor.select(QTextCursor::WordUnderCursor); const QString word = wordCursor.selectedText();
	const auto buffers = m_codeSearchPanel->captureBuffers ? m_codeSearchPanel->captureBuffers() : QVector<AssetTextBuffer> {};
	QHash<QString, int> versions;
	for (const auto& buffer : buffers) { const int version = client->documentVersion(buffer.filePath); if (version > 0) { versions.insert(buffer.filePath, version); } }
	const auto token = m_codeSearchPanel->beginReferences(client->rootPath(), word, client->serverName(),
		[client, requestId]() { if (client && *requestId >= 0) { client->cancelRequest(*requestId); } }, true);
	if (!token) { return; }
	if (m_codeOutputTabs) { m_codeOutputTabs->setCurrentWidget(m_codeSearchPanel); }
	const auto current = [this, client, document, path, revision, epoch, token]() {
		return client && client->ready() && document && document == m_codeEditor->document() && document->revision() == revision
			&& path == m_codeFilePath && epoch == m_codeLanguageEpoch && currentMode() == StudioMode::Code && !m_codeSaveRunning
			&& !m_codeProjectReplaceRunning && !m_codeEditor->isReadOnly() && m_codeSearchPanel->semanticRequestCurrent(token);
	};
	const auto chooseName = [this, client, requestId, token, path, cursor, buffers, versions, current](const LanguageRenamePreparation& preparation) {
		*requestId = -1;
		if (!current()) { m_codeSearchPanel->invalidateReferences(tr("The source context changed. Request rename again.")); return; }
		if (!preparation.error.isEmpty()) { LanguageRenameRequest request; request.rename.error = preparation.error; m_codeSearchPanel->finishRename(token, request); return; }
		QInputDialog dialog(this); dialog.setObjectName(QStringLiteral("codeRenameName"));
		dialog.setWindowTitle(tr("Rename Symbol")); dialog.setAccessibleName(tr("New symbol name")); dialog.setLabelText(tr("New name for %1:").arg(preparation.placeholder));
		dialog.setInputMode(QInputDialog::TextInput); dialog.setTextValue(preparation.placeholder); dialog.setOkButtonText(tr("Preview Rename"));
		if (auto* label = dialog.findChild<QLabel*>()) { label->setTextFormat(Qt::PlainText); label->setWordWrap(true); }
		if (auto* field = dialog.findChild<QLineEdit*>()) { field->setMaxLength(1024); field->setAccessibleName(tr("New symbol name")); }
		if (dialog.exec() != QDialog::Accepted) { m_codeSearchPanel->invalidateReferences(tr("Rename cancelled.")); return; }
		if (!current()) { m_codeSearchPanel->invalidateReferences(tr("The source context changed. Request rename again.")); return; }
		LanguageRenameRequest request; request.symbol = preparation.placeholder; request.newName = dialog.textValue(); request.buffers = buffers; request.versions = versions;
		if (!validLanguageRenameName(request.newName)) { request.rename.error = tr("Enter a nonempty name without line breaks or control characters."); m_codeSearchPanel->finishRename(token, request); return; }
		*requestId = client->rename(path, cursor.blockNumber(), cursor.positionInBlock(), request.newName,
			[this, current, token, requestId, request](const LanguageRename& result) mutable {
				*requestId = -1; if (!current()) { m_codeSearchPanel->invalidateReferences(tr("The source context changed. Request rename again.")); return; }
				request.rename = result; m_codeSearchPanel->finishRename(token, std::move(request));
			});
		if (*requestId < 0) { request.rename.error = tr("The language server could not start rename."); m_codeSearchPanel->finishRename(token, request); }
	};
	if (client->supportsPrepareRename()) {
		*requestId = client->prepareRename(path, cursor.blockNumber(), cursor.positionInBlock(), chooseName);
		if (*requestId < 0) { LanguageRenameRequest request; request.rename.error = tr("The language server could not prepare rename."); m_codeSearchPanel->finishRename(token, request); }
	} else { LanguageRenamePreparation preparation; preparation.placeholder = word; chooseName(preparation); }
}

} // namespace vibestudio
