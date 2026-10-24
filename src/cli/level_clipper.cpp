#include "cli/level_clipper.h"
#include "core/studio_settings.h"
#include <QCoreApplication>
#include <QSet>

namespace vibestudio::cli {
LevelClipperCliResult runLevelClipper(const QStringList& arguments)
{
	const auto fail = [](const char* message) { return LevelClipperCliResult{2,QCoreApplication::translate("LevelClipperCli",message),{}, {}}; };
	const QSet<QString> flags{"--cli","--json","--quiet","--verbose","--reset","--dry-run"};
	const QSet<QString> options{"--set","--settings-file","--locale","--catalog-root"};
	QSet<QString> seen; QStringList positional; QJsonObject changes;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const auto arg = arguments[i]; if (!arg.startsWith('-')) { positional << arg; continue; }
		const auto equal = arg.indexOf('='); const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key) && key != "--set") { return fail(QT_TRANSLATE_NOOP("LevelClipperCli","An option was repeated.")); }
		seen.insert(key);
		if (flags.contains(key) && equal < 0) { continue; }
		if (!options.contains(key)) { return fail(QT_TRANSLATE_NOOP("LevelClipperCli","Unknown option or a value supplied to a flag.")); }
		QString value;
		if (equal >= 0) { value = arg.mid(equal+1); }
		else if (i+1 < arguments.size() && !arguments[i+1].startsWith("--")) { value = arguments[++i]; }
		if (value.isEmpty()) { return fail(QT_TRANSLATE_NOOP("LevelClipperCli","An option value is missing.")); }
		if (key != "--set") { continue; }
		const auto split = value.indexOf('='); const auto field = value.left(split); const auto choice = value.mid(split+1);
		if (split <= 0 || changes.contains(field) || choice.isEmpty()) { return fail(QT_TRANSLATE_NOOP("LevelClipperCli","Each --set needs a distinct field=value pair.")); }
		if (field == "resetPoints" || field == "resetSide" || field == "twoPointsInPlan" || field == "wrapMerge") {
			if (choice != "on" && choice != "off") { return fail(QT_TRANSLATE_NOOP("LevelClipperCli","Boolean clipper preferences require on or off.")); }
			changes.insert(field,choice == "on");
		} else { changes.insert(field,choice); }
	}
	const bool reset = seen.contains("--reset"), dryRun = seen.contains("--dry-run"), writing = reset || !changes.isEmpty();
	if (positional.size() != 2 || (reset && !changes.isEmpty()) || (dryRun && !writing)) {
		return fail(QT_TRANSLATE_NOOP("LevelClipperCli","Use editor clipper to inspect, --set field=value to change, or --reset. --dry-run previews a change."));
	}
	StudioSettings reader(StudioSettings::AccessMode::ReadOnly);
	auto preferences = reset ? LevelClipperPreferences{} : reader.levelClipperPreferences(); QString error;
	if (!updateLevelClipperPreferences(changes,&preferences,&error)) { return {2,error,{}, {}}; }
	if (writing && !dryRun) {
		StudioSettings writer;
		if (!writer.setLevelClipperPreferences(preferences)) { return {4,QCoreApplication::translate("LevelClipperCli","Unable to save clipper preferences."),{}, {}}; }
	}
	const auto values = levelClipperPreferencesJson(preferences);
	LevelClipperCliResult result; result.payload = {{"values",values},{"applied",writing && !dryRun},{"dryRun",dryRun}};
	for (auto it = values.begin(); it != values.end(); ++it) { result.lines << QStringLiteral("%1 = %2").arg(it.key(),it.value().isBool() ? (it.value().toBool() ? "on" : "off") : it.value().toString()); }
	return result;
}
} // namespace vibestudio::cli
