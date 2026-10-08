#pragma once

#include "core/editor_profiles.h"
#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;
class QTextBrowser;

namespace vibestudio {

class EditorProfileDialog final : public QDialog {
	Q_OBJECT
public:
	explicit EditorProfileDialog(const QString& currentProfileId, QWidget* parent = nullptr);
	[[nodiscard]] QString selectedProfileId() const;

private:
	void refreshPreview();
	QVector<EditorProfileDescriptor> m_profiles;
	QListWidget* m_list = nullptr;
	QTextBrowser* m_preview = nullptr;
	QPushButton* m_apply = nullptr;
	QLabel* m_count = nullptr;
};

} // namespace vibestudio
