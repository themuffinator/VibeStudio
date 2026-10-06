#include "app/application_shell.h"
#include "app/level_placement_task_dialog.h"
#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/level_placement_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <algorithm>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* message, const QString& error = {}) {
	if (!value) {
		std::cerr << message << ": " << error.toStdString() << '\n';
		ok = false;
	}
	return value;
}
class Expansion final : public QTranslator {
  public:
	QString translate(const char*, const char* text, const char*, int) const override {
		const auto source = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(source, QString(source.size() / 3, QLatin1Char('~')));
	}
};
void selectAll(LevelMapDocument* doc) {
	QVector<LevelMapSelectionRef> refs;
	for (const auto& b : doc->brushes) {
		refs << LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush, b.id};
	}
	setLevelMapSelection(doc, refs);
}
} // namespace
int main(int argc, char** argv) {
	// Semantic Qt APIs and QWidget::render only; this offscreen clipboard is
	// process-local. No native input injection, capture or game launch.
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
	settings.setRestoreSession(false);
	LevelMapDocument large;
	QString error;
	if (!tests::createGeometryFixture(10000, &large, &error)) {
		return 1;
	}
	selectAll(&large);
	LevelPlacementRequest duplicate;
	duplicate.offset = {16, 0, 0, true};
	for (int scale : {100, 200}) {
		settings.setTextScalePercent(scale);
		settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, scale));
		Expansion expanded;
		if (scale == 200) {
			app.installTranslator(&expanded);
			app.setLayoutDirection(Qt::RightToLeft);
		}
		bool observed = false;
		int beats = 0;
		qint64 maximumGap = 0, previous = 0;
		QElapsedTimer elapsed, cancelling;
		elapsed.start();
		QTimer heartbeat;
		heartbeat.setInterval(5);
		QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] {
			const auto now = elapsed.elapsed();
			maximumGap = std::max(maximumGap, now - previous);
			previous = now;
			++beats;
		});
		QTimer drive;
		drive.setInterval(10);
		QObject::connect(&drive, &QTimer::timeout, &app, [&] {
			auto* dialog = QApplication::activeModalWidget();
			if (!dialog || dialog->objectName() != QStringLiteral("levelPlacementTaskDialog") || observed) {
				return;
			}
			observed = true;
			auto* phase = dialog->findChild<QLabel*>("levelPlacementTaskPhase");
			auto* progress = dialog->findChild<QProgressBar*>("levelPlacementTaskProgress");
			auto* cancel = dialog->findChild<QPushButton*>("levelPlacementTaskCancel");
			expect(phase && progress && cancel && !phase->text().isEmpty(), "visible progress state");
			expect(progress && QAccessible::queryAccessibleInterface(progress)->role() == QAccessible::ProgressBar,
				   "accessible progress role");
			expect(cancel && !cancel->accessibleDescription().isEmpty() && cancel->focusPolicy() != Qt::NoFocus,
				   "accessible cancel control");
			const auto capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
			if (!capture.isEmpty()) {
				QDir().mkpath(capture);
				QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied);
				image.fill(Qt::transparent);
				dialog->render(&image);
				expect(image.save(QDir(capture).filePath(QStringLiteral("placement-job-%1.png").arg(scale))), "render task dialog");
			}
			cancelling.start();
			cancel->click();
		});
		heartbeat.start();
		drive.start();
		const auto result = LevelPlacementTaskDialog::prepare(nullptr, large, duplicate);
		heartbeat.stop();
		drive.stop();
		expect(observed && result.cancelled && !result.succeeded && result.document.format == LevelMapFormat::Unknown,
			   "cancel private worker result", result.error);
		expect(beats >= 2 && maximumGap < 1000, "GUI heartbeat continues during preparation");
		expect(cancelling.isValid() && cancelling.elapsed() < 1000, "cancel returns without waiting for worker");
		std::cout << scale << "%: " << beats << " heartbeats, max gap " << maximumGap << " ms, cancel return " << cancelling.elapsed()
				  << " ms\n";
		if (scale == 200) {
			app.removeTranslator(&expanded);
			app.setLayoutDirection(Qt::LeftToRight);
		}
	}
	settings.setTextScalePercent(100);
	settings.setTheme(StudioTheme::Dark);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	LevelMapDocument small;
	tests::createGeometryFixture(4, &small, &error);
	selectAll(&small);
	expect(moveLevelMapSelection(&small, 3, 3, 3, &error), "small off-grid source", error);
	const auto original = serializeLevelMap(small).bytes;
	const auto ready = LevelPlacementTaskDialog::prepare(nullptr, small, duplicate);
	expect(ready.succeeded && ready.document.brushes.size() == 8 && serializeLevelMap(small).bytes == original,
		   "completed private candidate", ready.error);
	ApplicationShell shell;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	expect(shell.createLevelDocument(create, &error), "new shell map", error);
	app.clipboard()->setText(levelMapSelectionText(small));
	const auto trigger = [&](const QString& id) {
		auto* action = shell.findChild<QAction*>(id);
		if (expect(action && action->isEnabled(), "quick action enabled", id)) {
			action->trigger();
		}
	};
	trigger("map.paste");
	expect(shell.levelDocument().brushes.size() == 4 && shell.levelDocument().undoStack.size() == 1, "quick paste publishes once",
		   shell.statusBar()->currentMessage());
	auto* plan = shell.findChild<MapViewport*>("mapViewport");
	auto* grid = shell.findChild<QComboBox*>("levelMapGrid");
	grid->setCurrentIndex(grid->findData(16));
	plan->selectionSetChanged(shell.levelDocument().selection);
	const auto pasted = serializeLevelMap(shell.levelDocument()).bytes;
	trigger("map.duplicateSelection");
	expect(shell.levelDocument().brushes.size() == 8 &&
			   tests::placementUvsMatch(small.brushes.first(), shell.levelDocument().brushes[4], {16, -16, 0, true}),
		   "quick duplicate preserves projection/UVs", shell.statusBar()->currentMessage());
	trigger("map.undo");
	expect(serializeLevelMap(shell.levelDocument()).bytes == pasted, "quick duplicate exact undo");
	trigger("map.snapToGrid");
	expect(shell.levelDocument().brushes.first().mins.x == -16 && shell.levelDocument().brushes.first().mins.y == -16,
		   "quick snap commits aligned candidate", shell.statusBar()->currentMessage());
	trigger("map.undo");
	expect(serializeLevelMap(shell.levelDocument()).bytes == pasted, "quick snap exact undo");
	// A queued scene change while the worker starts must invalidate publication,
	// including during the brief delay before a progress window appears.
	expect(shell.saveLevelDocument(temp.filePath("placed.map"), false, &error), "save before stale replacement", error);
	QTimer::singleShot(0, &shell, [&] { shell.createLevelDocument(create, &error); });
	trigger("map.duplicateSelection");
	expect(shell.levelDocument().brushes.isEmpty() && shell.statusBar()->currentMessage().contains("changed"),
		   "stale quick candidate refused", shell.statusBar()->currentMessage());
	expect(shell.saveLevelDocument(temp.filePath("empty.map"), false, &error), "save replacement before closing", error);
	shell.close();
	QEventLoop drain;
	QTimer::singleShot(250, &drain, &QEventLoop::quit);
	drain.exec();
	return ok ? 0 : 1;
}
