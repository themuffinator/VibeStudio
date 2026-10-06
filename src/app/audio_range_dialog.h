#pragma once
#include "core/audio_range.h"
#include <QDialog>
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
namespace vibestudio
{
class AudioRangeDialog final : public QDialog {
	Q_OBJECT
  public:
	AudioRangeDialog(const AudioSession &session, const QStringList &tracks, qint64 first, qint64 end,
	                 QWidget *parent = nullptr);
	[[nodiscard]] AudioRangeEdit edit() const;
	void accept() override;

  private:
	void refresh();
	AudioSession m_session;
	QComboBox *m_operation = nullptr;
	QCheckBox *m_all = nullptr, *m_follow = nullptr, *m_master = nullptr;
	QListWidget *m_tracks = nullptr;
	QDoubleSpinBox *m_first = nullptr, *m_end = nullptr;
	QLabel *m_summary = nullptr, *m_error = nullptr;
};
} // namespace vibestudio
