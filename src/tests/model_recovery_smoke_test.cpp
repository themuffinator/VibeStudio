#include "core/model_design.h"
#include "core/model_recovery.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <QtEndian>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
QByteArray read(const QString &path)
{
	QFile f(path);
	return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
bool write(const QString &path, const QByteArray &bytes)
{
	QFile f(path);
	return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("mesh-recovery-XXXXXX")));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	const auto directory = modelRecoveryDirectory();
	bool ok = true;
	QString error;
	ModelDesign design;
	design.parts << ModelDesignPart{};
	ModelDocument document;
	ok &= expect(document.setMesh(buildModelDesignMesh(design), &error), "create source document");
	const auto source = QDir(temporary.path()).filePath(QStringLiteral("source.mesh.json"));
	ok &= expect(document.save(source, false, &error), "save baseline source");
	const auto original = read(source);
	ModelEdit edit;
	edit.selection.surface = 0;
	edit.selection.faces.insert(0);
	edit.translation = {0, 0, 5};
	ok &= expect(document.edit(edit, &error), "prepare unsaved edit");
	ModelRecoverySnapshot snapshot;
	snapshot.mesh = document.mesh();
	snapshot.selection = document.selection();
	snapshot.selection.edges = {modelSurfaceEdges(snapshot.mesh.surfaces[0]).first()};
	snapshot.frame = 0;
	snapshot.title = QStringLiteral("Unsaved prop");
	snapshot.sourcePath = document.path();
	snapshot.sourceSha256 = document.sourceFingerprint();
	const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	const auto path = writeModelRecovery(snapshot, directory, id, &error);
	ok &= expect(!path.isEmpty() && error.isEmpty() && read(source) == original, "checkpoint preserves the original source");
	const auto metadata = inspectModelRecovery(path);
	ok &= expect(metadata.isValid() && metadata.sourcePath == source && metadata.sourceSha256 == document.sourceFingerprint() &&
					 metadata.ownerProcessId == QCoreApplication::applicationPid(),
				 "header preserves source provenance and owner");
	ModelRecoverySnapshot recovered;
	const auto record = inspectModelRecovery(path, &recovered);
	ok &= expect(record.isValid() && editableModelJson(recovered.mesh) == editableModelJson(snapshot.mesh) &&
					 recovered.selection.faces == snapshot.selection.faces && recovered.selection.edges == snapshot.selection.edges,
				 "recovery restores complete mesh and selection");
	ModelDocument draft;
	ok &= expect(draft.restoreDraft(recovered.mesh, recovered.selection, &error) && draft.isModified() && draft.path().isEmpty() &&
					 draft.sourceFingerprint().isEmpty() && !draft.canUndo(),
				 "restoration is an unsaved draft with no overwrite binding");
	const auto restoredPath = QDir(temporary.path()).filePath(QStringLiteral("restored.mesh.json"));
	ok &= expect(draft.save(restoredPath, false, &error) && read(source) == original,
				 "draft saves separately without touching its provenance");
	const auto scan = listModelRecoveries(directory);
	ok &= expect(scan.error.isEmpty() && scan.records.size() == 1 && scan.records[0].id == id, "bounded catalog lists recovery metadata");
	const auto saved = read(path);
	const auto withEdges = [&](const QJsonValue &edges)
	{
		const quint32 headerSize = qFromLittleEndian<quint32>(saved.constData() + 8);
		auto header = QJsonDocument::fromJson(saved.mid(12, headerSize)).object();
		auto payload = QJsonDocument::fromJson(saved.mid(12 + headerSize)).object();
		auto selection = payload.value(QStringLiteral("recoverySelection")).toObject();
		selection.insert(QStringLiteral("edges"), edges);
		payload.insert(QStringLiteral("recoverySelection"), selection);
		const auto body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
		header.insert(QStringLiteral("payloadBytes"), body.size());
		header.insert(QStringLiteral("payloadSha256"),
					  QString::fromLatin1(QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex()));
		const auto meta = QJsonDocument(header).toJson(QJsonDocument::Compact);
		const quint32 size = qToLittleEndian(quint32(meta.size()));
		return saved.left(8) + QByteArray(reinterpret_cast<const char *>(&size), sizeof(size)) + meta + body;
	};
	ok &= expect(write(path, withEdges(QJsonValue::Undefined)) && inspectModelRecovery(path, &recovered).isValid() &&
					 recovered.selection.edges.isEmpty(),
				 "older v1 recovery copies without edge selections remain readable");
	const auto edge = *snapshot.selection.edges.cbegin();
	for (const auto &invalidEdges : {QJsonArray{QJsonArray{edge.second, edge.first}},
									 QJsonArray{QJsonArray{edge.first, edge.second}, QJsonArray{edge.first, edge.second}},
									 QJsonArray{QJsonArray{0, 99999}}, QJsonArray{QJsonArray{0.5, 1}}})
	{
		ok &= expect(write(path, withEdges(invalidEdges)) && !inspectModelRecovery(path, &recovered).isValid() &&
						 recovered.selection.edges.isEmpty(),
					 "recovery rejects reversed, duplicate, nonexistent and fractional edge identifiers without changing its output");
	}
	ok &= expect(write(path, saved) && inspectModelRecovery(path, &recovered).isValid() &&
					 recovered.selection.edges == snapshot.selection.edges,
				 "valid recovery retains stable edge endpoints");
	if (app.arguments().size() > 1)
	{
		const auto executable = app.arguments().at(1);
		const auto cli = [&](QStringList arguments, int expected, QJsonObject *result = nullptr)
		{
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			arguments.prepend(QStringLiteral("--cli"));
			arguments << QStringLiteral("--json");
			process.start(executable, arguments);
			const bool finished = process.waitForStarted(10000) && process.waitForFinished(30000);
			const auto output = process.readAllStandardOutput();
			const auto json = QJsonDocument::fromJson(output);
			if (!finished || process.exitCode() != expected)
			{
				std::cerr << output.constData() << process.readAllStandardError().constData();
			}
			if (result)
			{
				*result = json.object();
			}
			return finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && json.isObject();
		};
		QJsonObject listing;
		ok &= expect(
			cli({QStringLiteral("model"), QStringLiteral("recoveries"), QStringLiteral("--directory"), directory}, 0, &listing) &&
				listing.value(QStringLiteral("records")).toArray().size() == 1 &&
				!listing.value(QStringLiteral("records")).toArray().at(0).toObject().value(QStringLiteral("payloadVerified")).toBool(),
			"CLI catalog distinguishes header inspection from payload verification");
		const auto output = QDir(temporary.path()).filePath(QStringLiteral("cli-restored.mesh.json"));
		ok &= expect(
			cli({QStringLiteral("model"), QStringLiteral("recover"), path, QStringLiteral("--output"), output, QStringLiteral("--dry-run")},
				0) &&
				!QFileInfo::exists(output),
			"CLI restore dry-run validates without writing");
		ok &= expect(cli({QStringLiteral("model"), QStringLiteral("recover"), path, QStringLiteral("--output"), output}, 0),
					 "CLI writes a recovered editable source");
		ModelDocument cliDocument;
		ok &= expect(cliDocument.load(output, &error) && editableModelJson(cliDocument.mesh()) == editableModelJson(snapshot.mesh),
					 "CLI recovery preserves complete authoring geometry");
		ok &= expect(cli({QStringLiteral("model"), QStringLiteral("recover"), path, QStringLiteral("--output"), source}, 1) &&
						 cli({QStringLiteral("model"), QStringLiteral("recover"), path, QStringLiteral("--output"), source,
							  QStringLiteral("--overwrite")},
							 2) &&
						 read(source) == original && read(path) == saved,
					 "CLI recovery cannot overwrite originals or remove its recovery copy");
		auto invalid = saved;
		invalid[invalid.size() - 1] = char(invalid.back() ^ 1);
		ok &= expect(write(path, invalid), "prepare CLI corruption fixture");
		const auto invalidOutput = QDir(temporary.path()).filePath(QStringLiteral("invalid.mesh.json"));
		ok &= expect(cli({QStringLiteral("model"), QStringLiteral("recover"), path, QStringLiteral("--output"), invalidOutput}, 4) &&
						 !QFileInfo::exists(invalidOutput),
					 "CLI refuses a damaged recovery payload");
		ok &= expect(write(path, saved), "restore recovery after CLI corruption check");
	}
	ok &= expect(writeModelRecovery(snapshot, directory, id, &error, [] { return true; }).isEmpty() && read(path) == saved,
				 "cancelled checkpoint leaves prior recovery intact");
	int checkpoints = 0;
	ok &= expect(writeModelRecovery(snapshot, directory, id, &error, [&] { return ++checkpoints >= 3; }).isEmpty() && read(path) == saved,
				 "cancellation while writing preserves the committed copy");
	const auto beforeMesh = editableModelJson(recovered.mesh);
	auto corrupted = saved;
	corrupted[corrupted.size() - 1] = char(corrupted.back() ^ 1);
	ok &= expect(write(path, corrupted), "prepare checksum damage");
	ok &= expect(inspectModelRecovery(path).isValid(), "catalog reads only the bounded header");
	ok &= expect(!inspectModelRecovery(path, &recovered).isValid() && editableModelJson(recovered.mesh) == beforeMesh,
				 "checksum damage fails restoration without mutating its output");
	ok &= expect(write(path, saved.left(12)) && !inspectModelRecovery(path, &recovered).isValid(), "truncated record is rejected");
	ok &= expect(write(path, saved), "restore valid fixture");
	ok &= expect(writeModelRecovery(snapshot, directory, QStringLiteral("../outside"), &error).isEmpty() &&
					 !removeModelRecovery(directory, QStringLiteral("../outside"), &error),
				 "recovery identifiers cannot escape their directory");
	const auto cancelledScan = listModelRecoveries(directory, [] { return true; });
	ok &= expect(cancelledScan.limited && !cancelledScan.error.isEmpty(), "catalog cancellation is explicit");
	ModelRecoverySnapshot invalid = snapshot;
	invalid.selection.edges = {{0, 99999}};
	ok &= expect(writeModelRecovery(invalid, directory, id, &error).isEmpty() && read(path) == saved,
				 "invalid edge selection cannot replace a recovery copy");
	invalid = snapshot;
	invalid.selection.vertices.insert(modelDocumentMaxVertices + 1);
	ok &= expect(writeModelRecovery(invalid, directory, id, &error).isEmpty() && read(path) == saved,
				 "invalid context cannot replace a recovery");
	ok &= expect(removeModelRecovery(directory, id, &error) && !QFileInfo::exists(path) && read(source) == original,
				 "discard removes only the identified recovery file");
	StudioSettings::setOverrideFilePath({});
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
