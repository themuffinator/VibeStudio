#include "core/package_selection.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <limits>
#include <QSet>

namespace vibestudio {

PackageSelectionResult selectPackageFiles(const PackageArchiveReader& archive, const PackageSelectionRequest& request, const PackageReadControl& control)
{
	PackageSelectionResult result;
	if (!archive.isOpen()) {
		result.errors << QCoreApplication::translate("VibeStudioPackageSelection", "Open a package before selecting files.");
		return result;
	}
	if (request.entries.isEmpty() && request.prefixes.isEmpty() && request.query.trimmed().isEmpty() && request.entryIndexes.isEmpty()) {
		result.errors << QCoreApplication::translate("VibeStudioPackageSelection", "Specify entry indexes, paths, folder prefixes, or a filter query for the subset.");
		return result;
	}
	const StudioQuery query = parseStudioQuery(request.query);
	const QStringList unknown = studioQueryUnknownKeys(query, {QStringLiteral("path"), QStringLiteral("name"), QStringLiteral("ext"),
		QStringLiteral("folder"), QStringLiteral("type"), QStringLiteral("storage"), QStringLiteral("size"), QStringLiteral("packed"), QStringLiteral("kind")});
	if (!unknown.isEmpty()) {
		result.errors << QCoreApplication::translate("VibeStudioPackageSelection", "Unknown package filter fields: %1").arg(unknown.join(QStringLiteral(", ")));
		return result;
	}
	QStringList exact;
	QStringList prefixes;
	const auto normalize = [&result](const QStringList& input, QStringList* output, bool prefix) {
		for (const QString& path : input) {
			const PackageVirtualPath normalized = normalizePackageVirtualPath(path, false);
			if (!normalized.isSafe()) {
				result.errors << QCoreApplication::translate("VibeStudioPackageSelection", "Unsafe selection path: %1").arg(path);
				continue;
			}
			QString key = normalized.normalizedPath.toCaseFolded();
			if (prefix) {
				key += QLatin1Char('/');
			}
			if (!output->contains(key)) {
				output->append(key);
			}
		}
	};
	normalize(request.entries, &exact, false);
	normalize(request.prefixes, &prefixes, true);
	if (!result.errors.isEmpty()) {
		return result;
	}
	const auto entries = archive.entries();
	QSet<qsizetype> indexes;
	for (const qsizetype index : request.entryIndexes) {
		if (index < 0 || index >= entries.size() || entries.at(index).kind != PackageEntryKind::File) {
			result.errors << QCoreApplication::translate("VibeStudioPackageSelection", "Entry index is not a file in this snapshot: %1").arg(index); continue;
		}
		indexes.insert(index);
	}
	if (!result.errors.isEmpty()) { return result; }
	const QSet<QString> exactSet(exact.cbegin(), exact.cend());
	QHash<QString, int> occurrences;
	for (const auto& entry : entries) { if (entry.kind == PackageEntryKind::File) { ++occurrences[entry.virtualPath.toCaseFolded()]; } }
	for (const auto& path : exact) {
		if (occurrences.value(path) > 1) {
			result.errors << QCoreApplication::translate("VibeStudioPackageSelection", "Selected path is ambiguous; choose an entry index: %1").arg(path);
		}
	}
	if (!result.errors.isEmpty()) { return result; }
	QSet<QString> found;
	for (qsizetype index = 0; index < entries.size(); ++index) {
		if (control.isCancelled && control.isCancelled()) {
			result.errors << QCoreApplication::translate("VibeStudioPackageSelection", "Package selection cancelled."); return result;
		}
		const PackageEntry& entry = entries.at(index);
		if (entry.kind != PackageEntryKind::File) { continue; }
		const QString key = entry.virtualPath.toCaseFolded();
		bool included = exactSet.isEmpty() && prefixes.isEmpty() && indexes.isEmpty();
		if (indexes.contains(index)) { included = true; }
		if (exactSet.contains(key)) { included = true; found.insert(key); }
		for (const QString& prefix : prefixes) {
			if (key.startsWith(prefix)) { included = true; found.insert(prefix); }
		}
		if (!included || !studioQueryMatches(query, packageEntryQueryProperties(entry), entry.virtualPath)) { continue; }
		if (!entry.readable || !isSafePackageVirtualPath(entry.virtualPath)) {
			result.errors << QCoreApplication::translate("VibeStudioPackageSelection", "Selected file is unreadable or unsafe: %1").arg(entry.virtualPath);
		} else if (entry.sizeBytes > std::numeric_limits<quint64>::max() - result.totalBytes) {
			result.errors << QCoreApplication::translate("VibeStudioPackageSelection", "Selected file sizes exceed the supported total.");
		} else {
			result.paths << entry.virtualPath; result.entryIndexes << index; result.totalBytes += entry.sizeBytes;
		}
	}
	for (const QString& selector : exact + prefixes) {
		if (!found.contains(selector)) {
			result.errors << QCoreApplication::translate("VibeStudioPackageSelection", "Selection matches no files: %1").arg(selector);
		}
	}
	if (result.paths.isEmpty()) {
		result.errors << QCoreApplication::translate("VibeStudioPackageSelection", "No files match the package selection.");
	}
	return result;
}

QJsonObject packageSubsetReviewJson(const PackageSubsetReview& review)
{
	QJsonArray entries;
	for (const auto& member : review.members) {
		entries.append(QJsonObject{{QStringLiteral("entryIndex"), static_cast<qint64>(member.entryIndex)},
			{QStringLiteral("sourceOrdinal"), member.sourceOrdinal}, {QStringLiteral("virtualPath"), member.virtualPath},
			{QStringLiteral("sizeBytes"), QString::number(member.sizeBytes)}, {QStringLiteral("selected"), member.selected}, {QStringLiteral("reason"), member.reason}});
	}
	return {{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("entries"), entries},
		{QStringLiteral("totalBytes"), QString::number(review.totalBytes)}, {QStringLiteral("warnings"), QJsonArray::fromStringList(review.warnings)}};
}

} // namespace vibestudio
