#include "core/package_draft.h"
#include "core/package_draft_storage.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* label)
{
	if (!value) { std::cerr << label << '\n'; } return value;
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary;
	if (!temporary.isValid() || argc < 2) { return 1; }
	const QDir root(temporary.path()); const QString profile = root.filePath(QStringLiteral("settings.ini"));
	StudioSettings::setOverrideFilePath(profile); bool ok = true;
	for (const char* name : {"TEMP", "TMP", "TMPDIR"}) { qputenv(name, root.path().toLocal8Bit()); }
	const auto cli = [&](const QStringList& args, int expected = 0) {
		QProcess process; process.setWorkingDirectory(root.path());
		process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--settings-file", profile, "package"} + args + QStringList{"--json"});
		const bool ended = process.waitForFinished(15000); const auto output = process.readAllStandardOutput();
		if (!ended || process.exitCode() != expected) { std::cerr << args.join(' ').toStdString() << '\n' << output.toStdString() << process.readAllStandardError().toStdString(); }
		ok &= expect(ended && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected, "CLI exit");
		QJsonParseError error; const auto json = QJsonDocument::fromJson(output, &error);
		ok &= expect(error.error == QJsonParseError::NoError && json.isObject(), "machine-readable storage result"); return json.object();
	};
	const QString missing = root.filePath(QStringLiteral("absent.vibepackage"));
	cli({"draft-storage", missing}, 4);
	ok &= expect(!QFileInfo::exists(missing), "reviewing an absent draft creates no directory");
	PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Pk3); QString error;
	const QString draft = root.filePath(QStringLiteral("saved.vibepackage"));
	ok &= plan.addBytes("retained", QStringLiteral("one.txt"), &error);
	ok &= plan.addBytes("redo", QStringLiteral("two.txt"), &error); ok &= plan.undo();
	ok &= expect(PackageDraft::save(draft, &plan, false, &error), "prepare saved draft");
	if (!ok) { std::cerr << error.toStdString(); return 1; }
	const QByteArray unused("unused");
	const QString orphan = QDir(draft).filePath(QStringLiteral("objects/") + QString::fromLatin1(QCryptographicHash::hash(unused, QCryptographicHash::Sha256).toHex()));
	ok &= write(orphan, unused);
	const auto reviewed = cli({"draft-storage", draft}); const auto storage = reviewed.value("storage").toObject();
	const QString hash = storage.value("storageSha256").toString();
	ok &= expect(storage.value("complete").toBool() && storage.value("reclaimableFiles").toInt() == 1
		&& storage.value("reclaimableBytes").toInteger() == 6 && hash.size() == 64
		&& reviewed.value("limits").toObject().value("maximumFiles").toInt() == 200000, "review exposes verified history, unused objects and limits");
	const auto fingerprint = inspectPackageStorage(draft).fingerprint;
	const QStringList compact{"draft-compact", draft, "--expected-storage-sha256", hash};
	const auto preview = cli(compact);
	ok &= expect(preview.value("dryRun").toBool() && !preview.value("readerExclusion").toBool()
		&& inspectPackageStorage(draft).fingerprint == fingerprint && !QFileInfo::exists(QDir(draft).filePath(QStringLiteral(".write.lock"))), "default dry run writes no locks and reports its liveness boundary");
	const auto busy = cli(compact + QStringList{"--write"}, 4);
	ok &= expect(!busy.value("readerExclusion").toBool() && busy.value("reclaimedFiles").toInt() == 0 && QFileInfo::exists(orphan), "CLI maintenance respects a different process's active document");
	cli({"draft-storage", draft, "--write"}, 2); cli({"draft-storage", draft, "extra"}, 2);
	cli({"draft-compact", draft}, 2); cli(compact + QStringList{"--expected-storage-sha256", hash}, 2);
	cli(compact + QStringList{"--write", "--dry-run"}, 2); cli(compact + QStringList{"--write=true"}, 2);
	cli(compact + QStringList{"--unknown"}, 2); cli({"draft-compact", draft, "--expected-storage-sha256="}, 2);
	cli({"draft-compact", draft, "--expected-storage-sha256", "--write"}, 2);
	plan.clear();
	ok &= write(QDir(draft).filePath(QStringLiteral(".document-Ab1234")), "partial");
	cli(compact + QStringList{"--write"}, 4);
	ok &= expect(QFileInfo::exists(orphan), "stale checksum preserves all reviewed objects");
	const auto fresh = cli({"draft-storage", draft}).value("storage").toObject();
	const auto reclaimed = cli({"draft-compact", draft, "--expected-storage-sha256", fresh.value("storageSha256").toString(), "--write"});
	ok &= expect(reclaimed.value("readerExclusion").toBool() && reclaimed.value("reclaimedFiles").toInt() == 2
		&& reclaimed.value("reclaimedBytes").toInteger() == 13 && !QFileInfo::exists(orphan), "reviewed CLI cleanup excludes readers and removes only unused files");
	PackageStagingModel loaded; QByteArray bytes;
	ok &= expect(PackageDraft::load(draft, &loaded, &error) && loaded.redo()
		&& PackageStagingArchive(loaded).readEntryBytes(QStringLiteral("two.txt"), &bytes, &error) && bytes == "redo", "CLI compaction preserves redo payloads");
	loaded.clear();
	const QString foreign = QDir(draft).filePath(QStringLiteral("notes.txt")); ok &= write(foreign, "unrelated");
	cli({"draft-storage", draft}, 4); ok &= QFile::remove(foreign);
	const QString input = root.filePath(QStringLiteral("input.txt")), output = root.filePath(QStringLiteral("quota.vibepackage"));
	ok &= write(input, "same payload"); StudioSettings().setPackageDraftMaximumFiles(1);
	const QStringList create{"create", output, "--format", "pk3", "--add-file", input, "--as", "one.txt", "--add-file", input, "--as", "two.txt"};
	cli(create + QStringList{"--dry-run"}, 1); cli(create, 1);
	ok &= expect(!QFileInfo::exists(output), "CLI dry run and write both enforce quota before creating a draft");
	StudioSettings().setPackageDraftMaximumFiles(2); cli(create);
	ok &= expect(inspectPackageStorage(output).files.size() == 2, "CLI quota permits deduplicated payloads and metadata");
	std::cout << (ok ? "Package draft storage CLI smoke passed\n" : "Package draft storage CLI smoke failed\n"); return ok ? 0 : 1;
}
