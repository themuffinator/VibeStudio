#include "cli/level_visibility.h"
#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/editor_profiles.h"
#include "core/level_filter_tags.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <cmath>

namespace vibestudio::cli {
namespace { struct Text { Q_DECLARE_TR_FUNCTIONS(LevelVisibilityCli) }; }
LevelVisibilityCliResult runLevelVisibility(const QStringList& arguments)
{
	LevelVisibilityCliResult result; result.payload.insert("mapWritten",false); result.payload.insert("queryOnly",true);
	const auto fail = [&](int code, const QString& message) { result.exitCode = code; result.error = message; return result; };
	const QSet<QString> flags{"--cli","--json","--quiet","--verbose"};
	const QSet<QString> options{"--object","--hidden","--shown","--class","--policy","--profile","--mode","--node","--parent","--offset","--context","--engine","--map","--settings-file","--locale","--catalog-root"};
	QSet<QString> seen; QHash<QString,QStringList> values; QStringList positional;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const auto arg = arguments[i]; if (!arg.startsWith('-')) { positional.append(arg); continue; }
		const auto equal = arg.indexOf('='); const auto name = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(name) && name != "--object" && name != "--hidden" && name != "--shown" && name != "--class") { return fail(2,Text::tr("Repeated option: %1.").arg(name)); }
		seen.insert(name);
		if (flags.contains(name) && equal < 0) { continue; }
		if (!options.contains(name)) { return fail(2,Text::tr("Unexpected option: %1.").arg(arg)); }
		QString value;
		if (equal >= 0) { value = arg.mid(equal+1); }
		else if (i+1 < arguments.size() && (!arguments[i+1].startsWith('-') || (name == "--offset" && !arguments[i+1].startsWith("--")))) { value = arguments[++i]; }
		if (value.trimmed().isEmpty()) { return fail(2,Text::tr("Missing value for %1.").arg(name)); }
		values[name].append(value);
		if (values[name].size() > 16384) { return fail(2,Text::tr("At most 16,384 values are allowed for %1.").arg(name)); }
	}
	if (positional.size() != 3 || !QStringList{"map","maps","level"}.contains(positional[0]) || positional[1] != "visibility") {
		return fail(2,Text::tr("Expected map visibility <map> --mode inspect|hide|show-all|isolate|move|assign|duplicate, with session-state selectors."));
	}
	LevelPlacementRequest request; request.operation = LevelPlacementOperation::Visibility;
	EditorProfileDescriptor profile;
	if (!editorProfileForId(values.value("--profile").value(0,defaultEditorProfileId()),&profile)) {
		return fail(2,Text::tr("Choose a registered editor profile for --profile."));
	}
	const auto policy = values.value("--policy").value(0,profile.controls.inheritGroupVisibility ? QStringLiteral("inherited") : QStringLiteral("flat"));
	if (policy != "flat" && policy != "inherited") { return fail(2,Text::tr("Choose --policy flat or inherited.")); }
	request.visibility.inheritGroups = policy == "inherited";
	const auto mode = values.value("--mode").value(0);
	if (mode == "hide") { request.visibility.operation = LevelVisibilityOperation::HideSelected; }
	else if (mode == "show-all") { request.visibility.operation = LevelVisibilityOperation::ShowAll; }
	else if (mode == "isolate") { request.visibility.operation = LevelVisibilityOperation::Isolate; }
	else if (mode != "inspect" && mode != "move" && mode != "assign" && mode != "duplicate") {
		return fail(2,Text::tr("Choose --mode inspect, hide, show-all, isolate, move, assign or duplicate."));
	}
	const bool objectsRequired = mode == "hide" || mode == "isolate" || mode == "assign" || mode == "duplicate";
	if (objectsRequired != values.contains("--object")) { return fail(2,Text::tr("Hide, Isolate, Assign and Duplicate require --object; other modes do not accept it.")); }
	if ((mode == "move") != values.contains("--node") || (values.contains("--parent") && mode != "move" && mode != "assign")
		|| (values.contains("--offset") && mode != "duplicate")) {
		return fail(2,Text::tr("Move requires --node. Only Move and Assign accept --parent; only Duplicate accepts --offset."));
	}
	if (values.contains("--offset")) {
		const auto parts = values.value("--offset").first().split(',');
		if (parts.size() != 3) { return fail(2,Text::tr("Choose three finite numbers for --offset x,y,z.")); }
		double* axes[]{&request.offset.x,&request.offset.y,&request.offset.z};
		for (int i = 0; i < 3; ++i) {
			bool ok = false; *axes[i] = parts[i].toDouble(&ok);
			if (!ok || !std::isfinite(*axes[i])) { return fail(2,Text::tr("Choose three finite numbers for --offset x,y,z.")); }
		}
	}
	LevelMapDocument source; QString error;
	if (!loadLevelMap({QFileInfo(positional[2]).absoluteFilePath(),values.value("--map").value(0),values.value("--engine").value(0)},&source,&error)) { return fail(4,error); }
	source.insertionVisibility = profile.controls.insertionVisibility;
	if (values.contains("--context") && !openLevelSceneGroup(&source,values.value("--context").first(),&error)) { return fail(4,error); }
	const auto availableList = levelSceneObjects(source); const QSet<QString> available(availableList.cbegin(),availableList.cend());
	QVector<LevelMapSelectionRef> selection;
	const auto targets = levelMapVisibilityTargets(source);
	for (const auto& name : {QStringLiteral("--object"),QStringLiteral("--hidden"),QStringLiteral("--shown")}) {
		for (const auto& selector : values.value(name)) {
			if (name != "--object" && selector.startsWith("group:")) {
				if (!targets.contains(selector)) { return fail(2,Text::tr("Choose an existing group visibility selector: %1.").arg(selector)); }
				(name == "--hidden" ? source.hiddenObjects : source.shownObjects).insert(selector); continue;
			}
			if (!selectLevelMapObject(&source,selector,&error)) { return fail(2,error); }
			for (const auto& ref : source.selection) {
				const auto key = levelSceneCanonicalObject(source,levelMapSelectionRefId(ref));
				if (!available.contains(key)) { return fail(2,Text::tr("Worldspawn cannot be hidden or isolated.")); }
				if (name == "--hidden") { source.hiddenObjects.insert(key); }
				else if (name == "--shown") { source.shownObjects.insert(key); } else { selection.append(ref); }
			}
		}
	}
	if (!(source.hiddenObjects & source.shownObjects).isEmpty()) { return fail(2,Text::tr("A visibility target cannot be both hidden and shown.")); }
	if (!setLevelMapSelection(&source,selection,&error)) { return fail(2,error); }
	LevelPlacementResult prepared;
	if (mode == "inspect" || mode == "move" || mode == "assign") {
		prepared.document = source; bool succeeded = true;
		auto parent = values.value("--parent").value(0);
		if (parent == "default") { parent.clear(); }
		if (mode == "move") { succeeded = reparentLevelSceneNode(&prepared.document,values.value("--node").first(),parent,&error); }
		if (mode == "assign") {
			QStringList objects; for (const auto& ref : selection) { objects.append(levelMapSelectionRefId(ref)); }
			succeeded = assignLevelSceneObjects(&prepared.document,parent,objects,&error);
		}
		if (!succeeded) { return fail(4,error); }
	} else {
		if (mode == "duplicate") { request.operation = LevelPlacementOperation::Duplicate; }
		prepared = prepareLevelPlacement(source,request);
		if (!prepared.succeeded) { return fail(4,prepared.error); }
	}
	const bool changed = prepared.document.revision != source.revision || prepared.document.hiddenObjects != source.hiddenObjects
		|| prepared.document.shownObjects != source.shownObjects || prepared.document.selection != source.selection;
	if (mode == "move" || mode == "assign" || mode == "duplicate") { result.payload.insert("scene",levelSceneJson(prepared.document)); }
	QStringList hidden(prepared.document.hiddenObjects.cbegin(),prepared.document.hiddenObjects.cend()); hidden.sort();
	const auto effectiveSet = levelMapHiddenObjects(prepared.document);
	QStringList effective(effectiveSet.cbegin(),effectiveSet.cend()); effective.sort();
	QStringList selected; for (const auto& ref : prepared.document.selection) { selected.append(levelMapSelectionRefId(ref)); }
	QStringList shown(prepared.document.shownObjects.cbegin(),prepared.document.shownObjects.cend()); shown.sort();
	result.payload.insert("shown",QJsonArray::fromStringList(shown));
	result.payload.insert("policy",policy);
	result.payload.insert("profile",profile.id);
	result.payload.insert("insertionVisibility",source.insertionVisibility == LevelInsertionVisibility::FreshInherited ? "fresh-inherited" : "preserve-and-reveal");
	result.payload.insert("hidden",QJsonArray::fromStringList(hidden));
	result.payload.insert("effectiveHidden",QJsonArray::fromStringList(effective));
	result.payload.insert("selection",QJsonArray::fromStringList(selected));
	result.payload.insert("changed",changed); result.payload.insert("mode",mode);
	result.payload.insert("source",source.sourcePath); result.payload.insert("sourceSha256",QString::fromLatin1(source.sourceContentHash.toHex()));
	result.payload.insert("context",source.openSceneGroups.isEmpty() ? QString{} : source.openSceneGroups.last());
	// --class switches off view filters for whole entity classes, as the View
	// tab's Entity Classes and the Toggle <class> Visible commands do: every
	// entity of the class, those added later included, apart from session
	// hiding, which they leave unchanged.
	QJsonArray classFilters; QStringList classFiltered; QStringList classNames;
	for (const auto& list : values.value("--class")) {
		for (const auto& part : list.split(',',Qt::SkipEmptyParts)) {
			const auto name = part.trimmed();
			if (!levelFilterTagsApply(prepared.document)) { return fail(2,Text::tr("Class filters are for Quake-family maps.")); }
			if (!levelFilterClassForId(levelFilterClassId(name))) { return fail(2,Text::tr("%1 is not a class name a map can hold.").arg(name)); }
			if (classNames.contains(name,Qt::CaseInsensitive)) { continue; }
			classNames.append(name);
			QJsonArray members;
			for (const auto& ref : levelFilterClassMatches(prepared.document,name)) {
				const auto id = levelMapSelectionRefId(ref);
				members.append(id);
				if (!classFiltered.contains(id)) { classFiltered.append(id); }
			}
			classFilters.append(QJsonObject{{"class",name},{"id",levelFilterClassId(name)},{"count",members.size()},
				{"objects",members},{"command",levelFilterClassCommandId(name)}});
		}
	}
	classFiltered.sort();
	QStringList viewHidden = effective;
	for (const auto& id : std::as_const(classFiltered)) { if (!viewHidden.contains(id)) { viewHidden.append(id); } }
	viewHidden.sort();
	result.payload.insert("classFilters",classFilters);
	result.payload.insert("classFiltered",QJsonArray::fromStringList(classFiltered));
	result.payload.insert("viewHidden",QJsonArray::fromStringList(viewHidden));
	result.lines.append(Text::tr("Session visibility query: no map was written. Scene visibility and filters are independent."));
	result.lines.append(Text::tr("%n temporarily hidden object(s)",nullptr,static_cast<int>(hidden.size()))); result.lines.append(hidden);
	if (!classNames.isEmpty()) {
		const int filteredCount = static_cast<int>(classFiltered.size());
		result.lines.append(Text::tr("Class filters for %1 hide %n entity(ies) from the views as well.",nullptr,filteredCount).arg(classNames.join(QStringLiteral(", "))));
		result.lines.append(classFiltered);
	}
	return result;
}
}
