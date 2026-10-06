#pragma once
#include "core/level_editor_controls.h"
#include <QHash>
#include <QPointF>

namespace vibestudio {
struct EditorProfileDescriptor;
struct CameraKeyMotion {
	double forward = 0.0;
	double right = 0.0;
	double up = 0.0;
	double turn = 0.0;
	double pitch = 0.0;
};

// Position steering in normalized view coordinates (-1..1; Y points down).
// Returns forward/turn rates in units/degrees per second, on the ground plane.
CameraKeyMotion cameraPointerDriveMotion(const QPointF& normalizedPosition);

// Portable keys shared by preferences, diagnostics and the viewport. "none"
// disables a binding; direction keys are unmodified, toggles may use modifiers.
bool normalizeCameraKey(const QString& input, bool toggle, QString* normalized, QString* error = nullptr);
QStringList cameraKeyProblems(const CameraViewControls& controls);
QHash<int, CameraKeyMotion> cameraKeyMotions(const CameraViewControls& controls, bool looking,
	Qt::MouseButton pressedButton = Qt::NoButton, Qt::KeyboardModifiers pressModifiers = Qt::NoModifier,
	Qt::KeyboardModifiers modifiers = Qt::NoModifier);
// Fixed steps apply only to drive keys while idle. An active fly binding
// keeps its continuous motion when both sets use the same key.
bool cameraKeyUsesFixedStep(const CameraViewControls& controls, int key, bool looking,
	Qt::MouseButton pressedButton = Qt::NoButton, Qt::KeyboardModifiers pressModifiers = Qt::NoModifier,
	Qt::KeyboardModifiers modifiers = Qt::NoModifier);
bool cameraMotionModifiersAccepted(const CameraViewControls& controls, Qt::KeyboardModifiers modifiers,
	Qt::MouseButton pressedButton = Qt::NoButton, Qt::KeyboardModifiers pressModifiers = Qt::NoModifier, bool looking = false);
bool cameraLookToggleMatches(const CameraViewControls& controls, int key, Qt::KeyboardModifiers modifiers);
bool navigationHoldKeyMatches(const QString& binding, int key, Qt::KeyboardModifiers modifiers);
// Reports camera and temporary-plan-navigation overlaps with command bindings.
// Existing built-in navigation overlaps are omitted unless the command was
// explicitly rebound by the user. Command registration/scopes stay unchanged.
QStringList levelCameraShortcutWarnings(const EditorProfileDescriptor& profile, const LevelEditorControls& controls,
	const QHash<QString, QStringList>& userShortcuts = {});
} // namespace vibestudio
