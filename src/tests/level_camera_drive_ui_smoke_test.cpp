#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/map_preview_mesh.h"
#include "tests/level_geometry_test_helpers.h"
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFont>
#include <QTimer>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << "FAIL: " << message << '\n'; }
	return value;
}
bool near(double a, double b) { return std::abs(a - b) < 0.001; }
void settle(QApplication& app) { app.processEvents(QEventLoop::ExcludeUserInputEvents); }
bool settlePreview(ModelViewport& view)
{
	QElapsedTimer deadline; deadline.start();
	QImage probe(view.size(), QImage::Format_ARGB32_Premultiplied);
	do {
		view.render(&probe);
		if (!view.isRendering()) { return true; }
		QEventLoop loop; QTimer::singleShot(20, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	} while (deadline.elapsed() < 10000);
	return false;
}
}

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	LevelMapDocument source; QString error;
	if (!tests::createGeometryFixture(2, &source, &error)) { return 1; }
	const auto mesh = buildLevelMapPreviewMesh(source);
	bool ok = true;
	const auto classic = familiarLevelControls("q3radiant");
	const auto centered = cameraPointerDriveMotion({0, 0});
	const auto forward = cameraPointerDriveMotion({0, -1});
	const auto edge = cameraPointerDriveMotion({1, 0});
	const auto corner = cameraPointerDriveMotion({1, -1});
	ok &= expect(centered.forward == 0 && centered.turn == 0 && forward.forward == 400 && forward.turn == 0
		&& near(edge.turn, -270) && corner.forward == 400 && corner.turn == 0, "steering rates preserve centre, direction, dead zone and corner attenuation");
	ok &= expect(cameraPointerDriveMotion({0.05, 0}).turn == 0 && cameraPointerDriveMotion({-1, 0}).turn == 270
		&& cameraPointerDriveMotion({std::numeric_limits<double>::infinity(), 0}).turn == 0, "dead-zone symmetry and invalid positions are safe");
	for (const int scale : {100, 200}) {
		applyStudioTheme(app, studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark, UiDensity::Standard, scale));
		// The camera is a child pane in the editor. Keep its containing window
		// visible while exercising pane hiding, enabling and focus transfer.
		QWidget workspace; workspace.resize(800, 600);
		ModelViewport view(&workspace); view.resize(800, 600); view.setMesh(mesh.mesh); view.setCameraControls(classic.camera);
		view.setHighContrast(scale == 200);
		view.setLayoutDirection(scale == 100 ? Qt::LeftToRight : Qt::RightToLeft); workspace.show(); view.show(); settle(app);
		view.setCameraView({0, 0, 64}, 0, 60);
		view.stepCameraDrive({1, 0, 0, 0});
		ok &= expect(near(view.cameraPosition().x, 32) && near(view.cameraPosition().z, 64), "fixed forward step stays level despite camera pitch");
		view.stepCameraDrive({0, -1, 1, 1, 1});
		ok &= expect(near(view.cameraPosition().y, 32) && near(view.cameraPosition().z, 96)
			&& near(view.cameraYaw(), 22.5) && near(view.cameraPitch(), 82.5), "strafe, rise, yaw and pitch use classic step sizes");
		view.stepCameraDrive({0, 0, 0, 0, 1});
		ok &= expect(near(view.cameraPitch(), 85), "classic pitch stops before vertical");
		view.setCameraView({0, 0, 64}, 0, 60);
		ok &= expect(view.beginPointerDrive({400, 150}), "semantic position steering begins");
		view.advancePointerDrive(0.05);
		ok &= expect(near(view.cameraPosition().x, 10) && near(view.cameraPosition().z, 64), "position drives the camera at the elapsed-time rate on XY");
		view.updatePointerDrive({400, 300}); view.advancePointerDrive(10);
		ok &= expect(near(view.cameraPosition().x, 10) && near(view.cameraYaw(), 0), "centre stops movement while retaining the held gesture");
		view.updatePointerDrive({800, 300}); view.advancePointerDrive(10);
		ok &= expect(near(std::remainder(view.cameraYaw() + 27, 360), 0), "late ticks cap movement to one tenth of a second");
		view.setLooking(true);
		ok &= expect(!view.isLooking() && view.isPointerDriving(), "mouse look cannot interrupt a pending steering gesture");
		view.endPointerDrive(); const auto stopped = view.navigationState(); view.advancePointerDrive(1);
		ok &= expect(!view.isPointerDriving() && view.navigationState().position == stopped.position && view.cameraYaw() == stopped.yaw, "release stops all later drive steps");
		ok &= view.beginPointerDrive({400, 300});
		view.updatePointerDrive({std::numeric_limits<double>::quiet_NaN(), 0});
		ok &= expect(!view.isPointerDriving(), "invalid position ends steering");
		ok &= view.beginPointerDrive({400, 300}); view.setEnabled(false);
		ok &= expect(!view.isPointerDriving(), "disable ends steering"); view.setEnabled(true);
		ok &= view.beginPointerDrive({400, 300}); view.hide();
		ok &= expect(!view.isPointerDriving(), "hiding ends steering"); view.show(); settle(app);
		view.activateWindow();
		QEventLoop activation; QTimer::singleShot(30, &activation, &QEventLoop::quit);
		// Let the offscreen platform deliver its activation event. No native
		// input device or synthetic key/mouse event participates in this test.
		activation.exec();
		view.setFocus(); settle(app);
		ok &= expect(view.hasFocus() && QApplication::focusWidget() == &view, "focus-loss test starts with a focused viewport");
		ok &= view.beginPointerDrive({400, 300}); view.clearFocus();
		ok &= expect(!view.isPointerDriving(), "focus loss ends steering");
		ok &= view.beginPointerDrive({400, 300}); view.setCameraControls(classic.camera, true);
		ok &= expect(!view.isPointerDriving(), "profile application ends steering");
		ok &= view.beginPointerDrive({400, 300}); view.restoreNavigationState(stopped);
		ok &= expect(!view.isPointerDriving(), "saved-view restoration ends steering");
		ok &= view.beginPointerDrive({400, 300}); view.setMesh(mesh.mesh, true);
		ok &= expect(!view.isPointerDriving(), "preview replacement ends steering");
		view.setCameraView({-180, -220, 160}, 50, -30); ok &= view.beginPointerDrive({400, 300});
		ok &= expect(view.accessibleDescription().contains("Position steering"), "steering exposes accessible active state");
		const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!captures.isEmpty()) {
			ok &= expect(settlePreview(view), "camera preview worker completes before visual verification");
			QDir().mkpath(captures);
			QImage image(view.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); view.render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("camera-steering-%1.png").arg(scale))), "steering HUD renders offscreen");
		}
		view.endPointerDrive();
		// A real timer tick also moves, then cancellation prevents further ticks.
		view.setCameraView({0, 0, 64}, 0, 0); ok &= view.beginPointerDrive({400, 0});
		QEventLoop loop; QTimer::singleShot(80, &loop, &QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents);
		view.endPointerDrive();
		ok &= expect(view.cameraPosition().x > 0 && near(view.cameraPosition().z, 64), "timer integration drives without injecting input events");
		ok &= view.beginPointerDrive({400, 300}); view.clearMesh();
		ok &= expect(!view.isPointerDriving() && !view.beginPointerDrive({400, 0}), "clearing geometry ends steering and refuses an empty scene");
	}
	return ok ? 0 : 1;
}
