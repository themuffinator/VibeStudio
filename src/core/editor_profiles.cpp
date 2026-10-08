#include "core/editor_profiles.h"

#include "core/studio_semantics.h"

#include <QCoreApplication>
#include <QHash>
#include <QEvent>
#include <QPointer>
#include <QThread>
#include <algorithm>

namespace vibestudio {

namespace {

// Descriptor construction begins unresolved; attaching concrete viewport
// controls resolves these categories. IDs alone never imply implementation.
QStringList unresolvedPresetKinds()
{
	return {
		QStringLiteral("layout"),
		QStringLiteral("camera"),
		QStringLiteral("selection"),
		QStringLiteral("grid"),
	};
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
	// Computed, not asserted: only commands the shell registry knows about can
	// be honestly reported as implemented.
	const bool implemented = shellCommandIdExists(normalizedCommandId);
	return {actionId, displayName, shortcut, mouseGesture, context, normalizedCommandId, surfaceId.trimmed(), implemented};
}

// A binding that remaps a documented shell command. The action id mirrors the
// command id so lookups stay predictable.
EditorProfileBinding shellBinding(
	const QString& commandId,
	const QString& displayName,
	const QString& shortcut,
	const QString& context,
	const QString& surfaceId = QStringLiteral("shell"))
{
	return binding(commandId, displayName, shortcut, QString(), context, surfaceId, commandId);
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
		unresolvedPresetKinds(),
		// Placeholder while any preset kind still resolves to nothing. Shell
		// command remaps are live, but the editor presets are not.
		!unresolvedPresetKinds().isEmpty(),
		vibeStudioLevelControls(),
		{}, {}, {},
	};
}

// Gives a profile its own level editor controls: every preset now resolves,
// and each key the controls give a map command becomes a routed binding,
// replacing any the profile had for that command.
QString commandKey(QString id)
{
	return id.toLower().remove(QLatin1Char('-')).remove(QLatin1Char('_'));
}

EditorProfileDescriptor attachControls(EditorProfileDescriptor descriptor, const LevelEditorControls& controls, const QHash<QString, QString>& labels)
{
	descriptor.controls = controls;
	descriptor.layoutPresetId = levelViewLayoutId(controls.layout);
	for (const LevelEditorKeyBinding& key : controls.keys) {
		const QString wanted = normalizedEditorProfileId(key.commandId);
		for (int index = static_cast<int>(descriptor.bindings.size()) - 1; index >= 0; --index) {
			if (normalizedEditorProfileId(descriptor.bindings.at(index).commandId) == wanted) {
				descriptor.bindings.removeAt(index);
			}
		}
		const QString label = labels.value(commandKey(key.commandId), key.commandId);
		EditorProfileBinding binding = shellBinding(key.commandId, label, key.keys.join(QStringLiteral("; ")), QCoreApplication::translate("VibeStudioEditorProfiles", "Levels"), QStringLiteral("level-editor"));
		binding.clearsKeys = key.keys.isEmpty();
		descriptor.bindings.push_back(binding);
	}
	descriptor.unresolvedPresets.clear();
	descriptor.placeholder = false;
	return descriptor;
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

static QVector<EditorProfileDescriptor> buildEditorProfileDescriptors()
{
	// Build translated labels once per catalog, not once per binding. Rebuild
	// on each call so changing translators cannot leave stale UI language.
	QHash<QString, QString> labels;
	for (const auto& entry : commandPaletteEntries()) { labels.insert(commandKey(entry.commandId), entry.label); }
	for (const auto& entry : shortcutDescriptors()) { labels.insert(commandKey(entry.commandId), entry.label); }
	const auto withControls = [&labels](EditorProfileDescriptor descriptor, const LevelEditorControls& controls) {
		return attachControls(std::move(descriptor), controls, labels);
	};
	const QStringList allEngines = {
		QStringLiteral("idTech1"),
		QStringLiteral("idTech2"),
		QStringLiteral("idTech3"),
	};

	QVector<EditorProfileDescriptor> profiles {
		withControls(profile(
			QStringLiteral("vibestudio-default"),
			QCoreApplication::translate("VibeStudioEditorProfiles", "VibeStudio Default"),
			QCoreApplication::translate("VibeStudioEditorProfiles", "Default"),
			QCoreApplication::translate("VibeStudioEditorProfiles", "VibeStudio"),
			QCoreApplication::translate("VibeStudioEditorProfiles", "Balanced studio layout for users who want one consistent workflow across Doom, Quake, and Quake III-era projects."),
			QStringLiteral("studio-workbench"),
			QStringLiteral("hybrid-orbit-and-fly"),
			QStringLiteral("explicit-object-and-component"),
			QStringLiteral("adaptive-idtech-grid"),
			QStringLiteral("neutral-vibestudio"),
			allEngines,
			{
				QCoreApplication::translate("VibeStudioEditorProfiles", "Workspace"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Level View"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Asset Browser"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Inspector"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Compiler"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Activity"),
			},
			{
				QCoreApplication::translate("VibeStudioEditorProfiles", "Use summary-first panels with raw metadata available in drawers."),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Prefer explicit tools and visible operation state over hidden modal behavior."),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Keep Doom, Quake, and Quake III terminology neutral until a project profile narrows the context."),
			},
			{
				QCoreApplication::translate("VibeStudioEditorProfiles", "Standard file, edit, view, and command-palette shortcuts stay platform conventional."),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Profile bindings route to stable shell, package, compiler, and map-editor command identifiers."),
			},
			{
				QCoreApplication::translate("VibeStudioEditorProfiles", "Mouse bindings route to editor-surface commands and remain inactive outside their declared surface."),
			},
			{
				// The default profile keeps the documented shell sequences, so
				// its remaps are identity mappings of the registry defaults.
				shellBinding(QStringLiteral("shell.command-palette"), QCoreApplication::translate("VibeStudioEditorProfiles", "Command Palette"), QStringLiteral("Ctrl+Shift+P"), QCoreApplication::translate("VibeStudioEditorProfiles", "Global")),
				shellBinding(QStringLiteral("shell.focus-search"), QCoreApplication::translate("VibeStudioEditorProfiles", "Focus Workspace Search"), QStringLiteral("Ctrl+F"), QCoreApplication::translate("VibeStudioEditorProfiles", "Global")),
				shellBinding(QStringLiteral("package.save-as"), QCoreApplication::translate("VibeStudioEditorProfiles", "Save Package As"), QStringLiteral("Ctrl+Shift+S"), QCoreApplication::translate("VibeStudioEditorProfiles", "Package")),
				shellBinding(QStringLiteral("map.save-as"), QCoreApplication::translate("VibeStudioEditorProfiles", "Save Map As"), QStringLiteral("Ctrl+Alt+S"), QCoreApplication::translate("VibeStudioEditorProfiles", "Map")),
				shellBinding(QStringLiteral("compiler.run"), QCoreApplication::translate("VibeStudioEditorProfiles", "Run Compiler Profile"), QStringLiteral("Ctrl+R"), QCoreApplication::translate("VibeStudioEditorProfiles", "Compiler")),
				binding(QStringLiteral("view.focus-level"), QCoreApplication::translate("VibeStudioEditorProfiles", "Focus Level View"), QStringLiteral("F3"), QString(), QCoreApplication::translate("VibeStudioEditorProfiles", "Editor"), QStringLiteral("level-editor")),
				binding(QStringLiteral("view.focus-inspector"), QCoreApplication::translate("VibeStudioEditorProfiles", "Focus Inspector"), QStringLiteral("F8"), QString(), QCoreApplication::translate("VibeStudioEditorProfiles", "Inspector"), QStringLiteral("inspector")),
			}),
			vibeStudioLevelControls()),
		withControls(profile(
			QStringLiteral("gtkradiant-1-6"),
			QCoreApplication::translate("VibeStudioEditorProfiles", "GtkRadiant 1.6.0 Style"),
			QCoreApplication::translate("VibeStudioEditorProfiles", "GtkRadiant"),
			QCoreApplication::translate("VibeStudioEditorProfiles", "GtkRadiant 1.6.0"),
			QCoreApplication::translate("VibeStudioEditorProfiles", "GtkRadiant 1.6.0's controls: the camera beside a 2D view, Shift+click to select, the right button to pan the 2D view and to toggle free look in the camera, the arrows to drive it, Space to clone, and Backspace to delete."),
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
				QCoreApplication::translate("VibeStudioEditorProfiles", "Camera"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "XY View"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "XZ/YZ View"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Texture Browser"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Entity Inspector"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Build Menu"),
			},
			{
				QCoreApplication::translate("VibeStudioEditorProfiles", "Favor visible orthographic panes and a separate camera view."),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Keep brush/entity selection modes distinct."),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Expose grid stepping and clipping controls prominently."),
			},
			{
				QCoreApplication::translate("VibeStudioEditorProfiles", "Reserve Radiant-style camera, clipping, grid, and texture shortcuts for the map editor command table."),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Avoid overriding global platform shortcuts outside the editor context."),
			},
			{
				QCoreApplication::translate("VibeStudioEditorProfiles", "Reserve right-button camera movement and orthographic drag gestures for editor-only contexts."),
			},
			{
				// GtkRadiant treats Ctrl+O as "open map" and keeps project
				// selection on a modified sequence.
				shellBinding(QStringLiteral("map.open"), QCoreApplication::translate("VibeStudioEditorProfiles", "Open Map"), QStringLiteral("Ctrl+O"), QCoreApplication::translate("VibeStudioEditorProfiles", "Map")),
				shellBinding(QStringLiteral("project.open"), QCoreApplication::translate("VibeStudioEditorProfiles", "Open Project"), QStringLiteral("Ctrl+Shift+O"), QCoreApplication::translate("VibeStudioEditorProfiles", "Project")),
				shellBinding(QStringLiteral("compiler.run"), QCoreApplication::translate("VibeStudioEditorProfiles", "Run Compiler Profile"), QStringLiteral("Ctrl+B"), QCoreApplication::translate("VibeStudioEditorProfiles", "Compiler")),
				shellBinding(QStringLiteral("shell.command-palette"), QCoreApplication::translate("VibeStudioEditorProfiles", "Command Palette"), QStringLiteral("Ctrl+Shift+P"), QCoreApplication::translate("VibeStudioEditorProfiles", "Global")),
				// GtkRadiant tools VibeStudio does not have yet, on their keys.
				binding(QStringLiteral("editor.drag-edges"), QCoreApplication::translate("VibeStudioEditorProfiles", "Drag Edges"), QStringLiteral("E"), QString(), QCoreApplication::translate("VibeStudioEditorProfiles", "Map Editor"), QStringLiteral("level-editor")),
				binding(QStringLiteral("editor.drag-vertices"), QCoreApplication::translate("VibeStudioEditorProfiles", "Drag Vertices"), QStringLiteral("V"), QString(), QCoreApplication::translate("VibeStudioEditorProfiles", "Map Editor"), QStringLiteral("level-editor")),
				binding(QStringLiteral("editor.mouse-rotate"), QCoreApplication::translate("VibeStudioEditorProfiles", "Mouse Rotate"), QStringLiteral("R"), QString(), QCoreApplication::translate("VibeStudioEditorProfiles", "Map Editor"), QStringLiteral("level-editor")),
				binding(QStringLiteral("editor.make-detail"), QCoreApplication::translate("VibeStudioEditorProfiles", "Make Detail"), QStringLiteral("Ctrl+M"), QString(), QCoreApplication::translate("VibeStudioEditorProfiles", "Map Editor"), QStringLiteral("level-editor")),
			}),
			gtkRadiantLevelControls()),
		withControls(profile(
			QStringLiteral("netradiant-custom"),
			QCoreApplication::translate("VibeStudioEditorProfiles", "NetRadiant Custom Style"),
			QCoreApplication::translate("VibeStudioEditorProfiles", "NetRadiant"),
			QCoreApplication::translate("VibeStudioEditorProfiles", "NetRadiant Custom"),
			QCoreApplication::translate("VibeStudioEditorProfiles", "NetRadiant Custom's controls: the camera beside a 2D view, Shift to select, the right button to pan the 2D view and to look with the camera, the middle button to aim the camera, and Space to clone."),
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
				QCoreApplication::translate("VibeStudioEditorProfiles", "Camera"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Orthographic Views"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Filters"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Texture Tools"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Entity Inspector"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "q3map2 Build"),
			},
			{
				QCoreApplication::translate("VibeStudioEditorProfiles", "Make filters, texture projection, and compile profiles easy to reach."),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Keep dense toolbars optional so accessibility scaling remains viable."),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Treat q3map2 compile profiles as first-class build actions."),
			},
			{
				QCoreApplication::translate("VibeStudioEditorProfiles", "Reserve selection, texture-fit, filter, and build shortcuts for editor-command routing."),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Keep compiler shortcuts routed through reviewable command manifests."),
			},
			{
				QCoreApplication::translate("VibeStudioEditorProfiles", "Reserve Radiant-style camera and manipulation gestures with clear cursor/focus feedback."),
			},
			{
				shellBinding(QStringLiteral("map.open"), QCoreApplication::translate("VibeStudioEditorProfiles", "Open Map"), QStringLiteral("Ctrl+O"), QCoreApplication::translate("VibeStudioEditorProfiles", "Map")),
				shellBinding(QStringLiteral("project.open"), QCoreApplication::translate("VibeStudioEditorProfiles", "Open Project"), QStringLiteral("Ctrl+Shift+O"), QCoreApplication::translate("VibeStudioEditorProfiles", "Project")),
				shellBinding(QStringLiteral("compiler.run"), QCoreApplication::translate("VibeStudioEditorProfiles", "Run Compiler Profile"), QStringLiteral("Ctrl+B"), QCoreApplication::translate("VibeStudioEditorProfiles", "Compiler")),
				shellBinding(QStringLiteral("build.run-pipeline"), QCoreApplication::translate("VibeStudioEditorProfiles", "Run Build Pipeline"), QStringLiteral("Ctrl+Shift+B"), QCoreApplication::translate("VibeStudioEditorProfiles", "Build")),
				binding(QStringLiteral("editor.filters.toggle-caulk"), QCoreApplication::translate("VibeStudioEditorProfiles", "Toggle Caulk Filter"), QString(), QString(), QCoreApplication::translate("VibeStudioEditorProfiles", "Map Editor"), QStringLiteral("level-editor")),
				binding(QStringLiteral("editor.texture.fit"), QCoreApplication::translate("VibeStudioEditorProfiles", "Fit Texture"), QStringLiteral("Ctrl+F"), QString(), QCoreApplication::translate("VibeStudioEditorProfiles", "Texture Tools"), QStringLiteral("texture-tools")),
				binding(QStringLiteral("compiler.q3map2.plan"), QCoreApplication::translate("VibeStudioEditorProfiles", "Plan q3map2 Build"), QString(), QString(), QCoreApplication::translate("VibeStudioEditorProfiles", "Compiler"), QStringLiteral("compiler")),
				binding(QStringLiteral("editor.selection.expand"), QCoreApplication::translate("VibeStudioEditorProfiles", "Expand Selection"), QStringLiteral("Shift+E"), QString(), QCoreApplication::translate("VibeStudioEditorProfiles", "Map Editor"), QStringLiteral("level-editor")),
			}),
			netRadiantCustomLevelControls()),
		withControls(profile(
			QStringLiteral("trenchbroom"),
			QCoreApplication::translate("VibeStudioEditorProfiles", "TrenchBroom Style"),
			QCoreApplication::translate("VibeStudioEditorProfiles", "TrenchBroom"),
			QCoreApplication::translate("VibeStudioEditorProfiles", "TrenchBroom"),
			QCoreApplication::translate("VibeStudioEditorProfiles", "TrenchBroom's controls: one view, 3D first, the right button to look and to pan the 2D views, a drag over empty space to draw a brush, and WASD with Q and X to fly."),
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
				QCoreApplication::translate("VibeStudioEditorProfiles", "3D View"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Map Inspector"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Face Tools"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Entity Browser"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Texture Browser"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Build Console"),
			},
			{
				QCoreApplication::translate("VibeStudioEditorProfiles", "Prefer a large 3D canvas with contextual inspectors around it."),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Expose face, edge, vertex, brush, and entity modes without changing the underlying command stack."),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Keep project/game configuration visible beside map and compiler state."),
			},
			{
				QCoreApplication::translate("VibeStudioEditorProfiles", "Reserve component editing shortcuts for the editor context and keep shell navigation conventional."),
			},
			{
				QCoreApplication::translate("VibeStudioEditorProfiles", "Reserve fly-camera and component selection gestures for the 3D view."),
			},
			{
				shellBinding(QStringLiteral("map.open"), QCoreApplication::translate("VibeStudioEditorProfiles", "Open Map"), QStringLiteral("Ctrl+O"), QCoreApplication::translate("VibeStudioEditorProfiles", "Map")),
				shellBinding(QStringLiteral("map.save-as"), QCoreApplication::translate("VibeStudioEditorProfiles", "Save Map As"), QStringLiteral("Ctrl+Shift+S"), QCoreApplication::translate("VibeStudioEditorProfiles", "Map")),
				shellBinding(QStringLiteral("compiler.run"), QCoreApplication::translate("VibeStudioEditorProfiles", "Run Compiler Profile"), QStringLiteral("Ctrl+B"), QCoreApplication::translate("VibeStudioEditorProfiles", "Compiler")),
				shellBinding(QStringLiteral("game.launch"), QCoreApplication::translate("VibeStudioEditorProfiles", "Launch Game"), QStringLiteral("Ctrl+L"), QCoreApplication::translate("VibeStudioEditorProfiles", "Game")),
				binding(QStringLiteral("editor.mode.face"), QCoreApplication::translate("VibeStudioEditorProfiles", "Face Mode"), QStringLiteral("F"), QString(), QCoreApplication::translate("VibeStudioEditorProfiles", "Map Editor"), QStringLiteral("level-editor")),
				binding(QStringLiteral("editor.mode.vertex"), QCoreApplication::translate("VibeStudioEditorProfiles", "Vertex Mode"), QStringLiteral("V"), QString(), QCoreApplication::translate("VibeStudioEditorProfiles", "Map Editor"), QStringLiteral("level-editor")),
				binding(QStringLiteral("editor.texture.apply"), QCoreApplication::translate("VibeStudioEditorProfiles", "Apply Texture"), QString(), QString(), QCoreApplication::translate("VibeStudioEditorProfiles", "Face Tools"), QStringLiteral("texture-tools")),
			}),
			trenchBroomLevelControls()),
		withControls(profile(
			QStringLiteral("quark"),
			QCoreApplication::translate("VibeStudioEditorProfiles", "QuArK Style"),
			QCoreApplication::translate("VibeStudioEditorProfiles", "QuArK"),
			QCoreApplication::translate("VibeStudioEditorProfiles", "QuArK"),
			QCoreApplication::translate("VibeStudioEditorProfiles", "Explorer-heavy integrated preset for users who prefer package, asset, map, object, and property context side by side."),
			QStringLiteral("explorer-integrated-mdi"),
			QStringLiteral("quark-linked-views"),
			QStringLiteral("object-tree-property"),
			QStringLiteral("hierarchical-grid"),
			QStringLiteral("quark"),
			allEngines,
			{
				QCoreApplication::translate("VibeStudioEditorProfiles", "Explorer"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Map View"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Object Tree"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Property Editor"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Package View"),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Log"),
			},
			{
				QCoreApplication::translate("VibeStudioEditorProfiles", "Keep map, package, and object structure visible together."),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Favor property editing and tree navigation for integrated workflows."),
				QCoreApplication::translate("VibeStudioEditorProfiles", "Expose package and asset context without making package editing destructive."),
			},
			{
				QCoreApplication::translate("VibeStudioEditorProfiles", "Reserve object tree, property editing, and multi-document shortcuts for editor-command routing."),
			},
			{
				QCoreApplication::translate("VibeStudioEditorProfiles", "Reserve tree selection and linked-view gestures for the integrated explorer surface."),
			},
			{
				shellBinding(QStringLiteral("map.open"), QCoreApplication::translate("VibeStudioEditorProfiles", "Open Map"), QStringLiteral("Ctrl+O"), QCoreApplication::translate("VibeStudioEditorProfiles", "Map")),
				shellBinding(QStringLiteral("package.open"), QCoreApplication::translate("VibeStudioEditorProfiles", "Open Package"), QStringLiteral("Ctrl+Shift+O"), QCoreApplication::translate("VibeStudioEditorProfiles", "Package")),
				shellBinding(QStringLiteral("package.stage-rename"), QCoreApplication::translate("VibeStudioEditorProfiles", "Stage Rename"), QStringLiteral("F2"), QCoreApplication::translate("VibeStudioEditorProfiles", "Package"), QStringLiteral("package-manager")),
				shellBinding(QStringLiteral("shell.focus-search"), QCoreApplication::translate("VibeStudioEditorProfiles", "Focus Explorer Search"), QStringLiteral("Ctrl+F"), QCoreApplication::translate("VibeStudioEditorProfiles", "Explorer")),
				binding(QStringLiteral("editor.object.properties"), QCoreApplication::translate("VibeStudioEditorProfiles", "Object Properties"), QStringLiteral("Alt+Enter"), QString(), QCoreApplication::translate("VibeStudioEditorProfiles", "Object Tree"), QStringLiteral("object-tree")),
				binding(QStringLiteral("editor.tree.focus"), QCoreApplication::translate("VibeStudioEditorProfiles", "Focus Object Tree"), QStringLiteral("F6"), QString(), QCoreApplication::translate("VibeStudioEditorProfiles", "Object Tree"), QStringLiteral("object-tree")),
				binding(QStringLiteral("editor.object.rename"), QCoreApplication::translate("VibeStudioEditorProfiles", "Rename Object"), QStringLiteral("F2"), QString(), QCoreApplication::translate("VibeStudioEditorProfiles", "Object Tree"), QStringLiteral("object-tree")),
			}), familiarLevelControls(QStringLiteral("quark"))),
	};
	// Facts about documented behavior only. The adaptations are intentionally
	// visible: familiar controls do not imply another editor's file support.
	struct FamiliarProfile {
		const char* id;
		const char* name;
		const char* family;
		const char* description;
		const char* adaptation;
		const char* url;
		const char* aliases;
	};
	static const FamiliarProfile catalog[] = {
		{"gtkradiant-1-4", QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "GtkRadiant 1.4 Style"), "GtkRadiant 1.4",
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Classic GtkRadiant brush workflow: camera beside plan, 8-unit grid, Shift selection, Alt area selection, Shift+right plan zoom and right-click free look."),
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Based on the 1.4.0-era ZeroRadiant source: fixed arrow/comma/period/D/C camera steps, A/Z pitch, Space cloning, Backspace deletion and Shift+B texture fitting. Surface keys use the studio target and step settings. Native component modes, floor stepping, Z-checker, preference import and selected-set texture application are not emulated."),
			"https://github.com/TTimo/GtkRadiant/tree/5fc27697b313ddb925e57605c9983f5727a3c19f", "gtkradiant-1.4|gtkradiant-1.4.0|gtkradiant-1-4-0|gtk-radiant-1-4|gtk14"},
		{"gtkradiant-1-5", QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "GtkRadiant 1.5 Style"), "GtkRadiant 1.5",
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "GtkRadiant 1.5 selection workflow: Shift area selection, Shift+Alt selection cycling, right-click free look with arrow-key flight, 8-unit grid and Shift+B texture fitting."),
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Camera steps outside free look include A/Z pitch; free-look arrows translate. Middle samples a surface and Ctrl+Shift+middle pastes its definition onto the hit face. Area selection adds to the selection rather than toggling its members. Native manipulators, replacement-area selection, fractional grids and Doom 3 authoring are not added by this profile."),
			"https://github.com/TTimo/GtkRadiant/tree/017673373699174b574c92a262496826a6b409e9", "gtkradiant-1.5|gtkradiant-1.5.0|gtkradiant-1-5-0|gtk-radiant-1-5|gtk15"},
		{"qeradiant", QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "QeRadiant Style"), "QeRadiant",
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Classic Quake II Radiant workflow: right-button camera steering, Ctrl+right pan, Shift selection, Space cloning, Backspace deletion and Shift+5 or Ctrl+F texture fitting."),
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Uses the shared classic Radiant camera and brush services. Q3-only patch, hide/show and texture-lock keys are unassigned. Ctrl+F fits the Surfaces target while a map view has focus. Whole-entity selection mode, animated entity previews, Alt+right texture dragging, Z-checker and native preference import are not emulated."),
			"https://icculus.org/gtkradiant/documentation/q3radiant_manual/appndx/sskey_dl.htm", "qe-radiant|qer|qe|quake2-radiant|quake-ii-radiant"},
		{"q3radiant", QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Q3Radiant Style"), "Q3Radiant",
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Classic id Software Radiant: camera beside plan, 8-unit grid, right-button position steering, Ctrl+right pan, fixed arrow/comma/period/D/C movement and A/Z pitch steps."),
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Space clones, Backspace deletes, Insert/Delete zoom, S opens Surface Alignment, Shift+S edits patches and Ctrl+U merges brushes. Shift+arrows shift texture U/V, Shift+Page Up/Down rotate and Shift+5 fits using the Surfaces target and studio steps. Rotation is centre-anchored; native camera-relative UV shifts and additive scale keys are not emulated. Native preference import, Z-checker, floor stepping, End camera levelling, terrain, bend/rotation modes and UV-copying texture gestures remain unsupported. Pan speed and vertical field-of-view sizing follow the studio camera."),
			"https://github.com/id-Software/Quake-III-Arena/tree/dbe4ddb10315479fc00086f08e25d968b4b43c49/q3radiant", "q3-radiant|quake3-radiant|quake-iii-radiant"},
		{"doomedit", QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "DoomEdit Style"), "DoomEdit",
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Classic camera steering with right drag, Ctrl+right pan and Ctrl+Shift+right mouse look. Shift+M merges brushes, Ctrl+Shift+H isolates and Home or Ctrl+Tab cycles the plan projection."),
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Adapts the Doom 3 GPL editor controls to supported idTech1/2/3 maps. Uses the shared 8-unit grid, brush/patch tools and fixed camera steps. Doom 3 render mode, lights, materials, fractional grids, axial texture tools, floor stepping and native texture gestures are not emulated. Ctrl+U and Shift+U are not assigned brush operations; I remains unbound. This profile adds no Doom 3 or idStudio format support."),
			"https://github.com/id-Software/DOOM-3/tree/a9c49da5afb18201d31e3f0a429a037e56ce2b9a/neo/tools/radiant", "doom-edit|doom3-radiant|doom3-edit|doom-3-editor|d3radiant"},
		{"bsp", QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "BSP Quake Editor Style"), "BSP",
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "BSP 0.97 camera controls: WASD movement, R/F elevation, Q/E turning, middle-drag look, Shift+middle pan and right-click material sampling. Ctrl+Space clones, Ctrl+X deletes and Z opens surface alignment."),
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Uses four shared panes and a 16-unit grid. Shift selects in the plan; right drag pans. Plain camera clicks replace selection and Ctrl toggles it. Shift selection adds rather than cycling on hold; camera relocation, native texture drags and apply gestures, alternate mouse configurations, region bounds, clip-point modes and BSP settings import are not emulated. Ctrl+Shift+Y frames the focused view; Alt+R reveals shared hidden objects. Delete pitches the camera down; use Ctrl+X or keypad minus to delete."),
			"https://www.bspquakeeditor.com/", "bsp-editor|bsp-quake-editor|bsp-0.97|bsp97"},
		{"netradiant", QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "NetRadiant Style"), "NetRadiant",
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Standalone Xonotic NetRadiant controls: camera beside plan, 8-unit grid, 110-degree camera, Alt+right plan zoom, Delete/Insert zoom and Space cloning."),
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Distinct from NetRadiant Custom: Backspace or Z deletes, Escape or C deselects, Shift+A selects similar and Ctrl+Y redoes. Backtick or Ctrl+Shift+Tab frames the focused view. Camera Tab focus, discrete movement, floor stepping, right-button selection painting and unique cloning are not emulated; Tab keeps keyboard focus navigation."),
			"https://github.com/xonotic/netradiant/tree/b4b295d7a37797cc2752e48aa8ce42492e7016f0/radiant", "net-radiant|xonotic-netradiant|netradiant-classic"},
		{"sledge", QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Sledge Style"), "Sledge",
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Sledge 2.0.7.2 four-view workflow: hold Space to navigate, Z mouse look, WASD flight, Q/E elevation, arrow-key look, Shift+arrow panning and Shift+Z maximize."),
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Ctrl+A selects all, Ctrl+H isolates, Ctrl+Shift+H hollows and F9 uses the shared build/test pipeline. In mouse look, right mouse pans and left+right moves along the view. Navigation speed, numeric zoom presets, linked wheel zoom, camera-tool gestures, duplicate-on-drag, grouping semantics and tool modes differ. Native Sledge preferences and RMF/VMF formats are not imported by this profile."),
			"https://github.com/LogicAndTrick/sledge/tree/8762a6de07a9fa486d51aff0913cdc0306fd775c", "sledge-editor|sledge2|sledge-2"},
		{"hammer", QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Hammer / Worldcraft Style"), "Hammer",
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Four views, Z mouse look, WASD flight, bracket grid steps and F9 build/test for Worldcraft, Hammer and Hammer++ users."),
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Classic Hammer uses Ctrl+H to hollow, Shift+Z to maximize/restore the focused pane and Ctrl+A to equalize views. Middle drag looks; right drag pans the camera; middle drag pans the plan. Hammer++ aliases select these classic controls and do not emulate its extensions. Space-drag, duplicate-on-drag, displacement tools, Source 2 modes and VMF/RMF support are not emulated. Use the shared Duplicate and Select All commands."),
			"https://developer.valvesoftware.com/wiki/Hammer_Hotkey_Reference", "worldcraft|valve-hammer|hammer++|hammer-plus-plus"},
		{"jack", QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "J.A.C.K. Style"), "J.A.C.K.",
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Hammer-family four-view brush workflow, clipping, carving, hollowing and the shared build/test pipeline."),
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Uses the Hammer-family navigation adaptation, with J.A.C.K.'s Ctrl+H isolation and Ctrl+U or Ctrl+Shift+H hollowing. Ctrl+D is not used for map duplication. Studio file shortcuts remain available; JMF metadata, detail/structural modes, texture-tool function keys and texture application gestures are not emulated."),
			"https://jack.hlfx.ru/en/articles.html", "j.a.c.k.|jackhammer"},
		{"darkradiant", QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "DarkRadiant Style"), "DarkRadiant",
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Camera beside plan, Shift selection, right-button free look, Shift+right zoom and familiar Radiant grid and clipping controls."),
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Layers, groups, patches and prefabs use VibeStudio services. Fractional grid steps, Dark Mod game connection and Doom 3/Quake 4 authoring are not added by choosing these controls."),
			"https://www.darkradiant.net/userguide/", "dark-radiant"},
		{"doom-builder", QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Doom Builder 2 / X Style"), "Doom Builder",
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Plan-first Doom workflow, W visual view, ESDF flight, C deselect, Ctrl+D sector drawing and F9 build/test."),
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Use right drag to look, middle drag to pan and Ctrl+click to toggle selection. Crosshair editing, right-drag movement, gravity, V/L/S/T modal tools and wheel surface-height editing are not emulated."),
			"https://github.com/UltimateDoomBuilder/UltimateDoomBuilder/blob/6d9f6038db30adfee0edd74221b74b2de4837f6f/Help/e_visual.html", "doom-builder-2|doombuilder|doombuilder2|doom-builder-x|db2|dbx"},
		{"ultimate-doom-builder", QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Ultimate Doom Builder Style"), "Ultimate Doom Builder",
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Doom Builder-family editing with Q visual view, ESDF flight, bracket grid steps and the shared Doom/Hexen build workflow."),
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Uses the Doom Builder navigation adaptation with Q for the enhanced visual view. UDMF properties and native transforms are available; slopes, 3D floors, gravity and complete source-port visual effects remain unsupported."),
			"https://github.com/UltimateDoomBuilder/UltimateDoomBuilder/blob/6d9f6038db30adfee0edd74221b74b2de4837f6f/Source/Plugins/BuilderModes/Resources/Actions.cfg", "udb|gzdoom-builder|gzdb|gzdoom-builder-bugfix"},
		{"slade", QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "SLADE Style"), "SLADE",
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Plan-first map/package workflow, right-drag plan pan, Q visual view, WASD flight and Shift+G snapping."),
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Right drag looks in the camera; painting and sampling are explicit tools. V/L/S/T modal editing, visual-mode texture gestures and wheel height changes are not emulated. Packages keep their own shortcuts."),
			"https://github.com/sirjuddington/SLADE/blob/351fd983fa986423c1825c87128691c27e0817aa/src/General/KeyBind.cpp", "slade3|slade-3"},
		{"eureka", QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Eureka Style"), "Eureka",
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Plan-first Doom editing, middle-drag pan, Tab visual view, WASD and arrow navigation, Shift for slower flight and O duplication."),
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Use right drag to look and Ctrl+click to toggle selection. Grid digits cover 2 through 256 units; 512/1024, letter-held scrolling, right-button line drawing and V/L/S/T modal editing remain unimplemented."),
			"https://github.com/ioan-chera/eureka-editor/blob/f951281878f1f8f2619e0073592b6995aeea0dfc/bindings.cfg", "eureka-doom"},
		{"unreal", QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Unreal Editor Style"), "Unreal Editor",
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Large perspective view, right-button WASD flight with E/Q elevation, Alt+left orbit, F framing and Ctrl+W duplication."),
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Transforms use VibeStudio handles and numeric tools. Unreal assets, landscape, Blueprint, left-button camera driving, W/E/R gizmo modes and wheel flight-speed changes are not emulated."),
			"https://dev.epicgames.com/documentation/en-us/unreal-engine/viewport-controls-in-unreal-engine", "unreal-editor|ue4|ue5"},
		{"unity", QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Unity Scene View Style"), "Unity",
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Perspective scene workflow, right-button WASD flight, Alt+left orbit, middle-drag pan, F framing and Ctrl+D duplication."),
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Uses idTech units and Z-up coordinates. The wheel dollies; Alt+right zoom, Q/W/E/R tool modes, Unity assets and live play mode are not emulated."),
			"https://docs.unity3d.com/Manual/SceneViewNavigation.html", "unity-editor|unity-scene-view"},
		{"godot", QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Godot 3D Style"), "Godot",
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Middle-drag orbit, Shift+middle pan, right-button WASD flight with E/Q elevation, Shift+F free look, F framing and Y snapping."),
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Uses idTech units and Z-up coordinates. Godot nodes, W/E/R gizmos, keypad perspective switching and wheel flight-speed changes are not emulated; the Scene tab uses map layers and groups."),
			"https://docs.godotengine.org/en/stable/tutorials/3d/introduction_to_3d.html", "godot-3d|godot-editor"},
		{"blender", QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Blender Style"), "Blender",
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Middle-drag orbit, Shift+middle pan, keypad views/framing, Shift+D duplication, H hiding and Alt+H reveal."),
			QT_TRANSLATE_NOOP("VibeStudioEditorProfiles", "Shift+backtick enables mouse look; right-button flight is also available. Duplication commits one undoable edit. Blender mesh modes, G/R/S modal transforms, walk gravity and .blend files are not emulated."),
			"https://docs.blender.org/manual/en/latest/editors/3dview/navigate/index.html", "blender-3d"},
	};
	for (const auto& entry : catalog) {
		const QString id = QString::fromLatin1(entry.id);
		auto descriptor = withControls(profile(id, QCoreApplication::translate("VibeStudioEditorProfiles", entry.name),
			QString::fromUtf8(entry.family), QString::fromUtf8(entry.family), QCoreApplication::translate("VibeStudioEditorProfiles", entry.description),
			QString(), id + QStringLiteral("-camera"), id + QStringLiteral("-selection"), id + QStringLiteral("-grid"), id,
			allEngines, profiles.first().defaultPanels, profiles.first().workflowNotes, profiles.first().keybindingNotes,
			profiles.first().mouseBindingNotes, {}), familiarLevelControls(id));
		descriptor.aliases = QString::fromLatin1(entry.aliases).split(QLatin1Char('|'));
		descriptor.adaptations = {QCoreApplication::translate("VibeStudioEditorProfiles", entry.adaptation)};
		descriptor.referenceUrl = QString::fromLatin1(entry.url);
		profiles.push_back(descriptor);
	}
	for (auto& descriptor : profiles) {
		if (descriptor.id == QLatin1String("gtkradiant-1-6")) {
			descriptor.aliases = {QStringLiteral("gtkradiant"), QStringLiteral("gtk-radiant"), QStringLiteral("gtk16"),
				QStringLiteral("gtkradiant-1.6"), QStringLiteral("gtkradiant-1.6.0"), QStringLiteral("gtkradiant-1-6-0")};
			descriptor.referenceUrl = QStringLiteral("https://github.com/TTimo/GtkRadiant/tree/270af88f3c2471f6773bded0b5760a3115b52965");
		} else if (descriptor.id == QLatin1String("netradiant-custom")) {
			descriptor.aliases = {QStringLiteral("nrc"), QStringLiteral("netradiantcustom"), QStringLiteral("net-radiant-custom")};
			descriptor.referenceUrl = QStringLiteral("https://github.com/Garux/netradiant-custom");
		} else if (descriptor.id == QLatin1String("trenchbroom")) {
			descriptor.aliases = {QStringLiteral("tb"), QStringLiteral("trench-broom"), QStringLiteral("trenchbroom-2")};
			descriptor.referenceUrl = QStringLiteral("https://trenchbroom.github.io/");
		}
		if (descriptor.id == QLatin1String("gtkradiant-1-6") || descriptor.id == QLatin1String("gtkradiant-1-4")
			|| descriptor.id == QLatin1String("q3radiant") || descriptor.id == QLatin1String("qeradiant")) {
			descriptor.adaptations.append(QCoreApplication::translate("VibeStudioEditorProfiles", "Middle click samples the material and copies a brush face's mapping and flags. Shift+middle paints one material while preserving alignment; Ctrl+middle pastes the copied definition onto the hit brush, Ctrl+Shift+middle onto the hit face. Paste keeps the copied material even if the picker changes and requires matching mapping formats; World projection and Seamless wrap are explicit Surfaces options. Tool and selection remain unchanged. Native selected-set application, brush-depth and light-color sampling remain unsupported."));
		} else if (descriptor.id == QLatin1String("netradiant")) {
			descriptor.adaptations.append(QCoreApplication::translate("VibeStudioEditorProfiles", "Middle click samples a brush face's material, mapping and flags. Shift+middle pastes these values onto the hit face while keeping tool and selection. The copied material stays fixed when the picker changes. Native Ctrl and Ctrl+Shift paste aliases are unbound; choose a preferred chord in Gesture Preferences. World projection and Seamless wrap are available in Surfaces. Patch mapping, brush-depth and light-color sampling remain unsupported."));
		} else if (descriptor.id == QLatin1String("netradiant-custom")) {
			descriptor.adaptations.append(QCoreApplication::translate("VibeStudioEditorProfiles", "Middle click copies a brush surface. Shift+middle pastes values onto the hit and selection, keeping Valve axes and patch UVs; copied flags apply only to the hit brush face. Ctrl+middle wraps one face and advances the source. Alt+Shift pastes mapping values only; Alt+Ctrl wraps mapping only, using source and target dimensions for primitive texel density. Ctrl+Shift projects onto the hit and selected brushes/patches; Alt+Ctrl+Shift projects mapping only. Projection copies classic/Valve parameters and projects primitive matrices and patch UVs; perpendicular faces can become edge-on. Map-wide Valve conversion needs Surfaces consent. Tool and selection stay fixed. Drag wrapping, patch-source copying/wrapping and depth/light sampling remain unsupported. All roles can be customized."));
		}
		if (descriptor.id == QLatin1String("quark")) {
			descriptor.aliases = {QStringLiteral("quake-army-knife")};
			descriptor.referenceUrl = QStringLiteral("https://quark.sourceforge.io/infobase/intro.mapeditor.html");
			descriptor.adaptations = {QCoreApplication::translate("VibeStudioEditorProfiles", "Four synchronized panes replace QuArK's MDI layout. Right drag pans the plan, middle drag zooms, Ctrl toggles selection and arrow/D/C keys drive the camera. QuArK's auxiliary selection keys, tree hotkeys, multi-button gestures and native project format are not emulated.")};
		}
	}
	return profiles;
}

QVector<EditorProfileDescriptor> editorProfileDescriptors()
{
	// GUI settings, command installation and accessible text repeatedly query
	// the catalog. Cache on the application thread, invalidating on translator
	// changes. Worker callers build local values and never touch a GUI QObject.
	auto* app = QCoreApplication::instance();
	if (!app || QThread::currentThread() != app->thread()) { return buildEditorProfileDescriptors(); }
	class Catalog final : public QObject {
	public:
		explicit Catalog(QCoreApplication* owner) : QObject(owner) { owner->installEventFilter(this); }
		QVector<EditorProfileDescriptor> values()
		{
			if (dirty) { cached = buildEditorProfileDescriptors(); dirty = false; }
			return cached;
		}
	protected:
		bool eventFilter(QObject*, QEvent* event) override
		{
			if (event->type() == QEvent::LanguageChange) { dirty = true; }
			return false;
		}
	private:
		bool dirty = true;
		QVector<EditorProfileDescriptor> cached;
	};
	static QPointer<Catalog> catalog;
	if (!catalog) { catalog = new Catalog(app); }
	return catalog->values();
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
		if (normalizedEditorProfileId(descriptor.id) == normalized || std::any_of(descriptor.aliases.cbegin(), descriptor.aliases.cend(),
			[&normalized](const QString& alias) { return normalizedEditorProfileId(alias) == normalized; })) {
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
	if (!profile.aliases.isEmpty()) { lines << QCoreApplication::translate("VibeStudioEditorProfiles", "Also known as: %1").arg(profile.aliases.join(QStringLiteral(", "))); }
	for (const QString& note : profile.adaptations) { lines << QCoreApplication::translate("VibeStudioEditorProfiles", "Adaptation: %1").arg(note); }
	lines << QCoreApplication::translate("VibeStudioEditorProfiles", "Status: %1")
		.arg(profile.placeholder
			? QCoreApplication::translate("VibeStudioEditorProfiles", "placeholder presets (%1) with %2 of %3 bindings routed")
				.arg(profile.unresolvedPresets.join(QStringLiteral(", ")))
				.arg(editorProfileImplementedBindings(profile).size())
				.arg(profile.bindings.size())
			: QCoreApplication::translate("VibeStudioEditorProfiles", "all presets resolve to behaviour"));
	lines << profile.description;
	lines << QCoreApplication::translate("VibeStudioEditorProfiles", "Level editor controls:");
	for (const LevelEditorControlRow& row : levelEditorControlRows(profile.controls)) {
		lines << QStringLiteral("- %1 / %2: %3").arg(row.view, row.action, row.gesture);
	}
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
			lines << QStringLiteral("- %1 [%2 -> %3 @ %4]: %5 (%6)")
				.arg(binding.displayName, binding.actionId, binding.commandId, binding.surfaceId,
					gesture.isEmpty() ? QCoreApplication::translate("VibeStudioEditorProfiles", "unassigned") : gesture,
					binding.implemented
						? QCoreApplication::translate("VibeStudioEditorProfiles", "routed")
						: QCoreApplication::translate("VibeStudioEditorProfiles", "not implemented yet"));
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

QStringList editorProfileBindingKeys(const EditorProfileBinding& binding)
{
	if (binding.clearsKeys) {
		return {};
	}
	QStringList keys;
	for (const QString& key : binding.shortcut.split(QStringLiteral("; "), Qt::SkipEmptyParts)) {
		if (!key.trimmed().isEmpty()) {
			keys << key.trimmed();
		}
	}
	return keys;
}

LevelEditorControls levelEditorControlsForProfile(const QString& id)
{
	EditorProfileDescriptor descriptor;
	if (editorProfileForId(id, &descriptor)) {
		return descriptor.controls;
	}
	return vibeStudioLevelControls();
}

bool editorProfileHasShortcutConflict(const EditorProfileDescriptor& profile, QString* conflict)
{
	struct Seen {
		QString key;
		QString surface;
		QString actionId;
	};
	QVector<Seen> seen;
	for (const EditorProfileBinding& binding : profile.bindings) {
		const QString surface = binding.surfaceId.trimmed().toLower();
		for (const QString& key : editorProfileBindingKeys(binding)) {
			const QString normalized = key.toLower();
			for (const Seen& previous : seen) {
				if (previous.key == normalized && previous.surface == surface && previous.actionId != binding.actionId) {
					if (conflict) {
						*conflict = QCoreApplication::translate("VibeStudioEditorProfiles", "%1 conflicts with %2 on %3 using %4").arg(binding.actionId, previous.actionId, binding.surfaceId, key);
					}
					return true;
				}
			}
			seen.push_back({normalized, surface, binding.actionId});
		}
	}
	return false;
}

QVector<EditorProfileBinding> editorProfileImplementedBindings(const EditorProfileDescriptor& profile)
{
	QVector<EditorProfileBinding> implemented;
	for (const EditorProfileBinding& binding : profile.bindings) {
		if (binding.implemented) {
			implemented.push_back(binding);
		}
	}
	return implemented;
}

QStringList editorProfileRemappedShellCommandIds(const EditorProfileDescriptor& profile)
{
	QStringList ids;
	for (const EditorProfileBinding& binding : editorProfileImplementedBindings(profile)) {
		ids.push_back(binding.commandId);
	}
	ids.removeDuplicates();
	ids.sort();
	return ids;
}

} // namespace vibestudio
