#include "app/application_shell.h"
#include "app/level_placement_dialog.h"
#include "app/studio_theme.h"
#include "tests/level_material_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
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
	QElapsedTimer t;
	t.start();
	while (!ready() && t.elapsed() < 45000) {
		QEventLoop events;
		QTimer::singleShot(20, &events, &QEventLoop::quit);
		events.exec();
	}
	return ready();
}
class Expansion final : public QTranslator {
  public:
	QString translate(const char*, const char* source, const char*, int) const override {
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};
} // namespace
int main(int argc, char** argv) {
	// Semantic Qt calls and QWidget::render only. This platform's clipboard is
	// process-local; no native keyboard, mouse, clipboard or screen capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	settings.setReducedMotion(true);
	QString error;
	LevelMapDocument source;
	if (!expect(tests::createMaterialFixture(temp.path(), &source, &error), "asset fixture", error)) {
		return 1;
	}
	QVector<LevelMapSelectionRef> selection;
	for (const auto& b : source.brushes) {
		selection << LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush, b.id};
	}
	for (const auto& p : source.patches) {
		selection << LevelMapSelectionRef{LevelMapSelectionKind::QuakePatch, p.id};
	}
	selection << LevelMapSelectionRef{LevelMapSelectionKind::Entity, source.entities.last().id};
	setLevelMapSelection(&source, selection);
	const auto text = levelMapSelectionText(source, &error);
	const auto original = serializeLevelMap(source).bytes;
	PackageArchive archive;
	expect(archive.load(temp.filePath("assets"), &error), "load asset folder", error);
	PackageStagingModel staging;
	expect(staging.loadBaseArchive(archive, &error), "package draft", error);
	expect(staging.addBytes(tests::materialImage(128, 64, true), "textures/studio/grid.png", &error,
							PackageStageConflictResolution::ReplaceExisting),
		   "staged texture", error);
	for (int scale : {100, 200}) {
		settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
		settings.setTextScalePercent(scale);
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, scale));
		Expansion expansion;
		if (scale == 200) {
			app.installTranslator(&expansion);
		}
		LevelPlacementDialog dialog(source, LevelPlacementMode::Duplicate, {}, std::make_shared<PackageStagingArchive>(staging));
		if (scale == 200) {
			dialog.setLayoutDirection(Qt::RightToLeft);
			dialog.resize(1800, 1200);
		}
		dialog.setOffset({64, 0, 0, true});
		dialog.show();
		expect(until([&] { return dialog.isReady(); }) && dialog.previewValid(), "package placement preview",
			   dialog.findChild<QLabel*>("placementStatus")->text());
		auto* preview = dialog.findChild<ModelViewport*>();
		expect(preview->hasMesh() && preview->hasSkin() && preview->reducedMotion() &&
				   preview->renderMode() == ModelViewportRenderMode::Textured,
			   "staged materials and model renderer");
		const auto* stagedMaterial = tests::materialNamed(dialog.previewAssets(), QStringLiteral("studio/grid"));
		expect(stagedMaterial && stagedMaterial->sourceLayer == QStringLiteral("generated") &&
				   stagedMaterial->image.pixelColor(8, 0) == QColor("#d65c7c"),
			   "preview resolves staged bytes rather than base package image");
		expect(dialog.previewDocument().brushes.size() == source.brushes.size() * 2 && serializeLevelMap(source).bytes == original,
			   "immutable candidate");
		auto* coordinate = dialog.findChild<QDoubleSpinBox*>("placementOffset0");
		expect(coordinate->layoutDirection() == Qt::LeftToRight && !coordinate->accessibleDescription().isEmpty() &&
				   coordinate->focusPolicy() != Qt::NoFocus &&
				   QAccessible::queryAccessibleInterface(coordinate)->role() == QAccessible::SpinBox,
			   "accessible coordinate control");
		expect(dialog.findChild<QScrollArea*>()->horizontalScrollBar()->maximum() == 0, "expanded controls fit");
		auto* copies = dialog.findChild<QSpinBox*>("placementCopies");
		expect(copies && copies->focusPolicy() != Qt::NoFocus && !copies->accessibleDescription().isEmpty() &&
			copies->layoutDirection() == Qt::LeftToRight && copies->maximum() == kLevelMapMaxArrayCopies,
			"accessible bounded copy-count control");
		dialog.setCopies(3);
		expect(until([&] { return dialog.isReady(); }) && dialog.previewValid() &&
			dialog.previewDocument().brushes.size() == source.brushes.size() * 4 &&
			dialog.previewDocument().brushes.last().mins.x == source.brushes.last().mins.x + 192 &&
			dialog.previewDocument().undoStack.size() == source.undoStack.size() + 1,
			"array preview uses cumulative offsets and one undo");
		dialog.setCopies(1);
		expect(until([&] { return dialog.isReady(); }) && dialog.previewValid(), "single duplicate preview restored");
		expect(tests::settleModelViewport(*preview), "render settled");
		const auto capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!capture.isEmpty()) {
			QDir().mkpath(capture);
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			dialog.render(&image);
			expect(image.save(QDir(capture).filePath(QStringLiteral("placement-%1.png").arg(scale))), "save widget render");
		}
		dialog.setOffset({128, 0, 0, true});
		dialog.setOffset({192, 0, 0, true});
		expect(!dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled() &&
				   until([&] { return dialog.isReady(); }) && dialog.previewValid() &&
				   dialog.previewDocument().brushes.last().mins.x == source.brushes.last().mins.x + 192,
			   "latest preview wins");
		dialog.setOffset({65536, 0, 0, true});
		expect(until([&] { return dialog.isReady(); }) && !dialog.previewValid(), "invalid offset cannot apply");
		dialog.setOffset({64, 0, 0, true});
		expect(until([&] { return dialog.isReady(); }) && dialog.previewValid(), "preview recovers");
		dialog.setApplyHandler([](const auto&, QString* e) {
			*e = QStringLiteral("stale document");
			return false;
		});
		dialog.accept();
		expect(dialog.result() != QDialog::Accepted && dialog.isVisible(), "rejected publication stays reviewable");
		bool applied = false;
		dialog.setApplyHandler([&](const auto& candidate, QString*) {
			applied = candidate.undoStack.size() == source.undoStack.size() + 1;
			return applied;
		});
		dialog.accept();
		expect(applied && dialog.result() == QDialog::Accepted, "publish prepared insertion once");
		if (scale == 200) {
			app.removeTranslator(&expansion);
		}
	}
	settings.setTheme(StudioTheme::Dark);
	settings.setTextScalePercent(100);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	LevelMapCreateRequest create;
	create.starterRoom = false;
	ApplicationShell shell;
	expect(shell.createLevelDocument(create, &error), "empty shell map", error);
	const auto empty = serializeLevelMap(shell.levelDocument()).bytes;
	app.clipboard()->setText(text);
	QTimer drive, watchdog;
	drive.setInterval(25);
	watchdog.setSingleShot(true);
	int phase = 0;
	bool accepted = false, stale = false;
	QObject::connect(&drive, &QTimer::timeout, &shell, [&] {
		auto* modal = QApplication::activeModalWidget();
		if (!modal || modal->objectName() != QStringLiteral("levelPlacementDialog")) {
			return;
		}
		auto* dialog = static_cast<LevelPlacementDialog*>(modal);
		if (!dialog->isReady()) {
			return;
		}
		drive.stop();
		if (phase == 2) {
			shell.createLevelDocument(create, &error);
			dialog->accept();
			stale = dialog->result() != QDialog::Accepted && dialog->findChild<QLabel*>("placementStatus")->text().contains("changed");
			dialog->reject();
		} else {
			dialog->accept();
			accepted = dialog->result() == QDialog::Accepted;
		}
	});
	QObject::connect(&watchdog, &QTimer::timeout, &shell, [] {
		if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
			dialog->reject();
		}
	});
	const auto trigger = [&](const QString& id) {
		auto* action = shell.findChild<QAction*>(id);
		if (!expect(action && action->isEnabled(), "shell action enabled", id)) {
			return;
		}
		accepted = false;
		drive.start();
		watchdog.start(60000);
		action->trigger();
		drive.stop();
		watchdog.stop();
	};
	trigger("map.pasteWithOffset");
	expect(accepted && shell.levelDocument().brushes.size() == source.brushes.size() && shell.levelDocument().undoStack.size() == 1,
		   "actual shell paste");
	const auto placed = serializeLevelMap(shell.levelDocument()).bytes;
	shell.findChild<QAction*>("map.undo")->trigger();
	expect(serializeLevelMap(shell.levelDocument()).bytes == empty, "shell paste undo");
	shell.findChild<QAction*>("map.redo")->trigger();
	expect(serializeLevelMap(shell.levelDocument()).bytes == placed, "shell paste redo");
	phase = 1;
	trigger("map.duplicateWithOffset");
	expect(accepted && shell.levelDocument().brushes.size() == source.brushes.size() * 2, "actual shell duplicate");
	shell.findChild<QAction*>("map.undo")->trigger();
	expect(serializeLevelMap(shell.levelDocument()).bytes == placed, "shell copy undo");
	expect(shell.saveLevelDocument(temp.filePath("placed.map"), false, &error), "save before stale-state replacement", error);
	phase = 2;
	trigger("map.duplicateWithOffset");
	expect(stale && shell.levelDocument().brushes.isEmpty(), "actual stale snapshot guard");
	expect(shell.saveLevelDocument(temp.filePath("empty.map"), false, &error), "save replacement", error);
	shell.close();
	auto* cancelled = new LevelPlacementDialog(source, LevelPlacementMode::Paste, text);
	cancelled->show();
	QEventLoop started;
	QTimer::singleShot(125, &started, &QEventLoop::quit);
	started.exec();
	QElapsedTimer closeTime;
	closeTime.start();
	cancelled->reject();
	delete cancelled;
	expect(closeTime.elapsed() < 500, "close does not wait for worker");
	QEventLoop finish;
	QTimer::singleShot(250, &finish, &QEventLoop::quit);
	finish.exec();
	return ok ? 0 : 1;
}
