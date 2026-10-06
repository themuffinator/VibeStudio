#include "core/package_browser.h"
#include "core/package_index_p.h"
#include "core/package_plan_p.h"
#include "core/asset_formats.h"
#include "core/package_preview.h"

#include <QCoreApplication>
#include <QHash>
#include <utility>
#include <algorithm>

namespace vibestudio {

namespace {
QString compositionId(const PackageEntry& entry)
{
	if (entry.kind == PackageEntryKind::Directory) { return QStringLiteral("directory"); }
	const auto hint = entry.typeHint.toCaseFolded();
	const auto* format = assetFormatForPath(entry.virtualPath);
	const auto kind = assetPreviewKindForEntry(entry.virtualPath, entry.typeHint);
	if (entry.nestedArchiveCandidate || (format && format->readCapability == QStringLiteral("archive"))) { return QStringLiteral("archive"); }
	if (hint.contains(QStringLiteral("image")) || kind == AssetPreviewKind::Image) { return QStringLiteral("image"); }
	if (hint.contains(QStringLiteral("text")) || kind == AssetPreviewKind::Text) { return QStringLiteral("text"); }
	if (hint.contains(QStringLiteral("audio")) || kind == AssetPreviewKind::Audio) { return QStringLiteral("audio"); }
	if (hint.contains(QStringLiteral("model")) || kind == AssetPreviewKind::Model) { return QStringLiteral("model"); }
	if (entry.virtualPath.endsWith(QStringLiteral(".bsp"), Qt::CaseInsensitive)) { return QStringLiteral("map"); }
	return QStringLiteral("binary");
}
} // namespace

std::shared_ptr<const PackageBrowserIndex> preparePackageBrowserIndex(const PackageArchive& archive,
	QString* error, const PackageReadControl& control)
{
	if (error) { error->clear(); }
	if (!archive.isOpen()) { if (error) { *error = archive.errorString(); } return {}; }
	PackagePlanWork work(control, error, QCoreApplication::translate("VibeStudioPackageBrowser", "Indexing package entries"));
	if (!work.checkpoint()) { return {}; }
	auto result = std::make_shared<PackageBrowserIndex>();
	result->entries = archive.entries();
	const auto count = result->entries.size();
	result->summary.sourcePath = archive.sourcePath(); result->summary.format = archive.format();
	result->summary.warningCount = static_cast<int>(archive.warnings().size());
	if (count > PackageIndexLimits{}.maximumEntries) {
		work.refuse(QCoreApplication::translate("VibeStudioPackageBrowser", "Package browser entry limit exceeded.")); return {};
	}
	result->summary.entryCount = static_cast<int>(count);
	// Working keys and retained folder paths share an independent text bound.
	// Case folding may expand Unicode; charge the actual owned UTF-16 keys.
	constexpr qint64 keyLimit = 128ll * 1024 * 1024;
	qint64 keyBytes = 0;
	QHash<QString, qsizetype> children, occurrences;
	const auto chargeKey = [&](const QString& key) {
		const auto bytes = static_cast<qint64>(key.size()) * 2;
		if (bytes > keyLimit - keyBytes) {
			return work.refuse(QCoreApplication::translate("VibeStudioPackageBrowser", "Package browser index text limit exceeded."));
		}
		keyBytes += bytes; return true;
	};
	const auto add = [&](auto& counts, const QString& key) {
		auto found = counts.find(key);
		if (found != counts.end()) { ++found.value(); return true; }
		if (!chargeKey(key)) { return false; }
		counts.insert(key, 1); return true;
	};
	result->folders.append(PackageBrowserFolder{}); result->folderLookup.insert(QString(), 0);
	const auto folderFor = [&](const QString& path) -> qsizetype {
		QStringList missing;
		QString ancestor = path;
		auto found = result->folderLookup.constFind(ancestor);
		while (found == result->folderLookup.cend()) {
			if (!work.checkpoint()) { return -1; }
			missing.append(ancestor); ancestor = packagePlanParent(ancestor);
			found = result->folderLookup.constFind(ancestor);
		}
		qsizetype parent = found.value();
		for (auto at = missing.crbegin(); at != missing.crend(); ++at) {
			if (!work.checkpoint() || !chargeKey(*at)) { return -1; }
			if (result->folders.size() - 1 >= PackageIndexLimits{}.maximumEntries) {
				work.refuse(QCoreApplication::translate("VibeStudioPackageBrowser", "Package browser entry limit exceeded.")); return -1;
			}
			const auto index = result->folders.size();
			PackageBrowserFolder node; node.path = *at; node.parent = parent;
			result->folders.append(std::move(node));
			result->folders[parent].children.append(index);
			result->folderLookup.insert(*at, index); parent = index;
		}
		return parent;
	};
	for (qsizetype at = 0; at < count; ++at) {
		const auto& entry = std::as_const(result->entries).at(at);
		const auto parent = packagePlanParent(entry.virtualPath);
		if (!work.checkpoint() || !add(children, parent) || !add(occurrences, entry.virtualPath.toCaseFolded())) { return {}; }
		const bool directory = entry.kind == PackageEntryKind::Directory;
		if (directory) { ++result->summary.directoryCount; } else { ++result->summary.fileCount; }
		if (entry.nestedArchiveCandidate) { ++result->summary.nestedArchiveCount; }
		const auto folder = folderFor(directory ? entry.virtualPath : parent);
		if (folder < 0) { return {}; }
		if (directory) {
			// A repeated explicit directory must not hide an earlier diagnostic.
			auto& node = result->folders[folder];
			if (node.entry < 0 || result->entries.at(node.entry).note.isEmpty()) { node.entry = at; }
		}
		const auto id = compositionId(entry);
		auto bucket = std::find_if(result->composition.begin(), result->composition.end(), [&](const auto& value) { return value.id == id; });
		if (bucket == result->composition.end()) { result->composition.append({id, 0, 0}); bucket = result->composition.end() - 1; }
		++bucket->count;
		if (!directory) {
			// Oversized metadata remains inspectable so the user can remove or
			// replace offending entries. Never publish wrapped totals or ratios.
			accumulatePackageBytes(entry.sizeBytes, &bucket->bytes, &bucket->sizeOverflow);
			accumulatePackageBytes(entry.sizeBytes, &result->summary.totalSizeBytes, &result->summary.totalSizeOverflow);
		}
	}
	for (qsizetype parent = 0; parent < result->folders.size(); ++parent) {
		if (!work.checkpoint()) { return {}; }
		auto& siblings = result->folders[parent].children;
		if (!sortPackagePlan(&siblings, [&](qsizetype left, qsizetype right) {
			return QString::compare(result->folders.at(left).path, result->folders.at(right).path, Qt::CaseInsensitive) < 0;
		}, work)) { return {}; }
		for (qsizetype row = 0; row < siblings.size(); ++row) {
			if (!work.checkpoint()) { return {}; }
			result->folders[siblings.at(row)].row = row;
		}
	}
	// At most eight fixed categories; sorting these never walks the entries.
	std::stable_sort(result->composition.begin(), result->composition.end(), [](const auto& left, const auto& right) {
		if (left.sizeOverflow != right.sizeOverflow) { return left.sizeOverflow; }
		return left.bytes == right.bytes ? left.count > right.count : left.bytes > right.bytes;
	});
	result->childCounts.reserve(count); result->occurrences.reserve(count);
	for (const auto& entry : std::as_const(result->entries)) {
		if (!work.checkpoint()) { return {}; }
		result->childCounts.append(children.value(entry.virtualPath));
		result->occurrences.append(occurrences.value(entry.virtualPath.toCaseFolded()));
	}
	return work.finish() ? result : nullptr;
}

bool filterPackageBrowser(const PackageBrowserIndex& index, const QString& folder, const QString& text,
	QVector<qsizetype>* rows, QString* error, const PackageReadControl& control)
{
	if (error) { error->clear(); }
	PackagePlanWork work(control, error, QCoreApplication::translate("VibeStudioPackageBrowser", "Filtering package entries"));
	if (!rows || !work.checkpoint()) { return false; }
	if (index.entries.size() > PackageIndexLimits{}.maximumEntries || index.childCounts.size() != index.entries.size()
		|| index.occurrences.size() != index.entries.size()) {
		return work.refuse(QCoreApplication::translate("VibeStudioPackageBrowser", "Package browser index is invalid."));
	}
	const QString filter = text.trimmed().toCaseFolded();
	const bool searching = !filter.isEmpty();
	const auto query = parseStudioQuery(text);
	const bool querying = query.testsProperties();
	QVector<qsizetype> candidate;
	for (qsizetype at = 0; at < index.entries.size(); ++at) {
		if (!work.checkpoint()) { return false; }
		const auto& entry = index.entries.at(at);
		if (searching) {
			const auto haystack = QStringLiteral("%1 %2 %3").arg(entry.virtualPath, entry.typeHint, entry.storageMethod).toCaseFolded();
			if (querying ? studioQueryMatches(query, packageEntryQueryProperties(entry), haystack) : haystack.contains(filter)) { candidate.append(at); }
		} else if (packagePlanParent(entry.virtualPath) == folder) { candidate.append(at); }
	}
	if (!searching && !sortPackagePlan(&candidate, [&](qsizetype left, qsizetype right) {
		const auto& a = index.entries.at(left); const auto& b = index.entries.at(right);
		if (a.kind != b.kind) { return a.kind == PackageEntryKind::Directory; }
		return QString::compare(a.virtualPath, b.virtualPath, Qt::CaseInsensitive) < 0;
	}, work)) { return false; }
	if (!work.finish()) { return false; }
	*rows = std::move(candidate); return true;
}

} // namespace vibestudio
