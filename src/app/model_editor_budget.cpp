// Source size and undo history in the Mesh Editor: a compact summary in the
// status strip (the full sentences are its tooltip and accessible
// description), the Undo history limit (model/historyBudgetMiB) in Model >
// Undo History Limit…, the Undo and Redo tooltips, and a one-time notice when
// history discards old steps. The sizes come from core/model_document.h,
// which also enforces both limits.
#include "app/dialog_screen_fit.h"
#include "app/model_editor_dialog.h"
#include "app/status_line_label.h"

#include "core/studio_settings.h"

#include <QAction>
#include <QBoxLayout>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>

#include <algorithm>

namespace vibestudio
{
void ModelEditorDialog::createDocumentBudget(QBoxLayout *row)
{
	// The status strip holds the summary, so the editor keeps its height for
	// the viewport and tool shelf. A status line elides in the strip but keeps
	// the whole summary as its text, readable from the keyboard when cut short.
	auto *summary = new StatusLineLabel;
	m_documentBudget = summary;
	m_documentBudget->setObjectName(QStringLiteral("meshDocumentBudget"));
	m_documentBudget->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Source size and undo history"));
	row->addWidget(m_documentBudget, 1);
	// The limit is a setting for every Mesh Editor, so it sits in the Model
	// menu beside Keep Local Recovery Copies and opens a small window: the
	// status strip keeps its room at any text scale or translation.
	auto *dialog = new QDialog(this);
	dialog->setObjectName(QStringLiteral("meshHistoryLimitDialog"));
	dialog->setWindowTitle(QCoreApplication::translate("VibeStudioModelEditor", "Undo History Limit"));
	auto *layout = new QVBoxLayout(dialog);
	const auto description = QCoreApplication::translate(
		"VibeStudioModelEditor", "The most memory undo and redo keep beyond the current model, for every Mesh Editor. Steps that "
								 "share unchanged data cost only what they changed. When an edit fills it, the oldest undo steps "
								 "are discarded first; the step before the current one and every redo step are always kept.");
	auto *about = new QLabel(description);
	about->setObjectName(QStringLiteral("meshHistoryLimitAbout"));
	about->setTextFormat(Qt::PlainText);
	about->setWordWrap(true);
	layout->addWidget(about);
	auto *form = new QFormLayout;
	// No mnemonic: Alt+H reveals hidden geometry in the Blender profile.
	auto *label = new QLabel(QCoreApplication::translate("VibeStudioModelEditor", "Undo history limit:"));
	label->setObjectName(QStringLiteral("meshHistoryLimitLabel"));
	m_historyLimit = new QSpinBox;
	m_historyLimit->setObjectName(QStringLiteral("meshHistoryLimit"));
	m_historyLimit->setRange(16, 4096);
	m_historyLimit->setSingleStep(16);
	m_historyLimit->setSuffix(QCoreApplication::translate("VibeStudioModelEditor", " MiB"));
	m_historyLimit->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Undo history limit"));
	m_historyLimit->setAccessibleDescription(description);
	m_historyLimit->setToolTip(description);
	label->setBuddy(m_historyLimit);
	form->addRow(label, m_historyLimit);
	layout->addLayout(form);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	// Enter commits a typed limit and leaves the window open to show it took.
	buttons->button(QDialogButtonBox::Close)->setAutoDefault(false);
	connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	layout->addWidget(buttons);
	auto *open = new QAction(QCoreApplication::translate("VibeStudioModelEditor", "Undo History Limit…"), this);
	open->setObjectName(QStringLiteral("meshHistoryLimitAction"));
	open->setToolTip(description);
	open->setStatusTip(description);
	connect(open, &QAction::triggered, this,
			[this, dialog]()
			{
				fitDialogToScreen(dialog, dialog->sizeHint().expandedTo(QSize(dialog->fontMetrics().averageCharWidth() * 60, 0)));
				dialog->open();
				m_historyLimit->setFocus();
			});
	// A document command, so command search finds it and long work pauses it.
	m_documentActions << open;
	m_modelMenu->addAction(open);
	const int limit = StudioSettings(StudioSettings::AccessMode::ReadOnly).modelHistoryBudgetMiB();
	m_historyLimit->setValue(limit);
	setModelDocumentHistoryLimit(qint64(limit) * 1024 * 1024);
	// Only a committed value applies: typing 256 must not trim history to 25
	// MiB on the way. Arrow keys and the step buttons still apply each step.
	m_historyLimit->setKeyboardTracking(false);
	connect(m_historyLimit, qOverload<int>(&QSpinBox::valueChanged), this,
			[this](int mib)
			{
				StudioSettings settings;
				settings.setModelHistoryBudgetMiB(mib);
				settings.sync();
				setModelDocumentHistoryLimit(qint64(settings.modelHistoryBudgetMiB()) * 1024 * 1024);
				if (!m_working)
				{
					const int discarded = m_document.discardedHistorySteps();
					m_document.enforceHistoryLimit();
					m_undo->setEnabled(m_document.canUndo());
					m_redo->setEnabled(m_document.canRedo());
					// A lower limit discards at once, so say so as an edit would.
					if (m_document.discardedHistorySteps() > discarded)
					{
						m_historyNoticeShown = false;
						noteHistoryTrim(discarded);
					}
				}
				refreshDocumentBudget();
			});
}

void ModelEditorDialog::refreshDocumentBudget()
{
	if (!m_documentBudget)
	{
		return;
	}
	const QLocale locale;
	const auto source = modelMebibytesText(m_document.sourceBytes(), locale);
	const auto history = modelMebibytesText(m_document.historyBytes(), locale);
	const auto limit = modelMebibytesText(modelDocumentHistoryLimit(), locale);
	const bool over = m_document.sourceBytes() > modelDocumentMaxSourceBytes;
	const int undoSteps = m_document.undoSteps(), redoSteps = m_document.redoSteps();
	static_cast<StatusLineLabel *>(m_documentBudget)
		->setText(over ? QCoreApplication::translate("VibeStudioModelEditor", "Source %1, over the 64 MiB limit · Undo: %n step(s), %2 of %3",
														 nullptr, undoSteps)
								 .arg(source, history, limit)
						   : QCoreApplication::translate("VibeStudioModelEditor", "Source %1 of 64 MiB · Undo: %n step(s), %2 of %3", nullptr,
														 undoSteps)
								 .arg(source, history, limit));
	// The full account, for the tooltip and screen readers.
	QString text = over ? QCoreApplication::translate("VibeStudioModelEditor", "Source: %1, over the 64 MiB limit. Saving and recovery "
																			   "copies are unavailable until edits make it smaller.")
							  .arg(source)
						: QCoreApplication::translate("VibeStudioModelEditor", "Source: %1 of the 64 MiB limit.").arg(source);
	text += QLatin1Char(' ');
	text += m_document.discardedHistorySteps() > 0
		? QCoreApplication::translate("VibeStudioModelEditor", "Undo history: %n step(s), %1 of %2; older steps were discarded to stay "
															   "within it.", nullptr, undoSteps)
			  .arg(history, limit)
		: QCoreApplication::translate("VibeStudioModelEditor", "Undo history: %n step(s), %1 of %2.", nullptr, undoSteps).arg(history, limit);
	m_documentBudget->setAccessibleDescription(text);
	m_documentBudget->setToolTip(
		text + QStringLiteral("\n\n") +
		QCoreApplication::translate("VibeStudioModelEditor",
									"The size is that of the packed source, or of the readable one when only that fits, which the "
									"64 MiB limit for saving and recovery applies to. Sources stay readable JSON while that form fits, "
									"so a saved file can be larger. An edit that would pass the limit is refused. Model > Undo "
									"History Limit… sets how much memory undo and redo keep."));
	m_undo->setToolTip(QCoreApplication::translate("VibeStudioModelEditor", "Undo: %n step(s) available, %1 of history kept.", nullptr, undoSteps)
						   .arg(history));
	m_redo->setToolTip(QCoreApplication::translate("VibeStudioModelEditor", "Redo: %n step(s) available.", nullptr, redoSteps));
}

QString ModelEditorDialog::historyTrimNotice() const
{
	return QCoreApplication::translate("VibeStudioModelEditor", "Undo history reached its %1 limit, so the oldest steps were discarded. "
																"Raise it in Model > Undo History Limit… to keep more.")
		.arg(modelMebibytesText(modelDocumentHistoryLimit(), QLocale()));
}

void ModelEditorDialog::noteHistoryTrim(int discardedBefore)
{
	if (m_historyNoticeShown || m_document.discardedHistorySteps() <= discardedBefore)
	{
		return;
	}
	m_historyNoticeShown = true;
	const auto notice = historyTrimNotice();
	m_status->setText(m_status->text().isEmpty() ? notice : m_status->text() + QLatin1Char(' ') + notice);
}

} // namespace vibestudio
