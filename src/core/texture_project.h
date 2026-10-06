#pragma once

#include "core/texture_document.h"

#include <QJsonObject>

namespace vibestudio {

inline constexpr qint64 textureProjectFileLimit = 192ll * 1024 * 1024;

struct TextureProjectIdentity {
	QString path;
	QString canonicalPath;
	QByteArray sha256;
};

struct TextureProjectSaveRequest {
	QString path;
	bool overwrite = false;
	bool dryRun = false;
	bool keepBackup = true;
	// When saving an opened project, both identify the version the artist saw.
	// Empty values are for a separately reviewed Save As destination.
	QByteArray expectedSha256;
	QString expectedCanonicalPath;
};

struct TextureProjectSaveReport {
	bool succeeded = false;
	bool written = false;
	bool conflict = false;
	TextureProjectIdentity identity;
	QString backupPath;
	QString error;
};

// Process-local preparation, never project/recovery metadata. Capture the
// destination before cancellable encoding, then publish against that identity.
struct TextureProjectPreparedSave {
	TextureProjectSaveRequest request;
	TextureProjectSaveReport report;
	QByteArray bytes;
	QString directory;
	QString observedCanonicalPath;
	QByteArray observedSha256;
	bool existed = false;
};

// VibeStudio-owned format: magic, LE version and metadata length, bounded JSON,
// one lossless PNG payload per layer, then SHA-256 over all preceding bytes.
// Layer identity/order/properties and arbitrary bounded export metadata survive.
QByteArray encodeTextureProject(const TextureDocument& document, const QJsonObject& metadata = {}, QString* error = nullptr, const TextureProgress& progress = {});
bool decodeTextureProject(const QByteArray& bytes, TextureDocument* document, QJsonObject* metadata = nullptr, QString* error = nullptr, const TextureProgress& progress = {});
bool readTextureProject(const QString& path, TextureDocument* document, TextureProjectIdentity* identity = nullptr,
	QJsonObject* metadata = nullptr, QString* error = nullptr, const TextureProgress& progress = {});
TextureProjectPreparedSave prepareTextureProjectSave(const TextureDocument& document, const TextureProjectSaveRequest& request,
	const QJsonObject& metadata = {}, const TextureProgress& progress = {});
TextureProjectSaveReport publishTextureProjectSave(const TextureProjectPreparedSave& prepared);
TextureProjectSaveReport writeTextureProject(const TextureDocument& document, const TextureProjectSaveRequest& request,
	const QJsonObject& metadata = {});
QJsonObject texturePaletteMetadata(const IdTechPaletteResolution& resolution);
bool texturePaletteFromMetadata(const QJsonObject& metadata, IdTechPaletteResolution* resolution, QString* error = nullptr);

} // namespace vibestudio
