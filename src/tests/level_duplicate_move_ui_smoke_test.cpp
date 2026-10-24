// Duplicate and Move, Move to Camera and Repeat Transforms in the shell
// (app/level_duplicate_move_actions.cpp): registration in menus, Tools tiles
// and the palette; TrenchBroom's Ctrl+arrows and Ctrl+Page Up/Down claimed
// by the views while a copy can be made (plan panning with nothing selected,
// Raise Ceiling kept on Doom maps); NetRadiant Custom's Ctrl+R and
// Ctrl+Shift+V; one undo step each; copies selected; locked objects refused;
// picked components for Repeat Transforms. NetRadiant Custom, TrenchBroom and
// VibeStudio profiles at 100%, and at 200% with high contrast, expanded
// translations and right-to-left layout. Owned offscreen widgets and
// synthetic Qt Test key events only.
#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_document.h"
#include "core/level_nudge.h"
#include "core/level_paste.h"
#include "core/level_repeat.h"
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
QString text(const LevelMapVec3& v) { return QStringLiteral("%1 %2 %3").arg(v.x).arg(v.y).arg(v.z); }
// The lowest corner of an object's bounds.
LevelMapVec3 corner(const LevelMapDocument& map, const LevelMapSelectionRef& ref)
{
	LevelMapVec3 mins, maxs;
	return levelMapObjectsBounds(map, {ref}, &mins, &maxs) ? mins : LevelMapVec3 {};
}
bool at(const LevelMapVec3& v, double x, double y, double z) { return v.valid && std::abs(v.x - x) < 1e-6 && std::abs(v.y - y) < 1e-6 && std::abs(v.z - z) < 1e-6; }
LevelMapSelectionRef brush(int id) { return {LevelMapSelectionKind::QuakeBrush, id}; }
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
	const auto path = temp.filePath("duplicate-move.map");
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(serializeLevelMap(created).bytes) <= 0) { return 1; }
	file.close();
	LevelMapDocument doom;
	LevelMapCreateRequest doomRequest;
	doomRequest.game = QStringLiteral("doom");
	const auto wad = temp.filePath("duplicate-move.wad");
	if (!expect(createLevelMap(doomRequest, &doom, &error) && !doom.doomSectors.isEmpty() && saveLevelMapAs(doom, wad).succeeded(), "Doom fixture", error)) {
		return 1;
	}
	QStringList ids;
	for (const auto& command : levelDuplicateMoveCommands()) { ids << command.commandId; }
	ids << QStringLiteral("map.moveToCamera") << QStringLiteral("map.repeatTransforms") << QStringLiteral("map.resetRepeatTransforms");

	for (const auto* profile : {"trenchbroom", "netradiant-custom", "vibestudio-default"}) {
		const bool tb = QLatin1String(profile) == QLatin1String("trenchbroom");
		const bool nrc = QLatin1String(profile) == QLatin1String("netradiant-custom");
		for (int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Duplicate and Move UI " << phase.toStdString() << std::endl;
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
					if (view == camera && !camera->isEnabled()) {
						// A camera rebuilding its preview cannot hold focus; tell the
						// shell it was entered.
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

				// Registration: Edit menus, the Tools tiles and the palette;
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
					expect(keys("map.duplicateMoveForward") == QList<QKeySequence> {QKeySequence(Qt::ControlModifier | Qt::Key_Up)}
							&& keys("map.duplicateMoveLeft") == QList<QKeySequence> {QKeySequence(Qt::ControlModifier | Qt::Key_Left)}
							&& keys("map.duplicateMoveDepthUp") == QList<QKeySequence> {QKeySequence(Qt::ControlModifier | Qt::Key_PageUp)},
						"TrenchBroom keys", QKeySequence::listToString(keys("map.duplicateMoveForward")));
					// Raise Ceiling keeps Ctrl+Page Up where the views do not claim it.
					expect(keys("map.raiseCeiling").contains(QKeySequence(Qt::ControlModifier | Qt::Key_PageUp)), "Raise Ceiling keeps Ctrl+Page Up",
						QKeySequence::listToString(keys("map.raiseCeiling")));
				} else if (nrc) {
					expect(keys("map.repeatTransforms") == QList<QKeySequence> {QKeySequence(Qt::ControlModifier | Qt::Key_R)}
							&& keys("map.moveToCamera") == QList<QKeySequence> {QKeySequence(Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_V)}
							&& keys("map.duplicateMoveForward").isEmpty(),
						"NetRadiant Custom keys", QKeySequence::listToString(keys("map.repeatTransforms")));
				} else {
					expect(keys("map.duplicateMoveForward").isEmpty() && keys("map.repeatTransforms").isEmpty(), "studio leaves them to the palette");
				}

				// Nothing selected: Ctrl+Up still pans the plan view.
				if (tb) {
					const auto centre = top->navigationState().center;
					press(top, Qt::Key_Up, Qt::ControlModifier);
					expect(bytes() == baseline && top->navigationState().center != centre, "no selection: Ctrl+Up pans, no edit");
				}

				// Duplicate and Move Forward in the top view: one step, the copy selected.
				pick(top, {brush(0)});
				expect(action(QStringLiteral("map.duplicateMoveForward"))->isEnabled() && action(QStringLiteral("map.duplicateMoveDepthUp"))->isEnabled(),
					"enabled with a selection");
				const auto depth = shell.levelDocument().undoStack.size();
				if (tb) {
					press(top, Qt::Key_Up, Qt::ControlModifier);
				} else {
					action(QStringLiteral("map.duplicateMoveForward"))->trigger();
				}
				expect(until([&] { return shell.levelDocument().brushes.size() == 4; }, 10000) && shell.levelDocument().undoStack.size() == depth + 1
						&& shell.levelDocument().selection == QVector<LevelMapSelectionRef> {brush(3)} && at(corner(shell.levelDocument(), brush(3)), 0, 16, 0)
						&& at(corner(shell.levelDocument(), brush(0)), 0, 0, 0),
					"Duplicate and Move Forward copies up the top view as one step", text(corner(shell.levelDocument(), brush(3))) + shell.statusBar()->currentMessage());
				undoBack(baseline, "undo restores the exact map");

				// The front view's depth axis: towards the viewer, -Y.
				pick(front, {brush(0)});
				if (tb) {
					press(front, Qt::Key_PageUp, Qt::ControlModifier);
				} else {
					focus(front);
					action(QStringLiteral("map.duplicateMoveDepthUp"))->trigger();
				}
				expect(until([&] { return shell.levelDocument().brushes.size() == 4; }, 10000) && at(corner(shell.levelDocument(), brush(3)), 0, -16, 0),
					"Duplicate and Move Depth Up copies towards the front view's viewer", text(corner(shell.levelDocument(), brush(3))) + shell.statusBar()->currentMessage());
				undoBack(baseline, "undo the depth copy");

				// TrenchBroom's camera: Ctrl+Left copies along the camera's right axis, negated.
				if (tb) {
					camera->setCameraView({-256, -256, 64}, 60, -20);
					pick(top, {brush(0)});
					press(camera, Qt::Key_Left, Qt::ControlModifier);
					expect(until([&] { return shell.levelDocument().brushes.size() == 4; }, 10000) && at(corner(shell.levelDocument(), brush(3)), -16, 0, 0),
						"Ctrl+Left from the camera copies along -X", text(corner(shell.levelDocument(), brush(3))) + shell.statusBar()->currentMessage());
					undoBack(baseline, "undo the camera copy");
				}

				// A locked object refuses, with a message and no history.
				pick(top, {brush(2)});
				const auto lockedDepth = shell.levelDocument().undoStack.size();
				if (action(QStringLiteral("map.duplicateMoveRight"))->isEnabled()) {
					action(QStringLiteral("map.duplicateMoveRight"))->trigger();
				}
				expect(bytes() == baseline && shell.levelDocument().undoStack.size() == lockedDepth, "a locked object refuses Duplicate and Move",
					shell.statusBar()->currentMessage());

				// Repeat Transforms: a nudge records +16 X; each press copies the last
				// copy and moves it on.
				pick(top, {brush(0)});
				action(QStringLiteral("map.nudgeRight"))->trigger();
				expect(at(corner(shell.levelDocument(), brush(0)), 16, 0, 0) && shell.levelDocument().repeat.transforms.translation == std::array<double, 3> {16, 0, 0}
						&& action(QStringLiteral("map.repeatTransforms"))->isEnabled() && action(QStringLiteral("map.resetRepeatTransforms"))->isEnabled(),
					"a nudge records its translation", text(corner(shell.levelDocument(), brush(0))));
				const auto nudged = bytes();
				const auto repeatDepth = shell.levelDocument().undoStack.size();
				if (nrc) {
					press(top, Qt::Key_R, Qt::ControlModifier);
				} else {
					action(QStringLiteral("map.repeatTransforms"))->trigger();
				}
				expect(until([&] { return shell.levelDocument().brushes.size() == 4; }, 10000) && shell.levelDocument().undoStack.size() == repeatDepth + 1
						&& shell.levelDocument().selection == QVector<LevelMapSelectionRef> {brush(3)} && at(corner(shell.levelDocument(), brush(3)), 32, 0, 0)
						&& at(corner(shell.levelDocument(), brush(0)), 16, 0, 0),
					"Repeat Transforms copies and moves the copy as one step", text(corner(shell.levelDocument(), brush(3))) + shell.statusBar()->currentMessage());
				action(QStringLiteral("map.repeatTransforms"))->trigger();
				expect(until([&] { return shell.levelDocument().brushes.size() == 5; }, 10000) && at(corner(shell.levelDocument(), brush(4)), 48, 0, 0),
					"a second press continues the array", text(corner(shell.levelDocument(), brush(4))));
				undo->trigger();
				expect(shell.levelDocument().brushes.size() == 4, "the first undo removes the last copy");
				undo->trigger();
				expect(bytes() == nudged, "undo removes each copy", shell.statusBar()->currentMessage());
				expect(shell.levelDocument().repeat.transforms.translation == std::array<double, 3> {16, 0, 0}, "undo keeps the recorded transforms");

				if (tb && scale == 100) {
					// Picked components transform in place, without copies.
					action(QStringLiteral("map.vertexTool"))->trigger();
					expect(until([&] { return top->componentMode() == MapViewport::ComponentMode::Vertices && top->componentHandleCount() > 0; }), "vertex mode");
					top->pickAllComponents();
					top->componentPicksChanged(top->pickedComponentCount());
					const auto componentDepth = shell.levelDocument().undoStack.size();
					expect(action(QStringLiteral("map.repeatTransforms"))->isEnabled() && !action(QStringLiteral("map.duplicateMoveForward"))->isEnabled(),
						"component mode: Repeat Transforms on, Duplicate and Move off");
					action(QStringLiteral("map.repeatTransforms"))->trigger();
					expect(until([&] { return shell.levelDocument().undoStack.size() > componentDepth; }) && shell.levelDocument().brushes.size() == 3
							&& at(corner(shell.levelDocument(), brush(0)), 32, 0, 0),
						"Repeat Transforms moves the picked vertices", text(corner(shell.levelDocument(), brush(0))) + shell.statusBar()->currentMessage());
					undoBack(nudged, "undo the component repeat");
					action(QStringLiteral("map.vertexTool"))->trigger();
					expect(until([&] { return top->componentMode() == MapViewport::ComponentMode::Off; }), "vertex mode off");
				}

				action(QStringLiteral("map.resetRepeatTransforms"))->trigger();
				expect(shell.levelDocument().repeat.transforms.isIdentity() && !action(QStringLiteral("map.repeatTransforms"))->isEnabled() && bytes() == nudged,
					"Reset Repeat Transforms forgets them and leaves the map");
				undoBack(baseline, "undo the recording nudge");

				// Move to Camera: the shell's camera anchor and grid, one undo step.
				if (expect(until([&] { return camera->isEnabled(); }), "camera ready")) {
					if (camera->isPerspective()) {
						camera->setCameraView({200, 300, 100}, 0, 0);
					} else {
						camera->frameBounds({184, 284, 84}, {216, 316, 116});
					}
					pick(top, {brush(0)});
					LevelPastePlacement placement;
					const auto position = camera->isPerspective() ? camera->cameraPosition()
						: camera->viewRay({camera->width() * 0.5, camera->height() * 0.5}).origin;
					placement.point = {position.x, position.y, position.z, true};
					placement.grid = 16;
					LevelMapVec3 expected;
					expect(levelMoveToCameraOffset(shell.levelDocument(), placement, &expected, &error) && (expected.x != 0 || expected.y != 0 || expected.z != 0),
						"an expected camera offset", error);
					const auto cameraDepth = shell.levelDocument().undoStack.size();
					if (nrc) {
						press(top, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
					} else {
						action(QStringLiteral("map.moveToCamera"))->trigger();
					}
					expect(until([&] { return shell.levelDocument().undoStack.size() == cameraDepth + 1; }, 10000)
							&& at(corner(shell.levelDocument(), brush(0)), expected.x, expected.y, expected.z) && shell.levelDocument().brushes.size() == 3
							&& shell.levelDocument().selection == QVector<LevelMapSelectionRef> {brush(0)},
						"Move to Camera moves the selection in place by the snapped offset",
						text(corner(shell.levelDocument(), brush(0))) + QStringLiteral(" vs ") + text(expected) + shell.statusBar()->currentMessage());
					undoBack(baseline, "undo Move to Camera");
				}

				if (tb && scale == 100) {
					// Doom: Ctrl+Page Up still raises the selected sector's ceiling.
					shell.openPathFromCommandLine(wad);
					expect(until([&] { return shell.levelDocument().sourcePath == wad && !shell.levelDocument().doomSectors.isEmpty(); }), "Doom map ready");
					auto* doomTop = panes.value(0);
					const auto sector = shell.levelDocument().doomSectors.first();
					pick(doomTop, {{LevelMapSelectionKind::DoomSector, sector.id}});
					expect(!action(QStringLiteral("map.duplicateMoveDepthUp"))->isEnabled() && action(QStringLiteral("map.raiseCeiling"))->isEnabled(),
						"Doom depth copies give way");
					press(doomTop, Qt::Key_PageUp, Qt::ControlModifier);
					expect(until([&] { return shell.levelDocument().doomSectors.first().ceilingHeight == sector.ceilingHeight + 8; }, 5000), "Ctrl+Page Up raises the ceiling",
						shell.statusBar()->currentMessage());
				}
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}
	std::cout << (ok ? "Duplicate and Move UI checks passed.\n" : "Duplicate and Move UI checks failed.\n");
	return ok ? 0 : 1;
}
