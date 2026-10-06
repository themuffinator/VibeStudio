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
		&& a.pixelPhase.x() == b.pixelPhase.x() && a.pixelPhase.y() == b.pixelPhase.y();
}
namespace {
bool valid(const MapGridView& view)
{
	return !view.viewport.isEmpty() && std::isfinite(view.zoom) && view.zoom > 0
		&& std::isfinite(view.center.x()) && std::isfinite(view.center.y());
}
}

MapGridLines mapGridLines(const MapGridView& view)
{
	MapGridLines result;
	if (!valid(view)) { return result; }
	double step = view.units > 0 ? double(view.units) : 64.0;
	for (int guard = 0; guard < 40 && step * view.zoom < 6.0; ++guard) { step *= 2; }
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
	const auto lines = mapGridLines(view);
	const QRgb colors[] = {view.minor,view.major,view.axis};
	{
		QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing); painter.setBrush(Qt::NoBrush);
		painter.translate(view.pixelPhase / view.pixelRatio);
		for (int pass = 0; pass < 3; ++pass) {
			if (stopped()) { return false; }
			painter.setPen(QPen(QColor::fromRgba(colors[pass]),pass == 2 ? 2.0 : 1.0));
			// Check between small native batches so closing or superseding a
			// dense grid does not wait for an entire image's antialiased lines.
			for (qsizetype start = 0; start < lines[pass].size(); start += 16) {
				if (stopped()) { return false; }
				painter.drawLines(lines[pass].constData() + start,int(std::min<qsizetype>(16,lines[pass].size() - start)));
			}
		}
	}
	if (stopped()) { return false; }
	frame->view = view; frame->image = std::move(image); return true;
}
} // namespace vibestudio
