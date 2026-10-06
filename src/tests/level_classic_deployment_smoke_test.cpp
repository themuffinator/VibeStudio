#include "core/level_build_deployment.h"
#include "core/package_validation.h"
#include "core/studio_settings.h"
#include "tests/level_build_engines_test_helpers.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
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
bool emptyPak(const QString& path) {
	QByteArray bytes("PACK", 4);
	bytes += QByteArray(8, '\0');
	qToLittleEndian<quint32>(12, bytes.data() + 4);
	return subset_test::put(path, bytes);
}
int recorder(const QStringList& args) {
	// No game, UI or injected input: model the original engines' numbered search.
	const bool quake = args.contains("-game");
	const auto directory = args.value(args.indexOf(quake ? "-game" : "game") + 1);
	const auto map = args.value(args.indexOf("+map") + 1);
	bool valid = false;
	QString package;
	int selected = -1;
	for (int slot = 0; slot <= (quake ? 999 : 9); ++slot) {
		const auto path = QDir::current().filePath(directory + QStringLiteral("/pak%1.pak").arg(slot));
		if (!QFileInfo::exists(path)) {
			if (quake) {
				break;
			}
			continue;
		}
		PackageArchive archive;
		QString error;
		if (!archive.load(path, &error) || !validatePackage(archive).valid()) {
			return 8;
		}
		QByteArray bytes;
		if (archive.readEntryBytes("maps/" + map + ".bsp", &bytes, &error)) {
			valid = !bytes.isEmpty();
			package = path;
			selected = slot;
		}
	}
	return subset_test::put(QDir::current().filePath("classic-launch-record.json"),
							QJsonDocument(QJsonObject{{"arguments", QJsonArray::fromStringList(args)},
													  {"validPackageAtLaunch", valid},
													  {"package", package},
													  {"slot", selected}})
								.toJson())
			   ? 0
			   : 9;
}
} // namespace
int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	if (app.arguments().contains("+map")) {
		return recorder(app.arguments());
	}
	if (argc != 3) {
		return 2;
	}
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	QString error;
	for (const auto& target : QStringList{"quake", "quake2"}) {
		const auto root = temp.filePath(target);
		QDir().mkpath(root);
		LevelMapDocument map;
		PackageArchive assets;
		if (!expect(tests::buildEngineFixture(root, target, &map, &assets, &error), "assets", error)) {
			return 1;
		}
		LevelBuildWorkspaceRequest preparation;
		preparation.target = target;
		preparation.directory = QDir(root).filePath("workspace with spaces");
		auto workspace = prepareLevelBuildWorkspace(map, assets, preparation);
		BuildPipelineRequest build;
		build.pipelineId = workspace.defaultPipeline();
		for (const auto& tool : QStringList{"ericw-qbsp", "ericw-vis", "ericw-light"}) {
			build.executableOverrides.append({tool, QString::fromLocal8Bit(argv[1])});
		}
		const auto compiled = runLevelBuildWorkspace(workspace, build);
		if (!expect(workspace.ready && compiled.succeeded(), "compiler fixture", workspace.error + compiled.errors.join('\n'))) {
			return 1;
		}
		GameInstallationProfile installation;
		installation.id = target;
		installation.gameKey = target;
		installation.engineFamily = GameEngineFamily::IdTech2;
		installation.displayName = "Disposable " + target + " recorder";
		installation.rootPath = QDir(root).filePath("game installation");
		QDir().mkpath(installation.rootPath);
		installation.executablePath = QDir(installation.rootPath).filePath(QFileInfo(app.applicationFilePath()).fileName());
		expect(QFile::copy(app.applicationFilePath(), installation.executablePath), "independent recorder copy");
		installation.readOnly = true;
		settings.upsertGameInstallation(installation);
		settings.sync(); // Child CLI processes must see the saved profiles on disk.
		const auto mod = QDir(installation.rootPath).filePath("studio");
		auto plan = planLevelBuildDeployment(workspace, installation, "studio");
		if (!expect(plan.ready && plan.launch.runnable && plan.pakSlot.number == 0, "automatic first slot", plan.error)) {
			return 1;
		}
		expect(!QFileInfo::exists(mod), "review writes nothing");
		const auto args = plan.launch.arguments;
		expect(target == "quake"
				   ? args.contains("-window") && args.value(args.indexOf("-basedir") + 1) == installation.rootPath
				   : args.value(args.indexOf("vid_fullscreen") + 1) == "0" && args.value(args.indexOf("basedir") - 1) == "+set" &&
						 args.value(args.indexOf("basedir") + 1) == installation.rootPath && !args.contains("-basedir"),
			   "windowed engine-aware launch");
		LevelBuildDeploymentOptions options;
		options.dryRun = true;
		options.launch = true;
		auto result = deployLevelBuild(workspace, plan, options);
		expect(result.succeeded() && !result.launched && !QFileInfo::exists(mod), "dry deployment never writes or launches", result.error);
		options.dryRun = false;
		expect(!deployLevelBuild(workspace, plan, options).succeeded(), "read-only permission required");
		options.allowReadOnlyWrite = true;
		result = deployLevelBuild(workspace, plan, options);
		if (!expect(result.succeeded() && result.launched && result.publication.write.outputCommitted, "PAK commit and recorder launch",
					result.error)) {
			return 1;
		}
		const auto recordPath = QDir(installation.rootPath).filePath("classic-launch-record.json");
		QElapsedTimer timer;
		timer.start();
		while (!readJson(recordPath).contains("validPackageAtLaunch") && timer.elapsed() < 10000) {
			QThread::msleep(10);
		}
		expect(readJson(recordPath).value("validPackageAtLaunch").toBool() && readJson(recordPath).value("slot").toInt(-1) == 0,
			   "engine search finds verified deployed map");
		const auto original = subset_test::get(plan.packagePath);
		expect(readJson(plan.pakSlot.receiptPath).value("packageSha256").toString() == result.publication.write.sha256,
			   "receipt binds published bytes");
		options.launch = false;
		expect(!deployLevelBuild(workspace, plan, options).succeeded(), "stale absent slot review refused");
		plan = planLevelBuildDeployment(workspace, installation, "studio");
		expect(plan.ready && plan.pakSlot.number == 0 && plan.packageExists, "repeat deployment reuses slot", plan.error);
		expect(!deployLevelBuild(workspace, plan, options).succeeded(), "repeat requires overwrite opt-in");
		options.allowOverwrite = true;
		options.includeSourceMap = true;
		result = deployLevelBuild(workspace, plan, options);
		expect(result.succeeded() && subset_test::get(plan.backupPath) == original && !QFileInfo::exists(QDir(mod).filePath("pak1.pak")),
			   "replacement retains backup without accumulating slots", result.error);
		const auto stable = subset_test::get(plan.packagePath);
		plan = planLevelBuildDeployment(workspace, installation, "studio");
		const auto token = levelBuildDeploymentReviewSha256(plan);
		QProcess cli;
		int commandCount = 0;
		auto command = [&](QStringList tail) {
			cli.start(QString::fromLocal8Bit(argv[2]),
					  QStringList{"--cli", "--json", "--settings-file", temp.filePath("settings.ini"), "build"} + tail);
			if (!cli.waitForFinished(30000)) {
				cli.kill();
				cli.waitForFinished();
				return -1;
			}
			++commandCount;
			if (commandCount <= 2 && cli.exitCode() != 0) {
				std::cerr << "Unexpected CLI failure: " << cli.exitCode() << " " << cli.readAllStandardOutput().constData()
					<< cli.readAllStandardError().constData() << '\n';
			}
			return cli.exitCode();
		};
		expect(command({"deploy-plan", workspace.directory, "--installation", installation.id, "--mod", "studio", "--pak-slot", "0"}) == 0,
			   "CLI explicit slot review", QString::fromUtf8(cli.readAllStandardError()));
		expect(command({"deploy-prepared", workspace.directory, "--installation", installation.id, "--mod", "studio", "--pak-slot", "0",
						"--expected-deployment-sha256", token, "--overwrite", "--dry-run"}) == 0,
			   "CLI guarded dry deployment", QString::fromUtf8(cli.readAllStandardError()));
		expect(command({"deploy-plan", workspace.directory, "--installation", installation.id, "--pak-slot", "-1"}) != 0,
			   "CLI invalid slot rejected");
		QLockFile lock(QDir(mod).filePath(".vibestudio-deployment.lock"));
		lock.setStaleLockTime(0);
		expect(lock.tryLock(), "hold deployment lock");
		expect(!deployLevelBuild(workspace, plan, options).succeeded() && subset_test::get(plan.packagePath) == stable,
			   "busy folder leaves package intact");
		lock.unlock();
		emptyPak(QDir(mod).filePath("pak2.pak"));
		expect(!deployLevelBuild(workspace, plan, options).succeeded(), "changed search inventory rejects stale review");
		expect(command({"deploy-prepared", workspace.directory, "--installation", installation.id, "--mod", "studio", "--pak-slot", "0",
						"--expected-deployment-sha256", token, "--overwrite", "--dry-run"}) != 0,
			   "CLI complete review token rejects changed search inventory");
		expect(planLevelBuildPakSlot(mod, target, "different").number == 1, "unassigned map selects free slot");
		expect(planLevelBuildPakSlot(mod, target, "different", 2).ready() == (target == "quake2"), "engine-specific gap handling");
		emptyPak(QDir(mod).filePath("pak1.pak"));
		expect(planLevelBuildPakSlot(mod, target, "different", 2).ready(), "consecutive explicit slot supported");
		plan = planLevelBuildDeployment(workspace, installation, "studio");
		bool cancel = false;
		PackageReadControl control;
		control.isCancelled = [&] { return cancel; };
		control.progress = [&](const QString& phase, qint64, qint64) {
			if (phase.startsWith("Writing package:")) {
				cancel = true;
			}
		};
		result = deployLevelBuild(workspace, plan, options, control);
		expect(result.cancelled && !result.publication.write.outputCommitted && subset_test::get(plan.packagePath) == stable,
			   "cancelled package publication preserves installed PAK");
		subset_test::put(plan.packagePath, stable + "changed");
		expect(!planLevelBuildDeployment(workspace, installation, "studio").ready, "changed remembered package requires explicit review");
		expect(planLevelBuildDeployment(workspace, installation, "studio", {}, 0).ready, "explicit review can recover changed PAK");
		subset_test::put(plan.packagePath, stable);
		auto protectedInstallation = installation;
		protectedInstallation.basePackagePaths << plan.packagePath;
		expect(!planLevelBuildDeployment(workspace, protectedInstallation, "studio", {}, 0).ready,
			   "registered base packages cannot be replaced");
		protectedInstallation = installation;
		protectedInstallation.modPackagePaths << plan.pakSlot.receiptPath;
		expect(!planLevelBuildDeployment(workspace, protectedInstallation, "studio", {}, 0).ready,
			   "receipt cannot replace registered content");
		protectedInstallation = installation;
		protectedInstallation.gameKey = target == "quake" ? "quake2" : "quake";
		expect(!planLevelBuildDeployment(workspace, protectedInstallation, "studio").ready &&
				   !levelBuildDeploymentLaunchPlan(workspace, protectedInstallation, "studio").runnable,
			   "wrong game target rejected");
		expect(!planLevelBuildDeployment(workspace, installation, "../outside").ready &&
				   !planLevelBuildDeployment(workspace, installation, "studio", {}, target == "quake" ? 1000 : 10).ready,
			   "folder traversal and slot bounds rejected");
		const auto currentReceipt = subset_test::get(plan.pakSlot.receiptPath);
		auto duplicateReceipt = currentReceipt;
		duplicateReceipt.replace("\"slot\": 0", "\"slot\": 2");
		subset_test::put(QDir(mod).filePath(".vibestudio-pak2.json"), duplicateReceipt);
		expect(!planLevelBuildDeployment(workspace, installation, "studio").ready, "ambiguous map receipts require explicit slot");
		expect(planLevelBuildDeployment(workspace, installation, "studio", {}, 0).ready, "explicit slot disambiguates receipts");
		preparation.directory = QDir(root).filePath("new workspace");
		workspace = prepareLevelBuildWorkspace(map, assets, preparation);
		expect(runLevelBuildWorkspace(workspace, build).succeeded(), "fresh workspace compile");
		QFile::remove(QDir(mod).filePath(".vibestudio-pak2.json"));
		expect(planLevelBuildDeployment(workspace, installation, "studio").pakSlot.number == 0,
			   "fresh workspace for same map reuses installed slot");
		subset_test::put(plan.pakSlot.receiptPath, QByteArray(65537, 'x'));
		expect(!planLevelBuildDeployment(workspace, installation, "studio").ready, "oversized receipt rejected before reading");
		subset_test::put(plan.pakSlot.receiptPath, currentReceipt);
		// A metadata failure after the atomic package commit must not hide the
		// committed result or destroy a concurrent metadata edit.
		plan = planLevelBuildDeployment(workspace, installation, "studio", {}, 0);
		const auto outsideReceipt = currentReceipt + "\n";
		bool changedReceipt = false;
		control = {};
		control.progress = [&](const QString& phase, qint64, qint64) {
			if (!changedReceipt && phase.startsWith("Writing package:")) {
				changedReceipt = true;
				subset_test::put(plan.pakSlot.receiptPath, outsideReceipt);
			}
		};
		result = deployLevelBuild(workspace, plan, options, control);
		expect(changedReceipt && result.succeeded() && result.publication.write.outputCommitted &&
				   result.warnings.join('\n').contains("receipt could not be saved") &&
				   subset_test::get(plan.pakSlot.receiptPath) == outsideReceipt,
			   "metadata conflict preserves committed PAK and outside receipt", result.error);
		plan = planLevelBuildDeployment(workspace, installation, "studio", {}, 0);
		options.launch = true;
		bool changedLayout = false;
		control.progress = [&](const QString& phase, qint64, qint64) {
			if (!changedLayout && phase.startsWith("Writing package:")) {
				changedLayout = true;
				emptyPak(QDir(mod).filePath("pak3.pak"));
			}
		};
		result = deployLevelBuild(workspace, plan, options, control);
		expect(changedLayout && result.publication.write.outputCommitted && !result.succeeded() && !result.launched,
			   "search inventory change after publication blocks launch");
		const auto reserved = QDir(installation.rootPath).filePath("reserved");
		QDir().mkpath(reserved);
		subset_test::put(QDir(reserved).filePath(".vibestudio-pak0.json"), currentReceipt);
		const auto reservedPlan = planLevelBuildPakSlot(reserved, target, "another_map");
		expect(target == "quake" ? !reservedPlan.ready() : reservedPlan.ready() && reservedPlan.number == 1,
			   "missing PAK receipt reserves its map slot and respects Quake gaps");
		if (target == "quake2") {
			const auto full = QDir(installation.rootPath).filePath("full");
			QDir().mkpath(full);
			for (int slot = 0; slot < 10; ++slot) {
				emptyPak(QDir(full).filePath(QStringLiteral("pak%1.pak").arg(slot)));
			}
			expect(!planLevelBuildPakSlot(full, target, workspace.mapName).ready() &&
					   planLevelBuildPakSlot(full, target, workspace.mapName, 9).ready(),
				   "full Quake II folder requires explicit replacement");
		}
		QDir().mkpath(QDir(installation.rootPath).filePath("case"));
		emptyPak(QDir(installation.rootPath).filePath("case/PAK0.PAK"));
		expect(!planLevelBuildPakSlot(QDir(installation.rootPath).filePath("case"), target, workspace.mapName).ready(),
			   "nonportable slot case rejected");
	}
	return ok ? 0 : 1;
}
