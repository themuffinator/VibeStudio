#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_material_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMenu>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) { std::cerr << label << ": " << detail.toStdString() << '\n'; } return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer elapsed; elapsed.start();
	while (!ready() && elapsed.elapsed() < 45000) { QEventLoop loop; QTimer::singleShot(20, &loop, &QEventLoop::quit); loop.exec(); }
	return ready();
}
bool near(double a, double b) { return std::abs(a - b) < 1e-5; }
class Expansion final : public QTranslator {
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const auto value = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 3, QLatin1Char('~')));
	}
};
}

int main(int argc, char** argv)
{
	// Direct semantic Qt APIs and offscreen widget renders. No input injection.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv); QTemporaryDir temp; QString error;
	if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings; settings.setReducedMotion(true); settings.setRestoreSession(false);
	settings.setSelectedEditorProfileId(QStringLiteral("netradiant-custom"));
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	LevelMapDocument source;
	bool ok = tests::createMaterialFixture(temp.path(), &source, &error);
	const auto path = temp.filePath(QStringLiteral("linked.map"));
	ok &= tests::putMaterialFile(path, serializeLevelMap(source).bytes);
	Expansion expanded;
	for (int scale : {100, 200}) {
		settings.setLevelViewLinks({}); settings.setTextScalePercent(scale);
		const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
		settings.setTheme(theme); settings.sync();
		app.setLayoutDirection(scale == 100 ? Qt::LeftToRight : Qt::RightToLeft);
		if (scale == 200) { app.installTranslator(&expanded); }
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
		ApplicationShell shell; shell.resize(scale == 100 ? 1800 : 2600, scale == 100 ? 1100 : 1800); shell.show();
		shell.openPathFromCommandLine(path); shell.openPathFromCommandLine(temp.filePath(QStringLiteral("assets")));
		shell.findChild<QAction*>(QStringLiteral("shell.mode.levels"))->trigger();
		auto* top = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
		auto* front = shell.findChild<MapViewport*>(QStringLiteral("mapViewport1"));
		auto* side = shell.findChild<MapViewport*>(QStringLiteral("mapViewport2"));
		auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
		auto* layout = shell.findChild<QToolButton*>(QStringLiteral("levelViewLayoutButton"));
		if (!expect(top && front && side && camera && layout, "linked workspace exists")) { return 1; }
		auto action = [&](const char* id) { return shell.findChild<QAction*>(QString::fromLatin1(id)); };
		for (const auto* id : {"map.linkPlanCenters", "map.linkPlanZoom", "map.followCamera", "map.centerPlansOnCamera"}) {
			ok &= expect(action(id) && layout->menu()->actions().contains(action(id)), "navigation command is present in the Layout menu", QString::fromLatin1(id));
		}
		auto trigger = [&](const char* id) {
			auto* command = action(id); ok &= expect(command && command->isEnabled(), id); if (command) { command->trigger(); }
		};
		if (!expect(until([&] { return camera->isEnabled() && camera->hasMesh() && camera->hasSkin(); }), "material/model preview ready")) { return 1; }
		const QVector<LevelMapSelectionRef> selected {{LevelMapSelectionKind::QuakeBrush, 0}};
		top->setSelectionSet(selected); top->selectionSetChanged(selected);
		LevelViewState initial;
		ok &= shell.captureLevelViewState(&initial, &error);
		initial.plans = {{{0, {100, 200}, 2}, {1, {-50, 300}, 3}, {2, {400, -90}, 0.5}}};
		initial.links = {};
		ok &= shell.restoreLevelViewState(initial, &error);
		const auto bytes = serializeLevelMap(shell.levelDocument()).bytes;
		const auto revision = shell.levelDocument().revision;
		const auto history = shell.levelDocument().undoStack.size();
		trigger("map.linkPlanCenters");
		ok &= expect(front->navigationState().center == QPointF(100, 300) && side->navigationState().center == QPointF(200, 300)
			&& front->zoom() == 3 && side->zoom() == 0.5, "enabling centres aligns shared axes without zooming");
		front->applyLinkedNavigation({1, {-96, 144}, 0.75});
		ok &= expect(top->navigationState().center == QPointF(-96, 200) && side->navigationState().center == QPointF(200, 144)
			&& top->zoom() == 2 && side->zoom() == 0.5, "front navigation propagates X/Z only with independent scale");
		top->setClipMode(true);
		trigger("map.linkPlanZoom"); side->zoomIn();
		ok &= expect(near(top->zoom(), 2.5) && near(front->zoom(), 2.5) && top->clipMode() && top->selectionSet() == selected,
			"linked zoom preserves clip mode and selection");
		trigger("map.followCamera");
		camera->setCameraView({120, -240, 96}, 40, -15);
		ok &= expect(top->navigationState().center == QPointF(120, -240) && front->navigationState().center == QPointF(120, 96)
			&& side->navigationState().center == QPointF(-240, 96), "perspective camera position follows in all plan axes");
		const auto cameraBeforePan = camera->navigationState();
		top->applyLinkedNavigation({0, {400, 500}, top->zoom()});
		ok &= expect(camera->navigationState().position == cameraBeforePan.position && near(camera->cameraYaw(), 40), "plan navigation does not move or turn the camera");
		trigger("map.followCamera");
		camera->setCameraView({-80, 160, 48}, 20, -10);
		ok &= expect(top->navigationState().center == QPointF(400, 500), "follow off retains independent plan location");
		trigger("map.centerPlansOnCamera");
		ok &= expect(top->navigationState().center == QPointF(-80, 160) && side->navigationState().center == QPointF(160, 48)
			&& near(top->zoom(), 2.5), "one-shot centring leaves scale intact");
		trigger("map.followCamera");
		trigger("map.frameSelection");
		ok &= expect(near(top->zoom(), front->zoom()) && near(top->zoom(), side->zoom()), "frame selection chooses a common fitting scale");
		for (auto* view : {top, front, side}) {
			const auto projection = view->projection();
			const auto a = projection == MapViewportProjection::TopXY ? QPointF(-64, -64)
				: QPointF(-64, -16);
			const auto b = projection == MapViewportProjection::TopXY ? QPointF(64, 64)
				: QPointF(64, 0);
			ok &= expect(view->rect().contains(view->viewPointFor(a.x(), a.y()).toPoint()) && view->rect().contains(view->viewPointFor(b.x(), b.y()).toPoint()), "framed brush fits every pane");
		}
		ok &= expect(action("map.followCamera")->isChecked() && top->navigationState().center == QPointF(0, 0), "explicit framing takes precedence over camera follow");
		auto* fit = shell.findChild<QToolButton*>(QStringLiteral("levelMapZoomFit"));
		ok &= expect(fit != nullptr, "fit command exists");
		if (fit) { fit->click(); }
		ok &= expect(near(top->zoom(), front->zoom()) && near(top->zoom(), side->zoom()) && shell.levelDocument().selection == selected,
			"whole-map fitting shares the limiting scale and preserves selection");
		trigger("map.followCamera");
		const auto preferences = settings.levelViewLinks();
		LevelViewState bookmark; ok &= shell.captureLevelViewState(&bookmark, &error);
		bookmark.links = {false, false, true};
		bookmark.plans[0].center = {37, 59}; bookmark.plans[1].center = {71, 83}; bookmark.plans[2].center = {97, 109};
		ok &= shell.restoreLevelViewState(bookmark, &error);
		LevelViewState restored; ok &= shell.captureLevelViewState(&restored, &error);
		ok &= expect(restored.links == bookmark.links && top->navigationState().center == QPointF(37, 59)
			&& side->navigationState().center == QPointF(97, 109) && settings.levelViewLinks() == preferences,
			"bookmark restores exact independent poses and links without changing defaults", error);
		ok &= expect(!action("map.linkPlanCenters")->isChecked() && !action("map.linkPlanZoom")->isChecked() && action("map.followCamera")->isChecked(), "bookmark links shown by shared menu actions");
		trigger("map.linkPlanCenters");
		ok &= expect(settings.levelViewLinks() == preferences && !action("map.linkPlanZoom")->isChecked() && action("map.followCamera")->isChecked(),
			"changing one bookmark link does not persist its unrelated overrides");
		ok &= shell.restoreLevelViewState(bookmark, &error);
		ok &= shell.captureLevelBookmark(QStringLiteral("Linked review %1").arg(scale), &error);
		trigger("map.layout.camera-and-plan"); trigger("map.layout.four-views");
		ok &= expect(action("map.followCamera")->isChecked(), "layout changes do not clear link action state");
		auto orbit = camera->navigationState(); orbit.perspective = false; orbit.orbitTarget = {30, 45, 60};
		ok &= camera->restoreNavigationState(orbit);
		// The orbit renderer projects imported targets onto its current camera
		// plane. Follow the world target it actually exposes, without rounding.
		const auto target = camera->navigationState().orbitTarget;
		ok &= expect(near(top->navigationState().center.x(), target[0]) && near(top->navigationState().center.y(), target[1])
			&& near(front->navigationState().center.y(), target[2]) && near(side->navigationState().center.x(), target[1])
			&& near(side->navigationState().center.y(), target[2]), "orthographic camera follows its resolved world target");
		ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == bytes && shell.levelDocument().revision == revision
			&& shell.levelDocument().undoStack.size() == history && shell.levelDocument().selection == selected,
			"all navigation leaves map content, history and selection intact");
		ok &= tests::settleModelViewport(*camera);
		const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!captures.isEmpty()) {
			QImage image(shell.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); shell.render(&image);
			ok &= image.save(QDir(captures).filePath(QStringLiteral("linked-views-%1.png").arg(scale)));
			layout->menu()->popup(shell.mapToGlobal(QPoint(300, 250)));
			ok &= until([&] { return layout->menu()->isVisible(); });
			QImage menuImage(layout->menu()->size(), QImage::Format_ARGB32_Premultiplied); menuImage.fill(Qt::transparent); layout->menu()->render(&menuImage);
			ok &= menuImage.save(QDir(captures).filePath(QStringLiteral("linked-menu-%1.png").arg(scale)));
			layout->menu()->close();
		}
		ok &= expect(QAccessible::queryAccessibleInterface(layout) && !layout->accessibleName().isEmpty(), "layout menu remains accessible");
		LevelMapDocument doom; LevelMapCreateRequest create; create.game = QStringLiteral("doom");
		const auto doomPath = temp.filePath(QStringLiteral("linked-%1.wad").arg(scale));
		ok &= createLevelMap(create, &doom, &error) && tests::putMaterialFile(doomPath, serializeLevelMap(doom).bytes);
		shell.openPathFromCommandLine(doomPath);
		ok &= expect(action("map.linkPlanCenters")->isChecked() && action("map.linkPlanZoom")->isChecked() && !action("map.followCamera")->isChecked(), "new maps restore user link defaults");
		const auto doomBytes = serializeLevelMap(shell.levelDocument()).bytes;
		top->applyLinkedNavigation({0, {256, -128}, 1.5});
		ok &= expect(front->navigationState().center.x() == 256 && side->navigationState().center.x() == -128
			&& near(front->zoom(), 1.5) && serializeLevelMap(shell.levelDocument()).bytes == doomBytes, "Doom navigation shares controls without staling nodes");
		if (scale == 200) { app.removeTranslator(&expanded); }
	}
	return ok ? 0 : 1;
}
