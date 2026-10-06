#include "cli/package_copy_sessions.h"
#include "core/package_copy_store.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QSet>
#include <QUuid>

namespace vibestudio::cli {
PackageCopySessionsCliResult runPackageCopySessions(const QStringList& arguments)
{
	const auto failure = [](int code, const QString& error) { return PackageCopySessionsCliResult{code, error, {}, {}}; };
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--write", "--dry-run"};
	const QSet<QString> options{"--settings-file", "--locale", "--catalog-root", "--directory", "--expected-storage-sha256"};
	QSet<QString> seen; QHash<QString, QString> values; QStringList positional;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString arg = arguments.at(i);
		if (!arg.startsWith('-')) { positional << arg; continue; }
		const auto equal = arg.indexOf('='); const QString key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key) || (!flags.contains(key) && !options.contains(key))) {
			return failure(2, QCoreApplication::translate("PackageCopySessionsCli", "Unexpected or repeated option: %1").arg(key));
		}
		seen.insert(key);
		if (flags.contains(key)) {
			if (equal >= 0) { return failure(2, QCoreApplication::translate("PackageCopySessionsCli", "Flag %1 does not take a value.").arg(key)); }
			continue;
		}
		QString value = equal < 0 ? QString() : arg.mid(equal + 1);
		if (equal < 0 && i + 1 < arguments.size() && !arguments.at(i + 1).startsWith('-')) { value = arguments.at(++i); }
		if (value.isEmpty()) { return failure(2, QCoreApplication::translate("PackageCopySessionsCli", "Missing value for %1.").arg(key)); }
		values.insert(key, value);
	}
	const bool listing = positional.value(1) == QStringLiteral("copy-sessions");
	if (positional.value(0) != QStringLiteral("package") || positional.size() != (listing ? 2 : 3)
		|| (!listing && positional.value(1) != QStringLiteral("copy-discard"))
		|| (seen.contains("--write") && seen.contains("--dry-run"))
		|| (listing && (seen.contains("--write") || seen.contains("--dry-run") || seen.contains("--expected-storage-sha256")))) {
		return failure(2, QCoreApplication::translate("PackageCopySessionsCli", "Use package copy-sessions to inspect storage, or package copy-discard <id> --expected-storage-sha256 <checksum> [--dry-run | --write]."));
	}
	const QString directory = values.value(QStringLiteral("--directory"), packageCopyDirectory());
	PackageCopySessionsCliResult result;
	if (listing) {
		const auto inventory = listPackageCopies(directory);
		result.payload = packageCopyInventoryJson(inventory);
		result.exitCode = inventory.complete() ? 0 : 1;
		result.error = inventory.error;
		QStringList lines{QCoreApplication::translate("PackageCopySessionsCli", "Temporary copy store: %1").arg(QDir::toNativeSeparators(directory)),
			inventory.complete()
				? QCoreApplication::translate("PackageCopySessionsCli", "Sessions: %1; payload (bytes): %2; files: %3.").arg(inventory.sessions.size()).arg(inventory.bytes).arg(inventory.files)
				: QCoreApplication::translate("PackageCopySessionsCli", "Storage review is incomplete. Listed totals may be partial.")};
		for (const auto& info : inventory.sessions) {
			lines << QCoreApplication::translate("PackageCopySessionsCli", "Session: %1\nPath: %2\nPayload (bytes): %3; files: %4; batches: %5\nStorage review SHA-256: %6\nDiscard available: %7")
				.arg(info.id, QDir::toNativeSeparators(info.path)).arg(info.bytes).arg(info.files).arg(info.batches)
				.arg(QString::fromLatin1(info.fingerprint.toHex()), info.discardAvailable
					? QCoreApplication::translate("PackageCopySessionsCli", "yes; checked again on discard")
					: QCoreApplication::translate("PackageCopySessionsCli", "no"));
			for (const auto& error : {info.error, info.storageError, info.leaseError}) { if (!error.isEmpty()) { lines << error; } }
		}
		result.summary = lines.join(QLatin1Char('\n')); return result;
	}
	const QString id = positional.at(2), hash = values.value(QStringLiteral("--expected-storage-sha256")).toLower();
	if (QUuid(id).isNull() || QUuid(id).toString(QUuid::WithoutBraces) != id
		|| hash.size() != 64 || QByteArray::fromHex(hash.toLatin1()).toHex() != hash.toLatin1()) {
		return failure(2, QCoreApplication::translate("PackageCopySessionsCli", "Supply a session UUID and --expected-storage-sha256 from package copy-sessions."));
	}
	const bool write = seen.contains("--write");
	if (write && StudioSettings(StudioSettings::AccessMode::ReadOnly).storedSchemaIsNewer()) {
		return failure(1, QCoreApplication::translate("PackageCopySessionsCli", "The settings store is read-only."));
	}
	const bool succeeded = discardPackageCopies(directory, id, QByteArray::fromHex(hash.toLatin1()), !write, &result.error);
	result.exitCode = succeeded ? 0 : 1;
	result.payload = {{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("directory"), directory}, {QStringLiteral("id"), id},
		{QStringLiteral("storageSha256"), hash}, {QStringLiteral("dryRun"), !write}, {QStringLiteral("discarded"), write && succeeded}};
	if (succeeded) {
		result.summary = write ? QCoreApplication::translate("PackageCopySessionsCli", "Temporary copy session %1 discarded.").arg(id)
			: QCoreApplication::translate("PackageCopySessionsCli", "Session %1 is unchanged and currently unused. No files were removed; --write repeats the checks before discarding it.").arg(id);
	}
	return result;
}
} // namespace vibestudio::cli
