#include "core/editor_profiles.h"

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

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);

	const QVector<vibestudio::EditorProfileDescriptor> profiles = vibestudio::editorProfileDescriptors();
	if (profiles.size() < 5) {
		return fail("Expected all routed editor profiles to be registered.");
	}
	if (vibestudio::defaultEditorProfileId() != QStringLiteral("vibestudio-default")) {
		return fail("Expected stable default editor profile id.");
	}

	QSet<QString> ids;
	for (const vibestudio::EditorProfileDescriptor& profile : profiles) {
		if (profile.id.isEmpty() || profile.displayName.isEmpty() || profile.description.isEmpty()) {
			return fail("Expected every editor profile to have identity and copy.");
		}
		if (ids.contains(profile.id)) {
			return fail("Expected editor profile ids to be unique.");
		}
		ids.insert(profile.id);
		if (profile.layoutPresetId.isEmpty() || profile.cameraPresetId.isEmpty() || profile.selectionPresetId.isEmpty() || profile.gridPresetId.isEmpty() || profile.terminologyPresetId.isEmpty()) {
			return fail("Expected every editor profile to declare schema preset ids.");
		}
		if (profile.placeholder) {
			return fail("Expected editor profiles to be routed MVP presets rather than placeholders.");
		}
		if (profile.supportedEngineFamilies.isEmpty() || profile.defaultPanels.isEmpty() || profile.workflowNotes.isEmpty() || profile.bindings.isEmpty()) {
			return fail("Expected every editor profile to carry routed workflow data.");
		}
		QString conflict;
		if (vibestudio::editorProfileHasShortcutConflict(profile, &conflict)) {
			std::cerr << qPrintable(conflict) << "\n";
			return fail("Expected editor profile shortcuts to avoid surface-local conflicts.");
		}
		for (const vibestudio::EditorProfileBinding& binding : profile.bindings) {
			if (binding.actionId.isEmpty() || binding.displayName.isEmpty() || binding.context.isEmpty() || binding.commandId.isEmpty() || binding.surfaceId.isEmpty() || !binding.implemented) {
				return fail("Expected routed bindings to be inspectable.");
			}
			vibestudio::EditorProfileBinding routedBinding;
			if (!vibestudio::editorProfileBindingForAction(profile, binding.actionId, &routedBinding) || routedBinding.commandId != binding.commandId) {
				return fail("Expected action lookups to resolve stable command routes.");
			}
		}
	}

	const QStringList requiredIds = {
		QStringLiteral("vibestudio-default"),
		QStringLiteral("gtkradiant-1-6"),
		QStringLiteral("netradiant-custom"),
		QStringLiteral("trenchbroom"),
		QStringLiteral("quark"),
	};
	for (const QString& id : requiredIds) {
		if (!ids.contains(id)) {
			return fail("Expected named editor profile preset.");
		}
	}

	vibestudio::EditorProfileDescriptor radiant;
	if (!vibestudio::editorProfileForId(QStringLiteral("GtkRadiant_1_6"), &radiant) || radiant.id != QStringLiteral("gtkradiant-1-6")) {
		return fail("Expected editor profile lookup to normalize ids.");
	}
	if (vibestudio::editorProfileForId(QStringLiteral("missing-profile"))) {
		return fail("Expected missing editor profile lookup to fail.");
	}
	if (!vibestudio::editorProfileSummaryText(radiant).contains(QStringLiteral("radiant-four-pane"))) {
		return fail("Expected editor profile summary to expose layout preset.");
	}
	if (!vibestudio::editorProfileSummaryText(radiant).contains(QStringLiteral("Command bindings:"))) {
		return fail("Expected editor profile summary to expose routed command bindings.");
	}
	if (!vibestudio::editorProfileDisplayNameForId(QStringLiteral("trenchbroom")).contains(QStringLiteral("TrenchBroom"))) {
		return fail("Expected editor profile display-name lookup.");
	}

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
			return fail("Expected editor profile lookup for camera/selection smoke.");
		}
		if (profile.cameraPresetId != it.value().first || profile.selectionPresetId != it.value().second) {
			return fail("Expected profile-specific camera and selection presets to remain stable.");
		}
	}

	return EXIT_SUCCESS;
}
