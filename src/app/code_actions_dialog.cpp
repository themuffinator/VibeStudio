#include "app/code_actions_dialog.h"
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace vibestudio {
CodeActionsDialog::CodeActionsDialog(const LanguageCodeActions& actions, const QString& provider, QWidget* parent)
	: QDialog(parent), m_actions(actions), m_list(new QListWidget(this)), m_details(new QLabel(this))
{
	setObjectName(QStringLiteral("codeActionsDialog"));
	setWindowTitle(QCoreApplication::translate("CodeActionsDialog", "Code Actions")); setAccessibleName(windowTitle());
	auto* layout = new QVBoxLayout(this);
	auto* source = new QLabel(QCoreApplication::translate("CodeActionsDialog", "Provider: %1").arg(provider)); source->setTextFormat(Qt::PlainText); source->setWordWrap(true); layout->addWidget(source);
	m_list->setObjectName(QStringLiteral("codeActionsList")); m_list->setAccessibleName(QCoreApplication::translate("CodeActionsDialog", "Available and unavailable code actions"));
	m_list->setAccessibleDescription(QCoreApplication::translate("CodeActionsDialog", "Select an action to inspect its availability, then choose Preview Edits."));
	int firstAvailable = -1;
	for (const auto& action : actions.items) {
		QString label = action.title;
		if (action.preferred) { label += QCoreApplication::translate("CodeActionsDialog", " [preferred]"); }
		if (!action.available()) { label += QCoreApplication::translate("CodeActionsDialog", " [unavailable]"); }
		auto* item = new QListWidgetItem(label, m_list); item->setToolTip(action.disabledReason);
		item->setData(Qt::AccessibleDescriptionRole, action.disabledReason);
		if (firstAvailable < 0 && action.available()) { firstAvailable = m_list->count() - 1; }
	}
	layout->addWidget(m_list, 1);
	m_details->setTextFormat(Qt::PlainText); m_details->setWordWrap(true); m_details->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
	m_details->setAccessibleName(QCoreApplication::translate("CodeActionsDialog", "Selected action details")); layout->addWidget(m_details);
	if (actions.limited || actions.skipped) {
		auto* note = new QLabel(QCoreApplication::translate("CodeActionsDialog", "The action list is partial. Invalid entries omitted: %1. Request again if the expected action is missing.").arg(actions.skipped));
		note->setTextFormat(Qt::PlainText); note->setWordWrap(true); layout->addWidget(note);
	}
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel); m_preview = buttons->addButton(QCoreApplication::translate("CodeActionsDialog", "Preview Edits"), QDialogButtonBox::AcceptRole);
	m_preview->setObjectName(QStringLiteral("codeActionPreview")); m_preview->setAccessibleName(m_preview->text()); layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &CodeActionsDialog::accept); connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_list, &QListWidget::itemActivated, this, [this]() { accept(); });
	connect(m_list, &QListWidget::currentRowChanged, this, [this](int index) {
		const bool valid = index >= 0 && index < m_actions.items.size(); m_preview->setEnabled(valid && m_actions.items[index].available());
		if (!valid) { m_details->clear(); return; }
		const auto& action = m_actions.items[index];
		m_details->setText(action.title + (action.kind.isEmpty() ? QString() : QCoreApplication::translate("CodeActionsDialog", "\nKind: %1").arg(action.kind)) + QLatin1Char('\n')
			+ (!action.available() ? action.disabledReason : action.needsResolve ? QCoreApplication::translate("CodeActionsDialog", "The provider will resolve this action before preparing its edit preview.") : QCoreApplication::translate("CodeActionsDialog", "Review the proposed text edits before applying them.")));
	});
	m_preview->setEnabled(false); if (!actions.items.isEmpty()) { m_list->setCurrentRow(firstAvailable < 0 ? 0 : firstAvailable); }
	setMinimumSize(420, 300); resize(640, 440); m_list->setFocus();
}
int CodeActionsDialog::selectedAction() const { return m_list->currentRow(); }
void CodeActionsDialog::accept()
{
	const int index = selectedAction(); if (index >= 0 && index < m_actions.items.size() && m_actions.items[index].available()) { QDialog::accept(); }
}
} // namespace vibestudio
