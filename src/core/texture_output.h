#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <functional>

namespace vibestudio {

// Capture before encoding, then publish against the same observed destination.
// This identity is process-local state, never trusted project/recovery metadata.
struct TextureOutputTarget {
	QString path;
	QString directory;
	QString canonicalPath;
	QByteArray sha256;
	bool existed = false;
};

constexpr qint64 textureOutputByteLimit = 64ll * 1024 * 1024;
// Browser exports support the decoder's 16-million-pixel limit. Authoring
// callers still enforce their own smaller document/profile limits.
QByteArray encodeTextureOutputPng(const QImage& image, QString* error = nullptr, const std::function<bool()>& isCancelled = {});
bool inspectTextureOutputTarget(const QString& path, bool overwrite, TextureOutputTarget* target, QString* error = nullptr,
	const std::function<bool()>& isCancelled = {});
bool writeTextureOutput(const TextureOutputTarget& target, const QByteArray& bytes, bool dryRun, QString* error = nullptr);

} // namespace vibestudio
