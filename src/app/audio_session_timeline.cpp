#include "app/audio_session_timeline.h"
#include <QAccessible>
#include <QAccessibleWidget>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
QAccessibleInterface *sessionTimelineAccessibility(const QString &, QObject *object)
{
	if (auto *timeline = qobject_cast<AudioSessionTimeline *>(object)) {
		return new QAccessibleWidget(timeline, QAccessible::Graphic);
	}
	return nullptr;
}
} // namespace
AudioSessionTimeline::AudioSessionTimeline(QWidget *parent) : QWidget(parent)
{
	static const bool installed = [] {
		QAccessible::installFactory(sessionTimelineAccessibility);
		return true;
	}();
	Q_UNUSED(installed);
	setObjectName(QStringLiteral("audioSessionTimeline"));
	setAccessibleName(tr("Multitrack arrangement timeline"));
	setToolTip(tr("Select clips and drag to move them. Control/Command toggles selection; Shift adds a clip. "
	              "Left/Right nudges the selection by the snap step; Home moves "
	              "the cursor to zero. Exact frame controls are in the clip inspector."));
	setFocusPolicy(Qt::StrongFocus);
	setLayoutDirection(Qt::LeftToRight); // Time increases to the right in every UI locale.
	setMinimumWidth(360);
	m_time.prepare(m_session.musicalTime, m_session.sampleRate);
	describe();
}
int AudioSessionTimeline::rulerHeight() const { return fontMetrics().height() * (m_musicalRuler ? 3 : 2) + 12; }
int AudioSessionTimeline::laneHeight() const { return fontMetrics().height() * 4 + 16; }
int AudioSessionTimeline::labelWidth() const
{
	return fontMetrics().horizontalAdvance(QStringLiteral("MMMMMMMMMMMM")) + 16;
}
QSize AudioSessionTimeline::sizeHint() const
{
	return {900, rulerHeight() + laneHeight() * std::max(2, int(m_session.tracks.size()))};
}
void AudioSessionTimeline::setSession(const AudioSession &session, const QHash<QString, AudioWaveformData> &waveforms)
{
	m_session = session;
	m_time.prepare(session.musicalTime, session.sampleRate);
	m_waveforms = waveforms;
	setMinimumHeight(sizeHint().height());
	updateGeometry();
	describe();
	update();
}
void AudioSessionTimeline::setSelection(const QStringList &ids)
{
	m_selection = QSet<QString>(ids.cbegin(), ids.cend());
	describe();
	update();
}
void AudioSessionTimeline::setSelected(const QString &track, const QString &region)
{
	m_track = track;
	m_region = region;
	m_selection = region.isEmpty() ? QSet<QString>{} : QSet<QString>{region};
	describe();
	update();
}
void AudioSessionTimeline::setVisibleRange(qint64 first, qint64 end)
{
	m_first = std::clamp<qint64>(first, 0, AudioSessionFrameLimit - 1);
	m_end = std::clamp<qint64>(end, m_first + 1, AudioSessionFrameLimit);
	describe();
	update();
}
void AudioSessionTimeline::setCursor(qint64 frame)
{
	m_cursor = std::clamp<qint64>(frame, 0, AudioSessionFrameLimit);
	describe();
	update();
}
void AudioSessionTimeline::setTimeRange(qint64 first, qint64 end)
{
	m_rangeFirst = std::clamp<qint64>(first, 0, AudioSessionFrameLimit);
	m_rangeEnd = std::clamp<qint64>(end, 0, AudioSessionFrameLimit);
	describe();
	update();
}
void AudioSessionTimeline::setSnapFrames(qint64 frames)
{
	m_snap = std::clamp<qint64>(frames, 1, AudioSessionFrameLimit);
	m_musicalSnap = false;
}
void AudioSessionTimeline::setMusicalSnap(AudioMusicalGrid grid)
{
	m_grid = grid;
	m_musicalSnap = true;
	update();
}
void AudioSessionTimeline::setMusicalRuler(bool enabled)
{
	m_musicalRuler = enabled;
	setMinimumHeight(sizeHint().height());
	updateGeometry();
	describe();
	update();
}
double AudioSessionTimeline::xFor(qint64 frame) const
{
	return labelWidth() + double(frame - m_first) / double(m_end - m_first) * std::max(1, width() - labelWidth() - 8);
}
qint64 AudioSessionTimeline::frameAt(double x) const
{
	const double ratio = std::clamp((x - labelWidth()) / std::max(1, width() - labelWidth() - 8), 0.0, 1.0);
	return m_first + qint64(std::llround(ratio * double(m_end - m_first)));
}
qint64 AudioSessionTimeline::snapped(qint64 frame) const
{
	const qint64 clamped = std::clamp<qint64>(frame, 0, AudioSessionFrameLimit);
	if (m_musicalSnap)
		return m_time.snapFrame(clamped, m_grid);
	return std::min(AudioSessionFrameLimit, ((clamped + m_snap / 2) / m_snap) * m_snap);
}
qint64 AudioSessionTimeline::stepped(qint64 frame, int direction) const
{
	return m_musicalSnap ? m_time.stepFrame(frame, m_grid, direction)
	                     : std::clamp<qint64>(frame + direction * m_snap, 0, AudioSessionFrameLimit);
}
QRectF AudioSessionTimeline::regionRect(int track, const AudioSessionRegion &region) const
{
	return {xFor(region.position), double(rulerHeight() + track * laneHeight() + 5),
	        std::max(1.0, xFor(region.position + region.length) - xFor(region.position)), double(laneHeight() - 10)};
}
QString AudioSessionTimeline::hitRegion(const QPointF &point, QString *track) const
{
	if (point.x() < labelWidth() || point.y() < rulerHeight()) {
		return {};
	}
	const int row = int(point.y() - rulerHeight()) / laneHeight();
	if (row < 0 || row >= m_session.tracks.size()) {
		return {};
	}
	const auto &lane = m_session.tracks[row];
	if (track) {
		*track = lane.id;
	}
	for (auto it = lane.regions.crbegin(); it != lane.regions.crend(); ++it) {
		if (regionRect(row, *it).contains(point)) {
			return it->id;
		}
	}
	return {};
}
void AudioSessionTimeline::describe()
{
	setAccessibleDescription(
	    tr("Track count: %1. Visible frames %2 to %3. Cursor frame %4. Selected track %5; clip %6. Use "
	       "the track and clip lists for named item navigation.")
	        .arg(m_session.tracks.size())
	        .arg(m_first)
	        .arg(m_end)
	        .arg(m_cursor)
	        .arg(m_track, m_region));
	setAccessibleDescription(accessibleDescription() +
	                         tr(" Edit targets: %3. Musical cursor %1; %2 BPM.")
	                             .arg(audioMusicalPositionText(m_time.positionAtFrame(m_cursor)))
	                             .arg(m_time.tempoAtFrame(m_cursor))
	                             .arg(m_selection.size()));
	setAccessibleDescription(accessibleDescription() +
	                         tr(" Time range %1 to %2, end exclusive.").arg(m_rangeFirst).arg(m_rangeEnd));
	QAccessibleEvent event(this, QAccessible::DescriptionChanged);
	QAccessible::updateAccessibility(&event);
}
void AudioSessionTimeline::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	const auto colors = palette();
	painter.fillRect(rect(), colors.base());
	painter.setPen(colors.text().color());
	const int left = labelWidth();
	const auto clockText = [this](qint64 frame) {
		return locale().toString(double(frame) / m_session.sampleRate, 'f', 2) + tr(" s");
	};
	if (m_musicalRuler) {
		paintMusicalRuler(painter);
	} else {
		const int spacing = std::max(fontMetrics().horizontalAdvance(clockText(m_first)),
		                             fontMetrics().horizontalAdvance(clockText(m_end))) +
		                    16;
		const int ticks = std::clamp((width() - left - 8) / std::max(1, spacing), 1, 8);
		for (int tick = 0; tick <= ticks; ++tick) {
			const qint64 frame = m_first + (m_end - m_first) * tick / ticks;
			const int x = int(xFor(frame));
			painter.setPen(colors.mid().color());
			painter.drawLine(x, rulerHeight(), x, height());
			painter.setPen(colors.text().color());
			if (tick < ticks) {
				const int labelSpace = std::max(1, (width() - left - 8) / ticks - 8);
				painter.drawText(QRect(x + 3, 2, labelSpace, rulerHeight() - 4), Qt::AlignLeft | Qt::AlignVCenter,
				                 fontMetrics().elidedText(clockText(frame), Qt::ElideRight, labelSpace));
			}
		}
	}
	for (int row = 0; row < m_session.tracks.size(); ++row) {
		const auto &track = m_session.tracks[row];
		const int top = rulerHeight() + row * laneHeight();
		painter.fillRect(QRect(0, top, left, laneHeight()),
		                 track.id == m_track ? colors.alternateBase() : colors.window());
		painter.setPen(colors.text().color());
		painter.drawText(QRect(8, top + 4, left - 16, fontMetrics().height() * 2), Qt::AlignLeading | Qt::TextWordWrap,
		                 fontMetrics().elidedText(track.routing.bus ? tr("Bus: %1").arg(track.name) : track.name,
		                                          Qt::ElideRight, left - 16));
		painter.drawText(
		    QRect(8, top + laneHeight() - fontMetrics().height() - 8, left - 16, fontMetrics().height() + 2),
		    Qt::AlignLeading,
		    tr("%1 dB  %2%3")
		        .arg(track.gainDb, 0, 'f', 1)
		        .arg(track.muted ? tr("Muted ") : QString(), track.solo ? tr("Solo") : QString()));
		painter.setPen(colors.mid().color());
		painter.drawLine(0, top + laneHeight(), width(), top + laneHeight());
		painter.save();
		painter.setClipRect(QRect(left, top, width() - left, laneHeight()));
		for (const auto &region : track.regions) {
			const QRectF box = regionRect(row, region).intersected(QRectF(left, top, width() - left, laneHeight()));
			if (box.right() < left || box.left() > width()) {
				continue;
			}
			const bool selected = m_selection.contains(region.id);
			painter.fillRect(box, selected ? colors.highlight() : colors.alternateBase());
			painter.setPen(QPen(selected ? colors.highlightedText().color() : colors.text().color(), selected ? 2 : 1,
			                    region.muted || track.muted ? Qt::DashLine : Qt::SolidLine));
			painter.drawRect(box.adjusted(1, 1, -1, -1));
			const auto wave = m_waveforms.constFind(region.sourceId);
			if (wave != m_waveforms.cend() && wave->valid()) {
				const double middle = box.center().y() + fontMetrics().height() / 2;
				const double amplitude = std::max(1.0, (box.height() - fontMetrics().height() - 10) / 2);
				for (int x = int(std::max(double(left), std::floor(box.left())));
				     x < int(std::min(double(width()), std::ceil(box.right()))); x += 2) {
					const qint64 first =
					    region.sourceOffset + std::clamp<qint64>(frameAt(x) - region.position, 0, region.length - 1);
					const qint64 end =
					    std::min(region.sourceOffset + region.length,
					             std::max(first + 1, region.sourceOffset + frameAt(x + 2) - region.position));
					auto extrema = wave->range(0, first, end);
					if (wave->clip().channels == 2) {
						const auto right = wave->range(1, first, end);
						extrema.minimum = std::min(extrema.minimum, right.minimum);
						extrema.maximum = std::max(extrema.maximum, right.maximum);
					}
					painter.drawLine(QPointF(x, middle - std::clamp(double(extrema.maximum), -1.0, 1.0) * amplitude),
					                 QPointF(x, middle - std::clamp(double(extrema.minimum), -1.0, 1.0) * amplitude));
				}
			}
			painter.drawText(box.adjusted(5, 1, -5, -1), Qt::AlignTop | Qt::AlignLeading,
			                 fontMetrics().elidedText(region.name, Qt::ElideRight, std::max(1, int(box.width()) - 10)));
			if (m_dragging && selected) {
				auto preview = region;
				preview.position += m_dragPosition - m_dragOriginal;
				painter.setPen(QPen(colors.text().color(), 2, Qt::DashLine));
				painter.drawRect(regionRect(row, preview).intersected(QRectF(left, top, width() - left, laneHeight())));
			}
		}
		painter.restore();
	}
	if (m_rangeEnd > m_rangeFirst && m_rangeEnd > m_first && m_rangeFirst < m_end) {
		const double first = xFor(std::max(m_first, m_rangeFirst)), end = xFor(std::min(m_end, m_rangeEnd));
		auto shade = colors.highlight().color();
		shade.setAlpha(28);
		painter.fillRect(QRectF(first, rulerHeight() - 5, end - first, height() - rulerHeight() + 5), shade);
		painter.setPen(QPen(colors.text().color(), 2, Qt::DotLine));
		for (auto frame : {m_rangeFirst, m_rangeEnd})
			if (frame >= m_first && frame <= m_end)
				painter.drawLine(QPointF(xFor(frame), rulerHeight() - 5), QPointF(xFor(frame), height()));
		painter.setPen(QPen(colors.text().color(), 2));
		painter.drawLine(QPointF(first, rulerHeight() - 5), QPointF(end, rulerHeight() - 5));
	}
	if (m_cursor >= m_first && m_cursor <= m_end) {
		painter.setPen(QPen(colors.text().color(), 2, Qt::DashLine));
		painter.drawLine(QPointF(xFor(m_cursor), 0), QPointF(xFor(m_cursor), height()));
	}
	if (hasFocus()) {
		painter.setPen(QPen(colors.text().color(), 1, Qt::DotLine));
		painter.drawRect(rect().adjusted(1, 1, -2, -2));
	}
}
void AudioSessionTimeline::paintMusicalRuler(QPainter &painter)
{
	const auto colors = palette();
	const int left = labelWidth(), line = fontMetrics().height();
	const auto label = [this](qint64 frame) { return audioMusicalPositionText(m_time.positionAtFrame(frame)); };
	const int spacing =
	    std::max(fontMetrics().horizontalAdvance(label(m_first)), fontMetrics().horizontalAdvance(label(m_end))) + 20;
	const int count = std::clamp((width() - left - 8) / std::max(1, spacing), 1, 32);
	auto grid = m_musicalSnap ? m_grid : AudioMusicalGrid::Beat;
	const auto next = m_time.stepFrame(m_first, grid, 1);
	if (xFor(next) - xFor(m_first) < spacing)
		grid = AudioMusicalGrid::Bar;
	int lastX = left - spacing;
	qint64 lastFrame = -1;
	// Sample the visible span, then choose real map grid lines. Work stays
	// bounded even when the viewport covers billions of musical ticks.
	for (int i = 0; i <= count * 3; ++i) {
		const auto frame = m_time.snapFrame(m_first + (m_end - m_first) * i / (count * 3), grid);
		const int x = int(xFor(frame));
		if (frame < m_first || frame >= m_end || frame == lastFrame || x < lastX + spacing)
			continue;
		painter.setPen(colors.mid().color());
		painter.drawLine(x, rulerHeight(), x, height());
		painter.setPen(colors.text().color());
		painter.drawText(QRect(x + 3, 2, std::max(1, width() - x - 8), line + 2), Qt::AlignLeft | Qt::AlignVCenter,
		                 label(frame));
		lastX = x;
		lastFrame = frame;
	}
	if (lastFrame < 0) {
		painter.setPen(colors.text().color());
		painter.drawText(QRect(left + 3, 2, std::max(1, width() - left - 8), line + 2),
		                 Qt::AlignLeft | Qt::AlignVCenter, label(m_first));
	}
	int tempoRight = left, meterRight = left;
	const auto marker = [&](qint64 frame, const QString &text, int row, int &right) {
		if (frame < m_first || frame >= m_end)
			return;
		const int x = int(xFor(frame));
		painter.setPen(QPen(colors.text().color(), 1, Qt::DashLine));
		painter.drawLine(x, row * line + 3, x, rulerHeight());
		if (x < right)
			return;
		const int room = std::max(1, width() - x - 8);
		painter.drawText(QRect(x + 3, row * line + 3, room, line + 2), Qt::AlignLeft | Qt::AlignVCenter,
		                 fontMetrics().elidedText(text, Qt::ElideRight, room));
		right = x + fontMetrics().horizontalAdvance(text) + 16;
	};
	const auto currentMeter = m_time.meterAtFrame(m_first);
	marker(m_first, tr("%1 BPM").arg(m_time.tempoAtFrame(m_first)), 1, tempoRight);
	marker(m_first, QStringLiteral("%1/%2").arg(currentMeter.beatsPerBar).arg(currentMeter.beatUnit), 2, meterRight);
	for (const auto &change : m_session.musicalTime.tempoChanges)
		marker(m_time.frameAtTick(change.tick), tr("%1 BPM").arg(change.bpm), 1, tempoRight);
	for (const auto &change : m_session.musicalTime.meterChanges)
		marker(m_time.frameAtPosition({change.bar, 1, 0}),
		       QStringLiteral("%1/%2").arg(change.beatsPerBar).arg(change.beatUnit), 2, meterRight);
}
void AudioSessionTimeline::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton) {
		QWidget::mousePressEvent(event);
		return;
	}
	setFocus(Qt::MouseFocusReason);
	QString track;
	const auto region = hitRegion(event->position(), &track);
	setSelected(track, region);
	Q_EMIT selected(track, region);
	Q_EMIT selectionRequested(track, region, event->modifiers());
	setCursor(snapped(frameAt(event->position().x())));
	Q_EMIT cursorChanged(m_cursor);
	for (const auto &lane : m_session.tracks) {
		for (const auto &clip : lane.regions) {
			if (!region.isEmpty() && clip.id == region && m_selection.contains(region)) {
				m_dragging = true;
				m_dragOriginal = m_dragPosition = clip.position;
				m_dragAnchor = frameAt(event->position().x());
			}
		}
	}
}
void AudioSessionTimeline::mouseMoveEvent(QMouseEvent *event)
{
	if (!m_dragging) {
		QWidget::mouseMoveEvent(event);
		return;
	}
	m_dragPosition = snapped(m_dragOriginal + frameAt(event->position().x()) - m_dragAnchor);
	update();
}
void AudioSessionTimeline::mouseReleaseEvent(QMouseEvent *event)
{
	if (m_dragging && event->button() == Qt::LeftButton) {
		m_dragging = false;
		if (m_dragPosition != m_dragOriginal) {
			Q_EMIT moveRequested(m_track, m_region, m_dragPosition);
		}
		update();
	} else {
		QWidget::mouseReleaseEvent(event);
	}
}
void AudioSessionTimeline::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Home) {
		setCursor(0);
		Q_EMIT cursorChanged(0);
		return;
	}
	if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
		const int direction = event->key() == Qt::Key_Left ? -1 : 1;
		for (const auto &track : m_session.tracks) {
			for (const auto &region : track.regions) {
				if (m_selection.contains(region.id) && (region.id == m_region || !m_selection.contains(m_region))) {
					Q_EMIT moveRequested(track.id, region.id, stepped(region.position, direction));
					return;
				}
			}
		}
		setCursor(stepped(m_cursor, direction));
		Q_EMIT cursorChanged(m_cursor);
		return;
	}
	QWidget::keyPressEvent(event);
}
} // namespace vibestudio
