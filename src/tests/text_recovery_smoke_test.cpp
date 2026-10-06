#include "app/code_recovery.h"
#include "core/text_recovery.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>

#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* message)
{
	if (!condition) { std::cerr << message << '\n'; }
	return condition;
}
bool writeFile(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readFile(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool waitFor(const std::function<bool()>& done)
{
	QElapsedTimer timer;
	timer.start();
	while (!done() && timer.elapsed() < 15000) { QCoreApplication::processEvents(); QThread::msleep(2); }
	return done();
}
QJsonObject cli(const QString& binary, const QStringList& args, int expected, bool& ok)
{
	QProcess process;
	process.start(binary, QStringList {QStringLiteral("--cli"), QStringLiteral("--json")} + args);
	ok &= expect(process.waitForFinished(30000) && process.exitCode() == expected, "unexpected recovery CLI exit code");
	const auto json = QJsonDocument::fromJson(process.readAllStandardOutput());
	ok &= expect(json.isObject(), "recovery CLI must return JSON");
	return json.object();
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) { return 1; }
	bool ok = true;
	const QString directory = temp.filePath(QStringLiteral("recovery"));
	const QString sourcePath = temp.filePath(QStringLiteral("original.cfg"));
	const QByteArray original = QByteArray::fromHex("fffe61000d000a006200");
	ok &= expect(writeFile(sourcePath, original), "write recovery source fixture");
	TextRecoverySnapshot snapshot;
	snapshot.source = readTextFile(sourcePath);
	snapshot.text = QStringLiteral("A\nb雪");
	snapshot.title = QStringLiteral("original.cfg");
	snapshot.position = 4;
	snapshot.anchor = 1;
	snapshot.scroll = 30;
	const QString id = newId();
	QString error;
	const QString checkpoint = writeTextRecovery(snapshot, directory, id, &error);
	TextFileDocument restored;
	const auto record = inspectTextRecovery(checkpoint, &restored);
	QByteArray expected;
	encodeTextFile(snapshot.source, snapshot.text, &expected);
	ok &= expect(!checkpoint.isEmpty() && record.isValid() && restored.editable() && restored.path.isEmpty()
		&& restored.resolvedPath.isEmpty() && restored.originalBytes == expected && record.sourcePath == snapshot.source.path
		&& record.sourceSha256 == snapshot.source.sha256 && record.position == 4 && record.anchor == 1
		&& readFile(sourcePath) == original, "recovery preserves unsaved UTF-16, provenance and cursor while never writing the original");
	const auto scan = listTextRecoveries(directory);
	ok &= expect(scan.records.size() == 1 && scan.records.first().isValid() && !scan.limited, "recovery listing verifies saved records");
	if (app.arguments().size() > 1) {
		const auto binary = app.arguments()[1];
		cli(binary, {QStringLiteral("code"), QStringLiteral("recoveries"), QStringLiteral("--directory"), directory}, 0, ok);
		const QString output = temp.filePath(QStringLiteral("restored.cfg"));
		const QStringList recover {QStringLiteral("code"), QStringLiteral("text-recover"), checkpoint, QStringLiteral("--output"), output};
		cli(binary, recover, 0, ok);
		ok &= expect(!QFile::exists(output), "CLI recovery defaults to preview");
		cli(binary, recover + QStringList {QStringLiteral("--write")}, 0, ok);
		ok &= expect(readFile(output) == expected && readFile(sourcePath) == original && QFile::exists(checkpoint), "CLI recovery exports exact edited bytes and keeps both originals");
		cli(binary, recover + QStringList {QStringLiteral("--write")}, 2, ok);
	}
	const QString deletedText = writeTextRecovery(TextRecoverySnapshot {snapshot.source, {}, QStringLiteral("deletion.cfg")}, directory, newId(), &error);
	ok &= expect(inspectTextRecovery(deletedText, &restored).isValid() && restored.text.isEmpty()
		&& restored.originalBytes == QByteArray::fromHex("fffe"), "an entirely deleted buffer remains a valid recovery with its encoding");
	auto damaged = QJsonDocument::fromJson(readFile(checkpoint)).object();
	damaged.insert(QStringLiteral("payload"), QStringLiteral("Y29ycnVwdA=="));
	ok &= expect(writeFile(checkpoint, QJsonDocument(damaged).toJson()) && !inspectTextRecovery(checkpoint, &restored).isValid()
		&& !restored.editable(), "checksum failure cannot return a writable partial document");
	ok &= expect(writeTextRecovery(snapshot, directory, QStringLiteral("../outside"), &error).isEmpty()
		&& !removeTextRecovery(directory, QStringLiteral("../outside"), &error), "recovery identifiers cannot escape their directory");
	int cancellationChecks = 0;
	const QString cancelledId = newId();
	ok &= expect(writeTextRecovery(snapshot, directory, cancelledId, &error, [&]() { return ++cancellationChecks >= 2; }).isEmpty()
		&& !QFile::exists(QDir(directory).filePath(cancelledId + QStringLiteral(".vstextrecovery"))), "cancellation before commit never publishes a partial checkpoint");
	ok &= expect(removeTextRecovery(directory, id, &error) && !QFile::exists(checkpoint), "discard removes only the selected checkpoint");
	const QString workerDirectory = temp.filePath(QStringLiteral("worker"));
	const QString retiredId = newId(), queuedId = newId();
	{
		CodeRecoveryWriter writer(workerDirectory);
		TextRecoverySnapshot large {decodeTextFile({}), QString(2 * 1024 * 1024, QLatin1Char('x')), QStringLiteral("large")};
		writer.checkpoint(retiredId, 1, large);
		writer.checkpoint(queuedId, 1, TextRecoverySnapshot {decodeTextFile({}), QStringLiteral("older"), QStringLiteral("queued")});
		writer.checkpoint(queuedId, 2, TextRecoverySnapshot {decodeTextFile({}), QStringLiteral("latest"), QStringLiteral("queued")});
		writer.retire(retiredId);
		ok &= expect(waitFor([&]() { return !writer.busy(); }), "background recovery worker must finish");
		const QString queuedPath = QDir(workerDirectory).filePath(queuedId + QStringLiteral(".vstextrecovery"));
		ok &= expect(!QFile::exists(QDir(workerDirectory).filePath(retiredId + QStringLiteral(".vstextrecovery")))
			&& inspectTextRecovery(queuedPath, &restored).isValid() && restored.text == QStringLiteral("latest"), "retiring an in-flight write prevents resurrection and queued revisions coalesce to the newest text");
		ok &= expect(!writer.needsCheckpoint(queuedId, 2), "an unchanged document needs no repeated disk write");
		writer.retire(queuedId);
		ok &= expect(!QFile::exists(queuedPath), "retiring a completed checkpoint removes it");
	}
	const QString many = temp.filePath(QStringLiteral("many"));
	QDir().mkpath(many);
	for (int index = 0; index < 129; ++index) { writeFile(QDir(many).filePath(newId() + QStringLiteral(".vstextrecovery")), "{}"); }
	const auto bounded = listTextRecoveries(many);
	ok &= expect(bounded.limited && bounded.records.size() == 128, "recovery scans are bounded and report omitted records");
	return ok ? 0 : 1;
}
