#include "core/package_preview.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QStringDecoder>

#include <algorithm>
#include <limits>
#include <optional>

namespace vibestudio {

namespace {

QString previewText(const char* source)
{
	return QCoreApplication::translate("VibeStudioPackagePreview", source);
}

std::optional<PackageEntry> findEntry(const PackageArchive& archive, const QString& virtualPath)
{
	const PackageVirtualPath normalized = normalizePackageVirtualPath(virtualPath, false);
	if (!normalized.isSafe()) {
		return std::nullopt;
	}
	const QVector<PackageEntry> entries = archive.entries();
	for (const PackageEntry& entry : entries) {
		if (entry.virtualPath.compare(normalized.normalizedPath, Qt::CaseInsensitive) == 0) {
			return entry;
		}
	}
	return std::nullopt;
}

bool extensionUsuallyText(const QString& virtualPath)
{
	const QString ext = QFileInfo(virtualPath).suffix().toLower();
	return QStringList {
		QStringLiteral("txt"),
		QStringLiteral("cfg"),
		QStringLiteral("shader"),
		QStringLiteral("map"),
		QStringLiteral("def"),
		QStringLiteral("ent"),
		QStringLiteral("json"),
		QStringLiteral("xml"),
		QStringLiteral("log"),
		QStringLiteral("lst"),
		QStringLiteral("script"),
		QStringLiteral("arena"),
		QStringLiteral("skin"),
	}.contains(ext);
}

bool extensionUsuallyImage(const QString& virtualPath)
{
	const QString ext = QFileInfo(virtualPath).suffix().toLower();
	return QStringList {
		QStringLiteral("png"),
		QStringLiteral("jpg"),
		QStringLiteral("jpeg"),
		QStringLiteral("bmp"),
		QStringLiteral("gif"),
		QStringLiteral("tga"),
		QStringLiteral("webp"),
	}.contains(ext);
}

bool bytesLookTextual(const QByteArray& bytes)
{
	if (bytes.isEmpty()) {
		return true;
	}

	int printable = 0;
	int suspicious = 0;
	for (uchar byte : bytes) {
		if (byte == '\0') {
			++suspicious;
			continue;
		}
		if (byte == '\n' || byte == '\r' || byte == '\t' || (byte >= 0x20 && byte < 0x7f) || byte >= 0x80) {
			++printable;
		} else {
			++suspicious;
		}
	}
	return suspicious == 0 || (static_cast<double>(printable) / static_cast<double>(bytes.size())) >= 0.88;
}

QString decodeUtf8Preview(const QByteArray& bytes, bool* ok)
{
	QStringDecoder decoder(QStringDecoder::Utf8);
	const QString decoded = decoder.decode(bytes);
	if (ok) {
		*ok = !decoder.hasError();
	}
	return decoded;
}

QString hexDump(const QByteArray& bytes)
{
	QStringList lines;
	const int maxBytes = std::min<int>(bytes.size(), 256);
	for (int offset = 0; offset < maxBytes; offset += 16) {
		QStringList hex;
		QString ascii;
		for (int index = 0; index < 16 && offset + index < maxBytes; ++index) {
			const uchar value = static_cast<uchar>(bytes[offset + index]);
			hex << QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
			ascii += (value >= 0x20 && value < 0x7f) ? QChar(value) : QChar('.');
		}
		lines << QStringLiteral("%1  %2  %3")
			.arg(offset, 8, 16, QLatin1Char('0'))
			.arg(hex.join(' ').leftJustified(47, QLatin1Char(' ')))
			.arg(ascii);
	}
	if (bytes.size() > maxBytes) {
		lines << previewText("... truncated after %1 bytes").arg(maxBytes);
	}
	return lines.join('\n');
}

QString sizeText(qint64 bytes)
{
	if (bytes >= 1024ll * 1024ll) {
		return previewText("%1 MiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
	}
	if (bytes >= 1024ll) {
		return previewText("%1 KiB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 2);
	}
	return previewText("%1 B").arg(bytes);
}

PackagePreview unavailablePreview(const QString& virtualPath, const QString& error)
{
	PackagePreview preview;
	preview.virtualPath = virtualPath;
	preview.kind = PackagePreviewKind::Unavailable;
	preview.title = previewText("Preview unavailable");
	preview.summary = error;
	preview.error = error;
	preview.detailLines << previewText("Preview state: unavailable");
	preview.detailLines << previewText("Reason: %1").arg(error);
	preview.rawLines = preview.detailLines;
	return preview;
}

} // namespace

QString packagePreviewKindId(PackagePreviewKind kind)
{
	switch (kind) {
	case PackagePreviewKind::Unavailable:
		return QStringLiteral("unavailable");
	case PackagePreviewKind::Directory:
		return QStringLiteral("directory");
	case PackagePreviewKind::Text:
		return QStringLiteral("text");
	case PackagePreviewKind::Image:
		return QStringLiteral("image");
	case PackagePreviewKind::Binary:
		return QStringLiteral("binary");
	}
	return QStringLiteral("unavailable");
}

QString packagePreviewKindDisplayName(PackagePreviewKind kind)
{
	switch (kind) {
	case PackagePreviewKind::Unavailable:
		return previewText("Unavailable");
	case PackagePreviewKind::Directory:
		return previewText("Directory");
	case PackagePreviewKind::Text:
		return previewText("Text");
	case PackagePreviewKind::Image:
		return previewText("Image");
	case PackagePreviewKind::Binary:
		return previewText("Binary");
	}
	return previewText("Unavailable");
}

PackagePreview buildPackageEntryPreview(const PackageArchive& archive, const QString& virtualPath, qint64 byteLimit)
{
	const std::optional<PackageEntry> maybeEntry = findEntry(archive, virtualPath);
	if (!maybeEntry.has_value()) {
		return unavailablePreview(virtualPath, previewText("Entry not found."));
	}
	const PackageEntry& entry = maybeEntry.value();

	PackagePreview preview;
	preview.virtualPath = entry.virtualPath;
	preview.totalBytes = static_cast<qint64>(std::min<quint64>(entry.sizeBytes, static_cast<quint64>(std::numeric_limits<qint64>::max())));
	preview.truncated = byteLimit >= 0 && preview.totalBytes > byteLimit;

	if (entry.kind == PackageEntryKind::Directory) {
		preview.kind = PackagePreviewKind::Directory;
		preview.title = previewText("Directory");
		preview.summary = previewText("Directory placeholder generated from package entry paths.");
		preview.detailLines << previewText("Path: %1").arg(entry.virtualPath);
		preview.detailLines << previewText("Kind: directory");
		preview.rawLines = preview.detailLines;
		return preview;
	}

	if (!entry.readable) {
		return unavailablePreview(entry.virtualPath, entry.note.isEmpty() ? previewText("Entry bytes are not readable by the current package reader.") : entry.note);
	}

	QByteArray bytes;
	QString error;
	const qint64 readLimit = byteLimit < 0 ? -1 : std::max<qint64>(1, byteLimit);
	if (!archive.readEntryBytes(entry.virtualPath, &bytes, &error, readLimit)) {
		return unavailablePreview(entry.virtualPath, error.isEmpty() ? previewText("Unable to read entry bytes.") : error);
	}

	preview.bytesRead = bytes.size();
	preview.detailLines << previewText("Path: %1").arg(entry.virtualPath);
	preview.detailLines << previewText("Type hint: %1").arg(entry.typeHint);
	preview.detailLines << previewText("Storage: %1").arg(entry.storageMethod.isEmpty() ? previewText("unknown") : entry.storageMethod);
	preview.detailLines << previewText("Bytes read: %1").arg(sizeText(preview.bytesRead));
	preview.detailLines << previewText("Total size: %1").arg(sizeText(preview.totalBytes));
	preview.detailLines << previewText("Truncated: %1").arg(preview.truncated ? previewText("yes") : previewText("no"));

	if (extensionUsuallyImage(entry.virtualPath) || entry.typeHint.startsWith(QStringLiteral("image/"))) {
		QBuffer buffer(&bytes);
		buffer.open(QIODevice::ReadOnly);
		QImageReader reader(&buffer);
		const QSize size = reader.size();
		const QByteArray format = reader.format();
		if (size.isValid() || !format.isEmpty()) {
			preview.kind = PackagePreviewKind::Image;
			preview.title = previewText("Image metadata");
			preview.imageFormat = QString::fromLatin1(format).toUpper();
			preview.imageSize = size;
			preview.summary = size.isValid()
				? previewText("%1 image, %2 x %3").arg(preview.imageFormat.isEmpty() ? previewText("Image") : preview.imageFormat).arg(size.width()).arg(size.height())
				: previewText("%1 image").arg(preview.imageFormat.isEmpty() ? previewText("Image") : preview.imageFormat);
			preview.body = preview.summary;
			preview.detailLines << previewText("Image format: %1").arg(preview.imageFormat.isEmpty() ? previewText("unknown") : preview.imageFormat);
			preview.detailLines << previewText("Image size: %1").arg(size.isValid() ? previewText("%1 x %2").arg(size.width()).arg(size.height()) : previewText("unknown"));
			preview.rawLines = preview.detailLines;
			return preview;
		}
	}

	bool utf8Ok = false;
	const QString decoded = decodeUtf8Preview(bytes, &utf8Ok);
	if (extensionUsuallyText(entry.virtualPath) || (utf8Ok && bytesLookTextual(bytes))) {
		preview.kind = PackagePreviewKind::Text;
		preview.title = previewText("Text preview");
		preview.summary = preview.truncated ? previewText("Text preview truncated at %1.").arg(sizeText(preview.bytesRead)) : previewText("Text preview");
		preview.body = decoded;
		preview.detailLines << previewText("Encoding: UTF-8");
		preview.rawLines = preview.detailLines;
		preview.rawLines << previewText("Preview text follows:");
		preview.rawLines << decoded;
		return preview;
	}

	preview.kind = PackagePreviewKind::Binary;
	preview.title = previewText("Binary metadata");
	preview.summary = previewText("Binary entry, %1 sampled.").arg(sizeText(preview.bytesRead));
	preview.body = hexDump(bytes);
	preview.detailLines << previewText("Binary sample bytes: %1").arg(sizeText(preview.bytesRead));
	preview.rawLines = preview.detailLines;
	preview.rawLines << previewText("Hex sample:");
	preview.rawLines << preview.body;
	return preview;
}

} // namespace vibestudio
