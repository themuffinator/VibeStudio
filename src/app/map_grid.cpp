// Block grid and coordinate labels: workflow reference NRC XYWnd::XY_DrawGrid
// (coordinate text) and XYWnd::XY_DrawBlockGrid in radiant/xywindow.cpp at
// 449778b3 (GPL-2.0-or-later). Original Qt implementation; see
// docs/CREDITS.md.

#include "app/map_grid.h"
#include "app/viewport_image.h"
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace vibestudio {
bool sameMapGridView(const MapGridView& a, const MapGridView& b)
{
	return a.viewport == b.viewport && a.center.x() == b.center.x() && a.center.y() == b.center.y()
		&& a.zoom == b.zoom && a.pixelRatio == b.pixelRatio && a.units == b.units
		&& a.minor == b.minor && a.major == b.major && a.axis == b.axis
		&& a.pixelPhase.x() == b.pixelPhase.x() && a.pixelPhase.y() == b.pixelPhase.y()
		&& a.blockX == b.blockX && a.blockY == b.blockY && a.block == b.block && a.blockWidth == b.blockWidth
		&& a.blockBounds.x() == b.blockBounds.x() && a.blockBounds.y() == b.blockBounds.y()
		&& a.blockBounds.width() == b.blockBounds.width() && a.blockBounds.height() == b.blockBounds.height()
		&& a.clipBlocks == b.clipBlocks && a.lines == b.lines;
}
namespace {
bool valid(const MapGridView& view)
{
	return !view.viewport.isEmpty() && std::isfinite(view.zoom) && view.zoom > 0
		&& std::isfinite(view.center.x()) && std::isfinite(view.center.y());
}
}

double mapGridDrawnSpacing(double units, double zoom)
{
	double step = std::isfinite(units) && units > 0 ? units : 64.0;
	if (!std::isfinite(zoom) || zoom <= 0) { return step; }
	for (int guard = 0; guard < 40 && step * zoom < 6.0; ++guard) { step *= 2; }
	return step;
}

MapGridLines mapGridLines(const MapGridView& view)
{
	MapGridLines result;
	if (!valid(view)) { return result; }
	const double step = mapGridDrawnSpacing(view.units, view.zoom);
	if (step * view.zoom < 1.0) { return result; }
	const double left = view.center.x() - view.viewport.width() * 0.5 / view.zoom;
	const double right = view.center.x() + view.viewport.width() * 0.5 / view.zoom;
	const double bottom = view.center.y() - view.viewport.height() * 0.5 / view.zoom;
	const double top = view.center.y() + view.viewport.height() * 0.5 / view.zoom;
	const double firstX = std::floor(left / step) * step, firstY = std::floor(bottom / step) * step;
	if (!std::isfinite(firstX) || !std::isfinite(firstY) || !std::isfinite(right - firstX) || !std::isfinite(top - firstY)) { return result; }
	const int columns = int(std::clamp((right - firstX) / step + 2,0.0,1024.0));
	const int rows = int(std::clamp((top - firstY) / step + 2,0.0,1024.0));
	for (int i = 0; i < columns; ++i) {
		const double x = firstX + i * step, lane = std::round(x / step);
		if (lane == 0) { continue; }
		const double at = view.viewport.width() * 0.5 + (x - view.center.x()) * view.zoom;
		result[std::fmod(lane,8.0) == 0 ? 1 : 0].append(QLineF(at,0,at,view.viewport.height()));
	}
	for (int i = 0; i < rows; ++i) {
		const double y = firstY + i * step, lane = std::round(y / step);
		if (lane == 0) { continue; }
		const double at = view.viewport.height() * 0.5 - (y - view.center.y()) * view.zoom;
		result[std::fmod(lane,8.0) == 0 ? 1 : 0].append(QLineF(0,at,view.viewport.width(),at));
	}
	if (left <= 0 && right >= 0) {
		const double at = view.viewport.width() * 0.5 - view.center.x() * view.zoom;
		result[2].append(QLineF(at,0,at,view.viewport.height()));
	}
	if (bottom <= 0 && top >= 0) {
		const double at = view.viewport.height() * 0.5 + view.center.y() * view.zoom;
		result[2].append(QLineF(0,at,view.viewport.width(),at));
	}
	return result;
}

namespace {
// The visible drawing-plane range, x then y.
struct PlaneRange {
	double left = 0, right = 0, bottom = 0, top = 0;
};
PlaneRange visibleRange(const MapGridView& view)
{
	return {view.center.x() - view.viewport.width() * 0.5 / view.zoom, view.center.x() + view.viewport.width() * 0.5 / view.zoom,
		view.center.y() - view.viewport.height() * 0.5 / view.zoom, view.center.y() + view.viewport.height() * 0.5 / view.zoom};
}
double viewX(const MapGridView& view, double x) { return view.viewport.width() * 0.5 + (x - view.center.x()) * view.zoom; }
double viewY(const MapGridView& view, double y) { return view.viewport.height() * 0.5 - (y - view.center.y()) * view.zoom; }
// Multiples of `spacing` from low to high; false when there is nothing to
// draw or more than `limit` would be needed.
bool multiples(double low, double high, double spacing, int limit, double* first, int* count)
{
	if (!(spacing > 0) || !std::isfinite(low) || !std::isfinite(high) || low > high) { return false; }
	const double from = std::ceil(low / spacing), to = std::floor(high / spacing);
	if (!std::isfinite(from) || !std::isfinite(to) || to < from || to - from + 1 > limit) { return false; }
	*first = from; *count = int(to - from + 1); return true;
}
// Doubles a block size until its lines are 6 pixels apart, keeping every
// drawn line on a real boundary.
double drawnBlockSpacing(double size, double zoom)
{
	if (!std::isfinite(size) || size <= 0) { return 0; }
	for (int guard = 0; guard < 40 && size * zoom < 6.0; ++guard) { size *= 2; }
	return size;
}
}

QVector<QLineF> mapBlockGridLines(const MapGridView& view)
{
	QVector<QLineF> lines;
	if (!valid(view) || (view.blockX <= 0 && view.blockY <= 0)) { return lines; }
	PlaneRange range = visibleRange(view);
	if (view.clipBlocks || view.blockBounds.isValid()) {
		range.left = std::max(range.left,view.blockBounds.left()); range.right = std::min(range.right,view.blockBounds.right());
		range.bottom = std::max(range.bottom,view.blockBounds.top()); range.top = std::min(range.top,view.blockBounds.bottom());
		if (range.left > range.right || range.bottom > range.top) { return lines; }
	}
	const double top = viewY(view,range.top), bottom = viewY(view,range.bottom);
	const double left = viewX(view,range.left), right = viewX(view,range.right);
	double first = 0; int count = 0;
	if (const double spacing = drawnBlockSpacing(view.blockX,view.zoom); multiples(range.left,range.right,spacing,1024,&first,&count)) {
		for (int i = 0; i < count; ++i) { const double at = viewX(view,(first + i) * spacing); lines.append(QLineF(at,top,at,bottom)); }
	}
	if (const double spacing = drawnBlockSpacing(view.blockY,view.zoom); multiples(range.bottom,range.top,spacing,1024,&first,&count)) {
		for (int i = 0; i < count; ++i) { const double at = viewY(view,(first + i) * spacing); lines.append(QLineF(left,at,right,at)); }
	}
	return lines;
}

QPen mapBlockGridPen(const MapGridView& view)
{
	QPen pen(QColor::fromRgba(view.block),view.blockWidth > 0 ? view.blockWidth : 2.0,Qt::CustomDashLine,Qt::FlatCap);
	pen.setDashPattern({4.0,2.0});
	return pen;
}

QString mapGridCoordinateText(double value, double step)
{
	int decimals = 0;
	if (std::isfinite(step) && step > 0) {
		for (double scaled = step; decimals < 6 && std::abs(scaled - std::round(scaled)) > 1e-9 * std::max(1.0,std::abs(scaled)); scaled *= 10) { ++decimals; }
	}
	if (!std::isfinite(value) || std::abs(value) < 0.5 * std::pow(10.0,-decimals)) { value = 0; }
	QString text = QString::number(value,'f',decimals);
	if (text.contains(QLatin1Char('.'))) {
		while (text.endsWith(QLatin1Char('0'))) { text.chop(1); }
		if (text.endsWith(QLatin1Char('.'))) { text.chop(1); }
	}
	return text == QLatin1String("-0") ? QStringLiteral("0") : text;
}

MapGridCoordinateLayout mapGridCoordinateLabels(const MapGridView& view, const QFontMetricsF& metrics,
	Qt::LayoutDirection direction, const QVector<QRectF>& obstacles)
{
	MapGridCoordinateLayout layout;
	if (!valid(view)) { return layout; }
	constexpr double edge = 4, padX = 3, padY = 1, minimumGap = 32;
	const double width = view.viewport.width(), height = view.viewport.height();
	const double boxHeight = std::ceil(metrics.height()) + padY * 2;
	if (width < 48 || height < boxHeight * 3) { return layout; }
	const PlaneRange range = visibleRange(view);
	const double base = mapGridDrawnSpacing(view.units,view.zoom);
	const auto boxWidth = [&](double value, double step) {
		return std::ceil(metrics.horizontalAdvance(mapGridCoordinateText(value,step))) + padX * 2;
	};
	// Across: wide enough for the widest number in view, which is at an end:
	// the last line there, or its neighbour when that one needs more decimals.
	double acrossStep = base;
	for (int guard = 0; guard < 40; ++guard) {
		const double first = std::ceil(range.left / acrossStep), last = std::floor(range.right / acrossStep);
		double widest = 0;
		for (const double index : {first, first + 1, last - 1, last}) { widest = std::max(widest,boxWidth(index * acrossStep,acrossStep)); }
		if (acrossStep * view.zoom >= std::max(minimumGap,widest + 6)) { break; }
		acrossStep *= 2;
	}
	double upStep = base;
	for (int guard = 0; guard < 40 && upStep * view.zoom < std::max(minimumGap,boxHeight + 6); ++guard) { upStep *= 2; }
	layout.acrossStep = acrossStep; layout.upStep = upStep;
	const bool rtl = direction == Qt::RightToLeft;
	const QRectF pane(2,2,width - 4,height - 4);
	const QRectF bottomBand(0,height - edge - boxHeight - 2,width,boxHeight + edge + 2);
	const auto blocked = [&](const QRectF& box) {
		if (!pane.contains(box)) { return true; }
		for (const QRectF& obstacle : obstacles) { if (!obstacle.isEmpty() && box.intersects(obstacle)) { return true; } }
		return false;
	};
	// Up the leading edge first, so the bottom row can keep clear of it.
	double first = 0; int count = 0;
	double column = 0;
	if (multiples(range.bottom,range.top,upStep,256,&first,&count)) {
		for (int i = 0; i < count; ++i) {
			const double value = (first + i) * upStep;
			MapGridCoordinateLabel label; label.value = value == 0 ? 0 : value; label.text = mapGridCoordinateText(value,upStep); label.across = false;
			const double boxW = boxWidth(value,upStep);
			label.bounds = QRectF(rtl ? width - edge - boxW : edge,viewY(view,value) - boxHeight * 0.5,boxW,boxHeight);
			if (blocked(label.bounds) || label.bounds.intersects(bottomBand)) { continue; }
			column = std::max(column,boxW);
			layout.labels.append(label);
		}
	}
	const QRectF columnBand = column > 0 ? QRectF(rtl ? width - edge - column - 2 : 0,0,column + edge + 2,height) : QRectF();
	if (multiples(range.left,range.right,acrossStep,256,&first,&count)) {
		for (int i = 0; i < count; ++i) {
			const double value = (first + i) * acrossStep;
			MapGridCoordinateLabel label; label.value = value == 0 ? 0 : value; label.text = mapGridCoordinateText(value,acrossStep);
			const double boxW = boxWidth(value,acrossStep);
			label.bounds = QRectF(viewX(view,value) - boxW * 0.5,height - edge - boxHeight,boxW,boxHeight);
			if (blocked(label.bounds) || (!columnBand.isEmpty() && label.bounds.intersects(columnBand))) { continue; }
			layout.labels.append(label);
		}
	}
	return layout;
}

QSize mapGridImageSize(const MapGridView& view)
{
	return valid(view) ? viewportImageSize(view.viewport,view.pixelRatio,view.pixelPhase) : QSize();
}

bool renderMapGrid(const MapGridView& view, MapGridFrame* frame, const std::atomic_bool* cancelled)
{
	const auto stopped = [&] { return cancelled && cancelled->load(std::memory_order_relaxed); };
	const QSize size = mapGridImageSize(view);
	if (!frame || size.isEmpty() || stopped()) { return false; }
	if (!frame->image.isNull() && sameMapGridView(frame->view,view)) { return true; }
	QImage image(size,QImage::Format_ARGB32_Premultiplied);
	if (image.isNull()) { return false; }
	image.setDevicePixelRatio(view.pixelRatio); image.fill(Qt::transparent);
	const auto lines = view.lines ? mapGridLines(view) : MapGridLines();
	const auto blocks = mapBlockGridLines(view);
	const QRgb colors[] = {view.minor,view.major,view.axis};
	{
		QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing); painter.setBrush(Qt::NoBrush);
		painter.translate(view.pixelPhase / view.pixelRatio);
		const auto draw = [&](const QVector<QLineF>& batch) {
			// Check between small native batches so closing or superseding a
			// dense grid does not wait for an entire image's antialiased lines.
			for (qsizetype start = 0; start < batch.size(); start += 16) {
				if (stopped()) { return false; }
				painter.drawLines(batch.constData() + start,int(std::min<qsizetype>(16,batch.size() - start)));
			}
			return true;
		};
		for (int pass = 0; pass < 3; ++pass) {
			if (stopped()) { return false; }
			painter.setPen(QPen(QColor::fromRgba(colors[pass]),pass == 2 ? 2.0 : 1.0));
			if (!draw(lines[pass])) { return false; }
		}
		if (!blocks.isEmpty()) {
			painter.setPen(mapBlockGridPen(view));
			if (!draw(blocks)) { return false; }
		}
	}
	if (stopped()) { return false; }
	frame->view = view; frame->image = std::move(image); return true;
}
} // namespace vibestudio
