#include "core/texture_paint.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {
namespace {
int wrapped(int value, int size) { const int remainder = value % size; return remainder < 0 ? remainder + size : remainder; }

QRgb sourceOver(QRgb source, QRgb destination)
{
	const int alpha = qAlpha(source), inverse = 255 - alpha;
	if (alpha == 0) { return destination; }
	if (alpha == 255 || qAlpha(destination) == 0) { return source; }
	const int denominator = alpha * 255 + qAlpha(destination) * inverse;
	const auto channel = [&](int foreground, int background) {
		return (foreground * alpha * 255 + background * qAlpha(destination) * inverse + denominator / 2) / denominator;
	};
	return qRgba(channel(qRed(source), qRed(destination)), channel(qGreen(source), qGreen(destination)),
		channel(qBlue(source), qBlue(destination)), (denominator + 127) / 255);
}

void paintSpan(QImage* pixels, const QImage& source, int y, int left, int right, QRgb color, const TextureBrush& brush, QRect clip)
{
	if (brush.wrap) { y = wrapped(y, pixels->height()); }
	if (y < clip.top() || y > clip.bottom() || right < left) { return; }
	const auto write = [&](int first, int last) {
		first = std::max(first, clip.left()); last = std::min(last, clip.right());
		if (first > last) { return; }
		auto* row = reinterpret_cast<QRgb*>(pixels->scanLine(y));
		if (brush.mode == TexturePaintMode::Replace) { std::fill(row + first, row + last + 1, color); }
		else { for (int x = first; x <= last; ++x) { row[x] = sourceOver(color, source.pixel(x, y)); } }
	};
	if (!brush.wrap) { write(left, right); return; }
	const int length = right - left + 1;
	if (length >= pixels->width()) { write(0, pixels->width() - 1); return; }
	left = wrapped(left, pixels->width()); right = left + length - 1;
	write(left, std::min(right, pixels->width() - 1));
	if (right >= pixels->width()) { write(0, right - pixels->width()); }
}

bool validSurface(const QImage* pixels, const QImage& source, QRect clip, const TextureBrush& brush)
{
	return pixels && !pixels->isNull() && pixels->format() == QImage::Format_ARGB32 && !source.isNull() &&
		pixels->size() == source.size() && !clip.isEmpty() && pixels->rect().contains(clip) && validTextureBrush(brush);
}

bool report(const TextureProgress& progress, qint64 done, qint64 total) { return !progress || progress(done, total); }
}

QString textureBrushShapeId(TextureBrushShape shape)
{
	switch (shape) { case TextureBrushShape::Square: return QStringLiteral("square"); case TextureBrushShape::Round: return QStringLiteral("round"); }
	return {};
}
QString texturePaintModeId(TexturePaintMode mode)
{
	switch (mode) { case TexturePaintMode::Replace: return QStringLiteral("replace"); case TexturePaintMode::SourceOver: return QStringLiteral("source-over"); }
	return {};
}
bool textureBrushShapeFromId(const QString& id, TextureBrushShape* shape)
{
	for (auto value : {TextureBrushShape::Square, TextureBrushShape::Round}) {
		if (textureBrushShapeId(value) == id) { if (shape) { *shape = value; } return true; }
	}
	return false;
}
bool texturePaintModeFromId(const QString& id, TexturePaintMode* mode)
{
	for (auto value : {TexturePaintMode::Replace, TexturePaintMode::SourceOver}) {
		if (texturePaintModeId(value) == id) { if (mode) { *mode = value; } return true; }
	}
	return false;
}
bool validTextureBrush(const TextureBrush& brush)
{
	return brush.width >= 1 && brush.width <= 128 && !textureBrushShapeId(brush.shape).isEmpty() && !texturePaintModeId(brush.mode).isEmpty();
}
bool validTexturePaintPoint(QPoint point, QSize size, bool wrap)
{
	if (size.isEmpty() || size.width() > 4096 || size.height() > 4096) { return false; }
	return wrap ? QRect(-size.width(), -size.height(), size.width() * 3, size.height() * 3).contains(point) : QRect(QPoint(), size).contains(point);
}

QRect texturePixelBounds(QPoint first, QPoint last)
{
	// Sort pixel endpoints before constructing an inclusive QRect. Normalizing
	// a negative-width QRect adjusts its edges and can omit the endpoint texels.
	return QRect(QPoint(std::min(first.x(), last.x()), std::min(first.y(), last.y())),
		QPoint(std::max(first.x(), last.x()), std::max(first.y(), last.y())));
}

bool paintTextureSegment(QImage* pixels, const QImage& source, QPoint from, QPoint to,
	QRgb color, const TextureBrush& brush, QRect clip, const TextureProgress& progress)
{
	if (!validSurface(pixels, source, clip, brush) || !validTexturePaintPoint(from, pixels->size(), brush.wrap) ||
		!validTexturePaintPoint(to, pixels->size(), brush.wrap)) { return false; }
	const int bias = (brush.width - 1) / 2;
	const int firstRow = std::min(from.y(), to.y()) - bias;
	// The union of adjacent hard stamps along one monotone raster segment is
	// contiguous on each unwrapped row. Accumulate spans before touching pixels:
	// work scales with segment length * width, not length * width squared.
	struct Span { int left = std::numeric_limits<int>::max(), right = std::numeric_limits<int>::min(); };
	QVector<Span> spans(std::abs(to.y() - from.y()) + brush.width);
	QVector<Span> stamp(brush.width);
	for (int row = 0; row < brush.width; ++row) {
		int inset = 0;
		if (brush.shape == TextureBrushShape::Round) {
			const int dy = 2 * row - (brush.width - 1);
			while (inset < brush.width / 2) {
				const int dx = 2 * inset - (brush.width - 1);
				if (dx * dx + dy * dy <= brush.width * brush.width) { break; }
				++inset;
			}
		}
		stamp[row] = {inset - bias, brush.width - 1 - inset - bias};
	}
	int x = from.x(), y = from.y();
	const int dx = std::abs(to.x() - x), dy = -std::abs(to.y() - y);
	const int sx = x < to.x() ? 1 : -1, sy = y < to.y() ? 1 : -1;
	int distance = dx + dy, steps = 0;
	const int totalSteps = std::max(dx, -dy) + 1;
	const qint64 total = totalSteps + spans.size();
	while (true) {
		if ((steps++ % 64) == 0 && !report(progress, steps - 1, total)) { return false; }
		for (int row = 0; row < brush.width; ++row) {
			auto& span = spans[y - bias + row - firstRow];
			span.left = std::min(span.left, x + stamp[row].left);
			span.right = std::max(span.right, x + stamp[row].right);
		}
		if (x == to.x() && y == to.y()) { break; }
		const int twice = 2 * distance;
		if (twice >= dy) { distance += dy; x += sx; }
		if (twice <= dx) { distance += dx; y += sy; }
	}
	for (int row = 0; row < spans.size(); ++row) {
		if (row % 32 == 0 && !report(progress, totalSteps + row, total)) { return false; }
		paintSpan(pixels, source, firstRow + row, spans[row].left, spans[row].right, color, brush, clip);
	}
	return report(progress, total, total);
}

bool paintTextureShape(QImage* pixels, const QImage& source, TextureShape shape, QPoint from, QPoint to,
	QRgb color, const TextureBrush& brush, bool filled, QRect clip, const TextureProgress& progress)
{
	if (shape == TextureShape::Line) { return paintTextureSegment(pixels, source, from, to, color, brush, clip, progress); }
	if (!validSurface(pixels, source, clip, brush) || !validTexturePaintPoint(from, pixels->size(), brush.wrap) ||
		!validTexturePaintPoint(to, pixels->size(), brush.wrap) || (shape != TextureShape::Rectangle && shape != TextureShape::Ellipse)) { return false; }
	const QRect bounds = texturePixelBounds(from, to);
	const qint64 w = bounds.width(), h = bounds.height();
	const qint64 innerW = w - 2 * brush.width, innerH = h - 2 * brush.width;
	for (int row = 0; row < h; ++row) {
		if (row % 16 == 0 && !report(progress, row, h)) { return false; }
		const qint64 dy = 2 * row - (h - 1);
		int run = -1;
		for (int column = 0; column < w; ++column) {
			bool inside = false;
			if (shape == TextureShape::Rectangle) {
				inside = filled || row < brush.width || row >= h - brush.width || column < brush.width || column >= w - brush.width;
			} else {
				const qint64 dx = 2 * column - (w - 1);
				const bool outer = dx * dx * h * h + dy * dy * w * w <= w * w * h * h;
				const bool inner = innerW > 0 && innerH > 0 && dx * dx * innerH * innerH + dy * dy * innerW * innerW <= innerW * innerW * innerH * innerH;
				inside = outer && (filled || !inner);
			}
			if (inside && run < 0) { run = column; }
			if (!inside && run >= 0) { paintSpan(pixels, source, bounds.top() + row, bounds.left() + run, bounds.left() + column - 1, color, brush, clip); run = -1; }
		}
		if (run >= 0) { paintSpan(pixels, source, bounds.top() + row, bounds.left() + run, bounds.right(), color, brush, clip); }
	}
	return report(progress, h, h);
}

} // namespace vibestudio
