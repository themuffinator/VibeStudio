#include "core/texture_document.h"
#include "core/texture_export.h"
#include "core/texture_output.h"
#include "core/texture_project.h"
#include "core/package_archive.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>

#include <algorithm>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* message)
{
	if (!condition) { std::cerr << message << '\n'; }
	return condition;
}
void le32(QByteArray& bytes, int offset, quint32 value) { qToLittleEndian(value, bytes.data() + offset); }
QByteArray png(const QImage& image)
{
	QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
	return image.save(&buffer, "PNG") ? bytes : QByteArray{};
}
bool put(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
class ImageReadProbe final : public PackageArchiveReader {
public:
	PackageEntry entry; QByteArray payload; mutable int reads = 0; mutable qint64 maximumRead = 0;
	std::function<void()> afterRead;
	PackageArchiveFormat format() const override { return PackageArchiveFormat::Zip; }
	QString sourcePath() const override { return QStringLiteral("fixture.pk3"); }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override { return {entry}; }
	bool readEntryBytes(const QString&, QByteArray* out, QString*, qint64 maximum) const override { ++reads; maximumRead = maximum; *out = payload; if (afterRead) { afterRead(); } return true; }
};
QByteArray sprite(int groups, int framesPerGroup, quint32 interval = 0x3f800000)
{
	QByteArray bytes(36, '\0'); bytes.replace(0, 4, "IDSP");
	le32(bytes, 4, 1); le32(bytes, 16, 1); le32(bytes, 20, 1); le32(bytes, 24, groups);
	for (int group = 0; group < groups; ++group) {
		QByteArray header(8 + framesPerGroup * 4, '\0'); le32(header, 0, 1); le32(header, 4, framesPerGroup);
		for (int i = 0; i < framesPerGroup; ++i) { le32(header, 8 + i * 4, interval); }
		bytes += header;
		for (int i = 0; i < framesPerGroup; ++i) { QByteArray frame(17, '\0'); le32(frame, 8, 1); le32(frame, 12, 1); bytes += frame; }
	}
	return bytes;
}
QByteArray shorterProjectPalette(const QByteArray& bytes)
{
	const int length = qFromLittleEndian<quint32>(bytes.constData() + 12);
	auto root = QJsonDocument::fromJson(bytes.mid(16, length)).object();
	auto layers = root.value(QStringLiteral("layers")).toArray(); auto layer = layers[0].toObject();
	layer.insert(QStringLiteral("indexedPalette"), QJsonArray{QStringLiteral("#ff000000"), QStringLiteral("#ff010101")});
	layers[0] = layer; root.insert(QStringLiteral("layers"), layers);
	const auto json = QJsonDocument(root).toJson(QJsonDocument::Compact);
	QByteArray result = bytes.first(16); le32(result, 12, json.size()); result += json;
	result += bytes.mid(16 + length, bytes.size() - length - 48);
	result += QCryptographicHash::hash(result, QCryptographicHash::Sha256); return result;
}

bool safety()
{
	bool ok = true; QString error;
	IdTechPaletteResolution palette; palette.palette = generatedIdTechPalette(QStringLiteral("quake"));
	QImage small(16, 16, QImage::Format_ARGB32); small.fill(Qt::red);
	IdTechImageDecodeContext limits; limits.maximumImagePixels = 255;
	ImageReadProbe probe; probe.entry.virtualPath = QStringLiteral("image.png"); probe.payload = png(small);
	probe.entry.sizeBytes = 64 * 1024 * 1024 + 1; QByteArray imageBytes;
	ok &= expect(!readIdTechImageEntry(probe, probe.entry.virtualPath, &imageBytes, &error) && probe.reads == 0, "oversized decoded entries are rejected before invoking an archive reader");
	probe.entry.sizeBytes = probe.payload.size(); probe.entry.compressedSizeBytes = 64 * 1024 * 1024 + 1;
	ok &= expect(!readIdTechImageEntryAt(probe, 0, &imageBytes, &error) && probe.reads == 0, "oversized compressed input is rejected before allocation as well");
	probe.entry.compressedSizeBytes = probe.payload.size();
	ok &= expect(readIdTechImageEntry(probe, QStringLiteral("IMAGE.PNG"), &imageBytes, &error) && imageBytes == probe.payload, "bounded image reads retain case-insensitive package lookup");
	ok &= expect(probe.maximumRead == probe.payload.size() + 1, "a growing source can be probed by only one byte beyond its captured size");
	++probe.entry.sizeBytes;
	ok &= expect(!readIdTechImageEntryAt(probe, 0, &imageBytes, &error) && imageBytes.isEmpty(), "short or changing archive data cannot be mistaken for a complete image");
	for (auto format : {TextureExportFormat::Png, TextureExportFormat::IndexedPng, TextureExportFormat::Targa,
		TextureExportFormat::Pcx, TextureExportFormat::QuakeMiptex, TextureExportFormat::Quake2Wal, TextureExportFormat::DoomPatch}) {
		TextureExportOptions options; options.format = format; options.allowGeneratedPalette = true;
		const auto encoded = encodeTextureExport(small, options, palette);
		const auto result = decodeIdTechImage(QStringLiteral("image.") + textureExportSuffix(format), encoded.bytes, palette.palette, limits);
		ok &= expect(encoded.succeeded && !result.decoded && result.image.isNull() && result.mipLevels.isEmpty() &&
			result.error.contains(QStringLiteral("limit")), "every image profile checks caller dimensions before allocating or returning pixels");
		for (int cancelAt : {1, 3}) {
			int callbacks = 0; IdTechImageDecodeContext cancel;
			cancel.isCancelled = [&] { return ++callbacks >= cancelAt; };
			const auto cancelled = decodeIdTechImage(QStringLiteral("image.") + textureExportSuffix(format), encoded.bytes, palette.palette, cancel);
			ok &= expect(!cancelled.decoded && cancelled.image.isNull() && cancelled.mipLevels.isEmpty() && cancelled.frames.isEmpty() &&
				cancelled.error.contains(QStringLiteral("cancelled")), "early and in-progress cancellation never exposes partial native or Qt images");
		}
	}
	TextureExportOptions mip; mip.format = TextureExportFormat::QuakeMiptex; mip.allowGeneratedPalette = true;
	const auto encodedMip = encodeTextureExport(small, mip, palette);
	limits.maximumImagePixels = 256; limits.maximumTotalPixels = 256;
	const auto excessiveMips = decodeIdTechImage(QStringLiteral("image.mip"), encodedMip.bytes, palette.palette, limits);
	ok &= expect(!excessiveMips.decoded && excessiveMips.mipLevels.isEmpty() && excessiveMips.error.contains(QStringLiteral("aggregate")), "mip chains share one aggregate budget and never expose earlier decoded levels on failure");
	limits.maximumTotalPixels = 1;
	const auto excessiveFrames = decodeIdTechImage(QStringLiteral("image.spr"), sprite(1, 2), palette.palette, limits);
	ok &= expect(!excessiveFrames.decoded && excessiveFrames.frames.isEmpty() && excessiveFrames.error.contains(QStringLiteral("aggregate")), "grouped sprite frames share an allocation budget");
	const auto manyFrames = decodeIdTechImage(QStringLiteral("image.spr"), sprite(2, 4096), palette.palette);
	ok &= expect(!manyFrames.decoded && manyFrames.frames.isEmpty() && manyFrames.error.contains(QStringLiteral("4,096")), "sprite groups cannot multiply the stored frame count past its global cap");
	const auto infiniteInterval = decodeIdTechImage(QStringLiteral("image.spr"), sprite(1, 1, 0x7f800000), palette.palette);
	ok &= expect(!infiniteInterval.decoded && infiniteInterval.frames.isEmpty(), "non-finite sprite timing cannot overflow integer milliseconds");
	QByteArray repeatedPosts(8 + 256 * 4, '\0'); qToLittleEndian<quint16>(256, repeatedPosts.data()); qToLittleEndian<quint16>(1, repeatedPosts.data() + 2);
	for (int i = 0; i < 256; ++i) { le32(repeatedPosts, 8 + i * 4, repeatedPosts.size()); }
	QByteArray post(259, '\0'); post[1] = char(255);
	for (int i = 0; i < 4096; ++i) { repeatedPosts += post; } repeatedPosts += char(255);
	const auto repeatedColumns = decodeIdTechImage(QStringLiteral("picture.lmp"), repeatedPosts, palette.palette);
	ok &= expect(!repeatedColumns.decoded && repeatedColumns.image.isNull() && repeatedColumns.error.contains(QStringLiteral("work limit")), "shared Doom column offsets cannot multiply decoding work without a bound");
	QByteArray damagedColumn(8 + 32 * 4, '\0'); qToLittleEndian<quint16>(32, damagedColumn.data()); qToLittleEndian<quint16>(1, damagedColumn.data() + 2);
	for (int i = 0; i < 32; ++i) { le32(damagedColumn, 8 + i * 4, damagedColumn.size()); }
	damagedColumn += QByteArray::fromHex("0001000000ff"); le32(damagedColumn, 8 + 20 * 4, damagedColumn.size()); damagedColumn += QByteArray::fromHex("00030000");
	const auto brokenColumn = decodeIdTechImage(QStringLiteral("picture.lmp"), damagedColumn, palette.palette);
	ok &= expect(!brokenColumn.decoded && brokenColumn.image.isNull(), "corruption outside the sniffed Doom columns cannot return a partial picture");

	QTemporaryDir temporary; if (!temporary.isValid()) { return false; }
	const QDir root(temporary.path());
	ok &= put(root.filePath(QStringLiteral("frame.png")), png(small));
	PackageArchive archive; ok &= archive.load(root.path(), &error);
	QByteArray sp2(12 + 2 * 80, '\0'); sp2.replace(0, 4, "IDS2"); le32(sp2, 4, 2); le32(sp2, 8, 2);
	for (int i = 0; i < 2; ++i) { const int offset = 12 + i * 80; le32(sp2, offset, 16); le32(sp2, offset + 4, 16); sp2.replace(offset + 16, 9, "frame.png"); }
	limits.archive = &archive; limits.maximumTotalPixels = 256;
	const auto externalFrames = decodeIdTechImage(QStringLiteral("image.sp2"), sp2, palette.palette, limits);
	ok &= expect(!externalFrames.decoded && externalFrames.frames.isEmpty(), "external sprite references cannot reset the aggregate image budget");
	probe.entry.virtualPath = QStringLiteral("frame.png"); probe.entry.sizeBytes = probe.payload.size();
	probe.entry.compressedSizeBytes = probe.payload.size() + 32; probe.reads = 0;
	IdTechImageDecodeContext external; external.archive = &probe;
	external.maximumExternalBytes = probe.entry.compressedSizeBytes * 2 - 1;
	const auto excessiveReads = decodeIdTechImage(QStringLiteral("image.sp2"), sp2, palette.palette, external);
	ok &= expect(!excessiveReads.decoded && excessiveReads.frames.isEmpty() && excessiveReads.image.isNull() && probe.reads == 1 &&
		excessiveReads.error.contains(QStringLiteral("aggregate external-image")), "repeated sprite references charge stored and decoded input before the next archive read");
	external.maximumExternalBytes = probe.entry.compressedSizeBytes * 2; probe.reads = 0;
	ok &= expect(decodeIdTechImage(QStringLiteral("image.sp2"), sp2, palette.palette, external).decoded && probe.reads == 2,
		"external image byte budget accepts the exact bound");
	bool cancelledRead = false; probe.reads = 0; probe.afterRead = [&] { cancelledRead = true; };
	external.isCancelled = [&] { return cancelledRead; };
	const auto cancelledFrames = decodeIdTechImage(QStringLiteral("image.sp2"), sp2, palette.palette, external);
	ok &= expect(!cancelledFrames.decoded && cancelledFrames.frames.isEmpty() && probe.reads == 1 && cancelledFrames.error.contains(QStringLiteral("cancelled")),
		"external sprite cancellation stops after the current read and cannot publish partial frames");

	TextureDocument document; document.reset(small); document.paintStroke({{0, 0}}, Qt::blue, 1);
	const auto unchanged = document.image(); const auto revision = document.revision(); const auto history = document.historyBytes();
	QByteArray largeLump(8 + 2049 * 2048, '\0'); le32(largeLump, 0, 2049); le32(largeLump, 4, 2048);
	const auto nativePath = root.filePath(QStringLiteral("oversized.lmp")); ok &= put(nativePath, largeLump);
	IdTechImageDecodeResult untouched; untouched.textureName = QStringLiteral("sentinel");
	int importSteps = 0;
	ok &= expect(!loadTextureFile(root.filePath(QStringLiteral("frame.png")), palette.palette, &document, &error, &untouched,
		[&](qint64, qint64) { return ++importSteps < 3; }) && document.image() == unchanged && document.revision() == revision &&
		document.historyBytes() == history && untouched.textureName == QStringLiteral("sentinel"), "cancelled file import preserves document and caller metadata");
	ok &= expect(!loadTextureFile(nativePath, palette.palette, &document, &error, &untouched) && document.image() == unchanged &&
		document.revision() == revision && document.historyBytes() == history && untouched.textureName == QStringLiteral("sentinel"), "oversized native import preserves pixels, history, save state, and caller metadata");
	QByteArray bmp(54, '\0'); bmp.replace(0, 2, "BM"); le32(bmp, 2, 54); le32(bmp, 10, 54); le32(bmp, 14, 40);
	le32(bmp, 18, 32768); le32(bmp, 22, 32768); bmp[26] = 1; bmp[28] = 24;
	const auto oversizedQt = decodeIdTechImage(QStringLiteral("huge.bmp"), bmp, palette.palette);
	ok &= expect(!oversizedQt.decoded && oversizedQt.image.isNull() && oversizedQt.error.contains(QStringLiteral("limit")), "compressed or truncated Qt image headers cannot request unbounded decode allocations");
	QImage indices(16, 16, QImage::Format_Indexed8); indices.setColorTable({qRgb(0, 0, 0), qRgb(1, 1, 1)}); indices.fill(255);
	ok &= expect(!document.reset(indices, &error) && document.revision() == revision && document.image() == unchanged, "out-of-palette indices cannot enter an open document");
	ok &= expect(!document.pastePixels(indices, {0, 0}, &error) && document.image() == unchanged && document.revision() == revision &&
		!encodeTextureExport(indices, TextureExportOptions{}, palette).succeeded, "paste and direct export reject invalid caller-owned indexed images before Qt pixel conversion");
	indices.setColorTable(QList<QRgb>(257, qRgb(0, 0, 0))); indices.fill(0);
	ok &= expect(!document.reset(indices, &error) && document.revision() == revision, "oversized palettes cannot create a project that its own reader would reject");
	indices.fill(255);
	QList<QRgb> colors; for (int i = 0; i < 256; ++i) { colors << qRgb(i, i, i); } indices.setColorTable(colors);
	TextureDocument indexed; indexed.reset(indices);
	const auto malformedProject = shorterProjectPalette(encodeTextureProject(indexed));
	ok &= expect(!decodeTextureProject(malformedProject, &document, nullptr, &error) && document.image() == unchanged && document.revision() == revision, "valid project checksum cannot hide indices outside a restored grayscale palette");
	QJsonArray manyOperations; for (int i = 0; i < 257; ++i) { manyOperations.append(QJsonObject{{QStringLiteral("op"), QStringLiteral("select-none")}}); }
	ok &= expect(!applyTextureOperations(&document, manyOperations, palette.palette, &error) && document.revision() == revision, "recipe operation cap is checked before editing");
	int encodingSteps = 0;
	ok &= expect(encodeTextureProject(document, {{QStringLiteral("excess"), QString(256 * 1024, QLatin1Char('x'))}}, &error,
		[&](qint64, qint64) { ++encodingSteps; return true; }).isEmpty() && encodingSteps == 0, "oversized project metadata is rejected before encoding any layer pixels");
	return ok;
}

bool cachedStrokePreview()
{
	bool ok = true;
	for (int brushWidth : {1, 2, 7, 128}) {
		for (bool wrap : {false, true}) {
			QVector<TextureLayer> layers;
			for (int i = 0; i < 4; ++i) {
				QImage pixels(33, 23, QImage::Format_ARGB32);
				for (int y = 0; y < pixels.height(); ++y) { for (int x = 0; x < pixels.width(); ++x) { pixels.setPixel(x, y, qRgba(x * 7, y * 11, i * 61, 127 + i * 31)); } }
				layers.append({i + 1, QString::number(i), pixels, true, false, 35 + i * 20, TextureBlendMode(i)});
			}
			TextureDocument document; document.restoreLayers({33, 23}, layers, 2);
			const QImage before = document.image();
			TextureBrush brush; brush.width = brushWidth; brush.wrap = wrap; brush.mode = TexturePaintMode::SourceOver;
			const auto matchesFullComposite = [&]() { const auto reference = document.storageSnapshot(); return document.image() == reference.image(); };
			ok &= document.beginStroke({0, 0}, QColor(15, 95, 180, 137), brush);
			ok &= expect(matchesFullComposite(), "partial composite matches full rendering after the first brush stamp");
			for (QPoint point : {QPoint(5, 7), QPoint(32, 22), wrap ? QPoint(-12, -7) : QPoint(4, 21)}) {
				ok &= document.continueStroke(point);
				ok &= expect(matchesFullComposite(), "cached stroke rendering preserves all blend modes, even brush widths, and repeated seams");
			}
			ok &= document.endStroke();
			ok &= expect(matchesFullComposite() && document.undo() && document.image() == before && document.redo() && matchesFullComposite(), "committing cached previews preserves exact undo and redo pixels");
			const QImage saved = document.image();
			document.beginStroke({1, 1}, Qt::transparent, brush); document.continueStroke({31, 21});
			document.cancelStroke();
			ok &= expect(document.image() == saved, "cancelling a partly rendered stroke restores the prior composite");
			ok &= document.setSelection({2, 3, 20, 15});
			ok &= document.beginStroke({1, 1}, Qt::white, brush) && document.continueStroke({31, 21});
			ok &= expect(matchesFullComposite() && document.endStroke() && matchesFullComposite(), "selection clipping and deferred composite updates remain exact");
		}
	}
	return ok;
}

bool connectedFillReference()
{
	bool ok = true;
	for (int tolerance : {0, 48, 255}) {
		for (bool wrap : {false, true}) {
			for (QRect selection : {QRect(), QRect(1, 1, 6, 3), QRect(0, 0, 7, 5)}) {
				QImage source(8, 5, QImage::Format_ARGB32);
				for (int y = 0; y < 5; ++y) { for (int x = 0; x < 8; ++x) { source.setPixel(x, y, qRgba((x * 13 + y * 7) % 97, y * 19, x % 3 * 91, 85 + x * 20)); } }
				const QRect bounds = selection.isEmpty() ? source.rect() : selection;
				const QPoint seed = bounds.topLeft(); const auto target = source.pixelColor(seed);
				// Definition-based oracle: expand the set to a fixed point using
				// immutable source colors, independent of the production queue.
				QVector<uchar> reached(40, 0); reached[seed.y() * 8 + seed.x()] = 1;
				bool changed = true;
				while (changed) {
					changed = false;
					for (int y = bounds.top(); y <= bounds.bottom(); ++y) { for (int x = bounds.left(); x <= bounds.right(); ++x) {
						if (reached[y * 8 + x]) { continue; }
						const auto color = source.pixelColor(x, y);
						if (std::abs(color.red() - target.red()) > tolerance || std::abs(color.green() - target.green()) > tolerance ||
							std::abs(color.blue() - target.blue()) > tolerance || std::abs(color.alpha() - target.alpha()) > tolerance) { continue; }
						for (QPoint neighbor : {QPoint(x - 1, y), QPoint(x + 1, y), QPoint(x, y - 1), QPoint(x, y + 1)}) {
							if (wrap) { neighbor = {(neighbor.x() + 8) % 8, (neighbor.y() + 5) % 5}; }
							if (bounds.contains(neighbor) && reached[neighbor.y() * 8 + neighbor.x()]) { reached[y * 8 + x] = 1; changed = true; break; }
						}
					} }
				}
				QImage expected = source;
				const QColor replacement(12, 19, 27, 155);
				for (int i = 0; i < 40; ++i) { if (reached[i]) { expected.setPixelColor(i % 8, i / 8, replacement); } }
				TextureDocument document; document.reset(source); if (!selection.isEmpty()) { document.setSelection(selection); }
				ok &= expect(document.floodFill(seed, replacement, tolerance, wrap) && document.image() == expected && document.undo() && document.image() == source,
					"optimized fill matches four-connected RGBA reachability with tolerance, wrapping, selections, and exact undo");
			}
		}
	}
	return ok;
}

struct Measurement {
	QElapsedTimer timer;
	qint64 previous = 0, longest = 0;
	int callbacks = 0;
	Measurement() { timer.start(); }
	bool progress(qint64, qint64) { const auto now = timer.nsecsElapsed(); longest = std::max(longest, now - previous); previous = now; ++callbacks; return true; }
	QJsonObject result(const QString& name, bool passed) {
		progress(0, 0);
		return {{QStringLiteral("workload"), name}, {QStringLiteral("passed"), passed}, {QStringLiteral("milliseconds"), timer.nsecsElapsed() / 1e6},
			{QStringLiteral("maximumCheckpointGapMs"), longest / 1e6}, {QStringLiteral("checkpoints"), callbacks - 1}};
	}
};
bool benchmark()
{
	bool ok = true; QString error; QJsonArray measurements;
	TextureDocument document; document.create({2048, 2048}, Qt::black); document.markSaved();
	auto measure = [&](const QString& name, auto operation) {
		Measurement sample; const bool passed = operation([&](qint64 done, qint64 total) { return sample.progress(done, total); });
		measurements.append(sample.result(name, passed)); ok &= expect(passed, qPrintable(name));
	};
	measure(QStringLiteral("4M pixel connected fill"), [&](const TextureProgress& progress) { return document.floodFill({0, 0}, Qt::white, &error, progress); });
	const QImage before = document.image(); const auto revision = document.revision();
	measure(QStringLiteral("4M pixel cancellation rollback"), [&](const TextureProgress&) {
		return !document.floodFill({0, 0}, Qt::red, &error, [](qint64 done, qint64) { return done < 32768; }) && document.image() == before && document.revision() == revision;
	});
	measure(QStringLiteral("4M pixel wrapped ellipse"), [&](const TextureProgress& progress) {
		TextureBrush brush; brush.wrap = true; return document.drawShape(TextureShape::Ellipse, {-2048, -2048}, {4095, 4095}, Qt::blue, brush, true, &error, progress);
	});
	measure(QStringLiteral("4M pixel smooth resize"), [&](const TextureProgress& progress) { return document.resize({4096, 1024}, true, &error, progress); });
	measure(QStringLiteral("4M pixel rotation"), [&](const TextureProgress& progress) { return document.rotateClockwise(&error, progress); });
	measure(QStringLiteral("128 MiB undo saturation"), [&](const TextureProgress&) {
		for (int i = 0; i < 12; ++i) { if (!document.paintStroke({{i, i}}, QColor(i * 17, 7, 19), 1, &error) || document.historyBytes() > 128 * 1024 * 1024) { return false; } }
		int count = 0; while (document.undo()) { ++count; if (document.historyBytes() > 128 * 1024 * 1024) { return false; } }
		while (document.redo()) { if (document.historyBytes() > 128 * 1024 * 1024) { return false; } }
		return count > 0 && document.isDirty();
	});
	document = document.storageSnapshot();
	QVector<TextureLayer> layers;
	for (int i = 0; i < 8; ++i) { QImage pixels(2048, 2048, QImage::Format_ARGB32); pixels.fill(qRgba(i * 31, 80, 190, 128)); layers.append({i + 1, QString::number(i), pixels, true, false, 100, i % 2 ? TextureBlendMode::Multiply : TextureBlendMode::Normal}); }
	ok &= document.restoreLayers({2048, 2048}, layers, 7, &error); layers.clear();
	ok &= expect(!document.addLayer(QStringLiteral("overflow"), {}, &error) && document.layers().size() == 8, "maximum aggregate layer pixels cannot be exceeded by adding a layer");
	measure(QStringLiteral("32M layer pixel composite"), [&](const TextureProgress&) { return !document.image().isNull(); });
	measure(QStringLiteral("32M layer pixel interactive stamp and composite"), [&](const TextureProgress&) { return document.beginStroke({100, 100}, Qt::red, 32) && !document.image().isNull() && document.endStroke(); });
	QByteArray bytes;
	measure(QStringLiteral("32M layer pixel project encoding"), [&](const TextureProgress& progress) { bytes = encodeTextureProject(document, {}, &error, progress); return !bytes.isEmpty(); });
	measure(QStringLiteral("32M layer pixel project decoding"), [&](const TextureProgress& progress) { TextureDocument reopened; return decodeTextureProject(bytes, &reopened, nullptr, &error, progress) && reopened.layers().size() == 8; });
	TextureExportOptions options; options.format = TextureExportFormat::Quake2Wal; options.extendedLimits = true; options.allowGeneratedPalette = true; options.alpha = TextureExportAlpha::Matte;
	IdTechPaletteResolution palette; palette.palette = generatedIdTechPalette(QStringLiteral("quake2"));
	measure(QStringLiteral("4M pixel WAL export and mipmaps"), [&](const TextureProgress& progress) { return encodeTextureExport(document.image(), options, palette, progress).succeeded; });
	document = {}; bytes.clear();
	QImage noise(2048, 2048, QImage::Format_ARGB32); quint32 random = 0x195327u;
	for (int y = 0; y < noise.height(); ++y) {
		auto* row = reinterpret_cast<QRgb*>(noise.scanLine(y));
		for (int x = 0; x < noise.width(); ++x) { random ^= random << 13; random ^= random >> 17; random ^= random << 5; row[x] = random | 0xff000000u; }
	}
	options.format = TextureExportFormat::Png;
	measure(QStringLiteral("4M noisy pixel PNG encoding"), [&](const TextureProgress& progress) { const auto result = encodeTextureExport(noise, options, palette, progress); bytes = result.bytes; return result.succeeded; });
	measure(QStringLiteral("4M noisy pixel PNG encode cancellation"), [&](const TextureProgress& progress) {
		int steps = 0;
		const auto result = encodeTextureOutputPng(noise, &error, [&] { progress(0, 0); return ++steps > 16; });
		return result.isEmpty() && error.contains(QStringLiteral("cancelled"));
	});
	measure(QStringLiteral("4M noisy pixel PNG decoding"), [&](const TextureProgress& progress) {
		IdTechImageDecodeContext context; context.isCancelled = [&] { return !progress(0, 0); };
		return decodeIdTechImage(QStringLiteral("noise.png"), bytes, palette.palette, context).image == noise;
	});
	options.format = TextureExportFormat::Pcx; options.dither = true;
	measure(QStringLiteral("4M noisy pixel dithered PCX export"), [&](const TextureProgress& progress) { const auto result = encodeTextureExport(noise, options, palette, progress); bytes = result.bytes; return result.succeeded; });
	measure(QStringLiteral("4M noisy pixel PCX decoding"), [&](const TextureProgress& progress) {
		IdTechImageDecodeContext context; context.isCancelled = [&] { return !progress(0, 0); };
		return decodeIdTechImage(QStringLiteral("noise.pcx"), bytes, palette.palette, context).decoded;
	});
	measure(QStringLiteral("4M noisy pixel PCX decode cancellation"), [&](const TextureProgress& progress) {
		int steps = 0; IdTechImageDecodeContext context; context.isCancelled = [&] { progress(0, 0); return ++steps > 16; };
		const auto result = decodeIdTechImage(QStringLiteral("noise.pcx"), bytes, palette.palette, context);
		return !result.decoded && result.image.isNull() && result.error.contains(QStringLiteral("cancelled"));
	});
	std::cout << QJsonDocument(measurements).toJson(QJsonDocument::Compact).constData() << '\n';
	if (!ok) { std::cerr << error.toStdString() << '\n'; }
	return ok;
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	return (app.arguments().contains(QStringLiteral("--benchmark")) ? benchmark() : (safety() && cachedStrokePreview() && connectedFillReference())) ? EXIT_SUCCESS : EXIT_FAILURE;
}
