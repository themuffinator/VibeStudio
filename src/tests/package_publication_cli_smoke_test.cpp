#include "tests/package_publication_test_helpers.h"
#include "core/studio_settings.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::publication_test;
namespace {
bool expect(bool value, const char* label) { if (!value) { std::cerr << label << '\n'; } return value; }
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary; if (!temporary.isValid() || argc < 2) { return 1; }
	const QDir root(temporary.path()); bool ok = true;
	const auto info = interrupt(root.path()); if (!info.metadataValid()) { return 1; }
	const QString settings = root.filePath("settings.ini"); StudioSettings(settings).sync();
	const QByteArray settingsBefore = read(settings);
	const auto cli = [&](const QStringList& args, int exit = 0) {
		QProcess process; process.setWorkingDirectory(root.path());
		process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--settings-file", settings, "package"} + args + QStringList{"--json"});
		const bool ended = process.waitForFinished(15000); const QByteArray out = process.readAllStandardOutput();
		if (!ended || process.exitCode() != exit) { std::cerr << args.join(' ').toStdString() << '\n' << out.toStdString() << process.readAllStandardError().toStdString(); }
		ok &= expect(ended && process.exitStatus() == QProcess::NormalExit && process.exitCode() == exit, "CLI exit");
		QJsonParseError error; const auto json = QJsonDocument::fromJson(out, &error);
		ok &= expect(error.error == QJsonParseError::NoError && json.isObject(), "CLI result is JSON"); return json.object();
	};
	const auto inventory = cli({"interrupted-saves"}).value("inventory").toObject();
	const auto rows = inventory.value("journals").toArray();
	ok &= expect(inventory.value("complete").toBool() && !inventory.value("payloadsVerified").toBool() && rows.size() == 1, "current-directory discovery is bounded metadata");
	const QString hash = QString::fromLatin1(info.journalSha256.toHex());
	ok &= expect(rows.first().toObject().value("journalSha256").toString() == hash, "inventory exposes exact journal review token");
	ok &= expect(cli({"interrupted-saves", "--directory", root.path(), "--directory=" + root.path()}).value("inventory").toObject()
		.value("directories").toArray().size() == 1, "repeatable directory options deduplicate roots");
	for (const QStringList& args : {QStringList{"interrupted-saves", "--directory="}, {"interrupted-saves", "--directory"},
		{"interrupted-saves", "extra"}, {"interrupted-saves", "--finish"}, {"interrupted-saves", "--json=false"},
		{"recover", info.journalPath, "--expected-sha256="}, {"recover", info.journalPath, "--expected-sha256", "wrong"},
		{"recover", info.journalPath, "--finish", "--finish"}, {"recover", info.journalPath, "--backup", info.backupPath}}) { cli(args, 2); }
	QStringList tooMany{"interrupted-saves"}; for (int i = 0; i < 33; ++i) { tooMany << "--directory" << root.path(); } cli(tooMany, 2);
	cli({"interrupted-saves", "--directory", root.filePath("missing")}, 4);
	cli({"recover", root.filePath("missing.json")}, 3);
	const auto reviewed = cli({"recover", info.journalPath, "--expected-sha256", hash}).value("recovery").toObject();
	ok &= expect(reviewed.value("canFinish").toBool() && !reviewed.value("finished").toBool()
		&& !QFileInfo::exists(info.backupPath) && read(settings) == settingsBefore, "review commands do not publish a backup or update preferences");
	ok &= write(info.journalPath, read(info.journalPath) + '\n');
	cli({"recover", info.journalPath, "--finish", "--expected-sha256", hash}, 4);
	ok &= expect(!QFileInfo::exists(info.backupPath) && QFileInfo::exists(info.journalPath), "stale CLI review preserves transaction");
	const QString refreshed = QString::fromLatin1(inspectPackagePublicationJournal(info.journalPath).journalSha256.toHex());
	ok &= expect(cli({"recover", info.journalPath, "--finish", "--expected-sha256", refreshed}).value("recovery").toObject().value("finished").toBool()
		&& read(info.backupPath) == "original" && read(info.destinationPath) == "replacement", "reviewed finish publishes the original backup and preserves replacement");
	ok &= expect(cli({"interrupted-saves"}).value("inventory").toObject().value("journals").toArray().isEmpty(), "completed saves leave no discovery row");
	QDir().mkpath(root.filePath("exports"));
	const QString output = root.filePath("exports/created.pak");
	cli({"create", output, "--dry-run"});
	ok &= expect(!QFileInfo::exists(output) && read(settings) == settingsBefore, "archive dry run does not remember a destination");
	cli({"create", output});
	ok &= expect(QFileInfo::exists(output) && StudioSettings(settings).packagePublicationDirectories().contains(root.filePath("exports")), "actual CLI archive save remembers its output folder");
	return ok ? 0 : 1;
}
