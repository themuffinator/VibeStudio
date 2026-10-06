#include "app/application_shell.h"
#include "app/code_editor.h"
#include "app/code_language_panel.h"

#include <QLabel>
#include <QProgressDialog>
#include <QProgressBar>
#include <QStatusBar>
#include <QTextCursor>
#include <QTextDocument>

namespace vibestudio {

void ApplicationShell::retireCodeFormatting(const QString& reason, OperationState state)
{
	++m_codeFormattingToken;
	const int request = m_codeFormattingRequest; m_codeFormattingRequest = -1;
	if (request >= 0 && m_codeLanguagePanel) { m_codeLanguagePanel->client()->cancelRequest(request); }
	if (m_codeFormattingProgress) {
		disconnect(m_codeFormattingProgress, nullptr, this, nullptr);
		m_codeFormattingProgress->hide(); m_codeFormattingProgress->deleteLater(); m_codeFormattingProgress = nullptr;
	}
	if (!m_codeFormattingActivityId.isEmpty()) {
		const QString id = m_codeFormattingActivityId; m_codeFormattingActivityId.clear();
		if (state == OperationState::Failed) { m_activity.failTask(id, reason); }
		else if (state == OperationState::Completed) { m_activity.completeTask(id, reason); }
		else { m_activity.cancelTask(id, reason); }
		persistActivityTask(id); refreshActivityCenter(id); if (!reason.isEmpty()) { statusBar()->showMessage(reason, 15000); }
	}
}

void ApplicationShell::formatCodeDocument(bool selection)
{
	if (!m_codeEditor || !hasCodeDocument() || m_codeEditor->isReadOnly() || m_codeSaveRunning || m_codeProjectReplaceRunning) {
		statusBar()->showMessage(tr("Open an editable Code document and finish any pending save before formatting.")); return;
	}
	setMode(StudioMode::Code);
	const auto cursor = m_codeEditor->textCursor();
	if (selection && !cursor.hasSelection()) { statusBar()->showMessage(tr("Select source text to format.")); return; }
	retireCodeFormatting(tr("A new formatting request replaced the previous request."));
	if (!m_codeLanguagePanel || !m_codeLanguagePanel->client()->ready() || !m_codeLanguagePanel->client()->supportsFormatting(selection)
		|| !m_codeLanguagePanel->synchronizeNow() || m_codeLanguagePanel->client()->documentVersion(m_codeFilePath) == 0) {
		statusBar()->showMessage(selection ? tr("Connect a language server with selection formatting support for this document.") : tr("Connect a language server with document formatting support for this document.")); return;
	}
	LanguageFormattingOptions options;
	const QString unit = m_codeEditor->indentUnit(); options.insertSpaces = !unit.contains(QLatin1Char('\t')); options.tabSize = options.insertSpaces ? int(unit.size()) : 4;
	if (selection) { options.rangeOffset = cursor.selectionStart(); options.rangeLength = cursor.selectionEnd() - cursor.selectionStart(); }
	const QPointer<QTextDocument> document = m_codeEditor->document();
	const QString path = m_codeFilePath; const int revision = document->revision();
	const quint64 token = m_codeFormattingToken, epoch = m_codeLanguageEpoch;
	const QString title = selection ? tr("Format Selection") : tr("Format Document");
	const QString message = tr("Formatting with %1…").arg(m_codeLanguagePanel->client()->serverName());
	m_codeFormattingActivityId = m_activity.createTask(title, path, QStringLiteral("code"), OperationState::Running, true);
	refreshActivityCenter(m_codeFormattingActivityId);
	m_codeFormattingProgress = new QProgressDialog(this); m_codeFormattingProgress->setObjectName(QStringLiteral("codeFormattingProgress"));
	m_codeFormattingProgress->setWindowTitle(title); m_codeFormattingProgress->setAccessibleName(title);
	auto* label = new QLabel(message); label->setTextFormat(Qt::PlainText); label->setWordWrap(true); label->setAccessibleName(tr("Formatting status")); m_codeFormattingProgress->setLabel(label);
	m_codeFormattingProgress->setCancelButtonText(tr("Cancel")); m_codeFormattingProgress->setRange(0, 0); m_codeFormattingProgress->setMinimumDuration(0);
	m_codeFormattingProgress->setWindowModality(Qt::NonModal); m_codeFormattingProgress->setAutoClose(false); m_codeFormattingProgress->setAutoReset(false);
	if (auto* progress = m_codeFormattingProgress->findChild<QProgressBar*>()) { progress->setAccessibleName(tr("Waiting for formatting edits")); }
	connect(m_codeFormattingProgress, &QProgressDialog::canceled, this, [this]() { retireCodeFormatting(tr("Formatting cancelled. The document was not changed.")); });
	m_codeFormattingProgress->show(); statusBar()->showMessage(message);
	m_codeFormattingRequest = m_codeLanguagePanel->client()->formatting(path, options,
		[this, document, path, revision, epoch, token](const LanguageFormatting& report) {
			if (token != m_codeFormattingToken) { return; }
			m_codeFormattingRequest = -1;
			if (!document || document != m_codeEditor->document() || path != m_codeFilePath || revision != document->revision()
				|| epoch != m_codeLanguageEpoch || currentMode() != StudioMode::Code || m_codeEditor->isReadOnly() || m_codeSaveRunning || m_codeProjectReplaceRunning
				|| !m_codeLanguagePanel->client()->ready() || m_codeLanguagePanel->client()->documentVersion(path) != report.version) {
				retireCodeFormatting(tr("The source context changed. Request formatting again.")); return;
			}
			const QString source = document->toRawText().replace(QChar(0x2029), QLatin1Char('\n'));
			QString formatted, error;
			if (!previewLanguageFormatting(source, report, &formatted, &error)) { retireCodeFormatting(error, OperationState::Failed); return; }
			if (formatted == source) { retireCodeFormatting(tr("The document already matches the formatter's output."), OperationState::Completed); return; }
			const auto cursor = m_codeEditor->textCursor();
			const int anchor = languageFormattedOffset(report, formatted, cursor.anchor()), position = languageFormattedOffset(report, formatted, cursor.position());
			// Retire protocol/UI state before textChanged schedules other document services.
			const QString activity = m_codeFormattingActivityId; m_codeFormattingActivityId.clear(); retireCodeFormatting({});
			QTextCursor edit(document); edit.beginEditBlock();
			for (auto change = report.edits.crbegin(); change != report.edits.crend(); ++change) {
				edit.setPosition(int(change->offset)); edit.setPosition(int(change->offset + change->length), QTextCursor::KeepAnchor); edit.insertText(change->replacement);
			}
			edit.endEditBlock(); edit.setPosition(anchor); edit.setPosition(position, QTextCursor::KeepAnchor); m_codeEditor->setTextCursor(edit); m_codeEditor->ensureCursorVisible();
			const QString message = tr("Formatting applied to the unsaved document. Undo reverts all formatting edits.");
			m_activity.completeTask(activity, message); persistActivityTask(activity); refreshActivityCenter(activity); statusBar()->showMessage(message, 15000);
		});
	if (m_codeFormattingRequest < 0) { retireCodeFormatting(tr("The language server could not start formatting."), OperationState::Failed); }
}

} // namespace vibestudio
