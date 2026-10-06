#include "core/texture_export.h"
#include "core/package_archive.h"
#include "core/texture_project.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>

#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }
bool put(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
quint32 le32(const QByteArray& bytes, qsizetype at) { return qFromLittleEndian<quint32>(bytes.constData() + at); }
QImage indexedImage(QSize size, const IdTechPaletteResolution& palette, int index)
{
	QImage image(size, QImage::Format_Indexed8); image.setColorTable(palette.palette.colors); image.fill(index); return image;
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); bool ok = true; QString error;
	QTemporaryDir temporary; if (!temporary.isValid()) { return EXIT_FAILURE; } QDir root(temporary.path());
	IdTechPaletteResolution palette; palette.palette.id = QStringLiteral("test-grayscale"); palette.palette.sourceDescription = QStringLiteral("Synthetic test palette");
	for (int index = 0; index < 256; ++index) { palette.palette.colors << qRgb(index, index, index); }
	TextureExportOptions options;
	ok &= expect(!encodeTextureExport({}, options).error.isEmpty(), "empty input returns an actionable validation error");
	QImage rgba(3, 2, QImage::Format_ARGB32); const int alphas[]{255, 128, 0, 32, 64, 255};
	for (int y = 0; y < 2; ++y) { for (int x = 0; x < 3; ++x) { rgba.setPixel(x, y, qRgba(x * 90, y * 120, 30 + x + y, alphas[y * 3 + x])); } }
	const auto original = rgba; const auto png = encodeTextureExport(rgba, options);
	ok &= expect(png.succeeded && QImage::fromData(png.bytes).convertToFormat(QImage::Format_ARGB32) == rgba && png.colorChangedPixels == 0 && rgba == original, "RGBA PNG retains partial alpha and hidden RGB without editing source pixels");
	options.format = TextureExportFormat::Targa; const auto targa = encodeTextureExport(rgba, options);
	ok &= expect(targa.succeeded && targa.bytes.first(18) == QByteArray::fromHex("000002000000000000000000030002002008") && targa.bytes.mid(18, 4) == QByteArray::fromHex("1f780020") && targa.bytes.last(18) == QByteArray("TRUEVISION-XFILE.\0", 18), "TGA matches fixed bottom-origin header, BGRA texel and footer fixtures");
	ok &= expect(decodeIdTechImage(QStringLiteral("test.tga"), targa.bytes, {}).image == rgba, "TGA rows and alpha round trip");
	QImage invisible(3, 2, QImage::Format_ARGB32); invisible.fill(qRgba(32, 64, 96, 0)); const auto clearTarga = encodeTextureExport(invisible, options);
	ok &= expect(clearTarga.succeeded && decodeIdTechImage(QStringLiteral("clear.tga"), clearTarga.bytes, {}).image == invisible, "declared all-zero TGA alpha remains transparent");
	QByteArray legacy = clearTarga.bytes; legacy[17] = 0;
	ok &= expect(decodeIdTechImage(QStringLiteral("legacy.tga"), legacy, {}).image.pixelColor(0, 0).alpha() == 255, "undeclared zero TGA alpha retains legacy opaque fallback");
	options.format = TextureExportFormat::Pcx;
	ok &= expect(!encodeTextureExport(rgba, options, palette).succeeded, "PCX refuses implicit alpha loss");
	options.alpha = TextureExportAlpha::Matte; options.matte = Qt::white; const auto matte = encodeTextureExport(rgba, options, palette);
	ok &= expect(matte.succeeded && matte.alphaChangedPixels == 4 && matte.preview.pixelColor(2, 0) == QColor(Qt::white), "explicit matte is applied before quantization and reports changed alpha"); options.alpha = TextureExportAlpha::Strict;
	QImage runs = indexedImage({130, 1}, palette, 197); const auto pcx = encodeTextureExport(runs, options, palette);
	ok &= expect(pcx.succeeded && pcx.preservedIndices && pcx.bytes.first(4) == QByteArray::fromHex("0a050108") && quint8(pcx.bytes[65]) == 1 && qFromLittleEndian<quint16>(pcx.bytes.constData() + 66) == 130 && pcx.bytes.mid(128, 6) == QByteArray::fromHex("ffc5ffc5c4c5") && quint8(pcx.bytes[pcx.bytes.size() - 769]) == 12, "PCX fixture checks 63-pixel RLE runs, escaped bytes, stride and palette marker");
	ok &= expect(decodeIdTechImage(QStringLiteral("runs.pcx"), pcx.bytes, {}).image == runs, "PCX palette and indices round trip");
	QImage odd = indexedImage({7, 5}, palette, 0);
	for (int y = 0; y < 5; ++y) { for (int x = 0; x < 7; ++x) { odd.setPixel(x, y, x + y * 7); } }
	const auto oddPcx = encodeTextureExport(odd, options, palette);
	ok &= expect(oddPcx.succeeded && qFromLittleEndian<quint16>(oddPcx.bytes.constData() + 66) == 8 && decodeIdTechImage(QStringLiteral("odd.pcx"), oddPcx.bytes, {}).image == odd && oddPcx.warnings.size() >= 2, "odd PCX rows use even padding and explain engine incompatibility");
	options.format = TextureExportFormat::IndexedPng; auto alphaPalette = palette; alphaPalette.palette.colors[7] = qRgba(7, 7, 7, 64);
	const auto grayPng = encodeTextureExport(runs, options, palette);
	ok &= expect(grayPng.succeeded && quint8(grayPng.bytes[24]) == 8 && quint8(grayPng.bytes[25]) == 3 && QImage::fromData(grayPng.bytes) == runs, "indexed PNG always stores a palette and exact indices even for an identity grayscale palette");
	const auto alphaPng = encodeTextureExport(indexedImage({3, 3}, alphaPalette, 7), options, alphaPalette);
	ok &= expect(alphaPng.succeeded && alphaPng.preservedIndices && QImage::fromData(alphaPng.bytes).pixelColor(0, 0).alpha() == 64, "matching indexed PNG retains per-entry partial alpha");
	ok &= expect(!encodeTextureExport(rgba, options, palette).succeeded, "indexed PNG refuses unrepresentable alpha");
	auto generated = palette; generated.palette.generated = true;
	ok &= expect(!encodeTextureExport(runs, options, generated).succeeded, "generated palettes require explicit consent"); options.allowGeneratedPalette = true;
	const auto allowed = encodeTextureExport(runs, options, generated);
	ok &= expect(allowed.succeeded && !allowed.warnings.isEmpty(), "explicitly permitted generated palettes remain labelled"); options.allowGeneratedPalette = false;
	QImage texture = indexedImage({16, 16}, palette, 7); texture.setPixel(0, 0, 0); texture.setPixel(1, 0, 4); texture.setPixel(0, 1, 8); texture.setPixel(1, 1, 12);
	options.format = TextureExportFormat::QuakeMiptex; options.name = QStringLiteral("tex"); const auto mip = encodeTextureExport(texture, options, palette);
	const QByteArray expectedHeader = QByteArray("tex\0", 4) + QByteArray(12, '\0') + QByteArray::fromHex("100000001000000028000000280100006801000078010000");
	ok &= expect(mip.succeeded && mip.bytes.size() == 380 && mip.bytes.first(40) == expectedHeader && mip.mipLevels.size() == 4 && mip.mipLevels[1].pixelIndex(0, 0) == 6 && mip.mipLevels[3].size() == QSize(2, 2), "miptexture fixture checks exact offsets and four area-filtered levels");
	ok &= expect(decodeIdTechImage(QStringLiteral("tex.mip"), mip.bytes, palette.palette).mipLevels == mip.mipLevels, "every mip level round trips");
	options.mipFilter = TextureMipmapFilter::Nearest;
	ok &= expect(encodeTextureExport(texture, options, palette).mipLevels[1].pixelIndex(0, 0) == 12, "nearest mips share pixel-center sampling"); options.mipFilter = TextureMipmapFilter::Box;
	texture.setPixel(0, 0, 224);
	options.alpha = TextureExportAlpha::Matte;
	ok &= expect(encodeTextureExport(texture, options, palette).mipLevels[0].pixelIndex(0, 0) == 224, "an unused matte does not discard authored fullbright indices"); options.alpha = TextureExportAlpha::Strict;
	ok &= expect(encodeTextureExport(texture, options, palette).mipLevels[1].pixelIndex(0, 0) == 224, "area mips retain authored fullbright masks"); options.fullbright = TextureFullbrightMode::Exclude;
	const auto noBright = encodeTextureExport(texture, options, palette);
	ok &= expect(noBright.succeeded && noBright.mipLevels[0].pixelIndex(0, 0) == 223 && noBright.mipLevels[1].pixelIndex(0, 0) < 224, "exclude mode removes fullbrights from every level"); options.fullbright = TextureFullbrightMode::Allow;
	ok &= expect(encodeTextureExport(texture.convertToFormat(QImage::Format_ARGB32), options, palette).mipLevels[0].pixelIndex(0, 0) == 224, "allow mode quantizes RGBA into the fullbright band"); options.fullbright = TextureFullbrightMode::Preserve;
	options.format = TextureExportFormat::QuakeWad2; const auto wad = encodeTextureExport(texture, options, palette);
	ok &= expect(wad.succeeded && wad.bytes.size() == 424 && wad.bytes.first(12) == QByteArray::fromHex("574144320100000088010000") && le32(wad.bytes, 392) == 12 && le32(wad.bytes, 396) == 380 && le32(wad.bytes, 400) == 380 && quint8(wad.bytes[404]) == 0x44 && wad.bytes.mid(408, 16) == expectedHeader.first(16), "WAD2 fixture checks absolute offsets, sizes, name and miptex lump type");
	const auto wadPath = root.filePath(QStringLiteral("texture.wad")); put(wadPath, wad.bytes); PackageArchive archive;
	ok &= expect(archive.load(wadPath, &error) && archive.entries().size() == 1, "package reader accepts generated WAD2"); QByteArray lump;
	ok &= expect(archive.readEntryBytes(QStringLiteral("tex"), &lump, &error) && lump == wad.bytes.mid(12, 380), "package lookup returns exact native mip payload");
	options.format = TextureExportFormat::Quake2Wal; options.name = QStringLiteral("custom/tex"); options.animationNext = QStringLiteral("custom/next"); options.surfaceFlags = 0xf0000001u; options.contentFlags = 32; options.surfaceValue = -17; texture.setPixel(2, 0, 255);
	const auto wal = encodeTextureExport(texture, options, palette); const auto decodedWal = decodeIdTechImage(QStringLiteral("tex.wal"), wal.bytes, palette.palette);
	ok &= expect(wal.succeeded && wal.bytes.size() == 440 && le32(wal.bytes, 40) == 100 && le32(wal.bytes, 52) == 436 && le32(wal.bytes, 88) == 0xf0000001u && le32(wal.bytes, 92) == 32 && qFromLittleEndian<qint32>(wal.bytes.constData() + 96) == -17, "WAL fixture checks offsets and signed/unsigned metadata");
	ok &= expect(decodedWal.decoded && decodedWal.textureName == options.name && decodedWal.animationNextName == options.animationNext && decodedWal.surfaceFlags == options.surfaceFlags && decodedWal.contentFlags == 32 && decodedWal.surfaceValue == -17 && wal.preview.pixelIndex(2, 0) == 254, "WAL metadata round trips and opaque pixels avoid reserved index 255");
	const auto imported = textureExportOptionsForImage(decodedWal);
	auto duplicates = palette; duplicates.palette.colors[17] = duplicates.palette.colors[3]; QImage duplicateIndices = indexedImage({16, 16}, duplicates, 17); duplicateIndices.setPixel(0, 0, 255);
	const auto duplicateWal = encodeTextureExport(duplicateIndices, options, duplicates);
	ok &= expect(duplicateWal.succeeded && duplicateWal.preview.pixelIndex(0, 0) == 254 && duplicateWal.preview.pixelIndex(1, 0) == 17, "replacing reserved index 255 retains other exact duplicate-color indices");
	ok &= expect(imported.format == TextureExportFormat::Quake2Wal && imported.name == options.name && imported.animationNext == options.animationNext && imported.surfaceFlags == options.surfaceFlags && imported.contentFlags == options.contentFlags && imported.surfaceValue == options.surfaceValue, "native WAL import retains editable export metadata");
	options = {}; options.format = TextureExportFormat::DoomFlat; QImage flat = indexedImage({64, 64}, palette, 0);
	for (int y = 0; y < 64; ++y) { for (int x = 0; x < 64; ++x) { flat.setPixel(x, y, (x + y * 64) % 256); } }
	const auto flatOutput = encodeTextureExport(flat, options, palette);
	ok &= expect(flatOutput.succeeded && flatOutput.bytes.size() == 4096 && quint8(flatOutput.bytes[255]) == 255 && quint8(flatOutput.bytes[256]) == 0, "flat fixture is 4096 row-major indices including opaque index 255");
	options.format = TextureExportFormat::DoomPatch; options.leftOffset = -2; options.topOffset = 5; QImage patch(2, 4, QImage::Format_ARGB32); patch.fill(Qt::transparent);
	patch.setPixel(0, 0, qRgb(7, 7, 7)); patch.setPixel(0, 1, qRgb(8, 8, 8)); patch.setPixel(0, 3, qRgb(9, 9, 9)); patch.setPixel(1, 1, qRgb(10, 10, 10)); patch.setPixel(1, 2, qRgb(11, 11, 11));
	const auto patchOutput = encodeTextureExport(patch, options, palette); const auto patchDecoded = decodeIdTechImage(QStringLiteral("PATCH"), patchOutput.bytes, palette.palette);
	ok &= expect(patchOutput.succeeded && patchOutput.bytes == QByteArray::fromHex("02000400feff0500100000001c0000000002000708000301000900ff0102000a0b00ff"), "patch matches a complete independently specified two-column binary fixture");
	ok &= expect(patchDecoded.decoded && patchDecoded.image == patch && patchDecoded.leftOffset == -2 && patchDecoded.topOffset == 5, "patch transparency and offsets round trip");
	patch.setPixel(0, 0, qRgba(7, 7, 7, 127)); ok &= expect(!encodeTextureExport(patch, options, palette).succeeded, "strict patches reject partial alpha"); options.alpha = TextureExportAlpha::Threshold;
	const auto thresholded = encodeTextureExport(patch, options, palette);
	ok &= expect(thresholded.succeeded && thresholded.alphaChangedPixels == 1 && thresholded.preview.pixelColor(0, 0).alpha() == 0 && decodeIdTechImage(QStringLiteral("PATCH"), thresholded.bytes, palette.palette).image.pixelColor(0, 0).alpha() == 0, "threshold preview agrees with encoded transparency"); options.alpha = TextureExportAlpha::Strict;
	QImage tall(2, 2048, QImage::Format_ARGB32); tall.fill(Qt::transparent);
	for (int y = 0; y < tall.height(); ++y) { if (y % 2 == 0) { tall.setPixel(0, y, qRgb(200, 200, 200)); } tall.setPixel(1, y, qRgb(255, 255, 255)); }
	ok &= expect(!encodeTextureExport(tall, options, palette).succeeded, "tall posts require explicit source-port limits"); options.extendedLimits = true; const auto tallOutput = encodeTextureExport(tall, options, palette);
	ok &= expect(tallOutput.succeeded && decodeIdTechImage(QStringLiteral("TALL"), tallOutput.bytes, palette.palette).image == tall, "tall posts support gaps, starts around 254/255, long runs and over 512 posts");
	QImage far(1, 4096, QImage::Format_ARGB32); far.fill(Qt::transparent); far.setPixel(0, 4095, qRgb(12, 12, 12)); const auto farOutput = encodeTextureExport(far, options, palette);
	ok &= expect(farOutput.succeeded && farOutput.warnings.size() >= 2 && decodeIdTechImage(QStringLiteral("FAR"), farOutput.bytes, palette.palette).image == far, "empty tall posts reach the final row and report narrower port limits");
	TextureExportOptions parsed; const auto json = textureExportOptionsJson(options);
	ok &= expect(textureExportOptionsFromJson(json, &parsed, &error) && textureExportOptionsJson(parsed) == json, "versioned options round trip");
	for (const QJsonObject& invalid : {QJsonObject{{"version", 2}}, QJsonObject{{"profile", "unknown"}}, QJsonObject{{"dither", 1}}, QJsonObject{{"surfaceFlags", -1}}, QJsonObject{{"leftOffset", 32768}}, QJsonObject{{"alphaThreshold", 0}}, QJsonObject{{"matte", "#00ffffff"}}, QJsonObject{{"fullbright", "invalid"}}, QJsonObject{{"surfacFlags", 1}}}) {
		const auto before = textureExportOptionsJson(parsed); ok &= expect(!textureExportOptionsFromJson(invalid, &parsed, &error) && textureExportOptionsJson(parsed) == before, "malformed or misspelled options never partially replace configuration");
	}
	options = {}; options.format = TextureExportFormat::QuakeMiptex; options.name = QStringLiteral("../unsafe"); ok &= expect(!encodeTextureExport(texture, options, palette).succeeded, "unsafe texture names fail");
	options.name = QStringLiteral("sky1"); ok &= expect(!encodeTextureExport(texture, options, palette).succeeded, "classic sky dimensions are checked"); options.name = QStringLiteral("texture"); ok &= expect(!encodeTextureExport(odd, options, palette).succeeded, "mip dimensions must be 16 aligned");
	options.format = TextureExportFormat::Quake2Wal; const auto large = indexedImage({512, 512}, palette, 7);
	ok &= expect(!encodeTextureExport(large, options, palette).succeeded, "original WAL loader pixel budget is enforced"); options.extendedLimits = true; ok &= expect(encodeTextureExport(large, options, palette).succeeded, "extended limits permit larger WAL output");
	for (const auto& profile : textureExportProfiles()) {
		TextureExportOptions cancelOptions; cancelOptions.format = profile.format; const auto input = profile.format == TextureExportFormat::DoomFlat ? flat : indexedImage({128, 128}, palette, 7);
		for (int stop : {0, 40, 100}) {
			double previous = 0; bool monotonic = true; int calls = 0;
			const auto cancelled = encodeTextureExport(input, cancelOptions, palette, [&](qint64 done, qint64 total) { ++calls; const double fraction = double(done) / total; monotonic &= fraction >= previous; previous = fraction; return fraction * 100 < stop; });
			ok &= expect(!cancelled.succeeded && !cancelled.error.isEmpty() && monotonic && calls > 0, "every exporter cancels at start, after partial work and before publication with monotonic progress");
		}
	}
	options = {}; options.format = TextureExportFormat::Targa; const auto destination = root.filePath(QStringLiteral("written.tga"));
	ok &= expect(saveTextureExport(rgba, options, {}, destination, false, true, nullptr, &error) && !QFileInfo::exists(destination), "export dry-run creates no file");
	ok &= expect(saveTextureExport(rgba, options, {}, destination, false, false, nullptr, &error) && read(destination) == targa.bytes && !saveTextureExport(rgba, options, {}, destination, false, false, nullptr, &error), "native export shares guarded publication and explicit overwrite");
	bool changed = false;
	ok &= expect(!saveTextureExport(rgba, options, {}, destination, true, false, nullptr, &error, [&](qint64 done, qint64 total) { if (done == total && !changed) { changed = true; put(destination, "outside changes"); } return true; }) && read(destination) == "outside changes", "destination fingerprint precedes encoding and rejects intervening edits");
	const QDir evidence(qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT"));
	if (!qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT").isEmpty()) {
		auto colored = palette; for (int index = 0; index < 256; ++index) { colored.palette.colors[index] = qRgb(index, (index * 3) % 256, 255 - index); }
		auto colorImage = odd; colorImage.setColorTable(colored.palette.colors); TextureExportOptions pcxOptions; pcxOptions.format = TextureExportFormat::Pcx;
		const auto colorPcx = encodeTextureExport(colorImage, pcxOptions, colored);
		ok &= expect(colorPcx.succeeded && put(evidence.filePath(QStringLiteral("export-color.pcx")), colorPcx.bytes) && put(evidence.filePath(QStringLiteral("export-indexed-gray.png")), grayPng.bytes) && put(evidence.filePath(QStringLiteral("export-indexed-alpha.png")), alphaPng.bytes), "write indexed palette fixtures for independent color and alpha verification");
		ok &= expect(put(evidence.filePath(QStringLiteral("export-rgba.png")), png.bytes) && put(evidence.filePath(QStringLiteral("export-rgba.tga")), targa.bytes) && put(evidence.filePath(QStringLiteral("export-clear.tga")), clearTarga.bytes) && put(evidence.filePath(QStringLiteral("export-odd.pcx")), oddPcx.bytes) && put(evidence.filePath(QStringLiteral("export-runs.pcx")), pcx.bytes), "write synthetic fixtures for independent reader verification");
	}
	QElapsedTimer timer; timer.start(); options = {}; options.format = TextureExportFormat::QuakeMiptex; options.extendedLimits = true;
	ok &= expect(encodeTextureExport(indexedImage({2048, 2048}, palette, 7), options, palette).succeeded, "representative mip export succeeds"); std::cout << "2048 x 2048 indexed mip export: " << timer.elapsed() << " ms\n";
	if (argc > 1) {
		const auto cli = [&](QStringList args, int expected, QJsonObject* report = nullptr) {
			QProcess process; args.prepend(QStringLiteral("--cli")); args.append(QStringLiteral("--json")); process.start(QString::fromLocal8Bit(argv[1]), args);
			if (!process.waitForFinished(20000)) { process.kill(); process.waitForFinished(); std::cerr << "CLI did not finish\n"; return false; }
			QJsonParseError problem; const auto json = QJsonDocument::fromJson(process.readAllStandardOutput(), &problem);
			if (report) { *report = json.object(); }
			const bool passed = process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && problem.error == QJsonParseError::NoError && json.isObject();
			if (!passed) { std::cerr << "CLI: " << args.join(' ').toStdString() << " exit=" << process.exitCode() << " " << json.toJson().toStdString() << process.readAllStandardError().toStdString(); }
			return passed;
		};
		TextureDocument document; document.reset(flat); const auto projectPath = root.filePath(QStringLiteral("input.vtexture")); TextureProjectSaveRequest request; request.path = projectPath;
		options = {}; options.format = TextureExportFormat::Quake2Wal; options.name = QStringLiteral("custom/wall"); options.surfaceFlags = 0xf0000001u; options.surfaceValue = -3;
		ok &= expect(writeTextureProject(document, request, {{QStringLiteral("palette"), texturePaletteMetadata(palette)}, {QStringLiteral("export"), textureExportOptionsJson(options)}}).succeeded, "prepare native project CLI input");
		const auto projectBefore = read(projectPath); QJsonObject report;
		ok &= expect(cli({"texture", "profiles"}, 0, &report) && report.value("profiles").toArray().size() == 11, "CLI lists all eleven profiles with defaults");
		ok &= expect(cli({"texture", "validate", projectPath}, 0, &report) && report.value("export").toObject().value("options").toObject().value("surfaceFlags").toDouble() == double(0xf0000001u) && !report.value("written").toBool(), "CLI validates persisted native metadata without an output file");
		const auto config = root.filePath(QStringLiteral("export.json")); put(config, R"({"name":"wall","alpha":"strict"})");
		for (const auto& profile : textureExportProfiles()) {
			const auto output = root.filePath(profile.id + QLatin1Char('.') + profile.suffix);
			const QStringList args{"texture", "export", "--export-options", config, "--profile", profile.id, projectPath, "--output", output};
			ok &= expect(cli(args + QStringList{"--dry-run"}, 0, &report) && !QFileInfo::exists(output) && !report.value("written").toBool(), "every CLI profile dry-run encodes without writes");
			ok &= expect(cli(args, 0, &report) && QFileInfo::exists(output) && report.value("written").toBool(), "every CLI profile writes validated bytes");
			const auto before = read(output); ok &= expect(cli(args, 4) && read(output) == before, "every CLI profile refuses implicit replacement");
		}
		ok &= expect(read(projectPath) == projectBefore, "CLI exports leave layered source bytes intact");
		put(config, R"({"surfaceFlags":-1})"); ok &= expect(cli({"texture", "validate", projectPath, "--export-options", config}, 2), "CLI rejects malformed metadata overrides");
		ok &= expect(cli({"texture", "validate", projectPath, "--profile", "absent"}, 2) && cli({"texture", "validate", projectPath, "--output", root.filePath("ignored.wal")}, 2), "validation rejects unknown profiles and ignored output paths");
		const auto raster = root.filePath(QStringLiteral("input.png")); flat.save(raster);
		ok &= expect(cli({"texture", "validate", raster, "--profile", "doom-flat"}, 4) && cli({"texture", "validate", "--allow-generated-palette", raster, "--profile", "doom-flat"}, 0), "generated palette acceptance is explicit and flags do not consume the input");
		QByteArray rgb; for (int i = 0; i < 256; ++i) { rgb += QByteArray(3, char(i)); } const auto palettePath = root.filePath("palette.lmp"); put(palettePath, rgb);
		ok &= expect(cli({"texture", "validate", "--palette-file", palettePath, raster, "--profile", "doom-flat"}, 0, &report) && !report.value("export").toObject().value("palette").toObject().value("generated").toBool(), "explicit palette files resolve actual palette provenance before export");
		ok &= expect(cli({"texture", "validate", "--package", wadPath, "--entry", "tex", "--profile", "quake-miptex", "--palette-file", palettePath}, 0), "native package entries share the CLI export pipeline");
		ok &= expect(cli({"texture", "export", wadPath, "tex", "--output", root.filePath("legacy.png")}, 0), "legacy two-positional decode alias remains usable");
		const auto recipe = root.filePath("empty.json"); put(recipe, "[]"); const auto importedProject = root.filePath("imported.vtexture");
		ok &= expect(cli({"texture", "edit", root.filePath("quake2-wal.wal"), "--operations", recipe, "--output", importedProject}, 0), "CLI native editing writes layered projects");
		QJsonObject importedMetadata; TextureDocument importedDocument;
		ok &= expect(readTextureProject(importedProject, &importedDocument, nullptr, &importedMetadata) && importedMetadata.value("export").toObject().value("surfaceFlags").toDouble() == double(0xf0000001u), "CLI native editing retains export metadata in the resulting project");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
