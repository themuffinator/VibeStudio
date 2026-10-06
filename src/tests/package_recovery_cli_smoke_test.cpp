#include "core/package_recovery.h"
#include "core/package_draft.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>

#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* label)
{
	if (!value) { std::cerr << label << '\n'; }
	return value;
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary;
	if (!temporary.isValid() || argc < 2) { return 1; }
	const QDir root(temporary.path()); const QString store = root.filePath(QStringLiteral("package-recovery"));
	const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Pk3);
	plan.addBytes("retained", QStringLiteral("one.txt")); plan.addBytes("redo", QStringLiteral("two.txt")); plan.undo();
	auto session = PackageRecoverySession::acquire(store, id);
	if (!session) { return 1; }
	const auto checkpoint = session->checkpoint(plan, QStringLiteral("CLI fixture"));
	if (!checkpoint.succeeded()) { std::cerr << checkpoint.error.toStdString(); return 1; }
	session.reset(); bool ok = true;
	const QString hash = QString::fromLatin1(checkpoint.manifestSha256.toHex());
	const auto cli = [&](const QStringList& args, int exit = 0) {
		QProcess process; process.setWorkingDirectory(root.path());
		process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--settings-file", root.filePath("settings.ini"), "package"} + args + QStringList{"--json"});
		const bool ended = process.waitForFinished(15000); const QByteArray out = process.readAllStandardOutput();
		if (!ended || process.exitCode() != exit) { std::cerr << args.join(' ').toStdString() << '\n' << out.toStdString() << process.readAllStandardError().toStdString(); }
		ok &= expect(ended && process.exitStatus() == QProcess::NormalExit && process.exitCode() == exit, "CLI exit");
		QJsonParseError parse; const auto json = QJsonDocument::fromJson(out, &parse);
		ok &= expect(parse.error == QJsonParseError::NoError && json.isObject(), "machine-readable CLI result"); return json.object();
	};
	const auto inventory = cli({"recoveries"}).value("inventory").toObject().value("records").toArray();
	ok &= expect(inventory.size() == 1 && inventory.first().toObject().value("metadataValid").toBool()
		&& inventory.first().toObject().value("manifestSha256").toString() == hash, "settings-isolated default recovery directory");
	cli({"recoveries", "--directory", store, "--directory", store}, 2);
	cli({"recoveries", "--write"}, 2); cli({"recoveries", "extra"}, 2); cli({"recoveries", "--directory="}, 2);
	const QString destination = root.filePath(QStringLiteral("restored.vibepackage"));
	const QStringList restore{"draft-recover", id, "--expected-sha256", hash, "--output", destination};
	ok &= expect(cli(restore + QStringList{"--dry-run"}).value("dryRun").toBool() && !QFileInfo::exists(destination), "CLI restore dry run creates no files");
	cli({"draft-recover", id, "--output", destination}, 2);
	cli(restore + QStringList{"--overwrite"}, 2); cli(restore + QStringList{"--write"}, 2);
	cli(restore + QStringList{"--expected-sha256", hash}, 2);
	cli({"draft-recover", id, "--expected-sha256", QString(64, '0'), "--output", destination}, 4);
	ok &= expect(!QFileInfo::exists(destination), "invalid options and stale selection cannot create a draft");
	ok &= expect(cli(restore).value("recovered").toBool(), "CLI recovers to a new draft");
	cli(restore, 4);
	PackageStagingModel recovered; QString error;
	ok &= expect(PackageDraft::load(destination, &recovered, &error) && recovered.canRedo() && recovered.redo(), "CLI recovery preserves redo");
	const QStringList discard{"recovery-discard", id, "--expected-sha256", hash};
	ok &= expect(cli(discard).value("dryRun").toBool() && QFileInfo::exists(checkpoint.path), "discard defaults to a dry run");
	cli(discard + QStringList{"--write", "--dry-run"}, 2);
	cli(discard + QStringList{"--output", destination}, 2);
	cli({"recovery-discard", id}, 2);
	ok &= expect(cli(discard + QStringList{"--write"}).value("discarded").toBool() && !QFileInfo::exists(checkpoint.path), "reviewed CLI discard removes only the recovery copy");
	QByteArray bytes; PackageStagingArchive(recovered).readEntryBytes(QStringLiteral("two.txt"), &bytes, &error);
	ok &= expect(bytes == "redo", "restored draft survives CLI recovery discard");
	ok &= expect(cli({"recoveries"}).value("inventory").toObject().value("records").toArray().isEmpty(), "final inventory is empty");
	const QString incompleteId = QUuid::createUuid().toString(QUuid::WithoutBraces), incomplete = packageRecoveryPath(store, incompleteId);
	QDir().mkpath(QDir(incomplete).filePath(QStringLiteral("objects")));
	QFile partial(QDir(incomplete).filePath(QStringLiteral("objects/.writing-AbC123")));
	ok &= partial.open(QIODevice::WriteOnly) && partial.write("unfinished") == 10; partial.close();
	const auto listing = cli({"recoveries"}, 4);
	const auto storage = listing.value("inventory").toObject();
	const QString storageHash = storage.value("records").toArray().first().toObject().value("storageSha256").toString();
	ok &= expect(storage.value("storageComplete").toBool() && storage.value("storageBytes").toInteger() == 10
		&& listing.value("limits").toObject().value("maximumCopies").toInt() == 32 && storageHash.size() == 64, "CLI exposes incomplete storage usage, limits and review token");
	const QStringList reviewed{"recovery-discard", incompleteId, "--expected-storage-sha256", storageHash};
	cli(reviewed + QStringList{"--expected-sha256", hash}, 2);
	cli(reviewed + QStringList{"--expected-storage-sha256", storageHash}, 2);
	cli({"draft-recover", incompleteId, "--expected-storage-sha256", storageHash, "--output", destination}, 2);
	cli({"recoveries", "--expected-storage-sha256", storageHash}, 2);
	ok &= expect(cli(reviewed).value("dryRun").toBool() && QFileInfo::exists(incomplete), "incomplete discard defaults to preview");
	QFile added(QDir(incomplete).filePath(QStringLiteral(".document-Def456")));
	ok &= added.open(QIODevice::WriteOnly) && added.write("new") == 3; added.close();
	cli(reviewed + QStringList{"--write"}, 4);
	ok &= expect(QFileInfo::exists(partial.fileName()), "stale storage checksum preserves partial data");
	const QString refreshed = QString::fromLatin1(inspectPackageRecovery(incomplete).storageSha256.toHex());
	ok &= expect(cli({"recovery-discard", incompleteId, "--expected-storage-sha256", refreshed, "--write"}).value("discarded").toBool()
		&& !QFileInfo::exists(incomplete), "CLI discards a freshly reviewed incomplete copy");
	return ok ? 0 : 1;
}
