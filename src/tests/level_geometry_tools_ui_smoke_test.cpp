// The Levels page's geometry, patch, region, navigation and prefab tools in
// the real shell: vertex, edge and face handles in the 2D views, Snap
// Vertices, Hollow Touching and Make Room, the patch matrix commands and
// Thicken, drawn, brush-made and typed regions that the map remembers,
// Centre, Go to Coordinates and Find Brush, faces picked in the camera for
// the Surfaces tab, patch shapes in the Shapes tab, and prefabs dragged onto
// a 2D view or put in the selection's place. Qt events are sent to the
// widgets directly; no native input or screen capture is used.

#include "app/application_shell.h"
#include "app/level_surface_tools.h"
#include "app/map_viewport.h"
#include "app/map_viewport_projection.h"
#include "app/model_viewport.h"
#include "core/level_prefab.h"
#include "tests/map_viewport_test_helpers.h"
#include "core/map_preview_mesh.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool condition, const char* message, const QString& detail = {})
{
	if (!condition) {
		std::cerr << message << ": " << detail.toStdString() << '\n';
		ok = false;
	}
	return condition;
}
void drain(int ms = 20)
{
	QEventLoop loop;
	QTimer::singleShot(ms, &loop, &QEventLoop::quit);
	loop.exec(QEventLoop::ExcludeUserInputEvents);
}
bool until(const std::function<bool()>& ready, int timeoutMs = 45000)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < timeoutMs) {
		drain();
	}
	return ready();
}
// An axis-aligned Quake III brush, each face's points wound out of the box.
QString box(std::array<double, 3> lo, std::array<double, 3> hi, const QString& material, const QString& mapping = QStringLiteral("0 0 0 0.5 0.5"))
{
	QStringList lines {QStringLiteral("{")};
	const auto point = [](const std::array<double, 3>& v) {
		return QStringLiteral("( %1 %2 %3 )").arg(v[0], 0, 'g', 10).arg(v[1], 0, 'g', 10).arg(v[2], 0, 'g', 10);
	};
	for (int axis = 0; axis < 3; ++axis) {
		for (const bool positive : {false, true}) {
			std::array<double, 3> p = lo;
			p[axis] = positive ? hi[axis] : lo[axis];
			auto a = p;
			auto b = p;
			int first = (axis + 1) % 3;
			int second = (axis + 2) % 3;
			if (positive) {
				std::swap(first, second);
			}
			a[first] += 1;
			b[second] += 1;
			lines << QStringLiteral("%1 %2 %3 %4 %5 0 0 0").arg(point(p), point(a), point(b), material, mapping);
		}
	}
	lines << QStringLiteral("}");
	return lines.join(QLatin1Char('\n'));
}
// A flat 3 x 3 patch from (-128, -128) to (-64, -64) at height 0.
QString flatPatch()
{
	QStringList rows;
	for (int i = 0; i < 3; ++i) {
		QStringList points;
		for (int j = 0; j < 3; ++j) {
			points << QStringLiteral("( %1 %2 0 %3 %4 )").arg(-128 + 32 * i).arg(-128 + 32 * j).arg(i / 2.0).arg(j / 2.0);
		}
		rows << QStringLiteral("( %1 )").arg(points.join(QLatin1Char(' ')));
	}
	return QStringLiteral("{\npatchDef2\n{\nstudio/patch\n( 3 3 0 0 0 )\n(\n%1\n)\n}\n}").arg(rows.join(QLatin1Char('\n')));
}
const LevelMapBrush* brushById(const LevelMapDocument& document, int id)
{
	for (const LevelMapBrush& brush : document.brushes) {
		if (brush.id == id) {
			return &brush;
		}
	}
	return nullptr;
}
const LevelMapPatch* patchById(const LevelMapDocument& document, int id)
{
	for (const LevelMapPatch& patch : document.patches) {
		if (patch.id == id) {
			return &patch;
		}
	}
	return nullptr;
}
bool selects(const LevelMapDocument& document, LevelMapSelectionKind kind, int id)
{
	return std::any_of(document.selection.cbegin(), document.selection.cend(),
		[&](const LevelMapSelectionRef& ref) { return ref.kind == kind && ref.objectId == id; });
}
bool near(double a, double b, double tolerance = 0.01) { return std::abs(a - b) <= tolerance; }
void mouse(QWidget* view, QEvent::Type type, const QPointF& at, Qt::MouseButton button, Qt::MouseButtons buttons)
{
	QMouseEvent event(type, at, view->mapToGlobal(at), button, buttons, Qt::NoModifier);
	QCoreApplication::sendEvent(view, &event);
}
void key(QWidget* view, int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
	QKeyEvent event(QEvent::KeyPress, code, modifiers);
	QCoreApplication::sendEvent(view, &event);
}
// With VIBESTUDIO_TEST_CAPTURE_DIR set, the view as drawn is saved for review.
void capture(MapViewport* view, const QString& name)
{
	const QString directory = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
	if (directory.isEmpty() || !view) {
		return;
	}
	QDir().mkpath(directory);
	QImage image(view->size() * view->devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(view->devicePixelRatioF());
	image.fill(Qt::transparent);
	tests::settleMapViewport(*view, &image);
	expect(image.save(QDir(directory).filePath(name + QStringLiteral(".png"))), "capture saved", name);
}
} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	// Qt's own file dialog, which the test can answer.
	QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
	QApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	settings.sync();
	QString error;

	// Entity 0 holds brushes 0 and 1 (1 off the grid) and patch 0; entity 1,
	// a door, brushes 2 and 3; entity 2 is a light.
	const QString text = QStringLiteral("{\n\"classname\" \"worldspawn\"\n") + box({0, 0, 0}, {64, 64, 64}, QStringLiteral("studio/wall"), QStringLiteral("7 3 15 0.25 0.75"))
		+ QLatin1Char('\n') + box({128.5, 0.25, 0}, {192, 64, 64.5}, QStringLiteral("studio/wall")) + QLatin1Char('\n') + flatPatch()
		+ QStringLiteral("\n}\n{\n\"classname\" \"func_door\"\n") + box({256, 0, 0}, {320, 64, 64}, QStringLiteral("studio/door")) + QLatin1Char('\n')
		+ box({256, 128, 0}, {320, 192, 64}, QStringLiteral("studio/door"))
		+ QStringLiteral("\n}\n{\n\"classname\" \"light\"\n\"origin\" \"32 32 128\"\n\"light\" \"300\"\n}\n");
	const QString mapPath = temp.filePath(QStringLiteral("tools.map"));
	{
		QFile file(mapPath);
		if (!expect(file.open(QIODevice::WriteOnly) && file.write(text.toUtf8()) == text.toUtf8().size(), "fixture written")) {
			return 1;
		}
	}
	// A one-brush prefab for the drop and replace checks.
	const QString prefabPath = temp.filePath(QStringLiteral("crate.vprefab"));
	{
		LevelMapDocument piece;
		LevelMapCreateRequest create;
		create.game = QStringLiteral("quake3");
		create.starterRoom = false;
		LevelPrefab prefab;
		LevelPrefabCreateRequest request;
		request.name = QStringLiteral("Crate");
		expect(createLevelMap(create, &piece, &error) && addLevelMapBoxBrush(&piece, {-16, -16, 0, true}, {16, 16, 32, true}, QStringLiteral("studio/crate"), nullptr, &error)
				&& createLevelPrefab(piece, request, &prefab, nullptr, &error) && writeLevelPrefab(prefab, prefabPath, true, false, nullptr, &error),
			"prefab fixture", error);
	}

	ApplicationShell shell;
	shell.resize(1700, 1100);
	shell.show();
	shell.openPathFromCommandLine(mapPath);
	expect(until([&] { return shell.levelDocument().sourcePath == mapPath && shell.levelDocument().brushes.size() == 4 && shell.levelDocument().patches.size() == 1; }),
		"Quake III fixture loaded");
	const auto command = [&](const char* id) { return shell.findChild<QAction*>(QString::fromLatin1(id)); };
	command("shell.mode.levels")->trigger();
	MapViewport* top = nullptr;
	const QList<MapViewport*> views = shell.findChildren<MapViewport*>();
	for (MapViewport* view : views) {
		if (view->projection() == MapViewportProjection::TopXY && view->isVisible() && (!top || view->objectName() == QStringLiteral("mapViewport"))) {
			top = view;
		}
	}
	if (!expect(top && views.size() >= 3, "three 2D views")) {
		return 1;
	}
	const QByteArray original = serializeLevelMap(shell.levelDocument()).bytes;
	const auto document = [&]() -> const LevelMapDocument& { return shell.levelDocument(); };
	const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
	const auto select = [&](const QVector<LevelMapSelectionRef>& refs) {
		top->setSelectionSet(refs);
		top->selectionSetChanged(top->selectionSet());
	};
	const auto undoAll = [&] {
		for (int guard = 0; guard < 32 && !shell.levelDocument().undoStack.isEmpty(); ++guard) {
			command("map.undo")->trigger();
		}
		return bytes() == original;
	};
	// Modal dialogs are answered from the event loop, as a person would.
	std::function<bool(QWidget*)> onModal;
	QTimer drive;
	drive.setInterval(30);
	QObject::connect(&drive, &QTimer::timeout, &shell, [&] {
		QWidget* modal = QApplication::activeModalWidget();
		if (!modal || !onModal) {
			return;
		}
		auto handler = std::move(onModal);
		onModal = nullptr;
		if (!handler(modal)) {
			if (auto* dialog = qobject_cast<QDialog*>(modal)) {
				dialog->reject();
			}
		}
	});
	drive.start();
	const auto withDialog = [&](QAction* action, const std::function<bool(QWidget*)>& handler) {
		bool handled = false;
		onModal = [&handled, handler](QWidget* modal) { return handled = handler(modal); };
		QTimer watchdog;
		watchdog.setSingleShot(true);
		QObject::connect(&watchdog, &QTimer::timeout, [] {
			if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
				dialog->reject();
			}
		});
		watchdog.start(30000);
		action->trigger();
		watchdog.stop();
		onModal = nullptr;
		return handled;
	};

	// Commands follow the map and the selection.
	select({});
	expect(command("map.vertexTool")->isEnabled() && command("map.regionDraw")->isEnabled() && command("map.findObject")->isEnabled()
			&& !command("map.thickenPatch")->isEnabled() && !command("map.patchTranspose")->isEnabled() && !command("map.centerOnSelection")->isEnabled()
			&& !command("map.regionSelect")->isEnabled() && !command("map.replaceWithPrefab")->isEnabled() && !command("map.snapVertices")->isEnabled(),
		"geometry commands follow the map and the selection");
	for (const char* id : {"map.vertexTool", "map.edgeTool", "map.faceTool", "map.snapVertices", "map.hollowTouching", "map.makeRoom", "map.thickenPatch",
			 "map.patchInsertColumns", "map.patchSmoothRows", "map.regionDraw", "map.regionSetBrush", "map.regionBounds", "map.regionSelect", "map.zoomToFit",
			 "map.centerOnSelection", "map.goToCoordinates", "map.findObject", "map.replaceWithPrefab", "map.selectWholeEntities"}) {
		QAction* action = command(id);
		expect(action && !action->text().isEmpty() && !action->statusTip().isEmpty(), "every new command is named and explained", QString::fromLatin1(id));
	}

	// Vertex editing: pick the (64, 64) corner from above, nudge it, drag it.
	select({{LevelMapSelectionKind::QuakeBrush, 0}});
	QAction* vertexTool = command("map.vertexTool");
	vertexTool->trigger();
	bool allViews = true;
	for (MapViewport* view : views) {
		allViews &= view->componentMode() == MapViewport::ComponentMode::Vertices;
	}
	expect(vertexTool->isChecked() && allViews && top->componentHandleCount() == 8, "Edit Vertices shows a box's eight corners in every view",
		QString::number(top->componentHandleCount()));
	const auto cornerHandle = [&] {
		// Rightmost, then highest on screen: largest X, then largest Y.
		int best = -1;
		for (int i = 0; i < top->componentHandleCount(); ++i) {
			const QPointF at = top->componentHandlePosition(i);
			if (best < 0 || at.x() > top->componentHandlePosition(best).x() + 0.5
				|| (std::abs(at.x() - top->componentHandlePosition(best).x()) <= 0.5 && at.y() < top->componentHandlePosition(best).y())) {
				best = i;
			}
		}
		return best;
	};
	double leftPixel = 1e9;
	double rightPixel = -1e9;
	double topPixel = 1e9;
	double bottomPixel = -1e9;
	for (int i = 0; i < top->componentHandleCount(); ++i) {
		const QPointF at = top->componentHandlePosition(i);
		leftPixel = std::min(leftPixel, at.x());
		rightPixel = std::max(rightPixel, at.x());
		topPixel = std::min(topPixel, at.y());
		bottomPixel = std::max(bottomPixel, at.y());
	}
	const QPointF corner = top->componentHandlePosition(cornerHandle());
	mouse(top, QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton);
	mouse(top, QEvent::MouseButtonRelease, corner, Qt::LeftButton, Qt::NoButton);
	int sharedPicks = 0;
	for (MapViewport* view : views) {
		sharedPicks += view != top && view->pickedComponentCount() == 2 ? 1 : 0;
	}
	expect(top->pickedComponentCount() == 2 && sharedPicks == static_cast<int>(views.size()) - 1,
		"a click picks the corners stacked under it, and the other views show the picks", QString::number(top->pickedComponentCount()));
	for (MapViewport* view : views) {
		capture(view, QStringLiteral("vertex-picks-%1").arg(static_cast<int>(view->projection())));
	}
	const double grid = top->gridSize();
	const qsizetype steps = document().undoStack.size();
	key(top, Qt::Key_Right);
	expect(until([&] { return brushById(document(), 0) && near(brushById(document(), 0)->maxs.x, 64 + grid); }) && document().undoStack.size() == steps + 1,
		"the arrow keys move the picked corners a grid step as one undo step");
	expect(top->pickedComponentCount() == 2, "the picks follow the corners they moved");
	// A drag on a picked corner lands it on the grid, two steps further.
	const double pixelsPerUnit = (rightPixel - leftPixel) / 64.0;
	const QPointF grab = top->componentHandlePosition(cornerHandle());
	const QPointF drop = grab + QPointF(2 * grid * pixelsPerUnit, 0);
	mouse(top, QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton);
	mouse(top, QEvent::MouseMove, grab + QPointF(1, 0), Qt::NoButton, Qt::LeftButton);
	mouse(top, QEvent::MouseMove, drop, Qt::NoButton, Qt::LeftButton);
	mouse(top, QEvent::MouseButtonRelease, drop, Qt::LeftButton, Qt::NoButton);
	expect(until([&] { return near(brushById(document(), 0)->maxs.x, 64 + 3 * grid); }) && document().undoStack.size() == steps + 2,
		"a drag moves the picked corners onto the grid as one undo step", QString::number(brushById(document(), 0)->maxs.x));
	// Shift+arrow is eight steps, ahead of a profile's texture-shift shortcut.
	key(top, Qt::Key_Right, Qt::ShiftModifier);
	expect(until([&] { return near(brushById(document(), 0)->maxs.x, 64 + 11 * grid); }) && document().undoStack.size() == steps + 3
			&& brushById(document(), 0)->faces.first().shiftX == 7,
		"Shift+arrow moves the picks eight steps and shifts no texture", QString::number(brushById(document(), 0)->maxs.x));
	key(top, Qt::Key_Escape);
	expect(top->pickedComponentCount() == 0 && top->componentMode() == MapViewport::ComponentMode::Vertices, "Escape drops the picks first");
	key(top, Qt::Key_Escape);
	allViews = true;
	for (MapViewport* view : views) {
		allViews &= view->componentMode() == MapViewport::ComponentMode::Off;
	}
	expect(allViews && !vertexTool->isChecked(), "a second Escape leaves vertex editing in every view");
	expect(undoAll(), "undo restores the brush exactly");
	select({{LevelMapSelectionKind::QuakeBrush, 0}});
	command("map.edgeTool")->trigger();
	const int edges = top->componentHandleCount();
	command("map.faceTool")->trigger();
	const int faces = top->componentHandleCount();
	command("map.faceTool")->trigger();
	expect(edges == 12 && faces == 6 && top->componentMode() == MapViewport::ComponentMode::Off && !command("map.faceTool")->isChecked(),
		"edge and face modes show a box's twelve edges and six faces, and toggle off", QStringLiteral("%1 %2").arg(edges).arg(faces));
	select({{LevelMapSelectionKind::QuakePatch, 0}});
	vertexTool->trigger();
	expect(top->componentHandleCount() == 9, "a patch shows its nine control points", QString::number(top->componentHandleCount()));
	capture(top, QStringLiteral("patch-control-points"));
	vertexTool->trigger();

	// Snap Vertices rounds the off-grid brush.
	select({{LevelMapSelectionKind::QuakeBrush, 1}});
	command("map.snapVertices")->trigger();
	const LevelMapBrush* snapped = brushById(document(), 1);
	expect(snapped && near(snapped->mins.x, 128) && near(snapped->mins.y, 0) && near(snapped->maxs.z, 64) && document().undoStack.size() == 1,
		"Snap Vertices puts the brush on the grid in one step");
	expect(undoAll(), "undo restores the off-grid brush");

	// Hollow Touching asks for a thickness; Make Room builds outside walls.
	select({{LevelMapSelectionKind::QuakeBrush, 0}});
	expect(withDialog(command("map.hollowTouching"), [](QWidget* modal) {
		auto* input = qobject_cast<QInputDialog*>(modal);
		if (!input) {
			return false;
		}
		input->setIntValue(8);
		input->accept();
		return true;
	}) && until([&] { return document().brushes.size() == 9; }) && document().undoStack.size() == 1,
		"Hollow Touching makes six walls in one step", QString::number(document().brushes.size()));
	expect(undoAll(), "undo restores the hollowed brush");
	select({{LevelMapSelectionKind::QuakeBrush, 0}});
	command("map.makeRoom")->trigger();
	double lowest = 0;
	for (const LevelMapBrush& brush : document().brushes) {
		lowest = std::min(lowest, brush.mins.x);
	}
	expect(document().brushes.size() == 9 && !brushById(document(), 0) && near(lowest, -grid) && document().undoStack.size() == 1,
		"Make Room puts six walls outside the brush and removes it", QString::number(lowest));
	expect(undoAll(), "undo restores the room's brush");

	// Expand to whole entities, and Find Brush by compiler numbers.
	select({{LevelMapSelectionKind::QuakeBrush, 2}});
	command("map.selectWholeEntities")->trigger();
	expect(document().selection.size() == 1 && selects(document(), LevelMapSelectionKind::Entity, 1),
		"Expand Selection to Whole Entities selects the whole door");
	const auto find = [&](const QString& message) {
		return withDialog(command("map.findObject"), [message](QWidget* modal) {
			auto* field = modal->findChild<QLineEdit*>(QStringLiteral("levelFindMessage"));
			if (!field) {
				return false;
			}
			field->setText(message);
			static_cast<QDialog*>(modal)->accept();
			return true;
		});
	};
	select({});
	expect(find(QStringLiteral("Entity 1, Brush 1: mixed face contents")) && document().selection.size() == 1
			&& selects(document(), LevelMapSelectionKind::QuakeBrush, 3),
		"Find Brush reads a compiler line");
	expect(find(QStringLiteral("Entity 0, Brush 2")) && selects(document(), LevelMapSelectionKind::QuakePatch, 0),
		"brushes and patches are counted together, as compilers count them");

	// Navigation.
	select({{LevelMapSelectionKind::QuakeBrush, 0}});
	command("map.centerOnSelection")->trigger();
	bool centred = true;
	for (MapViewport* view : views) {
		if (view->hasDocument()) {
			const QPointF expected = mapViewportProjectPoint(view->projection(), {32, 32, 32, true});
			centred &= near(view->navigationState().center.x(), expected.x()) && near(view->navigationState().center.y(), expected.y());
		}
	}
	expect(centred, "Centre Views on Selection moves every view to the selection");
	expect(withDialog(command("map.goToCoordinates"), [](QWidget* modal) {
		auto* point = modal->findChild<QLineEdit*>(QStringLiteral("levelGoToPoint"));
		auto* camera = modal->findChild<QCheckBox*>(QStringLiteral("levelGoToCamera"));
		if (!point || !camera) {
			return false;
		}
		point->setText(QStringLiteral("200, -100 50"));
		camera->setChecked(false);
		static_cast<QDialog*>(modal)->accept();
		return true;
	}),
		"Go to Coordinates answered");
	bool there = true;
	for (MapViewport* view : views) {
		if (view->hasDocument()) {
			const QPointF expected = mapViewportProjectPoint(view->projection(), {200, -100, 50, true});
			there &= near(view->navigationState().center.x(), expected.x()) && near(view->navigationState().center.y(), expected.y());
		}
	}
	expect(there, "Go to Coordinates takes every view to the point, commas or spaces between");
	command("map.zoomToFit")->trigger();
	expect(!near(top->navigationState().center.x(), 200), "Zoom to Fit frames the map again");

	// Regions: drawn, from a brush, typed, and remembered.
	select({{LevelMapSelectionKind::QuakeBrush, 0}});
	vertexTool->trigger();
	leftPixel = rightPixel = top->componentHandlePosition(0).x();
	topPixel = bottomPixel = top->componentHandlePosition(0).y();
	for (int i = 0; i < top->componentHandleCount(); ++i) {
		const QPointF at = top->componentHandlePosition(i);
		leftPixel = std::min(leftPixel, at.x());
		rightPixel = std::max(rightPixel, at.x());
		topPixel = std::min(topPixel, at.y());
		bottomPixel = std::max(bottomPixel, at.y());
	}
	vertexTool->trigger();
	QAction* regionDraw = command("map.regionDraw");
	regionDraw->trigger();
	expect(regionDraw->isChecked() && top->regionDrawMode(), "Draw Region waits for a box");
	const QPointF from(leftPixel - 6, topPixel - 6);
	const QPointF to(rightPixel + 6, bottomPixel + 6);
	mouse(top, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
	mouse(top, QEvent::MouseMove, (from + to) / 2, Qt::NoButton, Qt::LeftButton);
	mouse(top, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
	mouse(top, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
	expect(until([&] { return command("map.regionSelect")->isEnabled(); }) && !regionDraw->isChecked() && !top->regionDrawMode(),
		"the drawn box becomes the region and the mode ends");
	capture(top, QStringLiteral("region-drawn"));
	select({});
	command("map.regionSelect")->trigger();
	expect(selects(document(), LevelMapSelectionKind::QuakeBrush, 0) && !selects(document(), LevelMapSelectionKind::QuakeBrush, 1)
			&& !selects(document(), LevelMapSelectionKind::QuakeBrush, 2) && !selects(document(), LevelMapSelectionKind::QuakePatch, 0),
		"Select Everything in Region takes what the drawn region keeps");
	select({{LevelMapSelectionKind::QuakeBrush, 0}});
	command("map.regionSetBrush")->trigger();
	expect(document().brushes.size() == 3 && !brushById(document(), 0) && command("map.regionSelect")->isEnabled(), "Set Region to Brush takes the brush away");
	command("map.undo")->trigger();
	select({});
	command("map.regionSelect")->trigger();
	expect(document().brushes.size() == 4 && document().selection.size() == 1 && selects(document(), LevelMapSelectionKind::QuakeBrush, 0),
		"undo brings the brush back inside the region it made");
	expect(withDialog(command("map.regionBounds"), [](QWidget* modal) {
		const double values[6] {-200, -200, -16, -32, -32, 16};
		for (int i = 0; i < 6; ++i) {
			auto* field = modal->findChild<QDoubleSpinBox*>(
				QStringLiteral("levelRegionBound%1%2").arg(i < 3 ? QStringLiteral("Min") : QStringLiteral("Max")).arg(i % 3));
			if (!field) {
				return false;
			}
			field->setValue(values[i]);
		}
		static_cast<QDialog*>(modal)->accept();
		return true;
	}),
		"Region Bounds answered");
	select({});
	command("map.regionSelect")->trigger();
	expect(document().selection.size() == 1 && selects(document(), LevelMapSelectionKind::QuakePatch, 0), "a typed region keeps only what lies in it");
	// The region is remembered with the map, and a new window finds it.
	{
		// Any question while opening would be a fault here; it is turned down.
		bool asked = false;
		onModal = [&asked](QWidget*) { asked = true; return false; };
		ApplicationShell second;
		second.resize(1200, 800);
		second.openPathFromCommandLine(mapPath);
		QAction* remembered = second.findChild<QAction*>(QStringLiteral("map.regionSelect"));
		expect(until([&] { return second.levelDocument().sourcePath == mapPath; }) && remembered && until([&] { return remembered->isEnabled(); }, 5000) && !asked,
			"reopening the map brings its region back");
		onModal = nullptr;
	}
	command("map.regionClear")->trigger();
	expect(!command("map.regionSelect")->isEnabled(), "Clear Region forgets it");

	// Natural on the whole selection, then on faces picked in the camera.
	expect(undoAll(), "region steps undo");
	select({{LevelMapSelectionKind::QuakeBrush, 0}});
	command("map.surfaceNatural")->trigger();
	expect(until([&] { return !shell.levelSurfaceEditsPending(); }), "Natural completes");
	bool natural = true;
	for (const LevelMapBrushFace& face : brushById(document(), 0)->faces) {
		natural &= face.shiftX == 0 && face.shiftY == 0 && face.rotation == 0 && near(face.scaleX, 0.5) && near(face.scaleY, 0.5);
	}
	expect(natural && document().undoStack.size() == 1, "Natural resets every face of the selection in one step");
	expect(undoAll(), "undo restores the odd mapping");
	auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
	auto* surfaces = static_cast<LevelSurfaceTools*>(shell.findChild<QScrollArea*>(QStringLiteral("levelSurfaceTools")));
	auto* target = surfaces ? surfaces->findChild<QComboBox*>(QStringLiteral("surfaceQuickTarget")) : nullptr;
	if (expect(camera && surfaces && target, "camera and Surfaces tab")
		&& expect(until([&] { return camera->isEnabled() && camera->hasMesh(); }), "camera preview ready")) {
		const LevelMapPreviewMesh preview = buildLevelMapPreviewMesh(document());
		const auto triangleOf = [&](int brush, int face) {
			for (int t = 0; t < preview.owners.size(); ++t) {
				if (preview.owners.at(t).kind == LevelMapSelectionKind::QuakeBrush && preview.owners.at(t).objectId == brush
					&& preview.ownerFaces.value(t, -1) == face) {
					return t;
				}
			}
			return -1;
		};
		select({});
		const int first = triangleOf(0, 0);
		const int third = triangleOf(0, 2);
		camera->trianglePicked(0, first, static_cast<int>(ModelViewportPick::FaceToggle));
		camera->trianglePicked(0, third, static_cast<int>(ModelViewportPick::FaceToggle));
		target->setCurrentIndex(1);
		auto* summary = surfaces->findChild<QLabel*>(QStringLiteral("surfaceQuickSummary"));
		expect(first >= 0 && third >= 0 && document().selection.size() == 1 && selects(document(), LevelMapSelectionKind::QuakeBrush, 0)
				&& summary && summary->text().contains(QLatin1Char('2')),
			"face picks gather on their brush for the Picked Faces target", summary ? summary->text() : QString());
		command("map.surfaceNatural")->trigger();
		expect(until([&] { return !shell.levelSurfaceEditsPending(); }), "picked Natural completes");
		const LevelMapBrush* picked = brushById(document(), 0);
		bool only = picked && picked->faces.size() == 6;
		for (int face = 0; only && face < 6; ++face) {
			const bool reset = picked->faces.at(face).shiftX == 0 && near(picked->faces.at(face).scaleX, 0.5);
			only &= (face == 0 || face == 2) ? reset : picked->faces.at(face).shiftX == 7;
		}
		expect(only, "Natural changes only the picked faces");
		// Selecting another way drops the picks, and the whole brush lights again.
		select({{LevelMapSelectionKind::QuakeBrush, 1}});
		select({{LevelMapSelectionKind::QuakeBrush, 0}});
		expect(summary && !summary->text().contains(QStringLiteral("picked")) && until([&] { return camera->highlightedTriangleCount() == 12; }, 10000),
			"selecting another way drops the picked faces", summary ? summary->text() : QString());
		target->setCurrentIndex(0);
		expect(undoAll(), "undo restores the picked faces");
	}

	// Patch matrix commands and Thicken.
	select({{LevelMapSelectionKind::QuakePatch, 0}});
	const LevelMapPatch before = *patchById(document(), 0);
	expect(command("map.patchTranspose")->isEnabled() && command("map.thickenPatch")->isEnabled(), "patch commands enabled for a selected patch");
	command("map.patchAddColumns")->trigger();
	expect(patchById(document(), 0)->width == 5, "Add Patch Columns adds two columns", QString::number(patchById(document(), 0)->width));
	command("map.patchDeleteLastColumns")->trigger();
	bool same = patchById(document(), 0)->width == 3;
	for (int i = 0; same && i < before.controlPoints.size(); ++i) {
		const LevelMapVec3 a = before.controlPoints.at(i);
		const LevelMapVec3 b = patchById(document(), 0)->controlPoints.at(i);
		same &= near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
	}
	expect(same, "Delete Last Patch Columns joins them back into the same surface");
	command("map.patchTranspose")->trigger();
	bool transposed = true;
	for (int row = 0; row < 3; ++row) {
		for (int column = 0; column < 3; ++column) {
			const LevelMapVec3 a = before.controlPoints.at(row * 3 + column);
			const LevelMapVec3 b = patchById(document(), 0)->controlPoints.at(column * 3 + row);
			transposed &= near(a.x, b.x) && near(a.y, b.y);
		}
	}
	expect(transposed && document().undoStack.size() == 3, "Transpose swaps rows and columns, each command one step");
	expect(undoAll(), "patch commands undo exactly");
	select({{LevelMapSelectionKind::QuakePatch, 0}});
	expect(withDialog(command("map.thickenPatch"), [](QWidget* modal) {
		auto* amount = modal->findChild<QDoubleSpinBox*>(QStringLiteral("levelThickenAmount"));
		auto* seams = modal->findChild<QCheckBox*>(QStringLiteral("levelThickenSeams"));
		if (modal->objectName() != QStringLiteral("levelThickenDialog") || !amount || !seams || !seams->isChecked()) {
			return false;
		}
		amount->setValue(16);
		static_cast<QDialog*>(modal)->accept();
		return true;
	}) && document().patches.size() == 6 && document().selection.size() == 5 && document().undoStack.size() == 1,
		"Thicken adds a back and four walls to an open patch and selects them", QString::number(document().patches.size()));
	expect(undoAll(), "undo removes every thickened piece");

	// Patch shapes in the Shapes tab.
	shell.showLevelSidebarTab(QStringLiteral("shapes"));
	auto* cylinder = shell.findChild<QToolButton*>(QStringLiteral("levelShape-patch-cylinder"));
	auto* add = shell.findChild<QPushButton*>(QStringLiteral("levelShapeAdd"));
	if (expect(cylinder && add && !cylinder->isHidden() && cylinder->isEnabled(), "a Quake III map offers patch shapes")) {
		cylinder->click();
		add->click();
		expect(until([&] { return document().patches.size() == 2; }) && document().selection.size() == 1
				&& document().selection.first().kind == LevelMapSelectionKind::QuakePatch,
			"Add at View Centre places a patch cylinder and selects it");
		expect(undoAll(), "undo removes the patch shape");
		shell.findChild<QToolButton*>(QStringLiteral("levelShape-box"))->click();
	}

	// Prefabs: dropped on a 2D view, and in the selection's place.
	const qsizetype brushes = document().brushes.size();
	const auto crateBrush = [&]() -> const LevelMapBrush* {
		for (const LevelMapBrush& brush : document().brushes) {
			if (!brush.faces.isEmpty() && brush.faces.first().textureName.contains(QStringLiteral("crate"))) {
				return &brush;
			}
		}
		return nullptr;
	};
	top->paletteDropped(QStringLiteral("prefab-file:") + prefabPath, QPointF(top->width() / 2.0, top->height() / 2.0));
	expect(document().brushes.size() == brushes + 1 && document().undoStack.size() == 1 && document().selection.size() == 1 && crateBrush()
			&& selects(document(), LevelMapSelectionKind::QuakeBrush, crateBrush()->id),
		"a prefab dropped on a 2D view is placed and selected in one step");
	expect(undoAll(), "undo removes the dropped prefab");
	select({{LevelMapSelectionKind::QuakeBrush, 1}});
	expect(command("map.replaceWithPrefab")->isEnabled() && withDialog(command("map.replaceWithPrefab"), [&](QWidget* modal) {
		auto* files = qobject_cast<QFileDialog*>(modal);
		if (!files) {
			return false;
		}
		// selectFile() leaves a focused name field alone, so type the path.
		auto* name = files->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
		if (!name) {
			return false;
		}
		name->setText(prefabPath);
		static_cast<QDialog*>(files)->accept();
		return true;
	}),
		"Replace Selection with Prefab asks for the prefab");
	// The replaced brush is known by its bounds, since a freed id may be reused.
	const LevelMapBrush* crate = crateBrush();
	const bool replaced = std::none_of(document().brushes.cbegin(), document().brushes.cend(), [](const LevelMapBrush& brush) { return near(brush.mins.x, 128.5); });
	expect(document().brushes.size() == brushes && replaced && document().undoStack.size() == 1 && crate
			&& near((crate->mins.x + crate->maxs.x) / 2, (128.5 + 192) / 2) && near((crate->mins.y + crate->maxs.y) / 2, (0.25 + 64) / 2)
			&& near(crate->mins.z, 0),
		"the prefab takes the brush's place, its anchor at the brush's bottom centre, in one step",
		crate ? QStringLiteral("%1 %2 %3 / %4 %5 %6").arg(crate->mins.x).arg(crate->mins.y).arg(crate->mins.z).arg(crate->maxs.x).arg(crate->maxs.y).arg(crate->maxs.z)
			  : shell.statusBar()->currentMessage());
	expect(undoAll(), "undo puts the replaced brush back");

	drive.stop();
	drain(100);
	return ok ? 0 : 1;
}
