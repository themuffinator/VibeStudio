#include "cli/level_region_selection.h"
#include "core/entity_builtin_catalogue.h"
#include "core/level_document.h"
#include "core/level_placement.h"
#include "core/level_scene.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>

namespace vibestudio::cli {
namespace { struct Text { Q_DECLARE_TR_FUNCTIONS(LevelRegionSelectionCli) }; }
LevelRegionSelectionCliResult runLevelRegionSelection(const QStringList& arguments)
{
	LevelRegionSelectionCliResult result; result.payload.insert("mapWritten",false);
	const auto fail = [&](int code, const QString& message) { result.exitCode = code; result.error = message; return result; };
	const QSet<QString> flags{"--cli","--json","--quiet","--verbose","--dry-run","--overwrite"};
	const QSet<QString> options{"--object","--mode","--axis","--policy","--output","--definitions","--context","--map-name","--engine",
		"--settings-file","--locale","--catalog-root"};
	QSet<QString> seen; QHash<QString,QString> values; QStringList positional, selectors;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const auto arg = arguments[i]; if (!arg.startsWith('-')) { positional.append(arg); continue; }
		const auto equal = arg.indexOf('='); const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key) && key != QLatin1String("--object")) { return fail(2,Text::tr("Repeated option: %1.").arg(key)); }
		seen.insert(key);
		if (flags.contains(key) && equal < 0) { continue; }
		if (!options.contains(key)) { return fail(2,Text::tr("Unexpected option: %1.").arg(arg)); }
		QString value;
		if (equal >= 0) { value = arg.mid(equal+1); }
		else if (i+1 < arguments.size() && !arguments[i+1].startsWith('-')) { value = arguments[++i]; }
		if (value.trimmed().isEmpty()) { return fail(2,Text::tr("Missing value for %1.").arg(key)); }
		if (key == QLatin1String("--object")) { selectors.append(value); } else { values.insert(key,value); }
	}
	if (positional.size() != 3 || !QStringList{"map","maps","level"}.contains(positional[0]) || positional[1] != QLatin1String("select-region")
		|| selectors.isEmpty() || selectors.size() > 16384) { return fail(2,Text::tr("Expected map select-region <map> and 1–16,384 --object selectors.")); }
	const auto policy = values.value("--policy","bounds"), mode = values.value("--mode","inside"), axis = values.value("--axis","z");
	if (!QStringList{"bounds","trenchbroom","radiant"}.contains(policy)) { return fail(2,Text::tr("--policy is bounds, trenchbroom or radiant.")); }
	const QStringList modes{"inside","touching","complete-tall","partial-tall"}, axes{"x","y","z"};
	if (!modes.contains(mode) || !axes.contains(axis)) { return fail(2,Text::tr("--mode is inside, touching, complete-tall or partial-tall; --axis is x, y or z.")); }
	const bool output = values.contains("--output"), dryRun = seen.contains("--dry-run");
	if (!output && (dryRun || seen.contains("--overwrite"))) { return fail(2,Text::tr("--dry-run and --overwrite require an --output map path.")); }
	QString error; LevelMapDocument source;
	if (!loadLevelMap({QFileInfo(positional[2]).absoluteFilePath(),values.value("--map-name"),values.value("--engine")},&source,&error)) { return fail(4,error); }
	if (values.contains("--context") && !openLevelSceneGroup(&source,values.value("--context"),&error)) { return fail(4,error); }
	QVector<LevelMapSelectionRef> selection;
	for (const auto& selector : selectors) {
		if (!selectLevelMapObject(&source,selector,&error)) { return fail(2,error); }
		for (const auto& ref : source.selection) { if (!selection.contains(ref)) { selection.append(ref); } }
	}
	if (!setLevelMapSelection(&source,selection,&error)) { return fail(2,error); }
	LevelPlacementRequest request; request.operation = LevelPlacementOperation::SelectRegion;
	request.region.policy = policy == QLatin1String("trenchbroom") ? LevelRegionSelectionPolicy::TrenchBroom
		: policy == QLatin1String("radiant") ? LevelRegionSelectionPolicy::Radiant : LevelRegionSelectionPolicy::Bounds;
	request.region.mode = static_cast<LevelMapRegionSelection>(modes.indexOf(mode)); request.region.axis = static_cast<int>(axes.indexOf(axis));
	const auto definitions = values.contains("--definitions") ? loadEntityDefinitions({values.value("--definitions")})
		: builtinEntityDefinitions(builtinEntityGameForMap(source));
	if (!definitions.error.isEmpty() || (values.contains("--definitions") && definitions.isEmpty())) {
		return fail(4,Text::tr("Could not load entity definitions: %1.").arg(definitions.error));
	}
	for (const auto& definition : definitions.classes) {
		if (definition.kind == EntityClassKind::Point && definition.hasSize) {
			request.region.entityBounds.insert(definition.className.toCaseFolded(),{{definition.mins[0],definition.mins[1],definition.mins[2]},
				{definition.maxs[0],definition.maxs[1],definition.maxs[2]}});
		}
	}
	const auto prepared = prepareLevelPlacement(source,request);
	if (!prepared.succeeded) { return fail(4,prepared.error); }
	QStringList matches, selected;
	for (const auto& ref : prepared.regionReport.matches) { matches.append(levelMapSelectionRefId(ref)); }
	for (const auto& ref : prepared.document.selection) { selected.append(levelMapSelectionRefId(ref)); }
	result.payload.insert("policy",policy); result.payload.insert("mode",mode); result.payload.insert("axis",axis);
	result.payload.insert("objects",QJsonArray::fromStringList(matches)); result.payload.insert("selection",QJsonArray::fromStringList(selected));
	result.payload.insert("removedObjects",prepared.regionReport.removedObjects); result.payload.insert("queryOnly",!output);
	result.payload.insert("dryRun",dryRun); result.payload.insert("warnings",QJsonArray::fromStringList(definitions.warnings));
	if (output) {
		LevelDocumentSaveRequest save; save.path = QFileInfo(values.value("--output")).absoluteFilePath();
		save.overwrite = seen.contains("--overwrite"); save.dryRun = dryRun;
		const auto saved = writeLevelDocument(prepared.document,save);
		if (!saved.succeeded()) { return fail(4,saved.errors.join('\n')); }
		result.payload.insert("output",save.path); result.payload.insert("backup",saved.backupPath);
		result.payload.insert("mapWritten",!dryRun); result.payload.insert("warnings",QJsonArray::fromStringList(definitions.warnings+saved.warnings));
		result.lines.append(dryRun ? Text::tr("Would save the volume-selection result to %1.").arg(save.path) : Text::tr("Saved the volume-selection result to %1.").arg(save.path));
	} else { result.lines.append(Text::tr("Query only: the source map was not changed.")); }
	result.lines.append(Text::tr("%n matching object(s)",nullptr,static_cast<int>(matches.size()))); result.lines.append(matches);
	return result;
}
} // namespace vibestudio::cli
