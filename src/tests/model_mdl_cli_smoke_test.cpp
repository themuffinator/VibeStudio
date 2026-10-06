#include "core/model_design.h"
#include "core/model_document.h"
#include "core/package_archive.h"
#include "core/package_draft.h"
#include "core/package_staging.h"
#include "tests/model_mdl_test_helpers.h"
#include "tests/model_skin_source_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

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
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	if (argc < 2)
	{
		return EXIT_FAILURE;
	}
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("mesh-mdl-cli-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	bool ok = true;
	QJsonObject output;
	const auto run = [&](QStringList args, int code = 0)
	{
		QProcess process;
		process.setWorkingDirectory(temporary.path());
		process.start(QString::fromLocal8Bit(argv[1]),
					  QStringList{"--cli", "--settings-file", path("settings.ini")} + args + QStringList{"--json"});
		if (!process.waitForFinished(30000))
		{
			process.kill();
			process.waitForFinished();
			return false;
		}
		const auto bytes = process.readAllStandardOutput();
		output = QJsonDocument::fromJson(bytes).object();
		if (process.exitCode() != code || output.isEmpty())
		{
			std::cerr << args.join(' ').toStdString() << " exit=" << process.exitCode() << '\n'
					  << bytes.toStdString() << process.readAllStandardError().toStdString() << '\n';
			return false;
		}
		return process.exitStatus() == QProcess::NormalExit;
	};
	const auto fixture = tests::groupedMdlFixture();
	ok &= expect(write(path("fixture.mdl"), fixture.bytes), "write synthetic native fixture");
	ok &= expect(run({"model", "import", path("fixture.mdl"), "--output", path("source.mesh.json")}),
				 "CLI imports every native group and skin");
	ok &= expect(run({"model", "mdl", path("source.mesh.json")}) && output.value("enabled").toBool() &&
					 output.value("skins").toArray().size() == 2 &&
					 output.value("settings").toObject().value("frameGroups").toArray().size() == 2,
				 "CLI inspection exposes native groups without embedding raw image payloads");
	QByteArray beforeSampling, afterSampling;
	QString sampleError;
	ok &= expect(readModelFile(path("source.mesh.json"), &beforeSampling, &sampleError), "read source before timing inspection");
	ok &= expect(run({"model", "mdl", path("source.mesh.json"), "--time", "0.23", "--native-frame", "0", "--skin", "0"}) &&
					 output.value("sample").toObject().value("pose").toInt() == 1 &&
					 output.value("sample").toObject().value("skinMember").toInt() == 1 && !output.value("written").toBool(),
				 "CLI stored timing samples independent pose and skin schedules without writing");
	ok &= expect(run({"model", "mdl", path("source.mesh.json"), "--time", "0.23", "--timing", "glquake", "--sync-phase", "0.12"}) &&
					 output.value("sample").toObject().value("pose").toInt() == 0 &&
					 output.value("sample").toObject().value("skinMember").toInt() == 0,
				 "CLI original GLQuake timing ignores sync phase and follows its four skin slots");
	ok &= expect(run({"model", "mdl", path("source.mesh.json"), "--time", "0.05", "--sync-phase", "0.06"}) &&
					 output.value("sample").toObject().value("pose").toInt() == 1,
				 "CLI software random sync uses explicit entity phase");
	ok &= expect(run({"model", "mdl", path("source.mesh.json"), "--time", "-1"}, 4) &&
					 run({"model", "mdl", path("source.mesh.json"), "--time", "nan"}, 2) &&
					 run({"model", "mdl", path("source.mesh.json"), "--time", "0", "--timing", "invalid"}, 2) &&
					 run({"model", "mdl", path("source.mesh.json"), "--native-frame", "1"}, 2) &&
					 run({"model", "mdl", path("source.mesh.json"), "--time", "0", "--output", path("unexpected.mesh.json")}, 2) &&
					 !QFileInfo::exists(path("unexpected.mesh.json")),
				 "invalid or mixed timing arguments cannot write outputs");
	ok &= expect(readModelFile(path("source.mesh.json"), &afterSampling, &sampleError) && beforeSampling == afterSampling,
				 "CLI timing queries leave source bytes unchanged");
	ok &= expect(run({"model", "mdl", path("source.mesh.json"), "--operation", "header", "--flags", "0xffffffff", "--eye=-1,2,3", "--sync",
					  "0", "--output", path("edited.mesh.json"), "--dry-run"}) &&
					 !QFileInfo::exists(path("edited.mesh.json")) &&
					 output.value("settings").toObject().value("flags").toDouble() == 4294967295.,
				 "CLI dry run reports proposed unsigned flags without writing");
	ok &= expect(run({"model", "mdl", path("source.mesh.json"), "--operation", "group", "--first-frame", "0", "--last-frame", "2",
					  "--duration", "0.2", "--output", path("edited.mesh.json")}),
				 "CLI native grouping writes a normal source document");
	ok &= expect(run({"model", "mdl", path("edited.mesh.json"), "--operation", "skin-duration", "--skin", "0", "--member", "1",
					  "--duration", "0.4", "--output", path("edited.mesh.json"), "--overwrite"}),
				 "CLI indexed member timing supports protected in-place source saves");
	ok &= expect(run({"model", "build", path("edited.mesh.json"), "--output", path("out.mdl")}) &&
					 output.value("storedVertices").toInt() == 4 && output.value("nativeFrames").toInt() == 1 &&
					 output.value("exportNotes").toArray().size() >= 2,
				 "CLI MDL build reports quantization and native format notes");
	QByteArray bytes;
	QString error;
	ok &= expect(readModelFile(path("out.mdl"), &bytes, &error) && decodeModelMesh("out.mdl", bytes).mdl.frameGroups.size() == 1,
				 "CLI binary reload retains grouping");
	const auto saved = bytes;
	ok &= expect(
		run({"model", "mdl", path("edited.mesh.json"), "--operation", "header", "--flags", "4294967296", "--output", path("bad.mesh.json")},
			2) &&
			!QFileInfo::exists(path("bad.mesh.json")),
		"CLI refuses flag overflow before writing");
	ok &= expect(run({"model", "mdl", path("edited.mesh.json"), "--operation", "remove-skin", "--skin", "0", "--member", "0", "--output",
					  path("bad.mesh.json")},
					 2),
				 "CLI refuses inapplicable options");
	ok &= expect(run({"model", "mdl", path("edited.mesh.json"), "--operation", "pose-duration", "--frame", "99", "--duration", "0.1",
					  "--output", path("bad.mesh.json")},
					 4),
				 "invalid native pose index uses validation exit code");
	ok &= expect(run({"model", "mdl", path("edited.mesh.json"), "--operation", "header", "--flags", "1", "--flags", "2", "--output",
					  path("bad.mesh.json")},
					 2),
				 "CLI refuses duplicate options");
	const auto directory = path("package");
	QDir().mkpath(directory);
	PackageArchive archive;
	PackageStagingModel staging;
	ok &= expect(archive.load(directory, &error), "open staging directory");
	ok &= expect(staging.loadBaseArchive(archive, &error), "prepare staging document");
	ok &= expect(stageModelExport(saved, "progs/fixture.mdl", &staging, nullptr, {}, false, &error) && staging.summary().canSave,
				 "shared package handoff accepts MDL exports");
	const auto count = staging.summary().addedCount;
	LevelMapDocument level;
	ok &= expect(!stageModelExport(saved, "progs/other.mdl", &staging, &level, {}, false, &error) && staging.summary().addedCount == count,
				 "unsupported level placement leaves package staging intact with an actionable diagnostic");
	const auto skin = tests::skinLump(63);
	ok &= expect(staging.addBytes(skin, "textures/skin.lmp", &error) &&
					 PackageDraft::save(path("skins.vibepackage"), &staging, false, &error),
				 "persist generated staged skin in a portable package draft");
	const auto draftManifest = QDir(path("skins.vibepackage")).filePath("document.json");
	QByteArray manifestBefore;
	ok &= expect(readModelFile(draftManifest, &manifestBefore, &error), "snapshot draft before import");
	const QStringList skinArgs{"model", "mdl", path("source.mesh.json"), "--operation", "add-skin"};
	ok &= expect(run(skinArgs + QStringList{"--package", path("skins.vibepackage"), "--entry", "textures/skin.lmp", "--output",
											path("draft-skin.mesh.json"), "--dry-run"}) &&
					 !QFileInfo::exists(path("draft-skin.mesh.json")) &&
					 output.value("skinSource").toObject().value("path") == "textures/skin.lmp" &&
					 output.value("skinSource").toObject().value("paletteKind") == "model",
				 "draft skin dry run exposes source receipt without writing");
	ok &= expect(run(skinArgs + QStringList{"--package", path("skins.vibepackage"), "--entry", "textures/skin.lmp", "--output",
											path("draft-skin.mesh.json")}),
				 "CLI imports a staged generated skin directly from a draft");
	ModelDocument imported;
	ok &= expect(imported.load(path("draft-skin.mesh.json"), &error) && imported.mesh().embeddedSkins.size() == 3 &&
					 imported.mesh().embeddedSkins.last().indexedFrames[0] == skin.mid(8),
				 "saved source embeds exact staged pixels");
	QByteArray manifestAfter;
	const auto protectedDraftOutput = QDir(path("skins.vibepackage")).filePath("protected.mesh.json");
	ok &= expect(run(skinArgs + QStringList{"--package", path("skins.vibepackage"), "--entry", "textures/skin.lmp", "--output",
											protectedDraftOutput, "--overwrite"},
					 2) &&
					 !QFileInfo::exists(protectedDraftOutput),
				 "CLI refuses writing model outputs inside its source draft");
	ok &= expect(readModelFile(draftManifest, &manifestAfter, &error) && manifestAfter == manifestBefore,
				 "skin handoff leaves the package draft unchanged");
	const auto wad = tests::repeatedSkinWad(tests::skinLump(19), tests::skinLump(99));
	ok &= expect(write(path("skins.wad"), wad), "write repeated WAD names fixture");
	ok &= expect(
		run(skinArgs + QStringList{"--package", path("skins.wad"), "--entry", "skin.lmp", "--output", path("ambiguous.mesh.json")}, 4) &&
			!QFileInfo::exists(path("ambiguous.mesh.json")),
		"CLI refuses ambiguous package path selectors");
	ok &= expect(
		run(skinArgs + QStringList{"--package", path("skins.wad"), "--entry-index", "1", "--output", path("exact-skin.mesh.json")}) &&
			output.value("skinSource").toObject().value("entryIndex").toInt(-1) == 1 &&
			imported.load(path("exact-skin.mesh.json"), &error) &&
			imported.mesh().embeddedSkins.last().indexedFrames[0] == tests::skinLump(99).mid(8),
		"CLI exact index imports the second physical WAD occurrence");
	const QStringList common{"--package", path("skins.wad"), "--output", path("invalid-skin.mesh.json")};
	for (const auto &bad : QList<QStringList>{{},
											  {"--entry-index", "-1"},
											  {"--entry-index", "0", "--entry", "skin.lmp"},
											  {"--entry-index", "0", "--image", path("skin.lmp")},
											  {"--entry-index", "0", "--palette", "unknown"}})
	{
		ok &= expect(run(skinArgs + common + bad, 2) && !QFileInfo::exists(path("invalid-skin.mesh.json")),
					 "invalid or conflicting skin selectors cannot create output");
	}
	ok &= expect(write(QDir(directory).filePath("skin.lmp"), skin) &&
					 write(QDir(directory).filePath("protected.mesh.json"), QByteArray("protected")),
				 "write input protection fixtures");
	ok &= expect(run(skinArgs + QStringList{"--package", directory, "--entry", "skin.lmp", "--output",
											QDir(directory).filePath("protected.mesh.json"), "--overwrite"},
					 2),
				 "CLI refuses overwriting a package input even with overwrite");
	QByteArray protectedBytes, wadAfter;
	ok &= expect(readModelFile(QDir(directory).filePath("protected.mesh.json"), &protectedBytes, &error) && protectedBytes == "protected" &&
					 readModelFile(path("skins.wad"), &wadAfter, &error) && wadAfter == wad &&
					 readModelFile(path("source.mesh.json"), &afterSampling, &error) && afterSampling == beforeSampling,
				 "package sources and input model are unchanged after successful and rejected imports");
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
