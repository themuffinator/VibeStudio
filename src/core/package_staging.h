#pragma once

#include "core/package_archive.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

enum class PackageStageOperationType {
	Add,
	Replace,
	Rename,
	Delete,
};

enum class PackageStageConflictResolution {
	Block,
	ReplaceExisting,
	Skip,
};

struct PackageStageOperation {
	QString id;
	PackageStageOperationType type = PackageStageOperationType::Add;
	QString virtualPath;
	QString targetVirtualPath;
	QString sourceFilePath;
	PackageStageConflictResolution conflictResolution = PackageStageConflictResolution::Block;
};

struct PackageStagedEntry {
	QString virtualPath;
	QByteArray bytes;
	QString source;
	QString operationId;
};

struct PackageStageConflict {
	QString operationId;
	QString virtualPath;
	QString message;
	bool blocking = true;
};

struct PackageCompositionBucket {
	QString id;
	QString label;
	int fileCount = 0;
	quint64 sizeBytes = 0;
};

struct PackageStagingSummary {
	QString sourcePath;
	PackageArchiveFormat sourceFormat = PackageArchiveFormat::Unknown;
	int baseFileCount = 0;
	int stagedFileCount = 0;
	int operationCount = 0;
	int addedCount = 0;
	int replacedCount = 0;
	int renamedCount = 0;
	int deletedCount = 0;
	int conflictCount = 0;
	int blockingCount = 0;
	quint64 beforeBytes = 0;
	quint64 afterBytes = 0;
	bool canSave = false;
	QStringList blockedMessages;
};

struct PackageWriteRequest {
	PackageArchiveFormat format = PackageArchiveFormat::Unknown;
	QString destinationPath;
	bool allowOverwrite = false;
	bool writeManifest = false;
	bool dryRun = false;
	QString manifestPath;
};

struct PackageWriteReport {
	QString sourcePath;
	QString outputPath;
	QString manifestPath;
	PackageArchiveFormat format = PackageArchiveFormat::Unknown;
	int entryCount = 0;
	quint64 bytesWritten = 0;
	QString sha256;
	bool deterministic = true;
	bool dryRun = false;
	bool wroteManifest = false;
	QStringList warnings;
	QStringList blockedMessages;

	[[nodiscard]] bool succeeded() const;
};

class PackageStagingModel final {
public:
	bool loadBaseArchive(const PackageArchiveReader& archive, QString* error = nullptr);
	void clear();

	[[nodiscard]] bool isLoaded() const;
	[[nodiscard]] QString sourcePath() const;
	[[nodiscard]] PackageArchiveFormat sourceFormat() const;
	[[nodiscard]] QVector<PackageStageOperation> operations() const;
	[[nodiscard]] QVector<PackageStageConflict> conflicts() const;
	[[nodiscard]] QVector<PackageStagedEntry> beforeEntries() const;
	[[nodiscard]] QVector<PackageStagedEntry> plannedEntries() const;
	[[nodiscard]] PackageStagingSummary summary() const;
	[[nodiscard]] QVector<PackageCompositionBucket> beforeComposition() const;
	[[nodiscard]] QVector<PackageCompositionBucket> afterComposition() const;
	[[nodiscard]] QByteArray manifestJson() const;

	bool addFile(const QString& sourceFilePath, const QString& virtualPath, QString* error = nullptr, PackageStageConflictResolution resolution = PackageStageConflictResolution::Block);
	bool replaceFile(const QString& virtualPath, const QString& sourceFilePath, QString* error = nullptr);
	bool renameEntry(const QString& virtualPath, const QString& targetVirtualPath, QString* error = nullptr, PackageStageConflictResolution resolution = PackageStageConflictResolution::Block);
	bool deleteEntry(const QString& virtualPath, QString* error = nullptr, PackageStageConflictResolution resolution = PackageStageConflictResolution::Block);
	bool clearOperation(const QString& operationId);

	bool exportManifest(const QString& outputPath, QString* error = nullptr) const;
	PackageWriteReport writeArchive(const PackageWriteRequest& request) const;

private:
	QString nextOperationId() const;
	bool appendOperation(PackageStageOperation operation, QString* error);

	QString m_sourcePath;
	PackageArchiveFormat m_sourceFormat = PackageArchiveFormat::Unknown;
	bool m_loaded = false;
	QVector<PackageStagedEntry> m_baseEntries;
	QVector<PackageStageOperation> m_operations;
	QVector<PackageStageConflict> m_baseConflicts;
};

QString packageStageOperationTypeId(PackageStageOperationType type);
QString packageStageOperationTypeDisplayName(PackageStageOperationType type);
PackageStageOperationType packageStageOperationTypeFromId(const QString& id);
QString packageStageConflictResolutionId(PackageStageConflictResolution resolution);
PackageStageConflictResolution packageStageConflictResolutionFromId(const QString& id);
QString packageWriteReportText(const PackageWriteReport& report);

} // namespace vibestudio
