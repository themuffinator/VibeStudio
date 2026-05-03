#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct EditorProfileBinding {
	QString actionId;
	QString displayName;
	QString shortcut;
	QString mouseGesture;
	QString context;
	QString commandId;
	QString surfaceId;
	bool implemented = true;
};

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

} // namespace vibestudio
