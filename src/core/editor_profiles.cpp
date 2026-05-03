#include "core/editor_profiles.h"

#include <QCoreApplication>

namespace vibestudio {

namespace {

QString profileText(const char* source)
{
	return QCoreApplication::translate("VibeStudioEditorProfiles", source);
}

EditorProfileBinding binding(
	const QString& actionId,
	const QString& displayName,
	const QString& shortcut,
	const QString& mouseGesture,
	const QString& context,
	const QString& surfaceId,
	const QString& commandId = QString())
{
	const QString normalizedCommandId = commandId.trimmed().isEmpty()
		? QStringLiteral("editor-command.%1").arg(actionId)
		: commandId.trimmed();
	return {actionId, displayName, shortcut, mouseGesture, context, normalizedCommandId, surfaceId.trimmed(), true};
}

EditorProfileDescriptor profile(
	const QString& id,
	const QString& displayName,
	const QString& shortName,
	const QString& lineage,
	const QString& description,
	const QString& layoutPresetId,
	const QString& cameraPresetId,
	const QString& selectionPresetId,
	const QString& gridPresetId,
	const QString& terminologyPresetId,
	const QStringList& supportedEngineFamilies,
	const QStringList& defaultPanels,
	const QStringList& workflowNotes,
	const QStringList& keybindingNotes,
	const QStringList& mouseBindingNotes,
	const QVector<EditorProfileBinding>& bindings)
{
	return {
		id,
		displayName,
		shortName,
		lineage,
		description,
		layoutPresetId,
		cameraPresetId,
		selectionPresetId,
		gridPresetId,
		terminologyPresetId,
		supportedEngineFamilies,
		defaultPanels,
		workflowNotes,
		keybindingNotes,
		mouseBindingNotes,
		bindings,
		false,
	};
}

} // namespace

QString defaultEditorProfileId()
{
	return QStringLiteral("vibestudio-default");
}

QString normalizedEditorProfileId(const QString& id)
{
	QString normalized = id.trimmed().toLower().replace('_', '-');
	normalized.replace(QStringLiteral(" "), QStringLiteral("-"));
	while (normalized.contains(QStringLiteral("--"))) {
		normalized.replace(QStringLiteral("--"), QStringLiteral("-"));
	}
	return normalized;
}

QVector<EditorProfileDescriptor> editorProfileDescriptors()
{
	const QStringList allEngines = {
		QStringLiteral("idTech1"),
		QStringLiteral("idTech2"),
		QStringLiteral("idTech3"),
	};

	return {
		profile(
			QStringLiteral("vibestudio-default"),
			profileText("VibeStudio Default"),
			profileText("Default"),
			profileText("VibeStudio"),
			profileText("Balanced studio layout for users who want one consistent workflow across Doom, Quake, and Quake III-era projects."),
			QStringLiteral("studio-workbench"),
			QStringLiteral("hybrid-orbit-and-fly"),
			QStringLiteral("explicit-object-and-component"),
			QStringLiteral("adaptive-idtech-grid"),
			QStringLiteral("neutral-vibestudio"),
			allEngines,
			{
				profileText("Workspace"),
				profileText("Level View"),
				profileText("Asset Browser"),
				profileText("Inspector"),
				profileText("Compiler"),
				profileText("Activity"),
			},
			{
				profileText("Use summary-first panels with raw metadata available in drawers."),
				profileText("Prefer explicit tools and visible operation state over hidden modal behavior."),
				profileText("Keep Doom, Quake, and Quake III terminology neutral until a project profile narrows the context."),
			},
			{
				profileText("Standard file, edit, view, and command-palette shortcuts stay platform conventional."),
				profileText("Profile bindings route to stable shell, package, compiler, and map-editor command identifiers."),
			},
			{
				profileText("Mouse bindings route to editor-surface commands and remain inactive outside their declared surface."),
			},
			{
				binding(QStringLiteral("command.palette"), profileText("Command Palette"), QStringLiteral("Ctrl+K"), QString(), profileText("Global"), QStringLiteral("shell")),
				binding(QStringLiteral("file.save"), profileText("Save"), QStringLiteral("Ctrl+S"), QString(), profileText("Global"), QStringLiteral("shell")),
				binding(QStringLiteral("view.focus-level"), profileText("Focus Level View"), QStringLiteral("F3"), QString(), profileText("Editor"), QStringLiteral("level-editor")),
				binding(QStringLiteral("view.focus-inspector"), profileText("Focus Inspector"), QStringLiteral("F8"), QString(), profileText("Shell"), QStringLiteral("shell")),
			}),
		profile(
			QStringLiteral("gtkradiant-1-6"),
			profileText("GtkRadiant 1.6.0 Style"),
			profileText("GtkRadiant"),
			profileText("GtkRadiant 1.6.0"),
			profileText("Radiant-family four-pane mental model with explicit camera, orthographic, brush, entity, grid, and texture workflows."),
			QStringLiteral("radiant-four-pane"),
			QStringLiteral("radiant-camera"),
			QStringLiteral("radiant-brush-entity"),
			QStringLiteral("radiant-grid"),
			QStringLiteral("radiant"),
			{
				QStringLiteral("idTech2"),
				QStringLiteral("idTech3"),
			},
			{
				profileText("Camera"),
				profileText("XY View"),
				profileText("XZ/YZ View"),
				profileText("Texture Browser"),
				profileText("Entity Inspector"),
				profileText("Build Menu"),
			},
			{
				profileText("Favor visible orthographic panes and a separate camera view."),
				profileText("Keep brush/entity selection modes distinct."),
				profileText("Expose grid stepping and clipping controls prominently."),
			},
			{
				profileText("Reserve Radiant-style camera, clipping, grid, and texture shortcuts for the map editor command table."),
				profileText("Avoid overriding global platform shortcuts outside the editor context."),
			},
			{
				profileText("Reserve right-button camera movement and orthographic drag gestures for editor-only contexts."),
			},
			{
				binding(QStringLiteral("editor.grid.smaller"), profileText("Grid Smaller"), QStringLiteral("["), QString(), profileText("Map Editor"), QStringLiteral("level-editor")),
				binding(QStringLiteral("editor.grid.larger"), profileText("Grid Larger"), QStringLiteral("]"), QString(), profileText("Map Editor"), QStringLiteral("level-editor")),
				binding(QStringLiteral("editor.clone-selection"), profileText("Clone Selection"), QStringLiteral("Space"), QString(), profileText("Map Editor"), QStringLiteral("level-editor")),
				binding(QStringLiteral("editor.camera.forward"), profileText("Camera Forward"), QString(), profileText("Right mouse drag"), profileText("Camera View"), QStringLiteral("camera-view")),
			}),
		profile(
			QStringLiteral("netradiant-custom"),
			profileText("NetRadiant Custom Style"),
			profileText("NetRadiant"),
			profileText("NetRadiant Custom"),
			profileText("Dense Radiant-family production preset for fast filtering, manipulation, texture projection, and q3map2-oriented build workflows."),
			QStringLiteral("radiant-dense-toolbar"),
			QStringLiteral("radiant-enhanced-camera"),
			QStringLiteral("radiant-fast-manipulation"),
			QStringLiteral("radiant-filtered-grid"),
			QStringLiteral("netradiant"),
			{
				QStringLiteral("idTech2"),
				QStringLiteral("idTech3"),
			},
			{
				profileText("Camera"),
				profileText("Orthographic Views"),
				profileText("Filters"),
				profileText("Texture Tools"),
				profileText("Entity Inspector"),
				profileText("q3map2 Build"),
			},
			{
				profileText("Make filters, texture projection, and compile profiles easy to reach."),
				profileText("Keep dense toolbars optional so accessibility scaling remains viable."),
				profileText("Treat q3map2 compile profiles as first-class build actions."),
			},
			{
				profileText("Reserve selection, texture-fit, filter, and build shortcuts for editor-command routing."),
				profileText("Keep compiler shortcuts routed through reviewable command manifests."),
			},
			{
				profileText("Reserve Radiant-style camera and manipulation gestures with clear cursor/focus feedback."),
			},
			{
				binding(QStringLiteral("editor.filters.toggle-caulk"), profileText("Toggle Caulk Filter"), QString(), QString(), profileText("Map Editor"), QStringLiteral("level-editor")),
				binding(QStringLiteral("editor.texture.fit"), profileText("Fit Texture"), QStringLiteral("Ctrl+F"), QString(), profileText("Texture Tools"), QStringLiteral("texture-tools")),
				binding(QStringLiteral("compiler.q3map2.plan"), profileText("Plan q3map2 Build"), QString(), QString(), profileText("Compiler"), QStringLiteral("compiler")),
				binding(QStringLiteral("editor.selection.expand"), profileText("Expand Selection"), QStringLiteral("Shift+E"), QString(), profileText("Map Editor"), QStringLiteral("level-editor")),
			}),
		profile(
			QStringLiteral("trenchbroom"),
			profileText("TrenchBroom Style"),
			profileText("TrenchBroom"),
			profileText("TrenchBroom"),
			profileText("Modern single-window brush-editing preset centered on a strong 3D view, compact inspectors, face tools, and project-aware workflows."),
			QStringLiteral("single-window-3d-forward"),
			QStringLiteral("trenchbroom-fly-camera"),
			QStringLiteral("component-face-edge-vertex"),
			QStringLiteral("trenchbroom-grid"),
			QStringLiteral("trenchbroom"),
			{
				QStringLiteral("idTech2"),
				QStringLiteral("idTech3"),
			},
			{
				profileText("3D View"),
				profileText("Map Inspector"),
				profileText("Face Tools"),
				profileText("Entity Browser"),
				profileText("Texture Browser"),
				profileText("Build Console"),
			},
			{
				profileText("Prefer a large 3D canvas with contextual inspectors around it."),
				profileText("Expose face, edge, vertex, brush, and entity modes without changing the underlying command stack."),
				profileText("Keep project/game configuration visible beside map and compiler state."),
			},
			{
				profileText("Reserve component editing shortcuts for the editor context and keep shell navigation conventional."),
			},
			{
				profileText("Reserve fly-camera and component selection gestures for the 3D view."),
			},
			{
				binding(QStringLiteral("editor.mode.face"), profileText("Face Mode"), QStringLiteral("F"), QString(), profileText("Map Editor"), QStringLiteral("level-editor")),
				binding(QStringLiteral("editor.mode.vertex"), profileText("Vertex Mode"), QStringLiteral("V"), QString(), profileText("Map Editor"), QStringLiteral("level-editor")),
				binding(QStringLiteral("editor.camera.fly"), profileText("Fly Camera"), QString(), profileText("Right mouse drag"), profileText("3D View"), QStringLiteral("camera-view")),
				binding(QStringLiteral("editor.texture.apply"), profileText("Apply Texture"), QString(), QString(), profileText("Face Tools"), QStringLiteral("texture-tools")),
			}),
		profile(
			QStringLiteral("quark"),
			profileText("QuArK Style"),
			profileText("QuArK"),
			profileText("QuArK"),
			profileText("Explorer-heavy integrated preset for users who prefer package, asset, map, object, and property context side by side."),
			QStringLiteral("explorer-integrated-mdi"),
			QStringLiteral("quark-linked-views"),
			QStringLiteral("object-tree-property"),
			QStringLiteral("hierarchical-grid"),
			QStringLiteral("quark"),
			allEngines,
			{
				profileText("Explorer"),
				profileText("Map View"),
				profileText("Object Tree"),
				profileText("Property Editor"),
				profileText("Package View"),
				profileText("Log"),
			},
			{
				profileText("Keep map, package, and object structure visible together."),
				profileText("Favor property editing and tree navigation for integrated workflows."),
				profileText("Expose package and asset context without making package editing destructive."),
			},
			{
				profileText("Reserve object tree, property editing, and multi-document shortcuts for editor-command routing."),
			},
			{
				profileText("Reserve tree selection and linked-view gestures for the integrated explorer surface."),
			},
			{
				binding(QStringLiteral("editor.object.properties"), profileText("Object Properties"), QStringLiteral("Alt+Enter"), QString(), profileText("Object Tree"), QStringLiteral("object-tree")),
				binding(QStringLiteral("editor.tree.focus"), profileText("Focus Object Tree"), QStringLiteral("F6"), QString(), profileText("Shell"), QStringLiteral("shell")),
				binding(QStringLiteral("package.focus"), profileText("Focus Package View"), QStringLiteral("F7"), QString(), profileText("Package View"), QStringLiteral("package-manager")),
				binding(QStringLiteral("editor.object.rename"), profileText("Rename Object"), QStringLiteral("F2"), QString(), profileText("Object Tree"), QStringLiteral("object-tree")),
			}),
	};
}

QStringList editorProfileIds()
{
	QStringList ids;
	for (const EditorProfileDescriptor& descriptor : editorProfileDescriptors()) {
		ids.push_back(descriptor.id);
	}
	return ids;
}

bool editorProfileForId(const QString& id, EditorProfileDescriptor* out)
{
	const QString normalized = normalizedEditorProfileId(id);
	for (const EditorProfileDescriptor& descriptor : editorProfileDescriptors()) {
		if (normalizedEditorProfileId(descriptor.id) == normalized) {
			if (out) {
				*out = descriptor;
			}
			return true;
		}
	}
	return false;
}

QString editorProfileDisplayNameForId(const QString& id)
{
	EditorProfileDescriptor descriptor;
	if (editorProfileForId(id, &descriptor)) {
		return descriptor.displayName;
	}
	return editorProfileDisplayNameForId(defaultEditorProfileId());
}

QString editorProfileSummaryText(const EditorProfileDescriptor& profile)
{
	QStringList lines;
	lines << QStringLiteral("%1 [%2]").arg(profile.displayName, profile.id);
	lines << QCoreApplication::translate("VibeStudioEditorProfiles", "Lineage: %1").arg(profile.lineage);
	lines << QCoreApplication::translate("VibeStudioEditorProfiles", "Layout preset: %1").arg(profile.layoutPresetId);
	lines << QCoreApplication::translate("VibeStudioEditorProfiles", "Camera preset: %1").arg(profile.cameraPresetId);
	lines << QCoreApplication::translate("VibeStudioEditorProfiles", "Selection preset: %1").arg(profile.selectionPresetId);
	lines << QCoreApplication::translate("VibeStudioEditorProfiles", "Grid preset: %1").arg(profile.gridPresetId);
	lines << QCoreApplication::translate("VibeStudioEditorProfiles", "Terminology: %1").arg(profile.terminologyPresetId);
	lines << QCoreApplication::translate("VibeStudioEditorProfiles", "Engines: %1").arg(profile.supportedEngineFamilies.join(QStringLiteral(", ")));
	lines << QCoreApplication::translate("VibeStudioEditorProfiles", "Panels: %1").arg(profile.defaultPanels.join(QStringLiteral(", ")));
	lines << profile.description;
	if (!profile.workflowNotes.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioEditorProfiles", "Workflow notes:");
		for (const QString& note : profile.workflowNotes) {
			lines << QStringLiteral("- %1").arg(note);
		}
	}
	if (!profile.bindings.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioEditorProfiles", "Command bindings:");
		for (const EditorProfileBinding& binding : profile.bindings) {
			const QString gesture = binding.mouseGesture.isEmpty() ? binding.shortcut : binding.mouseGesture;
			lines << QStringLiteral("- %1 [%2 -> %3 @ %4]: %5").arg(binding.displayName, binding.actionId, binding.commandId, binding.surfaceId, gesture.isEmpty() ? QCoreApplication::translate("VibeStudioEditorProfiles", "unassigned") : gesture);
		}
	}
	return lines.join('\n');
}

bool editorProfileBindingForAction(const EditorProfileDescriptor& profile, const QString& actionId, EditorProfileBinding* out)
{
	const QString normalized = actionId.trimmed().toLower();
	for (const EditorProfileBinding& binding : profile.bindings) {
		if (binding.actionId.trimmed().toLower() == normalized) {
			if (out) {
				*out = binding;
			}
			return true;
		}
	}
	return false;
}

bool editorProfileHasShortcutConflict(const EditorProfileDescriptor& profile, QString* conflict)
{
	QVector<EditorProfileBinding> seen;
	for (const EditorProfileBinding& binding : profile.bindings) {
		const QString shortcut = binding.shortcut.trimmed().toLower();
		if (shortcut.isEmpty()) {
			continue;
		}
		const QString surface = binding.surfaceId.trimmed().toLower();
		for (const EditorProfileBinding& previous : seen) {
			if (previous.shortcut.trimmed().toLower() == shortcut && previous.surfaceId.trimmed().toLower() == surface) {
				if (conflict) {
					*conflict = QCoreApplication::translate("VibeStudioEditorProfiles", "%1 conflicts with %2 on %3 using %4").arg(binding.actionId, previous.actionId, binding.surfaceId, binding.shortcut);
				}
				return true;
			}
		}
		seen.push_back(binding);
	}
	return false;
}

} // namespace vibestudio
