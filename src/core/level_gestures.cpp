#include "core/level_gestures.h"
#include "core/editor_profiles.h"
#include "core/level_camera_keys.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <functional>
#include <type_traits>
#include <tuple>

namespace vibestudio {
namespace {
constexpr qint64 maximumBytes = 64 * 1024;
using Options = QVector<LevelGestureOption>;
struct Field {
	LevelGestureField info;
	std::function<QString(const LevelEditorControls&)> get;
	std::function<void(LevelEditorControls&, const QString&)> set;
};
bool fail(QString* error, const QString& message) { if (error) { *error = message; } return false; }
template<typename T> int valueOf(const T& value) { return static_cast<int>(value); }
template<> int valueOf(const QVector<Qt::MouseButton>& buttons)
{
	int value = 0; for (const auto button : buttons) { value |= static_cast<int>(button); } return value;
}
template<typename T> T fromValue(int value)
{
	if constexpr (std::is_same_v<T, QVector<Qt::MouseButton>>) {
		QVector<Qt::MouseButton> buttons;
		for (const auto button : {Qt::RightButton, Qt::MiddleButton}) { if ((value & button) != 0) { buttons << button; } }
		return buttons;
	} else { return T(value); }
}
QVector<Field> fields()
{
	const Options buttons {{"none", QCoreApplication::translate("LevelGestures", "None"), Qt::NoButton},
		{"left", QCoreApplication::translate("LevelGestures", "Left"), Qt::LeftButton},
		{"right", QCoreApplication::translate("LevelGestures", "Right"), Qt::RightButton},
		{"middle", QCoreApplication::translate("LevelGestures", "Middle"), Qt::MiddleButton}};
	Options planButtons = buttons; planButtons.removeAt(1);
	Options pans = planButtons;
	pans << LevelGestureOption{"right+middle", QCoreApplication::translate("LevelGestures", "Right or middle"), Qt::RightButton | Qt::MiddleButton};
	const Options booleans {{"off", QCoreApplication::translate("LevelGestures", "Off"), 0}, {"on", QCoreApplication::translate("LevelGestures", "On"), 1}};
	Options modifiers;
	const QVector<LevelGestureOption> modifierNames {{"ctrl", QCoreApplication::translate("LevelGestures", "Ctrl"), Qt::ControlModifier},
		{"shift", QCoreApplication::translate("LevelGestures", "Shift"), Qt::ShiftModifier},
		{"alt", QCoreApplication::translate("LevelGestures", "Alt"), Qt::AltModifier},
		{"meta", QCoreApplication::translate("LevelGestures", "Meta"), Qt::MetaModifier}};
	for (int mask = 0; mask < 16; ++mask) {
		QStringList ids, labels; int value = 0;
		for (int i = 0; i < 4; ++i) { if ((mask & (1 << i)) != 0) { ids << modifierNames[i].id; labels << modifierNames[i].label; value |= modifierNames[i].value; } }
		modifiers << LevelGestureOption{ids.isEmpty() ? QStringLiteral("none") : ids.join('+'),
			labels.isEmpty() ? QCoreApplication::translate("LevelGestures", "None") : labels.join('+'), value};
	}
	const Options empty {{"box-select", QCoreApplication::translate("LevelGestures", "Box select"), int(PlanEmptyDrag::BoxSelect)},
		{"draw-brush", QCoreApplication::translate("LevelGestures", "Draw brush"), int(PlanEmptyDrag::DrawBrush)},
		{"resize-selection", QCoreApplication::translate("LevelGestures", "Resize selection"), int(PlanEmptyDrag::ResizeSelection)}};
	const Options wheel {{"zoom", QCoreApplication::translate("LevelGestures", "Zoom"), int(CameraWheel::Zoom)},
		{"dolly", QCoreApplication::translate("LevelGestures", "Move along view"), int(CameraWheel::Dolly)},
		{"dolly-to-pointer", QCoreApplication::translate("LevelGestures", "Move toward pointer"), int(CameraWheel::DollyToPointer)}};
	QVector<Field> result;
	const auto plan = [&]<typename T>(const QString& id, const QString& label, T PlanViewControls::*member, const Options& choices) {
		result << Field{{QStringLiteral("plan.") + id, label, QStringLiteral("plan"), choices},
			[member, choices](const LevelEditorControls& c) { for (const auto& choice : choices) { if (choice.value == valueOf(c.plan.*member)) { return choice.id; } } return QString(); },
			[member, choices](LevelEditorControls& c, const QString& value) { for (const auto& choice : choices) { if (choice.id == value) { c.plan.*member = fromValue<T>(choice.value); return; } } }};
	};
	const auto camera = [&]<typename T>(const QString& id, const QString& label, T CameraViewControls::*member, const Options& choices) {
		result << Field{{QStringLiteral("camera.") + id, label, QStringLiteral("camera"), choices},
			[member, choices](const LevelEditorControls& c) { for (const auto& choice : choices) { if (choice.value == valueOf(c.camera.*member)) { return choice.id; } } return QString(); },
			[member, choices](LevelEditorControls& c, const QString& value) { for (const auto& choice : choices) { if (choice.id == value) { c.camera.*member = fromValue<T>(choice.value); return; } } }};
	};
	plan(QStringLiteral("panButtons"), QCoreApplication::translate("LevelGestures", "Pan buttons"), &PlanViewControls::panButtons, pans);
	plan(QStringLiteral("zoomDragButton"), QCoreApplication::translate("LevelGestures", "Zoom drag button"), &PlanViewControls::zoomDragButton, planButtons);
	plan(QStringLiteral("zoomDragModifiers"), QCoreApplication::translate("LevelGestures", "Zoom drag modifiers"), &PlanViewControls::zoomDragModifiers, modifiers);
	plan(QStringLiteral("toggleModifiers"), QCoreApplication::translate("LevelGestures", "Toggle selection modifiers"), &PlanViewControls::toggleModifiers, modifiers);
	plan(QStringLiteral("addModifiers"), QCoreApplication::translate("LevelGestures", "Add selection modifiers"), &PlanViewControls::addModifiers, modifiers);
	plan(QStringLiteral("bandModifiers"), QCoreApplication::translate("LevelGestures", "Box selection modifiers"), &PlanViewControls::bandModifiers, modifiers);
	plan(QStringLiteral("emptyDrag"), QCoreApplication::translate("LevelGestures", "Empty-space drag"), &PlanViewControls::emptyDrag, empty);
	plan(QStringLiteral("emptyDragWithSelection"), QCoreApplication::translate("LevelGestures", "Empty-space drag with selection"), &PlanViewControls::emptyDragWithSelection, empty);
	plan(QStringLiteral("clickCyclesStack"), QCoreApplication::translate("LevelGestures", "Click cycles overlapping objects"), &PlanViewControls::clickCyclesStack, booleans);
	plan(QStringLiteral("plainClickSelects"), QCoreApplication::translate("LevelGestures", "Plain click selects"), &PlanViewControls::plainClickSelects, booleans);
	plan(QStringLiteral("cycleModifiers"), QCoreApplication::translate("LevelGestures", "Cycle overlapping objects modifiers"), &PlanViewControls::cycleModifiers, modifiers);
	plan(QStringLiteral("middleButtonDrivesCamera"), QCoreApplication::translate("LevelGestures", "Middle button aims or places camera"), &PlanViewControls::middleButtonDrivesCamera, booleans);
	plan(QStringLiteral("arrowsDriveCamera"), QCoreApplication::translate("LevelGestures", "Arrow keys drive camera"), &PlanViewControls::arrowsDriveCamera, booleans);
	plan(QStringLiteral("fixedCameraSteps"), QCoreApplication::translate("LevelGestures", "Fixed camera steps (32 units / 22.5 degrees)"), &PlanViewControls::fixedCameraSteps, booleans);
	plan(QStringLiteral("squareModifiers"), QCoreApplication::translate("LevelGestures", "Square brush modifiers"), &PlanViewControls::squareModifiers, modifiers);
	plan(QStringLiteral("cubeModifiers"), QCoreApplication::translate("LevelGestures", "Cube brush modifiers"), &PlanViewControls::cubeModifiers, modifiers);
	plan(QStringLiteral("panArrowModifiers"), QCoreApplication::translate("LevelGestures", "Arrow-key pan modifiers"), &PlanViewControls::panArrowModifiers, modifiers);
	camera(QStringLiteral("orbitButton"), QCoreApplication::translate("LevelGestures", "Orbit button"), &CameraViewControls::orbitButton, buttons);
	camera(QStringLiteral("orbitModifiers"), QCoreApplication::translate("LevelGestures", "Orbit modifiers"), &CameraViewControls::orbitModifiers, modifiers);
	camera(QStringLiteral("lookButton"), QCoreApplication::translate("LevelGestures", "Look button"), &CameraViewControls::lookButton, planButtons);
	camera(QStringLiteral("lookModifiers"), QCoreApplication::translate("LevelGestures", "Look modifiers"), &CameraViewControls::lookModifiers, modifiers);
	camera(QStringLiteral("driveButton"), QCoreApplication::translate("LevelGestures", "Position steering button"), &CameraViewControls::driveButton, planButtons);
	camera(QStringLiteral("driveModifiers"), QCoreApplication::translate("LevelGestures", "Position steering modifiers"), &CameraViewControls::driveModifiers, modifiers);
	camera(QStringLiteral("discreteDriveKeys"), QCoreApplication::translate("LevelGestures", "Drive keys step 32 units / 22.5 degrees"), &CameraViewControls::discreteDriveKeys, booleans);
	camera(QStringLiteral("lookClickToggles"), QCoreApplication::translate("LevelGestures", "Click toggles mouse look"), &CameraViewControls::lookClickToggles, booleans);
	camera(QStringLiteral("panButtons"), QCoreApplication::translate("LevelGestures", "Pan buttons"), &CameraViewControls::panButtons, pans);
	camera(QStringLiteral("panModifiers"), QCoreApplication::translate("LevelGestures", "Pan modifiers"), &CameraViewControls::panModifiers, modifiers);
	camera(QStringLiteral("leftPanModifiers"), QCoreApplication::translate("LevelGestures", "Left-drag pan modifier alternatives"), &CameraViewControls::leftPanModifiers, modifiers);
	camera(QStringLiteral("wheel"), QCoreApplication::translate("LevelGestures", "Mouse wheel"), &CameraViewControls::wheel, wheel);
	camera(QStringLiteral("fieldOfViewWheelModifiers"), QCoreApplication::translate("LevelGestures", "Field-of-view wheel modifiers"), &CameraViewControls::fieldOfViewWheelModifiers, modifiers);
	camera(QStringLiteral("toggleModifiers"), QCoreApplication::translate("LevelGestures", "Toggle selection modifiers"), &CameraViewControls::toggleModifiers, modifiers);
	camera(QStringLiteral("faceModifiers"), QCoreApplication::translate("LevelGestures", "Face selection modifiers"), &CameraViewControls::faceModifiers, modifiers);
	camera(QStringLiteral("materialSampleButton"), QCoreApplication::translate("LevelGestures", "Sample material button"), &CameraViewControls::materialSampleButton, planButtons);
	camera(QStringLiteral("materialSampleModifiers"), QCoreApplication::translate("LevelGestures", "Sample material modifiers"), &CameraViewControls::materialSampleModifiers, modifiers);
	camera(QStringLiteral("materialPaintButton"), QCoreApplication::translate("LevelGestures", "Paint material button"), &CameraViewControls::materialPaintButton, planButtons);
	camera(QStringLiteral("materialPaintModifiers"), QCoreApplication::translate("LevelGestures", "Paint material modifiers"), &CameraViewControls::materialPaintModifiers, modifiers);
	camera(QStringLiteral("surfacePasteFaceButton"), QCoreApplication::translate("LevelGestures", "Paste surface on face button"), &CameraViewControls::surfacePasteFaceButton, planButtons);
	camera(QStringLiteral("surfacePasteFaceModifiers"), QCoreApplication::translate("LevelGestures", "Paste surface on face modifiers"), &CameraViewControls::surfacePasteFaceModifiers, modifiers);
	camera(QStringLiteral("surfacePasteBrushButton"), QCoreApplication::translate("LevelGestures", "Paste surface on brush button"), &CameraViewControls::surfacePasteBrushButton, planButtons);
	camera(QStringLiteral("surfacePasteBrushModifiers"), QCoreApplication::translate("LevelGestures", "Paste surface on brush modifiers"), &CameraViewControls::surfacePasteBrushModifiers, modifiers);
	camera(QStringLiteral("surfaceWrapFaceButton"), QCoreApplication::translate("LevelGestures", "Wrap surface onto face button"), &CameraViewControls::surfaceWrapFaceButton, planButtons);
	camera(QStringLiteral("surfaceWrapFaceModifiers"), QCoreApplication::translate("LevelGestures", "Wrap surface onto face modifiers"), &CameraViewControls::surfaceWrapFaceModifiers, modifiers);
	camera(QStringLiteral("surfaceValuesButton"), QCoreApplication::translate("LevelGestures", "Paste values onto hit and selection button"), &CameraViewControls::surfaceValuesButton, planButtons);
	camera(QStringLiteral("surfaceValuesModifiers"), QCoreApplication::translate("LevelGestures", "Paste values onto hit and selection modifiers"), &CameraViewControls::surfaceValuesModifiers, modifiers);
	camera(QStringLiteral("surfaceValuesOnlyButton"), QCoreApplication::translate("LevelGestures", "Paste mapping values only button"), &CameraViewControls::surfaceValuesOnlyButton, planButtons);
	camera(QStringLiteral("surfaceValuesOnlyModifiers"), QCoreApplication::translate("LevelGestures", "Paste mapping values only modifiers"), &CameraViewControls::surfaceValuesOnlyModifiers, modifiers);
	camera(QStringLiteral("surfaceWrapOnlyButton"), QCoreApplication::translate("LevelGestures", "Wrap mapping only button"), &CameraViewControls::surfaceWrapOnlyButton, planButtons);
	camera(QStringLiteral("surfaceWrapOnlyModifiers"), QCoreApplication::translate("LevelGestures", "Wrap mapping only modifiers"), &CameraViewControls::surfaceWrapOnlyModifiers, modifiers);
	camera(QStringLiteral("surfaceProjectButton"), QCoreApplication::translate("LevelGestures", "Project surface onto hit and selection button"), &CameraViewControls::surfaceProjectButton, planButtons);
	camera(QStringLiteral("surfaceProjectModifiers"), QCoreApplication::translate("LevelGestures", "Project surface onto hit and selection modifiers"), &CameraViewControls::surfaceProjectModifiers, modifiers);
	camera(QStringLiteral("surfaceProjectOnlyButton"), QCoreApplication::translate("LevelGestures", "Project mapping only button"), &CameraViewControls::surfaceProjectOnlyButton, planButtons);
	camera(QStringLiteral("surfaceProjectOnlyModifiers"), QCoreApplication::translate("LevelGestures", "Project mapping only modifiers"), &CameraViewControls::surfaceProjectOnlyModifiers, modifiers);
	camera(QStringLiteral("plainClickSelects"), QCoreApplication::translate("LevelGestures", "Plain click selects"), &CameraViewControls::plainClickSelects, booleans);
	camera(QStringLiteral("dragMovesSelection"), QCoreApplication::translate("LevelGestures", "Left drag moves selection"), &CameraViewControls::dragMovesSelection, booleans);
	camera(QStringLiteral("flyNeedsLook"), QCoreApplication::translate("LevelGestures", "Fly keys require mouse look"), &CameraViewControls::flyNeedsLook, booleans);
	camera(QStringLiteral("fastModifiers"), QCoreApplication::translate("LevelGestures", "Fast flight modifiers"), &CameraViewControls::fastModifiers, modifiers);
	camera(QStringLiteral("slowModifiers"), QCoreApplication::translate("LevelGestures", "Slow flight modifiers"), &CameraViewControls::slowModifiers, modifiers);
	camera(QStringLiteral("lookPanUsesButtons"), QCoreApplication::translate("LevelGestures", "Use mouse buttons to pan during mouse look"), &CameraViewControls::lookPanUsesButtons, booleans);
	camera(QStringLiteral("turnKeysStrafeInLook"), QCoreApplication::translate("LevelGestures", "Turn keys strafe during mouse look"), &CameraViewControls::turnKeysStrafeInLook, booleans);
	camera(QStringLiteral("turnKeysPanModifiers"), QCoreApplication::translate("LevelGestures", "Turn / look key translation modifiers"), &CameraViewControls::turnKeysPanModifiers, modifiers);
	camera(QStringLiteral("speedModifiersRequireLook"), QCoreApplication::translate("LevelGestures", "Flight speed modifiers require mouse look"), &CameraViewControls::speedModifiersRequireLook, booleans);
	const Options noKey {{"none", QCoreApplication::translate("LevelGestures", "None"), 0}};
	result << Field{{QStringLiteral("plan.panHoldKey"), QCoreApplication::translate("LevelGestures", "Hold to pan"), QStringLiteral("plan"), noKey, LevelGestureFieldKind::HoldKey},
		[](const LevelEditorControls& c) { return c.plan.panHoldKey.isEmpty() ? QStringLiteral("none") : c.plan.panHoldKey; },
		[](LevelEditorControls& c, const QString& value) { c.plan.panHoldKey = value == QLatin1String("none") ? QString() : value; }};
	result << Field{{QStringLiteral("camera.lookHoldKey"), QCoreApplication::translate("LevelGestures", "Hold for mouse look"), QStringLiteral("camera-keys"), noKey, LevelGestureFieldKind::HoldKey},
		[](const LevelEditorControls& c) { return c.camera.lookHoldKey.isEmpty() ? QStringLiteral("none") : c.camera.lookHoldKey; },
		[](LevelEditorControls& c, const QString& value) { c.camera.lookHoldKey = value == QLatin1String("none") ? QString() : value; }};
	const auto movement = [&](const QString& id, const QString& label, QString CameraFlyKeys::*member) {
		for (bool fly : {true, false}) {
			const auto group = fly ? QStringLiteral("camera.flyKeys.") : QStringLiteral("camera.driveKeys.");
			const auto title = fly ? QCoreApplication::translate("LevelGestures", "Fly: %1").arg(label)
				: QCoreApplication::translate("LevelGestures", "Drive: %1").arg(label);
			result << Field{{group + id, title, QStringLiteral("camera-keys"), noKey, LevelGestureFieldKind::MotionKey},
				[fly, member](const LevelEditorControls& c) { const auto& value = (fly ? c.camera.flyKeys : c.camera.driveKeys).*member; return value.isEmpty() ? QStringLiteral("none") : value; },
				[fly, member](LevelEditorControls& c, const QString& value) { (fly ? c.camera.flyKeys : c.camera.driveKeys).*member = value == QLatin1String("none") ? QString() : value; }};
		}
	};
	movement(QStringLiteral("forward"), QCoreApplication::translate("LevelGestures", "Forward"), &CameraFlyKeys::forward);
	movement(QStringLiteral("back"), QCoreApplication::translate("LevelGestures", "Back"), &CameraFlyKeys::back);
	movement(QStringLiteral("left"), QCoreApplication::translate("LevelGestures", "Left"), &CameraFlyKeys::left);
	movement(QStringLiteral("right"), QCoreApplication::translate("LevelGestures", "Right"), &CameraFlyKeys::right);
	movement(QStringLiteral("up"), QCoreApplication::translate("LevelGestures", "Up"), &CameraFlyKeys::up);
	movement(QStringLiteral("down"), QCoreApplication::translate("LevelGestures", "Down"), &CameraFlyKeys::down);
	movement(QStringLiteral("turnLeft"), QCoreApplication::translate("LevelGestures", "Turn left"), &CameraFlyKeys::turnLeft);
	movement(QStringLiteral("turnRight"), QCoreApplication::translate("LevelGestures", "Turn right"), &CameraFlyKeys::turnRight);
	movement(QStringLiteral("pitchUp"), QCoreApplication::translate("LevelGestures", "Look up"), &CameraFlyKeys::pitchUp);
	movement(QStringLiteral("pitchDown"), QCoreApplication::translate("LevelGestures", "Look down"), &CameraFlyKeys::pitchDown);
	result << Field{{QStringLiteral("camera.lookToggleKey"), QCoreApplication::translate("LevelGestures", "Toggle mouse look"),
		QStringLiteral("camera-keys"), noKey, LevelGestureFieldKind::ToggleKey},
		[](const LevelEditorControls& c) { return c.camera.lookToggleKey.isEmpty() ? QStringLiteral("none") : c.camera.lookToggleKey; },
		[](LevelEditorControls& c, const QString& value) { c.camera.lookToggleKey = value == QLatin1String("none") ? QString() : value; }};
	return result;
}
}

QVector<LevelGestureField> levelGestureFields()
{
	QVector<LevelGestureField> result; for (const auto& field : fields()) { result << field.info; } return result;
}

QString levelGestureFieldKindId(LevelGestureFieldKind kind)
{
	switch (kind) {
	case LevelGestureFieldKind::MotionKey: return QStringLiteral("motion-key");
	case LevelGestureFieldKind::ToggleKey: return QStringLiteral("toggle-key");
	case LevelGestureFieldKind::HoldKey: return QStringLiteral("hold-key");
	case LevelGestureFieldKind::Choice: return QStringLiteral("choice");
	}
	return {};
}

QJsonObject levelGestureValues(const LevelEditorControls& controls)
{
	QJsonObject result;
	for (const auto& field : fields()) {
		result.insert(field.info.id, field.get(controls));
	}
	return result;
}

bool levelGestureFieldAvailable(const QString& id, const LevelEditorControls& controls)
{
	return controls.camera.perspective || !(id == QLatin1String("plan.middleButtonDrivesCamera") || id == QLatin1String("plan.arrowsDriveCamera")
		|| id.startsWith(QLatin1String("camera.look")) || id == QLatin1String("camera.wheel") || id == QLatin1String("camera.fieldOfViewWheelModifiers")
		|| id.startsWith(QLatin1String("camera.flyKeys.")) || id.startsWith(QLatin1String("camera.driveKeys."))
		|| id == QLatin1String("plan.fixedCameraSteps") || id == QLatin1String("camera.discreteDriveKeys") || id.startsWith(QLatin1String("camera.drive"))
		|| id == QLatin1String("camera.dragMovesSelection") || id == QLatin1String("camera.flyNeedsLook")
		|| id.startsWith(QLatin1String("camera.turnKeys")) || id == QLatin1String("camera.speedModifiersRequireLook")
		|| id == QLatin1String("camera.fastModifiers") || id == QLatin1String("camera.slowModifiers"));
}

bool applyLevelGestureOverrides(const LevelEditorControls& base, const QJsonObject& overrides,
	LevelEditorControls* result, QJsonObject* normalized, QString* error)
{
	if (error) { error->clear(); }
	auto candidate = base;
	QJsonObject cleaned;
	const auto catalog = fields();
	for (auto it = overrides.begin(); it != overrides.end(); ++it) {
		const auto field = std::find_if(catalog.cbegin(), catalog.cend(), [&](const Field& f) { return f.info.id == it.key(); });
		if (field == catalog.cend()) { return fail(error, QCoreApplication::translate("LevelGestures", "Unknown gesture setting: %1.").arg(it.key())); }
		if (!it.value().isString()) { return fail(error, QCoreApplication::translate("LevelGestures", "Invalid choice for %1.").arg(it.key())); }
		auto value = it.value().toString();
		if (field->info.kind == LevelGestureFieldKind::Choice) {
			if (std::none_of(field->info.options.cbegin(), field->info.options.cend(), [&](const LevelGestureOption& option) { return option.id == value; })) {
				return fail(error, QCoreApplication::translate("LevelGestures", "Invalid choice for %1.").arg(it.key()));
			}
		} else {
			QString keyError;
			if (!normalizeCameraKey(value, field->info.kind == LevelGestureFieldKind::ToggleKey, &value, &keyError)) {
				return fail(error, QCoreApplication::translate("LevelGestures", "%1: %2").arg(field->info.label, keyError));
			}
		}
		if (field->get(base) != value && !levelGestureFieldAvailable(it.key(), base)) {
			return fail(error, QCoreApplication::translate("LevelGestures", "%1 requires a profile with a perspective camera.").arg(field->info.label));
		}
		field->set(candidate, value);
		if (field->get(base) != value) { cleaned.insert(it.key(), value); }
	}
	// Existing navigation customizations predate material gestures. An inherited
	// material default yields to them, recording the disabled action explicitly
	// on the next save/export. Explicitly conflicting material choices still fail.
	const auto preserveNavigation = [&](const QString& prefix, Qt::MouseButton& button, Qt::KeyboardModifiers modifiers) {
		if (!overrides.contains(prefix + QLatin1String("Button")) && !overrides.contains(prefix + QLatin1String("Modifiers"))
			&& cameraMaterialGestureConflicts(candidate.camera, button, modifiers)) {
			button = Qt::NoButton;
			cleaned.insert(prefix + QLatin1String("Button"), QStringLiteral("none"));
		}
	};
	preserveNavigation(QStringLiteral("camera.materialSample"), candidate.camera.materialSampleButton, candidate.camera.materialSampleModifiers);
	preserveNavigation(QStringLiteral("camera.materialPaint"), candidate.camera.materialPaintButton, candidate.camera.materialPaintModifiers);
	const auto preserveOlderMaterial = [&](const QString& prefix, Qt::MouseButton& button, Qt::KeyboardModifiers modifiers) {
		preserveNavigation(prefix, button, modifiers);
		if (button == Qt::NoButton || overrides.contains(prefix + QLatin1String("Button")) || overrides.contains(prefix + QLatin1String("Modifiers"))) { return; }
		for (const auto& other : {QStringLiteral("camera.materialSample"), QStringLiteral("camera.materialPaint")}) {
			if (!overrides.contains(other + QLatin1String("Button")) && !overrides.contains(other + QLatin1String("Modifiers"))) { continue; }
			const bool sample = other.endsWith(QLatin1String("Sample"));
			if (button == (sample ? candidate.camera.materialSampleButton : candidate.camera.materialPaintButton)
				&& modifiers == (sample ? candidate.camera.materialSampleModifiers : candidate.camera.materialPaintModifiers)) {
				button = Qt::NoButton; cleaned.insert(prefix + QLatin1String("Button"), QStringLiteral("none")); return;
			}
		}
	};
	preserveOlderMaterial(QStringLiteral("camera.surfacePasteFace"), candidate.camera.surfacePasteFaceButton, candidate.camera.surfacePasteFaceModifiers);
	preserveOlderMaterial(QStringLiteral("camera.surfacePasteBrush"), candidate.camera.surfacePasteBrushButton, candidate.camera.surfacePasteBrushModifiers);
	preserveOlderMaterial(QStringLiteral("camera.surfaceWrapFace"), candidate.camera.surfaceWrapFaceButton, candidate.camera.surfaceWrapFaceModifiers);
	// Wrapping was introduced after parameter-paste customizations. Preserve
	// those too, but reject conflicts when both roles were explicitly chosen.
	if (!overrides.contains(QStringLiteral("camera.surfaceWrapFaceButton")) && !overrides.contains(QStringLiteral("camera.surfaceWrapFaceModifiers"))) {
		for (const auto& prefix : {QStringLiteral("camera.surfacePasteFace"), QStringLiteral("camera.surfacePasteBrush")}) {
			if (!overrides.contains(prefix + QLatin1String("Button")) && !overrides.contains(prefix + QLatin1String("Modifiers"))) { continue; }
			const bool face = prefix.endsWith(QLatin1String("Face"));
			if (candidate.camera.surfaceWrapFaceButton != Qt::NoButton
				&& candidate.camera.surfaceWrapFaceButton == (face ? candidate.camera.surfacePasteFaceButton : candidate.camera.surfacePasteBrushButton)
				&& candidate.camera.surfaceWrapFaceModifiers == (face ? candidate.camera.surfacePasteFaceModifiers : candidate.camera.surfacePasteBrushModifiers)) {
				candidate.camera.surfaceWrapFaceButton = Qt::NoButton;
				cleaned.insert(QStringLiteral("camera.surfaceWrapFaceButton"), QStringLiteral("none"));
			}
		}
	}
	const auto preserveEarlierSurface = [&](const QString& prefix, Qt::MouseButton& button, Qt::KeyboardModifiers modifiers) {
		preserveOlderMaterial(prefix, button, modifiers);
		if (button == Qt::NoButton || overrides.contains(prefix + QLatin1String("Button")) || overrides.contains(prefix + QLatin1String("Modifiers"))) { return; }
		const auto& camera = candidate.camera;
		for (const auto& entry : {std::tuple{QStringLiteral("camera.surfacePasteFace"), camera.surfacePasteFaceButton, camera.surfacePasteFaceModifiers},
			std::tuple{QStringLiteral("camera.surfacePasteBrush"), camera.surfacePasteBrushButton, camera.surfacePasteBrushModifiers},
			std::tuple{QStringLiteral("camera.surfaceWrapFace"), camera.surfaceWrapFaceButton, camera.surfaceWrapFaceModifiers}}) {
			const auto& [older, oldButton, oldModifiers] = entry;
			if ((overrides.contains(older + QLatin1String("Button")) || overrides.contains(older + QLatin1String("Modifiers")))
				&& button == oldButton && modifiers == oldModifiers) {
				button = Qt::NoButton; cleaned.insert(prefix + QLatin1String("Button"), QStringLiteral("none")); return;
			}
		}
	};
	preserveEarlierSurface(QStringLiteral("camera.surfaceValues"), candidate.camera.surfaceValuesButton, candidate.camera.surfaceValuesModifiers);
	preserveEarlierSurface(QStringLiteral("camera.surfaceValuesOnly"), candidate.camera.surfaceValuesOnlyButton, candidate.camera.surfaceValuesOnlyModifiers);
	preserveEarlierSurface(QStringLiteral("camera.surfaceWrapOnly"), candidate.camera.surfaceWrapOnlyButton, candidate.camera.surfaceWrapOnlyModifiers);
	const auto preserveBeforeProject = [&](const QString& prefix, Qt::MouseButton& button, Qt::KeyboardModifiers modifiers) {
		preserveEarlierSurface(prefix, button, modifiers);
		if (button == Qt::NoButton || overrides.contains(prefix + QLatin1String("Button")) || overrides.contains(prefix + QLatin1String("Modifiers"))) { return; }
		const auto& camera = candidate.camera;
		for (const auto& entry : {std::tuple{QStringLiteral("camera.surfaceValues"), camera.surfaceValuesButton, camera.surfaceValuesModifiers},
			std::tuple{QStringLiteral("camera.surfaceValuesOnly"), camera.surfaceValuesOnlyButton, camera.surfaceValuesOnlyModifiers},
			std::tuple{QStringLiteral("camera.surfaceWrapOnly"), camera.surfaceWrapOnlyButton, camera.surfaceWrapOnlyModifiers}}) {
			const auto& [older, oldButton, oldModifiers] = entry;
			if ((overrides.contains(older + QLatin1String("Button")) || overrides.contains(older + QLatin1String("Modifiers")))
				&& button == oldButton && modifiers == oldModifiers) {
				button = Qt::NoButton; cleaned.insert(prefix + QLatin1String("Button"), QStringLiteral("none")); return;
			}
		}
	};
	preserveBeforeProject(QStringLiteral("camera.surfaceProject"), candidate.camera.surfaceProjectButton, candidate.camera.surfaceProjectModifiers);
	preserveBeforeProject(QStringLiteral("camera.surfaceProjectOnly"), candidate.camera.surfaceProjectOnlyButton, candidate.camera.surfaceProjectOnlyModifiers);
	const auto& p = candidate.plan; const auto& c = candidate.camera;
	auto problems = levelEditorControlProblems(candidate);
	problems.append(cameraKeyProblems(candidate.camera));
	if ((c.fastModifiers & c.slowModifiers) != Qt::NoModifier) {
		problems << QCoreApplication::translate("LevelGestures", "Fast and slow flight must use different modifiers.");
	}
	if (p.squareModifiers != Qt::NoModifier && p.squareModifiers == p.cubeModifiers) {
		problems << QCoreApplication::translate("LevelGestures", "Square and cube brush constraints must use different modifiers.");
	}
	if (p.middleButtonDrivesCamera && (p.panButtons.contains(Qt::MiddleButton) || p.zoomDragButton == Qt::MiddleButton)) {
		problems << QCoreApplication::translate("LevelGestures", "The middle button cannot navigate the plan and drive the camera at the same time.");
	}
	if (!p.plainClickSelects && p.toggleModifiers == Qt::NoModifier && p.addModifiers == Qt::NoModifier && p.cycleModifiers == Qt::NoModifier) {
		problems << QCoreApplication::translate("LevelGestures", "The plan needs a click gesture that can select objects.");
	}
	if (p.cycleModifiers != Qt::NoModifier && (p.cycleModifiers == p.toggleModifiers || p.cycleModifiers == p.addModifiers || p.cycleModifiers == p.bandModifiers)) {
		problems << QCoreApplication::translate("LevelGestures", "Cycle selection conflicts with another plan selection gesture.");
	}
	if (!c.plainClickSelects && c.toggleModifiers == Qt::NoModifier) {
		problems << QCoreApplication::translate("LevelGestures", "The camera needs a click gesture that can select objects.");
	}
	if (c.perspective && c.lookClickToggles && c.lookModifiers != Qt::NoModifier) {
		problems << QCoreApplication::translate("LevelGestures", "Click-to-look uses an unmodified button; use drag-to-look for modifier bindings.");
	}
	if (c.perspective && c.lookButton != Qt::NoButton && !c.lookClickToggles) {
		for (const auto button : c.panButtons) {
			if (cameraNavigationDrag(c, button, c.panModifiers) != CameraNavigationDrag::Pan) {
				problems << QCoreApplication::translate("LevelGestures", "Camera look or orbit shadows the chosen pan gesture."); break;
			}
		}
	}
	if (!problems.isEmpty()) { return fail(error, problems.join(QLatin1Char('\n'))); }
	if (result) { *result = candidate; }
	if (normalized) { *normalized = cleaned; }
	return true;
}

QByteArray serializeLevelGestures(const QString& profile, const QJsonObject& overrides)
{
	return QJsonDocument(QJsonObject{{"format", "vibestudio.editor-gestures"}, {"version", 1},
		{"profile", normalizedEditorProfileId(profile)}, {"overrides", overrides}}).toJson();
}

bool parseLevelGestures(const QByteArray& bytes, QString* profile, QJsonObject* overrides, QString* error)
{
	if (error) { error->clear(); }
	if (bytes.size() > maximumBytes) { return fail(error, QCoreApplication::translate("LevelGestures", "A gesture file must be no larger than 64 KiB.")); }
	QJsonParseError parseError;
	const auto document = QJsonDocument::fromJson(bytes, &parseError);
	const auto object = document.object();
	if (parseError.error != QJsonParseError::NoError || !document.isObject() || object.size() != 4
		|| object.value("format") != QStringLiteral("vibestudio.editor-gestures") || object.value("version") != QJsonValue(1)
		|| !object.value("profile").isString() || !object.value("overrides").isObject()) {
		return fail(error, QCoreApplication::translate("LevelGestures", "Expected a version 1 VibeStudio gesture file with a profile and overrides."));
	}
	EditorProfileDescriptor base;
	if (!editorProfileForId(object.value("profile").toString(), &base)) { return fail(error, QCoreApplication::translate("LevelGestures", "The gesture file names an unknown editor profile.")); }
	QJsonObject cleaned;
	if (!applyLevelGestureOverrides(base.controls, object.value("overrides").toObject(), nullptr, &cleaned, error)) { return false; }
	if (profile) { *profile = base.id; }
	if (overrides) { *overrides = cleaned; }
	return true;
}

bool readLevelGestures(const QString& path, QString* profile, QJsonObject* overrides, QString* error)
{
	QFile input(path);
	if (!input.open(QIODevice::ReadOnly)) { return fail(error, input.errorString()); }
	return parseLevelGestures(input.read(maximumBytes + 1), profile, overrides, error);
}

bool writeLevelGestures(const QString& path, const QString& profile, const QJsonObject& overrides, bool overwrite, QString* error,
	const QString& settingsPath)
{
	QString canonical; QJsonObject cleaned;
	if (!parseLevelGestures(serializeLevelGestures(profile, overrides), &canonical, &cleaned, error)) { return false; }
	if (!settingsPath.isEmpty()) {
		const auto resolved = [](const QString& filename) {
			const QFileInfo info(filename);
			const auto target = info.canonicalFilePath();
			return target.isEmpty() ? info.absoluteFilePath() : target;
		};
#ifdef Q_OS_WIN
		const auto sensitivity = Qt::CaseInsensitive;
#else
		const auto sensitivity = Qt::CaseSensitive;
#endif
		if (resolved(path).compare(resolved(settingsPath), sensitivity) == 0) {
			return fail(error, QCoreApplication::translate("LevelGestures", "Choose a gesture output separate from the settings store."));
		}
	}
	if (QFileInfo::exists(path) && !overwrite) { return fail(error, QCoreApplication::translate("LevelGestures", "The gesture output already exists. Choose another file or allow overwrite.")); }
	QSaveFile output(path);
	const auto bytes = serializeLevelGestures(canonical, cleaned);
	if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) { return fail(error, output.errorString()); }
	return true;
}
} // namespace vibestudio
