#pragma once

#include "core/texture_project.h"

#include <QDateTime>

namespace vibestudio {

struct TextureRecoverySnapshot {
	TextureDocument document;
	QJsonObject metadata;
	TextureProjectIdentity source;
	QString displayName;
};

struct TextureRecoveryInfo {
	QString path, id, displayName, sourcePath;
	QDateTime writtenUtc;
	QByteArray sourceSha256, recordSha256;
	quint64 revision = 0;
	qint64 payloadBytes = 0;
	QSize size;
	int layerCount = 0;
	QString error;
	[[nodiscard]] bool isValid() const { return error.isEmpty() && !id.isEmpty() && payloadBytes > 0; }
};

struct TextureRecoveryList {
	QVector<TextureRecoveryInfo> records;
	bool truncated = false;
	bool cancelled = false;
	QString error;
};

// IDs are canonical UUIDs owned by an editor instance. These functions never
// read, write, or remove the source paths recorded inside a checkpoint.
QString textureRecoveryPath(const QString& directory, const QString& id);
QString writeTextureRecovery(const TextureRecoverySnapshot& snapshot, const QString& directory, const QString& id,
	QString* error = nullptr, const TextureProgress& progress = {});
TextureRecoveryInfo inspectTextureRecovery(const QString& path, const TextureProgress& progress = {});
TextureRecoveryList listTextureRecoveries(const QString& directory, const TextureProgress& progress = {});
bool restoreTextureRecovery(const QString& path, TextureDocument* document, QJsonObject* metadata = nullptr,
	TextureRecoveryInfo* info = nullptr, QString* error = nullptr, const QByteArray& expectedRecordSha256 = {}, const TextureProgress& progress = {});
bool removeTextureRecovery(const QString& directory, const QString& id, QString* error = nullptr);

} // namespace vibestudio
