#pragma once
#include "core/language_code_actions.h"
#include <QDialog>

class QListWidget;
class QLabel;
class QPushButton;

namespace vibestudio {
class CodeActionsDialog final : public QDialog {
public:
	CodeActionsDialog(const LanguageCodeActions& actions, const QString& provider, QWidget* parent = nullptr);
	int selectedAction() const;
	void accept() override;
private:
	LanguageCodeActions m_actions;
	QListWidget* m_list;
	QLabel* m_details;
	QPushButton* m_preview;
};
} // namespace vibestudio
