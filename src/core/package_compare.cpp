#include "core/package_compare.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <algorithm>
#include <functional>
#include <limits>

namespace vibestudio {

namespace {

QString normalizedId(QString value)
{
	return value.trimmed().toLower().replace('_', '-');
}

QString trimmedPath(QString path)
{
	while (path.endsWith('/')) {
		path.chop(1);
	}
	return path;
}

// Comparison key. Paths are normalized first so `./a//b.txt` and `a/b.txt` are
// the same entry, then case-folded so the two sides pair up on a
// case-insensitive filesystem exactly as an engine would see them.
QString compareKey(const QString& virtualPath)
{
	const PackageVirtualPath normalized = normalizePackageVirtualPath(virtualPath, false);
	const QString path = normalized.normalizedPath.isEmpty() ? trimmedPath(virtualPath) : normalized.normalizedPath;
	return trimmedPath(path).toCaseFolded();
}

QString displayPath(const QString& virtualPath)
{
	const PackageVirtualPath normalized = normalizePackageVirtualPath(virtualPath, false);
	return normalized.normalizedPath.isEmpty() ? trimmedPath(virtualPath) : trimmedPath(normalized.normalizedPath);
}

qint64 signedDelta(quint64 left, quint64 right)
{
	// Sizes are 64-bit unsigned on both sides, so the difference is taken in
	// the wider domain and clamped rather than wrapped.
	if (right >= left) {
		const quint64 delta = right - left;
		return delta > static_cast<quint64>(std::numeric_limits<qint64>::max())
			? std::numeric_limits<qint64>::max()
			: static_cast<qint64>(delta);
	}
	const quint64 delta = left - right;
	return delta > static_cast<quint64>(std::numeric_limits<qint64>::max())
		? std::numeric_limits<qint64>::min()
		: -static_cast<qint64>(delta);
}

// One side of the comparison, flattened so both an archive and a staged plan
// look the same to the pairing loop.
struct CompareSideEntry {
	QString path;
	QString key;
	PackageEntryKind kind = PackageEntryKind::File;
	quint64 sizeBytes = 0;
	bool readable = true;
	// Repetition of `key` within this side, zero-based.
	int occurrence = 0;
	// Position in the side's own entry list, which is what a positional
	// reader needs.
	int index = 0;
};

using SideHashReader = std::function<bool(const CompareSideEntry&, QString*, QString*, const PackageReadControl&)>;

using CompareStreamer = std::function<bool(const std::function<bool(QByteArrayView)>&)>;

bool hashEntryStream(const CompareSideEntry& entry, const CompareStreamer& read, QString* digest,
	QString* error, const PackageReadControl& control)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	quint64 received = 0;
	const auto cancelled = [&]() { return control.isCancelled && control.isCancelled(); };
	if (control.progress) control.progress(entry.path, 0, static_cast<qint64>(entry.sizeBytes));
	if (cancelled()) return false;
	bool wrongSize = false;
	const bool succeeded = read([&](QByteArrayView chunk) {
		if (static_cast<quint64>(chunk.size()) > entry.sizeBytes - received) { wrongSize = true; return false; }
		while (!chunk.isEmpty()) {
			if (cancelled()) return false;
			const qsizetype count = qMin<qsizetype>(65536, chunk.size());
			hash.addData(chunk.first(count));
			received += static_cast<quint64>(count);
			chunk = chunk.sliced(count);
			if (control.progress) control.progress(entry.path, static_cast<qint64>(received), static_cast<qint64>(entry.sizeBytes));
		}
		return !cancelled();
	});
	if (wrongSize || (succeeded && received != entry.sizeBytes)) {
		if (error) *error = QCoreApplication::translate("VibeStudioPackageCompare", "Entry size changed while comparing it.");
		return false;
	}
	if (!succeeded || cancelled()) return false;
	*digest = QString::fromLatin1(hash.result().toHex());
	return true;
}

struct CompareSide {
	QString sourcePath;
	PackageArchiveFormat format = PackageArchiveFormat::Unknown;
	QVector<CompareSideEntry> entries;
	SideHashReader hash;
};

void assignOccurrences(QVector<CompareSideEntry>* entries)
{
	if (!entries) {
		return;
	}
	QHash<QString, int> seen;
	for (qsizetype index = 0; index < entries->size(); ++index) {
		CompareSideEntry& entry = (*entries)[index];
		entry.index = static_cast<int>(index);
		entry.occurrence = seen[entry.key]++;
	}
}

CompareSide sideFromArchive(const PackageArchiveReader& archive, bool includeDirectories)
{
	CompareSide side;
	side.sourcePath = archive.sourcePath();
	side.format = archive.format();
	const auto listed = archive.entries();
	QVector<qsizetype> indices;
	for (qsizetype index = 0; index < listed.size(); ++index) {
		const PackageEntry& entry = listed.at(index);
		const bool directory = entry.kind == PackageEntryKind::Directory;
		if (directory && !includeDirectories) {
			continue;
		}
		// Synthetic directories are invented by the reader from file paths, so
		// comparing them would report differences that no package actually has.
		if (directory && entry.storageMethod == QStringLiteral("synthetic")) {
			continue;
		}
		CompareSideEntry flat;
		flat.path = displayPath(entry.virtualPath);
		flat.key = compareKey(entry.virtualPath);
		if (flat.key.isEmpty()) {
			continue;
		}
		flat.kind = entry.kind;
		flat.sizeBytes = directory ? 0 : entry.sizeBytes;
		flat.readable = !directory && entry.readable;
		side.entries.push_back(flat);
		indices.append(index);
	}
	assignOccurrences(&side.entries);

	side.hash = [&archive, indices](const CompareSideEntry& entry, QString* digest, QString* error, const PackageReadControl& control) {
		return hashEntryStream(entry, [&](const auto& consume) {
			return archive.streamEntryAt(indices.at(entry.index), consume, error, control.isCancelled);
		}, digest, error, control);
	};
	return side;
}

CompareSide sideFromPlan(const PackageStagingModel& plan, bool includeDirectories)
{
	CompareSide side;
	side.sourcePath = plan.sourcePath();
	side.format = plan.sourceFormat();
	const QVector<PackageStagedEntry> planned = plan.plannedEntries();
	QVector<PackageStagedEntry> kept;
	for (const PackageStagedEntry& entry : planned) {
		const bool directory = entry.kind == PackageEntryKind::Directory;
		if (directory && !includeDirectories) {
			continue;
		}
		CompareSideEntry flat;
		flat.path = displayPath(entry.virtualPath);
		flat.key = compareKey(entry.virtualPath);
		if (flat.key.isEmpty()) {
			continue;
		}
		flat.kind = entry.kind;
		flat.sizeBytes = directory ? 0 : entry.sizeBytes;
		flat.readable = !directory;
		side.entries.push_back(flat);
		kept.push_back(entry);
	}
	assignOccurrences(&side.entries);

	side.hash = [&plan, kept](const CompareSideEntry& entry, QString* digest, QString* error, const PackageReadControl& control) {
		if (entry.index < 0 || entry.index >= kept.size()) {
			if (error) *error = QCoreApplication::translate("VibeStudioPackageCompare", "Planned entry is out of range.");
			return false;
		}
		return hashEntryStream(entry, [&](const auto& consume) {
			PackageReadControl readControl; readControl.isCancelled = control.isCancelled;
			return plan.streamEntry(kept.at(entry.index), consume, error, readControl);
		}, digest, error, control);
	};
	return side;
}

struct ContentOutcome {
	PackageCompareContent content = PackageCompareContent::NotCompared;
	bool changed = false;
	QString leftHash;
	QString rightHash;
	QString noteId;
	QString warning;
};

ContentOutcome compareContent(const CompareSide& left, const CompareSideEntry& leftEntry, const CompareSide& right, const CompareSideEntry& rightEntry, const PackageCompareRequest& request, qint64 maxEntryBytes)
{
	ContentOutcome outcome;
	if (leftEntry.kind == PackageEntryKind::Directory || rightEntry.kind == PackageEntryKind::Directory) {
		outcome.noteId = QStringLiteral("directory");
		return outcome;
	}
	if (leftEntry.sizeBytes != rightEntry.sizeBytes) {
		outcome.content = PackageCompareContent::SizeOnly;
		outcome.changed = true;
		return outcome;
	}
	if (request.metadataOnly) {
		outcome.noteId = QStringLiteral("metadata-only");
		return outcome;
	}
	if (!leftEntry.readable || !rightEntry.readable) {
		outcome.noteId = leftEntry.readable ? QStringLiteral("unreadable-right") : QStringLiteral("unreadable-left");
		return outcome;
	}
	if (leftEntry.sizeBytes > static_cast<quint64>(maxEntryBytes)) {
		outcome.noteId = QStringLiteral("entry-too-large");
		return outcome;
	}

	PackageReadControl control;
	control.isCancelled = request.isCancelled;
	const auto progress = [&](PackageCompareSource source) {
		return [&, source](const QString& path, qint64 done, qint64 total) {
			if (request.byteProgress) request.byteProgress(source, path, static_cast<quint64>(done), static_cast<quint64>(total));
		};
	};
	control.progress = progress(PackageCompareSource::Left);
	QString leftError;
	if (!left.hash || !left.hash(leftEntry, &outcome.leftHash, &leftError, control)) {
		outcome.noteId = QStringLiteral("unreadable-left");
		outcome.warning = QCoreApplication::translate("VibeStudioPackageCompare", "Unable to read %1 from the left package: %2").arg(leftEntry.path, leftError.isEmpty() ? QCoreApplication::translate("VibeStudioPackageCompare", "unknown error") : leftError);
		return outcome;
	}
	control.progress = progress(PackageCompareSource::Right);
	QString rightError;
	if (!right.hash || !right.hash(rightEntry, &outcome.rightHash, &rightError, control)) {
		outcome.noteId = QStringLiteral("unreadable-right");
		outcome.warning = QCoreApplication::translate("VibeStudioPackageCompare", "Unable to read %1 from the right package: %2").arg(rightEntry.path, rightError.isEmpty() ? QCoreApplication::translate("VibeStudioPackageCompare", "unknown error") : rightError);
		return outcome;
	}

	outcome.content = PackageCompareContent::Sha256;
	outcome.changed = outcome.leftHash != outcome.rightHash;
	return outcome;
}

PackageCompareResult compareSides(const CompareSide& left, const CompareSide& right, const PackageCompareRequest& request)
{
	PackageCompareResult result;
	result.leftSource = left.sourcePath;
	result.rightSource = right.sourcePath;
	result.leftLabel = request.leftLabel.trimmed().isEmpty() ? QStringLiteral("left") : request.leftLabel.trimmed();
	result.rightLabel = request.rightLabel.trimmed().isEmpty() ? QStringLiteral("right") : request.rightLabel.trimmed();
	result.leftFormat = left.format;
	result.rightFormat = right.format;
	result.metadataOnly = request.metadataOnly;
	result.includedDirectories = request.includeDirectories;
	result.summary.leftCount = static_cast<int>(left.entries.size());
	result.summary.rightCount = static_cast<int>(right.entries.size());

	const qint64 maxEntryBytes = request.maxEntryBytes > 0 ? request.maxEntryBytes : kPackageCompareDefaultMaxEntryBytes;

	QHash<QString, QVector<int>> leftByKey;
	QHash<QString, QVector<int>> rightByKey;
	for (const CompareSideEntry& entry : left.entries) {
		leftByKey[entry.key].push_back(entry.index);
	}
	for (const CompareSideEntry& entry : right.entries) {
		rightByKey[entry.key].push_back(entry.index);
	}

	QStringList keys;
	keys.reserve(static_cast<qsizetype>(leftByKey.size() + rightByKey.size()));
	QSet<QString> seenKeys;
	for (const CompareSideEntry& entry : left.entries) {
		if (!seenKeys.contains(entry.key)) {
			seenKeys.insert(entry.key);
			keys.push_back(entry.key);
		}
	}
	for (const CompareSideEntry& entry : right.entries) {
		if (!seenKeys.contains(entry.key)) {
			seenKeys.insert(entry.key);
			keys.push_back(entry.key);
		}
	}
	// A total order over the folded keys: the result vector, the summary and
	// the JSON are then a pure function of the two inputs, whatever order the
	// readers happened to list their entries in.
	std::sort(keys.begin(), keys.end());
	int total = 0;
	for (const QString& key : keys) {
		total += static_cast<int>(std::max(leftByKey.value(key).size(), rightByKey.value(key).size()));
	}
	if (request.progress) { request.progress(0, total); }

	for (const QString& key : keys) {
		const QVector<int> leftSlots = leftByKey.value(key);
		const QVector<int> rightSlots = rightByKey.value(key);
		const int pairCount = static_cast<int>(std::max(leftSlots.size(), rightSlots.size()));
		for (int occurrence = 0; occurrence < pairCount; ++occurrence) {
			if (request.progress) { request.progress(static_cast<int>(result.entries.size()), total); }
			if (request.isCancelled && request.isCancelled()) {
				result.cancelled = true;
				result.summary.sizeDelta = signedDelta(result.summary.leftBytes, result.summary.rightBytes);
				return result;
			}
			PackageCompareEntry compared;
			compared.key = key;
			compared.occurrence = occurrence;
			compared.hasLeft = occurrence < static_cast<int>(leftSlots.size());
			compared.hasRight = occurrence < static_cast<int>(rightSlots.size());

			if (compared.hasLeft) {
				const CompareSideEntry& entry = left.entries.at(leftSlots.at(occurrence));
				compared.leftPath = entry.path;
				compared.leftBytes = entry.sizeBytes;
				compared.kind = entry.kind;
				result.summary.leftBytes += entry.sizeBytes;
			}
			if (compared.hasRight) {
				const CompareSideEntry& entry = right.entries.at(rightSlots.at(occurrence));
				compared.rightPath = entry.path;
				compared.rightBytes = entry.sizeBytes;
				if (!compared.hasLeft) {
					compared.kind = entry.kind;
				}
				result.summary.rightBytes += entry.sizeBytes;
			}
			compared.sizeDelta = signedDelta(compared.leftBytes, compared.rightBytes);

			if (!compared.hasRight) {
				compared.status = PackageCompareStatus::Removed;
				++result.summary.removedCount;
				result.entries.push_back(compared);
				continue;
			}
			if (!compared.hasLeft) {
				compared.status = PackageCompareStatus::Added;
				++result.summary.addedCount;
				result.entries.push_back(compared);
				continue;
			}

			const CompareSideEntry& leftEntry = left.entries.at(leftSlots.at(occurrence));
			const CompareSideEntry& rightEntry = right.entries.at(rightSlots.at(occurrence));
			if (leftEntry.kind != rightEntry.kind) {
				compared.status = PackageCompareStatus::Changed;
				compared.contentChanged = true;
				compared.noteId = QStringLiteral("kind-changed");
				++result.summary.changedCount;
				result.entries.push_back(compared);
				continue;
			}

			const ContentOutcome outcome = compareContent(left, leftEntry, right, rightEntry, request, maxEntryBytes);
			if (request.isCancelled && request.isCancelled()) {
				// Only completed rows belong in a partial result's counters.
				result.summary.leftBytes -= compared.leftBytes;
				result.summary.rightBytes -= compared.rightBytes;
				result.summary.sizeDelta = signedDelta(result.summary.leftBytes, result.summary.rightBytes);
				result.cancelled = true;
				return result;
			}
			compared.content = outcome.content;
			compared.contentChanged = outcome.changed;
			compared.leftHash = outcome.leftHash;
			compared.rightHash = outcome.rightHash;
			compared.noteId = outcome.noteId;
			if (!outcome.warning.isEmpty()) {
				result.warnings.push_back(outcome.warning);
			}
			if (outcome.content == PackageCompareContent::NotCompared && compared.kind != PackageEntryKind::Directory) {
				++result.summary.uncomparedCount;
			}

			// A path that differs only in letter case is reported as its own
			// category even when the bytes also moved: on a case-sensitive
			// filesystem the rename is the difference that breaks the package,
			// and `contentChanged` still carries the rest.
			if (leftEntry.path.compare(rightEntry.path, Qt::CaseSensitive) != 0) {
				compared.status = PackageCompareStatus::CaseOnly;
				++result.summary.caseOnlyCount;
			} else if (outcome.changed) {
				compared.status = PackageCompareStatus::Changed;
				++result.summary.changedCount;
			} else if (!request.metadataOnly && outcome.content == PackageCompareContent::NotCompared && compared.kind != PackageEntryKind::Directory) {
				compared.status = PackageCompareStatus::Uncompared;
			} else {
				compared.status = PackageCompareStatus::Identical;
				++result.summary.identicalCount;
			}
			result.entries.push_back(compared);
		}
	}

	result.summary.sizeDelta = signedDelta(result.summary.leftBytes, result.summary.rightBytes);
	result.cancelled = request.isCancelled && request.isCancelled();
	result.completed = !result.cancelled;
	if (request.progress) { request.progress(static_cast<int>(result.entries.size()), total); }
	return result;
}

QJsonObject summaryJson(const PackageCompareSummary& summary)
{
	QJsonObject object;
	object.insert(QStringLiteral("leftCount"), summary.leftCount);
	object.insert(QStringLiteral("rightCount"), summary.rightCount);
	object.insert(QStringLiteral("identicalCount"), summary.identicalCount);
	object.insert(QStringLiteral("addedCount"), summary.addedCount);
	object.insert(QStringLiteral("removedCount"), summary.removedCount);
	object.insert(QStringLiteral("changedCount"), summary.changedCount);
	object.insert(QStringLiteral("caseOnlyCount"), summary.caseOnlyCount);
	object.insert(QStringLiteral("uncomparedCount"), summary.uncomparedCount);
	object.insert(QStringLiteral("leftBytes"), static_cast<double>(summary.leftBytes));
	object.insert(QStringLiteral("rightBytes"), static_cast<double>(summary.rightBytes));
	object.insert(QStringLiteral("sizeDelta"), static_cast<double>(summary.sizeDelta));
	return object;
}

void appendReaderWarnings(PackageCompareResult* result, const PackageArchiveReader& reader)
{
	if (const auto* archive = dynamic_cast<const PackageArchive*>(&reader)) {
		for (const auto& warning : archive->warnings()) {
			result->warnings << QStringLiteral("%1: %2: %3").arg(reader.sourcePath(), warning.virtualPath, warning.message);
		}
	}
}

} // namespace

bool PackageCompareResult::identical() const
{
	return completed && !cancelled && warnings.isEmpty() && (metadataOnly || summary.uncomparedCount == 0)
		&& summary.addedCount == 0 && summary.removedCount == 0 && summary.changedCount == 0 && summary.caseOnlyCount == 0;
}

QString packageCompareStatusId(PackageCompareStatus status)
{
	switch (status) {
	case PackageCompareStatus::Identical:
		return QStringLiteral("identical");
	case PackageCompareStatus::Added:
		return QStringLiteral("added");
	case PackageCompareStatus::Removed:
		return QStringLiteral("removed");
	case PackageCompareStatus::Changed:
		return QStringLiteral("changed");
	case PackageCompareStatus::CaseOnly:
		return QStringLiteral("case-only");
	case PackageCompareStatus::Uncompared:
		return QStringLiteral("uncompared");
	}
	return QStringLiteral("identical");
}

QString packageCompareStatusDisplayName(PackageCompareStatus status)
{
	switch (status) {
	case PackageCompareStatus::Identical:
		return QCoreApplication::translate("VibeStudioPackageCompare", "Identical");
	case PackageCompareStatus::Added:
		return QCoreApplication::translate("VibeStudioPackageCompare", "Added");
	case PackageCompareStatus::Removed:
		return QCoreApplication::translate("VibeStudioPackageCompare", "Removed");
	case PackageCompareStatus::Changed:
		return QCoreApplication::translate("VibeStudioPackageCompare", "Changed");
	case PackageCompareStatus::CaseOnly:
		return QCoreApplication::translate("VibeStudioPackageCompare", "Case-only path difference");
	case PackageCompareStatus::Uncompared:
		return QCoreApplication::translate("VibeStudioPackageCompare", "Not compared");
	}
	return QCoreApplication::translate("VibeStudioPackageCompare", "Identical");
}

PackageCompareStatus packageCompareStatusFromId(const QString& id)
{
	const QString normalized = normalizedId(id);
	if (normalized == QStringLiteral("added")) {
		return PackageCompareStatus::Added;
	}
	if (normalized == QStringLiteral("removed")) {
		return PackageCompareStatus::Removed;
	}
	if (normalized == QStringLiteral("changed")) {
		return PackageCompareStatus::Changed;
	}
	if (normalized == QStringLiteral("case-only") || normalized == QStringLiteral("caseonly")) {
		return PackageCompareStatus::CaseOnly;
	}
	if (normalized == QStringLiteral("uncompared")) { return PackageCompareStatus::Uncompared; }
	return PackageCompareStatus::Identical;
}

QString packageCompareContentId(PackageCompareContent content)
{
	switch (content) {
	case PackageCompareContent::NotCompared:
		return QStringLiteral("not-compared");
	case PackageCompareContent::SizeOnly:
		return QStringLiteral("size");
	case PackageCompareContent::Crc32:
		return QStringLiteral("crc32");
	case PackageCompareContent::Sha256:
		return QStringLiteral("sha256");
	}
	return QStringLiteral("not-compared");
}

QString packageCompareContentDisplayName(PackageCompareContent content)
{
	switch (content) {
	case PackageCompareContent::NotCompared:
		return QCoreApplication::translate("VibeStudioPackageCompare", "Not compared");
	case PackageCompareContent::SizeOnly:
		return QCoreApplication::translate("VibeStudioPackageCompare", "Size");
	case PackageCompareContent::Crc32:
		return QCoreApplication::translate("VibeStudioPackageCompare", "Stored CRC-32");
	case PackageCompareContent::Sha256:
		return QCoreApplication::translate("VibeStudioPackageCompare", "SHA-256 of contents");
	}
	return QCoreApplication::translate("VibeStudioPackageCompare", "Not compared");
}

PackageCompareResult comparePackages(const PackageArchiveReader& left, const PackageArchiveReader& right, const PackageCompareRequest& request)
{
	PackageCompareResult result;
	if (!left.isOpen() || !right.isOpen()) {
		result.leftSource = left.sourcePath();
		result.rightSource = right.sourcePath();
		result.leftLabel = request.leftLabel.trimmed().isEmpty() ? QStringLiteral("left") : request.leftLabel.trimmed();
		result.rightLabel = request.rightLabel.trimmed().isEmpty() ? QStringLiteral("right") : request.rightLabel.trimmed();
		result.metadataOnly = request.metadataOnly;
		result.warnings.push_back(QCoreApplication::translate("VibeStudioPackageCompare", "Both packages must be open to compare them."));
		for (const auto* reader : {&left, &right}) { if (!reader->isOpen() && !reader->errorString().isEmpty()) { result.warnings << reader->errorString(); } }
		return result;
	}
	const CompareSide leftSide = sideFromArchive(left, request.includeDirectories);
	const CompareSide rightSide = sideFromArchive(right, request.includeDirectories);
	result = compareSides(leftSide, rightSide, request);
	appendReaderWarnings(&result, left);
	appendReaderWarnings(&result, right);
	return result;
}

PackageCompareResult comparePackageToPlan(const PackageArchiveReader& left, const PackageStagingModel& plan, const PackageCompareRequest& request)
{
	PackageCompareResult result;
	if (!left.isOpen() || !plan.isLoaded()) {
		result.leftSource = left.sourcePath();
		result.rightSource = plan.sourcePath();
		result.leftLabel = request.leftLabel.trimmed().isEmpty() ? QStringLiteral("left") : request.leftLabel.trimmed();
		result.rightLabel = request.rightLabel.trimmed().isEmpty() ? QStringLiteral("right") : request.rightLabel.trimmed();
		result.metadataOnly = request.metadataOnly;
		result.warnings.push_back(QCoreApplication::translate("VibeStudioPackageCompare", "The package must be open and the plan must be loaded to compare them."));
		if (!left.isOpen() && !left.errorString().isEmpty()) { result.warnings << left.errorString(); }
		return result;
	}
	const CompareSide leftSide = sideFromArchive(left, request.includeDirectories);
	const CompareSide rightSide = sideFromPlan(plan, request.includeDirectories);
	result = compareSides(leftSide, rightSide, request);
	appendReaderWarnings(&result, left);
	for (const auto& conflict : plan.conflicts()) {
		if (conflict.blocking) {
			result.completed = false;
			result.warnings << conflict.message;
		}
	}
	return result;
}

QStringList packageCompareLines(const PackageCompareResult& result)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioPackageCompare", "Package compare");
	if (!result.completed) {
		lines << (result.cancelled ? QCoreApplication::translate("VibeStudioPackageCompare", "Comparison cancelled; results are incomplete.")
			: QCoreApplication::translate("VibeStudioPackageCompare", "Comparison could not be completed."));
	}
	lines << QCoreApplication::translate("VibeStudioPackageCompare", "%1: %2").arg(result.leftLabel, result.leftSource.isEmpty() ? QCoreApplication::translate("VibeStudioPackageCompare", "not available") : result.leftSource);
	lines << QCoreApplication::translate("VibeStudioPackageCompare", "%1: %2").arg(result.rightLabel, result.rightSource.isEmpty() ? QCoreApplication::translate("VibeStudioPackageCompare", "not available") : result.rightSource);
	lines << QCoreApplication::translate("VibeStudioPackageCompare", "Mode: %1").arg(result.metadataOnly ? QCoreApplication::translate("VibeStudioPackageCompare", "metadata only") : QCoreApplication::translate("VibeStudioPackageCompare", "contents"));
	lines << QCoreApplication::translate("VibeStudioPackageCompare", "Entries: %1 -> %2").arg(result.summary.leftCount).arg(result.summary.rightCount);
	lines << QCoreApplication::translate("VibeStudioPackageCompare", "Added: %1").arg(result.summary.addedCount);
	lines << QCoreApplication::translate("VibeStudioPackageCompare", "Removed: %1").arg(result.summary.removedCount);
	lines << QCoreApplication::translate("VibeStudioPackageCompare", "Changed: %1").arg(result.summary.changedCount);
	lines << QCoreApplication::translate("VibeStudioPackageCompare", "Case-only path differences: %1").arg(result.summary.caseOnlyCount);
	lines << QCoreApplication::translate("VibeStudioPackageCompare", "Identical: %1").arg(result.summary.identicalCount);
	if (result.summary.uncomparedCount > 0) {
		lines << QCoreApplication::translate("VibeStudioPackageCompare", "Contents not compared: %1").arg(result.summary.uncomparedCount);
	}
	lines << QCoreApplication::translate("VibeStudioPackageCompare", "Size delta: %1").arg(result.summary.sizeDelta);

	for (const PackageCompareEntry& entry : result.entries) {
		if (entry.status == PackageCompareStatus::Identical) {
			continue;
		}
		const QString path = entry.hasRight ? entry.rightPath : entry.leftPath;
		QString line = QStringLiteral("- [%1] %2").arg(packageCompareStatusId(entry.status), path);
		if (entry.status == PackageCompareStatus::CaseOnly) {
			line += QStringLiteral(" (%1 -> %2)").arg(entry.leftPath, entry.rightPath);
		}
		if (entry.sizeDelta != 0) {
			line += QStringLiteral(" %1%2").arg(entry.sizeDelta > 0 ? QStringLiteral("+") : QString()).arg(entry.sizeDelta);
		}
		if (!entry.noteId.isEmpty()) { line += QStringLiteral(" (%1)").arg(entry.noteId); }
		lines << line;
	}
	if (!result.warnings.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioPackageCompare", "Warnings:");
		for (const QString& warning : result.warnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	return lines;
}

QString packageCompareText(const PackageCompareResult& result)
{
	return packageCompareLines(result).join('\n');
}

QJsonObject packageCompareJson(const PackageCompareResult& result)
{
	QJsonArray entries;
	for (const PackageCompareEntry& entry : result.entries) {
		QJsonObject object;
		object.insert(QStringLiteral("key"), entry.key);
		object.insert(QStringLiteral("occurrence"), entry.occurrence);
		object.insert(QStringLiteral("status"), packageCompareStatusId(entry.status));
		object.insert(QStringLiteral("kind"), packageEntryKindId(entry.kind));
		object.insert(QStringLiteral("leftPath"), entry.leftPath);
		object.insert(QStringLiteral("rightPath"), entry.rightPath);
		object.insert(QStringLiteral("hasLeft"), entry.hasLeft);
		object.insert(QStringLiteral("hasRight"), entry.hasRight);
		object.insert(QStringLiteral("leftBytes"), static_cast<double>(entry.leftBytes));
		object.insert(QStringLiteral("rightBytes"), static_cast<double>(entry.rightBytes));
		object.insert(QStringLiteral("sizeDelta"), static_cast<double>(entry.sizeDelta));
		object.insert(QStringLiteral("leftHash"), entry.leftHash);
		object.insert(QStringLiteral("rightHash"), entry.rightHash);
		object.insert(QStringLiteral("content"), packageCompareContentId(entry.content));
		object.insert(QStringLiteral("contentChanged"), entry.contentChanged);
		object.insert(QStringLiteral("noteId"), entry.noteId);
		entries.append(object);
	}

	QJsonArray warnings;
	for (const QString& warning : result.warnings) {
		warnings.append(warning);
	}

	QJsonObject root;
	root.insert(QStringLiteral("schemaVersion"), 1);
	root.insert(QStringLiteral("leftSource"), result.leftSource);
	root.insert(QStringLiteral("rightSource"), result.rightSource);
	root.insert(QStringLiteral("leftLabel"), result.leftLabel);
	root.insert(QStringLiteral("rightLabel"), result.rightLabel);
	root.insert(QStringLiteral("leftFormat"), packageArchiveFormatId(result.leftFormat));
	root.insert(QStringLiteral("rightFormat"), packageArchiveFormatId(result.rightFormat));
	root.insert(QStringLiteral("metadataOnly"), result.metadataOnly);
	root.insert(QStringLiteral("includedDirectories"), result.includedDirectories);
	root.insert(QStringLiteral("completed"), result.completed);
	root.insert(QStringLiteral("cancelled"), result.cancelled);
	root.insert(QStringLiteral("identical"), result.identical());
	root.insert(QStringLiteral("summary"), summaryJson(result.summary));
	root.insert(QStringLiteral("entries"), entries);
	root.insert(QStringLiteral("warnings"), warnings);
	return root;
}

QByteArray packageCompareJsonBytes(const PackageCompareResult& result)
{
	return QJsonDocument(packageCompareJson(result)).toJson(QJsonDocument::Indented);
}

} // namespace vibestudio
