#pragma once
#include "core/audio_arrangement.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
namespace vibestudio
{
class AudioArrangementDialog final : public QDialog {
	Q_OBJECT
  public:
	AudioArrangementDialog(const AudioSession &session, const QStringList &selection, bool linkedGroups, qint64 cursor,
	                       QWidget *parent = nullptr);
	[[nodiscard]] AudioArrangementEdit edit() const;
	void accept() override;

  private:
	void refresh();
	AudioSession m_session;
	QStringList m_selection;
	QFormLayout *m_form = nullptr;
	QComboBox *m_operation = nullptr;
	QCheckBox *m_linked = nullptr, *m_muted = nullptr;
	QDoubleSpinBox *m_offset = nullptr, *m_cursor = nullptr, *m_gain = nullptr, *m_fadeIn = nullptr,
	               *m_fadeOut = nullptr;
	QLineEdit *m_name = nullptr;
	QLabel *m_summary = nullptr, *m_error = nullptr;
};
} // namespace vibestudio
