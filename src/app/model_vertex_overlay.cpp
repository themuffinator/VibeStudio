#include "app/model_vertex_overlay.h"
#include "app/model_rasterizer.h"

#include <QRectF>
#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
bool finite(QPointF point) { return std::isfinite(point.x()) && std::isfinite(point.y()); }
int cell(double coordinate, int extent)
{
	// Clamp in floating point before conversion, including very distant points.
	return int(std::clamp(std::floor(coordinate / std::max(1, extent) * ModelVertexProjection::cellCount), 0.0,
						  double(ModelVertexProjection::cellCount - 1)));
}
bool stopped(const std::atomic_bool *cancelled) { return cancelled && cancelled->load(); }
} // namespace

bool indexModelVertices(ModelVertexProjection *projection, const std::atomic_bool *cancelled)
{
	if (!projection || projection->logicalSize.isEmpty() || projection->visible.size() != projection->positions.size())
	{
		return false;
	}
	for (auto &entries : projection->cells)
	{
		entries.clear();
	}
	for (int vertex = 0; vertex < projection->positions.size(); ++vertex)
	{
		if (stopped(cancelled))
		{
			return false;
		}
		const auto point = projection->positions[vertex];
		if (finite(point))
		{
			projection
				->cells[cell(point.y(), projection->logicalSize.height()) * ModelVertexProjection::cellCount +
						cell(point.x(), projection->logicalSize.width())]
				.append(vertex);
		}
	}
	return !stopped(cancelled);
}

int pickModelVertex(const ModelVertexProjection &projection, QPointF point, double tolerance, bool xray)
{
	if (!finite(point) || !std::isfinite(tolerance) || tolerance < 0 || tolerance > 64 || projection.logicalSize.isEmpty())
	{
		return -1;
	}
	const int left = cell(point.x() - tolerance, projection.logicalSize.width());
	const int right = cell(point.x() + tolerance, projection.logicalSize.width());
	const int top = cell(point.y() - tolerance, projection.logicalSize.height());
	const int bottom = cell(point.y() + tolerance, projection.logicalSize.height());
	int result = -1;
	double closest = tolerance * tolerance;
	for (int y = top; y <= bottom; ++y)
	{
		for (int x = left; x <= right; ++x)
		{
			for (int vertex : projection.cells[y * ModelVertexProjection::cellCount + x])
			{
				if (!xray && !projection.visible.testBit(vertex))
				{
					continue;
				}
				const auto delta = projection.positions[vertex] - point;
				const double distance = QPointF::dotProduct(delta, delta);
				if (distance < closest || (distance == closest && (result < 0 || vertex < result)))
				{
					closest = distance;
					result = vertex;
				}
			}
		}
	}
	return result;
}

bool renderModelVertexOverlay(const ModelVertexProjection &projection, QSize size, double pixelRatio, const QSet<int> &selectedVertices,
							  bool xray, bool transforming, QColor accent, QImage *image, const std::atomic_bool *cancelled)
{
	if (!image || size.isEmpty() || qint64(size.width()) * size.height() > modelRasterMaxPixels || !std::isfinite(pixelRatio) ||
		pixelRatio <= 0 || pixelRatio > 64 || projection.visible.size() != projection.positions.size())
	{
		return false;
	}
	QImage result(size, QImage::Format_ARGB32_Premultiplied);
	if (result.isNull())
	{
		return false;
	}
	result.fill(Qt::transparent);
	QVector<QPoint> visible, selected, hidden;
	const QRectF bounds(QPointF{}, projection.logicalSize);
	const int margin = std::max(1, int(std::ceil(4 * pixelRatio)));
	for (int vertex = 0; vertex < projection.positions.size(); ++vertex)
	{
		if (stopped(cancelled))
		{
			return false;
		}
		const auto point = projection.positions[vertex];
		if (!finite(point) || !bounds.contains(point))
		{
			continue;
		}
		const bool shown = projection.visible.testBit(vertex), active = selectedVertices.contains(vertex);
		if (!shown && !xray && !(transforming && active))
		{
			continue;
		}
		const auto physical = point * pixelRatio;
		if (physical.x() < -margin || physical.x() > size.width() + margin || physical.y() < -margin ||
			physical.y() > size.height() + margin)
		{
			continue;
		}
		// Marker centres snap to physical pixels. Constant-sized square stamps
		// keep dense markers crisp without per-vertex painter/path overhead.
		const QPoint centre(qRound(physical.x()), qRound(physical.y()));
		if (active)
		{
			selected.append(centre);
		}
		else if (shown)
		{
			visible.append(centre);
		}
		else
		{
			hidden.append(centre);
		}
	}
	auto *pixels = reinterpret_cast<QRgb *>(result.bits());
	const int stride = result.bytesPerLine() / int(sizeof(QRgb));
	const auto square = [&](QPoint centre, int width, QRgb color)
	{
		const int left = std::max(0, centre.x() - width / 2), top = std::max(0, centre.y() - width / 2);
		const int right = std::min(size.width(), centre.x() - width / 2 + width);
		const int bottom = std::min(size.height(), centre.y() - width / 2 + width);
		if (left >= right || top >= bottom)
		{
			return;
		}
		const int alpha = qAlpha(color);
		for (int y = top; y < bottom; ++y)
		{
			auto *row = pixels + y * stride;
			if (alpha == 255)
			{
				std::fill(row + left, row + right, color);
			}
			else if (alpha > 0)
			{
				for (int x = left; x < right; ++x)
				{
					const auto background = row[x];
					const auto blend = [&](int foreground, int behind) { return foreground + (behind * (255 - alpha) + 127) / 255; };
					row[x] = qRgba(blend(qRed(color), qRed(background)), blend(qGreen(color), qGreen(background)),
								   blend(qBlue(color), qBlue(background)), blend(alpha, qAlpha(background)));
				}
			}
		}
	};
	const auto scaled = [pixelRatio](double width) { return std::max(1, qRound(width * pixelRatio)); };
	for (const auto &[width, color] : {qMakePair(scaled(6), qRgb(0, 0, 0)), qMakePair(scaled(3), qRgb(255, 255, 255))})
	{
		for (const auto point : visible)
		{
			if (stopped(cancelled))
			{
				return false;
			}
			square(point, width, color);
		}
	}
	const int offset = scaled(2), dot = scaled(1);
	for (const auto point : hidden)
	{
		if (stopped(cancelled))
		{
			return false;
		}
		for (const int step : {-offset, 0, offset})
		{
			square(point + QPoint(step, -offset), dot, qRgb(255, 255, 255));
			square(point + QPoint(step, offset), dot, qRgb(255, 255, 255));
		}
		for (const int side : {-offset, offset})
		{
			square(point + QPoint(side, 0), dot, qRgb(255, 255, 255));
		}
	}
	// All outlines precede all centres so overlapping dense selections retain
	// their colour instead of later outlines painting earlier centres black.
	for (const auto &[width, color] : {qMakePair(scaled(8), qRgb(0, 0, 0)), qMakePair(scaled(4), qPremultiply(accent.rgba()))})
	{
		for (const auto point : selected)
		{
			if (stopped(cancelled))
			{
				return false;
			}
			square(point, width, color);
		}
	}
	if (stopped(cancelled))
	{
		return false;
	}
	*image = std::move(result);
	return true;
}
} // namespace vibestudio
