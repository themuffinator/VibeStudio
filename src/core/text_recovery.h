#pragma once

#include "core/text_document.h"

#include <QDateTime>
#include <functional>

namespace vibestudio {

struct TextRecoverySnapshot {
	TextFileDocument source;
	QString text;
	QString title;
	int position = 0;
	int anchor = 0;
	int scroll = 0;
	int horizontalScroll = 0;
};

struct TextRecoveryRecord {
	QString path;
	QString id;
	QString title;
	QString sourcePath;
	QByteArray sourceSha256;
	QDateTime writtenUtc;
	qint64 ownerProcessId = 0;
	qint64 payloadBytes = 0;
	int position = 0;
	int anchor = 0;
	int scroll = 0;
	int horizontalScroll = 0;
	QString error;
	[[nodiscard]] bool isValid() const { return !id.isEmpty() && error.isEmpty(); }
};

struct TextRecoveryScan {
	QVector<TextRecoveryRecord> records;
	bool limited = false;
	QString error;
};

QString textRecoveryDirectory();

// Versioned, checksummed local copies. Source paths are provenance only: a
// restored document always has an empty path and needs a reviewed Save As.
QString writeTextRecovery(const TextRecoverySnapshot& snapshot, const QString& directory, const QString& id,
	QString* error = nullptr, const std::function<bool()>& cancelled = {});
TextRecoveryRecord inspectTextRecovery(const QString& path, TextFileDocument* restored = nullptr);
TextRecoveryScan listTextRecoveries(const QString& directory, const std::function<bool()>& cancelled = {});
bool removeTextRecovery(const QString& directory, const QString& id, QString* error = nullptr);

} // namespace vibestudio
