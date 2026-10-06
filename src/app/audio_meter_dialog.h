#pragma once

#include "app/audio_session_playback.h"
#include <QDialog>

class QComboBox;
class QLabel;
class QPushButton;
class QTreeWidget;
namespace vibestudio
{
class AudioMeterDialog final : public QDialog {
	Q_OBJECT
  public:
	explicit AudioMeterDialog(QWidget *parent = nullptr);
	void setContext(const AudioSession &session, quint64 revision, qint64 first, qint64 end);
	void setPlayback(const AudioSessionPlaybackSnapshot &snapshot);
	void setReport(const AudioMeterReport &report);
	void setBusy(bool busy, bool analysis = false);
	[[nodiscard]] AudioMeterSnapshot meters() const { return m_meters; }
  Q_SIGNALS:
	void analyzeRequested();
	void resetRequested();
	void cancelRequested();

  private:
	void refresh();
	AudioMeterSnapshot m_meters;
	QTreeWidget *m_table = nullptr;
	QComboBox *m_tap = nullptr;
	QLabel *m_status = nullptr, *m_range = nullptr, *m_detail = nullptr;
	QPushButton *m_analyze = nullptr, *m_reset = nullptr, *m_cancel = nullptr;
	quint64 m_revision = 0;
	bool m_hasContext = false, m_offline = false, m_busy = false;
	qint64 m_first = 0, m_end = 0;
};
} // namespace vibestudio
