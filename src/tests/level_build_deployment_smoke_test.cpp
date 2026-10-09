#include "core/level_build_deployment.h"
#include "core/package_validation.h"
#include "core/studio_settings.h"
#include "tests/level_material_test_helpers.h"
#include "tests/package_subset_test_helpers.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool pass, const char* label, const QString& error = {}) {
	if (!pass) {
		ok = false;
		std::cerr << label << ": " << error.toStdString() << '\n';
	}
	return pass;
}
QJsonObject readJson(const QString& path) { return QJsonDocument::fromJson(subset_test::get(path)).object(); }
} // namespace
int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	// A recorder only: this executable never opens a game or controls user input.
	if (app.arguments().contains("+devmap")) {
		const auto args = app.arguments();
		const auto map = args.value(args.indexOf("+devmap") + 1);
		const auto mod = args.value(args.indexOf("fs_game") + 1);
		const auto package = QDir::current().filePath(mod + "/vibestudio_" + map + ".pk3");
		PackageArchive archive;
		QString error;
		const bool valid = archive.load(package, &error) && validatePackage(archive).valid();
		return tests::putMaterialFile(
				   QDir::current().filePath("launch-record.json"),
				   QJsonDocument(
					   QJsonObject{{"arguments", QJsonArray::fromStringList(args)}, {"package", package}, {"validPackageAtLaunch", valid}})
					   .toJson())
				   ? 0
				   : 9;
	}
	if (argc != 3) {
		return 2;
	}
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	QString error;
	LevelMapDocument map;
	if (!expect(tests::createMaterialFixture(temp.path(), &map, &error), "asset fixture", error)) {
		return 1;
	}
	tests::putMaterialFile(temp.filePath("assets/sound/studio/test.wav"),
						   QByteArray::fromHex("524946462400000057415645666d74201000000001000100401f0000803e0000020010006461746100000000"));
	PackageArchive assets;
	expect(assets.load(temp.filePath("assets"), &error), "load captured assets", error);
	LevelBuildWorkspaceRequest preparation;
	preparation.directory = temp.filePath("build workspace");
	const auto workspace = prepareLevelBuildWorkspace(map, assets, preparation);
	BuildPipelineRequest build;
	build.pipelineId = "quake3-full";
	build.executableOverrides = {{"vibemap3", QString::fromLocal8Bit(argv[1])}};
	const auto compiled = runLevelBuildWorkspace(workspace, build);
	if (!expect(workspace.ready && compiled.succeeded(), "prepared compiler fixture", workspace.error + compiled.errors.join('\n'))) {
		return 1;
	}
	GameInstallationProfile installation;
	installation.id = "deployment-fixture";
	installation.gameKey = "quake3";
	installation.engineFamily = GameEngineFamily::IdTech3;
	installation.displayName = "Recorder installation";
	installation.rootPath = temp.filePath("game installation");
	QDir().mkpath(installation.rootPath);
	installation.executablePath = QDir(installation.rootPath).filePath(QFileInfo(app.applicationFilePath()).fileName());
	expect(QFile::copy(app.applicationFilePath(), installation.executablePath), "independent recorder executable copy");
	installation.readOnly = true;
	auto review = planLevelBuildDeployment(workspace, installation, "studio");
	if (!expect(review.ready && review.launch.runnable && !review.packageExists, "read-only deployment plan", review.error)) {
		return 1;
	}
	expect(!QFileInfo::exists(QDir(installation.rootPath).filePath("studio")), "review creates no game folder");
	const auto arguments = review.launch.arguments;
	expect(arguments.value(arguments.indexOf("r_fullscreen") + 1) == "0" &&
			   arguments.value(arguments.indexOf("fs_homepath") + 1) == installation.rootPath &&
			   arguments.value(arguments.indexOf("fs_basepath") + 1) == installation.rootPath &&
			   arguments.value(arguments.indexOf("fs_game") + 1) == "studio" &&
			   arguments.value(arguments.indexOf("+devmap") + 1) == workspace.mapName,
		   "windowed captured map command retains paths with spaces");
	LevelBuildDeploymentOptions options;
	options.dryRun = true;
	options.launch = true;
	auto result = deployLevelBuild(workspace, review, options);
	expect(result.succeeded() && !result.launched && !QFileInfo::exists(QDir(installation.rootPath).filePath("studio")),
		   "dry deployment and launch write nothing", result.error);
	options.dryRun = false;
	expect(!deployLevelBuild(workspace, review, options).succeeded(), "read-only installation requires one-operation permission");
	options.allowReadOnlyWrite = true;
	options.launch = false;
	result = deployLevelBuild(workspace, review, options);
	if (!expect(result.succeeded() && result.publication.write.outputCommitted && installation.readOnly,
				"full PK3 deployment preserves installation permission", result.error)) {
		return 1;
	}
	PackageArchive deployed;
	expect(deployed.load(review.packagePath, &error) && validatePackage(deployed).valid(), "valid installed PK3", error);
	for (const auto& path : {"maps/studio_build.bsp", "maps/studio_build/lm_0000.tga", "scripts/q3map2_studio_build.shader",
							 "models/studio/prop.md3", "textures/studio/grid.png", "sound/studio/test.wav"}) {
		QByteArray bytes;
		expect(deployed.readEntryBytes(path, &bytes, &error) && !bytes.isEmpty(), "runtime asset included",
			   QString::fromLatin1(path) + error);
	}
	expect(!result.publication.paths.contains("maps/studio_build.prt") && !result.publication.paths.contains("maps/studio_build.map"),
		   "diagnostics and source omitted by default");
	const auto original = subset_test::get(review.packagePath);
	expect(!deployLevelBuild(workspace, review, options).succeeded() && subset_test::get(review.packagePath) == original,
		   "absent-file review cannot replace a newly appeared package");
	review = planLevelBuildDeployment(workspace, installation, "studio");
	expect(!deployLevelBuild(workspace, review, options).succeeded(), "replacement requires opt-in");
	options.allowOverwrite = true;
	options.includeSourceMap = true;
	options.launch = true;
	result = deployLevelBuild(workspace, review, options);
	expect(result.succeeded() && result.launched && result.processId > 0 && subset_test::get(review.backupPath) == original &&
			   result.publication.paths.contains("maps/studio_build.map"),
		   "verified backup before recorder launch", result.error);
	const auto record = QDir(installation.rootPath).filePath("launch-record.json");
	QElapsedTimer timer;
	timer.start();
	while (!readJson(record).contains("validPackageAtLaunch") && timer.elapsed() < 10000) {
		QThread::msleep(10);
	}
	expect(readJson(record).value("validPackageAtLaunch").toBool(), "recorder finds complete valid package on entry");
	options.launch = false;
	review = planLevelBuildDeployment(workspace, installation, "studio");
	const auto stable = subset_test::get(review.packagePath);
	auto changed = stable;
	changed[changed.size() - 1] ^= 1;
	tests::putMaterialFile(review.packagePath, changed);
	expect(!deployLevelBuild(workspace, review, options).succeeded() && subset_test::get(review.packagePath) == changed,
		   "same-size changed destination is preserved");
	tests::putMaterialFile(review.packagePath, stable);
	const auto receiptPath = QDir(workspace.directory).filePath("build-outputs.json");
	const auto receipt = subset_test::get(receiptPath);
	tests::putMaterialFile(receiptPath, receipt + "\n");
	expect(!deployLevelBuild(workspace, review, options).succeeded(), "changed compiler receipt requires re-review");
	tests::putMaterialFile(receiptPath, receipt);
	bool cancel = false;
	PackageReadControl control;
	control.isCancelled = [&] { return cancel; };
	control.progress = [&](const QString& phase, qint64, qint64) {
		if (phase.startsWith("Writing package:")) {
			cancel = true;
		}
	};
	result = deployLevelBuild(workspace, review, options, control);
	expect(result.cancelled && !result.publication.write.outputCommitted && subset_test::get(review.packagePath) == stable,
		   "cancelled publication preserves prior package", result.error);
	control = {};
	bool mutated = false;
	control.progress = [&](const QString& phase, qint64, qint64) {
		if (!mutated && phase.startsWith("Writing package:")) {
			mutated = true;
			tests::putMaterialFile(review.packagePath, changed);
		}
	};
	result = deployLevelBuild(workspace, review, options, control);
	expect(mutated && !result.succeeded() && subset_test::get(review.packagePath) == changed,
		   "destination changed between review and publication is preserved", result.error);
	tests::putMaterialFile(review.packagePath, stable);
	for (const auto& folder : {"../escape", "a/b", "CON", ".", "two words"}) {
		expect(!planLevelBuildDeployment(workspace, installation, folder).ready, "nonportable game folder rejected", folder);
	}
	auto invalid = installation;
	invalid.engineFamily = GameEngineFamily::IdTech2;
	expect(!planLevelBuildDeployment(workspace, invalid, "studio").ready, "wrong engine family rejected");
	invalid = installation;
	invalid.gameKey = "quake-live";
	expect(!planLevelBuildDeployment(workspace, invalid, "studio").ready, "different idTech3 game layout rejected");
	invalid = installation;
	invalid.basePackagePaths << review.packagePath;
	expect(!planLevelBuildDeployment(workspace, invalid, "studio").ready, "registered installation package protected");
	invalid = installation;
	invalid.modPackagePaths << review.backupPath;
	expect(!planLevelBuildDeployment(workspace, invalid, "studio").ready, "registered backup path protected");
	invalid = installation;
	invalid.rootPath = workspace.directory;
	expect(!planLevelBuildDeployment(workspace, invalid, "studio").ready, "captured workspace protected");
	invalid = installation;
	invalid.rootPath = temp.filePath("assets");
	expect(!planLevelBuildDeployment(workspace, invalid, "studio").ready, "original asset folder protected");
	invalid = installation;
	invalid.executablePath.clear();
	expect(planLevelBuildDeployment(workspace, invalid, "studio").ready &&
			   !planLevelBuildDeployment(workspace, invalid, "studio").launch.runnable,
		   "deployment alone does not require an executable");
	const auto fresh = planLevelBuildDeployment(workspace, installation, "cancelled_new");
	cancel = false;
	control.isCancelled = [&] { return cancel; };
	control.progress = [&](const QString& phase, qint64, qint64) {
		if (phase.startsWith("Writing package:")) {
			cancel = true;
		}
	};
	result = deployLevelBuild(workspace, fresh, options, control);
	expect(result.cancelled && !QFileInfo::exists(QDir(installation.rootPath).filePath("cancelled_new")),
		   "cancel removes only the newly created empty game folder");
	invalid = installation;
	invalid.executablePath = temp.filePath("not-an-engine.dat");
	tests::putMaterialFile(invalid.executablePath, "A nonexecutable launch failure fixture.");
	const auto failedLaunch = planLevelBuildDeployment(workspace, invalid, "failed_launch");
	options.launch = true;
	result = deployLevelBuild(workspace, failedLaunch, options);
	expect(!result.succeeded() && !result.launched && result.publication.write.outputCommitted && !result.error.isEmpty() &&
			   QFileInfo::exists(failedLaunch.packagePath),
		   "launch failure reports an already deployed package", result.error);
	options.launch = false;
	// Real CLI entry point, with settings and all deployment targets confined to this fixture.
	const auto settingsPath = temp.filePath("settings.ini");
	StudioSettings::setOverrideFilePath(settingsPath);
	StudioSettings settings;
	settings.upsertGameInstallation(installation);
	settings.setSelectedGameInstallation(installation.id);
	settings.sync();
	const auto cli = [&](const QStringList& args, int expected) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(QString::fromLocal8Bit(argv[2]), QStringList{"--cli", "--settings-file", settingsPath, "--json", "build"} + args);
		const bool finished = process.waitForFinished(60000);
		const auto output = process.readAllStandardOutput();
		expect(finished && process.exitCode() == expected, "CLI exit", QString::fromUtf8(output + process.readAllStandardError()));
		const auto json = QJsonDocument::fromJson(output).object();
		expect(!json.isEmpty(), "structured CLI result");
		return json;
	};
	const auto planJson = cli({"deploy-plan", workspace.directory, "--mod", "cli_mod"}, 0).value("deploymentPlan").toObject();
	expect(planJson.value("ready").toBool() && !QFileInfo::exists(planJson.value("packagePath").toString()), "CLI read-only plan");
	cli({"deploy-plan", workspace.directory, "--launch"}, 2);
	cli({"deploy-prepared", workspace.directory, "--bogus"}, 2);
	cli({"deploy-prepared", workspace.directory, "--mod", "studio", "--mod", "studio"}, 2);
	cli({"deploy-prepared", workspace.directory, "--expected-package-sha256", "bad"}, 2);
	cli({"deploy-plan", workspace.directory, "--installation", "missing"}, 3);
	cli({"deploy-prepared", workspace.directory, "--mod", "cli_mod"}, 4);
	const auto dry = cli({"deploy-prepared", workspace.directory, "--mod", "cli_mod", "--dry-run", "--launch"}, 0);
	expect(!dry.value("deployment").toObject().value("launched").toBool() &&
			   !QFileInfo::exists(QDir(installation.rootPath).filePath("cli_mod")),
		   "CLI dry-run never launches or creates directories");
	const auto digest = QString::fromLatin1(review.artifacts.recordSha256.toHex());
	const auto published = cli({"deploy-prepared", workspace.directory, "--mod", "cli_mod", "--allow-test-assets",
								"--expected-package-sha256", "missing", "--expected-output-sha256", digest},
							   0);
	expect(published.value("deployment").toObject().value("succeeded").toBool() && settings.gameInstallations().first().readOnly,
		   "CLI complete deployment does not persist permission");
	cli({"deploy-prepared", workspace.directory, "--mod", "cli_mod", "--allow-test-assets", "--overwrite", "--expected-package-sha256",
		 "missing"},
		4);
	cli({"deploy-prepared", workspace.directory, "--mod", "cli_mod", "--allow-test-assets", "--overwrite", "--expected-output-sha256",
		 QString(64, '0')},
		4);
	// The reviewed executable must still match even when the package itself is unchanged.
	review = planLevelBuildDeployment(workspace, installation, "studio");
	QFile executable(installation.executablePath);
	expect(executable.open(QIODevice::Append) && executable.write("changed") == 7, "mutate independent recorder executable");
	executable.close();
	options.launch = true;
	expect(!deployLevelBuild(workspace, review, options).succeeded() && subset_test::get(review.packagePath) == stable,
		   "changed launch executable blocks deployment");
	return ok ? 0 : 1;
}
