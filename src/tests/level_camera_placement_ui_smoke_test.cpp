#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/doom_preview_test_helpers.h"
#include "tests/render_test_support.h"
#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTranslator>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message, const QString &detail = {})
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << ": " << detail.toStdString() << '\n';
	}
	return value;
}
void drain(int milliseconds = 30)
{
	QEventLoop loop;
	QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
	loop.exec(QEventLoop::ExcludeUserInputEvents);
}
bool until(const std::function<bool()> &ready)
{
	QElapsedTimer elapsed;
	elapsed.start();
	while (!ready() && elapsed.elapsed() < 30000)
	{
		drain();
	}
	return ready();
}
bool settle(ModelViewport &view)
{
	return until(
		[&]
		{
			QImage target(view.size(), QImage::Format_ARGB32_Premultiplied);
			target.fill(Qt::transparent);
			view.render(&target);
			return view.isEnabled() && !view.isRendering();
		});
}
QTreeWidgetItem *select(QTreeWidget &palette, const QString &payload)
{
	for (int i = 0; i < palette.topLevelItemCount(); ++i)
	{
		auto *group = palette.topLevelItem(i);
		for (int j = 0; j < group->childCount(); ++j)
		{
			auto *item = group->child(j);
			if (item->data(0, Qt::UserRole).toString() == payload)
			{
				palette.setCurrentItem(item);
				return item;
			}
		}
	}
	return nullptr;
}
class Expanded final : public QTranslator
{
  public:
	bool isEmpty() const override
	{
		return false;
	}
	QString translate(const char *, const char *source, const char *, int) const override
	{
		return QStringLiteral("[%1 expanded]").arg(QString::fromUtf8(source));
	}
};
} // namespace
int main(int argc, char **argv)
{
	// Semantic widget calls and owned render targets only; no input injection,
	// screen capture or game launches. The harness supplies project-local TEMP.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("level-camera-placement-ui-smoke"); skip >= 0) {
		return skip;
	}
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp;
	QString error;
	bool ok = temp.isValid();
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	settings.setReducedMotion(true);
	settings.setRestoreSession(false);
	settings.setSelectedEditorProfileId(QStringLiteral("trenchbroom"));
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	settings.sync();
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	LevelMapCreateRequest create;
	create.starterRoom = false;
	LevelMapDocument fixture;
	ok &= createLevelMap(create, &fixture, &error);
	ok &= addLevelMapBoxBrush(&fixture, {-64, -64, 0, true}, {64, 64, 64, true}, QStringLiteral("studio/camera"), nullptr, &error);
	QString layer, locked;
	ok &= createLevelSceneNode(&fixture, LevelSceneNodeKind::Layer, QStringLiteral("Placement"), {}, &layer, &error);
	ok &= createLevelSceneNode(&fixture, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &locked, &error);
	ok &= setLevelSceneLocked(&fixture, locked, true, &error);
	const auto sourcePath = temp.filePath("camera.map");
	ok &= tests::doom::write(sourcePath, serializeLevelMap(fixture).bytes);
	ApplicationShell shell;
	shell.resize(1700, 1100);
	shell.show();
	shell.openPathFromCommandLine(sourcePath);
	shell.findChild<QAction *>("shell.mode.levels")->trigger();
	auto *camera = shell.findChild<ModelViewport *>("mapPreview3D");
	auto *palette = shell.findChild<QTreeWidget *>("levelMapPalette");
	auto *place = shell.findChild<QAbstractButton *>("levelCameraPlace");
	auto *clearance = shell.findChild<QDoubleSpinBox *>("levelCameraPlacementClearance");
	auto *command = shell.findChild<QAction *>("map.placeAtCamera");
	auto *destination = shell.findChild<QComboBox *>("levelSceneCreation");
	if (!expect(camera && palette && place && clearance && command && destination, "camera placement controls and command"))
	{
		return 1;
	}
	ok &= until([&] { return shell.levelDocument().sourcePath == sourcePath && camera->isEnabled() && camera->hasMesh(); });
	ok &= expect(select(*palette, QStringLiteral("entity:light")), "select point class in Create");
	camera->setCameraView({-200, 0, 32}, 0, 0);
	ok &= settle(*camera);
	const QPointF aim(camera->width() * 0.5, camera->height() * 0.5);
	CameraSurfacePoint sampled;
	int sampledTriangle = -1;
	ok &= expect(camera->surfacePointAt(aim, &sampled, &sampledTriangle) && std::abs(sampled.position[0] + 64) < 1e-5 &&
					 sampled.normal[0] < -0.999 && sampledTriangle >= 0,
				 "camera surface recovers exact wall point and outward normal");
	const auto preserved = sampled;
	ok &= expect(!camera->surfacePointAt({std::numeric_limits<double>::quiet_NaN(), 0}, &sampled) &&
					 !camera->surfacePointAt({-10, -10}, &sampled) && sampled.position == preserved.position,
				 "invalid hit queries preserve result");
	const auto before = serializeLevelMap(shell.levelDocument()).bytes;
	const auto count = shell.levelDocument().entities.size();
	camera->setCameraView({-220, 0, 32}, 0, 0);
	place->click();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == before && !camera->surfacePointAt(aim, &sampled),
				 "stale camera sample never edits document");
	ok &= settle(*camera);
	destination->setCurrentIndex(destination->findData(layer));
	clearance->setValue(17);
	ok &=
		expect(place->isEnabled() && command->isEnabled() && !place->accessibleName().isEmpty() && clearance->focusPolicy() != Qt::NoFocus,
			   "accessible placement controls are enabled for selected point class");
	command->trigger();
	if (!expect(shell.levelDocument().entities.size() == count + 1, "one camera entity is created", shell.statusBar()->currentMessage()))
	{
		return 1;
	}
	const auto placed = shell.levelDocument().entities.last();
	ok &= expect(placed.className == QStringLiteral("light") && placed.origin.x == -96 && placed.origin.y == 0 && placed.origin.z == 32 &&
					 shell.levelDocument().undoStack.size() == 1 &&
					 levelSceneMembership(shell.levelDocument().scene, QStringLiteral("entity:%1").arg(placed.id)) == layer,
				 "camera entity uses snapped outward clearance and active creation layer in one undo");
	const auto after = serializeLevelMap(shell.levelDocument()).bytes;
	shell.findChild<QAction *>("map.undo")->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == before, "camera placement undo restores exact map bytes");
	shell.findChild<QAction *>("map.redo")->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == after, "camera placement redo restores exact map bytes");
	const auto savedPath = temp.filePath("placed.map");
	ok &= shell.saveLevelDocument(savedPath, false, &error);
	LevelMapDocument reloaded;
	ok &= expect(loadLevelMap({savedPath, {}, QStringLiteral("quake")}, &reloaded, &error) && reloaded.entities.last().origin.x == -96 &&
					 reloaded.entities.last().origin.z == 32,
				 "save and reload retain camera placement", error);
	shell.findChild<QAction *>("map.undo")->trigger();
	ok &= settle(*camera);
	destination->setCurrentIndex(destination->findData(locked));
	const auto lockedBefore = serializeLevelMap(shell.levelDocument()).bytes;
	place->click();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == lockedBefore, "locked creation layer refuses camera entity",
				 shell.statusBar()->currentMessage());
	destination->setCurrentIndex(destination->findData(layer));
	QTimer::singleShot(0, &shell, [&] { destination->setCurrentIndex(destination->findData(locked)); });
	place->click();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == lockedBefore &&
					 shell.statusBar()->currentMessage().contains(QStringLiteral("changed")),
				 "creation destination changed during worker cancels publication", shell.statusBar()->currentMessage());
	destination->setCurrentIndex(destination->findData(layer));
	camera->setCameraView({-200, 0, 32}, 180, 0);
	ok &= settle(*camera);
	place->click();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == lockedBefore, "empty camera direction cannot place entity");
	camera->setCameraView({-200, 0, 32}, 0, 0);
	ok &= settle(*camera);
	camera->setEnabled(false);
	command->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == lockedBefore, "disabled preview rejects old geometry");
	camera->setEnabled(true);
	// Loaded class bounds take precedence over the fallback marker bounds.
	const auto definitionPath = temp.filePath("camera.def");
	ok &= tests::doom::write(definitionPath, "/*QUAKED camera_test (1 1 1) (-20 -12 -16) (28 12 40)\nCamera placement test.\n*/\n");
	for (auto *field : shell.findChildren<QLineEdit *>())
	{
		if (field->accessibleName() == QStringLiteral("Entity definition path"))
		{
			field->setText(definitionPath);
			QMetaObject::invokeMethod(field, "returnPressed", Qt::DirectConnection);
			break;
		}
	}
	ok &= expect(select(*palette, QStringLiteral("entity:camera_test")), "loaded definition appears in Create");
	ok &= settle(*camera);
	clearance->setValue(0);
	place->click();
	ok &= expect(shell.levelDocument().entities.last().className == QStringLiteral("camera_test") &&
					 shell.levelDocument().entities.last().origin.x == -96,
				 "definition maximum X keeps entity outside negative-facing wall", shell.statusBar()->currentMessage());
	ok &= shell.saveLevelDocument(savedPath, true, &error);

	for (const auto &game : {QStringLiteral("doom"), QStringLiteral("hexen")})
	{
		create.game = game;
		create.starterRoom = true;
		ok &= shell.createLevelDocument(create, &error);
		ok &= settle(*camera);
		ok &= expect(select(*palette, QStringLiteral("thing:1")), "select native player thing");
		const auto things = shell.levelDocument().doomThings.size();
		camera->setCameraView({-64, -64, 64}, 0, -89);
		ok &= settle(*camera);
		place->click();
		ok &= expect(shell.levelDocument().doomThings.size() == things + 1 && shell.levelDocument().doomThings.last().x == -64 &&
						 shell.levelDocument().doomThings.last().y == -64 && !clearance->isEnabled(),
					 "camera floor creates native thing", game + shell.statusBar()->currentMessage());
		shell.findChild<QAction *>("map.undo")->trigger();
		ok &= settle(*camera);
		camera->setCameraView({-64, -64, 64}, 0, 89);
		ok &= settle(*camera);
		place->click();
		ok &= expect(shell.levelDocument().doomThings.size() == things, "ceiling cannot create floor-relative thing",
					 shell.statusBar()->currentMessage());
		ok &= shell.saveLevelDocument(temp.filePath(game + QStringLiteral(".wad")), false, &error);
	}
	Expanded expanded;
	app.installTranslator(&expanded);
	shell.hide();
	settings.setTheme(StudioTheme::HighContrastDark);
	settings.setTextScalePercent(200);
	settings.sync();
	applyStudioTheme(app, studioThemeTokens(StudioTheme::HighContrastDark, UiDensity::Standard, 200));
	ApplicationShell large;
	large.setLayoutDirection(Qt::RightToLeft);
	large.resize(2200, 1450);
	large.show();
	large.openPathFromCommandLine(savedPath);
	large.findChild<QAction *>("shell.mode.levels")->trigger();
	auto *largePlace = large.findChild<QAbstractButton *>("levelCameraPlace");
	auto *largeClearance = large.findChild<QDoubleSpinBox *>("levelCameraPlacementClearance");
	auto *largeLabel = large.findChild<QLabel *>("levelCameraPlacementLabel");
	auto *largeCamera = large.findChild<ModelViewport *>("mapPreview3D");
	auto *largePalette = large.findChild<QTreeWidget *>("levelMapPalette");
	auto *shelf = largePlace->parentWidget();
	auto *createPanel = shelf->parentWidget();
	// The placement shelf is in the Entities tab's Place section.
	ok &= expect(large.showLevelSidebarTab(QStringLiteral("entities")), "the Entities sidebar tab shows");
	ok &= until([&] { return large.levelDocument().sourcePath == savedPath && largeCamera->isEnabled() && largeCamera->hasMesh(); });
	ok &= expect(select(*largePalette, QStringLiteral("entity:light")), "expanded Create palette selects a class");
	largeCamera->setCameraView({-200, 32, 32}, 0, 0);
	ok &= settle(*largeCamera);
	drain();
	QImage capture(large.size(), QImage::Format_ARGB32_Premultiplied);
	capture.fill(Qt::transparent);
	large.render(&capture);
	ok &= expect(!capture.isNull() && largePlace->text().contains(QStringLiteral("expanded")) && !largePlace->accessibleName().isEmpty() &&
					 largePlace->focusPolicy() != Qt::NoFocus && largePlace->isEnabled() && largePlace->isVisible() &&
					 largePlace->parentWidget()->rect().contains(largePlace->geometry()) &&
					 largeClearance->parentWidget()->rect().contains(largeClearance->geometry()) &&
					 !largeClearance->geometry().intersects(largePlace->geometry()) &&
					 largePlace->height() >= largePlace->sizeHint().height() &&
					 largeClearance->height() >= largeClearance->sizeHint().height(),
				 "high-contrast scaled RTL render retains accessible camera action");
	ok &= expect(createPanel->rect().contains(shelf->geometry()) && createPanel->parentWidget()->rect().contains(createPanel->geometry()) &&
					 largeLabel->buddy() == largeClearance && !largeLabel->geometry().intersects(largeClearance->geometry()) &&
					 largeLabel->height() >= largeLabel->heightForWidth(largeLabel->width()) &&
					 largePlace->height() >= largePlace->heightForWidth(largePlace->width()),
				 "translated placement shelf fits the tab viewport and wraps without clipping");
	const auto output = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
	if (!output.isEmpty())
	{
		QDir().mkpath(output);
		ok &= capture.save(QDir(output).filePath("camera-placement-workspace.png"));
	}
	app.removeTranslator(&expanded);
	std::cout << "Camera surface placement shell " << (ok ? "passed" : "failed") << '\n';
	return ok ? 0 : 1;
}
