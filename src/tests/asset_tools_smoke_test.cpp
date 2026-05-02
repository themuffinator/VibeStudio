#include "core/asset_tools.h"
#include "core/package_archive.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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

QByteArray readFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return file.readAll();
}

void appendLe16(QByteArray* data, quint16 value)
{
	data->append(static_cast<char>(value & 0xff));
	data->append(static_cast<char>((value >> 8) & 0xff));
}

void appendLe32(QByteArray* data, quint32 value)
{
	data->append(static_cast<char>(value & 0xff));
	data->append(static_cast<char>((value >> 8) & 0xff));
	data->append(static_cast<char>((value >> 16) & 0xff));
	data->append(static_cast<char>((value >> 24) & 0xff));
}

QByteArray wavFixture()
{
	const QByteArray samples = QByteArray::fromHex("0000ff7f00800100");
	QByteArray data;
	data.append("RIFF");
	appendLe32(&data, static_cast<quint32>(36 + samples.size()));
	data.append("WAVE");
	data.append("fmt ");
	appendLe32(&data, 16);
	appendLe16(&data, 1);
	appendLe16(&data, 1);
	appendLe32(&data, 11025);
	appendLe32(&data, 11025 * 2);
	appendLe16(&data, 2);
	appendLe16(&data, 16);
	data.append("data");
	appendLe32(&data, static_cast<quint32>(samples.size()));
	data.append(samples);
	return data;
}

} // namespace

int main()
{
	bool ok = true;
	QTemporaryDir tempDir;
	ok &= expect(tempDir.isValid(), "temporary directory should be valid");
	QDir root(tempDir.path());
	ok &= expect(root.mkpath(QStringLiteral("package/textures")), "package textures directory should be created");
	ok &= expect(root.mkpath(QStringLiteral("package/sound")), "package sound directory should be created");
	ok &= expect(root.mkpath(QStringLiteral("project/scripts")), "project scripts directory should be created");

	QImage image(4, 4, QImage::Format_ARGB32);
	image.fill(qRgba(80, 40, 20, 255));
	ok &= expect(image.save(root.filePath(QStringLiteral("package/textures/wall.png")), "PNG"), "image fixture should be written");
	ok &= expect(writeFile(root.filePath(QStringLiteral("package/sound/pickup.wav")), wavFixture()), "WAV fixture should be written");
	ok &= expect(writeFile(root.filePath(QStringLiteral("project/scripts/autoexec.cfg")), QByteArray("set developer 1\nbind SPACE +jump\n")), "CFG fixture should be written");

	PackageArchive archive;
	QString error;
	ok &= expect(archive.load(root.filePath(QStringLiteral("package")), &error), "folder package should load");

	AssetImageConversionRequest dryImageRequest;
	dryImageRequest.virtualPaths = {QStringLiteral("textures/wall.png")};
	dryImageRequest.outputDirectory = root.filePath(QStringLiteral("converted-dry"));
	dryImageRequest.outputFormat = QStringLiteral("bmp");
	dryImageRequest.resizeSize = QSize(2, 2);
	dryImageRequest.paletteMode = QStringLiteral("grayscale");
	dryImageRequest.dryRun = true;
	const AssetImageConversionReport dryImageReport = convertPackageImages(archive, dryImageRequest);
	ok &= expect(dryImageReport.succeeded(), "dry-run image conversion should succeed");
	ok &= expect(dryImageReport.processedCount == 1, "dry-run image conversion count mismatch");
	ok &= expect(!QFileInfo::exists(root.filePath(QStringLiteral("converted-dry/textures/wall.bmp"))), "dry-run image conversion should not write output");
	ok &= expect(!dryImageReport.entries.isEmpty() && dryImageReport.entries.front().afterSize == QSize(2, 2), "dry-run image conversion resize preview mismatch");

	AssetImageConversionRequest writeImageRequest = dryImageRequest;
	writeImageRequest.outputDirectory = root.filePath(QStringLiteral("converted"));
	writeImageRequest.dryRun = false;
	const AssetImageConversionReport writeImageReport = convertPackageImages(archive, writeImageRequest);
	ok &= expect(writeImageReport.succeeded(), "write image conversion should succeed");
	ok &= expect(writeImageReport.writtenCount == 1, "write image conversion count mismatch");
	ok &= expect(QFileInfo::exists(root.filePath(QStringLiteral("converted/textures/wall.bmp"))), "converted image should exist");

	const QString wavOutput = root.filePath(QStringLiteral("pickup-copy.wav"));
	const AssetAudioExportReport dryAudioReport = exportPackageAudioToWav(archive, QStringLiteral("sound/pickup.wav"), wavOutput, true, false);
	ok &= expect(dryAudioReport.succeeded() && !QFileInfo::exists(wavOutput), "dry-run WAV export should not write output");
	const AssetAudioExportReport writeAudioReport = exportPackageAudioToWav(archive, QStringLiteral("sound/pickup.wav"), wavOutput, false, false);
	ok &= expect(writeAudioReport.succeeded() && QFileInfo::exists(wavOutput), "WAV export should write output");
	ok &= expect(readFile(wavOutput) == wavFixture(), "WAV export bytes should match source");

	AssetTextSearchRequest searchRequest;
	searchRequest.rootPath = root.filePath(QStringLiteral("project"));
	searchRequest.findText = QStringLiteral("developer");
	const AssetTextSearchReport searchReport = findReplaceProjectText(searchRequest);
	ok &= expect(searchReport.succeeded(), "project text search should succeed");
	ok &= expect(searchReport.matchCount == 1, "project text search match count mismatch");
	ok &= expect(searchReport.saveState == QStringLiteral("clean"), "project text search save state mismatch");

	AssetTextSearchRequest replaceDryRequest = searchRequest;
	replaceDryRequest.replace = true;
	replaceDryRequest.replaceText = QStringLiteral("sv_cheats");
	replaceDryRequest.dryRun = true;
	const AssetTextSearchReport replaceDryReport = findReplaceProjectText(replaceDryRequest);
	ok &= expect(replaceDryReport.succeeded(), "dry-run project text replace should succeed");
	ok &= expect(replaceDryReport.replacementCount == 1, "dry-run project text replace count mismatch");
	ok &= expect(replaceDryReport.saveState == QStringLiteral("modified"), "dry-run project text replace save state mismatch");
	ok &= expect(readFile(root.filePath(QStringLiteral("project/scripts/autoexec.cfg"))).contains("developer"), "dry-run project text replace should not write");

	AssetTextSearchRequest replaceWriteRequest = replaceDryRequest;
	replaceWriteRequest.dryRun = false;
	const AssetTextSearchReport replaceWriteReport = findReplaceProjectText(replaceWriteRequest);
	ok &= expect(replaceWriteReport.succeeded(), "write project text replace should succeed");
	ok &= expect(replaceWriteReport.saveState == QStringLiteral("saved"), "write project text replace save state mismatch");
	ok &= expect(readFile(root.filePath(QStringLiteral("project/scripts/autoexec.cfg"))).contains("sv_cheats"), "write project text replace should update file");

	return ok ? 0 : 1;
}
