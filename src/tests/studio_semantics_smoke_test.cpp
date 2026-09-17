#include "core/operation_state.h"
#include "core/studio_semantics.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QTranslator>

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

// Proves the registry text really goes through the translation API: a
// QString::fromUtf8() helper could never produce these strings.
class SemanticsTranslator final : public QTranslator {
public:
	QString translate(const char* context, const char* sourceText, const char* disambiguation = nullptr, int n = -1) const override
	{
		Q_UNUSED(disambiguation);
		Q_UNUSED(n);
		if (qstrcmp(context, "VibeStudioSemantics") != 0) {
			return {};
		}
		if (qstrcmp(sourceText, "Command Palette") == 0) {
			return QStringLiteral("<<command palette>>");
		}
		if (qstrcmp(sourceText, "Show Command Palette") == 0) {
			return QStringLiteral("<<show command palette>>");
		}
		if (qstrcmp(sourceText, "Project Ready") == 0) {
			return QStringLiteral("<<project ready>>");
		}
		return {};
	}
};

bool runStatusChipSmoke()
{
	bool ok = true;
	const QVector<vibestudio::StatusChipDescriptor> chips = vibestudio::statusChipDescriptors();
	ok &= expect(chips.size() >= 8, "Expected status chip descriptors for the routed domains.");

	const QStringList domains = vibestudio::statusChipDomains();
	for (const QString& domain : {QStringLiteral("project"), QStringLiteral("package"), QStringLiteral("compiler"), QStringLiteral("install"), QStringLiteral("ai"), QStringLiteral("validation"), QStringLiteral("activity")}) {
		ok &= expect(domains.contains(domain), "Expected status chip domain coverage.");
	}

	vibestudio::StatusChipDescriptor chip;
	ok &= expect(vibestudio::statusChipForId(QStringLiteral("compiler-blocked"), &chip) && chip.state == vibestudio::OperationState::Failed && !chip.nonColorCue.isEmpty(),
		"Expected compiler-blocked status chip with non-color cue.");

	// The token vocabulary must stay small and mechanically mappable.
	QStringList tokenProblems;
	ok &= expect(vibestudio::statusChipTokensAreValid(&tokenProblems) && tokenProblems.isEmpty(),
		"Expected every status chip color/icon token to come from the documented vocabulary.");
	if (!tokenProblems.isEmpty()) {
		std::cerr << qPrintable(tokenProblems.join(QStringLiteral("; "))) << "\n";
	}

	const QStringList colorTokens = vibestudio::statusChipColorTokens();
	const QStringList accents = vibestudio::statusChipAccentColorTokens();
	ok &= expect(colorTokens.size() == vibestudio::operationStateIds().size() + accents.size(),
		"Expected the color token vocabulary to be the operation states plus the documented accents.");
	for (const QString& stateId : vibestudio::operationStateIds()) {
		ok &= expect(colorTokens.contains(stateId), "Expected every operation state id to be a usable color token.");
	}
	for (const vibestudio::StatusChipDescriptor& descriptor : chips) {
		ok &= expect(colorTokens.contains(descriptor.colorToken), "Expected chip color tokens to stay inside the vocabulary.");
		ok &= expect(vibestudio::statusChipIconNames().contains(descriptor.iconName), "Expected chip icon names to stay inside the vocabulary.");
		ok &= expect(accents.contains(descriptor.colorToken) || descriptor.colorToken == vibestudio::operationStateId(descriptor.state),
			"Expected a non-accent color token to equal the chip operation state id.");
	}
	return ok;
}

bool runShortcutSmoke()
{
	bool ok = true;
	QStringList conflicts;
	ok &= expect(!vibestudio::shortcutRegistryHasConflicts(&conflicts) && conflicts.isEmpty(),
		"Expected the shipped shortcut set to be conflict-free.");
	if (!conflicts.isEmpty()) {
		std::cerr << qPrintable(conflicts.join(QStringLiteral("; "))) << "\n";
	}

	// The palette shortcut is reconciled here and nowhere else: Ctrl+Shift+P is
	// the default and Ctrl+K is the documented alternate.
	vibestudio::ShortcutDescriptor palette;
	ok &= expect(vibestudio::shortcutForCommandId(QStringLiteral("shell.command-palette"), &palette), "Expected a command palette shortcut.");
	ok &= expect(palette.defaultSequence == QStringLiteral("Ctrl+Shift+P"), "Expected Ctrl+Shift+P as the command palette default.");
	ok &= expect(palette.alternateSequences.contains(QStringLiteral("Ctrl+K")), "Expected Ctrl+K as the command palette alternate.");

	vibestudio::ShortcutDescriptor cancel;
	ok &= expect(vibestudio::shortcutForCommandId(QStringLiteral("activity.cancel"), &cancel) && !cancel.userRemappable,
		"Expected task cancellation to stay on a fixed sequence.");

	// Everything the shell is about to offer needs a stable command id.
	const QStringList requiredCommandIds = {
		QStringLiteral("project.open"),
		QStringLiteral("project.initialize-manifest"),
		QStringLiteral("project.copy-manifest"),
		QStringLiteral("package.open"),
		QStringLiteral("package.open-folder"),
		QStringLiteral("package.close"),
		QStringLiteral("package.save-as"),
		QStringLiteral("package.extract-selected"),
		QStringLiteral("package.extract-all"),
		QStringLiteral("package.stage-add"),
		QStringLiteral("package.stage-replace"),
		QStringLiteral("package.stage-rename"),
		QStringLiteral("package.stage-delete"),
		QStringLiteral("map.open"),
		QStringLiteral("map.save-as"),
		QStringLiteral("build.run-pipeline"),
		QStringLiteral("compiler.run"),
		QStringLiteral("compiler.copy-cli"),
		QStringLiteral("game.detect-installations"),
		QStringLiteral("game.launch"),
		QStringLiteral("activity.cancel"),
		QStringLiteral("diagnostics.bundle"),
		QStringLiteral("shell.focus-search"),
		QStringLiteral("shell.command-palette"),
		QStringLiteral("shell.mode.workspace"),
		QStringLiteral("shell.mode.levels"),
		QStringLiteral("shell.mode.models"),
		QStringLiteral("shell.mode.textures"),
		QStringLiteral("shell.mode.audio"),
		QStringLiteral("shell.mode.packages"),
		QStringLiteral("shell.mode.code"),
		QStringLiteral("shell.mode.shaders"),
		QStringLiteral("shell.mode.build"),
		QStringLiteral("shell.mode.settings"),
		QStringLiteral("app.preferences"),
		QStringLiteral("app.about"),
		QStringLiteral("app.quit"),
	};
	for (const QString& commandId : requiredCommandIds) {
		ok &= expect(vibestudio::shellCommandIdExists(commandId), "Expected a registered shell command id.");
		ok &= expect(vibestudio::shortcutForCommandId(commandId), "Expected a keyboard shortcut for the routed shell command.");
		ok &= expect(vibestudio::commandPaletteEntryForCommandId(commandId), "Expected a command palette entry for the routed shell command.");
	}
	ok &= expect(!vibestudio::shellCommandIdExists(QStringLiteral("editor-command.editor.mode.face")),
		"Expected editor-only command ids to stay outside the shell registry.");

	for (const vibestudio::ShortcutDescriptor& descriptor : vibestudio::shortcutDescriptors()) {
		ok &= expect(!descriptor.id.isEmpty() && !descriptor.commandId.isEmpty() && !descriptor.label.isEmpty() && !descriptor.context.isEmpty(),
			"Expected every shortcut descriptor to be fully identified.");
		ok &= expect(!descriptor.defaultSequence.trimmed().isEmpty(), "Expected every shortcut descriptor to carry a default sequence.");
	}
	return ok;
}

bool runCommandPaletteSmoke()
{
	bool ok = true;
	vibestudio::CommandPaletteEntry entry;
	ok &= expect(vibestudio::commandPaletteEntryForCommandId(QStringLiteral("diagnostics.bundle"), &entry) && entry.category == QStringLiteral("Support"),
		"Expected diagnostics bundle command palette entry.");
	ok &= expect(vibestudio::commandPaletteEntryForCommandId(QStringLiteral("package.save-as"), &entry) && entry.destructive && entry.stagedOrDryRun,
		"Expected package save-as command to be marked staged.");

	// Palette rows never spell their own sequence.
	for (const vibestudio::CommandPaletteEntry& paletteEntry : vibestudio::commandPaletteEntries()) {
		vibestudio::ShortcutDescriptor descriptor;
		if (vibestudio::shortcutForCommandId(paletteEntry.commandId, &descriptor)) {
			ok &= expect(paletteEntry.defaultShortcut == descriptor.defaultSequence,
				"Expected palette shortcuts to mirror the shortcut registry.");
		} else {
			ok &= expect(paletteEntry.defaultShortcut.isEmpty(), "Expected palette-only commands to advertise no shortcut.");
		}
	}

	ok &= expect(vibestudio::statusChipSummaryText().contains(QStringLiteral("Status chips")), "Expected a status chip summary.");
	ok &= expect(vibestudio::shortcutRegistrySummaryText().contains(QStringLiteral("Conflicts: none")), "Expected a conflict-free shortcut summary.");
	ok &= expect(vibestudio::commandPaletteSummaryText().contains(QStringLiteral("Command palette")), "Expected a command palette summary.");
	return ok;
}

bool runTranslationSmoke()
{
	bool ok = true;
	SemanticsTranslator translator;
	if (!QCoreApplication::installTranslator(&translator)) {
		return expect(false, "Expected the semantics translator to install.");
	}

	vibestudio::ShortcutDescriptor palette;
	ok &= expect(vibestudio::shortcutForCommandId(QStringLiteral("shell.command-palette"), &palette) && palette.label == QStringLiteral("<<command palette>>"),
		"Expected shortcut labels to go through QCoreApplication::translate.");

	vibestudio::StatusChipDescriptor chip;
	ok &= expect(vibestudio::statusChipForId(QStringLiteral("project-ready"), &chip) && chip.label == QStringLiteral("<<project ready>>"),
		"Expected status chip labels to go through QCoreApplication::translate.");

	vibestudio::CommandPaletteEntry entry;
	ok &= expect(vibestudio::commandPaletteEntryForCommandId(QStringLiteral("shell.command-palette"), &entry) && entry.label == QStringLiteral("<<show command palette>>"),
		"Expected command palette labels to go through QCoreApplication::translate.");

	QCoreApplication::removeTranslator(&translator);
	ok &= expect(vibestudio::shortcutForCommandId(QStringLiteral("shell.command-palette"), &palette) && palette.label == QStringLiteral("Command Palette"),
		"Expected the untranslated source text once the translator is removed.");
	return ok;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	ok &= runStatusChipSmoke();
	ok &= runShortcutSmoke();
	ok &= runCommandPaletteSmoke();
	ok &= runTranslationSmoke();
	if (!ok) {
		return fail("studio_semantics smoke test failed.");
	}
	return EXIT_SUCCESS;
}
