#include "cli/level_component_selection.h"
#include "core/level_component_selection.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QRegularExpression>

namespace vibestudio::cli {
LevelComponentSelectionCliResult runLevelComponentSelection(const QStringList& arguments)
{
	const auto fail = [](int code, const char* message) { return LevelComponentSelectionCliResult{code,QCoreApplication::translate("LevelComponentSelectionCli",message),{}, {}}; };
	const QSet<QString> options{"--input","--engine","--map-name","--settings-file","--locale","--catalog-root","--set","--operation","--to","--coverage","--steps"};
	const QSet<QString> flags{"--cli","--json","--quiet","--verbose"};
	QSet<QString> seen; QMap<QString,QString> values; QStringList positional, selections;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const auto arg = arguments[i]; if (!arg.startsWith('-')) { positional << arg; continue; }
		const auto equals = arg.indexOf('='); const auto name = equals < 0 ? arg : arg.left(equals);
		if (seen.contains(name) && name != "--set") { return fail(2,QT_TRANSLATE_NOOP("LevelComponentSelectionCli","Repeated option. Only --set may be repeated.")); }
		seen.insert(name);
		if (flags.contains(name)) { if (equals >= 0) { return fail(2,QT_TRANSLATE_NOOP("LevelComponentSelectionCli","Flags do not accept values.")); } continue; }
		if (!options.contains(name)) { return fail(2,QT_TRANSLATE_NOOP("LevelComponentSelectionCli","Unexpected option. Selection queries do not write maps or accept --output.")); }
		QString value;
		if (equals >= 0) { value = arg.mid(equals+1); }
		else if (i+1 < arguments.size() && !arguments[i+1].startsWith("--")) { value = arguments[++i]; }
		if (value.trimmed().isEmpty()) { return fail(2,QT_TRANSLATE_NOOP("LevelComponentSelectionCli","An option value is missing.")); }
		if (name == "--set") { selections << value; } else { values.insert(name,value); }
	}
	if (positional.size() != (seen.contains("--input") ? 2 : 3)) { return fail(2,QT_TRANSLATE_NOOP("LevelComponentSelectionCli","Supply exactly one input map, positionally or with --input.")); }
	const auto path = seen.contains("--input") ? values["--input"] : positional[2];
	LevelComponentSelectionRequest request; const auto operation = values.value("--operation");
	if (operation == "convert") { request.operation = LevelComponentSelectionOperation::Convert; }
	else if (operation == "grow") { request.operation = LevelComponentSelectionOperation::Grow; }
	else if (operation == "shrink") { request.operation = LevelComponentSelectionOperation::Shrink; }
	else if (operation == "connected") { request.operation = LevelComponentSelectionOperation::Connected; }
	else { return fail(2,QT_TRANSLATE_NOOP("LevelComponentSelectionCli","--operation requires convert, grow, shrink or connected.")); }
	if (operation == "convert") {
		const QStringList kinds{"vertex","edge","face"}; const int target = kinds.indexOf(values.value("--to"));
		if (target < 0) { return fail(2,QT_TRANSLATE_NOOP("LevelComponentSelectionCli","Conversion requires --to vertex, edge or face.")); }
		request.target = static_cast<LevelBrushComponent>(target);
		const auto coverage = values.value("--coverage","complete");
		if (coverage != "complete" && coverage != "touching") { return fail(2,QT_TRANSLATE_NOOP("LevelComponentSelectionCli","--coverage requires complete or touching.")); }
		request.touching = coverage == "touching";
	} else if (seen.contains("--to") || seen.contains("--coverage")) { return fail(2,QT_TRANSLATE_NOOP("LevelComponentSelectionCli","--to and --coverage apply only to convert.")); }
	if (seen.contains("--steps")) {
		bool valid = false; request.steps = values["--steps"].toInt(&valid);
		if (!valid || request.steps < 1 || request.steps > 256 || (operation != "grow" && operation != "shrink")) { return fail(2,QT_TRANSLATE_NOOP("LevelComponentSelectionCli","--steps requires 1–256 and applies only to grow or shrink.")); }
	}
	if (selections.isEmpty() || selections.size() > 256) { return fail(2,QT_TRANSLATE_NOOP("LevelComponentSelectionCli","Repeat --set for 1–256 component sets: brush:0/face/4,5 or patch:0/vertex/0,1.")); }
	static const QRegularExpression pattern(QStringLiteral("^(brush|patch):([0-9]+)/(vertex|edge|face)/([0-9]+(?:,[0-9]+)*)$"));
	QVector<LevelMapComponentSet> sets;
	for (const auto& text : selections) {
		if (text.size() > 180224) { return fail(2,QT_TRANSLATE_NOOP("LevelComponentSelectionCli","Too many component IDs in --set.")); }
		const auto match = pattern.match(text); if (!match.hasMatch()) { return fail(2,QT_TRANSLATE_NOOP("LevelComponentSelectionCli","Invalid --set. Use object:id/kind/component,component with non-negative IDs.")); }
		bool valid = false; const int id = match.captured(2).toInt(&valid); if (!valid) { return fail(2,QT_TRANSLATE_NOOP("LevelComponentSelectionCli","Object ID is out of range.")); }
		LevelMapComponentSet set; set.object = {match.captured(1) == "brush" ? LevelMapSelectionKind::QuakeBrush : LevelMapSelectionKind::QuakePatch,id};
		set.kind = match.captured(3) == "vertex" ? LevelBrushComponent::Vertex : match.captured(3) == "edge" ? LevelBrushComponent::Edge : LevelBrushComponent::Face;
		for (const auto& index : match.captured(4).split(',')) { const int component = index.toInt(&valid); if (!valid) { return fail(2,QT_TRANSLATE_NOOP("LevelComponentSelectionCli","Component ID is out of range.")); } set.components << component; }
		sets << set;
	}
	LevelMapDocument document; QString error;
	if (!loadLevelMap({path,values.value("--map-name"),values.value("--engine")},&document,&error)) { return {3,error,{}, {}}; }
	QVector<LevelMapComponentSet> selected;
	if (!selectLevelMapComponents(document,sets,request,&selected,&error)) { return {4,error,{}, {}}; }
	QJsonArray output, selectors; int componentCount = 0; QStringList lines;
	for (const auto& set : selected) {
		const auto object = QStringLiteral("%1:%2").arg(set.object.kind == LevelMapSelectionKind::QuakeBrush ? "brush" : "patch").arg(set.object.objectId);
		const auto kind = set.kind == LevelBrushComponent::Vertex ? QStringLiteral("vertex") : set.kind == LevelBrushComponent::Edge ? QStringLiteral("edge") : QStringLiteral("face");
		QJsonArray ids; QStringList textIds;
		for (int id : set.components) { ids << id; textIds << QString::number(id); }
		const auto selector = object+'/'+kind+'/'+textIds.join(','); selectors << selector; lines << selector; componentCount += int(set.components.size());
		output << QJsonObject{{"object",object},{"kind",kind},{"components",ids}};
	}
	QJsonObject payload{{"schemaVersion",1},{"path",document.sourcePath},{"operation",operation},{"componentCount",componentCount},{"objectCount",int(selected.size())},{"sets",output},{"selectors",selectors}};
	if (operation == "convert") { payload.insert("to",values["--to"]); payload.insert("coverage",request.touching ? "touching" : "complete"); }
	if (operation == "grow" || operation == "shrink") { payload.insert("steps",request.steps); }
	if (lines.isEmpty()) { lines << QCoreApplication::translate("LevelComponentSelectionCli","No components selected."); }
	return {0,{},payload,lines};
}
}
