// The Assemble Brush tool in the shell (app/level_brush_assemble_actions.cpp,
// app/map_viewport_assemble.cpp, app/model_viewport_assemble.cpp):
// registration in the palette, a menu and the Tools tab, TrenchBroom's B;
// one tool across the plan views and the camera, giving way to the clip
// tool; mouse events drawing a grid rectangle in the Top view and extruding
// it with Shift in the Front view, the shared points, the worker-checked hull
// and Enter creating one brush in the current material as one exact undo
// step; clicks, moving a point, Backspace, Escape cancelling a drag, then
// clearing, then leaving; a stale check replaced by the newest; a locked
// creation layer refusing; and, in the camera, grid points on faces, a face
// rectangle, Shift-drag extrusion, a face's corners, the construction plane
// where no face is, and a hidden brush staying out of reach. TrenchBroom,
// NetRadiant Custom and VibeStudio profiles at 100%, and at 200% with high
// contrast, expanded translations and right-to-left layout. Owned offscreen
// widgets only.
#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_document.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/studio_semantics.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTranslator>

#include <algorithm>
#include <cmath>
#include <functional>
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

bool settle(ModelViewport* view)
{
	return until([&] {
		QImage image(view->size(), QImage::Format_ARGB32_Premultiplied);
		view->render(&image);
		return view->isEnabled() && !view->isRendering();
	});
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

bool has(const QVector<LevelMapVec3>& points, double x, double y, double z)
{
	return std::any_of(points.cbegin(), points.cend(), [&](const LevelMapVec3& p) {
		return std::abs(p.x - x) < 1e-6 && std::abs(p.y - y) < 1e-6 && std::abs(p.z - z) < 1e-6;
	});
}

QString text(const QVector<LevelMapVec3>& points)
{
	QStringList parts;
	for (const auto& p : points) {
		parts << QStringLiteral("(%1 %2 %3)").arg(p.x).arg(p.y).arg(p.z);
	}
	return parts.join(QLatin1Char(' '));
}

void mouse(QWidget* widget, QEvent::Type type, const QPointF& position, Qt::MouseButton button, Qt::MouseButtons buttons,
	Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
	QMouseEvent event(type, position, widget->mapToGlobal(position), button, buttons, modifiers);
	QApplication::sendEvent(widget, &event);
}

void drag(QWidget* widget, const QPointF& from, const QPointF& to, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
	mouse(widget, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, modifiers);
	mouse(widget, QEvent::MouseMove, (from + to) / 2, Qt::NoButton, Qt::LeftButton, modifiers);
	mouse(widget, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, modifiers);
	mouse(widget, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, modifiers);
}

void click(QWidget* widget, const QPointF& at)
{
	mouse(widget, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
	mouse(widget, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
}

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
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference("four-views");
	// Brush 0, a 64 cube at the origin, and brush 1 on a locked layer.
	LevelMapDocument created;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	QString error;
	QString layer;
	if (!expect(createLevelMap(create, &created, &error)
				&& addLevelMapBoxBrush(&created, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("studio/box"), nullptr, &error)
				&& addLevelMapBoxBrush(&created, {128, 0, 0, true}, {192, 32, 64, true}, QStringLiteral("studio/box"), nullptr, &error)
				&& createLevelSceneNode(&created, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
				&& assignLevelSceneObjects(&created, layer, {QStringLiteral("brush:1")}, &error) && setLevelSceneLocked(&created, layer, true, &error),
			"fixture", error)) {
		return 1;
	}
	const QString path = temp.filePath("assemble.map");
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(serializeLevelMap(created).bytes) <= 0) {
		return 1;
	}
	file.close();
	const QStringList ids {QStringLiteral("map.assembleBrushTool"), QStringLiteral("map.createAssembledBrush")};

	for (const auto* profile : {"trenchbroom", "netradiant-custom", "vibestudio-default"}) {
		for (int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Assemble brush UI " << qPrintable(phase) << std::endl;
			const bool trenchBroom = QLatin1String(profile) == QLatin1String("trenchbroom");
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
				shell.resize(1700, 1100);
				shell.show();
				shell.openPathFromCommandLine(path);
				const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
				action(QStringLiteral("shell.mode.levels"))->trigger();
				action(QStringLiteral("map.layout.four-views"))->trigger();
				auto* top = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
				auto* front = shell.findChild<MapViewport*>(QStringLiteral("mapViewport1"));
				auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
				if (!expect(top && front && camera && until([&] { return shell.levelDocument().brushes.size() == 2 && camera->hasMesh(); }),
						"map ready")) {
					return 1;
				}
				if (!expect(top->projection() == MapViewportProjection::TopXY && front->projection() == MapViewportProjection::FrontXZ, "pane projections")) {
					return 1;
				}
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				const auto focus = [&](QWidget* view) {
					shell.activateWindow();
					expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
					view->setFocus(Qt::OtherFocusReason);
					expect(until([&] { return view->hasFocus(); }), "view takes focus", view->objectName());
				};
				const auto key = [&](QWidget* view, Qt::Key code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
					focus(view);
					QTest::keyClick(view, code, modifiers);
				};
				const auto shared = [&] {
					const auto& state = top->brushAssembleState();
					return front->brushAssembleState() == state && camera->brushAssembleState() == state;
				};
				const auto points = [&] { return top->brushAssembleState().points; };
				const auto ready = [&] { return until([&] { return top->brushAssemblePreview().status == LevelBrushAssemblePreview::Status::Ready; }); };
				auto* paint = shell.findChild<QComboBox*>(QStringLiteral("levelPaintMaterial"));
				if (expect(paint != nullptr, "current material picker")) {
					paint->setEditText(QStringLiteral("studio/assembled"));
				}
				const auto baseline = bytes();
				const auto depth = shell.levelDocument().undoStack.size();

				// Registration: palette, a menu and the Tools tab; TB's B.
				for (const QString& id : ids) {
					auto* command = action(id);
					if (!expect(command != nullptr, "registered", id)) {
						return 1;
					}
					bool menu = false;
					for (auto* owner : command->associatedObjects()) {
						menu |= qobject_cast<QMenu*>(owner) != nullptr;
					}
					auto* row = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-") + id);
					CommandPaletteEntry entry;
					expect(!command->text().isEmpty() && !command->statusTip().isEmpty() && menu && commandPaletteEntryForCommandId(id, &entry)
							&& !entry.label.isEmpty() && !entry.summary.isEmpty(),
						"labelled, in the palette and a menu", id);
					expect(row && row->focusPolicy() != Qt::NoFocus && !row->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(row),
						"accessible Tools tile", id);
				}
				auto* tool = action(QStringLiteral("map.assembleBrushTool"));
				auto* createBrush = action(QStringLiteral("map.createAssembledBrush"));
				expect(tool->isCheckable() && tool->isEnabled() && !tool->isChecked() && !createBrush->isEnabled(), "tool ready, create waits for points");
				expect((tool->shortcut() == QKeySequence(QStringLiteral("B"))) == trenchBroom, "B is TrenchBroom's Brush Tool key",
					tool->shortcut().toString());

				// One tool in every view.
				if (trenchBroom) {
					key(top, Qt::Key_B);
				} else {
					tool->trigger();
				}
				expect(top->brushAssembleTool() && front->brushAssembleTool() && camera->brushAssembleTool() && tool->isChecked(), "the tool is on everywhere");
				expect(top->accessibleDescription().contains(QStringLiteral("Assemble Brush")) && camera->accessibleDescription().contains(QStringLiteral("Assemble Brush"))
						&& !shell.statusBar()->currentMessage().isEmpty(),
					"views describe the tool", top->accessibleDescription());

				// The Top view draws a grid rectangle at the work zone's floor.
				drag(top, top->viewPointFor(1, 2), top->viewPointFor(47, 31));
				expect(shared() && points().size() == 4 && has(points(), 0, 0, 0) && has(points(), 48, 0, 0) && has(points(), 48, 32, 0) && has(points(), 0, 32, 0),
					"a grid rectangle shared by every view", text(points()));
				expect(until([&] { return top->brushAssemblePreview().status == LevelBrushAssemblePreview::Status::Empty && !top->brushAssemblePreview().message.isEmpty(); })
						&& top->brushAssemblePreview().shape == LevelBrushAssembleShape::Polygon && createBrush->isEnabled()
						&& top->accessibleDescription().contains(top->brushAssemblePreview().message),
					"a flat outline is explained, not a brush", top->brushAssemblePreview().message);
				// Shift-drag in the Front view extrudes it up 64 units.
				mouse(front, QEvent::MouseButtonPress, front->viewPointFor(24, 0), Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
				mouse(front, QEvent::MouseMove, front->viewPointFor(24, 31), Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
				expect(shared() && points().size() == 8 && has(points(), 48, 32, 32), "first extrusion move", text(points()));
				mouse(front, QEvent::MouseMove, front->viewPointFor(24, 63), Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
				expect(shared() && points().size() == 8 && has(points(), 48, 32, 64), "second extrusion move keeps the original plane", text(points()));
				mouse(front, QEvent::MouseMove, front->viewPointFor(24, 47), Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
				expect(shared() && points().size() == 8 && has(points(), 48, 32, 48), "extrusion can shrink during the same drag", text(points()));
				mouse(front, QEvent::MouseButtonRelease, front->viewPointFor(24, 63), Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
				expect(shared() && points().size() == 8 && has(points(), 0, 0, 64) && has(points(), 48, 32, 64), "Shift-drag extrudes the outline", text(points()));
				expect(ready() && top->brushAssemblePreview().corners == 8 && top->brushAssemblePreview().faces == 6 && camera->brushAssemblePreview().status
							== LevelBrushAssemblePreview::Status::Ready && createBrush->isEnabled() && top->accessibleDescription().contains(top->brushAssemblePreview().message),
					"the worker checks the hull", top->brushAssemblePreview().message);
				// Enter creates it: one step, selected, in the current material.
				key(front, Qt::Key_Return);
				expect(until([&] { return shell.levelDocument().brushes.size() == 3; }), "Enter creates the brush", shell.statusBar()->currentMessage());
				{
					const auto& document = shell.levelDocument();
					const auto& brush = document.brushes.last();
					expect(document.undoStack.size() == depth + 1 && document.selection.size() == 1 && document.selection.first().objectId == brush.id
							&& brush.faceCount == 6 && brush.mins.x == 0 && brush.maxs.x == 48 && brush.mins.y == 0 && brush.maxs.y == 32 && brush.mins.z == 0
							&& brush.maxs.z == 64
							&& std::all_of(brush.faces.cbegin(), brush.faces.cend(), [](const LevelMapBrushFace& face) { return face.textureName == QStringLiteral("studio/assembled"); }),
						"one step adds the selected box in the current material", shell.statusBar()->currentMessage());
				}
				expect(points().isEmpty() && shared() && top->brushAssembleTool() && !createBrush->isEnabled(), "the points clear; the tool stays");
				action(QStringLiteral("map.undo"))->trigger();
				expect(bytes() == baseline && shell.levelDocument().undoStack.size() == depth, "undo restores the exact map");

				// Clicks, moving a point, Backspace and Escape.
				click(top, top->viewPointFor(64, 96));
				click(top, top->viewPointFor(96, 97));
				expect(points().size() == 2 && has(points(), 64, 96, 0) && has(points(), 96, 96, 0) && shared(), "clicks add grid points", text(points()));
				drag(top, top->brushAssemblePointPosition(1), top->viewPointFor(112, 128));
				expect(points().size() == 2 && has(points(), 112, 128, 0) && !has(points(), 96, 96, 0) && shared(), "a point moves in the view's plane", text(points()));
				key(top, Qt::Key_Backspace);
				expect(points().size() == 1 && has(points(), 64, 96, 0) && shared(), "Backspace removes the last point", text(points()));
				mouse(top, QEvent::MouseButtonPress, top->viewPointFor(0, 0), Qt::LeftButton, Qt::LeftButton);
				mouse(top, QEvent::MouseMove, top->viewPointFor(32, 32), Qt::NoButton, Qt::LeftButton);
				expect(points().size() == 5 && top->isAssemblingBrush(), "a drag in progress", text(points()));
				// The press gave the view focus; Escape goes straight to it.
				QTest::keyClick(top, Qt::Key_Escape);
				mouse(top, QEvent::MouseButtonRelease, top->viewPointFor(32, 32), Qt::LeftButton, Qt::NoButton);
				expect(points().size() == 1 && shared() && top->brushAssembleTool(), "Escape cancels the drag", text(points()));
				key(top, Qt::Key_Escape);
				expect(points().isEmpty() && shared() && top->brushAssembleTool(), "Escape clears the points");
				key(top, Qt::Key_Escape);
				expect(!top->brushAssembleTool() && !front->brushAssembleTool() && !camera->brushAssembleTool() && !tool->isChecked(), "Escape leaves the tool");
				expect(bytes() == baseline, "collecting points changes nothing");

				// One tool at a time.
				tool->trigger();
				action(QStringLiteral("map.clipTool"))->trigger();
				expect(top->clipMode() && !top->brushAssembleTool() && !camera->brushAssembleTool() && !tool->isChecked(), "the clip tool replaces it");
				tool->trigger();
				expect(!top->clipMode() && !camera->clipMode() && top->brushAssembleTool() && tool->isChecked(), "it replaces the clip tool");

				// A newer set of points replaces a check still running.
				LevelBrushAssembleState first;
				first.points = {{256, 0, 0, true}, {320, 0, 0, true}, {256, 64, 0, true}, {256, 0, 64, true}};
				LevelBrushAssembleState second;
				second.points = {{256, 0, 0, true}, {288, 0, 0, true}, {288, 32, 0, true}, {256, 32, 0, true}, {256, 0, 16, true}, {288, 0, 16, true},
					{288, 32, 16, true}, {256, 32, 16, true}};
				top->setBrushAssembleState(first);
				front->setBrushAssembleState(second);
				expect(shared() && top->brushAssembleState() == second && ready() && top->brushAssemblePreview().corners == 8, "the newest points are checked",
					top->brushAssemblePreview().message);
				// A locked creation layer refuses the brush.
				auto* creation = shell.findChild<QComboBox*>(QStringLiteral("levelSceneCreation"));
				const int locked = creation ? creation->findText(QStringLiteral("Locked"), Qt::MatchStartsWith) : -1;
				if (expect(creation && locked >= 0, "the locked layer is a creation choice")) {
					creation->setCurrentIndex(locked);
					key(top, Qt::Key_Return);
					expect(until([&] { return top->brushAssemblePreview().status == LevelBrushAssemblePreview::Status::Refused; })
							&& shell.levelDocument().brushes.size() == 2 && bytes() == baseline && !top->brushAssemblePreview().message.isEmpty(),
						"a locked layer refuses it", top->brushAssemblePreview().message);
					creation->setCurrentIndex(0);
				}
				key(top, Qt::Key_Return);
				expect(until([&] { return shell.levelDocument().brushes.size() == 3; }) && shell.levelDocument().brushes.last().maxs.z == 16, "then it is created",
					shell.statusBar()->currentMessage());
				action(QStringLiteral("map.undo"))->trigger();
				expect(bytes() == baseline, "undo again");

				// The camera: grid points on faces, a face rectangle, extrusion.
				if (trenchBroom) {
					camera->setCameraView({-48, 32, 150}, 0, -47);
					if (!expect(settle(camera), "camera settles")) {
						return 1;
					}
					const auto at = [&](double x, double y, double z) { return camera->componentGizmoFrame().project({x, y, z, true}); };
					expect(camera->beginBrushAssemble(at(13, 21, 64)), "press on the top face");
					camera->finishBrushAssemble(true);
					expect(points().size() == 1 && has(points(), 16, 16, 64) && shared(), "a grid point on the face", text(points()));
					key(camera, Qt::Key_Escape);
					expect(points().isEmpty(), "Escape clears the camera's points");
					expect(camera->beginBrushAssemble(at(5, 7, 64)) && camera->updateBrushAssemble(at(40, 30, 64)), "drag on the top face");
					camera->finishBrushAssemble(true);
					expect(points().size() == 4 && has(points(), 0, 0, 64) && has(points(), 48, 32, 64) && shared(), "a grid rectangle on the face", text(points()));
					expect(camera->beginBrushAssemble(at(20, 20, 64), Qt::ShiftModifier) && camera->updateBrushAssemble(at(20, 20, 96)), "Shift-drag the outline");
					camera->finishBrushAssemble(true);
					expect(points().size() == 8 && has(points(), 0, 0, 96) && has(points(), 48, 32, 96) && shared(), "the outline extruded 32 units", text(points()));
					expect(ready(), "the camera's hull is checked", top->brushAssemblePreview().message);
					key(camera, Qt::Key_Return);
					expect(until([&] { return shell.levelDocument().brushes.size() == 3; }) && shell.levelDocument().brushes.last().mins.z == 64
							&& shell.levelDocument().brushes.last().maxs.z == 96 && shell.levelDocument().undoStack.size() == depth + 1,
						"Enter in the camera creates it", shell.statusBar()->currentMessage());
					action(QStringLiteral("map.undo"))->trigger();
					expect(bytes() == baseline, "camera undo");
					camera->setCameraView({-48, 32, 150}, 0, -47);
					expect(settle(camera), "camera settles after undo");
					expect(camera->addBrushAssembleFace(at(32, 32, 64)) && points().size() == 4 && has(points(), 0, 0, 64) && has(points(), 64, 64, 64),
						"double-click adds a face's corners", text(points()));
					camera->clearBrushAssemblePoints();
					// No face under the pointer: the construction plane, XY at 0.
					camera->setBrushDrawPlane(2, 0, 64);
					expect(camera->beginBrushAssemble(at(224, 96, 0)), "press on empty space");
					camera->finishBrushAssemble(true);
					expect(points().size() == 1 && has(points(), 224, 96, 0), "a grid point on the construction plane", text(points()));
					camera->clearBrushAssemblePoints();
					// A hidden brush is out of reach.
					top->setSelectionSet({{LevelMapSelectionKind::QuakeBrush, 0}});
					top->selectionSetChanged({{LevelMapSelectionKind::QuakeBrush, 0}});
					action(QStringLiteral("map.hideSelection"))->trigger();
					expect(settle(camera) && top->brushAssembleTool(), "hidden brush rendered away");
					camera->setBrushDrawPlane(2, 0, 64);
					expect(camera->beginBrushAssemble(at(32, 32, 64)), "press where the hidden brush was");
					camera->finishBrushAssemble(true);
					expect(points().size() == 1 && points().first().z == 0, "the point falls through to the construction plane", text(points()));
					action(QStringLiteral("map.showAll"))->trigger();
					camera->clearBrushAssemblePoints();
				}
				tool->trigger();
				expect(!top->brushAssembleTool() && !camera->brushAssembleTool(), "the tool turns off");
				expect(bytes().size() > 0 && shell.levelDocument().brushes.size() == 2, "the map keeps its brushes");
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}
	std::cout << (ok ? "level brush assemble UI smoke passed" : "level brush assemble UI smoke FAILED") << std::endl;
	return ok ? 0 : 1;
}
