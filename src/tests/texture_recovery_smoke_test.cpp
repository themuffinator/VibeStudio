#include "core/texture_recovery.h"
#include "app/texture_recovery.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QtEndian>

#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }
bool put(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool settled(TextureRecoveryWriter& writer)
{
	QEventLoop loop; QTimer poll, timeout; timeout.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (!writer.busy()) { loop.quit(); } });
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(5); timeout.start(15000); if (writer.busy()) { loop.exec(); } return !writer.busy();
}
QByteArray revise(const QByteArray& input, const std::function<void(QJsonObject&)>& change)
{
	const auto length = qFromLittleEndian<quint32>(input.constData() + 12);
	auto metadata = QJsonDocument::fromJson(input.mid(24, length)).object(); change(metadata);
	const auto json = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
	QByteArray output = input.first(24); qToLittleEndian<quint32>(quint32(json.size()), output.data() + 12);
	output += json; output += input.mid(24 + length, input.size() - 56 - length);
	output += QCryptographicHash::hash(output, QCryptographicHash::Sha256); return output;
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary;
	if (!temporary.isValid()) { return EXIT_FAILURE; }
	QDir root(temporary.path()); bool ok = true; QString error;
	const auto directory = root.filePath(QStringLiteral("recovery")); const auto id = uuid();
	TextureDocument document; document.create({8, 4}, QColor(12, 34, 56, 78)); document.addLayer(QStringLiteral("Detail"));
	document.paintStroke({{2, 1}}, QColor(100, 110, 120, 130), 1);
	IdTechPaletteResolution palette; palette.palette = generatedIdTechPalette(QStringLiteral("doom"));
	const QJsonObject metadata{{QStringLiteral("palette"), texturePaletteMetadata(palette)}, {QStringLiteral("packageTexturePath"), QStringLiteral("textures/test.png")}};
	const auto sourcePath = root.filePath(QStringLiteral("original.vtexture"));
	TextureProjectSaveRequest sourceRequest; sourceRequest.path = sourcePath;
	const auto saved = writeTextureProject(document, sourceRequest, metadata);
	if (!saved.succeeded) { std::cerr << saved.error.toStdString(); return EXIT_FAILURE; }
	const auto sourceBytes = read(sourcePath);
	TextureRecoverySnapshot snapshot{document, metadata, saved.identity, QStringLiteral("Layered draft")};
	const auto path = writeTextureRecovery(snapshot, directory, id, &error);
	ok &= expect(!path.isEmpty(), "checkpoint writes atomically to its document UUID");
	const auto bytes = read(path); const auto info = inspectTextureRecovery(path);
	ok &= expect(info.isValid() && info.id == id && info.size == document.size() && info.layerCount == 2 && info.sourcePath == sourcePath && info.sourceSha256 == saved.identity.sha256, "checkpoint inspection reports verified envelope metadata");
	TextureDocument restored; restored.create({1, 1}, Qt::yellow); QJsonObject restoredMetadata;
	ok &= expect(restoreTextureRecovery(path, &restored, &restoredMetadata, nullptr, &error, info.recordSha256) && restored.isDirty() && !restored.canUndo() &&
		encodeTextureProject(restored, restoredMetadata) == encodeTextureProject(document, metadata) && read(sourcePath) == sourceBytes, "restoration exactly preserves layers and metadata as a fresh unsaved draft without rewriting source");
	const auto snapshotWithoutHistory = document.storageSnapshot();
	ok &= expect(!snapshotWithoutHistory.canUndo() && snapshotWithoutHistory.historyBytes() == 0 && snapshotWithoutHistory.revision() == document.revision() &&
		encodeTextureProject(snapshotWithoutHistory) == encodeTextureProject(document), "storage snapshots release history without changing current layer contents");
	const auto preserved = encodeTextureProject(restored); const auto preservedMetadata = restoredMetadata;
	ok &= expect(!restoreTextureRecovery(path, &restored, &restoredMetadata, nullptr, &error, QByteArray(32, 'x')) && encodeTextureProject(restored) == preserved && restoredMetadata == preservedMetadata, "stale selection fingerprints cannot replace an open document");
	ok &= expect(!restoreTextureRecovery(path, &restored, nullptr, nullptr, &error, {}, [](qint64, qint64) { return false; }) && encodeTextureProject(restored) == preserved, "cancelled restore is atomic");
	ok &= expect(!restoreTextureRecovery(path, &restored, nullptr, nullptr, &error, {}, [](qint64 done, qint64 total) { return done * 4 < total * 3; }) && encodeTextureProject(restored) == preserved, "restore can cancel during layer decoding after envelope verification");
	ok &= expect(writeTextureRecovery(snapshot, root.filePath(QStringLiteral("cancelled")), uuid(), &error, [](qint64, qint64) { return false; }).isEmpty() && !QFileInfo::exists(root.filePath(QStringLiteral("cancelled"))), "early cancellation creates no recovery folder");
	snapshot.document.paintStroke({{0, 0}}, Qt::blue, 1);
	ok &= expect(writeTextureRecovery(snapshot, directory, id, &error, [](qint64 done, qint64 total) { return done < total; }).isEmpty() && read(path) == bytes, "late cancelled replacement retains the previous checkpoint");
	ok &= expect(textureRecoveryPath(directory, QStringLiteral("../escape")).isEmpty() && !removeTextureRecovery(directory, QStringLiteral("../original.vtexture"), &error) && read(sourcePath) == sourceBytes, "invalid IDs cannot traverse or remove source files");
	const auto corruptPath = root.filePath(QStringLiteral("corrupt.vtrecovery")); auto corrupt = bytes; corrupt[corrupt.size() - 40] ^= 1; put(corruptPath, corrupt);
	ok &= expect(!inspectTextureRecovery(corruptPath).isValid() && !restoreTextureRecovery(corruptPath, &restored, &restoredMetadata, nullptr, &error) && encodeTextureProject(restored) == preserved, "corruption fails checksum verification without partially replacing the document");
	for (const auto& replacement : QVector<QByteArray>{
		bytes.first(20),
		revise(bytes, [](QJsonObject& object) { object.insert(QStringLiteral("id"), QStringLiteral("../../source")); }),
		revise(bytes, [](QJsonObject& object) { object.insert(QStringLiteral("width"), 999999); }),
		revise(bytes, [](QJsonObject& object) { object.insert(QStringLiteral("revision"), QStringLiteral("-1")); }),
		revise(bytes, [](QJsonObject& object) { object.insert(QStringLiteral("sourceSha256"), QStringLiteral("bad")); })}) {
		put(corruptPath, replacement);
		ok &= expect(!restoreTextureRecovery(corruptPath, &restored, nullptr, nullptr, &error) && encodeTextureProject(restored) == preserved, "malformed bounds or metadata are rejected even with an authentic envelope checksum");
	}
	auto unsupported = bytes; qToLittleEndian<quint32>(999, unsupported.data() + 8); put(corruptPath, unsupported);
	ok &= expect(!inspectTextureRecovery(corruptPath).isValid(), "unsupported recovery versions fail safely");
	put(corruptPath, revise(bytes, [](QJsonObject& object) { object.insert(QStringLiteral("width"), 4); }));
	ok &= expect(!restoreTextureRecovery(corruptPath, &restored, nullptr, nullptr, &error) && encodeTextureProject(restored) == preserved, "summary dimensions must agree with the embedded document");
	put(corruptPath, revise(bytes, [](QJsonObject& object) { object.insert(QStringLiteral("sourcePath"), QStringLiteral("//untrusted.invalid/never/follow/this.vtexture")); }));
	ok &= expect(restoreTextureRecovery(corruptPath, &restored, nullptr, nullptr, &error) && read(sourcePath) == sourceBytes, "recorded source paths are informational and never resolved or read");
	const auto second = uuid(); writeTextureRecovery(snapshot, directory, second);
	put(QDir(directory).filePath(QStringLiteral("bad.vtrecovery")), "broken");
	const auto listing = listTextureRecoveries(directory);
	int invalid = 0; for (const auto& record : listing.records) { invalid += !record.isValid(); }
	ok &= expect(listing.error.isEmpty() && !listing.truncated && listing.records.size() == 3 && invalid == 1, "recovery listing exposes damaged entries alongside valid drafts");
	ok &= expect(listTextureRecoveries(directory, [](qint64, qint64) { return false; }).cancelled, "scanning checkpoints is cancellable");
	ok &= expect(removeTextureRecovery(directory, second, &error) && !QFileInfo::exists(textureRecoveryPath(directory, second)) && QFileInfo::exists(path), "retirement removes only the named checkpoint");
	{
		TextureRecoveryWriter writer(directory); const auto workerId = uuid(); int finished = 0; QString workerError;
		writer.finished = [&](const QString&, const QString&, const QString& problem) { ++finished; workerError = problem; };
		writer.checkpoint(workerId, snapshot);
		snapshot.document.paintStroke({{3, 2}}, Qt::green, 1); writer.checkpoint(workerId, snapshot);
		snapshot.metadata.insert(QStringLiteral("latest"), true); writer.checkpoint(workerId, snapshot);
		ok &= expect(settled(writer) && workerError.isEmpty() && finished <= 2 && restoreTextureRecovery(textureRecoveryPath(directory, workerId), &restored, &restoredMetadata) && restoredMetadata.value(QStringLiteral("latest")).toBool() && restored.activeLayer()->pixels.pixelColor(3, 2) == QColor(Qt::green), "background writer coalesces pending changes and saves the latest pixels plus metadata");
		ok &= expect(!writer.checkpoint(workerId, snapshot) && !writer.busy(), "unchanged snapshots do not trigger repeated disk writes");
		snapshot.document.paintStroke({{4, 2}}, Qt::cyan, 1); writer.checkpoint(workerId, snapshot); writer.retire(workerId);
		ok &= expect(settled(writer) && !QFileInfo::exists(textureRecoveryPath(directory, workerId)), "retired in-flight writes cannot resurrect discarded drafts");
	}
	const auto destroyedId = uuid();
	{
		TextureRecoveryWriter writer(directory); writer.checkpoint(destroyedId, snapshot);
		snapshot.metadata.insert(QStringLiteral("destruction"), true); writer.checkpoint(destroyedId, snapshot);
	}
	ok &= expect(restoreTextureRecovery(textureRecoveryPath(directory, destroyedId), &restored, &restoredMetadata) && restoredMetadata.value(QStringLiteral("destruction")).toBool(), "unexpected owner destruction flushes its newest accepted pending snapshot");
	if (argc > 1) {
		const QString binary = QString::fromLocal8Bit(argv[1]); QJsonObject outputJson;
		const auto cli = [&](const QStringList& arguments, int expected) {
			QProcess process; process.start(binary, QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"), root.filePath(QStringLiteral("settings.ini")), QStringLiteral("--json")} + arguments);
			if (!process.waitForFinished(30000)) { process.kill(); process.waitForFinished(); return false; }
			const auto result = QJsonDocument::fromJson(process.readAllStandardOutput()); outputJson = result.object();
			if (process.exitCode() != expected) { std::cerr << result.toJson().constData(); }
			return process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && result.isObject();
		};
		ok &= expect(cli({QStringLiteral("texture"), QStringLiteral("recoveries"), directory}, 0) && !outputJson.value(QStringLiteral("checkpoints")).toArray().isEmpty(), "CLI recovery listing uses the shared bounded scanner");
		const auto output = root.filePath(QStringLiteral("restored.vtexture"));
		const QStringList recover{QStringLiteral("texture"), QStringLiteral("recover"), path, QStringLiteral("--output"), output};
		ok &= expect(cli(recover + QStringList{QStringLiteral("--dry-run")}, 0) && !outputJson.value(QStringLiteral("written")).toBool() && !QFileInfo::exists(output), "CLI restore dry-run creates no output");
		ok &= expect(cli(recover, 0) && readTextureProject(output, &restored, nullptr, &restoredMetadata) && encodeTextureProject(restored, restoredMetadata) == encodeTextureProject(document, metadata), "CLI restores editable native layers and metadata");
		const auto recoveredBytes = read(output);
		ok &= expect(cli(recover, 1) && read(output) == recoveredBytes, "CLI recovery refuses implicit overwrites");
		ok &= expect(cli(recover + QStringList{QStringLiteral("--overwrite")}, 2) && read(output) == recoveredBytes, "CLI recovery always requires a new destination");
		put(corruptPath, "broken");
		ok &= expect(cli({QStringLiteral("texture"), QStringLiteral("recover"), corruptPath, QStringLiteral("--output"), root.filePath(QStringLiteral("bad.vtexture"))}, 4), "CLI damaged recovery returns structured validation failure");
		ok &= expect(read(path) == bytes && read(sourcePath) == sourceBytes, "CLI restoration leaves the checkpoint and recorded source untouched");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
