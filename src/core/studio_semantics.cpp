#include "core/studio_semantics.h"

#include <QSet>

#include <algorithm>

namespace vibestudio {

namespace {

QString semanticsText(const char* source)
{
	return QString::fromUtf8(source);
}

QString normalizedId(QString value)
{
	value = value.trimmed().toLower();
	value.replace('_', '-');
	value.replace(' ', '-');
	return value;
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
			QStringLiteral("folder-check"),
			QStringLiteral("success"),
			semanticsText("checkmark plus Ready text"),
			semanticsText("Open the workspace dashboard or validate the project."),
		},
		{
			QStringLiteral("project-needs-attention"),
			QStringLiteral("project"),
			semanticsText("Project Needs Attention"),
			semanticsText("The project can be inspected, but validation found warnings or missing optional context."),
			OperationState::Warning,
			QStringLiteral("folder-warning"),
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
			QStringLiteral("archive-check"),
			QStringLiteral("success"),
			semanticsText("archive icon plus Loaded text"),
			semanticsText("Browse entries or open the package detail drawer."),
		},
		{
			QStringLiteral("package-staged"),
			QStringLiteral("package"),
			semanticsText("Package Staged"),
			semanticsText("Package edits are staged for a non-destructive save-as operation."),
			OperationState::Warning,
			QStringLiteral("archive-restore"),
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
			QStringLiteral("terminal-check"),
			QStringLiteral("success"),
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
			QStringLiteral("terminal-x"),
			QStringLiteral("failure"),
			semanticsText("stop symbol plus Blocked text"),
			semanticsText("Open compiler registry details and configure the missing path."),
		},
		{
			QStringLiteral("install-confirmable"),
			QStringLiteral("install"),
			semanticsText("Install Confirmable"),
			semanticsText("A detected game installation candidate is visible but will not be saved until the user confirms it."),
			OperationState::Warning,
			QStringLiteral("hard-drive-download"),
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
			QStringLiteral("shield-check"),
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
			QStringLiteral("sparkles-warning"),
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
			QStringLiteral("badge-x"),
			QStringLiteral("failure"),
			semanticsText("X badge plus Failed text"),
			semanticsText("Open diagnostics and resolve blocking findings."),
		},
		{
			QStringLiteral("task-paused"),
			QStringLiteral("activity"),
			semanticsText("Task Paused"),
			semanticsText("A task is waiting for user input, a missing path, or an external tool before it can continue."),
			OperationState::Queued,
			QStringLiteral("pause-circle"),
			QStringLiteral("paused"),
			semanticsText("pause icon plus Paused text"),
			semanticsText("Open the task detail drawer and resolve the blocker."),
		},
		{
			QStringLiteral("task-cancelled"),
			QStringLiteral("activity"),
			semanticsText("Task Cancelled"),
			semanticsText("A user-cancelled task has stopped and cleanup/results are visible in the activity details."),
			OperationState::Cancelled,
			QStringLiteral("circle-slash"),
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

QVector<ShortcutDescriptor> shortcutDescriptors()
{
	return {
		{
			QStringLiteral("open-project"),
			QStringLiteral("project.open"),
			semanticsText("Open Project"),
			QStringLiteral("global"),
			QStringLiteral("Ctrl+O"),
			{},
			semanticsText("Open or remember a project folder without changing project files."),
		},
		{
			QStringLiteral("command-palette"),
			QStringLiteral("shell.command-palette"),
			semanticsText("Command Palette"),
			QStringLiteral("global"),
			QStringLiteral("Ctrl+Shift+P"),
			{QStringLiteral("F1")},
			semanticsText("Open the searchable command palette shell."),
		},
		{
			QStringLiteral("package-open"),
			QStringLiteral("package.open"),
			semanticsText("Open Package"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Alt+O"),
			{},
			semanticsText("Open a folder, PAK, WAD, ZIP, or PK3 package for browsing."),
		},
		{
			QStringLiteral("package-save-as"),
			QStringLiteral("package.save-as"),
			semanticsText("Save Package As"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Shift+S"),
			{},
			semanticsText("Write staged package changes to a new package path."),
		},
		{
			QStringLiteral("compiler-run"),
			QStringLiteral("compiler.run"),
			semanticsText("Run Compiler"),
			QStringLiteral("compiler"),
			QStringLiteral("Ctrl+R"),
			{},
			semanticsText("Run the selected compiler profile through the shared task runner."),
		},
		{
			QStringLiteral("compiler-copy-cli"),
			QStringLiteral("compiler.copy-cli"),
			semanticsText("Copy Compiler CLI"),
			QStringLiteral("compiler"),
			QStringLiteral("Ctrl+Shift+C"),
			{},
			semanticsText("Copy a reproducible compiler command line for support or automation."),
		},
		{
			QStringLiteral("diagnostics-bundle"),
			QStringLiteral("diagnostics.bundle"),
			semanticsText("Export Diagnostics Bundle"),
			QStringLiteral("global"),
			QStringLiteral("Ctrl+Alt+D"),
			{},
			semanticsText("Export a redacted diagnostic bundle for support and reproducibility."),
		},
		{
			QStringLiteral("cancel-task"),
			QStringLiteral("activity.cancel"),
			semanticsText("Cancel Selected Task"),
			QStringLiteral("activity"),
			QStringLiteral("Esc"),
			{},
			semanticsText("Cancel a selected cancellable task and report cleanup state."),
			false,
		},
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
	QSet<QString> seen;
	QStringList found;
	for (const ShortcutDescriptor& descriptor : shortcutDescriptors()) {
		const QString key = QStringLiteral("%1/%2").arg(descriptor.context, descriptor.defaultSequence).toLower();
		if (descriptor.defaultSequence.isEmpty()) {
			continue;
		}
		if (seen.contains(key)) {
			found.push_back(QStringLiteral("%1 in %2").arg(descriptor.defaultSequence, descriptor.context));
		}
		seen.insert(key);
	}
	if (conflicts) {
		*conflicts = found;
	}
	return !found.isEmpty();
}

QVector<CommandPaletteEntry> commandPaletteEntries()
{
	QVector<CommandPaletteEntry> entries = {
		{
			QStringLiteral("open-project"),
			QStringLiteral("project.open"),
			semanticsText("Open Project"),
			semanticsText("Workspace"),
			semanticsText("Open or remember a project folder."),
			QStringLiteral("Ctrl+O"),
		},
		{
			QStringLiteral("validate-project"),
			QStringLiteral("project.validate"),
			semanticsText("Validate Project"),
			semanticsText("Workspace"),
			semanticsText("Run project health checks and show blocking issues."),
			QString(),
			true,
		},
		{
			QStringLiteral("open-package"),
			QStringLiteral("package.open"),
			semanticsText("Open Package"),
			semanticsText("Packages"),
			semanticsText("Open a folder, PAK, WAD, ZIP, or PK3 package."),
			QStringLiteral("Ctrl+Alt+O"),
		},
		{
			QStringLiteral("save-package-as"),
			QStringLiteral("package.save-as"),
			semanticsText("Save Staged Package As"),
			semanticsText("Packages"),
			semanticsText("Write staged package edits to a new package path."),
			QStringLiteral("Ctrl+Shift+S"),
			false,
			true,
			true,
		},
		{
			QStringLiteral("run-compiler"),
			QStringLiteral("compiler.run"),
			semanticsText("Run Compiler Profile"),
			semanticsText("Compilers"),
			semanticsText("Run the selected compiler profile with task-state feedback."),
			QStringLiteral("Ctrl+R"),
			true,
		},
		{
			QStringLiteral("copy-compiler-cli"),
			QStringLiteral("compiler.copy-cli"),
			semanticsText("Copy Compiler CLI"),
			semanticsText("Compilers"),
			semanticsText("Copy a reproducible compiler command line."),
			QStringLiteral("Ctrl+Shift+C"),
			true,
		},
		{
			QStringLiteral("localization-report"),
			QStringLiteral("localization.report"),
			semanticsText("Localization Report"),
			semanticsText("QA"),
			semanticsText("Inspect targets, RTL coverage, formatting, expansion, and catalog status."),
		},
		{
			QStringLiteral("diagnostics-bundle"),
			QStringLiteral("diagnostics.bundle"),
			semanticsText("Export Diagnostics Bundle"),
			semanticsText("Support"),
			semanticsText("Export redacted version, platform, command, module, and localization diagnostics."),
			QStringLiteral("Ctrl+Alt+D"),
		},
		{
			QStringLiteral("ai-review"),
			QStringLiteral("ai.review"),
			semanticsText("Review AI Proposal"),
			semanticsText("AI"),
			semanticsText("Open a generated proposal and inspect staged actions before applying anything."),
			QString(),
			true,
			false,
			true,
		},
	};
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
		lines << semanticsText("- %1: %2 (%3)").arg(descriptor.defaultSequence, descriptor.label, descriptor.context);
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
