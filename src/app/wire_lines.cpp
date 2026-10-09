#include "app/wire_lines.h"

#include "app/viewport_image.h"

#include <QRect>

#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{

// A cached tile is proven to contain this pass's opaque stroke colour at every
// pixel. Subsequent strokes of that same colour cannot change it, regardless of
// coverage or dash phase. Unknown/partial tiles always use normal rasterization.
class OpaqueWireTiles
{
  public:
	OpaqueWireTiles(uchar *bits, qsizetype stride, QSize size, bool enabled)
		: m_bits(bits), m_stride(stride), m_size(size), m_columns((size.width() + 15) / 16),
		  m_full(enabled ? m_columns * ((size.height() + 15) / 16) : 0, 0)
	{
	}
	bool enabled() const
	{
		return !m_full.isEmpty();
	}
	bool covers(const QRect &box, QRgb color)
	{
		for (int ty = box.top() / 16; ty <= box.bottom() / 16; ++ty)
		{
			for (int tx = box.left() / 16; tx <= box.right() / 16; ++tx)
			{
				auto &full = m_full[ty * m_columns + tx];
				if (full)
				{
					continue;
				}
				const int right = std::min((tx + 1) * 16, m_size.width()), bottom = std::min((ty + 1) * 16, m_size.height());
				for (int y = ty * 16; y < bottom; ++y)
				{
					const auto *row = reinterpret_cast<const QRgb *>(m_bits + y * m_stride);
					for (int x = tx * 16; x < right; ++x)
					{
						if (row[x] != color)
						{
							return false;
						}
					}
				}
				full = 1;
			}
		}
		return true;
	}

  private:
	uchar *m_bits;
	qsizetype m_stride;
	QSize m_size;
	int m_columns;
	QVector<quint8> m_full;
};

} // namespace

bool paintWireLines(QImage *output, const QVector<WireSegment> &segments, const WireStyle &style, const std::atomic_bool *cancelled,
					const std::function<bool()> &cancelledCallback)
{
	if (!output || output->isNull() || output->format() != QImage::Format_ARGB32_Premultiplied)
	{
		return false;
	}
	auto &image = *output;
	const auto size = image.size();
	const auto stopped = [&] {
		return (cancelled && cancelled->load(std::memory_order_relaxed)) || (cancelledCallback && cancelledCallback());
	};
	const qint64 pixels = qint64(size.width()) * size.height();
	if (stopped() || size.isEmpty() || pixels > viewportImageMaxPixels || !std::isfinite(style.pixelRatio) || style.pixelRatio <= 0 ||
		!std::isfinite(style.width) || style.width <= 0 || style.width > 64 || !std::isfinite(style.selectionWidth) ||
		style.selectionWidth <= 0 || style.selectionWidth > 64 ||
		!std::isfinite(style.pixelRatio * std::max(style.width, style.selectionWidth)) ||
		style.pixelRatio * std::min(style.width, style.selectionWidth) <= 0 || !std::isfinite(style.dashLength) || style.dashLength < 0 ||
		style.dashLength > 64 || !std::isfinite(style.dashGap) || style.dashGap <= 0 || style.dashGap > 64 ||
		!std::isfinite(style.selectionWidth * style.pixelRatio * (style.dashLength + style.dashGap)))
	{
		return false;
	}
	auto *bits = image.bits();
	const qsizetype stride = image.bytesPerLine();
	for (bool selected : {false, true})
	{
		const double width = (selected ? style.selectionWidth : style.width) * style.pixelRatio;
		const double radius = width * .5;
		const QRgb color = qPremultiply(selected ? style.selection : style.wire);
		const int red = qRed(color), green = qGreen(color), blue = qBlue(color), alpha = qAlpha(color);
		OpaqueWireTiles opaque(bits, stride, size, alpha == 255 && segments.size() >= 1024);
		for (const auto &segment : segments)
		{
			if (stopped())
			{
				return false;
			}
			if (segment.selected != selected)
			{
				continue;
			}
			auto a = segment.a * style.pixelRatio, b = segment.b * style.pixelRatio;
			if (!std::isfinite(a.x()) || !std::isfinite(a.y()) || !std::isfinite(b.x()) || !std::isfinite(b.y()))
			{
				continue;
			}
			// Anchor dashes at the leftmost (then topmost) endpoint independently
			// of the scan axis. Near-diagonal rounding must not reverse the pattern.
			const bool dashFromB = a.x() > b.x() || (a.x() == b.x() && a.y() > b.y());
			// Scanning the major axis bounds work by line length, not its box.
			const bool vertical = std::abs(b.y() - a.y()) > std::abs(b.x() - a.x());
			if (vertical)
			{
				a = {a.y(), a.x()};
				b = {b.y(), b.x()};
			}
			const bool swapForScan = a.x() > b.x() || (a.x() == b.x() && a.y() > b.y());
			const bool reverseDash = dashFromB != swapForScan;
			if (swapForScan)
			{
				std::swap(a, b);
			}
			const double dx = b.x() - a.x(), dy = b.y() - a.y(), length = std::hypot(dx, dy);
			if (!std::isfinite(length))
			{
				continue;
			}
			const double dashLength = width * style.dashLength, dashPeriod = width * (style.dashLength + style.dashGap);
			const bool dashed = selected && style.dashLength > 0 && length > dashLength;
			const int majorSize = vertical ? size.height() : size.width(), minorSize = vertical ? size.width() : size.height();
			const double ux = length > 0 ? dx / length : 1, uy = length > 0 ? dy / length : 0;
			// Integrate the strip over a square pixel. A centre-distance ramp
			// alone changes the brightness of thin diagonal edges with phase.
			const double minorNormal = std::abs(uy), halfSupport = (ux + minorNormal) * .5;
			const double shoulder = (ux - minorNormal) * .5, inverseMajor = 1 / ux;
			const double inverseArea = minorNormal > 0 ? .5 / (ux * minorNormal) : 0;
			const double reach = radius + halfSupport;
			const double margin = reach * (ux + std::abs(uy));
			const double first = std::max(0.0, std::ceil(a.x() - margin - .5));
			const double last = std::min(double(majorSize - 1), std::floor(b.x() + margin - .5));
			if (first > last || std::min(a.y(), b.y()) - margin > minorSize - .5 || std::max(a.y(), b.y()) + margin < .5)
			{
				continue;
			}
			const double slope = dx > 0 ? dy / dx : 0;
			const double intercept = std::fma(-a.x(), slope, a.y());
			const double band = reach / ux;
			int nextTile = int(first);
			for (int major = int(first); major <= int(last); ++major)
			{
				if ((major & 127) == 0 && stopped())
				{
					return false;
				}
				if (opaque.enabled() && length > 64 && major == nextTile)
				{
					const int end = std::min(int(last), (major / 16 + 1) * 16 - 1);
					nextTile = end + 1;
					const double beginCentre = std::fma(major + .5, slope, intercept), endCentre = std::fma(end + .5, slope, intercept);
					const double lo = std::max(0.0, std::ceil(std::min(beginCentre, endCentre) - band - .5));
					const double hi = std::min(double(minorSize - 1), std::floor(std::max(beginCentre, endCentre) + band - .5));
					if (std::isfinite(beginCentre) && std::isfinite(endCentre) &&
						(lo > hi || opaque.covers(vertical ? QRect(QPoint(int(lo), major), QPoint(int(hi), end))
														   : QRect(QPoint(major, int(lo)), QPoint(end, int(hi))),
												  color)))
					{
						major = end;
						continue;
					}
				}
				const double alongMajor = major + .5 - a.x();
				const double centre = std::fma(major + .5, slope, intercept);
				if (!std::isfinite(centre))
				{
					continue;
				}
				const double low = std::max(0.0, std::ceil(centre - band - .5));
				const double high = std::min(double(minorSize - 1), std::floor(centre + band - .5));
				if (low > high)
				{
					continue;
				}
				for (int minor = int(low); minor <= int(high); ++minor)
				{
					const int x = vertical ? minor : major, y = vertical ? major : minor;
					auto &pixel = reinterpret_cast<QRgb *>(bits + y * stride)[x];
					// Any coverage of this opaque colour over itself is unchanged.
					// Dense meshes can overlap thousands of strokes at one pixel;
					// this exact test saves blending without discarding geometry.
					if (alpha == 255 && pixel == color)
					{
						continue;
					}
					const double offset = minor + .5 - centre;
					const double along = alongMajor / ux + uy * offset;
					double distance = ux * offset;
					if (distance < 0)
					{
						distance = -distance;
					}
					if (!std::isfinite(along) || !std::isfinite(distance))
					{
						continue;
					}
					const double nearBoundary = radius - distance;
					if (nearBoundary <= -halfSupport)
					{
						continue;
					}
					double coverage = 1;
					if (nearBoundary < halfSupport)
					{
						if (nearBoundary >= shoulder)
						{
							const double tail = halfSupport - nearBoundary;
							coverage = 1 - tail * tail * inverseArea;
						}
						else if (nearBoundary <= -shoulder)
						{
							const double tail = halfSupport + nearBoundary;
							coverage = tail * tail * inverseArea;
						}
						else
						{
							coverage = .5 + nearBoundary * inverseMajor;
						}
					}
					const double farBoundary = radius + distance;
					if (farBoundary < halfSupport)
					{
						if (farBoundary >= shoulder)
						{
							const double tail = halfSupport - farBoundary;
							coverage -= tail * tail * inverseArea;
						}
						else
						{
							coverage -= .5 - farBoundary * inverseMajor;
						}
					}
					if (reach + along < coverage)
					{
						coverage = reach + along;
					}
					if (reach + length - along < coverage)
					{
						coverage = reach + length - along;
					}
					if (dashed && coverage > 0)
					{
						const double phase = std::fmod(std::clamp(reverseDash ? length - along : along, 0.0, length), dashPeriod);
						if (phase > dashLength)
						{
							coverage = std::min(coverage, reach - std::min(phase - dashLength, dashPeriod - phase));
						}
					}
					if (coverage <= 0)
					{
						continue;
					}
					const int factor = int(coverage * 255 + .5);
					if (factor == 255 && alpha == 255)
					{
						pixel = color;
						continue;
					}
					const unsigned sourceAlpha = (alpha * factor + 127) / 255, remaining = 255 - sourceAlpha;
					// Packed premultiplied channels keep the hot loop independent of
					// painter/path allocation and preserve translucent line colours.
					const unsigned r = (red * factor + 127) / 255 + (((pixel >> 16) & 255) * remaining + 127) / 255;
					const unsigned g = (green * factor + 127) / 255 + (((pixel >> 8) & 255) * remaining + 127) / 255;
					const unsigned b = (blue * factor + 127) / 255 + ((pixel & 255) * remaining + 127) / 255;
					const unsigned a = sourceAlpha + ((pixel >> 24) * remaining + 127) / 255;
					pixel = (a << 24) | (r << 16) | (g << 8) | b;
				}
			}
		}
	}
	if (stopped())
	{
		return false;
	}
	return true;
}

bool renderWireLines(const QSize &size, const QVector<WireSegment> &segments, const WireStyle &style, QImage *output,
					 const std::atomic_bool *cancelled)
{
	if (!output)
	{
		return false;
	}
	*output = {};
	const auto stopped = [&] { return cancelled && cancelled->load(std::memory_order_relaxed); };
	const qint64 pixels = qint64(size.width()) * size.height();
	if (stopped() || size.isEmpty() || pixels > viewportImageMaxPixels || !std::isfinite(style.pixelRatio) || style.pixelRatio <= 0 ||
		!std::isfinite(style.width) || style.width <= 0 || style.width > 64 || !std::isfinite(style.selectionWidth) ||
		style.selectionWidth <= 0 || style.selectionWidth > 64 ||
		!std::isfinite(style.pixelRatio * std::max(style.width, style.selectionWidth)) ||
		style.pixelRatio * std::min(style.width, style.selectionWidth) <= 0)
	{
		return false;
	}
	QImage image(size, QImage::Format_ARGB32_Premultiplied);
	if (image.isNull())
	{
		return false;
	}
	image.fill(Qt::transparent);
	if (!paintWireLines(&image, segments, style, cancelled))
	{
		return false;
	}
	*output = std::move(image);
	return true;
}

} // namespace vibestudio
