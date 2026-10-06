#include "core/level_document.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>

#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char* message, const QString& error = {})
{
	if (!value) {
		std::cerr << message << ": " << error.toStdString() << '\n';
	}
	return value;
}
bool put(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return file.readAll();
}
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return EXIT_FAILURE;
	}
	QDir root(temp.path());
	bool ok = true;
	QString error;
	for (const auto& game :
		 {QStringLiteral("quake"), QStringLiteral("quake2"), QStringLiteral("quake3"), QStringLiteral("doom"), QStringLiteral("hexen")}) {
		for (bool room : {false, true}) {
			LevelMapCreateRequest request;
			request.game = game;
			request.starterRoom = room;
			LevelMapDocument document;
			if (!expect(createLevelMap(request, &document, &error), "create every game/preset", error)) {
				return EXIT_FAILURE;
			}
			const bool doom = game == QStringLiteral("doom") || game == QStringLiteral("hexen");
			ok &= expect(document.sourcePath.isEmpty() && document.sourceContentHash.isEmpty() &&
							 document.editState == QStringLiteral("modified") && document.undoStack.isEmpty(),
						 "new documents are unsaved and have no synthetic undo history");
			ok &= expect(doom ? document.doomSectors.size() == (room ? 1 : 0) && document.doomThings.size() == (room ? 1 : 0)
							  : document.brushes.size() == (room ? 6 : 0) && document.entities.size() == (room ? 3 : 1),
						 "starter room geometry and player starts");
			if (game == QStringLiteral("quake2")) {
				ok &= expect(compilerRequestForLevelMap(document, {}).extraArguments.contains(QStringLiteral("-q2bsp")),
							 "new Quake II maps request Quake II BSP output");
			}
			const auto serialized = serializeLevelMap(document);
			ok &= expect(serialized.succeeded(), "serialize new map", serialized.errors.join(','));
			LevelMapDocument reopened;
			ok &= expect(
				loadLevelMapBytes({root.filePath(doom ? QStringLiteral("new.wad") : QStringLiteral("new.map")), {}, document.engineFamily},
								  serialized.bytes, &reopened, &error),
				"round trip new map", error);
			ok &= expect(reopened.format == document.format && reopened.doomFormat == document.doomFormat &&
							 reopened.brushes.size() == document.brushes.size() && reopened.entities.size() == document.entities.size() &&
							 reopened.doomThings.size() == document.doomThings.size(),
						 "format and object counts survive serialization");
			if (game == QStringLiteral("quake2")) {
				LevelMapDocument autodetected;
				auto crlf = serialized.bytes;
				crlf.replace("\n", "\r\n");
				ok &= expect(loadLevelMapBytes({QStringLiteral("q2.map"), {}, {}}, crlf, &autodetected, &error) &&
								 autodetected.format == LevelMapFormat::QuakeMap &&
								 compilerRequestForLevelMap(autodetected, {}).extraArguments.contains(QStringLiteral("-q2bsp")),
							 "Quake II target survives reopening with CRLF and no engine hint");
			}
			const QString checkpoint = writeLevelMapRecovery(document, root.filePath(QStringLiteral("recovery")), uuid(), &error);
			ok &= expect(!checkpoint.isEmpty() && inspectLevelMapRecovery(checkpoint).isValid(), "all map dialects checkpoint", error);
			ok &= expect(restoreLevelMapRecovery(checkpoint, &reopened, &error) && reopened.sourcePath.isEmpty() &&
							 reopened.editState == QStringLiteral("modified") && reopened.doomFormat == document.doomFormat &&
							 reopened.doomGeometryChanged == document.doomGeometryChanged,
						 "all dialects recover unsaved state", error);
		}
	}
	LevelMapDocument document;
	createLevelMap({}, &document, &error);
	const QByteArray initial = serializeLevelMap(document).bytes;
	LevelMapCreateRequest invalid;
	invalid.game = QStringLiteral("unsupported");
	ok &= expect(!createLevelMap(invalid, &document, &error) && serializeLevelMap(document).bytes == initial,
				 "failed creation preserves the previous document");
	invalid.game = QStringLiteral("doom");
	invalid.wallTexture = QStringLiteral("TOO-LONG-NAME");
	ok &= expect(!createLevelMap(invalid, &document, &error), "Doom texture names are validated without truncation");
	ok &= expect(!loadLevelMap({root.filePath(QStringLiteral("absent.map")), {}, {}}, &document, &error) &&
					 serializeLevelMap(document).bytes == initial,
				 "failed open preserves edits");
	ok &= expect(!loadLevelMapBytes({QStringLiteral("bad.wad"), {}, {}}, QByteArray("PWAD"), &document, &error) &&
					 serializeLevelMap(document).bytes == initial,
				 "bad WAD load is transactional");
	LevelDocumentSaveRequest save;
	save.path = root.filePath(QStringLiteral("never-created/dry.map"));
	save.dryRun = true;
	const auto dry = writeLevelDocument(document, save);
	ok &= expect(dry.succeeded() && !dry.written && !QFileInfo::exists(root.filePath(QStringLiteral("never-created"))),
				 "dry run validates without creating output directories");
	LevelMapDocument unknown;
	ok &= expect(!writeLevelDocument(unknown, save).succeeded(), "dry run rejects unsupported documents");
	save.path = root.filePath(QStringLiteral("arena.map"));
	save.dryRun = false;
	auto report = writeLevelDocument(document, save);
	ok &= expect(report.succeeded() && report.written && report.contentHash.size() == 32, "first save succeeds", report.errors.join(','));
	adoptLevelDocumentSave(&document, report);
	ok &= expect(document.sourcePath == save.path && document.editState == QStringLiteral("saved"), "first save adopts the file identity");
	ok &= expect(setLevelMapEntityProperty(&document, 0, QStringLiteral("message"), QStringLiteral("First edit"), &error),
				 "edit before repeat save", error);
	save.overwrite = true;
	report = writeLevelDocument(document, save);
	ok &=
		expect(report.succeeded() && read(report.backupPath) == initial, "overwrite keeps the exact prior bytes", report.errors.join(','));
	adoptLevelDocumentSave(&document, report);
	ok &= expect(document.undoStack.size() == 1 && undoLevelMapEdit(&document, &error) && document.editState == QStringLiteral("modified"),
				 "save preserves undo", error);
	ok &= expect(redoLevelMapEdit(&document, &error) && document.editState == QStringLiteral("saved"), "redo returns to the saved state",
				 error);
	const QByteArray saved = read(save.path);
	QByteArray changed = saved;
	changed.replace("First edit", "Other edit");
	put(save.path, changed);
	ok &=
		expect(!writeLevelDocument(document, save).succeeded() && read(save.path) == changed, "same-size external changes block overwrite");
	save.dryRun = true;
	ok &= expect(!writeLevelDocument(document, save).succeeded(), "dry run catches source conflicts too");
	save.dryRun = false;
	save.path = root.filePath(QStringLiteral("copy.map"));
	save.overwrite = false;
	report = writeLevelDocument(document, save);
	ok &= expect(report.succeeded() && read(save.path) == saved, "Save As preserves the loaded edits despite source changes");
	ok &= expect(!writeLevelDocument(document, save).succeeded(), "existing outputs require explicit overwrite");
	save.overwrite = true;
	save.isCancelled = []() { return true; };
	ok &= expect(!writeLevelDocument(document, save).succeeded() && read(save.path) == saved, "cancelled save preserves output");
	save.isCancelled = {};
	save.path = document.sourcePath;
	QFile::remove(save.path);
	ok &= expect(!writeLevelDocument(document, save).succeeded() && !QFileInfo::exists(save.path),
				 "deleted original is not silently recreated");

	// A WAD save uses its loaded snapshot, including duplicate resource lumps
	// and the other map, even when the source is subsequently removed.
	LevelMapCreateRequest doom;
	doom.game = QStringLiteral("doom");
	LevelMapDocument wad;
	createLevelMap(doom, &wad, &error);
	LevelMapDocument second;
	doom.mapName = QStringLiteral("MAP02");
	createLevelMap(doom, &second, &error);
	LevelMapDocument secondSerialized;
	loadLevelMapBytes({QStringLiteral("second.wad"), {}, {}}, serializeLevelMap(second).bytes, &secondSerialized, &error);
	wad.doomArchiveLumps += secondSerialized.doomArchiveLumps;
	wad.doomArchiveLumps << LevelMapWadSourceLump{QStringLiteral("EXTRA"), QByteArray("one")}
						 << LevelMapWadSourceLump{QStringLiteral("EXTRA"), QByteArray("two")};
	const QString wadPath = root.filePath(QStringLiteral("maps.wad"));
	put(wadPath, serializeLevelMap(wad).bytes);
	ok &= expect(loadLevelMap({wadPath, QStringLiteral("MAP01"), {}}, &wad, &error), "multi-map WAD loads", error);
	const auto archiveBefore = serializeLevelMap(wad).bytes;
	QFile::remove(wadPath);
	ok &= expect(serializeLevelMap(wad).bytes == archiveBefore, "serialization does not reread a disappeared WAD");
	save.path = root.filePath(QStringLiteral("maps-copy.wad"));
	save.overwrite = false;
	ok &= expect(writeLevelDocument(wad, save).succeeded(), "WAD copy saves from the owned archive snapshot");
	LevelMapDocument copied;
	ok &= expect(loadLevelMap({save.path, QStringLiteral("MAP02"), {}}, &copied, &error) &&
					 copied.doomThings.size() == second.doomThings.size(),
				 "other WAD map survives", error);
	ok &= expect(copied.doomArchiveLumps.at(copied.doomArchiveLumps.size() - 2).bytes == QByteArray("one") &&
					 copied.doomArchiveLumps.last().bytes == QByteArray("two"),
				 "duplicate unrelated lumps retain order and content");

	const QString recoveryDirectory = root.filePath(QStringLiteral("saved-recovery")), id = uuid();
	const QString checkpoint = writeLevelMapRecovery(document, recoveryDirectory, id, &error);
	const auto record = inspectLevelMapRecovery(checkpoint);
	ok &= expect(record.isValid() && record.sourceContentHash == document.sourceContentHash && record.sourcePath == document.sourcePath,
				 "checkpoint retains source conflict baseline", error);
	LevelMapDocument restored;
	ok &= expect(restoreLevelMapRecovery(checkpoint, &restored, &error) && restored.editState == QStringLiteral("modified") &&
					 restored.savedUndoDepth == -1,
				 "recovered map requires an explicit save", error);
	const QString originalRecovery = root.filePath(QStringLiteral("original.vsrecovery"));
	put(originalRecovery, read(checkpoint));
	auto damaged = read(checkpoint);
	damaged[damaged.size() - 1] ^= 1;
	put(checkpoint, damaged);
	const auto restoredBefore = serializeLevelMap(restored).bytes;
	ok &= expect(!restoreLevelMapRecovery(checkpoint, &restored, &error) && serializeLevelMap(restored).bytes == restoredBefore,
				 "damaged payload fails integrity check without replacing the document");
	put(checkpoint, QByteArray("VSR1\xff\xff\xff\xff", 8));
	ok &= expect(!inspectLevelMapRecovery(checkpoint).isValid(), "oversized recovery metadata is rejected");
	ok &= expect(writeLevelMapRecovery(document, recoveryDirectory, QStringLiteral("../escape"), &error).isEmpty(),
				 "checkpoint IDs cannot traverse directories");
	ok &= expect(removeLevelMapRecovery(recoveryDirectory, id, &error) && !QFileInfo::exists(checkpoint),
				 "only the selected owned checkpoint is removed", error);

	if (argc > 1) {
		const auto cli = [&](const QStringList& arguments, int expected) {
			QProcess process;
			process.start(QString::fromLocal8Bit(argv[1]),
						  QStringList{QStringLiteral("--cli")} + arguments + QStringList{QStringLiteral("--json")});
			if (!process.waitForFinished(30000) || process.exitCode() != expected) {
				ok &= expect(false, "CLI execution", QString::fromUtf8(process.readAllStandardError()));
				return QJsonObject();
			}
			return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
		};
		const QString cliPath = root.filePath(QStringLiteral("cli-room.map"));
		auto result = cli({QStringLiteral("map"), QStringLiteral("new"), QStringLiteral("--game"), QStringLiteral("quake3"),
						   QStringLiteral("--output"), cliPath, QStringLiteral("--dry-run")},
						  0);
		ok &= expect(result.value(QStringLiteral("save")).toObject().value(QStringLiteral("succeeded")).toBool() &&
						 !QFileInfo::exists(cliPath),
					 "CLI new dry run matches the shared service");
		cli({QStringLiteral("map"), QStringLiteral("new"), QStringLiteral("--game"), QStringLiteral("quake3"), QStringLiteral("--output"),
			 cliPath},
			0);
		ok &= expect(read(cliPath) == initial, "CLI and GUI core generate identical starter rooms");
		cli({QStringLiteral("map"), QStringLiteral("recover"), originalRecovery, QStringLiteral("--output"),
			 root.filePath(QStringLiteral("cli-recovered.map"))},
			0);
		ok &= expect(read(root.filePath(QStringLiteral("cli-recovered.map"))) == saved, "CLI recovery writes the verified snapshot");
		cli({QStringLiteral("map"), QStringLiteral("new"), QStringLiteral("--preset"), QStringLiteral("bad"), QStringLiteral("--output"),
			 cliPath},
			2);
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
