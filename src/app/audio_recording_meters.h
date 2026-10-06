#pragma once
#include "app/audio_recording.h"
#include <QWidget>

class QLabel;
class QTreeWidget;
class QPushButton;
namespace vibestudio
{
class AudioRecordingMeters final : public QWidget {
	Q_OBJECT
  public:
	explicit AudioRecordingMeters(QWidget *parent = nullptr);
	void setPass(const AudioSession &, const AudioRecordingPlan &);
	void setSnapshot(const AudioRecordingSnapshot &, bool resetPending);
  Q_SIGNALS:
	void resetRequested();

  private:
	void refresh();
	struct Row {
		int arm, channel;
	};
	QVector<Row> m_rows;
	AudioRecordingSnapshot m_snapshot;
	QString m_name;
	bool m_resetPending = false;
	QLabel *m_status = nullptr, *m_detail = nullptr;
	QTreeWidget *m_table = nullptr;
	QPushButton *m_reset = nullptr;
};
} // namespace vibestudio
