#include "core/studio_semantics.h"

#include <QCoreApplication>
#include <QHash>

#include <algorithm>
#include <utility>

namespace vibestudio {

namespace {

QString semanticsText(const char* source)
{
	return QCoreApplication::translate("VibeStudioSemantics", source);
}

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
			semanticsText("Project Ready"),
			semanticsText("A project manifest is loaded or can be initialized from the selected folder."),
			OperationState::Completed,
			QStringLiteral("folder"),
			QStringLiteral("completed"),
			semanticsText("checkmark plus Ready text"),
			semanticsText("Open the workspace dashboard or validate the project."),
		},
		{
			QStringLiteral("project-needs-attention"),
			QStringLiteral("project"),
			semanticsText("Project Needs Attention"),
			semanticsText("The project can be inspected, but validation found warnings or missing optional context."),
			OperationState::Warning,
			QStringLiteral("folder"),
			QStringLiteral("warning"),
			semanticsText("warning triangle plus Needs Attention text"),
			semanticsText("Open project validation details."),
		},
		{
			QStringLiteral("package-loaded"),
			QStringLiteral("package"),
			semanticsText("Package Loaded"),
			semanticsText("The selected folder, PAK, WAD, ZIP, or PK3 is open and entries are indexed."),
			OperationState::Completed,
			QStringLiteral("archive"),
			QStringLiteral("completed"),
			semanticsText("archive icon plus Loaded text"),
			semanticsText("Browse entries or open the package detail drawer."),
		},
		{
			QStringLiteral("package-staged"),
			QStringLiteral("package"),
			semanticsText("Package Staged"),
			semanticsText("Package edits are staged for a non-destructive save-as operation."),
			OperationState::Warning,
			QStringLiteral("archive"),
			QStringLiteral("staged"),
			semanticsText("staged badge plus Save-As Required text"),
			semanticsText("Review staged changes and choose a save-as path."),
		},
		{
			QStringLiteral("package-read-only"),
			QStringLiteral("package"),
			semanticsText("Package Read-Only"),
			semanticsText("The package is mounted for inspection and extraction, but no in-place mutation will be performed."),
			OperationState::Completed,
			QStringLiteral("lock"),
			QStringLiteral("read-only"),
			semanticsText("lock icon plus Read-Only text"),
			semanticsText("Use staged save-as if changes are needed."),
		},
		{
			QStringLiteral("compiler-ready"),
			QStringLiteral("compiler"),
			semanticsText("Compiler Ready"),
			semanticsText("A compiler profile has enough executable and input context to produce a command plan."),
			OperationState::Completed,
			QStringLiteral("terminal"),
			QStringLiteral("completed"),
			semanticsText("terminal icon plus Ready text"),
			semanticsText("Plan or run the selected compiler profile."),
		},
		{
			QStringLiteral("compiler-running"),
			QStringLiteral("compiler"),
			semanticsText("Compiler Running"),
			semanticsText("A compiler process is active and task-state/progress logs are available."),
			OperationState::Running,
			QStringLiteral("terminal"),
			QStringLiteral("running"),
			semanticsText("moving progress indicator plus Running text"),
			semanticsText("Watch task logs or cancel when the task allows it."),
		},
		{
			QStringLiteral("compiler-blocked"),
			QStringLiteral("compiler"),
			semanticsText("Compiler Blocked"),
			semanticsText("The compiler profile is missing an executable, input, working directory, or required output context."),
			OperationState::Failed,
			QStringLiteral("terminal"),
			QStringLiteral("failed"),
			semanticsText("stop symbol plus Blocked text"),
			semanticsText("Open compiler registry details and configure the missing path."),
		},
		{
			QStringLiteral("install-confirmable"),
			QStringLiteral("install"),
			semanticsText("Install Confirmable"),
			semanticsText("A detected game installation candidate is visible but will not be saved until the user confirms it."),
			OperationState::Warning,
			QStringLiteral("drive"),
			QStringLiteral("warning"),
			semanticsText("drive icon plus Confirm text"),
			semanticsText("Review the detected path and import or skip it."),
		},
		{
			QStringLiteral("ai-free"),
			QStringLiteral("ai"),
			semanticsText("AI-Free"),
			semanticsText("Cloud and agentic AI workflows are disabled while deterministic local workflows remain available."),
			OperationState::Completed,
			QStringLiteral("shield"),
			QStringLiteral("local"),
			semanticsText("shield icon plus AI-Free text"),
			semanticsText("Continue with local package, project, compiler, and CLI workflows."),
		},
		{
			QStringLiteral("ai-cloud-optional"),
			QStringLiteral("ai"),
			semanticsText("Cloud AI Optional"),
			semanticsText("Cloud connector settings exist, but core workflows remain available without provider credentials."),
			OperationState::Idle,
			QStringLiteral("cloud"),
			QStringLiteral("cloud"),
			semanticsText("cloud icon plus Optional text"),
			semanticsText("Configure a connector only after reviewing consent and redaction settings."),
		},
		{
			QStringLiteral("ai-review-required"),
			QStringLiteral("ai"),
			semanticsText("AI Review Required"),
			semanticsText("An AI-generated proposal exists but cannot mutate project files until reviewed and approved."),
			OperationState::Warning,
			QStringLiteral("sparkles"),
			QStringLiteral("staged"),
			semanticsText("review badge plus Approval Required text"),
			semanticsText("Open the proposal review surface."),
		},
		{
			QStringLiteral("validation-failed"),
			QStringLiteral("validation"),
			semanticsText("Validation Failed"),
			semanticsText("A validation pass found blocking issues that should be resolved before release or destructive operations."),
			OperationState::Failed,
			QStringLiteral("badge"),
			QStringLiteral("failed"),
			semanticsText("X badge plus Failed text"),
			semanticsText("Open diagnostics and resolve blocking findings."),
		},
		{
			QStringLiteral("task-paused"),
			QStringLiteral("activity"),
			semanticsText("Task Paused"),
			semanticsText("A task is waiting for user input, a missing path, or an external tool before it can continue."),
			OperationState::Queued,
			QStringLiteral("pause"),
			QStringLiteral("queued"),
			semanticsText("pause icon plus Paused text"),
			semanticsText("Open the task detail drawer and resolve the blocker."),
		},
		{
			QStringLiteral("task-cancelled"),
			QStringLiteral("activity"),
			semanticsText("Task Cancelled"),
			semanticsText("A user-cancelled task has stopped and cleanup/results are visible in the activity details."),
			OperationState::Cancelled,
			QStringLiteral("slash"),
			QStringLiteral("cancelled"),
			semanticsText("slash icon plus Cancelled text"),
			semanticsText("Review cleanup state before starting the task again."),
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
			semanticsText("Command Palette"),
			QStringLiteral("global"),
			QStringLiteral("Ctrl+Shift+P"),
			semanticsText("Open the searchable command palette shell."),
			{QStringLiteral("Ctrl+K")}),
		shortcut(
			QStringLiteral("focus-workspace-search"),
			QStringLiteral("shell.focus-search"),
			semanticsText("Focus Workspace Search"),
			QStringLiteral("global"),
			QStringLiteral("Ctrl+F"),
			semanticsText("Move focus to the workspace search field without changing the active mode.")),
		shortcut(
			QStringLiteral("preferences"),
			QStringLiteral("app.preferences"),
			semanticsText("Preferences"),
			QStringLiteral("global"),
			QStringLiteral("Ctrl+,"),
			semanticsText("Open language, accessibility, theme, density, editor profile, and automation preferences.")),
		shortcut(
			QStringLiteral("about"),
			QStringLiteral("app.about"),
			semanticsText("About VibeStudio"),
			QStringLiteral("global"),
			QStringLiteral("Shift+F1"),
			semanticsText("Show version, platform, module, and credits information.")),
		shortcut(
			QStringLiteral("quit"),
			QStringLiteral("app.quit"),
			semanticsText("Quit"),
			QStringLiteral("global"),
			QStringLiteral("Ctrl+Q"),
			semanticsText("Close the studio shell after offering to review unsaved staged work.")),

		// Projects.
		shortcut(
			QStringLiteral("open-project"),
			QStringLiteral("project.open"),
			semanticsText("Open Project"),
			QStringLiteral("project"),
			QStringLiteral("Ctrl+O"),
			semanticsText("Open or remember a project folder without changing project files.")),
		shortcut(
			QStringLiteral("initialize-manifest"),
			QStringLiteral("project.initialize-manifest"),
			semanticsText("Initialize Project Manifest"),
			QStringLiteral("project"),
			QStringLiteral("Ctrl+Shift+I"),
			semanticsText("Write a new studio manifest into the selected project folder.")),
		shortcut(
			QStringLiteral("copy-manifest"),
			QStringLiteral("project.copy-manifest"),
			semanticsText("Copy Project Manifest"),
			QStringLiteral("project"),
			QStringLiteral("Ctrl+Alt+M"),
			semanticsText("Copy the resolved project manifest to the clipboard for review or support.")),

		// Packages.
		shortcut(
			QStringLiteral("package-open"),
			QStringLiteral("package.open"),
			semanticsText("Open Package"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Alt+O"),
			semanticsText("Open a PAK, WAD, ZIP, or PK3 package for browsing.")),
		shortcut(
			QStringLiteral("package-open-folder"),
			QStringLiteral("package.open-folder"),
			semanticsText("Open Folder As Package"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Shift+O"),
			semanticsText("Mount a plain folder with the same read-only package semantics as an archive.")),
		shortcut(
			QStringLiteral("package-close"),
			QStringLiteral("package.close"),
			semanticsText("Close Package"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+W"),
			semanticsText("Close the open package and report any staged edits that would be discarded.")),
		shortcut(
			QStringLiteral("package-save-as"),
			QStringLiteral("package.save-as"),
			semanticsText("Save Package As"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Shift+S"),
			semanticsText("Write staged package changes to a new package path.")),
		shortcut(
			QStringLiteral("package-extract-selected"),
			QStringLiteral("package.extract-selected"),
			semanticsText("Extract Selected Entries"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+E"),
			semanticsText("Extract the selected package entries into a chosen output folder.")),
		shortcut(
			QStringLiteral("package-extract-all"),
			QStringLiteral("package.extract-all"),
			semanticsText("Extract All Entries"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Shift+E"),
			semanticsText("Extract every package entry into a chosen output folder.")),
		shortcut(
			QStringLiteral("package-stage-add"),
			QStringLiteral("package.stage-add"),
			semanticsText("Stage Add"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Alt+A"),
			semanticsText("Stage new files for the next package save-as without touching the open package.")),
		shortcut(
			QStringLiteral("package-stage-replace"),
			QStringLiteral("package.stage-replace"),
			semanticsText("Stage Replace"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Alt+R"),
			semanticsText("Stage a replacement for the selected entry without touching the open package.")),
		shortcut(
			QStringLiteral("package-stage-rename"),
			QStringLiteral("package.stage-rename"),
			semanticsText("Stage Rename"),
			QStringLiteral("package"),
			QStringLiteral("F2"),
			semanticsText("Stage a new archive path for the selected entry.")),
		shortcut(
			QStringLiteral("package-stage-delete"),
			QStringLiteral("package.stage-delete"),
			semanticsText("Stage Delete"),
			QStringLiteral("package"),
			QStringLiteral("Del"),
			semanticsText("Stage the selected entry for removal from the next saved package.")),

		// Level documents.
		shortcut(
			QStringLiteral("map-open"),
			QStringLiteral("map.open"),
			semanticsText("Open Map"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+M"),
			semanticsText("Open a Quake/Quake III .map or a Doom map lump for inspection and editing.")),
		shortcut(
			QStringLiteral("map-save-as"),
			QStringLiteral("map.save-as"),
			semanticsText("Save Map As"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+Alt+S"),
			semanticsText("Write the edited level document to a new path, never over the source in place.")),

		// Compilers and the build pipeline.
		shortcut(
			QStringLiteral("compiler-run"),
			QStringLiteral("compiler.run"),
			semanticsText("Run Compiler Profile"),
			QStringLiteral("compiler"),
			QStringLiteral("Ctrl+R"),
			semanticsText("Run the selected compiler profile through the shared task runner.")),
		shortcut(
			QStringLiteral("compiler-copy-cli"),
			QStringLiteral("compiler.copy-cli"),
			semanticsText("Copy CLI Equivalent"),
			QStringLiteral("compiler"),
			QStringLiteral("Ctrl+Shift+C"),
			semanticsText("Copy a reproducible command line for the selected GUI action.")),
		shortcut(
			QStringLiteral("build-run-pipeline"),
			QStringLiteral("build.run-pipeline"),
			semanticsText("Run Build Pipeline"),
			QStringLiteral("build"),
			QStringLiteral("F7"),
			semanticsText("Run the ordered compile, validate, and package pipeline with per-step task state.")),

		// Game installations.
		shortcut(
			QStringLiteral("detect-installations"),
			QStringLiteral("game.detect-installations"),
			semanticsText("Detect Game Installations"),
			QStringLiteral("game"),
			QStringLiteral("Ctrl+Shift+G"),
			semanticsText("Scan for candidate installations and present them for confirmation without writing game files.")),
		shortcut(
			QStringLiteral("launch-game"),
			QStringLiteral("game.launch"),
			semanticsText("Launch Game"),
			QStringLiteral("game"),
			QStringLiteral("Ctrl+G"),
			semanticsText("Launch the selected installation with the current mod/map arguments.")),

		// Activity and support.
		shortcut(
			QStringLiteral("cancel-task"),
			QStringLiteral("activity.cancel"),
			semanticsText("Cancel Selected Task"),
			QStringLiteral("activity"),
			QStringLiteral("Esc"),
			semanticsText("Cancel a selected cancellable task and report cleanup state."),
			{},
			false),
		shortcut(
			QStringLiteral("diagnostics-bundle"),
			QStringLiteral("diagnostics.bundle"),
			semanticsText("Copy Diagnostic Bundle"),
			QStringLiteral("diagnostics"),
			QStringLiteral("Ctrl+Alt+D"),
			semanticsText("Copy a redacted diagnostic bundle for support and reproducibility.")),

		// Mode rail. Ctrl+1..Ctrl+9 select the nine work surfaces in rail order;
		// Ctrl+0 opens settings, which is a destination rather than a mode.
		shortcut(
			QStringLiteral("mode-workspace"),
			QStringLiteral("shell.mode.workspace"),
			semanticsText("Workspace Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+1"),
			semanticsText("Show the workspace dashboard.")),
		shortcut(
			QStringLiteral("mode-levels"),
			QStringLiteral("shell.mode.levels"),
			semanticsText("Levels Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+2"),
			semanticsText("Show the level editor surface.")),
		shortcut(
			QStringLiteral("mode-models"),
			QStringLiteral("shell.mode.models"),
			semanticsText("Models Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+3"),
			semanticsText("Show the model browser and inspector.")),
		shortcut(
			QStringLiteral("mode-textures"),
			QStringLiteral("shell.mode.textures"),
			semanticsText("Textures Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+4"),
			semanticsText("Show the texture and palette surface.")),
		shortcut(
			QStringLiteral("mode-audio"),
			QStringLiteral("shell.mode.audio"),
			semanticsText("Audio Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+5"),
			semanticsText("Show the audio browser and waveform preview.")),
		shortcut(
			QStringLiteral("mode-packages"),
			QStringLiteral("shell.mode.packages"),
			semanticsText("Packages Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+6"),
			semanticsText("Show the package manager surface.")),
		shortcut(
			QStringLiteral("mode-code"),
			QStringLiteral("shell.mode.code"),
			semanticsText("Code Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+7"),
			semanticsText("Show the code and script editor surface.")),
		shortcut(
			QStringLiteral("mode-shaders"),
			QStringLiteral("shell.mode.shaders"),
			semanticsText("Shaders Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+8"),
			semanticsText("Show the shader and material surface.")),
		shortcut(
			QStringLiteral("mode-build"),
			QStringLiteral("shell.mode.build"),
			semanticsText("Build Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+9"),
			semanticsText("Show the compiler, pipeline, and activity surface.")),
		shortcut(
			QStringLiteral("mode-settings"),
			QStringLiteral("shell.mode.settings"),
			semanticsText("Settings Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+0"),
			semanticsText("Show settings, setup progress, and diagnostics.")),
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
	// The shell installs these as window-level actions, so a sequence may be
	// claimed exactly once across the whole registry -- alternates included.
	QHash<QString, QString> owners;
	QStringList found;
	for (const ShortcutDescriptor& descriptor : shortcutDescriptors()) {
		QStringList sequences;
		sequences << descriptor.defaultSequence;
		sequences << descriptor.alternateSequences;
		for (const QString& sequence : std::as_const(sequences)) {
			const QString key = normalizedSequence(sequence);
			if (key.isEmpty()) {
				continue;
			}
			const auto owner = owners.constFind(key);
			if (owner != owners.constEnd()) {
				found.push_back(QStringLiteral("%1 claimed by %2 and %3").arg(sequence.trimmed(), owner.value(), descriptor.commandId));
				continue;
			}
			owners.insert(key, descriptor.commandId);
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
		paletteEntry(
			QStringLiteral("open-project"),
			QStringLiteral("project.open"),
			semanticsText("Open Project"),
			semanticsText("Workspace"),
			semanticsText("Open or remember a project folder.")),
		paletteEntry(
			QStringLiteral("initialize-manifest"),
			QStringLiteral("project.initialize-manifest"),
			semanticsText("Initialize Project Manifest"),
			semanticsText("Workspace"),
			semanticsText("Write a studio manifest into the selected project folder."),
			true),
		paletteEntry(
			QStringLiteral("copy-manifest"),
			QStringLiteral("project.copy-manifest"),
			semanticsText("Copy Project Manifest"),
			semanticsText("Workspace"),
			semanticsText("Copy the resolved manifest for review, support, or automation."),
			true),
		paletteEntry(
			QStringLiteral("validate-project"),
			QStringLiteral("project.validate"),
			semanticsText("Validate Project"),
			semanticsText("Workspace"),
			semanticsText("Run project health checks and show blocking issues."),
			true),
		paletteEntry(
			QStringLiteral("focus-workspace-search"),
			QStringLiteral("shell.focus-search"),
			semanticsText("Focus Workspace Search"),
			semanticsText("Shell"),
			semanticsText("Move focus to the workspace search field.")),
		paletteEntry(
			QStringLiteral("command-palette"),
			QStringLiteral("shell.command-palette"),
			semanticsText("Show Command Palette"),
			semanticsText("Shell"),
			semanticsText("Open the searchable command palette.")),
		paletteEntry(
			QStringLiteral("preferences"),
			QStringLiteral("app.preferences"),
			semanticsText("Preferences"),
			semanticsText("Shell"),
			semanticsText("Open language, accessibility, theme, editor profile, and automation preferences.")),
		paletteEntry(
			QStringLiteral("about"),
			QStringLiteral("app.about"),
			semanticsText("About VibeStudio"),
			semanticsText("Support"),
			semanticsText("Show version, platform, module, and credits information.")),
		paletteEntry(
			QStringLiteral("quit"),
			QStringLiteral("app.quit"),
			semanticsText("Quit VibeStudio"),
			semanticsText("Shell"),
			semanticsText("Close the studio shell after reviewing staged work.")),
		paletteEntry(
			QStringLiteral("open-package"),
			QStringLiteral("package.open"),
			semanticsText("Open Package"),
			semanticsText("Packages"),
			semanticsText("Open a PAK, WAD, ZIP, or PK3 package.")),
		paletteEntry(
			QStringLiteral("open-folder-package"),
			QStringLiteral("package.open-folder"),
			semanticsText("Open Folder As Package"),
			semanticsText("Packages"),
			semanticsText("Mount a plain folder with read-only package semantics.")),
		paletteEntry(
			QStringLiteral("close-package"),
			QStringLiteral("package.close"),
			semanticsText("Close Package"),
			semanticsText("Packages"),
			semanticsText("Close the open package and report staged edits that would be discarded.")),
		paletteEntry(
			QStringLiteral("save-package-as"),
			QStringLiteral("package.save-as"),
			semanticsText("Save Staged Package As"),
			semanticsText("Packages"),
			semanticsText("Write staged package edits to a new package path."),
			false,
			true,
			true),
		paletteEntry(
			QStringLiteral("extract-selected"),
			QStringLiteral("package.extract-selected"),
			semanticsText("Extract Selected Entries"),
			semanticsText("Packages"),
			semanticsText("Extract the selected entries into a chosen output folder.")),
		paletteEntry(
			QStringLiteral("extract-all"),
			QStringLiteral("package.extract-all"),
			semanticsText("Extract All Entries"),
			semanticsText("Packages"),
			semanticsText("Extract every entry into a chosen output folder.")),
		paletteEntry(
			QStringLiteral("stage-add"),
			QStringLiteral("package.stage-add"),
			semanticsText("Stage Add"),
			semanticsText("Packages"),
			semanticsText("Stage new files for the next package save-as."),
			false,
			false,
			true),
		paletteEntry(
			QStringLiteral("stage-replace"),
			QStringLiteral("package.stage-replace"),
			semanticsText("Stage Replace"),
			semanticsText("Packages"),
			semanticsText("Stage a replacement for the selected entry."),
			false,
			false,
			true),
		paletteEntry(
			QStringLiteral("stage-rename"),
			QStringLiteral("package.stage-rename"),
			semanticsText("Stage Rename"),
			semanticsText("Packages"),
			semanticsText("Stage a new archive path for the selected entry."),
			false,
			false,
			true),
		paletteEntry(
			QStringLiteral("stage-delete"),
			QStringLiteral("package.stage-delete"),
			semanticsText("Stage Delete"),
			semanticsText("Packages"),
			semanticsText("Stage the selected entry for removal from the next saved package."),
			false,
			true,
			true),
		paletteEntry(
			QStringLiteral("open-map"),
			QStringLiteral("map.open"),
			semanticsText("Open Map"),
			semanticsText("Maps"),
			semanticsText("Open a .map file or a Doom map lump.")),
		paletteEntry(
			QStringLiteral("save-map-as"),
			QStringLiteral("map.save-as"),
			semanticsText("Save Map As"),
			semanticsText("Maps"),
			semanticsText("Write the edited level document to a new path."),
			false,
			true,
			true),
		paletteEntry(
			QStringLiteral("run-compiler"),
			QStringLiteral("compiler.run"),
			semanticsText("Run Selected Compiler Profile"),
			semanticsText("Compilers"),
			semanticsText("Run the selected compiler profile with task-state feedback."),
			true),
		paletteEntry(
			QStringLiteral("copy-cli-equivalent"),
			QStringLiteral("compiler.copy-cli"),
			semanticsText("Copy CLI Equivalent"),
			semanticsText("Compilers"),
			semanticsText("Copy a reproducible command line for the selected GUI action."),
			true),
		paletteEntry(
			QStringLiteral("run-build-pipeline"),
			QStringLiteral("build.run-pipeline"),
			semanticsText("Run Build Pipeline"),
			semanticsText("Build"),
			semanticsText("Run the ordered compile, validate, and package pipeline."),
			true),
		paletteEntry(
			QStringLiteral("detect-installations"),
			QStringLiteral("game.detect-installations"),
			semanticsText("Detect Game Installations"),
			semanticsText("Game"),
			semanticsText("Scan for candidate installations and confirm them before saving.")),
		paletteEntry(
			QStringLiteral("launch-game"),
			QStringLiteral("game.launch"),
			semanticsText("Launch Game"),
			semanticsText("Game"),
			semanticsText("Launch the selected installation with the current mod and map arguments.")),
		paletteEntry(
			QStringLiteral("cancel-task"),
			QStringLiteral("activity.cancel"),
			semanticsText("Cancel Selected Task"),
			semanticsText("Activity"),
			semanticsText("Cancel a cancellable task and report its cleanup state.")),
		paletteEntry(
			QStringLiteral("localization-report"),
			QStringLiteral("localization.report"),
			semanticsText("Localization Report"),
			semanticsText("QA"),
			semanticsText("Inspect targets, RTL coverage, formatting, expansion, and catalog status.")),
		paletteEntry(
			QStringLiteral("diagnostics-bundle"),
			QStringLiteral("diagnostics.bundle"),
			semanticsText("Copy Diagnostic Bundle"),
			semanticsText("Support"),
			semanticsText("Copy redacted version, platform, command, module, and localization diagnostics.")),
		paletteEntry(
			QStringLiteral("ai-review"),
			QStringLiteral("ai.review"),
			semanticsText("Review AI Proposal"),
			semanticsText("AI"),
			semanticsText("Open a generated proposal and inspect staged actions before applying anything."),
			true,
			false,
			true),
	};

	for (const ShortcutDescriptor& descriptor : shortcutDescriptors()) {
		if (!descriptor.commandId.startsWith(QLatin1String("shell.mode."))) {
			continue;
		}
		entries.push_back(paletteEntry(
			descriptor.id,
			descriptor.commandId,
			descriptor.label,
			semanticsText("Modes"),
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
	for (const QString& id : shellCommandIds()) {
		if (normalizedId(id) == requested) {
			return true;
		}
	}
	return false;
}

QString statusChipSummaryText()
{
	QStringList lines;
	lines << semanticsText("Status chips");
	for (const StatusChipDescriptor& descriptor : statusChipDescriptors()) {
		lines << semanticsText("- %1 [%2/%3]: %4").arg(descriptor.label, descriptor.domain, operationStateId(descriptor.state), descriptor.nonColorCue);
		lines << semanticsText("  Next action: %1").arg(descriptor.nextAction);
	}
	return lines.join('\n');
}

QString shortcutRegistrySummaryText()
{
	QStringList lines;
	lines << semanticsText("Shortcut registry");
	QStringList conflicts;
	const bool hasConflicts = shortcutRegistryHasConflicts(&conflicts);
	lines << semanticsText("Conflicts: %1").arg(hasConflicts ? conflicts.join(QStringLiteral(", ")) : semanticsText("none"));
	for (const ShortcutDescriptor& descriptor : shortcutDescriptors()) {
		QString line = semanticsText("- %1: %2 (%3)").arg(descriptor.defaultSequence, descriptor.label, descriptor.context);
		if (!descriptor.alternateSequences.isEmpty()) {
			line += semanticsText(" - also %1").arg(descriptor.alternateSequences.join(QStringLiteral(", ")));
		}
		lines << line;
	}
	return lines.join('\n');
}

QString commandPaletteSummaryText()
{
	QStringList lines;
	lines << semanticsText("Command palette");
	for (const CommandPaletteEntry& entry : commandPaletteEntries()) {
		QStringList flags;
		if (entry.requiresProject) {
			flags.push_back(semanticsText("requires project"));
		}
		if (entry.destructive) {
			flags.push_back(entry.stagedOrDryRun ? semanticsText("staged") : semanticsText("destructive"));
		}
		lines << semanticsText("- %1 [%2]%3").arg(entry.label, entry.category, flags.isEmpty() ? QString() : semanticsText(" - %1").arg(flags.join(QStringLiteral(", "))));
		lines << semanticsText("  %1").arg(entry.summary);
	}
	return lines.join('\n');
}

} // namespace vibestudio
