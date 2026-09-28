#include "app/studio_charts.h"

#include <QBrush>
#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QRectF>
#include <QSizePolicy>

#include <algorithm>
#include <cmath>

namespace vibestudio {

namespace {

QString chartText(const char* source)
{
	return QCoreApplication::translate("VibeStudioCharts", source);
}

// ---------------------------------------------------------------------------
// Shared metrics
// ---------------------------------------------------------------------------

constexpr int kMargin = 8;
constexpr int kBarHeight = 22;
constexpr int kBarGap = 8;
constexpr int kTitleGap = 4;
constexpr int kSwatchWidth = 16;
constexpr int kSwatchHeight = 12;
constexpr int kSwatchGap = 6;
constexpr int kLegendGap = 14;
constexpr int kLegendRowPad = 4;

// A slice narrower than this is still drawn at this width so that tiny
// categories never vanish; the surplus is taken back from the wider slices,
// which means the drawn widths are an approximation of the true shares once a
// tiny slice is present. Legend percentages always report the true share.
constexpr double kMinSliceWidth = 3.0;

constexpr int kBoxPadding = 8;
constexpr int kMinBoxWidth = 92;
constexpr int kMaxBoxWidth = 240;
constexpr int kArrowWidth = 22;
constexpr int kRowGap = 12;

constexpr int kGlyphColumn = 18;
constexpr int kTimelineRowGap = 4;

// Glyphs are written as numeric code points so this file stays pure ASCII and
// does not depend on the compiler's input charset.
QString codePointGlyph(char16_t code)
{
	return QString(QChar(code));
}

QString stateName(OperationState state)
{
	switch (state) {
	case OperationState::Idle:
		return chartText("Idle");
	case OperationState::Queued:
		return chartText("Queued");
	case OperationState::Loading:
		return chartText("Loading");
	case OperationState::Running:
		return chartText("Running");
	case OperationState::Warning:
		return chartText("Warning");
	case OperationState::Failed:
		return chartText("Failed");
	case OperationState::Cancelled:
		return chartText("Cancelled");
	case OperationState::Completed:
		return chartText("Completed");
	}
	return chartText("Idle");
}

bool paletteIsLight(const QPalette& palette)
{
	return palette.color(QPalette::Window).lightness() > 128;
}

QColor chartForeground(const QPalette& palette, bool highContrast, bool lightTheme)
{
	if (highContrast) {
		return lightTheme ? QColor(0, 0, 0) : QColor(255, 255, 255);
	}
	return palette.color(QPalette::WindowText);
}

QColor chartMuted(const QPalette& palette, bool highContrast, bool lightTheme)
{
	if (highContrast) {
		return lightTheme ? QColor(0, 0, 0) : QColor(255, 255, 255);
	}
	QColor muted = palette.color(QPalette::WindowText);
	muted.setAlpha(lightTheme ? 165 : 180);
	return muted;
}

QColor chartSurface(const QPalette& palette, bool highContrast, bool lightTheme)
{
	if (highContrast) {
		return lightTheme ? QColor(255, 255, 255) : QColor(0, 0, 0);
	}
	QColor surface = palette.color(QPalette::Base);
	if (lightTheme) {
		surface = surface.darker(103);
	} else {
		surface = surface.lighter(112);
	}
	return surface;
}

QColor chartOutline(const QPalette& palette, bool highContrast, bool lightTheme)
{
	if (highContrast) {
		return lightTheme ? QColor(0, 0, 0) : QColor(255, 255, 255);
	}
	QColor outline = palette.color(QPalette::WindowText);
	outline.setAlpha(lightTheme ? 90 : 110);
	return outline;
}

Qt::BrushStyle patternForIndex(int patternIndex)
{
	// Non-colour cue: every slice gets its own hatch so the chart still reads
	// in greyscale, in high-contrast themes, and for colour-blind users.
	static const Qt::BrushStyle kPatterns[] = {
		Qt::BDiagPattern,
		Qt::FDiagPattern,
		Qt::CrossPattern,
		Qt::HorPattern,
		Qt::VerPattern,
		Qt::DiagCrossPattern,
		Qt::Dense4Pattern,
		Qt::Dense6Pattern,
	};
	const int count = static_cast<int>(sizeof(kPatterns) / sizeof(kPatterns[0]));
	int index = patternIndex % count;
	if (index < 0) {
		index += count;
	}
	return kPatterns[index];
}

QColor patternInk(const QColor& base)
{
	QColor ink = base.lightness() > 140 ? QColor(0, 0, 0) : QColor(255, 255, 255);
	ink.setAlpha(150);
	return ink;
}

QColor readableTextOn(const QColor& base)
{
	return base.lightness() > 140 ? QColor(16, 16, 16) : QColor(245, 245, 245);
}

// Durations and relative times share one formatter so the timeline never mixes
// two spellings of the same quantity. `coarse` drops the trailing unit, which
// is what relative stamps ("2 min ago") want.
QString formatDurationText(qint64 milliseconds, bool coarse)
{
	const qint64 ms = milliseconds < 0 ? 0 : milliseconds;
	if (ms < 1000) {
		if (coarse) {
			return chartText("under 1 s");
		}
		return chartText("%1 ms").arg(ms);
	}
	if (ms < 60000) {
		if (coarse) {
			return chartText("%1 s").arg(ms / 1000);
		}
		const double seconds = static_cast<double>(ms) / 1000.0;
		return chartText("%1 s").arg(QString::number(seconds, 'f', 1));
	}
	if (ms < 3600000) {
		const qint64 minutes = ms / 60000;
		const qint64 seconds = (ms % 60000) / 1000;
		if (coarse || seconds == 0) {
			return chartText("%1 min").arg(minutes);
		}
		return chartText("%1 min %2 s").arg(minutes).arg(seconds);
	}
	const qint64 hours = ms / 3600000;
	const qint64 minutes = (ms % 3600000) / 60000;
	if (coarse || minutes == 0) {
		return chartText("%1 h").arg(hours);
	}
	return chartText("%1 h %2 min").arg(hours).arg(minutes);
}

QString formatRelativeText(qint64 deltaMs)
{
	if (deltaMs <= 1500) {
		return chartText("just now");
	}
	return chartText("%1 ago").arg(formatDurationText(deltaMs, true));
}

QString formatShareText(double share)
{
	if (share > 0.0 && share < 1.0) {
		return chartText("<1%");
	}
	return chartText("%1%").arg(QString::number(share, 'f', share < 10.0 ? 1 : 0));
}

void drawPatternedRect(QPainter& painter, const QRectF& rect, const QColor& color, int patternIndex, qreal radius)
{
	if (rect.width() <= 0.0 || rect.height() <= 0.0) {
		return;
	}
	painter.save();
	painter.setPen(Qt::NoPen);
	painter.setBrush(color);
	if (radius > 0.0) {
		painter.drawRoundedRect(rect, radius, radius);
	} else {
		painter.drawRect(rect);
	}
	painter.setBrush(QBrush(patternInk(color), patternForIndex(patternIndex)));
	if (radius > 0.0) {
		painter.drawRoundedRect(rect, radius, radius);
	} else {
		painter.drawRect(rect);
	}
	painter.restore();
}

void drawFocusRing(QPainter& painter, const QRectF& rect, const QColor& color, qreal radius)
{
	painter.save();
	painter.setBrush(Qt::NoBrush);
	QPen pen(color, 1.0, Qt::DotLine);
	painter.setPen(pen);
	const QRectF ring = rect.adjusted(1.5, 1.5, -1.5, -1.5);
	if (ring.width() <= 0.0 || ring.height() <= 0.0) {
		painter.restore();
		return;
	}
	if (radius > 0.0) {
		painter.drawRoundedRect(ring, radius, radius);
	} else {
		painter.drawRect(ring);
	}
	painter.restore();
}

void drawArrow(QPainter& painter, const QPointF& from, const QPointF& to, const QColor& color)
{
	painter.save();
	painter.setPen(QPen(color, 1.4));
	painter.drawLine(from, to);
	QPolygonF head;
	head << to << QPointF(to.x() - 5.0, to.y() - 3.5) << QPointF(to.x() - 5.0, to.y() + 3.5);
	painter.setPen(Qt::NoPen);
	painter.setBrush(color);
	painter.drawPolygon(head);
	painter.restore();
}

void drawDashedStub(QPainter& painter, const QPointF& from, const QPointF& to, const QColor& color)
{
	painter.save();
	QPen pen(color, 1.2, Qt::DashLine);
	painter.setPen(pen);
	painter.drawLine(from, to);
	painter.restore();
}

// ---------------------------------------------------------------------------
// Composition layout
// ---------------------------------------------------------------------------

struct CompositionLayout {
	QRectF titleRect;
	QRectF barRect;
	QVector<QRectF> sliceRects;
	QVector<QRectF> legendRects;
	QVector<double> shares;
	QVector<QString> legendTexts;
	int requiredHeight = 0;
};

QVector<double> sliceShares(const QVector<StudioChartSlice>& slices)
{
	QVector<double> shares;
	shares.reserve(slices.size());
	double total = 0.0;
	for (const StudioChartSlice& slice : slices) {
		total += slice.value > 0.0 ? slice.value : 0.0;
	}
	if (slices.isEmpty()) {
		return shares;
	}
	if (total <= 0.0) {
		// All-zero sets still deserve a readable bar: split it evenly.
		const double even = 100.0 / static_cast<double>(slices.size());
		for (int index = 0; index < slices.size(); ++index) {
			shares.push_back(even);
		}
		return shares;
	}
	for (const StudioChartSlice& slice : slices) {
		const double value = slice.value > 0.0 ? slice.value : 0.0;
		shares.push_back(value / total * 100.0);
	}
	return shares;
}

// Turns shares into pixel widths that always sum to exactly the bar width and
// never drop below kMinSliceWidth.
QVector<QRectF> distributeSlices(const QVector<double>& shares, const QRectF& barRect)
{
	QVector<QRectF> rects;
	const int count = static_cast<int>(shares.size());
	if (count <= 0 || barRect.width() <= 0.0) {
		return rects;
	}
	rects.reserve(count);

	QVector<double> widths;
	widths.reserve(count);
	const double available = barRect.width();
	if (available < kMinSliceWidth * count) {
		const double even = available / static_cast<double>(count);
		for (int index = 0; index < count; ++index) {
			widths.push_back(even);
		}
	} else {
		for (int index = 0; index < count; ++index) {
			widths.push_back(available * shares.at(index) / 100.0);
		}
		// Raise the tiny slices to the floor, then shrink the slices that are
		// still above the floor to pay for it. A handful of passes converges
		// because every pass either lifts a slice or leaves the set alone.
		for (int pass = 0; pass < count + 1; ++pass) {
			double deficit = 0.0;
			double payable = 0.0;
			for (int index = 0; index < count; ++index) {
				if (widths.at(index) < kMinSliceWidth) {
					deficit += kMinSliceWidth - widths.at(index);
				} else {
					payable += widths.at(index) - kMinSliceWidth;
				}
			}
			if (deficit <= 0.0) {
				break;
			}
			if (payable <= 0.0) {
				for (int index = 0; index < count; ++index) {
					widths[index] = available / static_cast<double>(count);
				}
				break;
			}
			const double factor = std::max(0.0, (payable - deficit) / payable);
			for (int index = 0; index < count; ++index) {
				if (widths.at(index) < kMinSliceWidth) {
					widths[index] = kMinSliceWidth;
				} else {
					widths[index] = kMinSliceWidth + (widths.at(index) - kMinSliceWidth) * factor;
				}
			}
		}
	}

	// Accumulate in doubles and round only the edges, so rounding error never
	// grows across the bar and the last slice lands exactly on the right edge.
	double cursor = 0.0;
	for (int index = 0; index < count; ++index) {
		const double start = cursor;
		cursor += widths.at(index);
		const double left = barRect.left() + std::round(start);
		double right = barRect.left() + std::round(cursor);
		if (index == count - 1) {
			right = barRect.right();
		}
		if (right < left + 1.0) {
			right = left + 1.0;
		}
		rects.push_back(QRectF(left, barRect.top(), right - left, barRect.height()));
	}
	return rects;
}

CompositionLayout computeCompositionLayout(const QVector<StudioChartSlice>& slices,
	const QRect& widgetRect,
	const QFontMetrics& titleMetrics,
	const QFontMetrics& bodyMetrics,
	bool hasTitle)
{
	CompositionLayout layout;
	const int left = widgetRect.left() + kMargin;
	const int right = widgetRect.right() - kMargin;
	const int width = std::max(1, right - left);
	int y = widgetRect.top() + kMargin;

	if (hasTitle) {
		layout.titleRect = QRectF(left, y, width, titleMetrics.height());
		y += titleMetrics.height() + kTitleGap;
	}

	layout.barRect = QRectF(left, y, width, kBarHeight);
	y += kBarHeight + kBarGap;

	layout.shares = sliceShares(slices);
	layout.sliceRects = distributeSlices(layout.shares, layout.barRect);

	const int rowHeight = std::max(bodyMetrics.height(), kSwatchHeight) + kLegendRowPad;
	int x = left;
	layout.legendTexts.reserve(slices.size());
	layout.legendRects.reserve(slices.size());
	for (int index = 0; index < slices.size(); ++index) {
		const StudioChartSlice& slice = slices.at(index);
		QString text = slice.label;
		if (!slice.valueText.isEmpty()) {
			text += QStringLiteral(" ") + slice.valueText;
		}
		const double share = index < layout.shares.size() ? layout.shares.at(index) : 0.0;
		text += QStringLiteral(" ") + formatShareText(share);
		layout.legendTexts.push_back(text);

		const int textWidth = bodyMetrics.horizontalAdvance(text);
		const int entryWidth = std::min(width, kSwatchWidth + kSwatchGap + textWidth);
		if (x > left && x + entryWidth > right) {
			x = left;
			y += rowHeight;
		}
		layout.legendRects.push_back(QRectF(x, y, entryWidth, rowHeight));
		x += entryWidth + kLegendGap;
	}
	if (!slices.isEmpty()) {
		y += rowHeight;
	}

	layout.requiredHeight = y + kMargin - widgetRect.top();
	return layout;
}

// ---------------------------------------------------------------------------
// Pipeline layout
// ---------------------------------------------------------------------------

struct PipelineBox {
	QRectF rect;
	int stageIndex = -1; // -1 marks the synthetic source/artifact endpoints.
	int row = 0;
	QString label;
	QString glyph;
	QString badge;
	bool optional = false;
	bool endpoint = false;
	OperationState state = OperationState::Idle;
};

struct PipelineLayout {
	QRectF titleRect;
	QVector<PipelineBox> boxes;
	int requiredHeight = 0;
};

QString pipelineBadgeLine(const PipelineBox& box)
{
	QString badge = box.badge;
	if (box.optional) {
		if (badge.isEmpty()) {
			badge = chartText("optional");
		} else {
			badge += QStringLiteral(" ") + chartText("optional");
		}
	}
	return badge;
}

PipelineLayout computePipelineLayout(const QVector<PipelineStageNode>& stages,
	const QRect& widgetRect,
	const QFontMetrics& titleMetrics,
	const QFontMetrics& bodyMetrics,
	bool hasTitle,
	const QString& sourceLabel,
	const QString& artifactLabel)
{
	PipelineLayout layout;
	const int left = widgetRect.left() + kMargin;
	const int right = widgetRect.right() - kMargin;
	const int width = std::max(1, right - left);
	int y = widgetRect.top() + kMargin;

	if (hasTitle) {
		layout.titleRect = QRectF(left, y, width, titleMetrics.height());
		y += titleMetrics.height() + kTitleGap;
	}

	QVector<PipelineBox> boxes;
	boxes.reserve(stages.size() + 2);

	PipelineBox source;
	source.label = sourceLabel;
	source.glyph = codePointGlyph(0x25a6); // square with orthogonal fill
	source.endpoint = true;
	boxes.push_back(source);

	for (int index = 0; index < stages.size(); ++index) {
		const PipelineStageNode& stage = stages.at(index);
		PipelineBox box;
		box.rect = QRectF();
		box.stageIndex = index;
		box.label = stage.label;
		box.glyph = studioStateGlyph(stage.state);
		box.badge = stage.badgeText;
		box.optional = stage.optional;
		box.state = stage.state;
		boxes.push_back(box);
	}

	PipelineBox artifact;
	artifact.label = artifactLabel;
	artifact.glyph = codePointGlyph(0x25a3); // white square containing black square
	artifact.endpoint = true;
	boxes.push_back(artifact);

	const int boxHeight = bodyMetrics.height() * 2 + 16;
	int x = left;
	int row = 0;
	int rowTop = y;
	bool firstInRow = true;
	for (PipelineBox& box : boxes) {
		const QString labelLine = box.glyph + QStringLiteral(" ") + box.label;
		const QString badgeLine = pipelineBadgeLine(box);
		int boxWidth = std::max(bodyMetrics.horizontalAdvance(labelLine), bodyMetrics.horizontalAdvance(badgeLine)) + 2 * kBoxPadding;
		boxWidth = std::max(boxWidth, kMinBoxWidth);
		boxWidth = std::min(boxWidth, kMaxBoxWidth);
		boxWidth = std::min(boxWidth, width);

		if (!firstInRow && x + kArrowWidth + boxWidth > right) {
			++row;
			rowTop += boxHeight + kRowGap;
			x = left;
			firstInRow = true;
		}
		if (!firstInRow) {
			x += kArrowWidth;
		}
		box.rect = QRectF(x, rowTop, boxWidth, boxHeight);
		box.row = row;
		x += boxWidth;
		firstInRow = false;
	}

	// A chain that fits on one row with room to spare spreads out instead of
	// huddling at the left edge: boxes widen a little first, then the
	// connectors lengthen. Both are capped so a wide panel does not produce
	// sparse boxes joined by long lines.
	if (row == 0 && boxes.size() > 1) {
		constexpr qreal kComfortableBoxWidth = 168.0;
		constexpr qreal kMaxExtraGap = 72.0;
		qreal used = static_cast<qreal>(boxes.size() - 1) * kArrowWidth;
		for (const PipelineBox& box : boxes) {
			used += box.rect.width();
		}
		qreal spare = width - used;
		if (spare > 0.0) {
			const qreal grow = spare / static_cast<qreal>(boxes.size());
			for (PipelineBox& box : boxes) {
				const qreal widened = std::max(box.rect.width(), std::min(box.rect.width() + grow, kComfortableBoxWidth));
				spare -= widened - box.rect.width();
				box.rect.setWidth(widened);
			}
			const qreal gap = kArrowWidth + std::clamp(spare / static_cast<qreal>(boxes.size() - 1), 0.0, kMaxExtraGap);
			qreal cursor = left;
			for (PipelineBox& box : boxes) {
				box.rect.moveLeft(cursor);
				cursor += box.rect.width() + gap;
			}
		}
	}

	layout.boxes = boxes;
	layout.requiredHeight = rowTop + boxHeight + kMargin - widgetRect.top();
	return layout;
}

// ---------------------------------------------------------------------------
// Timeline layout
// ---------------------------------------------------------------------------

int timelineRowHeight(const QFontMetrics& metrics)
{
	return metrics.height() * 2 + 10;
}

QVector<QRectF> computeTimelineRows(int count, const QRect& widgetRect, const QFontMetrics& metrics)
{
	QVector<QRectF> rows;
	if (count <= 0) {
		return rows;
	}
	rows.reserve(count);
	const int left = widgetRect.left() + kMargin;
	const int width = std::max(1, widgetRect.width() - 2 * kMargin);
	const int rowHeight = timelineRowHeight(metrics);
	int y = widgetRect.top() + kMargin;
	for (int index = 0; index < count; ++index) {
		rows.push_back(QRectF(left, y, width, rowHeight));
		y += rowHeight + kTimelineRowGap;
	}
	return rows;
}

} // namespace

// ---------------------------------------------------------------------------
// Shared tokens
// ---------------------------------------------------------------------------

QColor studioStateColor(OperationState state, bool highContrast, bool lightTheme)
{
	// Four tuned ramps: the two high-contrast ramps assume a pure black or pure
	// white background and stay saturated; the two standard ramps sit on the
	// studio shell surfaces. Every ramp keeps the eight states distinguishable
	// by hue *and* lightness, and each colour is always paired with a glyph or
	// hatch so colour is never the only cue.
	if (highContrast) {
		if (lightTheme) {
			switch (state) {
			case OperationState::Idle:
				return QColor(0x00, 0x00, 0x00);
			case OperationState::Queued:
				return QColor(0x5b, 0x00, 0xc8);
			case OperationState::Loading:
				return QColor(0x00, 0x6d, 0x7a);
			case OperationState::Running:
				return QColor(0x00, 0x33, 0xcc);
			case OperationState::Warning:
				return QColor(0x8a, 0x4d, 0x00);
			case OperationState::Failed:
				return QColor(0xc0, 0x00, 0x00);
			case OperationState::Cancelled:
				return QColor(0x9c, 0x00, 0x8f);
			case OperationState::Completed:
				return QColor(0x00, 0x62, 0x22);
			}
			return QColor(0x00, 0x00, 0x00);
		}
		switch (state) {
		case OperationState::Idle:
			return QColor(0xff, 0xff, 0xff);
		case OperationState::Queued:
			return QColor(0xa8, 0x8c, 0xff);
		case OperationState::Loading:
			return QColor(0x00, 0xe5, 0xff);
		case OperationState::Running:
			return QColor(0x4d, 0xa3, 0xff);
		case OperationState::Warning:
			return QColor(0xff, 0xd4, 0x00);
		case OperationState::Failed:
			return QColor(0xff, 0x52, 0x52);
		case OperationState::Cancelled:
			return QColor(0xff, 0x7b, 0xd5);
		case OperationState::Completed:
			return QColor(0x00, 0xe6, 0x76);
		}
		return QColor(0xff, 0xff, 0xff);
	}

	if (lightTheme) {
		switch (state) {
		case OperationState::Idle:
			return QColor(0x5c, 0x64, 0x70);
		case OperationState::Queued:
			return QColor(0x43, 0x38, 0xa8);
		case OperationState::Loading:
			return QColor(0x0d, 0x6d, 0x73);
		case OperationState::Running:
			return QColor(0x12, 0x59, 0xa8);
		case OperationState::Warning:
			return QColor(0x8a, 0x5a, 0x00);
		case OperationState::Failed:
			return QColor(0xb3, 0x26, 0x1e);
		case OperationState::Cancelled:
			return QColor(0x6b, 0x3f, 0xa0);
		case OperationState::Completed:
			return QColor(0x1d, 0x6f, 0x3f);
		}
		return QColor(0x5c, 0x64, 0x70);
	}

	switch (state) {
	case OperationState::Idle:
		return QColor(0x9a, 0xa4, 0xb2);
	case OperationState::Queued:
		return QColor(0x8c, 0x9a, 0xf0);
	case OperationState::Loading:
		return QColor(0x46, 0xc4, 0xd8);
	case OperationState::Running:
		return QColor(0x4f, 0x8e, 0xf7);
	case OperationState::Warning:
		return QColor(0xe0, 0xa3, 0x3a);
	case OperationState::Failed:
		return QColor(0xf2, 0x64, 0x5a);
	case OperationState::Cancelled:
		return QColor(0xb0, 0x8c, 0xd8);
	case OperationState::Completed:
		return QColor(0x4f, 0xbf, 0x7b);
	}
	return QColor(0x9a, 0xa4, 0xb2);
}

QString studioStateGlyph(OperationState state)
{
	// Deliberately conservative code points: Latin-1 punctuation plus a few
	// geometric shapes and the check mark, all of which are present in the
	// terminal-adjacent fonts the studio ships with.
	switch (state) {
	case OperationState::Idle:
		return codePointGlyph(0x00b7); // middle dot
	case OperationState::Queued:
		return codePointGlyph(0x00bb); // right-pointing guillemet
	case OperationState::Loading:
		return codePointGlyph(0x25cb); // white circle (spinner-ish)
	case OperationState::Running:
		return codePointGlyph(0x25b6); // black right-pointing triangle
	case OperationState::Warning:
		return codePointGlyph(0x25b2); // black up-pointing triangle (warning)
	case OperationState::Failed:
		return codePointGlyph(0x00d7); // multiplication sign (cross)
	case OperationState::Cancelled:
		return QStringLiteral("||"); // pause bars
	case OperationState::Completed:
		return codePointGlyph(0x2713); // check mark
	}
	return codePointGlyph(0x00b7);
}

// ---------------------------------------------------------------------------
// CompositionChart
// ---------------------------------------------------------------------------

CompositionChart::CompositionChart(QWidget* parent)
	: QWidget(parent)
{
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
	m_emptyText = tr("No composition data yet.");
	setAccessibleName(tr("Composition chart"));
	setAccessibleDescription(accessibleSummary());
}

void CompositionChart::setTitle(const QString& title)
{
	if (m_title == title) {
		return;
	}
	m_title = title;
	setAccessibleName(title.isEmpty() ? tr("Composition chart") : title);
	setAccessibleDescription(accessibleSummary());
	updateGeometry();
	update();
}

void CompositionChart::setSlices(const QVector<StudioChartSlice>& slices)
{
	m_slices = slices;
	m_sliceRects.clear();
	m_hoverIndex = -1;
	setAccessibleDescription(accessibleSummary());
	updateGeometry();
	update();
}

void CompositionChart::setEmptyText(const QString& text)
{
	if (m_emptyText == text) {
		return;
	}
	m_emptyText = text;
	if (m_slices.isEmpty()) {
		setAccessibleDescription(accessibleSummary());
		update();
	}
}

void CompositionChart::setHighContrast(bool enabled)
{
	if (m_highContrast == enabled) {
		return;
	}
	m_highContrast = enabled;
	update();
}

void CompositionChart::clear()
{
	setSlices(QVector<StudioChartSlice>());
}

QVector<StudioChartSlice> CompositionChart::slices() const
{
	return m_slices;
}

QString CompositionChart::accessibleSummary() const
{
	if (m_slices.isEmpty()) {
		return m_title.isEmpty() ? m_emptyText : tr("%1. %2").arg(m_title, m_emptyText);
	}
	const QString body = tr("%1 categories. %2").arg(m_slices.size()).arg(summaryLines().join(QStringLiteral("; ")));
	return m_title.isEmpty() ? body : tr("%1. %2").arg(m_title, body);
}

QStringList CompositionChart::summaryLines() const
{
	QStringList lines;
	if (m_slices.isEmpty()) {
		lines << m_emptyText;
		return lines;
	}
	const QVector<double> shares = sliceShares(m_slices);
	lines.reserve(m_slices.size());
	for (int index = 0; index < m_slices.size(); ++index) {
		const StudioChartSlice& slice = m_slices.at(index);
		const double share = index < shares.size() ? shares.at(index) : 0.0;
		QString line = tr("%1: %2 (%3)").arg(slice.label,
			slice.valueText.isEmpty() ? QString::number(slice.value, 'f', 0) : slice.valueText,
			formatShareText(share));
		if (!slice.detail.isEmpty()) {
			line += tr(" - %1").arg(slice.detail);
		}
		lines << line;
	}
	return lines;
}

QSize CompositionChart::sizeHint() const
{
	// Wrapping content means the height depends on the width; without a
	// heightForWidth hook we measure at the width we currently have and fall
	// back to a sensible default before the first layout pass.
	const int preferredWidth = width() > 0 ? width() : 320;
	const QFont bodyFont = font();
	QFont titleFont = bodyFont;
	titleFont.setBold(true);
	const CompositionLayout layout = computeCompositionLayout(m_slices,
		QRect(0, 0, preferredWidth, 1000),
		QFontMetrics(titleFont),
		QFontMetrics(bodyFont),
		!m_title.isEmpty());
	return QSize(std::max(preferredWidth, 320), std::max(layout.requiredHeight, minimumSizeHint().height()));
}

QSize CompositionChart::minimumSizeHint() const
{
	const QFontMetrics metrics(font());
	int height = 2 * kMargin + kBarHeight + kBarGap + metrics.height() + kLegendRowPad;
	if (!m_title.isEmpty()) {
		height += metrics.height() + kTitleGap;
	}
	return QSize(180, height);
}

void CompositionChart::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event);

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QPalette pal = palette();
	const bool lightTheme = paletteIsLight(pal);
	const QColor foreground = chartForeground(pal, m_highContrast, lightTheme);
	const QColor muted = chartMuted(pal, m_highContrast, lightTheme);
	const QColor outline = chartOutline(pal, m_highContrast, lightTheme);
	const QColor surface = chartSurface(pal, m_highContrast, lightTheme);

	const QFont bodyFont = font();
	QFont titleFont = bodyFont;
	titleFont.setBold(true);
	const QFontMetrics bodyMetrics(bodyFont);
	const QFontMetrics titleMetrics(titleFont);

	const CompositionLayout layout = computeCompositionLayout(m_slices, rect(), titleMetrics, bodyMetrics, !m_title.isEmpty());
	m_sliceRects = layout.sliceRects;

	if (!m_title.isEmpty()) {
		painter.setFont(titleFont);
		painter.setPen(foreground);
		painter.drawText(layout.titleRect, Qt::AlignLeft | Qt::AlignVCenter,
			titleMetrics.elidedText(m_title, Qt::ElideRight, static_cast<int>(layout.titleRect.width())));
	}

	painter.setFont(bodyFont);

	if (m_slices.isEmpty()) {
		painter.setPen(QPen(outline, 1.0, Qt::DashLine));
		painter.setBrush(surface);
		painter.drawRoundedRect(layout.barRect, 4.0, 4.0);
		painter.setPen(muted);
		painter.drawText(layout.barRect, Qt::AlignCenter,
			bodyMetrics.elidedText(m_emptyText, Qt::ElideRight, static_cast<int>(layout.barRect.width()) - 8));
		return;
	}

	for (int index = 0; index < m_slices.size() && index < layout.sliceRects.size(); ++index) {
		const StudioChartSlice& slice = m_slices.at(index);
		const QRectF sliceRect = layout.sliceRects.at(index);
		QColor color = studioStateColor(slice.state, m_highContrast, lightTheme);
		// Spread the slice colours away from the state ramp so neighbouring
		// slices with the same state stay separable.
		if (index % 3 == 1) {
			color = color.lighter(lightTheme ? 118 : 124);
		} else if (index % 3 == 2) {
			color = color.darker(lightTheme ? 112 : 118);
		}
		drawPatternedRect(painter, sliceRect, color, slice.patternIndex, 0.0);

		if (sliceRect.width() >= 36.0) {
			painter.setPen(readableTextOn(color));
			const double share = index < layout.shares.size() ? layout.shares.at(index) : 0.0;
			painter.drawText(sliceRect, Qt::AlignCenter,
				bodyMetrics.elidedText(formatShareText(share), Qt::ElideRight, static_cast<int>(sliceRect.width()) - 4));
		}

		if (index == m_hoverIndex) {
			painter.setPen(QPen(foreground, 1.6));
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(sliceRect.adjusted(0.8, 0.8, -0.8, -0.8));
		}
	}

	painter.setPen(QPen(outline, 1.0));
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(layout.barRect.adjusted(0.5, 0.5, -0.5, -0.5));

	for (int index = 0; index < m_slices.size() && index < layout.legendRects.size(); ++index) {
		const StudioChartSlice& slice = m_slices.at(index);
		const QRectF entry = layout.legendRects.at(index);
		QColor color = studioStateColor(slice.state, m_highContrast, lightTheme);
		if (index % 3 == 1) {
			color = color.lighter(lightTheme ? 118 : 124);
		} else if (index % 3 == 2) {
			color = color.darker(lightTheme ? 112 : 118);
		}
		const QRectF swatch(entry.left(), entry.top() + (entry.height() - kSwatchHeight) / 2.0, kSwatchWidth, kSwatchHeight);
		drawPatternedRect(painter, swatch, color, slice.patternIndex, 2.0);
		painter.setPen(QPen(outline, 1.0));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(swatch.adjusted(0.5, 0.5, -0.5, -0.5), 2.0, 2.0);

		const QRectF textRect(entry.left() + kSwatchWidth + kSwatchGap, entry.top(),
			std::max(0.0, entry.width() - kSwatchWidth - kSwatchGap), entry.height());
		painter.setPen(index == m_hoverIndex ? foreground : muted);
		const QString text = index < layout.legendTexts.size() ? layout.legendTexts.at(index) : slice.label;
		painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
			bodyMetrics.elidedText(text, Qt::ElideRight, static_cast<int>(textRect.width())));
	}

	if (hasFocus() && m_hoverIndex >= 0 && m_hoverIndex < layout.sliceRects.size()) {
		drawFocusRing(painter, layout.sliceRects.at(m_hoverIndex), foreground, 0.0);
	}
}

void CompositionChart::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton) {
		QWidget::mousePressEvent(event);
		return;
	}
	const int index = sliceAt(event->position().toPoint());
	if (index < 0) {
		QWidget::mousePressEvent(event);
		return;
	}
	if (m_hoverIndex != index) {
		m_hoverIndex = index;
		update();
	}
	setFocus(Qt::MouseFocusReason);
	Q_EMIT sliceActivated(m_slices.at(index).id);
	event->accept();
}

void CompositionChart::mouseMoveEvent(QMouseEvent* event)
{
	const int index = sliceAt(event->position().toPoint());
	if (index == m_hoverIndex) {
		QWidget::mouseMoveEvent(event);
		return;
	}
	m_hoverIndex = index;
	update();
	const QStringList lines = summaryLines();
	Q_EMIT hoverChanged(index >= 0 && index < lines.size() ? lines.at(index) : QString());
	QWidget::mouseMoveEvent(event);
}

void CompositionChart::keyPressEvent(QKeyEvent* event)
{
	if (m_slices.isEmpty()) {
		QWidget::keyPressEvent(event);
		return;
	}

	const int last = static_cast<int>(m_slices.size()) - 1;
	int next = m_hoverIndex;
	switch (event->key()) {
	case Qt::Key_Right:
	case Qt::Key_Down:
		next = m_hoverIndex < 0 ? 0 : std::min(last, m_hoverIndex + 1);
		break;
	case Qt::Key_Left:
	case Qt::Key_Up:
		next = m_hoverIndex < 0 ? last : std::max(0, m_hoverIndex - 1);
		break;
	case Qt::Key_Home:
		next = 0;
		break;
	case Qt::Key_End:
		next = last;
		break;
	case Qt::Key_Tab:
		// Reached only when the focus chain leaves the key to us; otherwise the
		// arrow keys are the in-widget traversal.
		if (m_hoverIndex >= last) {
			QWidget::keyPressEvent(event);
			return;
		}
		next = m_hoverIndex < 0 ? 0 : m_hoverIndex + 1;
		break;
	case Qt::Key_Backtab:
		if (m_hoverIndex <= 0) {
			QWidget::keyPressEvent(event);
			return;
		}
		next = m_hoverIndex - 1;
		break;
	case Qt::Key_Return:
	case Qt::Key_Enter:
	case Qt::Key_Space:
		if (m_hoverIndex >= 0 && m_hoverIndex <= last) {
			Q_EMIT sliceActivated(m_slices.at(m_hoverIndex).id);
			event->accept();
			return;
		}
		QWidget::keyPressEvent(event);
		return;
	default:
		QWidget::keyPressEvent(event);
		return;
	}

	if (next != m_hoverIndex) {
		m_hoverIndex = next;
		update();
		const QStringList lines = summaryLines();
		Q_EMIT hoverChanged(next >= 0 && next < lines.size() ? lines.at(next) : QString());
	}
	event->accept();
}

void CompositionChart::leaveEvent(QEvent* event)
{
	if (!hasFocus() && m_hoverIndex != -1) {
		m_hoverIndex = -1;
		update();
		Q_EMIT hoverChanged(QString());
	}
	QWidget::leaveEvent(event);
}

int CompositionChart::sliceAt(const QPoint& point) const
{
	if (m_slices.isEmpty()) {
		return -1;
	}
	if (m_sliceRects.size() == m_slices.size()) {
		for (int index = 0; index < m_sliceRects.size(); ++index) {
			if (m_sliceRects.at(index).contains(point)) {
				return index;
			}
		}
	}
	const QFont bodyFont = font();
	QFont titleFont = bodyFont;
	titleFont.setBold(true);
	const CompositionLayout layout = computeCompositionLayout(m_slices, rect(), QFontMetrics(titleFont), QFontMetrics(bodyFont), !m_title.isEmpty());
	for (int index = 0; index < layout.sliceRects.size(); ++index) {
		if (layout.sliceRects.at(index).contains(point)) {
			return index;
		}
	}
	for (int index = 0; index < layout.legendRects.size(); ++index) {
		if (layout.legendRects.at(index).contains(point)) {
			return index;
		}
	}
	return -1;
}

// ---------------------------------------------------------------------------
// PipelineChart
// ---------------------------------------------------------------------------

PipelineChart::PipelineChart(QWidget* parent)
	: QWidget(parent)
{
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
	m_emptyText = tr("No compiler stages configured.");
	setAccessibleName(tr("Compiler pipeline"));
	setAccessibleDescription(accessibleSummary());
}

void PipelineChart::setTitle(const QString& title)
{
	if (m_title == title) {
		return;
	}
	m_title = title;
	setAccessibleName(title.isEmpty() ? tr("Compiler pipeline") : title);
	setAccessibleDescription(accessibleSummary());
	updateGeometry();
	update();
}

void PipelineChart::setStages(const QVector<PipelineStageNode>& stages)
{
	m_stages = stages;
	m_stageRects.clear();
	m_hoverIndex = -1;
	setAccessibleDescription(accessibleSummary());
	updateGeometry();
	update();
}

void PipelineChart::setEmptyText(const QString& text)
{
	if (m_emptyText == text) {
		return;
	}
	m_emptyText = text;
	if (m_stages.isEmpty()) {
		setAccessibleDescription(accessibleSummary());
		update();
	}
}

void PipelineChart::setHighContrast(bool enabled)
{
	if (m_highContrast == enabled) {
		return;
	}
	m_highContrast = enabled;
	update();
}

void PipelineChart::setActiveStageId(const QString& stageId)
{
	if (m_activeStageId == stageId) {
		return;
	}
	m_activeStageId = stageId;
	setAccessibleDescription(accessibleSummary());
	update();
}

void PipelineChart::clear()
{
	m_activeStageId.clear();
	setStages(QVector<PipelineStageNode>());
}

QString PipelineChart::accessibleSummary() const
{
	if (m_stages.isEmpty()) {
		return m_title.isEmpty() ? m_emptyText : tr("%1. %2").arg(m_title, m_emptyText);
	}
	QString active = tr("none");
	for (const PipelineStageNode& stage : m_stages) {
		if (stage.id == m_activeStageId) {
			active = stage.label;
			break;
		}
	}
	const QString body = tr("Source to artifact through %1 stages. Active stage: %2. %3")
		.arg(m_stages.size())
		.arg(active, summaryLines().join(QStringLiteral("; ")));
	return m_title.isEmpty() ? body : tr("%1. %2").arg(m_title, body);
}

QStringList PipelineChart::summaryLines() const
{
	QStringList lines;
	if (m_stages.isEmpty()) {
		lines << m_emptyText;
		return lines;
	}
	lines.reserve(m_stages.size());
	for (int index = 0; index < m_stages.size(); ++index) {
		const PipelineStageNode& stage = m_stages.at(index);
		QString line = tr("Stage %1 of %2: %3 - %4")
			.arg(index + 1)
			.arg(m_stages.size())
			.arg(stage.label, stateName(stage.state));
		if (!stage.badgeText.isEmpty()) {
			line += tr(", %1").arg(stage.badgeText);
		}
		if (stage.optional) {
			line += tr(", optional");
		}
		if (stage.id == m_activeStageId) {
			line += tr(", active");
		}
		if (!stage.detail.isEmpty()) {
			line += tr(" - %1").arg(stage.detail);
		}
		lines << line;
	}
	return lines;
}

QSize PipelineChart::sizeHint() const
{
	// Same reasoning as CompositionChart::sizeHint(): measure the wrapped rows
	// at the width we have, not at an assumed one.
	const int preferredWidth = width() > 0 ? width() : 420;
	const QFont bodyFont = font();
	QFont titleFont = bodyFont;
	titleFont.setBold(true);
	const PipelineLayout layout = computePipelineLayout(m_stages,
		QRect(0, 0, preferredWidth, 2000),
		QFontMetrics(titleFont),
		QFontMetrics(bodyFont),
		!m_title.isEmpty(),
		tr("Source"),
		tr("Artifact"));
	return QSize(std::max(preferredWidth, 420), std::max(layout.requiredHeight, minimumSizeHint().height()));
}

QSize PipelineChart::minimumSizeHint() const
{
	const QFontMetrics metrics(font());
	int height = 2 * kMargin + metrics.height() * 2 + 16;
	if (!m_title.isEmpty()) {
		height += metrics.height() + kTitleGap;
	}
	return QSize(kMinBoxWidth + 2 * kMargin, height);
}

void PipelineChart::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event);

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QPalette pal = palette();
	const bool lightTheme = paletteIsLight(pal);
	const QColor foreground = chartForeground(pal, m_highContrast, lightTheme);
	const QColor muted = chartMuted(pal, m_highContrast, lightTheme);
	const QColor outline = chartOutline(pal, m_highContrast, lightTheme);
	const QColor surface = chartSurface(pal, m_highContrast, lightTheme);

	const QFont bodyFont = font();
	QFont titleFont = bodyFont;
	titleFont.setBold(true);
	QFont glyphFont = bodyFont;
	glyphFont.setBold(true);
	const QFontMetrics bodyMetrics(bodyFont);
	const QFontMetrics titleMetrics(titleFont);

	const PipelineLayout layout = computePipelineLayout(m_stages, rect(), titleMetrics, bodyMetrics,
		!m_title.isEmpty(), tr("Source"), tr("Artifact"));

	if (!m_title.isEmpty()) {
		painter.setFont(titleFont);
		painter.setPen(foreground);
		painter.drawText(layout.titleRect, Qt::AlignLeft | Qt::AlignVCenter,
			titleMetrics.elidedText(m_title, Qt::ElideRight, static_cast<int>(layout.titleRect.width())));
	}

	painter.setFont(bodyFont);

	if (m_stages.isEmpty()) {
		m_stageRects.clear();
		QRectF box = layout.boxes.isEmpty() ? QRectF(rect().adjusted(kMargin, kMargin, -kMargin, -kMargin)) : layout.boxes.first().rect;
		box.setRight(rect().right() - kMargin);
		painter.setPen(QPen(outline, 1.0, Qt::DashLine));
		painter.setBrush(surface);
		painter.drawRoundedRect(box, 6.0, 6.0);
		painter.setPen(muted);
		painter.drawText(box, Qt::AlignCenter,
			bodyMetrics.elidedText(m_emptyText, Qt::ElideRight, static_cast<int>(box.width()) - 8));
		return;
	}

	m_stageRects.fill(QRectF(), static_cast<int>(m_stages.size()));

	// Connectors first so the boxes paint over the arrow tips.
	for (int index = 1; index < layout.boxes.size(); ++index) {
		const PipelineBox& previous = layout.boxes.at(index - 1);
		const PipelineBox& current = layout.boxes.at(index);
		if (previous.row == current.row) {
			const qreal y = previous.rect.center().y();
			drawArrow(painter, QPointF(previous.rect.right() + 3.0, y), QPointF(current.rect.left() - 2.0, y), muted);
		} else {
			// Wrapped rows: a dashed stub leaves the old row and re-enters the
			// new one, so the chain still reads without an off-screen arrow.
			drawDashedStub(painter, QPointF(previous.rect.right() + 3.0, previous.rect.center().y()),
				QPointF(rect().right() - kMargin / 2.0, previous.rect.center().y()), muted);
			drawDashedStub(painter, QPointF(rect().left() + kMargin / 2.0, current.rect.center().y()),
				QPointF(current.rect.left() - 2.0, current.rect.center().y()), muted);
		}
	}

	for (const PipelineBox& box : layout.boxes) {
		const bool isStage = box.stageIndex >= 0;
		const bool isActive = isStage && m_stages.at(box.stageIndex).id == m_activeStageId && !m_activeStageId.isEmpty();
		const bool isHovered = isStage && box.stageIndex == m_hoverIndex;
		if (isStage) {
			m_stageRects[box.stageIndex] = box.rect;
		}

		QColor accent = box.endpoint ? muted : studioStateColor(box.state, m_highContrast, lightTheme);
		QColor fill = surface;
		if (isActive) {
			QColor tint = accent;
			tint.setAlpha(m_highContrast ? 40 : 56);
			fill = tint;
		} else if (isHovered) {
			QColor tint = accent;
			tint.setAlpha(m_highContrast ? 28 : 34);
			fill = tint;
		}

		painter.setBrush(fill);
		Qt::PenStyle style = Qt::SolidLine;
		if (box.endpoint || box.optional) {
			style = Qt::DashLine; // Non-colour cue for endpoints and optional stages.
		}
		painter.setPen(QPen(isActive ? accent : outline, isActive ? 2.0 : 1.0, style));
		painter.drawRoundedRect(box.rect.adjusted(0.5, 0.5, -0.5, -0.5), 6.0, 6.0);

		const QRectF inner = box.rect.adjusted(kBoxPadding, 4.0, -kBoxPadding, -4.0);
		const qreal lineHeight = inner.height() / 2.0;

		const QRectF glyphRect(inner.left(), inner.top(), kGlyphColumn, lineHeight);
		painter.setFont(glyphFont);
		painter.setPen(accent);
		painter.drawText(glyphRect, Qt::AlignLeft | Qt::AlignVCenter, box.glyph);

		painter.setFont(bodyFont);
		const QRectF labelRect(inner.left() + kGlyphColumn, inner.top(),
			std::max(0.0, inner.width() - kGlyphColumn), lineHeight);
		painter.setPen(isActive || isHovered ? foreground : (box.endpoint ? muted : foreground));
		painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter,
			bodyMetrics.elidedText(box.label, Qt::ElideRight, static_cast<int>(labelRect.width())));

		const QString badge = pipelineBadgeLine(box);
		if (!badge.isEmpty()) {
			const QRectF badgeRect(inner.left(), inner.top() + lineHeight, inner.width(), lineHeight);
			painter.setPen(muted);
			painter.drawText(badgeRect, Qt::AlignLeft | Qt::AlignVCenter,
				bodyMetrics.elidedText(badge, Qt::ElideRight, static_cast<int>(badgeRect.width())));
		}

		if (isActive) {
			painter.setPen(Qt::NoPen);
			painter.setBrush(accent);
			painter.drawRoundedRect(QRectF(box.rect.left() + 2.0, box.rect.top() + 6.0, 3.0, box.rect.height() - 12.0), 1.5, 1.5);
		}
	}

	if (hasFocus() && m_hoverIndex >= 0 && m_hoverIndex < m_stageRects.size()) {
		drawFocusRing(painter, m_stageRects.at(m_hoverIndex), foreground, 6.0);
	}
}

void PipelineChart::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton) {
		QWidget::mousePressEvent(event);
		return;
	}
	const int index = stageAt(event->position().toPoint());
	if (index < 0) {
		QWidget::mousePressEvent(event);
		return;
	}
	if (m_hoverIndex != index) {
		m_hoverIndex = index;
		update();
	}
	setFocus(Qt::MouseFocusReason);
	Q_EMIT stageActivated(m_stages.at(index).id);
	event->accept();
}

void PipelineChart::mouseMoveEvent(QMouseEvent* event)
{
	const int index = stageAt(event->position().toPoint());
	if (index == m_hoverIndex) {
		QWidget::mouseMoveEvent(event);
		return;
	}
	m_hoverIndex = index;
	update();
	const QStringList lines = summaryLines();
	Q_EMIT hoverChanged(index >= 0 && index < lines.size() ? lines.at(index) : QString());
	QWidget::mouseMoveEvent(event);
}

void PipelineChart::keyPressEvent(QKeyEvent* event)
{
	if (m_stages.isEmpty()) {
		QWidget::keyPressEvent(event);
		return;
	}

	const int last = static_cast<int>(m_stages.size()) - 1;
	int next = m_hoverIndex;
	switch (event->key()) {
	case Qt::Key_Right:
	case Qt::Key_Down:
		next = m_hoverIndex < 0 ? 0 : std::min(last, m_hoverIndex + 1);
		break;
	case Qt::Key_Left:
	case Qt::Key_Up:
		next = m_hoverIndex < 0 ? last : std::max(0, m_hoverIndex - 1);
		break;
	case Qt::Key_Home:
		next = 0;
		break;
	case Qt::Key_End:
		next = last;
		break;
	case Qt::Key_Tab:
		if (m_hoverIndex >= last) {
			QWidget::keyPressEvent(event);
			return;
		}
		next = m_hoverIndex < 0 ? 0 : m_hoverIndex + 1;
		break;
	case Qt::Key_Backtab:
		if (m_hoverIndex <= 0) {
			QWidget::keyPressEvent(event);
			return;
		}
		next = m_hoverIndex - 1;
		break;
	case Qt::Key_Return:
	case Qt::Key_Enter:
	case Qt::Key_Space:
		if (m_hoverIndex >= 0 && m_hoverIndex <= last) {
			Q_EMIT stageActivated(m_stages.at(m_hoverIndex).id);
			event->accept();
			return;
		}
		QWidget::keyPressEvent(event);
		return;
	default:
		QWidget::keyPressEvent(event);
		return;
	}

	if (next != m_hoverIndex) {
		m_hoverIndex = next;
		update();
		const QStringList lines = summaryLines();
		Q_EMIT hoverChanged(next >= 0 && next < lines.size() ? lines.at(next) : QString());
	}
	event->accept();
}

void PipelineChart::leaveEvent(QEvent* event)
{
	if (!hasFocus() && m_hoverIndex != -1) {
		m_hoverIndex = -1;
		update();
		Q_EMIT hoverChanged(QString());
	}
	QWidget::leaveEvent(event);
}

int PipelineChart::stageAt(const QPoint& point) const
{
	if (m_stages.isEmpty()) {
		return -1;
	}
	if (m_stageRects.size() == m_stages.size()) {
		for (int index = 0; index < m_stageRects.size(); ++index) {
			if (m_stageRects.at(index).contains(point)) {
				return index;
			}
		}
		return -1;
	}
	const QFont bodyFont = font();
	QFont titleFont = bodyFont;
	titleFont.setBold(true);
	const PipelineLayout layout = computePipelineLayout(m_stages, rect(), QFontMetrics(titleFont), QFontMetrics(bodyFont),
		!m_title.isEmpty(), tr("Source"), tr("Artifact"));
	for (const PipelineBox& box : layout.boxes) {
		if (box.stageIndex >= 0 && box.rect.contains(point)) {
			return box.stageIndex;
		}
	}
	return -1;
}

// ---------------------------------------------------------------------------
// ActivityTimelineChart
// ---------------------------------------------------------------------------

ActivityTimelineChart::ActivityTimelineChart(QWidget* parent)
	: QWidget(parent)
{
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
	m_emptyText = tr("No recent activity.");
	setAccessibleName(tr("Activity timeline"));
	setAccessibleDescription(accessibleSummary());
}

void ActivityTimelineChart::setEvents(const QVector<TimelineEvent>& events)
{
	m_events = events;
	// Newest first; ties keep their incoming order so callers can control it.
	std::stable_sort(m_events.begin(), m_events.end(), [](const TimelineEvent& lhs, const TimelineEvent& rhs) {
		return lhs.startedMsSinceEpoch > rhs.startedMsSinceEpoch;
	});
	m_eventRects.clear();
	m_hoverIndex = -1;
	setAccessibleDescription(accessibleSummary());
	updateGeometry();
	update();
}

void ActivityTimelineChart::setEmptyText(const QString& text)
{
	if (m_emptyText == text) {
		return;
	}
	m_emptyText = text;
	if (m_events.isEmpty()) {
		setAccessibleDescription(accessibleSummary());
		update();
	}
}

void ActivityTimelineChart::setHighContrast(bool enabled)
{
	if (m_highContrast == enabled) {
		return;
	}
	m_highContrast = enabled;
	update();
}

void ActivityTimelineChart::clear()
{
	setEvents(QVector<TimelineEvent>());
}

QString ActivityTimelineChart::accessibleSummary() const
{
	if (m_events.isEmpty()) {
		return m_emptyText;
	}
	return tr("%1 recent events, newest first. %2").arg(m_events.size()).arg(summaryLines().join(QStringLiteral("; ")));
}

QStringList ActivityTimelineChart::summaryLines() const
{
	QStringList lines;
	if (m_events.isEmpty()) {
		lines << m_emptyText;
		return lines;
	}
	// Relative stamps are measured from the newest event, never from the wall
	// clock, so the widget renders the same text every time for a given set.
	const qint64 reference = m_events.first().startedMsSinceEpoch;
	lines.reserve(m_events.size());
	for (const TimelineEvent& item : m_events) {
		QString line = tr("%1: %2").arg(stateName(item.state), item.label);
		if (!item.source.isEmpty()) {
			line += tr(" from %1").arg(item.source);
		}
		line += tr(", %1").arg(formatRelativeText(reference - item.startedMsSinceEpoch));
		line += tr(", took %1").arg(formatDurationText(item.durationMs, false));
		if (!item.detail.isEmpty()) {
			line += tr(" - %1").arg(item.detail);
		}
		lines << line;
	}
	return lines;
}

QSize ActivityTimelineChart::sizeHint() const
{
	const QFontMetrics metrics(font());
	const int rows = std::max(1, static_cast<int>(m_events.size()));
	const int rowHeight = timelineRowHeight(metrics);
	const int height = 2 * kMargin + rows * rowHeight + (rows - 1) * kTimelineRowGap;
	return QSize(std::max(width(), 320), height);
}

QSize ActivityTimelineChart::minimumSizeHint() const
{
	const QFontMetrics metrics(font());
	return QSize(200, 2 * kMargin + timelineRowHeight(metrics));
}

void ActivityTimelineChart::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event);

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QPalette pal = palette();
	const bool lightTheme = paletteIsLight(pal);
	const QColor foreground = chartForeground(pal, m_highContrast, lightTheme);
	const QColor muted = chartMuted(pal, m_highContrast, lightTheme);
	const QColor outline = chartOutline(pal, m_highContrast, lightTheme);
	const QColor surface = chartSurface(pal, m_highContrast, lightTheme);

	const QFont bodyFont = font();
	QFont glyphFont = bodyFont;
	glyphFont.setBold(true);
	const QFontMetrics metrics(bodyFont);
	painter.setFont(bodyFont);

	if (m_events.isEmpty()) {
		m_eventRects.clear();
		const QRectF box = QRectF(rect()).adjusted(kMargin, kMargin, -kMargin, -kMargin);
		painter.setPen(QPen(outline, 1.0, Qt::DashLine));
		painter.setBrush(surface);
		painter.drawRoundedRect(box, 6.0, 6.0);
		painter.setPen(muted);
		painter.drawText(box, Qt::AlignCenter,
			metrics.elidedText(m_emptyText, Qt::ElideRight, static_cast<int>(box.width()) - 8));
		return;
	}

	m_eventRects = computeTimelineRows(static_cast<int>(m_events.size()), rect(), metrics);

	qint64 longest = 0;
	for (const TimelineEvent& item : m_events) {
		longest = std::max(longest, item.durationMs);
	}
	const qint64 reference = m_events.first().startedMsSinceEpoch;
	const qreal lineHeight = static_cast<qreal>(metrics.height());

	for (int index = 0; index < m_events.size() && index < m_eventRects.size(); ++index) {
		const TimelineEvent& item = m_events.at(index);
		const QRectF row = m_eventRects.at(index);
		const QColor accent = studioStateColor(item.state, m_highContrast, lightTheme);

		if (index == m_hoverIndex) {
			QColor tint = accent;
			tint.setAlpha(m_highContrast ? 34 : 42);
			painter.setPen(Qt::NoPen);
			painter.setBrush(tint);
			painter.drawRoundedRect(row, 4.0, 4.0);
		}

		const QRectF inner = row.adjusted(4.0, 4.0, -4.0, -4.0);

		const QString relative = formatRelativeText(reference - item.startedMsSinceEpoch);
		const int relativeWidth = metrics.horizontalAdvance(relative) + 6;

		painter.setFont(glyphFont);
		painter.setPen(accent);
		painter.drawText(QRectF(inner.left(), inner.top(), kGlyphColumn, lineHeight),
			Qt::AlignLeft | Qt::AlignVCenter, studioStateGlyph(item.state));

		painter.setFont(bodyFont);
		const QRectF labelRect(inner.left() + kGlyphColumn, inner.top(),
			std::max(0.0, inner.width() - kGlyphColumn - relativeWidth), lineHeight);
		painter.setPen(foreground);
		painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter,
			metrics.elidedText(item.label, Qt::ElideRight, static_cast<int>(labelRect.width())));

		painter.setPen(muted);
		painter.drawText(QRectF(inner.right() - relativeWidth, inner.top(), static_cast<qreal>(relativeWidth), lineHeight),
			Qt::AlignRight | Qt::AlignVCenter, relative);

		const qreal secondTop = inner.top() + lineHeight;
		const QString durationText = formatDurationText(item.durationMs, false);
		const int durationWidth = metrics.horizontalAdvance(durationText) + 6;
		const qreal sourceWidth = std::max(0.0, inner.width() * 0.38);
		painter.setPen(muted);
		painter.drawText(QRectF(inner.left() + kGlyphColumn, secondTop, std::max(0.0, sourceWidth - kGlyphColumn), lineHeight),
			Qt::AlignLeft | Qt::AlignVCenter,
			metrics.elidedText(item.source, Qt::ElideRight, static_cast<int>(std::max(0.0, sourceWidth - kGlyphColumn))));

		const qreal trackLeft = inner.left() + sourceWidth + 6.0;
		const qreal trackRight = inner.right() - durationWidth;
		const qreal trackWidth = trackRight - trackLeft;
		const qreal barHeight = std::max(6.0, lineHeight * 0.45);
		const QRectF track(trackLeft, secondTop + (lineHeight - barHeight) / 2.0, std::max(0.0, trackWidth), barHeight);
		if (track.width() > 2.0) {
			painter.setPen(QPen(outline, 1.0));
			painter.setBrush(Qt::NoBrush);
			painter.drawRoundedRect(track.adjusted(0.5, 0.5, -0.5, -0.5), 2.0, 2.0);

			// Zero-duration and single-event sets never divide: an empty or
			// unknown duration is drawn as a minimal tick instead.
			qreal fillWidth = 2.0;
			if (item.durationMs > 0 && longest > 0) {
				const double ratio = static_cast<double>(item.durationMs) / static_cast<double>(longest);
				fillWidth = std::max(3.0, track.width() * ratio);
			}
			fillWidth = std::min(fillWidth, track.width());
			const QRectF fill(track.left(), track.top(), fillWidth, track.height());
			drawPatternedRect(painter, fill, accent, index, 2.0);
		}

		painter.setPen(muted);
		painter.drawText(QRectF(inner.right() - durationWidth, secondTop, static_cast<qreal>(durationWidth), lineHeight),
			Qt::AlignRight | Qt::AlignVCenter, durationText);
	}

	if (hasFocus() && m_hoverIndex >= 0 && m_hoverIndex < m_eventRects.size()) {
		drawFocusRing(painter, m_eventRects.at(m_hoverIndex), foreground, 4.0);
	}
}

void ActivityTimelineChart::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton) {
		QWidget::mousePressEvent(event);
		return;
	}
	const int index = eventAt(event->position().toPoint());
	if (index < 0) {
		QWidget::mousePressEvent(event);
		return;
	}
	if (m_hoverIndex != index) {
		m_hoverIndex = index;
		update();
	}
	setFocus(Qt::MouseFocusReason);
	Q_EMIT eventActivated(m_events.at(index).id);
	event->accept();
}

void ActivityTimelineChart::mouseMoveEvent(QMouseEvent* event)
{
	const int index = eventAt(event->position().toPoint());
	if (index == m_hoverIndex) {
		QWidget::mouseMoveEvent(event);
		return;
	}
	m_hoverIndex = index;
	update();
	const QStringList lines = summaryLines();
	Q_EMIT hoverChanged(index >= 0 && index < lines.size() ? lines.at(index) : QString());
	QWidget::mouseMoveEvent(event);
}

void ActivityTimelineChart::leaveEvent(QEvent* event)
{
	if (!hasFocus() && m_hoverIndex != -1) {
		m_hoverIndex = -1;
		update();
		Q_EMIT hoverChanged(QString());
	}
	QWidget::leaveEvent(event);
}

int ActivityTimelineChart::eventAt(const QPoint& point) const
{
	if (m_events.isEmpty()) {
		return -1;
	}
	if (m_eventRects.size() == m_events.size()) {
		for (int index = 0; index < m_eventRects.size(); ++index) {
			if (m_eventRects.at(index).contains(point)) {
				return index;
			}
		}
		return -1;
	}
	const QVector<QRectF> rows = computeTimelineRows(static_cast<int>(m_events.size()), rect(), QFontMetrics(font()));
	for (int index = 0; index < rows.size(); ++index) {
		if (rows.at(index).contains(point)) {
			return index;
		}
	}
	return -1;
}

} // namespace vibestudio
