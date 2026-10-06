#pragma once
#include "core/audio_session.h"
#include "core/audio_waveform.h"
#include <QSet>
#include <QWidget>

namespace vibestudio
{
class AudioSessionTimeline final : public QWidget {
	Q_OBJECT
  public:
	explicit AudioSessionTimeline(QWidget *parent = nullptr);
	void setSession(const AudioSession &session, const QHash<QString, AudioWaveformData> &waveforms);
	void setSelection(const QStringList &ids);
	void setSelected(const QString &track, const QString &region);
	void setVisibleRange(qint64 first, qint64 end);
	void setTimeRange(qint64 first, qint64 end);
	void setCursor(qint64 frame);
	void setSnapFrames(qint64 frames);
	void setMusicalSnap(AudioMusicalGrid grid);
	void setMusicalRuler(bool enabled);
	[[nodiscard]] qint64 visibleFirst() const { return m_first; }
	[[nodiscard]] qint64 visibleEnd() const { return m_end; }
	[[nodiscard]] qint64 frameAt(double x) const;
	[[nodiscard]] QRectF regionRect(int track, const AudioSessionRegion &region) const;
	[[nodiscard]] QString hitRegion(const QPointF &point, QString *track = nullptr) const;
	QSize sizeHint() const override;
  Q_SIGNALS:
	void selectionRequested(const QString &track, const QString &region, Qt::KeyboardModifiers modifiers);
	void selected(const QString &track, const QString &region);
	void cursorChanged(qint64 frame);
	void moveRequested(const QString &track, const QString &region, qint64 frame);

  protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

  private:
	int rulerHeight() const;
	int laneHeight() const;
	int labelWidth() const;
	double xFor(qint64 frame) const;
	qint64 snapped(qint64 frame) const;
	qint64 stepped(qint64 frame, int direction) const;
	void paintMusicalRuler(QPainter &painter);
	void describe();
	AudioSession m_session;
	AudioTempoTimeline m_time;
	AudioMusicalGrid m_grid = AudioMusicalGrid::Beat;
	bool m_musicalSnap = false, m_musicalRuler = false;
	QHash<QString, AudioWaveformData> m_waveforms;
	QString m_track, m_region;
	QSet<QString> m_selection;
	qint64 m_first = 0, m_end = 48000 * 10, m_cursor = 0, m_snap = 1;
	qint64 m_rangeFirst = 0, m_rangeEnd = 0;
	qint64 m_dragAnchor = 0, m_dragOriginal = 0, m_dragPosition = 0;
	bool m_dragging = false;
};
} // namespace vibestudio
