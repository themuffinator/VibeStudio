#include "app/audio_waveform_view.h"

#include <QAccessible>
#include <QAccessibleWidget>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
QAccessibleInterface* waveformAccessibility(const QString&, QObject* object)
{
	if (auto* waveform = qobject_cast<AudioWaveformView*>(object)) {
		return new QAccessibleWidget(waveform, QAccessible::Graphic);
	}
	return nullptr;
}
} // namespace

AudioWaveformView::AudioWaveformView(QWidget* parent) : QWidget(parent)
{
	static const bool installed = []() {
		QAccessible::installFactory(waveformAccessibility);
		return true;
	}();
	Q_UNUSED(installed);
	setFocusPolicy(Qt::StrongFocus);
	setAccessibleName(tr("Editable audio waveform"));
	setToolTip(
	    tr("Drag to select frames; Shift extends selection. Left/Right move one frame. "
	       "Ctrl+wheel zooms; wheel, middle-drag, or the overview pans. Frame fields provide exact boundaries."));
	updateMinimumHeight();
	updateDescription();
}

QRectF AudioWaveformView::lanes() const
{
	const int line = fontMetrics().height();
	return QRectF(8, line + 10, std::max(1, width() - 16), std::max(1, height() - line * 3 - 50));
}

QRectF AudioWaveformView::overview() const
{
	return QRectF(8, height() - fontMetrics().height() - 24, std::max(1, width() - 16), fontMetrics().height() + 16);
}

void AudioWaveformView::setData(const AudioWaveformData& data, bool resetView)
{
	const bool wasFit = m_visibleFirst == 0 && m_visibleEnd == m_data.clip().frameCount();
	const qint64 span = m_visibleEnd - m_visibleFirst;
	m_data = data;
	m_markers = data.clip().markers;
	const qint64 frames = m_data.clip().frameCount();
	m_first = std::min(m_first, frames);
	m_end = std::min(m_end, frames);
	m_anchor = std::min(m_anchor, frames);
	m_cursor = std::min(m_cursor, frames);
	m_playhead = std::min(m_playhead, frames);
	if (resetView) {
		m_first = m_end = m_anchor = m_cursor = m_playhead = 0;
	}
	setVisibleRange(resetView || wasFit ? 0 : m_visibleFirst, resetView || wasFit ? frames : m_visibleFirst + span);
	updateMinimumHeight();
	updateDescription();
	update();
}

void AudioWaveformView::setMarkers(const AudioMarkers& markers)
{
	if (m_markers == markers) {
		return;
	}
	m_markers = markers;
	updateDescription();
	update();
}

void AudioWaveformView::setSelectionFrames(qint64 first, qint64 end)
{
	first = std::clamp<qint64>(first, 0, m_data.clip().frameCount());
	end = std::clamp(end, first, m_data.clip().frameCount());
	if (first == m_first && end == m_end) {
		return;
	}
	m_first = m_anchor = first;
	m_end = m_cursor = end;
	updateDescription();
	update();
}

void AudioWaveformView::setPlayheadFrame(qint64 frame)
{
	frame = std::clamp<qint64>(frame, 0, m_data.clip().frameCount());
	if (m_playhead == frame) {
		return;
	}
	m_playhead = frame;
	update();
}

void AudioWaveformView::setHighContrast(bool enabled)
{
	m_highContrast = enabled;
	update();
}

void AudioWaveformView::setVisibleRange(qint64 first, qint64 end)
{
	const qint64 frames = m_data.clip().frameCount();
	// Public view requests may lie outside the clip (panning at either edge).
	// Bound both before subtraction to avoid signed overflow from invalid callers.
	first = std::clamp(first, -AudioSampleLimit, AudioSampleLimit);
	end = std::clamp(end, -AudioSampleLimit, AudioSampleLimit * 2);
	const qint64 span = std::clamp<qint64>(end - first, frames > 0 ? 1 : 0, frames);
	first = std::clamp<qint64>(first, 0, frames - span);
	end = first + span;
	if (first == m_visibleFirst && end == m_visibleEnd) {
		return;
	}
	m_visibleFirst = first;
	m_visibleEnd = end;
	updateDescription();
	update();
	emit viewChanged(first, end);
}

void AudioWaveformView::zoom(double factor, qint64 anchor)
{
	const qint64 oldSpan = m_visibleEnd - m_visibleFirst;
	if (oldSpan == 0) {
		return;
	}
	anchor = std::clamp(anchor, m_visibleFirst, m_visibleEnd);
	const qint64 span = std::clamp<qint64>(qRound64(oldSpan * factor), 1, m_data.clip().frameCount());
	const double fraction = static_cast<double>(anchor - m_visibleFirst) / oldSpan;
	const qint64 first = anchor - qRound64(fraction * span);
	setVisibleRange(first, first + span);
}

void AudioWaveformView::zoomIn()
{
	zoom(0.5, m_cursor >= m_visibleFirst && m_cursor <= m_visibleEnd ? m_cursor : (m_visibleFirst + m_visibleEnd) / 2);
}
void AudioWaveformView::zoomOut() { zoom(2.0, (m_visibleFirst + m_visibleEnd) / 2); }
void AudioWaveformView::zoomToFit() { setVisibleRange(0, m_data.clip().frameCount()); }
void AudioWaveformView::zoomToSelection()
{
	if (m_end > m_first) {
		setVisibleRange(m_first, m_end);
	}
}
void AudioWaveformView::panFrames(qint64 delta)
{
	const qint64 span = m_visibleEnd - m_visibleFirst;
	// Saturate before adding a public caller's offset.
	delta = std::clamp(delta, -m_visibleFirst, m_data.clip().frameCount() - m_visibleEnd);
	setVisibleRange(m_visibleFirst + delta, m_visibleFirst + delta + span);
}

double AudioWaveformView::xForFrame(qint64 frame) const
{
	const auto area = lanes();
	return area.left() + static_cast<double>(frame - m_visibleFirst) * area.width() /
	                         std::max<qint64>(1, m_visibleEnd - m_visibleFirst);
}

qint64 AudioWaveformView::frameForX(double x) const
{
	const auto area = lanes();
	const double fraction = std::clamp((x - area.left()) / area.width(), 0.0, 1.0);
	return m_visibleFirst + qRound64(fraction * (m_visibleEnd - m_visibleFirst));
}

void AudioWaveformView::moveCursor(qint64 frame, bool extendSelection)
{
	frame = std::clamp<qint64>(frame, 0, m_data.clip().frameCount());
	if (!extendSelection) {
		m_anchor = frame;
	}
	m_cursor = frame;
	m_first = std::min(frame, m_anchor);
	m_end = std::max(frame, m_anchor);
	setPlayheadFrame(frame);
	const qint64 span = m_visibleEnd - m_visibleFirst;
	if (frame < m_visibleFirst) {
		setVisibleRange(frame, frame + span);
	} else if (frame > m_visibleEnd) {
		setVisibleRange(frame - span, frame);
	}
	updateDescription();
	update();
	emit selectionChanged(m_first, m_end);
	emit seekRequested(frame);
}

void AudioWaveformView::overviewAt(double x)
{
	const auto area = overview();
	const double fraction = std::clamp((x - area.left()) / area.width(), 0.0, 1.0);
	const qint64 span = m_visibleEnd - m_visibleFirst;
	const qint64 first = qRound64(fraction * m_data.clip().frameCount()) - span / 2;
	setVisibleRange(first, first + span);
}

QString AudioWaveformView::accessibleSummary() const
{
	QString description =
	    tr("%1 channels, %2 Hz, %3 frames. Selection %4 to %5, end exclusive. Visible frames %6 to %7. "
	       "Left/Right moves one frame; Shift extends selection; Page Up/Down moves a tenth of the view; "
	       "Home/End moves to the sound boundaries.")
	        .arg(m_data.clip().channels)
	        .arg(m_data.clip().sampleRate)
	        .arg(m_data.clip().frameCount())
	        .arg(m_first)
	        .arg(m_end)
	        .arg(m_visibleFirst)
	        .arg(m_visibleEnd);
	description += QLatin1Char(' ') + tr("Cue markers: %1.").arg(m_markers.cues.size());
	if (m_markers.loop) {
		description += QLatin1Char(' ') +
		               tr("Forward loop: %1 to %2, end exclusive.").arg(m_markers.loop->first).arg(m_markers.loop->end);
	}
	return description;
}

void AudioWaveformView::updateDescription()
{
	setAccessibleDescription(accessibleSummary());
	QAccessibleEvent changed(this, QAccessible::DescriptionChanged);
	QAccessible::updateAccessibility(&changed);
}

void AudioWaveformView::updateMinimumHeight()
{
	setMinimumHeight(std::max(190, std::max(1, m_data.clip().channels) * (fontMetrics().height() * 2 + 12) +
	                                   fontMetrics().height() * 3 + 50));
}
QSize AudioWaveformView::sizeHint() const { return QSize(740, minimumHeight()); }
QSize AudioWaveformView::minimumSizeHint() const { return QSize(180, minimumHeight()); }

void AudioWaveformView::mousePressEvent(QMouseEvent* event)
{
	if (m_data.clip().frameCount() == 0) {
		return;
	}
	if (event->button() == Qt::MiddleButton) {
		m_panning = true;
		m_panX = event->position().x();
		m_panFirst = m_visibleFirst;
	} else if (event->button() == Qt::LeftButton) {
		setFocus(Qt::MouseFocusReason);
		if (overview().contains(event->position())) {
			m_overviewDragging = true;
			overviewAt(event->position().x());
		} else if (lanes().contains(event->position())) {
			m_selecting = true;
			moveCursor(frameForX(event->position().x()), event->modifiers().testFlag(Qt::ShiftModifier));
		}
	} else {
		QWidget::mousePressEvent(event);
		return;
	}
	event->accept();
}

void AudioWaveformView::mouseMoveEvent(QMouseEvent* event)
{
	if (m_panning) {
		const qint64 span = m_visibleEnd - m_visibleFirst;
		const qint64 first = m_panFirst - qRound64((event->position().x() - m_panX) * span / lanes().width());
		setVisibleRange(first, first + span);
	} else if (m_overviewDragging) {
		overviewAt(event->position().x());
	} else if (m_selecting) {
		moveCursor(frameForX(event->position().x()), true);
	} else {
		QWidget::mouseMoveEvent(event);
	}
}

void AudioWaveformView::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton) {
		m_selecting = m_overviewDragging = false;
	}
	if (event->button() == Qt::MiddleButton) {
		m_panning = false;
	}
	QWidget::mouseReleaseEvent(event);
}

void AudioWaveformView::wheelEvent(QWheelEvent* event)
{
	const QPoint delta = event->angleDelta().isNull() ? event->pixelDelta() * 3 : event->angleDelta();
	const double steps = (delta.y() == 0 ? delta.x() : delta.y()) / 120.0;
	if (event->modifiers().testFlag(Qt::ControlModifier)) {
		zoom(std::pow(2.0, -std::clamp(steps, -8.0, 8.0)), frameForX(event->position().x()));
	} else {
		panFrames(qRound64(-steps * std::max<qint64>(1, (m_visibleEnd - m_visibleFirst) / 8)));
	}
	event->accept();
}

void AudioWaveformView::keyPressEvent(QKeyEvent* event)
{
	qint64 frame = m_cursor;
	const qint64 page = std::max<qint64>(1, (m_visibleEnd - m_visibleFirst) / 10);
	switch (event->key()) {
	case Qt::Key_Left:
		--frame;
		break;
	case Qt::Key_Right:
		++frame;
		break;
	case Qt::Key_PageUp:
		frame -= page;
		break;
	case Qt::Key_PageDown:
		frame += page;
		break;
	case Qt::Key_Home:
		frame = 0;
		break;
	case Qt::Key_End:
		frame = m_data.clip().frameCount();
		break;
	default:
		QWidget::keyPressEvent(event);
		return;
	}
	moveCursor(frame, event->modifiers().testFlag(Qt::ShiftModifier));
	event->accept();
}
void AudioWaveformView::focusInEvent(QFocusEvent* event)
{
	QWidget::focusInEvent(event);
	update();
}
void AudioWaveformView::focusOutEvent(QFocusEvent* event)
{
	QWidget::focusOutEvent(event);
	update();
}
void AudioWaveformView::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::FontChange) {
		updateMinimumHeight();
	}
	if (event->type() == QEvent::LanguageChange) {
		updateDescription();
	}
	update();
}

void AudioWaveformView::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	const QColor background = palette().color(QPalette::Base);
	const QColor text = palette().color(QPalette::Text);
	const QColor accent = m_highContrast ? text : palette().color(QPalette::Highlight);
	painter.fillRect(rect(), background);
	if (!m_data.valid() || m_data.clip().frameCount() == 0) {
		painter.setPen(text);
		painter.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap,
		                 tr("Empty sound. Paste audio or insert silence to begin."));
		return;
	}
	const auto area = lanes();
	const auto span = m_visibleEnd - m_visibleFirst;
	const auto& clip = m_data.clip();
	const int columns = std::max(1, static_cast<int>(area.width()));
	const qreal laneHeight = area.height() / clip.channels;
	// Fill first, then draw envelopes and dashed boundaries; selected audio
	// remains legible without relying on colour alone.
	const QRectF selected(xForFrame(m_first), area.top(), xForFrame(m_end) - xForFrame(m_first), area.height());
	QColor selection = accent;
	selection.setAlpha(m_highContrast ? 45 : 35);
	painter.fillRect(selected.intersected(area), selection);
	painter.save();
	painter.setClipRect(area);
	for (int channel = 0; channel < clip.channels; ++channel) {
		const QRectF lane(area.left(), area.top() + channel * laneHeight, area.width(), laneHeight);
		const qreal center = lane.center().y(), half = lane.height() / 2 - 3;
		painter.setPen(QPen(text, 1, Qt::DotLine));
		painter.drawLine(QPointF(lane.left(), center), QPointF(lane.right(), center));
		painter.setPen(QPen(accent, 1));
		if (span <= columns / 3) {
			QPainterPath line;
			const qint64 end = std::min(m_visibleEnd + 1, clip.frameCount());
			for (qint64 frame = m_visibleFirst; frame < end; ++frame) {
				const double sample = std::clamp<double>(clip.samples[frame * clip.channels + channel], -1, 1);
				const QPointF point(xForFrame(frame), center - sample * half);
				if (frame == m_visibleFirst) {
					line.moveTo(point);
				} else {
					line.lineTo(point);
				}
				painter.drawEllipse(point, 2, 2);
			}
			painter.drawPath(line);
		} else {
			for (int x = 0; x < columns; ++x) {
				const qint64 first = m_visibleFirst + x * span / columns;
				const qint64 end =
				    std::min(clip.frameCount(), std::max(first + 1, m_visibleFirst + (x + 1) * span / columns));
				const auto range = m_data.range(channel, first, end);
				const qreal top = center - std::clamp<double>(range.maximum, -1, 1) * half;
				const qreal bottom = center - std::clamp<double>(range.minimum, -1, 1) * half;
				painter.drawLine(QPointF(area.left() + x + .5, top),
				                 QPointF(area.left() + x + .5, std::max(top + 1, bottom)));
			}
		}
		const QString label = clip.channels == 1   ? tr("Mono")
		                      : clip.channels == 2 ? (channel == 0 ? tr("Left") : tr("Right"))
		                                           : tr("Channel %1").arg(channel + 1);
		const QRectF chip(lane.left() + 3, lane.top() + 3, fontMetrics().horizontalAdvance(label) + 10,
		                  fontMetrics().height() + 2);
		painter.fillRect(chip, background);
		painter.setPen(text);
		painter.drawText(chip, Qt::AlignCenter, label);
		painter.drawLine(lane.bottomLeft(), lane.bottomRight());
	}
	// Shape and line style distinguish cues, loop boundaries, and selection.
	painter.setPen(QPen(text, m_highContrast ? 2 : 1, Qt::DotLine));
	for (const auto& cue : m_markers.cues) {
		if (cue.frame < m_visibleFirst || cue.frame >= m_visibleEnd) {
			continue;
		}
		const qreal x = xForFrame(cue.frame);
		painter.drawLine(QPointF(x, area.top() + 8), QPointF(x, area.bottom()));
		painter.setBrush(text);
		painter.drawPolygon(
		    QPolygonF{QPointF(x - 4, area.top()), QPointF(x + 4, area.top()), QPointF(x, area.top() + 7)});
		painter.setBrush(Qt::NoBrush);
	}
	if (m_markers.loop && m_markers.loop->first < m_visibleEnd && m_markers.loop->end > m_visibleFirst) {
		const qreal left = std::max(area.left(), xForFrame(m_markers.loop->first));
		const qreal right = std::min(area.right(), xForFrame(m_markers.loop->end));
		const qreal y = area.bottom() - 5;
		painter.setPen(QPen(text, 2));
		painter.drawLine(QPointF(left, y), QPointF(right, y));
		if (m_markers.loop->first >= m_visibleFirst) {
			painter.drawLine(QPointF(left, y - 12), QPointF(left, y));
		}
		if (m_markers.loop->end <= m_visibleEnd) {
			painter.drawLine(QPointF(right, y - 12), QPointF(right, y));
		}
		const QString label = tr("Loop");
		const qreal labelWidth = fontMetrics().horizontalAdvance(label) + 10;
		if (right - left > labelWidth + 8) {
			const QRectF chip((left + right - labelWidth) / 2, y - fontMetrics().height() - 3, labelWidth,
			                  fontMetrics().height() + 2);
			painter.fillRect(chip, background);
			painter.drawText(chip, Qt::AlignCenter, label);
		}
	}
	painter.setPen(QPen(text, m_highContrast ? 2 : 1, Qt::DashLine));
	if (m_end > m_first) {
		painter.drawRect(selected);
	}
	if (m_playhead >= m_visibleFirst && m_playhead <= m_visibleEnd) {
		painter.setPen(QPen(text, 2));
		painter.drawLine(QPointF(xForFrame(m_playhead), area.top()), QPointF(xForFrame(m_playhead), area.bottom()));
	}
	painter.restore();
	painter.setPen(text);
	// Sparse exact frame labels avoid rounding short selections to milliseconds.
	const int labelWidth = fontMetrics().horizontalAdvance(tr("%1 f").arg(QLocale().toString(m_visibleEnd))) + 20;
	const int ticks = columns < labelWidth * 2 ? 0 : std::clamp(columns / std::max(1, labelWidth), 1, 4);
	if (ticks == 0) {
		const QString label = tr("%1–%2 f").arg(QLocale().toString(m_visibleFirst), QLocale().toString(m_visibleEnd));
		painter.drawText(QRectF(area.left(), 2, area.width(), fontMetrics().height() + 4), Qt::AlignCenter,
		                 fontMetrics().elidedText(label, Qt::ElideMiddle, columns));
	}
	for (int tick = 0; ticks > 0 && tick <= ticks; ++tick) {
		const qint64 frame = m_visibleFirst + span * tick / ticks;
		const QString label = tr("%1 f").arg(QLocale().toString(frame));
		const int width = fontMetrics().horizontalAdvance(label);
		const double left =
		    std::clamp(xForFrame(frame) - width / 2.0, area.left(), std::max(area.left(), area.right() - width));
		painter.drawText(QRectF(left, 2, width, fontMetrics().height() + 4), Qt::AlignCenter, label);
	}
	const auto strip = overview();
	painter.drawText(QRectF(strip.left(), area.bottom() + 3, strip.width(), fontMetrics().height() + 2),
	                 Qt::AlignLeft | Qt::AlignVCenter,
	                 tr("Overview · %1 s").arg(clip.frameCount() / double(clip.sampleRate), 0, 'f', 3));
	painter.setPen(QPen(accent, 1));
	for (int x = 0; x < columns; ++x) {
		const qint64 first = x * clip.frameCount() / columns;
		const qint64 end = std::min(clip.frameCount(), std::max(first + 1, (x + 1) * clip.frameCount() / columns));
		AudioExtrema value = m_data.range(0, first, end);
		for (int channel = 1; channel < clip.channels; ++channel) {
			const auto next = m_data.range(channel, first, end);
			value.minimum = std::min(value.minimum, next.minimum);
			value.maximum = std::max(value.maximum, next.maximum);
		}
		painter.drawLine(
		    QPointF(strip.left() + x + .5,
		            strip.center().y() - std::clamp<double>(value.maximum, -1, 1) * strip.height() / 2),
		    QPointF(strip.left() + x + .5,
		            strip.center().y() - std::clamp<double>(value.minimum, -1, 1) * strip.height() / 2 + 1));
	}
	const double scale = strip.width() / clip.frameCount();
	const QRectF visible(strip.left() + m_visibleFirst * scale, strip.top(), std::max(2.0, span * scale),
	                     strip.height());
	painter.fillRect(visible, selection);
	painter.setPen(QPen(text, 2, Qt::DashLine));
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(visible);
	if (hasFocus()) {
		painter.setPen(QPen(text, 2));
		painter.drawRect(rect().adjusted(1, 1, -2, -2));
	}
}

} // namespace vibestudio
