#include "app/code_quick_info.h"
#include "app/language_documentation.h"
#include <QScopedPointer>
#include "app/application_shell.h"
#include "app/code_editor.h"
#include "app/code_language_panel.h"
#include "app/ui_primitives.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocumentFragment>
#include <QToolTip>
#include <QVBoxLayout>

namespace vibestudio {
namespace {
QString infoStatus(const LanguageHover& report, const QString& provider, const QString& location)
{
	QString text = report.error.isEmpty() ? QCoreApplication::translate("CodeQuickInfo", "%1 · %2").arg(provider, location) : report.error;
	if (report.error.isEmpty() && report.contents.isEmpty()) { text += QCoreApplication::translate("CodeQuickInfo", " · No symbol information at this position."); }
	if (report.limited) { text += QCoreApplication::translate("CodeQuickInfo", " · Documentation shortened."); }
	if (report.skipped > 0) { text += QCoreApplication::translate("CodeQuickInfo", " · Invalid or unsupported parts omitted: %1.").arg(report.skipped); }
	return text;
}
}

CodeQuickInfoPanel::CodeQuickInfoPanel(QWidget* parent) : QWidget(parent)
{
	setObjectName(QStringLiteral("codeQuickInfoPanel")); setAccessibleName(tr("Quick Info"));
	auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
	auto* row = new QHBoxLayout;
	m_status = new QLabel(tr("No symbol information requested.")); m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true); m_status->setAccessibleName(tr("Quick Info status"));
	m_status->setObjectName(QStringLiteral("codeQuickInfoStatus")); row->addWidget(m_status, 1);
	m_cancel = new QPushButton(tr("Cancel")); m_cancel->setObjectName(QStringLiteral("codeQuickInfoCancel")); m_cancel->setAccessibleName(tr("Cancel Quick Info request")); row->addWidget(m_cancel);
	connect(m_cancel, &QPushButton::clicked, this, [this]() { if (cancelRequested) { cancelRequested(); } }); layout->addLayout(row);
	m_progress = new QProgressBar; m_progress->setRange(0, 0); m_progress->setTextVisible(false); m_progress->setAccessibleName(tr("Loading symbol information")); layout->addWidget(m_progress);
	m_view = new QTextBrowser; m_view->setObjectName(QStringLiteral("codeQuickInfoText")); m_view->setAccessibleName(tr("Symbol documentation"));
	m_view->setAccessibleDescription(tr("Read-only symbol information from the connected language server. Text can be selected and copied."));
	m_view->setDocument(createLanguageDocumentationDocument(m_view)); m_view->setOpenLinks(false); m_view->setOpenExternalLinks(false);
	m_view->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard); m_view->setFocusPolicy(Qt::StrongFocus);
	layout->addWidget(m_view, 1); setBusy(false);
}
void CodeQuickInfoPanel::setBusy(bool busy) { m_busy = busy; m_cancel->setEnabled(busy); m_progress->setVisible(busy); }
void CodeQuickInfoPanel::begin(const QString& provider, const QString& location)
{
	m_hasInfo = true; m_report = {}; m_view->clear(); setBusy(true); m_status->setText(tr("Loading Quick Info from %1 · %2…").arg(provider, location));
}
void CodeQuickInfoPanel::finish(const LanguageHover& report, const QString& provider, const QString& location)
{
	m_hasInfo = true; m_report = report; setBusy(false); m_status->setText(infoStatus(report, provider, location));
	m_view->document()->setDefaultFont(font()); renderLanguageDocumentation(m_view->document(), report.contents); m_view->moveCursor(QTextCursor::Start);
}
void CodeQuickInfoPanel::invalidate(const QString& reason) { if (m_busy || m_hasInfo) { showUnavailable(reason); } }
void CodeQuickInfoPanel::showUnavailable(const QString& reason) { m_hasInfo = false; m_report = {}; setBusy(false); m_view->clear(); m_status->setText(reason); }
QString CodeQuickInfoPanel::hintText(const LanguageHover& report)
{
	QScopedPointer<QTextDocument> document(createLanguageDocumentationDocument()); renderLanguageDocumentation(document.data(), report.contents);
	const QString full = document->toPlainText(); QString text = full.left(1800); const auto lines = text.split(QLatin1Char('\n'));
	if (lines.size() > 12) { text = lines.mid(0, 12).join(QLatin1Char('\n')); }
	if (!text.isEmpty() && text.back().isHighSurrogate()) { text.chop(1); }
	if (text.size() < full.size() || report.limited) { text += tr("\n… Quick Info shows the full available documentation."); }
	if (report.skipped > 0) { text += tr("\nInvalid or unsupported parts omitted: %1.").arg(report.skipped); }
	return text;
}

QWidget* ApplicationShell::buildCodeQuickInfoPanel()
{
	m_codeQuickInfo = new CodeQuickInfoPanel;
	m_codeQuickInfo->cancelRequested = [this]() { retireCodeQuickInfo(tr("Quick Info cancelled."), true); };
	m_codeEditor->quickInfoRequested = [this](int offset, const QPoint& global) { return requestCodeQuickInfo(offset, false, global); };
	m_codeEditor->quickInfoDismissed = [this]() { if (!m_codeQuickInfoPersistent) { retireCodeQuickInfo({}, false); } };
	connect(m_codeEditor, &QPlainTextEdit::cursorPositionChanged, this, [this]() { if (m_codeQuickInfoRequest >= 0) { retireCodeQuickInfo(tr("The caret moved. Request Quick Info again."), m_codeQuickInfoPersistent); } });
	return m_codeQuickInfo;
}
void ApplicationShell::retireCodeQuickInfo(const QString& reason, bool clearPane)
{
	++m_codeQuickInfoToken;
	const int request = m_codeQuickInfoRequest; m_codeQuickInfoRequest = -1; m_codeQuickInfoPersistent = false;
	if (request >= 0 && m_codeLanguagePanel) { m_codeLanguagePanel->client()->cancelRequest(request); }
	QToolTip::hideText();
	if (clearPane && m_codeQuickInfo) { m_codeQuickInfo->invalidate(reason); }
}
bool ApplicationShell::requestCodeQuickInfo(int offset, bool persistent, const QPoint& global)
{
	if (!m_codeEditor || !m_codeQuickInfo || !hasCodeDocument() || currentMode() != StudioMode::Code) { return false; }
	if (!persistent && m_codeQuickInfoRequest >= 0 && m_codeQuickInfoPersistent) { return false; }
	retireCodeQuickInfo({}, false);
	if (persistent) { m_codeOutputTabs->setCurrentWidget(m_codeQuickInfo); m_codeQuickInfo->view()->setFocus(Qt::ShortcutFocusReason); }
	if (!m_codeLanguagePanel || !m_codeLanguagePanel->client()->ready() || !m_codeLanguagePanel->client()->supportsHover()
		|| !m_codeLanguagePanel->synchronizeNow() || m_codeLanguagePanel->client()->documentVersion(m_codeFilePath) == 0) {
		if (persistent) { m_codeQuickInfo->showUnavailable(tr("Connect a language server with Quick Info support for this document.")); } return false;
	}
	const QPointer<QTextDocument> document = m_codeEditor->document();
	if (offset < 0 || offset >= document->characterCount()) { return false; }
	QTextCursor at(document); at.setPosition(offset);
	const QString path = m_codeFilePath, provider = m_codeLanguagePanel->client()->serverName();
	const int revision = document->revision(), caret = m_codeEditor->textCursor().position();
	const quint64 epoch = m_codeLanguageEpoch, token = m_codeQuickInfoToken;
	const QString location = tr("%1:%2:%3").arg(QDir(m_settings.currentProjectPath()).relativeFilePath(path)).arg(at.blockNumber() + 1).arg(at.positionInBlock() + 1);
	m_codeQuickInfoPersistent = persistent;
	if (persistent) { m_codeQuickInfo->begin(provider, location); }
	else { QToolTip::showText(global, tr("Loading symbol information…"), m_codeEditor); }
	m_codeQuickInfoRequest = m_codeLanguagePanel->client()->hover(path, at.blockNumber(), at.positionInBlock(),
		[this, document, path, revision, caret, epoch, token, persistent, global, provider, location](const LanguageHover& result) {
			if (token != m_codeQuickInfoToken) { return; }
			m_codeQuickInfoRequest = -1;
			if (!document || m_codeEditor->document() != document || m_codeFilePath != path || document->revision() != revision || m_codeLanguageEpoch != epoch
				|| currentMode() != StudioMode::Code || (persistent && m_codeEditor->textCursor().position() != caret)
				|| !m_codeLanguagePanel->client()->ready() || m_codeLanguagePanel->client()->documentVersion(path) != result.version) {
				retireCodeQuickInfo(tr("The source context changed. Request Quick Info again."), persistent); return;
			}
			if (result.error.isEmpty() && QCryptographicHash::hash(document->toRawText().replace(QChar(0x2029), QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha256) != result.sourceSha256) {
				retireCodeQuickInfo(tr("The source snapshot changed. Request Quick Info again."), persistent); return;
			}
			if (persistent) { m_codeQuickInfo->finish(result, provider, location); }
			else {
				QString hint = result.error.isEmpty() ? CodeQuickInfoPanel::hintText(result) : result.error.left(1800);
				if (hint.isEmpty()) { QToolTip::hideText(); }
				else { QToolTip::showText(global, QStringLiteral("<p style='white-space:pre-wrap'>") + hint.toHtmlEscaped() + QStringLiteral("</p>"), m_codeEditor); }
			}
		});
	if (m_codeQuickInfoRequest < 0) { retireCodeQuickInfo(tr("The language server could not start Quick Info."), persistent); return false; }
	return true;
}

} // namespace vibestudio
