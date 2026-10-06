#pragma once
#include "core/audio_session.h"
#include <QDialog>
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
namespace vibestudio
{
class AudioRoutingDialog final : public QDialog {
	Q_OBJECT
  public:
	AudioRoutingDialog(const AudioSession &session, const QString &trackId, QWidget *parent = nullptr);
	[[nodiscard]] AudioSessionEdit edit() const;

  private:
	void validate();
	void refreshSends(int selection);
	void selectSend();
	void updateSend();
	void destinations(QComboBox *combo);
	AudioSession m_draft;
	int m_track = -1;
	bool m_updating = false;
	QComboBox *m_output = nullptr, *m_target = nullptr, *m_tap = nullptr;
	QCheckBox *m_outputEnabled = nullptr, *m_left = nullptr, *m_right = nullptr, *m_swap = nullptr,
	          *m_enabled = nullptr;
	QDoubleSpinBox *m_gain = nullptr, *m_pan = nullptr;
	QListWidget *m_sends = nullptr;
	QWidget *m_sendForm = nullptr;
	QPushButton *m_add = nullptr, *m_remove = nullptr;
	QLabel *m_status = nullptr;
	QDialogButtonBox *m_buttons = nullptr;
};
} // namespace vibestudio
