#pragma once

#include "core/deflate.h"
#include "core/package_archive.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

#include <limits>
#include <memory>
#include <optional>

namespace vibestudio {

struct PackageSubsetReview;

enum class PackageStageOperationType {
	Add,
	Replace,
	Rename,
	Delete,
	CreateDirectory,
	RenameDirectory,
	DeleteDirectory,
};

enum class PackageStageConflictResolution {
	Block,
	ReplaceExisting,
	Skip,
};

// VerifyOnly is for short-lived read-only CLI diagnostics and dry-run plans;
// interactive edits retain independent bytes for document/history lifetimes.
enum class PackageFileImportMode { Snapshot, VerifyOnly };

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
	// Generated assets own their content; they never rely on a temporary file.
	QByteArray inlineBytes;
	bool hasInlineBytes = false;
	PackageFileIdentityPtr sourceIdentity;
	QString sourceError;
	QDateTime sourceModifiedUtc;
	// An original source occurrence, retained through replacement and rename.
	// -1 keeps the existing unambiguous path contract. The saved path is also
	// checked when replaying, so removing an earlier rename cannot retarget it.
	int sourceOrdinal = -1;
	// Folder operations verify the captured subtree before atomic replay.
	QString sourceTreeIdentity;
	// Native WAD edits can set a directory type and position a new lump before
	// a unique namespace end marker. -1 retains the existing/default type.
	int wadLumpType = -1;
	QString wadInsertBefore;
	QString wadNamespace;
};

// History stores insertion/removal deltas, not a copy of the complete plan for
// each edit. Payloads and their captured identities remain implicitly shared.
struct PackageStageHistoryChange {
	PackageStageOperation operation;
	qsizetype index = 0;
	bool inserted = true;
};

struct PackageStageHistoryStep {
	QString label;
	QVector<PackageStageHistoryChange> changes;
	quint64 beforeRevision = 0;
	quint64 afterRevision = 0;
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
// `sourceFilePath` (an independently retained import) or the base archive entry named
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
	// it indexes the WAD directory; for other sources it uses their directory
	// ordinal when available, falling back to the base reader's entry order.
	int sourceOrdinal = -1;
	// Index in the immutable base reader snapshot; independent of WAD order.
	qsizetype sourceReaderIndex = -1;
	// Transient position in the metadata reader supplied to base adoption.
	// Subset selectors use this even when captured physical order differs.
	// Drafts do not persist it; their staged readers supply a fresh entry order.
	qsizetype sourceMetadataIndex = -1;
	// WAD2/WAD3 directory "type" byte, preserved from a texture WAD source.
	quint8 wadLumpType = 0;
	QString wadInsertBefore;
	QString wadNamespace;
	QByteArray inlineBytes;
	bool hasInlineBytes = false;
	PackageFileIdentityPtr sourceIdentity;
	// Explicitly unavailable original bytes retained by a version-3 draft.
	// This record preserves identity/history, never a readable empty payload.
	QString unavailableReason;
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
	bool sizeOverflow = false;
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
	// Failed plan preparation exposes unavailable totals, never a partial sum.
	bool totalsAvailable = false;
	bool beforeSizeOverflow = false, afterSizeOverflow = false;
	bool canSave = false;
	QStringList blockedMessages;
};

enum class PackageWritePhase { VerifySources, Measure, Write, VerifyDeterminism, Manifest, Publish, CheckIndex };

struct PackageWriteRequest {
	PackageArchiveFormat format = PackageArchiveFormat::Unknown;
	QString destinationPath;
	bool allowOverwrite = false;
	// Opt-in save over an existing package, including the package that is
	// currently open. Without it `writeArchive` keeps refusing a destination
	// that resolves to the source path.
	//
	// Output and original bytes are verified before atomic replacement. A
	// journal and independent original copy support interrupted-save recovery.
	bool allowInPlaceOverwrite = false;
	// Where the replaced bytes are copied. Empty means `<destination>.bak`.
	// Also applies when allowOverwrite replaces a different existing output.
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
	// Checked within reads/compression and before commit. Cancelling before
	// publication leaves the destination untouched.
	std::function<bool()> isCancelled;
	// Worker-thread notification before each file and after each writing pass.
	// Determinism verification starts a second pass. No UI calls in callbacks.
	std::function<void(int completed, int total, const QString& virtualPath)> progress;
	// Per-entry byte progress, including compression measurement and source
	// verification. Totals restart per phase/file; CheckIndex reports records
	// instead of bytes. Runs on the caller's thread.
	std::function<void(PackageWritePhase phase, const QString& path, quint64 completed, quint64 total)> byteProgress;
	// WAD output magic: empty inherits the source WAD magic (PWAD/IWAD/WAD2/WAD3),
	// falling back to PWAD. Stable technical identifier, never translated.
	QString wadMagic;
	// Optional reviewed destination guard, forwarded to atomic publication.
	// An empty hash means the reviewed path did not exist.
	std::optional<QString> expectedDestinationSha256;
	// Both real writes and no-write dry runs must fit the selected reopening
	// policy. Callers may lower these limits, never raise the hard ceilings.
	PackageIndexLimits indexLimits;
};

struct PackageWriteReport {
	QString sourcePath;
	QString outputPath;
	QString manifestPath;
	// Verified original copy, empty when nothing was replaced.
	QString backupPath;
	// Files retained after a failed commit/restore or backup cleanup.
	QStringList recoveryPaths;
	// True when the output replaced an existing file through the backup path.
	bool overwroteInPlace = false;
	// Authoritative even if optional manifest/backup finalization reports a warning.
	bool outputCommitted = false;
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
	bool cancelled = false;
	QStringList warnings;
	QStringList blockedMessages;

	[[nodiscard]] bool succeeded() const;
};

// Retained payload storage across the base, current edits, undo/redo and an
// open group. These are per-model budgets; independent snapshots own their
// lifetimes. Metadata/index limits are separate. Callers may only lower caps.
struct PackageStagingContentLimits {
	static constexpr qint64 generatedCeiling = 256ll * 1024 * 1024;
	static constexpr qint64 fingerprintCeiling = 128ll * 1024 * 1024;
	qint64 maximumGeneratedBytes = generatedCeiling;
	qint64 maximumFingerprintBytes = fingerprintCeiling;
};
struct PackageStagingContentUsage {
	qint64 generatedBytes = 0;
	qint64 fingerprintBytes = 0;
};

// Logical retained records and index/text bytes. One active slot is reserved
// for each operation reachable through history, so Undo never needs extra room.
// Generated content, hashes, and derived plan/snapshot projections are separate.
struct PackageStagingMetadataLimits {
	static constexpr qsizetype recordCeiling = 1000000;
	static constexpr qint64 metadataCeiling = 128ll * 1024 * 1024;
	qsizetype maximumRecords = recordCeiling;
	qint64 maximumMetadataBytes = metadataCeiling;
};
struct PackageStagingMetadataUsage {
	qsizetype records = 0;
	qint64 metadataBytes = 0;
};

// Bounds each materialized plan representation before it grows. Text is
// logical UTF-16, not allocator/process memory; retained payloads are separate.
struct PackageStagingPlanLimits {
	static constexpr qsizetype recordCeiling = 250000;
	static constexpr qsizetype indexKeyCeiling = 500000;
	static constexpr qint64 metadataCeiling = 128ll * 1024 * 1024;
	qsizetype maximumRecords = recordCeiling;
	qsizetype maximumIndexKeys = indexKeyCeiling;
	qint64 maximumMetadataBytes = metadataCeiling;
};

class PackageStagingModel final {
public:
	explicit PackageStagingModel(const PackageStagingContentLimits& limits = {}, const PackageStagingMetadataLimits& metadata = {},
		const PackageStagingPlanLimits& plan = {}, const PackageIndexLimits& view = {})
		: m_contentLimits(limits), m_metadataLimits(metadata), m_planLimits(plan), m_viewLimits(view) {}
	[[nodiscard]] PackageStagingContentLimits contentLimits() const { return m_contentLimits; }
	[[nodiscard]] PackageStagingMetadataLimits metadataLimits() const { return m_metadataLimits; }
	[[nodiscard]] PackageStagingPlanLimits planLimits() const { return m_planLimits; }
	// Browser rows, warnings, implied folders and text share the index policy.
	// Snapshot admission does not charge source fingerprints.
	[[nodiscard]] PackageIndexLimits viewLimits() const { return m_viewLimits; }
	bool metadataUsage(PackageStagingMetadataUsage* usage, QString* error = nullptr, const PackageReadControl& control = {}) const;
	bool contentUsage(PackageStagingContentUsage* usage, QString* error = nullptr, const PackageReadControl& control = {}) const;
	// Starts an unsaved document without a filesystem source; failure preserves it.
	bool createEmpty(PackageArchiveFormat format, const QString& wadMagic = {}, QString* error = nullptr);
	// Admit a private candidate before publication. Cancellation covers base
	// metadata, WAD directories, sorting, rebasing and the final browser view.
	// Archive adapters retain owned virtual providers. Other readers describe a
	// filesystem base captured now; missing/mismatched backing stays unavailable,
	// never deferred to an unrelated later file. Use loadSnapshot for virtual bytes.
	bool loadBaseArchive(const PackageArchiveReader& archive, QString* error = nullptr, const PackageReadControl& control = {});
	// Build an independent save-as plan containing exactly these files. Names
	// must be unambiguous and present; failure leaves this model unchanged.
	// Path selectors require a unique file. Exact indexes refer to the supplied
	// immutable snapshot; WAD subsets include required map/namespace members.
	bool loadBaseArchiveSubset(const PackageArchiveReader& archive, const QStringList& virtualPaths, QString* error = nullptr, const PackageReadControl& control = {});
	bool loadBaseArchiveSubsetAt(const PackageArchiveReader& archive, const QVector<qsizetype>& entryIndexes,
		QString* error = nullptr, PackageSubsetReview* review = nullptr, const PackageReadControl& control = {});
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
	// Prepare the derived view on a worker before querying it. Cancellation
	// publishes no partial cache and leaves edits, history and revision intact.
	bool preparePlan(QString* error = nullptr, const PackageReadControl& control = {}) const;
	[[nodiscard]] QVector<PackageStagedEntry> plannedEntries() const;
	[[nodiscard]] PackageStagingSummary summary() const;
	[[nodiscard]] QVector<PackageCompositionBucket> beforeComposition() const;
	[[nodiscard]] QVector<PackageCompositionBucket> afterComposition() const;
	[[nodiscard]] QByteArray manifestJson(QString* error = nullptr, const PackageReadControl& control = {}) const;

	// Reads the bytes for a planned entry on demand. Directory entries yield an
	// empty byte array.
	bool entryBytes(const PackageStagedEntry& entry, QByteArray* out, QString* error = nullptr, qint64 maxBytes = -1) const;
	bool streamEntry(const PackageStagedEntry& entry, const std::function<bool(QByteArrayView)>& sink,
		QString* error = nullptr, const PackageReadControl& control = {}) const;
	bool verifySources(QString* error = nullptr, const PackageReadControl& control = {}) const;

	bool addFile(const QString& sourceFilePath, const QString& virtualPath, QString* error = nullptr, PackageStageConflictResolution resolution = PackageStageConflictResolution::Block, const PackageReadControl& control = {}, PackageFileImportMode mode = PackageFileImportMode::Snapshot);
	bool addBytes(const QByteArray& bytes, const QString& virtualPath, QString* error = nullptr, PackageStageConflictResolution resolution = PackageStageConflictResolution::Block);
	// Atomic native-lump staging, including required Doom namespace markers.
	// namespaceId is empty for texture WADs, or flat/patch/sprite for Doom.
	// Replacement stays in its existing namespace; ambiguous names are refused.
	bool addWadBytes(const QByteArray& bytes, const QString& name, const QString& namespaceId, int lumpType,
		bool replaceExisting, QString* error = nullptr);
	bool replaceFile(const QString& virtualPath, const QString& sourceFilePath, QString* error = nullptr, const PackageReadControl& control = {}, PackageFileImportMode mode = PackageFileImportMode::Snapshot);
	bool renameEntry(const QString& virtualPath, const QString& targetVirtualPath, QString* error = nullptr, PackageStageConflictResolution resolution = PackageStageConflictResolution::Block, const PackageReadControl& control = {});
	bool deleteEntry(const QString& virtualPath, QString* error = nullptr, PackageStageConflictResolution resolution = PackageStageConflictResolution::Block, const PackageReadControl& control = {});
	bool createDirectory(const QString& virtualPath, QString* error = nullptr, const PackageReadControl& control = {});
	bool renameDirectory(const QString& virtualPath, const QString& targetVirtualPath, QString* error = nullptr, const PackageReadControl& control = {});
	bool deleteDirectory(const QString& virtualPath, QString* error = nullptr, const PackageReadControl& control = {});
	bool replaceOccurrence(int sourceOrdinal, const QString& sourceFilePath, QString* error = nullptr, const PackageReadControl& control = {}, PackageFileImportMode mode = PackageFileImportMode::Snapshot);
	bool renameOccurrence(int sourceOrdinal, const QString& targetVirtualPath, QString* error = nullptr, PackageStageConflictResolution resolution = PackageStageConflictResolution::Block, const PackageReadControl& control = {});
	bool deleteOccurrence(int sourceOrdinal, QString* error = nullptr, PackageStageConflictResolution resolution = PackageStageConflictResolution::Block, const PackageReadControl& control = {});
	// Removes exactly one staged operation.
	bool clearOperation(const QString& operationId, QString* error = nullptr, const PackageReadControl& control = {});
	// Nested groups become one undo step. Cancelling a group rolls back the
	// entire open group. No-op and failed edits do not consume history.
	bool beginOperationGroup(const QString& label, QString* error = nullptr, const PackageReadControl& control = {});
	// Outermost commit admits the complete browser view once. Refusal or
	// cancellation rolls back the whole group, retaining revision and redo.
	bool endOperationGroup(bool commit = true, QString* error = nullptr, const PackageReadControl& control = {});
	[[nodiscard]] bool canUndo() const;
	[[nodiscard]] bool canRedo() const;
	[[nodiscard]] QString undoLabel() const;
	[[nodiscard]] QString redoLabel() const;
	bool undo();
	bool redo();
	[[nodiscard]] bool isModified() const;
	void markSaved();
	[[nodiscard]] quint64 revision() const;
	[[nodiscard]] QString draftPath() const;

	bool exportManifest(const QString& outputPath, QString* error = nullptr) const;
	// Writes the plan. After a successful in-place overwrite the model still
	// describes the package that was replaced, so reload the base archive
	// before staging anything else on top of it.
	PackageWriteReport writeArchive(const PackageWriteRequest& request) const;

private:
	friend class PackageStagingArchive;
	friend class PackageDraft;
	friend class PackageWadGroupEditor;
	bool loadBaseArchiveUnchecked(const PackageArchiveReader& archive, QString* error, const PackageReadControl& control);
	static PackageStagingMetadataUsage entryMetadata(const PackageStagedEntry& entry);
	static PackageStagingMetadataUsage operationMetadata(const PackageStageOperation& operation);
	static PackageStagingMetadataUsage historyMetadata(const QString& label);
	bool metadataFits(const PackageStagingMetadataUsage& usage, const PackageStagingMetadataUsage& addition, QString* error = nullptr) const;
	// UINT64_MAX stays invalid on disk; edits reserve the next usable serial
	// before changing content or history. Undo/Redo reuse existing revisions.
	static constexpr quint64 maximumSerial = std::numeric_limits<quint64>::max() - 1;
	bool admitHistoryEdit(bool needsOperationId, QString* error) const;
	void recordHistory(PackageStageHistoryChange change, const QString& label);
	void commitHistory(PackageStageHistoryStep step);
	void applyHistory(const PackageStageHistoryStep& step, bool forward);
	void resetHistory();
	QString nextOperationId();
	bool appendOperation(PackageStageOperation operation, QString* error, const PackageReadControl& control = {}, PackageFileImportMode mode = PackageFileImportMode::Snapshot);
	bool appendOccurrenceOperation(PackageStageOperation operation, QString* error, const PackageReadControl& control = {}, PackageFileImportMode mode = PackageFileImportMode::Snapshot);
	bool appendDirectoryOperation(PackageStageOperation operation, QString* error, const PackageReadControl& control);
	void invalidatePlan();
	void ensurePlan() const;
	bool admitView(QString* error, const PackageReadControl& control = {}) const;
	bool protectsInputPath(const QString& path) const;
	bool visitProtectedInputPaths(const std::function<bool(const QString&)>& visitor, QString* error, const PackageReadControl& control) const;
	bool freezeInputProtections(QString* error, const PackageReadControl& control, const QString& implicitDirectory = {},
		const PackageStagingModel* source = nullptr);

	PackageStagingContentLimits m_contentLimits;
	PackageStagingMetadataLimits m_metadataLimits;
	PackageStagingPlanLimits m_planLimits;
	PackageIndexLimits m_viewLimits;
	mutable PackageStagingMetadataUsage m_metadataUsage;
	mutable bool m_metadataUsageValid = false;
	mutable bool m_metadataUsageExact = false;
	// Appends update a conservative bound without walking the whole document.
	// Ownership changes invalidate it; public usage and a full allowance force
	// an exact recount before reporting or reclaiming unreachable history.
	mutable PackageStagingContentUsage m_contentUsage;
	mutable bool m_contentUsageValid = false;
	mutable bool m_contentUsageExact = false;
	QString m_sourcePath;
	// Flattened plans keep original import destinations protected even when
	// their entries read independently retained files and history is reset.
	QStringList m_protectedInputPaths;
	PackageArchiveFormat m_sourceFormat = PackageArchiveFormat::Unknown;
	QString m_sourceWadMagic;
	QVector<PackageWadLumpLocation> m_sourceWadLumps;
	bool m_loaded = false;
	QVector<PackageStagedEntry> m_baseEntries;
	QVector<PackageStagedEntry> m_baseDirectories;
	QVector<PackageStageOperation> m_operations;
	QVector<PackageStageConflict> m_baseConflicts;
	quint64 m_operationSerial = 0;
	QVector<PackageStageHistoryStep> m_history;
	qsizetype m_historyCursor = 0;
	int m_groupDepth = 0;
	PackageStageHistoryStep m_group;
	quint64 m_revision = 0;
	quint64 m_revisionSerial = 0;
	quint64 m_savedRevision = 0;
	QString m_draftPath;
	PackageFileIdentityPtr m_draftIdentity;
	mutable PackageStagingSummary m_summary;
	mutable QVector<PackageCompositionBucket> m_beforeComposition, m_afterComposition;
	mutable bool m_planValid = false;
	mutable QString m_planError;
	mutable QVector<PackageStagedEntry> m_planEntries;
	mutable QVector<PackageStageConflict> m_planConflicts;
	std::shared_ptr<PackageArchive> m_baseReader;
};

// A read-only value snapshot of the current plan, shared by dependency audits
// and selected-asset exports. Later edits to the live plan cannot change it.
// On-disk sources remain lazy; generated bytes are owned by the snapshot.
enum class PackageStagingReadMode {
	CompletePlan,
	// Inspect the effective entries even when other edits are blocked. Keeps
	// unreadable source rows and supplies synthetic browser directories. A
	// blocked inspection snapshot cannot become an export plan.
	InspectPlan,
};

class PackageStagingArchive final : public PackageArchiveReader {
public:
	explicit PackageStagingArchive(const PackageStagingModel& staging, PackageStagingReadMode mode = PackageStagingReadMode::CompletePlan,
		const PackageReadControl& control = {}, const PackageIndexLimits& limits = {});
	[[nodiscard]] QVector<PackageLoadWarning> warnings() const;
	[[nodiscard]] PackageArchiveFormat format() const override;
	[[nodiscard]] QString sourcePath() const override;
	[[nodiscard]] QString wadMagic() const override;
	[[nodiscard]] bool isOpen() const override;
	[[nodiscard]] QString errorString() const override { return m_error; }
	[[nodiscard]] QVector<PackageEntry> entries() const override;
	bool readEntryBytes(const QString& virtualPath, QByteArray* out, QString* error, qint64 maxBytes = -1) const override;
	bool readEntryAt(qsizetype index, QByteArray* out, QString* error, qint64 maxBytes = -1) const override;
	bool streamEntryAt(qsizetype index, const std::function<bool(QByteArrayView)>& sink, QString* error,
		const std::function<bool()>& isCancelled = {}) const override;
	bool visitProtectedInputPaths(const std::function<bool(const QString&)>& visitor,
		QString* error = nullptr, const PackageReadControl& control = {}) const override;
private:
	friend class PackageStagingModel;
	PackageStagingModel m_snapshot;
	QVector<PackageStagedEntry> m_entries;
	QVector<PackageEntry> m_metadata;
	bool m_ready = false;
	QString m_error;
};

// Shared GUI/CLI inspection adapter, including conflict diagnostics. Never
// falls back to stale base bytes when a staged read fails.
PackageArchive packagePlannedArchive(const PackageStagingModel& staging, QString* error = nullptr,
	const PackageReadControl& control = {}, const PackageIndexLimits& limits = {});

QString packageStageOperationTypeId(PackageStageOperationType type);
QString packageStageOperationTypeDisplayName(PackageStageOperationType type);
PackageStageOperationType packageStageOperationTypeFromId(const QString& id);
QString packageStageConflictResolutionId(PackageStageConflictResolution resolution);
PackageStageConflictResolution packageStageConflictResolutionFromId(const QString& id);
QString packageTimestampModeId(PackageTimestampMode mode);
PackageTimestampMode packageTimestampModeFromId(const QString& id);
QString packageWriteReportText(const PackageWriteReport& report);

} // namespace vibestudio
