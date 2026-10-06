#include "core/texture_output.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QSaveFile>
#include <QTemporaryFile>
#include <algorithm>

namespace vibestudio {
namespace {
bool fail(QString* error, const QString& message) { if (error) { *error = message; } return false; }

bool cancelled(const std::function<bool()>& isCancelled, QString* error)
{
	if (!isCancelled || !isCancelled()) { return false; }
	fail(error, QCoreApplication::translate("VibeStudioTextureOutput", "Texture export cancelled. No output was written.")); return true;
}

class PngBuffer final : public QBuffer {
public:
	PngBuffer(QByteArray* bytes, const std::function<bool()>& isCancelled) : QBuffer(bytes), m_isCancelled(isCancelled) {}
	QString failure;
protected:
	qint64 writeData(const char* bytes, qint64 length) override
	{
		if (cancelled(m_isCancelled, &failure)) { return -1; }
		if (length < 0 || length > textureOutputByteLimit - pos()) {
			failure = QCoreApplication::translate("VibeStudioTextureOutput", "The encoded texture exceeds 64 MiB."); return -1;
		}
		const qint64 needed = pos() + length;
		if (buffer().capacity() < needed) {
			// Explicitly bound capacity growth too: an implicit QByteArray growth
			// near the byte limit can otherwise reserve another power of two.
			buffer().reserve(std::min(textureOutputByteLimit, std::max({needed, qint64(buffer().capacity()) * 2, qint64(64 * 1024)})));
		}
		return QBuffer::writeData(bytes, length);
	}
private:
	std::function<bool()> m_isCancelled;
};

bool fingerprint(const QString& path, QByteArray* digest, QString* error, const std::function<bool()>& isCancelled = {})
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > textureOutputByteLimit) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureOutput", "Unable to inspect the export destination, or it exceeds the 64 MiB limit."));
	}
	QCryptographicHash hash(QCryptographicHash::Sha256); qint64 total = 0;
	while (!file.atEnd()) {
		if (cancelled(isCancelled, error)) { return false; }
		const auto block = file.read(64 * 1024); total += block.size();
		if (file.error() != QFile::NoError || total > textureOutputByteLimit || block.isEmpty()) {
			return fail(error, QCoreApplication::translate("VibeStudioTextureOutput", "Unable to read the export destination within its byte limit."));
		}
		hash.addData(block);
	}
	*digest = hash.result(); return true;
}

bool unchanged(const TextureOutputTarget& target, QString* error, const std::function<bool()>& isCancelled = {})
{
	if (cancelled(isCancelled, error)) { return false; }
	const QFileInfo current(target.path);
	QByteArray hash;
	if (target.path.isEmpty() || !current.isAbsolute() || target.directory.isEmpty() ||
		QDir(current.absolutePath()).canonicalPath() != target.directory || current.isSymLink() ||
		current.exists() != target.existed || (current.exists() && (!current.isFile() || current.canonicalFilePath() != target.canonicalPath ||
			!fingerprint(target.path, &hash, error, isCancelled) || hash != target.sha256))) {
		if (cancelled(isCancelled, error)) { return false; }
		return fail(error, QCoreApplication::translate("VibeStudioTextureOutput", "The export destination changed during encoding or saving. No output was replaced; choose a new path or retry after reviewing it."));
	}
	return true;
}
}

QByteArray encodeTextureOutputPng(const QImage& image, QString* error, const std::function<bool()>& isCancelled)
{
	if (error) { error->clear(); }
	if (cancelled(isCancelled, error)) { return {}; }
	if (image.isNull() || image.width() > 65535 || image.height() > 65535 ||
		qint64(image.width()) * image.height() > 16ll * 1024 * 1024 || image.sizeInBytes() > textureOutputByteLimit) {
		fail(error, QCoreApplication::translate("VibeStudioTextureOutput", "PNG export requires an image of at most 16,777,216 pixels and 64 MiB of decoded data.")); return {};
	}
	QByteArray bytes; PngBuffer buffer(&bytes, isCancelled); buffer.open(QIODevice::WriteOnly);
	QImageWriter writer(&buffer, "PNG");
	if (!writer.write(image)) {
		fail(error, buffer.failure.isEmpty() ? QCoreApplication::translate("VibeStudioTextureOutput", "Unable to encode the texture as PNG: %1").arg(writer.errorString()) : buffer.failure); return {};
	}
	if (cancelled(isCancelled, error)) { return {}; }
	return bytes;
}

bool inspectTextureOutputTarget(const QString& path, bool overwrite, TextureOutputTarget* target, QString* error, const std::function<bool()>& isCancelled)
{
	if (error) { error->clear(); }
	if (cancelled(isCancelled, error)) { return false; }
	const QFileInfo info(path);
	if (!target || path.isEmpty() || info.isSymLink() || (info.exists() && (!info.isFile() || !overwrite))) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureOutput", "Choose an export file and explicitly permit replacing an existing file. Symbolic-link destinations are not supported."));
	}
	TextureOutputTarget next;
	next.path = info.absoluteFilePath(); next.directory = QDir(info.absolutePath()).canonicalPath();
	next.canonicalPath = info.canonicalFilePath(); next.existed = info.exists();
	if (next.directory.isEmpty()) { return fail(error, QCoreApplication::translate("VibeStudioTextureOutput", "The export folder must already exist.")); }
	if (next.existed && !fingerprint(next.path, &next.sha256, error, isCancelled)) { return false; }
	if (!unchanged(next, error, isCancelled)) { return false; }
	*target = std::move(next); return true;
}

bool writeTextureOutput(const TextureOutputTarget& target, const QByteArray& bytes, bool dryRun, QString* error)
{
	if (error) { error->clear(); }
	if (bytes.isEmpty() || bytes.size() > textureOutputByteLimit) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureOutput", "Encoded texture output must contain 1–67,108,864 bytes."));
	}
	if (!unchanged(target, error)) { return false; }
	if (dryRun) { return true; }
	// Use the observed real directory; retargeting a parent alias cannot choose
	// a different destination after the initial check.
	const QString path = QDir(target.directory).filePath(QFileInfo(target.path).fileName());
	if (!target.existed) {
		QTemporaryFile output(QDir(target.directory).filePath(QStringLiteral(".vibestudio-texture-export-XXXXXX.tmp")));
		if (!output.open() || output.write(bytes) != bytes.size() || !output.flush()) { return fail(error, output.errorString()); }
		output.close();
		if (!unchanged(target, error)) { return false; }
		// QFile::rename refuses an existing destination, including one created
		// after the check above. QSaveFile::commit would replace that new file.
		if (!output.rename(path)) {
			return fail(error, QCoreApplication::translate("VibeStudioTextureOutput", "Unable to publish the new texture export. No existing destination was replaced."));
		}
		output.setAutoRemove(false); return true;
	}
	QSaveFile output(path);
	if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size()) { return fail(error, output.errorString()); }
	if (!unchanged(target, error)) { output.cancelWriting(); return false; }
	if (!output.commit()) { return fail(error, output.errorString()); }
	return true;
}

} // namespace vibestudio
