#include "core/level_editor_controls.h"

#include "core/studio_semantics.h"

#include <QCoreApplication>
#include <QHash>
#include <QKeySequence>
#include <QSet>
#include <cmath>
#include <tuple>

namespace vibestudio {

namespace {

LevelEditorKeyBinding bind(const char* commandId, const QStringList& keys)
{
	return {QString::fromLatin1(commandId), keys};
}

// Grid sizes 1 to 256 on the digits 1 to 9, as Radiant and TrenchBroom both
// have them, with 0 showing or hiding the grid.
QVector<LevelEditorKeyBinding> gridDigitKeys()
{
	return {
		bind("map.grid1", {QStringLiteral("1")}),
		bind("map.grid2", {QStringLiteral("2")}),
		bind("map.grid4", {QStringLiteral("3")}),
		bind("map.grid8", {QStringLiteral("4")}),
		bind("map.grid16", {QStringLiteral("5")}),
		bind("map.grid32", {QStringLiteral("6")}),
		bind("map.grid64", {QStringLiteral("7")}),
		bind("map.grid128", {QStringLiteral("8")}),
		bind("map.grid256", {QStringLiteral("9")}),
		bind("map.toggleGrid", {QStringLiteral("0")}),
	};
}

QString buttonName(Qt::MouseButton button)
{
	switch (button) {
	case Qt::LeftButton:
		return QCoreApplication::translate("VibeStudioLevelControls", "Left");
	case Qt::RightButton:
		return QCoreApplication::translate("VibeStudioLevelControls", "Right");
	case Qt::MiddleButton:
		return QCoreApplication::translate("VibeStudioLevelControls", "Middle");
	default:
		break;
	}
	return QCoreApplication::translate("VibeStudioLevelControls", "Button");
}

QString modifierPrefix(Qt::KeyboardModifiers modifiers)
{
	QStringList parts;
	if (modifiers.testFlag(Qt::ControlModifier)) {
		parts << QCoreApplication::translate("VibeStudioLevelControls", "Ctrl");
	}
	if (modifiers.testFlag(Qt::ShiftModifier)) {
		parts << QCoreApplication::translate("VibeStudioLevelControls", "Shift");
	}
	if (modifiers.testFlag(Qt::AltModifier)) {
		parts << QCoreApplication::translate("VibeStudioLevelControls", "Alt");
	}
	if (modifiers.testFlag(Qt::MetaModifier)) {
		parts << QCoreApplication::translate("VibeStudioLevelControls", "Meta");
	}
	return parts.isEmpty() ? QString() : parts.join(QLatin1Char('+')) + QLatin1Char('+');
}

QString buttonsText(const QVector<Qt::MouseButton>& buttons, Qt::KeyboardModifiers modifiers, const QString& what)
{
	QStringList parts;
	for (const Qt::MouseButton button : buttons) {
		parts << mouseGestureText(button, modifiers, what);
	}
	return parts.join(QStringLiteral(", "));
}

QString keysText(const CameraFlyKeys& keys)
{
	QStringList parts;
	const auto add = [&parts](const QString& key, const QString& what) {
		if (!key.isEmpty()) {
			parts << QCoreApplication::translate("VibeStudioLevelControls", "%1 %2").arg(key, what);
		}
	};
	add(keys.forward, QCoreApplication::translate("VibeStudioLevelControls", "forward"));
	add(keys.back, QCoreApplication::translate("VibeStudioLevelControls", "back"));
	add(keys.left, QCoreApplication::translate("VibeStudioLevelControls", "left"));
	add(keys.right, QCoreApplication::translate("VibeStudioLevelControls", "right"));
	add(keys.up, QCoreApplication::translate("VibeStudioLevelControls", "up"));
	add(keys.down, QCoreApplication::translate("VibeStudioLevelControls", "down"));
	add(keys.turnLeft, QCoreApplication::translate("VibeStudioLevelControls", "turn left"));
	add(keys.turnRight, QCoreApplication::translate("VibeStudioLevelControls", "turn right"));
	add(keys.pitchUp, QCoreApplication::translate("VibeStudioLevelControls", "look up"));
	add(keys.pitchDown, QCoreApplication::translate("VibeStudioLevelControls", "look down"));
	return parts.join(QStringLiteral(", "));
}

} // namespace

QStringList CameraFlyKeys::all() const
{
	QStringList keys;
	for (const QString& key : {forward, back, left, right, up, down, turnLeft, turnRight, pitchUp, pitchDown}) {
		if (!key.isEmpty()) {
			keys << key;
		}
	}
	return keys;
}

bool CameraFlyKeys::isEmpty() const
{
	return all().isEmpty();
}

LevelEditorControls vibeStudioLevelControls()
{
	// The Levels page as it has always behaved: a 2D plan first, a rubber
	// band over empty space, the middle button to pan, and an orthographic
	// 3D view that orbits the map.
	LevelEditorControls controls;
	controls.layout = LevelViewLayout::Single2D;
	controls.defaultGridUnits = 64;
	controls.keys = {bind("map.maximizeView", {QStringLiteral("Ctrl+Space")})};
	// The camera's defaults are this view: Shift or Ctrl with the left
	// button pans, so 3D clicks only ever replace the selection.
	return controls;
}

LevelEditorControls trenchBroomLevelControls()
{
	// TrenchBroom's defaults: one pane, 3D first, a 16-unit grid; the right
	// button looks and pans the 2D views; a drag over empty space draws a
	// brush; WASD with Q and X fly the camera.
	LevelEditorControls controls;
	controls.layout = LevelViewLayout::Single3D;
	controls.defaultGridUnits = 16;

	PlanViewControls& plan = controls.plan;
	plan.panButtons = {Qt::RightButton, Qt::MiddleButton};
	plan.toggleModifiers = Qt::ControlModifier;
	plan.addModifiers = Qt::NoModifier;
	// TrenchBroom has no rubber band; Shift+drag, which it leaves unused in
	// the 2D views, keeps one within reach.
	plan.bandModifiers = Qt::ShiftModifier;
	plan.emptyDrag = PlanEmptyDrag::DrawBrush;
	plan.emptyDragWithSelection = PlanEmptyDrag::DrawBrush;
	plan.squareModifiers = Qt::ShiftModifier;
	plan.cubeModifiers = Qt::ShiftModifier | Qt::ControlModifier;

	CameraViewControls& camera = controls.camera;
	camera.perspective = true;
	camera.fieldOfViewDegrees = 90.0;
	camera.lookButton = Qt::RightButton;
	camera.orbitButton = Qt::RightButton;
	camera.orbitModifiers = Qt::AltModifier;
	camera.panButtons = {Qt::MiddleButton};
	camera.wheel = CameraWheel::Dolly;
	camera.leftPanModifiers = Qt::NoModifier;
	camera.fieldOfViewWheelModifiers = Qt::ShiftModifier;
	camera.toggleModifiers = Qt::ControlModifier;
	camera.faceModifiers = Qt::ShiftModifier;
	camera.dragMovesSelection = true;
	camera.flyKeys = {QStringLiteral("W"), QStringLiteral("S"), QStringLiteral("A"), QStringLiteral("D"), QStringLiteral("Q"), QStringLiteral("X"), QString(), QString()};

	controls.keys = {
		bind("map.duplicateSelection", {QStringLiteral("Ctrl+D")}),
		bind("map.deleteSelection", {QStringLiteral("Del"), QStringLiteral("Backspace")}),
		bind("map.selectAll", {QStringLiteral("Ctrl+A")}),
		bind("map.selectNone", {QStringLiteral("Ctrl+Shift+A")}),
		bind("map.invertSelection", {QStringLiteral("Ctrl+Alt+A")}),
		bind("map.isolateSelection", {QStringLiteral("Ctrl+I")}),
		bind("map.hideSelection", {QStringLiteral("Ctrl+Alt+I")}),
		bind("map.showAll", {QStringLiteral("Ctrl+Shift+I")}),
		bind("map.clipTool", {QStringLiteral("C")}),
		bind("map.gridSmaller", {QStringLiteral("-"), QStringLiteral("[")}),
		bind("map.gridLarger", {QStringLiteral("+"), QStringLiteral("="), QStringLiteral("]")}),
		bind("map.toggleSnap", {QStringLiteral("Alt+0")}),
		bind("map.cycleView", {QStringLiteral("Space")}),
		bind("map.frameSelection", {QStringLiteral("Ctrl+U")}),
		bind("map.carve", {QStringLiteral("Ctrl+K")}),
		bind("map.hollowSelection", {QStringLiteral("Ctrl+Shift+K")}),
		bind("map.undo", {QStringLiteral("Ctrl+Z")}),
		bind("map.redo", {QStringLiteral("Ctrl+Shift+Z"), QStringLiteral("Ctrl+Y")}),
		// W, D, and X fly the camera here, so the commands that had them give
		// them up.
		bind("map.previewWireframe", {}),
		bind("map.drawSector", {}),
	};
	controls.keys += gridDigitKeys();
	return controls;
}

LevelEditorControls netRadiantCustomLevelControls()
{
	// NetRadiant Custom's defaults: the camera beside a 2D view, a 16-unit
	// grid; Shift selects, the right button pans the 2D view and toggles
	// mouse look in the camera, the middle button aims the camera from the
	// 2D view, and Space clones.
	LevelEditorControls controls;
	controls.layout = LevelViewLayout::CameraAndPlan;
	controls.defaultGridUnits = 16;

	PlanViewControls& plan = controls.plan;
	plan.panButtons = {Qt::RightButton};
	plan.zoomDragButton = Qt::RightButton;
	plan.zoomDragModifiers = Qt::AltModifier;
	plan.toggleModifiers = Qt::ShiftModifier;
	plan.addModifiers = Qt::NoModifier;
	plan.bandModifiers = Qt::ShiftModifier;
	plan.emptyDrag = PlanEmptyDrag::DrawBrush;
	plan.emptyDragWithSelection = PlanEmptyDrag::ResizeSelection;
	plan.clickCyclesStack = true;
	plan.middleButtonDrivesCamera = true;
	plan.arrowsDriveCamera = true;
	plan.squareModifiers = Qt::ShiftModifier;
	plan.cubeModifiers = Qt::ControlModifier;

	CameraViewControls& camera = controls.camera;
	camera.perspective = true;
	camera.fieldOfViewDegrees = 100.0;
	// Surface sampling and seamless wrapping follow selection.cpp at NetRadiant Custom
	// 68ecbed64b7be78741878c730279b5471d978c7c (GPL-2.0-or-later).
	// Click paste and single-face wrapping; drag wrapping remains separate.
	camera.materialSampleButton = Qt::MiddleButton;
	camera.surfaceWrapFaceButton = Qt::MiddleButton;
	camera.surfaceWrapFaceModifiers = Qt::ControlModifier;
	camera.surfaceValuesButton = Qt::MiddleButton; camera.surfaceValuesModifiers = Qt::ShiftModifier;
	camera.surfaceValuesOnlyButton = Qt::MiddleButton; camera.surfaceValuesOnlyModifiers = Qt::AltModifier | Qt::ShiftModifier;
	camera.surfaceWrapOnlyButton = Qt::MiddleButton; camera.surfaceWrapOnlyModifiers = Qt::AltModifier | Qt::ControlModifier;
	camera.surfaceProjectButton = Qt::MiddleButton; camera.surfaceProjectModifiers = Qt::ControlModifier | Qt::ShiftModifier;
	camera.surfaceProjectOnlyButton = Qt::MiddleButton; camera.surfaceProjectOnlyModifiers = Qt::AltModifier | Qt::ControlModifier | Qt::ShiftModifier;
	camera.lookButton = Qt::RightButton;
	camera.lookClickToggles = true;
	camera.orbitButton = Qt::RightButton;
	camera.orbitModifiers = Qt::AltModifier;
	// A right drag strafes, as Radiant's camera does; a right click without
	// dragging turns mouse look on or off.
	camera.panButtons = {Qt::RightButton};
	camera.wheel = CameraWheel::DollyToPointer;
	camera.leftPanModifiers = Qt::NoModifier;
	camera.toggleModifiers = Qt::ShiftModifier;
	camera.faceModifiers = Qt::ControlModifier;
	camera.dragMovesSelection = true;
	camera.flyKeys = {QStringLiteral("W"), QStringLiteral("S"), QStringLiteral("A"), QStringLiteral("D"), QString(), QString(), QString(), QString()};
	camera.flyNeedsLook = true;
	camera.driveKeys = {QStringLiteral("Up"), QStringLiteral("Down"), QStringLiteral("A"), QStringLiteral("D"), QString(), QString(), QStringLiteral("Left"), QStringLiteral("Right")};

	controls.keys = {
		bind("map.duplicateSelection", {QStringLiteral("Space"), QStringLiteral("Shift+Space")}),
		bind("map.deleteSelection", {QStringLiteral("Del"), QStringLiteral("Backspace"), QStringLiteral("Z")}),
		bind("map.selectAll", {QStringLiteral("Ctrl+A")}),
		bind("map.selectNone", {QStringLiteral("C")}),
		bind("map.invertSelection", {QStringLiteral("I")}),
		bind("map.hideSelection", {QStringLiteral("H")}),
		bind("map.showAll", {QStringLiteral("Shift+H")}),
		bind("map.clipTool", {QStringLiteral("X")}),
		bind("map.gridSmaller", {QStringLiteral("[")}),
		bind("map.gridLarger", {QStringLiteral("]")}),
		bind("map.nextProjection", {QStringLiteral("Ctrl+Tab")}),
		bind("map.viewTop", {QStringLiteral("Num+7")}),
		bind("map.viewFront", {QStringLiteral("Num+1")}),
		bind("map.viewSide", {QStringLiteral("Num+3")}),
		bind("map.toggle3D", {QStringLiteral("Ctrl+Shift+C")}),
		// NetRadiant Custom mainframe.cpp, pinned revision 68ecbed.
		bind("map.maximizeView", {QStringLiteral("F12")}),
		bind("map.frameSelection", {QStringLiteral("`")}),
		bind("map.carve", {QStringLiteral("Shift+U")}),
		bind("map.snapToGrid", {QStringLiteral("Ctrl+G")}),
		bind("map.connectEntities", {QStringLiteral("Ctrl+K")}),
		bind("map.undo", {QStringLiteral("Ctrl+Z")}),
		bind("map.redo", {QStringLiteral("Ctrl+Shift+Z"), QStringLiteral("Ctrl+Y")}),
		// W is a camera key in mouse look.
		bind("map.previewWireframe", {}),
	};
	controls.keys += gridDigitKeys();
	return controls;
}

LevelEditorControls gtkRadiantLevelControls()
{
	// GtkRadiant 1.6.0's defaults (radiant/mainframe.cpp g_Commands[],
	// xywindow.cpp, camwindow.cpp, drag.cpp, preferences.cpp): the camera
	// beside the 2D view, an 8-unit grid; Shift+click selects and a plain
	// press only manipulates; the right button pans the 2D view and, with
	// Shift, zooms it, and a right click in the camera turns free look on and
	// off; Space clones, Backspace deletes, and Delete and Insert zoom.
	LevelEditorControls controls;
	controls.layout = LevelViewLayout::CameraAndPlan;
	controls.defaultGridUnits = 8;

	PlanViewControls& plan = controls.plan;
	plan.panButtons = {Qt::RightButton};
	plan.zoomDragButton = Qt::RightButton;
	plan.zoomDragModifiers = Qt::ShiftModifier;
	plan.plainClickSelects = false;
	plan.toggleModifiers = Qt::ShiftModifier;
	plan.addModifiers = Qt::NoModifier;
	plan.cycleModifiers = Qt::ShiftModifier | Qt::AltModifier;
	// Alt+drag is its area selection, "completely tall".
	plan.bandModifiers = Qt::AltModifier;
	plan.emptyDrag = PlanEmptyDrag::DrawBrush;
	plan.emptyDragWithSelection = PlanEmptyDrag::ResizeSelection;
	plan.middleButtonDrivesCamera = true;
	plan.arrowsDriveCamera = true;
	// Its new-brush drag keeps no aspect.
	plan.squareModifiers = Qt::NoModifier;
	plan.cubeModifiers = Qt::NoModifier;

	CameraViewControls& camera = controls.camera;
	camera.perspective = true;
	camera.fieldOfViewDegrees = 90.0;
	// GtkRadiant drag.cpp at 270af88f3c2471f6773bded0b5760a3115b52965
	// and Q3Radiant DRAG.CPP at dbe4ddb10315479fc00086f08e25d968b4b43c49:
	// middle samples; Shift+middle replaces only the hit face's material.
	// Behavioral adaptation, GPL-2.0-or-later; see docs/CREDITS.md.
	camera.materialSampleButton = Qt::MiddleButton;
	camera.materialPaintButton = Qt::MiddleButton;
	camera.materialPaintModifiers = Qt::ShiftModifier;
	// Q3Radiant/GtkRadiant drag.cpp: Ctrl+middle applies the full texture
	// definition to a brush; Ctrl+Shift+middle applies it to one face. See credits.
	camera.surfacePasteFaceButton = Qt::MiddleButton;
	camera.surfacePasteFaceModifiers = Qt::ControlModifier | Qt::ShiftModifier;
	camera.surfacePasteBrushButton = Qt::MiddleButton;
	camera.surfacePasteBrushModifiers = Qt::ControlModifier;
	camera.lookButton = Qt::RightButton;
	camera.lookClickToggles = true;
	camera.orbitButton = Qt::NoButton;
	camera.panButtons = {};
	camera.leftPanModifiers = Qt::NoModifier;
	camera.wheel = CameraWheel::Dolly;
	camera.plainClickSelects = false;
	camera.toggleModifiers = Qt::ShiftModifier;
	camera.faceModifiers = Qt::ShiftModifier | Qt::ControlModifier;
	camera.dragMovesSelection = true;
	// The camera keys, held: the arrows move and turn, comma and period
	// strafe, D rises and C sinks. In free look they glide.
	const CameraFlyKeys keys {QStringLiteral("Up"), QStringLiteral("Down"), QStringLiteral(","), QStringLiteral("."), QStringLiteral("D"), QStringLiteral("C"),
		QStringLiteral("Left"), QStringLiteral("Right")};
	camera.driveKeys = keys;
	camera.flyKeys = keys;
	camera.flyNeedsLook = true;

	controls.keys = {
		bind("map.duplicateSelection", {QStringLiteral("Space")}),
		bind("map.deleteSelection", {QStringLiteral("Backspace")}),
		bind("map.zoomIn", {QStringLiteral("Del")}),
		bind("map.zoomOut", {QStringLiteral("Ins")}),
		bind("map.selectSimilar", {QStringLiteral("Shift+A")}),
		bind("map.invertSelection", {QStringLiteral("I")}),
		bind("map.hideSelection", {QStringLiteral("H")}),
		bind("map.showAll", {QStringLiteral("Shift+H")}),
		bind("map.clipTool", {QStringLiteral("X")}),
		bind("map.gridSmaller", {QStringLiteral("[")}),
		bind("map.gridLarger", {QStringLiteral("]")}),
		bind("map.nextProjection", {QStringLiteral("Ctrl+Tab")}),
		bind("map.frameSelection", {QStringLiteral("Ctrl+Shift+Tab")}),
		bind("map.toggle3D", {QStringLiteral("Ctrl+Shift+C")}),
		bind("map.carve", {QStringLiteral("Shift+U")}),
		bind("map.snapToGrid", {QStringLiteral("Ctrl+G")}),
		bind("map.connectEntities", {QStringLiteral("Ctrl+K")}),
		bind("map.loadLeakTrail", {QStringLiteral("Shift+L")}),
		bind("map.undo", {QStringLiteral("Ctrl+Z")}),
		bind("map.redo", {QStringLiteral("Ctrl+Y")}),
		// C sinks the camera.
		bind("map.previewWireframe", {}),
	};
	controls.keys += gridDigitKeys();
	return controls;
}

QString levelViewLayoutId(LevelViewLayout layout)
{
	switch (layout) {
	case LevelViewLayout::FourViews:
		return QStringLiteral("four-views");
	case LevelViewLayout::Single3D:
		return QStringLiteral("single-3d");
	case LevelViewLayout::CameraAndPlan:
		return QStringLiteral("camera-and-plan");
	case LevelViewLayout::Single2D:
		break;
	}
	return QStringLiteral("single-2d");
}

bool levelViewLayoutForId(const QString& id, LevelViewLayout* layout)
{
	for (const auto candidate : {LevelViewLayout::Single2D, LevelViewLayout::Single3D, LevelViewLayout::CameraAndPlan, LevelViewLayout::FourViews}) {
		if (id == levelViewLayoutId(candidate)) {
			if (layout) { *layout = candidate; }
			return true;
		}
	}
	return false;
}

QString levelViewLayoutDisplayName(LevelViewLayout layout)
{
	switch (layout) {
	case LevelViewLayout::FourViews:
		return QCoreApplication::translate("VibeStudioLevelControls", "Four views: camera, top, front and side");
	case LevelViewLayout::Single3D:
		return QCoreApplication::translate("VibeStudioLevelControls", "One view, 3D first");
	case LevelViewLayout::CameraAndPlan:
		return QCoreApplication::translate("VibeStudioLevelControls", "3D camera beside a 2D view");
	case LevelViewLayout::Single2D:
		break;
	}
	return QCoreApplication::translate("VibeStudioLevelControls", "One view, 2D first");
}

QString planEmptyDragId(PlanEmptyDrag drag)
{
	switch (drag) {
	case PlanEmptyDrag::DrawBrush:
		return QStringLiteral("draw-brush");
	case PlanEmptyDrag::ResizeSelection:
		return QStringLiteral("resize-selection");
	case PlanEmptyDrag::BoxSelect:
		break;
	}
	return QStringLiteral("box-select");
}

QString cameraWheelId(CameraWheel wheel)
{
	switch (wheel) {
	case CameraWheel::Dolly:
		return QStringLiteral("dolly");
	case CameraWheel::DollyToPointer:
		return QStringLiteral("dolly-to-pointer");
	case CameraWheel::Zoom:
		break;
	}
	return QStringLiteral("zoom");
}

QString mouseGestureText(Qt::MouseButton button, Qt::KeyboardModifiers modifiers, const QString& what)
{
	return QCoreApplication::translate("VibeStudioLevelControls", "%1%2 %3").arg(modifierPrefix(modifiers), buttonName(button), what);
}

QVector<LevelEditorControlRow> levelEditorControlRows(const LevelEditorControls& controls)
{
	QVector<LevelEditorControlRow> rows;
	const QString layout = QCoreApplication::translate("VibeStudioLevelControls", "Layout");
	const QString plan = QCoreApplication::translate("VibeStudioLevelControls", "2D view");
	const QString camera = QCoreApplication::translate("VibeStudioLevelControls", "3D view");
	const QString keyboard = QCoreApplication::translate("VibeStudioLevelControls", "Keys");
	const QString drag = QCoreApplication::translate("VibeStudioLevelControls", "drag");
	const QString click = QCoreApplication::translate("VibeStudioLevelControls", "click");
	const QHash<QString, QString> sections {
		{layout, QStringLiteral("layout")},
		{plan, QStringLiteral("plan")},
		{camera, QStringLiteral("camera")},
		{keyboard, QStringLiteral("keys")},
	};
	const auto row = [&rows, &sections](const QString& view, const QString& action, const QString& gesture) {
		if (!gesture.isEmpty()) {
			rows.push_back({view, action, gesture, sections.value(view)});
		}
	};

	row(layout, QCoreApplication::translate("VibeStudioLevelControls", "Views"), levelViewLayoutDisplayName(controls.layout));
	row(layout, QCoreApplication::translate("VibeStudioLevelControls", "Grid"), QCoreApplication::translate("VibeStudioLevelControls", "%1 units").arg(controls.defaultGridUnits));

	const PlanViewControls& p = controls.plan;
	row(plan, QCoreApplication::translate("VibeStudioLevelControls", "Pan"), buttonsText(p.panButtons, Qt::NoModifier, drag));
	QString zoom = QCoreApplication::translate("VibeStudioLevelControls", "Wheel");
	if (p.zoomDragButton != Qt::NoButton) {
		zoom += QStringLiteral(", ") + mouseGestureText(p.zoomDragButton, p.zoomDragModifiers, drag);
	}
	row(plan, QCoreApplication::translate("VibeStudioLevelControls", "Zoom"), zoom);
	if (p.plainClickSelects) {
		row(plan, QCoreApplication::translate("VibeStudioLevelControls", "Select"), p.clickCyclesStack
				? QCoreApplication::translate("VibeStudioLevelControls", "Left click; again to pick the next object under the pointer")
				: mouseGestureText(Qt::LeftButton, Qt::NoModifier, click));
		if (p.toggleModifiers != Qt::NoModifier) {
			row(plan, QCoreApplication::translate("VibeStudioLevelControls", "Add or remove one"), mouseGestureText(Qt::LeftButton, p.toggleModifiers, click));
		}
	} else {
		row(plan, QCoreApplication::translate("VibeStudioLevelControls", "Select or deselect one"),
			QCoreApplication::translate("VibeStudioLevelControls", "%1 (a plain click selects nothing)").arg(mouseGestureText(Qt::LeftButton, p.toggleModifiers, click)));
	}
	if (p.cycleModifiers != Qt::NoModifier) {
		row(plan, QCoreApplication::translate("VibeStudioLevelControls", "Pick the next object under the pointer"), mouseGestureText(Qt::LeftButton, p.cycleModifiers, click));
	}
	if (p.addModifiers != Qt::NoModifier) {
		row(plan, QCoreApplication::translate("VibeStudioLevelControls", "Add one"), mouseGestureText(Qt::LeftButton, p.addModifiers, click));
	}
	const QString emptyDrag = QCoreApplication::translate("VibeStudioLevelControls", "Left drag over empty space");
	if (p.bandModifiers != Qt::NoModifier) {
		row(plan, QCoreApplication::translate("VibeStudioLevelControls", "Select by area"), mouseGestureText(Qt::LeftButton, p.bandModifiers, drag));
	} else if (p.emptyDrag == PlanEmptyDrag::BoxSelect) {
		row(plan, QCoreApplication::translate("VibeStudioLevelControls", "Select by area"), emptyDrag);
	}
	if (p.emptyDrag == PlanEmptyDrag::DrawBrush) {
		const QString when = p.emptyDragWithSelection == PlanEmptyDrag::DrawBrush ? emptyDrag : QCoreApplication::translate("VibeStudioLevelControls", "Left drag over empty space, nothing selected");
		row(plan, QCoreApplication::translate("VibeStudioLevelControls", "Draw a brush"), p.squareModifiers == Qt::NoModifier || p.cubeModifiers == Qt::NoModifier ? when
			: QCoreApplication::translate("VibeStudioLevelControls", "%1 (%2 square, %3 cube)").arg(when, modifierPrefix(p.squareModifiers).chopped(1), modifierPrefix(p.cubeModifiers).chopped(1)));
	}
	row(plan, QCoreApplication::translate("VibeStudioLevelControls", "Move the selection"), QCoreApplication::translate("VibeStudioLevelControls", "Left drag the selection"));
	row(plan, QCoreApplication::translate("VibeStudioLevelControls", "Resize the selection"), p.emptyDragWithSelection == PlanEmptyDrag::ResizeSelection
			? QCoreApplication::translate("VibeStudioLevelControls", "Left drag its handles, or over empty space beside it")
			: QCoreApplication::translate("VibeStudioLevelControls", "Left drag its handles"));
	if (p.middleButtonDrivesCamera) {
		row(plan, QCoreApplication::translate("VibeStudioLevelControls", "Aim the 3D camera here"), mouseGestureText(Qt::MiddleButton, Qt::NoModifier, click));
		row(plan, QCoreApplication::translate("VibeStudioLevelControls", "Move the 3D camera here"), mouseGestureText(Qt::MiddleButton, Qt::ControlModifier, click));
	}
	row(plan, QCoreApplication::translate("VibeStudioLevelControls", "Nudge the selection"), p.arrowsDriveCamera ? QCoreApplication::translate("VibeStudioLevelControls", "Alt+Arrow keys") : QCoreApplication::translate("VibeStudioLevelControls", "Arrow keys"));
	if (p.arrowsDriveCamera) {
		row(plan, QCoreApplication::translate("VibeStudioLevelControls", "Drive the 3D camera"), QCoreApplication::translate("VibeStudioLevelControls", "Arrow keys"));
		if (p.fixedCameraSteps) { row(plan, QCoreApplication::translate("VibeStudioLevelControls", "Camera step"), QCoreApplication::translate("VibeStudioLevelControls", "32 units / 22.5 degrees on the ground plane")); }
	}
	row(plan, QCoreApplication::translate("VibeStudioLevelControls", "Map actions menu"), mouseGestureText(Qt::RightButton, Qt::NoModifier, click));

	const CameraViewControls& c = controls.camera;
	if (!p.panHoldKey.isEmpty()) {
		row(plan, QCoreApplication::translate("VibeStudioLevelControls", "Temporary pan"),
			QCoreApplication::translate("VibeStudioLevelControls", "Hold %1 and move the pointer; release to stop").arg(p.panHoldKey));
	}
	if (p.panArrowModifiers != Qt::NoModifier) {
		row(plan, QCoreApplication::translate("VibeStudioLevelControls", "Pan a quarter of the view"),
			QCoreApplication::translate("VibeStudioLevelControls", "%1Arrow keys").arg(modifierPrefix(p.panArrowModifiers)));
	}
	row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Camera"), c.perspective
			? QCoreApplication::translate("VibeStudioLevelControls", "First-person, %1 degree field of view").arg(c.fieldOfViewDegrees, 0, 'g', 4)
			: QCoreApplication::translate("VibeStudioLevelControls", "Orthographic, orbiting the map"));
	row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Resize the selection"),
		QCoreApplication::translate("VibeStudioLevelControls", "Left drag an axis handle or its label; Escape cancels. Resize Selection opens numeric editing."));
	if (c.materialSampleButton != Qt::NoButton) {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Sample material and copy surface"),
			QCoreApplication::translate("VibeStudioLevelControls", "%1 while navigation is idle; brush faces also copy mapping and flags for surface paste").arg(mouseGestureText(c.materialSampleButton, c.materialSampleModifiers, click)));
	}
	if (c.materialPaintButton != Qt::NoButton) {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Paint material on one surface"),
			QCoreApplication::translate("VibeStudioLevelControls", "%1 while navigation is idle; applies on press, preserves alignment and selection, and supports Undo").arg(mouseGestureText(c.materialPaintButton, c.materialPaintModifiers, click)));
	}
	if (c.surfacePasteFaceButton != Qt::NoButton) {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Paste copied surface on one face"),
			QCoreApplication::translate("VibeStudioLevelControls", "%1; pastes material, matching-format mapping and flags as one undo step").arg(mouseGestureText(c.surfacePasteFaceButton, c.surfacePasteFaceModifiers, click)));
	}
	if (c.surfacePasteBrushButton != Qt::NoButton) {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Paste copied surface on a whole brush"),
			QCoreApplication::translate("VibeStudioLevelControls", "%1; targets the hit brush and keeps the scene selection").arg(mouseGestureText(c.surfacePasteBrushButton, c.surfacePasteBrushModifiers, click)));
	}
	if (c.surfaceWrapFaceButton != Qt::NoButton) {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Wrap copied surface onto one face"),
			QCoreApplication::translate("VibeStudioLevelControls", "%1; seamless brush mapping on press, then the pasted face becomes the next source. Map-wide conversion requires consent in Surfaces.").arg(mouseGestureText(c.surfaceWrapFaceButton, c.surfaceWrapFaceModifiers, click)));
	}
	for (const auto& item : {std::tuple{c.surfaceValuesButton, c.surfaceValuesModifiers, QCoreApplication::translate("VibeStudioLevelControls", "Paste Radiant values onto hit and selection")},
		std::tuple{c.surfaceValuesOnlyButton, c.surfaceValuesOnlyModifiers, QCoreApplication::translate("VibeStudioLevelControls", "Paste mapping values onto hit and selection")},
		std::tuple{c.surfaceWrapOnlyButton, c.surfaceWrapOnlyModifiers, QCoreApplication::translate("VibeStudioLevelControls", "Wrap mapping only onto one face")},
		std::tuple{c.surfaceProjectButton, c.surfaceProjectModifiers, QCoreApplication::translate("VibeStudioLevelControls", "Project copied surface onto hit and selection")},
		std::tuple{c.surfaceProjectOnlyButton, c.surfaceProjectOnlyModifiers, QCoreApplication::translate("VibeStudioLevelControls", "Project mapping only onto hit and selection")}}) {
		const auto& [button, modifiers, action] = item;
		if (button != Qt::NoButton) { row(camera, action, mouseGestureText(button, modifiers, click)); }
	}
	row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Draw a brush"),
		QCoreApplication::translate("VibeStudioLevelControls", "Choose Draw Brush; left-drag on the construction plane, wheel adjusts depth, release creates. Plan square/cube modifiers apply. Escape cancels; Add Brush opens numeric creation."));
	if (c.lookButton != Qt::NoButton) {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Look around"), c.lookClickToggles
				? QCoreApplication::translate("VibeStudioLevelControls", "%1 to start or stop, then move the mouse").arg(mouseGestureText(c.lookButton, c.lookModifiers, click))
				: mouseGestureText(c.lookButton, c.lookModifiers, drag));
	}
	if (c.driveButton != Qt::NoButton) {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Position steering"),
			QCoreApplication::translate("VibeStudioLevelControls", "%1: above/below centre moves forward/back; left/right steers. Centre stops. Release or Escape ends steering.").arg(mouseGestureText(c.driveButton, c.driveModifiers, QCoreApplication::translate("VibeStudioLevelControls", "hold"))));
	}
	if (!c.lookToggleKey.isEmpty()) {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Toggle mouse look"), c.lookToggleKey);
	}
	if (!c.lookHoldKey.isEmpty()) {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Temporary mouse look"),
			QCoreApplication::translate("VibeStudioLevelControls", "Hold %1 and move the pointer; release to restore the previous mode").arg(c.lookHoldKey));
	}
	if (c.lookPanUsesButtons) {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Navigate during mouse look"),
			QCoreApplication::translate("VibeStudioLevelControls", "Right mouse pans; left+right moves along the view"));
	}
	if (c.orbitButton != Qt::NoButton) {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Orbit"), mouseGestureText(c.orbitButton, c.orbitModifiers, drag));
	}
	QString pan = buttonsText(c.panButtons, c.panModifiers, drag);
	if (c.leftPanModifiers != Qt::NoModifier) {
		QStringList modifiers;
		for (const Qt::KeyboardModifier modifier : {Qt::ShiftModifier, Qt::ControlModifier, Qt::AltModifier}) {
			if (c.leftPanModifiers.testFlag(modifier)) {
				modifiers << mouseGestureText(Qt::LeftButton, modifier, drag);
			}
		}
		pan += (pan.isEmpty() ? QString() : QStringLiteral(", ")) + modifiers.join(QStringLiteral(", "));
	}
	row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Pan"), pan);
	switch (c.wheel) {
	case CameraWheel::Zoom:
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Zoom"), QCoreApplication::translate("VibeStudioLevelControls", "Wheel"));
		break;
	case CameraWheel::Dolly:
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Move forward and back"), QCoreApplication::translate("VibeStudioLevelControls", "Wheel"));
		break;
	case CameraWheel::DollyToPointer:
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Move toward the pointer"), QCoreApplication::translate("VibeStudioLevelControls", "Wheel"));
		break;
	}
	if (c.fieldOfViewWheelModifiers != Qt::NoModifier) {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Zoom the field of view"), QCoreApplication::translate("VibeStudioLevelControls", "%1Wheel").arg(modifierPrefix(c.fieldOfViewWheelModifiers)));
	}
	if (!c.flyKeys.isEmpty()) {
		row(camera, c.flyNeedsLook ? QCoreApplication::translate("VibeStudioLevelControls", "Fly while looking") : QCoreApplication::translate("VibeStudioLevelControls", "Fly"),
			QCoreApplication::translate("VibeStudioLevelControls", "%1 (%2 faster, %3 slower)").arg(keysText(c.flyKeys), modifierPrefix(c.fastModifiers).chopped(1), modifierPrefix(c.slowModifiers).chopped(1)));
	}
	if (!c.driveKeys.isEmpty()) {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Drive"), keysText(c.driveKeys));
		if (c.discreteDriveKeys) { row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Drive key movement"), QCoreApplication::translate("VibeStudioLevelControls", "Each press/repeat steps 32 units or 22.5 degrees; movement stays on the ground plane")); }
	}
	if (c.turnKeysPanModifiers != Qt::NoModifier) {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Translate with turn / look keys"),
			QCoreApplication::translate("VibeStudioLevelControls", "%1turn keys strafe; %1look keys move vertically").arg(modifierPrefix(c.turnKeysPanModifiers)));
	}
	if (!c.flyKeys.turnLeft.isEmpty() || !c.flyKeys.turnRight.isEmpty() || !c.driveKeys.turnLeft.isEmpty() || !c.driveKeys.turnRight.isEmpty()) {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Turn keys during mouse look"), c.turnKeysStrafeInLook
			? QCoreApplication::translate("VibeStudioLevelControls", "Strafe") : QCoreApplication::translate("VibeStudioLevelControls", "Turn"));
	}
	if (c.speedModifiersRequireLook) {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Flight speed modifiers"),
			QCoreApplication::translate("VibeStudioLevelControls", "Active only during mouse look"));
	}
	if (c.plainClickSelects) {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Select"), mouseGestureText(Qt::LeftButton, Qt::NoModifier, click));
		if (c.toggleModifiers != Qt::NoModifier) {
			row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Add or remove one"), mouseGestureText(Qt::LeftButton, c.toggleModifiers, click));
		}
	} else {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Select or deselect one"),
			QCoreApplication::translate("VibeStudioLevelControls", "%1 (a plain click selects nothing)").arg(mouseGestureText(Qt::LeftButton, c.toggleModifiers, click)));
	}
	if (c.faceModifiers != Qt::NoModifier) {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Pick a face"), mouseGestureText(Qt::LeftButton, c.faceModifiers, click));
	}
	if (c.dragMovesSelection) {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Move the selection"), QCoreApplication::translate("VibeStudioLevelControls", "Left drag the selection; with Alt, up and down"));
	}

	QHash<QString, QString> labels;
	const auto commandKey = [](QString id) { return id.toLower().remove(QLatin1Char('-')).remove(QLatin1Char('_')); };
	for (const auto& entry : commandPaletteEntries()) { labels.insert(commandKey(entry.commandId), entry.label); }
	for (const auto& entry : shortcutDescriptors()) { labels.insert(commandKey(entry.commandId), entry.label); }
	for (const LevelEditorKeyBinding& binding : controls.keys) {
		// Named as the menus and the command palette name the command.
		const QString label = labels.value(commandKey(binding.commandId), binding.commandId);
		row(keyboard, label, binding.keys.isEmpty() ? QCoreApplication::translate("VibeStudioLevelControls", "(none: the key does something else here)") : binding.keys.join(QStringLiteral(", ")));
	}
	return rows;
}

QStringList levelEditorControlProblems(const LevelEditorControls& controls)
{
	QStringList problems;
	if (controls.defaultGridUnits < 1 || controls.defaultGridUnits > 256 || (controls.defaultGridUnits & (controls.defaultGridUnits - 1)) != 0) {
		problems << QCoreApplication::translate("VibeStudioLevelControls", "The default grid must be a supported power of two from 1 to 256.");
	}
	if (!std::isfinite(controls.camera.fieldOfViewDegrees) || controls.camera.fieldOfViewDegrees < 15 || controls.camera.fieldOfViewDegrees > 150) {
		problems << QCoreApplication::translate("VibeStudioLevelControls", "The field of view must be between 15 and 150 degrees.");
	}
	const PlanViewControls& plan = controls.plan;
	if (plan.panButtons.contains(Qt::LeftButton)) {
		problems << QCoreApplication::translate("VibeStudioLevelControls", "The left button selects in the 2D view, so it cannot pan there too.");
	}
	if (plan.toggleModifiers != Qt::NoModifier && plan.toggleModifiers == plan.addModifiers) {
		problems << QCoreApplication::translate("VibeStudioLevelControls", "One set of keys both toggles and adds in the 2D view.");
	}
	if (plan.zoomDragButton != Qt::NoButton && plan.zoomDragModifiers == Qt::NoModifier && plan.panButtons.contains(plan.zoomDragButton)) {
		problems << QCoreApplication::translate("VibeStudioLevelControls", "One 2D drag both pans and zooms.");
	}
	const CameraViewControls& camera = controls.camera;
	const QVector<QPair<Qt::MouseButton, Qt::KeyboardModifiers>> materialGestures{
		{camera.materialSampleButton, camera.materialSampleModifiers}, {camera.materialPaintButton, camera.materialPaintModifiers},
		{camera.surfacePasteFaceButton, camera.surfacePasteFaceModifiers}, {camera.surfacePasteBrushButton, camera.surfacePasteBrushModifiers},
		{camera.surfaceWrapFaceButton, camera.surfaceWrapFaceModifiers}, {camera.surfaceValuesButton, camera.surfaceValuesModifiers},
		{camera.surfaceValuesOnlyButton, camera.surfaceValuesOnlyModifiers}, {camera.surfaceWrapOnlyButton, camera.surfaceWrapOnlyModifiers},
		{camera.surfaceProjectButton, camera.surfaceProjectModifiers}, {camera.surfaceProjectOnlyButton, camera.surfaceProjectOnlyModifiers}};
	for (int i = 0; i < materialGestures.size(); ++i) {
		const auto [button, modifiers] = materialGestures[i];
		if (cameraMaterialGestureConflicts(camera, button, modifiers)) {
			problems << QCoreApplication::translate("VibeStudioLevelControls", "Material actions need a right or middle button gesture separate from camera navigation.");
		}
		for (int j = 0; j < i; ++j) {
			if (button != Qt::NoButton && materialGestures[j] == materialGestures[i]) {
				problems << QCoreApplication::translate("VibeStudioLevelControls", "Sampling, material painting and surface pasting must use different gestures.");
			}
		}
	}
	const bool lookDrags = camera.lookButton != Qt::NoButton && !camera.lookClickToggles;
	auto withoutDrive = camera; withoutDrive.driveButton = Qt::NoButton;
	if (camera.driveButton != Qt::NoButton && (
		(cameraNavigationDrag(withoutDrive, camera.driveButton, camera.driveModifiers) == CameraNavigationDrag::Look)
		|| (camera.driveButton == camera.lookButton && (camera.lookClickToggles || camera.driveModifiers == camera.lookModifiers))
		|| (camera.driveButton == camera.orbitButton && camera.driveModifiers == camera.orbitModifiers)
		|| (camera.panButtons.contains(camera.driveButton) && camera.driveModifiers == camera.panModifiers)
		|| (camera.driveButton == Qt::LeftButton))) {
		problems << QCoreApplication::translate("VibeStudioLevelControls", "Position steering must use a separate right or middle button gesture from look, orbit and pan.");
	}
	if (lookDrags && camera.lookButton == camera.orbitButton && camera.lookModifiers == camera.orbitModifiers) {
		problems << QCoreApplication::translate("VibeStudioLevelControls", "One 3D drag both looks and orbits.");
	}
	if (lookDrags && camera.panButtons.contains(camera.lookButton) && camera.lookModifiers == camera.panModifiers) {
		problems << QCoreApplication::translate("VibeStudioLevelControls", "One 3D drag both looks and pans.");
	}
	if (camera.orbitButton != Qt::NoButton && camera.orbitModifiers == camera.panModifiers && camera.panButtons.contains(camera.orbitButton)) {
		problems << QCoreApplication::translate("VibeStudioLevelControls", "One 3D drag both orbits and pans.");
	}
	if (camera.toggleModifiers != Qt::NoModifier && camera.toggleModifiers == camera.faceModifiers) {
		problems << QCoreApplication::translate("VibeStudioLevelControls", "One set of keys both toggles and picks faces in the 3D view.");
	}
	if (camera.toggleModifiers != Qt::NoModifier && camera.leftPanModifiers.testAnyFlags(camera.toggleModifiers)) {
		problems << QCoreApplication::translate("VibeStudioLevelControls", "A 3D click that toggles would pan instead.");
	}
	// No key may belong to two commands.
	QHash<QString, QString> owners;
	for (const LevelEditorKeyBinding& binding : controls.keys) {
		for (const QString& key : binding.keys) {
			const auto sequence = QKeySequence::fromString(key, QKeySequence::PortableText);
			if (sequence.isEmpty() || sequence[0].key() == Qt::Key_unknown) {
				problems << QCoreApplication::translate("VibeStudioLevelControls", "Invalid shortcut: %1.").arg(key);
				continue;
			}
			const QString normalized = sequence.toString(QKeySequence::PortableText).toLower();
			const auto owner = owners.constFind(normalized);
			if (owner != owners.constEnd() && owner.value() != binding.commandId) {
				problems << QCoreApplication::translate("VibeStudioLevelControls", "%1 is bound to both %2 and %3.").arg(key, owner.value(), binding.commandId);
				continue;
			}
			owners.insert(normalized, binding.commandId);
		}
	}
	for (const auto& lookKey : {camera.lookToggleKey, camera.lookHoldKey}) {
		if (lookKey.isEmpty()) { continue; }
		const auto toggle = QKeySequence::fromString(lookKey, QKeySequence::PortableText);
		if (!camera.perspective || toggle.count() != 1 || toggle[0].key() == Qt::Key_unknown) {
			problems << QCoreApplication::translate("VibeStudioLevelControls", "Mouse look needs a perspective camera and one valid activation key.");
		}
		if (owners.contains(toggle.toString(QKeySequence::PortableText).toLower())) {
			problems << QCoreApplication::translate("VibeStudioLevelControls", "The mouse-look activation key also belongs to a command.");
		}
	}
	if (!plan.panHoldKey.isEmpty() && owners.contains(QKeySequence::fromString(plan.panHoldKey, QKeySequence::PortableText).toString(QKeySequence::PortableText).toLower())) {
		problems << QCoreApplication::translate("VibeStudioLevelControls", "The temporary plan-pan key also belongs to a command.");
	}
	for (const auto& keys : {camera.flyKeys, camera.driveKeys}) {
		QSet<QString> directions;
		for (const auto& name : keys.all()) {
			const auto sequence = QKeySequence::fromString(name, QKeySequence::PortableText);
			const auto normalized = sequence.toString(QKeySequence::PortableText).toLower();
			if (sequence.count() != 1 || sequence[0].key() == Qt::Key_unknown || sequence[0].keyboardModifiers() != Qt::NoModifier || directions.contains(normalized)) {
				problems << QCoreApplication::translate("VibeStudioLevelControls", "Camera directions need distinct, unmodified single keys: %1.").arg(name);
			}
			directions.insert(normalized);
		}
	}
	return problems;
}

} // namespace vibestudio
