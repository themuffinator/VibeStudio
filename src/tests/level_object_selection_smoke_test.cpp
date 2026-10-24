#include "core/level_placement.h"
#include "core/level_document.h"
#include "core/level_patch.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/level_surface_test_helpers.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* label, const QString& detail = {}) {
	if (!value) { ok = false; std::cerr << "FAIL: " << label << ": " << detail.toStdString() << '\n'; } return value;
}
LevelMapSelectionRef brush(int id) { return {LevelMapSelectionKind::QuakeBrush,id}; }
QSet<QString> keys(const QVector<LevelMapSelectionRef>& refs) {
	QSet<QString> result; for (const auto& ref : refs) { result.insert(levelMapSelectionRefId(ref)); } return result;
}
LevelMapDocument fixture() {
	LevelMapDocument map; QString error; tests::createGeometryFixture(6,&map,&error);
	setLevelMapSelection(&map,{brush(4),brush(5)});
	expect(tieLevelMapSelectionToEntity(&map,"func_group",{},nullptr,&error),"create brush entity",error);
	expect(addLevelMapEntity(&map,"light",{0,0,96,true},{},nullptr,&error),"create point entity",error);
	map.format = LevelMapFormat::Quake3Map;
	LevelMapPatch patch; expect(createLevelPatch({},&patch,&error) && addLevelMapPatch(&map,patch,nullptr,&error),"create patch",error);
	const auto saved = serializeLevelMap(map);
	expect(saved.succeeded() && loadLevelMapBytes({"selection.map",{},"quake3"},saved.bytes,&map,&error),"reload fixture",error);
	return map;
}
bool query(LevelMapDocument* map, LevelObjectSelectionMode mode, const QVector<int>& lines = {}, const QSet<QString>& excluded = {}) {
	LevelObjectSelectionRequest request; request.mode = mode; request.lines = lines; request.excludedObjects = excluded; request.undoSelection = true;
	QString error; const auto bytes = serializeLevelMap(*map).bytes; const auto revision = map->revision;
	LevelObjectSelectionReport report;
	const bool result = selectLevelMapObjects(map,request,&report,&error);
	expect(result,"selection query",error);
	expect(serializeLevelMap(*map).bytes == bytes && map->revision == revision,"query preserves geometry and native bytes");
	expect(!result || report.selection == map->selection,"report matches published selection"); return result;
}
void siblings() {
	auto map = fixture(); const auto baseline = map;
	setLevelMapSelection(&map,{brush(0)}); query(&map,LevelObjectSelectionMode::Siblings);
	const QSet<QString> all{"brush:0","brush:1","brush:2","brush:3","brush:4","brush:5","patch:0","entity:2"};
	expect(keys(map.selection) == all,"world siblings descend brush entities and include point entities and patches");
	expect(map.undoStack.size() == 1 && map.editState == "saved" && undoLevelMapEdit(&map)
		&& map.selection == QVector<LevelMapSelectionRef>{brush(0)} && redoLevelMapEdit(&map),"selection history does not dirty saved map");
	const auto history = map.historyRevision; query(&map,LevelObjectSelectionMode::Siblings);
	expect(map.historyRevision == history,"identical sibling result does not create undo step");
	map = baseline; setLevelMapSelection(&map,{brush(4)}); query(&map,LevelObjectSelectionMode::Siblings);
	expect(keys(map.selection) == QSet<QString>{"brush:4","brush:5"},"owned brush siblings stay in owner");
	setLevelMapSelection(&map,{{LevelMapSelectionKind::Entity,1}}); query(&map,LevelObjectSelectionMode::Siblings);
	expect(keys(map.selection) == QSet<QString>{"brush:4","brush:5"},"whole brush-entity wrapper expands to native child siblings");
	setLevelMapSelection(&map,{brush(0),brush(4)}); query(&map,LevelObjectSelectionMode::Siblings);
	expect(keys(map.selection) == all && map.selection.size() == all.size(),"multiple parents deduplicate overlapping results");
	setLevelMapSelection(&map,{brush(0)}); query(&map,LevelObjectSelectionMode::Siblings,{}, {"entity:1","patch:0"});
	expect(keys(map.selection) == QSet<QString>{"brush:0","brush:1","brush:2","brush:3","entity:2"},"filters inherit from brush entity");
}
void groups() {
	auto map = fixture(); QString group, inner, layer, error;
	createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Outer",{},&group,&error);
	createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Inner",group,&inner,&error);
	createLevelSceneNode(&map,LevelSceneNodeKind::Layer,"Layer",{},&layer,&error);
	assignLevelSceneObjects(&map,group,{"brush:0","brush:1"},&error);
	assignLevelSceneObjects(&map,inner,{"brush:2"},&error);
	assignLevelSceneObjects(&map,layer,{"brush:3"},&error);
	const auto baseline = map;
	setLevelMapSelection(&map,{brush(0)}); query(&map,LevelObjectSelectionMode::Siblings);
	expect(keys(map.selection) == QSet<QString>{"brush:0","brush:1","brush:2","brush:4","brush:5","patch:0","entity:2"},"closed group siblings use group parent and exclude other layer");
	setLevelMapSelection(&map,{brush(3)}); query(&map,LevelObjectSelectionMode::Siblings);
	expect(map.selection == QVector<LevelMapSelectionRef>{brush(3)},"layer boundaries");
	openLevelSceneGroup(&map,group,&error); setLevelMapSelection(&map,{brush(0)}); query(&map,LevelObjectSelectionMode::Siblings);
	expect(keys(map.selection) == QSet<QString>{"brush:0","brush:1","brush:2"},"open group exposes direct children and nested group");
	openLevelSceneGroup(&map,inner,&error); setLevelMapSelection(&map,{brush(2)}); query(&map,LevelObjectSelectionMode::Siblings);
	expect(map.selection == QVector<LevelMapSelectionRef>{brush(2)},"nested open context");
	query(&map,LevelObjectSelectionMode::SourceLines,{map.brushes[0].startLine}); expect(map.selection.isEmpty(),"source lines cannot escape open context");
	map = baseline; query(&map,LevelObjectSelectionMode::SourceLines,{map.brushes[0].startLine});
	expect(keys(map.selection) == QSet<QString>{"brush:0","brush:1","brush:2"},"source lines select complete closed group");
	query(&map,LevelObjectSelectionMode::SourceLines,{map.brushes[0].startLine},{"brush:2"}); expect(map.selection.isEmpty(),"filtered child excludes whole closed group");
	map = baseline; setLevelSceneLocked(&map,inner,true,&error); query(&map,LevelObjectSelectionMode::SourceLines,{map.brushes[0].startLine});
	expect(map.selection.isEmpty(),"locked descendant cannot enter selection via outer group");
	map = baseline; setLevelSceneVisible(&map,inner,false,&error); query(&map,LevelObjectSelectionMode::SourceLines,{map.brushes[0].startLine});
	expect(map.selection.isEmpty(),"hidden descendant cannot enter selection via outer group");
	map = fixture(); createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Owned",{},&group,&error);
	assignLevelSceneObjects(&map,group,{"entity:1","brush:0"},&error);
	query(&map,LevelObjectSelectionMode::SourceLines,{map.brushes[4].startLine});
	expect(keys(map.selection) == QSet<QString>{"entity:1","brush:0"},"closed groups retain complete owner representation without child duplicates");
	openLevelSceneGroup(&map,group,&error); setLevelMapSelection(&map,{brush(0)}); query(&map,LevelObjectSelectionMode::Siblings);
	expect(keys(map.selection) == QSet<QString>{"brush:0","brush:4","brush:5"},"open group siblings descend owned geometry");
	closeLevelSceneGroup(&map,true,&error);
	createLevelSceneNode(&map,LevelSceneNodeKind::Layer,"Independent child",{},&layer,&error);
	assignLevelSceneObjects(&map,layer,{"brush:4"},&error);
	query(&map,LevelObjectSelectionMode::SourceLines,{map.brushes[0].startLine});
	expect(map.selection.isEmpty(),"closed group cannot select owner geometry assigned outside the group");
	setLevelSceneLocked(&map,layer,true,&error);
	query(&map,LevelObjectSelectionMode::SourceLines,{map.entities[1].startLine});
	expect(map.selection.isEmpty(),"closed owner cannot bypass independently locked child");
	map = fixture(); createLevelSceneNode(&map,LevelSceneNodeKind::Layer,"Child layer",{},&layer,&error);
	assignLevelSceneObjects(&map,layer,{"brush:4"},&error);
	setLevelMapSelection(&map,{brush(4)}); query(&map,LevelObjectSelectionMode::Siblings);
	expect(map.selection == QVector<LevelMapSelectionRef>{brush(4)},"owned sibling query respects independently assigned layer");
	setLevelMapSelection(&map,{brush(0)}); query(&map,LevelObjectSelectionMode::Siblings);
	expect(!map.selection.contains(brush(4)) && map.selection.contains(brush(5)),"world sibling query traverses only children in its layer");
}
void sourceLines() {
	auto map = fixture(); const auto baseline = map;
	for (int line : {map.brushes[0].startLine,map.brushes[0].faces.first().line,map.brushes[0].endLine}) {
		query(&map,LevelObjectSelectionMode::SourceLines,{line}); expect(map.selection == QVector<LevelMapSelectionRef>{brush(0)},"brush source span includes both braces and faces");
	}
	query(&map,LevelObjectSelectionMode::SourceLines,{map.entities[0].startLine}); expect(map.selection.isEmpty(),"world header is not a selectable node");
	query(&map,LevelObjectSelectionMode::SourceLines,{map.entities[1].startLine});
	expect(keys(map.selection) == QSet<QString>{"brush:4","brush:5"},"entity header selects all children");
	query(&map,LevelObjectSelectionMode::SourceLines,{map.entities[1].startLine,map.brushes[4].startLine});
	expect(map.selection == QVector<LevelMapSelectionRef>{brush(4)},"matching child suppresses owner fallback even with header line");
	query(&map,LevelObjectSelectionMode::SourceLines,{map.brushes[4].startLine},{"brush:4"});
	expect(map.selection == QVector<LevelMapSelectionRef>{brush(5)},"native entity fallback when matching child is excluded");
	query(&map,LevelObjectSelectionMode::SourceLines,{map.entities[2].endLine,map.patches[0].startLine,map.brushes[0].startLine});
	expect(keys(map.selection) == QSet<QString>{"entity:2","patch:0","brush:0"},"multi-line point entity, patch and brush selection");
	query(&map,LevelObjectSelectionMode::SourceLines,{2147483647}); expect(map.selection.isEmpty(),"unmatched valid positions clear selection");
	map = baseline; setLevelMapSelection(&map,{brush(0)}); duplicateLevelMapSelection(&map,128,0,0);
	const auto copied = map.selection;
	query(&map,LevelObjectSelectionMode::SourceLines,{baseline.brushes[0].startLine});
	expect(map.selection == QVector<LevelMapSelectionRef>{brush(0)} && map.selection != copied,"new copies do not inherit source positions");
	LevelMapSaveReport saved; saved.written = true; saved.outputPath = "saved.map"; saved.contentHash = "synthetic";
	adoptLevelDocumentSave(&map,saved); query(&map,LevelObjectSelectionMode::SourceLines,{baseline.brushes[0].startLine});
	expect(map.selection == QVector<LevelMapSelectionRef>{brush(0)},"save adoption keeps loaded-source coordinates");
	deleteLevelMapSelection(&map); query(&map,LevelObjectSelectionMode::SourceLines,{baseline.brushes[0].startLine});
	expect(map.selection.isEmpty(),"deleted source object does not select a surviving copy");
	expect(undoLevelMapEdit(&map),"undo source-object deletion"); query(&map,LevelObjectSelectionMode::SourceLines,{baseline.brushes[0].startLine});
	expect(map.selection == QVector<LevelMapSelectionRef>{brush(0)},"undo restores source identity");
	for (const auto& dialect : {"classic","valve220","brushDef","brushDef3"}) {
		expect(loadLevelMapBytes({"dialect.map",{},"quake3"},tests::surfaceFixture(QLatin1String(dialect)),&map),"load dialect");
		query(&map,LevelObjectSelectionMode::SourceLines,{map.brushes[0].endLine}); expect(map.selection == QVector<LevelMapSelectionRef>{brush(0)},"dialect and CRLF source positions");
	}
}
void validationAndCancellation() {
	QVector<int> parsed{7}; QString error;
	expect(parseLevelMapSourceLines(" 15, 2\t15\n3 ",&parsed,&error) && parsed == QVector<int>{2,3,15},"parse, sort and deduplicate positions");
	expect(parseLevelMapSourceLines(QString::fromUtf8("١٢ ۴۵ ７８"),&parsed,&error) && parsed == QVector<int>{12,45,78},"localised decimal digits retain deterministic source positions");
	for (const auto& text : {"",", ,","0","-1","+1","1;2","1e2","2.5","2147483648","1 nope"}) {
		const auto before = parsed; expect(!parseLevelMapSourceLines(QLatin1String(text),&parsed,&error) && parsed == before && !error.isEmpty(),"invalid source input is atomic",QLatin1String(text));
	}
	expect(!parseLevelMapSourceLines(QString("1 ").repeated(16385),&parsed,&error),"position count limit");
	auto map = fixture(); setLevelMapSelection(&map,{brush(0)}); const auto baseline = map;
	LevelObjectSelectionRequest request; request.excludedObjects = {"brush:0"}; LevelObjectSelectionReport report; report.selection = {brush(99)};
	expect(!selectLevelMapObjects(&map,request,&report,&error) && map.selection == baseline.selection && report.selection == QVector<LevelMapSelectionRef>{brush(99)}
		&& map.historyRevision == baseline.historyRevision,"invalid seed leaves state and report unchanged");
	request = {}; map.scene.opaqueMetadata = "future-version";
	expect(!selectLevelMapObjects(&map,request,&report,&error),"unrecognised scene metadata cannot be ignored");
	map = baseline; request.mode = static_cast<LevelObjectSelectionMode>(99);
	expect(!selectLevelMapObjects(&map,request,&report,&error),"invalid request mode");
	map.format = LevelMapFormat::DoomWad; request = {};
	expect(!selectLevelMapObjects(&map,request,&report,&error),"unsupported binary map selection is explicit");
	map = baseline; LevelPlacementRequest worker; worker.operation = LevelPlacementOperation::SelectObjects; worker.objectSelection.undoSelection = true;
	for (const auto phase : {LevelPlacementPhase::SelectingObjects,LevelPlacementPhase::Finalizing,LevelPlacementPhase::Complete}) {
		bool cancel = false; LevelPlacementControl control; control.isCancelled = [&] { return cancel; };
		control.progress = [&](const auto& progress) { cancel |= progress.phase == phase; };
		const auto result = prepareLevelPlacement(map,worker,control);
		expect(result.cancelled && !result.succeeded && result.objectSelectionReport.selection.isEmpty() && map.selection == baseline.selection
			&& map.historyRevision == baseline.historyRevision,"worker cancellation discards selection and report at every publication boundary");
	}
	tests::createGeometryFixture(16000,&map,&error); setLevelMapSelection(&map,{brush(0)});
	QElapsedTimer timer; timer.start(); const auto result = prepareLevelPlacement(map,worker);
	expect(result.succeeded && result.document.selection.size() == 16000 && result.document.undoStack.size() == 1,"dense hierarchy selects all peers in one step",result.error);
	std::cout << "16,000-brush siblings: " << timer.elapsed() << " ms\n";
}
}
int main(int argc, char** argv) {
	QCoreApplication app(argc,argv); siblings(); groups(); sourceLines(); validationAndCancellation(); return ok ? 0 : 1;
}
