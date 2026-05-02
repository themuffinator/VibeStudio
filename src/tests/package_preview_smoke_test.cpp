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
	ok &= expect(writeFile(dir.filePath(QStringLiteral("binary.dat")), QByteArray::fromHex("00010203fffefd")), "binary fixture should be written");

	QImage image(2, 3, QImage::Format_ARGB32);
	image.fill(qRgba(10, 20, 30, 255));
	ok &= expect(image.save(dir.filePath(QStringLiteral("textures/tiny.png")), "PNG"), "image fixture should be written");

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

	const PackagePreview wavPreview = buildPackageEntryPreview(archive, QStringLiteral("sound/pickup.wav"));
	ok &= expect(wavPreview.kind == PackagePreviewKind::Audio, "WAV preview kind mismatch");
	ok &= expect(wavPreview.audioFormat == QStringLiteral("WAV"), "WAV preview format mismatch");
	ok &= expect(!wavPreview.audioWaveformLines.isEmpty(), "WAV waveform should be populated");

	const PackagePreview modelPreview = buildPackageEntryPreview(archive, QStringLiteral("models/player.md2"));
	ok &= expect(modelPreview.kind == PackagePreviewKind::Model, "MD2 preview kind mismatch");
	ok &= expect(modelPreview.modelFormat == QStringLiteral("MD2"), "MD2 model format mismatch");
	ok &= expect(!modelPreview.modelViewportLines.isEmpty(), "MD2 viewport metadata should be populated");
	ok &= expect(!modelPreview.modelMaterialLines.isEmpty(), "MD2 material metadata should be populated");

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
