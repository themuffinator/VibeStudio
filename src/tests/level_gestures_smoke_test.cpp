#include "core/editor_profiles.h"
#include "core/level_gestures.h"
#include "core/level_camera_keys.h"
#include "core/studio_settings.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* message, const QString& detail = {})
{
	if (!condition) { std::cerr << message << ": " << detail.toStdString() << '\n'; }
	return condition;
}
QByteArray bytes(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return {}; }
	return file.readAll();
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp; if (!temp.isValid()) { return EXIT_FAILURE; }
	const auto settingsPath = temp.filePath(QStringLiteral("settings.ini"));
	StudioSettings::setOverrideFilePath(settingsPath);
	StudioSettings settings;
	bool ok = true; QString error;
	for (const auto& profile : editorProfileDescriptors()) {
		QJsonObject normalized; LevelEditorControls effective;
		const auto values = levelGestureValues(profile.controls);
		ok &= expect(values.size() == levelGestureFields().size(), "every profile exposes every supported field", profile.id);
		ok &= expect(applyLevelGestureOverrides(profile.controls, values, &effective, &normalized, &error), "base profile is valid", profile.id + error);
		ok &= expect(normalized.isEmpty() && levelGestureValues(effective) == values, "unchanged values normalize to defaults", profile.id);
	}
	const auto base = netRadiantCustomLevelControls();
	const QJsonObject changes {{"camera.lookClickToggles", "off"}, {"camera.panButtons", "middle"}, {"camera.materialSampleButton", "none"},
		{"plan.panButtons", "middle"}, {"plan.middleButtonDrivesCamera", "off"}};
	LevelEditorControls effective;
	ok &= expect(applyLevelGestureOverrides(base, changes, &effective, nullptr, &error), "valid mixed gesture transaction", error);
	ok &= expect(cameraNavigationDrag(effective.camera, Qt::RightButton, Qt::NoModifier) == CameraNavigationDrag::Look
		&& cameraNavigationDrag(effective.camera, Qt::RightButton, Qt::ShiftModifier) == CameraNavigationDrag::Look
		&& cameraNavigationDrag(effective.camera, Qt::RightButton, Qt::AltModifier) == CameraNavigationDrag::Orbit
		&& cameraNavigationDrag(effective.camera, Qt::MiddleButton, Qt::NoModifier) == CameraNavigationDrag::Pan, "effective camera routing preserves specific gestures");
	const auto validValues = levelGestureValues(effective);
	// A pre-material-gesture preference file can already use the middle button
	// for navigation. Loading it must preserve that customization, not fall back.
	auto legacy = changes; legacy.remove("camera.materialSampleButton");
	QJsonObject migrated; LevelEditorControls migratedControls;
	ok &= expect(applyLevelGestureOverrides(base, legacy, &migratedControls, &migrated, &error)
		&& migrated == changes && levelGestureValues(migratedControls) == validValues, "legacy navigation takes priority over inherited material sampling", error);
	QString migratedProfile; QJsonObject migratedImport;
	ok &= expect(parseLevelGestures(serializeLevelGestures("netradiant-custom", legacy), &migratedProfile, &migratedImport, &error)
		&& migratedImport == changes, "legacy portable import records disabled material default", error);
	for (const auto& profile : editorProfileDescriptors()) {
		const auto& camera = profile.controls.camera;
		const bool samples = QStringList{"q3radiant", "gtkradiant-1-6", "netradiant", "netradiant-custom"}.contains(profile.id);
		const bool paints = profile.id == "q3radiant" || profile.id == "gtkradiant-1-6";
		ok &= expect((cameraMaterialGesture(camera, Qt::MiddleButton, Qt::NoModifier) == CameraMaterialGesture::Sample) == samples
			&& (cameraMaterialGesture(camera, Qt::MiddleButton, Qt::ShiftModifier) == CameraMaterialGesture::Paint) == paints,
			"audited material defaults are independent from other profiles", profile.id);
		ok &= expect(cameraMaterialGesture(camera, Qt::NoButton, Qt::NoModifier) == CameraMaterialGesture::None
			&& cameraMaterialGesture(camera, Qt::MiddleButton, Qt::ControlModifier) == (paints ? CameraMaterialGesture::PasteBrush : profile.id == "netradiant-custom" ? CameraMaterialGesture::WrapFace : CameraMaterialGesture::None)
			&& cameraMaterialGesture(camera, Qt::MiddleButton, Qt::ShiftModifier | Qt::ControlModifier) == (paints ? CameraMaterialGesture::PasteFace : profile.id == "netradiant-custom" ? CameraMaterialGesture::ProjectSelection : CameraMaterialGesture::None),
			"surface-paste defaults are confined to their audited profiles", profile.id);
		const bool custom = profile.id == "netradiant-custom";
		ok &= expect(cameraMaterialGesture(camera, Qt::MiddleButton, Qt::AltModifier | Qt::ShiftModifier) == (custom ? CameraMaterialGesture::ValuesSelectionOnly : CameraMaterialGesture::None)
			&& cameraMaterialGesture(camera, Qt::MiddleButton, Qt::AltModifier | Qt::ControlModifier) == (custom ? CameraMaterialGesture::WrapFaceOnly : CameraMaterialGesture::None)
			&& cameraMaterialGesture(camera, Qt::MiddleButton, Qt::AltModifier | Qt::ControlModifier | Qt::ShiftModifier) == (custom ? CameraMaterialGesture::ProjectSelectionOnly : CameraMaterialGesture::None)
			&& (!custom || cameraMaterialGesture(camera, Qt::MiddleButton, Qt::ShiftModifier) == CameraMaterialGesture::ValuesSelection), "Custom selected-value and mapping-only defaults", profile.id);
	}
	const auto materialBase = gtkRadiantLevelControls();
	for (const QJsonObject& invalid : {QJsonObject{{"camera.materialSampleModifiers", "shift"}},
		QJsonObject{{"camera.materialPaintButton", "left"}}, QJsonObject{{"camera.materialSampleButton", "right"}},
		QJsonObject{{"camera.panButtons", "middle"}, {"camera.materialSampleButton", "middle"}}}) {
		ok &= expect(!applyLevelGestureOverrides(materialBase, invalid, nullptr, nullptr, &error), "explicit ambiguous material gesture rejected", error);
	}
	LevelEditorControls alternate;
	ok &= expect(applyLevelGestureOverrides(materialBase, {{"camera.materialSampleModifiers", "alt"}, {"camera.materialPaintModifiers", "ctrl+shift"}}, &alternate, nullptr, &error)
		&& cameraMaterialGesture(alternate.camera, Qt::MiddleButton, Qt::AltModifier) == CameraMaterialGesture::Sample
		&& cameraMaterialGesture(alternate.camera, Qt::MiddleButton, Qt::ShiftModifier | Qt::ControlModifier) == CameraMaterialGesture::Paint
		&& cameraMaterialGesture(alternate.camera, Qt::MiddleButton, Qt::AltModifier | Qt::MetaModifier) == CameraMaterialGesture::None,
		"custom chords match exactly", error);
	QJsonObject pasteMigration;
	ok &= expect(applyLevelGestureOverrides(base, {{"camera.surfaceValuesModifiers", "ctrl+shift"}}, &alternate, &pasteMigration, &error)
		&& alternate.camera.surfaceProjectButton == Qt::NoButton && pasteMigration.value("camera.surfaceProjectButton") == "none",
		"existing value binding wins over inherited projection", error);
	ok &= expect(!applyLevelGestureOverrides(base, {{"camera.surfaceValuesModifiers", "ctrl+shift"}, {"camera.surfaceProjectButton", "middle"}}, nullptr, nullptr, &error),
		"explicit value and projection collision rejected");
	ok &= expect(applyLevelGestureOverrides(base, {{"camera.surfaceWrapOnlyModifiers", "ctrl+shift+alt"}}, &alternate, &pasteMigration, &error)
		&& alternate.camera.surfaceProjectOnlyButton == Qt::NoButton, "old wrap-only binding wins over mapping-only projection", error);
	ok &= expect(applyLevelGestureOverrides(base, {{"camera.surfacePasteFaceButton", "middle"}, {"camera.surfacePasteFaceModifiers", "shift"}}, &alternate, &pasteMigration, &error)
		&& alternate.camera.surfaceValuesButton == Qt::NoButton && pasteMigration.value("camera.surfaceValuesButton") == "none", "older face-paste preference wins over new selection default", error);
	ok &= expect(!applyLevelGestureOverrides(base, {{"camera.surfacePasteFaceButton", "middle"}, {"camera.surfacePasteFaceModifiers", "shift"},
		{"camera.surfaceValuesButton", "middle"}}, nullptr, nullptr, &error), "explicit selected-value collision rejected");
	ok &= expect(cameraMaterialGesture(familiarLevelControls("netradiant").camera, Qt::MiddleButton, Qt::ShiftModifier) == CameraMaterialGesture::PasteFace,
		"ordinary NetRadiant pastes parameters rather than material-only paint");
	ok &= expect(applyLevelGestureOverrides(base, {{"camera.surfacePasteFaceButton", "middle"}, {"camera.surfacePasteFaceModifiers", "ctrl"}}, &alternate, &pasteMigration, &error)
		&& alternate.camera.surfaceWrapFaceButton == Qt::NoButton && pasteMigration.value("camera.surfaceWrapFaceButton") == "none",
		"older parameter paste wins over the new wrap default", error);
	ok &= expect(!applyLevelGestureOverrides(base, {{"camera.surfacePasteFaceButton", "middle"}, {"camera.surfacePasteFaceModifiers", "ctrl"},
		{"camera.surfaceWrapFaceButton", "middle"}}, nullptr, nullptr, &error), "explicit wrap and paste collision is rejected");
	ok &= expect(applyLevelGestureOverrides(base, {{"camera.surfaceWrapFaceModifiers", "alt"}}, &alternate, &pasteMigration, &error)
		&& cameraMaterialGesture(alternate.camera, Qt::MiddleButton, Qt::AltModifier) == CameraMaterialGesture::WrapFace
		&& cameraMaterialGesture(alternate.camera, Qt::MiddleButton, Qt::ControlModifier) == CameraMaterialGesture::None, "wrap is exactly remappable", error);
	ok &= expect(applyLevelGestureOverrides(materialBase, {{"camera.materialPaintModifiers", "ctrl+shift"}}, &alternate, &pasteMigration, &error)
		&& alternate.camera.surfacePasteFaceButton == Qt::NoButton && pasteMigration.value("camera.surfacePasteFaceButton") == "none"
		&& alternate.camera.materialPaintModifiers == (Qt::ControlModifier | Qt::ShiftModifier), "older explicit painting preserves its chord", error);
	ok &= expect(applyLevelGestureOverrides(materialBase, {{"camera.panButtons", "middle"}, {"camera.panModifiers", "ctrl"}}, &alternate, &pasteMigration, &error)
		&& alternate.camera.surfacePasteBrushButton == Qt::NoButton && pasteMigration.value("camera.surfacePasteBrushButton") == "none", "older navigation wins over new paste defaults", error);
	for (const auto& invalid : {QJsonObject{{"camera.surfacePasteFaceModifiers", "shift"}},
		QJsonObject{{"camera.surfacePasteBrushButton", "left"}}, QJsonObject{{"camera.surfacePasteFaceModifiers", "ctrl"}}}) {
		ok &= expect(!applyLevelGestureOverrides(materialBase, invalid, nullptr, nullptr, &error), "explicit surface-paste conflict rejected");
	}
	for (const QJsonObject& invalid : {QJsonObject{{"plan.panButtons", "middle"}}, QJsonObject{{"camera.lookClickToggles", "off"}},
		QJsonObject{{"camera.lookModifiers", "shift"}}, QJsonObject{{"unknown", "on"}}, QJsonObject{{"plan.panButtons", "left"}},
		QJsonObject{{"plan.emptyDrag", true}}, QJsonObject{{"plan.toggleModifiers", "ctrl"}, {"plan.addModifiers", "ctrl"}}}) {
		ok &= expect(!applyLevelGestureOverrides(base, invalid, &effective, nullptr, &error) && !error.isEmpty(), "invalid gestures rejected");
		ok &= expect(levelGestureValues(effective) == validValues, "rejected batch leaves result unchanged");
	}
	ok &= expect(!applyLevelGestureOverrides(base, {{"camera.slowModifiers", "shift"}}, nullptr, nullptr, &error), "flight speed ambiguity rejected");
	ok &= expect(!applyLevelGestureOverrides(vibeStudioLevelControls(), {{"camera.lookButton", "right"}}, nullptr, nullptr, &error), "inactive perspective gesture rejected");
	const QJsonObject cameraKeys {{"camera.flyKeys.forward", "i"}, {"camera.flyKeys.back", "K"},
		{"camera.flyKeys.left", "J"}, {"camera.flyKeys.right", "L"}, {"camera.flyKeys.up", "U"}, {"camera.flyKeys.down", "O"},
		{"camera.driveKeys.up", "PgUp"}, {"camera.lookToggleKey", "Ctrl+Space"}};
	EditorProfileDescriptor hammer; editorProfileForId("hammer", &hammer);
	LevelEditorControls keyed; QJsonObject normalizedKeys;
	ok &= expect(levelGestureFields().size() == 84 && applyLevelGestureOverrides(hammer.controls, cameraKeys, &keyed, &normalizedKeys, &error), "84 shared pointer and key preferences", error);
	ok &= expect(normalizedKeys.value("camera.flyKeys.forward") == "I", "movement key uses portable canonical spelling");
	ok &= expect(cameraKeyMotions(keyed.camera, false).size() == 1 && cameraKeyMotions(keyed.camera, false).value(Qt::Key_PageUp).up == 1,
		"drive keys stay active before looking");
	const auto heldLook = cameraKeyMotions(keyed.camera, false, keyed.camera.lookButton, keyed.camera.lookModifiers);
	ok &= expect(heldLook.value(Qt::Key_I).forward == 1 && heldLook.value(Qt::Key_K).forward == -1
		&& heldLook.value(Qt::Key_J).right == -1 && heldLook.value(Qt::Key_L).right == 1
		&& heldLook.value(Qt::Key_U).up == 1 && heldLook.value(Qt::Key_O).up == -1 && !heldLook.contains(Qt::Key_W), "custom held-button flight uses every new direction");
	ok &= expect(cameraMotionModifiersAccepted(keyed.camera, Qt::ShiftModifier)
		&& !cameraMotionModifiersAccepted(keyed.camera, Qt::ControlModifier)
		&& cameraLookToggleMatches(keyed.camera, Qt::Key_Space, Qt::ControlModifier), "speed modifiers and look toggle use shared matching");
	for (const auto& bad : {QJsonObject{{"camera.flyKeys.forward", "Esc"}}, QJsonObject{{"camera.driveKeys.forward", "Tab"}},
		QJsonObject{{"camera.flyKeys.forward", "Ctrl+I"}}, QJsonObject{{"camera.flyKeys.forward", "I, J"}},
		QJsonObject{{"camera.flyKeys.back", "W"}}, QJsonObject{{"camera.driveKeys.back", "W"}, {"camera.flyNeedsLook", "off"}},
		QJsonObject{{"camera.lookToggleKey", "Shift+W"}}, QJsonObject{{"camera.lookToggleKey", "Shift"}},
		QJsonObject{{"camera.lookToggleKey", "none"}, {"camera.lookButton", "none"}}}) {
		ok &= expect(!applyLevelGestureOverrides(hammer.controls, bad, nullptr, nullptr, &error), "ambiguous, inaccessible and reserved camera keys rejected");
	}
	LevelEditorControls modal;
	ok &= expect(applyLevelGestureOverrides(hammer.controls, {{"camera.driveKeys.back", "W"}}, &modal, nullptr, &error)
		&& cameraKeyMotions(modal.camera, false).value(Qt::Key_W).forward == -1
		&& cameraKeyMotions(modal.camera, true).value(Qt::Key_W).forward == 1, "modal fly keys override drive keys only while looking", error);
	LevelEditorControls modifiedLook;
	ok &= expect(applyLevelGestureOverrides(hammer.controls, {{"camera.lookModifiers", "ctrl"}}, &modifiedLook, nullptr, &error)
		&& !cameraMotionModifiersAccepted(modifiedLook.camera, Qt::ControlModifier)
		&& cameraMotionModifiersAccepted(modifiedLook.camera, Qt::ControlModifier | Qt::ShiftModifier, modifiedLook.camera.lookButton, Qt::ControlModifier)
		&& !cameraMotionModifiersAccepted(modifiedLook.camera, Qt::ControlModifier, Qt::RightButton, Qt::ControlModifier)
		&& cameraKeyMotions(modifiedLook.camera, false, modifiedLook.camera.lookButton, Qt::ControlModifier).contains(Qt::Key_W),
		"required held-look modifiers allow flight only during that gesture", error);
	ok &= expect(!applyLevelGestureOverrides(hammer.controls, {{"camera.lookModifiers", "ctrl"}, {"camera.lookToggleKey", "Ctrl+W"}}, nullptr, nullptr, &error),
		"toggle cannot consume a key used during modified held look");
	ok &= expect(!levelCameraShortcutWarnings(hammer, keyed, {{"map.selectAll", {"L"}}}).isEmpty(), "command overlap is reported without discarding custom navigation");
	ok &= expect(levelCameraShortcutWarnings(hammer, keyed, {{"map.selectAll", {"L"}}})
		== levelCameraShortcutWarnings(hammer, keyed, {{"map.select-all", {"L"}}}), "command aliases share overlap diagnostics");
	ok &= expect(!levelCameraShortcutWarnings(hammer, keyed, {{"map.selectSimilar", {"L"}}}).isEmpty(), "palette-only map commands participate in overlap diagnostics");
	ok &= expect(!levelCameraShortcutWarnings(hammer, modifiedLook, {{"map.selectAll", {"Ctrl+W"}}}).isEmpty(), "held-look modifiers participate in overlap diagnostics");
	ok &= expect(levelCameraShortcutWarnings(hammer, hammer.controls, {{"package.save-as", {"W"}}}).isEmpty(), "other surface shortcuts are outside the camera scope");
	ok &= expect(levelCameraShortcutWarnings(hammer, hammer.controls).isEmpty(), "unchanged built-in overlaps are not new warnings");
	const auto radiant = gtkRadiantLevelControls().camera;
	ok &= expect(cameraKeyMotions(radiant, false).value(Qt::Key_Left).turn == 1
		&& cameraKeyMotions(radiant, true).value(Qt::Key_Left).right == -1
		&& cameraKeyMotions(radiant, true).value(Qt::Key_Left).turn == 0, "Radiant free-look arrows strafe despite duplicate fly bindings");
	ok &= expect(cameraKeyMotions(familiarLevelControls("quark").camera, true).value(Qt::Key_D).up == 1, "QuArK vertical drive remains available in free look");
	const auto sledge = familiarLevelControls("sledge");
	const auto classic = familiarLevelControls("q3radiant");
	ok &= expect(classic.layout == LevelViewLayout::CameraAndPlan && classic.defaultGridUnits == 8
		&& classic.plan.fixedCameraSteps && classic.camera.discreteDriveKeys && classic.camera.flyKeys.isEmpty()
		&& cameraNavigationDrag(classic.camera, Qt::RightButton, Qt::NoModifier) == CameraNavigationDrag::Drive
		&& cameraNavigationDrag(classic.camera, Qt::RightButton, Qt::ControlModifier) == CameraNavigationDrag::Pan,
		"Q3Radiant retains separate classic steering, pan and fixed camera steps");
	ok &= expect(cameraKeyUsesFixedStep(classic.camera, Qt::Key_Up, false)
		&& !cameraKeyUsesFixedStep(classic.camera, Qt::Key_Up, false, Qt::NoButton, Qt::NoModifier, Qt::ControlModifier)
		&& !cameraKeyUsesFixedStep(radiant, Qt::Key_Up, false), "only accepted classic drive keys use fixed steps");
	auto customStepped = radiant; customStepped.discreteDriveKeys = true; customStepped.lookClickToggles = false;
	ok &= expect(cameraKeyUsesFixedStep(customStepped, Qt::Key_Up, false)
		&& !cameraKeyUsesFixedStep(customStepped, Qt::Key_Up, true)
		&& !cameraKeyUsesFixedStep(customStepped, Qt::Key_Up, false, customStepped.lookButton, customStepped.lookModifiers),
		"toggled and held fly bindings retain continuous motion over a stepped drive binding");
	customStepped.flyNeedsLook = false;
	ok &= expect(!cameraKeyUsesFixedStep(customStepped, Qt::Key_Up, false), "always-active fly bindings retain continuous motion");
	auto heldRadiant = gtkRadiantLevelControls(); heldRadiant.camera.lookClickToggles = false;
	ok &= expect(!applyLevelGestureOverrides(heldRadiant, {{"camera.driveButton", "right"}, {"camera.driveModifiers", "shift"}}, nullptr, nullptr, &error),
		"steering cannot shadow a held-look speed modifier");
	ok &= expect(!applyLevelGestureOverrides(gtkRadiantLevelControls(), {{"camera.driveButton", "right"}, {"camera.driveModifiers", "shift"}}, nullptr, nullptr, &error),
		"steering cannot share a button whose clicks toggle mouse look");
	const auto classicKeys = cameraKeyMotions(classic.camera, false);
	ok &= expect(classicKeys.value(Qt::Key_Up).forward == 1 && classicKeys.value(Qt::Key_Comma).right == -1
		&& classicKeys.value(Qt::Key_Period).right == 1 && classicKeys.value(Qt::Key_D).up == 1
		&& classicKeys.value(Qt::Key_C).up == -1 && classicKeys.value(Qt::Key_A).pitch == 1
		&& classicKeys.value(Qt::Key_Z).pitch == -1 && classicKeys.value(Qt::Key_Left).turn == 1,
		"classic camera movement follows the audited command table");
	ok &= expect(planCameraStep(classic.plan, Qt::Key_Up, Qt::NoModifier, 256) == QPointF(32, 0)
		&& planCameraStep(classic.plan, Qt::Key_Right, Qt::NoModifier, 1) == QPointF(0, -22.5)
		&& planCameraStep(classic.plan, Qt::Key_Up, Qt::ShiftModifier, 8).isNull()
		&& planCameraStep(classic.plan, Qt::Key_Up, Qt::ControlModifier, 8).isNull(), "classic plan steps are grid independent and preserve texture chords");
	for (const auto& invalid : {QJsonObject{{"camera.lookButton", "right"}}, QJsonObject{{"camera.panModifiers", "none"}},
		QJsonObject{{"camera.driveButton", "left"}}}) {
		ok &= expect(!applyLevelGestureOverrides(classic, invalid, nullptr, nullptr, &error), "ambiguous position steering rejected");
	}
	LevelEditorControls customClassic; QJsonObject classicChanges;
	ok &= expect(applyLevelGestureOverrides(classic, {{"camera.driveButton", "middle"}, {"camera.driveModifiers", "alt"},
		{"camera.discreteDriveKeys", "off"}, {"plan.fixedCameraSteps", "off"}}, &customClassic, &classicChanges, &error)
		&& cameraNavigationDrag(customClassic.camera, Qt::MiddleButton, Qt::AltModifier) == CameraNavigationDrag::Drive,
		"all new navigation preferences share validated overrides", error);
	QString classicProfile; QJsonObject classicParsed;
	ok &= expect(parseLevelGestures(serializeLevelGestures("quake-iii-radiant", classicChanges), &classicProfile, &classicParsed, &error)
		&& classicProfile == "q3radiant" && classicParsed == classicChanges, "classic profile aliases and steering preferences roundtrip", error);
	const auto sledgeIdle = cameraKeyMotions(sledge.camera, false);
	const auto sledgeLook = cameraKeyMotions(sledge.camera, true);
	const auto sledgeShift = cameraKeyMotions(sledge.camera, false, Qt::NoButton, Qt::NoModifier, Qt::ShiftModifier);
	ok &= expect(sledgeIdle.value(Qt::Key_W).forward == 1 && sledgeIdle.value(Qt::Key_Q).up == 1 && sledgeIdle.value(Qt::Key_E).up == -1
		&& sledgeIdle.value(Qt::Key_Up).pitch == 1 && sledgeIdle.value(Qt::Key_Left).turn == 1
		&& sledgeLook.value(Qt::Key_Left).turn == 1 && sledgeLook.value(Qt::Key_Up).pitch == 1, "Sledge arrows rotate in both modes while Q/E move vertically");
	ok &= expect(sledgeShift.value(Qt::Key_Left).right == -1 && sledgeShift.value(Qt::Key_Up).up == 1
		&& !sledgeShift.contains(Qt::Key_Q), "Shift arrows translate without consuming Shift+Q deselection outside look");
	ok &= expect(cameraKeyMotions(sledge.camera, true, Qt::NoButton, Qt::NoModifier, Qt::ControlModifier).contains(Qt::Key_W)
		&& !cameraKeyMotions(sledge.camera, false, Qt::NoButton, Qt::NoModifier, Qt::ControlModifier).contains(Qt::Key_W)
		&& !cameraKeyMotions(sledge.camera, true, Qt::NoButton, Qt::NoModifier, Qt::ControlModifier).contains(Qt::Key_Up), "Sledge speed and arrow modifiers retain separate scopes");
	ok &= expect(cameraFreeLookMotion(sledge.camera, Qt::RightButton, Qt::NoModifier) == CameraFreeLookMotion::Pan
		&& cameraFreeLookMotion(sledge.camera, Qt::RightButton | Qt::LeftButton, Qt::NoModifier) == CameraFreeLookMotion::Dolly
		&& cameraFreeLookMotion(sledge.camera, Qt::NoButton, Qt::ControlModifier) == CameraFreeLookMotion::Look
		&& cameraFreeLookMotion(radiant, Qt::NoButton, Qt::ControlModifier) == CameraFreeLookMotion::Pan, "Sledge mouse buttons and Radiant modifiers route independently");
	ok &= expect(planArrowPanDirection(sledge.plan, Qt::Key_Up, Qt::ShiftModifier) == QPoint(0, 1)
		&& planArrowPanDirection(sledge.plan, Qt::Key_Left, Qt::ControlModifier).isNull()
		&& planArrowPanDirection(hammer.controls.plan, Qt::Key_Up, Qt::ShiftModifier).isNull(), "quarter-view panning is profile scoped");
	ok &= expect(navigationHoldKeyMatches(sledge.plan.panHoldKey, Qt::Key_Space, Qt::NoModifier)
		&& !navigationHoldKeyMatches(sledge.plan.panHoldKey, Qt::Key_Space, Qt::ShiftModifier), "hold navigation requires the exact unmodified key");
	for (const QJsonObject& invalid : {QJsonObject{{"camera.lookHoldKey", "Z"}}, QJsonObject{{"camera.lookHoldKey", "W"}},
		QJsonObject{{"camera.lookHoldKey", "Ctrl+Space"}}, QJsonObject{{"plan.panHoldKey", "Tab"}}, QJsonObject{{"camera.driveKeys.pitchUp", "Left"}}}) {
		ok &= expect(!applyLevelGestureOverrides(sledge, invalid, nullptr, nullptr, &error), "invalid hold and pitch assignments rejected");
	}
	const QJsonObject sledgeChanges {{"plan.panHoldKey", "P"}, {"camera.lookHoldKey", "P"}, {"camera.driveKeys.pitchUp", "PgUp"}};
	ok &= expect(settings.setEditorGestureOverrides("sledge-editor", sledgeChanges, &error)
		&& StudioSettings().editorGestureOverrides("sledge") == sledgeChanges, "new hold and pitch preferences persist under canonical aliases", error);
	ok &= expect(settings.editorGestureOverrides("netradiant").isEmpty(), "standalone Radiant has an independent preference store");
	EditorProfileDescriptor sledgeProfile; editorProfileForId("sledge", &sledgeProfile);
	ok &= expect(!levelCameraShortcutWarnings(sledgeProfile, sledge, {{"map.selectAll", {"Space"}}}).isEmpty()
		&& !levelCameraShortcutWarnings(sledgeProfile, sledge, {{"map.selectAll", {"Shift+Up"}}}).isEmpty(), "temporary pan and arrow pan participate in shortcut diagnostics");
	QString importedKeyProfile; QJsonObject importedKeys;
	ok &= expect(parseLevelGestures(serializeLevelGestures("hammer", normalizedKeys), &importedKeyProfile, &importedKeys, &error)
		&& importedKeyProfile == "hammer" && importedKeys == normalizedKeys, "camera keys use existing portable gesture document", error);
	ok &= expect(settings.setEditorGestureOverrides("netradiant-custom", changes, &error), "preferences persist", error);
	ok &= expect(levelGestureValues(StudioSettings().effectiveLevelEditorControls("netradiant-custom")) == validValues, "fresh settings use overrides");
	ok &= expect(settings.editorGestureOverrides("hammer").isEmpty(), "other profile unchanged");
	const auto savedSettings = bytes(settingsPath);
	ok &= expect(!settings.setEditorGestureOverrides("netradiant-custom", {{"camera.panButtons", "left"}}, &error)
		&& bytes(settingsPath) == savedSettings, "rejected settings leave bytes unchanged");
	const auto output = temp.filePath(QStringLiteral("gestures.json"));
	ok &= expect(writeLevelGestures(output, "netradiant-custom", changes, false, &error), "portable export", error);
	QString profile; QJsonObject imported;
	ok &= expect(readLevelGestures(output, &profile, &imported, &error) && profile == "netradiant-custom" && imported == changes, "portable round trip", error);
	const auto savedFile = bytes(output);
	ok &= expect(!writeLevelGestures(output, "hammer", {}, false, &error) && bytes(output) == savedFile, "export refuses unapproved overwrite");
	auto malformed = QJsonDocument::fromJson(savedFile).object(); malformed.insert("version", 2);
	ok &= expect(!parseLevelGestures(QJsonDocument(malformed).toJson(), &profile, &imported, &error) && imported == changes, "unknown version rejected atomically");
	ok &= expect(!parseLevelGestures(QByteArray(65537, ' '), nullptr, nullptr, &error), "bounded gesture import");
	ok &= expect(settings.setEditorGestureOverrides("hammer++", {{"plan.panButtons", "right+middle"}}, &error)
		&& settings.editorGestureOverrides("hammer").value("plan.panButtons") == "right+middle", "aliases share canonical settings", error);
	StudioSettings readOnly(settingsPath, StudioSettings::AccessMode::ReadOnly);
	ok &= expect(!readOnly.setEditorGestureOverrides("hammer", {}, &error), "read-only write rejected");
	if (argc > 1) {
		const auto binary = QString::fromLocal8Bit(argv[1]);
		auto cli = [&](const QStringList& arguments, int expected, const QString& store = QString()) {
			QProcess process; process.setWorkingDirectory(temp.path());
			process.start(binary, QStringList{"--cli", "--settings-file", store.isEmpty() ? settingsPath : store, "editor"} + arguments + QStringList{"--json"});
			const bool finished = process.waitForFinished(30000);
			const auto stdoutBytes = process.readAllStandardOutput();
			ok &= expect(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected,
				"CLI exit", QString::fromUtf8(stdoutBytes + process.readAllStandardError()));
			return QJsonDocument::fromJson(stdoutBytes).object();
		};
		cli({"gestures", "hammer", "--reset"}, 0);
		const auto before = bytes(settingsPath);
		auto result = cli({"gestures", "hammer++", "--set", "plan.panButtons=right+middle", "--dry-run"}, 0);
		ok &= expect(result.value("dryRun").toBool() && !result.value("applied").toBool() && bytes(settingsPath) == before, "CLI dry run writes nothing");
		cli({"gestures", "hammer", "--set", "plan.panButtons=right+middle"}, 0);
		ok &= expect(cli({"controls", "hammer"}, 0).value("gestureOverrides").toObject().value("plan.panButtons") == "right+middle", "controls reference uses effective preferences");
		const auto after = bytes(settingsPath);
		cli({"gestures", "hammer", "--set", "plan.panButtons=left"}, 4);
		cli({"gestures", "hammer", "--set", "plan.panButtons=none", "--set", "plan.panButtons=middle"}, 2);
		cli({"gestures", "hammer", "--set", "unknown=default"}, 2);
		cli({"gestures", "hammer", "--input", output}, 2);
		cli({"gestures", "hammer", "extra"}, 2); cli({"gestures", "hammer", "--typo"}, 2);
		cli({"gestures", "hammer", "--output", settingsPath, "--overwrite"}, 4);
#ifdef Q_OS_WIN
		cli({"gestures", "hammer", "--output", settingsPath.toUpper(), "--overwrite"}, 4);
#endif
		ok &= expect(bytes(settingsPath) == after, "bad CLI requests preserve settings");
		cli({"gestures", "hammer", "--set", "plan.panButtons=default"}, 0);
		ok &= expect(cli({"gestures", "hammer"}, 0).value("overrides").toObject().isEmpty(), "single-field reset follows default");
		settings.setUserShortcuts({{"map.selectAll", {"I"}}}); settings.sync();
		const auto keysBefore = bytes(settingsPath);
		cli({"gestures", "hammer", "--set", "camera.flyKeys.forward=I", "--set", "camera.lookToggleKey=Ctrl+Space", "--dry-run"}, 0);
		ok &= expect(bytes(settingsPath) == keysBefore, "camera key preview preserves settings");
		auto keyResult = cli({"gestures", "hammer", "--set", "camera.flyKeys.forward=I", "--set", "camera.lookToggleKey=Ctrl+Space"}, 0);
		const auto materialResult = cli({"gestures", "q3radiant", "--set", "camera.materialSampleModifiers=alt", "--set", "camera.materialPaintModifiers=ctrl+shift"}, 0);
		ok &= expect(materialResult.value("values").toObject().value("camera.materialSampleModifiers") == "alt"
			&& materialResult.value("values").toObject().value("camera.materialPaintModifiers") == "ctrl+shift", "CLI persists exact material chords");
		const auto materialSettings = bytes(settingsPath);
		cli({"gestures", "q3radiant", "--set", "camera.materialPaintModifiers=alt"}, 4);
		ok &= expect(bytes(settingsPath) == materialSettings, "CLI rejects conflicting material actions atomically");
		cli({"gestures", "q3radiant", "--reset"}, 0);
		ok &= expect(keyResult.value("values").toObject().value("camera.flyKeys.forward") == "I", "CLI applies camera key transaction");
		ok &= expect(!keyResult.value("shortcutWarnings").toArray().isEmpty(), "CLI reports camera overlaps with saved command keys");
		QHash<QString, QString> fieldTypes;
		for (const auto& field : keyResult.value("fields").toArray()) { const auto item = field.toObject(); fieldTypes.insert(item.value("id").toString(), item.value("type").toString()); }
		ok &= expect(fieldTypes.size() == 84 && fieldTypes.value("plan.panButtons") == "choice"
			&& fieldTypes.value("camera.flyKeys.forward") == "motion-key" && fieldTypes.value("camera.lookToggleKey") == "toggle-key"
			&& fieldTypes.value("plan.panHoldKey") == "hold-key" && fieldTypes.value("camera.lookHoldKey") == "hold-key", "CLI describes all field types");
		const auto keyedBytes = bytes(settingsPath);
		cli({"gestures", "hammer", "--set", "camera.flyKeys.back=I"}, 4);
		ok &= expect(bytes(settingsPath) == keyedBytes, "duplicate CLI direction preserves previous bindings");
		cli({"gestures", "hammer", "--set", "camera.flyKeys.forward=none"}, 0);
		ok &= expect(cli({"gestures", "hammer"}, 0).value("values").toObject().value("camera.flyKeys.forward") == "none", "CLI disables a direction explicitly");
		cli({"gestures", "hammer", "--set", "camera.flyKeys.forward=default"}, 0);
		ok &= expect(cli({"gestures", "hammer"}, 0).value("values").toObject().value("camera.flyKeys.forward") == "W", "CLI restores the profile movement key");
		cli({"gestures", "netradiant-custom", "--reset"}, 0);
		cli({"gestures", "netradiant-custom", "--input", output}, 0);
		ok &= expect(cli({"gestures", "netradiant-custom"}, 0).value("values").toObject() == validValues, "CLI imports complete preferences");
		const auto exportPath = temp.filePath(QStringLiteral("cli-export.json"));
		cli({"gestures", "netradiant-custom", "--output", exportPath}, 0);
		ok &= expect(bytes(exportPath) == savedFile, "GUI/core and CLI use same portable file");
		cli({"gestures", "netradiant-custom", "--output", exportPath}, 4);
		cli({"gestures", "netradiant-custom", "--output", exportPath, "--overwrite"}, 0);
		const auto future = temp.filePath(QStringLiteral("future.ini"));
		{ QSettings newer(future, QSettings::IniFormat); newer.setValue("app/settingsSchemaVersion", 999); newer.sync(); }
		const auto protectedBytes = bytes(future);
		cli({"gestures", "hammer", "--set", "plan.panButtons=right"}, 1, future);
		ok &= expect(bytes(future) == protectedBytes, "newer settings schema preserved");
	}
	{
		QSettings raw(settingsPath, QSettings::IniFormat); raw.setValue("editor/gestureOverrides/hammer", QByteArray("broken")); raw.sync();
	}
	const auto corruptBytes = bytes(settingsPath);
	const auto fallback = StudioSettings().effectiveLevelEditorControls("hammer", &error);
	ok &= expect(!error.isEmpty() && levelGestureValues(fallback) == levelGestureValues(levelEditorControlsForProfile("hammer"))
		&& bytes(settingsPath) == corruptBytes, "invalid stored preferences report fallback without rewriting");
	ok &= expect(settings.setEditorGestureOverrides("hammer", {}, &error) && settings.editorGestureOverrides("hammer", &error).isEmpty()
		&& error.isEmpty(), "explicit reset repairs invalid preferences");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
