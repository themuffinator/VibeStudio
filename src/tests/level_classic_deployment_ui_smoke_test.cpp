#include "app/application_shell.h"
#include "app/level_build_package_dialog.h"
#include "app/level_build_workspace_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/level_build_engines_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
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
		const auto value = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 3, '~'));
	}
};
} // namespace
int main(int argc, char** argv) {
	// Offscreen semantic Qt calls and QWidget::render only; no native input.
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
	QString error;
	int heartbeats = 0;
	QTimer heartbeat;
	heartbeat.setInterval(1);
	QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++heartbeats; });
	heartbeat.start();
	for (const auto& target : QStringList{"quake", "quake2"}) {
		const auto root = temp.filePath(target);
		QDir().mkpath(root);
		LevelMapDocument map;
		PackageArchive assets;
		if (!expect(tests::buildEngineFixture(root, target, &map, &assets, &error), "assets", error)) {
			return 1;
		}
		LevelBuildWorkspaceRequest prepare;
		prepare.target = target;
		prepare.directory = QDir(root).filePath("build workspace");
		const auto workspace = prepareLevelBuildWorkspace(map, assets, prepare);
		BuildPipelineRequest build;
		build.pipelineId = workspace.defaultPipeline();
		for (const auto& tool : QStringList{"vibemap2-bsp", "vibemap2-vis", "vibemap2-light"}) {
			build.executableOverrides.append({tool, QString::fromLocal8Bit(argv[1])});
		}
		const auto compiled = runLevelBuildWorkspace(workspace, build);
		if (!expect(compiled.succeeded(), "compiler fixture", compiled.errors.join('\n'))) {
			return 1;
		}
		GameInstallationProfile installation;
		installation.id = installation.gameKey = target;
		installation.engineFamily = GameEngineFamily::IdTech2;
		installation.displayName = "Disposable " + target + " recorder";
		installation.rootPath = QDir(root).filePath("game installation");
		QDir().mkpath(installation.rootPath);
		installation.executablePath = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
		installation.readOnly = true;
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
			dialog.configureDeployment(installation, QStringLiteral("studio_%1").arg(scale), false);
			if (scale == 200) {
				dialog.setLayoutDirection(Qt::RightToLeft);
				dialog.resize(1900, 1300);
			}
			dialog.show();
			auto* slot = dialog.findChild<QSpinBox*>("buildDeploymentPakSlot");
			auto* deploy = dialog.findChild<QPushButton*>("buildPackagePublish");
			auto* review = dialog.findChild<QPushButton*>("buildPackageReview");
			auto* consent = dialog.findChild<QCheckBox*>("buildDeploymentAllow");
			auto* output = dialog.findChild<QLineEdit*>("buildPackageOutput");
			auto* status = dialog.findChild<QLabel*>("buildPackageStatus");
			expect(until([&] { return dialog.reviewReady(); }), "worker slot review", status->text());
			expect(slot->isVisible() && slot->value() == -1 && slot->maximum() == (target == "quake" ? 999 : 9) &&
					   !slot->accessibleDescription().isEmpty() && slot->focusPolicy() != Qt::NoFocus &&
					   QAccessible::queryAccessibleInterface(slot)->role() == QAccessible::SpinBox,
				   "accessible target-aware slot control");
			expect(!dialog.findChild<QComboBox*>("buildPackageCompression")->isEnabled() && output->text().endsWith("pak0.pak"),
				   "PAK storage and numbered output review");
			consent->setChecked(true);
			expect(deploy->isEnabled(), "one-operation permission enables publication");
			slot->setValue(0);
			expect(!dialog.reviewReady() && !deploy->isEnabled(), "slot change invalidates review");
			review->click();
			expect(until([&] { return dialog.reviewReady(); }), "explicit slot re-review", status->text());
			const auto captures = qEnvironmentVariable("VIBESTUDIO_CAPTURE_DIR");
			if (!captures.isEmpty()) {
				QDir().mkpath(captures);
				QPixmap pixels(dialog.size());
				dialog.render(&pixels);
				expect(pixels.save(QDir(captures).filePath(QStringLiteral("classic-%1-%2.png").arg(target).arg(scale))),
					   "Qt widget render");
			}
			auto* scroll = dialog.findChild<QScrollArea*>();
			scroll->ensureWidgetVisible(slot);
			QApplication::processEvents();
			expect(scroll->viewport()->rect().contains(slot->mapTo(scroll->viewport(), slot->rect().center())),
				   "slot reachable at both text scales");
			deploy->click();
			expect(until([&] { return !dialog.busy(); }), "asynchronous publication completes");
			expect(dialog.lastDeploymentResult().succeeded() && dialog.lastDeploymentResult().plan.pakSlot.number == 0,
				   "GUI deployment publishes selected PAK", status->text());
			slot->setValue(-1);
			review->click();
			expect(until([&] { return dialog.reviewReady(); }), "repeat automatic review");
			expect(output->text().endsWith("pak0.pak") && !deploy->isEnabled(), "automatic reuse requires explicit replacement");
			dialog.findChild<QCheckBox*>("buildPackageOverwrite")->setChecked(true);
			deploy->click();
			expect(until([&] { return !dialog.busy(); }), "repeat deployment completes");
			expect(dialog.lastDeploymentResult().succeeded() && QFileInfo::exists(output->text() + ".bak"), "repeat PAK preserves backup",
				   status->text());
			dialog.reject();
			if (scale == 200) {
				app.removeTranslator(&expansion);
			}
		}
		settings.setTheme(StudioTheme::Dark);
		settings.setTextScalePercent(100);
		applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
		settings.upsertGameInstallation(installation);
		settings.setSelectedGameInstallation(installation.id);
		settings.setLaunchGameDirectory(installation.id, "studio_shell");
		for (const auto& compiler : build.executableOverrides) {
			settings.upsertCompilerToolPathOverride(compiler);
		}
		const auto source = QDir(root).filePath("live.map");
		subset_test::put(source, serializeLevelMap(map).bytes);
		ApplicationShell shell;
		shell.openPathFromCommandLine(QDir(root).filePath("assets"));
		shell.openPathFromCommandLine(source);
		expect(until([&] { return shell.levelDocument().sourcePath == source; }), "shell loads classic map");
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
					dialog->setDestination(QDir(root).filePath("shell-build"), "shell_build");
					dialog->findChild<QPushButton*>("levelBuildPrepare")->click();
				} else if (dialog->isReady()) {
					expect(dialog->workspace().target == target, "shell retains captured target");
					dialog->accept();
					accepted = dialog->result() == QDialog::Accepted;
					drive.stop();
				}
			} else if (modal->objectName() == "levelBuildPackageDialog") {
				auto* dialog = static_cast<LevelBuildPackageDialog*>(modal);
				if (dialog->reviewReady() && !started) {
					started = true;
					expect(dialog->findChild<QCheckBox*>("buildDeploymentLaunch")->isChecked(), "shell launch requires deployment review");
					dialog->findChild<QCheckBox*>("buildDeploymentAllow")->setChecked(true);
					dialog->findChild<QPushButton*>("buildPackagePublish")->click();
				} else if (started && !dialog->busy()) {
					deployed = dialog->lastDeploymentResult().succeeded() && dialog->lastDeploymentResult().launched;
					expect(deployed, "shell deployment succeeds", dialog->lastDeploymentResult().error);
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
		expect(accepted, "shell accepts prepared workspace");
		auto* buildLaunch = shell.findChild<QAction*>("build.runAndLaunch");
		expect(buildLaunch->isEnabled() && shell.findChild<QAction*>("build.deployPrepared")->isEnabled(),
			   "classic installation enables Build and Launch");
		started = false;
		drive.start();
		watchdog.start(60000);
		buildLaunch->trigger();
		expect(until([&] { return deployed; }), "classic shell build-deploy-launch chain");
		drive.stop();
		watchdog.stop();
		const auto recordPath = QDir(installation.rootPath).filePath("classic-launch-record.json");
		expect(until([&] {
				   const auto record = QJsonDocument::fromJson(subset_test::get(recordPath)).object();
				   return record.value("validPackageAtLaunch").toBool() &&
						  record.value("package").toString().contains("studio_shell/pak0.pak");
			   }),
			   "shell recorder finds complete numbered PAK");
		shell.close();
	}
	expect(heartbeats > 20, "UI event loop remains live through package work");
	return ok ? 0 : 1;
}
