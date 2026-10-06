#pragma once

// Package comparison: what actually differs between two idTech packages, or
// between a package and a staged write plan.
//
// Packaging work is full of questions that a file list cannot answer. Did the
// rebuild change anything? What did the staged plan really add? Is the release
// pk3 the same content as the working folder? This module answers them per
// entry: added, removed, changed, identical, plus the case-only category that
// the other four hide.
//
// Case-only differences get their own category on purpose. idTech engines read
// packages on case-sensitive and case-insensitive filesystems alike, and the
// path inside a package is what a `.shader`, a `.map` or a sound call quotes,
// so `textures/Base/Wall.tga` versus `textures/base/wall.tga` is a real
// portability bug: it works on Windows, silently fails on Linux, and mounting
// both packages at once makes one shadow the other in an order that depends on
// the engine. Folding paths for the comparison and then reporting the fold as
// its own result is the only way to both pair the entries up and keep the bug
// visible.
//
// Content comparison reads and verifies the payloads and hashes them with
// SHA-256. Equal CRC metadata alone cannot establish that the bytes are intact.
// `metadataOnly` skips content entirely for a fast pass over packages that are
// too large, or too remote, to read.

#include "core/package_archive.h"
#include "core/package_staging.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

// How one path was accounted for. `CaseOnly` means both sides hold the entry
// but spell its path with different letter case.
enum class PackageCompareStatus {
	Identical,
	Added,
	Removed,
	Changed,
	CaseOnly,
	Uncompared,
};

// Which comparison actually decided `PackageCompareStatus` for one entry.
enum class PackageCompareContent {
	// No content comparison ran: metadata-only mode, a directory record, an
	// unreadable entry, or an entry above the read budget.
	NotCompared,
	// The sizes differ, so no digest was needed.
	SizeOnly,
	// Legacy report value, retained for readers of earlier saved reports.
	Crc32,
	// The bytes were read from both sides and hashed.
	Sha256,
};

// Default I/O budget for one entry during a content comparison. Equal-sized
// entries above it remain unchecked with a reason. Payload memory is bounded
// independently of this budget, including archive-backed and generated plans.
inline constexpr qint64 kPackageCompareDefaultMaxEntryBytes = 256LL * 1024LL * 1024LL;

enum class PackageCompareSource { Left, Right };

struct PackageCompareRequest {
	// Never reads entry contents. Status then comes from size alone, and
	// equal-sized entries are reported as identical with `NotCompared`.
	bool metadataOnly = false;
	// Directory records take part in the comparison. Off by default: most
	// readers synthesise directories from file paths, so comparing them
	// mostly reports differences in how a package was produced.
	bool includeDirectories = false;
	// Per-entry read budget; <= 0 uses kPackageCompareDefaultMaxEntryBytes.
	qint64 maxEntryBytes = kPackageCompareDefaultMaxEntryBytes;
	// Untranslated labels for the two sides, used by the text and JSON
	// renderers. Empty falls back to "left"/"right".
	QString leftLabel;
	QString rightLabel;
	// Called on the caller's thread, within reads and between entries. Cancellation returns a
	// partial result that can never be reported as identical.
	std::function<bool()> isCancelled;
	std::function<void(int completed, int total)> progress;
	// Current entry's verified bytes, restarting on each side/entry. Runs on
	// the caller's thread; callbacks must not manipulate GUI controls directly.
	std::function<void(PackageCompareSource source, const QString& path, quint64 completed, quint64 total)> byteProgress;
};

struct PackageCompareEntry {
	// Case-folded comparison key, shared by both sides.
	QString key;
	// Which repetition of `key` this is, zero-based. Only a Doom WAD normally
	// produces more than one: lump names repeat once per map
	// (https://doomwiki.org/wiki/WAD), so repetitions are paired up in source
	// order rather than collapsed.
	int occurrence = 0;
	// The path exactly as each side spells it; empty on a side that does not
	// hold the entry.
	QString leftPath;
	QString rightPath;
	PackageCompareStatus status = PackageCompareStatus::Identical;
	PackageEntryKind kind = PackageEntryKind::File;
	bool hasLeft = false;
	bool hasRight = false;
	quint64 leftBytes = 0;
	quint64 rightBytes = 0;
	// rightBytes - leftBytes, clamped to qint64.
	qint64 sizeDelta = 0;
	// Lowercase hex. Eight digits for a CRC-32, sixty-four for a SHA-256,
	// empty when no content comparison ran.
	QString leftHash;
	QString rightHash;
	PackageCompareContent content = PackageCompareContent::NotCompared;
	// True when the two sides' bytes are known to differ. Set alongside
	// `CaseOnly`, where the headline difference is the path but the content
	// may have moved too.
	bool contentChanged = false;
	// Untranslated reason token, e.g. "entry-too-large", "unreadable-left",
	// "duplicate-path", "metadata-only". Empty when nothing needed saying.
	QString noteId;
};

struct PackageCompareSummary {
	int leftCount = 0;
	int rightCount = 0;
	int identicalCount = 0;
	int addedCount = 0;
	int removedCount = 0;
	int changedCount = 0;
	int caseOnlyCount = 0;
	quint64 leftBytes = 0;
	quint64 rightBytes = 0;
	// rightBytes - leftBytes across every compared entry.
	qint64 sizeDelta = 0;
	// Entries whose content was never compared, for whatever reason.
	int uncomparedCount = 0;
};

struct PackageCompareResult {
	QString leftSource;
	QString rightSource;
	QString leftLabel;
	QString rightLabel;
	PackageArchiveFormat leftFormat = PackageArchiveFormat::Unknown;
	PackageArchiveFormat rightFormat = PackageArchiveFormat::Unknown;
	bool metadataOnly = false;
	bool includedDirectories = false;
	bool completed = false;
	bool cancelled = false;
	PackageCompareSummary summary;
	// Sorted by `key` then `occurrence`, so two runs over the same inputs
	// produce the same vector and the same JSON.
	QVector<PackageCompareEntry> entries;
	QStringList warnings;

	// True only after a complete comparison with no differences or unreadable
	// content. Metadata-only mode deliberately compares names and sizes only.
	[[nodiscard]] bool identical() const;
};

QString packageCompareStatusId(PackageCompareStatus status);
QString packageCompareStatusDisplayName(PackageCompareStatus status);
PackageCompareStatus packageCompareStatusFromId(const QString& id);
QString packageCompareContentId(PackageCompareContent content);
QString packageCompareContentDisplayName(PackageCompareContent content);

// Compares two open packages.
PackageCompareResult comparePackages(const PackageArchiveReader& left, const PackageArchiveReader& right, const PackageCompareRequest& request = {});

// Compares an open package against what a staging model would write. The plan
// is the right-hand side, so "added" means the plan adds it.
PackageCompareResult comparePackageToPlan(const PackageArchiveReader& left, const PackageStagingModel& plan, const PackageCompareRequest& request = {});

QStringList packageCompareLines(const PackageCompareResult& result);
QString packageCompareText(const PackageCompareResult& result);
QJsonObject packageCompareJson(const PackageCompareResult& result);
// Indented UTF-8 JSON. Byte-for-byte reproducible for the same comparison.
QByteArray packageCompareJsonBytes(const PackageCompareResult& result);

} // namespace vibestudio
