#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

// A profile binding remaps one command. `commandId` is either a shell command
// documented in core/studio_semantics.h -- in which case the binding really
// changes what the shell does -- or an `editor-command.*` id for an editor
// surface that does not exist yet.
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
};

// `placeholder` is true while any of the profile's presets still resolve to
// nothing; `unresolvedPresets` names which ones. Today every profile is a
// placeholder because the layout, camera, selection, and grid presets are pure
// data with no editor behind them -- only the shell command remaps are live.
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

// Bindings whose command id exists in the shell registry, i.e. the subset the
// shell can apply today, and the shell command ids the profile remaps.
QVector<EditorProfileBinding> editorProfileImplementedBindings(const EditorProfileDescriptor& profile);
QStringList editorProfileRemappedShellCommandIds(const EditorProfileDescriptor& profile);

} // namespace vibestudio
