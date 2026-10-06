#include "app/code_signature_panel.h"
#include "app/language_documentation.h"
#include "app/ui_primitives.h"

#include <QComboBox>
#include <QAbstractTextDocumentLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

namespace vibestudio {

CodeSignaturePanel::CodeSignaturePanel(QWidget* parent) : QWidget(parent)
{
	setObjectName(QStringLiteral("codeSignaturePanel")); setAccessibleName(tr("Parameter hints"));
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
	auto* layout = new QVBoxLayout(this); layout->setContentsMargins(6, 4, 6, 4); layout->setSpacing(3);
	layout->setAlignment(Qt::AlignTop);
	auto* row = new QHBoxLayout;
	m_status = new QLabel; m_status->setObjectName(QStringLiteral("codeSignatureStatus")); m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true); m_status->setAccessibleName(tr("Parameter hints status")); row->addWidget(m_status, 1);
	m_overloads = new QComboBox; m_overloads->setObjectName(QStringLiteral("codeSignatureOverloads")); m_overloads->setAccessibleName(tr("Call overload"));
	m_overloads->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	m_details = new QToolButton; m_details->setObjectName(QStringLiteral("codeSignatureDetails")); m_details->setText(tr("Documentation")); m_details->setCheckable(true);
	m_details->setAccessibleName(tr("Show parameter documentation"));
	m_close = new QPushButton(tr("Close")); m_close->setObjectName(QStringLiteral("codeSignatureClose")); m_close->setAccessibleName(tr("Dismiss parameter hints"));
	m_close->setToolTip(tr("Dismiss parameter hints (Escape).")); row->addWidget(m_close); layout->addLayout(row);
	auto* controls = new QWidget; controls->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_controls = new QBoxLayout(QBoxLayout::LeftToRight, controls); m_controls->setContentsMargins(0, 0, 0, 0);
	m_controls->addWidget(m_overloads); m_controls->addWidget(m_details); m_controls->addStretch(); layout->addWidget(controls);
	m_progress = new QProgressBar; m_progress->setRange(0, 0); m_progress->setTextVisible(false); m_progress->setMaximumHeight(4);
	m_progress->setAccessibleName(tr("Loading parameter hints")); layout->addWidget(m_progress);
	m_signature = new QTextBrowser; m_signature->setObjectName(QStringLiteral("codeSignatureText")); m_signature->setAccessibleName(tr("Call signature"));
	m_signature->setDocument(createLanguageDocumentationDocument(m_signature)); m_signature->setOpenLinks(false); m_signature->setOpenExternalLinks(false);
	m_signature->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard); m_signature->setFocusPolicy(Qt::StrongFocus);
	m_signature->setLayoutDirection(Qt::LeftToRight); m_signature->setLineWrapMode(QTextEdit::WidgetWidth); layout->addWidget(m_signature);
	connect(m_signature->document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this, [this]() { updateSignatureHeight(); });
	m_parameter = new QLabel; m_parameter->setObjectName(QStringLiteral("codeSignatureParameter")); m_parameter->setTextFormat(Qt::PlainText);
	m_parameter->setWordWrap(true); m_parameter->setAccessibleName(tr("Active parameter")); layout->addWidget(m_parameter);
	m_documentation = new QTextBrowser; m_documentation->setObjectName(QStringLiteral("codeSignatureDocumentation")); m_documentation->setAccessibleName(tr("Call and parameter documentation"));
	m_documentation->setDocument(createLanguageDocumentationDocument(m_documentation)); m_documentation->setOpenLinks(false); m_documentation->setOpenExternalLinks(false);
	m_documentation->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard); m_documentation->setFocusPolicy(Qt::StrongFocus); layout->addWidget(m_documentation);
	connect(m_details, &QToolButton::toggled, this, [this](bool shown) { m_documentation->setVisible(shown && m_details->isEnabled()); });
	connect(m_close, &QPushButton::clicked, this, [this]() { if (dismissed) { dismissed(); } });
	connect(m_overloads, &QComboBox::currentIndexChanged, this, [this](int index) { if (!m_busy && index >= 0 && index < m_report.signatures.size()) { m_report.activeSignature = index; showSignature(); } });
	auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this); escape->setContext(Qt::WidgetWithChildrenShortcut);
	connect(escape, &QShortcut::activated, this, [this]() { if (dismissed) { dismissed(); } });
	setTabOrder(m_overloads, m_signature); setTabOrder(m_signature, m_details); setTabOrder(m_details, m_documentation); setTabOrder(m_documentation, m_close);
	clear();
}
void CodeSignaturePanel::setBusy(bool busy)
{
	m_busy = busy; m_progress->setVisible(busy); m_signature->setEnabled(!busy); m_documentation->setEnabled(!busy); m_overloads->setEnabled(!busy);
}
void CodeSignaturePanel::begin(const QString& provider)
{
	setBusy(true); m_status->setText(tr("Loading parameter hints from %1…").arg(provider)); m_parameter->clear(); show();
}
void CodeSignaturePanel::finish(const LanguageSignatureHelp& report, const QString& provider)
{
	m_report = report; setBusy(false);
	QString status = report.error.isEmpty() ? tr("Parameter hints · %1").arg(provider) : report.error;
	if (report.error.isEmpty() && report.signatures.isEmpty()) { status += tr(" · No call signature at this position."); }
	if (report.limited) { status += tr(" · Results shortened."); }
	if (report.skipped > 0) { status += tr(" · Invalid parts omitted: %1.").arg(report.skipped); }
	m_status->setText(status);
	{ QSignalBlocker guard(m_overloads); m_overloads->clear();
		for (int index = 0; index < report.signatures.size(); ++index) {
			m_overloads->addItem(tr("Overload %1 of %2").arg(index + 1).arg(report.signatures.size())); m_overloads->setItemData(index, report.signatures[index].label, Qt::ToolTipRole);
		}
		m_overloads->setCurrentIndex(report.activeSignature);
	}
	m_overloads->setVisible(report.signatures.size() > 1); showSignature(); show();
}
void CodeSignaturePanel::showSignature()
{
	m_signature->clear(); m_parameter->clear(); m_documentation->clear(); m_details->setEnabled(false); m_documentation->hide();
	const int selected = m_report.activeSignature;
	if (selected < 0 || selected >= m_report.signatures.size()) { m_signature->hide(); m_parameter->hide(); return; }
	const auto& signature = m_report.signatures[selected];
	auto codeFont = studioMonospaceFont(); codeFont.setPointSizeF(font().pointSizeF()); m_signature->setFont(codeFont);
	m_signature->document()->setDefaultFont(codeFont); m_signature->document()->setPlainText(signature.label);
	// QTextEdit can retain the previous cursor's emphasis across a plain-text refresh.
	QTextCursor reset(m_signature->document()); reset.select(QTextCursor::Document); reset.setCharFormat(QTextCharFormat());
	m_signature->show();
	m_documentation->setFixedHeight(QFontMetrics(font()).lineSpacing() * 6 + 14); m_documentation->document()->setDefaultFont(font());
	QVector<LanguageHoverPart> docs; if (!signature.documentation.text.isEmpty()) { docs << signature.documentation; }
	if (signature.activeParameter >= 0 && signature.activeParameter < signature.parameters.size()) {
		const auto& parameter = signature.parameters[signature.activeParameter];
		QTextCursor cursor(m_signature->document()); cursor.setPosition(parameter.offset); cursor.setPosition(parameter.offset + parameter.length, QTextCursor::KeepAnchor);
		QTextCharFormat format; format.setFontWeight(QFont::Bold); format.setFontUnderline(true); cursor.mergeCharFormat(format);
		cursor.clearSelection(); m_signature->setTextCursor(cursor); m_signature->ensureCursorVisible();
		m_parameter->setText(tr("Parameter %1 of %2: %3").arg(signature.activeParameter + 1).arg(signature.parameters.size()).arg(parameter.label)); m_parameter->show();
		if (!parameter.documentation.text.isEmpty()) { docs << parameter.documentation; }
	} else { m_parameter->setText(tr("This signature has no parameters.")); m_parameter->show(); }
	m_signature->setAccessibleDescription(m_parameter->text());
	renderLanguageDocumentation(m_documentation->document(), docs); m_documentation->moveCursor(QTextCursor::Start);
	m_details->setEnabled(!docs.isEmpty()); m_documentation->setVisible(!docs.isEmpty() && m_details->isChecked());
	updateSignatureHeight();
}
void CodeSignaturePanel::updateSignatureHeight()
{
	const int line = QFontMetrics(m_signature->font()).lineSpacing();
	const int height = qBound(line + 14, qCeil(m_signature->document()->size().height()) + 6, line * 3 + 14);
	if (m_signature->height() != height) { m_signature->setFixedHeight(height); }
}
void CodeSignaturePanel::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	const int controlsWidth = (m_overloads->isHidden() ? 0 : m_overloads->sizeHint().width() + m_controls->spacing()) + m_details->sizeHint().width();
	m_controls->setDirection(controlsWidth > width() - 12 ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
	updateSignatureHeight();
}
void CodeSignaturePanel::clear(const QString& reason)
{
	m_report = {}; setBusy(false); m_status->setText(reason); m_overloads->hide(); m_signature->clear(); m_signature->hide();
	m_parameter->clear(); m_parameter->hide(); m_documentation->clear(); m_documentation->hide(); m_details->setEnabled(false);
}

} // namespace vibestudio
