#include "app/application_shell.h"
#include "app/level_build_package_dialog.h"
#include "app/level_build_workspace_dialog.h"
#include "app/studio_theme.h"
#include "app/ui_primitives.h"
#include "core/package_draft.h"
#include "tests/level_build_engines_test_helpers.h"
#include "tests/level_material_test_helpers.h"
#include "tests/package_subset_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* label, const QString& error = {}) {
	if (!value) {
		ok = false;
		std::cerr << label << ": " << error.toStdString() << '\n';
	}
	return value;
}
bool until(const std::function<bool()>& ready) {
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 45000) {
		QEventLoop loop;
		QTimer::singleShot(20, &loop, &QEventLoop::quit);
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
	// Semantic widget calls and QWidget::render only; never native user input or screen capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	settings.setReducedMotion(true);
	QString error;
	LevelMapDocument map;
	if (!expect(tests::createMaterialFixture(temp.path(), &map, &error), "fixture", error)) {
		return 1;
	}
	const auto source = temp.filePath("original.map");
	tests::putMaterialFile(source, serializeLevelMap(map).bytes);
	PackageArchive archive;
	expect(archive.load(temp.filePath("assets"), &error), "assets", error);
	PackageStagingModel staging;
	expect(staging.loadBaseArchive(archive, &error), "staging", error);
	const auto replacement = tests::materialImage(128, 64, true);
	expect(staging.addBytes(replacement, "textures/studio/grid.png", &error, PackageStageConflictResolution::ReplaceExisting),
		   "edited texture", error);
	const auto draft = temp.filePath("assets.vibepackage");
	expect(PackageDraft::save(draft, &staging, false, &error), "save draft", error);
	const auto snapshot = std::make_shared<PackageStagingArchive>(staging);
	for (int scale : {100, 200}) {
		const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
		settings.setTheme(theme);
		settings.setTextScalePercent(scale);
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
		Expansion expansion;
		if (scale == 200) {
			app.installTranslator(&expansion);
		}
		LevelBuildWorkspaceDialog dialog(map, snapshot);
		if (scale == 200) {
			dialog.setLayoutDirection(Qt::RightToLeft);
			dialog.resize(1800, 1200);
		}
		dialog.setDestination(temp.filePath(QStringLiteral("scale-%1").arg(scale)), "studio_build");
		dialog.show();
		auto* prepare = dialog.findChild<QPushButton*>("levelBuildPrepare");
		auto* use = dialog.findChild<QPushButton*>("levelBuildUse");
		auto* name = dialog.findChild<QLineEdit*>("levelBuildMapName");
		expect(name && name->layoutDirection() == Qt::LeftToRight && name->focusPolicy() != Qt::NoFocus &&
				   !name->accessibleDescription().isEmpty() &&
				   QAccessible::queryAccessibleInterface(name)->role() == QAccessible::EditableText,
			   "accessible keyboard-focusable name control");
		prepare->click();
		expect(!use->isEnabled() && until([&] { return dialog.isReady() || prepare->isEnabled(); }) && dialog.isReady(),
			   "worker preparation", dialog.findChild<QLabel*>("levelBuildStatus")->text());
		expect(subset_test::get(QDir(dialog.workspace().assetsPath()).filePath("textures/studio/grid.png")) == replacement,
			   "GUI captures package draft edits");
		expect(dialog.findChild<QScrollArea*>()->horizontalScrollBar()->maximum() == 0, "expanded form fits without horizontal scrolling");
		const auto capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!capture.isEmpty()) {
			QDir().mkpath(capture);
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			dialog.render(&image);
			expect(image.save(QDir(capture).filePath(QStringLiteral("build-workspace-%1.png").arg(scale))), "widget render");
		}
		dialog.setApplyHandler([](const auto&, QString* reason) {
			*reason = "stale input";
			return false;
		});
		dialog.accept();
		expect(dialog.result() != QDialog::Accepted && dialog.isVisible(), "rejected handoff stays visible");
		dialog.setApplyHandler({});
		dialog.accept();
		expect(dialog.result() == QDialog::Accepted, "prepared workspace accepted");
		for (const auto& target : QStringList{"quake", "quake2"}) {
			const auto root = temp.filePath(QStringLiteral("%1-%2").arg(target).arg(scale));
			QDir().mkpath(root);
			LevelMapDocument classic;
			auto assets = std::make_shared<PackageArchive>();
			expect(tests::buildEngineFixture(root, target, &classic, assets.get(), &error), "classic GUI fixture", error);
			LevelBuildWorkspaceDialog prepareDialog(classic, assets);
			prepareDialog.setDestination(QDir(root).filePath("prepared"), "studio_build");
			prepareDialog.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
			if (scale == 200) {
				prepareDialog.resize(1800, 1200);
			}
			prepareDialog.show();
			auto* targetChoice = prepareDialog.findChild<QComboBox*>("levelBuildTarget");
			expect(targetChoice && targetChoice->currentData().toString() == target && targetChoice->count() == 2 &&
					   QAccessible::queryAccessibleInterface(targetChoice)->role() == QAccessible::ComboBox &&
					   targetChoice->focusPolicy() != Qt::NoFocus,
				   "target honors saved game and exposes accessible keyboard choice");
			prepareDialog.findChild<QPushButton*>("levelBuildPrepare")->click();
			expect(
				until([&] { return prepareDialog.isReady() || prepareDialog.findChild<QPushButton*>("levelBuildPrepare")->isEnabled(); }) &&
					prepareDialog.isReady(),
				"GUI classic preparation", prepareDialog.findChild<QLabel*>("levelBuildStatus")->text());
			if (!prepareDialog.isReady() || argc != 2) {
				ok = false;
				continue;
			}
			const auto preparedClassic = prepareDialog.workspace();
			expect(preparedClassic.target == target, "GUI target handoff");
			BuildPipelineRequest build;
			build.pipelineId = preparedClassic.defaultPipeline();
			for (const auto& id : QStringList{"ericw-qbsp", "ericw-vis", "ericw-light"}) {
				build.executableOverrides.append({id, QString::fromLocal8Bit(argv[1])});
			}
			if (target == "quake") {
				build.stageExtraArguments["light"] = {"-lit"};
			}
			const auto compiled = runLevelBuildWorkspace(preparedClassic, build);
			expect(compiled.succeeded(), "GUI fixture compiler", compiled.errors.join('\n'));
			LevelBuildPackageDialog publisher(preparedClassic);
			publisher.setLayoutDirection(prepareDialog.layoutDirection());
			if (scale == 200) {
				publisher.resize(1800, 1200);
			}
			publisher.setOutputPath(QDir(root).filePath("pak0.pak"));
			publisher.show();
			publisher.findChild<QPushButton*>("buildPackageReview")->click();
			expect(until([&] { return publisher.reviewReady(); }), "GUI PAK review",
				   publisher.findChild<QLabel*>("buildPackageStatus")->text());
			expect(publisher.findChild<QPushButton*>("buildPackagePublish")->text().contains("PAK") &&
					   !publisher.findChild<QComboBox*>("buildPackageCompression")->isEnabled(),
				   "PAK publication controls");
			auto* files = publisher.findChild<QTableWidget*>("buildPackageFiles");
			bool hasLit = false;
			for (int row = 0; row < files->rowCount(); ++row) {
				hasLit |= files->item(row, 0)->text() == "maps/studio_build.lit";
			}
			expect(hasLit == (target == "quake"), "review shows runtime colored lighting");
			auto* details = publisher.findChild<QPlainTextEdit*>("buildPackageDetails");
			auto* detailsToggle = publisher.findChild<QPushButton*>("buildPackageDetailsToggle");
			expect(details && detailsToggle && details->isHidden() && detailsToggle->isEnabled() && detailsToggle->isCheckable() &&
					   detailsToggle->focusPolicy() != Qt::NoFocus &&
					   publisher.findChild<QLabel*>("buildPackageStatus")->text().count('\n') == 0,
				   "retained warnings do not displace the package file review");
			detailsToggle->click();
			expect(details->isVisible() && details->isReadOnly() && details->focusPolicy() != Qt::NoFocus &&
					   !details->accessibleName().isEmpty() &&
					   details->toPlainText().contains(inspectLevelBuildArtifacts(preparedClassic).runId) &&
					   details->maximumHeight() <= publisher.fontMetrics().lineSpacing() * 8,
				   "complete review remains selectable in bounded keyboard-accessible details");
			detailsToggle->click();
			app.processEvents();
			if (!capture.isEmpty()) {
				for (auto* widget : QVector<QWidget*>{&prepareDialog, &publisher}) {
					QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied);
					image.fill(Qt::transparent);
					widget->render(&image);
					expect(image.save(QDir(capture).filePath(QStringLiteral("%1-%2-%3.png").arg(target, widget->objectName()).arg(scale))),
						   "classic widget render");
				}
			}
			bool published = false;
			publisher.setPublishedHandler([&](const auto& result) { published = result.succeeded(); });
			publisher.findChild<QPushButton*>("buildPackagePublish")->click();
			expect(until([&] { return published; }), "GUI PAK publication");
			targetChoice->setCurrentIndex(targetChoice->currentIndex() == 0 ? 1 : 0);
			expect(!prepareDialog.isReady(), "changing target invalidates captured handoff");
			prepareDialog.reject();
			publisher.reject();
		}
		if (scale == 200) {
			app.removeTranslator(&expansion);
		}
	}
	settings.setTheme(StudioTheme::Dark);
	settings.setTextScalePercent(100);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ApplicationShell shell;
	shell.openPathFromCommandLine(draft);
	shell.openPathFromCommandLine(source);
	expect(until([&] { return shell.levelDocument().sourcePath == source; }), "load shell map");
	auto changed = shell.levelDocument();
	selectLevelMapObject(&changed, "brush:0");
	expect(moveLevelMapSelection(&changed, 8, 0, 0, {true, false}, &error), "prepare unsaved brush edit", error);
	expect(shell.applyLevelBrushGeometry(changed.brushes[0], changed.brushes[0].id, &error), "unsaved shell edit", error);
	const auto current = serializeLevelMap(shell.levelDocument()).bytes, original = subset_test::get(source);
	const auto revision = shell.levelDocument().revision;
	QTimer drive, watchdog;
	drive.setInterval(20);
	watchdog.setSingleShot(true);
	bool started = false, accepted = false, stale = false;
	int phase = 0;
	QObject::connect(&drive, &QTimer::timeout, &shell, [&] {
		auto* modal = QApplication::activeModalWidget();
		if (!modal || modal->objectName() != "levelBuildWorkspaceDialog") {
			return;
		}
		auto* dialog = static_cast<LevelBuildWorkspaceDialog*>(modal);
		if (!started) {
			started = true;
			dialog->setDestination(temp.filePath(phase == 0 ? "shell-build" : "stale-build"), "shell_build");
			dialog->findChild<QPushButton*>("levelBuildPrepare")->click();
			return;
		}
		if (!dialog->isReady()) {
			return;
		}
		if (phase == 1) {
			// Selection-independent, real document mutation while the dialog is open.
			auto edit = shell.levelDocument();
			selectLevelMapObject(&edit, "brush:0");
			moveLevelMapSelection(&edit, 8, 0, 0, {true, false});
			shell.applyLevelBrushGeometry(edit.brushes[0], edit.brushes[0].id, &error);
		}
		dialog->accept();
		accepted = dialog->result() == QDialog::Accepted;
		stale = !accepted && dialog->findChild<QLabel*>("levelBuildStatus")->text().contains("changed");
		if (!accepted) {
			dialog->reject();
		}
		drive.stop();
	});
	QObject::connect(&watchdog, &QTimer::timeout, &shell, [&] {
		if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
			modal->reject();
		}
	});
	const auto trigger = [&] {
		auto* action = shell.findChild<QAction*>("build.prepareWorkspace");
		if (!expect(action && action->isEnabled(), "shell prepare action enabled")) {
			return;
		}
		started = false;
		drive.start();
		watchdog.start(60000);
		action->trigger();
		drive.stop();
		watchdog.stop();
	};
	trigger();
	const auto prepared = readLevelBuildWorkspace(temp.filePath("shell-build"));
	expect(accepted && prepared.ready && subset_test::get(prepared.inputPath()) == current, "actual shell current-map handoff",
		   prepared.error);
	expect(shell.levelDocument().revision == revision && subset_test::get(source) == original, "handoff preserves unsaved map and source");
	expect(shell.findChild<QLineEdit*>("buildPipelineInput")->text() == prepared.inputPath(), "Build input pinned to snapshot");
	expect(!shell.findChild<QAction*>("build.runAndLaunch")->isEnabled(), "incomplete asset deployment cannot auto-launch");
	expect(shell.findChild<QAction*>("build.openWorkspaceAssets")->isEnabled(), "captured assets have an explicit package handoff");
	const auto inputBytes = subset_test::get(prepared.inputPath());
	tests::putMaterialFile(prepared.inputPath(), inputBytes + "\n");
	shell.findChild<QAction*>("build.runPipeline")->trigger();
	expect(until([&] { return shell.findChild<QAction*>("build.runPipeline")->isEnabled(); }), "build verification worker finishes");
	bool failed = false;
	for (auto* widget : shell.findChildren<QWidget*>()) {
		if (widget->accessibleName() == "Build pipeline state") {
			const auto* pane = dynamic_cast<LoadingPane*>(widget);
			failed = pane && pane->state() == OperationState::Failed;
		}
	}
	expect(failed, "pre-stage verification failure is a failed build in the actual shell");
	tests::putMaterialFile(prepared.inputPath(), inputBytes);
	phase = 1;
	trigger();
	expect(stale, "actual shell rejects changed document");
	expect(shell.findChild<QLineEdit*>("buildPipelineInput")->text() == prepared.inputPath(), "stale handoff preserves prior Build input");
	expect(shell.saveLevelDocument(temp.filePath("edited.map"), false, &error), "save test document before closing", error);
	shell.findChild<QAction*>("build.openWorkspaceAssets")->trigger();
	expect(QDir::cleanPath(settings.recentFiles("package").value(0)) == QDir::cleanPath(prepared.assetsPath()),
		   "actual prepared asset folder opens through the package service");
	shell.close();
	auto* cancelled = new LevelBuildWorkspaceDialog(map, snapshot);
	cancelled->setDestination(temp.filePath("cancelled"), "cancelled");
	cancelled->findChild<QPushButton*>("levelBuildPrepare")->click();
	QElapsedTimer close;
	close.start();
	cancelled->reject();
	delete cancelled;
	expect(close.elapsed() < 250, "close returns without waiting for worker");
	QEventLoop drain;
	QTimer::singleShot(500, &drain, &QEventLoop::quit);
	drain.exec();
	expect(!QFileInfo::exists(temp.filePath("cancelled")), "cancel before publication leaves no final workspace");
	return ok ? 0 : 1;
}
