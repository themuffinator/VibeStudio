#include "cli/level_view_filters.h"
#include "core/level_view_filters.h"
#include "core/studio_settings.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>

namespace vibestudio::cli {
namespace { struct Text { Q_DECLARE_TR_FUNCTIONS(LevelViewFiltersCli) }; }

LevelViewFiltersCliResult runLevelViewFilters(const QStringList& arguments)
{
	LevelViewFiltersCliResult result; result.payload.insert("mapWritten",false); result.payload.insert("queryOnly",true);
	const auto fail = [&](int code, const QString& message) { result.exitCode = code; result.error = message; return result; };
	const QSet<QString> flags{"--cli","--json","--quiet","--verbose","--saved","--invert","--objects"};
	const QSet<QString> options{"--off","--engine","--map","--settings-file","--locale","--catalog-root"};
	QSet<QString> seen; QHash<QString,QStringList> values; QStringList positional;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const auto arg = arguments[i]; if (!arg.startsWith('-')) { positional.append(arg); continue; }
		const auto equal = arg.indexOf('='); const auto name = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(name) && name != "--off") { return fail(2,Text::tr("Repeated option: %1.").arg(name)); }
		seen.insert(name);
		if (flags.contains(name) && equal < 0) { continue; }
		if (!options.contains(name)) { return fail(2,Text::tr("Unexpected option: %1.").arg(arg)); }
		QString value;
		if (equal >= 0) { value = arg.mid(equal+1); }
		else if (i+1 < arguments.size() && !arguments[i+1].startsWith('-')) { value = arguments[++i]; }
		if (value.trimmed().isEmpty()) { return fail(2,Text::tr("Missing value for %1.").arg(name)); }
		values[name].append(value);
		if (values[name].size() > 256) { return fail(2,Text::tr("At most 256 values are allowed for %1.").arg(name)); }
	}
	if (positional.size() != 3 || !QStringList{"map","maps","level"}.contains(positional[0]) || positional[1] != "filters") {
		return fail(2,Text::tr("Expected map filters <map>, with optional --off <kinds>, --saved, --invert and --objects."));
	}
	LevelMapDocument document; QString error;
	if (!loadLevelMap({QFileInfo(positional[2]).absoluteFilePath(),values.value("--map").value(0),values.value("--engine").value(0)},&document,&error)) {
		return fail(4,error);
	}
	const QVector<LevelViewFilter> filters = levelViewFilters(document);
	QSet<QString> offered;
	for (const auto& filter : filters) { offered.insert(filter.id); }
	// The View tab's saved list keeps kinds for other formats; only the ones
	// this map offers count here, as they do in the views.
	QStringList off;
	if (seen.contains("--saved")) {
		for (const auto& id : levelViewFiltersFromSaved(StudioSettings().shellLayoutState(levelViewFiltersSettingsKey()))) {
			if (offered.contains(id) && !off.contains(id)) { off.append(id); }
		}
	}
	for (const auto& list : values.value("--off")) {
		for (const auto& part : list.split(',',Qt::SkipEmptyParts)) {
			const auto id = part.trimmed();
			if (!offered.contains(id)) {
				QStringList known; for (const auto& filter : filters) { known.append(filter.id); }
				return fail(2,known.isEmpty() ? Text::tr("This map's format has no view filters.")
					: Text::tr("This map has no %1 filter. Choose from: %2.").arg(id,known.join(QStringLiteral(", "))));
			}
			if (!off.contains(id)) { off.append(id); }
		}
	}
	const bool invert = seen.contains("--invert");
	if (invert) { off = levelViewFiltersInverted(document,off); }
	const bool objects = seen.contains("--objects");
	QJsonArray entries;
	for (const auto& filter : filters) {
		const auto matches = levelViewFilterMatches(document,filter.id);
		QJsonObject entry{{"id",filter.id},{"title",filter.title},{"description",filter.description},
			{"count",static_cast<int>(matches.size())},{"shown",!off.contains(filter.id)},{"command",levelViewFilterCommandId(filter.id)}};
		if (objects) {
			QJsonArray selectors; for (const auto& ref : matches) { selectors.append(levelMapSelectionRefId(ref)); }
			entry.insert("objects",selectors);
		}
		entries.append(entry);
	}
	QStringList filtered;
	for (const auto& ref : levelViewFilteredObjects(document,off)) { filtered.append(levelMapSelectionRefId(ref)); }
	result.payload.insert("filters",entries);
	result.payload.insert("off",QJsonArray::fromStringList(off));
	result.payload.insert("inverted",invert);
	result.payload.insert("saved",seen.contains("--saved"));
	result.payload.insert("settingsKey",levelViewFiltersSettingsKey());
	result.payload.insert("filteredObjects",QJsonArray::fromStringList(filtered));
	result.payload.insert("filteredCount",static_cast<int>(filtered.size()));
	result.payload.insert("source",document.sourcePath); result.payload.insert("sourceSha256",QString::fromLatin1(document.sourceContentHash.toHex()));
	result.lines.append(Text::tr("View filters query: the map and the View tab's saved filters are unchanged."));
	if (filters.isEmpty()) { result.lines.append(Text::tr("This map's format has no view filters.")); }
	for (const auto& entry : std::as_const(entries)) {
		const auto object = entry.toObject();
		const int count = object.value("count").toInt();
		const auto state = object.value("shown").toBool() ? Text::tr("shown") : Text::tr("hidden");
		result.lines.append(Text::tr("%1 (%2): %3, %n object(s)",nullptr,count).arg(object.value("title").toString(),object.value("id").toString(),state));
		if (objects) {
			for (const auto& selector : object.value("objects").toArray()) { result.lines.append(QStringLiteral("  ")+selector.toString()); }
		}
	}
	const int hiddenCount = static_cast<int>(filtered.size());
	result.lines.append(Text::tr("The switched-off kinds hide %n object(s) from the views.",nullptr,hiddenCount));
	return result;
}
} // namespace vibestudio::cli
