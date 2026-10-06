#include "core/texture_transform.h"

#include <QCoreApplication>

#include <algorithm>
#include <cstring>

namespace vibestudio {
namespace {
bool report(const TextureProgress& progress, qint64 done, qint64 total, QString* error)
{
	if (!progress || progress(done, total)) { return true; }
	if (error) { *error = QCoreApplication::translate("VibeStudioTexture", "Texture operation cancelled."); }
	return false;
}
QImage allocationFailure(QString* error)
{
	if (error) { *error = QCoreApplication::translate("VibeStudioTexture", "Unable to allocate transformed pixels."); }
	return {};
}
bool validImage(const QImage& image)
{
	return !image.isNull() && image.width() <= 4096 && image.height() <= 4096 && qint64(image.width()) * image.height() <= 4 * 1024 * 1024 &&
		(image.format() == QImage::Format_Indexed8 || image.format() == QImage::Format_ARGB32 || image.format() == QImage::Format_RGB32);
}

// Prefer an existing transparent palette entry, then a spare slot. A full opaque
// palette or RGB32 image needs RGBA storage when exposed pixels must be cleared.
QImage transparentSurface(QImage source, int* transparentIndex)
{
	*transparentIndex = 0;
	if (source.format() == QImage::Format_Indexed8) {
		auto colors = source.colorTable();
		for (int i = 0; i < colors.size(); ++i) { if (qAlpha(colors[i]) == 0) { *transparentIndex = i; return source; } }
		if (colors.size() < 256) { *transparentIndex = int(colors.size()); colors << qRgba(0, 0, 0, 0); source.setColorTable(colors); return source; }
	}
	return source.convertToFormat(QImage::Format_ARGB32);
}

template<typename Pixel>
bool nearest(const QImage& source, QImage* destination, QString* error, const TextureProgress& progress)
{
	for (int y = 0; y < destination->height(); ++y) {
		if (y % 16 == 0 && !report(progress, y, destination->height(), error)) { return false; }
		const int sourceY = int(qint64(2 * y + 1) * source.height() / (2 * destination->height()));
		const auto* input = reinterpret_cast<const Pixel*>(source.constScanLine(sourceY));
		auto* output = reinterpret_cast<Pixel*>(destination->scanLine(y));
		for (int x = 0; x < destination->width(); ++x) {
			output[x] = input[qint64(2 * x + 1) * source.width() / (2 * destination->width())];
		}
	}
	return report(progress, destination->height(), destination->height(), error);
}

template<typename Pixel>
bool quarterTurn(const QImage& source, QImage* destination, QString* error, const TextureProgress& progress)
{
	const auto* input = reinterpret_cast<const Pixel*>(source.constBits());
	auto* output = reinterpret_cast<Pixel*>(destination->bits());
	const qsizetype inputStride = source.bytesPerLine() / sizeof(Pixel), outputStride = destination->bytesPerLine() / sizeof(Pixel);
	// Small tiles keep column writes local and bound cooperative cancellation.
	for (int tileY = 0; tileY < source.height(); tileY += 32) {
		if (!report(progress, tileY, source.height(), error)) { return false; }
		for (int tileX = 0; tileX < source.width(); tileX += 32) {
			for (int y = tileY; y < std::min(tileY + 32, source.height()); ++y) {
				for (int x = tileX; x < std::min(tileX + 32, source.width()); ++x) {
					output[x * outputStride + source.height() - 1 - y] = input[y * inputStride + x];
				}
			}
		}
	}
	return report(progress, source.height(), source.height(), error);
}
}

QString textureAnchorId(TextureAnchor anchor)
{
	switch (anchor) {
	case TextureAnchor::TopLeft: return QStringLiteral("top-left");
	case TextureAnchor::Top: return QStringLiteral("top");
	case TextureAnchor::TopRight: return QStringLiteral("top-right");
	case TextureAnchor::Left: return QStringLiteral("left");
	case TextureAnchor::Center: return QStringLiteral("center");
	case TextureAnchor::Right: return QStringLiteral("right");
	case TextureAnchor::BottomLeft: return QStringLiteral("bottom-left");
	case TextureAnchor::Bottom: return QStringLiteral("bottom");
	case TextureAnchor::BottomRight: return QStringLiteral("bottom-right");
	}
	return {};
}
bool textureAnchorFromId(const QString& id, TextureAnchor* anchor)
{
	for (int index = 0; index < 9; ++index) {
		const auto value = static_cast<TextureAnchor>(index);
		if (textureAnchorId(value) == id) { if (anchor) { *anchor = value; } return true; }
	}
	return false;
}
QPoint textureAnchorOffset(QSize content, QSize container, TextureAnchor anchor)
{
	const int index = int(anchor);
	const auto offset = [](int difference, int position) { return position == 0 ? 0 : (position == 2 ? difference : (difference >= 0 ? difference / 2 : (difference - 1) / 2)); };
	return {offset(container.width() - content.width(), index % 3), offset(container.height() - content.height(), index / 3)};
}

QImage resizeTexturePixels(const QImage& source, QSize size, bool smooth, QString* error, const TextureProgress& progress)
{
	if (!validImage(source) || size.isEmpty() || size.width() > 4096 || size.height() > 4096 || qint64(size.width()) * size.height() > 4 * 1024 * 1024) { return allocationFailure(error); }
	if (!report(progress, 0, size.height(), error)) { return {}; }
	if (source.size() == size) { return report(progress, 1, 1, error) ? source : QImage(); }
	if (smooth) {
		QImage next = source.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		if (!next.isNull() && !validImage(next)) { next = next.convertToFormat(QImage::Format_ARGB32); }
		if (next.isNull()) { return allocationFailure(error); }
		return report(progress, 1, 1, error) ? next : QImage();
	}
	QImage next(size, source.format());
	if (next.isNull()) { return allocationFailure(error); }
	if (source.format() == QImage::Format_Indexed8) { next.setColorTable(source.colorTable()); }
	const bool succeeded = source.format() == QImage::Format_Indexed8 ? nearest<uchar>(source, &next, error, progress) : nearest<QRgb>(source, &next, error, progress);
	return succeeded ? next : QImage();
}

QImage rotateTexturePixelsClockwise(const QImage& source, QString* error, const TextureProgress& progress)
{
	if (!validImage(source)) { return allocationFailure(error); }
	QImage next(source.size().transposed(), source.format());
	if (next.isNull()) { return allocationFailure(error); }
	if (source.format() == QImage::Format_Indexed8) { next.setColorTable(source.colorTable()); }
	const bool succeeded = source.format() == QImage::Format_Indexed8 ? quarterTurn<uchar>(source, &next, error, progress) : quarterTurn<QRgb>(source, &next, error, progress);
	return succeeded ? next : QImage();
}

QImage resizeTextureCanvas(const QImage& source, QSize size, QPoint offset, QString* error, const TextureProgress& progress)
{
	if (!validImage(source) || size.isEmpty() || size.width() > 4096 || size.height() > 4096 || qint64(size.width()) * size.height() > 4 * 1024 * 1024 ||
		qint64(offset.x()) + source.width() <= 0 || qint64(offset.y()) + source.height() <= 0 || offset.x() >= size.width() || offset.y() >= size.height()) { return allocationFailure(error); }
	if (!report(progress, 0, size.height(), error)) { return {}; }
	if (source.size() == size && offset.isNull()) { return report(progress, 1, 1, error) ? source : QImage(); }
	const QRect canvas(QPoint(), size), placed(offset, source.size());
	int transparentIndex = 0;
	QImage pixels = placed.contains(canvas) ? source : transparentSurface(source, &transparentIndex);
	if (pixels.isNull()) { return allocationFailure(error); }
	QImage next(size, pixels.format()); if (next.isNull()) { return allocationFailure(error); }
	if (pixels.format() == QImage::Format_Indexed8) { next.setColorTable(pixels.colorTable()); next.fill(transparentIndex); }
	else { next.fill(Qt::transparent); }
	const QRect visible = canvas.intersected(placed);
	const int pixelBytes = pixels.format() == QImage::Format_Indexed8 ? 1 : 4;
	for (int y = visible.top(); y <= visible.bottom(); ++y) {
		if ((y - visible.top()) % 16 == 0 && !report(progress, y - visible.top(), visible.height(), error)) { return {}; }
		std::memcpy(next.scanLine(y) + visible.left() * pixelBytes,
			pixels.constScanLine(y - offset.y()) + (visible.left() - offset.x()) * pixelBytes, size_t(visible.width() * pixelBytes));
	}
	return report(progress, 1, 1, error) ? next : QImage();
}

QImage replaceTexturePixels(const QImage& source, QRect clearArea, const QImage& replacement, QPoint position, QString* error, const TextureProgress& progress)
{
	if (!validImage(source) || clearArea.isEmpty() || !source.rect().contains(clearArea) ||
		(!replacement.isNull() && (!validImage(replacement) || position.x() < 0 || position.y() < 0 ||
		 qint64(position.x()) + replacement.width() > source.width() || qint64(position.y()) + replacement.height() > source.height()))) { return allocationFailure(error); }
	const QRect placed(position, replacement.size());
	const bool clear = replacement.isNull() || !placed.contains(clearArea);
	QImage next = source;
	if (!replacement.isNull() && (replacement.format() != source.format() || replacement.colorTable() != source.colorTable())) { next = source.convertToFormat(QImage::Format_ARGB32); }
	int transparentIndex = 0;
	if (clear) { next = transparentSurface(next, &transparentIndex); }
	if (next.isNull()) { return allocationFailure(error); }
	const QImage pixels = replacement.isNull() || next.format() == replacement.format() ? replacement : replacement.convertToFormat(next.format());
	if (!replacement.isNull() && pixels.isNull()) { return allocationFailure(error); }
	const int total = (clear ? clearArea.height() : 0) + pixels.height();
	if (!report(progress, 0, total, error)) { return {}; }
	if (clear) {
		for (int y = clearArea.top(); y <= clearArea.bottom(); ++y) {
			if ((y - clearArea.top()) % 16 == 0 && !report(progress, y - clearArea.top(), total, error)) { return {}; }
			if (next.format() == QImage::Format_Indexed8) { std::fill_n(next.scanLine(y) + clearArea.left(), clearArea.width(), uchar(transparentIndex)); }
			else { std::fill_n(reinterpret_cast<QRgb*>(next.scanLine(y)) + clearArea.left(), clearArea.width(), qRgba(0, 0, 0, 0)); }
		}
	}
	const int pixelBytes = next.format() == QImage::Format_Indexed8 ? 1 : 4;
	for (int y = 0; y < pixels.height(); ++y) {
		if (y % 16 == 0 && !report(progress, (clear ? clearArea.height() : 0) + y, total, error)) { return {}; }
		std::memcpy(next.scanLine(position.y() + y) + position.x() * pixelBytes, pixels.constScanLine(y), size_t(pixels.width() * pixelBytes));
	}
	return report(progress, total, total, error) ? next : QImage();
}

} // namespace vibestudio
