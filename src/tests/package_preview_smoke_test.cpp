#include "core/asset_tools.h"
#include "core/idtech_image.h"
#include "core/package_archive.h"
#include "core/package_preview.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

bool writeFile(const QString& path, const QByteArray& data)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
	return file.write(data) == data.size();
}

void writeLe16(QByteArray* data, qsizetype offset, quint16 value)
{
	(*data)[offset] = static_cast<char>(value & 0xff);
	(*data)[offset + 1] = static_cast<char>((value >> 8) & 0xff);
}

void writeLe32(QByteArray* data, qsizetype offset, quint32 value)
{
	(*data)[offset] = static_cast<char>(value & 0xff);
	(*data)[offset + 1] = static_cast<char>((value >> 8) & 0xff);
	(*data)[offset + 2] = static_cast<char>((value >> 16) & 0xff);
	(*data)[offset + 3] = static_cast<char>((value >> 24) & 0xff);
}

QByteArray wavFixture()
{
	const QByteArray samples = QByteArray::fromHex("0000ff7f00800100");
	QByteArray data(44, '\0');
	data.replace(0, 4, "RIFF");
	writeLe32(&data, 4, static_cast<quint32>(36 + samples.size()));
	data.replace(8, 4, "WAVE");
	data.replace(12, 4, "fmt ");
	writeLe32(&data, 16, 16);
	writeLe16(&data, 20, 1);
	writeLe16(&data, 22, 1);
	writeLe32(&data, 24, 22050);
	writeLe32(&data, 28, 22050 * 2);
	writeLe16(&data, 32, 2);
	writeLe16(&data, 34, 16);
	data.replace(36, 4, "data");
	writeLe32(&data, 40, static_cast<quint32>(samples.size()));
	data.append(samples);
	return data;
}

QByteArray md2Fixture()
{
	QByteArray data(68, '\0');
	data.replace(0, 4, "IDP2");
	writeLe32(&data, 4, 8);
	writeLe32(&data, 8, 64);
	writeLe32(&data, 12, 64);
	writeLe32(&data, 16, 40);
	writeLe32(&data, 20, 1);
	writeLe32(&data, 24, 3);
	writeLe32(&data, 32, 1);
	writeLe32(&data, 40, 1);
	writeLe32(&data, 44, 68);
	writeLe32(&data, 56, 132);
	writeLe32(&data, 64, 172);
	QByteArray skin(64, '\0');
	skin.replace(0, 22, "models/player/skin.pcx");
	data.append(skin);
	QByteArray frame(40, '\0');
	frame.replace(24, 4, "idle");
	data.append(frame);
	return data;
}

// An MD3 that declares three surfaces but only carries one, so the surface walk
// has to admit the counts are partial instead of reporting them as totals.
// md3Header_t/md3Surface_t layout: Quake III md3.h.
QByteArray truncatedMd3Fixture()
{
	QByteArray header(108, '\0');
	header.replace(0, 4, "IDP3");
	writeLe32(&header, 4, 15); // MD3_VERSION
	header.replace(8, 11, "models/test");
	writeLe32(&header, 76, 1); // numFrames
	writeLe32(&header, 80, 0); // numTags
	writeLe32(&header, 84, 3); // numSurfaces
	writeLe32(&header, 88, 0); // numSkins
	writeLe32(&header, 92, 108); // ofsFrames
	writeLe32(&header, 96, 108); // ofsTags
	writeLe32(&header, 100, 108); // ofsSurfaces
	writeLe32(&header, 104, 216); // ofsEnd

	QByteArray surface(108, '\0');
	surface.replace(0, 4, "IDP3");
	surface.replace(4, 5, "torso");
	writeLe32(&surface, 72, 1); // numFrames
	writeLe32(&surface, 76, 1); // numShaders
	writeLe32(&surface, 80, 10); // numVerts
	writeLe32(&surface, 84, 6); // numTriangles
	writeLe32(&surface, 104, 108); // ofsEnd

	return header + surface;
}

// Quake "qpic" lump: int width, int height, byte indices[width * height].
QByteArray quakeLumpFixture(int width, int height)
{
	QByteArray data;
	data.resize(8);
	writeLe32(&data, 0, static_cast<quint32>(width));
	writeLe32(&data, 4, static_cast<quint32>(height));
	for (int index = 0; index < width * height; ++index) {
		data.append(static_cast<char>(index % 200));
	}
	return data;
}

} // namespace

int main()
{
	bool ok = true;

	QTemporaryDir root;
	ok &= expect(root.isValid(), "temporary package root should be valid");
	QDir dir(root.path());
	ok &= expect(dir.mkpath(QStringLiteral("textures")), "textures directory should be created");
	ok &= expect(dir.mkpath(QStringLiteral("scripts")), "scripts directory should be created");
	ok &= expect(dir.mkpath(QStringLiteral("sound")), "sound directory should be created");
	ok &= expect(dir.mkpath(QStringLiteral("models")), "models directory should be created");
	ok &= expect(writeFile(dir.filePath(QStringLiteral("readme.txt")), QByteArray("hello preview\nsecond line")), "text fixture should be written");
	ok &= expect(writeFile(dir.filePath(QStringLiteral("scripts/autoexec.cfg")), QByteArray("bind SPACE +jump\n// WARNING fixture\n")), "CFG fixture should be written");
	ok &= expect(writeFile(dir.filePath(QStringLiteral("sound/pickup.wav")), wavFixture()), "WAV fixture should be written");
	ok &= expect(writeFile(dir.filePath(QStringLiteral("models/player.md2")), md2Fixture()), "MD2 fixture should be written");
	ok &= expect(writeFile(dir.filePath(QStringLiteral("models/partial.md3")), truncatedMd3Fixture()), "MD3 fixture should be written");
	ok &= expect(writeFile(dir.filePath(QStringLiteral("binary.dat")), QByteArray::fromHex("00010203fffefd")), "binary fixture should be written");

	QImage image(2, 3, QImage::Format_ARGB32);
	image.fill(qRgba(10, 20, 30, 255));
	ok &= expect(image.save(dir.filePath(QStringLiteral("textures/tiny.png")), "PNG"), "image fixture should be written");

	ok &= expect(dir.mkpath(QStringLiteral("gfx")), "gfx directory should be created");
	const QByteArray lumpBytes = quakeLumpFixture(24, 12);
	ok &= expect(writeFile(dir.filePath(QStringLiteral("gfx/menu.lmp")), lumpBytes), "LMP fixture should be written");

	// Drop a real 768-byte palette where the resolver looks for it, so the
	// preview must report a package palette rather than the generated fallback.
	QString paletteCandidate;
	for (const QString& candidate : idTechPaletteCandidatePaths(defaultIdTechPaletteIdForFormat(IdTechImageFormat::QuakeLump))) {
		if (candidate.endsWith(QStringLiteral(".lmp"), Qt::CaseInsensitive)) {
			paletteCandidate = candidate;
			break;
		}
	}
	if (!paletteCandidate.isEmpty()) {
		const int slash = paletteCandidate.lastIndexOf('/');
		if (slash > 0) {
			ok &= expect(dir.mkpath(paletteCandidate.left(slash)), "palette directory should be created");
		}
		QByteArray paletteBytes;
		for (int index = 0; index < 256; ++index) {
			paletteBytes.append(static_cast<char>(index));
			paletteBytes.append(static_cast<char>((index * 5) % 256));
			paletteBytes.append(static_cast<char>((index * 9) % 256));
		}
		ok &= expect(writeFile(dir.filePath(paletteCandidate), paletteBytes), "palette fixture should be written");
	}

	PackageArchive archive;
	QString error;
	ok &= expect(archive.load(root.path(), &error), "folder package should load");

	const PackagePreview textPreview = buildPackageEntryPreview(archive, QStringLiteral("readme.txt"));
	ok &= expect(textPreview.kind == PackagePreviewKind::Text, "text preview kind mismatch");
	ok &= expect(textPreview.body.contains(QStringLiteral("hello preview")), "text preview body mismatch");
	ok &= expect(!textPreview.detailLines.isEmpty(), "text preview details missing");

	const PackagePreview truncatedTextPreview = buildPackageEntryPreview(archive, QStringLiteral("readme.txt"), 5);
	ok &= expect(truncatedTextPreview.kind == PackagePreviewKind::Text, "truncated text preview kind mismatch");
	ok &= expect(truncatedTextPreview.truncated, "text preview truncation should be reported");
	ok &= expect(truncatedTextPreview.bytesRead == 5, "text preview truncation byte count mismatch");

	const PackagePreview imagePreview = buildPackageEntryPreview(archive, QStringLiteral("textures/tiny.png"));
	ok &= expect(imagePreview.kind == PackagePreviewKind::Image, "image preview kind mismatch");
	ok &= expect(imagePreview.imageSize == QSize(2, 3), "image preview size mismatch");
	ok &= expect(!imagePreview.imageFormat.isEmpty(), "image preview format missing");
	ok &= expect(imagePreview.imageDepth > 0, "image preview depth missing");

	const PackagePreview cfgPreview = buildPackageEntryPreview(archive, QStringLiteral("scripts/autoexec.cfg"));
	ok &= expect(cfgPreview.kind == PackagePreviewKind::Text, "CFG preview kind mismatch");
	ok &= expect(cfgPreview.textLanguageId == QStringLiteral("cfg"), "CFG language id mismatch");
	ok &= expect(!cfgPreview.textHighlightLines.isEmpty(), "CFG highlights should be populated");
	ok &= expect(!cfgPreview.textDiagnosticLines.isEmpty(), "CFG diagnostics should be populated");

	const PackagePreview lumpPreview = buildPackageEntryPreview(archive, QStringLiteral("gfx/menu.lmp"));
	ok &= expect(lumpPreview.kind == PackagePreviewKind::Image, "LMP preview kind mismatch");
	ok &= expect(lumpPreview.imageIdTechFormat, "LMP preview should report an idTech format");
	ok &= expect(lumpPreview.imageSize == QSize(24, 12), "LMP preview size mismatch");
	ok &= expect(!lumpPreview.imagePixels.isNull(), "LMP preview should carry decoded pixels");
	ok &= expect(lumpPreview.imagePixels.size() == QSize(24, 12), "LMP preview pixel size mismatch");
	ok &= expect(lumpPreview.imagePaletteAware, "LMP preview should be palette-aware");
	ok &= expect(!lumpPreview.imagePaletteId.isEmpty(), "LMP preview should name the palette it used");
	if (!paletteCandidate.isEmpty()) {
		ok &= expect(lumpPreview.imagePaletteFromPackage, "LMP preview should use the palette shipped in the package");
		ok &= expect(!lumpPreview.imagePaletteGenerated, "a package palette must not be reported as generated");
		ok &= expect(!lumpPreview.imagePaletteResolutionLines.isEmpty(), "palette resolution lines should be populated");
	}

	// An image entry must still decode when the generic sampling limit is tiny.
	const PackagePreview smallLimitImagePreview = buildPackageEntryPreview(archive, QStringLiteral("gfx/menu.lmp"), 8);
	ok &= expect(smallLimitImagePreview.kind == PackagePreviewKind::Image, "byte-limited image preview kind mismatch");
	ok &= expect(smallLimitImagePreview.bytesRead == lumpBytes.size(), "an image preview should read the whole entry");
	ok &= expect(!smallLimitImagePreview.imagePixels.isNull(), "a byte-limited image preview should still decode pixels");
	ok &= expect(!smallLimitImagePreview.truncated, "an image preview read in full is not truncated");

	// Non-image entries keep the generic byte limit.
	const PackagePreview smallLimitBinaryPreview = buildPackageEntryPreview(archive, QStringLiteral("models/player.md2"), 16);
	ok &= expect(smallLimitBinaryPreview.bytesRead == 16, "non-image previews should honour the byte limit");
	ok &= expect(smallLimitBinaryPreview.truncated, "non-image truncation should still be reported");

	const PackagePreview wavPreview = buildPackageEntryPreview(archive, QStringLiteral("sound/pickup.wav"));
	ok &= expect(wavPreview.kind == PackagePreviewKind::Audio, "WAV preview kind mismatch");
	ok &= expect(wavPreview.audioFormat == QStringLiteral("WAV"), "WAV preview format mismatch");
	ok &= expect(!wavPreview.audioWaveformLines.isEmpty(), "WAV waveform should be populated");
	ok &= expect(wavPreview.audioPeaks.valid, "WAV preview should carry decoded peaks");
	ok &= expect(wavPreview.audioPeaks.channels == 1, "WAV preview peak channel count mismatch");
	ok &= expect(wavPreview.audioChannels == 1, "WAV preview channel count mismatch");
	ok &= expect(wavPreview.audioSampleRate == 22050, "WAV preview sample rate mismatch");
	ok &= expect(wavPreview.audioBitsPerSample == 16, "WAV preview bit depth mismatch");
	ok &= expect(wavPreview.audioPeaks.frameCount == 4, "WAV preview frame count mismatch");

	const PackagePreview modelPreview = buildPackageEntryPreview(archive, QStringLiteral("models/player.md2"));
	ok &= expect(modelPreview.kind == PackagePreviewKind::Model, "MD2 preview kind mismatch");
	ok &= expect(modelPreview.modelFormat == QStringLiteral("MD2"), "MD2 model format mismatch");
	ok &= expect(!modelPreview.modelViewportLines.isEmpty(), "MD2 viewport metadata should be populated");
	ok &= expect(!modelPreview.modelMaterialLines.isEmpty(), "MD2 material metadata should be populated");
	ok &= expect(!modelPreview.modelCountsPartial, "a complete MD2 should not be flagged as partial");

	const PackagePreview md3Preview = buildPackageEntryPreview(archive, QStringLiteral("models/partial.md3"));
	ok &= expect(md3Preview.kind == PackagePreviewKind::Model, "MD3 preview kind mismatch");
	ok &= expect(md3Preview.modelFormat == QStringLiteral("MD3"), "MD3 model format mismatch");
	ok &= expect(md3Preview.modelCountsPartial, "a short MD3 surface walk must be reported as partial");
	bool sawPartialNote = false;
	for (const QString& line : md3Preview.modelMaterialLines) {
		if (line.contains(QStringLiteral("Surface walk incomplete"))) {
			sawPartialNote = true;
		}
	}
	ok &= expect(sawPartialNote, "the MD3 preview should say the surface walk is incomplete");

	const PackagePreview binaryPreview = buildPackageEntryPreview(archive, QStringLiteral("binary.dat"));
	ok &= expect(binaryPreview.kind == PackagePreviewKind::Binary, "binary preview kind mismatch");
	ok &= expect(binaryPreview.body.contains(QStringLiteral("00 01 02 03")), "binary preview hex dump missing");

	const PackagePreview directoryPreview = buildPackageEntryPreview(archive, QStringLiteral("textures"));
	ok &= expect(directoryPreview.kind == PackagePreviewKind::Directory, "directory preview kind mismatch");

	const PackagePreview missingPreview = buildPackageEntryPreview(archive, QStringLiteral("missing.txt"));
	ok &= expect(missingPreview.kind == PackagePreviewKind::Unavailable, "missing preview should be unavailable");
	ok &= expect(!missingPreview.error.isEmpty(), "missing preview error should be populated");

	return ok ? 0 : 1;
}
