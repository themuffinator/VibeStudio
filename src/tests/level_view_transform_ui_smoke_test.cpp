// View-relative turns and flips in the shell (app/level_axis_transform_actions.
// cpp): Roll and Pitch registered in menus, Tools tiles and the palette;
// TrenchBroom's Alt+arrows, Alt+Page Up/Down, Ctrl+F and Ctrl+Alt+F claimed
// by the views ahead of Go Back, Go Forward and Find; plan-view and camera
// axes, the Radiant camera keeping the last plan view's axes; the grid and
// half-grid pivots; one undo step each; locked objects refused; the sweep
// destination turned by the rotation step; Doom maps refusing roll and pitch.
// TrenchBroom, NetRadiant Custom and VibeStudio profiles at 100%, and at 200%
// with high contrast, expanded translations and right-to-left layout. Owned
// offscreen widgets and synthetic Qt Test key events only.
#include "app/application_shell.h"
#include "app/level_sweep_gizmo.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/editor_profiles.h"
#include "core/level_document.h"
#include "core/level_quick_transform.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/studio_semantics.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QMenu>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTranslator>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
QString phase;
bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << qPrintable(phase) << ": " << label << ": " << qPrintable(detail) << std::endl;
	}
	return value;
}
bool until(const std::function<bool()>& ready, int milliseconds = 30000)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < milliseconds) {
		QEventLoop loop;
		QTimer::singleShot(15, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, '~'));
	}
};
} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference("four-views");
	// Brush 0 is on the grid; brush 1 is 36 units wide (its centre, x = 82, is
	// off the half grid); brush 2 sits on a locked layer.
	LevelMapDocument created;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	QString error;
	QString layer;
	if (!expect(createLevelMap(create, &created, &error)
			&& addLevelMapBoxBrush(&created, {0, 0, 0, true}, {32, 32, 32, true}, "studio/checker", nullptr, &error)
			&& addLevelMapBoxBrush(&created, {64, 0, 0, true}, {100, 32, 32, true}, "studio/checker", nullptr, &error)
			&& addLevelMapBoxBrush(&created, {128, 0, 0, true}, {160, 32, 32, true}, "studio/checker", nullptr, &error)
			&& createLevelSceneNode(&created, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
			&& assignLevelSceneObjects(&created, layer, {QStringLiteral("brush:2")}, &error) && setLevelSceneLocked(&created, layer, true, &error),
			"fixture", error)) {
		return 1;
	}
	const auto path = temp.filePath("view.map");
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(serializeLevelMap(created).bytes) <= 0) { return 1; }
	file.close();
	LevelMapDocument doom;
	LevelMapCreateRequest doomRequest;
	doomRequest.game = QStringLiteral("doom");
	const auto wad = temp.filePath("view.wad");
	if (!expect(createLevelMap(doomRequest, &doom, &error) && !doom.doomThings.isEmpty() && saveLevelMapAs(doom, wad).succeeded(), "Doom fixture", error)) {
		return 1;
	}
	const QStringList added {QStringLiteral("map.rollClockwise"), QStringLiteral("map.rollCounterclockwise"), QStringLiteral("map.pitchClockwise"),
		QStringLiteral("map.pitchCounterclockwise")};
	QStringList ids;
	for (const auto& transform : levelViewTransforms()) { ids << transform.commandId; }

	for (const auto* profile : {"trenchbroom", "netradiant-custom", "vibestudio-default"}) {
		const bool tb = QLatin1String(profile) == QLatin1String("trenchbroom");
		const bool nrc = QLatin1String(profile) == QLatin1String("netradiant-custom");
		const LevelEditorControls controls = levelEditorControlsForProfile(QString::fromLatin1(profile));
		for (int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "View transforms UI " << qPrintable(phase) << std::endl;
			settings.setSelectedEditorProfileId(profile);
			settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
			settings.sync();
			applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, scale));
			Expanded expanded;
			if (scale == 200) {
				app.installTranslator(&expanded);
				app.setLayoutDirection(Qt::RightToLeft);
			}
			{
				ApplicationShell shell;
				shell.resize(1600, 1000);
				shell.show();
				shell.openPathFromCommandLine(path);
				const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
				action(QStringLiteral("shell.mode.levels"))->trigger();
				action(QStringLiteral("map.layout.four-views"))->trigger();
				if (!expect(until([&] { return shell.levelDocument().brushes.size() == 3 && shell.findChildren<MapViewport*>().size() >= 3; }), "map ready")) {
					return 1;
				}
				QHash<int, MapViewport*> panes;
				for (auto* pane : shell.findChildren<MapViewport*>()) {
					if (!panes.contains(static_cast<int>(pane->projection()))) { panes.insert(static_cast<int>(pane->projection()), pane); }
				}
				auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
				auto* grid = shell.findChild<QComboBox*>("levelMapGrid");
				auto* snap = shell.findChild<QCheckBox*>("levelMapSnap");
				auto* undo = action(QStringLiteral("map.undo"));
				if (!expect(panes.size() == 3 && camera && grid && snap && undo, "panes, camera, grid and snap")) { return 1; }
				grid->setCurrentIndex(grid->findData(16));
				snap->setChecked(true);
				auto* top = panes.value(0);
				auto* front = panes.value(1);
				auto* side = panes.value(2);
				const auto pick = [&](MapViewport* pane, const QVector<LevelMapSelectionRef>& refs) {
					pane->setSelectionSet(refs);
					pane->selectionSetChanged(refs);
				};
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				const auto focus = [&](QWidget* view) {
					shell.activateWindow();
					expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
					view->setFocus(Qt::OtherFocusReason);
					if (view == camera) {
						// The offscreen camera has no GPU backend and stays disabled,
						// so it cannot hold focus; tell the shell it was entered.
						QFocusEvent entered(QEvent::FocusIn, Qt::OtherFocusReason);
						QApplication::sendEvent(camera, &entered);
					} else {
						expect(until([&] { return view->hasFocus(); }), "view takes focus", view->objectName());
					}
				};
				const auto press = [&](QWidget* view, Qt::Key key, Qt::KeyboardModifiers modifiers) {
					focus(view);
					QTest::keyClick(view, key, modifiers);
				};
				const auto baseline = bytes();
				// The edit the shell should make: the shared service with the
				// profile's pivot policy, from the current document.
				const auto turned = [&](int axis, int turns) {
					auto expected = shell.levelDocument();
					LevelMapRotationRequest rotation;
					rotation.axis = axis;
					rotation.degrees = 90.0 * turns;
					if (controls.quickTurnSnapsPivot) { levelMapQuickTurnPivot(expected, 16, &rotation.pivot); }
					rotation.textureLock = settings.levelTextureLock();
					rotation.allowValve220 = settings.levelAllowValve220();
					QString problem;
					expect(rotateLevelMapSelection(&expected, rotation, &problem), "reference turn", problem);
					return serializeLevelMap(expected).bytes;
				};
				const auto flipped = [&](int axis) {
					auto expected = shell.levelDocument();
					LevelMapVec3 pivot;
					if (controls.flipSnapsPivot) { levelMapFlipPivot(expected, 16, &pivot); }
					QString problem;
					expect(flipLevelMapSelection(&expected, axis, {settings.levelTextureLock(), settings.levelAllowValve220()}, pivot, &problem), "reference flip", problem);
					return serializeLevelMap(expected).bytes;
				};
				// Runs `edit`, checks it matches `expected` as one undo step, then undoes it.
				const auto check = [&](const QByteArray& expected, const std::function<void()>& edit, const QString& label) {
					const auto depth = shell.levelDocument().undoStack.size();
					edit();
					expect(bytes() == expected && shell.levelDocument().undoStack.size() == depth + 1, "the shared edit as one undo step",
						label + QLatin1Char(' ') + shell.statusBar()->currentMessage());
					undo->trigger();
					expect(bytes() == baseline, "undo restores the exact map", label);
				};

				// Registration: Edit > Transform, the Tools tiles and the palette;
				// disabled without a selection.
				pick(top, {});
				for (const auto& id : ids) {
					auto* command = action(id);
					if (!expect(command != nullptr, "registered", id)) { return 1; }
					bool menu = false;
					for (auto* owner : command->associatedObjects()) { menu |= qobject_cast<QMenu*>(owner) != nullptr; }
					auto* row = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-") + id);
					CommandPaletteEntry entry;
					expect(!command->isEnabled() && !command->text().isEmpty() && !command->statusTip().isEmpty() && menu
							&& commandPaletteEntryForCommandId(id, &entry) && !entry.label.isEmpty(),
						"disabled without a selection, labelled and in a menu", id);
					expect(row && row->focusPolicy() != Qt::NoFocus && !row->accessibleName().isEmpty() && !row->toolTip().isEmpty()
							&& QAccessible::queryAccessibleInterface(row),
						"accessible Tools tile", id);
				}
				const auto keys = [&](const char* id) { return action(QString::fromLatin1(id))->shortcuts(); };
				if (tb) {
					expect(keys("map.rollClockwise") == QList<QKeySequence> {QKeySequence(Qt::AltModifier | Qt::Key_Up)}
							&& keys("map.rotateRight") == QList<QKeySequence> {QKeySequence(Qt::AltModifier | Qt::Key_Left)}
							&& keys("map.pitchCounterclockwise") == QList<QKeySequence> {QKeySequence(Qt::AltModifier | Qt::Key_PageDown)}
							&& keys("map.flipHorizontal") == QList<QKeySequence> {QKeySequence(Qt::ControlModifier | Qt::Key_F)}
							&& keys("map.flipVertical") == QList<QKeySequence> {QKeySequence(Qt::ControlModifier | Qt::AltModifier | Qt::Key_F)},
						"TrenchBroom keys", QKeySequence::listToString(keys("map.rollClockwise")));
					// The views claim them; Go Back, Go Forward and Find keep them elsewhere.
					expect(keys("shell.navigateBack").contains(QKeySequence(Qt::AltModifier | Qt::Key_Left))
							&& keys("shell.navigateForward").contains(QKeySequence(Qt::AltModifier | Qt::Key_Right))
							&& keys("shell.focusSearch").contains(QKeySequence(Qt::ControlModifier | Qt::Key_F)),
						"Go Back, Go Forward and Find keep their keys", QKeySequence::listToString(keys("shell.focusSearch")));
				} else {
					for (const auto& id : added) {
						expect(action(id)->shortcuts().isEmpty(), "no keys outside the TrenchBroom profile", id);
					}
				}

				// Nothing selected: TrenchBroom's Alt+Up gives way, so the plan view pans.
				if (tb) {
					const auto centre = top->navigationState().center;
					press(top, Qt::Key_Up, Qt::AltModifier);
					expect(bytes() == baseline && top->navigationState().center != centre && top->isVisible(), "no selection: Alt+Up pans, nothing turns");
				}

				// The palette and menus use the last active plan view's axes.
				pick(top, {{LevelMapSelectionKind::QuakeBrush, 0}});
				for (const auto& id : ids) { expect(action(id)->isEnabled(), "enabled with a selection", id); }
				check(turned(1, 1), [&] { action(QStringLiteral("map.rollClockwise"))->trigger(); }, QStringLiteral("top-view Roll Clockwise turns +90 about Y"));
				check(turned(0, 1), [&] { action(QStringLiteral("map.pitchCounterclockwise"))->trigger(); }, QStringLiteral("top-view Pitch Counter-clockwise turns +90 about X"));
				check(turned(2, -1), [&] { action(QStringLiteral("map.rotateRight"))->trigger(); }, QStringLiteral("top-view Rotate 90° Right turns -90 about Z"));
				pick(front, {{LevelMapSelectionKind::QuakeBrush, 0}});
				check(turned(2, -1), [&] { action(QStringLiteral("map.rollCounterclockwise"))->trigger(); }, QStringLiteral("front-view Roll Counter-clockwise turns -90 about Z"));
				pick(side, {{LevelMapSelectionKind::QuakeBrush, 0}});
				check(turned(1, -1), [&] { action(QStringLiteral("map.pitchClockwise"))->trigger(); }, QStringLiteral("side-view Pitch Clockwise turns -90 about Y"));
				// The off-grid brush: TrenchBroom's flips mirror about the half-grid
				// centre (x = 80), the others about the exact centre (x = 82).
				pick(top, {{LevelMapSelectionKind::QuakeBrush, 1}});
				const auto flipX = flipped(0);
				check(flipX, [&] { action(QStringLiteral("map.flipHorizontal"))->trigger(); }, QStringLiteral("top-view Flip Horizontal"));
				{
					auto exact = shell.levelDocument();
					QString problem;
					flipLevelMapSelection(&exact, 0, {settings.levelTextureLock(), settings.levelAllowValve220()}, &problem);
					expect((serializeLevelMap(exact).bytes != flipX) == tb, "only TrenchBroom moves the flip pivot onto the half grid");
				}

				if (tb) {
					// The keys in the views, ahead of Go Back, Go Forward and Find.
					pick(front, {{LevelMapSelectionKind::QuakeBrush, 0}});
					check(turned(2, 1), [&] { press(front, Qt::Key_Up, Qt::AltModifier); }, QStringLiteral("Alt+Up rolls about Z in the front view"));
					check(turned(1, 1), [&] { press(front, Qt::Key_Left, Qt::AltModifier); }, QStringLiteral("Alt+Left yaws clockwise in the front view"));
					expect(front->isVisible(), "Go Back did not leave the Levels page");
					check(turned(1, -1), [&] { press(front, Qt::Key_Right, Qt::AltModifier); }, QStringLiteral("Alt+Right yaws counter-clockwise"));
					check(flipped(2), [&] { press(front, Qt::Key_F, Qt::ControlModifier | Qt::AltModifier); }, QStringLiteral("Ctrl+Alt+F flips along Z in the front view"));
					pick(side, {{LevelMapSelectionKind::QuakeBrush, 0}});
					check(turned(1, -1), [&] { press(side, Qt::Key_PageUp, Qt::AltModifier); }, QStringLiteral("Alt+Page Up pitches about Y in the side view"));
					pick(top, {{LevelMapSelectionKind::QuakeBrush, 1}});
					check(flipped(0), [&] { press(top, Qt::Key_F, Qt::ControlModifier); }, QStringLiteral("Ctrl+F flips along X in the top view"));
					expect(top->hasFocus(), "Find did not take focus from the view");

					// The camera: its nearest horizontal axis, right axis and Z.
					camera->setCameraView({-256, -256, 64}, 60, -20);
					pick(top, {{LevelMapSelectionKind::QuakeBrush, 0}});
					check(turned(1, 1), [&] { press(camera, Qt::Key_Up, Qt::AltModifier); }, QStringLiteral("Alt+Up from the camera rolls about +Y"));
					check(turned(2, -1), [&] { press(camera, Qt::Key_Left, Qt::AltModifier); }, QStringLiteral("Alt+Left from the camera yaws about Z"));
					check(turned(0, 1), [&] { press(camera, Qt::Key_PageDown, Qt::AltModifier); }, QStringLiteral("Alt+Page Down from the camera pitches about +X"));
					check(flipped(2), [&] { press(camera, Qt::Key_F, Qt::ControlModifier | Qt::AltModifier); }, QStringLiteral("Ctrl+Alt+F from the camera flips along Z"));
				} else if (nrc) {
					// Radiant cameras keep the last plan view's axes.
					camera->setCameraView({-256, -256, 64}, 60, -20);
					pick(front, {{LevelMapSelectionKind::QuakeBrush, 0}});
					focus(front);
					focus(camera);
					check(turned(2, 1), [&] { action(QStringLiteral("map.rollClockwise"))->trigger(); }, QStringLiteral("from the NRC camera Roll uses the front view"));
				} else {
					// The studio's orbiting camera: Yaw about Z.
					pick(top, {{LevelMapSelectionKind::QuakeBrush, 0}});
					focus(camera);
					check(turned(2, 1), [&] { action(QStringLiteral("map.rotateLeft"))->trigger(); }, QStringLiteral("from the camera Rotate 90° Left yaws about Z"));
					focus(front);
				}

				// A locked object refuses, with a message and no history.
				pick(top, {{LevelMapSelectionKind::QuakeBrush, 2}});
				const auto lockedDepth = shell.levelDocument().undoStack.size();
				action(QStringLiteral("map.rollClockwise"))->trigger();
				expect(bytes() == baseline && shell.levelDocument().undoStack.size() == lockedDepth && !shell.statusBar()->currentMessage().isEmpty(),
					"a locked object refuses the turn", shell.statusBar()->currentMessage());

				if (tb && scale == 100) {
					// The sweep destination turns by the rotation step; flips wait.
					pick(top, {{LevelMapSelectionKind::QuakeBrush, 0}});
					action(QStringLiteral("map.faceTool"))->trigger();
					expect(until([&] { return top->componentMode() == MapViewport::ComponentMode::Faces && top->componentHandleCount() > 0; }), "face mode");
					LevelMapComponentSet face;
					face.object = {LevelMapSelectionKind::QuakeBrush, 0};
					face.kind = LevelBrushComponent::Face;
					face.components = {0};
					expect(top->setPickedComponents({face}), "pick a face");
					top->componentPicksChanged(top->pickedComponentCount());
					action(QStringLiteral("map.sweepTool"))->trigger();
					auto* sweep = shell.findChild<LevelSweepGizmo*>(QStringLiteral("levelSweepGizmo"));
					if (expect(sweep != nullptr, "sweep tool active")) {
						const double step = sweep->angleStep() > 0 ? sweep->angleStep() : 15.0;
						press(top, Qt::Key_Up, Qt::AltModifier);
						const auto& turn = sweep->request();
						expect(std::abs(turn.angle - step) < 1e-6 && std::abs(turn.rotationAxis.y - 1) < 1e-6 && bytes() == baseline,
							"Alt+Up turns the sweep destination about +Y by the rotation step",
							QStringLiteral("%1 about %2 %3 %4").arg(turn.angle).arg(turn.rotationAxis.x).arg(turn.rotationAxis.y).arg(turn.rotationAxis.z));
						expect(!action(QStringLiteral("map.flipHorizontal"))->isEnabled(), "flips wait for the sweep to close");
						action(QStringLiteral("map.sweepTool"))->trigger();
						expect(until([&] { return !shell.findChild<LevelSweepGizmo*>(QStringLiteral("levelSweepGizmo")); }), "sweep tool closes");
					}
					action(QStringLiteral("map.faceTool"))->trigger();
					expect(until([&] { return top->componentMode() == MapViewport::ComponentMode::Off; }), "face mode off");
					expect(bytes() == baseline, "sweep work leaves the map as it was");

					// Doom: yaw and flips in the plane; roll and pitch never.
					shell.openPathFromCommandLine(wad);
					expect(until([&] { return shell.levelDocument().sourcePath == wad && !shell.levelDocument().doomThings.isEmpty(); }), "Doom map ready");
					auto* doomTop = panes.value(0);
					pick(doomTop, {{LevelMapSelectionKind::DoomThing, shell.levelDocument().doomThings.first().id}});
					expect(!action(QStringLiteral("map.rollClockwise"))->isEnabled() && !action(QStringLiteral("map.pitchClockwise"))->isEnabled()
							&& action(QStringLiteral("map.rotateLeft"))->isEnabled() && action(QStringLiteral("map.flipHorizontal"))->isEnabled(),
						"Doom maps take yaw and flips only");
				}
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}
	return ok ? 0 : 1;
}
