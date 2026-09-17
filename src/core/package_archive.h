#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace vibestudio {

enum class PackageArchiveFormat {
	Unknown,
	Folder,
	Pak,
	Wad,
	Zip,
	Pk3,
};

enum class PackageEntryKind {
	File,
	Directory,
};

enum class PackagePathIssue {
	None,
	Empty,
	AbsolutePath,
	DriveQualifiedPath,
	TraversalSegment,
	CurrentDirectorySegment,
	Colon,
	ControlCharacter,
	TooLong,
	// Appended after the original values so existing serialized ids stay stable.
	ReservedDeviceName,
	TrailingDotOrSpace,
};

struct PackageArchiveFormatDescriptor {
	PackageArchiveFormat format = PackageArchiveFormat::Unknown;
	QString id;
	QString displayName;
	QStringList extensions;
	QStringList capabilities;
	QString description;
};

struct PackageVirtualPath {
	QString originalPath;
	QString normalizedPath;
	PackagePathIssue issue = PackagePathIssue::Empty;
	bool trailingSlash = false;

	[[nodiscard]] bool isSafe() const;
};

struct PackageEntry {
	QString virtualPath;
	PackageEntryKind kind = PackageEntryKind::File;
	quint64 sizeBytes = 0;
	quint64 compressedSizeBytes = 0;
	qint64 dataOffset = -1;
	QDateTime modifiedUtc;
	QString typeHint;
	QString storageMethod;
	QString sourceArchiveId;
	// Identifier of the mount layer that owns this entry inside a
	// PackageArchiveSession. Empty for entries read straight from a
	// PackageArchive.
	QString layerId;
	quint32 crc32 = 0;
	bool hasCrc32 = false;
	bool nestedArchiveCandidate = false;
	bool readable = true;
	QString note;
};

struct PackageLoadWarning {
	QString virtualPath;
	QString message;
};

struct PackageArchiveSummary {
	QString sourcePath;
	PackageArchiveFormat format = PackageArchiveFormat::Unknown;
	int entryCount = 0;
	int fileCount = 0;
	int directoryCount = 0;
	int nestedArchiveCount = 0;
	quint64 totalSizeBytes = 0;
	int warningCount = 0;
};

struct PackageMountLayer {
	QString id;
	QString displayName;
	QString sourcePath;
	QString mountPath;
	PackageArchiveFormat format = PackageArchiveFormat::Unknown;
	int entryCount = 0;
	bool readOnly = true;
};

struct PackageExtractionRequest {
	QString targetDirectory;
	QStringList virtualPaths;
	bool extractAll = false;
	bool dryRun = false;
	bool overwriteExisting = false;
};

struct PackageExtractionEntryResult {
	QString virtualPath;
	QString outputPath;
	PackageEntryKind kind = PackageEntryKind::File;
	quint64 bytes = 0;
	bool dryRun = false;
	bool written = false;
	bool skipped = false;
	QString message;
	QString error;
};

struct PackageExtractionReport {
	QString sourcePath;
	QString targetDirectory;
	bool extractAll = false;
	bool dryRun = false;
	bool overwriteExisting = false;
	bool cancelled = false;
	int requestedCount = 0;
	int processedCount = 0;
	int writtenCount = 0;
	int directoryCount = 0;
	int skippedCount = 0;
	int errorCount = 0;
	quint64 totalBytes = 0;
	QVector<PackageExtractionEntryResult> entries;
	QStringList warnings;

	[[nodiscard]] bool succeeded() const;
};

using PackageExtractionProgressCallback = std::function<bool(const PackageExtractionEntryResult& result, const PackageExtractionReport& report)>;

class PackageArchiveReader {
public:
	virtual ~PackageArchiveReader() = default;

	[[nodiscard]] virtual PackageArchiveFormat format() const = 0;
	[[nodiscard]] virtual QString sourcePath() const = 0;
	[[nodiscard]] virtual bool isOpen() const = 0;
	[[nodiscard]] virtual QVector<PackageEntry> entries() const = 0;
	virtual bool readEntryBytes(const QString& virtualPath, QByteArray* out, QString* error, qint64 maxBytes = -1) const = 0;
};

class PackageArchive final : public PackageArchiveReader {
public:
	bool load(const QString& path, QString* error = nullptr);
	void clear();

	[[nodiscard]] PackageArchiveFormat format() const override;
	[[nodiscard]] QString sourcePath() const override;
	[[nodiscard]] bool isOpen() const override;
	[[nodiscard]] QVector<PackageEntry> entries() const override;
	[[nodiscard]] QVector<PackageLoadWarning> warnings() const;
	[[nodiscard]] PackageArchiveSummary summary() const;
	bool readEntryBytes(const QString& virtualPath, QByteArray* out, QString* error, qint64 maxBytes = -1) const override;

private:
	bool loadFolder(const QString& path, QString* error);
	bool loadPak(const QString& path, QString* error);
	bool loadWad(const QString& path, QString* error);
	bool loadZipFamily(const QString& path, PackageArchiveFormat format, QString* error);
	bool readFileEntryBytes(const PackageEntry& entry, QByteArray* out, QString* error, qint64 maxBytes) const;
	bool readOffsetEntryBytes(const PackageEntry& entry, QByteArray* out, QString* error, qint64 maxBytes) const;
	const PackageEntry* findEntry(const QString& virtualPath) const;
	void finalizeEntries();
	void addWarning(const QString& virtualPath, const QString& message);

	PackageArchiveFormat m_format = PackageArchiveFormat::Unknown;
	QString m_sourcePath;
	bool m_open = false;
	QVector<PackageEntry> m_entries;
	QVector<PackageLoadWarning> m_warnings;
};

// A stack of package layers. The struct-only API (setPrimaryLayer /
// pushMountedLayer) is pure bookkeeping and touches no files; the archive API
// below actually opens each layer and merges the entry lists using idTech pk3
// semantics, where a later layer shadows an identical path in an earlier one.
class PackageArchiveSession final {
public:
	bool setPrimaryLayer(const PackageMountLayer& layer, QString* error = nullptr);
	[[nodiscard]] bool hasPrimaryLayer() const;
	[[nodiscard]] PackageMountLayer primaryLayer() const;

	bool pushMountedLayer(const PackageMountLayer& layer, QString* error = nullptr);
	bool popMountedLayer();
	void clearMountedLayers();
	[[nodiscard]] bool hasMountedLayer() const;
	[[nodiscard]] QVector<PackageMountLayer> mountedLayers() const;
	[[nodiscard]] PackageMountLayer currentLayer() const;
	[[nodiscard]] int depth() const;
	void clear();

	// Opens `path` and makes it the base layer, replacing any mounted layers.
	bool openPrimaryArchive(const QString& path, QString* error = nullptr);
	// Opens `path` and pushes it on top of the stack. `mountPath` relocates the
	// layer's entries under a virtual subdirectory; an empty value mounts at the
	// session root.
	bool mountArchive(const QString& path, const QString& mountPath = QString(), QString* error = nullptr);

	[[nodiscard]] int openArchiveCount() const;
	[[nodiscard]] bool hasOpenArchive() const;
	// Merged view across all open layers, later layers shadowing earlier ones.
	[[nodiscard]] QVector<PackageEntry> entries() const;
	[[nodiscard]] QVector<PackageLoadWarning> warnings() const;
	[[nodiscard]] PackageArchiveSummary summary() const;
	bool readEntryBytes(const QString& virtualPath, QByteArray* out, QString* error = nullptr, qint64 maxBytes = -1) const;
	// Layer index of the winning entry: 0 is the primary layer, 1..n the mounted
	// layers in push order. Returns -1 when no open layer owns the path.
	[[nodiscard]] int entryLayerIndex(const QString& virtualPath) const;
	[[nodiscard]] QString entryLayerId(const QString& virtualPath) const;
	[[nodiscard]] PackageMountLayer layerAt(int index) const;
	[[nodiscard]] const PackageArchive* archiveAt(int index) const;

private:
	struct LayerState {
		PackageMountLayer layer;
		std::shared_ptr<PackageArchive> archive;
	};

	bool normalizeLayer(PackageMountLayer* layer, QString* error) const;
	QString uniqueLayerId(const QString& candidate) const;
	const LayerState* layerStateAt(int index) const;
	void rebuildIndex() const;

	LayerState m_primary;
	bool m_hasPrimaryLayer = false;
	QVector<LayerState> m_mountedLayers;

	mutable bool m_indexDirty = true;
	mutable QVector<PackageEntry> m_mergedEntries;
	mutable QHash<QString, int> m_entryOwner;
};

QString packageArchiveFormatId(PackageArchiveFormat format);
QString packageArchiveFormatDisplayName(PackageArchiveFormat format);
PackageArchiveFormat packageArchiveFormatFromId(const QString& id);
PackageArchiveFormat packageArchiveFormatFromFileName(const QString& fileName);
PackageArchiveFormat packageArchiveFormatFromContent(const QString& filePath);
QVector<PackageArchiveFormatDescriptor> packageArchiveFormatDescriptors();
QString packageEntryKindId(PackageEntryKind kind);
QString packageEntryKindDisplayName(PackageEntryKind kind);

QString packagePathIssueId(PackagePathIssue issue);
QString packagePathIssueDisplayName(PackagePathIssue issue);
PackageVirtualPath normalizePackageVirtualPath(const QString& path, bool preserveTrailingSlash = true);
bool isSafePackageVirtualPath(const QString& path);
QString packageVirtualPathFileName(const QString& path);
QString packageVirtualPathParent(const QString& path);
bool packageEntryLooksNestedArchive(const QString& virtualPath);

// Extra restrictions that only matter once a package path becomes a real file
// on disk: Windows reserved device names and segments ending in a dot or a
// space. Enforced on every platform so an archive does not extract cleanly on
// one OS and produce an unusable tree on another.
PackagePathIssue packageFilesystemPathIssue(const QString& virtualPath);

bool packagePathIsInsideDirectory(const QString& rootDirectory, const QString& candidatePath);
QString safePackageOutputPath(const QString& rootDirectory, const QString& virtualPath, QString* error = nullptr);
PackageExtractionReport extractPackageEntries(const PackageArchive& archive, const PackageExtractionRequest& request, PackageExtractionProgressCallback progress = {});
QString packageExtractionReportText(const PackageExtractionReport& report);

} // namespace vibestudio
