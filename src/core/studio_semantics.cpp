#include "core/studio_semantics.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <utility>

namespace vibestudio {

namespace {

// Shell commands are registered with camelCase ids ("shell.commandPalette")
// while this registry documents them in kebab-case ("shell.command-palette").
// Both spellings name the same command, so a lower-to-upper transition becomes
// a hyphen before the comparison.
QString normalizedId(QString value)
{
	value = value.trimmed();
	QString normalized;
	normalized.reserve(value.size() + 8);
	for (int index = 0; index < value.size(); ++index) {
		const QChar ch = value.at(index);
		if (ch.isUpper() && index > 0 && (value.at(index - 1).isLower() || value.at(index - 1).isDigit())) {
			normalized += QLatin1Char('-');
		}
		normalized += ch.toLower();
	}
	normalized.replace('_', '-');
	normalized.replace(' ', '-');
	return normalized;
}

QString normalizedSequence(const QString& sequence)
{
	return sequence.trimmed().toLower().remove(' ');
}

ShortcutDescriptor shortcut(
	const QString& id,
	const QString& commandId,
	const QString& label,
	const QString& context,
	const QString& defaultSequence,
	const QString& description,
	const QStringList& alternateSequences = {},
	bool userRemappable = true)
{
	return {id, commandId, label, context, defaultSequence, alternateSequences, description, userRemappable};
}

// A shortcut that fires only while focus is inside its context's surface.
ShortcutDescriptor surfaceShortcut(
	const QString& id,
	const QString& commandId,
	const QString& label,
	const QString& context,
	const QString& defaultSequence,
	const QString& description,
	const QStringList& alternateSequences = {},
	bool userRemappable = true)
{
	ShortcutDescriptor descriptor = shortcut(id, commandId, label, context, defaultSequence, description, alternateSequences, userRemappable);
	descriptor.surfaceScoped = true;
	return descriptor;
}

// Palette entries never spell their own shortcut: it is looked up from the
// shortcut registry so the two can never drift apart.
const QHash<QString, QString>& defaultSequencesByCommandId()
{
	static const QHash<QString, QString> sequences = [] {
		QHash<QString, QString> map;
		for (const ShortcutDescriptor& descriptor : shortcutDescriptors()) {
			map.insert(normalizedId(descriptor.commandId), descriptor.defaultSequence);
		}
		return map;
	}();
	return sequences;
}

CommandPaletteEntry paletteEntry(
	const QString& id,
	const QString& commandId,
	const QString& label,
	const QString& category,
	const QString& summary,
	bool requiresProject = false,
	bool destructive = false,
	bool stagedOrDryRun = false)
{
	return {
		id,
		commandId,
		label,
		category,
		summary,
		defaultSequencesByCommandId().value(normalizedId(commandId)),
		requiresProject,
		destructive,
		stagedOrDryRun,
	};
}

} // namespace

QVector<StatusChipDescriptor> statusChipDescriptors()
{
	return {
		{
			QStringLiteral("project-ready"),
			QStringLiteral("project"),
			QCoreApplication::translate("VibeStudioSemantics", "Project Ready"),
			QCoreApplication::translate("VibeStudioSemantics", "A project manifest is loaded or can be initialized from the selected folder."),
			OperationState::Completed,
			QStringLiteral("folder"),
			QStringLiteral("completed"),
			QCoreApplication::translate("VibeStudioSemantics", "checkmark plus Ready text"),
			QCoreApplication::translate("VibeStudioSemantics", "Open the workspace dashboard or validate the project."),
		},
		{
			QStringLiteral("project-needs-attention"),
			QStringLiteral("project"),
			QCoreApplication::translate("VibeStudioSemantics", "Project Needs Attention"),
			QCoreApplication::translate("VibeStudioSemantics", "The project can be inspected, but validation found warnings or missing optional context."),
			OperationState::Warning,
			QStringLiteral("folder"),
			QStringLiteral("warning"),
			QCoreApplication::translate("VibeStudioSemantics", "warning triangle plus Needs Attention text"),
			QCoreApplication::translate("VibeStudioSemantics", "Open project validation details."),
		},
		{
			QStringLiteral("package-loaded"),
			QStringLiteral("package"),
			QCoreApplication::translate("VibeStudioSemantics", "Package Loaded"),
			QCoreApplication::translate("VibeStudioSemantics", "The selected folder, PAK, WAD, ZIP, or PK3 is open and entries are indexed."),
			OperationState::Completed,
			QStringLiteral("archive"),
			QStringLiteral("completed"),
			QCoreApplication::translate("VibeStudioSemantics", "archive icon plus Loaded text"),
			QCoreApplication::translate("VibeStudioSemantics", "Browse entries or open the package detail drawer."),
		},
		{
			QStringLiteral("package-staged"),
			QStringLiteral("package"),
			QCoreApplication::translate("VibeStudioSemantics", "Package Staged"),
			QCoreApplication::translate("VibeStudioSemantics", "Package edits are staged for a non-destructive save-as operation."),
			OperationState::Warning,
			QStringLiteral("archive"),
			QStringLiteral("staged"),
			QCoreApplication::translate("VibeStudioSemantics", "staged badge plus Save-As Required text"),
			QCoreApplication::translate("VibeStudioSemantics", "Review staged changes and choose a save-as path."),
		},
		{
			QStringLiteral("package-read-only"),
			QStringLiteral("package"),
			QCoreApplication::translate("VibeStudioSemantics", "Package Read-Only"),
			QCoreApplication::translate("VibeStudioSemantics", "The package is mounted for inspection and extraction, but no in-place mutation will be performed."),
			OperationState::Completed,
			QStringLiteral("lock"),
			QStringLiteral("read-only"),
			QCoreApplication::translate("VibeStudioSemantics", "lock icon plus Read-Only text"),
			QCoreApplication::translate("VibeStudioSemantics", "Use staged save-as if changes are needed."),
		},
		{
			QStringLiteral("compiler-ready"),
			QStringLiteral("compiler"),
			QCoreApplication::translate("VibeStudioSemantics", "Compiler Ready"),
			QCoreApplication::translate("VibeStudioSemantics", "A compiler profile has enough executable and input context to produce a command plan."),
			OperationState::Completed,
			QStringLiteral("terminal"),
			QStringLiteral("completed"),
			QCoreApplication::translate("VibeStudioSemantics", "terminal icon plus Ready text"),
			QCoreApplication::translate("VibeStudioSemantics", "Plan or run the selected compiler profile."),
		},
		{
			QStringLiteral("compiler-running"),
			QStringLiteral("compiler"),
			QCoreApplication::translate("VibeStudioSemantics", "Compiler Running"),
			QCoreApplication::translate("VibeStudioSemantics", "A compiler process is active and task-state/progress logs are available."),
			OperationState::Running,
			QStringLiteral("terminal"),
			QStringLiteral("running"),
			QCoreApplication::translate("VibeStudioSemantics", "moving progress indicator plus Running text"),
			QCoreApplication::translate("VibeStudioSemantics", "Watch task logs or cancel when the task allows it."),
		},
		{
			QStringLiteral("compiler-blocked"),
			QStringLiteral("compiler"),
			QCoreApplication::translate("VibeStudioSemantics", "Compiler Blocked"),
			QCoreApplication::translate("VibeStudioSemantics", "The compiler profile is missing an executable, input, working directory, or required output context."),
			OperationState::Failed,
			QStringLiteral("terminal"),
			QStringLiteral("failed"),
			QCoreApplication::translate("VibeStudioSemantics", "stop symbol plus Blocked text"),
			QCoreApplication::translate("VibeStudioSemantics", "Open compiler registry details and configure the missing path."),
		},
		{
			QStringLiteral("install-confirmable"),
			QStringLiteral("install"),
			QCoreApplication::translate("VibeStudioSemantics", "Install Confirmable"),
			QCoreApplication::translate("VibeStudioSemantics", "A detected game installation candidate is visible but will not be saved until the user confirms it."),
			OperationState::Warning,
			QStringLiteral("drive"),
			QStringLiteral("warning"),
			QCoreApplication::translate("VibeStudioSemantics", "drive icon plus Confirm text"),
			QCoreApplication::translate("VibeStudioSemantics", "Review the detected path and import or skip it."),
		},
		{
			QStringLiteral("ai-free"),
			QStringLiteral("ai"),
			QCoreApplication::translate("VibeStudioSemantics", "AI-Free"),
			QCoreApplication::translate("VibeStudioSemantics", "Cloud and agentic AI workflows are disabled while deterministic local workflows remain available."),
			OperationState::Completed,
			QStringLiteral("shield"),
			QStringLiteral("local"),
			QCoreApplication::translate("VibeStudioSemantics", "shield icon plus AI-Free text"),
			QCoreApplication::translate("VibeStudioSemantics", "Continue with local package, project, compiler, and CLI workflows."),
		},
		{
			QStringLiteral("ai-cloud-optional"),
			QStringLiteral("ai"),
			QCoreApplication::translate("VibeStudioSemantics", "Cloud AI Optional"),
			QCoreApplication::translate("VibeStudioSemantics", "Cloud connector settings exist, but core workflows remain available without provider credentials."),
			OperationState::Idle,
			QStringLiteral("cloud"),
			QStringLiteral("cloud"),
			QCoreApplication::translate("VibeStudioSemantics", "cloud icon plus Optional text"),
			QCoreApplication::translate("VibeStudioSemantics", "Configure a connector only after reviewing consent and redaction settings."),
		},
		{
			QStringLiteral("ai-review-required"),
			QStringLiteral("ai"),
			QCoreApplication::translate("VibeStudioSemantics", "AI Review Required"),
			QCoreApplication::translate("VibeStudioSemantics", "An AI-generated proposal exists but cannot mutate project files until reviewed and approved."),
			OperationState::Warning,
			QStringLiteral("sparkles"),
			QStringLiteral("staged"),
			QCoreApplication::translate("VibeStudioSemantics", "review badge plus Approval Required text"),
			QCoreApplication::translate("VibeStudioSemantics", "Open the proposal review surface."),
		},
		{
			QStringLiteral("validation-failed"),
			QStringLiteral("validation"),
			QCoreApplication::translate("VibeStudioSemantics", "Validation Failed"),
			QCoreApplication::translate("VibeStudioSemantics", "A validation pass found blocking issues that should be resolved before release or destructive operations."),
			OperationState::Failed,
			QStringLiteral("badge"),
			QStringLiteral("failed"),
			QCoreApplication::translate("VibeStudioSemantics", "X badge plus Failed text"),
			QCoreApplication::translate("VibeStudioSemantics", "Open diagnostics and resolve blocking findings."),
		},
		{
			QStringLiteral("task-paused"),
			QStringLiteral("activity"),
			QCoreApplication::translate("VibeStudioSemantics", "Task Paused"),
			QCoreApplication::translate("VibeStudioSemantics", "A task is waiting for user input, a missing path, or an external tool before it can continue."),
			OperationState::Queued,
			QStringLiteral("pause"),
			QStringLiteral("queued"),
			QCoreApplication::translate("VibeStudioSemantics", "pause icon plus Paused text"),
			QCoreApplication::translate("VibeStudioSemantics", "Open the task detail drawer and resolve the blocker."),
		},
		{
			QStringLiteral("task-cancelled"),
			QStringLiteral("activity"),
			QCoreApplication::translate("VibeStudioSemantics", "Task Cancelled"),
			QCoreApplication::translate("VibeStudioSemantics", "A user-cancelled task has stopped and cleanup/results are visible in the activity details."),
			OperationState::Cancelled,
			QStringLiteral("slash"),
			QStringLiteral("cancelled"),
			QCoreApplication::translate("VibeStudioSemantics", "slash icon plus Cancelled text"),
			QCoreApplication::translate("VibeStudioSemantics", "Review cleanup state before starting the task again."),
		},
	};
}

QStringList statusChipDomains()
{
	QStringList domains;
	for (const StatusChipDescriptor& descriptor : statusChipDescriptors()) {
		domains.push_back(descriptor.domain);
	}
	domains.removeDuplicates();
	domains.sort(Qt::CaseInsensitive);
	return domains;
}

bool statusChipForId(const QString& id, StatusChipDescriptor* out)
{
	const QString requested = normalizedId(id);
	for (const StatusChipDescriptor& descriptor : statusChipDescriptors()) {
		if (descriptor.id == requested) {
			if (out) {
				*out = descriptor;
			}
			return true;
		}
	}
	return false;
}

QStringList statusChipAccentColorTokens()
{
	return {
		QStringLiteral("staged"),
		QStringLiteral("read-only"),
		QStringLiteral("local"),
		QStringLiteral("cloud"),
	};
}

QStringList statusChipColorTokens()
{
	QStringList tokens = operationStateIds();
	tokens.append(statusChipAccentColorTokens());
	return tokens;
}

QStringList statusChipIconNames()
{
	return {
		QStringLiteral("archive"),
		QStringLiteral("badge"),
		QStringLiteral("cloud"),
		QStringLiteral("drive"),
		QStringLiteral("folder"),
		QStringLiteral("lock"),
		QStringLiteral("pause"),
		QStringLiteral("shield"),
		QStringLiteral("slash"),
		QStringLiteral("sparkles"),
		QStringLiteral("terminal"),
	};
}

bool statusChipTokensAreValid(QStringList* problems)
{
	const QStringList accents = statusChipAccentColorTokens();
	const QStringList icons = statusChipIconNames();
	QStringList found;
	for (const StatusChipDescriptor& descriptor : statusChipDescriptors()) {
		if (!icons.contains(descriptor.iconName)) {
			found.push_back(QStringLiteral("%1: icon %2").arg(descriptor.id, descriptor.iconName));
		}
		if (accents.contains(descriptor.colorToken)) {
			continue;
		}
		if (descriptor.colorToken != operationStateId(descriptor.state)) {
			found.push_back(QStringLiteral("%1: color %2").arg(descriptor.id, descriptor.colorToken));
		}
	}
	if (problems) {
		*problems = found;
	}
	return found.isEmpty();
}

QVector<ShortcutDescriptor> shortcutDescriptors()
{
	return {
		// Application shell.
		shortcut(
			QStringLiteral("command-palette"),
			QStringLiteral("shell.command-palette"),
			QCoreApplication::translate("VibeStudioSemantics", "Command Palette"),
			QStringLiteral("global"),
			QStringLiteral("Ctrl+Shift+P"),
			QCoreApplication::translate("VibeStudioSemantics", "Open the searchable command palette shell."),
			{QStringLiteral("Ctrl+K")}),
		shortcut(
			QStringLiteral("go-to-file"),
			QStringLiteral("shell.go-to-file"),
			QCoreApplication::translate("VibeStudioSemantics", "Go to File"),
			QStringLiteral("global"),
			QStringLiteral("Ctrl+P"),
			QCoreApplication::translate("VibeStudioSemantics", "Find a project file or an entry of the open package by name and open it where it belongs.")),
		shortcut(
			QStringLiteral("focus-workspace-search"),
			QStringLiteral("shell.focus-search"),
			QCoreApplication::translate("VibeStudioSemantics", "Find"),
			QStringLiteral("global"),
			QStringLiteral("Ctrl+F"),
			QCoreApplication::translate("VibeStudioSemantics", "Move focus to the search or filter field of the current work surface without changing the active mode.")),
		shortcut(
			QStringLiteral("preferences"),
			QStringLiteral("app.preferences"),
			QCoreApplication::translate("VibeStudioSemantics", "Preferences"),
			QStringLiteral("global"),
			QStringLiteral("Ctrl+,"),
			QCoreApplication::translate("VibeStudioSemantics", "Open language, accessibility, theme, density, editor profile, and automation preferences.")),
		shortcut(
			QStringLiteral("read-aloud"),
			QStringLiteral("accessibility.read-aloud"),
			QCoreApplication::translate("VibeStudioSemantics", "Read Aloud"),
			QStringLiteral("global"),
			QStringLiteral("Ctrl+Shift+U"),
			QCoreApplication::translate("VibeStudioSemantics", "Read the selected text, the focused item, or the latest status message aloud with this computer's voice; press again to stop.")),
		shortcut(
			QStringLiteral("about"),
			QStringLiteral("app.about"),
			QCoreApplication::translate("VibeStudioSemantics", "About VibeStudio"),
			QStringLiteral("global"),
			QStringLiteral("Shift+F1"),
			QCoreApplication::translate("VibeStudioSemantics", "Show version, platform, module, and credits information.")),
		shortcut(
			QStringLiteral("quit"),
			QStringLiteral("app.quit"),
			QCoreApplication::translate("VibeStudioSemantics", "Quit"),
			QStringLiteral("global"),
			QStringLiteral("Ctrl+Q"),
			QCoreApplication::translate("VibeStudioSemantics", "Close the studio shell after offering to review unsaved staged work.")),

		// Projects.
		shortcut(
			QStringLiteral("open-project"),
			QStringLiteral("project.open"),
			QCoreApplication::translate("VibeStudioSemantics", "Open Project"),
			QStringLiteral("project"),
			QStringLiteral("Ctrl+O"),
			QCoreApplication::translate("VibeStudioSemantics", "Open or remember a project folder without changing project files.")),
		shortcut(
			QStringLiteral("initialize-manifest"),
			QStringLiteral("project.initialize-manifest"),
			QCoreApplication::translate("VibeStudioSemantics", "Initialize Project Manifest"),
			QStringLiteral("project"),
			QStringLiteral("Ctrl+Shift+I"),
			QCoreApplication::translate("VibeStudioSemantics", "Write a new studio manifest into the selected project folder.")),
		shortcut(
			QStringLiteral("copy-manifest"),
			QStringLiteral("project.copy-manifest"),
			QCoreApplication::translate("VibeStudioSemantics", "Copy Project Manifest"),
			QStringLiteral("project"),
			QStringLiteral("Ctrl+Alt+M"),
			QCoreApplication::translate("VibeStudioSemantics", "Copy the resolved project manifest to the clipboard for review or support.")),

		// Packages.
		shortcut(
			QStringLiteral("package-open"),
			QStringLiteral("package.open"),
			QCoreApplication::translate("VibeStudioSemantics", "Open Package"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Alt+O"),
			QCoreApplication::translate("VibeStudioSemantics", "Open a PAK, WAD, ZIP, or PK3 package for browsing.")),
		shortcut(
			QStringLiteral("package-open-folder"),
			QStringLiteral("package.open-folder"),
			QCoreApplication::translate("VibeStudioSemantics", "Open Folder As Package"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Shift+O"),
			QCoreApplication::translate("VibeStudioSemantics", "Mount a plain folder with the same read-only package semantics as an archive.")),
		shortcut(
			QStringLiteral("package-close"),
			QStringLiteral("package.close"),
			QCoreApplication::translate("VibeStudioSemantics", "Close Package"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+W"),
			QCoreApplication::translate("VibeStudioSemantics", "Close the open package and report any staged edits that would be discarded.")),
		surfaceShortcut(
			QStringLiteral("package-save-as"),
			QStringLiteral("package.save-as"),
			QCoreApplication::translate("VibeStudioSemantics", "Save Package As"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Shift+S"),
			QCoreApplication::translate("VibeStudioSemantics", "Write staged package changes to a new package path.")),
		surfaceShortcut(
			QStringLiteral("package-extract-selected"),
			QStringLiteral("package.extract-selected"),
			QCoreApplication::translate("VibeStudioSemantics", "Extract Selected Entries"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+E"),
			QCoreApplication::translate("VibeStudioSemantics", "Extract the selected package entries into a chosen output folder.")),
		shortcut(
			QStringLiteral("package-extract-all"),
			QStringLiteral("package.extract-all"),
			QCoreApplication::translate("VibeStudioSemantics", "Extract All Entries"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Shift+E"),
			QCoreApplication::translate("VibeStudioSemantics", "Extract every package entry into a chosen output folder.")),
		surfaceShortcut(
			QStringLiteral("package-stage-add"),
			QStringLiteral("package.stage-add"),
			QCoreApplication::translate("VibeStudioSemantics", "Stage Add"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Alt+A"),
			QCoreApplication::translate("VibeStudioSemantics", "Stage new files for the next package save-as without touching the open package.")),
		surfaceShortcut(
			QStringLiteral("package-stage-replace"),
			QStringLiteral("package.stage-replace"),
			QCoreApplication::translate("VibeStudioSemantics", "Stage Replace"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Alt+R"),
			QCoreApplication::translate("VibeStudioSemantics", "Stage a replacement for the selected entry without touching the open package.")),
		surfaceShortcut(
			QStringLiteral("package-stage-rename"),
			QStringLiteral("package.stage-rename"),
			QCoreApplication::translate("VibeStudioSemantics", "Stage Rename"),
			QStringLiteral("package"),
			QStringLiteral("F2"),
			QCoreApplication::translate("VibeStudioSemantics", "Stage a new archive path for the selected entry.")),
		surfaceShortcut(
			QStringLiteral("package-stage-delete"),
			QStringLiteral("package.stage-delete"),
			QCoreApplication::translate("VibeStudioSemantics", "Stage Delete"),
			QStringLiteral("package"),
			QStringLiteral("Del"),
			QCoreApplication::translate("VibeStudioSemantics", "Stage the selected entry for removal from the next saved package.")),
		surfaceShortcut(
			QStringLiteral("package-unstage-last"),
			QStringLiteral("package.unstage-last"),
			QCoreApplication::translate("VibeStudioSemantics", "Undo Package Edit"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Z"),
			QCoreApplication::translate("VibeStudioSemantics", "Undo the last package edit or import group while the Packages surface has focus.")),
		surfaceShortcut(QStringLiteral("package-redo"), QStringLiteral("package.redo"),
			QCoreApplication::translate("VibeStudioSemantics", "Redo Package Edit"), QStringLiteral("package"), QStringLiteral("Ctrl+Shift+Z"),
			QCoreApplication::translate("VibeStudioSemantics", "Redo the last undone package edit while the Packages surface has focus.")),
		surfaceShortcut(QStringLiteral("package-save-draft"), QStringLiteral("package.save-draft"),
			QCoreApplication::translate("VibeStudioSemantics", "Save Package Draft"), QStringLiteral("package"), QStringLiteral("Ctrl+S"),
			QCoreApplication::translate("VibeStudioSemantics", "Save package content and edit history in a portable draft directory.")),

		// Level documents.
		surfaceShortcut(QStringLiteral("map-new"), QStringLiteral("map.new"),
			QCoreApplication::translate("VibeStudioSemantics", "New Map"), QStringLiteral("map"), QStringLiteral("Ctrl+N"),
			QCoreApplication::translate("VibeStudioSemantics", "Create a map while the Levels surface has focus.")),
		surfaceShortcut(QStringLiteral("map-save"), QStringLiteral("map.save"),
			QCoreApplication::translate("VibeStudioSemantics", "Save Map"), QStringLiteral("map"), QStringLiteral("Ctrl+S"),
			QCoreApplication::translate("VibeStudioSemantics", "Save the map while the Levels surface has focus.")),
		shortcut(
			QStringLiteral("map-open"),
			QStringLiteral("map.open"),
			QCoreApplication::translate("VibeStudioSemantics", "Open Map"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+M"),
			QCoreApplication::translate("VibeStudioSemantics", "Open a Quake/Quake III .map or a Doom map lump for inspection and editing.")),
		shortcut(
			QStringLiteral("map-save-as"),
			QStringLiteral("map.save-as"),
			QCoreApplication::translate("VibeStudioSemantics", "Save Map As"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+Alt+S"),
			QCoreApplication::translate("VibeStudioSemantics", "Choose a destination for the edited map, with a backup before replacing an existing file.")),
		surfaceShortcut(
			QStringLiteral("map-undo"),
			QStringLiteral("map.undo"),
			QCoreApplication::translate("VibeStudioSemantics", "Undo Map Edit"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+Z"),
			QCoreApplication::translate("VibeStudioSemantics", "Undo the last edit to the open level document while the Levels surface has focus.")),
		surfaceShortcut(
			QStringLiteral("map-redo"),
			QStringLiteral("map.redo"),
			QCoreApplication::translate("VibeStudioSemantics", "Redo Map Edit"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+Y"),
			QCoreApplication::translate("VibeStudioSemantics", "Redo the last undone edit to the open level document while the Levels surface has focus."),
			{QStringLiteral("Ctrl+Shift+Z")}),
		surfaceShortcut(
			QStringLiteral("map-hide-selection"),
			QStringLiteral("map.hide-selection"),
			QCoreApplication::translate("VibeStudioSemantics", "Hide Selection"),
			QStringLiteral("map"),
			QStringLiteral("H"),
			QCoreApplication::translate("VibeStudioSemantics", "Hide the selected map objects from the view, without changing the map, while the Levels surface has focus.")),
		surfaceShortcut(
			QStringLiteral("map-show-all"),
			QStringLiteral("map.show-all"),
			QCoreApplication::translate("VibeStudioSemantics", "Show All Hidden"),
			QStringLiteral("map"),
			QStringLiteral("Shift+H"),
			QCoreApplication::translate("VibeStudioSemantics", "Bring every hidden map object back into the view while the Levels surface has focus.")),
		surfaceShortcut(
			QStringLiteral("map-select-all"),
			QStringLiteral("map.select-all"),
			QCoreApplication::translate("VibeStudioSemantics", "Select All"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+A"),
			QCoreApplication::translate("VibeStudioSemantics", "Select every entity, brush, and patch, or every Doom thing, that is not hidden, while the Levels surface has focus.")),
		surfaceShortcut(
			QStringLiteral("map-select-none"),
			QStringLiteral("map.select-none"),
			QCoreApplication::translate("VibeStudioSemantics", "Select None"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+Shift+A"),
			QCoreApplication::translate("VibeStudioSemantics", "Clear the Levels selection.")),
		surfaceShortcut(
			QStringLiteral("map-invert-selection"),
			QStringLiteral("map.invert-selection"),
			QCoreApplication::translate("VibeStudioSemantics", "Invert Selection"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+I"),
			QCoreApplication::translate("VibeStudioSemantics", "Select what is not selected and drop what is, among the objects Select All would pick.")),
		surfaceShortcut(
			QStringLiteral("map-preview-wireframe"),
			QStringLiteral("map.preview-wireframe"),
			QCoreApplication::translate("VibeStudioSemantics", "3D Wireframe"),
			QStringLiteral("map"),
			QStringLiteral("W"),
			QCoreApplication::translate("VibeStudioSemantics", "Switch the Levels 3D preview between flat shading and wireframe while it is showing.")),
		surfaceShortcut(
			QStringLiteral("map-clip-tool"),
			QStringLiteral("map.clip-tool"),
			QCoreApplication::translate("VibeStudioSemantics", "Clip Tool"),
			QStringLiteral("map"),
			QStringLiteral("X"),
			QCoreApplication::translate("VibeStudioSemantics", "Turn the Levels clip tool on or off: drag a line across brushes, Tab to choose what stays, Enter to cut.")),
		surfaceShortcut(
			QStringLiteral("map-draw-sector"),
			QStringLiteral("map.draw-sector"),
			QCoreApplication::translate("VibeStudioSemantics", "Draw Sector"),
			QStringLiteral("map"),
			QStringLiteral("D"),
			QCoreApplication::translate("VibeStudioSemantics", "Turn Draw Sector on or off on a Doom map: click the corners, then the first corner or Enter to close the shape.")),
		// Doom Builder's raise and lower, on the map view only: in the objects
		// list and the inspector Page Up and Page Down still page.
		surfaceShortcut(
			QStringLiteral("map-raise-floor"),
			QStringLiteral("map.raise-floor"),
			QCoreApplication::translate("VibeStudioSemantics", "Raise Floor"),
			QStringLiteral("map-view"),
			QStringLiteral("PgUp"),
			QCoreApplication::translate("VibeStudioSemantics", "Raise the selected Doom sectors' floors by 8, or by 1 with Shift, while the map view has focus."),
			{QStringLiteral("Shift+PgUp")}),
		surfaceShortcut(
			QStringLiteral("map-lower-floor"),
			QStringLiteral("map.lower-floor"),
			QCoreApplication::translate("VibeStudioSemantics", "Lower Floor"),
			QStringLiteral("map-view"),
			QStringLiteral("PgDown"),
			QCoreApplication::translate("VibeStudioSemantics", "Lower the selected Doom sectors' floors by 8, or by 1 with Shift, while the map view has focus."),
			{QStringLiteral("Shift+PgDown")}),
		surfaceShortcut(
			QStringLiteral("map-raise-ceiling"),
			QStringLiteral("map.raise-ceiling"),
			QCoreApplication::translate("VibeStudioSemantics", "Raise Ceiling"),
			QStringLiteral("map-view"),
			QStringLiteral("Ctrl+PgUp"),
			QCoreApplication::translate("VibeStudioSemantics", "Raise the selected Doom sectors' ceilings by 8, or by 1 with Shift, while the map view has focus."),
			{QStringLiteral("Ctrl+Shift+PgUp")}),
		surfaceShortcut(
			QStringLiteral("map-lower-ceiling"),
			QStringLiteral("map.lower-ceiling"),
			QCoreApplication::translate("VibeStudioSemantics", "Lower Ceiling"),
			QStringLiteral("map-view"),
			QStringLiteral("Ctrl+PgDown"),
			QCoreApplication::translate("VibeStudioSemantics", "Lower the selected Doom sectors' ceilings by 8, or by 1 with Shift, while the map view has focus."),
			{QStringLiteral("Ctrl+Shift+PgDown")}),
		surfaceShortcut(
			QStringLiteral("map-grid-smaller"),
			QStringLiteral("map.grid-smaller"),
			QCoreApplication::translate("VibeStudioSemantics", "Smaller Grid"),
			QStringLiteral("map"),
			QStringLiteral("["),
			QCoreApplication::translate("VibeStudioSemantics", "Halve the Levels grid, down to 1 unit, as Radiant and TrenchBroom do, while the Levels surface has focus.")),
		surfaceShortcut(
			QStringLiteral("map-grid-larger"),
			QStringLiteral("map.grid-larger"),
			QCoreApplication::translate("VibeStudioSemantics", "Larger Grid"),
			QStringLiteral("map"),
			QStringLiteral("]"),
			QCoreApplication::translate("VibeStudioSemantics", "Double the Levels grid, up to 256 units, while the Levels surface has focus.")),
		surfaceShortcut(
			QStringLiteral("map-copy-selection"),
			QStringLiteral("map.copy-selection"),
			QCoreApplication::translate("VibeStudioSemantics", "Copy Selection"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+C"),
			QCoreApplication::translate("VibeStudioSemantics", "Copy the selected entities, brushes, and patches as .map text, the format other Quake-family editors paste.")),
		surfaceShortcut(
			QStringLiteral("map-cut-selection"),
			QStringLiteral("map.cut-selection"),
			QCoreApplication::translate("VibeStudioSemantics", "Cut Selection"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+X"),
			QCoreApplication::translate("VibeStudioSemantics", "Copy the selected map objects as .map text, then delete them; undo restores them.")),
		surfaceShortcut(
			QStringLiteral("map-paste"),
			QStringLiteral("map.paste"),
			QCoreApplication::translate("VibeStudioSemantics", "Paste"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+V"),
			QCoreApplication::translate("VibeStudioSemantics", "Add the entities and brushes in .map text from the clipboard at their own coordinates, and select them.")),
		surfaceShortcut(
			QStringLiteral("map-duplicate-selection"),
			QStringLiteral("map.duplicate-selection"),
			QCoreApplication::translate("VibeStudioSemantics", "Duplicate Selection"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+D"),
			QCoreApplication::translate("VibeStudioSemantics", "Copy the selected entities, brushes, and patches one grid step over while the Levels surface has focus.")),
		surfaceShortcut(
			QStringLiteral("map-delete-selection"),
			QStringLiteral("map.delete-selection"),
			QCoreApplication::translate("VibeStudioSemantics", "Delete Selection"),
			QStringLiteral("map"),
			QStringLiteral("Del"),
			QCoreApplication::translate("VibeStudioSemantics", "Delete the selected entities, brushes, and patches from the open map while the Levels surface has focus; undo restores them.")),

		// Code and script editor.
		surfaceShortcut(
			QStringLiteral("code-new"), QStringLiteral("code.new"),
			QCoreApplication::translate("VibeStudioSemantics", "New Text File"),
			QStringLiteral("code"), QStringLiteral("Ctrl+N"),
			QCoreApplication::translate("VibeStudioSemantics", "Start an untitled document while the Code surface has focus.")),
		surfaceShortcut(
			QStringLiteral("code-save-as"), QStringLiteral("code.save-as"),
			QCoreApplication::translate("VibeStudioSemantics", "Save File As"),
			QStringLiteral("code"), QStringLiteral("Ctrl+Shift+S"),
			QCoreApplication::translate("VibeStudioSemantics", "Choose a destination for the document while the Code surface has focus.")),
		surfaceShortcut(
			QStringLiteral("code-save"),
			QStringLiteral("code.save"),
			QCoreApplication::translate("VibeStudioSemantics", "Save File"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+S"),
			QCoreApplication::translate("VibeStudioSemantics", "Save the file open in the code editor while the Code surface has focus.")),
		shortcut(
			QStringLiteral("code-find-in-files"),
			QStringLiteral("code.find-replace"),
			QCoreApplication::translate("VibeStudioSemantics", "Find In Project Files"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+Shift+F"),
			QCoreApplication::translate("VibeStudioSemantics", "Search, and optionally replace, across every text file in the project.")),
		surfaceShortcut(
			QStringLiteral("code-go-to-line"),
			QStringLiteral("code.go-to-line"),
			QCoreApplication::translate("VibeStudioSemantics", "Go To Line"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+L"),
			QCoreApplication::translate("VibeStudioSemantics", "Move the editor caret to a line number.")),
		surfaceShortcut(
			QStringLiteral("code-go-to-symbol"),
			QStringLiteral("code.go-to-symbol"),
			QCoreApplication::translate("VibeStudioSemantics", "Go To Symbol"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+T"),
			QCoreApplication::translate("VibeStudioSemantics", "Find a function, shader, or entity class in the open file by name and move the caret to it.")),
		surfaceShortcut(
			QStringLiteral("code-go-to-definition"),
			QStringLiteral("code.go-to-definition"),
			QCoreApplication::translate("VibeStudioSemantics", "Go to Definition"),
			QStringLiteral("code"),
			QStringLiteral("F12"),
			QCoreApplication::translate("VibeStudioSemantics", "Open where the name at the caret is defined, in the open file or across the project; Ctrl+click does the same.")),
		surfaceShortcut(
			QStringLiteral("code-complete"),
			QStringLiteral("code.complete"),
			QCoreApplication::translate("VibeStudioSemantics", "Complete Name"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+Space"),
			QCoreApplication::translate("VibeStudioSemantics", "Offer the names that start with what is typed: the file's own, the project's symbols, and the language's keywords.")),
		surfaceShortcut(
			QStringLiteral("code-find-references"),
			QStringLiteral("code.find-references"),
			QCoreApplication::translate("VibeStudioSemantics", "Find All References"),
			QStringLiteral("code"),
			QStringLiteral("Shift+F12"),
			QCoreApplication::translate("VibeStudioSemantics", "List every use of the name at the caret across the project, as whole words, under Search Results.")),
		surfaceShortcut(
			QStringLiteral("code-format-document"), QStringLiteral("code.format-document"),
			QCoreApplication::translate("VibeStudioSemantics", "Format Document"), QStringLiteral("code"), QStringLiteral("Alt+Shift+F"),
			QCoreApplication::translate("VibeStudioSemantics", "Format the open document with the connected language server as one unsaved Undo step.")),
		surfaceShortcut(
			QStringLiteral("code-rename-symbol"), QStringLiteral("code.rename-symbol"),
			QCoreApplication::translate("VibeStudioSemantics", "Rename Symbol"), QStringLiteral("code"), QStringLiteral("F2"),
			QCoreApplication::translate("VibeStudioSemantics", "Preview a language server rename across project files and unsaved documents.")),
		surfaceShortcut(
			QStringLiteral("code-code-actions"), QStringLiteral("code.code-actions"),
			QCoreApplication::translate("VibeStudioSemantics", "Code Actions"), QStringLiteral("code"), QStringLiteral("Ctrl+."),
			QCoreApplication::translate("VibeStudioSemantics", "Choose a provider quick fix or refactoring and review its project edits.")),
		surfaceShortcut(
			QStringLiteral("code-format-selection"), QStringLiteral("code.format-selection"),
			QCoreApplication::translate("VibeStudioSemantics", "Format Selection"), QStringLiteral("code"), QStringLiteral("Ctrl+Alt+F"),
			QCoreApplication::translate("VibeStudioSemantics", "Format selected source text with the connected language server.")),
		surfaceShortcut(
			QStringLiteral("code-quick-info"),
			QStringLiteral("code.quick-info"),
			QCoreApplication::translate("VibeStudioSemantics", "Quick Info"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+I"),
			QCoreApplication::translate("VibeStudioSemantics", "Show the symbol's type and documentation from the connected language server.")),
		surfaceShortcut(
			QStringLiteral("code-parameter-hints"), QStringLiteral("code.parameter-hints"),
			QCoreApplication::translate("VibeStudioSemantics", "Parameter Hints"), QStringLiteral("code"), QStringLiteral("Ctrl+Shift+Space"),
			QCoreApplication::translate("VibeStudioSemantics", "Show call overloads and the active parameter from the connected language server.")),
		shortcut(
			QStringLiteral("shell-navigate-back"),
			QStringLiteral("shell.navigate-back"),
			QCoreApplication::translate("VibeStudioSemantics", "Go Back"),
			QStringLiteral("global"),
			QStringLiteral("Alt+Left"),
			QCoreApplication::translate("VibeStudioSemantics", "Return to the page and place you were at before the last page change or jump; the mouse's back button does the same.")),
		shortcut(
			QStringLiteral("shell-navigate-forward"),
			QStringLiteral("shell.navigate-forward"),
			QCoreApplication::translate("VibeStudioSemantics", "Go Forward"),
			QStringLiteral("global"),
			QStringLiteral("Alt+Right"),
			QCoreApplication::translate("VibeStudioSemantics", "Return to the place Go Back left; the mouse's forward button does the same.")),
		shortcut(
			QStringLiteral("build-next-problem"),
			QStringLiteral("build.next-problem"),
			QCoreApplication::translate("VibeStudioSemantics", "Next Build Problem"),
			QStringLiteral("global"),
			QStringLiteral("F4"),
			QCoreApplication::translate("VibeStudioSemantics", "Show the last build's next problem, wrapping at the end, from any page: the map object, file line, or leak it names.")),
		shortcut(
			QStringLiteral("build-previous-problem"),
			QStringLiteral("build.previous-problem"),
			QCoreApplication::translate("VibeStudioSemantics", "Previous Build Problem"),
			QStringLiteral("global"),
			QStringLiteral("Shift+F4"),
			QCoreApplication::translate("VibeStudioSemantics", "Show the last build's previous problem, wrapping at the start.")),
		surfaceShortcut(
			QStringLiteral("code-next-problem"),
			QStringLiteral("code.next-problem"),
			QCoreApplication::translate("VibeStudioSemantics", "Next Problem"),
			QStringLiteral("code"),
			QStringLiteral("F8"),
			QCoreApplication::translate("VibeStudioSemantics", "Move the caret to the open file's next diagnostic, wrapping at the end, and say what it reports.")),
		surfaceShortcut(
			QStringLiteral("code-previous-problem"),
			QStringLiteral("code.previous-problem"),
			QCoreApplication::translate("VibeStudioSemantics", "Previous Problem"),
			QStringLiteral("code"),
			QStringLiteral("Shift+F8"),
			QCoreApplication::translate("VibeStudioSemantics", "Move the caret to the open file's previous diagnostic, wrapping at the start.")),
		surfaceShortcut(
			QStringLiteral("code-fold"),
			QStringLiteral("code.fold"),
			QCoreApplication::translate("VibeStudioSemantics", "Fold"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+Shift+["),
			QCoreApplication::translate("VibeStudioSemantics", "Fold the innermost { } block holding the caret; the gutter's chevrons fold with a click.")),
		surfaceShortcut(
			QStringLiteral("code-unfold"),
			QStringLiteral("code.unfold"),
			QCoreApplication::translate("VibeStudioSemantics", "Unfold"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+Shift+]"),
			QCoreApplication::translate("VibeStudioSemantics", "Unfold the block folded on the caret's line.")),
		surfaceShortcut(
			QStringLiteral("code-find-next"),
			QStringLiteral("code.find-next"),
			QCoreApplication::translate("VibeStudioSemantics", "Find Next"),
			QStringLiteral("code"),
			QStringLiteral("F3"),
			QCoreApplication::translate("VibeStudioSemantics", "Select the next match of the editor find text in the open file.")),
		surfaceShortcut(
			QStringLiteral("code-replace"),
			QStringLiteral("code.replace"),
			QCoreApplication::translate("VibeStudioSemantics", "Replace"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+H"),
			QCoreApplication::translate("VibeStudioSemantics", "Open the editor find bar with its replace row: Enter replaces the selected match, Ctrl+Enter every match.")),
		surfaceShortcut(
			QStringLiteral("code-find-previous"),
			QStringLiteral("code.find-previous"),
			QCoreApplication::translate("VibeStudioSemantics", "Find Previous"),
			QStringLiteral("code"),
			QStringLiteral("Shift+F3"),
			QCoreApplication::translate("VibeStudioSemantics", "Select the previous match of the editor find text in the open file.")),
		surfaceShortcut(
			QStringLiteral("code-toggle-comment"),
			QStringLiteral("code.toggle-comment"),
			QCoreApplication::translate("VibeStudioSemantics", "Toggle Line Comment"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+/"),
			QCoreApplication::translate("VibeStudioSemantics", "Comment or uncomment the selected lines with the file type's line comment.")),
		surfaceShortcut(
			QStringLiteral("code-duplicate-lines"),
			QStringLiteral("code.duplicate-lines"),
			QCoreApplication::translate("VibeStudioSemantics", "Duplicate Lines"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+D"),
			QCoreApplication::translate("VibeStudioSemantics", "Copy the selected lines below themselves.")),
		surfaceShortcut(
			QStringLiteral("code-move-lines-up"),
			QStringLiteral("code.move-lines-up"),
			QCoreApplication::translate("VibeStudioSemantics", "Move Lines Up"),
			QStringLiteral("code"),
			QStringLiteral("Alt+Up"),
			QCoreApplication::translate("VibeStudioSemantics", "Swap the selected lines with the line above.")),
		surfaceShortcut(
			QStringLiteral("code-move-lines-down"),
			QStringLiteral("code.move-lines-down"),
			QCoreApplication::translate("VibeStudioSemantics", "Move Lines Down"),
			QStringLiteral("code"),
			QStringLiteral("Alt+Down"),
			QCoreApplication::translate("VibeStudioSemantics", "Swap the selected lines with the line below.")),
		surfaceShortcut(
			QStringLiteral("code-close-file"),
			QStringLiteral("code.close-file"),
			QCoreApplication::translate("VibeStudioSemantics", "Close File"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+F4"),
			QCoreApplication::translate("VibeStudioSemantics", "Close the code editor's current tab, asking first if it has unsaved changes.")),
		surfaceShortcut(
			QStringLiteral("code-next-file"),
			QStringLiteral("code.next-file"),
			QCoreApplication::translate("VibeStudioSemantics", "Next Open File"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+PgDown"),
			// Not Ctrl+Tab: in the editor, where Tab types a tab, Ctrl+Tab is
			// the keyboard's way out.
			QCoreApplication::translate("VibeStudioSemantics", "Move to the code editor's next tab.")),
		surfaceShortcut(
			QStringLiteral("code-previous-file"),
			QStringLiteral("code.previous-file"),
			QCoreApplication::translate("VibeStudioSemantics", "Previous Open File"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+PgUp"),
			QCoreApplication::translate("VibeStudioSemantics", "Move to the code editor's previous tab.")),
		surfaceShortcut(
			QStringLiteral("code-zoom-in"),
			QStringLiteral("code.zoom-in"),
			QCoreApplication::translate("VibeStudioSemantics", "Zoom In Editor"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+="),
			QCoreApplication::translate("VibeStudioSemantics", "Make the code editor's text larger; Ctrl with the mouse wheel does the same."),
			{QStringLiteral("Ctrl++")}),
		surfaceShortcut(
			QStringLiteral("code-zoom-out"),
			QStringLiteral("code.zoom-out"),
			QCoreApplication::translate("VibeStudioSemantics", "Zoom Out Editor"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+-"),
			QCoreApplication::translate("VibeStudioSemantics", "Make the code editor's text smaller.")),

		// Compilers and the build pipeline.
		shortcut(
			QStringLiteral("compiler-run"),
			QStringLiteral("compiler.run"),
			QCoreApplication::translate("VibeStudioSemantics", "Run Compiler Profile"),
			QStringLiteral("compiler"),
			QStringLiteral("Ctrl+R"),
			QCoreApplication::translate("VibeStudioSemantics", "Run the selected compiler profile through the shared task runner.")),
		shortcut(
			QStringLiteral("compiler-copy-cli"),
			QStringLiteral("compiler.copy-cli"),
			QCoreApplication::translate("VibeStudioSemantics", "Copy CLI Equivalent"),
			QStringLiteral("compiler"),
			QStringLiteral("Ctrl+Shift+C"),
			QCoreApplication::translate("VibeStudioSemantics", "Copy a reproducible command line for the selected GUI action.")),
		shortcut(
			QStringLiteral("build-run-pipeline"),
			QStringLiteral("build.run-pipeline"),
			QCoreApplication::translate("VibeStudioSemantics", "Run Build Pipeline"),
			QStringLiteral("build"),
			QStringLiteral("F7"),
			QCoreApplication::translate("VibeStudioSemantics", "Run the ordered compile, validate, and package pipeline with per-step task state.")),
		shortcut(
			QStringLiteral("build-run-and-launch"),
			QStringLiteral("build.run-and-launch"),
			QCoreApplication::translate("VibeStudioSemantics", "Build and Launch"),
			QStringLiteral("build"),
			QStringLiteral("F5"),
			QCoreApplication::translate("VibeStudioSemantics", "Run the build pipeline, then launch the game with the built map once it succeeds.")),

		// Game installations.
		shortcut(
			QStringLiteral("detect-installations"),
			QStringLiteral("game.detect-installations"),
			QCoreApplication::translate("VibeStudioSemantics", "Detect Game Installations"),
			QStringLiteral("game"),
			QStringLiteral("Ctrl+Shift+G"),
			QCoreApplication::translate("VibeStudioSemantics", "Scan for candidate installations and present them for confirmation without writing game files.")),
		shortcut(
			QStringLiteral("launch-game"),
			QStringLiteral("game.launch"),
			QCoreApplication::translate("VibeStudioSemantics", "Launch Game"),
			QStringLiteral("game"),
			QStringLiteral("Ctrl+G"),
			QCoreApplication::translate("VibeStudioSemantics", "Launch the selected installation with the current mod/map arguments.")),

		// Audio browser.
		surfaceShortcut(
			QStringLiteral("audio-play-pause"),
			QStringLiteral("audio.play-pause"),
			QCoreApplication::translate("VibeStudioSemantics", "Play or Pause Sound"),
			QStringLiteral("audio"),
			QStringLiteral("Space"),
			QCoreApplication::translate("VibeStudioSemantics", "Play the selected sound from the playhead, or pause it, while the sound list or the waveform has focus.")),

		// Activity and support.
		surfaceShortcut(
			QStringLiteral("cancel-task"),
			QStringLiteral("activity.cancel"),
			QCoreApplication::translate("VibeStudioSemantics", "Cancel Selected Task"),
			QStringLiteral("activity"),
			QStringLiteral("Esc"),
			QCoreApplication::translate("VibeStudioSemantics", "Cancel the selected cancellable task, or the running build, while the Activity panel or Build surface has focus."),
			{},
			false),
		shortcut(
			QStringLiteral("diagnostics-bundle"),
			QStringLiteral("diagnostics.bundle"),
			QCoreApplication::translate("VibeStudioSemantics", "Copy Diagnostic Bundle"),
			QStringLiteral("diagnostics"),
			QStringLiteral("Ctrl+Alt+D"),
			QCoreApplication::translate("VibeStudioSemantics", "Copy a redacted diagnostic bundle for support and reproducibility.")),

		// Mode rail. Ctrl+1..Ctrl+9 select the nine work surfaces in rail order;
		// Ctrl+0 opens settings, which is a destination rather than a mode.
		shortcut(
			QStringLiteral("mode-workspace"),
			QStringLiteral("shell.mode.workspace"),
			QCoreApplication::translate("VibeStudioSemantics", "Workspace Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+1"),
			QCoreApplication::translate("VibeStudioSemantics", "Show the workspace dashboard.")),
		shortcut(
			QStringLiteral("mode-levels"),
			QStringLiteral("shell.mode.levels"),
			QCoreApplication::translate("VibeStudioSemantics", "Levels Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+2"),
			QCoreApplication::translate("VibeStudioSemantics", "Show the level editor surface.")),
		shortcut(
			QStringLiteral("mode-models"),
			QStringLiteral("shell.mode.models"),
			QCoreApplication::translate("VibeStudioSemantics", "Models Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+3"),
			QCoreApplication::translate("VibeStudioSemantics", "Show the model browser and inspector.")),
		shortcut(
			QStringLiteral("mode-textures"),
			QStringLiteral("shell.mode.textures"),
			QCoreApplication::translate("VibeStudioSemantics", "Textures Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+4"),
			QCoreApplication::translate("VibeStudioSemantics", "Show the texture and palette surface.")),
		shortcut(
			QStringLiteral("mode-audio"),
			QStringLiteral("shell.mode.audio"),
			QCoreApplication::translate("VibeStudioSemantics", "Audio Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+5"),
			QCoreApplication::translate("VibeStudioSemantics", "Show the audio browser and waveform preview.")),
		shortcut(
			QStringLiteral("mode-packages"),
			QStringLiteral("shell.mode.packages"),
			QCoreApplication::translate("VibeStudioSemantics", "Packages Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+6"),
			QCoreApplication::translate("VibeStudioSemantics", "Show the package manager surface.")),
		shortcut(
			QStringLiteral("mode-code"),
			QStringLiteral("shell.mode.code"),
			QCoreApplication::translate("VibeStudioSemantics", "Code Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+7"),
			QCoreApplication::translate("VibeStudioSemantics", "Show the code and script editor surface.")),
		shortcut(
			QStringLiteral("mode-shaders"),
			QStringLiteral("shell.mode.shaders"),
			QCoreApplication::translate("VibeStudioSemantics", "Materials Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+8"),
			QCoreApplication::translate("VibeStudioSemantics", "Show the textures, shaders and materials surface.")),
		shortcut(
			QStringLiteral("mode-build"),
			QStringLiteral("shell.mode.build"),
			QCoreApplication::translate("VibeStudioSemantics", "Build Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+9"),
			QCoreApplication::translate("VibeStudioSemantics", "Show the compiler, pipeline, and activity surface.")),
		shortcut(
			QStringLiteral("mode-settings"),
			QStringLiteral("shell.mode.settings"),
			QCoreApplication::translate("VibeStudioSemantics", "Settings Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+0"),
			QCoreApplication::translate("VibeStudioSemantics", "Show settings, setup progress, and diagnostics.")),
	};
}

bool shortcutForCommandId(const QString& commandId, ShortcutDescriptor* out)
{
	const QString requested = normalizedId(commandId);
	for (const ShortcutDescriptor& descriptor : shortcutDescriptors()) {
		if (normalizedId(descriptor.commandId) == requested) {
			if (out) {
				*out = descriptor;
			}
			return true;
		}
	}
	return false;
}

bool shortcutRegistryHasConflicts(QStringList* conflicts)
{
	// A window-level key may be claimed once across the whole registry,
	// alternates included. Surface-scoped keys fire only on their own surface,
	// so two of them on different surfaces may share a sequence (Ctrl+Z undoes a
	// map edit on Levels and a staged change on Packages).
	QHash<QString, QVector<const ShortcutDescriptor*>> owners;
	QStringList found;
	const QVector<ShortcutDescriptor> descriptors = shortcutDescriptors();
	for (const ShortcutDescriptor& descriptor : descriptors) {
		QStringList sequences;
		sequences << descriptor.defaultSequence;
		sequences << descriptor.alternateSequences;
		for (const QString& sequence : std::as_const(sequences)) {
			const QString key = normalizedSequence(sequence);
			if (key.isEmpty()) {
				continue;
			}
			bool clash = false;
			for (const ShortcutDescriptor* owner : owners.value(key)) {
				const bool separateSurfaces = owner->surfaceScoped && descriptor.surfaceScoped && owner->context != descriptor.context;
				if (!separateSurfaces) {
					found.push_back(QStringLiteral("%1 claimed by %2 and %3").arg(sequence.trimmed(), owner->commandId, descriptor.commandId));
					clash = true;
					break;
				}
			}
			if (!clash) {
				owners[key].push_back(&descriptor);
			}
		}
	}
	if (conflicts) {
		*conflicts = found;
	}
	return !found.isEmpty();
}

QVector<CommandPaletteEntry> commandPaletteEntries()
{
	QVector<CommandPaletteEntry> entries = {
		paletteEntry(QStringLiteral("texture-editor"), QStringLiteral("texture.editor"),
			QCoreApplication::translate("VibeStudioSemantics", "Texture Editor"),
			QCoreApplication::translate("VibeStudioSemantics", "Textures"),
			QCoreApplication::translate("VibeStudioSemantics", "Create or open a texture document, then export or stage it for the project.")),
		paletteEntry(QStringLiteral("texture-edit"), QStringLiteral("texture.edit"),
			QCoreApplication::translate("VibeStudioSemantics", "Edit Selected Texture"),
			QCoreApplication::translate("VibeStudioSemantics", "Textures"),
			QCoreApplication::translate("VibeStudioSemantics", "Edit the displayed mip level or sprite frame with its native export metadata.")),
		paletteEntry(QStringLiteral("texture-export"), QStringLiteral("texture.export"),
			QCoreApplication::translate("VibeStudioSemantics", "Export Texture as PNG"),
			QCoreApplication::translate("VibeStudioSemantics", "Textures"),
			QCoreApplication::translate("VibeStudioSemantics", "Export the displayed texture as a separate PNG image.")),
		paletteEntry(QStringLiteral("model-editor"), QStringLiteral("model.editor"),
			QCoreApplication::translate("VibeStudioSemantics", "Mesh Editor"),
			QCoreApplication::translate("VibeStudioSemantics", "Models"),
			QCoreApplication::translate("VibeStudioSemantics", "Edit the selected model or resume the open mesh document. With no model selected, start a new mesh.")),
		paletteEntry(QStringLiteral("model-design"), QStringLiteral("model.design"),
			QCoreApplication::translate("VibeStudioSemantics", "Prop Designer"),
			QCoreApplication::translate("VibeStudioSemantics", "Models"),
			QCoreApplication::translate("VibeStudioSemantics", "Build a prop from primitives, then export, stage or place it in a level.")),
		paletteEntry(QStringLiteral("model-export"), QStringLiteral("model.export"),
			QCoreApplication::translate("VibeStudioSemantics", "Export Model Frame"),
			QCoreApplication::translate("VibeStudioSemantics", "Models"),
			QCoreApplication::translate("VibeStudioSemantics", "Write the selected model's current frame to a Wavefront OBJ file.")),
		paletteEntry(QStringLiteral("model-assembly"), QStringLiteral("model.assembly"),
			QCoreApplication::translate("VibeStudioSemantics", "Model Assembly"),
			QCoreApplication::translate("VibeStudioSemantics", "Models"),
			QCoreApplication::translate("VibeStudioSemantics", "Link models at named tags, save the assembly or bake a pose for editing.")),
		paletteEntry(QStringLiteral("audio-open"), QStringLiteral("audio.open"),
			QCoreApplication::translate("VibeStudioSemantics", "Open Audio"),
			QCoreApplication::translate("VibeStudioSemantics", "Audio"),
			QCoreApplication::translate("VibeStudioSemantics", "Open an audio file, clip project or multitrack session for editing.")),
		paletteEntry(QStringLiteral("audio-edit"), QStringLiteral("audio.edit"),
			QCoreApplication::translate("VibeStudioSemantics", "Edit Selected Sound"),
			QCoreApplication::translate("VibeStudioSemantics", "Audio"),
			QCoreApplication::translate("VibeStudioSemantics", "Edit the selected package sound, then export or stage the result.")),
		paletteEntry(QStringLiteral("audio-session"), QStringLiteral("audio.session"),
			QCoreApplication::translate("VibeStudioSemantics", "Multitrack Session"),
			QCoreApplication::translate("VibeStudioSemantics", "Audio"),
			QCoreApplication::translate("VibeStudioSemantics", "Create or resume a multitrack audio session.")),
		paletteEntry(QStringLiteral("audio-export"), QStringLiteral("audio.export"),
			QCoreApplication::translate("VibeStudioSemantics", "Export Sound as WAV"),
			QCoreApplication::translate("VibeStudioSemantics", "Audio"),
			QCoreApplication::translate("VibeStudioSemantics", "Export the selected sound as a separate PCM WAV file.")),
		paletteEntry(QStringLiteral("texture-reveal"), QStringLiteral("texture.reveal"),
			QCoreApplication::translate("VibeStudioSemantics", "Show Selected Asset in Package"),
			QCoreApplication::translate("VibeStudioSemantics", "Textures"),
			QCoreApplication::translate("VibeStudioSemantics", "Return to the exact package entry to review its source and staged changes.")),
		paletteEntry(QStringLiteral("model-reveal"), QStringLiteral("model.reveal"),
			QCoreApplication::translate("VibeStudioSemantics", "Show Selected Asset in Package"),
			QCoreApplication::translate("VibeStudioSemantics", "Models"),
			QCoreApplication::translate("VibeStudioSemantics", "Return to the exact package entry to review its source and staged changes.")),
		paletteEntry(QStringLiteral("audio-reveal"), QStringLiteral("audio.reveal"),
			QCoreApplication::translate("VibeStudioSemantics", "Show Selected Asset in Package"),
			QCoreApplication::translate("VibeStudioSemantics", "Audio"),
			QCoreApplication::translate("VibeStudioSemantics", "Return to the exact package entry to review its source and staged changes.")),
		paletteEntry(QStringLiteral("open-workspace"), QStringLiteral("workspace.open"),
			QCoreApplication::translate("VibeStudioSemantics", "Open Workspace"),
			QCoreApplication::translate("VibeStudioSemantics", "Workspace"),
			QCoreApplication::translate("VibeStudioSemantics", "Restore saved project and module references.")),
		paletteEntry(QStringLiteral("save-workspace"), QStringLiteral("workspace.save"),
			QCoreApplication::translate("VibeStudioSemantics", "Save Workspace As"),
			QCoreApplication::translate("VibeStudioSemantics", "Workspace"),
			QCoreApplication::translate("VibeStudioSemantics", "Save portable references to the current work.")),
		paletteEntry(
			QStringLiteral("open-project"),
			QStringLiteral("project.open"),
			QCoreApplication::translate("VibeStudioSemantics", "Open Project"),
			QCoreApplication::translate("VibeStudioSemantics", "Workspace"),
			QCoreApplication::translate("VibeStudioSemantics", "Open or remember a project folder.")),
		paletteEntry(
			QStringLiteral("initialize-manifest"),
			QStringLiteral("project.initialize-manifest"),
			QCoreApplication::translate("VibeStudioSemantics", "Initialize Project Manifest"),
			QCoreApplication::translate("VibeStudioSemantics", "Workspace"),
			QCoreApplication::translate("VibeStudioSemantics", "Write a studio manifest into the selected project folder."),
			true),
		paletteEntry(
			QStringLiteral("copy-manifest"),
			QStringLiteral("project.copy-manifest"),
			QCoreApplication::translate("VibeStudioSemantics", "Copy Project Manifest"),
			QCoreApplication::translate("VibeStudioSemantics", "Workspace"),
			QCoreApplication::translate("VibeStudioSemantics", "Copy the resolved manifest for review, support, or automation."),
			true),
		paletteEntry(
			QStringLiteral("validate-project"),
			QStringLiteral("project.validate"),
			QCoreApplication::translate("VibeStudioSemantics", "Validate Project"),
			QCoreApplication::translate("VibeStudioSemantics", "Workspace"),
			QCoreApplication::translate("VibeStudioSemantics", "Run project health checks and show blocking issues."),
			true),
		paletteEntry(
			QStringLiteral("focus-workspace-search"),
			QStringLiteral("shell.focus-search"),
			QCoreApplication::translate("VibeStudioSemantics", "Find"),
			QCoreApplication::translate("VibeStudioSemantics", "Shell"),
			QCoreApplication::translate("VibeStudioSemantics", "Move focus to the current work surface's search or filter field.")),
		paletteEntry(
			QStringLiteral("command-palette"),
			QStringLiteral("shell.command-palette"),
			QCoreApplication::translate("VibeStudioSemantics", "Show Command Palette"),
			QCoreApplication::translate("VibeStudioSemantics", "Shell"),
			QCoreApplication::translate("VibeStudioSemantics", "Open the searchable command palette.")),
		paletteEntry(
			QStringLiteral("preferences"),
			QStringLiteral("app.preferences"),
			QCoreApplication::translate("VibeStudioSemantics", "Preferences"),
			QCoreApplication::translate("VibeStudioSemantics", "Shell"),
			QCoreApplication::translate("VibeStudioSemantics", "Open language, accessibility, theme, editor profile, and automation preferences.")),
		paletteEntry(
			QStringLiteral("about"),
			QStringLiteral("app.about"),
			QCoreApplication::translate("VibeStudioSemantics", "About VibeStudio"),
			QCoreApplication::translate("VibeStudioSemantics", "Support"),
			QCoreApplication::translate("VibeStudioSemantics", "Show version, platform, module, and credits information.")),
		paletteEntry(
			QStringLiteral("quit"),
			QStringLiteral("app.quit"),
			QCoreApplication::translate("VibeStudioSemantics", "Quit VibeStudio"),
			QCoreApplication::translate("VibeStudioSemantics", "Shell"),
			QCoreApplication::translate("VibeStudioSemantics", "Close the studio shell after reviewing staged work.")),
		paletteEntry(
			QStringLiteral("open-package"),
			QStringLiteral("package.open"),
			QCoreApplication::translate("VibeStudioSemantics", "Open Package"),
			QCoreApplication::translate("VibeStudioSemantics", "Packages"),
			QCoreApplication::translate("VibeStudioSemantics", "Open a PAK, WAD, ZIP, or PK3 package.")),
		paletteEntry(
			QStringLiteral("open-folder-package"),
			QStringLiteral("package.open-folder"),
			QCoreApplication::translate("VibeStudioSemantics", "Open Folder As Package"),
			QCoreApplication::translate("VibeStudioSemantics", "Packages"),
			QCoreApplication::translate("VibeStudioSemantics", "Mount a plain folder with read-only package semantics.")),
		paletteEntry(
			QStringLiteral("close-package"),
			QStringLiteral("package.close"),
			QCoreApplication::translate("VibeStudioSemantics", "Close Package"),
			QCoreApplication::translate("VibeStudioSemantics", "Packages"),
			QCoreApplication::translate("VibeStudioSemantics", "Close the open package and report staged edits that would be discarded.")),
		paletteEntry(
			QStringLiteral("save-package-as"),
			QStringLiteral("package.save-as"),
			QCoreApplication::translate("VibeStudioSemantics", "Save Staged Package As"),
			QCoreApplication::translate("VibeStudioSemantics", "Packages"),
			QCoreApplication::translate("VibeStudioSemantics", "Write staged package edits to a new package path."),
			false,
			true,
			true),
		paletteEntry(
			QStringLiteral("extract-selected"),
			QStringLiteral("package.extract-selected"),
			QCoreApplication::translate("VibeStudioSemantics", "Extract Selected Entries"),
			QCoreApplication::translate("VibeStudioSemantics", "Packages"),
			QCoreApplication::translate("VibeStudioSemantics", "Extract the selected entries into a chosen output folder.")),
		paletteEntry(
			QStringLiteral("extract-all"),
			QStringLiteral("package.extract-all"),
			QCoreApplication::translate("VibeStudioSemantics", "Extract All Entries"),
			QCoreApplication::translate("VibeStudioSemantics", "Packages"),
			QCoreApplication::translate("VibeStudioSemantics", "Extract every entry into a chosen output folder.")),
		paletteEntry(
			QStringLiteral("stage-add"),
			QStringLiteral("package.stage-add"),
			QCoreApplication::translate("VibeStudioSemantics", "Stage Add"),
			QCoreApplication::translate("VibeStudioSemantics", "Packages"),
			QCoreApplication::translate("VibeStudioSemantics", "Stage new files for the next package save-as."),
			false,
			false,
			true),
		paletteEntry(
			QStringLiteral("stage-replace"),
			QStringLiteral("package.stage-replace"),
			QCoreApplication::translate("VibeStudioSemantics", "Stage Replace"),
			QCoreApplication::translate("VibeStudioSemantics", "Packages"),
			QCoreApplication::translate("VibeStudioSemantics", "Stage a replacement for the selected entry."),
			false,
			false,
			true),
		paletteEntry(
			QStringLiteral("stage-rename"),
			QStringLiteral("package.stage-rename"),
			QCoreApplication::translate("VibeStudioSemantics", "Stage Rename"),
			QCoreApplication::translate("VibeStudioSemantics", "Packages"),
			QCoreApplication::translate("VibeStudioSemantics", "Stage a new archive path for the selected entry."),
			false,
			false,
			true),
		paletteEntry(
			QStringLiteral("stage-delete"),
			QStringLiteral("package.stage-delete"),
			QCoreApplication::translate("VibeStudioSemantics", "Stage Delete"),
			QCoreApplication::translate("VibeStudioSemantics", "Packages"),
			QCoreApplication::translate("VibeStudioSemantics", "Stage the selected entry for removal from the next saved package."),
			false,
			true,
			true),
		paletteEntry(
			QStringLiteral("open-map"),
			QStringLiteral("map.open"),
			QCoreApplication::translate("VibeStudioSemantics", "Open Map"),
			QCoreApplication::translate("VibeStudioSemantics", "Maps"),
			QCoreApplication::translate("VibeStudioSemantics", "Open a .map file or a Doom map lump.")),
		paletteEntry(
			QStringLiteral("save-map-as"),
			QStringLiteral("map.save-as"),
			QCoreApplication::translate("VibeStudioSemantics", "Save Map As"),
			QCoreApplication::translate("VibeStudioSemantics", "Maps"),
			QCoreApplication::translate("VibeStudioSemantics", "Write the edited level document to a new path."),
			false,
			true,
			true),
		paletteEntry(
			QStringLiteral("run-compiler"),
			QStringLiteral("compiler.run"),
			QCoreApplication::translate("VibeStudioSemantics", "Run Selected Compiler Profile"),
			QCoreApplication::translate("VibeStudioSemantics", "Compilers"),
			QCoreApplication::translate("VibeStudioSemantics", "Run the selected compiler profile with task-state feedback."),
			true),
		paletteEntry(
			QStringLiteral("copy-cli-equivalent"),
			QStringLiteral("compiler.copy-cli"),
			QCoreApplication::translate("VibeStudioSemantics", "Copy CLI Equivalent"),
			QCoreApplication::translate("VibeStudioSemantics", "Compilers"),
			QCoreApplication::translate("VibeStudioSemantics", "Copy a reproducible command line for the selected GUI action."),
			true),
		paletteEntry(
			QStringLiteral("run-build-pipeline"),
			QStringLiteral("build.run-pipeline"),
			QCoreApplication::translate("VibeStudioSemantics", "Run Build Pipeline"),
			QCoreApplication::translate("VibeStudioSemantics", "Build"),
			QCoreApplication::translate("VibeStudioSemantics", "Run the ordered compile, validate, and package pipeline."),
			true),
		paletteEntry(
			QStringLiteral("detect-installations"),
			QStringLiteral("game.detect-installations"),
			QCoreApplication::translate("VibeStudioSemantics", "Detect Game Installations"),
			QCoreApplication::translate("VibeStudioSemantics", "Game"),
			QCoreApplication::translate("VibeStudioSemantics", "Scan for candidate installations and confirm them before saving.")),
		paletteEntry(
			QStringLiteral("launch-game"),
			QStringLiteral("game.launch"),
			QCoreApplication::translate("VibeStudioSemantics", "Launch Game"),
			QCoreApplication::translate("VibeStudioSemantics", "Game"),
			QCoreApplication::translate("VibeStudioSemantics", "Launch the selected installation with the current mod and map arguments.")),
		paletteEntry(
			QStringLiteral("cancel-task"),
			QStringLiteral("activity.cancel"),
			QCoreApplication::translate("VibeStudioSemantics", "Cancel Selected Task"),
			QCoreApplication::translate("VibeStudioSemantics", "Activity"),
			QCoreApplication::translate("VibeStudioSemantics", "Cancel a cancellable task and report its cleanup state.")),
		paletteEntry(
			QStringLiteral("localization-report"),
			QStringLiteral("localization.report"),
			QCoreApplication::translate("VibeStudioSemantics", "Localization Report"),
			QCoreApplication::translate("VibeStudioSemantics", "QA"),
			QCoreApplication::translate("VibeStudioSemantics", "Inspect targets, RTL coverage, formatting, expansion, and catalog status.")),
		paletteEntry(
			QStringLiteral("diagnostics-bundle"),
			QStringLiteral("diagnostics.bundle"),
			QCoreApplication::translate("VibeStudioSemantics", "Copy Diagnostic Bundle"),
			QCoreApplication::translate("VibeStudioSemantics", "Support"),
			QCoreApplication::translate("VibeStudioSemantics", "Copy redacted version, platform, command, module, and localization diagnostics.")),
		paletteEntry(
			QStringLiteral("ai-review"),
			QStringLiteral("ai.review"),
			QCoreApplication::translate("VibeStudioSemantics", "Review AI Proposal"),
			QCoreApplication::translate("VibeStudioSemantics", "AI"),
			QCoreApplication::translate("VibeStudioSemantics", "Open a generated proposal and inspect staged actions before applying anything."),
			true,
			false,
			true),
		paletteEntry(
			QStringLiteral("ai-assistant"),
			QStringLiteral("ai.assistant"),
			QCoreApplication::translate("VibeStudioSemantics", "Assistant"),
			QCoreApplication::translate("VibeStudioSemantics", "AI"),
			QCoreApplication::translate("VibeStudioSemantics", "Ask a model about the open map, code, build, or project, with the context you choose; nothing is sent until you press Send.")),
		paletteEntry(
			QStringLiteral("ai-explain-build"),
			QStringLiteral("ai.explain-build"),
			QCoreApplication::translate("VibeStudioSemantics", "Explain Build Problems"),
			QCoreApplication::translate("VibeStudioSemantics", "AI"),
			QCoreApplication::translate("VibeStudioSemantics", "Open the Assistant with the last build's problems ticked and a question proposed.")),
		paletteEntry(
			QStringLiteral("ai-ask-about-code"),
			QStringLiteral("ai.ask-about-code"),
			QCoreApplication::translate("VibeStudioSemantics", "Ask About This Code"),
			QCoreApplication::translate("VibeStudioSemantics", "AI"),
			QCoreApplication::translate("VibeStudioSemantics", "Open the Assistant with the selected code, or the open file, ticked and a question proposed.")),
	};

	// Level editor commands the editor profiles give keys to (see
	// core/level_editor_controls.h). The VibeStudio profile leaves them
	// without one, so they are listed here rather than among the shortcuts,
	// and the shell keeps whatever keys they get to the Levels page.
	static const struct {
		const char* id;
		const char* commandId;
		const char* label;
		const char* summary;
	} kLevelCommands[] = {
		{"map-cycle-view", "map.cycle-view", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Cycle Map View"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Step one view through 3D, Top, Front, and Side; beside the camera, step the 2D view through its planes.")},
		{"map-next-projection", "map.next-projection", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Next 2D View"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Step the 2D view from Top to Front to Side.")},
		{"map-save-view", "map.save-view", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Save Current Level View"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Name and save the level camera, plan views and layout.")},
		{"map-paint-material", "map.paint-material", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Paint Map Materials"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Paint individual map surfaces in the camera with one undo step per stroke.")},
		{"map-draw-brush", "map.draw-brush", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Draw Brush in Camera"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Drag a brush footprint on a construction plane; adjust depth with the wheel.")},
		{"map-place-at-camera", "map.place-at-camera", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Place at Camera Surface"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Place the selected Create class at the surface under the camera centre.")},
		{"map-sample-material", "map.sample-material", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Sample Map Material"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Choose the material of a camera surface without changing the map.")},
		{"map-manage-views", "map.manage-views", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Saved Level Views"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Navigate, update, rename, remove, import or export saved views.")},
		{"map-next-saved-view", "map.next-saved-view", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Next Saved Level View"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Restore the next saved view for this map.")},
		{"map-previous-saved-view", "map.previous-saved-view", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Previous Saved Level View"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Restore the previous saved view for this map.")},
		{"map-view-top", "map.view-top", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Top View"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Show the map from above in the 2D view.")},
		{"map-view-front", "map.view-front", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Front View"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Show the map from the front in the 2D view.")},
		{"map-view-side", "map.view-side", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Side View"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Show the map from the side in the 2D view.")},
		{"map-frame-selection", "map.frame-selection", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Frame Selection"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Frame the selection in the 2D view and turn the 3D camera to it.")},
		{"map-toggle-grid", "map.toggle-grid", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Show Grid"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Show or hide the grid lines in the 2D view.")},
		{"map-toggle-snap", "map.toggle-snap", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Snap to Grid"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Turn grid snapping for drags, nudges, and drawn brushes on or off.")},
		{"map-grid-1", "map.grid1", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Grid 1"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Set the Levels grid to 1 unit.")},
		{"map-grid-2", "map.grid2", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Grid 2"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Set the Levels grid to 2 units.")},
		{"map-grid-4", "map.grid4", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Grid 4"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Set the Levels grid to 4 units.")},
		{"map-grid-8", "map.grid8", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Grid 8"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Set the Levels grid to 8 units.")},
		{"map-grid-16", "map.grid16", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Grid 16"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Set the Levels grid to 16 units.")},
		{"map-grid-32", "map.grid32", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Grid 32"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Set the Levels grid to 32 units.")},
		{"map-grid-64", "map.grid64", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Grid 64"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Set the Levels grid to 64 units.")},
		{"map-grid-128", "map.grid128", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Grid 128"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Set the Levels grid to 128 units.")},
		{"map-grid-256", "map.grid256", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Grid 256"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Set the Levels grid to 256 units.")},
		{"map-isolate-selection", "map.isolate-selection", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Isolate Selection"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Hide everything but the selection; Show All Hidden brings it back.")},
		{"map-carve", "map.carve", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Carve"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Cut the selected brushes out of the brushes they overlap (CSG subtract).")},
		{"map-merge-brushes", "map.merge-brushes", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Merge Brushes"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Preview a convex union and resolve conflicting brush materials, UVs and flags.")},
		{"map-align-surfaces", "map.align-surfaces", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Surface Alignment"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Shift, scale, rotate, fit and align selected brush faces using package material dimensions.")},
		{"map-copy-surface", "map.copySurface", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Copy Surface"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Copy one brush face's material, mapping and flags into the surface clipboard.")},
		{"map-paste-surface", "map.pasteSurface", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Paste Surface"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Paste a copied surface onto the explicit Surfaces target as one undoable operation.")},
		{"map-surface-shift-left", "map.surfaceShiftLeft", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Shift Texture U −"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Shift the surface target by the negative U texel step.")},
		{"map-surface-shift-right", "map.surfaceShiftRight", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Shift Texture U +"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Shift the surface target by the positive U texel step.")},
		{"map-surface-shift-down", "map.surfaceShiftDown", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Shift Texture V −"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Shift the surface target by the negative V texel step.")},
		{"map-surface-shift-up", "map.surfaceShiftUp", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Shift Texture V +"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Shift the surface target by the positive V texel step.")},
		{"map-surface-rotate-left", "map.surfaceRotateLeft", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Rotate Texture −"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Rotate surface mapping by the negative angle step around each face centre.")},
		{"map-surface-rotate-right", "map.surfaceRotateRight", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Rotate Texture +"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Rotate surface mapping by the positive angle step around each face centre.")},
		{"map-surface-shrink-u", "map.surfaceShrinkU", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Shrink Texture U"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Divide texture width by the surface size factor, keeping each face centre fixed.")},
		{"map-surface-grow-u", "map.surfaceGrowU", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Grow Texture U"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Multiply texture width by the surface size factor, keeping each face centre fixed.")},
		{"map-surface-shrink-v", "map.surfaceShrinkV", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Shrink Texture V"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Divide texture height by the surface size factor, keeping each face centre fixed.")},
		{"map-surface-grow-v", "map.surfaceGrowV", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Grow Texture V"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Multiply texture height by the surface size factor, keeping each face centre fixed.")},
		{"map-surface-fit", "map.surfaceFit", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Fit Texture 1 × 1"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Fit one texture repeat across each targeted brush face.")},
		{"map-surface-center", "map.surfaceCenter", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Centre Texture"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Centre texture mapping on each targeted brush face.")},
		{"map-edit-patch", "map.edit-patch", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Edit Patch Control Points"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Reshape, subdivide and texture the selected patch.")},
		{"map-cap-patch", "map.cap-patch", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Cap Patch"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Close curved patch boundaries with planar caps and review their material mapping.")},
		{"map-hollow", "map.hollow-selection", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Hollow"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Turn the selected brushes into rooms with walls one grid step thick.")},
		{"map-select-similar", "map.select-similar", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Select All of This Class"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Select every entity of the selected entity's class, or every thing of the selected thing's type.")},
		{"map-load-leak-trail", "map.load-leak-trail", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Load Leak Trail"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Draw a compiler leak point file (.pts or .lin) over the open map.")},
		{"map-zoom-in", "map.zoom-in", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Zoom In"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Zoom the 2D view in about its centre.")},
		{"map-zoom-out", "map.zoom-out", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Zoom Out"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Zoom the 2D view out about its centre.")},
		{"map-toggle-3d", "map.toggle3D", QT_TRANSLATE_NOOP("VibeStudioSemantics", "3D View"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Show or hide the 3D view of the open map.")},
		{"map-maximize-view", "map.maximizeView", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Maximize or Restore Active View"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Temporarily enlarge the focused level pane, preserving the workspace for restoration.")},
		{"editor-gestures", "editor.gestures", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Customize Editor Gestures"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Customize pointer gestures, camera movement keys and mouse-look keys for the current editor profile.")},
		{"map-equalize-views", "map.equalizeViews", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Equalize View Sizes"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Restore the workspace and equalize its visible pane sizes.")},
		{"map-snap-selection", "map.snap-to-grid", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Snap Selection to Grid"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Move the selected objects onto the grid.")},
		{"map-flip-horizontal", "map.flip-horizontal", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Flip Horizontal"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Mirror the selected map objects horizontally in the active view.")},
		{"map-flip-vertical", "map.flip-vertical", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Flip Vertical"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Mirror the selected map objects vertically in the active view.")},
		{"map-rotate-left", "map.rotate-left", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Rotate 90 Degrees Left"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Turn the selection a quarter turn counterclockwise in the active view.")},
		{"map-rotate-right", "map.rotate-right", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Rotate 90 Degrees Right"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Turn the selection a quarter turn clockwise in the active view.")},
		{"map-texture-lock", "map.texture-lock", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Texture Lock"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Keep map surface textures fixed during movement and rotation.")},
		{"map-connect-entities", "map.connect-entities", QT_TRANSLATE_NOOP("VibeStudioSemantics", "Connect Entities"),
			QT_TRANSLATE_NOOP("VibeStudioSemantics", "Make the first selected entity target the others.")},
	};
	for (const auto& command : kLevelCommands) {
		entries.push_back(paletteEntry(QString::fromLatin1(command.id), QString::fromLatin1(command.commandId), QCoreApplication::translate("VibeStudioSemantics", command.label),
			QCoreApplication::translate("VibeStudioSemantics", "Levels"), QCoreApplication::translate("VibeStudioSemantics", command.summary)));
	}

	for (const ShortcutDescriptor& descriptor : shortcutDescriptors()) {
		if (!descriptor.commandId.startsWith(QLatin1String("shell.mode."))) {
			continue;
		}
		entries.push_back(paletteEntry(
			descriptor.id,
			descriptor.commandId,
			descriptor.label,
			QCoreApplication::translate("VibeStudioSemantics", "Modes"),
			descriptor.description));
	}

	std::sort(entries.begin(), entries.end(), [](const CommandPaletteEntry& left, const CommandPaletteEntry& right) {
		return QString::localeAwareCompare(left.label, right.label) < 0;
	});
	return entries;
}

bool commandPaletteEntryForCommandId(const QString& commandId, CommandPaletteEntry* out)
{
	const QString requested = normalizedId(commandId);
	for (const CommandPaletteEntry& entry : commandPaletteEntries()) {
		if (normalizedId(entry.commandId) == requested) {
			if (out) {
				*out = entry;
			}
			return true;
		}
	}
	return false;
}

QStringList shellCommandIds()
{
	QStringList ids;
	for (const ShortcutDescriptor& descriptor : shortcutDescriptors()) {
		ids.push_back(descriptor.commandId);
	}
	for (const CommandPaletteEntry& entry : commandPaletteEntries()) {
		ids.push_back(entry.commandId);
	}
	ids.removeDuplicates();
	ids.sort();
	return ids;
}

bool shellCommandIdExists(const QString& commandId)
{
	const QString requested = normalizedId(commandId);
	if (requested.isEmpty()) {
		return false;
	}
	// Profile construction checks every binding. These built-in identifiers
	// are invariant across locale changes; rebuilding translated shortcut and
	// palette descriptions for each lookup makes shell startup quadratic.
	// Cache identifiers only, so visible labels still use the current locale.
	static const QSet<QString> registered = [] {
		QSet<QString> ids;
		for (const QString& id : shellCommandIds()) {
			ids.insert(normalizedId(id));
		}
		return ids;
	}();
	return registered.contains(requested);
}

QString statusChipSummaryText()
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioSemantics", "Status chips");
	for (const StatusChipDescriptor& descriptor : statusChipDescriptors()) {
		lines << QCoreApplication::translate("VibeStudioSemantics", "- %1 [%2/%3]: %4").arg(descriptor.label, descriptor.domain, operationStateId(descriptor.state), descriptor.nonColorCue);
		lines << QCoreApplication::translate("VibeStudioSemantics", "  Next action: %1").arg(descriptor.nextAction);
	}
	return lines.join('\n');
}

QString shortcutRegistrySummaryText()
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioSemantics", "Shortcut registry");
	QStringList conflicts;
	const bool hasConflicts = shortcutRegistryHasConflicts(&conflicts);
	lines << QCoreApplication::translate("VibeStudioSemantics", "Conflicts: %1").arg(hasConflicts ? conflicts.join(QStringLiteral(", ")) : QCoreApplication::translate("VibeStudioSemantics", "none"));
	for (const ShortcutDescriptor& descriptor : shortcutDescriptors()) {
		QString line = QCoreApplication::translate("VibeStudioSemantics", "- %1: %2 (%3)").arg(descriptor.defaultSequence, descriptor.label, descriptor.context);
		if (!descriptor.alternateSequences.isEmpty()) {
			line += QCoreApplication::translate("VibeStudioSemantics", " - also %1").arg(descriptor.alternateSequences.join(QStringLiteral(", ")));
		}
		lines << line;
	}
	return lines.join('\n');
}

QString commandPaletteSummaryText()
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioSemantics", "Command palette");
	for (const CommandPaletteEntry& entry : commandPaletteEntries()) {
		QStringList flags;
		if (entry.requiresProject) {
			flags.push_back(QCoreApplication::translate("VibeStudioSemantics", "requires project"));
		}
		if (entry.destructive) {
			flags.push_back(entry.stagedOrDryRun ? QCoreApplication::translate("VibeStudioSemantics", "staged") : QCoreApplication::translate("VibeStudioSemantics", "destructive"));
		}
		lines << QCoreApplication::translate("VibeStudioSemantics", "- %1 [%2]%3").arg(entry.label, entry.category, flags.isEmpty() ? QString() : QCoreApplication::translate("VibeStudioSemantics", " - %1").arg(flags.join(QStringLiteral(", "))));
		lines << QCoreApplication::translate("VibeStudioSemantics", "  %1").arg(entry.summary);
	}
	return lines.join('\n');
}

} // namespace vibestudio
