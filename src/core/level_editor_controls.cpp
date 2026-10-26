#include "core/level_editor_controls.h"
#include "core/level_camera_texture.h"

#include "core/level_map.h"
#include "core/studio_semantics.h"

#include <QCoreApplication>
#include <QHash>
#include <QKeySequence>
#include <QSet>
#include <algorithm>
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

// Workflow reference: NRC radiant/grid.cpp GridPower (GRIDPOWER_0125 to
// GRIDPOWER_1024) at 449778b3 (GPL-2.0-or-later) and TB Grid (MinSize -3,
// a 0.125-unit grid) at 4c756be1 (GPL-3.0-or-later). Facts only; original
// implementation, see docs/CREDITS.md.
const QVector<double>& levelEditorGridSizes()
{
	static const QVector<double> sizes{0.125, 0.25, 0.5, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024};
	return sizes;
}

bool levelEditorGridSizeSupported(double units)
{
	return std::isfinite(units) && levelEditorGridSizes().contains(units);
}

double levelEditorFinestGridUnits(const LevelMapDocument& document)
{
	// The binary Doom and Hexen lumps write each coordinate as a 16-bit
	// integer, so a finer grid would place points the save then rounds.
	const bool wholeUnits = document.format == LevelMapFormat::DoomWad && document.doomFormat != LevelMapDoomFormat::Udmf;
	return wholeUnits ? 1.0 : kLevelEditorFinestGridUnits;
}

double levelEditorSteppedGridUnits(double units, int direction, double finest)
{
	const QVector<double>& sizes = levelEditorGridSizes();
	if (!std::isfinite(finest) || finest < sizes.first()) {
		finest = sizes.first();
	}
	if (!std::isfinite(units) || units <= 0.0) {
		units = std::max(finest, 16.0);
	}
	// The next listed size past `units` in the asked direction, so an
	// unlisted size steps from where it is.
	if (direction < 0) {
		const auto finer = std::find_if(sizes.crbegin(), sizes.crend(), [units](double size) { return size < units; });
		return finer != sizes.crend() && *finer >= finest ? *finer : std::max(units, finest);
	}
	const auto coarser = std::find_if(sizes.cbegin(), sizes.cend(), [units](double size) { return size > units; });
	return coarser != sizes.cend() ? std::max(*coarser, finest) : std::max(units, finest);
}

QString levelEditorGridSizeText(double units)
{
	return QString::number(units, 'g', 6);
}

QString levelEditorGridCommandId(double units)
{
	return QStringLiteral("map.grid%1").arg(levelEditorGridSizeText(units));
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
	// The arrows nudge in the plan views (Shift: eight steps); the orbiting
	// camera keeps them for turning and panning (core/level_nudge.h).
	controls.keys += {
		bind("map.nudgeLeft", {QStringLiteral("Left")}),
		bind("map.nudgeRight", {QStringLiteral("Right")}),
		bind("map.nudgeForward", {QStringLiteral("Up")}),
		bind("map.nudgeBack", {QStringLiteral("Down")}),
	};
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
	controls.pasteAtCursor = true;
	// TB MapWindow::duplicateSelection copies nodes without displacement.
	// v2026.2 / 4c756be1 (GPL-3.0-or-later), see docs/CREDITS.md.
	controls.nudgeAfterDuplicate = false;
	controls.copyFaceAttributes = true;
	controls.undoObjectSelection = true;
	// TB MapWindow::deleteSelection at 4c756be1 (GPL-3.0-or-later).
	controls.deleteRemovesClipPoint = true;
	controls.inheritGroupVisibility = true;
	controls.regionSelection = LevelRegionSelectionPolicy::TrenchBroom;
	// TB MapViewBase::makeStructural at 4c756be1 (GPL-3.0-or-later) also moves
	// brushes out of brush entities. Workflow reference; see docs/CREDITS.md.
	controls.makeStructuralLeavesEntities = true;
	// TB MapViewBase::rotate turns about grid.referencePoint(selection bounds)
	// at 4c756be1 (GPL-3.0-or-later). Workflow reference; see docs/CREDITS.md.
	controls.quickTurnSnapsPivot = true;
	// TB MapViewBase::flip mirrors about the half-grid reference point at
	// 4c756be1 (GPL-3.0-or-later). Workflow reference; see docs/CREDITS.md.
	controls.flipSnapsPivot = true;
	controls.plan.componentBandToggles = true;
	controls.camera.componentBandToggles = true;
	controls.plan.componentInsertModifiers = Qt::ShiftModifier;
	controls.camera.componentInsertModifiers = Qt::ShiftModifier;
	controls.plan.componentAxisDrag = true;
	controls.camera.componentAxisDrag = true;
	controls.plan.componentQuickSnapModifiers = Qt::AltModifier|Qt::ShiftModifier;
	controls.camera.componentQuickSnapModifiers = Qt::AltModifier|Qt::ShiftModifier;

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
		bind("map.selectInside", {QStringLiteral("Ctrl+E")}),
		bind("map.selectSiblings", {QStringLiteral("Ctrl+B")}),
		bind("map.selectTouching", {QStringLiteral("Ctrl+T")}),
		bind("map.selectCompleteTall", {QStringLiteral("Ctrl+Shift+E")}),
		bind("map.invertSelection", {QStringLiteral("Ctrl+Alt+A")}),
		bind("map.isolateSelection", {QStringLiteral("Ctrl+I")}),
		bind("map.hideSelection", {QStringLiteral("Ctrl+Alt+I")}),
		bind("map.showAll", {QStringLiteral("Ctrl+Shift+I")}),
		bind("map.clipTool", {QStringLiteral("C")}),
		bind("map.sweepTool", {QStringLiteral("Y")}),
		bind("map.gridSmaller", {QStringLiteral("-"), QStringLiteral("[")}),
		bind("map.gridLarger", {QStringLiteral("+"), QStringLiteral("="), QStringLiteral("]")}),
		bind("map.toggleSnap", {QStringLiteral("Alt+0")}),
		bind("map.cycleView", {QStringLiteral("Space")}),
		bind("map.frameSelection", {QStringLiteral("Ctrl+U")}),
		// TB ActionManager "Controls/Map view/Reset camera zoom" at 4c756be1
		// (GPL-3.0-or-later). Key facts only; see docs/CREDITS.md.
		bind("map.resetCameraZoom", {QStringLiteral("Ctrl+Alt+Z")}),
		bind("map.carve", {QStringLiteral("Ctrl+K")}),
		bind("map.hollowSelection", {QStringLiteral("Ctrl+Shift+K")}),
		// TB ActionManager "Controls/Map view/Make structural" at 4c756be1.
		bind("map.makeStructural", {QStringLiteral("Alt+S")}),
		bind("map.undo", {QStringLiteral("Ctrl+Z")}),
		bind("map.redo", {QStringLiteral("Ctrl+Shift+Z"), QStringLiteral("Ctrl+Y")}),
		// W, D, and X fly the camera here, so the commands that had them give
		// them up.
		bind("map.previewWireframe", {}),
		bind("map.drawSector", {}),
		// TB ActionManager.cpp at 4c756be1 (GPL-3.0-or-later): the Vertex,
		// Edge and Face tools, Snap Vertices to Grid and Create Linked
		// Duplicate. Key facts only; see docs/CREDITS.md. F takes the plan
		// views' own framing key, which TrenchBroom keeps on Ctrl+U.
		bind("map.vertexTool", {QStringLiteral("V")}),
		bind("map.edgeTool", {QStringLiteral("E")}),
		bind("map.faceTool", {QStringLiteral("F")}),
		bind("map.snapVertices", {QStringLiteral("Ctrl+Alt+Shift+V")}),
		bind("map.createLinkedDuplicate", {QStringLiteral("Ctrl+Shift+D")}),
		// TB ActionManager "Menu/Edit/UV Lock" at 4c756be1 has U; the camera
		// flies on W, S, A, D, Q and X here, so U stays free.
		bind("map.componentUvLock", {QStringLiteral("U")}),
		// TB ActionManager "Controls/Map view/Move objects ..." at 4c756be1
		// (GPL-3.0-or-later): the arrows move along the view, Page Up and Page
		// Down along its depth axis, or Z in the camera (core/level_nudge.h).
		// Key facts only; see docs/CREDITS.md. Doom maps keep Page Up and Page
		// Down for Raise Floor and Lower Floor, since nudges stay in the plane.
		bind("map.nudgeLeft", {QStringLiteral("Left")}),
		bind("map.nudgeRight", {QStringLiteral("Right")}),
		bind("map.nudgeForward", {QStringLiteral("Up")}),
		bind("map.nudgeBack", {QStringLiteral("Down")}),
		bind("map.nudgeDepthUp", {QStringLiteral("PgUp")}),
		bind("map.nudgeDepthDown", {QStringLiteral("PgDown")}),
		// TB ActionManager "Controls/Map view/Roll, Yaw and Pitch objects
		// clockwise/counter-clockwise" and "Flip objects horizontally/
		// vertically" at 4c756be1 (GPL-3.0-or-later): view-relative quarter
		// turns and flips (core/level_quick_transform.h). Key facts only; see
		// docs/CREDITS.md. The views claim them while the command can act, so
		// Go Back, Go Forward and Find keep them everywhere else.
		bind("map.rollClockwise", {QStringLiteral("Alt+Up")}),
		bind("map.rollCounterclockwise", {QStringLiteral("Alt+Down")}),
		bind("map.rotateRight", {QStringLiteral("Alt+Left")}),
		bind("map.rotateLeft", {QStringLiteral("Alt+Right")}),
		bind("map.pitchClockwise", {QStringLiteral("Alt+PgUp")}),
		bind("map.pitchCounterclockwise", {QStringLiteral("Alt+PgDown")}),
		bind("map.flipHorizontal", {QStringLiteral("Ctrl+F")}),
		bind("map.flipVertical", {QStringLiteral("Ctrl+Alt+F")}),
		// TB ActionManager "Controls/Map view/Duplicate and move objects ..." at
		// 4c756be1 (GPL-3.0-or-later): Ctrl+arrows copy the selection and move the
		// copies along the view, Ctrl+Page Up and Down along its depth axis, or Z
		// in the camera (core/level_nudge.h). Key facts only; see docs/CREDITS.md.
		// The views claim them while a copy can be made, so Ctrl+arrows still pan
		// a plan view with nothing selected, and Doom maps keep Ctrl+Page Up and
		// Down for Raise Ceiling and Lower Ceiling.
		bind("map.duplicateMoveLeft", {QStringLiteral("Ctrl+Left")}),
		bind("map.duplicateMoveRight", {QStringLiteral("Ctrl+Right")}),
		bind("map.duplicateMoveForward", {QStringLiteral("Ctrl+Up")}),
		bind("map.duplicateMoveBack", {QStringLiteral("Ctrl+Down")}),
		bind("map.duplicateMoveDepthUp", {QStringLiteral("Ctrl+PgUp")}),
		bind("map.duplicateMoveDepthDown", {QStringLiteral("Ctrl+PgDown")}),
		// TB ActionManager "Controls/Map view/Move textures up, down, left and
		// right" (plain, coarse and fine) and "Flip textures horizontally /
		// vertically" at 4c756be1 (GPL-3.0-or-later): the 3D view with faces
		// selected. Key facts only; see docs/CREDITS.md. They act only in the
		// camera, and only while faces are picked there; otherwise these keys
		// nudge, copy and flip objects as above (core/level_camera_texture.h).
		bind("map.moveTexturesUp", {QStringLiteral("Up")}),
		bind("map.moveTexturesDown", {QStringLiteral("Down")}),
		bind("map.moveTexturesLeft", {QStringLiteral("Left")}),
		bind("map.moveTexturesRight", {QStringLiteral("Right")}),
		bind("map.moveTexturesUpCoarse", {QStringLiteral("Shift+Up")}),
		bind("map.moveTexturesDownCoarse", {QStringLiteral("Shift+Down")}),
		bind("map.moveTexturesLeftCoarse", {QStringLiteral("Shift+Left")}),
		bind("map.moveTexturesRightCoarse", {QStringLiteral("Shift+Right")}),
		bind("map.moveTexturesUpFine", {QStringLiteral("Ctrl+Up")}),
		bind("map.moveTexturesDownFine", {QStringLiteral("Ctrl+Down")}),
		bind("map.moveTexturesLeftFine", {QStringLiteral("Ctrl+Left")}),
		bind("map.moveTexturesRightFine", {QStringLiteral("Ctrl+Right")}),
		bind("map.flipTexturesHorizontally", {QStringLiteral("Ctrl+F")}),
		bind("map.flipTexturesVertically", {QStringLiteral("Ctrl+Alt+F")}),
		// TB ActionManager "Controls/Map view/Rotate textures clockwise /
		// counter-clockwise" (plain, coarse and fine) at 4c756be1
		// (GPL-3.0-or-later), the same face-selection context: 15°, 90° and 1°.
		// Key facts only; see docs/CREDITS.md. With no face picked, Page Up and
		// Page Down nudge along the depth axis and Ctrl copies, as above.
		bind("map.rotateTexturesClockwise", {QStringLiteral("PgUp")}),
		bind("map.rotateTexturesClockwiseCoarse", {QStringLiteral("Shift+PgUp")}),
		bind("map.rotateTexturesClockwiseFine", {QStringLiteral("Ctrl+PgUp")}),
		bind("map.rotateTexturesCounterclockwise", {QStringLiteral("PgDown")}),
		bind("map.rotateTexturesCounterclockwiseCoarse", {QStringLiteral("Shift+PgDown")}),
		bind("map.rotateTexturesCounterclockwiseFine", {QStringLiteral("Ctrl+PgDown")}),
		// TB ActionManager "Controls/Map view/Reset texture alignment" and
		// "... to world aligned" at 4c756be1 (GPL-3.0-or-later): Shift+R keeps
		// each face's projection kind; Shift+Alt+R projects along the world
		// axes, as Natural does. Key facts only; see docs/CREDITS.md.
		bind("map.surfaceReset", {QStringLiteral("Shift+R")}),
		bind("map.surfaceNatural", {QStringLiteral("Alt+Shift+R")}),
	};
	controls.keys += gridDigitKeys();
	return controls;
}

LevelEditorControls netRadiantCustomLevelControls()
{
	// See NRC select.cpp: Inside consumes the selection; Touching adds to it.
	// NetRadiant Custom's defaults: the camera beside a 2D view, a 16-unit
	// grid; Shift selects, the right button pans the 2D view and toggles
	// mouse look in the camera, the middle button aims the camera from the
	// 2D view, and Space clones.
	LevelEditorControls controls;
	controls.layout = LevelViewLayout::CameraAndPlan;
	controls.defaultGridUnits = 16;
	controls.regionSelection = LevelRegionSelectionPolicy::Radiant;
	controls.insertionVisibility = LevelInsertionVisibility::FreshInherited;
	// NRC select.cpp Selection_Clone / NudgeAfterClone at 449778b3
	// (GPL-2.0-or-later): primitive mode only; optional nudge defaults off.
	controls.nudgeAfterDuplicate = false;
	controls.duplicateRequiresObjectMode = true;
	// NRC select.cpp deleteSelection / csg.cpp CSG_DeleteComponents at
	// 449778b3 (GPL-2.0-or-later). Workflow reference; see docs/CREDITS.md.
	controls.deleteObjectsWithoutComponents = true;
	controls.deleteIgnoresPatchPoints = true;
	controls.entityConnectionMode = LevelEntityConnectionMode::RadiantTarget;
	// NRC radiant/grid.cpp GridPrev, GridNext and setGridPower at 449778b3
	// (GPL-2.0-or-later) set g_grid_snap before choosing a size; workflow
	// reference only, see docs/CREDITS.md.
	controls.gridChangeEnablesSnap = true;
	// NRC radiant/selection.cpp rotateSelected(snapOrigin) at 449778b3
	// (GPL-2.0-or-later) snaps the pivot of every quick turn to the snap grid
	// unless the pivot is custom. Workflow reference; see docs/CREDITS.md.
	controls.quickTurnSnapsPivot = true;
	// NRC radiant/surfacedialog.h si_globals_t and select.cpp Texdef_Rotate at
	// 449778b3 (GPL-2.0-or-later): the Surface Inspector's rotate increment is
	// 45° and texture turns keep the texture origin. Key facts only; see
	// docs/CREDITS.md.
	controls.surfaceRotationStep = 45;
	controls.surfaceRotateAboutOrigin = true;

	PlanViewControls& plan = controls.plan;
	// selection_mtor_clip.h at 449778b3840f8f2062daab15c87cb012f2ac945c.
	plan.quickClipModifiers = Qt::ControlModifier;
	controls.camera.clipViewAxis = true;
	controls.camera.clipDragStyle = CameraClipDragStyle::WorldAxes;
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
	// NRC camwindow.cpp CameraFreeFocus at 449778b3 (GPL-2.0-or-later): Tab
	// held in free move keeps the selection framed. It acts only during mouse
	// look here, so Tab still moves keyboard focus everywhere else. Key facts
	// only; see docs/CREDITS.md.
	camera.focusHoldKey = QStringLiteral("Tab");

	controls.keys = {
		bind("map.duplicateSelection", {QStringLiteral("Space")}),
		bind("map.duplicateUnique", {QStringLiteral("Shift+Space")}),
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
		// NRC camwindow.cpp CenterView at 449778b3 (GPL-2.0-or-later) levels
		// the camera on End. Key facts only; see docs/CREDITS.md.
		bind("map.levelCamera", {QStringLiteral("End")}),
		bind("map.carve", {QStringLiteral("Shift+U")}),
		// NRC radiant/csg.cpp CSGWrapMerge at 449778b3840f8f2062daab15c87cb012f2ac945c.
		bind("map.wrapMerge", {QStringLiteral("Ctrl+U")}),
		// NRC select.cpp Selection_SnapToGrid at 449778b3 rounds each vertex,
		// control point and origin (or only the picked components) on Ctrl+G,
		// so Snap Selection to Grid, which moves whole objects, gives it up.
		bind("map.roundToGrid", {QStringLiteral("Ctrl+G")}),
		bind("map.snapToGrid", {}),
		bind("map.connectEntities", {QStringLiteral("Ctrl+K")}),
		bind("map.killConnectEntities", {QStringLiteral("Shift+K")}),
		// NRC entity.cpp EntityMovePrimitivesToLast at 449778b3; the first-selected
		// variant and Ungroup Primitives have no default key there either.
		bind("map.movePrimitivesToEntity", {QStringLiteral("Ctrl+M")}),
		// NRC entity.cpp EntityColorSet at 449778b3 has K; EntityColorNormalize
		// has no default key there.
		bind("map.setEntityColour", {QStringLiteral("K")}),
		bind("map.selectConnectedEntities", {QStringLiteral("Ctrl+Shift+E")}),
		bind("map.selectAllOfType", {QStringLiteral("Shift+A")}),
		bind("map.selectTextured", {QStringLiteral("Ctrl+Shift+A")}),
		bind("map.selectSimilar", {}),
		bind("map.undo", {QStringLiteral("Ctrl+Z")}),
		bind("map.redo", {QStringLiteral("Ctrl+Shift+Z"), QStringLiteral("Ctrl+Y")}),
		// W is a camera key in mouse look.
		bind("map.previewWireframe", {}),
		// NRC camwindow.cpp CameraModeNext and CameraModePrev at 449778b3
		// (GPL-2.0-or-later) step the camera's draw mode on Shift+] and
		// Shift+[ (core/level_camera_render.h). Key facts only; see
		// docs/CREDITS.md.
		bind("map.nextCameraMode", {QStringLiteral("Shift+]")}),
		bind("map.previousCameraMode", {QStringLiteral("Shift+[")}),
		// NRC 449778b3 (GPL-2.0-or-later): tools.cpp DragVertices, DragEdges
		// and DragFaces; patchmanip.cpp's patch matrix, Invert, Transpose and
		// Thicken and Smooth Columns keys and InvertCurveTextureY;
		// brushmanip.cpp MakeDetail, MakeStructural and TogTexLock;
		// xywindow.cpp CenterXYView. Key facts only; see docs/CREDITS.md.
		// Ctrl+Shift+I (flip texture X) and Ctrl+W (smooth rows) stay unbound:
		// the studio keeps them for Initialize Project Manifest and Close
		// Package, window-wide keys a profile key would take everywhere.
		bind("map.vertexTool", {QStringLiteral("V")}),
		bind("map.edgeTool", {QStringLiteral("E")}),
		bind("map.faceTool", {QStringLiteral("F")}),
		bind("map.patchInsertRows", {QStringLiteral("Ctrl+Num++")}),
		bind("map.patchInsertColumns", {QStringLiteral("Ctrl+Shift+Num++")}),
		bind("map.patchDeleteLastRows", {QStringLiteral("Ctrl+Num+-")}),
		bind("map.patchDeleteLastColumns", {QStringLiteral("Ctrl+Shift+Num+-")}),
		bind("map.patchInvert", {QStringLiteral("Ctrl+I")}),
		bind("map.patchTranspose", {QStringLiteral("Ctrl+Shift+M")}),
		bind("map.thickenPatch", {QStringLiteral("Ctrl+T")}),
		bind("map.patchSmoothColumns", {QStringLiteral("Ctrl+Shift+W")}),
		bind("map.surfaceFlipV", {QStringLiteral("Shift+I")}),
		// NRC radiant/select.cpp TexRotateClock, TexRotateCounter and
		// TexScaleUp/Down/Left/Right at 449778b3 (GPL-2.0-or-later): Shift+Page
		// Down subtracts the rotate increment and Shift+Page Up adds it, and
		// Ctrl+arrows add or subtract the scale increment, ahead of the 2D
		// view's Ctrl+arrow panning. Key facts only; see docs/CREDITS.md.
		bind("map.surfaceRotateLeft", {QStringLiteral("Shift+PgDown")}),
		bind("map.surfaceRotateRight", {QStringLiteral("Shift+PgUp")}),
		bind("map.surfaceScaleStepUp", {QStringLiteral("Ctrl+Up")}),
		bind("map.surfaceScaleStepDown", {QStringLiteral("Ctrl+Down")}),
		bind("map.surfaceScaleStepLeft", {QStringLiteral("Ctrl+Left")}),
		bind("map.surfaceScaleStepRight", {QStringLiteral("Ctrl+Right")}),
		// NRC radiant/surfacedialog.cpp "TextureReset/Cap" at 449778b3
		// (GPL-2.0-or-later) resets textures and caps patches on Shift+N. Key
		// facts only; see docs/CREDITS.md.
		bind("map.surfaceRadiantReset", {QStringLiteral("Shift+N")}),
		bind("map.textureLock", {QStringLiteral("Shift+T")}),
		bind("map.makeDetail", {QStringLiteral("Alt+D")}),
		bind("map.makeStructural", {QStringLiteral("Alt+S")}),
		bind("map.centerOnSelection", {QStringLiteral("Ctrl+Shift+Tab")}),
		// NRC layerswindow.cpp Layers_registerCommands at 449778b3 puts the
		// layers menu on Shift+Q and Expand Selection to Layers on Ctrl+Q,
		// which is the studio's Quit, so the expansion takes Ctrl+Shift+Q.
		bind("map.moveToLayer", {QStringLiteral("Shift+Q")}),
		bind("map.expandSelectionToLayers", {QStringLiteral("Ctrl+Shift+Q")}),
		// NRC radiant/select.cpp SelectNudgeLeft/Right/Up/Down and
		// MoveSelectionUP/DOWN at 449778b3 (GPL-2.0-or-later): Alt+arrows nudge
		// in the last plan view's axes from any view, the keypad's plus and
		// minus along world Z, ahead of Go Back and view zoom. Key facts only;
		// see docs/CREDITS.md.
		bind("map.nudgeLeft", {QStringLiteral("Alt+Left")}),
		bind("map.nudgeRight", {QStringLiteral("Alt+Right")}),
		bind("map.nudgeForward", {QStringLiteral("Alt+Up")}),
		bind("map.nudgeBack", {QStringLiteral("Alt+Down")}),
		bind("map.nudgeWorldUp", {QStringLiteral("Num++")}),
		bind("map.nudgeWorldDown", {QStringLiteral("Num+-")}),
		// NRC radiant/select.cpp "MoveToCamera" and "RepeatTransforms" at 449778b3
		// (GPL-2.0-or-later): Ctrl+Shift+V centres the selection on the camera and
		// Ctrl+R repeats the recorded transforms on a copy (core/level_paste.h,
		// core/level_repeat.h). Key facts only; see docs/CREDITS.md. Its Alt+R
		// (Reset Transforms) stays off a menu-bar mnemonic chord here.
		bind("map.moveToCamera", {QStringLiteral("Ctrl+Shift+V")}),
		bind("map.repeatTransforms", {QStringLiteral("Ctrl+R")}),
		// NRC radiant/select.cpp "ArbitraryRotation" and "ArbitraryScale" at
		// 449778b3 (GPL-2.0-or-later): Shift+R opens the rotation window and
		// Ctrl+Shift+S the scale window. NRC's Save Map As has no key, and the
		// studio's Save Map As keeps Ctrl+Alt+S, so neither clashes. Key facts
		// only; see docs/CREDITS.md.
		bind("map.rotatePrecisely", {QStringLiteral("Shift+R")}),
		bind("map.scaleSelection", {QStringLiteral("Ctrl+Shift+S")}),
		// NRC radiant/filters.cpp ConstructFilters at 449778b3
		// (GPL-2.0-or-later): the Filter menu's keys, for the kinds VibeStudio
		// filters. Key facts only; see docs/CREDITS.md. Ctrl+P (Patches) stays
		// unbound: the studio keeps it for Go to File, a window-wide key, and
		// Lightgrid has no key upstream either.
		bind("map.filterWorld", {QStringLiteral("Alt+1")}),
		bind("map.filterEntities", {QStringLiteral("Alt+2")}),
		bind("map.filterAreaportals", {QStringLiteral("Alt+3")}),
		bind("map.filterTranslucent", {QStringLiteral("Alt+4")}),
		bind("map.filterLiquids", {QStringLiteral("Alt+5")}),
		bind("map.filterCaulk", {QStringLiteral("Alt+6")}),
		bind("map.filterClip", {QStringLiteral("Alt+7")}),
		bind("map.filterPaths", {QStringLiteral("Alt+8")}),
		bind("map.filterClusterportals", {QStringLiteral("Alt+9")}),
		bind("map.filterLights", {QStringLiteral("Alt+0")}),
		bind("map.filterStructural", {QStringLiteral("Ctrl+Shift+D")}),
		bind("map.filterDetail", {QStringLiteral("Ctrl+D")}),
		bind("map.filterHint", {QStringLiteral("Ctrl+H")}),
		bind("map.filterModels", {QStringLiteral("Shift+M")}),
		bind("map.filterTriggers", {QStringLiteral("Ctrl+Shift+T")}),
		bind("map.filterBotclip", {QStringLiteral("Alt+M")}),
		bind("map.filterDecals", {QStringLiteral("Shift+D")}),
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
		// The Radiant family's Center on End: Q3Radiant MainFrm.cpp
		// ID_VIEW_CENTER at dbe4ddb1, kept by GtkRadiant's and NetRadiant's
		// CenterView (b4b295d7 camwindow.cpp). Key facts only; see docs/CREDITS.md.
		bind("map.levelCamera", {QStringLiteral("End")}),
		bind("map.toggle3D", {QStringLiteral("Ctrl+Shift+C")}),
		bind("map.carve", {QStringLiteral("Shift+U")}),
		bind("map.snapToGrid", {QStringLiteral("Ctrl+G")}),
		bind("map.connectEntities", {QStringLiteral("Ctrl+K")}),
		bind("map.loadLeakTrail", {QStringLiteral("Shift+L")}),
		bind("map.undo", {QStringLiteral("Ctrl+Z")}),
		bind("map.redo", {QStringLiteral("Ctrl+Y")}),
		// C sinks the camera.
		bind("map.previewWireframe", {}),
		// The Radiant family's nudge keys, which NetRadiant Custom keeps from
		// GtkRadiant (radiant/select.cpp at 449778b3): Alt+arrows in the plan
		// view's axes, the keypad's plus and minus along world Z.
		bind("map.nudgeLeft", {QStringLiteral("Alt+Left")}),
		bind("map.nudgeRight", {QStringLiteral("Alt+Right")}),
		bind("map.nudgeForward", {QStringLiteral("Alt+Up")}),
		bind("map.nudgeBack", {QStringLiteral("Alt+Down")}),
		bind("map.nudgeWorldUp", {QStringLiteral("Num++")}),
		bind("map.nudgeWorldDown", {QStringLiteral("Num+-")}),
	};
	controls.keys += gridDigitKeys();
	return controls;
}

QString levelViewLayoutId(LevelViewLayout layout)
{
	switch (layout) {
	case LevelViewLayout::CameraBesidePlans:
		return QStringLiteral("camera-beside-plans");
	case LevelViewLayout::CameraAbovePlans:
		return QStringLiteral("camera-above-plans");
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

bool levelViewLayoutHasThreePlans(LevelViewLayout layout)
{
	return layout == LevelViewLayout::FourViews || layout == LevelViewLayout::CameraAbovePlans
		|| layout == LevelViewLayout::CameraBesidePlans;
}

bool levelViewLayoutForId(const QString& id, LevelViewLayout* layout)
{
	for (const auto candidate : {LevelViewLayout::Single2D, LevelViewLayout::Single3D, LevelViewLayout::CameraAndPlan, LevelViewLayout::FourViews, LevelViewLayout::CameraAbovePlans, LevelViewLayout::CameraBesidePlans}) {
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
	case LevelViewLayout::CameraBesidePlans:
		return QCoreApplication::translate("VibeStudioLevelControls", "Camera Beside Plans");
	case LevelViewLayout::CameraAbovePlans:
		return QCoreApplication::translate("VibeStudioLevelControls", "Camera Above Plans");
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

int levelPlanPanesShownCount(const LevelPlanPanes& panes)
{
	return static_cast<int>(std::count(panes.shown.cbegin(), panes.shown.cend(), true));
}

bool setLevelPlanPaneShown(LevelPlanPanes* panes, int index, bool shown)
{
	if (!panes || index < 0 || index >= static_cast<int>(panes->shown.size())) {
		return false;
	}
	auto& pane = panes->shown[static_cast<std::size_t>(index)];
	if (!shown && pane && levelPlanPanesShownCount(*panes) <= 1) {
		return false;
	}
	pane = shown;
	return true;
}

QString levelPlanPaneId(int index)
{
	switch (index) {
	case 0:
		return QStringLiteral("top");
	case 1:
		return QStringLiteral("front");
	case 2:
		return QStringLiteral("side");
	default:
		break;
	}
	return {};
}

QString levelPlanPanesId(const LevelPlanPanes& panes)
{
	QStringList ids;
	for (int i = 0; i < static_cast<int>(panes.shown.size()); ++i) {
		if (panes.shown[static_cast<std::size_t>(i)]) {
			ids << levelPlanPaneId(i);
		}
	}
	return ids.join(QLatin1Char(','));
}

bool levelPlanPanesForId(const QString& id, LevelPlanPanes* panes)
{
	const QString trimmed = id.trimmed();
	if (trimmed == QStringLiteral("all")) {
		if (panes) {
			*panes = LevelPlanPanes {};
		}
		return true;
	}
	LevelPlanPanes parsed;
	parsed.shown = {false, false, false};
	const QStringList parts = trimmed.split(QLatin1Char(','));
	for (const QString& part : parts) {
		int index = -1;
		for (int i = 0; i < 3; ++i) {
			if (part.trimmed() == levelPlanPaneId(i)) {
				index = i;
			}
		}
		if (index < 0 || parsed.shown[static_cast<std::size_t>(index)]) {
			return false;
		}
		parsed.shown[static_cast<std::size_t>(index)] = true;
	}
	if (levelPlanPanesShownCount(parsed) == 0) {
		return false;
	}
	if (panes) {
		*panes = parsed;
	}
	return true;
}

int levelPlanPaneNextShown(const LevelPlanPanes& panes, int from)
{
	const int count = static_cast<int>(panes.shown.size());
	const int start = ((from % count) + count) % count;
	for (int step = 0; step < count; ++step) {
		const int index = (start + step) % count;
		if (panes.shown[static_cast<std::size_t>(index)]) {
			return index;
		}
	}
	return 0;
}

QString levelViewSplitterStatePrefix(LevelViewLayout layout, const LevelPlanPanes& panes)
{
	QString prefix = QStringLiteral("levelViews/%1/").arg(levelViewLayoutId(layout));
	if (levelViewLayoutHasThreePlans(layout) && levelPlanPanesShownCount(panes) < static_cast<int>(panes.shown.size())) {
		prefix += levelPlanPanesId(panes).replace(QLatin1Char(','), QLatin1Char('-')) + QLatin1Char('/');
	}
	return prefix;
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
	row(layout, QCoreApplication::translate("VibeStudioLevelControls", "Changing the grid"),
		controls.gridChangeEnablesSnap ? QCoreApplication::translate("VibeStudioLevelControls", "Turns Snap on")
			: QCoreApplication::translate("VibeStudioLevelControls", "Leaves Snap as it was"));
	row(layout, QCoreApplication::translate("VibeStudioLevelControls", "Quick-turn pivot"),
		controls.quickTurnSnapsPivot ? QCoreApplication::translate("VibeStudioLevelControls", "Selection centre rounded to the grid while Snap is on")
			: QCoreApplication::translate("VibeStudioLevelControls", "Exact selection centre"));
	row(layout, QCoreApplication::translate("VibeStudioLevelControls", "Flip pivot"),
		controls.flipSnapsPivot ? QCoreApplication::translate("VibeStudioLevelControls", "Selection centre rounded to half the grid while Snap is on")
			: QCoreApplication::translate("VibeStudioLevelControls", "Exact selection centre"));
	row(layout, QCoreApplication::translate("VibeStudioLevelControls", "Texture rotation step"),
		controls.surfaceRotateAboutOrigin
			? QCoreApplication::translate("VibeStudioLevelControls", "%1° about the texture origin").arg(controls.surfaceRotationStep)
			: QCoreApplication::translate("VibeStudioLevelControls", "%1° about each face centre").arg(controls.surfaceRotationStep));
	row(layout, QCoreApplication::translate("VibeStudioLevelControls", "Paste placement"),
		controls.pasteAtCursor ? QCoreApplication::translate("VibeStudioLevelControls", "Cursor") :
			QCoreApplication::translate("VibeStudioLevelControls", "Original position"));
	row(layout, QCoreApplication::translate("VibeStudioLevelControls", "Duplicate placement"),
		controls.nudgeAfterDuplicate ? QCoreApplication::translate("VibeStudioLevelControls", "One grid step right and down in the active plan")
			: QCoreApplication::translate("VibeStudioLevelControls", "Original position"));
	row(layout, QCoreApplication::translate("VibeStudioLevelControls", "Duplicate selection mode"),
		controls.duplicateRequiresObjectMode ? QCoreApplication::translate("VibeStudioLevelControls", "Object mode only")
			: QCoreApplication::translate("VibeStudioLevelControls", "Selected objects, including while editing their components"));
	row(layout, QCoreApplication::translate("VibeStudioLevelControls", "Entity connections"),
		controls.entityConnectionMode == LevelEntityConnectionMode::RadiantTarget
			? QCoreApplication::translate("VibeStudioLevelControls", "Two objects, source name first; KillConnect uses the active game")
			: QCoreApplication::translate("VibeStudioLevelControls", "Multiple sources, destination name first"));
	row(layout, QCoreApplication::translate("VibeStudioLevelControls", "Copy selection"),
		controls.copyFaceAttributes ? QCoreApplication::translate("VibeStudioLevelControls", "Picked face attributes, otherwise objects") :
			QCoreApplication::translate("VibeStudioLevelControls", "Objects; use Copy Surface for face attributes"));
	row(layout, QCoreApplication::translate("VibeStudioLevelControls", "Delete in component mode"),
		controls.deleteObjectsWithoutComponents ? QCoreApplication::translate("VibeStudioLevelControls", "Picked brush components; objects when no components are picked")
			: QCoreApplication::translate("VibeStudioLevelControls", "Picked brush components; no object fallback"));
	row(layout, QCoreApplication::translate("VibeStudioLevelControls", "Patch points during deletion"),
		controls.deleteIgnoresPatchPoints ? QCoreApplication::translate("VibeStudioLevelControls", "Keep patch points and remove picked brush components")
			: QCoreApplication::translate("VibeStudioLevelControls", "Deselect patch points before removing brush components"));
	row(layout, QCoreApplication::translate("VibeStudioLevelControls", "Delete command in clip mode"),
		controls.deleteRemovesClipPoint ? QCoreApplication::translate("VibeStudioLevelControls", "Remove the last clip point")
			: QCoreApplication::translate("VibeStudioLevelControls", "Finish clipping before deleting map objects"));
	row(layout, QCoreApplication::translate("VibeStudioLevelControls", "Object selection history"),
		controls.undoObjectSelection ? QCoreApplication::translate("VibeStudioLevelControls", "Undo and redo object picks and group navigation without changing map contents")
			: QCoreApplication::translate("VibeStudioLevelControls", "Selection and group navigation stay outside undo history"));
	row(layout, QCoreApplication::translate("VibeStudioLevelControls", "Temporary visibility"),
		controls.undoObjectSelection ? QCoreApplication::translate("VibeStudioLevelControls", "Hide, Show All, Isolate and object-list reveal are non-content undo steps")
			: QCoreApplication::translate("VibeStudioLevelControls", "Hide, Show All and Isolate stay outside undo history"));
	row(layout, QCoreApplication::translate("VibeStudioLevelControls", "New and duplicated objects"),
		controls.insertionVisibility == LevelInsertionVisibility::FreshInherited
			? QCoreApplication::translate("VibeStudioLevelControls", "Fresh temporary visibility; hidden parents still hide new objects")
			: QCoreApplication::translate("VibeStudioLevelControls", "Reveal inserted roots; copies retain independently hidden descendants"));
	row(layout, QCoreApplication::translate("VibeStudioLevelControls", "Select by region"),
		controls.regionSelection == LevelRegionSelectionPolicy::TrenchBroom
			? QCoreApplication::translate("VibeStudioLevelControls", "Each brush's volume; removes selection brushes in one undo step")
			: controls.regionSelection == LevelRegionSelectionPolicy::Radiant
				? QCoreApplication::translate("VibeStudioLevelControls", "Each brush's bounds; Inside removes selected objects, Touching adds matches")
				: QCoreApplication::translate("VibeStudioLevelControls", "Combined selection bounds; keeps geometry"));
	row(layout, QCoreApplication::translate("VibeStudioLevelControls", "Make Structural"),
		controls.makeStructuralLeavesEntities
			? QCoreApplication::translate("VibeStudioLevelControls", "Clear detail and move the brushes out of brush entities to the world or current group")
			: QCoreApplication::translate("VibeStudioLevelControls", "Clear detail; brushes of other brush entities stay in them"));

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
	if (c.perspective && !c.focusHoldKey.isEmpty()) {
		row(camera, QCoreApplication::translate("VibeStudioLevelControls", "Keep the selection in view"),
			QCoreApplication::translate("VibeStudioLevelControls", "Hold %1 during mouse look; the wheel moves nearer or farther").arg(c.focusHoldKey));
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
		QStringList speedNotes;
		if (c.fastModifiers != Qt::NoModifier) {
			speedNotes << QCoreApplication::translate("VibeStudioLevelControls", "%1 faster").arg(modifierPrefix(c.fastModifiers).chopped(1));
		}
		if (c.slowModifiers != Qt::NoModifier) {
			speedNotes << QCoreApplication::translate("VibeStudioLevelControls", "%1 slower").arg(modifierPrefix(c.slowModifiers).chopped(1));
		}
		row(camera, c.flyNeedsLook ? QCoreApplication::translate("VibeStudioLevelControls", "Fly while looking") : QCoreApplication::translate("VibeStudioLevelControls", "Fly"),
			speedNotes.isEmpty() ? keysText(c.flyKeys) : QCoreApplication::translate("VibeStudioLevelControls", "%1 (%2)").arg(keysText(c.flyKeys), speedNotes.join(QStringLiteral(", "))));
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
	if (!levelEditorGridSizeSupported(controls.defaultGridUnits)) {
		problems << QCoreApplication::translate("VibeStudioLevelControls", "The default grid must be a listed size from 0.125 to 1024 units.");
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
	// No key may belong to two commands. The camera's texture moves and flips
	// are apart: they take their keys only while faces are picked there, and
	// the same keys work the views otherwise (core/level_camera_texture.h).
	QHash<QString, QString> owners, faceOwners;
	for (const LevelEditorKeyBinding& binding : controls.keys) {
		QHash<QString, QString>& keyOwners = levelCameraTextureCommandForId(binding.commandId) ? faceOwners : owners;
		for (const QString& key : binding.keys) {
			const auto sequence = QKeySequence::fromString(key, QKeySequence::PortableText);
			if (sequence.isEmpty() || sequence[0].key() == Qt::Key_unknown) {
				problems << QCoreApplication::translate("VibeStudioLevelControls", "Invalid shortcut: %1.").arg(key);
				continue;
			}
			const QString normalized = sequence.toString(QKeySequence::PortableText).toLower();
			const auto owner = keyOwners.constFind(normalized);
			if (owner != keyOwners.constEnd() && owner.value() != binding.commandId) {
				problems << QCoreApplication::translate("VibeStudioLevelControls", "%1 is bound to both %2 and %3.").arg(key, owner.value(), binding.commandId);
				continue;
			}
			keyOwners.insert(normalized, binding.commandId);
		}
	}
	for (auto face = faceOwners.cbegin(); face != faceOwners.cend(); ++face) {
		if (!owners.contains(face.key())) {
			owners.insert(face.key(), face.value());
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
	if (!camera.focusHoldKey.isEmpty()) {
		const auto focus = QKeySequence::fromString(camera.focusHoldKey, QKeySequence::PortableText);
		if (!camera.perspective || focus.count() != 1 || focus[0].key() == Qt::Key_unknown || focus[0].keyboardModifiers() != Qt::NoModifier) {
			problems << QCoreApplication::translate("VibeStudioLevelControls", "The camera focus key needs a perspective camera and one unmodified key.");
		}
		if (owners.contains(focus.toString(QKeySequence::PortableText).toLower())) {
			problems << QCoreApplication::translate("VibeStudioLevelControls", "The camera focus key also belongs to a command.");
		}
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
