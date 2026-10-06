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
	while (!ready() && timer.elapsed() < 45000) {
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
	// Semantic Qt calls and QWidget::render only. No native input or screen capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	if (argc != 2) {
		return 1;
	}
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	settings.setReducedMotion(true);
	settings.upsertCompilerToolPathOverride({"q3map2", QString::fromLocal8Bit(argv[1])});
	QString error;
	LevelMapDocument map;
	if (!expect(tests::createMaterialFixture(temp.path(), &map, &error), "fixture", error)) {
		return 1;
	}
	for (int at = 0; at < 305; ++at) {
		tests::putMaterialFile(temp.filePath(QStringLiteral("assets/notes/asset-%1.txt").arg(at)), "original test content");
	}
	tests::putMaterialFile(temp.filePath("assets/notes/large.bin"), QByteArray(2 * 1024 * 1024, 'x'));
	const auto source = temp.filePath("original.map");
	tests::putMaterialFile(source, serializeLevelMap(map).bytes);
	PackageArchive archive;
	expect(archive.load(temp.filePath("assets"), &error), "assets", error);
	LevelBuildWorkspaceRequest prepare;
	prepare.directory = temp.filePath("prepared");
	const auto workspace = prepareLevelBuildWorkspace(map, archive, prepare);
	BuildPipelineRequest request;
	request.pipelineId = "quake3-full";
	request.executableOverrides = settings.compilerToolPathOverrides();
	const auto build = runLevelBuildWorkspace(workspace, request);
	if (!expect(build.succeeded(), "fixture compiler", build.errors.join('\n'))) {
		return 1;
	}
	int heartbeats = 0;
	QTimer heartbeat;
	heartbeat.setInterval(1);
	QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++heartbeats; });
	heartbeat.start();
	for (int scale : {100, 200}) {
		const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
		settings.setTheme(theme);
		settings.setTextScalePercent(scale);
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
		Expansion expansion;
		if (scale == 200) {
			app.installTranslator(&expansion);
		}
		LevelBuildPackageDialog dialog(workspace);
		if (scale == 200) {
			dialog.setLayoutDirection(Qt::RightToLeft);
			dialog.resize(1800, 1200);
		}
		const auto output = temp.filePath(QStringLiteral("published-%1.pk3").arg(scale));
		dialog.setOutputPath(output);
		dialog.show();
		auto* publish = dialog.findChild<QPushButton*>("buildPackagePublish");
		auto* status = dialog.findChild<QLabel*>("buildPackageStatus");
		auto* path = dialog.findChild<QLineEdit*>("buildPackageOutput");
		auto* table = dialog.findChild<QTableWidget*>("buildPackageFiles");
		auto* page = dialog.findChild<QSpinBox*>("buildPackagePage");
		expect(!publish->isEnabled(), "publication waits for review");
		expect(path->layoutDirection() == Qt::LeftToRight && path->focusPolicy() != Qt::NoFocus &&
				   !path->accessibleDescription().isEmpty() &&
				   QAccessible::queryAccessibleInterface(path)->role() == QAccessible::EditableText,
			   "accessible destination retains path direction");
		expect(until([&] { return dialog.reviewReady(); }), "asynchronous review", status->text());
		expect(table->rowCount() == 300 && page->maximum() == 2 && table->focusPolicy() != Qt::NoFocus &&
				   QAccessible::queryAccessibleInterface(table)->role() == QAccessible::Table,
			   "bounded accessible file review");
		page->setValue(2);
		const auto finalRows = table->rowCount();
		dialog.findChild<QCheckBox*>("buildPackageSource")->setChecked(true);
		expect(table->rowCount() == finalRows + 1, "source option updates reviewed payload");
		page->setValue(1);
		expect(dialog.findChild<QScrollArea*>()->horizontalScrollBar()->maximum() == 0, "expanded RTL form fits width");
		int callbacks = 0;
		dialog.setPublishedHandler([&](const auto&) { ++callbacks; });
		publish->click();
		expect(!publish->isEnabled() && until([&] { return !dialog.busy(); }) && dialog.lastResult().succeeded() && callbacks == 1,
			   "asynchronous publication completes once", status->text());
		PackageArchive package;
		QByteArray bytes;
		expect(package.load(output, &error) && package.readEntryBytes("maps/studio_build.map", &bytes, &error) &&
				   bytes == subset_test::get(workspace.inputPath()),
			   "published source matches captured bytes", error);
		expect(dialog.lastResult().paths.contains("scripts/q3map2_studio_build.shader") &&
				   !dialog.lastResult().paths.contains("maps/studio_build.prt"),
			   "runtime-only generated payload");
		const auto before = subset_test::get(output);
		publish->click();
		expect(until([&] { return !dialog.busy(); }) && !dialog.lastResult().succeeded() && subset_test::get(output) == before &&
				   callbacks == 1,
			   "existing output refused without overwrite");
		dialog.findChild<QCheckBox*>("buildPackageOverwrite")->setChecked(true);
		publish->click();
		expect(until([&] { return !dialog.busy(); }) && dialog.lastResult().succeeded() && subset_test::get(output + ".bak") == before &&
				   status->text().contains(output + ".bak"),
			   "replacement exposes preserved backup", status->text());
		const auto capture = qEnvironmentVariable("VIBESTUDIO_CAPTURE_DIR");
		if (!capture.isEmpty()) {
			QDir().mkpath(capture);
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			dialog.render(&image);
			expect(image.save(QDir(capture).filePath(QStringLiteral("build-package-%1.png").arg(scale))), "widget render");
		}
		dialog.reject();
		if (scale == 200) {
			app.removeTranslator(&expansion);
		}
	}
	settings.setTheme(StudioTheme::Dark);
	settings.setTextScalePercent(100);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	LevelBuildPackageDialog stale(workspace);
	stale.setOutputPath(temp.filePath("stale.pk3"));
	stale.show();
	expect(until([&] { return stale.reviewReady(); }), "initial stale review");
	expect(runLevelBuildWorkspace(workspace, request).succeeded(), "new build replaces review");
	stale.findChild<QPushButton*>("buildPackagePublish")->click();
	expect(until([&] { return !stale.busy(); }) && !stale.lastResult().succeeded() && stale.lastResult().error.contains("since review") &&
			   !QFileInfo::exists(temp.filePath("stale.pk3")),
		   "stale UI review cannot publish");
	stale.findChild<QPushButton*>("buildPackageReview")->click();
	expect(until([&] { return stale.reviewReady(); }), "explicit refreshed review");
	stale.findChild<QPushButton*>("buildPackagePublish")->click();
	stale.reject();
	expect(until([&] { return !stale.busy(); }) && stale.lastResult().cancelled && !QFileInfo::exists(temp.filePath("stale.pk3")),
		   "cancel acknowledged without destination change");
	stale.reject();
	// Exercise the real shell command chain, including modal review and publication.
	ApplicationShell shell;
	shell.openPathFromCommandLine(temp.filePath("assets"));
	shell.openPathFromCommandLine(source);
	expect(until([&] { return shell.levelDocument().sourcePath == source; }), "shell map loaded");
	QTimer drive, watchdog;
	drive.setInterval(10);
	watchdog.setSingleShot(true);
	bool started = false, accepted = false, published = false;
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
				dialog->setOutputPath(temp.filePath("shell.pk3"));
				dialog->findChild<QPushButton*>("buildPackagePublish")->click();
			} else if (started && !dialog->busy()) {
				published = dialog->lastResult().succeeded();
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
	expect(accepted, "shell adopts prepared workspace");
	shell.findChild<QAction*>("build.runPipeline")->trigger();
	expect(until([&] { return shell.findChild<QAction*>("build.runPipeline")->isEnabled(); }), "shell compiler finishes");
	const auto built = readLevelBuildWorkspace(temp.filePath("shell-build"));
	expect(inspectLevelBuildArtifacts(built).verified, "shell records successful outputs");
	started = false;
	drive.start();
	watchdog.start(60000);
	auto* publish = shell.findChild<QAction*>("build.publishPrepared");
	expect(publish && publish->isEnabled(), "shell publication command enabled");
	if (publish) {
		publish->trigger();
	}
	drive.stop();
	watchdog.stop();
	expect(published && QFileInfo::exists(temp.filePath("shell.pk3")), "actual shell build-to-PK3 handoff");
	expect(!shell.findChild<QAction*>("build.runAndLaunch")->isEnabled(), "full asset deployment remains an explicit gate");
	shell.close();
	auto* closing = new LevelBuildPackageDialog(workspace);
	closing->show();
	QApplication::processEvents();
	QElapsedTimer close;
	close.start();
	delete closing;
	expect(close.elapsed() < 250, "destruction never waits for hashing worker");
	QEventLoop drain;
	QTimer::singleShot(500, &drain, &QEventLoop::quit);
	drain.exec();
	expect(heartbeats > 10, "UI event loop remains live during worker operations");
	return ok ? 0 : 1;
}
