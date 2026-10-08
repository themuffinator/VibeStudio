#include "core/level_editor_controls.h"
#include <algorithm>
#include <cmath>

namespace vibestudio {
namespace {

// Independent behavior adaptations; no upstream implementation is incorporated.
// Source revisions, documentation and remaining differences: docs/EDITOR_PROFILES.md
// and docs/CREDITS.md. These schemes always use VibeStudio's editing services.
void key(LevelEditorControls& controls, const char* command, const QStringList& keys)
{
	const QString id = QString::fromLatin1(command);
	for (auto& binding : controls.keys) {
		if (binding.commandId == id) { binding.keys = keys; return; }
	}
	controls.keys.push_back({id, keys});
}

LevelEditorControls baseControls()
{
	LevelEditorControls controls;
	controls.defaultGridUnits = 16;
	controls.camera.perspective = true;
	controls.camera.orbitButton = Qt::NoButton;
	controls.camera.lookButton = Qt::RightButton;
	controls.camera.leftPanModifiers = Qt::NoModifier;
	controls.camera.toggleModifiers = Qt::ControlModifier;
	controls.camera.faceModifiers = Qt::ShiftModifier;
	controls.camera.wheel = CameraWheel::Dolly;
	controls.camera.dragMovesSelection = true;
	key(controls, "map.open", {QStringLiteral("Ctrl+O")});
	key(controls, "map.save-as", {QStringLiteral("Ctrl+Shift+S")});
	key(controls, "map.undo", {QStringLiteral("Ctrl+Z")});
	key(controls, "map.redo", {QStringLiteral("Ctrl+Y"), QStringLiteral("Ctrl+Shift+Z")});
	key(controls, "map.duplicateSelection", {QStringLiteral("Ctrl+D")});
	key(controls, "map.deleteSelection", {QStringLiteral("Del")});
	key(controls, "map.selectAll", {QStringLiteral("Ctrl+A")});
	key(controls, "map.selectNone", {QStringLiteral("Ctrl+Shift+A")});
	key(controls, "map.gridSmaller", {QStringLiteral("[")});
	key(controls, "map.gridLarger", {QStringLiteral("]")});
	// Keep movement letters free. Paint, Draw Sector and Wireframe remain in
	// the toolbar and command palette, with user shortcuts available.
	key(controls, "map.previewWireframe", {});
	key(controls, "map.drawSector", {});
	return controls;
}

void wasd(CameraViewControls& camera)
{
	camera.flyKeys = {QStringLiteral("W"), QStringLiteral("S"), QStringLiteral("A"), QStringLiteral("D"),
		QStringLiteral("E"), QStringLiteral("Q"), {}, {}};
}

} // namespace

LevelEditorControls familiarLevelControls(const QString& id)
{
	LevelEditorControls controls = baseControls();
	auto& plan = controls.plan;
	auto& camera = controls.camera;
	if (id == QLatin1String("doomedit")) {
		// id Software DOOM-3 a9c49da5, MainFrm.cpp / CamWnd.cpp / XYWnd.cpp,
		// GPL-3.0-or-later with additional terms. Behaviour facts only; see credits.
		controls = familiarLevelControls(QStringLiteral("q3radiant"));
		camera.lookButton = Qt::RightButton;
		camera.lookModifiers = Qt::ControlModifier | Qt::ShiftModifier;
		// DoomEdit has its own texture editing; do not inherit unaudited gestures.
		camera.materialSampleButton = Qt::NoButton;
		camera.materialPaintButton = Qt::NoButton;
		camera.surfacePasteFaceButton = Qt::NoButton;
		camera.surfacePasteBrushButton = Qt::NoButton;
		key(controls, "map.open", {QStringLiteral("Ctrl+O")});
		key(controls, "map.nextProjection", {QStringLiteral("Ctrl+Tab"), QStringLiteral("Home")});
		key(controls, "map.mergeBrushes", {QStringLiteral("Shift+M")});
		key(controls, "map.isolateSelection", {QStringLiteral("Ctrl+Shift+H")});
		key(controls, "map.toggleGrid", {QStringLiteral("0")});
		// Ctrl+U and Shift+U are axial texture tools; I opens the entity list.
		key(controls, "map.carve", {});
		key(controls, "map.invertSelection", {});
		return controls;
	}
	if (id == QLatin1String("bsp")) {
		// BSP 0.97q7 Settings/{bspmouse,bspmou3d,keyboard}.cfg and release notes,
		// reviewed 2026-10-07. Public binding facts only; no proprietary code,
		// configuration text, binaries or game assets are incorporated. See credits.
		controls.layout = LevelViewLayout::FourViews;
		plan.panButtons = {Qt::RightButton};
		plan.plainClickSelects = false;
		plan.toggleModifiers = Qt::ShiftModifier;
		plan.addModifiers = Qt::NoModifier;
		plan.bandModifiers = Qt::ShiftModifier;
		plan.emptyDrag = PlanEmptyDrag::DrawBrush;
		plan.emptyDragWithSelection = PlanEmptyDrag::ResizeSelection;
		plan.squareModifiers = plan.cubeModifiers = Qt::NoModifier;
		camera.lookButton = Qt::MiddleButton;
		camera.panButtons = {Qt::MiddleButton};
		camera.panModifiers = Qt::ShiftModifier;
		camera.slowModifiers = Qt::NoModifier;
		camera.materialSampleButton = Qt::RightButton;
		camera.materialSampleModifiers = Qt::NoModifier;
		camera.flyKeys = {QStringLiteral("W"), QStringLiteral("S"), QStringLiteral("A"), QStringLiteral("D"),
			QStringLiteral("R"), QStringLiteral("F"), QStringLiteral("Q"), QStringLiteral("E"),
			QStringLiteral("PgDown"), QStringLiteral("Del")};
		camera.driveKeys = {QStringLiteral("Up"), QStringLiteral("Down"), QStringLiteral("Ins"), QStringLiteral("PgUp"),
			QStringLiteral("Home"), QStringLiteral("End"), QStringLiteral("Left"), QStringLiteral("Right")};
		camera.turnKeysStrafeInLook = false;
		key(controls, "map.duplicateSelection", {QStringLiteral("Ctrl+Space")});
		key(controls, "map.deleteSelection", {QStringLiteral("Ctrl+X"), QStringLiteral("Num+-")});
		key(controls, "map.selectAll", {QStringLiteral("`")});
		key(controls, "map.selectNone", {QStringLiteral("Escape")});
		key(controls, "map.snapToGrid", {QStringLiteral("Alt+S")});
		key(controls, "map.frameSelection", {QStringLiteral("Ctrl+Shift+Y")});
		key(controls, "map.alignSurfaces", {QStringLiteral("Z")});
		key(controls, "map.hideSelection", {QStringLiteral("H")});
		key(controls, "map.showAll", {QStringLiteral("Alt+R")});
		key(controls, "map.carve", {QStringLiteral("C"), QStringLiteral("Shift+C")});
		key(controls, "map.toggle3D", {QStringLiteral("Ctrl+Return")});
		// Native split/clip-point modes do not match the shared modal clip tool.
		key(controls, "map.clipTool", {});
		return controls;
	}
	if (id == QLatin1String("gtkradiant-1-4") || id == QLatin1String("gtkradiant-1-5")) {
		// GtkRadiant 1.4.0-era ZeroRadiant 5fc27697 and 1.5 01767337:
		// mainframe, camwindow, xywindow, selection/drag and grid defaults.
		// GPL-2.0-or-later; independent behavior adaptation, see docs/CREDITS.md.
		controls = gtkRadiantLevelControls();
		plan.fixedCameraSteps = true;
		camera.discreteDriveKeys = true;
		camera.driveKeys.pitchUp = QStringLiteral("A");
		camera.driveKeys.pitchDown = QStringLiteral("Z");
		// Modified arrows belong to texture tools, not accelerated camera steps.
		camera.speedModifiersRequireLook = true;
		key(controls, "map.open", {QStringLiteral("Ctrl+O")});
		key(controls, "map.selectNone", {QStringLiteral("Escape")});
		key(controls, "map.selectAll", {});
		key(controls, "map.mergeBrushes", {QStringLiteral("Ctrl+U")});
		key(controls, "map.alignSurfaces", {QStringLiteral("S")});
		key(controls, "map.editPatch", {QStringLiteral("Shift+S")});
		key(controls, "map.capPatch", {QStringLiteral("Shift+C")});
		key(controls, "map.textureLock", {QStringLiteral("Shift+T")});
		key(controls, "map.surfaceShiftLeft", {QStringLiteral("Shift+Left")});
		key(controls, "map.surfaceShiftRight", {QStringLiteral("Shift+Right")});
		key(controls, "map.surfaceShiftDown", {QStringLiteral("Shift+Down")});
		key(controls, "map.surfaceShiftUp", {QStringLiteral("Shift+Up")});
		key(controls, "map.surfaceRotateLeft", {QStringLiteral("Shift+PgUp")});
		key(controls, "map.surfaceRotateRight", {QStringLiteral("Shift+PgDown")});
		key(controls, "map.surfaceFit", {QStringLiteral("Shift+B")});
		if (id == QLatin1String("gtkradiant-1-5")) {
			// 1.5's selection system uses Shift area selection, unlike 1.4's Alt.
			plan.bandModifiers = Qt::ShiftModifier;
			camera.materialPaintButton = Qt::NoButton;
			camera.surfacePasteBrushButton = Qt::NoButton;
			// Keep middle sampling and Ctrl+Shift+middle application. The latter
			// uses our explicit hit-face clipboard target, as documented.
			camera.flyKeys = {QStringLiteral("Up"), QStringLiteral("Down"),
				QStringLiteral("Left"), QStringLiteral("Right"), {}, {}, {}, {}};
			key(controls, "map.loadLeakTrail", {});
		}
		return controls;
	}
	if (id == QLatin1String("qeradiant")) {
		// Eutectic's QeRadiant/Q3Radiant manual, Appendix G (reviewed 2026-10-07).
		// Facts only: no manual prose or external implementation is incorporated.
		controls = familiarLevelControls(QStringLiteral("q3radiant"));
		key(controls, "map.open", {QStringLiteral("Ctrl+O")});
		key(controls, "map.surfaceFit", {QStringLiteral("Shift+5"), QStringLiteral("Ctrl+F")});
		// These Q3-specific keys must not run unrelated tools for Qe users.
		key(controls, "map.editPatch", {});
		key(controls, "map.capPatch", {});
		key(controls, "map.textureLock", {});
		key(controls, "map.hideSelection", {});
		key(controls, "map.showAll", {});
		key(controls, "map.selectSimilar", {});
		return controls;
	}
	if (id == QLatin1String("q3radiant")) {
		// id Software Q3Radiant dbe4ddb1, GPL-2.0-or-later: MainFrm.cpp,
		// CamWnd.cpp, XYWnd.cpp, PrefsDlg.cpp, QE3.CPP and DRAG.CPP. See credits.
		controls = gtkRadiantLevelControls();
		plan.zoomDragButton = Qt::NoButton;
		plan.fixedCameraSteps = true;
		camera.lookButton = Qt::NoButton;
		camera.lookClickToggles = false;
		camera.driveButton = Qt::RightButton;
		camera.panButtons = {Qt::RightButton};
		camera.panModifiers = Qt::ControlModifier;
		camera.flyKeys = {};
		camera.discreteDriveKeys = true;
		camera.driveKeys.pitchUp = QStringLiteral("A");
		camera.driveKeys.pitchDown = QStringLiteral("Z");
		camera.fastModifiers = Qt::NoModifier;
		camera.slowModifiers = Qt::NoModifier;
		camera.turnKeysStrafeInLook = false;
		key(controls, "map.selectNone", {QStringLiteral("Escape")});
		key(controls, "map.selectAll", {});
		key(controls, "map.invertSelection", {});
		key(controls, "map.frameSelection", {});
		key(controls, "map.loadLeakTrail", {});
		key(controls, "map.mergeBrushes", {QStringLiteral("Ctrl+U")});
		key(controls, "map.alignSurfaces", {QStringLiteral("S")});
		key(controls, "map.editPatch", {QStringLiteral("Shift+S")});
		key(controls, "map.capPatch", {QStringLiteral("Shift+C")});
		key(controls, "map.textureLock", {QStringLiteral("Shift+T")});
		// MainFrm.cpp texture shortcuts. Operations use the explicit Surfaces
		// target and VibeStudio's centre-anchored UV service; see profile notes.
		key(controls, "map.surfaceShiftLeft", {QStringLiteral("Shift+Left")});
		key(controls, "map.surfaceShiftRight", {QStringLiteral("Shift+Right")});
		key(controls, "map.surfaceShiftDown", {QStringLiteral("Shift+Down")});
		key(controls, "map.surfaceShiftUp", {QStringLiteral("Shift+Up")});
		key(controls, "map.surfaceRotateLeft", {QStringLiteral("Shift+PgUp")});
		key(controls, "map.surfaceRotateRight", {QStringLiteral("Shift+PgDown")});
		key(controls, "map.surfaceFit", {QStringLiteral("Shift+5")});
		return controls;
	}
	if (id == QLatin1String("netradiant")) {
		// Independent adaptation of Xonotic NetRadiant b4b295d7 (GPL-2.0-or-later):
		// radiant/{camwindow,xywindow,selection,mainframe,grid}.cpp. See credits.
		controls = netRadiantCustomLevelControls();
		// NetRadiant b4b295d7 surfacedialog.cpp pastes values, not seamless UVs.
		camera.surfaceWrapFaceButton = Qt::NoButton;
		camera.surfaceValuesButton = camera.surfaceValuesOnlyButton = camera.surfaceWrapOnlyButton = Qt::NoButton;
		camera.surfaceProjectButton = camera.surfaceProjectOnlyButton = Qt::NoButton;
		camera.surfacePasteFaceButton = Qt::MiddleButton;
		camera.surfacePasteFaceModifiers = Qt::ShiftModifier;
		controls.defaultGridUnits = 8;
		camera.fieldOfViewDegrees = 110;
		camera.orbitButton = Qt::NoButton;
		camera.panButtons.clear();
		key(controls, "map.duplicateSelection", {QStringLiteral("Space")});
		key(controls, "map.deleteSelection", {QStringLiteral("Backspace"), QStringLiteral("Z")});
		key(controls, "map.selectNone", {QStringLiteral("Escape"), QStringLiteral("C")});
		key(controls, "map.selectAll", {});
		key(controls, "map.selectSimilar", {QStringLiteral("Shift+A")});
		key(controls, "map.zoomIn", {QStringLiteral("Del")});
		key(controls, "map.zoomOut", {QStringLiteral("Ins")});
		key(controls, "map.frameSelection", {QStringLiteral("`"), QStringLiteral("Ctrl+Shift+Tab")});
		key(controls, "map.redo", {QStringLiteral("Ctrl+Y")});
		return controls;
	}
	if (id == QLatin1String("sledge")) {
		// Sledge 2.0.7.2, 8762a6de (BSD-3-Clause): CameraNavigationViewportSettings,
		// PerspectiveCameraNavigationViewportListener, OrthographicCameraViewportListener
		// and command DefaultHotkey attributes. Behavior only; no upstream code.
		controls.layout = LevelViewLayout::FourViews;
		plan.panHoldKey = QStringLiteral("Space");
		plan.panArrowModifiers = Qt::ShiftModifier;
		camera.fieldOfViewDegrees = 60;
		camera.lookButton = Qt::NoButton;
		camera.lookToggleKey = QStringLiteral("Z");
		camera.lookHoldKey = QStringLiteral("Space");
		camera.lookPanUsesButtons = true;
		camera.panButtons.clear();
		wasd(camera);
		camera.flyKeys.up = QStringLiteral("Q"); camera.flyKeys.down = QStringLiteral("E");
		camera.driveKeys.turnLeft = QStringLiteral("Left"); camera.driveKeys.turnRight = QStringLiteral("Right");
		camera.driveKeys.pitchUp = QStringLiteral("Up"); camera.driveKeys.pitchDown = QStringLiteral("Down");
		camera.turnKeysStrafeInLook = false;
		camera.turnKeysPanModifiers = Qt::ShiftModifier;
		camera.speedModifiersRequireLook = true;
		camera.slowModifiers = Qt::ControlModifier;
		key(controls, "map.duplicateSelection", {});
		key(controls, "map.selectNone", {QStringLiteral("Shift+Q")});
		key(controls, "map.clipTool", {QStringLiteral("Shift+X")});
		key(controls, "map.carve", {QStringLiteral("Ctrl+Shift+C")});
		key(controls, "map.hollowSelection", {QStringLiteral("Ctrl+Shift+H")});
		key(controls, "map.hideSelection", {QStringLiteral("H")});
		key(controls, "map.isolateSelection", {QStringLiteral("Ctrl+H")});
		key(controls, "map.showAll", {QStringLiteral("U")});
		key(controls, "map.textureLock", {QStringLiteral("Shift+L")});
		key(controls, "map.toggleGrid", {QStringLiteral("Shift+R")});
		key(controls, "map.toggleSnap", {QStringLiteral("Shift+W")});
		key(controls, "map.maximizeView", {QStringLiteral("Shift+Z")});
		key(controls, "build.runAndLaunch", {QStringLiteral("F9")});
		return controls;
	}
	if (id == QLatin1String("hammer") || id == QLatin1String("jack")) {
		controls.layout = LevelViewLayout::FourViews;
		camera.lookButton = Qt::MiddleButton;
		camera.lookToggleKey = QStringLiteral("Z");
		camera.flyNeedsLook = true;
		camera.panButtons = {Qt::RightButton};
		wasd(camera);
		camera.flyKeys.up.clear(); camera.flyKeys.down.clear();
		key(controls, "map.clipTool", {QStringLiteral("Shift+X")});
		key(controls, "map.carve", {QStringLiteral("Ctrl+Shift+C")});
		key(controls, "map.hollowSelection", {QStringLiteral("Ctrl+H")});
		key(controls, "map.duplicateSelection", {});
		key(controls, "map.selectNone", {QStringLiteral("Shift+Q")});
		key(controls, "map.snapToGrid", {QStringLiteral("Ctrl+B")});
		key(controls, "map.flipVertical", {QStringLiteral("Ctrl+I")});
		key(controls, "map.flipHorizontal", {QStringLiteral("Ctrl+L")});
		key(controls, "map.textureLock", {QStringLiteral("Shift+L")});
		key(controls, "map.frameSelection", {QStringLiteral("Ctrl+E")});
		key(controls, "map.toggleGrid", {QStringLiteral("Shift+R")});
		key(controls, "map.toggleSnap", {QStringLiteral("Shift+W")});
		key(controls, "map.nextProjection", {QStringLiteral("Tab")});
		// Hammer 3.4 Hotkey Reference; J.A.C.K. 1.1 Reference Manual.
		key(controls, "map.maximizeView", {QStringLiteral("Shift+Z")});
		key(controls, "build.runAndLaunch", {QStringLiteral("F9")});
		if (id == QLatin1String("jack")) {
			// J.A.C.K. differs from classic Hammer here. Ctrl+H hides the
			// unselected objects; it must never unexpectedly hollow geometry.
			key(controls, "map.isolateSelection", {QStringLiteral("Ctrl+H")});
			key(controls, "map.hollowSelection", {QStringLiteral("Ctrl+U"), QStringLiteral("Ctrl+Shift+H")});
			key(controls, "map.selectNone", {QStringLiteral("Ctrl+Q"), QStringLiteral("Shift+Q")});
			key(controls, "map.invertSelection", {QStringLiteral("Shift+I")});
			key(controls, "map.hideSelection", {QStringLiteral("H")});
			key(controls, "map.showAll", {QStringLiteral("U")});
			key(controls, "map.rotateLeft", {QStringLiteral("Ctrl+R")});
			key(controls, "map.rotateRight", {QStringLiteral("Ctrl+Shift+R")});
		} else {
			// Hammer uses Ctrl+A for its four-pane sizing, not Select All.
			key(controls, "map.selectAll", {});
			key(controls, "map.equalizeViews", {QStringLiteral("Ctrl+A")});
		}
		return controls;
	}
	if (id == QLatin1String("quark")) {
		controls.layout = LevelViewLayout::FourViews;
		plan.panButtons = {Qt::RightButton};
		plan.zoomDragButton = Qt::MiddleButton;
		camera.lookButton = Qt::RightButton;
		camera.panButtons = {Qt::MiddleButton};
		camera.driveKeys = {QStringLiteral("Up"), QStringLiteral("Down"), QStringLiteral("End"), QStringLiteral("PgDown"),
			QStringLiteral("D"), QStringLiteral("C"), QStringLiteral("Left"), QStringLiteral("Right")};
		key(controls, "map.selectNone", {QStringLiteral("Escape")});
		return controls;
	}
	if (id == QLatin1String("darkradiant")) {
		controls = gtkRadiantLevelControls();
		// Texture gestures need their own DarkRadiant audit, not inherited
		// classic-Radiant defaults.
		camera.materialSampleButton = Qt::NoButton;
		camera.materialPaintButton = Qt::NoButton;
		camera.surfacePasteFaceButton = Qt::NoButton;
		camera.surfacePasteBrushButton = Qt::NoButton;
		camera.surfaceWrapFaceButton = Qt::NoButton;
		camera.surfaceValuesButton = camera.surfaceValuesOnlyButton = camera.surfaceWrapOnlyButton = Qt::NoButton;
		camera.surfaceProjectButton = camera.surfaceProjectOnlyButton = Qt::NoButton;
		camera.materialPaintModifiers = Qt::NoModifier;
		controls.defaultGridUnits = 8;
		controls.camera.flyKeys = {QStringLiteral("W"), QStringLiteral("S"), QStringLiteral("A"), QStringLiteral("D"), {}, {}, {}, {}};
		controls.camera.flyNeedsLook = true;
		key(controls, "map.open", {QStringLiteral("Ctrl+O")});
		key(controls, "map.save-as", {QStringLiteral("Ctrl+Shift+S")});
		key(controls, "map.deleteSelection", {QStringLiteral("Backspace")});
		key(controls, "map.gridSmaller", {QStringLiteral("[")});
		key(controls, "map.gridLarger", {QStringLiteral("]")});
		key(controls, "map.zoomIn", {QStringLiteral("Del")});
		key(controls, "map.zoomOut", {QStringLiteral("Ins")});
		return controls;
	}
	if (id == QLatin1String("doom-builder") || id == QLatin1String("ultimate-doom-builder")) {
		controls.layout = LevelViewLayout::Single2D;
		controls.defaultGridUnits = 32;
		camera.flyKeys = {QStringLiteral("E"), QStringLiteral("D"), QStringLiteral("S"), QStringLiteral("F"), {}, {}, {}, {}};
		key(controls, "map.toggle3D", {id == QLatin1String("doom-builder") ? QStringLiteral("W") : QStringLiteral("Q")});
		key(controls, "map.selectNone", {QStringLiteral("C")});
		key(controls, "map.clipTool", {});
		key(controls, "map.drawSector", {QStringLiteral("Ctrl+D")});
		key(controls, "map.duplicateSelection", {});
		key(controls, "build.runAndLaunch", {QStringLiteral("F9")});
		return controls;
	}
	if (id == QLatin1String("slade")) {
		controls.layout = LevelViewLayout::Single2D;
		controls.defaultGridUnits = 32;
		plan.panButtons = {Qt::RightButton};
		wasd(camera);
		camera.flyKeys.up = QStringLiteral("Up"); camera.flyKeys.down = QStringLiteral("Down");
		camera.driveKeys.turnLeft = QStringLiteral("Left"); camera.driveKeys.turnRight = QStringLiteral("Right");
		key(controls, "map.toggle3D", {QStringLiteral("Q")});
		key(controls, "map.toggleSnap", {QStringLiteral("Shift+G")});
		key(controls, "map.selectNone", {QStringLiteral("C")});
		key(controls, "map.clipTool", {});
		key(controls, "map.zoomIn", {QStringLiteral("=")});
		key(controls, "map.zoomOut", {QStringLiteral("-")});
		return controls;
	}
	if (id == QLatin1String("eureka")) {
		controls.layout = LevelViewLayout::Single2D;
		wasd(camera);
		camera.flyKeys.up = QStringLiteral("PgUp"); camera.flyKeys.down = QStringLiteral("PgDown");
		camera.fastModifiers = Qt::AltModifier; camera.slowModifiers = Qt::ShiftModifier;
		camera.driveKeys = {QStringLiteral("Up"), QStringLiteral("Down"), {}, {}, {}, {}, QStringLiteral("Left"), QStringLiteral("Right")};
		key(controls, "map.toggle3D", {QStringLiteral("Tab")});
		key(controls, "map.duplicateSelection", {QStringLiteral("O")});
		key(controls, "map.invertSelection", {QStringLiteral("Ctrl+I")});
		key(controls, "map.zoomIn", {QStringLiteral("=")});
		key(controls, "map.zoomOut", {QStringLiteral("-")});
		// VibeStudio's current grid tops out at 256; do not mislabel 512/1024.
		for (int digit = 1, units = 2; units <= 256; ++digit, units *= 2) {
			controls.keys.push_back({QStringLiteral("map.grid%1").arg(units), {QString::number(digit)}});
		}
		return controls;
	}
	if (id == QLatin1String("unreal") || id == QLatin1String("unity") || id == QLatin1String("godot") || id == QLatin1String("blender")) {
		controls.layout = LevelViewLayout::Single3D;
		wasd(camera);
		camera.flyNeedsLook = true;
		camera.orbitButton = Qt::LeftButton;
		camera.orbitModifiers = Qt::AltModifier;
		camera.dragMovesSelection = false; // Alt+left belongs to orbit, never vertical movement.
		plan.panButtons = {Qt::MiddleButton, Qt::RightButton};
		key(controls, "map.frameSelection", {QStringLiteral("F")});
		if (id == QLatin1String("godot") || id == QLatin1String("blender")) {
			camera.orbitButton = Qt::MiddleButton;
			camera.orbitModifiers = Qt::NoModifier;
			camera.panModifiers = Qt::ShiftModifier;
			camera.toggleModifiers = Qt::ShiftModifier;
			camera.faceModifiers = Qt::ControlModifier;
			camera.lookToggleKey = id == QLatin1String("godot") ? QStringLiteral("Shift+F") : QStringLiteral("Shift+`");
			plan.toggleModifiers = Qt::ShiftModifier; plan.addModifiers = Qt::NoModifier;
			key(controls, "map.viewTop", {QStringLiteral("Num+7")});
			key(controls, "map.viewFront", {QStringLiteral("Num+1")});
			key(controls, "map.viewSide", {QStringLiteral("Num+3")});
			key(controls, "map.toggleSnap", {id == QLatin1String("godot") ? QStringLiteral("Y") : QStringLiteral("Shift+Tab")});
		}
		if (id == QLatin1String("unreal")) {
			key(controls, "map.duplicateSelection", {QStringLiteral("Ctrl+W")});
			key(controls, "map.hideSelection", {QStringLiteral("H")});
			key(controls, "map.showAll", {QStringLiteral("Ctrl+H")});
		}
		if (id == QLatin1String("blender")) {
			key(controls, "map.duplicateSelection", {QStringLiteral("Shift+D")});
			key(controls, "map.frameSelection", {QStringLiteral("Num+.")});
			key(controls, "map.selectAll", {QStringLiteral("A")});
			key(controls, "map.selectNone", {QStringLiteral("Alt+A")});
			key(controls, "map.hideSelection", {QStringLiteral("H")});
			key(controls, "map.showAll", {QStringLiteral("Alt+H")});
		}
		return controls;
	}
	return vibeStudioLevelControls();
}

CameraMaterialGesture cameraMaterialGesture(const CameraViewControls& controls, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
	if (button == Qt::NoButton) { return CameraMaterialGesture::None; }
	if (button == controls.materialSampleButton && modifiers == controls.materialSampleModifiers) { return CameraMaterialGesture::Sample; }
	if (button == controls.materialPaintButton && modifiers == controls.materialPaintModifiers) { return CameraMaterialGesture::Paint; }
	if (button == controls.surfacePasteFaceButton && modifiers == controls.surfacePasteFaceModifiers) { return CameraMaterialGesture::PasteFace; }
	if (button == controls.surfacePasteBrushButton && modifiers == controls.surfacePasteBrushModifiers) { return CameraMaterialGesture::PasteBrush; }
	if (button == controls.surfaceWrapFaceButton && modifiers == controls.surfaceWrapFaceModifiers) { return CameraMaterialGesture::WrapFace; }
	if (button == controls.surfaceValuesButton && modifiers == controls.surfaceValuesModifiers) { return CameraMaterialGesture::ValuesSelection; }
	if (button == controls.surfaceValuesOnlyButton && modifiers == controls.surfaceValuesOnlyModifiers) { return CameraMaterialGesture::ValuesSelectionOnly; }
	if (button == controls.surfaceWrapOnlyButton && modifiers == controls.surfaceWrapOnlyModifiers) { return CameraMaterialGesture::WrapFaceOnly; }
	if (button == controls.surfaceProjectButton && modifiers == controls.surfaceProjectModifiers) { return CameraMaterialGesture::ProjectSelection; }
	if (button == controls.surfaceProjectOnlyButton && modifiers == controls.surfaceProjectOnlyModifiers) { return CameraMaterialGesture::ProjectSelectionOnly; }
	return CameraMaterialGesture::None;
}

bool cameraMaterialGestureConflicts(const CameraViewControls& controls, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
	return button != Qt::NoButton && ((button != Qt::RightButton && button != Qt::MiddleButton)
		|| cameraNavigationDrag(controls, button, modifiers) != CameraNavigationDrag::None
		|| (controls.perspective && controls.lookClickToggles && button == controls.lookButton));
}

CameraNavigationDrag cameraNavigationDrag(const CameraViewControls& controls, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
	if (button == Qt::NoButton) { return CameraNavigationDrag::None; }
	const auto keys = modifiers & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
	if (controls.perspective && controls.driveButton == button && controls.driveModifiers == keys) { return CameraNavigationDrag::Drive; }
	// Specific gestures first. This also makes an unmodified middle orbit
	// distinct from Shift+middle pan (Blender/Godot).
	if (controls.orbitButton == button && controls.orbitModifiers == keys) { return CameraNavigationDrag::Orbit; }
	// An explicit modified pan (BSP's Shift+middle) wins over the look
	// gesture's optional speed modifiers. Exact look/pan collisions are invalid.
	if (controls.panButtons.contains(button) && controls.panModifiers == keys) { return CameraNavigationDrag::Pan; }
	if (controls.perspective && controls.lookButton == button && !controls.lookClickToggles
		&& (keys & ~((controls.fastModifiers | controls.slowModifiers) & ~controls.lookModifiers)) == controls.lookModifiers) {
		return CameraNavigationDrag::Look;
	}
	if (button == Qt::LeftButton && controls.leftPanModifiers != Qt::NoModifier && (keys & controls.leftPanModifiers) != Qt::NoModifier) {
		return CameraNavigationDrag::Pan;
	}
	return CameraNavigationDrag::None;
}

bool cameraFlyKeysActive(const CameraViewControls& controls, bool freeLook, Qt::MouseButton pressedButton, Qt::KeyboardModifiers pressModifiers)
{
	return controls.perspective && (!controls.flyNeedsLook || freeLook
		|| cameraNavigationDrag(controls, pressedButton, pressModifiers) == CameraNavigationDrag::Look);
}

CameraFreeLookMotion cameraFreeLookMotion(const CameraViewControls& controls, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers)
{
	if (controls.lookPanUsesButtons) {
		if (buttons.testFlag(Qt::RightButton)) { return buttons.testFlag(Qt::LeftButton) ? CameraFreeLookMotion::Dolly : CameraFreeLookMotion::Pan; }
	} else {
		if (modifiers.testFlag(Qt::ControlModifier)) { return CameraFreeLookMotion::Pan; }
		if (modifiers.testFlag(Qt::ShiftModifier)) { return CameraFreeLookMotion::Dolly; }
	}
	return CameraFreeLookMotion::Look;
}

QPoint planArrowPanDirection(const PlanViewControls& controls, int key, Qt::KeyboardModifiers modifiers)
{
	if (controls.panArrowModifiers == Qt::NoModifier || modifiers != controls.panArrowModifiers) { return {}; }
	switch (key) {
	case Qt::Key_Left: return {-1, 0};
	case Qt::Key_Right: return {1, 0};
	case Qt::Key_Up: return {0, 1};
	case Qt::Key_Down: return {0, -1};
	default: return {};
	}
}

QPointF planCameraStep(const PlanViewControls& controls, int key, Qt::KeyboardModifiers modifiers, double gridUnits)
{
	if (!controls.arrowsDriveCamera || ((modifiers & ~Qt::KeypadModifier) != Qt::NoModifier
		&& (controls.fixedCameraSteps || (modifiers & ~Qt::KeypadModifier) != Qt::ShiftModifier))) { return {}; }
	if (!std::isfinite(gridUnits) || gridUnits <= 0) { gridUnits = 16.0; }
	const double speed = modifiers.testFlag(Qt::ShiftModifier) ? 4.0 : 1.0;
	const double move = controls.fixedCameraSteps ? 32.0 : std::max(gridUnits, 16.0) * speed;
	const double turn = controls.fixedCameraSteps ? 22.5 : 15.0 * speed;
	switch (key) {
	case Qt::Key_Up: return {move, 0};
	case Qt::Key_Down: return {-move, 0};
	case Qt::Key_Left: return {0, turn};
	case Qt::Key_Right: return {0, -turn};
	default: return {};
	}
}

} // namespace vibestudio
