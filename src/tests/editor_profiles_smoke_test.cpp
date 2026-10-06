#include "core/editor_profiles.h"
#include "core/studio_semantics.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QTranslator>

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

const QSet<QString> kProfilesWithControls = {
	QStringLiteral("vibestudio-default"),
	QStringLiteral("trenchbroom"),
	QStringLiteral("netradiant-custom"),
	QStringLiteral("netradiant"), QStringLiteral("sledge"),
	QStringLiteral("q3radiant"),
	QStringLiteral("gtkradiant-1-6"),
	QStringLiteral("quark"), QStringLiteral("hammer"), QStringLiteral("jack"), QStringLiteral("darkradiant"),
	QStringLiteral("doom-builder"), QStringLiteral("ultimate-doom-builder"), QStringLiteral("slade"), QStringLiteral("eureka"),
	QStringLiteral("unreal"), QStringLiteral("unity"), QStringLiteral("godot"), QStringLiteral("blender"),
};

bool runRegistrySmoke(QSet<QString>* ids)
{
	bool ok = true;
	const QVector<vibestudio::EditorProfileDescriptor> profiles = vibestudio::editorProfileDescriptors();
	ok &= expect(profiles.size() >= 19, "Expected brush, Doom and modern scene-editor families to be registered.");
	ok &= expect(vibestudio::defaultEditorProfileId() == QStringLiteral("vibestudio-default"), "Expected stable default editor profile id.");

	for (const vibestudio::EditorProfileDescriptor& profile : profiles) {
		ok &= expect(!profile.id.isEmpty() && !profile.displayName.isEmpty() && !profile.description.isEmpty(),
			"Expected every editor profile to have identity and copy.");
		ok &= expect(!ids->contains(profile.id), "Expected editor profile ids to be unique.");
		ids->insert(profile.id);
		ok &= expect(!profile.layoutPresetId.isEmpty() && !profile.cameraPresetId.isEmpty() && !profile.selectionPresetId.isEmpty() && !profile.gridPresetId.isEmpty() && !profile.terminologyPresetId.isEmpty(),
			"Expected every editor profile to declare schema preset ids.");

		// Honest placeholder reporting: a profile with its own level editor
		// controls resolves every preset; the others use the VibeStudio
		// controls and must say their presets are still data.
		const bool hasControls = kProfilesWithControls.contains(profile.id);
		ok &= expect(profile.placeholder == !hasControls, "Expected exactly the profiles without their own controls to be placeholders.");
		if (hasControls) {
			ok &= expect(profile.unresolvedPresets.isEmpty(), "Expected a profile with controls to leave no preset unresolved.");
		} else {
			for (const QString& preset : {QStringLiteral("layout"), QStringLiteral("camera"), QStringLiteral("selection"), QStringLiteral("grid")}) {
				ok &= expect(profile.unresolvedPresets.contains(preset), "Expected the unresolved preset list to name every unrouted preset kind.");
			}
		}
		const QStringList problems = vibestudio::levelEditorControlProblems(profile.controls);
		for (const QString& problem : problems) {
			std::cerr << qPrintable(profile.id) << ": " << qPrintable(problem) << "\n";
		}
		ok &= expect(problems.isEmpty(), "Expected every profile's controls to hold together.");
		ok &= expect(!vibestudio::levelEditorControlRows(profile.controls).isEmpty(), "Expected every profile's controls to read as a reference.");

		ok &= expect(!profile.supportedEngineFamilies.isEmpty() && !profile.defaultPanels.isEmpty() && !profile.workflowNotes.isEmpty() && !profile.bindings.isEmpty(),
			"Expected every editor profile to carry routed workflow data.");

		QString conflict;
		if (vibestudio::editorProfileHasShortcutConflict(profile, &conflict)) {
			std::cerr << qPrintable(conflict) << "\n";
			ok &= expect(false, "Expected editor profile shortcuts to avoid surface-local conflicts.");
		}

		int implementedCount = 0;
		int unimplementedCount = 0;
		for (const vibestudio::EditorProfileBinding& binding : profile.bindings) {
			ok &= expect(!binding.actionId.isEmpty() && !binding.displayName.isEmpty() && !binding.context.isEmpty() && !binding.commandId.isEmpty() && !binding.surfaceId.isEmpty(),
				"Expected routed bindings to be inspectable.");
			// `implemented` is computed from the shell registry, never asserted.
			ok &= expect(binding.implemented == vibestudio::shellCommandIdExists(binding.commandId),
				"Expected binding.implemented to mirror the shell command registry.");
			if (binding.implemented) {
				++implementedCount;
				ok &= expect(vibestudio::shortcutForCommandId(binding.commandId) || vibestudio::commandPaletteEntryForCommandId(binding.commandId),
					"Expected an implemented binding to resolve in the shell registry.");
				ok &= expect(!binding.shortcut.trimmed().isEmpty() || binding.clearsKeys,
					"Expected a remapped shell command to declare its keys, or to take them away.");
			} else {
				++unimplementedCount;
				ok &= expect(binding.commandId.startsWith(QStringLiteral("editor-command.")),
					"Expected unimplemented bindings to use the editor-command namespace.");
			}
			vibestudio::EditorProfileBinding routedBinding;
			ok &= expect(vibestudio::editorProfileBindingForAction(profile, binding.actionId, &routedBinding) && routedBinding.commandId == binding.commandId,
				"Expected action lookups to resolve stable command routes.");
		}
		ok &= expect(implementedCount > 0, "Expected every profile to remap at least one real shell command.");
		ok &= expect(implementedCount + unimplementedCount == profile.bindings.size(), "Expected every binding to have a reported implementation state.");
		for (const auto& alias : profile.aliases) {
			vibestudio::EditorProfileDescriptor resolved;
			ok &= expect(vibestudio::editorProfileForId(alias, &resolved) && resolved.id == profile.id, "Expected aliases to resolve to their canonical profile.");
		}
		ok &= expect(vibestudio::editorProfileImplementedBindings(profile).size() == implementedCount,
			"Expected the implemented-binding helper to agree with the binding data.");
		ok &= expect(!vibestudio::editorProfileRemappedShellCommandIds(profile).isEmpty(),
			"Expected every profile to expose the shell command ids it remaps.");
	}
	return ok;
}

bool runLookupSmoke(const QSet<QString>& ids)
{
	bool ok = true;
	const QStringList requiredIds = {
		QStringLiteral("vibestudio-default"),
		QStringLiteral("gtkradiant-1-6"),
		QStringLiteral("netradiant-custom"),
		QStringLiteral("trenchbroom"),
		QStringLiteral("quark"),
	};
	for (const QString& id : requiredIds) {
		ok &= expect(ids.contains(id), "Expected named editor profile preset.");
	}

	vibestudio::EditorProfileDescriptor radiant;
	ok &= expect(vibestudio::editorProfileForId(QStringLiteral("GtkRadiant_1_6"), &radiant) && radiant.id == QStringLiteral("gtkradiant-1-6"),
		"Expected editor profile lookup to normalize ids.");
	ok &= expect(!vibestudio::editorProfileForId(QStringLiteral("missing-profile")), "Expected missing editor profile lookup to fail.");
	vibestudio::EditorProfileDescriptor quark;
	ok &= expect(vibestudio::editorProfileForId(QStringLiteral("quark"), &quark), "Expected the QuArK profile to resolve.");
	const QString summary = vibestudio::editorProfileSummaryText(quark);
	ok &= expect(summary.contains(QStringLiteral("four-views")), "Expected QuArK to use the shared four-view workspace.");
	ok &= expect(summary.contains(QStringLiteral("Command bindings:")), "Expected editor profile summary to expose routed command bindings.");
	ok &= expect(!quark.placeholder && !quark.adaptations.isEmpty() && summary.contains(QStringLiteral("Adaptation:")), "Expected live QuArK controls with explicit differences.");
	ok &= expect(vibestudio::editorProfileSummaryText(radiant).contains(QStringLiteral("not implemented yet")),
		"Expected the summary to flag unimplemented bindings.");
	ok &= expect(vibestudio::editorProfileDisplayNameForId(QStringLiteral("trenchbroom")).contains(QStringLiteral("TrenchBroom")),
		"Expected editor profile display-name lookup.");

	// A profile genuinely changes a shortcut: Radiant opens maps with Ctrl+O,
	// while the shell default for map.open is Ctrl+M.
	vibestudio::EditorProfileBinding mapOpen;
	ok &= expect(vibestudio::editorProfileBindingForAction(radiant, QStringLiteral("map.open"), &mapOpen) && mapOpen.implemented,
		"Expected the Radiant profile to remap the shell map.open command.");
	vibestudio::ShortcutDescriptor shellMapOpen;
	ok &= expect(vibestudio::shortcutForCommandId(QStringLiteral("map.open"), &shellMapOpen), "Expected a shell shortcut for map.open.");
	ok &= expect(mapOpen.shortcut == QStringLiteral("Ctrl+O") && mapOpen.shortcut != shellMapOpen.defaultSequence,
		"Expected the Radiant profile to override the shell default sequence.");

	// Editor profiles never declare a competing command palette sequence.
	vibestudio::EditorProfileDescriptor defaultProfile;
	ok &= expect(vibestudio::editorProfileForId(vibestudio::defaultEditorProfileId(), &defaultProfile), "Expected the default profile to resolve.");
	vibestudio::EditorProfileBinding paletteBinding;
	ok &= expect(vibestudio::editorProfileBindingForAction(defaultProfile, QStringLiteral("shell.command-palette"), &paletteBinding),
		"Expected the default profile to reference the shell command palette command id.");
	vibestudio::ShortcutDescriptor paletteShortcut;
	ok &= expect(vibestudio::shortcutForCommandId(QStringLiteral("shell.command-palette"), &paletteShortcut), "Expected the registry palette shortcut.");
	ok &= expect(paletteBinding.shortcut == paletteShortcut.defaultSequence,
		"Expected the default profile to keep the documented command palette sequence.");

	const QMap<QString, QPair<QString, QString>> expectedCameraSelection = {
		{QStringLiteral("vibestudio-default"), {QStringLiteral("hybrid-orbit-and-fly"), QStringLiteral("explicit-object-and-component")}},
		{QStringLiteral("gtkradiant-1-6"), {QStringLiteral("radiant-camera"), QStringLiteral("radiant-brush-entity")}},
		{QStringLiteral("netradiant-custom"), {QStringLiteral("radiant-enhanced-camera"), QStringLiteral("radiant-fast-manipulation")}},
		{QStringLiteral("trenchbroom"), {QStringLiteral("trenchbroom-fly-camera"), QStringLiteral("component-face-edge-vertex")}},
		{QStringLiteral("quark"), {QStringLiteral("quark-linked-views"), QStringLiteral("object-tree-property")}},
	};
	for (auto it = expectedCameraSelection.cbegin(); it != expectedCameraSelection.cend(); ++it) {
		vibestudio::EditorProfileDescriptor profile;
		if (!vibestudio::editorProfileForId(it.key(), &profile)) {
			ok &= expect(false, "Expected editor profile lookup for camera/selection smoke.");
			continue;
		}
		ok &= expect(profile.cameraPresetId == it.value().first && profile.selectionPresetId == it.value().second,
			"Expected profile-specific camera and selection presets to remain stable.");
	}
	return ok;
}

// The TrenchBroom and NetRadiant Custom schemes carry those editors' own
// defaults (read from their sources); these are the facts a user of each
// would notice first.
bool runControlsSmoke()
{
	bool ok = true;
	using namespace vibestudio;
	const LevelEditorControls standard = levelEditorControlsForProfile(QStringLiteral("vibestudio-default"));
	ok &= expect(standard.layout == LevelViewLayout::Single2D && !standard.camera.perspective && standard.plan.emptyDrag == PlanEmptyDrag::BoxSelect
			&& standard.plan.panButtons == QVector<Qt::MouseButton> {Qt::MiddleButton} && standard.defaultGridUnits == 64,
		"Expected the VibeStudio plan and camera defaults to remain familiar.");
	ok &= expect(standard.keys.size() == 1 && standard.keys.first().commandId == QStringLiteral("map.maximizeView")
		&& standard.keys.first().keys == QStringList{QStringLiteral("Ctrl+Space")}, "The default profile exposes its viewport-local maximize shortcut.");
	ok &= expect(levelEditorControlsForProfile(QStringLiteral("quark")).layout == LevelViewLayout::FourViews,
		"Expected QuArK's controls to be routed to real views.");

	const LevelEditorControls trench = levelEditorControlsForProfile(QStringLiteral("TrenchBroom"));
	ok &= expect(trench.layout == LevelViewLayout::Single3D && trench.defaultGridUnits == 16, "Expected TrenchBroom's one-pane, 3D-first layout and 16-unit grid.");
	ok &= expect(trench.camera.perspective && trench.camera.lookButton == Qt::RightButton && !trench.camera.lookClickToggles
			&& trench.camera.orbitButton == Qt::RightButton && trench.camera.orbitModifiers == Qt::AltModifier
			&& trench.camera.panButtons == QVector<Qt::MouseButton> {Qt::MiddleButton} && trench.camera.wheel == CameraWheel::Dolly
			&& trench.camera.fieldOfViewWheelModifiers == Qt::ShiftModifier,
		"Expected TrenchBroom's camera: right drag looks, Alt+right orbits, middle pans, the wheel moves, Shift+wheel zooms.");
	ok &= expect(trench.camera.flyKeys.all() == QStringList {QStringLiteral("W"), QStringLiteral("S"), QStringLiteral("A"), QStringLiteral("D"), QStringLiteral("Q"), QStringLiteral("X")}
			&& !trench.camera.flyNeedsLook,
		"Expected TrenchBroom's fly keys, W S A D with Q up and X down, always live in the 3D view.");
	ok &= expect(trench.camera.toggleModifiers == Qt::ControlModifier && trench.camera.faceModifiers == Qt::ShiftModifier
			&& trench.plan.toggleModifiers == Qt::ControlModifier && trench.plan.emptyDrag == PlanEmptyDrag::DrawBrush
			&& trench.plan.panButtons.contains(Qt::RightButton) && trench.plan.panButtons.contains(Qt::MiddleButton),
		"Expected TrenchBroom's selection and 2D view: Ctrl toggles, Shift picks faces, a drag draws a brush, right or middle pans.");

	// GtkRadiant 1.6.0: Shift+click selects and a plain click selects
	// nothing, Shift+Alt drills, Alt+drag selects an area, the right button
	// pans and with Shift zooms, and the camera toggles free look.
	const LevelEditorControls gtk = levelEditorControlsForProfile(QStringLiteral("gtkradiant-1-6"));
	ok &= expect(gtk.layout == LevelViewLayout::CameraAndPlan && gtk.defaultGridUnits == 8 && qFuzzyCompare(gtk.camera.fieldOfViewDegrees, 90.0),
		"Expected GtkRadiant's camera beside a 2D view, 8-unit grid, and 90 degree view.");
	ok &= expect(!gtk.plan.plainClickSelects && gtk.plan.toggleModifiers == Qt::ShiftModifier && gtk.plan.cycleModifiers == (Qt::ShiftModifier | Qt::AltModifier)
			&& gtk.plan.bandModifiers == Qt::AltModifier && gtk.plan.panButtons == QVector<Qt::MouseButton> {Qt::RightButton}
			&& gtk.plan.zoomDragButton == Qt::RightButton && gtk.plan.zoomDragModifiers == Qt::ShiftModifier && !gtk.plan.clickCyclesStack
			&& gtk.plan.middleButtonDrivesCamera && gtk.plan.arrowsDriveCamera && gtk.plan.emptyDrag == PlanEmptyDrag::DrawBrush,
		"Expected GtkRadiant's 2D view: Shift+click selects, Shift+Alt drills, Alt+drag bands, right pans, Shift+right zooms.");
	ok &= expect(!gtk.camera.plainClickSelects && gtk.camera.lookClickToggles && gtk.camera.orbitButton == Qt::NoButton && gtk.camera.panButtons.isEmpty()
			&& gtk.camera.wheel == CameraWheel::Dolly && gtk.camera.faceModifiers == (Qt::ShiftModifier | Qt::ControlModifier)
			&& gtk.camera.driveKeys.all() == QStringList {QStringLiteral("Up"), QStringLiteral("Down"), QStringLiteral(","), QStringLiteral("."), QStringLiteral("D"), QStringLiteral("C"), QStringLiteral("Left"), QStringLiteral("Right")},
		"Expected GtkRadiant's camera: a right click toggles free look, the arrows, comma, period, D, and C drive it.");
	EditorProfileDescriptor gtkProfile;
	EditorProfileBinding gtkBinding;
	ok &= expect(editorProfileForId(QStringLiteral("gtkradiant-1-6"), &gtkProfile) && !gtkProfile.placeholder
			&& editorProfileBindingForAction(gtkProfile, QStringLiteral("map.zoomIn"), &gtkBinding) && gtkBinding.implemented
			&& editorProfileBindingKeys(gtkBinding) == QStringList {QStringLiteral("Del")}
			&& editorProfileBindingForAction(gtkProfile, QStringLiteral("map.deleteSelection"), &gtkBinding)
			&& editorProfileBindingKeys(gtkBinding) == QStringList {QStringLiteral("Backspace")},
		"Expected GtkRadiant's Delete to zoom in and Backspace to delete.");

	const LevelEditorControls radiant = levelEditorControlsForProfile(QStringLiteral("netradiant-custom"));
	ok &= expect(radiant.layout == LevelViewLayout::CameraAndPlan && radiant.defaultGridUnits == 16 && qFuzzyCompare(radiant.camera.fieldOfViewDegrees, 100.0),
		"Expected NetRadiant Custom's camera beside a 2D view, 16-unit grid, and 100 degree view.");
	ok &= expect(radiant.plan.toggleModifiers == Qt::ShiftModifier && radiant.plan.panButtons == QVector<Qt::MouseButton> {Qt::RightButton}
			&& radiant.plan.zoomDragButton == Qt::RightButton && radiant.plan.zoomDragModifiers == Qt::AltModifier
			&& radiant.plan.clickCyclesStack && radiant.plan.middleButtonDrivesCamera && radiant.plan.arrowsDriveCamera
			&& radiant.plan.emptyDrag == PlanEmptyDrag::DrawBrush && radiant.plan.emptyDragWithSelection == PlanEmptyDrag::ResizeSelection,
		"Expected Radiant's 2D view: Shift selects, right drag pans, Alt+right zooms, clicks tunnel, the middle button drives the camera.");
	ok &= expect(radiant.camera.lookClickToggles && radiant.camera.flyNeedsLook && radiant.camera.panButtons == QVector<Qt::MouseButton> {Qt::RightButton}
			&& radiant.camera.wheel == CameraWheel::DollyToPointer && radiant.camera.faceModifiers == Qt::ControlModifier,
		"Expected Radiant's camera: a right click toggles mouse look, a right drag strafes, the wheel moves toward the pointer.");

	// The keymaps reach the command registry as routed bindings.
	EditorProfileDescriptor trenchProfile;
	EditorProfileDescriptor radiantProfile;
	ok &= expect(editorProfileForId(QStringLiteral("trenchbroom"), &trenchProfile) && editorProfileForId(QStringLiteral("netradiant-custom"), &radiantProfile),
		"Expected both editor profiles to resolve.");
	EditorProfileBinding binding;
	ok &= expect(editorProfileBindingForAction(trenchProfile, QStringLiteral("map.clipTool"), &binding) && binding.implemented
			&& editorProfileBindingKeys(binding) == QStringList {QStringLiteral("C")},
		"Expected TrenchBroom's clip tool on C, routed to the shell.");
	ok &= expect(editorProfileBindingForAction(trenchProfile, QStringLiteral("map.previewWireframe"), &binding) && binding.implemented && binding.clearsKeys
			&& editorProfileBindingKeys(binding).isEmpty(),
		"Expected TrenchBroom to take W from the wireframe toggle, since W flies.");
	ok &= expect(editorProfileBindingForAction(trenchProfile, QStringLiteral("map.cycleView"), &binding) && binding.implemented
			&& editorProfileBindingKeys(binding) == QStringList {QStringLiteral("Space")},
		"Expected TrenchBroom's Space to cycle the map view.");
	ok &= expect(editorProfileBindingForAction(radiantProfile, QStringLiteral("map.duplicateSelection"), &binding) && binding.implemented
			&& editorProfileBindingKeys(binding) == QStringList {QStringLiteral("Space"), QStringLiteral("Shift+Space")},
		"Expected Radiant's Space to clone the selection.");
	ok &= expect(editorProfileBindingForAction(radiantProfile, QStringLiteral("map.grid16"), &binding) && binding.implemented
			&& editorProfileBindingKeys(binding) == QStringList {QStringLiteral("5")},
		"Expected Radiant's digits to set the grid, 5 for 16 units.");
	ok &= expect(editorProfileSummaryText(trenchProfile).contains(QStringLiteral("Level editor controls:"))
			&& editorProfileSummaryText(trenchProfile).contains(QStringLiteral("all presets resolve")),
		"Expected the profile summary to spell out its controls.");
	return ok;
}

bool runFamiliarNavigationSmoke()
{
	using namespace vibestudio;
	bool ok = true;
	const auto hammer = levelEditorControlsForProfile(QStringLiteral("Worldcraft"));
	ok &= expect(hammer.layout == LevelViewLayout::FourViews && hammer.camera.lookToggleKey == QStringLiteral("Z"), "Hammer has four views and its Z toggle.");
	ok &= expect(!cameraFlyKeysActive(hammer.camera, false) && cameraFlyKeysActive(hammer.camera, true)
		&& cameraFlyKeysActive(hammer.camera, false, Qt::MiddleButton), "Hammer flight is gated by mouse look so idle movement letters leave tool shortcuts available.");
	const auto bound = [](const LevelEditorControls& controls, const char* id) {
		for (const auto& binding : controls.keys) { if (binding.commandId == QLatin1String(id)) { return binding.keys; } }
		return QStringList{};
	};
	const auto jack = levelEditorControlsForProfile(QStringLiteral("J.A.C.K."));
	const auto netradiant = levelEditorControlsForProfile(QStringLiteral("xonotic-netradiant"));
	const auto sledge = levelEditorControlsForProfile(QStringLiteral("sledge-editor"));
	ok &= expect(netradiant.defaultGridUnits == 8 && netradiant.camera.fieldOfViewDegrees == 110
		&& netradiant.camera.orbitButton == Qt::NoButton && netradiant.camera.panButtons.isEmpty()
		&& netradiant.plan.zoomDragModifiers == Qt::AltModifier, "Standalone NetRadiant keeps its audited defaults distinct from Custom.");
	ok &= expect(bound(netradiant, "map.deleteSelection") == QStringList{"Backspace", "Z"}
		&& bound(netradiant, "map.zoomIn") == QStringList{"Del"} && bound(netradiant, "map.zoomOut") == QStringList{"Ins"}
		&& bound(netradiant, "map.duplicateSelection") == QStringList{"Space"}
		&& bound(netradiant, "map.selectNone") == QStringList{"Escape", "C"}, "Standalone NetRadiant reserves zoom and cloning keys correctly.");
	ok &= expect(sledge.layout == LevelViewLayout::FourViews && sledge.defaultGridUnits == 16 && sledge.camera.fieldOfViewDegrees == 60
		&& sledge.plan.panHoldKey == "Space" && sledge.camera.lookHoldKey == "Space" && sledge.camera.lookToggleKey == "Z"
		&& sledge.camera.flyKeys.up == "Q" && sledge.camera.flyKeys.down == "E" && !sledge.camera.flyNeedsLook,
		"Sledge has its own hold navigation, camera lens and vertical flight defaults.");
	ok &= expect(bound(sledge, "map.selectAll") == QStringList{"Ctrl+A"} && bound(sledge, "map.equalizeViews").isEmpty()
		&& bound(sledge, "map.isolateSelection") == QStringList{"Ctrl+H"}
		&& bound(sledge, "map.hollowSelection") == QStringList{"Ctrl+Shift+H"}, "Sledge does not inherit Hammer's different select-all and hollow keys.");
	ok &= expect(bound(hammer, "map.maximizeView") == QStringList{QStringLiteral("Shift+Z")}
		&& bound(jack, "map.maximizeView") == QStringList{QStringLiteral("Shift+Z")}
		&& bound(hammer, "map.equalizeViews") == QStringList{QStringLiteral("Ctrl+A")}
		&& bound(netRadiantCustomLevelControls(), "map.maximizeView") == QStringList{QStringLiteral("F12")},
		"Audited Hammer and NetRadiant workspace shortcuts route to their matching operations.");
	ok &= expect(bound(hammer, "map.hollowSelection") == QStringList{QStringLiteral("Ctrl+H")}
		&& bound(jack, "map.isolateSelection") == QStringList{QStringLiteral("Ctrl+H")}
		&& bound(jack, "map.hollowSelection") == QStringList{QStringLiteral("Ctrl+U"), QStringLiteral("Ctrl+Shift+H")},
		"Classic Hammer and J.A.C.K. retain their distinct isolate/hollow shortcuts.");
	ok &= expect(bound(jack, "map.duplicateSelection").isEmpty() && bound(hammer, "map.selectAll").isEmpty()
		&& bound(jack, "map.viewTop").isEmpty() && bound(jack, "map.viewFront").isEmpty() && bound(jack, "map.viewSide").isEmpty(),
		"Detail, pane-sizing and texture-tool keys are not assigned unrelated map edits.");
	const auto udb = levelEditorControlsForProfile(QStringLiteral("UDB"));
	ok &= expect(udb.camera.flyKeys.all() == QStringList{QStringLiteral("E"), QStringLiteral("D"), QStringLiteral("S"), QStringLiteral("F")}, "Doom Builder retains ESDF rather than generic WASD.");
	for (const auto& id : {QStringLiteral("unreal"), QStringLiteral("unity"), QStringLiteral("godot"), QStringLiteral("blender")}) {
		const auto controls = levelEditorControlsForProfile(id);
		const auto& camera = controls.camera;
		ok &= expect(!cameraFlyKeysActive(camera, false), "Modern fly keys are inactive without looking.");
		ok &= expect(cameraFlyKeysActive(camera, false, Qt::RightButton), "A held look button enables flight before any pointer movement.");
		ok &= expect(cameraFlyKeysActive(camera, false, Qt::RightButton, Qt::ShiftModifier), "Holding the fast modifier before looking still enables flight.");
		ok &= expect(!cameraFlyKeysActive(camera, false, Qt::MiddleButton) && !cameraFlyKeysActive(camera, false, Qt::RightButton, Qt::ControlModifier), "Pan/orbit and unrelated modifiers do not enable flight.");
		ok &= expect(cameraFlyKeysActive(camera, true), "A toggled look state also enables flight.");
		ok &= expect(cameraNavigationDrag(camera, camera.orbitButton, camera.orbitModifiers) == CameraNavigationDrag::Orbit, "Declared orbit gestures reach navigation routing.");
		ok &= expect(cameraNavigationDrag(camera, Qt::MiddleButton, camera.panModifiers) == CameraNavigationDrag::Pan, "Declared pan gestures reach navigation routing.");
	}
	const auto trench = trenchBroomLevelControls();
	ok &= expect(cameraNavigationDrag(trench.camera, Qt::RightButton, Qt::AltModifier) == CameraNavigationDrag::Orbit, "TrenchBroom's Alt+right remains orbit, not accelerated look.");
	const auto radiant = netRadiantCustomLevelControls();
	ok &= expect(cameraNavigationDrag(radiant.camera, Qt::RightButton, Qt::NoModifier) == CameraNavigationDrag::Pan
		&& !cameraFlyKeysActive(radiant.camera, false, Qt::RightButton), "Radiant right drag remains strafe; click toggles free look.");
	auto bad = familiarLevelControls(QStringLiteral("godot"));
	bad.camera.panModifiers = Qt::NoModifier;
	ok &= expect(!levelEditorControlProblems(bad).isEmpty(), "A middle orbit/pan collision is rejected.");
	bad = familiarLevelControls(QStringLiteral("godot")); bad.camera.flyKeys.left = bad.camera.flyKeys.right;
	ok &= expect(!levelEditorControlProblems(bad).isEmpty(), "Duplicate motion keys are rejected.");
	bad = familiarLevelControls(QStringLiteral("hammer")); bad.keys.push_back({QStringLiteral("map.clipTool"), {QStringLiteral("Z")}});
	ok &= expect(!levelEditorControlProblems(bad).isEmpty(), "Mouse-look toggles cannot double as command shortcuts.");
	bad = vibeStudioLevelControls(); bad.defaultGridUnits = 3;
	ok &= expect(!levelEditorControlProblems(bad).isEmpty(), "Unsupported default grid values are rejected.");
	QElapsedTimer timer; timer.start();
	for (int i = 0; i < 100; ++i) { EditorProfileDescriptor profile; ok &= editorProfileForId(QStringLiteral("hammer++"), &profile); }
	std::cout << "100 canonical/alias profile lookups: " << timer.elapsed() << " ms\n";
	class Translation final : public QTranslator {
	public:
		bool isEmpty() const override { return false; }
		QString translate(const char*, const char* source, const char*, int) const override { return QStringLiteral("translated:") + QString::fromUtf8(source); }
	} translation;
	const auto originalName = editorProfileDisplayNameForId(QStringLiteral("hammer"));
	QCoreApplication::installTranslator(&translation);
	ok &= expect(editorProfileDisplayNameForId(QStringLiteral("hammer")).startsWith(QStringLiteral("translated:")), "Catalog cache invalidates when translators change.");
	QCoreApplication::removeTranslator(&translation);
	ok &= expect(editorProfileDisplayNameForId(QStringLiteral("hammer")) == originalName, "Removing a translator restores current-language descriptors.");
	return ok;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QSet<QString> ids;
	bool ok = true;
	ok &= runRegistrySmoke(&ids);
	ok &= runLookupSmoke(ids);
	ok &= runControlsSmoke();
	ok &= runFamiliarNavigationSmoke();
	if (!ok) {
		return fail("editor_profiles smoke test failed.");
	}
	return EXIT_SUCCESS;
}
