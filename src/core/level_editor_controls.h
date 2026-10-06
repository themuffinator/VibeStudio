#pragma once

// How the level editor's views answer the mouse and keyboard under an editor
// profile: which button pans, what a drag over empty space does, how the 3D
// camera moves and which keys fly it, how the Levels page lays its views out,
// and which keys the map commands take.
//
// The views read these values and nothing else, so an editor profile is data:
// a new profile is a new set of values, not new view code. The TrenchBroom,
// NetRadiant Custom, and GtkRadiant values follow those editors' own defaults,
// read from their sources (see docs/CREDITS.md and docs/EDITOR_PROFILES.md).

#include <QString>
#include <QStringList>
#include <QPoint>
#include <QVector>
#include <Qt>

namespace vibestudio {

// How the Levels page arranges its views.
enum class LevelViewLayout {
	// One view, the 2D plan first; the 3D view is a toggle away.
	Single2D,
	// One view, the 3D camera first; Cycle View steps 3D, Top, Front, Side
	// (TrenchBroom's one-pane layout).
	Single3D,
	// The 3D camera and a 2D plan side by side (Radiant's regular layout).
	CameraAndPlan,
	// Camera, top, front and side share one document and selection.
	FourViews,
};

// What a left drag that starts over empty space in a 2D view does.
enum class PlanEmptyDrag {
	// A rubber band selects what it encloses.
	BoxSelect,
	// A new box brush the size of the drag.
	DrawBrush,
	// The sides of the selection's box that face the press point follow the
	// pointer (Radiant's face dragging, on the selection's box).
	ResizeSelection,
};

struct PlanViewControls {
	// Buttons that pan by dragging. The right button pans only once the drag
	// moves, so a right click still opens the context menu.
	QVector<Qt::MouseButton> panButtons {Qt::MiddleButton};
	// A drag of this button, with these keys held, zooms: up zooms in.
	Qt::MouseButton zoomDragButton = Qt::NoButton;
	Qt::KeyboardModifiers zoomDragModifiers = Qt::NoModifier;
	// Held on a click, these take one object out of the selection or put it
	// in; `addModifiers` only ever puts it in. NoModifier disables a role.
	Qt::KeyboardModifiers toggleModifiers = Qt::ControlModifier;
	Qt::KeyboardModifiers addModifiers = Qt::ShiftModifier;
	// With these held a left drag draws a rubber band wherever it starts and
	// adds what it encloses (Radiant's area selection). NoModifier leaves the
	// rubber band to `emptyDrag`.
	Qt::KeyboardModifiers bandModifiers = Qt::NoModifier;
	PlanEmptyDrag emptyDrag = PlanEmptyDrag::BoxSelect;
	PlanEmptyDrag emptyDragWithSelection = PlanEmptyDrag::BoxSelect;
	// A plain click on objects stacked under the pointer steps through them,
	// one per click (Radiant's tunnel selection).
	bool clickCyclesStack = false;
	// A plain click selects what is under the pointer. GtkRadiant leaves
	// selecting to Shift+click: there a plain press only moves, resizes, or
	// draws, and a plain click changes nothing.
	bool plainClickSelects = true;
	// With these held, a click steps down through the objects stacked under
	// the pointer (GtkRadiant's Shift+Alt drill). NoModifier: only a plain
	// click cycles, with `clickCyclesStack`.
	Qt::KeyboardModifiers cycleModifiers = Qt::NoModifier;
	// The middle button aims the 3D camera at the point, and with Ctrl moves
	// the camera there (Radiant).
	bool middleButtonDrivesCamera = false;
	// Plain arrow keys drive the 3D camera from the 2D view and Alt+arrows
	// nudge the selection (Radiant); otherwise plain arrows nudge.
	bool arrowsDriveCamera = false;
	// Fixed 32-unit / 22.5-degree camera steps instead of grid-relative steps.
	bool fixedCameraSteps = false;
	// While drawing a brush: keep the box square in the view, or make it a
	// cube.
	Qt::KeyboardModifiers squareModifiers = Qt::ShiftModifier;
	Qt::KeyboardModifiers cubeModifiers = Qt::ControlModifier;
	// Temporary pan without a mouse button, released on key-up or focus loss.
	QString panHoldKey;
	// These modifiers with arrows pan by a quarter of the view (Sledge).
	Qt::KeyboardModifiers panArrowModifiers = Qt::NoModifier;
};

// What the mouse wheel does over the 3D view.
enum class CameraWheel {
	// Scale about the pointer: the orthographic orbit view's zoom.
	Zoom,
	// Move the camera along its view.
	Dolly,
	// Move the camera toward the point under the pointer (Radiant).
	DollyToPointer,
};

// A held-key set for flying the camera. Each entry is one key in Qt's
// portable text ("W", "Up"); an empty entry leaves that direction unbound.
struct CameraFlyKeys {
	QString forward;
	QString back;
	QString left;
	QString right;
	QString up;
	QString down;
	QString turnLeft;
	QString turnRight;
	QString pitchUp = {};
	QString pitchDown = {};
	[[nodiscard]] QStringList all() const;
	[[nodiscard]] bool isEmpty() const;
};

struct CameraViewControls {
	// A first-person perspective camera, as TrenchBroom and Radiant have;
	// otherwise the orthographic view orbits the map's centre.
	bool perspective = false;
	double fieldOfViewDegrees = 90.0;
	Qt::MouseButton orbitButton = Qt::LeftButton;
	Qt::KeyboardModifiers orbitModifiers = Qt::NoModifier;
	Qt::MouseButton lookButton = Qt::NoButton;
	Qt::KeyboardModifiers lookModifiers = Qt::NoModifier;
	// Hold to steer from the pointer's position relative to the view centre.
	Qt::MouseButton driveButton = Qt::NoButton;
	Qt::KeyboardModifiers driveModifiers = Qt::NoModifier;
	// A click of the look button that does not drag keeps looking, the
	// pointer turning the camera, until the next click or Escape (Radiant's
	// free look). Holding the button and dragging looks either way.
	bool lookClickToggles = false;
	// Optional viewport-local free-look toggle (Hammer's Z, Godot's Shift+F).
	QString lookToggleKey;
	QString lookHoldKey;
	// In free look, right mouse pans and left+right dollies (Sledge).
	// Otherwise Ctrl pans and Shift dollies (Radiant-style adaptation).
	bool lookPanUsesButtons = false;
	QVector<Qt::MouseButton> panButtons {Qt::MiddleButton};
	Qt::KeyboardModifiers panModifiers = Qt::NoModifier;
	// Left drags with any of these held pan too (the orbit view's Shift or
	// Ctrl). NoModifier: a left drag never pans.
	Qt::KeyboardModifiers leftPanModifiers = Qt::ShiftModifier | Qt::ControlModifier;
	CameraWheel wheel = CameraWheel::Zoom;
	// With these held the wheel changes the field of view instead.
	Qt::KeyboardModifiers fieldOfViewWheelModifiers = Qt::NoModifier;
	// Left clicks: replace the selection with the object under the pointer;
	// these held toggle it, or pick the face under the pointer. The orbit
	// view's Shift and Ctrl pan, so there a click only replaces.
	Qt::KeyboardModifiers toggleModifiers = Qt::NoModifier;
	Qt::KeyboardModifiers faceModifiers = Qt::NoModifier;
	// One-shot material actions in the level camera, without changing tools.
	// Exact modifiers; NoButton disables an action. Sample captures a brush's
	// mapping too; Paint applies only its material, Paste/Wrap include UVs.
	Qt::MouseButton materialSampleButton = Qt::NoButton;
	Qt::KeyboardModifiers materialSampleModifiers = Qt::NoModifier;
	Qt::MouseButton materialPaintButton = Qt::NoButton;
	Qt::KeyboardModifiers materialPaintModifiers = Qt::NoModifier;
	Qt::MouseButton surfacePasteFaceButton = Qt::NoButton;
	Qt::KeyboardModifiers surfacePasteFaceModifiers = Qt::NoModifier;
	Qt::MouseButton surfacePasteBrushButton = Qt::NoButton;
	Qt::KeyboardModifiers surfacePasteBrushModifiers = Qt::NoModifier;
	Qt::MouseButton surfaceWrapFaceButton = Qt::NoButton;
	Qt::KeyboardModifiers surfaceWrapFaceModifiers = Qt::NoModifier;
	Qt::MouseButton surfaceValuesButton = Qt::NoButton;
	Qt::KeyboardModifiers surfaceValuesModifiers = Qt::NoModifier;
	Qt::MouseButton surfaceValuesOnlyButton = Qt::NoButton;
	Qt::KeyboardModifiers surfaceValuesOnlyModifiers = Qt::NoModifier;
	Qt::MouseButton surfaceWrapOnlyButton = Qt::NoButton;
	Qt::KeyboardModifiers surfaceWrapOnlyModifiers = Qt::NoModifier;
	Qt::MouseButton surfaceProjectButton = Qt::NoButton;
	Qt::KeyboardModifiers surfaceProjectModifiers = Qt::NoModifier;
	Qt::MouseButton surfaceProjectOnlyButton = Qt::NoButton;
	Qt::KeyboardModifiers surfaceProjectOnlyModifiers = Qt::NoModifier;
	// A plain left click replaces the selection; GtkRadiant's camera, like
	// its 2D views, selects only with the toggle keys.
	bool plainClickSelects = true;
	// A left drag that starts on the selection moves it across the ground,
	// and with Alt up and down (TrenchBroom, Radiant). The orbit view's left
	// drag orbits instead.
	bool dragMovesSelection = false;
	// Keys held while the 3D view has focus fly the camera; with
	// `flyNeedsLook` only while looking.
	CameraFlyKeys flyKeys;
	bool flyNeedsLook = false;
	// Keys that drive the camera when not looking (Radiant's arrows).
	CameraFlyKeys driveKeys;
	// Drive keys step on each press/repeat instead of using held-key flight.
	bool discreteDriveKeys = false;
	bool turnKeysStrafeInLook = true;
	// Turn/pitch keys translate horizontally/vertically with these modifiers.
	Qt::KeyboardModifiers turnKeysPanModifiers = Qt::NoModifier;
	bool speedModifiersRequireLook = false;
	Qt::KeyboardModifiers fastModifiers = Qt::ShiftModifier;
	Qt::KeyboardModifiers slowModifiers = Qt::AltModifier;
};

// Keys an editor profile gives a shell command, replacing its own. An empty
// list takes the command's keys away, where the profile needs the key for
// something else (TrenchBroom's W flies the camera).
struct LevelEditorKeyBinding {
	QString commandId;
	QStringList keys;
};

struct LevelEditorControls {
	LevelViewLayout layout = LevelViewLayout::Single2D;
	int defaultGridUnits = 64;
	PlanViewControls plan;
	CameraViewControls camera;
	QVector<LevelEditorKeyBinding> keys;
};

// One row of a controls reference: where, what, and how. `section` is a
// stable id for the group ("layout", "plan", "camera", or "keys"); `view` is
// its translated name.
struct LevelEditorControlRow {
	QString view;
	QString action;
	QString gesture;
	QString section;
};

// The controls of each profile that has them. A profile without its own
// controls uses the VibeStudio ones.
[[nodiscard]] LevelEditorControls vibeStudioLevelControls();
[[nodiscard]] LevelEditorControls trenchBroomLevelControls();
[[nodiscard]] LevelEditorControls netRadiantCustomLevelControls();
[[nodiscard]] LevelEditorControls gtkRadiantLevelControls();
// Familiarity schemes with explicit adaptation notes on their descriptors.
[[nodiscard]] LevelEditorControls familiarLevelControls(const QString& profileId);

enum class CameraNavigationDrag { None, Orbit, Look, Pan, Drive };
enum class CameraMaterialGesture { None, Sample, Paint, PasteFace, PasteBrush, WrapFace, ValuesSelection, ValuesSelectionOnly, WrapFaceOnly, ProjectSelection, ProjectSelectionOnly };
[[nodiscard]] CameraMaterialGesture cameraMaterialGesture(const CameraViewControls& controls,
	Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
[[nodiscard]] bool cameraMaterialGestureConflicts(const CameraViewControls& controls,
	Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
enum class CameraFreeLookMotion { Look, Pan, Dolly };
[[nodiscard]] CameraFreeLookMotion cameraFreeLookMotion(const CameraViewControls& controls,
	Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers);
[[nodiscard]] QPoint planArrowPanDirection(const PlanViewControls& controls, int key, Qt::KeyboardModifiers modifiers);
// Forward units and yaw degrees for a plan's camera arrow command.
[[nodiscard]] QPointF planCameraStep(const PlanViewControls& controls, int key, Qt::KeyboardModifiers modifiers, double gridUnits);
// Shared, input-independent routing used by the viewport and regression tests.
[[nodiscard]] CameraNavigationDrag cameraNavigationDrag(const CameraViewControls& controls,
	Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
[[nodiscard]] bool cameraFlyKeysActive(const CameraViewControls& controls, bool freeLook,
	Qt::MouseButton pressedButton = Qt::NoButton, Qt::KeyboardModifiers pressModifiers = Qt::NoModifier);

[[nodiscard]] QString levelViewLayoutId(LevelViewLayout layout);
bool levelViewLayoutForId(const QString& id, LevelViewLayout* layout);
[[nodiscard]] QString levelViewLayoutDisplayName(LevelViewLayout layout);
[[nodiscard]] QString planEmptyDragId(PlanEmptyDrag drag);
[[nodiscard]] QString cameraWheelId(CameraWheel wheel);

// Readable names for mouse gestures, "Alt+Right drag" and the like, in the
// same portable form as key sequences.
[[nodiscard]] QString mouseGestureText(Qt::MouseButton button, Qt::KeyboardModifiers modifiers, const QString& what);

// The whole scheme as reference rows: the 2D view, the 3D view, then the
// keys, each gesture as a user would say it.
[[nodiscard]] QVector<LevelEditorControlRow> levelEditorControlRows(const LevelEditorControls& controls);

// Problems with a scheme: two gestures on one button and modifier set, a key
// bound twice, a fly key a command also takes. Empty when it holds together.
[[nodiscard]] QStringList levelEditorControlProblems(const LevelEditorControls& controls);

} // namespace vibestudio
