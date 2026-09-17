#include "core/editor_profiles.h"
#include "core/studio_semantics.h"

#include <QCoreApplication>
#include <QMap>
#include <QPair>
#include <QSet>

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

bool runRegistrySmoke(QSet<QString>* ids)
{
	bool ok = true;
	const QVector<vibestudio::EditorProfileDescriptor> profiles = vibestudio::editorProfileDescriptors();
	ok &= expect(profiles.size() >= 5, "Expected all routed editor profiles to be registered.");
	ok &= expect(vibestudio::defaultEditorProfileId() == QStringLiteral("vibestudio-default"), "Expected stable default editor profile id.");

	for (const vibestudio::EditorProfileDescriptor& profile : profiles) {
		ok &= expect(!profile.id.isEmpty() && !profile.displayName.isEmpty() && !profile.description.isEmpty(),
			"Expected every editor profile to have identity and copy.");
		ok &= expect(!ids->contains(profile.id), "Expected editor profile ids to be unique.");
		ids->insert(profile.id);
		ok &= expect(!profile.layoutPresetId.isEmpty() && !profile.cameraPresetId.isEmpty() && !profile.selectionPresetId.isEmpty() && !profile.gridPresetId.isEmpty() && !profile.terminologyPresetId.isEmpty(),
			"Expected every editor profile to declare schema preset ids.");

		// Honest placeholder reporting: the layout/camera/selection/grid presets
		// do not resolve to behaviour yet, so the profile must say so.
		ok &= expect(profile.placeholder, "Expected profiles with unresolved presets to be reported as placeholders.");
		ok &= expect(!profile.unresolvedPresets.isEmpty(), "Expected a placeholder profile to name its unresolved presets.");
		for (const QString& preset : {QStringLiteral("layout"), QStringLiteral("camera"), QStringLiteral("selection"), QStringLiteral("grid")}) {
			ok &= expect(profile.unresolvedPresets.contains(preset), "Expected the unresolved preset list to name every unrouted preset kind.");
		}

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
				ok &= expect(!binding.shortcut.trimmed().isEmpty(), "Expected a remapped shell command to declare a preferred sequence.");
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
		ok &= expect(unimplementedCount > 0, "Expected profiles to keep declaring the editor commands that do not exist yet.");
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
	const QString summary = vibestudio::editorProfileSummaryText(radiant);
	ok &= expect(summary.contains(QStringLiteral("radiant-four-pane")), "Expected editor profile summary to expose layout preset.");
	ok &= expect(summary.contains(QStringLiteral("Command bindings:")), "Expected editor profile summary to expose routed command bindings.");
	ok &= expect(summary.contains(QStringLiteral("placeholder presets")), "Expected the summary to admit the unresolved presets.");
	ok &= expect(summary.contains(QStringLiteral("not implemented yet")), "Expected the summary to flag unimplemented bindings.");
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

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QSet<QString> ids;
	bool ok = true;
	ok &= runRegistrySmoke(&ids);
	ok &= runLookupSmoke(ids);
	if (!ok) {
		return fail("editor_profiles smoke test failed.");
	}
	return EXIT_SUCCESS;
}
