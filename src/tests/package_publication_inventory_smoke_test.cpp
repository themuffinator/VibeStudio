#include "tests/package_publication_test_helpers.h"
#include "core/studio_settings.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QLockFile>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::publication_test;
namespace {
bool expect(bool value, const char* label) { if (!value) { std::cerr << label << '\n'; } return value; }
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	const QDir root(temporary.path()); bool ok = true;
	const auto info = interrupt(root.filePath("interrupted"), QByteArray(262144, 'a'), QByteArray(262144, 'b'));
	ok &= expect(info.metadataValid(), "prepare interrupted save"); if (!ok) { return 1; }
	const QString folder = QFileInfo(info.journalPath).absolutePath();
	const auto entries = QDir(folder).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
	auto inventory = listPackagePublicationJournals({folder, folder + "/."});
	ok &= expect(inventory.complete() && inventory.directories.size() == 1 && inventory.journals.size() == 1
		&& inventory.journals.first().journalSha256 == info.journalSha256, "deduplicate roots and discover review token");
	ok &= expect(!packagePublicationInventoryJson(inventory).value("payloadsVerified").toBool()
		&& entries == QDir(folder).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), "inventory is metadata-only and writes no lock or payload");
	bool cancelled = false; int callbacks = 0;
	PackageReadControl control; control.isCancelled = [&]() { return cancelled; };
	control.progress = [&](const QString& path, qint64 done, qint64 total) {
		++callbacks; if (path == info.destinationPath && done >= 65536 && total == 262144) { cancelled = true; }
	};
	auto report = recoverPackagePublication(info.journalPath, false, {}, control, info.journalSha256);
	ok &= expect(report.cancelled && !report.error.isEmpty() && !report.finished && callbacks >= 2
		&& read(info.destinationPath) == QByteArray(262144, 'b') && QFileInfo::exists(info.journalPath), "cancel content verification within a large file without mutation");
	report = recoverPackagePublication(info.journalPath, true, {}, {}, QByteArray(32, 'x'));
	ok &= expect(!report.error.isEmpty() && !report.finished && !QFileInfo::exists(info.backupPath), "stale review rejects before backup publication");
	const QString lockPath = info.destinationPath + ".vibestudio-save.lock";
	{
		QLockFile lock(lockPath); ok &= expect(lock.tryLock(), "reserve live output lock");
		ok &= expect(recoverPackagePublication(info.journalPath).canFinish, "read-only verification does not acquire writer lock");
		ok &= expect(!recoverPackagePublication(info.journalPath, true).error.isEmpty(), "finish respects live output writer");
	}
	const auto payload = read(info.replacementPath); ok &= write(info.replacementPath, "changed");
	ok &= expect(listPackagePublicationJournals({folder}).journals.first().metadataValid()
		&& !recoverPackagePublication(info.journalPath).error.isEmpty(), "metadata discovery never implies payload integrity");
	ok &= write(info.replacementPath, payload);
	ok &= write(info.journalPath, read(info.journalPath) + '\n');
	report = recoverPackagePublication(info.journalPath, true, {}, {}, info.journalSha256);
	ok &= expect(!report.error.isEmpty() && !QFileInfo::exists(info.backupPath), "even valid journal edits invalidate the selected review");
	const auto refreshed = inspectPackagePublicationJournal(info.journalPath);
	cancelled = false;
	control.progress = [&](const QString& path, qint64, qint64) {
		if (path == info.destinationPath && QFileInfo::exists(info.backupPath)) { cancelled = true; }
	};
	report = recoverPackagePublication(info.journalPath, true, {}, control, refreshed.journalSha256);
	ok &= expect(report.cancelled && report.backupVerified && !report.finished && !report.canFinish
		&& read(info.backupPath) == QByteArray(262144, 'a') && QFileInfo::exists(info.journalPath)
		&& QFileInfo::exists(info.replacementPath) && QFileInfo::exists(info.originalPath), "cancellation after backup publication preserves a retryable journal and retained content");
	report = recoverPackagePublication(info.journalPath, true, {}, {}, refreshed.journalSha256);
	ok &= expect(report.finished && report.backupVerified && !QFileInfo::exists(info.journalPath)
		&& read(info.destinationPath) == QByteArray(262144, 'b'), "fresh retry completes verified cleanup without changing output");
	const auto created = interrupt(root.filePath("new"), QByteArray());
	ok &= expect(created.metadataValid() && recoverPackagePublication(created.journalPath, true).finished, "new output recovery needs no original or backup");
#ifndef Q_OS_WIN
	const QString upper = root.filePath("CaseFolder"), lower = root.filePath("casefolder"); QDir().mkpath(upper); QDir().mkpath(lower);
	if (QFileInfo(upper).canonicalFilePath() != QFileInfo(lower).canonicalFilePath()) {
		const QString backup = QDir(lower).filePath("backup.pak"); const auto caseSensitive = interrupt(upper, "old", "new", backup);
		report = recoverPackagePublication(caseSensitive.journalPath);
		ok &= expect(report.requiresBackupConfirmation && !report.canFinish, "case-different sibling folder is an external backup on case-sensitive filesystems");
		ok &= expect(!recoverPackagePublication(caseSensitive.journalPath, true, QDir(upper).filePath("backup.pak")).error.isEmpty()
			&& recoverPackagePublication(caseSensitive.journalPath, true, backup).finished, "external confirmation respects filesystem case sensitivity");
	}
#endif
	const auto original = interrupt(root.filePath("before-commit"), "original", "new", {}, PackagePublicationStep::CommitOutput);
	report = recoverPackagePublication(original.journalPath, true);
	ok &= expect(report.state == "original" && !report.finished && read(original.destinationPath) == "original", "recovery does not install an uncommitted replacement");
	const auto invalidRoot = root.filePath("invalid"); QDir().mkpath(invalidRoot);
	ok &= write(QDir(invalidRoot).filePath(".vibestudio-package-AAAAAA.payload.json"), QByteArray(65537, 'x'));
	ok &= write(QDir(invalidRoot).filePath(".vibestudio-package-BBBBBB.payload.json"), "{");
	ok &= write(QDir(invalidRoot).filePath("unrelated.json"), "{}");
	inventory = listPackagePublicationJournals({invalidRoot});
	ok &= expect(!inventory.complete() && inventory.journals.size() == 2 && inventory.errors.size() == 2
		&& !inventory.journals.first().metadataValid() && inventory.visitedEntries == 3, "bounded invalid journals remain visible alongside truthful scan errors");
	const auto full = root.filePath("full"), extra = root.filePath("extra"); QDir().mkpath(full); QDir().mkpath(extra);
	for (int i = 0; i < PackagePublicationJournalLimit; ++i) {
		ok &= write(QDir(full).filePath(QStringLiteral(".vibestudio-package-%1.payload.json").arg(i, 6, 10, QLatin1Char('0'))), "{}");
	}
	ok &= write(QDir(extra).filePath(".vibestudio-package-ABCDEF.payload.json"), "{}");
	inventory = listPackagePublicationJournals({full});
	ok &= expect(!inventory.truncated && inventory.journals.size() == PackagePublicationJournalLimit, "exact journal limit is not falsely truncated");
	inventory = listPackagePublicationJournals({full, extra});
	ok &= expect(inventory.truncated && inventory.journals.size() == PackagePublicationJournalLimit, "next folder beyond exact journal limit is reported incomplete");
	QStringList many; for (int i = 0; i < PackagePublicationDirectoryLimit + 1; ++i) { many << folder; }
	ok &= expect(listPackagePublicationJournals(many).truncated, "directory input is bounded");
	cancelled = true; ok &= expect(listPackagePublicationJournals({folder}, control).cancelled, "folder scan can be cancelled before I/O");
	StudioSettings::setOverrideFilePath(root.filePath("settings.ini"));
	for (int i = 0; i < 20; ++i) { StudioSettings().rememberPackagePublicationDirectory(root.filePath(QString::number(i))); }
	StudioSettings().rememberPackagePublicationDirectory(root.filePath("19/."));
	const auto remembered = StudioSettings().packagePublicationDirectories();
	ok &= expect(remembered.size() == 16 && remembered.first() == root.filePath("19") && remembered.last() == root.filePath("4"), "save destinations persist immediately with a bounded deduplicated history");
	return ok ? 0 : 1;
}
