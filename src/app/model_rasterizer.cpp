#include "app/model_rasterizer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{

struct Edge
{
	double a = 0, b = 0, c = 0, length = 0;
	bool inclusive = false;
	double at(double x, double y) const { return a * x + b * y + c; }
};

struct Triangle
{
	ModelRasterTriangle input;
	std::array<Edge, 3> edge;
	double area = 0;
	double inverseArea = 0;
	QRect bounds;
	const uchar *texels = nullptr;
	qsizetype textureStride = 0;
	int textureWidth = 0, textureHeight = 0;
};

struct Sample
{
	double weight[3]{};
	double depth = 0;
};

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

bool prepare(const ModelRasterTriangle &input, const QRect &bounds, Triangle *output)
{
	output->input = input;
	if (!std::isfinite(input.light))
	{
		return false;
	}
	if (input.texture && input.texture->format() == QImage::Format_ARGB32_Premultiplied && !input.texture->isNull())
	{
		output->texels = input.texture->constBits();
		output->textureStride = input.texture->bytesPerLine();
		output->textureWidth = input.texture->width();
		output->textureHeight = input.texture->height();
	}
	auto &v = output->input.vertices;
	for (const auto &vertex : v)
	{
		if (!std::isfinite(vertex.screen.x()) || !std::isfinite(vertex.screen.y()) || !std::isfinite(vertex.depth) ||
			!std::isfinite(vertex.reciprocalW) || vertex.reciprocalW <= 0 || !std::isfinite(vertex.uv.x()) || !std::isfinite(vertex.uv.y()))
		{
			return false;
		}
	}
	const auto a = v[1].screen - v[0].screen, b = v[2].screen - v[0].screen;
	output->area = a.x() * b.y() - a.y() * b.x();
	if (!std::isfinite(output->area) || std::abs(output->area) <= 1e-12)
	{
		return false;
	}
	if (output->area < 0)
	{
		std::swap(v[1], v[2]);
		std::swap(output->input.edges[1], output->input.edges[2]);
		std::swap(output->input.selectedEdges[1], output->input.selectedEdges[2]);
		output->area = -output->area;
	}
	output->inverseArea = 1.0 / output->area;
	for (int i = 0; i < 3; ++i)
	{
		const auto &start = v[(i + 1) % 3].screen;
		const auto &end = v[(i + 2) % 3].screen;
		// Construct both directions of a shared edge from the same endpoint.
		// Computing the constant from opposite endpoints rounds differently at
		// fractional screen positions, leaving cracks or double-alpha seams.
		const bool forward = start.x() < end.x() || (start.x() == end.x() && start.y() < end.y());
		const auto &origin = forward ? start : end;
		const auto &tip = forward ? end : start;
		const double dx = tip.x() - origin.x(), dy = tip.y() - origin.y();
		const double direction = forward ? 1.0 : -1.0;
		output->edge[i] = {-dy * direction, dx * direction, (dy * origin.x() - dx * origin.y()) * direction, std::hypot(dx, dy),
						   dy * direction < 0 || (dy == 0 && dx * direction > 0)};
	}
	if (bounds.isEmpty())
	{
		return true;
	}
	// Clamp in floating point before converting: near-plane projections can
	// extend far beyond an integer viewport even for valid model coordinates.
	const double left = std::max(double(bounds.left()), std::ceil(std::min({v[0].screen.x(), v[1].screen.x(), v[2].screen.x()}) - 0.5));
	const double right = std::min(double(bounds.right()), std::floor(std::max({v[0].screen.x(), v[1].screen.x(), v[2].screen.x()}) - 0.5));
	const double top = std::max(double(bounds.top()), std::ceil(std::min({v[0].screen.y(), v[1].screen.y(), v[2].screen.y()}) - 0.5));
	const double bottom =
		std::min(double(bounds.bottom()), std::floor(std::max({v[0].screen.y(), v[1].screen.y(), v[2].screen.y()}) - 0.5));
	if (left > right || top > bottom)
	{
		return false;
	}
	output->bounds = QRect(QPoint(int(left), int(top)), QPoint(int(right), int(bottom)));
	return true;
}

bool sample(const Triangle &triangle, double x, double y, Sample *result)
{
	result->depth = 0;
	for (int i = 0; i < 3; ++i)
	{
		const auto &edge = triangle.edge[i];
		const double value = edge.at(x, y);
		// The same half-open edge rule on both windings gives exactly one owner
		// at shared edges, including the diagonal introduced by near clipping.
		if (value < 0 || (value == 0 && !edge.inclusive))
		{
			return false;
		}
		result->weight[i] = value * triangle.inverseArea;
		result->depth += result->weight[i] * triangle.input.vertices[i].depth;
	}
	return std::isfinite(result->depth);
}

QRgb sampleTexture(const Triangle &triangle, const Sample &sample)
{
	if (!triangle.texels)
	{
		return triangle.input.color;
	}
	double w = 0, u = 0, v = 0;
	for (int i = 0; i < 3; ++i)
	{
		const auto &vertex = triangle.input.vertices[i];
		const double factor = sample.weight[i] * vertex.reciprocalW;
		w += factor;
		u += factor * vertex.uv.x();
		v += factor * vertex.uv.y();
	}
	if (!std::isfinite(w) || w <= 0 || !std::isfinite(u) || !std::isfinite(v))
	{
		return 0;
	}
	u /= w;
	v /= w;
	if (!std::isfinite(u) || !std::isfinite(v))
	{
		return 0;
	}
	// Repeat in normalized space before multiplying to avoid integer overflow
	// on deliberately large UVs. Bilinear interpolation is premultiplied, so a
	// transparent texel never leaks its hidden RGB into the visible border.
	const int width = triangle.textureWidth, height = triangle.textureHeight;
	if (u < 0 || u >= 1)
	{
		u -= std::floor(u);
	}
	if (v < 0 || v >= 1)
	{
		v -= std::floor(v);
	}
	const double x = u * width - 0.5, y = v * height - 0.5;
	const int ix = x < 0 ? -1 : int(x), iy = y < 0 ? -1 : int(y);
	const int x0 = ix < 0 ? width - 1 : ix, y0 = iy < 0 ? height - 1 : iy;
	const int x1 = x0 + 1 == width ? 0 : x0 + 1, y1 = y0 + 1 == height ? 0 : y0 + 1;
	const auto *row0 = reinterpret_cast<const QRgb *>(triangle.texels + y0 * triangle.textureStride);
	const auto *row1 = reinterpret_cast<const QRgb *>(triangle.texels + y1 * triangle.textureStride);
	const double tx = x - ix, ty = y - iy;
	const double weights[] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
	const QRgb pixels[] = {row0[x0], row0[x1], row1[x0], row1[x1]};
	double r = 0, g = 0, b = 0, alpha = 0;
	for (int i = 0; i < 4; ++i)
	{
		r += qRed(pixels[i]) * weights[i];
		g += qGreen(pixels[i]) * weights[i];
		b += qBlue(pixels[i]) * weights[i];
		alpha += qAlpha(pixels[i]) * weights[i];
	}
	return qRgba(int(r + 0.5), int(g + 0.5), int(b + 0.5), int(alpha + 0.5));
}

QRgb tint(QRgb base, QRgb overlay)
{
	const int factor = qAlpha(overlay), alpha = qAlpha(base);
	const auto channel = [factor, alpha](int a, int b) { return (a * (255 - factor) + (b * alpha * factor + 127) / 255 + 127) / 255; };
	return qRgba(channel(qRed(base), qRed(overlay)), channel(qGreen(base), qGreen(overlay)), channel(qBlue(base), qBlue(overlay)), alpha);
}

QRgb colorAt(const Triangle &triangle, const Sample &sample, double x, double y, const ModelRasterStyle &style)
{
	QRgb color = sampleTexture(triangle, sample);
	if (qAlpha(color) == 0)
	{
		return 0;
	}
	if (triangle.input.texture)
	{
		const double light = std::clamp(triangle.input.light, 0.0, 1.0);
		color = qRgba(int(qRed(color) * light + 0.5), int(qGreen(color) * light + 0.5), int(qBlue(color) * light + 0.5), qAlpha(color));
	}
	if (triangle.input.highlighted && std::fmod((x + y) / style.pixelRatio, 8.0) < 1.2)
	{
		color = tint(color, style.hatch);
	}
	for (int i = 0; i < 3; ++i)
	{
		if (triangle.input.selectedEdges[i] && sample.weight[i] * triangle.area <= 2.8 * style.pixelRatio * triangle.edge[i].length &&
			std::fmod((x + y) / style.pixelRatio, 9.0) < 6.0)
		{
			return tint(color, style.selection);
		}
	}
	if (style.showEdges || triangle.input.hovered)
	{
		const double width = (triangle.input.hovered ? 1.8 : 0.9) * style.pixelRatio;
		for (int i = 0; i < 3; ++i)
		{
			if (triangle.input.edges[i] && sample.weight[i] * triangle.area <= width * triangle.edge[i].length)
			{
				return tint(color, triangle.input.hovered ? style.hover : style.edge);
			}
		}
	}
	return color;
}

QRgb over(QRgb source, QRgb destination)
{
	const int remaining = 255 - qAlpha(source);
	return qRgba(qRed(source) + (qRed(destination) * remaining + 127) / 255, qGreen(source) + (qGreen(destination) * remaining + 127) / 255,
				 qBlue(source) + (qBlue(destination) * remaining + 127) / 255,
				 qAlpha(source) + (qAlpha(destination) * remaining + 127) / 255);
}

bool nearer(double depth, int source, double previousDepth, int previousSource)
{
	return depth > previousDepth || (depth == previousDepth && source < previousSource);
}

} // namespace

void ModelRasterFrame::clear()
{
	image = QImage();
	depth.clear();
	source.clear();
}

bool modelTextureHasAlpha(const QImage &image)
{
	if (!image.hasAlphaChannel())
	{
		return false;
	}
	for (int y = 0; y < image.height(); ++y)
	{
		for (int x = 0; x < image.width(); ++x)
		{
			if (qAlpha(image.pixel(x, y)) != 255)
			{
				return true;
			}
		}
	}
	return false;
}

bool paintModelWireframe(QImage *output, const QVector<ModelWireSegment> &segments, const ModelWireStyle &style,
						 const std::atomic_bool *cancelled, const std::function<bool()> &cancelledCallback)
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
	if (stopped() || size.isEmpty() || pixels > modelRasterMaxPixels || !std::isfinite(style.pixelRatio) || style.pixelRatio <= 0 ||
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

bool renderModelWireframe(const QSize &size, const QVector<ModelWireSegment> &segments, const ModelWireStyle &style, QImage *output,
						  const std::atomic_bool *cancelled)
{
	if (!output)
	{
		return false;
	}
	*output = {};
	const auto stopped = [&] { return cancelled && cancelled->load(std::memory_order_relaxed); };
	const qint64 pixels = qint64(size.width()) * size.height();
	if (stopped() || size.isEmpty() || pixels > modelRasterMaxPixels || !std::isfinite(style.pixelRatio) || style.pixelRatio <= 0 ||
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
	if (!paintModelWireframe(&image, segments, style, cancelled))
	{
		return false;
	}
	*output = std::move(image);
	return true;
}

bool renderModelRaster(const QSize &size, const QVector<ModelRasterTriangle> &triangles, const ModelRasterStyle &style,
					   ModelRasterFrame *output, const std::atomic_bool *cancelled)
{
	if (!output)
	{
		return false;
	}
	const auto stopped = [&]() {
		if (cancelled && cancelled->load(std::memory_order_relaxed))
		{
			output->clear();
			return true;
		}
		return false;
	};
	if (stopped())
	{
		return false;
	}
	const qint64 count = qint64(size.width()) * size.height();
	if (size.width() <= 0 || size.height() <= 0 || count > modelRasterMaxPixels || !std::isfinite(style.pixelRatio) ||
		style.pixelRatio <= 0)
	{
		output->clear();
		return false;
	}
	if (output->image.size() != size)
	{
		output->image = QImage(size, QImage::Format_ARGB32_Premultiplied);
	}
	if (output->image.isNull())
	{
		output->clear();
		return false;
	}
	output->image.fill(Qt::transparent);
	output->depth.fill(-std::numeric_limits<double>::infinity(), count);
	output->source.fill(-1, count);
	auto *depths = output->depth.data();
	auto *sources = output->source.data();
	QVector<Triangle> prepared;
	prepared.reserve(triangles.size());
	QVector<int> transparent;
	const QRect bounds(QPoint(), size);
	for (const auto &input : triangles)
	{
		if (stopped())
		{
			return false;
		}
		Triangle triangle;
		if (input.source < 0 || !prepare(input, bounds, &triangle))
		{
			continue;
		}
		if (input.texture && input.textureHasAlpha)
		{
			transparent.append(int(prepared.size()));
		}
		prepared.append(triangle);
	}
	// Opaque fragments (including solid parts of alpha textures) settle first.
	for (const auto &triangle : prepared)
	{
		for (int y = triangle.bounds.top(); y <= triangle.bounds.bottom(); ++y)
		{
			if (stopped())
			{
				return false;
			}
			auto *pixels = reinterpret_cast<QRgb *>(output->image.scanLine(y));
			const qsizetype row = qsizetype(y) * size.width();
			for (int x = triangle.bounds.left(); x <= triangle.bounds.right(); ++x)
			{
				Sample hit;
				const qsizetype position = row + x;
				if (!sample(triangle, x + 0.5, y + 0.5, &hit) ||
					!nearer(hit.depth, triangle.input.source, depths[position], sources[position]))
				{
					continue;
				}
				const QRgb color = colorAt(triangle, hit, x + 0.5, y + 0.5, style);
				if (qAlpha(color) != 255)
				{
					continue;
				}
				pixels[x] = color;
				depths[position] = hit.depth;
				sources[position] = triangle.input.source;
			}
		}
	}
	struct Fragment
	{
		double depth;
		int source;
		QRgb color;
	};
	QVector<Fragment> fragments;
	QVector<int> band, tile;
	// Reuse tile membership and one pixel's fragments. Even a pathological
	// stack consumes O(triangles + pixels), never O(triangles * pixels) memory.
	constexpr int tileSize = 16;
	for (int top = 0; !transparent.isEmpty() && top < size.height(); top += tileSize)
	{
		const int bottom = std::min(top + tileSize - 1, size.height() - 1);
		band.clear();
		for (int index : transparent)
		{
			const auto &box = prepared.at(index).bounds;
			if (box.top() <= bottom && box.bottom() >= top)
			{
				band.append(index);
			}
		}
		if (band.isEmpty())
		{
			continue;
		}
		for (int left = 0; left < size.width(); left += tileSize)
		{
			const int right = std::min(left + tileSize - 1, size.width() - 1);
			tile.clear();
			for (int index : band)
			{
				const auto &box = prepared.at(index).bounds;
				if (box.left() <= right && box.right() >= left)
				{
					tile.append(index);
				}
			}
			if (tile.isEmpty())
			{
				continue;
			}
			for (int y = top; y <= bottom; ++y)
			{
				if (stopped())
				{
					return false;
				}
				auto *pixels = reinterpret_cast<QRgb *>(output->image.scanLine(y));
				for (int x = left; x <= right; ++x)
				{
					const qsizetype position = qsizetype(y) * size.width() + x;
					fragments.clear();
					for (int index : tile)
					{
						const auto &triangle = prepared.at(index);
						Sample hit;
						if (!triangle.bounds.contains(x, y) || !sample(triangle, x + 0.5, y + 0.5, &hit) ||
							!nearer(hit.depth, triangle.input.source, depths[position], sources[position]))
						{
							continue;
						}
						const QRgb color = colorAt(triangle, hit, x + 0.5, y + 0.5, style);
						if (qAlpha(color) > 0 && qAlpha(color) < 255)
						{
							fragments.append({hit.depth, triangle.input.source, color});
						}
					}
					std::sort(fragments.begin(), fragments.end(), [](const Fragment &a, const Fragment &b)
							  { return a.depth == b.depth ? a.source > b.source : a.depth < b.depth; });
					for (const auto &fragment : fragments)
					{
						pixels[x] = over(fragment.color, pixels[x]);
					}
					if (!fragments.isEmpty())
					{
						depths[position] = fragments.last().depth;
						sources[position] = fragments.last().source;
					}
				}
			}
		}
	}
	return true;
}

int pickModelRaster(const QPointF &point, const QVector<ModelRasterTriangle> &triangles)
{
	int source = -1;
	double depth = -std::numeric_limits<double>::infinity();
	if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
	{
		return -1;
	}
	for (const auto &input : triangles)
	{
		Triangle triangle;
		Sample hit;
		if (input.source >= 0 && prepare(input, QRect(), &triangle) && sample(triangle, point.x(), point.y(), &hit) &&
			nearer(hit.depth, input.source, depth, source) && qAlpha(sampleTexture(triangle, hit)) > 0)
		{
			source = input.source;
			depth = hit.depth;
		}
	}
	return source;
}

bool buildModelRasterPickIndex(const QSize &size, const QVector<ModelRasterTriangle> &triangles, ModelRasterPickIndex *output,
							   const std::atomic_bool *cancelled)
{
	if (!output || size.isEmpty())
	{
		return false;
	}
	ModelRasterPickIndex result;
	result.size = size;
	constexpr int side = 32, maxReferences = 2 * 1024 * 1024;
	result.cells.resize(side * side);
	int references = 0;
	for (int i = 0; i < triangles.size(); ++i)
	{
		if (cancelled && cancelled->load())
		{
			return false;
		}
		const auto &v = triangles[i].vertices;
		double left = std::numeric_limits<double>::infinity(), top = left, right = -left, bottom = -left;
		bool finite = true;
		for (const auto &vertex : v)
		{
			finite &= std::isfinite(vertex.screen.x()) && std::isfinite(vertex.screen.y());
			left = std::min(left, vertex.screen.x());
			right = std::max(right, vertex.screen.x());
			top = std::min(top, vertex.screen.y());
			bottom = std::max(bottom, vertex.screen.y());
		}
		if (!finite || right < 0 || bottom < 0 || left >= size.width() || top >= size.height())
		{
			continue;
		}
		const int x0 = int(std::clamp(left / size.width() * side, 0.0, side - 1.0));
		const int x1 = int(std::clamp(right / size.width() * side, 0.0, side - 1.0));
		const int y0 = int(std::clamp(top / size.height() * side, 0.0, side - 1.0));
		const int y1 = int(std::clamp(bottom / size.height() * side, 0.0, side - 1.0));
		const int count = (x1 - x0 + 1) * (y1 - y0 + 1);
		if (count > 16 || references + count > maxReferences)
		{
			result.broad.append(i);
			continue;
		}
		for (int y = y0; y <= y1; ++y)
		{
			for (int x = x0; x <= x1; ++x)
			{
				result.cells[y * side + x].append(i);
			}
		}
		references += count;
	}
	*output = std::move(result);
	return true;
}

int pickModelRaster(const QPointF &point, const QVector<ModelRasterTriangle> &triangles, const ModelRasterPickIndex &index)
{
	constexpr int side = 32;
	if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || index.size.isEmpty() || index.cells.size() != side * side ||
		point.x() < 0 || point.y() < 0 || point.x() >= index.size.width() || point.y() >= index.size.height())
	{
		return -1;
	}
	int source = -1;
	double depth = -std::numeric_limits<double>::infinity();
	const auto sampleCandidates = [&](const QVector<int> &candidates)
	{
		for (int i : candidates)
		{
			if (i < 0 || i >= triangles.size())
			{
				continue;
			}
			const auto &input = triangles[i];
			Triangle triangle;
			Sample hit;
			if (input.source >= 0 && prepare(input, QRect(), &triangle) && sample(triangle, point.x(), point.y(), &hit) &&
				nearer(hit.depth, input.source, depth, source) && qAlpha(sampleTexture(triangle, hit)) > 0)
			{
				source = input.source;
				depth = hit.depth;
			}
		}
	};
	sampleCandidates(index.broad);
	const int x = int(point.x() / index.size.width() * side), y = int(point.y() / index.size.height() * side);
	sampleCandidates(index.cells[y * side + x]);
	return source;
}

} // namespace vibestudio
