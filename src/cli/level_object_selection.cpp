#include "cli/level_object_selection.h"
#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_material_paint.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>

namespace vibestudio::cli {
namespace { struct Text { Q_DECLARE_TR_FUNCTIONS(LevelObjectSelectionCli) }; }
LevelObjectSelectionCliResult runLevelObjectSelection(const QStringList& arguments)
{
	LevelObjectSelectionCliResult result; result.payload.insert("mapWritten",false); result.payload.insert("queryOnly",true);
	const auto fail = [&](int code, const QString& message) { result.exitCode = code; result.error = message; return result; };
	const QSet<QString> flags{"--cli","--json","--quiet","--verbose","--within-selection"};
	const QSet<QString> options{"--object","--line","--exclude","--context","--engine","--settings-file","--locale","--catalog-root","--mode","--material","--face"};
	const QSet<QString> repeated{"--object","--line","--exclude","--face"};
	QSet<QString> seen; QHash<QString,QStringList> values; QStringList positional;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const auto arg = arguments[i]; if (!arg.startsWith('-')) { positional.append(arg); continue; }
		const auto equal = arg.indexOf('='); const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key) && !repeated.contains(key)) { return fail(2,Text::tr("Repeated option: %1.").arg(key)); }
		seen.insert(key);
		if (flags.contains(key) && equal < 0) { continue; }
		if (!options.contains(key)) { return fail(2,Text::tr("Unexpected option: %1.").arg(arg)); }
		QString value;
		if (equal >= 0) { value = arg.mid(equal+1); }
		else if (i+1 < arguments.size() && !arguments[i+1].startsWith('-')) { value = arguments[++i]; }
		if (value.trimmed().isEmpty()) { return fail(2,Text::tr("Missing value for %1.").arg(key)); }
		values[key].append(value);
		if (values[key].size() > 16384) { return fail(2,Text::tr("At most 16,384 values are allowed for %1.").arg(key)); }
	}
	if (positional.size() != 3 || !QStringList{"map","maps","level"}.contains(positional[0])
		|| !QStringList{"select-siblings","select-lines","select-connected","select-type","select-textured"}.contains(positional[1])) {
		return fail(2,Text::tr("Expected map select-siblings, select-connected, select-type, select-textured or select-lines followed by a map and selection options."));
	}
	const bool lines = positional[1] == QLatin1String("select-lines");
	const bool connected = positional[1] == QLatin1String("select-connected");
	const bool byType = positional[1] == QLatin1String("select-type");
	const bool textured = positional[1] == QLatin1String("select-textured");
	if ((lines && (!values.contains("--line") || values.contains("--object")))
		|| (!lines && (values.contains("--line") || (!byType && !textured && !values.contains("--object"))))) {
		return fail(2,Text::tr("Select Siblings and Select Connected require --object; only Select Lines accepts --line and requires it."));
	}
	if ((!byType && values.contains("--mode")) || (!byType && !textured && values.contains("--material"))) {
		return fail(2,Text::tr("Only Select Type accepts --mode; only Select Type and Select Textured accept --material."));
	}
	if ((!textured && values.contains("--face")) || (textured && !values.contains("--material"))) {
		return fail(2,Text::tr("Select Textured requires --material and is the only query accepting existing --face picks."));
	}
	const bool withinSelection = seen.contains("--within-selection");
	if (withinSelection && (!textured || !values.contains("--object"))) {
		return fail(2,Text::tr("Only Select Textured accepts --within-selection, and it needs the --object objects to search."));
	}
	LevelPlacementRequest request; request.operation = textured ? LevelPlacementOperation::SelectTextured : LevelPlacementOperation::SelectObjects;
	request.objectSelection.mode = byType ? LevelObjectSelectionMode::RadiantType : connected ? LevelObjectSelectionMode::ConnectedEntities
		: lines ? LevelObjectSelectionMode::SourceLines : LevelObjectSelectionMode::Siblings;
	const auto typeMode = values.value("--mode").value(0,QStringLiteral("objects"));
	if (byType) {
		if (typeMode == "objects") { request.objectSelection.typeScope = LevelTypeSelectionScope::Objects; }
		else if (typeMode == "faces") { request.objectSelection.typeScope = LevelTypeSelectionScope::Faces; }
		else if (typeMode == "vertices") { request.objectSelection.typeScope = LevelTypeSelectionScope::Vertices; }
		else if (typeMode == "edges") { request.objectSelection.typeScope = LevelTypeSelectionScope::Edges; }
		else { return fail(2,Text::tr("Select Type mode must be objects, faces, vertices or edges.")); }
		if (values.contains("--material")) { request.objectSelection.material = values.value("--material").first(); }
	}
	QString error;
	if (textured) {
		request.texturedSelection.material = values.value("--material").first();
		request.texturedSelection.withinSelection = withinSelection;
		for (const auto& selector : values.value("--face")) {
			LevelMaterialTarget face;
			if (!parseLevelMaterialTarget(selector,&face,&error)) { return fail(2,error); }
			if (face.kind != LevelMaterialKind::BrushFace) { return fail(2,Text::tr("Existing --face picks must use face:brushId:faceNumber with a one-based face number.")); }
			request.texturedSelection.faces.append({face.objectId,face.faceIndex});
		}
	}
	if (lines && !parseLevelMapSourceLines(values.value("--line").join(' '),&request.objectSelection.lines,&error)) { return fail(2,error); }
	LevelMapDocument source;
	if (!loadLevelMap({QFileInfo(positional[2]).absoluteFilePath(),{},values.value("--engine").value(0)},&source,&error)) { return fail(4,error); }
	if (values.contains("--context") && !openLevelSceneGroup(&source,values.value("--context").first(),&error)) { return fail(4,error); }
	QVector<LevelMapSelectionRef> selection; QSet<QString> selected;
	for (const auto& selector : values.value("--object")) {
		if (!selectLevelMapObject(&source,selector,&error)) { return fail(2,error); }
		for (const auto& ref : source.selection) {
			const auto key = levelMapSelectionRefId(ref); if (!selected.contains(key)) { selection.append(ref); selected.insert(key); }
		}
	}
	for (const auto& selector : values.value("--exclude")) {
		if (!selectLevelMapObject(&source,selector,&error)) { return fail(2,error); }
		for (const auto& ref : source.selection) {
			if (textured) { request.texturedSelection.excludedObjects.insert(levelMapSelectionRefId(ref)); }
			else { request.objectSelection.excludedObjects.insert(levelMapSelectionRefId(ref)); }
		}
	}
	if (!setLevelMapSelection(&source,selection,&error)) { return fail(2,error); }
	const auto prepared = prepareLevelPlacement(source,request);
	if (!prepared.succeeded) { return fail(4,prepared.error); }
	QStringList objects;
	for (const auto& ref : textured ? prepared.texturedSelectionReport.selection : prepared.objectSelectionReport.selection) { objects.append(levelMapSelectionRefId(ref)); }
	result.payload.insert("selection",QJsonArray::fromStringList(objects));
	if (textured) {
		const auto& report = prepared.texturedSelectionReport;
		QJsonArray faces, patches, targets; QJsonObject geometry;
		for (const auto& face : report.faces) {
			const auto id = levelMaterialTargetId({LevelMaterialKind::BrushFace,face.brushId,face.faceIndex}); faces.append(id); targets.append(id);
		}
		for (int patch : report.matchedPatches) { const auto id = levelMaterialTargetId({LevelMaterialKind::Patch,patch}); patches.append(id); targets.append(id); }
		for (auto it = report.brushGeometry.cbegin(); it != report.brushGeometry.cend(); ++it) { geometry.insert(QString::number(it.key()),QString::fromLatin1(it.value().toHex())); }
		QStringList before; for (const auto& ref : source.selection) { before.append(levelMapSelectionRefId(ref)); }
		result.payload.insert("originalSelection",QJsonArray::fromStringList(before));
		result.payload.insert("faces",faces); result.payload.insert("matchedPatches",patches); result.payload.insert("surfaceTargets",targets);
		result.payload.insert("brushGeometrySha256",geometry); result.payload.insert("addedFaces",report.addedFaces); result.payload.insert("addedPatches",report.addedPatches);
		result.payload.insert("material",request.texturedSelection.material); result.payload.insert("additive",true);
		result.payload.insert("withinSelection",withinSelection);
		result.payload.insert("parentBrushesAdded",false); result.payload.insert("componentSelectionChanged",false);
	}
	if (connected) {
		QStringList before; for (const auto& ref : source.selection) { before.append(levelMapSelectionRefId(ref)); }
		result.payload.insert("originalSelection",QJsonArray::fromStringList(before));
		result.payload.insert("added",objects.size()-before.size());
		result.payload.insert("connectionKeys",QJsonArray{"target","targetname"});
		result.payload.insert("transitive",false);
	}
	if (byType) {
		const auto& report = prepared.objectSelectionReport;
		QJsonArray sets, selectors, surfaceTargets; int count = 0;
		for (const auto& set : report.components) {
			QJsonArray ids; QStringList textIds;
			for (int id : set.components) {
				ids.append(id); textIds.append(QString::number(id));
				surfaceTargets.append(levelMaterialTargetId({LevelMaterialKind::BrushFace,set.object.objectId,id}));
			}
			const auto object = levelMapSelectionRefId(set.object);
			sets.append(QJsonObject{{"object",object},{"kind","face"},{"components",ids}});
			selectors.append(object+"/face/"+textIds.join(',')); count += static_cast<int>(set.components.size());
		}
		result.payload.insert("mode",typeMode); result.payload.insert("matchedClasses",QJsonArray::fromStringList(report.matchedClasses));
		result.payload.insert("materialFallback",report.materialFallback);
		result.payload.insert("componentSelectionChanged",report.componentSelectionChanged);
		result.payload.insert("sets",sets); result.payload.insert("selectors",selectors); result.payload.insert("componentCount",count);
		result.payload.insert("surfaceTargets",surfaceTargets);
		if (request.objectSelection.material) { result.payload.insert("material",*request.objectSelection.material); }
	}
	result.payload.insert("source",source.sourcePath); result.payload.insert("sourceSha256",QString::fromLatin1(source.sourceContentHash.toHex()));
	result.payload.insert("context",source.openSceneGroups.isEmpty() ? QString{} : source.openSceneGroups.last());
	if (lines) {
		QJsonArray numbers; for (int number : request.objectSelection.lines) { numbers.append(number); }
		result.payload.insert("lines",numbers); result.payload.insert("lineBasis","loaded-source");
	}
	result.lines.append(Text::tr("Query only: the source map was not changed."));
	if (textured) {
		result.lines.append(Text::tr("Added %n independent face pick(s).",nullptr,prepared.texturedSelectionReport.addedFaces));
		result.lines.append(Text::tr("Added %n patch object pick(s).",nullptr,prepared.texturedSelectionReport.addedPatches));
		for (const auto& target : result.payload.value("surfaceTargets").toArray()) { result.lines.append(target.toString()); }
	} else if (prepared.objectSelectionReport.componentSelectionChanged) {
		result.lines.append(Text::tr("%n selected face(s)",nullptr,result.payload.value("componentCount").toInt()));
		for (const auto& selector : result.payload.value("selectors").toArray()) { result.lines.append(selector.toString()); }
	} else if (byType && request.objectSelection.typeScope != LevelTypeSelectionScope::Objects) {
		result.lines.append(Text::tr("Vertex and edge modes leave object and component selections unchanged."));
	} else { result.lines.append(Text::tr("%n selected object(s)",nullptr,static_cast<int>(objects.size()))); result.lines.append(objects); }
	return result;
}
} // namespace vibestudio::cli
