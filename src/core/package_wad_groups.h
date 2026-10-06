#pragma once

#include "core/package_archive.h"
#include <QHash>
#include <QJsonObject>
#include <QSet>

namespace vibestudio {
class PackageStagingModel;

// Positions always address plannedEntries() in WAD directory order, never a
// reader's path-sorted listing. Source IDs retain their ordinals; IDs for newly
// staged markers can change after an edit. Always use a fresh inventory/fingerprint.
struct PackageWadGroupRange { qsizetype first = -1, last = -1; };
struct PackageWadGroup {
	QString id, name, kind, error;
	QVector<PackageWadGroupRange> ranges;
	qsizetype memberCount = 0;
	bool canRename = false;
};
struct PackageWadGroupInventory {
	QString fingerprint, error;
	QVector<PackageWadGroup> groups;
	[[nodiscard]] bool succeeded() const { return error.isEmpty(); }
};
struct PackageWadGroupEditRequest {
	QString groupId, expectedFingerprint;
	// Empty for deletion; map rename accepts the new main map label, including
	// when the selected group is its GL companion.
	QString targetName;
	bool remove = false;
};
struct PackageWadGroupChange {
	qsizetype entryIndex = -1;
	int sourceOrdinal = -1;
	QString before, after;
};
struct PackageWadGroupEditReview {
	QString groupId, groupName;
	bool remove = false;
	QVector<PackageWadGroupChange> changes;
	QStringList warnings;
};

PackageWadGroupInventory inspectPackageWadGroups(const PackageStagingModel& model, const PackageReadControl& control = {});
QJsonObject packageWadGroupsJson(const PackageWadGroupInventory& inventory);
QJsonObject packageWadGroupEditJson(const PackageWadGroupEditReview& review);
// Atomic, cancellable and one undo step. A mandatory fingerprint binds the
// edit to its inspected plan. Existing draft/history operations are reused.
bool stagePackageWadGroupEdit(PackageStagingModel* model, const PackageWadGroupEditRequest& request,
	PackageWadGroupEditReview* review = nullptr, QString* error = nullptr, const PackageReadControl& control = {});

// Shared semantic scan for subset export. Adds required map/GL peers and
// namespace boundaries; deletion deliberately does not remove enclosing
// namespace markers when only a nested group is selected.
bool expandPackageWadGroups(const QStringList& names, const QSet<qsizetype>& requested, QSet<qsizetype>* included,
	QHash<qsizetype, QString>* reasons, QStringList* warnings, QString* error, const PackageReadControl& control = {});

// Construct the positional plan of a new Doom WAD. Canonicalizes binary/GL
// runs and assigns loose reserved lumps only when one map can own them. UDMF
// sidecars and namespace regions retain their order. Existing WADs bypass this.
bool orderNewPackageDoomLumps(const QStringList& names, QVector<qsizetype>* order,
	QString* error, const PackageReadControl& control = {});
} // namespace vibestudio
