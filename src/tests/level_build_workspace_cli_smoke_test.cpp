#include "core/level_build_workspace.h"
#include "core/package_draft.h"
#include "tests/level_material_test_helpers.h"
#include "tests/package_subset_test_helpers.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
using namespace vibestudio;
int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (argc != 2 || !temp.isValid()) {
		return 1;
	}
	bool ok = true;
	const auto expect = [&](bool pass, const char* message) {
		if (!pass) {
			ok = false;
			std::cerr << message << '\n';
		}
	};
	QString error;
	LevelMapDocument map;
	expect(tests::createMaterialFixture(temp.path(), &map, &error), "fixture");
	const auto source = temp.filePath("arena.map");
	tests::putMaterialFile(source, serializeLevelMap(map).bytes);
	PackageArchive archive;
	expect(archive.load(temp.filePath("assets"), &error), "asset folder");
	PackageStagingModel draft;
	expect(draft.loadBaseArchive(archive, &error), "staging");
	const auto replacement = tests::materialImage(96, 48, true);
	expect(draft.addBytes(replacement, "textures/studio/grid.png", &error, PackageStageConflictResolution::ReplaceExisting),
		   "staged texture");
	const auto draftPath = temp.filePath("assets.vibepackage");
	expect(PackageDraft::save(draftPath, &draft, false, &error), "draft save");
	const auto cli = [&](QStringList arguments, int code = 0) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(QString::fromLocal8Bit(argv[1]),
					  QStringList{"--cli", "--json", "--settings-file", temp.filePath("settings.ini"), "build"} + arguments);
		const bool finished = process.waitForFinished(30000);
		const auto bytes = process.readAllStandardOutput();
		if (!finished || process.exitCode() != code) {
			std::cerr << arguments.join(' ').toStdString() << '\n' << bytes.toStdString() << process.readAllStandardError().toStdString();
		}
		expect(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code, "CLI exit status");
		const auto json = QJsonDocument::fromJson(bytes);
		expect(json.isObject(), "structured CLI response");
		return json.object();
	};
	const auto output = temp.filePath("prepared");
	const QStringList prepare{"prepare", source, "--package", draftPath, "--output", output};
	const auto dry = cli(prepare + QStringList{"--dry-run"});
	expect(dry["workspace"].toObject()["prepared"].toBool() && !QFileInfo::exists(output), "dry run writes nothing");
	cli(prepare);
	const auto workspace = readLevelBuildWorkspace(output);
	expect(workspace.ready && verifyLevelBuildWorkspace(workspace, &error), "CLI output inventory verifies");
	expect(subset_test::get(QDir(workspace.assetsPath()).filePath("textures/studio/grid.png")) == replacement, "CLI includes draft edits");
	cli(prepare, 4);
	cli(prepare + QStringList{"--typo"}, 2);
	cli(prepare + QStringList{"--max-bytes", "0"}, 2);
	cli({"prepare", source, "--package", draftPath, "--output", temp.filePath("limited"), "--max-bytes", "1"}, 4);
	cli(prepare + QStringList{"--output", temp.filePath("repeat")}, 2);
	cli({"prepare", source, "--package", temp.filePath("assets"), "--output", temp.filePath("assets/nested")}, 4);
	cli({"prepare", source, "--package", temp.filePath("assets"), "--output", temp.filePath("folder")});
	const QStringList run{"run-prepared", output, "--tool", "vibemap3=" + QString::fromLocal8Bit(argv[1]), "--dry-run"};
	const auto plan = cli(run);
	expect(plan["pipeline"].toObject()["stages"].toArray().size() == 3, "CLI shared pipeline plans three stages");
	const auto retired = cli({"run-prepared", output, "--tool", "q3map2=" + QString::fromLocal8Bit(argv[1]), "--dry-run"}, 2);
	expect(QJsonDocument(retired).toJson().contains("vibemap3"), "a retired --tool id names its VibeMap3 replacement");
	cli(run + QStringList{"--stage-args", "bsp=-fs_basepath somewhere"}, 4);
	cli(run + QStringList{"--disable-stage", "typo"}, 2);
	cli(run + QStringList{"--pipeline", "quake-full"}, 4);
	auto damaged = subset_test::get(workspace.inputPath());
	damaged[0] = damaged[0] == '{' ? '[' : '{';
	tests::putMaterialFile(workspace.inputPath(), damaged);
	const auto refused = cli(run, 4);
	expect(refused["pipeline"].toObject()["stages"].toArray().isEmpty(), "changed input blocked before stages");
	expect(subset_test::get(source) == serializeLevelMap(map).bytes, "original map unchanged");
	return ok ? 0 : 1;
}
