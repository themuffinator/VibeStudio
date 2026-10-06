#include "core/package_validation.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>

namespace vibestudio {

bool PackageValidationReport::valid() const
{
	return completed && !cancelled && sourceVerified && failedCount == 0 && uncheckedCount == 0
		&& verifiedCount == fileCount && warnings.isEmpty();
}

PackageValidationReport validatePackage(const PackageArchive& archive, const PackageValidationRequest& request)
{
	PackageValidationReport report;
	report.sourcePath = archive.sourcePath();
	report.warnings = archive.warnings();
	if (!archive.isOpen()) {
		report.warnings.append({{}, archive.errorString().isEmpty() ? QCoreApplication::translate("VibeStudioPackageValidation", "Open a package before validating it.") : archive.errorString()});
		return report;
	}
	const auto entries = archive.entries();
	for (const auto& entry : entries) {
		if (entry.kind == PackageEntryKind::File) { ++report.fileCount; }
	}
	const auto cancelled = [&]() { return request.isCancelled && request.isCancelled(); };
	for (qsizetype index = 0; index < entries.size(); ++index) {
		const auto& entry = entries.at(index);
		if (entry.kind != PackageEntryKind::File) { continue; }
		if (cancelled()) { report.cancelled = true; break; }
		PackageValidationEntry result;
		result.virtualPath = entry.virtualPath;
		result.sourceOrdinal = entry.sourceOrdinal;
		const auto progress = [&]() {
			if (request.progress) { request.progress(static_cast<int>(report.entries.size()), report.fileCount, report.bytesRead + result.bytesRead, entry.virtualPath); }
		};
		progress();
		const bool exceedsBudget = (request.maxEntryBytes && entry.sizeBytes > request.maxEntryBytes)
			|| (request.maxTotalBytes && (report.bytesRead >= request.maxTotalBytes
				? entry.sizeBytes > 0 : entry.sizeBytes > request.maxTotalBytes - report.bytesRead));
		if (exceedsBudget) {
			result.status = QStringLiteral("unchecked");
			result.message = QCoreApplication::translate("VibeStudioPackageValidation", "Entry exceeds the requested validation byte budget.");
			++report.uncheckedCount;
		} else {
			QCryptographicHash hash(QCryptographicHash::Sha256);
			const bool read = archive.streamEntryAt(index, [&](QByteArrayView chunk) {
				hash.addData(chunk);
				result.bytesRead += static_cast<quint64>(chunk.size());
				progress();
				return !cancelled();
			}, &result.message, request.isCancelled);
			if (cancelled()) {
				result.status = QStringLiteral("cancelled");
				report.cancelled = true;
			} else if (read) {
				result.status = QStringLiteral("verified");
				result.sha256 = QString::fromLatin1(hash.result().toHex());
				++report.verifiedCount;
			} else {
				result.status = QStringLiteral("failed");
				++report.failedCount;
			}
		}
		report.bytesRead += result.bytesRead;
		report.entries.append(result);
		if (report.cancelled) { break; }
	}
	if (!report.cancelled && cancelled()) { report.cancelled = true; }
	// Entry reads cover payloads. Verify the full captured source as well so
	// changed headers, directory records, empty containers and folder membership
	// cannot pass merely because no affected payload was visited.
	if (!report.cancelled && report.failedCount == 0 && report.uncheckedCount == 0) {
		PackageReadControl control;
		control.isCancelled = request.isCancelled;
		qint64 previousBytes = 0;
		control.progress = [&](const QString& path, qint64 completed, qint64) {
			if (completed == 0) { previousBytes = 0; }
			report.sourceBytesRead += static_cast<quint64>(completed - previousBytes);
			previousBytes = completed;
			if (request.progress) { request.progress(static_cast<int>(report.entries.size()), report.fileCount, report.bytesRead + report.sourceBytesRead, path); }
		};
		QString error;
		report.sourceVerified = archive.verifySourceIdentity(&error, control);
		if (cancelled()) { report.cancelled = true; }
		else if (!report.sourceVerified) { report.warnings.append({{}, error}); }
	}
	report.completed = !report.cancelled && report.entries.size() == report.fileCount;
	if (request.progress) { request.progress(static_cast<int>(report.entries.size()), report.fileCount, report.bytesRead + report.sourceBytesRead, {}); }
	return report;
}

QJsonObject packageValidationJson(const PackageValidationReport& report)
{
	QJsonArray entries;
	for (const auto& entry : report.entries) {
		entries.append(QJsonObject {
			{QStringLiteral("path"), entry.virtualPath}, {QStringLiteral("sourceOrdinal"), entry.sourceOrdinal},
			{QStringLiteral("status"), entry.status}, {QStringLiteral("bytesRead"), static_cast<qint64>(entry.bytesRead)},
			{QStringLiteral("sha256"), entry.sha256}, {QStringLiteral("message"), entry.message},
		});
	}
	QJsonArray warnings;
	for (const auto& warning : report.warnings) {
		warnings.append(QJsonObject {{QStringLiteral("path"), warning.virtualPath}, {QStringLiteral("message"), warning.message}});
	}
	return {
		{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("sourcePath"), report.sourcePath},
		{QStringLiteral("valid"), report.valid()}, {QStringLiteral("usable"), report.valid()},
		{QStringLiteral("completed"), report.completed}, {QStringLiteral("cancelled"), report.cancelled},
		{QStringLiteral("fileCount"), report.fileCount}, {QStringLiteral("verifiedCount"), report.verifiedCount},
		{QStringLiteral("failedCount"), report.failedCount}, {QStringLiteral("uncheckedCount"), report.uncheckedCount},
		{QStringLiteral("bytesRead"), static_cast<qint64>(report.bytesRead)},
		{QStringLiteral("sourceBytesRead"), static_cast<qint64>(report.sourceBytesRead)},
		{QStringLiteral("sourceVerified"), report.sourceVerified},
		{QStringLiteral("warningCount"), report.warnings.size()}, {QStringLiteral("warnings"), warnings},
		{QStringLiteral("entries"), entries},
	};
}

QString packageValidationText(const PackageValidationReport& report)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioPackageValidation", "Package: %1").arg(report.sourcePath);
	lines << (report.cancelled ? QCoreApplication::translate("VibeStudioPackageValidation", "Validation cancelled.")
		: report.valid() ? QCoreApplication::translate("VibeStudioPackageValidation", "Validation passed.")
		: QCoreApplication::translate("VibeStudioPackageValidation", "Validation did not pass."));
	lines << QCoreApplication::translate("VibeStudioPackageValidation", "Verified: %1 / %2; failed: %3; unchecked: %4; bytes read: %5")
		.arg(report.verifiedCount).arg(report.fileCount).arg(report.failedCount).arg(report.uncheckedCount).arg(report.bytesRead);
	lines << QCoreApplication::translate("VibeStudioPackageValidation", "Source identity verified: %1; source bytes read: %2")
		.arg(report.sourceVerified ? QCoreApplication::translate("VibeStudioPackageValidation", "yes") : QCoreApplication::translate("VibeStudioPackageValidation", "no"))
		.arg(report.sourceBytesRead);
	for (const auto& entry : report.entries) {
		if (!entry.message.isEmpty()) { lines << QStringLiteral("%1 [%2]: %3").arg(entry.virtualPath).arg(entry.sourceOrdinal).arg(entry.message); }
	}
	for (const auto& warning : report.warnings) { lines << QStringLiteral("%1: %2").arg(warning.virtualPath, warning.message); }
	return lines.join(QLatin1Char('\n'));
}

} // namespace vibestudio
