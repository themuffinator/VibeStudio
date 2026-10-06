#include "cli/package_copy_limits.h"
#include "core/package_copy_store.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>

namespace vibestudio::cli {
PackageCopyLimitsCliResult runPackageCopyStoreLimits(const QStringList& arguments)
{
	const auto failure = [](int code, const QString& error) { return PackageCopyLimitsCliResult{code, error, {}, {}}; };
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--write", "--dry-run"};
	const QSet<QString> globals{"--settings-file", "--locale", "--catalog-root", "--directory", "--expected-policy-sha256"};
	const QHash<QString, quint64> maxima{{"--max-mib", PackageCopyMaximumMiB}, {"--max-files", PackageCopyMaximumFiles},
		{"--max-entries", PackageCopyStorePayloadEntryLimit}, {"--max-batches", PackageCopyMaximumBatches}};
	QSet<QString> seen; QHash<QString, quint64> values; QHash<QString, QString> strings; QStringList positional;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString arg = arguments.at(i); if (!arg.startsWith('-')) { positional << arg; continue; }
		const auto equal = arg.indexOf('='); const QString key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key) || (!flags.contains(key) && !globals.contains(key) && !maxima.contains(key))) {
			return failure(2, QCoreApplication::translate("PackageCopyStoreLimitsCli", "Unexpected or repeated option: %1").arg(key));
		}
		seen.insert(key);
		if (flags.contains(key)) {
			if (equal >= 0) { return failure(2, QCoreApplication::translate("PackageCopyStoreLimitsCli", "Flag %1 does not take a value.").arg(key)); }
			continue;
		}
		QString value = equal < 0 ? QString() : arg.mid(equal + 1);
		if (equal < 0 && i + 1 < arguments.size() && !arguments.at(i + 1).startsWith('-')) { value = arguments.at(++i); }
		if (value.isEmpty()) { return failure(2, QCoreApplication::translate("PackageCopyStoreLimitsCli", "Missing value for %1.").arg(key)); }
		if (globals.contains(key)) { strings.insert(key, value); continue; }
		bool parsed = false; const auto number = value.toULongLong(&parsed);
		if (!parsed || !QRegularExpression(QStringLiteral("^[0-9]+$")).match(value).hasMatch() || !number || number > maxima.value(key)) {
			return failure(2, QCoreApplication::translate("PackageCopyStoreLimitsCli", "%1 must be an integer from 1 to %2.").arg(key).arg(maxima.value(key)));
		}
		values.insert(key, number);
	}
	if (positional != QStringList{QStringLiteral("package"), QStringLiteral("copy-store-limits")}
		|| (seen.contains("--write") && seen.contains("--dry-run"))
		|| (values.isEmpty() && (seen.contains("--write") || seen.contains("--expected-policy-sha256")))) {
		return failure(2, QCoreApplication::translate("PackageCopyStoreLimitsCli", "Use package copy-store-limits with optional limits; --write requires a limit and cannot accompany --dry-run."));
	}
	const auto hash = strings.value(QStringLiteral("--expected-policy-sha256")).toLower();
	if (seen.contains("--expected-policy-sha256") && (hash.size() != 64 || QByteArray::fromHex(hash.toLatin1()).toHex() != hash.toLatin1())) {
		return failure(2, QCoreApplication::translate("PackageCopyStoreLimitsCli", "Supply a 64-digit policy SHA-256 from package copy-store-limits."));
	}
	const QString directory = strings.value(QStringLiteral("--directory"), packageCopyDirectory());
	auto quota = inspectPackageCopyQuota(directory); const auto stored = quota.limits; auto limits = stored;
	if (values.contains("--max-mib")) { limits.maximumBytes = values.value("--max-mib") * 1024 * 1024; }
	if (values.contains("--max-files")) { limits.maximumFiles = values.value("--max-files"); }
	if (values.contains("--max-entries")) { limits.maximumEntries = values.value("--max-entries"); }
	if (values.contains("--max-batches")) { limits.maximumBatches = values.value("--max-batches"); }
	const bool write = seen.contains("--write"); PackageCopyLimitsCliResult result;
	if (!values.isEmpty()) {
		if (write && StudioSettings(StudioSettings::AccessMode::ReadOnly).storedSchemaIsNewer()) {
			return failure(1, QCoreApplication::translate("PackageCopyStoreLimitsCli", "The settings store is read-only."));
		}
		if (!configurePackageCopyQuota(directory, limits, hash.isEmpty() ? quota.policyFingerprint : QByteArray::fromHex(hash.toLatin1()), !write, &result.error)) {
			result.exitCode = 1;
		} else if (write) { quota = inspectPackageCopyQuota(directory); }
	} else if (!quota.complete()) { result.exitCode = 1; result.error = quota.error; }
	result.payload = {{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("scope"), QStringLiteral("managed-copy-store")},
		{QStringLiteral("limits"), packageCopyLimitsJson(limits)}, {QStringLiteral("storedLimits"), packageCopyLimitsJson(write && !result.exitCode ? limits : stored)},
		{QStringLiteral("dryRun"), !write}, {QStringLiteral("updated"), write && !result.exitCode}, {QStringLiteral("quota"), packageCopyQuotaJson(quota)}};
	if (quota.policyFingerprint.size() == 32) {
		result.summary = QCoreApplication::translate("PackageCopyStoreLimitsCli", "Shared copy store: %1\nLimits: %2 MiB; files: %3; entries: %4; batches: %5\nPolicy SHA-256: %6\n%7")
			.arg(quota.directory).arg(limits.maximumBytes / (1024 * 1024)).arg(limits.maximumFiles).arg(limits.maximumEntries).arg(limits.maximumBatches)
			.arg(QString::fromLatin1(quota.policyFingerprint.toHex()), write && !result.exitCode
				? QCoreApplication::translate("PackageCopyStoreLimitsCli", "Shared limits saved. Existing copies are preserved.")
				: QCoreApplication::translate("PackageCopyStoreLimitsCli", "No files were written."));
		result.summary += QLatin1Char('\n') + (quota.complete()
			? QCoreApplication::translate("PackageCopyStoreLimitsCli", "Shared reserved payload (bytes): %1; files: %2; entries: %3; batches: %4; pending or interrupted: %5")
				.arg(quota.reserved.bytes).arg(quota.reserved.files).arg(quota.reserved.entries).arg(quota.reserved.batches).arg(quota.reserved.pendingBatches)
			: quota.error);
	}
	return result;
}
} // namespace vibestudio::cli
