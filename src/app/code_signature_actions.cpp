#include "app/application_shell.h"
#include "app/code_editor.h"
#include "app/code_language_panel.h"
#include "app/code_signature_panel.h"
#include "core/project_text_search.h"

#include <QTextCursor>
#include <QTimer>

namespace vibestudio {

QWidget* ApplicationShell::buildCodeSignaturePanel()
{
	m_codeSignature = new CodeSignaturePanel; m_codeSignature->hide();
	m_codeSignatureTimer = new QTimer(this); m_codeSignatureTimer->setSingleShot(true); m_codeSignatureTimer->setInterval(120);
	connect(m_codeSignatureTimer, &QTimer::timeout, this, [this]() { requestCodeSignatureHelp(); });
	m_codeSignature->dismissed = [this]() { retireCodeSignatureHelp(); if (m_codeEditor) { m_codeEditor->setFocus(Qt::OtherFocusReason); } };
	m_codeEditor->signatureHelpDismissed = [this]() { return retireCodeSignatureHelp(); };
	m_codeEditor->signatureHelpTyped = [this](const QString& text) {
		if (m_codeLanguagePanel && m_codeLanguagePanel->client()->signatureTriggers(m_codeSignatureActive).contains(text)) { queueCodeSignatureHelp(2, text); }
	};
	connect(m_codeEditor, &QPlainTextEdit::textChanged, this, [this]() { codeSignatureContextChanged(); });
	connect(m_codeEditor, &QPlainTextEdit::cursorPositionChanged, this, [this]() { codeSignatureContextChanged(); });
	return m_codeSignature;
}

bool ApplicationShell::retireCodeSignatureHelp()
{
	const bool shown = m_codeSignature && !m_codeSignature->isHidden();
	++m_codeSignatureToken; m_codeSignatureActive = false; m_codeSignatureRetrigger = false;
	m_codeSignatureDocument.clear(); m_codeSignaturePath.clear();
	if (m_codeSignatureTimer) { m_codeSignatureTimer->stop(); }
	const int request = m_codeSignatureRequest; m_codeSignatureRequest = -1;
	if (request >= 0 && m_codeLanguagePanel) { m_codeLanguagePanel->client()->cancelRequest(request); }
	if (m_codeSignature) { m_codeSignature->clear(); m_codeSignature->hide(); }
	return shown;
}

void ApplicationShell::codeSignatureContextChanged()
{
	if (!m_codeSignatureActive) { return; }
	if (!hasCodeDocument() || currentMode() != StudioMode::Code || m_codeSignatureDocument != m_codeEditor->document()
		|| m_codeSignaturePath != m_codeFilePath || m_codeEditor->textCursor().hasSelection()) { retireCodeSignatureHelp(); return; }
	queueCodeSignatureHelp(3);
}

bool ApplicationShell::queueCodeSignatureHelp(int triggerKind, const QString& trigger)
{
	if (!m_codeSignature || !m_codeEditor || !hasCodeDocument() || currentMode() != StudioMode::Code || m_codeEditor->textCursor().hasSelection()) { return false; }
	if (!m_codeLanguagePanel || !m_codeLanguagePanel->client()->ready() || !m_codeLanguagePanel->client()->supportsSignatureHelp()) {
		retireCodeSignatureHelp();
		if (triggerKind == 1) { m_codeSignature->clear(tr("Connect a language server with parameter-hint support for this document.")); m_codeSignature->show(); }
		return false;
	}
	const int request = m_codeSignatureRequest; m_codeSignatureRequest = -1; ++m_codeSignatureToken;
	if (request >= 0) { m_codeLanguagePanel->client()->cancelRequest(request); }
	m_codeSignatureRetrigger = m_codeSignatureActive; m_codeSignatureActive = true;
	m_codeSignatureDocument = m_codeEditor->document(); m_codeSignaturePath = m_codeFilePath;
	m_codeSignatureTriggerKind = triggerKind; m_codeSignatureTrigger = trigger;
	m_codeSignature->begin(m_codeLanguagePanel->client()->serverName()); m_codeSignatureTimer->stop();
	if (triggerKind == 1) { requestCodeSignatureHelp(); } else { m_codeSignatureTimer->start(); }
	return true;
}

void ApplicationShell::requestCodeSignatureHelp()
{
	if (!m_codeSignatureActive || !m_codeLanguagePanel || !m_codeLanguagePanel->client()->ready()
		|| !m_codeLanguagePanel->synchronizeNow() || m_codeLanguagePanel->client()->documentVersion(m_codeFilePath) == 0) { retireCodeSignatureHelp(); return; }
	const auto document = m_codeSignatureDocument;
	if (!document || document != m_codeEditor->document() || m_codeSignaturePath != m_codeFilePath || m_codeEditor->textCursor().hasSelection()) { retireCodeSignatureHelp(); return; }
	const int revision = document->revision(), caret = m_codeEditor->textCursor().position();
	const auto cursor = m_codeEditor->textCursor(); const QString path = m_codeFilePath, provider = m_codeLanguagePanel->client()->serverName();
	const quint64 token = m_codeSignatureToken, epoch = m_codeLanguageEpoch; const bool explicitRequest = m_codeSignatureTriggerKind == 1;
	m_codeSignatureRequest = m_codeLanguagePanel->client()->signatureHelp(path, cursor.blockNumber(), cursor.positionInBlock(),
		m_codeSignatureTriggerKind, m_codeSignatureTrigger, m_codeSignatureRetrigger, m_codeSignature->report(),
		[this, document, revision, caret, path, provider, token, epoch, explicitRequest](const LanguageSignatureHelp& result) {
			if (token != m_codeSignatureToken || !m_codeSignatureActive) { return; }
			m_codeSignatureRequest = -1;
			if (!document || document != m_codeEditor->document() || path != m_codeFilePath || document->revision() != revision
				|| m_codeEditor->textCursor().position() != caret || m_codeEditor->textCursor().hasSelection() || m_codeLanguageEpoch != epoch
				|| currentMode() != StudioMode::Code || !m_codeLanguagePanel->client()->ready() || m_codeLanguagePanel->client()->documentVersion(path) != result.version
				|| (result.error.isEmpty() && assetTextSnapshotHash(document->toRawText().replace(QChar(0x2029), QLatin1Char('\n'))) != result.sourceSha256)) { retireCodeSignatureHelp(); return; }
			if (result.error.isEmpty() && result.signatures.isEmpty() && !result.limited && result.skipped == 0 && !explicitRequest) { retireCodeSignatureHelp(); return; }
			m_codeSignature->finish(result, provider); m_codeSignatureActive = result.error.isEmpty() && !result.signatures.isEmpty();
		});
	if (m_codeSignatureRequest < 0) {
		m_codeSignatureActive = false; m_codeSignature->clear(tr("Parameter hints could not start for this document. Request them after reconnecting.")); m_codeSignature->show();
	}
}

} // namespace vibestudio
