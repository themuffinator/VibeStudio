#include "cli/level_gestures.h"
#include "core/editor_profiles.h"
#include "core/level_gestures.h"
#include "core/level_camera_keys.h"
#include "core/studio_settings.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QSet>

namespace vibestudio::cli {
LevelGesturesCliResult runLevelGestures(const QStringList& arguments)
{
	const auto failure = [](int code, const QString& error) { return LevelGesturesCliResult{code, error, {}, {}}; };
	const QSet<QString> globals {"--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> flags {"--cli", "--json", "--quiet", "--verbose", "--reset", "--dry-run", "--overwrite"};
	const QSet<QString> options {"--input", "--output", "--set"};
	QStringList positional; QSet<QString> seen; QHash<QString, QString> values; QJsonObject changes;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const auto arg = arguments[i];
		if (!arg.startsWith('-')) { positional << arg; continue; }
		const auto equal = arg.indexOf('='); const auto key = equal < 0 ? arg : arg.left(equal);
		if (key != QStringLiteral("--set") && seen.contains(key)) { return failure(2, QCoreApplication::translate("LevelGesturesCli", "Repeated option: %1.").arg(key)); }
		seen.insert(key);
		if (flags.contains(key) && equal < 0) { continue; }
		if (!options.contains(key) && !globals.contains(key)) { return failure(2, QCoreApplication::translate("LevelGesturesCli", "Unexpected option: %1.").arg(arg)); }
		QString value;
		if (equal >= 0) { value = arg.mid(equal + 1); }
		else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith('-')) { value = arguments[++i]; }
		if (value.trimmed().isEmpty()) { return failure(2, QCoreApplication::translate("LevelGesturesCli", "Missing value for %1.").arg(key)); }
		if (key == QStringLiteral("--set")) {
			const auto split = value.indexOf('='); const auto field = value.left(split);
			if (split <= 0 || changes.contains(field)) { return failure(2, QCoreApplication::translate("LevelGesturesCli", "Each --set needs a distinct field=choice pair.")); }
			changes.insert(field, value.mid(split + 1));
		} else { values.insert(key, value); }
	}
	if (positional.size() < 2 || positional.size() > 3) { return failure(2, QCoreApplication::translate("LevelGesturesCli", "Expected editor gestures [profile] with optional --set, --reset, --input or --output.")); }
	const bool reset = seen.contains(QStringLiteral("--reset"));
	const bool importing = values.contains(QStringLiteral("--input"));
	const bool exporting = values.contains(QStringLiteral("--output"));
	const bool writing = reset || importing || !changes.isEmpty();
	const bool dryRun = seen.contains(QStringLiteral("--dry-run"));
	if ((reset && (importing || !changes.isEmpty())) || (importing && !changes.isEmpty()) || (exporting && writing)
		|| (seen.contains(QStringLiteral("--overwrite")) && !exporting) || (dryRun && !writing)) {
		return failure(2, QCoreApplication::translate("LevelGesturesCli", "Use one operation: change fields, reset, import or export. --dry-run previews a preference change; --overwrite applies only to export."));
	}
	StudioSettings settings(StudioSettings::AccessMode::ReadOnly);
	EditorProfileDescriptor profile;
	const auto requested = positional.size() == 3 ? positional.last() : settings.selectedEditorProfileId();
	if (!editorProfileForId(requested, &profile)) { return failure(2, QCoreApplication::translate("LevelGesturesCli", "Unknown editor profile: %1.").arg(requested)); }
	QString error;
	QJsonObject overrides = reset || importing ? QJsonObject() : settings.editorGestureOverrides(profile.id, &error);
	if (!error.isEmpty()) { return failure(4, error); }
	if (importing) {
		QString importedProfile;
		if (!readLevelGestures(values.value("--input"), &importedProfile, &overrides, &error)) { return failure(4, error); }
		if (importedProfile != profile.id) { return failure(2, QCoreApplication::translate("LevelGesturesCli", "This file belongs to %1. Specify that profile when importing.").arg(importedProfile)); }
	}
	for (auto it = changes.begin(); it != changes.end(); ++it) {
		if (it.value() == QStringLiteral("default")) { overrides.remove(it.key()); }
		else { overrides.insert(it.key(), it.value()); }
	}
	LevelEditorControls effective;
	QJsonObject normalized;
	if (!applyLevelGestureOverrides(profile.controls, overrides, &effective, &normalized, &error)) { return failure(4, error); }
	// Even a reset-to-default must name a real field, never silently accept a typo.
	const auto defaults = levelGestureValues(profile.controls);
	for (auto it = changes.begin(); it != changes.end(); ++it) { if (!defaults.contains(it.key())) { return failure(2, QCoreApplication::translate("LevelGesturesCli", "Unknown gesture setting: %1.").arg(it.key())); } }
	if (writing && !dryRun) {
		StudioSettings writable;
		if (!writable.setEditorGestureOverrides(profile.id, normalized, &error)) { return failure(1, error); }
	}
	if (exporting) {
		const auto output = values.value("--output");
		if (!writeLevelGestures(output, profile.id, normalized, seen.contains("--overwrite"), &error, settings.storageLocation())) { return failure(4, error); }
	}
	LevelGesturesCliResult result;
	const auto effectiveValues = levelGestureValues(effective);
	QJsonArray catalog;
	for (const auto& field : levelGestureFields()) {
		QJsonArray options;
		for (const auto& option : field.options) { options << QJsonObject{{"id", option.id}, {"label", option.label}}; }
		catalog << QJsonObject{{"id", field.id}, {"label", field.label}, {"section", field.section},
			{"type", levelGestureFieldKindId(field.kind)},
			{"default", defaults.value(field.id)}, {"value", effectiveValues.value(field.id)}, {"choices", options},
			{"available", levelGestureFieldAvailable(field.id, profile.controls)}};
		result.lines << QStringLiteral("%1 = %2%3").arg(field.id, effectiveValues.value(field.id).toString(), normalized.contains(field.id) ? QStringLiteral(" *") : QString());
	}
	result.lines.prepend(QCoreApplication::translate("LevelGesturesCli", "%1 gestures; %2 override(s)%3.").arg(profile.displayName).arg(normalized.size())
		.arg(dryRun ? QCoreApplication::translate("LevelGesturesCli", " (preview only)") : QString()));
	const auto warnings = levelCameraShortcutWarnings(profile, effective, settings.userShortcuts());
	result.lines.append(warnings);
	result.payload = {{"profileId", profile.id}, {"overrides", normalized}, {"values", effectiveValues}, {"fields", catalog},
		{"shortcutWarnings", QJsonArray::fromStringList(warnings)}, {"dryRun", dryRun}, {"applied", writing && !dryRun}};
	return result;
}
} // namespace vibestudio::cli
