#include "core/level_brush.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_transform_test_helpers.h"
#include "tests/level_geometry_test_helpers.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value,const char* label,const QString& detail = {}) { if (!value) { ok = false; std::cerr << label << ": " << detail.toStdString() << '\n'; } return value; }
QByteArray bytes(const LevelMapDocument& map) { return serializeLevelMap(map).bytes; }
bool near(LevelMapVec3 a,LevelMapVec3 b) { return std::abs(a.x-b.x) < .001 && std::abs(a.y-b.y) < .001 && std::abs(a.z-b.z) < .001; }
void coreChecks() {
	QString error;
	for (const auto& dialect : {QStringLiteral("classic"),QStringLiteral("valve220"),QStringLiteral("brushDef"),QStringLiteral("brushDef3")}) {
		LevelMapDocument map; if (!expect(tests::loadTransformFixture(tests::transformFixture(dialect),&map,&error),"fixture",error)) { continue; }
		LevelBrushTopology before; levelBrushTopology(map.brushes[0],&before); const auto edge = before.edges[0];
		const QVector<LevelMapComponentSet> sets{{{LevelMapSelectionKind::QuakeBrush,map.brushes[0].id},LevelBrushComponent::Vertex,{edge[0]}}};
		const auto original = bytes(map); const auto history = map.undoStack.size(); LevelComponentTransformRequest request; request.allowCollapse = true;
		if (!expect(levelMapVertexSnapDelta(map,sets,sets[0].object,edge[1],&request.delta,&error),"snap preparation",error)) { continue; }
		if (!expect(transformLevelMapComponents(&map,sets,request,nullptr,&error),"corner merge",dialect+error)) { continue; }
		LevelBrushTopology after; levelBrushTopology(map.brushes[0],&after);
		expect(after.vertices.size() == before.vertices.size()-1 && std::any_of(after.vertices.cbegin(),after.vertices.cend(),[&](auto p) { return near(p,before.vertices[edge[1]]); }),"target retained and source merged",dialect);
		const auto changed = bytes(map); expect(map.undoStack.size() == history+1 && undoLevelMapEdit(&map) && bytes(map) == original && redoLevelMapEdit(&map) && bytes(map) == changed,"one exact undo/redo",dialect);
	}
	LevelMapDocument map; tests::createGeometryFixture(2,&map,&error);
	map.brushes[1] = map.brushes[0]; map.brushes[1].id = 1;
	LevelBrushTopology topology; levelBrushTopology(map.brushes[0],&topology); const auto edge = topology.edges[0];
	QVector<LevelMapComponentSet> sets{{{LevelMapSelectionKind::QuakeBrush,0},LevelBrushComponent::Vertex,{edge[0]}},{{LevelMapSelectionKind::QuakeBrush,1},LevelBrushComponent::Vertex,{edge[0]}}};
	LevelComponentTransformRequest request; request.allowCollapse = true;
	expect(levelMapVertexSnapDelta(map,sets,sets[0].object,edge[1],&request.delta,&error),"coincident source clump",error);
	auto moved = map; LevelMapComponentReport report;
	expect(transformLevelMapComponents(&moved,sets,request,&report,&error) && report.brushes == 2 && report.collapsedVertices == 2 && moved.undoStack.size() == map.undoStack.size()+1,"coincident batch merges atomically",error);
	LevelMapVec3 delta{123,456,789,true}; const auto sentinel = delta;
	auto separated = sets; separated[1].components = {edge[1]};
	expect(!levelMapVertexSnapDelta(map,separated,sets[0].object,edge[1],&delta,&error) && near(delta,sentinel),"different source positions refused without output");
	auto invalid = sets; invalid[0].kind = LevelBrushComponent::Edge;
	expect(!levelMapVertexSnapDelta(map,invalid,sets[0].object,edge[1],&delta,&error),"edge source refused");
	expect(!levelMapVertexSnapDelta(map,sets,{LevelMapSelectionKind::QuakePatch,0},0,&delta,&error)
		&& !levelMapVertexSnapDelta(map,sets,{LevelMapSelectionKind::QuakeBrush,99},0,&delta,&error)
		&& !levelMapVertexSnapDelta(map,sets,sets[0].object,999,&delta,&error),"invalid target references refused");
	auto noop = map; expect(levelMapVertexSnapDelta(noop,sets,sets[0].object,edge[0],&request.delta,&error) && near(request.delta,{0,0,0,true}),"same target yields zero displacement");
	expect(transformLevelMapComponents(&noop,sets,request,nullptr,&error) && bytes(noop) == bytes(map) && noop.undoStack.size() == map.undoStack.size(),"same target preserves history");
	QString group; expect(createLevelSceneNode(&map,LevelSceneNodeKind::Group,"locked",{},&group,&error)
		&& assignLevelSceneObjects(&map,group,{"brush:1"},&error) && setLevelSceneLocked(&map,group,true,&error),"lock one shared source",error);
	const auto lockedBytes = bytes(map); const auto lockedHistory = map.undoStack.size();
	expect(levelMapVertexSnapDelta(map,sets,sets[0].object,edge[1],&request.delta,&error),"read-only target preparation with locks");
	expect(!transformLevelMapComponents(&map,sets,request,nullptr,&error) && bytes(map) == lockedBytes && map.undoStack.size() == lockedHistory,"one locked member refuses whole snap",error);
	// Full valid world width requires a displacement greater than 32768.
	LevelMapDocument wide; tests::createGeometryFixture(1,&wide,&error);
	for (auto& face : wide.brushes[0].faces) { for (auto* p : {&face.p0,&face.p1,&face.p2}) { p->x *= 2000; } }
	wide.brushes[0].mins.x = -32000; wide.brushes[0].maxs.x = 32000; wide.brushes[0].geometryDirty = true;
	expect(levelBrushTopology(wide.brushes[0],&topology,&error),"wide valid brush",error);
	bool testedWideSnap = false;
	for (const auto& e : topology.edges) {
		if (std::abs(topology.vertices[e[1]].x-topology.vertices[e[0]].x) < 60000) { continue; }
		testedWideSnap = true;
		const QVector<LevelMapComponentSet> source{{{LevelMapSelectionKind::QuakeBrush,0},LevelBrushComponent::Vertex,{e[0]}}};
		expect(levelMapVertexSnapDelta(wide,source,source[0].object,e[1],&request.delta,&error) && std::abs(request.delta.x) == 64000
			&& transformLevelMapComponents(&wide,source,request,nullptr,&error),"snap across full valid world range",error); break;
	}
	expect(testedWideSnap,"wide snap fixture exercises displacement over 32768");
}
void cliChecks(const QString& binary,const QString& directory) {
	QString error; LevelMapDocument source; tests::createGeometryFixture(1,&source,&error);
	const QString input = directory+"/source.map",output = directory+"/snapped.map";
	expect(saveLevelMapAs(source,input).succeeded(),"CLI source save"); LevelBrushTopology topology; levelBrushTopology(source.brushes[0],&topology); const auto edge = topology.edges[0];
	const QStringList args{input,"--set",QStringLiteral("brush:0/vertex/%1").arg(edge[0]),"--operation","snap","--target",QStringLiteral("brush:0/vertex/%1").arg(edge[1]),"--output",output};
	const auto run = [&](const QStringList& arguments,bool success = true) {
		QProcess process; process.setWorkingDirectory(directory); process.start(binary,QStringList{"--cli","--json","--settings-file",directory+"/settings.ini","map","transform-components"}+arguments);
		const bool finished = process.waitForStarted() && process.waitForFinished(30000); const auto out = process.readAllStandardOutput();
		expect(finished && process.exitStatus() == QProcess::NormalExit && (process.exitCode() == 0) == success,"CLI snap result",QString::fromUtf8(out+process.readAllStandardError()));
		if (!finished) { process.kill(); process.waitForFinished(); } return QJsonDocument::fromJson(out).object();
	};
	auto report = run(args+QStringList{"--dry-run"}); expect(!QFile::exists(output) && report["collapsedVertices"].toInt() == 1 && report["target"].toString().contains("vertex/"),"CLI snap dry run reports merge");
	run(args); LevelMapDocument loaded; expect(loadLevelMap({output,{},"idtech3"},&loaded,&error),"load snapped map",error);
	LevelBrushTopology after; levelBrushTopology(loaded.brushes[0],&after); expect(after.vertices.size() == 7,"CLI persists merged hull");
	const auto original = bytes(loaded); run(args,false);
	for (const auto& extra : QVector<QStringList>{{"--grid","0"},{"--pivot","0,0,0"},{"--delta","0,0,1"},{"--target","brush:0/vertex/2"},{"--set","brush:99/vertex/0"}}) { run(args+QStringList{"--overwrite"}+extra,false); }
	expect(loadLevelMap({output,{},"idtech3"},&loaded,&error) && bytes(loaded) == original,"refusals preserve output");
}
}
int main(int argc,char** argv) { QCoreApplication app(argc,argv); QTemporaryDir temp; if (argc != 2 || !temp.isValid()) { return 1; } coreChecks(); cliChecks(QString::fromLocal8Bit(argv[1]),temp.path()); return ok ? 0 : 1; }
