#include "cli/package_copy_limits.h"
#include "core/package_copy_budget.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>

namespace vibestudio::cli {
PackageCopyLimitsCliResult runPackageCopyLimits(const QStringList& arguments)
{
	const auto failure = [](int code, const QString& error) { return PackageCopyLimitsCliResult{code, error, {}, {}}; };
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--write", "--dry-run"};
	const QSet<QString> globals{"--settings-file", "--locale", "--catalog-root"};
	const QHash<QString, quint64> maxima{{"--max-mib", PackageCopyMaximumMiB}, {"--max-files", PackageCopyMaximumFiles},
		{"--max-entries", PackageCopyMaximumEntries}, {"--max-batches", PackageCopyMaximumBatches}};
	QSet<QString> seen; QHash<QString, quint64> values; QStringList positional;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString arg = arguments.at(i);
		if (!arg.startsWith('-')) { positional << arg; continue; }
		const auto equal = arg.indexOf('='); const QString key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key) || (!flags.contains(key) && !globals.contains(key) && !maxima.contains(key))) {
			return failure(2, QCoreApplication::translate("PackageCopyLimitsCli", "Unexpected or repeated option: %1").arg(key));
		}
		seen.insert(key);
		if (flags.contains(key)) {
			if (equal >= 0) { return failure(2, QCoreApplication::translate("PackageCopyLimitsCli", "Flag %1 does not take a value.").arg(key)); }
			continue;
		}
		QString value = equal < 0 ? QString() : arg.mid(equal + 1);
		if (equal < 0 && i + 1 < arguments.size() && !arguments.at(i + 1).startsWith('-')) { value = arguments.at(++i); }
		if (value.isEmpty()) { return failure(2, QCoreApplication::translate("PackageCopyLimitsCli", "Missing value for %1.").arg(key)); }
		if (globals.contains(key)) { continue; }
		bool parsed = false; const auto number = value.toULongLong(&parsed);
		if (!parsed || !QRegularExpression(QStringLiteral("^[0-9]+$")).match(value).hasMatch() || number < 1 || number > maxima.value(key)) {
			return failure(2, QCoreApplication::translate("PackageCopyLimitsCli", "%1 must be an integer from 1 to %2.").arg(key).arg(maxima.value(key)));
		}
		values.insert(key, number);
	}
	if (positional != QStringList{QStringLiteral("package"), QStringLiteral("copy-limits")}
		|| (seen.contains("--write") && seen.contains("--dry-run")) || (seen.contains("--write") && values.isEmpty())) {
		return failure(2, QCoreApplication::translate("PackageCopyLimitsCli", "Use package copy-limits with optional limits; --write requires a limit and cannot accompany --dry-run."));
	}
	const auto stored = StudioSettings(StudioSettings::AccessMode::ReadOnly).packageCopyLimits(); auto limits = stored;
	if (values.contains("--max-mib")) { limits.maximumBytes = values.value("--max-mib") * 1024 * 1024; }
	if (values.contains("--max-files")) { limits.maximumFiles = values.value("--max-files"); }
	if (values.contains("--max-entries")) { limits.maximumEntries = values.value("--max-entries"); }
	if (values.contains("--max-batches")) { limits.maximumBatches = values.value("--max-batches"); }
	const bool write = seen.contains("--write");
	if (write) {
		StudioSettings settings;
		if (settings.isReadOnly()) { return failure(1, QCoreApplication::translate("PackageCopyLimitsCli", "The settings store is read-only.")); }
		settings.setPackageCopyLimits(limits); settings.sync();
		if (settings.status() != QSettings::NoError) { return failure(1, QCoreApplication::translate("PackageCopyLimitsCli", "The temporary copy limits could not be saved.")); }
	}
	PackageCopyLimitsCliResult result;
	result.payload = {{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("scope"), QStringLiteral("studio-window")},
		{QStringLiteral("limits"), packageCopyLimitsJson(limits)}, {QStringLiteral("storedLimits"), packageCopyLimitsJson(write ? limits : stored)},
		{QStringLiteral("dryRun"), !write}, {QStringLiteral("updated"), write}, {QStringLiteral("usageAvailable"), false}};
	result.summary = QCoreApplication::translate("PackageCopyLimitsCli", "Temporary copy limits per studio window: payload %1 MiB; files %2; entries %3; batches %4.\n%5\nLive usage is available in File > Temporary Package Copies.")
		.arg(limits.maximumBytes / (1024 * 1024)).arg(limits.maximumFiles).arg(limits.maximumEntries).arg(limits.maximumBatches)
		.arg(write ? QCoreApplication::translate("PackageCopyLimitsCli", "Limits saved. Existing copies are preserved.")
			: QCoreApplication::translate("PackageCopyLimitsCli", "No settings were written."));
	return result;
}
} // namespace vibestudio::cli
