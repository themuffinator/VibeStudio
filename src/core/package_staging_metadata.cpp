#include "core/package_staging.h"

#include <QCoreApplication>
#include <QSet>

namespace vibestudio {
namespace {
void textCost(PackageStagingMetadataUsage* usage, const QString& value)
{
	constexpr auto ceiling = PackageStagingMetadataLimits::metadataCeiling;
	if (value.size() > (ceiling - qMin(usage->metadataBytes, ceiling)) / 2) { usage->metadataBytes = ceiling + 1; }
	else { usage->metadataBytes += value.size() * 2; }
}
void identityCost(PackageStagingMetadataUsage* usage, const PackageFileIdentityPtr& identity)
{
	if (!identity) { return; }
	++usage->records;
	textCost(usage, identity->path); textCost(usage, identity->resolvedPath); textCost(usage, identity->storageDirectory);
}
}

PackageStagingMetadataUsage PackageStagingModel::entryMetadata(const PackageStagedEntry& entry)
{
	PackageStagingMetadataUsage cost{1, 0};
	for (const auto* text : {&entry.virtualPath, &entry.source, &entry.operationId, &entry.sourceFilePath,
		&entry.baseVirtualPath, &entry.wadInsertBefore, &entry.wadNamespace, &entry.unavailableReason}) { textCost(&cost, *text); }
	identityCost(&cost, entry.sourceIdentity);
	return cost;
}

PackageStagingMetadataUsage PackageStagingModel::operationMetadata(const PackageStageOperation& operation)
{
	PackageStagingMetadataUsage usage{1, 0};
	for (const auto* value : {&operation.id, &operation.virtualPath, &operation.targetVirtualPath, &operation.sourceFilePath,
		&operation.sourceError, &operation.sourceTreeIdentity, &operation.wadInsertBefore, &operation.wadNamespace}) { textCost(&usage, *value); }
	identityCost(&usage, operation.sourceIdentity);
	return usage;
}

PackageStagingMetadataUsage PackageStagingModel::historyMetadata(const QString& label)
{
	PackageStagingMetadataUsage usage{1, 0}; textCost(&usage, label); return usage;
}

bool PackageStagingModel::metadataFits(const PackageStagingMetadataUsage& usage, const PackageStagingMetadataUsage& addition, QString* error) const
{
	if (addition.records < 0 || usage.records < 0 || addition.records > m_metadataLimits.maximumRecords - usage.records) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "Retained package metadata exceeds the %1-record document limit. Save a smaller package and reopen it to release edit history.").arg(m_metadataLimits.maximumRecords); }
		return false;
	}
	if (addition.metadataBytes < 0 || usage.metadataBytes < 0 || addition.metadataBytes > m_metadataLimits.maximumMetadataBytes - usage.metadataBytes) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "Retained package metadata exceeds the %1-byte document limit. Use shorter paths or save a smaller package and reopen it.").arg(m_metadataLimits.maximumMetadataBytes); }
		return false;
	}
	return true;
}

bool PackageStagingModel::metadataUsage(PackageStagingMetadataUsage* output, QString* error, const PackageReadControl& control) const
{
	if (output) { *output = {}; }
	if (error) { error->clear(); }
	if (m_metadataLimits.maximumRecords < 0 || m_metadataLimits.maximumRecords > PackageStagingMetadataLimits::recordCeiling
		|| m_metadataLimits.maximumMetadataBytes < 0 || m_metadataLimits.maximumMetadataBytes > PackageStagingMetadataLimits::metadataCeiling) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "Package metadata limits are outside their supported range."); }
		return false;
	}
	const auto stopped = [&] {
		if (!control.isCancelled || !control.isCancelled()) { return false; }
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "Package metadata admission cancelled."); }
		return true;
	};
	if (stopped()) { return false; }
	if (m_metadataUsageValid && m_metadataUsageExact) { if (output) { *output = m_metadataUsage; } return true; }
	PackageStagingMetadataUsage usage;
	const auto charge = [&](const PackageStagingMetadataUsage& addition) {
		if (stopped() || !metadataFits(usage, addition, error)) { return false; }
		usage.records += addition.records; usage.metadataBytes += addition.metadataBytes; return true;
	};
	PackageStagingMetadataUsage document;
	textCost(&document, m_sourcePath); textCost(&document, m_sourceWadMagic); textCost(&document, m_draftPath);
	identityCost(&document, m_draftIdentity);
	if (!charge(document)) { return false; }
	if (m_baseReader) {
		const auto index = m_baseReader->indexUsage();
		if (!charge({index.entries, index.metadataBytes})) { return false; }
	}
	for (const auto& path : m_protectedInputPaths) { if (!charge(historyMetadata(path))) { return false; } }
	for (const auto& lump : m_sourceWadLumps) { if (!charge(historyMetadata(lump.name))) { return false; } }
	for (const auto& conflict : m_baseConflicts) {
		PackageStagingMetadataUsage value{1, 0};
		textCost(&value, conflict.operationId); textCost(&value, conflict.virtualPath); textCost(&value, conflict.message);
		if (!charge(value)) { return false; }
	}
	const auto entry = [&](const PackageStagedEntry& value) {
		const auto cost = entryMetadata(value);
		return charge(cost);
	};
	for (const auto& value : m_baseEntries) { if (!entry(value)) { return false; } }
	for (const auto& value : m_baseDirectories) { if (!entry(value)) { return false; } }
	// Reserve one active slot for every unique retained operation, even if it
	// currently exists only in undo/redo. History deltas occupy separate slots.
	// Undo/redo can therefore change visibility without requiring new admission.
	QSet<QString> operations;
	const auto operation = [&](const PackageStageOperation& value) {
		if (stopped()) { return false; }
		if (operations.contains(value.id)) { return true; }
		if (!charge(operationMetadata(value))) { return false; }
		operations.insert(value.id); return true;
	};
	for (const auto& value : m_operations) { if (!operation(value)) { return false; } }
	const auto history = [&](const PackageStageHistoryStep& step) {
		if (!charge(historyMetadata(step.label))) { return false; }
		for (const auto& change : step.changes) { if (!charge({1, 0}) || !operation(change.operation)) { return false; } }
		return true;
	};
	for (const auto& step : m_history) { if (!history(step)) { return false; } }
	if (m_groupDepth > 0 && !history(m_group)) { return false; }
	if (stopped()) { return false; }
	m_metadataUsage = usage; m_metadataUsageValid = true; m_metadataUsageExact = true;
	if (output) { *output = usage; }
	return true;
}

} // namespace vibestudio
