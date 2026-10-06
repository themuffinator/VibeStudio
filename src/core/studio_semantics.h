#pragma once

#include "core/operation_state.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

// Status chip vocabulary
// ---------------------
// `colorToken` and `iconName` are stable, untranslated tokens the GUI maps
// mechanically to a palette entry and a glyph. Both vocabularies are
// deliberately small:
//
// * `colorToken` is either one of `operationStateIds()` ("idle", "queued",
//   "loading", "running", "warning", "failed", "cancelled", "completed") -- in
//   which case it always equals `operationStateId(state)` -- or one of the four
//   documented accent tokens below, used when a domain needs an emphasis that
//   the operation state alone does not carry:
//       "staged"    non-destructive edits waiting for an explicit save-as
//       "read-only" mounted for inspection only
//       "local"     deterministic/offline path, no network involved
//       "cloud"     optional network/provider path
//   `statusChipColorTokens()` returns the whole vocabulary and
//   `statusChipTokensAreValid()` enforces the rule above.
// * `iconName` names the *subject* glyph only ("folder", "archive", ...). The
//   state overlay (check, warning, stop, pause, slash) is derived from `state`
//   or `colorToken`, so the icon set stays small.
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

// Shell command identifiers
// -------------------------
// `commandId` is the single source of truth shared by the menu bar, the tool
// bar, the command palette, the CLI and the editor profiles. Identifiers are
// lowercase, dot-separated and grouped by namespace:
//     app.*          application-level (preferences, about, quit)
//     shell.*        shell navigation (modes, search focus, command palette)
//     project.*      project folder and manifest
//     package.*      package open/close/extract/stage/save-as
//     map.*          level documents
//     compiler.*     a single compiler profile invocation
//     build.*        the multi-step build pipeline
//     game.*         game installation detection and launching
//     activity.*     the task runner
//     diagnostics.*  support bundles
//     localization.* / ai.*  QA and optional AI surfaces
// Editor profiles remap these ids; they never invent competing ones.
struct ShortcutDescriptor {
	QString id;
	QString commandId;
	QString label;
	QString context;
	QString defaultSequence;
	QStringList alternateSequences;
	QString description;
	bool userRemappable = true;
	// The key fires only while focus is inside its context's surface (the
	// Packages page for "package", Levels for "map", Code for "code", Build and
	// the Activity panel for "activity"). Two such keys on different surfaces
	// may share a sequence; see shortcutRegistryHasConflicts().
	bool surfaceScoped = false;
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
QStringList statusChipColorTokens();
QStringList statusChipAccentColorTokens();
QStringList statusChipIconNames();
bool statusChipTokensAreValid(QStringList* problems = nullptr);

QVector<ShortcutDescriptor> shortcutDescriptors();
bool shortcutForCommandId(const QString& commandId, ShortcutDescriptor* out = nullptr);
bool shortcutRegistryHasConflicts(QStringList* conflicts = nullptr);

QVector<CommandPaletteEntry> commandPaletteEntries();
bool commandPaletteEntryForCommandId(const QString& commandId, CommandPaletteEntry* out = nullptr);

// Every command id known to the shell registry (shortcuts plus palette-only
// entries), and a membership test used by editor profiles to report honestly
// whether a binding routes to a real command.
QStringList shellCommandIds();
bool shellCommandIdExists(const QString& commandId);

QString statusChipSummaryText();
QString shortcutRegistrySummaryText();
QString commandPaletteSummaryText();

} // namespace vibestudio
