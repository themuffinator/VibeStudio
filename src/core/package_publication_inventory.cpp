#include "core/package_publication.h"
#include "core/package_storage.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace vibestudio {

PackagePublicationInventory listPackagePublicationJournals(const QStringList& directories, const PackageReadControl& control)
{
	PackagePublicationInventory inventory; QSet<QString> seen; bool exhausted = false;
	static const QRegularExpression pattern(QStringLiteral("^\\.vibestudio-package-[A-Za-z0-9]{6,}\\.payload\\.json$"));
	if (directories.isEmpty()) {
		inventory.errors << QCoreApplication::translate("PackagePublicationInventory", "Choose an output folder to inspect interrupted saves."); return inventory;
	}
	if (directories.size() > PackagePublicationDirectoryLimit) {
		inventory.truncated = true;
		inventory.errors << QCoreApplication::translate("PackagePublicationInventory", "Too many output folders. Review at most %1 folders at a time.").arg(PackagePublicationDirectoryLimit);
	}
	const auto stopped = [&]() {
		if (!control.isCancelled || !control.isCancelled()) { return false; }
		inventory.cancelled = true; return true;
	};
	for (qsizetype index = 0; index < qMin<qsizetype>(directories.size(), PackagePublicationDirectoryLimit); ++index) {
		if (stopped()) { break; }
		const QString path = directories.at(index); const QFileInfo directory(path); QString error;
		if (path.trimmed().isEmpty() || !safePackageStoragePath(path, &error) || !directory.isDir() || !directory.isReadable()) {
			inventory.errors << QCoreApplication::translate("PackagePublicationInventory", "Output folder is unavailable or unsafe: %1").arg(path); continue;
		}
		const QString canonical = directory.canonicalFilePath();
#ifdef Q_OS_WIN
		const QString key = canonical.toCaseFolded();
#else
		const QString key = canonical;
#endif
		if (seen.contains(key)) { continue; } seen.insert(key); inventory.directories << canonical;
		// Count every directory member instead of relying on a name-filtered
		// iterator that could traverse an unbounded number of unrelated files.
		QDirIterator entries(canonical, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
		while (entries.hasNext()) {
			if (stopped()) { break; }
			if (inventory.visitedEntries >= PackagePublicationDirectoryEntryLimit) {
				inventory.truncated = true; exhausted = true;
				inventory.errors << QCoreApplication::translate("PackagePublicationInventory", "The folder scan reached %1 entries. Narrow the selected output folders.").arg(PackagePublicationDirectoryEntryLimit); break;
			}
			const QString file = entries.next(); ++inventory.visitedEntries;
			if (control.progress) { control.progress(canonical, inventory.visitedEntries, 0); }
			if (!pattern.match(entries.fileName()).hasMatch()) { continue; }
			if (inventory.journals.size() >= PackagePublicationJournalLimit) {
				inventory.truncated = true; exhausted = true;
				inventory.errors << QCoreApplication::translate("PackagePublicationInventory", "The review reached %1 interrupted saves. Select fewer output folders.").arg(PackagePublicationJournalLimit); break;
			}
			auto journal = inspectPackagePublicationJournal(file);
			if (!journal.metadataValid()) { inventory.errors << QCoreApplication::translate("PackagePublicationInventory", "%1: %2").arg(journal.journalPath, journal.error); }
			inventory.journals.append(std::move(journal));
		}
		if (inventory.cancelled || exhausted) { break; }
	}
	std::sort(inventory.journals.begin(), inventory.journals.end(), [](const auto& left, const auto& right) { return left.journalPath < right.journalPath; });
	return inventory;
}

QJsonObject packagePublicationInventoryJson(const PackagePublicationInventory& inventory)
{
	QJsonArray journals;
	for (const auto& info : inventory.journals) {
		journals.append(QJsonObject{{QStringLiteral("journalPath"), info.journalPath}, {QStringLiteral("destinationPath"), info.destinationPath},
			{QStringLiteral("replacementPath"), info.replacementPath}, {QStringLiteral("originalPath"), info.originalPath}, {QStringLiteral("backupPath"), info.backupPath},
			{QStringLiteral("journalSha256"), QString::fromLatin1(info.journalSha256.toHex())}, {QStringLiteral("metadataValid"), info.metadataValid()},
			{QStringLiteral("error"), info.error}});
	}
	return {{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("directories"), QJsonArray::fromStringList(inventory.directories)},
		{QStringLiteral("journals"), journals}, {QStringLiteral("visitedEntries"), inventory.visitedEntries},
		{QStringLiteral("complete"), inventory.complete()}, {QStringLiteral("truncated"), inventory.truncated},
		{QStringLiteral("cancelled"), inventory.cancelled}, {QStringLiteral("errors"), QJsonArray::fromStringList(inventory.errors)},
		{QStringLiteral("payloadsVerified"), false}};
}

} // namespace vibestudio
