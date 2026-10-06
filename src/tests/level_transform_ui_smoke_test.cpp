#include "app/application_shell.h"
#include "app/level_rotation_dialog.h"
#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_transform_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QMenu>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
bool expect(bool ok, const char *message, const QString &error = {})
{
	if (!ok) {
		std::cerr << message << ": " << error.toStdString() << '\n';
	}
	return ok;
}
template <typename Predicate> bool settle(Predicate ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 45000) {
		QEventLoop events;
		QTimer::singleShot(30, &events, &QEventLoop::quit);
		events.exec();
	}
	return ready();
}
} // namespace
int main(int argc, char **argv)
{
	// Direct Qt actions and completed-operation signals; no mouse/key injection,
	// native input, game launch or operating-system screenshot capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return EXIT_FAILURE;
	}
	const QDir root(temp.path());
	StudioSettings::setOverrideFilePath(root.filePath(QStringLiteral("settings.ini")));
	const auto input = root.filePath(QStringLiteral("source.map"));
	QFile file(input);
	if (!file.open(QIODevice::WriteOnly)) {
		return EXIT_FAILURE;
	}
	file.write(transformFixture(QStringLiteral("classic")));
	file.close();
	StudioSettings settings;
	settings.setReducedMotion(true);
	settings.setRestoreSession(false);
	bool ok = expect(settings.levelTextureLock() && !settings.levelTextureScaleLock() && !settings.levelAllowValve220(),
					 "safe default preferences");
	ApplicationShell shell;
	shell.openPathFromCommandLine(input);
	if (!expect(settle([&] { return shell.levelDocument().brushes.size() == 1; }), "shell loads source")) {
		return EXIT_FAILURE;
	}
	// The multi-pane shell's QObject traversal order does not identify the top
	// view. Address a stable pane explicitly and exercise each projection below.
	auto *view = shell.findChild<MapViewport *>(QStringLiteral("mapViewport"));
	auto *lock = shell.findChild<QAction *>(QStringLiteral("map.textureLock"));
	auto *stretch = shell.findChild<QAction *>(QStringLiteral("map.textureScaleLock"));
	auto *convert = shell.findChild<QAction *>(QStringLiteral("map.allowValve220"));
	auto *undo = shell.findChild<QAction *>(QStringLiteral("map.undo"));
	if (!expect(view && lock && stretch && convert && undo, "shared transform controls registered")) {
		return EXIT_FAILURE;
	}
	view->selectionSetChanged({{LevelMapSelectionKind::QuakeBrush, 0}});
	ok &= expect(lock->isEnabled() && lock->isCheckable() && lock->isChecked() && !stretch->isChecked() && !convert->isChecked(),
				 "visible default checked state");
	const auto original = serializeLevelMap(shell.levelDocument()).bytes;
	const auto before = shell.levelDocument().brushes.first();
	view->moveRequested(64, 0, 0);
	ok &= expect(shell.levelDocument().undoStack.size() == 1 &&
					 transformUvsMatch(before, shell.levelDocument().brushes.first(),
									   [](const auto &p) { return LevelMapVec3{p.x + 64, p.y, p.z, true}; }),
				 "viewport move uses lock policy");
	undo->trigger();
	lock->trigger();
	ok &= expect(!settings.levelTextureLock(), "toggle persisted");
	view->moveRequested(64, 0, 0);
	ok &= expect(!transformUvsMatch(before, shell.levelDocument().brushes.first(),
									[](const auto &p) { return LevelMapVec3{p.x + 64, p.y, p.z, true}; }),
				 "viewport unlocked move keeps mapping in world");
	undo->trigger();
	lock->trigger();
	const LevelMapVec3 mins{100, -20, 12, true}, maxs{356, 12, 92, true};
	const auto resized = [](const auto &p) {
		return LevelMapVec3{100 + (p.x + 32) * 2, -20 + (p.y - 16) * 0.5, 12 + (p.z + 8) * 1.25, true};
	};
	view->resizeRequested(mins, maxs);
	ok &= expect(shell.levelDocument().undoStack.size() == 1 && !transformUvsMatch(before, shell.levelDocument().brushes[0], resized),
				 "resize default does not stretch");
	undo->trigger();
	stretch->trigger();
	view->resizeRequested(mins, maxs);
	ok &= expect(shell.levelDocument().undoStack.isEmpty() && serializeLevelMap(shell.levelDocument()).bytes == original,
				 "unapproved dialect conversion changes nothing");
	convert->trigger();
	view->resizeRequested(mins, maxs);
	ok &= expect(shell.levelDocument().undoStack.size() == 1 && transformUvsMatch(before, shell.levelDocument().brushes[0], resized),
				 "resize controls permit exact conversion");
	undo->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == original,
				 "resize undo restores the source before quick transforms");
	for (int projection = 0; projection < 3; ++projection) {
		auto *plane = shell.findChild<MapViewport *>(projection == 0 ? QStringLiteral("mapViewport")
			: QStringLiteral("mapViewport%1").arg(projection));
		if (!expect(plane && static_cast<int>(plane->projection()) == projection, "stable pane projection")) {
			return EXIT_FAILURE;
		}
		plane->selectionSetChanged({{LevelMapSelectionKind::QuakeBrush, 0}});
		const int axis = projection == 0 ? 2 : projection == 1 ? 1 : 0;
		const int degrees = projection == 1 ? -90 : 90;
		shell.findChild<QAction *>(QStringLiteral("map.rotateLeft"))->trigger();
		ok &= expect(shell.levelDocument().undoStack.size() == 1
			&& transformUvsMatch(before, shell.levelDocument().brushes[0],
				[axis, degrees](const auto &p) { return rotateLevelPoint(p, {32, 48, 24, true}, axis, degrees); }),
			"quick rotation locks classic UVs in each projection", shell.statusBar()->currentMessage());
		undo->trigger();
		plane->selectionSetChanged({{LevelMapSelectionKind::QuakeBrush, 0}});
		shell.findChild<QAction *>(QStringLiteral("map.flipHorizontal"))->trigger();
		ok &= expect(shell.levelDocument().undoStack.size() == 1
			&& transformUvsMatch(before, shell.levelDocument().brushes[0], [projection](const auto &p) {
				return projection == 2 ? LevelMapVec3{p.x, 96-p.y, p.z, true} : LevelMapVec3{64-p.x, p.y, p.z, true};
			}), "quick mirror locks classic UVs in each projection", shell.statusBar()->currentMessage());
		undo->trigger();
		ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == original,
			"quick transform undo restores the exact source");
	}
	// Numeric rotation inherits the same controls when it opens.
	lock->trigger();
	bool inherited = false;
	QTimer inspect;
	inspect.setInterval(30);
	QObject::connect(&inspect, &QTimer::timeout, &shell, [&] {
		auto *active = QApplication::activeModalWidget();
		if (!active || active->objectName() != QStringLiteral("levelRotationDialog")) {
			return;
		}
		auto *dialog = static_cast<LevelRotationDialog *>(active);
		inherited = !dialog->request().textureLock && dialog->request().allowValve220;
		dialog->reject();
		inspect.stop();
	});
	inspect.start();
	shell.findChild<QAction *>(QStringLiteral("map.rotatePrecisely"))->trigger();
	ok &= expect(inherited, "numeric dialog inherits shared policy");
	settings.sync();
	StudioSettings persisted(root.filePath(QStringLiteral("settings.ini")));
	ok &= expect(!persisted.levelTextureLock() && persisted.levelTextureScaleLock() && persisted.levelAllowValve220(),
				 "preferences survive reload");
	// Render the actual actions as a compact native menu at both text scales.
	for (int scale : {100, 200}) {
		applyStudioTheme(app,
						 studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark, UiDensity::Standard, scale));
		QMenu menu;
		menu.setAccessibleName(QStringLiteral("Level texture transform options"));
		menu.setToolTipsVisible(true);
		for (auto *action : {lock, stretch, convert}) {
			if (scale == 200) {
				action->setText(QStringLiteral("[%1 expanded translation]").arg(action->text()));
			}
			menu.addAction(action);
			ok &= expect(action->isCheckable() && !action->toolTip().isEmpty() && !action->statusTip().isEmpty(),
						 "native action state and descriptions");
		}
		if (scale == 200) {
			menu.setLayoutDirection(Qt::RightToLeft);
		}
		menu.show();
		app.processEvents();
		menu.adjustSize();
		app.processEvents();
		const auto *accessible = QAccessible::queryAccessibleInterface(&menu);
		ok &= expect(accessible && accessible->role() == QAccessible::PopupMenu && accessible->childCount() >= 3,
					 "accessible native menu roles");
		for (auto *action : {lock, stretch, convert}) {
			const auto rect = menu.actionGeometry(action);
			ok &= expect(menu.rect().contains(rect) && rect.width() > menu.fontMetrics().horizontalAdvance(action->text().remove('&')),
						 "scaled labels fit native menu");
		}
		const auto capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!capture.isEmpty()) {
			QDir().mkpath(capture);
			QImage image(menu.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			menu.render(&image);
			ok &=
				expect(image.save(QDir(capture).filePath(QStringLiteral("texture-transform-%1.png").arg(scale))), "rendered menu evidence");
		}
		menu.hide();
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
