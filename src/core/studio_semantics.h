#pragma once

#include "core/operation_state.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct StatusChipDescriptor {
	QString id;
	QString domain;
	QString label;
	QString description;
	OperationState state = OperationState::Idle;
	QString iconName;
	QString colorToken;
	QString nonColorCue;
	QString nextAction;
};

struct ShortcutDescriptor {
	QString id;
	QString commandId;
	QString label;
	QString context;
	QString defaultSequence;
	QStringList alternateSequences;
	QString description;
	bool userRemappable = true;
};

struct CommandPaletteEntry {
	QString id;
	QString commandId;
	QString label;
	QString category;
	QString summary;
	QString defaultShortcut;
	bool requiresProject = false;
	bool destructive = false;
	bool stagedOrDryRun = false;
};

QVector<StatusChipDescriptor> statusChipDescriptors();
QStringList statusChipDomains();
bool statusChipForId(const QString& id, StatusChipDescriptor* out = nullptr);

QVector<ShortcutDescriptor> shortcutDescriptors();
bool shortcutForCommandId(const QString& commandId, ShortcutDescriptor* out = nullptr);
bool shortcutRegistryHasConflicts(QStringList* conflicts = nullptr);

QVector<CommandPaletteEntry> commandPaletteEntries();
bool commandPaletteEntryForCommandId(const QString& commandId, CommandPaletteEntry* out = nullptr);

QString statusChipSummaryText();
QString shortcutRegistrySummaryText();
QString commandPaletteSummaryText();

} // namespace vibestudio
