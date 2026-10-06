#pragma once

#include "core/package_archive.h"

#include <QJsonObject>

namespace vibestudio {

struct PackageValidationRequest {
	// Zero means unlimited. Streaming keeps payload memory bounded regardless
	// of these optional work budgets. An unchecked file prevents a valid result.
	quint64 maxEntryBytes = 0;
	quint64 maxTotalBytes = 0;
	std::function<bool()> isCancelled;
	std::function<void(int completed, int total, quint64 bytesRead, const QString& path)> progress;
};

struct PackageValidationEntry {
	QString virtualPath;
	qint64 sourceOrdinal = -1;
	QString status; // verified, failed, unchecked, cancelled
	quint64 bytesRead = 0;
	QString sha256;
	QString message;
};

struct PackageValidationReport {
	QString sourcePath;
	bool completed = false;
	bool cancelled = false;
	int fileCount = 0;
	int verifiedCount = 0;
	int failedCount = 0;
	int uncheckedCount = 0;
	quint64 bytesRead = 0;
	quint64 sourceBytesRead = 0;
	bool sourceVerified = false;
	QVector<PackageValidationEntry> entries;
	QVector<PackageLoadWarning> warnings;
	[[nodiscard]] bool valid() const;
};

// Checks every physical file, including repeated WAD lump names. Payloads are
// streamed, decompressed, size/CRC checked and hashed. This checks container
// integrity; it does not validate each asset's format or game compatibility.
PackageValidationReport validatePackage(const PackageArchive& archive, const PackageValidationRequest& request = {});
QJsonObject packageValidationJson(const PackageValidationReport& report);
QString packageValidationText(const PackageValidationReport& report);

} // namespace vibestudio
