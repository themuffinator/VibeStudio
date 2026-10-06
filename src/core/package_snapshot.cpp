#include "core/package_snapshot_p.h"
#include "core/package_index_p.h"
#include "core/package_plan_p.h"

#include <QCoreApplication>
#include <QSet>

namespace vibestudio {

bool admitPackageSnapshot(const QString& source, const QVector<PackageEntry>& entries,
	const QVector<PackageLoadWarning>& warnings, const PackageIndexLimits& limits,
	const PackageReadControl& control, PackageIndexUsage* output,
	QVector<PackageEntry>* impliedDirectories, QString* error)
{
	if (output) { *output = {}; }
	if (impliedDirectories) { impliedDirectories->clear(); }
	if (error) { error->clear(); }
	const auto fail = [&](const QString& message) { if (error) { *error = message; } return false; };
	if (!validPackageIndexLimits(limits, error)) { return false; }
	const auto stopped = [&] {
		if (!control.isCancelled || !control.isCancelled()) { return false; }
		fail(QCoreApplication::translate("VibeStudioPackageArchive", "Package snapshot preparation cancelled.")); return true;
	};
	if (stopped()) { return false; }
	PackageIndexUsage usage;
	const auto exhausted = [&] {
		return fail(QCoreApplication::translate("VibeStudioPackageArchive", "Package snapshot exceeds the limits of %1 records or %2 metadata bytes. Undo recent edits or use a smaller package.")
			.arg(limits.maximumEntries).arg(limits.maximumMetadataBytes));
	};
	const auto records = [&](qsizetype count) {
		if (count < 0 || count > limits.maximumEntries - usage.entries) { return exhausted(); }
		usage.entries += count; return true;
	};
	const auto text = [&](const QString& value) {
		if (value.size() > (limits.maximumMetadataBytes - usage.metadataBytes) / qint64(sizeof(QChar))) { return exhausted(); }
		usage.metadataBytes += value.size() * qint64(sizeof(QChar)); return true;
	};
	if (!records(entries.size()) || !records(warnings.size()) || !text(source)) { return false; }
	const auto metadata = [&](const PackageEntry& entry) {
		for (const auto* value : {&entry.virtualPath, &entry.typeHint, &entry.storageMethod, &entry.sourceArchiveId, &entry.layerId, &entry.note}) {
			if (!text(*value)) { return false; }
		}
		return true;
	};
	QSet<QString> directories;
	qsizetype prepared = 0;
	const auto progress = [&] {
		if (control.progress && (prepared % 256 == 0 || prepared == entries.size())) {
			control.progress(QCoreApplication::translate("VibeStudioPackageArchive", "Preparing package snapshot"), prepared, entries.size());
		}
	};
	progress();
	for (const auto& entry : entries) {
		if (stopped() || !metadata(entry)) { return false; }
		const auto normalized = normalizePackageVirtualPath(entry.virtualPath, false);
		if ((entry.kind != PackageEntryKind::File && entry.kind != PackageEntryKind::Directory)
			|| !normalized.isSafe() || normalized.normalizedPath != entry.virtualPath) {
			return fail(QCoreApplication::translate("VibeStudioPackageArchive", "The package snapshot contains an invalid entry path or kind."));
		}
		if (entry.virtualPath.count(QLatin1Char('/')) + 1 > limits.maximumPathDepth) {
			return fail(QCoreApplication::translate("VibeStudioPackageArchive", "Package entry paths exceed the maximum indexing depth of %1.").arg(limits.maximumPathDepth));
		}
		if (entry.kind == PackageEntryKind::Directory) { directories.insert(entry.virtualPath); }
		++prepared; progress();
	}
	for (const auto& warning : warnings) {
		if (stopped() || !text(warning.virtualPath) || !text(warning.message)) { return false; }
	}
	QVector<PackageEntry> implied; qsizetype folderCount = 0;
	const auto folderProgress = [&] {
		if (control.progress) { control.progress(QCoreApplication::translate("VibeStudioPackageArchive", "Preparing package snapshot folders"), folderCount, 0); }
	};
	folderProgress();
	for (const auto& entry : entries) {
		if (stopped()) { return false; }
		// Entry paths were validated above; their prefixes are already canonical.
		for (QString parent = packagePlanParent(entry.virtualPath); !parent.isEmpty(); parent = packagePlanParent(parent)) {
			if (stopped()) { return false; }
			if (directories.contains(parent)) { continue; }
			PackageEntry directory;
			directory.virtualPath = parent; directory.kind = PackageEntryKind::Directory;
			directory.typeHint = QStringLiteral("directory"); directory.storageMethod = QStringLiteral("synthetic");
			directory.sourceArchiveId = source; directory.layerId = QStringLiteral("base");
			if (!records(1) || !metadata(directory)) { return false; }
			directories.insert(parent);
			if (++folderCount % 256 == 0) { folderProgress(); }
			if (impliedDirectories) { implied.append(std::move(directory)); }
		}
	}
	if (stopped()) { return false; }
	if (output) { *output = usage; }
	if (impliedDirectories) { *impliedDirectories = std::move(implied); }
	return true;
}

} // namespace vibestudio
