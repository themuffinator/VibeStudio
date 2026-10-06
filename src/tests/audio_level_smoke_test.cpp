#include "core/audio_level.h"
#include "core/level_dependencies.h"
#include "core/level_document.h"
#include "core/package_draft.h"
#include "core/package_staging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char* message, const QString& detail = {})
{
	if (!value) {
		std::cerr << message << ": " << detail.toStdString() << '\n';
	}
	return value;
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
QString property(const LevelMapEntity& entity, const QString& key)
{
	for (const auto& value : entity.properties) {
		if (value.key == key) {
			return value.value;
		}
	}
	return {};
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	const QString root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT", QDir::currentPath());
	QDir().mkpath(root);
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("audio-level-XXXXXX")));
	if (!temporary.isValid() || argc < 2) {
		return 2;
	}
	bool ok = true;
	QString error;
	// Independent four-sample legacy RIFF fixture: 22050 Hz, mono PCM16.
	const QByteArray wav = QByteArray::fromHex("524946462c00000057415645666d742010000000010001002256000044ac0"
	                                           "0000200100064617461080000000000004000c0ff7f");
	ok &= expect(validateLevelSoundWav(wav, &error), "independent WAV accepted", error);
	for (int offset : {4, 16, 20, 22, 24, 28, 32, 34, 40}) {
		auto bad = wav;
		bad[offset] = char(quint8(bad[offset]) ^ 3);
		ok &= expect(!validateLevelSoundWav(bad), "invalid RIFF/PCM fields rejected");
	}
	ok &= expect(!validateLevelSoundWav(wav.first(wav.size() - 1)), "truncated sample rejected");
	auto junk = wav;
	junk.insert(12, QByteArray::fromHex("4a554e4b020000000000"));
	qToLittleEndian<quint32>(quint32(junk.size() - 8), junk.data() + 4);
	ok &= expect(validateLevelSoundWav(junk), "bounded noncanonical RIFF metadata accepted");
	auto duplicate = wav;
	duplicate.append(wav.mid(36));
	qToLittleEndian<quint32>(quint32(duplicate.size() - 8), duplicate.data() + 4);
	ok &= expect(!validateLevelSoundWav(duplicate), "duplicate data rejected");
	LevelSoundRequest request{{}, QStringLiteral("sound/world/hum.wav"), {64, -32, 96, true}};
	for (const QString& game : {QStringLiteral("quake2"), QStringLiteral("quake3")}) {
		LevelMapCreateRequest create;
		create.game = game;
		create.starterRoom = false;
		LevelMapDocument document;
		ok &= expect(createLevelMap(create, &document, &error), "create map", error);
		const auto original = serializeLevelMap(document).bytes;
		const auto entities = document.entities.size();
		const auto plan = planLevelSound(levelSoundTarget(document), request);
		const QString expected =
		    game == QStringLiteral("quake2") ? QStringLiteral("world/hum.wav") : request.virtualPath;
		ok &= expect(plan.valid() && plan.game == game && plan.soundReference == expected,
		             "game-specific path resolution");
		PackageStagingModel staging;
		ok &= expect(staging.createEmpty(PackageArchiveFormat::Pk3, {}, &error), "create pending package",
		             error);
		int id = -1;
		ok &= expect(stageLevelSound(&staging, &document, request, wav, false, &id, &error),
		             "atomic sound handoff", error);
		ok &= expect(document.entities.size() == entities + 1 && document.entities.last().id == id &&
		                 document.entities.last().className == QStringLiteral("target_speaker") &&
		                 property(document.entities.last(), QStringLiteral("noise")) == expected &&
		                 property(document.entities.last(), QStringLiteral("spawnflags")) ==
		                     QStringLiteral("1") &&
		                 document.entities.last().origin.y == -32,
		             "entity schema and coordinates match independent contract");
		QByteArray delivered;
		PackageStagingArchive preview(staging);
		ok &= expect(preview.readEntryBytes(request.virtualPath, &delivered, &error) && delivered == wav,
		             "planned package sees exact delivered bytes", error);
		const auto dependencies = inspectLevelDependencies(document, preview);
		ok &= expect(dependencies.complete && dependencies.problemCount == 0 &&
		                 dependencies.resolvedPaths.contains(request.virtualPath),
		             "dependency review resolves placed sound");
		const auto edited = serializeLevelMap(document).bytes;
		auto wrongRoot = document;
		for (auto& value : wrongRoot.entities.last().properties) {
			if (value.key == QStringLiteral("noise")) {
				value.value = game == QStringLiteral("quake2") ? request.virtualPath : QStringLiteral("world/hum.wav");
			}
		}
		ok &= expect(inspectLevelDependencies(wrongRoot, preview).missingCount == 1,
		             "dependency inspection does not repair a speaker's wrong game root");
		const auto revision = document.revision;
		const auto stageRevision = staging.revision();
		ok &= expect(!stageLevelSound(&staging, &document, request, wav, false, &id, &error) && id == -1 &&
		                 document.revision == revision && serializeLevelMap(document).bytes == edited &&
		                 staging.revision() == stageRevision,
		             "collision rolls back both histories", error);
		ok &= expect(!stageLevelSound(&staging, &document, request, wav.first(20), true, &id, &error) &&
		                 document.revision == revision && staging.revision() == stageRevision,
		             "malformed delivery changes neither surface");
		ok &= expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == original &&
		                 staging.operations().size() == 1,
		             "map undo is exact and preserves independently staged sound", error);
		ok &= expect(redoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == edited,
		             "map redo restores entity", error);
		ok &= expect(staging.undo() &&
		                 inspectLevelDependencies(document, PackageStagingArchive(staging)).missingCount == 1,
		             "package undo makes missing map reference visible");
		ok &= expect(staging.redo() &&
		                 inspectLevelDependencies(document, PackageStagingArchive(staging)).problemCount == 0,
		             "package redo restores dependency");
		if (game == QStringLiteral("quake2")) {
			ok &= expect(
			    staging.addBytes(QByteArray("wrong root sound"), QStringLiteral("world/hum.wav"), &error),
			    "add root decoy");
			const auto report = inspectLevelDependencies(document, PackageStagingArchive(staging));
			ok &= expect(report.resolvedPaths == QStringList{request.virtualPath},
			             "Quake II sound root cannot be shadowed by an unrelated root file");
		}
		PackageWriteRequest publish;
		auto numbered = document;
		for (auto& value : numbered.entities.last().properties) {
			if (value.key == QStringLiteral("noise")) { value.value = QStringLiteral("123"); }
		}
		const QString numericPath = game == QStringLiteral("quake2") ? QStringLiteral("sound/123.wav") : QStringLiteral("123.wav");
		ok &= expect(staging.addBytes(wav, numericPath, &error), "add numeric speaker filename");
		const auto numeric = inspectLevelDependencies(numbered, PackageStagingArchive(staging));
		ok &= expect(numeric.resolvedPaths.contains(numericPath) && numeric.builtinCount == 0,
		             "speaker numeric values resolve WAV files rather than built-in IDs");
		ok &= expect(staging.undo() && inspectLevelDependencies(numbered, PackageStagingArchive(staging)).missingCount == 1,
		             "missing numeric speaker files are reported");
		publish.destinationPath = QDir(temporary.path()).filePath(game + QStringLiteral(".pk3"));
		const QString mapPath = QDir(temporary.path()).filePath(game + QStringLiteral(".map"));
		ok &= expect(staging.writeArchive(publish).succeeded() && saveLevelMapAs(document, mapPath).succeeded(),
		             "save the two reviewed surfaces");
		PackageArchive published;
		LevelMapDocument savedMap;
		ok &= expect(published.load(publish.destinationPath, &error) && loadLevelMap({mapPath, {}, {}}, &savedMap, &error) &&
		                 levelSoundTarget(savedMap).game == game &&
		                 published.readEntryBytes(request.virtualPath, &delivered, &error) && delivered == wav &&
		                 inspectLevelDependencies(savedMap, published).problemCount == 0,
		             "saved package bytes and sound references remain consistent after reopening", error);
		for (const QString& mode : {QStringLiteral("loop-off"), QStringLiteral("triggered")}) {
			auto named = request;
			named.mode = mode;
			ok &= expect(!planLevelSound(levelSoundTarget(document), named).valid(),
			             "inactive sound requires target name");
			named.targetName = QStringLiteral("alarm_1");
			ok &= expect(
			    placeLevelSound(&document, named, wav, &id, &error) &&
			        property(document.entities.last(), QStringLiteral("targetname")) == named.targetName &&
			        property(document.entities.last(), QStringLiteral("spawnflags")) ==
			            (mode == QStringLiteral("loop-off") ? QStringLiteral("2") : QStringLiteral("0")),
			    "inactive speaker produces reviewed target and flags", error);
		}
	}
	const LevelSoundTarget q3{LevelMapFormat::Quake3Map, QStringLiteral("quake3")};
	LevelMapDocument header;
	header.format = LevelMapFormat::QuakeMap;
	header.originalText = QString::fromLatin1(kQuake2MapTargetHeader).replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
	ok &= expect(levelSoundTarget(header).game == QStringLiteral("quake2"), "CRLF target comment is recognized");
	header.originalText = QStringLiteral("// VibeStudio target: quake2") + QString(4096, QLatin1Char(' ')) + QStringLiteral("other\n");
	ok &= expect(levelSoundTarget(header).game.isEmpty(), "bounded header recognition cannot accept a truncated marker");
	for (const QString& path :
	     {QStringLiteral("sound/../hum.wav"), QStringLiteral("sound/./hum.wav"),
	      QStringLiteral("sound/hum.WAV"), QStringLiteral("sound/bad\".wav"),
	      QStringLiteral("sound/bad\nhum.wav"), QStringLiteral("sound/hum.wav\n"), QStringLiteral("music/hum.wav"),
	      QStringLiteral("sound/é.wav"),
	      QStringLiteral("sound/") + QString(54, QLatin1Char('a')) + QStringLiteral(".wav")}) {
		auto invalid = request;
		invalid.virtualPath = path;
		ok &= expect(!planLevelSound(q3, invalid).valid(), "unsafe or oversized engine path rejected", path);
	}
	auto finite = request;
	finite.targetName = QStringLiteral("alarm\n");
	ok &= expect(!planLevelSound(q3, finite).valid(), "trailing newline in target rejected during review");
	finite.targetName.clear();
	finite.origin.x = std::numeric_limits<double>::quiet_NaN();
	ok &= expect(!planLevelSound(q3, finite).valid(), "NaN coordinates rejected");
	auto mismatch = request;
	mismatch.game = QStringLiteral("quake2");
	ok &= expect(!planLevelSound(q3, mismatch).valid(), "mismatched map game rejected");
	ok &= expect(!planLevelSound({LevelMapFormat::QuakeMap, {}}, request).valid(),
	             "ambiguous legacy map never assumes Quake II");
	ok &= expect(planLevelSound({LevelMapFormat::QuakeMap, {}}, mismatch).valid(),
	             "explicit Quake II choice handles an unmarked legacy map");
	ok &= expect(!planLevelSound({LevelMapFormat::DoomWad, {}}, request).valid(),
	             "Doom uses a separate game workflow");
	const QDir dir(temporary.path());
	const QString assets = dir.filePath(QStringLiteral("assets"));
	QDir().mkpath(QDir(assets).filePath(QStringLiteral("sound/world")));
	const QString asset = QDir(assets).filePath(request.virtualPath);
	ok &= expect(write(asset, wav), "write CLI sound fixture");
	LevelMapDocument source;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	ok &= expect(createLevelMap(create, &source, &error), "create CLI map");
	const QString input = dir.filePath(QStringLiteral("input.map")),
	              output = dir.filePath(QStringLiteral("placed.map"));
	const auto original = serializeLevelMap(source).bytes;
	ok &= expect(write(input, original), "write CLI map fixture");
	QJsonObject result;
	const auto run = [&](const QStringList& arguments, int expected) {
		QProcess process;
		process.start(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(),
		              QStringList{QStringLiteral("--cli"), QStringLiteral("--json"),
		                          QStringLiteral("--settings-file"),
		                          dir.filePath(QStringLiteral("settings.ini"))} +
		                  arguments);
		if (!process.waitForFinished(20000)) {
			process.kill();
			process.waitForFinished();
			return expect(false, "CLI timeout");
		}
		const auto out = process.readAllStandardOutput();
		result = QJsonDocument::fromJson(out).object();
		return expect(process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected &&
		                  !result.isEmpty(),
		              "CLI result/exit code", QString::fromUtf8(out + process.readAllStandardError()));
	};
	const QStringList arguments{QStringLiteral("map"),
	                            QStringLiteral("place-sound"),
	                            input,
	                            QStringLiteral("--package"),
	                            assets,
	                            QStringLiteral("--sound"),
	                            request.virtualPath,
	                            QStringLiteral("--origin"),
	                            QStringLiteral("64,-32,96"),
	                            QStringLiteral("--output"),
	                            output};
	ok &= run(arguments + QStringList{QStringLiteral("--dry-run")}, 0);
	ok &= expect(
	    !QFileInfo::exists(output) &&
	        result.value(QStringLiteral("sound")).toObject().value(QStringLiteral("reference")).toString() ==
	            request.virtualPath,
	    "dry run reports resolved entity and creates no output");
	const auto planned = result.value(QStringLiteral("sound"));
	ok &= run(arguments + QStringList{QStringLiteral("--dry-run")}, 0);
	ok &= expect(result.value(QStringLiteral("sound")) == planned, "sound dry run is deterministic");
	ok &= run(arguments, 0);
	LevelMapDocument reopened;
	ok &= expect(loadLevelMap({output, {}, {}}, &reopened, &error) &&
	                 reopened.entities.size() == source.entities.size() + 1 &&
	                 property(reopened.entities.last(), QStringLiteral("noise")) == request.virtualPath &&
	                 read(input) == original && read(asset) == wav,
	             "CLI saved speaker round trip preserves inputs", error);
	ok &= run(arguments + QStringList{QStringLiteral("--unexpected")}, 2);
	ok &= run(arguments + QStringList{QStringLiteral("--sound"), request.virtualPath}, 2);
	ok &= run(arguments + QStringList{QStringLiteral("--game")}, 2);
	ok &= run(arguments + QStringList{QStringLiteral("--mode"), QStringLiteral("triggered"),
	                                  QStringLiteral("--dry-run")},
	          4);
	for (const QString& destination : {input, asset}) {
		auto changed = arguments;
		changed.last() = destination;
		// Non-map asset extensions are rejected at the command boundary.
		ok &= run(changed + QStringList{QStringLiteral("--overwrite")}, destination == input ? 4 : 2);
	}
	auto missing = arguments;
	missing[missing.indexOf(QStringLiteral("--sound")) + 1] = QStringLiteral("sound/world/missing.wav");
	ok &= run(missing + QStringList{QStringLiteral("--dry-run")}, 4);
	auto wrongCase = arguments;
	wrongCase[wrongCase.indexOf(QStringLiteral("--sound")) + 1] = QStringLiteral("sound/world/HUM.wav");
	ok &= run(wrongCase + QStringList{QStringLiteral("--dry-run")}, 4);
	// A native draft's inline sound is eligible before package publication.
	PackageStagingModel draft;
	const QString draftPath = dir.filePath(QStringLiteral("sounds.vibepackage"));
	ok &= expect(draft.createEmpty(PackageArchiveFormat::Pk3, {}, &error) &&
	                 draft.addBytes(wav, request.virtualPath, &error) && PackageDraft::save(draftPath, &draft, false, &error),
	             "save native package draft", error);
	auto native = arguments;
	native[native.indexOf(QStringLiteral("--package")) + 1] = draftPath;
	native.last() = dir.filePath(QStringLiteral("from-draft.map"));
	ok &= run(native, 0);
	ok &= expect(read(native.last()).contains("target_speaker"), "CLI places unpublished native draft sound");
	const QStringList dependencies{QStringLiteral("map"), QStringLiteral("dependencies"), native.last(),
	                               QStringLiteral("--package"), draftPath};
	const QString draftManifest = QDir(draftPath).filePath(QStringLiteral("document.json"));
	const QByteArray savedDraft = read(draftManifest);
	ok &= run(dependencies, 0);
	const auto dependencyReport = result.value(QStringLiteral("dependencies")).toObject();
	ok &= expect(dependencyReport.value(QStringLiteral("files")).toArray() ==
	                 QJsonArray{request.virtualPath} && dependencyReport.value(QStringLiteral("bytes")).toDouble() == wav.size() &&
	                 dependencyReport.value(QStringLiteral("package")).toString() == QFileInfo(draftPath).absoluteFilePath() &&
	                 read(draftManifest) == savedDraft,
	             "CLI dependency review identifies the draft and resolves inline audio without changing it");
	ok &= expect(draft.deleteEntry(request.virtualPath, &error) && PackageDraft::save(draftPath, &draft, true, &error),
	             "save a draft with the placed sound deleted", error);
	ok &= run(dependencies, 4);
	ok &= expect(result.value(QStringLiteral("dependencies")).toObject().value(QStringLiteral("missing")).toInt() == 1,
	             "dependency review sees staged deletion rather than storage objects");
	ok &= expect(draft.undo() && PackageDraft::save(draftPath, &draft, true, &error), "restore staged sound", error);
	ok &= run(dependencies, 0);
	const QByteArray restoredDraft = read(draftManifest);
	ok &= expect(write(draftManifest, QByteArray("{}")), "write a corrupt draft fixture");
	ok &= run(dependencies, 1);
	ok &= expect(!result.contains(QStringLiteral("dependencies")) && read(draftManifest) == QByteArray("{}"),
	             "corrupt draft review fails to load instead of scanning its storage directory");
	ok &= expect(write(draftManifest, restoredDraft), "restore the draft fixture manifest");
	auto triggered = arguments;
	triggered.last() = dir.filePath(QStringLiteral("triggered.map"));
	ok &= run(triggered + QStringList{QStringLiteral("--mode"), QStringLiteral("triggered"), QStringLiteral("--targetname"), QStringLiteral("alarm")}, 0);
	ok &= expect(result.value(QStringLiteral("sound")).toObject().value(QStringLiteral("properties")).toObject().value(QStringLiteral("targetname")).toString() == QStringLiteral("alarm"), "CLI reports target wiring");
	ok &= expect(read(input) == original && read(asset) == wav, "failed CLI requests preserve sources");
	return ok ? 0 : 1;
}
