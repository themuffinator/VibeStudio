#pragma once

#include "core/audio_waveform.h"

#include <QWidget>

namespace vibestudio
{

// Frame-accurate authoring view; the browser's time-based preview remains a
// separate widget. Display data is prepared on the editor's worker thread.
class AudioWaveformView final : public QWidget
{
	Q_OBJECT
public:
	explicit AudioWaveformView(QWidget* parent = nullptr);
	void setData(const AudioWaveformData& data, bool resetView = false);
	void setMarkers(const AudioMarkers& markers);
	void setSelectionFrames(qint64 first, qint64 end);
	void setPlayheadFrame(qint64 frame);
	void setHighContrast(bool enabled);
	void setVisibleRange(qint64 first, qint64 end);
	void zoomIn();
	void zoomOut();
	void zoomToFit();
	void zoomToSelection();
	void panFrames(qint64 delta);
	void moveCursor(qint64 frame, bool extendSelection = false);
	[[nodiscard]] qint64 visibleStart() const { return m_visibleFirst; }
	[[nodiscard]] qint64 visibleEnd() const { return m_visibleEnd; }
	[[nodiscard]] qint64 selectionStart() const { return m_first; }
	[[nodiscard]] qint64 selectionEnd() const { return m_end; }
	[[nodiscard]] qint64 playheadFrame() const { return m_playhead; }
	[[nodiscard]] double xForFrame(qint64 frame) const;
	[[nodiscard]] qint64 frameForX(double x) const;
	[[nodiscard]] QString accessibleSummary() const;
	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

signals:
	void selectionChanged(qint64 firstFrame, qint64 endFrame);
	void seekRequested(qint64 frame);
	void viewChanged(qint64 firstFrame, qint64 endFrame);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void focusInEvent(QFocusEvent* event) override;
	void focusOutEvent(QFocusEvent* event) override;
	void changeEvent(QEvent* event) override;

private:
	[[nodiscard]] QRectF lanes() const;
	[[nodiscard]] QRectF overview() const;
	void zoom(double factor, qint64 anchor);
	void overviewAt(double x);
	void updateDescription();
	void updateMinimumHeight();
	AudioWaveformData m_data;
	AudioMarkers m_markers;
	qint64 m_visibleFirst = 0, m_visibleEnd = 0;
	qint64 m_first = 0, m_end = 0, m_anchor = 0, m_cursor = 0, m_playhead = 0;
	bool m_highContrast = false;
	bool m_selecting = false, m_panning = false, m_overviewDragging = false;
	double m_panX = 0;
	qint64 m_panFirst = 0;
};

} // namespace vibestudio
