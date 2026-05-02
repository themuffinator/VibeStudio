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

} // namespace

int main()
{
	bool ok = true;

	QTemporaryDir root;
	ok &= expect(root.isValid(), "temporary package root should be valid");
	QDir dir(root.path());
	ok &= expect(dir.mkpath(QStringLiteral("textures")), "textures directory should be created");
	ok &= expect(writeFile(dir.filePath(QStringLiteral("readme.txt")), QByteArray("hello preview\nsecond line")), "text fixture should be written");
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
