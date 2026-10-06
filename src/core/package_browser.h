#pragma once

#include "core/package_archive.h"

#include <QHash>
#include <memory>

namespace vibestudio {

struct PackageBrowserFolder {
	QString path;
	qsizetype parent = -1;
	qsizetype row = 0;
	qsizetype entry = -1; // Exact explicit directory entry, or -1 for an implied folder.
	QVector<qsizetype> children;
};

struct PackageBrowserComposition {
	QString id;
	int count = 0;
	quint64 bytes = 0;
	bool sizeOverflow = false;
};

// Immutable positional metadata. Rows always refer to this exact admitted
// snapshot, so repeated WAD names and unreadable entries retain their identity.
struct PackageBrowserIndex {
	QVector<PackageEntry> entries;
	QVector<qsizetype> childCounts;
	QVector<qsizetype> occurrences;
	// Folder zero is the package root. Paths are exact/case-sensitive, matching
	// the archive namespace. Relationships and sorted sibling rows are prepared
	// once on the worker; views never reconstruct a tree from entry paths.
	QVector<PackageBrowserFolder> folders;
	QHash<QString, qsizetype> folderLookup;
	QVector<PackageBrowserComposition> composition;
	PackageArchiveSummary summary;
};

std::shared_ptr<const PackageBrowserIndex> preparePackageBrowserIndex(const PackageArchive& archive,
	QString* error = nullptr, const PackageReadControl& control = {});

// An empty query lists direct children, directories first with stable ordering.
// A query searches the whole snapshot using the same property-query service as
// the CLI and other studio surfaces. Failure publishes no partial row vector.
bool filterPackageBrowser(const PackageBrowserIndex& index, const QString& folder, const QString& query,
	QVector<qsizetype>* rows, QString* error = nullptr, const PackageReadControl& control = {});

} // namespace vibestudio
