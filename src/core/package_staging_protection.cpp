#include "core/package_staging.h"
#include "core/package_plan_p.h"
#include "core/package_protection_p.h"

namespace vibestudio {
bool PackageStagingModel::visitProtectedInputPaths(const std::function<bool(const QString&)>& visitor,
	QString* error, const PackageReadControl& control) const
{
	PackagePlanWork work(control, error, QCoreApplication::translate("VibeStudioPackageStaging", "Retaining package source protections"));
	const auto path = [&](const QString& value) { return work.checkpoint() && (value.isEmpty() || (visitor && visitor(value))); };
	const auto identity = [&](const PackageFileIdentityPtr& value) {
		return !value || (path(value->path) && path(value->resolvedPath)
			&& (!(value->storage || value->draftAccess) || path(value->storageDirectory)));
	};
	for (const auto& value : m_protectedInputPaths) { if (!path(value)) { return false; } }
	if (!path(m_sourcePath) || !path(m_draftPath) || !identity(m_draftIdentity)) { return false; }
	if (m_baseReader && !m_baseReader->visitProtectedInputPaths(visitor, error, work.nestedControl())) { return false; }
	const auto operation = [&](const PackageStageOperation& value) { return path(value.sourceFilePath) && identity(value.sourceIdentity); };
	for (const auto& value : m_operations) { if (!operation(value)) { return false; } }
	for (const auto& value : m_baseEntries) { if (!path(value.sourceFilePath) || !identity(value.sourceIdentity)) { return false; } }
	for (const auto& step : m_history) {
		if (!work.checkpoint()) { return false; }
		for (const auto& change : step.changes) { if (!operation(change.operation)) { return false; } }
	}
	for (const auto& change : m_group.changes) { if (!operation(change.operation)) { return false; } }
	return work.finish();
}
bool PackageStagingModel::freezeInputProtections(QString* error, const PackageReadControl& control,
	const QString& implicitDirectory, const PackageStagingModel* source)
{
	PackageStagingMetadataUsage retained;
	if (!metadataUsage(&retained, error, control)) { return false; }
	// The replacement set includes the existing roots. Reserve only the rest
	// of the destination's metadata while preparing the complete candidate set.
	for (const auto& path : m_protectedInputPaths) {
		--retained.records; retained.metadataBytes -= path.size() * qint64(sizeof(QChar));
	}
	PackageInputProtectionSet inputs(m_metadataLimits.maximumRecords - retained.records,
		m_metadataLimits.maximumMetadataBytes - retained.metadataBytes, error, control, implicitDirectory);
	const auto& from = source ? *source : *this;
	if (!from.visitProtectedInputPaths([&](const QString& path) { return inputs.add(path); }, error, control) || !inputs.finish()) {
		if (error && error->isEmpty()) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "Unable to retain all package source protections."); }
		return false;
	}
	m_protectedInputPaths = inputs.paths(); invalidatePlan(); return true;
}
bool PackageStagingModel::protectsInputPath(const QString& path) const
{
	QString error; PackageInputProtectionSet inputs(PackageInputProtectionSet::pathCeiling, PackageInputProtectionSet::byteCeiling, &error);
	return !visitProtectedInputPaths([&](const QString& input) { return inputs.add(input); }, &error, {})
		|| !inputs.finish() || inputs.protects(path);
}
bool PackageStagingArchive::visitProtectedInputPaths(const std::function<bool(const QString&)>& visitor,
	QString* error, const PackageReadControl& control) const
{
	return m_snapshot.visitProtectedInputPaths(visitor, error, control);
}
} // namespace vibestudio
