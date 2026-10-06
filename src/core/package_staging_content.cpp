#include "core/package_staging.h"

#include <QCoreApplication>
#include <QSet>

namespace vibestudio {

bool PackageStagingModel::contentUsage(PackageStagingContentUsage* output, QString* error, const PackageReadControl& control) const
{
	if (output) { *output = {}; }
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) { if (error) { *error = message; } return false; };
	if (m_contentLimits.maximumGeneratedBytes < 0 || m_contentLimits.maximumGeneratedBytes > PackageStagingContentLimits::generatedCeiling
		|| m_contentLimits.maximumFingerprintBytes < 0 || m_contentLimits.maximumFingerprintBytes > PackageStagingContentLimits::fingerprintCeiling) {
		return fail(QCoreApplication::translate("VibeStudioPackageStaging", "Package content limits are outside their supported range."));
	}
	const auto stopped = [&] {
		if (!control.isCancelled || !control.isCancelled()) { return false; }
		fail(QCoreApplication::translate("VibeStudioPackageStaging", "Package content admission cancelled.")); return true;
	};
	if (stopped()) { return false; }
	if (m_contentUsageValid && m_contentUsageExact) {
		if (output) { *output = m_contentUsage; }
		return true;
	}
	PackageStagingContentUsage usage;
	const auto generated = [&](qint64 bytes) {
		if (bytes < 0 || bytes > m_contentLimits.maximumGeneratedBytes - usage.generatedBytes) {
			return fail(QCoreApplication::translate("VibeStudioPackageStaging", "Retained generated content exceeds the %1-byte document limit. Save and reopen the draft to release inline content, or stage smaller assets.").arg(m_contentLimits.maximumGeneratedBytes));
		}
		usage.generatedBytes += bytes; return true;
	};
	const auto fingerprints = [&](qint64 bytes) {
		if (bytes < 0 || bytes > m_contentLimits.maximumFingerprintBytes - usage.fingerprintBytes) {
			return fail(QCoreApplication::translate("VibeStudioPackageStaging", "Retained source fingerprints exceed the %1-byte document limit. Use smaller inputs or save a smaller package and reopen it.").arg(m_contentLimits.maximumFingerprintBytes));
		}
		usage.fingerprintBytes += bytes; return true;
	};
	// The immutable on-disk base keeps its independently admitted index alive,
	// even when every visible entry was replaced or deleted by staged edits.
	if (m_baseReader && !fingerprints(m_baseReader->indexUsage().fingerprintBytes)) { return false; }
	QSet<const char*> generatedStorage, fingerprintStorage;
	const auto content = [&](bool hasBytes, const QByteArray& bytes, const PackageFileIdentityPtr& identity) {
		if (stopped()) { return false; }
		if (hasBytes && !bytes.isEmpty() && !generatedStorage.contains(bytes.constData())) {
			if (!generated(bytes.size())) { return false; }
			generatedStorage.insert(bytes.constData());
		}
		if (identity && !identity->chunkHashes.isEmpty() && !fingerprintStorage.contains(identity->chunkHashes.constData())) {
			if (!fingerprints(identity->chunkHashes.size())) { return false; }
			fingerprintStorage.insert(identity->chunkHashes.constData());
		}
		return true;
	};
	for (const auto& entry : m_baseEntries) {
		if (!content(entry.hasInlineBytes, entry.inlineBytes, entry.sourceIdentity)) { return false; }
	}
	for (const auto& entry : m_baseDirectories) {
		if (!content(entry.hasInlineBytes, entry.inlineBytes, entry.sourceIdentity)) { return false; }
	}
	QSet<QString> operations;
	const auto operation = [&](const PackageStageOperation& value) {
		if (stopped()) { return false; }
		if (operations.contains(value.id)) { return true; }
		operations.insert(value.id);
		return content(value.hasInlineBytes, value.inlineBytes, value.sourceIdentity);
	};
	for (const auto& value : m_operations) { if (!operation(value)) { return false; } }
	for (const auto& step : m_history) {
		for (const auto& change : step.changes) { if (!operation(change.operation)) { return false; } }
	}
	for (const auto& change : m_group.changes) { if (!operation(change.operation)) { return false; } }
	if (stopped()) { return false; }
	m_contentUsage = usage; m_contentUsageValid = true; m_contentUsageExact = true;
	if (output) { *output = usage; }
	return true;
}

} // namespace vibestudio
