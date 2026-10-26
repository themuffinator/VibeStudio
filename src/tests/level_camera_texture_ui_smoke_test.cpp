// Offscreen checks of the camera-relative texture moves and flips on the
// Levels page, under the TrenchBroom profile at 100% and at 200% text with the
// high-visibility theme, an expanding translator and right-to-left layout, and
// under NetRadiant Custom. The twenty commands (the moves, flips and the
// fixed-step Rotate Textures turns) are registered, labelled, in the Surfaces
// menu's submenus, the Tools tab and the palette, and disabled with nothing to
// adjust. In the TrenchBroom camera the arrows and Page Up nudge the selection
// until a face is picked; then the arrows, Shift+arrows and Ctrl+arrows move
// the picked face's texture by the grid, twice the grid and one texel, Ctrl+F
// and Ctrl+Alt+F flip it, and Page Up and Page Down with Shift and Ctrl turn it
// 15°, 90° and 1° clockwise and counter-clockwise, each the exact edit the
// shared service makes, as one undo step. The menu command acts on the
// Surfaces target when no face is picked; NetRadiant Custom leaves the keys
// unbound; hidden objects are not targets.

#include "app/application_shell.h"
#include "app/level_surface_tools.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_camera_texture.h"
#include "core/level_editor_controls.h"
#include "core/map_geometry.h"
#include "core/map_preview_mesh.h"
#include "core/studio_semantics.h"
#include "tests/level_surface_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QMenu>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#ifdef VIBESTUDIO_HAVE_QTEST
#include <QTest>
#endif

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

bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 30000) {
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
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};

} // namespace

int main(int argc, char** argv)
{
	// Owned offscreen Qt widgets and semantic events only; no system input.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "temporary directory")) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	// Classic faces with shift 7 9, rotation 17 and scale -0.5 2.
	const QString path = temp.filePath(QStringLiteral("camera.map"));
	{
		QFile file(path);
		if (!expect(file.open(QIODevice::WriteOnly) && file.write(tests::surfaceFixture(QStringLiteral("classic"))) > 0, "fixture")) {
			return 1;
		}
	}
	QStringList ids;
	for (const auto& command : levelCameraTextureCommands()) {
		ids << command.commandId;
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(command.commandId, &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(), "palette entry", command.commandId);
	}

	struct Pass {
		const char* profile;
		int scale;
	};
	for (const Pass& pass : {Pass {"trenchbroom", 100}, Pass {"trenchbroom", 200}, Pass {"netradiant-custom", 100}}) {
		const bool tb = QLatin1String(pass.profile) == QLatin1String("trenchbroom");
		phase = QStringLiteral("%1 %2%").arg(QLatin1String(pass.profile)).arg(pass.scale);
		std::cout << "Camera texture UI " << qPrintable(phase) << std::endl;
		settings.setSelectedEditorProfileId(QString::fromLatin1(pass.profile));
		settings.setTextScalePercent(pass.scale);
		settings.setTheme(pass.scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
		settings.sync();
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, pass.scale));
		Expanded expanded;
		if (pass.scale == 200) {
			app.installTranslator(&expanded);
			app.setLayoutDirection(Qt::RightToLeft);
		}
		{
			ApplicationShell shell;
			shell.resize(pass.scale == 100 ? 1650 : 2400, pass.scale == 100 ? 1000 : 1500);
			shell.show();
			shell.openPathFromCommandLine(path);
			const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
			action(QStringLiteral("shell.mode.levels"))->trigger();
			action(QStringLiteral("map.layout.four-views"))->trigger();
			auto* top = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
			auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
			auto* grid = shell.findChild<QComboBox*>(QStringLiteral("levelMapGrid"));
			if (!expect(top && camera && grid && until([&] { return shell.levelDocument().sourcePath == path && !shell.levelDocument().brushes.isEmpty(); }),
					"map ready")) {
				return 1;
			}
			const bool cameraLive = until([&] {
				QImage image(camera->size(), QImage::Format_ARGB32_Premultiplied);
				camera->render(&image);
				return camera->isEnabled() && !camera->isRendering();
			});
			grid->setCurrentIndex(grid->findData(16));
			camera->setCameraView({32, -320, 24}, 90, -10);
			const int brushId = shell.levelDocument().brushes.first().id;
			const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
			const auto settle = [&] { return until([&] { return !shell.levelSurfaceEditsPending(); }); };
			const auto select = [&](bool brush) {
				const QVector<LevelMapSelectionRef> refs = brush ? QVector<LevelMapSelectionRef> {{LevelMapSelectionKind::QuakeBrush, brushId}}
																 : QVector<LevelMapSelectionRef> {};
				top->setSelectionSet(refs);
				top->selectionSetChanged(refs);
			};
			// The face looking back at the camera.
			const auto geometry = solveBrushGeometry(shell.levelDocument().brushes.first().faces);
			int front = -1;
			for (int i = 0; i < geometry.faces.size(); ++i) {
				if (geometry.faces[i].isValid() && geometry.faces[i].plane.normalY < -0.9) {
					front = i;
				}
			}
			const auto pick = [&](int face) {
				const auto mesh = buildLevelMapPreviewMesh(shell.levelDocument());
				for (int i = 0; i < mesh.owners.size(); ++i) {
					if (mesh.owners[i].kind == LevelMapSelectionKind::QuakeBrush && mesh.owners[i].objectId == brushId && mesh.ownerFaces.value(i, -1) == face) {
						Q_EMIT camera->trianglePicked(0, i, static_cast<int>(ModelViewportPick::FaceToggle));
						return true;
					}
				}
				return false;
			};
			// The edit the command should make: the shared service, from the
			// camera's own directions at this moment.
			const auto reference = [&](const QString& id, const QVector<LevelSurfaceFace>& faces, double gridUnits) {
				LevelCameraTextureCommand command;
				levelCameraTextureCommandForId(id, &command);
				ModelVec3 right;
				ModelVec3 up;
				camera->viewAxes(&right, &up);
				const auto request = levelCameraTextureRequest(command, {right.x, right.y, right.z, true}, {up.x, up.y, up.z, true}, gridUnits);
				auto expected = shell.levelDocument();
				LevelSurfaceEditPlan plan;
				QString problem;
				expect(prepareLevelSurfaceEdits(expected, faces, {}, {request}, {}, &plan, &problem) && commitLevelSurfaceEdit(&expected, plan, &problem),
					"reference edit", problem);
				return serializeLevelMap(expected).bytes;
			};

			// Registration: menus, the Tools tab and accessibility; nothing to adjust yet.
			select(false);
			for (const auto& id : ids) {
				QAction* command = action(id);
				if (!expect(command != nullptr, "registered", id)) {
					return 1;
				}
				bool menu = false;
				for (auto* owner : command->associatedObjects()) {
					menu |= qobject_cast<QMenu*>(owner) != nullptr;
				}
				auto* row = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-") + id);
				expect(!command->text().isEmpty() && !command->statusTip().isEmpty() && menu && !command->isEnabled(),
					"labelled, in a menu, disabled without a target", id);
				expect(row && row->focusPolicy() != Qt::NoFocus && !row->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(row),
					"accessible Tools tile", id);
			}
			expect(shell.findChild<QMenu*>(QStringLiteral("levelCameraTextureToolsMenu")) != nullptr, "the Surfaces menu's Move Textures from Camera submenu");
			if (auto* turns = shell.findChild<QMenu*>(QStringLiteral("levelRotateTextureToolsMenu")); expect(turns != nullptr, "the Surfaces menu's Rotate Textures submenu")) {
				expect(turns->actions().contains(action(QStringLiteral("map.rotateTexturesClockwise")))
						&& turns->actions().contains(action(QStringLiteral("map.rotateTexturesCounterclockwiseFine")))
						&& !turns->actions().contains(action(QStringLiteral("map.moveTexturesUp"))),
					"the turns are in their own submenu");
			}
			const auto keys = [&](const char* id) { return action(QString::fromLatin1(id))->shortcuts(); };
			if (tb) {
				expect(keys("map.moveTexturesUp") == QList<QKeySequence> {QKeySequence(Qt::Key_Up)}
						&& keys("map.moveTexturesLeftCoarse") == QList<QKeySequence> {QKeySequence(Qt::ShiftModifier | Qt::Key_Left)}
						&& keys("map.moveTexturesRightFine") == QList<QKeySequence> {QKeySequence(Qt::ControlModifier | Qt::Key_Right)}
						&& keys("map.flipTexturesHorizontally") == QList<QKeySequence> {QKeySequence(Qt::ControlModifier | Qt::Key_F)}
						&& keys("map.flipTexturesVertically") == QList<QKeySequence> {QKeySequence(Qt::ControlModifier | Qt::AltModifier | Qt::Key_F)},
					"TrenchBroom's texture keys", QKeySequence::listToString(keys("map.moveTexturesUp")));
				expect(keys("map.rotateTexturesClockwise") == QList<QKeySequence> {QKeySequence(Qt::Key_PageUp)}
						&& keys("map.rotateTexturesClockwiseCoarse") == QList<QKeySequence> {QKeySequence(Qt::ShiftModifier | Qt::Key_PageUp)}
						&& keys("map.rotateTexturesClockwiseFine") == QList<QKeySequence> {QKeySequence(Qt::ControlModifier | Qt::Key_PageUp)}
						&& keys("map.rotateTexturesCounterclockwise") == QList<QKeySequence> {QKeySequence(Qt::Key_PageDown)}
						&& keys("map.rotateTexturesCounterclockwiseCoarse") == QList<QKeySequence> {QKeySequence(Qt::ShiftModifier | Qt::Key_PageDown)}
						&& keys("map.rotateTexturesCounterclockwiseFine") == QList<QKeySequence> {QKeySequence(Qt::ControlModifier | Qt::Key_PageDown)},
					"TrenchBroom's texture turn keys", QKeySequence::listToString(keys("map.rotateTexturesClockwise")));
				expect(keys("map.nudgeDepthUp") == QList<QKeySequence> {QKeySequence(Qt::Key_PageUp)}
						&& keys("map.duplicateMoveDepthDown") == QList<QKeySequence> {QKeySequence(Qt::ControlModifier | Qt::Key_PageDown)},
					"the depth nudge and copy keep Page Up and Page Down beside them");
				expect(keys("map.nudgeForward") == QList<QKeySequence> {QKeySequence(Qt::Key_Up)}
						&& keys("map.duplicateMoveRight") == QList<QKeySequence> {QKeySequence(Qt::ControlModifier | Qt::Key_Right)}
						&& keys("map.flipHorizontal") == QList<QKeySequence> {QKeySequence(Qt::ControlModifier | Qt::Key_F)},
					"the nudge, copy and flip keys keep their keys beside them");
			} else {
				for (const auto& id : ids) {
					expect(action(id)->shortcuts().isEmpty(), "no camera texture keys outside TrenchBroom", id);
				}
			}

			const QString unchanged = QStringLiteral("unchanged"), changedText = QStringLiteral("changed");
			// Runs `edit`, checks the map is then `wanted` with exactly one more
			// undo step, and undoes it back to the exact bytes. TrenchBroom's
			// selection changes are undo steps too, so depths are relative.
			const auto check = [&](const QByteArray& wanted, const std::function<void()>& edit, const QString& label) {
				const QByteArray start = bytes();
				const auto startDepth = shell.levelDocument().undoStack.size();
				edit();
				const bool settled = settle();
				const QByteArray now = bytes();
				expect(settled && now == wanted && shell.levelDocument().undoStack.size() == startDepth + 1, "the shared service's edit, as one undo step",
					label + QStringLiteral(" (undo %1 -> %2, map %3) ").arg(startDepth).arg(shell.levelDocument().undoStack.size()).arg(now == start ? unchanged : changedText)
						+ shell.statusBar()->currentMessage());
				action(QStringLiteral("map.undo"))->trigger();
				expect(settle() && bytes() == start && shell.levelDocument().undoStack.size() == startDepth, "undo restores the exact map", label);
			};
			// From the menu with the brush selected and no face picked: every
			// face of the Surfaces target.
			select(true);
			for (const auto& id : ids) {
				expect(action(id)->isEnabled(), "enabled with a brush selected", id);
			}
			QVector<LevelSurfaceFace> allFaces;
			for (int i = 0; i < shell.levelDocument().brushes.first().faces.size(); ++i) {
				allFaces.append({brushId, i});
			}
			check(reference(QStringLiteral("map.moveTexturesUp"), allFaces, 16), [&] { action(QStringLiteral("map.moveTexturesUp"))->trigger(); },
				QStringLiteral("Move Textures Up on the Surfaces target"));
			check(reference(QStringLiteral("map.rotateTexturesClockwiseCoarse"), allFaces, 16),
				[&] { action(QStringLiteral("map.rotateTexturesClockwiseCoarse"))->trigger(); },
				QStringLiteral("Rotate Textures Clockwise (Coarse) on the Surfaces target"));
			{
				// A queued flip can be cancelled before it lands.
				const QByteArray start = bytes();
				const auto startDepth = shell.levelDocument().undoStack.size();
				action(QStringLiteral("map.flipTexturesVertically"))->trigger();
				shell.cancelLevelSurfaceEdits();
				expect(settle() && bytes() == start && shell.levelDocument().undoStack.size() == startDepth, "a cancelled flip leaves the map alone");
			}

			// One picked face: only it moves, whatever the Surfaces target says.
			select(false);
			if (!expect(front >= 0 && pick(front), "pick the face towards the camera")) {
				return 1;
			}
			const QVector<LevelSurfaceFace> picked {{brushId, front}};
			for (const auto& id : ids) {
				expect(action(id)->isEnabled(), "enabled with a picked face", id);
			}
			check(reference(QStringLiteral("map.flipTexturesHorizontally"), picked, 16),
				[&] { action(QStringLiteral("map.flipTexturesHorizontally"))->trigger(); }, QStringLiteral("Flip Textures Horizontally on the picked face"));
			check(reference(QStringLiteral("map.rotateTexturesCounterclockwise"), picked, 16),
				[&] { action(QStringLiteral("map.rotateTexturesCounterclockwise"))->trigger(); },
				QStringLiteral("Rotate Textures Counter-clockwise on the picked face"));

#ifdef VIBESTUDIO_HAVE_QTEST
			if (tb && !cameraLive) {
				std::cout << "The offscreen camera has no renderer here; its key checks are skipped." << std::endl;
			}
			if (tb && cameraLive) {
				const auto focusCamera = [&] {
					shell.activateWindow();
					expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
					// An edit re-renders the camera, which is disabled until it can
					// draw again.
					until([&] { return camera->isEnabled(); });
					camera->setFocus(Qt::OtherFocusReason);
					return expect(until([&] { return camera->hasFocus(); }), "camera focus");
				};
				struct Key {
					Qt::Key key;
					Qt::KeyboardModifiers modifiers;
					const char* command;
				};
				const Key presses[] {
					{Qt::Key_Right, Qt::NoModifier, "map.moveTexturesRight"},
					{Qt::Key_Up, Qt::NoModifier, "map.moveTexturesUp"},
					{Qt::Key_Left, Qt::ShiftModifier, "map.moveTexturesLeftCoarse"},
					{Qt::Key_Down, Qt::ControlModifier, "map.moveTexturesDownFine"},
					{Qt::Key_F, Qt::ControlModifier, "map.flipTexturesHorizontally"},
					{Qt::Key_F, Qt::ControlModifier | Qt::AltModifier, "map.flipTexturesVertically"},
					{Qt::Key_PageUp, Qt::NoModifier, "map.rotateTexturesClockwise"},
					{Qt::Key_PageUp, Qt::ShiftModifier, "map.rotateTexturesClockwiseCoarse"},
					{Qt::Key_PageUp, Qt::ControlModifier, "map.rotateTexturesClockwiseFine"},
					{Qt::Key_PageDown, Qt::NoModifier, "map.rotateTexturesCounterclockwise"},
					{Qt::Key_PageDown, Qt::ShiftModifier, "map.rotateTexturesCounterclockwiseCoarse"},
					{Qt::Key_PageDown, Qt::ControlModifier, "map.rotateTexturesCounterclockwiseFine"},
				};
				for (const Key& press : presses) {
					if (!focusCamera()) {
						break;
					}
					const auto brushes = shell.levelDocument().brushes.size();
					check(reference(QString::fromLatin1(press.command), picked, 16), [&] { QTest::keyClick(camera, press.key, press.modifiers); },
						QStringLiteral("the camera key for ") + QString::fromLatin1(press.command));
					expect(shell.levelDocument().brushes.size() == brushes, "the key copies nothing", QString::fromLatin1(press.command));
				}
				// With nothing picked, the arrows go back to nudging the brush. Picks
				// go once the selection changes, even if it later comes back.
				select(false);
				select(true);
				if (focusCamera()) {
					const auto faces = shell.levelDocument().brushes.first().faces;
					const auto startDepth = shell.levelDocument().undoStack.size();
					QTest::keyClick(camera, Qt::Key_Up);
					settle();
					const auto after = shell.levelDocument().brushes.first().faces;
					bool moved = false;
					for (int i = 0; i < faces.size() && i < after.size(); ++i) {
						moved |= faces[i].p0.x != after[i].p0.x || faces[i].p0.y != after[i].p0.y || faces[i].p0.z != after[i].p0.z;
					}
					expect(moved && shell.levelDocument().undoStack.size() == startDepth + 1, "without a picked face Up nudges the brush from the camera");
					action(QStringLiteral("map.undo"))->trigger();
					settle();
				}
				if (focusCamera()) {
					// Page Up goes back to the depth nudge, which moves the brush up.
					const auto start = bytes();
					const auto faces = shell.levelDocument().brushes.first().faces;
					const auto startDepth = shell.levelDocument().undoStack.size();
					QTest::keyClick(camera, Qt::Key_PageUp);
					settle();
					const auto after = shell.levelDocument().brushes.first().faces;
					bool raised = !after.isEmpty() && after.size() == faces.size();
					for (int i = 0; raised && i < faces.size(); ++i) {
						// Texture lock may rewrite the stored values, but the texture's
						// directions stay: no turn.
						const auto was = levelTextureProjection(faces[i]), now = levelTextureProjection(after[i]);
						const auto same = [](const LevelMapVec3& a, const LevelMapVec3& b) {
							return std::abs(a.x - b.x) < 1e-6 && std::abs(a.y - b.y) < 1e-6 && std::abs(a.z - b.z) < 1e-6;
						};
						raised = after[i].p0.z > faces[i].p0.z && same(was.u, now.u) && same(was.v, now.v);
					}
					expect(raised && shell.levelDocument().undoStack.size() == startDepth + 1,
						"without a picked face Page Up nudges the brush up and turns no texture",
					QStringLiteral("undo %1 -> %2, z %3 -> %4, %5")
						.arg(startDepth)
						.arg(shell.levelDocument().undoStack.size())
						.arg(faces.value(0).p0.z)
						.arg(after.value(0).p0.z)
						.arg(shell.statusBar()->currentMessage()));
					action(QStringLiteral("map.undo"))->trigger();
					expect(settle() && bytes() == start, "the nudge undoes exactly");
				}
			}
#endif

			// Hidden objects are not targets (hiding waits for the face picks to go).
			select(false);
			select(true);
			action(QStringLiteral("map.hideSelection"))->trigger();
			expect(until([&] { return !action(QStringLiteral("map.moveTexturesUp"))->isEnabled() && !action(QStringLiteral("map.flipTexturesHorizontally"))->isEnabled(); }),
				"hidden objects are not texture targets");
			action(QStringLiteral("map.showAll"))->trigger();
			shell.close();
		}
		if (pass.scale == 200) {
			app.removeTranslator(&expanded);
			app.setLayoutDirection(Qt::LeftToRight);
		}
	}
	QEventLoop drain;
	QTimer::singleShot(200, &drain, &QEventLoop::quit);
	drain.exec();
	std::cout << (ok ? "Camera texture UI checks passed" : "Camera texture UI checks failed") << std::endl;
	return ok ? 0 : 1;
}
