// SPDX-License-Identifier: GPL-3.0-only
// FTX/SWL layouts and DDS writer derived from PakFu src/formats/{ftx_image,
// swl_image,image_writer}.cpp, working snapshot 2026-10-05 based on
// revision 13111e4c07513548a29fd7eb74fc9d004c44aa59.
// https://github.com/themuffinator/PakFu/tree/13111e4c07513548a29fd7eb74fc9d004c44aa59/src/formats
#include "core/extra_image.h"

#include <QCoreApplication>
#include <QtEndian>
#include <algorithm>
#include <cstring>

namespace vibestudio {
namespace {
quint32 u32(const QByteArray& bytes, qsizetype at) { return qFromLittleEndian<quint32>(bytes.constData() + at); }
void put32(QByteArray& bytes, qsizetype at, quint32 n) { qToLittleEndian(n, bytes.data() + at); }
QString message(const char* text) { return QCoreApplication::translate("VibeStudioExtraImage", text); }
bool cancelled(const IdTechImageDecodeContext& context) { return context.isCancelled && context.isCancelled(); }
bool dimensions(quint32 w, quint32 h, qint64 retained, const IdTechImageDecodeContext& context)
{
	return context.maximumDimension > 0 && context.maximumImagePixels > 0 && context.maximumTotalPixels > 0 &&
		w && h && w <= quint32(context.maximumDimension) && h <= quint32(context.maximumDimension) &&
		quint64(w) * h <= quint64(context.maximumImagePixels) && retained + qint64(w) * h <= context.maximumTotalPixels;
}
}

IdTechImageDecodeResult decodeExtraImage(IdTechImageFormat format, const QByteArray& bytes, const IdTechImageDecodeContext& context)
{
	IdTechImageDecodeResult result;
	result.format = format; result.formatId = idTechImageFormatId(format); result.formatName = idTechImageFormatDisplayName(format);
	const auto fail = [&](const char* reason) {
		result.decoded = false; result.image = {}; result.mipLevels.clear(); result.error = message(reason); return result;
	};
	if (bytes.size() > 64 * 1024 * 1024 || cancelled(context)) { return fail(QT_TRANSLATE_NOOP("VibeStudioExtraImage", "Image import exceeds its byte limit or was cancelled.")); }
	if (format == IdTechImageFormat::Dds) {
		result.image = decodeDdsImage(bytes, context, &result.error);
		if (bytes.size() >= 128 && u32(bytes, 28) > 1) {
			result.warnings << message(QT_TRANSLATE_NOOP("VibeStudioExtraImage", "DDS import uses the base mip only; lower mip levels are not retained."));
		}
	} else if (format == IdTechImageFormat::Ftx) {
		if (bytes.size() < 12) { return fail(QT_TRANSLATE_NOOP("VibeStudioExtraImage", "The FTX header is truncated.")); }
		const auto w = u32(bytes, 0), h = u32(bytes, 4), alpha = u32(bytes, 8);
		if (!dimensions(w, h, 0, context) || alpha > 1 || 12 + quint64(w) * h * 4 != quint64(bytes.size())) {
			return fail(QT_TRANSLATE_NOOP("VibeStudioExtraImage", "Invalid FTX dimensions, alpha flag, or pixel payload."));
		}
		QImage image(int(w), int(h), QImage::Format_RGBA8888);
		if (image.isNull()) { return fail(QT_TRANSLATE_NOOP("VibeStudioExtraImage", "Unable to allocate image pixels.")); }
		for (int y = 0; y < int(h); ++y) {
			if (cancelled(context)) { return fail(QT_TRANSLATE_NOOP("VibeStudioExtraImage", "Image decoding cancelled.")); }
			auto* row = image.scanLine(y);
			std::memcpy(row, bytes.constData() + 12 + qint64(y) * w * 4, size_t(w) * 4);
			if (!alpha) { for (int x = 0; x < int(w); ++x) { row[x * 4 + 3] = 255; } }
		}
		result.image = image;
	} else if (format == IdTechImageFormat::SinSwl) {
		if (bytes.size() < 1236) { return fail(QT_TRANSLATE_NOOP("VibeStudioExtraImage", "The SWL header is truncated.")); }
		const auto width = u32(bytes, 64), height = u32(bytes, 68);
		quint64 previousEnd = 1236;
		qint64 retained = 0;
		QVector<QRgb> colors;
		for (int i = 0; i < 256; ++i) {
			const auto* c = reinterpret_cast<const uchar*>(bytes.constData()) + 72 + i * 4;
			colors << qRgba(c[0], c[1], c[2], i == 255 ? 0 : 255);
		}
		for (int mip = 0; mip < 4; ++mip) {
			const auto w = std::max(1u, width >> mip), h = std::max(1u, height >> mip), offset = u32(bytes, 1100 + mip * 4);
			if (!width || !height || !dimensions(w, h, retained, context) || offset < previousEnd || quint64(offset) + quint64(w) * h > quint64(bytes.size())) {
				return fail(QT_TRANSLATE_NOOP("VibeStudioExtraImage", "Invalid, overlapping, truncated, or oversized SWL mip levels."));
			}
			previousEnd = quint64(offset) + quint64(w) * h; retained += qint64(w) * h;
			QImage image(int(w), int(h), QImage::Format_Indexed8); image.setColorTable(colors);
			if (image.isNull()) { return fail(QT_TRANSLATE_NOOP("VibeStudioExtraImage", "Unable to allocate image pixels.")); }
			for (int y = 0; y < int(h); ++y) {
				if (cancelled(context)) { return fail(QT_TRANSLATE_NOOP("VibeStudioExtraImage", "Image decoding cancelled.")); }
				std::memcpy(image.scanLine(y), bytes.constData() + offset + qint64(y) * w, w);
			}
			result.mipLevels << image;
		}
		result.image = result.mipLevels.first(); result.paletted = true;
		result.paletteId = QStringLiteral("swl-embedded"); result.paletteSourceVirtualPath = QStringLiteral("embedded");
		result.textureName = QString::fromLatin1(bytes.left(64).split('\0').first());
	}
	result.decoded = !result.image.isNull(); result.width = result.image.width(); result.height = result.image.height();
	for (int y = 0; result.image.hasAlphaChannel() && !result.hasTransparency && y < result.height; ++y) {
		if (cancelled(context)) { return fail(QT_TRANSLATE_NOOP("VibeStudioExtraImage", "Image decoding cancelled.")); }
		for (int x = 0; x < result.width; ++x) { if (qAlpha(result.image.pixel(x, y)) < 255) { result.hasTransparency = true; break; } }
	}
	return result;
}

QByteArray encodeExtraImage(const QImage& source, bool dds, QString* error, const std::function<bool()>& isCancelled)
{
	if (error) { error->clear(); }
	const auto fail = [&](const char* reason) { if (error) { *error = message(reason); } return QByteArray(); };
	if (source.isNull() || qint64(source.width()) * source.height() > 4 * 1024 * 1024) {
		return fail(QT_TRANSLATE_NOOP("VibeStudioExtraImage", "Invalid or oversized image for texture export."));
	}
	if (isCancelled && isCancelled()) { return fail(QT_TRANSLATE_NOOP("VibeStudioExtraImage", "Texture export cancelled.")); }
	const auto image = source.convertToFormat(QImage::Format_ARGB32);
	if (image.isNull()) { return fail(QT_TRANSLATE_NOOP("VibeStudioExtraImage", "Unable to allocate image pixels.")); }
	QByteArray bytes(dds ? 128 : 12, '\0');
	if (dds) {
		bytes.replace(0, 4, "DDS "); put32(bytes, 4, 124); put32(bytes, 8, 0x100f);
		put32(bytes, 12, image.height()); put32(bytes, 16, image.width()); put32(bytes, 20, image.width() * 4);
		put32(bytes, 76, 32); put32(bytes, 80, 0x41); put32(bytes, 88, 32);
		put32(bytes, 92, 0x00ff0000); put32(bytes, 96, 0x0000ff00); put32(bytes, 100, 0x000000ff); put32(bytes, 104, 0xff000000); put32(bytes, 108, 0x1000);
	} else { put32(bytes, 0, image.width()); put32(bytes, 4, image.height()); put32(bytes, 8, 1); }
	bytes.reserve(bytes.size() + qsizetype(image.width()) * image.height() * 4);
	for (int y = 0; y < image.height(); ++y) {
		if (isCancelled && isCancelled()) { return fail(QT_TRANSLATE_NOOP("VibeStudioExtraImage", "Texture export cancelled.")); }
		for (int x = 0; x < image.width(); ++x) {
			const auto color = image.pixel(x, y);
			bytes += char(dds ? qBlue(color) : qRed(color)); bytes += char(qGreen(color));
			bytes += char(dds ? qRed(color) : qBlue(color)); bytes += char(qAlpha(color));
		}
	}
	return bytes;
}
} // namespace vibestudio
