#include "core/package_selection.h"
#include "core/package_staging.h"
#include "core/package_wad_groups.h"

#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <limits>

namespace vibestudio {
namespace {
struct Member {
	PackageStagedEntry entry;
	qsizetype readerIndex = -1;
};
QString message(const char* source) { return QCoreApplication::translate("VibeStudioPackageSubset", source); }
bool stopped(const PackageReadControl& control, QString* error)
{
	if (!control.isCancelled || !control.isCancelled()) { return false; }
	*error = message(QT_TRANSLATE_NOOP("VibeStudioPackageSubset", "Package subset preparation cancelled.")); return true;
}
} // namespace

bool PackageStagingModel::loadBaseArchiveSubsetAt(const PackageArchiveReader& archive, const QVector<qsizetype>& entryIndexes,
	QString* error, PackageSubsetReview* review, const PackageReadControl& control)
{
	QString localError; if (!error) { error = &localError; } error->clear(); if (review) { *review = {}; }
	if (stopped(control, error)) { return false; }
	if (const auto* wrapper = dynamic_cast<const PackageArchive*>(&archive); wrapper && wrapper->snapshotReader()) {
		for (const auto& warning : wrapper->warnings()) {
			if (warning.blocksSaving) { *error = warning.message; return false; }
		}
		// Known value snapshots can be rebased. Opaque owned providers must stay
		// attached to their wrapper so adoption retains their bytes and guards.
		if (dynamic_cast<const PackageArchive*>(wrapper->snapshotReader().get())
			|| dynamic_cast<const PackageStagingArchive*>(wrapper->snapshotReader().get())) {
			return loadBaseArchiveSubsetAt(*wrapper->snapshotReader(), entryIndexes, error, review, control);
		}
	}
	PackageSelectionRequest request; request.entryIndexes = entryIndexes;
	const auto selection = selectPackageFiles(archive, request, control);
	if (!selection.succeeded()) { *error = selection.errors.join(QLatin1Char('\n')); return false; }
	PackageStagingModel candidate(m_contentLimits, m_metadataLimits, m_planLimits, m_viewLimits); if (!candidate.loadBaseArchive(archive, error, control)) { return false; }
	const auto metadata = archive.entries(); QVector<Member> entries;
	if (const auto* staged = dynamic_cast<const PackageStagingArchive*>(&archive)) {
		for (qsizetype at = 0; at < staged->m_entries.size(); ++at) {
			if (staged->m_entries.at(at).kind == PackageEntryKind::File) { entries.append({staged->m_entries.at(at), at}); }
		}
	} else {
		for (const auto& entry : std::as_const(candidate.m_baseEntries)) { entries.append({entry, entry.sourceMetadataIndex}); }
		// Base admission retains unreadable occurrences and source order too.
		// Adding a second overlay would duplicate them in WAD group expansion.
	}
	QSet<qsizetype> requested, selectedReaders(selection.entryIndexes.cbegin(), selection.entryIndexes.cend());
	for (qsizetype at = 0; at < entries.size(); ++at) { if (selectedReaders.contains(entries.at(at).readerIndex)) { requested.insert(at); } }
	if (requested.size() != selectedReaders.size()) { *error = message(QT_TRANSLATE_NOOP("VibeStudioPackageSubset", "Selected occurrences are unavailable in the source snapshot.")); return false; }
	QSet<qsizetype> included = requested; QHash<qsizetype, QString> reasons; PackageSubsetReview prepared;
	if (candidate.m_sourceFormat == PackageArchiveFormat::Wad) {
		const QString magic = candidate.m_sourceWadMagic;
		if (magic == QStringLiteral("PWAD") || magic == QStringLiteral("IWAD")) {
			QStringList names; for (const auto& item : entries) { names << item.entry.virtualPath; }
			if (!expandPackageWadGroups(names, requested, &included, &reasons, &prepared.warnings, error, control)) { return false; }
		} else if (magic != QStringLiteral("WAD2") && magic != QStringLiteral("WAD3")) {
			*error = message(QT_TRANSLATE_NOOP("VibeStudioPackageSubset", "The source WAD layout could not be verified.")); return false;
		}
	}
	QVector<PackageStagedEntry> selected; QSet<QString> paths;
	for (qsizetype at = 0; at < entries.size(); ++at) {
		if (stopped(control, error)) { return false; } if (!included.contains(at)) { continue; }
		const auto& item = entries.at(at); const auto& entry = item.entry;
		if (item.readerIndex < 0 || item.readerIndex >= metadata.size() || !metadata.at(item.readerIndex).readable || !isSafePackageVirtualPath(entry.virtualPath)) {
			*error = message(QT_TRANSLATE_NOOP("VibeStudioPackageSubset", "A required subset member is unreadable or unsafe: %1")).arg(entry.virtualPath); return false;
		}
		if (entry.sizeBytes > std::numeric_limits<quint64>::max() - prepared.totalBytes) { *error = message(QT_TRANSLATE_NOOP("VibeStudioPackageSubset", "Subset sizes exceed the supported total.")); return false; }
		selected << entry; paths.insert(entry.virtualPath.toCaseFolded()); prepared.totalBytes += entry.sizeBytes;
		prepared.members.append({item.readerIndex, entry.sourceOrdinal, entry.virtualPath, entry.sizeBytes, requested.contains(at), reasons.value(at)});
	}
	candidate.m_baseEntries = std::move(selected); candidate.m_baseDirectories.clear();
	QSet<QString> representedPaths;
	for (const auto& entry : metadata) { representedPaths.insert(entry.virtualPath.toCaseFolded()); }
	candidate.m_baseConflicts.erase(std::remove_if(candidate.m_baseConflicts.begin(), candidate.m_baseConflicts.end(), [&](const auto& conflict) {
		// A skipped WAD directory record has no reliable group membership.
		// Preserve its blocking diagnostic even when its name also occurs in
		// an unrelated map. Repair the source before exporting a semantic subset.
		if (conflict.blocking && candidate.m_sourceFormat == PackageArchiveFormat::Wad) { return false; }
		const QString key = conflict.virtualPath.toCaseFolded();
		return !key.isEmpty() && !paths.contains(key) && (!conflict.blocking || representedPaths.contains(key));
	}), candidate.m_baseConflicts.end());
	candidate.invalidatePlan();
	if (!candidate.admitView(error, control)) { return false; }
	if (!candidate.summary().canSave) { *error = candidate.summary().blockedMessages.join(QLatin1Char('\n')); return false; }
	*this = std::move(candidate); if (review) { *review = std::move(prepared); } return true;
}

} // namespace vibestudio
