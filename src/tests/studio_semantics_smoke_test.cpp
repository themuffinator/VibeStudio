#include "core/studio_semantics.h"

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

} // namespace

int main()
{
	if (vibestudio::statusChipDescriptors().size() < 8) {
		return fail("Expected status chip descriptors for project, package, compiler, install, AI, and validation surfaces.");
	}
	const QStringList domains = vibestudio::statusChipDomains();
	for (const QString& domain : {QStringLiteral("project"), QStringLiteral("package"), QStringLiteral("compiler"), QStringLiteral("install"), QStringLiteral("ai"), QStringLiteral("validation")}) {
		if (!domains.contains(domain)) {
			return fail("Expected status chip domain coverage.");
		}
	}

	vibestudio::StatusChipDescriptor chip;
	if (!vibestudio::statusChipForId(QStringLiteral("compiler-blocked"), &chip) || chip.state != vibestudio::OperationState::Failed || chip.nonColorCue.isEmpty()) {
		return fail("Expected compiler-blocked status chip with non-color cue.");
	}
	QStringList colorTokens;
	for (const vibestudio::StatusChipDescriptor& descriptor : vibestudio::statusChipDescriptors()) {
		colorTokens.push_back(descriptor.colorToken);
	}
	for (const QString& token : {QStringLiteral("success"), QStringLiteral("warning"), QStringLiteral("failure"), QStringLiteral("running"), QStringLiteral("paused"), QStringLiteral("cancelled"), QStringLiteral("local"), QStringLiteral("cloud"), QStringLiteral("staged"), QStringLiteral("read-only")}) {
		if (!colorTokens.contains(token)) {
			return fail("Expected icon/color semantic token coverage.");
		}
	}

	QStringList shortcutConflicts;
	if (vibestudio::shortcutRegistryHasConflicts(&shortcutConflicts) || !shortcutConflicts.isEmpty()) {
		return fail("Expected shortcut registry to be conflict-free.");
	}
	vibestudio::ShortcutDescriptor shortcut;
	if (!vibestudio::shortcutForCommandId(QStringLiteral("shell.command-palette"), &shortcut) || shortcut.defaultSequence != QStringLiteral("Ctrl+Shift+P")) {
		return fail("Expected command palette shortcut registration.");
	}

	vibestudio::CommandPaletteEntry entry;
	if (!vibestudio::commandPaletteEntryForCommandId(QStringLiteral("diagnostics.bundle"), &entry) || entry.category != QStringLiteral("Support")) {
		return fail("Expected diagnostics bundle command palette entry.");
	}
	if (!vibestudio::commandPaletteEntryForCommandId(QStringLiteral("package.save-as"), &entry) || !entry.destructive || !entry.stagedOrDryRun) {
		return fail("Expected package save-as command to be marked staged.");
	}

	if (!vibestudio::statusChipSummaryText().contains(QStringLiteral("Status chips"))
		|| !vibestudio::shortcutRegistrySummaryText().contains(QStringLiteral("Conflicts: none"))
		|| !vibestudio::commandPaletteSummaryText().contains(QStringLiteral("Command palette"))) {
		return fail("Expected human-readable semantics summaries.");
	}

	return EXIT_SUCCESS;
}
