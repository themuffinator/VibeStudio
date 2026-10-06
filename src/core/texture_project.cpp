#include "core/texture_project.h"
#include "core/texture_export.h"
#include "core/texture_output.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {
namespace {
const QByteArray magic = QByteArrayLiteral("VSTEX\r\n\x1a");
constexpr quint32 version = 1;
constexpr qsizetype metadataLimit = 256 * 1024;

bool fail(QString* error, const QString& message)
{
	if (error) { *error = message; }
	return false;
}

bool validateMetadata(const QJsonObject& metadata, QString* error)
{
	if (metadata.contains(QStringLiteral("export"))) {
		TextureExportOptions options;
		if (!metadata.value(QStringLiteral("export")).isObject()) { return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "The saved export settings must be an object.")); }
		if (!textureExportOptionsFromJson(metadata.value(QStringLiteral("export")).toObject(), &options, error)) { return false; }
	}
	if (metadata.contains(QStringLiteral("palette"))) {
		IdTechPaletteResolution palette;
		if (!texturePaletteFromMetadata(metadata.value(QStringLiteral("palette")).toObject(), &palette, error)) { return false; }
	}
	const auto path = metadata.value(QStringLiteral("packageTexturePath"));
	if (!path.isUndefined() && (!path.isString() || path.toString().size() > 4096 || path.toString().contains(QChar::Null))) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "The project package path must be text of at most 4,096 characters without null characters."));
	}
	return true;
}

bool readInteger(const QJsonObject& object, const QString& key, int minimum, int maximum, int* out)
{
	const auto field = object.value(key);
	const double value = field.toDouble(std::numeric_limits<double>::quiet_NaN());
	if (!field.isDouble() || !std::isfinite(value) || std::floor(value) != value || value < minimum || value > maximum) { return false; }
	*out = static_cast<int>(value); return true;
}

bool checkpoint(const TextureProgress& progress, QString* error)
{
	return !progress || progress(0, 0) || fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Texture project operation cancelled."));
}

bool hashBytes(QByteArrayView bytes, QByteArray* hash, QString* error, const TextureProgress& progress = {})
{
	QCryptographicHash digest(QCryptographicHash::Sha256);
	for (qsizetype offset = 0; offset < bytes.size(); offset += 64 * 1024) {
		if (!checkpoint(progress, error)) { return false; }
		digest.addData(bytes.sliced(offset, std::min<qsizetype>(64 * 1024, bytes.size() - offset)));
	}
	*hash = digest.result(); return checkpoint(progress, error);
}

bool hashFile(const QString& path, QByteArray* hash, QString* error, const TextureProgress& progress = {})
{
	QFile file(path);
	if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) || file.size() > textureProjectFileLimit) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "The project cannot be read or exceeds the 192 MiB file limit."));
	}
	QCryptographicHash digest(QCryptographicHash::Sha256);
	qint64 count = 0;
	while (!file.atEnd()) {
		if (!checkpoint(progress, error)) { return false; }
		const auto chunk = file.read(64 * 1024);
		if (file.error() != QFile::NoError) { return fail(error, file.errorString()); }
		count += chunk.size();
		if (count > textureProjectFileLimit) { return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "The project grew beyond the file limit while it was being read.")); }
		digest.addData(chunk);
	}
	*hash = digest.result(); return true;
}

bool saveBackup(const QString& source, const QString& destination, const QByteArray& expectedHash, QString* error)
{
	const QFileInfo target(destination);
	if (target.isSymLink() || (target.exists() && !target.isFile())) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "The texture backup destination is not a regular file."));
	}
	if (target.exists()) {
		QByteArray actual;
		return (hashFile(destination, &actual, error) && actual == expectedHash) ||
			fail(error, QCoreApplication::translate("VibeStudioTextureProject", "An existing texture backup has unexpected contents. The source was not replaced."));
	}
	QFile input(source); QSaveFile output(destination);
	if (!input.open(QIODevice::ReadOnly) || input.size() > textureProjectFileLimit || !output.open(QIODevice::WriteOnly)) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Unable to create a backup before replacing the texture project."));
	}
	QCryptographicHash digest(QCryptographicHash::Sha256); qint64 count = 0;
	while (!input.atEnd()) {
		const auto chunk = input.read(64 * 1024); count += chunk.size();
		if (input.error() != QFile::NoError || count > textureProjectFileLimit || output.write(chunk) != chunk.size()) {
			output.cancelWriting(); return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "The texture backup could not be completed. The source was not replaced."));
		}
		digest.addData(chunk);
	}
	if (digest.result() != expectedHash) {
		output.cancelWriting(); return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "The source changed while its backup was being made. The project was not replaced."));
	}
	if (!output.commit()) { return fail(error, output.errorString()); }
	return true;
}
}

QByteArray encodeTextureProject(const TextureDocument& document, const QJsonObject& metadata, QString* error, const TextureProgress& progress)
{
	if (error) { error->clear(); }
	if (!validateMetadata(metadata, error)) { return {}; }
	if (QJsonDocument(metadata).toJson(QJsonDocument::Compact).size() > metadataLimit) {
		fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Project metadata exceeds 256 KiB.")); return {};
	}
	TextureDocument validated;
	if (!validated.restoreLayers(document.size(), document.layers(), document.activeLayerIndex(), error)) { return {}; }
	QJsonArray layers;
	QByteArray payload;
	int completed = 0;
	for (const auto& layer : document.layers()) {
		bool interrupted = false;
		const auto png = encodeTextureOutputPng(layer.pixels, error, [&]() {
			interrupted = progress && !progress(completed, document.layers().size()); return interrupted;
		});
		if (png.isEmpty()) {
			if (interrupted) { fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Texture project encoding cancelled.")); }
			return {};
		}
		++completed;
		QJsonObject entry;
		entry.insert(QStringLiteral("id"), layer.id); entry.insert(QStringLiteral("name"), layer.name);
		entry.insert(QStringLiteral("visible"), layer.visible); entry.insert(QStringLiteral("locked"), layer.locked);
		entry.insert(QStringLiteral("opacity"), layer.opacity); entry.insert(QStringLiteral("blend"), textureBlendModeId(layer.blend));
		if (layer.pixels.format() == QImage::Format_Indexed8) {
			QJsonArray colors; for (QRgb color : layer.pixels.colorTable()) { colors.append(QColor::fromRgba(color).name(QColor::HexArgb)); }
			entry.insert(QStringLiteral("indexedPalette"), colors);
		}
		entry.insert(QStringLiteral("bytes"), png.size()); layers.append(entry);
		if (payload.size() + png.size() > textureProjectFileLimit - metadataLimit - 48) {
			fail(error, QCoreApplication::translate("VibeStudioTextureProject", "The encoded project exceeds the file limit.")); return {};
		}
		payload.append(png);
	}
	QJsonObject root;
	root.insert(QStringLiteral("width"), document.size().width()); root.insert(QStringLiteral("height"), document.size().height());
	root.insert(QStringLiteral("activeLayer"), document.activeLayerIndex()); root.insert(QStringLiteral("layers"), layers);
	root.insert(QStringLiteral("metadata"), metadata);
	const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
	if (json.size() > metadataLimit) { fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Project metadata exceeds 256 KiB.")); return {}; }
	QByteArray bytes(16, '\0');
	bytes.replace(0, magic.size(), magic);
	qToLittleEndian<quint32>(version, bytes.data() + 8);
	qToLittleEndian<quint32>(static_cast<quint32>(json.size()), bytes.data() + 12);
	bytes.append(json); bytes.append(payload);
	QByteArray hash;
	if (!hashBytes(bytes, &hash, error, progress)) { return {}; }
	bytes.append(hash);
	if (progress && !progress(document.layers().size(), document.layers().size())) { fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Texture project encoding cancelled.")); return {}; }
	return bytes;
}

bool decodeTextureProject(const QByteArray& bytes, TextureDocument* document, QJsonObject* metadata, QString* error, const TextureProgress& progress)
{
	if (error) { error->clear(); }
	const auto checkpoint = [&](qint64 done, qint64 total) { return !progress || progress(done, total) || fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Texture project decoding cancelled.")); };
	if (!checkpoint(0, 1)) { return false; }
	if (!document || bytes.size() < 48 || bytes.size() > textureProjectFileLimit || !bytes.startsWith(magic)) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Not a bounded VibeStudio texture project."));
	}
	if (qFromLittleEndian<quint32>(bytes.constData() + 8) != version) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "This texture project version is not supported."));
	}
	const quint32 jsonSize = qFromLittleEndian<quint32>(bytes.constData() + 12);
	const qsizetype payloadEnd = bytes.size() - 32;
	QByteArray hash;
	if (jsonSize > metadataLimit || jsonSize > payloadEnd - 16) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Texture project length or checksum is invalid."));
	}
	if (!hashBytes(QByteArrayView(bytes.constData(), payloadEnd), &hash, error, progress)) { return false; }
	if (hash != bytes.last(32)) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Texture project length or checksum is invalid."));
	}
	QJsonParseError parseError;
	const auto parsed = QJsonDocument::fromJson(bytes.mid(16, jsonSize), &parseError);
	if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Texture project metadata is malformed."));
	}
	const auto root = parsed.object(); int width = 0, height = 0, active = 0;
	const auto entries = root.value(QStringLiteral("layers")).toArray();
	if (!readInteger(root, QStringLiteral("width"), 1, TextureDocument::MaximumDimension, &width) ||
		!readInteger(root, QStringLiteral("height"), 1, TextureDocument::MaximumDimension, &height) ||
		!readInteger(root, QStringLiteral("activeLayer"), 0, int(entries.size()) - 1, &active) ||
		!root.value(QStringLiteral("metadata")).isObject() || entries.isEmpty() || entries.size() > TextureDocument::MaximumLayers ||
		qint64(width) * height * entries.size() > TextureDocument::MaximumLayerPixels || !validTextureSize({width, height}, error)) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Project dimensions, layer count, or active layer are invalid."));
	}
	if (!validateMetadata(root.value(QStringLiteral("metadata")).toObject(), error)) { return false; }
	QVector<TextureLayer> layers;
	qint64 pixelBytes = 0;
	qsizetype offset = 16 + jsonSize;
	for (const auto& value : entries) {
		if (!checkpoint(layers.size(), entries.size())) { return false; }
		if (!value.isObject()) { return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Layer metadata must be an object.")); }
		const auto entry = value.toObject(); TextureLayer layer; int length = 0;
		if (!readInteger(entry, QStringLiteral("id"), 1, 1000000000, &layer.id) ||
			!readInteger(entry, QStringLiteral("opacity"), 0, 100, &layer.opacity) ||
			!readInteger(entry, QStringLiteral("bytes"), 1, int(textureProjectFileLimit), &length) || length > payloadEnd - offset ||
			!entry.value(QStringLiteral("name")).isString() || !entry.value(QStringLiteral("visible")).isBool() ||
			!entry.value(QStringLiteral("locked")).isBool() || !textureBlendModeFromId(entry.value(QStringLiteral("blend")).toString(), &layer.blend)) {
			return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Layer properties or payload length are invalid."));
		}
		layer.name = entry.value(QStringLiteral("name")).toString(); layer.visible = entry.value(QStringLiteral("visible")).toBool(); layer.locked = entry.value(QStringLiteral("locked")).toBool();
		QBuffer buffer; buffer.setData(bytes.mid(offset, length)); buffer.open(QIODevice::ReadOnly); QImageReader reader(&buffer, "PNG");
		if (reader.size() != QSize(width, height)) { return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "A layer's decoded dimensions do not match the canvas.")); }
		layer.pixels = reader.read();
		if (layer.pixels.isNull()) { return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Unable to decode a project layer: %1").arg(reader.errorString())); }
		// Qt 6.10.1 qpnghandler.cpp stores identity indexed palettes as grayscale.
		// Restore the index plane explicitly; never quantize it. See docs/CREDITS.md.
		QVector<QRgb> palette;
		const auto colors = entry.value(QStringLiteral("indexedPalette"));
		if (!colors.isUndefined()) {
			if (!colors.isArray() || colors.toArray().isEmpty() || colors.toArray().size() > 256) { return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Invalid layer palette.")); }
			for (const auto& value : colors.toArray()) {
				const QColor color(value.toString());
				if (!value.isString() || !color.isValid()) { return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Invalid layer palette color.")); }
				palette.append(color.rgba());
			}
		}
		if (layer.pixels.format() == QImage::Format_Grayscale8) {
			if (palette.isEmpty()) { for (int index = 0; index < 256; ++index) { palette.append(qRgb(index, index, index)); } }
			for (int index = 0; index < palette.size(); ++index) { if (palette[index] != qRgb(index, index, index)) { return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "A grayscale layer disagrees with its recorded index palette.")); } }
			if (!layer.pixels.reinterpretAsFormat(QImage::Format_Indexed8)) { return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Unable to restore grayscale palette indices.")); }
			layer.pixels.setColorTable(palette);
		} else if (!palette.isEmpty() && (layer.pixels.format() != QImage::Format_Indexed8 || layer.pixels.colorTable() != palette)) {
			return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "A layer disagrees with its recorded index palette."));
		}
		pixelBytes += layer.pixels.sizeInBytes();
		if (pixelBytes > TextureDocument::MaximumLayerBytes) { return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Decoded layer storage exceeds 128 MiB.")); }
		layers.append(std::move(layer)); offset += length;
	}
	if (offset != payloadEnd) { return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Unexpected trailing project data.")); }
	TextureDocument next;
	if (!next.restoreLayers({width, height}, layers, active, error)) { return false; }
	if (!checkpoint(entries.size(), entries.size())) { return false; }
	*document = std::move(next);
	if (metadata) { *metadata = root.value(QStringLiteral("metadata")).toObject(); }
	return true;
}

bool readTextureProject(const QString& path, TextureDocument* document, TextureProjectIdentity* identity, QJsonObject* metadata, QString* error, const TextureProgress& progress)
{
	QFile file(path); const QFileInfo before(path);
	const QString canonical = before.canonicalFilePath();
	if (!document || !before.isFile() || !file.open(QIODevice::ReadOnly) || file.size() > textureProjectFileLimit) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "The project cannot be read or exceeds the 192 MiB file limit."));
	}
	QByteArray bytes; bytes.reserve(file.size());
	while (!file.atEnd()) {
		if (!checkpoint(progress, error)) { return false; }
		const auto chunk = file.read(64 * 1024);
		if (file.error() != QFile::NoError) { return fail(error, file.errorString()); }
		if (bytes.size() + chunk.size() > textureProjectFileLimit) {
			return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "The project grew beyond the file limit while it was being read."));
		}
		bytes.append(chunk);
	}
	TextureDocument next; QJsonObject values;
	if (!decodeTextureProject(bytes, &next, &values, error, progress)) { return false; }
	TextureProjectIdentity source{before.absoluteFilePath(), canonical, {}};
	if (!hashBytes(bytes, &source.sha256, error, progress)) { return false; }
	QByteArray afterHash;
	if (!hashFile(path, &afterHash, error, progress)) { return false; }
	if (source.canonicalPath != QFileInfo(path).canonicalFilePath() || afterHash != source.sha256) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "The project changed while it was being opened. Try opening it again."));
	}
	if (progress && !progress(1, 1)) { return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "Texture project decoding cancelled.")); }
	*document = std::move(next); if (identity) { *identity = source; } if (metadata) { *metadata = values; }
	return true;
}

TextureProjectPreparedSave prepareTextureProjectSave(const TextureDocument& document, const TextureProjectSaveRequest& request,
	const QJsonObject& metadata, const TextureProgress& progress)
{
	TextureProjectPreparedSave prepared; prepared.request = request;
	auto& report = prepared.report;
	const auto failReport = [&](const QString& message, bool conflict = false) {
		report.error = message; report.conflict = conflict; return prepared;
	};
	const QFileInfo before(request.path);
	prepared.request.path = before.absoluteFilePath();
	prepared.directory = QDir(before.absolutePath()).canonicalPath();
	prepared.observedCanonicalPath = before.canonicalFilePath();
	prepared.existed = before.exists();
	if (request.path.isEmpty() || before.suffix().compare(QStringLiteral("vtexture"), Qt::CaseInsensitive) != 0 || before.isSymLink() ||
		(before.exists() && (!before.isFile() || !request.overwrite))) {
		return failReport(QCoreApplication::translate("VibeStudioTextureProject", "Choose a .vtexture file and explicitly permit replacing an existing file. Symbolic-link destinations are not supported."));
	}
	if (prepared.directory.isEmpty()) { return failReport(QCoreApplication::translate("VibeStudioTextureProject", "The project folder must already exist.")); }
	if (!checkpoint(progress, &report.error)) { return prepared; }
	if (before.exists() && !hashFile(before.absoluteFilePath(), &prepared.observedSha256, &report.error, progress)) { return prepared; }
	if ((!request.expectedSha256.isEmpty() && request.expectedSha256 != prepared.observedSha256) ||
		(!request.expectedCanonicalPath.isEmpty() && request.expectedCanonicalPath != prepared.observedCanonicalPath)) {
		return failReport(QCoreApplication::translate("VibeStudioTextureProject", "The project was changed, moved, or deleted outside the editor. Save a copy or reopen it before replacing it."), true);
	}
	prepared.bytes = encodeTextureProject(document, metadata, &report.error, progress);
	if (prepared.bytes.isEmpty()) { return prepared; }
	report.identity.path = before.absoluteFilePath();
	if (!hashBytes(prepared.bytes, &report.identity.sha256, &report.error, progress)) { return prepared; }
	if (before.exists() && request.keepBackup && prepared.observedSha256 != report.identity.sha256) {
		const QString studioDirectory = QDir(prepared.directory).filePath(QStringLiteral(".vibestudio"));
		const QString backupDirectory = QDir(studioDirectory).filePath(QStringLiteral("texture-backups"));
		if (QFileInfo(studioDirectory).isSymLink() || QFileInfo(backupDirectory).isSymLink()) {
			return failReport(QCoreApplication::translate("VibeStudioTextureProject", "Texture backup folders must not be symbolic links. The project was not replaced."));
		}
		// Content-addressed names retain separate versions and deduplicate repeated saves.
		// No backup is automatically pruned; removal is an explicit user operation.
		report.backupPath = QDir(backupDirectory).filePath(QString::fromLatin1(prepared.observedSha256.toHex()) + QStringLiteral(".vtexture"));
	}
	if (progress && !progress(1, 1)) { return failReport(QCoreApplication::translate("VibeStudioTextureProject", "Texture project encoding cancelled.")); }
	report.succeeded = true; return prepared;
}

TextureProjectSaveReport publishTextureProjectSave(const TextureProjectPreparedSave& prepared)
{
	auto report = prepared.report;
	if (!report.succeeded) { return report; }
	report.succeeded = false;
	const auto failReport = [&](const QString& message, bool conflict = false) {
		report.error = message; report.conflict = conflict; return report;
	};
	if (prepared.bytes.isEmpty() || prepared.bytes.size() > textureProjectFileLimit || prepared.directory.isEmpty()) {
		return failReport(QCoreApplication::translate("VibeStudioTextureProject", "The prepared texture project is invalid."));
	}
	const auto unchanged = [&]() {
		const QFileInfo current(prepared.request.path); QByteArray hash;
		return QDir(current.absolutePath()).canonicalPath() == prepared.directory && !current.isSymLink() && current.exists() == prepared.existed &&
			(!current.exists() || (current.isFile() && current.canonicalFilePath() == prepared.observedCanonicalPath &&
				hashFile(current.absoluteFilePath(), &hash, &report.error) && hash == prepared.observedSha256));
	};
	const auto conflict = [&]() { return failReport(QCoreApplication::translate("VibeStudioTextureProject", "The destination changed during saving. No project was replaced."), true); };
	if (!unchanged()) { return conflict(); }
	if (prepared.request.dryRun) { report.succeeded = true; return report; }
	// Use the captured real directory so a retargeted parent alias cannot choose
	// another publication destination between checks.
	const QString path = QDir(prepared.directory).filePath(QFileInfo(prepared.request.path).fileName());
	if (!prepared.existed) {
		// A new-file publish must stay no-overwrite even if another process creates
		// the destination between the final check and publication. QFile::rename
		// refuses an existing destination; QSaveFile::commit would replace it.
		QTemporaryFile output(QDir(prepared.directory).filePath(QStringLiteral(".vibestudio-texture-XXXXXX.tmp")));
		if (!output.open() || output.write(prepared.bytes) != prepared.bytes.size() || !output.flush()) { return failReport(output.errorString()); }
		output.close();
		if (!unchanged()) { return conflict(); }
		if (!output.rename(path)) { return failReport(QCoreApplication::translate("VibeStudioTextureProject", "Unable to publish the new texture project. The destination may already exist; no existing file was replaced.")); }
		output.setAutoRemove(false);
		report.identity.canonicalPath = QFileInfo(path).canonicalFilePath(); report.succeeded = true; report.written = true;
		return report;
	}
	if (!report.backupPath.isEmpty()) {
		const QString backupDirectory = QFileInfo(report.backupPath).absolutePath();
		if (QFileInfo(QDir(prepared.directory).filePath(QStringLiteral(".vibestudio"))).isSymLink() || QFileInfo(backupDirectory).isSymLink()) {
			return failReport(QCoreApplication::translate("VibeStudioTextureProject", "Texture backup folders must not be symbolic links. The project was not replaced."));
		}
		if (!QDir().mkpath(backupDirectory)) { return failReport(QCoreApplication::translate("VibeStudioTextureProject", "Unable to create the texture backup directory. The project was not replaced.")); }
		if (!saveBackup(path, report.backupPath, prepared.observedSha256, &report.error)) { return report; }
	}
	QSaveFile output(path);
	if (!output.open(QIODevice::WriteOnly) || output.write(prepared.bytes) != prepared.bytes.size()) { return failReport(output.errorString()); }
	if (!unchanged()) { output.cancelWriting(); return conflict(); }
	if (!output.commit()) { return failReport(output.errorString()); }
	report.identity.canonicalPath = QFileInfo(path).canonicalFilePath(); report.succeeded = true; report.written = true;
	return report;
}

TextureProjectSaveReport writeTextureProject(const TextureDocument& document, const TextureProjectSaveRequest& request, const QJsonObject& metadata)
{
	return publishTextureProjectSave(prepareTextureProjectSave(document, request, metadata));
}

QJsonObject texturePaletteMetadata(const IdTechPaletteResolution& resolution)
{
	QJsonArray colors;
	for (const auto color : resolution.palette.colors) { colors.append(QColor::fromRgba(color).name(QColor::HexArgb)); }
	return {{QStringLiteral("id"), resolution.palette.id}, {QStringLiteral("name"), resolution.palette.displayName},
		{QStringLiteral("description"), resolution.palette.sourceDescription}, {QStringLiteral("colors"), colors},
		{QStringLiteral("transparentIndex"), resolution.palette.transparentIndex}, {QStringLiteral("fullbrightStartIndex"), resolution.palette.fullbrightStartIndex},
		{QStringLiteral("generated"), resolution.palette.generated}, {QStringLiteral("fromPackage"), resolution.fromPackage},
		{QStringLiteral("sourceEntry"), resolution.sourceVirtualPath}, {QStringLiteral("sourcePackage"), resolution.sourcePackagePath}};
}

bool texturePaletteFromMetadata(const QJsonObject& metadata, IdTechPaletteResolution* resolution, QString* error)
{
	IdTechPaletteResolution next;
	const auto colors = metadata.value(QStringLiteral("colors")).toArray();
	if (!resolution || colors.size() != 256 || !metadata.value(QStringLiteral("id")).isString() ||
		!metadata.value(QStringLiteral("generated")).isBool() || !metadata.value(QStringLiteral("fromPackage")).isBool() ||
		!readInteger(metadata, QStringLiteral("transparentIndex"), -1, 255, &next.palette.transparentIndex) ||
		!readInteger(metadata, QStringLiteral("fullbrightStartIndex"), -1, 255, &next.palette.fullbrightStartIndex)) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "The saved palette is invalid."));
	}
	for (const auto& value : colors) {
		const QString text = value.toString(); const QColor color(text);
		if (text.size() != 9 || !text.startsWith(QLatin1Char('#')) || !color.isValid()) {
			return fail(error, QCoreApplication::translate("VibeStudioTextureProject", "The saved palette contains an invalid color."));
		}
		next.palette.colors.append(color.rgba());
	}
	next.palette.id = metadata.value(QStringLiteral("id")).toString(); next.requestedPaletteId = next.palette.id;
	next.palette.displayName = metadata.value(QStringLiteral("name")).toString(); next.palette.sourceDescription = metadata.value(QStringLiteral("description")).toString();
	next.palette.generated = metadata.value(QStringLiteral("generated")).toBool(); next.fromPackage = metadata.value(QStringLiteral("fromPackage")).toBool();
	next.sourceVirtualPath = metadata.value(QStringLiteral("sourceEntry")).toString(); next.sourcePackagePath = metadata.value(QStringLiteral("sourcePackage")).toString();
	*resolution = std::move(next); return true;
}

} // namespace vibestudio
