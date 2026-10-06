#include "core/level_camera_keys.h"
#include "core/editor_profiles.h"
#include "core/studio_semantics.h"
#include <QCoreApplication>
#include <QKeySequence>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace vibestudio {
namespace {
const QVector<QString CameraFlyKeys::*> directions {&CameraFlyKeys::forward, &CameraFlyKeys::back, &CameraFlyKeys::left,
	&CameraFlyKeys::right, &CameraFlyKeys::up, &CameraFlyKeys::down, &CameraFlyKeys::turnLeft, &CameraFlyKeys::turnRight,
	&CameraFlyKeys::pitchUp, &CameraFlyKeys::pitchDown};
bool fail(QString* error, const QString& text) { if (error) { *error = text; } return false; }
// Match camelCase profile/action ids with the registry's dashed ids, as the
// Controls reference does when resolving their labels.
QString commandKey(QString id) { return id.trimmed().toLower().remove(QLatin1Char('-')).remove(QLatin1Char('_')); }
bool claims(const CameraViewControls& controls, const QKeyCombination& key, bool looking)
{
	return cameraLookToggleMatches(controls, key.key(), key.keyboardModifiers())
		|| navigationHoldKeyMatches(controls.lookHoldKey, key.key(), key.keyboardModifiers())
		|| cameraKeyMotions(controls, looking, Qt::NoButton, Qt::NoModifier, key.keyboardModifiers()).contains(key.key())
		|| (looking && cameraKeyMotions(controls, false, controls.lookButton, controls.lookModifiers, key.keyboardModifiers()).contains(key.key()));
}
}

CameraKeyMotion cameraPointerDriveMotion(const QPointF& position)
{
	// Q3Radiant CamWnd.cpp::Cam_MouseControl behavior, id Software 1999-2005,
	// GPL-2.0-or-later, dbe4ddb10315479fc00086f08e25d968b4b43c49.
	// Independent normalized Qt implementation; see docs/CREDITS.md.
	if (!std::isfinite(position.x()) || !std::isfinite(position.y())) { return {}; }
	const double vertical = std::clamp(-position.y(), -1.0, 1.0);
	const double steering = std::clamp(position.x(), -1.0, 1.0) * (1.0 - std::abs(vertical));
	const double turn = std::copysign(std::max(0.0, std::abs(steering) - 0.1), -steering);
	return {vertical * 400.0, 0.0, 0.0, turn * 300.0};
}

bool normalizeCameraKey(const QString& input, bool toggle, QString* normalized, QString* error)
{
	if (error) { error->clear(); }
	if (input.trimmed() == QLatin1String("none")) { if (normalized) { *normalized = QStringLiteral("none"); } return true; }
	const auto sequence = QKeySequence::fromString(input.trimmed(), QKeySequence::PortableText);
	if (input.size() > 64 || sequence.count() != 1 || sequence[0].key() == Qt::Key_unknown || sequence[0].key() == 0
		|| (!toggle && sequence[0].keyboardModifiers() != Qt::NoModifier)) {
		return fail(error, toggle ? QCoreApplication::translate("LevelCameraKeys", "Use one key combination for mouse look, or none to disable it.")
			: QCoreApplication::translate("LevelCameraKeys", "Use one unmodified movement key, or none to disable it."));
	}
	const auto key = sequence[0].key();
	if (key == Qt::Key_Escape || key == Qt::Key_Tab || key == Qt::Key_Backtab || key == Qt::Key_Shift || key == Qt::Key_Control
		|| key == Qt::Key_Alt || key == Qt::Key_Meta || key == Qt::Key_AltGr || key == Qt::Key_CapsLock
		|| key == Qt::Key_NumLock || key == Qt::Key_ScrollLock) {
		return fail(error, QCoreApplication::translate("LevelCameraKeys", "Escape, Tab, modifier keys and lock keys are reserved for cancellation, focus and modifiers."));
	}
	if (normalized) { *normalized = sequence.toString(QKeySequence::PortableText); }
	return true;
}

QStringList cameraKeyProblems(const CameraViewControls& controls)
{
	QStringList problems;
	for (const auto& keys : {controls.flyKeys, controls.driveKeys}) {
		for (const auto& key : keys.all()) {
			QString error;
			if (!normalizeCameraKey(key, false, nullptr, &error)) { problems << error; }
		}
	}
	for (const auto& lookKey : {controls.lookToggleKey, controls.lookHoldKey}) {
		if (lookKey.isEmpty()) { continue; }
		QString error;
		if (!normalizeCameraKey(lookKey, lookKey == controls.lookToggleKey, nullptr, &error)) { problems << error; }
		const auto toggle = QKeySequence::fromString(lookKey, QKeySequence::PortableText);
		if (toggle.count() == 1 && (cameraMotionModifiersAccepted(controls, toggle[0].keyboardModifiers(), Qt::NoButton, Qt::NoModifier, true)
			|| cameraMotionModifiersAccepted(controls, toggle[0].keyboardModifiers(), controls.lookButton, controls.lookModifiers))) {
			for (const auto& keys : {controls.flyKeys, controls.driveKeys}) {
				for (const auto& key : keys.all()) {
					if (QKeySequence::fromString(key, QKeySequence::PortableText)[0].key() == toggle[0].key()) {
						problems << QCoreApplication::translate("LevelCameraKeys", "Mouse-look activation conflicts with a camera movement key.");
					}
				}
			}
		}
	}
	if (!controls.lookHoldKey.isEmpty() && controls.lookHoldKey == controls.lookToggleKey) {
		problems << QCoreApplication::translate("LevelCameraKeys", "Hold and toggle mouse look must use different keys.");
	}
	// Modal profiles can reuse a drive key for another fly direction: the fly
	// binding wins while mouse look is active, then the drive binding resumes.
	for (qsizetype fly = 0; !controls.flyNeedsLook && fly < directions.size(); ++fly) {
		const auto& key = controls.flyKeys.*directions[fly];
		if (key.isEmpty()) { continue; }
		for (qsizetype drive = 0; drive < directions.size(); ++drive) {
			if (fly != drive && key == controls.driveKeys.*directions[drive]) {
				problems << QCoreApplication::translate("LevelCameraKeys", "%1 moves in different directions in the fly and drive bindings.").arg(key);
			}
		}
	}
	if (controls.perspective && controls.flyNeedsLook && !controls.flyKeys.isEmpty()
		&& controls.lookButton == Qt::NoButton && controls.lookToggleKey.isEmpty() && controls.lookHoldKey.isEmpty()) {
		problems << QCoreApplication::translate("LevelCameraKeys", "Fly keys require a mouse-look button, hold key or toggle key when mouse look is required.");
	}
	problems.removeDuplicates();
	return problems;
}

bool cameraMotionModifiersAccepted(const CameraViewControls& controls, Qt::KeyboardModifiers modifiers,
	Qt::MouseButton pressedButton, Qt::KeyboardModifiers pressModifiers, bool looking)
{
	const bool heldLook = cameraNavigationDrag(controls, pressedButton, pressModifiers) == CameraNavigationDrag::Look;
	auto allowed = Qt::KeyboardModifiers(Qt::KeypadModifier);
	if (!controls.speedModifiersRequireLook || looking || heldLook) { allowed |= controls.fastModifiers | controls.slowModifiers; }
	if (heldLook) { allowed |= controls.lookModifiers; }
	return (modifiers & ~allowed) == Qt::NoModifier;
}

bool navigationHoldKeyMatches(const QString& binding, int key, Qt::KeyboardModifiers modifiers)
{
	if (binding.isEmpty() || modifiers != Qt::NoModifier) { return false; }
	const auto sequence = QKeySequence::fromString(binding, QKeySequence::PortableText);
	return sequence.count() == 1 && sequence[0].keyboardModifiers() == Qt::NoModifier && sequence[0].key() == key;
}

bool cameraLookToggleMatches(const CameraViewControls& controls, int key, Qt::KeyboardModifiers modifiers)
{
	if (!controls.perspective || controls.lookToggleKey.isEmpty()) { return false; }
	const auto sequence = QKeySequence::fromString(controls.lookToggleKey, QKeySequence::PortableText);
	return sequence.count() == 1 && sequence[0].key() == key && sequence[0].keyboardModifiers() == modifiers;
}

QHash<int, CameraKeyMotion> cameraKeyMotions(const CameraViewControls& controls, bool looking,
	Qt::MouseButton pressedButton, Qt::KeyboardModifiers pressModifiers, Qt::KeyboardModifiers modifiers)
{
	QHash<int, CameraKeyMotion> result;
	if (!controls.perspective) { return result; }
	const auto add = [&](const QString& name, CameraKeyMotion motion) {
		if (name.isEmpty()) { return; }
		const auto sequence = QKeySequence::fromString(name, QKeySequence::PortableText);
		if (sequence.count() == 1) { result.insert(sequence[0].key(), motion); }
	};
	const auto addSet = [&](const CameraFlyKeys& keys) {
		const bool movement = cameraMotionModifiersAccepted(controls, modifiers, pressedButton, pressModifiers, looking);
		if (movement) {
			add(keys.forward, {1, 0, 0, 0}); add(keys.back, {-1, 0, 0, 0});
			add(keys.left, {0, -1, 0, 0}); add(keys.right, {0, 1, 0, 0});
			add(keys.up, {0, 0, 1, 0}); add(keys.down, {0, 0, -1, 0});
		}
		const auto plainModifiers = modifiers & ~Qt::KeypadModifier;
		const bool translate = controls.turnKeysPanModifiers != Qt::NoModifier && plainModifiers == controls.turnKeysPanModifiers;
		// Sledge's arrow rotation has its own modifier scope: Shift translates,
		// Ctrl/Alt leave the arrows to commands, even during free look.
		if (controls.turnKeysPanModifiers != Qt::NoModifier ? (!translate && plainModifiers != Qt::NoModifier) : !movement) { return; }
		const bool strafe = translate || (looking && controls.turnKeysStrafeInLook);
		add(keys.turnLeft, strafe ? CameraKeyMotion{0, -1, 0, 0} : CameraKeyMotion{0, 0, 0, 1});
		add(keys.turnRight, strafe ? CameraKeyMotion{0, 1, 0, 0} : CameraKeyMotion{0, 0, 0, -1});
		add(keys.pitchUp, translate ? CameraKeyMotion{0, 0, 1, 0} : CameraKeyMotion{0, 0, 0, 0, 1});
		add(keys.pitchDown, translate ? CameraKeyMotion{0, 0, -1, 0} : CameraKeyMotion{0, 0, 0, 0, -1});
	};
	addSet(controls.driveKeys);
	if (cameraFlyKeysActive(controls, looking, pressedButton, pressModifiers)) { addSet(controls.flyKeys); }
	return result;
}

bool cameraKeyUsesFixedStep(const CameraViewControls& controls, int key, bool looking,
	Qt::MouseButton pressedButton, Qt::KeyboardModifiers pressModifiers, Qt::KeyboardModifiers modifiers)
{
	if (!controls.discreteDriveKeys || looking) { return false; }
	auto flyOnly = controls; flyOnly.driveKeys = {};
	return !cameraKeyMotions(flyOnly, looking, pressedButton, pressModifiers, modifiers).contains(key)
		&& cameraKeyMotions(controls, looking, pressedButton, pressModifiers, modifiers).contains(key);
}

QStringList levelCameraShortcutWarnings(const EditorProfileDescriptor& profile, const LevelEditorControls& controls,
	const QHash<QString, QStringList>& userShortcuts)
{
	QStringList warnings;
	QHash<QString, QStringList> overrides;
	for (const auto& binding : editorProfileImplementedBindings(profile)) {
		const auto keys = editorProfileBindingKeys(binding);
		if (!keys.isEmpty() || binding.clearsKeys) { overrides.insert(commandKey(binding.commandId), keys); }
	}
	QHash<QString, QStringList> customKeys;
	for (auto it = userShortcuts.begin(); it != userShortcuts.end(); ++it) { customKeys.insert(commandKey(it.key()), it.value()); }
	auto commands = shortcutDescriptors();
	QSet<QString> seen;
	for (const auto& command : commands) { seen.insert(commandKey(command.commandId)); }
	// Palette-only level actions are also remappable in Keyboard settings.
	for (const auto& entry : commandPaletteEntries()) {
		const auto id = commandKey(entry.commandId);
		if (!entry.commandId.startsWith(QLatin1String("map.")) || seen.contains(id)) { continue; }
		ShortcutDescriptor command;
		command.commandId = entry.commandId; command.label = entry.label; command.context = QStringLiteral("map"); command.surfaceScoped = true;
		commands << command; seen.insert(id);
	}
	seen.clear();
	for (const auto& command : commands) {
		const auto id = commandKey(command.commandId);
		if ((command.surfaceScoped && command.context != QLatin1String("map")) || seen.contains(id)) { continue; }
		seen.insert(id);
		auto keys = command.defaultSequence.isEmpty() ? QStringList() : QStringList{command.defaultSequence};
		keys.append(command.alternateSequences);
		if (overrides.contains(id)) { keys = overrides.value(id); }
		const bool custom = command.userRemappable && customKeys.contains(id);
		if (custom) { keys = customKeys.value(id); }
		for (const auto& text : keys) {
			const auto key = QKeySequence::fromString(text, QKeySequence::PortableText);
			if (key.isEmpty()) { continue; }
			const bool always = claims(controls.camera, key[0], false);
			const bool looking = claims(controls.camera, key[0], true);
			const auto planClaims = [&](const PlanViewControls& plan) {
				return navigationHoldKeyMatches(plan.panHoldKey, key[0].key(), key[0].keyboardModifiers())
					|| !planArrowPanDirection(plan, key[0].key(), key[0].keyboardModifiers()).isNull();
			};
			const bool plan = planClaims(controls.plan);
			if ((!always && !looking && !plan) || (!custom && (!always || claims(profile.controls.camera, key[0], false))
				&& (!looking || claims(profile.controls.camera, key[0], true)) && (!plan || planClaims(profile.controls.plan)))) { continue; }
			warnings << (plan ? QCoreApplication::translate("LevelCameraKeys", "%1 may take priority over %2 while a plan view has focus.")
				: always ? QCoreApplication::translate("LevelCameraKeys", "%1 may take priority over %2 while the camera has focus.")
				: QCoreApplication::translate("LevelCameraKeys", "%1 may take priority over %2 during mouse look."))
				.arg(key.toString(QKeySequence::NativeText), command.label);
		}
	}
	warnings.removeDuplicates();
	return warnings;
}
} // namespace vibestudio
