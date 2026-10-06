#pragma once

#include "core/studio_query.h"
#include "core/package_content.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace vibestudio {

// Shared by the archive reader and staged WAD view. Empty means no marker.
QString doomNamespaceMarker(const QString& lumpName, bool* opens = nullptr);

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
	// What the entry is thought to hold, from its extension or its container:
	// a WAD2/WAD3 lump is "wad-texture"; a Doom WAD lump is "wad-flat",
	// "wad-sprite", "wad-patch" or "wad-texture" between namespace markers,
	// "wad-marker" for the markers themselves, "wad-sound" when its first bytes
	// are a DMX sound header, and "wad-lump" otherwise.
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
	// Physical directory position, before the browser's path sorting. Synthetic
	// directories have no ordinal. WAD lump names alone are not identities.
	qint64 sourceOrdinal = -1;
	// Exact WAD2/WAD3 directory type; -1 means no WAD type was supplied.
	int wadLumpType = -1;
};

struct PackageLoadWarning {
	QString virtualPath;
	QString message;
	// Skipped or unreadable records cannot be silently discarded on save.
	// Preserved duplicate records are advisory and remain position-addressable.
	bool blocksSaving = true;
};

struct PackageArchiveSummary {
	QString sourcePath;
	PackageArchiveFormat format = PackageArchiveFormat::Unknown;
	int entryCount = 0;
	int fileCount = 0;
	int directoryCount = 0;
	int nestedArchiveCount = 0;
	// Saturated when overflow is true; callers must not present it as exact.
	quint64 totalSizeBytes = 0;
	bool totalSizeOverflow = false;
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

struct PackageExtractionSelection {
	// Exact file position in the supplied immutable reader snapshot.
	qsizetype entryIndex = -1;
	// Empty preserves its archive path. A relative override can separate
	// repeated names; the full mapped namespace is checked before any output.
	QString outputVirtualPath;
};

struct PackageExtractionRequest {
	QString targetDirectory;
	QStringList virtualPaths;
	bool extractAll = false;
	bool dryRun = false;
	bool overwriteExisting = false;
	// Runs on the operation thread, including within a large stored/DEFLATE file.
	PackageReadControl control;
	QVector<PackageExtractionSelection> entrySelections;
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
	// Position in the reader snapshot, preserving repeated archive names.
	qsizetype entryIndex = -1;
	qint64 sourceOrdinal = -1;
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
	quint64 bytesRead = 0;
	QVector<PackageExtractionEntryResult> entries;
	QStringList warnings;

	[[nodiscard]] bool succeeded() const;
};

using PackageExtractionProgressCallback = std::function<bool(const PackageExtractionEntryResult& result, const PackageExtractionReport& report)>;

// Whether `head`, a lump's first 8 bytes, is a Doom DMX digital sound's
// header (the Doom Wiki's "Sound"): format 3, a rate a sound card plays (4000
// to 48000 Hz), and a sample count that fills the lump of `size` bytes but for
// a few trailing ones. Map lumps are never sounds, so `lumpName` rules those
// out; the WAD reader and the audio analyser share this one test.
bool dmxSoundHeaderLooksValid(const QByteArray& head, qint64 size, const QString& lumpName = QString());

class PackageArchiveReader {
public:
	virtual ~PackageArchiveReader() = default;

	[[nodiscard]] virtual PackageArchiveFormat format() const = 0;
	[[nodiscard]] virtual QString sourcePath() const = 0;
	// Stable WAD subtype when known; virtual WAD providers must supply it for
	// editable adoption. Their file rows carry ordinal and directory type too.
	[[nodiscard]] virtual QString wadMagic() const { return {}; }
	[[nodiscard]] virtual bool isOpen() const = 0;
	[[nodiscard]] virtual QString errorString() const { return {}; }
	[[nodiscard]] virtual QVector<PackageEntry> entries() const = 0;
	virtual bool readEntryBytes(const QString& virtualPath, QByteArray* out, QString* error, qint64 maxBytes = -1) const = 0;
	// Position in entries(), not sourceOrdinal. The fallback refuses ambiguous
	// names; concrete readers can resolve repeated names without guessing.
	virtual bool readEntryAt(qsizetype index, QByteArray* out, QString* error, qint64 maxBytes = -1) const;
	// Consumers must discard uncommitted output if streaming fails: checksum
	// validation can fail after the last chunk. The default refuses streaming.
	virtual bool streamEntryAt(qsizetype index, const std::function<bool(QByteArrayView)>& sink, QString* error,
		const std::function<bool()>& isCancelled = {}) const;
	// Complete source roots, including auxiliary files and backing stores.
	// Providers must stop when the visitor returns false; metadata must remain
	// immutable with the owned snapshot. Roots protect files and descendants.
	virtual bool visitProtectedInputPaths(const std::function<bool(const QString&)>& visitor,
		QString* error = nullptr, const PackageReadControl& control = {}) const;
	// All readers use the enumerable contract so portable drafts can retain it.
	[[nodiscard]] virtual bool protectsInputPath(const QString& path) const final;
};

// Bounds for an archive/folder index, combined session or archive output. Callers can select stricter limits, but
// cannot raise these hard ceilings. Entry counts include skipped records and
// implied folders. Metadata counts directory records and retained UTF-16 text;
// chunk fingerprints have their own aggregate budget, not a payload-size quota.
struct PackageIndexLimits {
	static constexpr qsizetype entryCeiling = 250000;
	static constexpr qint64 metadataCeiling = 64ll * 1024 * 1024;
	static constexpr qint64 fingerprintCeiling = 64ll * 1024 * 1024;
	static constexpr int depthCeiling = 128;
	qsizetype maximumEntries = entryCeiling;
	qint64 maximumMetadataBytes = metadataCeiling;
	qint64 maximumFingerprintBytes = fingerprintCeiling;
	int maximumPathDepth = depthCeiling;
};

// Logical resources retained by an index. Hidden/skipped source records stay
// charged when layers override one another; these are not exact heap sizes.
struct PackageIndexUsage {
	qsizetype entries = 0;
	qint64 metadataBytes = 0;
	qint64 fingerprintBytes = 0;
};

class PackageArchive final : public PackageArchiveReader {
public:
	bool load(const QString& path, QString* error = nullptr, const PackageReadControl& control = {}, const PackageIndexLimits& limits = {});
	// Adapt an immutable document reader for existing archive consumers. Entry
	// indexes are preserved exactly; the reader owns its payload backing. A failed
	// admission preserves this archive and records the error. Known archive
	// adapters are frozen and flattened; other readers must remain immutable.
	bool loadSnapshot(std::shared_ptr<const PackageArchiveReader> reader, QString* error = nullptr,
		const QVector<PackageLoadWarning>& warnings = {}, const PackageReadControl& control = {}, const PackageIndexLimits& limits = {});
	[[nodiscard]] std::shared_ptr<const PackageArchiveReader> snapshotReader() const;
	// Admitted disk index or snapshot projection, including implied directories.
	// Known backing indexes remain admitted too; usage keeps their larger charges.
	[[nodiscard]] PackageIndexUsage indexUsage() const;
	void clear();

	[[nodiscard]] PackageArchiveFormat format() const override;
	[[nodiscard]] QString sourcePath() const override;
	[[nodiscard]] QString wadMagic() const override;
	[[nodiscard]] bool isOpen() const override;
	[[nodiscard]] QString errorString() const override { return m_error; }
	[[nodiscard]] QVector<PackageEntry> entries() const override;
	[[nodiscard]] QVector<PackageLoadWarning> warnings() const;
	[[nodiscard]] PackageArchiveSummary summary() const;
	bool readEntryBytes(const QString& virtualPath, QByteArray* out, QString* error, qint64 maxBytes = -1) const override;
	bool readEntryAt(qsizetype index, QByteArray* out, QString* error, qint64 maxBytes = -1) const override;
	bool streamEntryAt(qsizetype index, const std::function<bool(QByteArrayView)>& sink, QString* error,
		const std::function<bool()>& isCancelled = {}) const override;
	[[nodiscard]] PackageFileIdentityPtr fileIdentity(const QString& virtualPath = {}) const;
	[[nodiscard]] QByteArray contentId() const;
	bool verifySourceIdentity(QString* error = nullptr, const PackageReadControl& control = {}) const;
	bool visitProtectedInputPaths(const std::function<bool(const QString&)>& visitor,
		QString* error = nullptr, const PackageReadControl& control = {}) const override;

private:
	bool loadFolder(const QString& path, QString* error);
	bool loadPak(const QString& path, QString* error);
	bool loadWad(const QString& path, QString* error);
	bool loadZipFamily(const QString& path, PackageArchiveFormat format, QString* error);
	bool readFileEntryBytes(const PackageEntry& entry, QByteArray* out, QString* error, qint64 maxBytes) const;
	bool readOffsetEntryBytes(const PackageEntry& entry, QByteArray* out, QString* error, qint64 maxBytes) const;
	bool readEntry(const PackageEntry& entry, QByteArray* out, QString* error, qint64 maxBytes) const;
	bool sourceMatchesSnapshot() const;
	const PackageEntry* findEntry(const QString& virtualPath) const;
	bool finalizeEntries(QString* error);
	bool accountIndex(qsizetype entries, qint64 metadataBytes, QString* error);
	bool accountIndexPath(const QString& path, QString* error);
	bool addWarning(const QString& virtualPath, const QString& message, bool blocksSaving = true, QString* error = nullptr);

	PackageArchiveFormat m_format = PackageArchiveFormat::Unknown;
	QString m_sourcePath;
	QString m_wadMagic;
	QString m_error;
	bool m_open = false;
	QVector<PackageEntry> m_entries;
	QVector<PackageLoadWarning> m_warnings;
	PackageArchiveSummary m_summary;
	qint64 m_sourceSizeBytes = -1;
	QDateTime m_sourceModifiedUtc;
	PackageFileIdentityPtr m_fileIdentity;
	QHash<QString, PackageFileIdentityPtr> m_folderIdentities;
	QString m_resolvedRoot;
	// Only retained during load; copied/open readers contain no loader callback.
	PackageReadControl m_loadControl;
	PackageIndexLimits m_indexLimits;
	qsizetype m_indexEntries = 0;
	qint64 m_indexMetadataBytes = 0, m_indexFingerprintBytes = 0;
	std::shared_ptr<const PackageArchiveReader> m_snapshotReader;
};

// A stack of package layers. The struct-only API (setPrimaryLayer /
// pushMountedLayer) is pure bookkeeping and touches no files; the archive API
// below actually opens each layer and merges the entry lists using idTech pk3
// semantics, where a later layer shadows an identical path in an earlier one.
// A session shares one admission budget across every retained source, including
// shadowed records and relocation prefixes. Failed/cancelled additions preserve
// the previous stack and merged view; popping layers releases their charges.
class PackageArchiveSession final {
public:
	static constexpr int layerCeiling = 64;
	explicit PackageArchiveSession(const PackageIndexLimits& limits = {});
	[[nodiscard]] PackageIndexUsage indexUsage() const;
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
	bool openPrimaryArchive(const QString& path, QString* error = nullptr, const PackageReadControl& control = {});
	// Opens `path` and pushes it on top of the stack. `mountPath` relocates the
	// layer's entries under a virtual subdirectory; an empty value mounts at the
	// session root.
	bool mountArchive(const QString& path, const QString& mountPath = QString(), QString* error = nullptr, const PackageReadControl& control = {});

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
		PackageIndexUsage usage;
	};

	bool normalizeLayer(PackageMountLayer* layer, QString* error) const;
	QString uniqueLayerId(const QString& candidate) const;
	const LayerState* layerStateAt(int index) const;
	bool rebuildIndex(QString* error = nullptr, const PackageReadControl& control = {}) const;
	bool admitLayer(const PackageMountLayer& layer, const std::shared_ptr<PackageArchive>& archive,
		PackageIndexUsage* usage, QString* error, const PackageReadControl& control = {}) const;
	bool openLayer(const QString& path, const QString& mountPath, bool primary, QString* error, const PackageReadControl& control);
	void updateUsage();
	PackageIndexLimits m_limits;
	PackageIndexUsage m_usage;

	LayerState m_primary;
	bool m_hasPrimaryLayer = false;
	QVector<LayerState> m_mountedLayers;

	mutable bool m_indexDirty = true;
	mutable QVector<PackageEntry> m_mergedEntries;
	mutable PackageArchiveSummary m_summary;
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
// A package entry's properties as a query reads them (core/studio_query.h):
// "path", "name", "ext" (the suffix without its dot), "folder", "type" (what
// it is thought to hold), "storage", "size" and "packed" (bytes), and "kind"
// (file or directory). So `ext=wav size>1mb` finds the large sounds.
StudioQueryProperties packageEntryQueryProperties(const PackageEntry& entry);
bool packageEntryLooksNestedArchive(const QString& virtualPath);

// Extra restrictions that only matter once a package path becomes a real file
// on disk: Windows reserved device names and segments ending in a dot or a
// space. Enforced on every platform so an archive does not extract cleanly on
// one OS and produce an unusable tree on another.
PackagePathIssue packageFilesystemPathIssue(const QString& virtualPath);

// Resolves existing prefixes, including Windows junctions, with cancellation.
// Empty means resolution could not complete. Missing suffixes remain lexical.
QString packageResolvedAbsolutePath(const QString& path, const PackageReadControl& control = {});
bool packagePathIsInsideDirectory(const QString& rootDirectory, const QString& candidatePath);
QString safePackageOutputPath(const QString& rootDirectory, const QString& virtualPath, QString* error = nullptr);
// Preflights the complete selection before creating output. Repeated or
// case-folded output names are conflicts, including when overwrite is enabled.
// Files commit independently; cancellation retains already completed files.
PackageExtractionReport extractPackageEntries(const PackageArchiveReader& archive, const PackageExtractionRequest& request, PackageExtractionProgressCallback progress = {});
QString packageExtractionReportText(const PackageExtractionReport& report);

} // namespace vibestudio
