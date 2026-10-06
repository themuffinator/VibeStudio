#include "app/application_shell.h"
#include "app/code_editor.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace vibestudio {

QWidget* ApplicationShell::buildCodeSnippetBar()
{
	auto* bar = new QWidget; bar->setObjectName(QStringLiteral("codeSnippetBar")); bar->setAccessibleName(tr("Snippet fields")); bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
	auto* layout = new QVBoxLayout(bar); layout->setContentsMargins(6, 3, 6, 3); layout->setSpacing(3);
	auto* row = new QHBoxLayout;
	auto* status = new QLabel; status->setObjectName(QStringLiteral("codeSnippetStatus")); status->setTextFormat(Qt::PlainText); status->setWordWrap(true); status->setAccessibleName(tr("Active snippet field")); row->addWidget(status, 1);
	auto* choices = new QComboBox; choices->setObjectName(QStringLiteral("codeSnippetChoices")); choices->setAccessibleName(tr("Snippet field choice"));
	choices->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); choices->setMinimumContentsLength(12);
	auto* previous = new QToolButton; previous->setObjectName(QStringLiteral("codeSnippetPrevious")); previous->setText(tr("Previous")); previous->setAccessibleName(tr("Previous snippet field")); previous->setToolTip(tr("Previous snippet field (Shift+Tab).")); row->addWidget(previous);
	auto* next = new QToolButton; next->setObjectName(QStringLiteral("codeSnippetNext")); next->setText(tr("Next")); next->setAccessibleName(tr("Next snippet field")); next->setToolTip(tr("Next snippet field (Tab).")); row->addWidget(next);
	auto* finish = new QPushButton(tr("Finish")); finish->setObjectName(QStringLiteral("codeSnippetFinish")); finish->setAccessibleName(tr("Finish snippet editing")); finish->setToolTip(tr("Finish snippet editing and move to its final position.")); row->addWidget(finish); layout->addLayout(row); layout->addWidget(choices);
	connect(previous, &QToolButton::clicked, this, [this]() { m_codeEditor->navigateSnippet(-1); m_codeEditor->setFocus(Qt::OtherFocusReason); });
	connect(next, &QToolButton::clicked, this, [this]() { m_codeEditor->navigateSnippet(1); m_codeEditor->setFocus(Qt::OtherFocusReason); });
	connect(finish, &QPushButton::clicked, this, [this]() { m_codeEditor->finishSnippet(true); m_codeEditor->setFocus(Qt::OtherFocusReason); });
	connect(choices, &QComboBox::activated, this, [this](int index) { m_codeEditor->chooseSnippetValue(index); m_codeEditor->setFocus(Qt::OtherFocusReason); });
	m_codeEditor->snippetChanged = [this, bar, status, choices]() {
		const bool active = m_codeEditor->snippetActive(); status->setText(m_codeEditor->snippetStatus());
		const auto values = m_codeEditor->snippetChoices(); const QSignalBlocker guard(choices); choices->clear(); choices->addItems(values);
		const int selected = values.indexOf(m_codeEditor->textCursor().selectedText().replace(QChar(0x2029), QLatin1Char('\n'))); choices->setCurrentIndex(selected);
		choices->setVisible(!values.isEmpty()); bar->setVisible(active);
		if (!active && !m_codeEditor->snippetStatus().isEmpty()) { statusBar()->showMessage(m_codeEditor->snippetStatus(), 15000); }
	};
	bar->hide(); return bar;
}

} // namespace vibestudio
