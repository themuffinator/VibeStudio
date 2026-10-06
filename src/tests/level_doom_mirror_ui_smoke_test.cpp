#include "app/application_shell.h"
#include "core/level_doom_nodes.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_doom_mirror_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMenu>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace d = vibestudio::tests::doom;
namespace f = vibestudio::tests::doomMirror;
namespace {
bool ok = true;
bool expect(bool value, const char* message, const QString& detail = {}) {
	if (!value) {
		ok = false;
		std::cerr << message << ": " << detail.toStdString() << '\n';
	}
	return value;
}
bool until(const std::function<bool()>& ready) {
	QElapsedTimer elapsed;
	elapsed.start();
	while (!ready() && elapsed.elapsed() < 30000) {
		QEventLoop loop;
		QTimer::singleShot(10, &loop, &QEventLoop::quit);
		loop.exec();
	}
	return ready();
}
class Expansion final : public QTranslator {
  public:
	QString translate(const char*, const char* text, const char*, int) const override {
		const auto source = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(source, QString(source.size() / 3, QLatin1Char('~')));
	}
};
void capture(QWidget& widget, const QString& name) {
	const auto path = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
	if (path.isEmpty()) {
		return;
	}
	QDir().mkpath(path);
	QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	widget.render(&image);
	expect(image.save(QDir(path).filePath(name + ".png")), "widget render evidence");
}
} // namespace
int main(int argc, char** argv) {
	// Semantic Qt calls and QWidget::render; no native input or screen capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QTemporaryDir temp;
	QString error;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	settings.setReducedMotion(true);
	settings.setRestoreSession(false);
	const auto path = temp.filePath("mirror.wad");
	expect(d::write(path, f::fixture(true)), "UI fixture");
	const auto assetPath = temp.filePath("assets.wad");
	expect(d::write(assetPath, d::wad(d::assets())), "UI asset package");
	for (int scale : {100, 200}) {
		const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
		settings.setTheme(theme);
		settings.setTextScalePercent(scale);
		settings.sync();
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
		Expansion expanded;
		if (scale == 200) {
			app.installTranslator(&expanded);
			app.setLayoutDirection(Qt::RightToLeft);
		}
		ApplicationShell shell;
		shell.resize(scale == 100 ? 1600 : 2400, scale == 100 ? 1000 : 1600);
		shell.show();
		shell.openPathFromCommandLine(assetPath);
		shell.openPathFromCommandLine(path);
		expect(until([&] { return shell.levelDocument().doomVertices.size() == 10; }), "load native Hexen document");
		auto* plan = shell.findChild<MapViewport*>("mapViewport");
		auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
		auto* connected = shell.findChild<QAction*>("map.selectConnectedGeometry");
		auto* horizontal = shell.findChild<QAction*>("map.flipHorizontal");
		auto* vertical = shell.findChild<QAction*>("map.flipVertical");
		if (!expect(plan && camera && connected && horizontal && vertical, "shared editor controls")) {
			return 1;
		}
		const auto trigger = [&](QAction* action) {
			if (expect(action->isEnabled(), "action enabled", action->objectName())) {
				action->trigger();
			}
		};
		plan->selectionSetChanged({{LevelMapSelectionKind::DoomThing, 0}});
		expect(!connected->isEnabled(), "things do not enable geometry expansion");
		plan->selectionSetChanged({{LevelMapSelectionKind::DoomLinedef, 0}, {LevelMapSelectionKind::DoomThing, 0}});
		const auto before = serializeLevelMap(shell.levelDocument()).bytes;
		trigger(horizontal);
		expect(shell.levelDocument().undoStack.isEmpty() && serializeLevelMap(shell.levelDocument()).bytes == before,
			   "partial UI reflection refused");
		expect(shell.statusBar()->currentMessage().contains("Connected Geometry"), "actionable refusal");
		trigger(connected);
		expect(shell.levelDocument().selection.size() == 8 && shell.levelDocument().undoStack.isEmpty(),
			   "connected command is selection-only");
		plan->setProjection(MapViewportProjection::TopXY);
		trigger(horizontal);
		expect(shell.levelDocument().undoStack.size() == 1 && shell.levelDocument().doomVertices[0].x == 256 &&
				   shell.levelDocument().doomLinedefs[0].startVertex == 1 && shell.levelDocument().doomThings[0].angle == 150,
			   "UI native mirror publishes once");
		const auto reflected = serializeLevelMap(shell.levelDocument()).bytes;
		capture(shell, QStringLiteral("doom-mirror-plan-%1").arg(scale));
		auto* toggle3D = shell.findChild<QAction*>("map.toggle3D");
		if (!toggle3D->isChecked()) {
			trigger(toggle3D);
		}
		expect(until([&] { return camera->isEnabled() && camera->hasMesh() && camera->hasSkin(); }) && tests::settleModelViewport(*camera),
			   "camera rebuild resolves package materials after reflection");
		camera->setCameraView({216, 40, 64}, 135, -15);
		expect(tests::settleModelViewport(*camera), "reflected room camera render");
		expect(QAccessible::queryAccessibleInterface(plan) && !plan->accessibleName().isEmpty() && plan->focusPolicy() != Qt::NoFocus,
			   "plan accessible and focusable");
		capture(shell, QStringLiteral("doom-mirror-%1").arg(scale));
		QMenu menu;
		menu.setLayoutDirection(app.layoutDirection());
		menu.addAction(connected);
		menu.addAction(horizontal);
		menu.addAction(vertical);
		menu.show();
		app.processEvents();
		capture(menu, QStringLiteral("doom-mirror-controls-%1").arg(scale));
		menu.close();
		trigger(shell.findChild<QAction*>("map.undo"));
		expect(serializeLevelMap(shell.levelDocument()).bytes == before, "UI exact undo including nodes");
		trigger(shell.findChild<QAction*>("map.redo"));
		expect(serializeLevelMap(shell.levelDocument()).bytes == reflected, "UI exact redo");
		expect(shell.saveLevelDocument(temp.filePath(QStringLiteral("saved-%1.wad").arg(scale)), false, &error), "native save", error);
		LevelMapDocument saved;
		expect(loadLevelMap({temp.filePath(QStringLiteral("saved-%1.wad").arg(scale)), "MAP01", {}}, &saved, &error) &&
				   saved.doomFormat == LevelMapDoomFormat::Hexen && saved.doomLinedefs[0].startVertex == 1,
			   "reopen saved Hexen", error);
		expect(inspectLevelDoomNodes(saved).state == LevelDoomNodeState::Missing &&
			levelMapValidationLines(saved).join('\n').contains("doom-nodes-missing"), "reopened UI save retains node rebuild requirement");
		// Queued selection changes must invalidate adoption even before the delayed
		// progress window appears. The original document remains unchanged.
		QTimer::singleShot(0, &shell, [&] { plan->selectionSetChanged({{LevelMapSelectionKind::DoomThing, 1}}); });
		trigger(vertical);
		expect(shell.statusBar()->currentMessage().contains("changed") && serializeLevelMap(shell.levelDocument()).bytes == reflected &&
				   shell.levelDocument().selection == QVector<LevelMapSelectionRef>{{LevelMapSelectionKind::DoomThing, 1}},
			   "stale mirror candidate refused", shell.statusBar()->currentMessage());
		shell.close();
		if (scale == 200) {
			app.removeTranslator(&expanded);
			app.setLayoutDirection(Qt::LeftToRight);
		}
	}
	QEventLoop drain;
	QTimer::singleShot(250, &drain, &QEventLoop::quit);
	drain.exec();
	return ok ? 0 : 1;
}
