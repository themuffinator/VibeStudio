// Directional and depth nudge commands in the shell (app/level_nudge_actions.
// cpp): registration in menus, Tools tiles and the palette; each profile's
// keys (TrenchBroom's arrows and Page Up/Down, NetRadiant Custom's Alt+arrows
// ahead of Go Back and the keypad's plus and minus, the studio's arrows);
// plan-view axes, depth axes and camera-relative axes; the last plan view's
// axes from the Radiant camera; panning when nothing is selected; one undo
// step per nudge; locked objects refused; picked components, the component
// pivot and the sweep destination; Doom maps keeping Raise Floor on Page Up.
// NetRadiant Custom, TrenchBroom and VibeStudio profiles at 100%, and at 200%
// with high contrast, expanded translations and right-to-left layout. Owned
// offscreen widgets and synthetic Qt Test key events only.
#include "app/application_shell.h"
#include "app/level_sweep_gizmo.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_document.h"
#include "core/level_nudge.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/map_geometry.h"
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
		std::cerr << "FAIL: " << phase.toStdString() << ": " << label << ": " << detail.toStdString() << std::endl;
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
// The lowest corner of a brush.
LevelMapVec3 corner(const LevelMapDocument& map, int id)
{
	for (const LevelMapBrush& brush : map.brushes) {
		if (brush.id != id) { continue; }
		const MapBrushGeometry mesh = solveBrushGeometry(brush.faces);
		LevelMapVec3 low {1e9, 1e9, 1e9, mesh.solved};
		for (const auto& face : mesh.faces) {
			for (const LevelMapVec3& p : face.points) {
				low.x = std::min(low.x, p.x);
				low.y = std::min(low.y, p.y);
				low.z = std::min(low.z, p.z);
			}
		}
		return low;
	}
	return {};
}
QString text(const LevelMapVec3& v) { return QStringLiteral("%1 %2 %3").arg(v.x).arg(v.y).arg(v.z); }
bool at(const LevelMapVec3& v, double x, double y, double z) { return v.valid && std::abs(v.x - x) < 1e-6 && std::abs(v.y - y) < 1e-6 && std::abs(v.z - z) < 1e-6; }
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
	// Brushes 0 and 1 are free; brush 2 sits on a locked layer.
	LevelMapDocument created;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	QString error;
	QString layer;
	if (!expect(createLevelMap(create, &created, &error)
			&& addLevelMapBoxBrush(&created, {0, 0, 0, true}, {32, 32, 32, true}, "studio/checker", nullptr, &error)
			&& addLevelMapBoxBrush(&created, {64, 0, 0, true}, {96, 32, 32, true}, "studio/checker", nullptr, &error)
			&& addLevelMapBoxBrush(&created, {128, 0, 0, true}, {160, 32, 32, true}, "studio/checker", nullptr, &error)
			&& createLevelSceneNode(&created, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
			&& assignLevelSceneObjects(&created, layer, {QStringLiteral("brush:2")}, &error) && setLevelSceneLocked(&created, layer, true, &error),
			"fixture", error)) {
		return 1;
	}
	const auto path = temp.filePath("nudge.map");
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(serializeLevelMap(created).bytes) <= 0) { return 1; }
	file.close();
	LevelMapDocument doom;
	LevelMapCreateRequest doomRequest;
	doomRequest.game = QStringLiteral("doom");
	const auto wad = temp.filePath("nudge.wad");
	if (!expect(createLevelMap(doomRequest, &doom, &error) && !doom.doomSectors.isEmpty() && saveLevelMapAs(doom, wad).succeeded(), "Doom fixture", error)) {
		return 1;
	}
	QStringList ids;
	for (const auto& command : levelNudgeCommands()) { ids << command.commandId; }

	for (const auto* profile : {"trenchbroom", "netradiant-custom", "vibestudio-default"}) {
		const bool tb = QLatin1String(profile) == QLatin1String("trenchbroom");
		const bool nrc = QLatin1String(profile) == QLatin1String("netradiant-custom");
		for (int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Nudge UI " << phase.toStdString() << std::endl;
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
				const auto press = [&](QWidget* view, Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
					focus(view);
					QTest::keyClick(view, key, modifiers);
				};
				const auto undoBack = [&](const QByteArray& baseline, const char* label) {
					undo->trigger();
					expect(bytes() == baseline, label, shell.statusBar()->currentMessage());
				};
				const auto baseline = bytes();

				// Registration: Edit > Nudge, the Tools tiles and the palette;
				// disabled without a selection; the profile's keys installed.
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
					expect(keys("map.nudgeForward") == QList<QKeySequence> {QKeySequence(Qt::Key_Up)} && keys("map.nudgeDepthUp") == QList<QKeySequence> {QKeySequence(Qt::Key_PageUp)}
							&& keys("map.nudgeDepthDown") == QList<QKeySequence> {QKeySequence(Qt::Key_PageDown)},
						"TrenchBroom keys");
				} else if (nrc) {
					expect(keys("map.nudgeLeft") == QList<QKeySequence> {QKeySequence(Qt::AltModifier | Qt::Key_Left)}
							&& keys("map.nudgeWorldUp") == QList<QKeySequence> {QKeySequence(Qt::KeypadModifier | Qt::Key_Plus)},
						"NetRadiant Custom keys");
					// The views claim Alt+Left; Go Back keeps it everywhere else.
					expect(keys("shell.navigateBack").contains(QKeySequence(Qt::AltModifier | Qt::Key_Left)), "Go Back keeps Alt+Left", QKeySequence::listToString(keys("shell.navigateBack")));
				} else {
					expect(keys("map.nudgeRight") == QList<QKeySequence> {QKeySequence(Qt::Key_Right)} && keys("map.nudgeDepthUp").isEmpty(), "studio keys");
				}
				expect(keys("map.raiseFloor").contains(QKeySequence(Qt::Key_PageUp)), "Raise Floor keeps Page Up", QKeySequence::listToString(keys("map.raiseFloor")));

				// Nothing selected: the plan view pans with the arrows.
				const auto centre = top->navigationState().center;
				press(top, Qt::Key_Up, nrc ? Qt::AltModifier : Qt::NoModifier);
				expect(bytes() == baseline, "no selection, no edit");
				if (!nrc) {
					const auto panned = top->navigationState().center;
					expect(panned != centre, "the plan view pans instead", text({panned.x(), panned.y(), 0, true}));
				}

				// The palette and menus nudge in the last active plan view.
				pick(top, {{LevelMapSelectionKind::QuakeBrush, 0}});
				expect(action(QStringLiteral("map.nudgeForward"))->isEnabled() && action(QStringLiteral("map.nudgeWorldUp"))->isEnabled(), "enabled with a selection");
				const auto depth = shell.levelDocument().undoStack.size();
				action(QStringLiteral("map.nudgeForward"))->trigger();
				expect(at(corner(shell.levelDocument(), 0), 0, 16, 0) && shell.levelDocument().undoStack.size() == depth + 1,
					"Nudge Forward moves up the top view as one step", text(corner(shell.levelDocument(), 0)) + shell.statusBar()->currentMessage());
				undoBack(baseline, "undo restores the exact map");
				action(QStringLiteral("map.nudgeWorldDown"))->trigger();
				expect(at(corner(shell.levelDocument(), 0), 0, 0, -16), "Nudge Down Along Z", text(corner(shell.levelDocument(), 0)));
				undoBack(baseline, "undo the Z nudge");

				// Keys in the front view: depth is towards the viewer, -Y.
				pick(front, {{LevelMapSelectionKind::QuakeBrush, 0}});
				if (tb) {
					press(front, Qt::Key_PageUp);
					expect(at(corner(shell.levelDocument(), 0), 0, -16, 0), "Page Up nudges towards the viewer in the front view", text(corner(shell.levelDocument(), 0)));
					undoBack(baseline, "undo the depth nudge");
				}
				if (nrc) {
					press(front, Qt::Key_Left, Qt::AltModifier);
					// Go Back would have left the Levels page and hidden the view.
					expect(at(corner(shell.levelDocument(), 0), -16, 0, 0) && front->isVisible(), "Alt+Left nudges ahead of Go Back", text(corner(shell.levelDocument(), 0)));
					undoBack(baseline, "undo the Alt nudge");
					press(front, Qt::Key_Plus, Qt::KeypadModifier);
					expect(at(corner(shell.levelDocument(), 0), 0, 0, 16), "keypad plus nudges up world Z ahead of zoom", text(corner(shell.levelDocument(), 0)));
					undoBack(baseline, "undo the keypad nudge");
				} else {
					press(front, Qt::Key_Left);
					expect(at(corner(shell.levelDocument(), 0), -16, 0, 0), "Left nudges left in the front view", text(corner(shell.levelDocument(), 0)));
					undoBack(baseline, "undo the arrow nudge");
					press(front, Qt::Key_Up, Qt::ShiftModifier);
					expect(at(corner(shell.levelDocument(), 0), 0, 0, 128), "Shift nudges eight grid steps", text(corner(shell.levelDocument(), 0)));
					undoBack(baseline, "undo the coarse nudge");
				}

				// The camera: TrenchBroom's nearest horizontal axis, Radiant's last
				// plan view, the studio's orbiting camera keeping its arrows.
				camera->setCameraView({-256, -256, 64}, 60, -20);
				if (tb) {
					expect(at(levelCameraNudgeAxis(camera->cameraYaw(), camera->cameraPitch(), LevelNudgeDirection::Forward), 0, 1, 0), "the camera looks nearest +Y");
					press(camera, Qt::Key_Up);
					expect(at(corner(shell.levelDocument(), 0), 0, 16, 0), "Up moves forward along the camera's nearest axis", text(corner(shell.levelDocument(), 0)));
					undoBack(baseline, "undo the camera nudge");
					press(camera, Qt::Key_PageDown);
					expect(at(corner(shell.levelDocument(), 0), 0, 0, -16), "Page Down moves down Z from the camera", text(corner(shell.levelDocument(), 0)));
					undoBack(baseline, "undo the camera depth nudge");
				} else if (nrc) {
					focus(front);
					press(camera, Qt::Key_Left, Qt::AltModifier);
					expect(at(corner(shell.levelDocument(), 0), -16, 0, 0), "Alt+Left from the camera uses the front view's axes", text(corner(shell.levelDocument(), 0)));
					undoBack(baseline, "undo the camera nudge");
				} else if (!camera->isPerspective()) {
					press(camera, Qt::Key_Left);
					expect(bytes() == baseline, "the orbiting camera keeps its arrows");
				}

				// A locked object refuses, with a message and no history.
				pick(top, {{LevelMapSelectionKind::QuakeBrush, 2}});
				const auto lockedDepth = shell.levelDocument().undoStack.size();
				action(QStringLiteral("map.nudgeRight"))->trigger();
				expect(bytes() == baseline && shell.levelDocument().undoStack.size() == lockedDepth && !shell.statusBar()->currentMessage().isEmpty(),
					"a locked object refuses the nudge", shell.statusBar()->currentMessage());

				if (tb && scale == 100) {
					// Picked vertices: the arrows' component path in the plan view,
					// the camera's world move, then the pivot.
					pick(top, {{LevelMapSelectionKind::QuakeBrush, 0}});
					action(QStringLiteral("map.vertexTool"))->trigger();
					expect(until([&] { return top->componentMode() == MapViewport::ComponentMode::Vertices && top->componentHandleCount() > 0; }), "vertex mode");
					top->pickAllComponents();
					top->componentPicksChanged(top->pickedComponentCount());
					const auto componentDepth = shell.levelDocument().undoStack.size();
					press(top, Qt::Key_Right);
					expect(until([&] { return shell.levelDocument().undoStack.size() > componentDepth; }), "the component nudge commits", shell.statusBar()->currentMessage());
					expect(at(corner(shell.levelDocument(), 0), 16, 0, 0), "Right nudges the picked vertices", text(corner(shell.levelDocument(), 0)));
					undoBack(baseline, "undo the component nudge");
					until([&] { return top->pickedComponentCount() > 0 || (top->pickAllComponents(), top->pickedComponentCount() > 0); });
					top->componentPicksChanged(top->pickedComponentCount());
					press(camera, Qt::Key_Left);
					expect(at(corner(shell.levelDocument(), 0), -16, 0, 0), "Left from the camera moves the picked vertices along -X", text(corner(shell.levelDocument(), 0)));
					undoBack(baseline, "undo the camera component nudge");
					until([&] { return top->pickedComponentCount() > 0 || (top->pickAllComponents(), top->pickedComponentCount() > 0); });
					top->componentPicksChanged(top->pickedComponentCount());
					action(QStringLiteral("map.componentPivotTool"))->trigger();
					const auto before = top->componentGizmo()->pivot();
					press(top, Qt::Key_Up);
					const auto after = top->componentGizmo()->pivot();
					expect(before.valid && at(after, before.x, before.y + 16, before.z) && bytes() == baseline, "Move Component Pivot nudges the pivot only",
						text(before) + QStringLiteral(" -> ") + text(after));
					action(QStringLiteral("map.componentMoveTool"))->trigger();
					action(QStringLiteral("map.componentResetPivot"))->trigger();

					// The sweep destination follows the nudge keys.
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
						press(top, Qt::Key_PageUp);
						expect(at(sweep->request().translation, 0, 0, 16) && bytes() == baseline, "Page Up raises the sweep destination", text(sweep->request().translation));
						action(QStringLiteral("map.sweepTool"))->trigger();
						expect(until([&] { return !shell.findChild<LevelSweepGizmo*>(QStringLiteral("levelSweepGizmo")); }), "sweep tool closes");
					}
					action(QStringLiteral("map.faceTool"))->trigger();
					expect(until([&] { return top->componentMode() == MapViewport::ComponentMode::Off; }), "face mode off");
					expect(bytes() == baseline, "component work leaves the map as it was");

					// Doom: Page Up still raises the selected sector's floor.
					shell.openPathFromCommandLine(wad);
					expect(until([&] { return shell.levelDocument().sourcePath == wad && !shell.levelDocument().doomSectors.isEmpty(); }), "Doom map ready");
					auto* doomTop = panes.value(0);
					const auto sector = shell.levelDocument().doomSectors.first();
					pick(doomTop, {{LevelMapSelectionKind::DoomSector, sector.id}});
					expect(!action(QStringLiteral("map.nudgeDepthUp"))->isEnabled() && action(QStringLiteral("map.raiseFloor"))->isEnabled(), "Doom depth nudges give way");
					press(doomTop, Qt::Key_PageUp);
					expect(until([&] { return shell.levelDocument().doomSectors.first().floorHeight == sector.floorHeight + 8; }, 5000), "Page Up raises the floor",
						shell.statusBar()->currentMessage());
				}
				if (nrc && scale == 100) {
					// The keypad's minus moves down world Z from the camera, ahead of
					// its zoom, and Alt+arrows move picked vertices, not whole brushes.
					pick(top, {{LevelMapSelectionKind::QuakeBrush, 0}});
					press(camera, Qt::Key_Minus, Qt::KeypadModifier);
					expect(at(corner(shell.levelDocument(), 0), 0, 0, -16), "keypad minus from the camera nudges down world Z", text(corner(shell.levelDocument(), 0)));
					undoBack(baseline, "undo the camera keypad nudge");
					action(QStringLiteral("map.vertexTool"))->trigger();
					expect(until([&] { return top->componentMode() == MapViewport::ComponentMode::Vertices && top->componentHandleCount() > 0; }), "NRC vertex mode");
					top->pickAllComponents();
					top->componentPicksChanged(top->pickedComponentCount());
					const auto componentDepth = shell.levelDocument().undoStack.size();
					press(top, Qt::Key_Right, Qt::AltModifier);
					expect(until([&] { return shell.levelDocument().undoStack.size() > componentDepth; }), "the Alt+arrow component nudge commits",
						shell.statusBar()->currentMessage());
					expect(at(corner(shell.levelDocument(), 0), 16, 0, 0) && at(corner(shell.levelDocument(), 1), 64, 0, 0), "Alt+Right nudges the picked vertices only",
						text(corner(shell.levelDocument(), 0)) + QStringLiteral(" / ") + text(corner(shell.levelDocument(), 1)));
					undoBack(baseline, "undo the Alt+arrow component nudge");
					action(QStringLiteral("map.vertexTool"))->trigger();
					expect(until([&] { return top->componentMode() == MapViewport::ComponentMode::Off; }), "NRC vertex mode off");
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
