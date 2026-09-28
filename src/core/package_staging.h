#pragma once

#include "core/deflate.h"
#include "core/package_archive.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

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

// Reproducible keeps the fixed 1980-01-01 MS-DOS timestamp used by reproducible
// build pipelines; PreserveSource carries the source entry/file timestamp into
// the written archive.
enum class PackageTimestampMode {
	Reproducible,
	PreserveSource,
};

struct PackageStageOperation {
	QString id;
	PackageStageOperationType type = PackageStageOperationType::Add;
	QString virtualPath;
	QString targetVirtualPath;
	QString sourceFilePath;
	PackageStageConflictResolution conflictResolution = PackageStageConflictResolution::Block;
};

// One record of a WAD's own lump directory, in on-disk order.
//
// A Doom IWAD/PWAD addresses map data positionally: every map repeats THINGS,
// LINEDEFS, SIDEDEFS, VERTEXES, SEGS, SSECTORS, NODES, SECTORS, REJECT and
// BLOCKMAP behind its own marker lump, so a lump *name* is not a key
// (https://doomwiki.org/wiki/WAD). Staging therefore keeps the source
// directory and addresses base lumps by ordinal.
struct PackageWadLumpLocation {
	// Lump name as the directory spells it, or `lump-NNNN` for a blank name,
	// matching what the reader exposes as a virtual path.
	QString name;
	qint64 dataOffset = -1;
	// Bytes actually stored (WAD2/WAD3 `diskSize`; the only size a Doom WAD has).
	qint64 diskSizeBytes = 0;
	// Logical size, which differs from `diskSizeBytes` only for a compressed
	// WAD2/WAD3 lump.
	qint64 sizeBytes = 0;
	// WAD2/WAD3 directory "type" byte; 0 for Doom WADs.
	quint8 type = 0;
	// WAD2/WAD3 "compression" byte; non-zero lumps are not decoded.
	quint8 compression = 0;
};

// A planned archive member. Bytes are not resident: they are read on demand from
// `sourceFilePath` (a staged file on disk) or from the base archive entry named
// by `baseVirtualPath`, so staging one file into a large package never loads the
// whole package into memory.
struct PackageStagedEntry {
	QString virtualPath;
	PackageEntryKind kind = PackageEntryKind::File;
	quint64 sizeBytes = 0;
	QDateTime modifiedUtc;
	QString source;
	QString operationId;
	QString sourceFilePath;
	QString baseVirtualPath;
	// Zero-based position of this entry in the source package's own directory,
	// or -1 for an entry that does not come from the base archive.
	//
	// `virtualPath` is not a unique key for every package format: a Doom WAD
	// repeats lump names once per map. The ordinal is what keeps those
	// duplicates apart, what makes `entryBytes` resolve the right lump, and
	// what preserves the source lump order through the plan. For a WAD source
	// it indexes the WAD directory; for every other source it is the base
	// reader's own entry order.
	int sourceOrdinal = -1;
	// WAD2/WAD3 directory "type" byte, preserved from a texture WAD source.
	quint8 wadLumpType = 0;
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
	int baseDirectoryCount = 0;
	int stagedFileCount = 0;
	int stagedDirectoryCount = 0;
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
	// Opt-in save over an existing package, including the package that is
	// currently open. Without it `writeArchive` keeps refusing a destination
	// that resolves to the source path.
	//
	// The new bytes are streamed to a temporary sibling of the destination and
	// verified; only then is the original moved to `backupPath` and the
	// temporary renamed into place. Any failure before that last rename leaves
	// the original file exactly as it was.
	bool allowInPlaceOverwrite = false;
	// Where the replaced file is moved. Empty means `<destination>.bak`.
	QString backupPath;
	bool writeManifest = false;
	bool dryRun = false;
	QString manifestPath;
	// ZIP/PK3 compression. Deflate is the default because every engine and tool
	// that reads PK3/ZIP expects method 8; DeflateLevel::Store is the explicit
	// "no compression" choice. Ignored by PAK and WAD output.
	DeflateLevel compression = DeflateLevel::Default;
	PackageTimestampMode timestampMode = PackageTimestampMode::Reproducible;
	// Writes the archive a second time into a hash-only sink and compares, so
	// the report can state determinism as an observation instead of a claim.
	bool verifyDeterminism = false;
	// WAD output magic: empty inherits the source WAD magic (PWAD/IWAD/WAD2/WAD3),
	// falling back to PWAD. Stable technical identifier, never translated.
	QString wadMagic;
};

struct PackageWriteReport {
	QString sourcePath;
	QString outputPath;
	QString manifestPath;
	// Where the replaced file was moved, empty when nothing was replaced.
	QString backupPath;
	// True when the output replaced an existing file through the backup path.
	bool overwroteInPlace = false;
	PackageArchiveFormat format = PackageArchiveFormat::Unknown;
	int entryCount = 0;
	int directoryCount = 0;
	quint64 bytesWritten = 0;
	quint64 uncompressedBytes = 0;
	// Written payload bytes divided by uncompressed payload bytes: 1.0 means no
	// saving, 0.25 means the payload shrank to a quarter. 1.0 when empty.
	double compressionRatio = 1.0;
	QString compressionId;
	QString timestampModeId;
	QString wadMagic;
	QString sha256;
	// True only when the writer's reproducibility guarantee holds for this
	// request, or when `verifyDeterminism` actually observed identical bytes.
	bool deterministic = false;
	bool determinismVerified = false;
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
	[[nodiscard]] QString sourceWadMagic() const;
	// The source WAD's directory in on-disk order, empty for every other
	// format. Indexed by `PackageStagedEntry::sourceOrdinal`.
	[[nodiscard]] QVector<PackageWadLumpLocation> sourceWadLumps() const;
	[[nodiscard]] QVector<PackageStageOperation> operations() const;
	[[nodiscard]] QVector<PackageStageConflict> conflicts() const;
	[[nodiscard]] QVector<PackageStagedEntry> beforeEntries() const;
	[[nodiscard]] QVector<PackageStagedEntry> plannedEntries() const;
	[[nodiscard]] PackageStagingSummary summary() const;
	[[nodiscard]] QVector<PackageCompositionBucket> beforeComposition() const;
	[[nodiscard]] QVector<PackageCompositionBucket> afterComposition() const;
	[[nodiscard]] QByteArray manifestJson() const;

	// Reads the bytes for a planned entry on demand. Directory entries yield an
	// empty byte array.
	bool entryBytes(const PackageStagedEntry& entry, QByteArray* out, QString* error = nullptr) const;

	bool addFile(const QString& sourceFilePath, const QString& virtualPath, QString* error = nullptr, PackageStageConflictResolution resolution = PackageStageConflictResolution::Block);
	bool replaceFile(const QString& virtualPath, const QString& sourceFilePath, QString* error = nullptr);
	bool renameEntry(const QString& virtualPath, const QString& targetVirtualPath, QString* error = nullptr, PackageStageConflictResolution resolution = PackageStageConflictResolution::Block);
	bool deleteEntry(const QString& virtualPath, QString* error = nullptr, PackageStageConflictResolution resolution = PackageStageConflictResolution::Block);
	// Removes exactly one staged operation.
	bool clearOperation(const QString& operationId);

	bool exportManifest(const QString& outputPath, QString* error = nullptr) const;
	// Writes the plan. After a successful in-place overwrite the model still
	// describes the package that was replaced, so reload the base archive
	// before staging anything else on top of it.
	PackageWriteReport writeArchive(const PackageWriteRequest& request) const;

private:
	QString nextOperationId();
	bool appendOperation(PackageStageOperation operation, QString* error);
	void invalidatePlan();
	void ensurePlan() const;

	QString m_sourcePath;
	PackageArchiveFormat m_sourceFormat = PackageArchiveFormat::Unknown;
	QString m_sourceWadMagic;
	QVector<PackageWadLumpLocation> m_sourceWadLumps;
	bool m_loaded = false;
	QVector<PackageStagedEntry> m_baseEntries;
	QVector<PackageStagedEntry> m_baseDirectories;
	QVector<PackageStageOperation> m_operations;
	QVector<PackageStageConflict> m_baseConflicts;
	quint64 m_operationSerial = 0;
	mutable bool m_planValid = false;
	mutable QVector<PackageStagedEntry> m_planEntries;
	mutable QVector<PackageStageConflict> m_planConflicts;
	mutable std::shared_ptr<PackageArchive> m_baseReader;
};

QString packageStageOperationTypeId(PackageStageOperationType type);
QString packageStageOperationTypeDisplayName(PackageStageOperationType type);
PackageStageOperationType packageStageOperationTypeFromId(const QString& id);
QString packageStageConflictResolutionId(PackageStageConflictResolution resolution);
PackageStageConflictResolution packageStageConflictResolutionFromId(const QString& id);
QString packageTimestampModeId(PackageTimestampMode mode);
PackageTimestampMode packageTimestampModeFromId(const QString& id);
QString packageWriteReportText(const PackageWriteReport& report);

} // namespace vibestudio
