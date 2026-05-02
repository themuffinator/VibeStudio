#include "core/editor_profiles.h"

#include <QCoreApplication>
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
		return fail("Expected all placeholder editor profiles to be registered.");
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
		if (profile.supportedEngineFamilies.isEmpty() || profile.defaultPanels.isEmpty() || profile.workflowNotes.isEmpty() || profile.bindings.isEmpty()) {
			return fail("Expected every editor profile to carry placeholder workflow data.");
		}
		for (const vibestudio::EditorProfileBinding& binding : profile.bindings) {
			if (binding.actionId.isEmpty() || binding.displayName.isEmpty() || binding.context.isEmpty()) {
				return fail("Expected placeholder bindings to be inspectable.");
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
	if (!vibestudio::editorProfileDisplayNameForId(QStringLiteral("trenchbroom")).contains(QStringLiteral("TrenchBroom"))) {
		return fail("Expected editor profile display-name lookup.");
	}

	return EXIT_SUCCESS;
}
