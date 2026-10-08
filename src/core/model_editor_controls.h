#pragma once

// How the modeller answers the mouse and keyboard, and how it lays itself
// out, under a modeller profile.
//
// The modeller follows Blender's layout and navigation by default, and offers
// profiles that make Blender, 3ds Max and MilkShape 3D users feel at home:
// which button orbits, pans and zooms; which keys add to or take from the
// selection; whether G/R/S start a transform at once (Blender) or W/E/R and
// F1-F4 pick a lasting tool (3ds Max, MilkShape 3D); one view or four; which
// sidebar tabs sit where and what they are called; and which keys every
// modeller command takes.
//
// Profiles are data: the editor reads these values and nothing else, so a
// new profile is a new set of values rather than new editor code, and the
// CLI reports exactly what the editor does. Users customise a profile with
// overrides (keys, gestures, layout) saved per profile, and can export and
// import a profile with its overrides as a small JSON file.
//
// The level editor's profiles live in core/editor_profiles.h and
// core/level_editor_controls.h; the modeller reuses their camera routing
// (CameraViewControls) so both 3D views navigate alike under one family.

#include "core/level_editor_controls.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <Qt>

namespace vibestudio {

// How the modeller arranges its views.
enum class ModelViewLayout {
	// One view (Blender's default; Ctrl+Alt+Q toggles four).
	Single,
	// Four views sharing the document and selection: three orthographic panes
	// and a 3D view (3ds Max, MilkShape 3D).
	FourViews,
};

// What a view pane shows.
enum class ModelPaneView {
	Perspective,
	Top,
	Bottom,
	Front,
	Back,
	Left,
	Right,
};

// How transforms start.
enum class ModelTransformStyle {
	// G, R and S start a transform that follows the pointer at once; X, Y and
	// Z constrain it, digits type a value, a click or Enter confirms (Blender).
	Modal,
	// Keys pick a lasting Move, Rotate or Scale tool; dragging the selection
	// or its gizmo applies it (3ds Max, MilkShape 3D).
	ToolMode,
};

struct ModelNavigationControls {
	// The 3D view's orbit, pan, look, fly and wheel routing, as the level
	// editor's camera reads it.
	CameraViewControls view3D;
	// The same for the orthographic panes. A pane that is not allowed to
	// orbit has NoButton as its orbit button.
	CameraViewControls orthographic;
	// A drag of this button with these keys zooms either kind of view (up
	// zooms in): Blender Ctrl+middle, 3ds Max Ctrl+Alt+middle, MilkShape
	// Shift+left. NoButton disables it.
	Qt::MouseButton zoomButton = Qt::NoButton;
	Qt::KeyboardModifiers zoomModifiers = Qt::NoModifier;
	// Wheel up zooms out in the 3D view (MilkShape).
	bool invertWheel3D = false;
	// Orbiting an orthographic pane turns it into a user view; otherwise
	// orthographic panes never orbit (MilkShape).
	bool orbitLeavesOrthographic = true;
	// Alt with the left button stands in for the middle button, so a two-
	// button mouse or a pen can orbit (Blender's Emulate 3 Button Mouse).
	bool emulateMiddleButton = false;
};

struct ModelSelectionControls {
	// Held on a click or region, these add to the selection; with
	// `extendToggles` a click on a selected element takes it out instead
	// (Blender's Shift). NoModifier disables extending.
	Qt::KeyboardModifiers extendModifiers = Qt::ShiftModifier;
	bool extendToggles = true;
	// Held on a click or region, these take elements out of the selection.
	Qt::KeyboardModifiers subtractModifiers = Qt::ControlModifier;
	// A drag of this button with these keys takes what it encloses out of
	// the selection (MilkShape's Shift+right drag). NoButton disables it.
	Qt::MouseButton subtractDragButton = Qt::NoButton;
	Qt::KeyboardModifiers subtractDragModifiers = Qt::NoModifier;
	// A left drag over empty space draws a selection rectangle.
	bool emptyDragBoxSelects = true;
	// Modifiers on a left click that pick an edge loop, an edge ring or the
	// shortest path to the clicked element. NoModifier disables a role.
	Qt::KeyboardModifiers loopModifiers = Qt::AltModifier;
	Qt::KeyboardModifiers ringModifiers = Qt::ControlModifier | Qt::AltModifier;
	Qt::KeyboardModifiers pathModifiers = Qt::ControlModifier;
	// A double-click on an edge selects its loop (3ds Max).
	bool doubleClickSelectsLoop = false;
	// A drag of this button with these keys draws a lasso.
	Qt::MouseButton lassoButton = Qt::RightButton;
	Qt::KeyboardModifiers lassoModifiers = Qt::ControlModifier;
	// A click of this button with these keys places the 3D cursor.
	Qt::MouseButton cursorButton = Qt::RightButton;
	Qt::KeyboardModifiers cursorModifiers = Qt::ShiftModifier;
	// A plain right click opens the context menu for the current mode
	// (Blender's context menu, 3ds Max's quad menu).
	bool rightClickMenu = true;
};

struct ModelTransformControls {
	ModelTransformStyle style = ModelTransformStyle::Modal;
	// With a lasting tool, a left drag that starts on the selection applies
	// the tool, not only a drag on the gizmo.
	bool dragSelectionTransforms = false;
	// With the Move tool, a drag with these keys duplicates the selection
	// first (3ds Max's Shift+drag clone). NoModifier disables it.
	Qt::KeyboardModifiers duplicateDragModifiers = Qt::NoModifier;
	// During a transform these keys give fine control and snapping.
	Qt::KeyboardModifiers precisionModifiers = Qt::ShiftModifier;
	Qt::KeyboardModifiers snapModifiers = Qt::ControlModifier;
};

struct ModelLayoutControls {
	ModelViewLayout layout = ModelViewLayout::Single;
	// What each pane of the four-view layout shows, in reading order (top
	// left, top right, bottom left, bottom right). Four entries.
	QVector<ModelPaneView> panes{ModelPaneView::Top, ModelPaneView::Front, ModelPaneView::Right, ModelPaneView::Perspective};
	// The single view starts in perspective rather than the orthographic
	// orbit view.
	bool startInPerspective = false;
	// The family of editors the sidebars and terms follow (see
	// core/model_sidebar.h): "studio", "blender", "max" or "milkshape".
	QString family = QStringLiteral("studio");
	bool timelineVisible = true;
	bool toolShelfVisible = true;
};

// Keys a profile gives a modeller command, replacing its default keys. An
// empty list takes the command's keys away.
struct ModelEditorKeyBinding {
	QString commandId;
	QStringList keys;
};

struct ModelEditorControls {
	QString profileId;
	ModelNavigationControls navigation;
	ModelSelectionControls selection;
	ModelTransformControls transform;
	ModelLayoutControls layout;
	// When true, commands the profile does not bind keep their default
	// (studio) keys; when false they have none, so a profile's keymap is
	// exactly what it lists (3ds Max, MilkShape 3D).
	bool inheritDefaultKeys = true;
	QVector<ModelEditorKeyBinding> keys;
};

struct ModelEditorProfile {
	QString id;
	// Translated.
	QString displayName;
	// The editor's own name, untranslated ("Blender", "3ds Max").
	QString shortName;
	QString description;
	// Where VibeStudio differs from the reference editor, translated.
	QStringList adaptations;
	QString referenceUrl;
	QStringList aliases;
	ModelEditorControls controls;
};

// A modeller command: one of the editor's actions, with the keys the studio
// profile gives it.
struct ModelEditorCommand {
	// The editor action's object name ("meshMove").
	QString id;
	// "select", "transform", "mesh", "view", "uv", "animation", "edit",
	// "file" or "tools".
	QString category;
	// Translated.
	QString title;
	QStringList defaultKeys;
	// Escape-style keys that must stay put.
	bool userRemappable = true;
};

struct ModelEditorControlRow {
	QString section;
	QString view;
	QString action;
	QString gesture;
};

[[nodiscard]] QString defaultModelEditorProfileId();
[[nodiscard]] QString normalizedModelEditorProfileId(const QString& id);
[[nodiscard]] QVector<ModelEditorProfile> modelEditorProfiles();
[[nodiscard]] QStringList modelEditorProfileIds();
[[nodiscard]] bool modelEditorProfileForId(const QString& id, ModelEditorProfile* out = nullptr);
// The profile's controls, or the studio ones for an unknown id.
[[nodiscard]] ModelEditorControls modelEditorControlsForProfile(const QString& id);
// The modeller profile that suits a level editor profile, so a user who picked
// "Blender Style" for levels starts the modeller as a Blender user too.
[[nodiscard]] QString modelEditorProfileForLevelProfile(const QString& levelProfileId);

[[nodiscard]] QVector<ModelEditorCommand> modelEditorCommands();
[[nodiscard]] bool modelEditorCommandForId(const QString& id, ModelEditorCommand* out = nullptr);
// The keys a command takes under these controls, in portable text.
[[nodiscard]] QStringList modelEditorCommandKeys(const ModelEditorControls& controls, const QString& commandId);
// Every command with its keys under these controls.
[[nodiscard]] QVector<ModelEditorKeyBinding> modelEditorEffectiveKeys(const ModelEditorControls& controls);

[[nodiscard]] QString modelViewLayoutId(ModelViewLayout layout);
bool modelViewLayoutForId(const QString& id, ModelViewLayout* out);
[[nodiscard]] QString modelPaneViewId(ModelPaneView view);
bool modelPaneViewForId(const QString& id, ModelPaneView* out);
// Translated.
[[nodiscard]] QString modelPaneViewDisplayName(ModelPaneView view);
[[nodiscard]] QString modelTransformStyleId(ModelTransformStyle style);
bool modelTransformStyleForId(const QString& id, ModelTransformStyle* out);

// The whole scheme as reference rows: navigation, selection, transforms,
// layout, then every key.
[[nodiscard]] QVector<ModelEditorControlRow> modelEditorControlRows(const ModelEditorControls& controls);
// Problems with a scheme: two gestures on one button and modifier set, a key
// bound to two commands, an unknown command. Empty when it holds together.
[[nodiscard]] QStringList modelEditorControlProblems(const ModelEditorControls& controls);

// --- Customisation --------------------------------------------------------
//
// Overrides are a JSON object with any of these members, each optional:
//   "keys":       { "<commandId>": ["Ctrl+D", ...], ... }   ([] removes keys)
//   "navigation": { "orbit": "Alt+Middle", "pan": "Middle", "zoom": "Ctrl+Alt+Middle",
//                   "invertWheel3D": false, "orbitLeavesOrthographic": true,
//                   "emulateMiddleButton": false }
//   "selection":  { "extend": "Shift", "extendToggles": true, "subtract": "Ctrl",
//                   "subtractDrag": "Shift+Right", "loop": "Alt", "ring": "Ctrl+Alt",
//                   "path": "Ctrl", "lasso": "Ctrl+Right", "cursor": "Shift+Right",
//                   "doubleClickSelectsLoop": false, "rightClickMenu": true,
//                   "emptyDragBoxSelects": true }
//   "transform":  { "style": "modal" | "tool", "dragSelectionTransforms": false,
//                   "duplicateDrag": "Shift" }
//   "layout":     { "layout": "single" | "four-views", "panes": ["top", "front", "right", "perspective"],
//                   "startInPerspective": false, "timeline": true, "toolShelf": true }
// Gestures are "<modifiers>+<Button>" with Left, Middle or Right ("None"
// disables); modifier sets are "Shift", "Ctrl+Alt" or "None".

[[nodiscard]] QJsonObject modelEditorControlsJson(const ModelEditorControls& controls);
// Applies overrides on top of `base`. Unknown members and malformed values
// are skipped with a warning; a non-object fails.
bool applyModelEditorControlOverrides(const ModelEditorControls& base, const QJsonObject& overrides, ModelEditorControls* out,
	QStringList* warnings = nullptr, QString* error = nullptr);
// The overrides that turn `base` into `edited` (only what differs).
[[nodiscard]] QJsonObject modelEditorControlOverrides(const ModelEditorControls& base, const ModelEditorControls& edited);

// A shareable profile file: {"format": "vibestudio.modeller-controls",
// "version": 1, "profile": "<id>", "overrides": {...}}.
[[nodiscard]] QByteArray modelEditorControlsFile(const QString& profileId, const QJsonObject& overrides);
bool parseModelEditorControlsFile(const QByteArray& bytes, QString* profileId, QJsonObject* overrides, QString* error = nullptr);

// Gesture text helpers, in the portable form used by the overrides.
[[nodiscard]] QString modelGestureText(Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
bool parseModelGesture(const QString& text, Qt::MouseButton* button, Qt::KeyboardModifiers* modifiers);
[[nodiscard]] QString modelModifierText(Qt::KeyboardModifiers modifiers);
bool parseModelModifiers(const QString& text, Qt::KeyboardModifiers* modifiers);

} // namespace vibestudio
