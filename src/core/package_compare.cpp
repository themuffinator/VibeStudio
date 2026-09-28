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

QString compareText(const char* source)
{
	return QCoreApplication::translate("VibeStudioPackageCompare", source);
}

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

QString crcHex(quint32 crc)
{
	return QStringLiteral("%1").arg(crc, 8, 16, QLatin1Char('0'));
}

// One side of the comparison, flattened so both an archive and a staged plan
// look the same to the pairing loop.
struct CompareSideEntry {
	QString path;
	QString key;
	PackageEntryKind kind = PackageEntryKind::File;
	quint64 sizeBytes = 0;
	quint32 crc32 = 0;
	bool hasCrc32 = false;
	bool readable = true;
	// Repetition of `key` within this side, zero-based.
	int occurrence = 0;
	// Position in the side's own entry list, which is what a positional
	// reader needs.
	int index = 0;
};

using SideByteReader = std::function<bool(const CompareSideEntry&, QByteArray*, QString*)>;

struct CompareSide {
	QString sourcePath;
	PackageArchiveFormat format = PackageArchiveFormat::Unknown;
	QVector<CompareSideEntry> entries;
	SideByteReader read;
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
	for (const PackageEntry& entry : archive.entries()) {
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
		flat.crc32 = entry.crc32;
		flat.hasCrc32 = !directory && entry.hasCrc32;
		flat.readable = !directory && entry.readable;
		side.entries.push_back(flat);
	}
	assignOccurrences(&side.entries);

	side.read = [&archive](const CompareSideEntry& entry, QByteArray* out, QString* error) {
		// A reader addresses entries by path only, so a repeated path (Doom
		// WAD lump names repeat once per map) cannot be resolved here. The
		// caller checks `occurrence` before asking.
		return archive.readEntryBytes(entry.path, out, error, -1);
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

	side.read = [&plan, kept](const CompareSideEntry& entry, QByteArray* out, QString* error) {
		if (entry.index < 0 || entry.index >= static_cast<int>(kept.size())) {
			if (error) {
				*error = compareText("Planned entry is out of range.");
			}
			return false;
		}
		// The plan resolves its own entries positionally, so a repeated path
		// reads the right bytes here.
		return plan.entryBytes(kept.at(entry.index), out, error);
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
	if (leftEntry.hasCrc32 && rightEntry.hasCrc32) {
		// Both sides already store a CRC-32, so the bytes never have to be
		// read. Equal sizes have been established above, which is what makes a
		// 32-bit check meaningful rather than a coin flip.
		outcome.content = PackageCompareContent::Crc32;
		outcome.changed = leftEntry.crc32 != rightEntry.crc32;
		outcome.leftHash = crcHex(leftEntry.crc32);
		outcome.rightHash = crcHex(rightEntry.crc32);
		return outcome;
	}
	if (!leftEntry.readable || !rightEntry.readable) {
		outcome.noteId = leftEntry.readable ? QStringLiteral("unreadable-right") : QStringLiteral("unreadable-left");
		return outcome;
	}
	if (leftEntry.occurrence > 0 || rightEntry.occurrence > 0) {
		outcome.noteId = QStringLiteral("duplicate-path");
		return outcome;
	}
	if (static_cast<qint64>(leftEntry.sizeBytes) > maxEntryBytes) {
		outcome.noteId = QStringLiteral("entry-too-large");
		return outcome;
	}

	QByteArray leftBytes;
	QString leftError;
	if (!left.read || !left.read(leftEntry, &leftBytes, &leftError)) {
		outcome.noteId = QStringLiteral("unreadable-left");
		outcome.warning = QCoreApplication::translate("VibeStudioPackageCompare", "Unable to read %1 from the left package: %2").arg(leftEntry.path, leftError.isEmpty() ? compareText("unknown error") : leftError);
		return outcome;
	}
	QByteArray rightBytes;
	QString rightError;
	if (!right.read || !right.read(rightEntry, &rightBytes, &rightError)) {
		outcome.noteId = QStringLiteral("unreadable-right");
		outcome.warning = QCoreApplication::translate("VibeStudioPackageCompare", "Unable to read %1 from the right package: %2").arg(rightEntry.path, rightError.isEmpty() ? compareText("unknown error") : rightError);
		return outcome;
	}

	outcome.content = PackageCompareContent::Sha256;
	outcome.leftHash = QString::fromLatin1(QCryptographicHash::hash(leftBytes, QCryptographicHash::Sha256).toHex());
	outcome.rightHash = QString::fromLatin1(QCryptographicHash::hash(rightBytes, QCryptographicHash::Sha256).toHex());
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

	for (const QString& key : keys) {
		const QVector<int> leftSlots = leftByKey.value(key);
		const QVector<int> rightSlots = rightByKey.value(key);
		const int pairCount = static_cast<int>(std::max(leftSlots.size(), rightSlots.size()));
		for (int occurrence = 0; occurrence < pairCount; ++occurrence) {
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
			compared.content = outcome.content;
			compared.contentChanged = outcome.changed;
			compared.leftHash = outcome.leftHash;
			compared.rightHash = outcome.rightHash;
			compared.noteId = outcome.noteId;
			if (!outcome.warning.isEmpty()) {
				result.warnings.push_back(outcome.warning);
			}
			if (outcome.content == PackageCompareContent::NotCompared) {
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
			} else {
				compared.status = PackageCompareStatus::Identical;
				++result.summary.identicalCount;
			}
			result.entries.push_back(compared);
		}
	}

	result.summary.sizeDelta = signedDelta(result.summary.leftBytes, result.summary.rightBytes);
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

} // namespace

bool PackageCompareResult::identical() const
{
	return summary.addedCount == 0 && summary.removedCount == 0 && summary.changedCount == 0 && summary.caseOnlyCount == 0;
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
	}
	return QStringLiteral("identical");
}

QString packageCompareStatusDisplayName(PackageCompareStatus status)
{
	switch (status) {
	case PackageCompareStatus::Identical:
		return compareText("Identical");
	case PackageCompareStatus::Added:
		return compareText("Added");
	case PackageCompareStatus::Removed:
		return compareText("Removed");
	case PackageCompareStatus::Changed:
		return compareText("Changed");
	case PackageCompareStatus::CaseOnly:
		return compareText("Case-only path difference");
	}
	return compareText("Identical");
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
		return compareText("Not compared");
	case PackageCompareContent::SizeOnly:
		return compareText("Size");
	case PackageCompareContent::Crc32:
		return compareText("Stored CRC-32");
	case PackageCompareContent::Sha256:
		return compareText("SHA-256 of contents");
	}
	return compareText("Not compared");
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
		result.warnings.push_back(compareText("Both packages must be open to compare them."));
		return result;
	}
	const CompareSide leftSide = sideFromArchive(left, request.includeDirectories);
	const CompareSide rightSide = sideFromArchive(right, request.includeDirectories);
	return compareSides(leftSide, rightSide, request);
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
		result.warnings.push_back(compareText("The package must be open and the plan must be loaded to compare them."));
		return result;
	}
	const CompareSide leftSide = sideFromArchive(left, request.includeDirectories);
	const CompareSide rightSide = sideFromPlan(plan, request.includeDirectories);
	return compareSides(leftSide, rightSide, request);
}

QStringList packageCompareLines(const PackageCompareResult& result)
{
	QStringList lines;
	lines << compareText("Package compare");
	lines << QCoreApplication::translate("VibeStudioPackageCompare", "%1: %2").arg(result.leftLabel, result.leftSource.isEmpty() ? compareText("not available") : result.leftSource);
	lines << QCoreApplication::translate("VibeStudioPackageCompare", "%1: %2").arg(result.rightLabel, result.rightSource.isEmpty() ? compareText("not available") : result.rightSource);
	lines << QCoreApplication::translate("VibeStudioPackageCompare", "Mode: %1").arg(result.metadataOnly ? compareText("metadata only") : compareText("contents"));
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
		lines << line;
	}
	if (!result.warnings.isEmpty()) {
		lines << compareText("Warnings:");
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
