#include "package_copy_test_helpers.h"
#include "core/package_copy_store.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QUuid>

using namespace vibestudio;
using namespace package_copy_test;
namespace {
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary;
	if (!temporary.isValid() || argc < 2) { return 1; }
	const auto cleanup = qScopeGuard([] { waitForPackageCopyCleanup(); });
	const QString root = QDir(temporary.path()).canonicalPath(), store = QDir(root).filePath(QStringLiteral("copies"));
	const QString settings = QDir(root).filePath(QStringLiteral("settings.ini")); bool ok = true;
	const auto cli = [&](const QStringList& arguments, int expected = 0, bool json = true) {
		QProcess process; process.setWorkingDirectory(root);
		process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--settings-file", settings, "package"} + arguments
			+ QStringList{"--directory", store} + (json ? QStringList{"--json"} : QStringList{}));
		const bool finished = process.waitForFinished(15000);
		ok &= expect(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected, "copy session CLI returns documented exit status");
		if (!finished) { process.kill(); process.waitForFinished(); }
		const auto output = process.readAllStandardOutput();
		if (process.exitCode() != expected) { std::cerr << output.toStdString() << process.readAllStandardError().toStdString(); }
		if (json) { ok &= expect(!QJsonDocument::fromJson(output).object().isEmpty(), "copy session CLI returns JSON on success and failure"); }
		return output;
	};
	auto inventory = QJsonDocument::fromJson(cli({"copy-sessions"})).object();
	ok &= expect(inventory.value("complete").toBool() && inventory.value("sessions").toArray().isEmpty()
		&& !QFileInfo::exists(store) && !QFileInfo::exists(settings), "missing-store inspection creates neither storage nor settings");
	auto shared = QJsonDocument::fromJson(cli({"copy-store-limits"})).object();
	const QString defaultPolicy = shared.value("quota").toObject().value("policySha256").toString();
	ok &= expect(shared.value("scope").toString() == QStringLiteral("managed-copy-store") && !QFileInfo::exists(store) && !QFileInfo::exists(settings), "shared policy inspection has physical-store scope and creates nothing");
	cli({"copy-store-limits", "--max-mib", "2", "--max-files", "4", "--max-entries", "8", "--max-batches", "2"});
	ok &= expect(!QFileInfo::exists(store) && !QFileInfo::exists(settings), "shared limit proposal creates neither storage nor settings");
	cli({"copy-store-limits", "--max-mib", "2", "--max-files", "4", "--max-entries", "8", "--max-batches", "2", "--write"});
	const QString policyPath = QDir(store).filePath(QStringLiteral("limits.json")); const auto savedPolicy = read(policyPath);
	ok &= expect(!savedPolicy.isEmpty() && !QFileInfo::exists(settings), "shared policy commits to its physical store without migrating preferences");
	for (const QStringList& invalid : {QStringList{"--max-mib", "0"}, {"--max-files", "100001"}, {"--max-entries", QString::number(PackageCopyStorePayloadEntryLimit + 1)},
		{"--max-batches", "1.5"}, {"--max-files", "3", "--max-files=4"}, {"--max-mib", "1", "--write", "--dry-run"},
		{"--write"}, {"--unknown"}, {"--write=false"}, {"--max-mib", "1", "--expected-policy-sha256", "invalid"}}) {
		cli(QStringList{"copy-store-limits"} + invalid, 2); ok &= expect(read(policyPath) == savedPolicy, "invalid shared policy options preserve committed limits");
	}
	cli({"copy-store-limits", "--max-mib", "1", "--expected-policy-sha256", defaultPolicy, "--write"}, 1);
	ok &= expect(read(policyPath) == savedPolicy, "CLI policy compare-and-swap refuses stale reviews");
	Reader reader; reader.add(QStringLiteral("read.txt"), "active"); PackageCopyRequest request; request.storeDirectory = store; request.entryIndexes = {0};
	auto active = copyPackageEntries(reader, request); if (!expect(active.succeeded(), "prepare a live managed copy for cross-process CLI protection")) { return 1; }
	inventory = QJsonDocument::fromJson(cli({"copy-sessions"})).object();
	const auto activeInfo = inventory.value("sessions").toArray().first().toObject();
	ok &= expect(inventory.value("quota").toObject().value("reserved").toObject().value("bytes").toInteger() == 6
		&& activeInfo.value("reservationKnown").toBool(), "CLI inventory exposes another process's durable shared reservation");
	ok &= expect(!activeInfo.value("discardAvailable").toBool(true) && activeInfo.value("payloadBytes").toInteger() == 6, "CLI reports actual live usage without claiming discard availability");
	QStringList discard{"copy-discard", activeInfo.value("id").toString(), "--expected-storage-sha256", activeInfo.value("storageSha256").toString()};
	cli(discard, 1); cli(discard + QStringList{"--write"}, 1);
	ok &= expect(read(active.paths.first()) == "active", "dry run and write cannot discard another process's live session");
	active.storage.reset(); active.session.reset(); waitForPackageCopyCleanup();
	const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces), path = QDir(store).filePath(id + QStringLiteral(".copies"));
	const QString batch = QDir(path).filePath(QStringLiteral("package-copy-abc123")), payload = QDir(batch).filePath(QStringLiteral("keep.txt"));
	if (!expect(QDir().mkpath(batch) && write(payload, "orphan"), "create an isolated interrupted-record fixture")) { return 1; }
	inventory = QJsonDocument::fromJson(cli({"copy-sessions"})).object();
	const auto info = inventory.value("sessions").toArray().first().toObject();
	shared = QJsonDocument::fromJson(cli({"copy-store-limits"}, 1)).object();
	ok &= expect(!shared.value("quota").toObject().value("complete").toBool(true) && !shared.value("quota").toObject().value("error").toString().isEmpty(), "legacy interrupted sessions make shared admission accounting explicitly incomplete");
	discard = {"copy-discard", id, "--expected-storage-sha256", info.value("storageSha256").toString()};
	ok &= expect(info.value("reviewable").toBool() && info.value("discardAvailable").toBool() && !info.value("error").toString().isEmpty(),
		"CLI exposes interrupted-record diagnostics and a usable storage review token");
	const QByteArray text = cli({"copy-sessions"}, 0, false);
	ok &= expect(text.contains(id.toUtf8()) && text.contains(info.value("storageSha256").toString().toLatin1()), "human output includes full session IDs and review tokens");
	const auto preview = QJsonDocument::fromJson(cli(discard)).object();
	ok &= expect(preview.value("dryRun").toBool() && !preview.value("discarded").toBool(true) && read(payload) == "orphan"
		&& !QFileInfo::exists(settings), "default discard previews create no settings and preserve all files");
	for (const QStringList& invalid : {QStringList{"copy-sessions", "--write"}, {"copy-sessions", "--dry-run"}, {"copy-sessions", "extra"},
		{"copy-sessions", "--unknown"}, {"copy-sessions", "--directory=again"}, {"copy-sessions", "--json=false"},
		{"copy-discard", "../escape", "--expected-storage-sha256", QString(64, 'a')}, {"copy-discard", id},
		{"copy-discard", id, "--expected-storage-sha256", QString(64, 'g')},
		discard + QStringList{"--write", "--dry-run"}, discard + QStringList{"--write=false"},
		discard + QStringList{"--expected-storage-sha256=again"}, discard + QStringList{"extra"}}) {
		cli(invalid, 2); ok &= expect(read(payload) == "orphan" && !QFileInfo::exists(settings), "invalid commands cannot mutate storage or settings");
	}
	const QByteArray legacy("[custom]\nvalue=preserved\n"); ok &= expect(write(settings, legacy), "write owned legacy settings");
	cli({"copy-sessions"}); cli(discard); ok &= expect(read(settings) == legacy, "inspection and dry discard never migrate legacy settings");
	const QByteArray future("[app]\nsettingsSchemaVersion=9999\n[custom]\nvalue=preserved\n");
	ok &= expect(write(settings, future), "write owned future settings"); cli(discard + QStringList{"--write"}, 1);
	cli({"copy-store-limits", "--max-mib", "1", "--write"}, 1);
	ok &= expect(read(policyPath) == savedPolicy, "future preferences also prevent shared limit writes");
	ok &= expect(read(settings) == future && read(payload) == "orphan", "future settings prevent written discard without mutation");
	ok &= expect(write(settings, legacy) && write(payload, "consumer edits"), "modify only owned fixture payload");
	cli(discard + QStringList{"--write"}, 1); ok &= expect(read(payload) == "consumer edits", "stale review fails before removing changed payloads");
	inventory = QJsonDocument::fromJson(cli({"copy-sessions"})).object();
	discard[3] = inventory.value("sessions").toArray().first().toObject().value("storageSha256").toString();
	const auto removed = QJsonDocument::fromJson(cli(discard + QStringList{"--write"})).object();
	ok &= expect(removed.value("discarded").toBool() && !removed.value("dryRun").toBool(true) && !QFileInfo::exists(path)
		&& read(settings) == legacy, "reviewed written discard removes the selected session without modifying preferences");
	ok &= expect(QDir().mkpath(path) && write(QDir(path).filePath(QStringLiteral("unexpected.txt")), "retain"), "prepare unsupported storage fixture");
	const auto incomplete = QJsonDocument::fromJson(cli({"copy-sessions"}, 1)).object();
	ok &= expect(!incomplete.value("ok").toBool(true) && !incomplete.value("complete").toBool(true)
		&& incomplete.value("sessions").toArray().size() == 1 && !incomplete.value("sessions").toArray().first().toObject().value("storageError").toString().isEmpty(),
		"failed inspection keeps partial inventory and per-session diagnostics in the JSON error envelope");
	return ok ? 0 : 1;
}
