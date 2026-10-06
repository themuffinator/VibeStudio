#pragma once
#include "core/audio_tempo.h"
#include <QDialog>
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QLineEdit;
class QTreeWidget;
class QLabel;
class QPushButton;

namespace vibestudio
{
class AudioTempoDialog final : public QDialog {
	Q_OBJECT
  public:
	AudioTempoDialog(const AudioTempoMap &map, int sampleRate, qint64 cursor, QWidget *parent = nullptr);
	[[nodiscard]] const AudioTempoMap &tempoMap() const { return m_map; }

  private:
	void refresh();
	bool replace(const AudioTempoMap &map);
	void setTempo();
	void setMeter();
	AudioTempoMap m_map;
	AudioTempoTimeline m_time;
	int m_rate;
	QTreeWidget *m_tempos = nullptr, *m_meters = nullptr;
	QLineEdit *m_position = nullptr;
	QDoubleSpinBox *m_bpm = nullptr, *m_bar = nullptr;
	QSpinBox *m_beats = nullptr;
	QComboBox *m_unit = nullptr;
	QLabel *m_status = nullptr;
	QPushButton *m_removeTempo = nullptr, *m_removeMeter = nullptr;
};
} // namespace vibestudio
