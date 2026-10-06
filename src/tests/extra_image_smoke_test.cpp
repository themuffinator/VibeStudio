#include "core/extra_image.h"
#include "core/texture_export.h"
#include "core/asset_formats.h"
#include "core/package_staging.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool ok, const char* text, const QString& detail = {}) { if (!ok) { std::cerr << text << ": " << detail.toStdString() << '\n'; } return ok; }
void put32(QByteArray& bytes, int offset, quint32 n) { qToLittleEndian(n, bytes.data() + offset); }
QByteArray blockDds(const char* fourcc, const QByteArray& block)
{
	QByteArray bytes(128, '\0'); bytes.replace(0, 4, "DDS "); put32(bytes, 4, 124); put32(bytes, 8, 0x81007);
	put32(bytes, 12, 4); put32(bytes, 16, 4); put32(bytes, 76, 32); put32(bytes, 80, 4); bytes.replace(84, 4, fourcc, 4); put32(bytes, 108, 0x1000);
	return bytes + block;
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); bool ok = true;
	QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	QImage pixels(3, 2, QImage::Format_ARGB32);
	pixels.setPixel(0, 0, qRgba(255, 0, 0, 255)); pixels.setPixel(1, 0, qRgba(0, 255, 0, 128)); pixels.setPixel(2, 0, qRgba(0, 0, 255, 0));
	pixels.setPixel(0, 1, qRgba(12, 23, 34, 45)); pixels.setPixel(1, 1, qRgba(56, 67, 78, 89)); pixels.setPixel(2, 1, qRgba(90, 101, 112, 123));
	for (const auto format : {TextureExportFormat::Dds, TextureExportFormat::Ftx}) {
		TextureExportOptions options; options.format = format;
		const auto encoded = encodeTextureExport(pixels, options);
		const auto path = QStringLiteral("test.") + textureExportSuffix(format);
		const auto decoded = decodeIdTechImage(path, encoded.bytes, {});
		ok &= expect(encoded.succeeded && decoded.decoded && decoded.image.convertToFormat(QImage::Format_ARGB32) == pixels, "RGBA multi-row codec round trip", encoded.error + decoded.error);
		const auto prefix = format == TextureExportFormat::Dds ? 128 : 12;
		ok &= expect(encoded.bytes.size() == prefix + 24 && uchar(encoded.bytes[prefix]) == (format == TextureExportFormat::Dds ? 0 : 255), "independent output size/channel layout");
		ok &= expect(!decodeIdTechImage(path, encoded.bytes.chopped(1), {}).decoded, "truncated pixels rejected");
		IdTechImageDecodeContext limits; limits.maximumImagePixels = 5;
		ok &= expect(!decodeIdTechImage(path, encoded.bytes, {}, limits).decoded, "per-surface pixel limit");
		limits = {}; int checks = 0; limits.isCancelled = [&] { return ++checks >= 3; };
		const auto stopped = decodeIdTechImage(path, encoded.bytes, {}, limits);
		ok &= expect(!stopped.decoded && stopped.image.isNull(), "cancellation retains no pixels");
		const auto cancelled = encodeTextureExport(pixels, options, {}, [](qint64, qint64) { return false; });
		ok &= expect(!cancelled.succeeded && cancelled.bytes.isEmpty(), "export cancellation");
		PackageStagingModel staging;
		QString error;
		ok &= expect(staging.createEmpty(PackageArchiveFormat::Zip, {}, &error) &&
			stageTextureExport(encoded, options, QStringLiteral("textures/") + path, &staging, false, &error), "native export stages through package service", error);
		for (const auto* suffix : {"pk4", "pkz"}) {
			PackageWriteRequest write; write.format = PackageArchiveFormat::Zip;
			write.destinationPath = temp.filePath(textureExportSuffix(format) + QStringLiteral(".") + QLatin1String(suffix));
			const auto written = staging.writeArchive(write);
			PackageArchive archive;
			ok &= expect(written.succeeded() && archive.load(write.destinationPath, &error), "ZIP-family package round trip", error);
			AssetImageConversionRequest conversion; conversion.virtualPaths << QStringLiteral("textures/") + path;
			conversion.outputFormat = format == TextureExportFormat::Dds ? QStringLiteral("ftx") : QStringLiteral("dds");
			conversion.outputDirectory = temp.filePath(QString::fromLatin1(suffix) + textureExportSuffix(format));
			const auto converted = convertPackageImages(archive, conversion);
			ok &= expect(converted.succeeded() && converted.writtenCount == 1, "shared package image conversion", assetImageConversionReportText(converted));
			if (converted.writtenCount == 1) {
				QFile output(converted.entries.first().outputPath);
				ok &= expect(output.open(QIODevice::ReadOnly), "conversion readable");
				const auto read = decodeIdTechImage(converted.entries.first().outputPath, output.readAll(), {});
				ok &= expect(read.decoded && read.image.convertToFormat(QImage::Format_ARGB32) == pixels, "converted package pixels preserved", read.error);
			}
		}
	}
	// Independent block fixtures: red BC1/2/3, transparent BC1, grayscale BC4,
	// and red/green channels for BC5. They exercise every advertised block family.
	const QByteArray red = QByteArray::fromHex("00f8e00700000000");
	for (const auto& test : QVector<QPair<QByteArray, QByteArray>>{{"DXT1", red}, {"DXT3", QByteArray(8, char(0xff)) + red}, {"DXT5", QByteArray::fromHex("ff00000000000000") + red}}) {
		const auto decoded = decodeIdTechImage(QStringLiteral("block.dds"), blockDds(test.first.constData(), test.second), {});
		ok &= expect(decoded.decoded && decoded.image.pixel(3, 3) == qRgba(255, 0, 0, 255), "BC1/2/3 red oracle", decoded.error);
	}
	const auto transparent = decodeIdTechImage(QStringLiteral("block.dds"), blockDds("DXT1", QByteArray::fromHex("0000ffff03000000")), {});
	ok &= expect(transparent.decoded && qAlpha(transparent.image.pixel(0, 0)) == 0, "BC1 transparent selector");
	for (const auto* tag : {"ATI1", "BC4U", "BC4S"}) {
		const auto image = decodeIdTechImage(QStringLiteral("block.dds"), blockDds(tag, QByteArray::fromHex("7f00000000000000")), {});
		ok &= expect(image.decoded && qRed(image.image.pixel(0, 0)) == (QByteArray(tag) == "BC4S" ? 255 : 127), "BC4 oracle", image.error);
	}
	for (const auto* tag : {"ATI2", "BC5U", "BC5S"}) {
		const auto image = decodeIdTechImage(QStringLiteral("block.dds"), blockDds(tag, QByteArray::fromHex("7f000000000000007f00000000000000")), {});
		ok &= expect(image.decoded && qRed(image.image.pixel(0, 0)) == (QByteArray(tag) == "BC5S" ? 255 : 127), "BC5 oracle", image.error);
	}
	const auto rxgb = decodeIdTechImage(QStringLiteral("normal.dds"), blockDds("RXGB", QByteArray::fromHex("ff00000000000000") + QByteArray::fromHex("e007000000000000")), {});
	ok &= expect(rxgb.decoded && qRed(rxgb.image.pixel(0, 0)) == 255 && qGreen(rxgb.image.pixel(0, 0)) == 255, "RXGB red from alpha", rxgb.error);
	auto dx10 = blockDds("DX10", QByteArray(20, '\0') + red);
	put32(dx10, 128, 71); put32(dx10, 132, 3); put32(dx10, 140, 1);
	const auto dx10Image = decodeIdTechImage(QStringLiteral("dx10.dds"), dx10, {});
	ok &= expect(dx10Image.decoded && dx10Image.image.pixel(0, 0) == qRgb(255, 0, 0), "DX10 BC1 oracle", dx10Image.error);
	put32(dx10, 140, 2);
	ok &= expect(!decodeIdTechImage(QStringLiteral("array.dds"), dx10, {}).decoded, "DX10 texture arrays rejected");
	put32(dx10, 140, 1); put32(dx10, 136, 4);
	ok &= expect(!decodeIdTechImage(QStringLiteral("cube.dds"), dx10, {}).decoded, "DX10 cubes rejected");
	QString error;
	const auto masks = encodeExtraImage(pixels, true, &error);
	for (const quint32 mask : {0x0000ff00u, 0x00550000u}) {
		auto invalid = masks; put32(invalid, 92, mask);
		ok &= expect(!decodeIdTechImage(QStringLiteral("mask.dds"), invalid, {}).decoded, "overlapping or noncontiguous DDS masks rejected");
	}
	const auto premultiplied = pixels.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	const auto straight = premultiplied.convertToFormat(QImage::Format_ARGB32);
	for (const bool dds : {true, false}) {
		const auto decoded = decodeIdTechImage(dds ? QStringLiteral("straight.dds") : QStringLiteral("straight.ftx"), encodeExtraImage(premultiplied, dds, &error), {});
		ok &= expect(decoded.decoded && decoded.image.convertToFormat(QImage::Format_ARGB32) == straight, "native writer unpremultiplies input pixels", decoded.error);
	}
	auto cube = blockDds("DXT1", red); put32(cube, 112, 0x200);
	ok &= expect(!decodeIdTechImage(QStringLiteral("cube.dds"), cube, {}).decoded, "cube cannot masquerade as 2D texture");
	auto huge = blockDds("DXT1", red); put32(huge, 12, 16384); put32(huge, 16, 16384);
	ok &= expect(!decodeIdTechImage(QStringLiteral("huge.dds"), huge, {}).decoded, "oversized compressed surface rejected before allocation");
	QByteArray swl(1236, '\0'); put32(swl, 64, 8); put32(swl, 68, 8);
	swl[72 + 4] = char(24); swl[72 + 5] = char(48); swl[72 + 6] = char(96);
	for (int mip = 0; mip < 4; ++mip) { put32(swl, 1100 + mip * 4, swl.size()); const int side = 8 >> mip; swl += QByteArray(side * side, char(1)); }
	swl[1236] = char(255);
	const auto sin = decodeIdTechImage(QStringLiteral("wall.swl"), swl, {});
	ok &= expect(sin.decoded && sin.mipLevels.size() == 4 && sin.mipLevels[3].size() == QSize(1, 1) && sin.image.pixel(1, 0) == qRgb(24, 48, 96) && qAlpha(sin.image.pixel(0, 0)) == 0,
		"SWL embedded palette, transparency and all mips", sin.error);
	put32(swl, 1104, 1236);
	ok &= expect(!decodeIdTechImage(QStringLiteral("bad.swl"), swl, {}).decoded, "overlapping SWL mip rejected");
	ok &= expect(assetImageOpenFilter().contains(QStringLiteral("*.dds")) && assetImageOpenFilter().contains(QStringLiteral("*.swl")), "shared import filter includes native codecs");
	return ok ? 0 : 1;
}
