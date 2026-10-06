#pragma once

#include "core/level_editor_controls.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

// A profile binding remaps one command. `commandId` is either a shell command
// documented in core/studio_semantics.h -- in which case the binding really
// changes what the shell does -- or an `editor-command.*` id for an editor
// surface that does not exist yet. `shortcut` may hold several keys, "; "
// between them; with `clearsKeys` the command gives up its keys instead,
// where the profile needs them for something else.
//
// `implemented` is computed, never asserted: it is true only when
// `shellCommandIdExists(commandId)` is true, so CLI JSON and the GUI report the
// state of the code rather than an intention.
struct EditorProfileBinding {
	QString actionId;
	QString displayName;
	QString shortcut;
	QString mouseGesture;
	QString context;
	QString commandId;
	QString surfaceId;
	bool implemented = false;
	bool clearsKeys = false;
};

// `placeholder` is true while any of the profile's presets still resolve to
// nothing; `unresolvedPresets` names which ones. A profile with its own
// `controls` resolves the view, camera, layout, grid and key presets. This
// does not imply upstream feature parity: `adaptations` records differences
// and any unsupported command bindings still report implemented=false.
struct EditorProfileDescriptor {
	QString id;
	QString displayName;
	QString shortName;
	QString lineage;
	QString description;
	QString layoutPresetId;
	QString cameraPresetId;
	QString selectionPresetId;
	QString gridPresetId;
	QString terminologyPresetId;
	QStringList supportedEngineFamilies;
	QStringList defaultPanels;
	QStringList workflowNotes;
	QStringList keybindingNotes;
	QStringList mouseBindingNotes;
	QVector<EditorProfileBinding> bindings;
	QStringList unresolvedPresets;
	bool placeholder = false;
	LevelEditorControls controls;
	QStringList aliases;
	// Differences from the reference editor, visible in Controls and CLI JSON.
	QStringList adaptations;
	QString referenceUrl;
};

QString defaultEditorProfileId();
QString normalizedEditorProfileId(const QString& id);
QVector<EditorProfileDescriptor> editorProfileDescriptors();
QStringList editorProfileIds();
bool editorProfileForId(const QString& id, EditorProfileDescriptor* out = nullptr);
QString editorProfileDisplayNameForId(const QString& id);
QString editorProfileSummaryText(const EditorProfileDescriptor& profile);
bool editorProfileBindingForAction(const EditorProfileDescriptor& profile, const QString& actionId, EditorProfileBinding* out = nullptr);
bool editorProfileHasShortcutConflict(const EditorProfileDescriptor& profile, QString* conflict = nullptr);
// The keys a binding gives its command, split from `shortcut`; empty when it
// takes them away.
[[nodiscard]] QStringList editorProfileBindingKeys(const EditorProfileBinding& binding);
// The level editor controls of the profile `id`, or the VibeStudio ones.
[[nodiscard]] LevelEditorControls levelEditorControlsForProfile(const QString& id);

// Bindings whose command id exists in the shell registry, i.e. the subset the
// shell can apply today, and the shell command ids the profile remaps.
QVector<EditorProfileBinding> editorProfileImplementedBindings(const EditorProfileDescriptor& profile);
QStringList editorProfileRemappedShellCommandIds(const EditorProfileDescriptor& profile);

} // namespace vibestudio
