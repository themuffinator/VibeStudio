#include "core/level_component_selection.h"
#include "core/level_patch.h"
#include "tests/level_geometry_test_helpers.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
namespace {
using Kind = LevelBrushComponent;
using Op = LevelComponentSelectionOperation;
bool ok = true;
bool expect(bool value, const char* label, const QString& detail = {}) {
	if (!value) { ok = false; std::cerr << label << ": " << detail.toStdString() << '\n'; } return value;
}
LevelMapComponentSet brush(Kind kind, QVector<int> ids, int object = 0) { return {{LevelMapSelectionKind::QuakeBrush,object},kind,ids}; }
LevelMapComponentSet patch(QVector<int> ids) { return {{LevelMapSelectionKind::QuakePatch,0},Kind::Vertex,ids}; }
int size(const QVector<LevelMapComponentSet>& sets) { int count = 0; for (const auto& set : sets) { count += int(set.components.size()); } return count; }
void coreChecks(const LevelMapDocument& map) {
	const auto bytes = serializeLevelMap(map).bytes; const auto undo = map.undoStack.size(); QString error;
	const auto query = [&](const QVector<LevelMapComponentSet>& sets, Op operation, Kind target = Kind::Vertex, bool touching = false, int steps = 1) {
		QVector<LevelMapComponentSet> result;
		expect(selectLevelMapComponents(map,sets,{operation,target,touching,steps},&result,&error),"selection query",error); return result;
	};
	auto vertices = query({brush(Kind::Face,{0})},Op::Convert);
	expect(size(vertices) == 4,"cube face has four vertices");
	auto edges = query(vertices,Op::Convert,Kind::Edge);
	expect(size(edges) == 4,"complete perimeter has four edges");
	auto faces = query(edges,Op::Convert,Kind::Face);
	expect(size(faces) == 1 && faces[0].components == QVector<int>{0},"round trip returns the original face");
	edges[0].components.removeLast();
	expect(size(query(edges,Op::Convert)) == 4 && query(edges,Op::Convert,Kind::Face).isEmpty(),"three edges retain all corners but do not completely cover a face");
	for (auto kind : {Kind::Edge,Kind::Face}) {
		expect(query({brush(Kind::Vertex,{0})},Op::Convert,kind).isEmpty(),"one corner is not a complete edge or face");
		expect(size(query({brush(Kind::Vertex,{0})},Op::Convert,kind,true)) == 3,"one cube corner touches three edges and faces");
	}
	auto grown = query({brush(Kind::Vertex,{0})},Op::Grow);
	expect(size(grown) == 4,"vertex growth follows edges without diagonals");
	auto shrunk = query(grown,Op::Shrink);
	expect(size(shrunk) == 1 && shrunk[0].components == QVector<int>{0},"vertex growth erodes back to seed");
	expect(size(query({brush(Kind::Vertex,{0})},Op::Grow,Kind::Vertex,false,2)) == 7,"two edge steps exclude the opposite cube corner");
	auto all = query({brush(Kind::Vertex,{0})},Op::Connected);
	expect(size(all) == 8 && all.size() == 1 && all[0].object.objectId == 0,"connected stays inside its seeded object");
	expect(size(query(all,Op::Shrink,Kind::Vertex,false,256)) == 8,"a fully selected closed solid has no open boundary");
	expect(size(query({brush(Kind::Edge,{0})},Op::Grow)) == 5,"edge growth shares endpoints");
	auto faceRing = query({brush(Kind::Face,{0})},Op::Grow);
	expect(size(faceRing) == 5 && size(query(faceRing,Op::Shrink)) == 1,"face growth crosses boundary edges");
	expect(size(query({brush(Kind::Face,{0})},Op::Grow,Kind::Vertex,false,2)) == 6,"second face ring reaches the opposite face");
	expect(size(query({brush(Kind::Vertex,{0,0}),brush(Kind::Vertex,{0,1})},Op::Convert)) == 2,"repeated same-kind sets merge deterministically");
	for (auto kind : {Kind::Vertex,Kind::Edge,Kind::Face}) {
		const auto converted = query({patch({12})},Op::Convert,kind);
		expect(size(converted) == 1 && converted[0].kind == Kind::Vertex && converted[0].components == QVector<int>{12},"patch control points survive every brush mode");
	}
	auto grid = query({patch({12})},Op::Grow);
	expect(size(grid) == 5 && size(query(grid,Op::Shrink)) == 1,"patch centre uses four grid neighbours");
	expect(size(query({patch({0})},Op::Grow)) == 3,"patch corner does not wrap to the next row");
	expect(size(query({patch({12})},Op::Grow,Kind::Vertex,false,2)) == 13,"patch second ring uses Manhattan distance");
	grid = query({patch({12})},Op::Connected);
	expect(size(grid) == 25,"patch connected visits whole control grid");
	expect(size(query(grid,Op::Shrink)) == 9 && size(query(grid,Op::Shrink,Kind::Vertex,false,2)) == 1
		&& query(grid,Op::Shrink,Kind::Vertex,false,3).isEmpty(),"open patch border erodes one ring per step");
	expect(size(query({brush(Kind::Face,{0}),patch({12})},Op::Grow)) == 10,"mixed brush/patch query remains independent");
	QVector<LevelMapComponentSet> sentinel{brush(Kind::Vertex,{7})};
	for (const auto& invalid : QVector<QVector<LevelMapComponentSet>>{
		{brush(Kind::Vertex,{0}),brush(Kind::Face,{0})},{brush(Kind::Vertex,{-1})},{brush(Kind::Vertex,{8})},
		{brush(Kind::Edge,{12})},{brush(Kind::Face,{6})},{brush(Kind::Vertex,{0},99)},
		{{{LevelMapSelectionKind::QuakePatch,0},Kind::Face,{0}}},{patch({25})}}) {
		expect(!selectLevelMapComponents(map,invalid,{},&sentinel,&error) && sentinel.size() == 1 && sentinel[0].components == QVector<int>{7},"invalid query is atomic",error);
	}
	for (const auto& invalid : QVector<LevelComponentSelectionRequest>{{Op::Grow,Kind::Vertex,false,0},{Op::Grow,Kind::Vertex,false,257},
		{Op::Shrink,Kind::Vertex,true,1},{Op::Connected,Kind::Vertex,false,2},{Op::Convert,static_cast<Kind>(9),false,1}}) {
		expect(!selectLevelMapComponents(map,{brush(Kind::Vertex,{0})},invalid,&sentinel,&error),"invalid selection request rejected");
	}
	LevelBrushTopology solved; expect(levelBrushTopology(map.brushes[0],&solved,&error),"solved cache");
	auto broken = levelComponentSelectionTopology({LevelMapSelectionKind::QuakeBrush,0},solved); broken.edges.removeLast();
	expect(!queryLevelComponentSelection({broken},{brush(Kind::Vertex,{0})},{},&sentinel,&error),"incomplete cached topology rejected");
	expect(serializeLevelMap(map).bytes == bytes && map.undoStack.size() == undo,"queries leave map and history untouched");
	QVector<LevelComponentSelectionTopology> cached; QVector<LevelMapComponentSet> seeds, result;
	for (int id = 0; id < 128; ++id) { cached << levelComponentSelectionTopology({LevelMapSelectionKind::QuakeBrush,id},solved); seeds << brush(Kind::Vertex,{0},id); }
	QElapsedTimer timer; QVector<double> timings;
	for (int run = 0; run < 25; ++run) {
		timer.start(); expect(queryLevelComponentSelection(cached,seeds,{Op::Grow},&result,&error) && size(result) == 512,"bounded cached growth"); timings << timer.nsecsElapsed()/1e6;
	}
	std::sort(timings.begin(),timings.end()); std::cout << "128 cached brushes, growth query: median " << timings[12] << " ms; p95 " << timings[23] << " ms.\n";
}
void cliChecks(const LevelMapDocument& source, const QString& executable, const QString& root) {
	const auto input = root+"/selection.map", output = root+"/moved.map"; expect(saveLevelMapAs(source,input).succeeded(),"CLI source saved");
	const auto read = [](const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); };
	const auto original = read(input);
	const auto run = [&](const QStringList& arguments, int expectedCode = 0, const QString& command = "select-components") {
		QProcess child; child.setWorkingDirectory(root);
		child.start(executable,QStringList{"--cli","--json","--settings-file",root+"/settings.ini","map",command}+arguments);
		const bool finished = child.waitForStarted() && child.waitForFinished(30000);
		const auto bytes = child.readAllStandardOutput();
		expect(finished && child.exitStatus() == QProcess::NormalExit && child.exitCode() == expectedCode,"CLI exit code",QString::fromUtf8(bytes+child.readAllStandardError()));
		if (!finished) { child.kill(); child.waitForFinished(); } return QJsonDocument::fromJson(bytes).object();
	};
	const QStringList base{input,"--set","brush:0/face/0","--operation","convert","--to","vertex"};
	auto converted = run(base); expect(converted["componentCount"].toInt() == 4 && converted["selectors"].toArray().size() == 1,"CLI conversion emits replayable selector");
	expect(run({"--input",input,"--set=brush:0/vertex/0","--operation=convert","--to=face","--coverage=touching"})["componentCount"].toInt() == 3,"CLI touching conversion and equals options");
	expect(run({input,"--set","brush:0/vertex/0","--set","brush:0/vertex/1","--operation","convert","--to","edge"})["componentCount"].toInt() == 1,"CLI merges repeated same-kind input");
	expect(run({input,"--set","patch:0/vertex/12","--operation","grow","--steps","2"})["componentCount"].toInt() == 13,"CLI patch growth");
	auto connected = run({input,"--set","brush:0/vertex/0","--set","brush:1/vertex/0","--operation","connected"});
	expect(connected["componentCount"].toInt() == 16 && connected["objectCount"].toInt() == 2,"CLI whole-object connected selection");
	for (const auto& invalid : QVector<QStringList>{{"--typo"},{"--to","face"},{"--output",output},{"--overwrite"},{"--steps","2"},{"--coverage","invalid"},{"extra"},{"--json=yes"},{"--set","brush:0/vertex/-1"}}) { run(base+invalid,2); }
	for (const auto& invalid : QVector<QStringList>{{"--set","brush:0/vertex/1"},{"--set","brush:99/face/0"},{"--set","patch:0/edge/0"},{"--set","brush:1/face/99"}}) { run(base+invalid,4); }
	expect(read(input) == original && !QFile::exists(output),"selection queries and failures never write geometry");
	QStringList move{input};
	for (const auto& selector : connected["selectors"].toArray()) { move << "--set" << selector.toString(); }
	run(move+QStringList{"--operation","move","--delta","0,0,8","--output",output},0,"transform-components");
	LevelMapDocument moved; QString error; expect(loadLevelMap({output,{},"idtech3"},&moved,&error),"query output drives checked transform",error);
	if (moved.brushes.size() == 2) {
		for (int i = 0; i < 2; ++i) { expect(moved.brushes[i].mins.z == source.brushes[i].mins.z+8 && moved.brushes[i].maxs.z == source.brushes[i].maxs.z+8,"both selected brush graphs move intact"); }
	}
	expect(moved.patches.size() == 1 && moved.patches[0].controlPoints[12].z == source.patches[0].controlPoints[12].z,"unselected patch stays unchanged");
}
}
int main(int argc, char** argv) {
	QCoreApplication app(argc,argv); QTemporaryDir temp; if (argc != 2 || !temp.isValid()) { return 1; }
	LevelMapDocument map; QString error;
	if (!expect(tests::createGeometryFixture(2,&map,&error),"fixture",error)) { return 1; }
	LevelPatchCreateRequest request; request.columns = request.rows = 5; LevelMapPatch patch;
	if (!expect(createLevelPatch(request,&patch,&error) && addLevelMapPatch(&map,patch,nullptr,&error),"patch fixture",error)) { return 1; }
	coreChecks(map); cliChecks(map,QString::fromLocal8Bit(argv[1]),temp.path()); return ok ? 0 : 1;
}
