#include "app/application_shell.h"
#include "app/level_build_package_dialog.h"
#include "app/level_build_workspace_dialog.h"
#include "app/studio_theme.h"
#include "tests/level_material_test_helpers.h"
#include "tests/package_subset_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
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
bool until(const std::function<bool()>& ready) {
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 60000) {
		QEventLoop loop;
		QTimer::singleShot(10, &loop, &QEventLoop::quit);
		loop.exec();
	}
	return ready();
}
class Expansion final : public QTranslator {
  public:
	QString translate(const char*, const char* source, const char*, int) const override {
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, '~'));
	}
};
} // namespace
int main(int argc, char** argv) {
	// Semantic Qt calls and widget rendering; no OS input or screen capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	if (argc != 3) {
		return 2;
	}
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	settings.setReducedMotion(true);
	settings.upsertCompilerToolPathOverride({"q3map2", QString::fromLocal8Bit(argv[1])});
	GameInstallationProfile installation;
	installation.id = "deployment-ui-fixture";
	installation.gameKey = "quake3";
	installation.engineFamily = GameEngineFamily::IdTech3;
	installation.displayName = "Disposable recorder installation";
	installation.rootPath = temp.filePath("game installation");
	QDir().mkpath(installation.rootPath);
	installation.executablePath = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
	installation.readOnly = true;
	settings.upsertGameInstallation(installation);
	settings.setSelectedGameInstallation(installation.id);
	settings.setLaunchGameDirectory(installation.id, "studio_shell");
	QString error;
	LevelMapDocument map;
	if (!expect(tests::createMaterialFixture(temp.path(), &map, &error), "fixture", error)) {
		return 1;
	}
	const auto source = temp.filePath("original.map");
	tests::putMaterialFile(source, serializeLevelMap(map).bytes);
	PackageArchive assets;
	expect(assets.load(temp.filePath("assets"), &error), "assets", error);
	LevelBuildWorkspaceRequest prepare;
	prepare.directory = temp.filePath("prepared");
	const auto workspace = prepareLevelBuildWorkspace(map, assets, prepare);
	BuildPipelineRequest request;
	request.pipelineId = "quake3-full";
	request.executableOverrides = settings.compilerToolPathOverrides();
	const auto compiled = runLevelBuildWorkspace(workspace, request);
	if (!expect(compiled.succeeded(), "fixture compiler", compiled.errors.join('\n'))) {
		return 1;
	}
	int heartbeats = 0;
	QTimer heartbeat;
	heartbeat.setInterval(1);
	QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++heartbeats; });
	heartbeat.start();
	for (const auto scale : {100, 200}) {
		const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
		settings.setTheme(theme);
		settings.setTextScalePercent(scale);
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
		Expansion expansion;
		if (scale == 200) {
			app.installTranslator(&expansion);
		}
		LevelBuildPackageDialog dialog(workspace);
		dialog.configureDeployment(installation, QStringLiteral("studio_%1").arg(scale), true);
		if (scale == 200) {
			dialog.setLayoutDirection(Qt::RightToLeft);
			dialog.resize(1900, 1300);
		}
		dialog.show();
		auto* deploy = dialog.findChild<QPushButton*>("buildPackagePublish");
		auto* status = dialog.findChild<QLabel*>("buildPackageStatus");
		auto* folder = dialog.findChild<QLineEdit*>("buildDeploymentDirectory");
		auto* consent = dialog.findChild<QCheckBox*>("buildDeploymentAllow");
		auto* launch = dialog.findChild<QCheckBox*>("buildDeploymentLaunch");
		auto* output = dialog.findChild<QLineEdit*>("buildPackageOutput");
		expect(until([&] { return dialog.reviewReady(); }), "worker deployment review", status->text());
		expect(!deploy->isEnabled() && !consent->isChecked() && launch->isChecked() && output->isReadOnly(),
			   "review requires explicit one-operation consent");
		expect(folder->layoutDirection() == Qt::LeftToRight && folder->focusPolicy() != Qt::NoFocus &&
				   !folder->accessibleDescription().isEmpty() &&
				   QAccessible::queryAccessibleInterface(folder)->role() == QAccessible::EditableText &&
				   QAccessible::queryAccessibleInterface(consent)->role() == QAccessible::CheckBox,
			   "accessible keyboard controls preserve path direction");
		expect(dialog.findChild<QTableWidget*>("buildPackageFiles")->rowCount() >= 8, "file review exposes model and generated assets");
		const auto captures = qEnvironmentVariable("VIBESTUDIO_CAPTURE_DIR");
		if (!captures.isEmpty()) {
			QDir().mkpath(captures);
			QPixmap image(dialog.size());
			dialog.render(&image);
			expect(image.save(QDir(captures).filePath(QStringLiteral("deployment-%1.png").arg(scale))), "widget review capture");
		}
		auto* scroll = dialog.findChild<QScrollArea*>();
		auto* table = dialog.findChild<QTableWidget*>("buildPackageFiles");
		auto* page = dialog.findChild<QSpinBox*>("buildPackagePage");
		expect(table->mapTo(&dialog, QPoint(0, table->height())).y() <= page->mapTo(&dialog, QPoint()).y(),
			   "review table and page control do not overlap");
		scroll->ensureWidgetVisible(page);
		QApplication::processEvents();
		expect(scroll->viewport()->rect().contains(page->mapTo(scroll->viewport(), page->rect().center())),
			   "page control reachable by scrolling at both scales");
		consent->setChecked(true);
		expect(deploy->isEnabled(), "one-operation permission enables deployment");
		deploy->click();
		expect(until([&] { return !dialog.busy(); }), "worker deployment completes");
		expect(dialog.lastDeploymentResult().succeeded() && dialog.lastDeploymentResult().launched && !deploy->isEnabled(),
			   "reviewed package deployed before recorder launch", status->text());
		expect(settings.gameInstallations().first().readOnly, "GUI does not persist write permission");
		dialog.findChild<QPushButton*>("buildPackageReview")->click();
		expect(until([&] { return dialog.reviewReady(); }) && !deploy->isEnabled(), "existing destination needs replacement permission");
		dialog.findChild<QCheckBox*>("buildPackageOverwrite")->setChecked(true);
		launch->setChecked(false);
		expect(deploy->isEnabled(), "reviewed replacement enabled");
		const auto old = subset_test::get(output->text());
		deploy->click();
		dialog.reject();
		expect(until([&] { return !dialog.busy(); }), "cancel settles asynchronously");
		expect(dialog.lastDeploymentResult().cancelled && subset_test::get(output->text()) == old, "cancel preserves installed package");
		folder->setText("changed_folder");
		expect(!dialog.reviewReady() && !deploy->isEnabled(), "folder edit invalidates prior review");
		dialog.reject();
		if (scale == 200) {
			app.removeTranslator(&expansion);
		}
	}
	settings.setTheme(StudioTheme::Dark);
	settings.setTextScalePercent(100);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	LevelBuildPackageDialog stale(workspace);
	stale.configureDeployment(installation, "stale");
	stale.show();
	expect(until([&] { return stale.reviewReady(); }), "stale destination review");
	const auto target = stale.findChild<QLineEdit*>("buildPackageOutput")->text();
	tests::putMaterialFile(target, "externally created");
	stale.findChild<QCheckBox*>("buildDeploymentAllow")->setChecked(true);
	stale.findChild<QCheckBox*>("buildPackageOverwrite")->setChecked(true);
	stale.findChild<QPushButton*>("buildPackagePublish")->click();
	expect(until([&] { return !stale.busy(); }) && !stale.lastDeploymentResult().succeeded() &&
			   subset_test::get(target) == "externally created",
		   "stale UI cannot overwrite a newly appeared package");
	stale.reject();
	// Actual shell Prepare -> Build and Launch -> reviewed deployment, using only the recorder executable.
	ApplicationShell shell;
	shell.openPathFromCommandLine(temp.filePath("assets"));
	shell.openPathFromCommandLine(source);
	expect(until([&] { return shell.levelDocument().sourcePath == source; }), "shell map loaded");
	QTimer drive, watchdog;
	drive.setInterval(10);
	watchdog.setSingleShot(true);
	bool started = false, accepted = false, deployed = false;
	QObject::connect(&drive, &QTimer::timeout, &shell, [&] {
		auto* modal = QApplication::activeModalWidget();
		if (!modal) {
			return;
		}
		if (modal->objectName() == "levelBuildWorkspaceDialog") {
			auto* dialog = static_cast<LevelBuildWorkspaceDialog*>(modal);
			if (!started) {
				started = true;
				dialog->setDestination(temp.filePath("shell-build"), "shell_build");
				dialog->findChild<QPushButton*>("levelBuildPrepare")->click();
			} else if (dialog->isReady()) {
				dialog->accept();
				accepted = dialog->result() == QDialog::Accepted;
				drive.stop();
			}
		} else if (modal->objectName() == "levelBuildPackageDialog") {
			auto* dialog = static_cast<LevelBuildPackageDialog*>(modal);
			if (dialog->reviewReady() && !started) {
				started = true;
				expect(dialog->findChild<QCheckBox*>("buildDeploymentLaunch")->isChecked(),
					   "Build and Launch routes through deployment review");
				dialog->findChild<QCheckBox*>("buildDeploymentAllow")->setChecked(true);
				dialog->findChild<QPushButton*>("buildPackagePublish")->click();
			} else if (started && !dialog->busy()) {
				deployed = dialog->lastDeploymentResult().succeeded() && dialog->lastDeploymentResult().launched;
				expect(deployed, "shell deployment result", dialog->findChild<QLabel*>("buildPackageStatus")->text());
				dialog->reject();
				drive.stop();
			}
		}
	});
	QObject::connect(&watchdog, &QTimer::timeout, &shell, [&] {
		if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
			modal->reject();
		}
	});
	drive.start();
	watchdog.start(60000);
	shell.findChild<QAction*>("build.prepareWorkspace")->trigger();
	drive.stop();
	watchdog.stop();
	expect(accepted, "shell adopts workspace");
	auto* buildLaunch = shell.findChild<QAction*>("build.runAndLaunch");
	expect(buildLaunch->isEnabled() && shell.findChild<QAction*>("build.deployPrepared")->isEnabled(),
		   "compatible installation enables integrated build/deploy commands");
	started = false;
	drive.start();
	watchdog.start(60000);
	buildLaunch->trigger();
	expect(until([&] { return deployed; }), "shell full build and deployment chain");
	drive.stop();
	watchdog.stop();
	expect(QFileInfo::exists(QDir(installation.rootPath).filePath("studio_shell/vibestudio_shell_build.pk3")) &&
			   settings.gameInstallations().first().readOnly,
		   "shell uses selected game folder and retains permission");
	const auto record = QDir(installation.rootPath).filePath("launch-record.json");
	expect(until([&] {
			   const auto json = QJsonDocument::fromJson(subset_test::get(record)).object();
			   return json.value("validPackageAtLaunch").toBool() && json.value("package").toString().contains("shell_build");
		   }),
		   "shell recorder verifies package at launch");
	shell.close();
	expect(heartbeats > 10, "event loop stays live during hashing and publication");
	return ok ? 0 : 1;
}
