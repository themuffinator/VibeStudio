#pragma once

#include "core/package_archive.h"
#include <QJsonObject>

namespace vibestudio {

// PakFu-inspired exact entry/prefix selection, kept in the non-UI core.
// Reference: https://github.com/themuffinator/PakFu/blob/13111e4c07513548a29fd7eb74fc9d004c44aa59/src/cli/cli.cpp
// GPL-3.0, reviewed 2026-10-03; conceptual adaptation, no copied code.
// Entry and prefix selectors form a union; the optional query filters that
// union. A query alone searches all files. Empty and misspelled selectors fail
// closed so a release script cannot accidentally export the whole archive.
struct PackageSelectionRequest {
	QStringList entries;
	QStringList prefixes;
	QString query;
	// Exact positions from this immutable reader's entries(), never path keys.
	QVector<qsizetype> entryIndexes;
};

struct PackageSelectionResult {
	QStringList paths;
	quint64 totalBytes = 0;
	QStringList errors;
	QVector<qsizetype> entryIndexes;
	[[nodiscard]] bool succeeded() const { return errors.isEmpty() && !paths.isEmpty(); }
};

struct PackageSubsetMember {
	qsizetype entryIndex = -1;
	qint64 sourceOrdinal = -1;
	QString virtualPath;
	quint64 sizeBytes = 0;
	bool selected = false;
	QString reason;
};

struct PackageSubsetReview {
	QVector<PackageSubsetMember> members;
	QStringList warnings;
	quint64 totalBytes = 0;
};

PackageSelectionResult selectPackageFiles(const PackageArchiveReader& archive, const PackageSelectionRequest& request,
	const PackageReadControl& control = {});
QJsonObject packageSubsetReviewJson(const PackageSubsetReview& review);

} // namespace vibestudio
