#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_textured_selection.h"
#include "tests/level_type_selection_test_helpers.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <array>
#include <cmath>
#include <iostream>

// Independent face manipulation: faces picked by Select Textured move, rotate
// and scale through the shared component transform without parent selection.
using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* label, const QString& error = {})
{
	if (!value) { ok = false; std::cerr << "FAIL: " << label << ": " << error.toStdString() << std::endl; } return value;
}
bool near(double a, double b, double tolerance = 1e-3) { return std::abs(a-b) <= tolerance; }
LevelMapDocument fixture()
{
	LevelMapDocument map; QString error;
	expect(loadLevelMapBytes({"faces.map",{},{}},tests::typeSelectionFixture(),&map,&error),"parse face fixture",error); return map;
}
const LevelMapBrush& brush(const LevelMapDocument& map, int id)
{
	return *std::find_if(map.brushes.cbegin(),map.brushes.cend(),[&](const auto& b) { return b.id == id; });
}
struct Plane { LevelMapVec3 normal; double distance = 0; };
Plane plane(const LevelMapBrush& source, int face)
{
	LevelBrushTopology topology; levelBrushTopology(source,&topology);
	const auto& ids = topology.faces[face]; double nx = 0, ny = 0, nz = 0, d = 0;
	for (qsizetype i = 0; i < ids.size(); ++i) {
		const auto& a = topology.vertices[ids[i]]; const auto& b = topology.vertices[ids[(i+1)%ids.size()]];
		nx += (a.y-b.y)*(a.z+b.z); ny += (a.z-b.z)*(a.x+b.x); nz += (a.x-b.x)*(a.y+b.y);
	}
	const double length = std::sqrt(nx*nx+ny*ny+nz*nz); Plane result; result.normal = {nx/length,ny/length,nz/length,true};
	// Orient outward from the brush centre, whatever the solved winding.
	double cx = 0, cy = 0, cz = 0;
	for (int id : ids) { cx += topology.vertices[id].x; cy += topology.vertices[id].y; cz += topology.vertices[id].z; }
	const auto count = static_cast<double>(ids.size());
	const double outward = (cx/count-(source.mins.x+source.maxs.x)/2)*result.normal.x+(cy/count-(source.mins.y+source.maxs.y)/2)*result.normal.y
		+(cz/count-(source.mins.z+source.maxs.z)/2)*result.normal.z;
	if (outward < 0) { result.normal = {-result.normal.x,-result.normal.y,-result.normal.z,true}; }
	for (int id : ids) { const auto& p = topology.vertices[id]; d += p.x*result.normal.x+p.y*result.normal.y+p.z*result.normal.z; }
	result.distance = d/count; return result;
}
int faceFacing(const LevelMapBrush& source, const LevelMapVec3& normal)
{
	for (int face = 0; face < source.faces.size(); ++face) {
		const auto p = plane(source,face).normal; if (p.x*normal.x+p.y*normal.y+p.z*normal.z > 0.9999) { return face; }
	}
	return -1;
}
LevelSurfaceFacePicks query(const LevelMapDocument& map, const QString& material)
{
	auto copy = map; LevelTexturedSelectionRequest request; request.material = material; LevelTexturedSelectionReport report; QString error;
	expect(selectLevelMapTextured(&copy,request,&report,&error),"Select Textured seed",error);
	return {report.faces,report.brushGeometry,{}};
}
void moveFace()
{
	auto map = fixture(); const auto picks = query(map,"metal");
	if (!expect(picks.faces == QVector<LevelSurfaceFace>{{0,0}},"metal face is the only independent pick")) { return; }
	const auto before = plane(brush(map,0),0); const auto other = levelSurfaceSelectionGeometryKey(brush(map,1));
	const auto bytes = serializeLevelMap(map).bytes; const auto history = map.undoStack.size(); const auto selection = map.selection;
	LevelComponentTransformRequest move; move.textureLock = true;
	move.delta = {before.normal.x*16,before.normal.y*16,before.normal.z*16,true};
	LevelSurfaceFaceTransformReport report; QString error;
	if (!expect(transformLevelMapSurfaceFaces(&map,picks,{},move,&report,&error),"move independent face along its normal",error)) { return; }
	const auto& moved = brush(map,0);
	expect(report.faces.size() == 1 && report.retiredFaces == 0 && report.components.brushes == 1 && report.sets.size() == 1
		&& report.sets[0].kind == LevelBrushComponent::Face,"one face set, one changed brush, pick retained");
	const auto after = plane(moved,report.faces[0].faceIndex);
	expect(after.normal.x*before.normal.x+after.normal.y*before.normal.y+after.normal.z*before.normal.z > 0.9999
		&& near(after.distance,before.distance+16),"the pick follows the moved plane");
	expect(moved.faces[report.faces[0].faceIndex].textureName == "metal","the moved face keeps its material");
	expect(report.brushGeometry.value(0) == levelSurfaceSelectionGeometryKey(moved) && report.brushGeometry.size() == 1,"returned identity binds the new geometry");
	expect(map.selection == selection && !map.selection.contains({LevelMapSelectionKind::QuakeBrush,0}),"the parent brush stays unselected");
	expect(levelSurfaceSelectionGeometryKey(brush(map,1)) == other,"unpicked brushes are untouched");
	const auto changed = serializeLevelMap(map).bytes;
	expect(map.undoStack.size() == history+1 && undoLevelMapEdit(&map,&error) && serializeLevelMap(map).bytes == bytes
		&& redoLevelMapEdit(&map,&error) && serializeLevelMap(map).bytes == changed,"one exact undo step",error);

	// UV lock: an in-plane move keeps every corner's texture coordinates.
	map = fixture(); const auto source = brush(map,0); const auto n = plane(source,0).normal;
	const LevelMapVec3 along = std::abs(n.z) < 0.9 ? LevelMapVec3{0,0,8,true} : LevelMapVec3{8,0,0,true};
	LevelComponentTransformRequest slide; slide.delta = along; slide.textureLock = true; slide.allowValve220 = true;
	if (!expect(transformLevelMapSurfaceFaces(&map,picks,{},slide,&report,&error) && report.faces.size() == 1,"slide picked face within its plane",error)) { return; }
	LevelBrushTopology topology; levelBrushTopology(source,&topology);
	const auto beforeUv = levelTextureProjection(source.faces[0]);
	const auto afterUv = levelTextureProjection(brush(map,0).faces[report.faces[0].faceIndex]);
	for (int vertex : topology.faces[0]) {
		const auto p = topology.vertices[vertex]; const auto expected = beforeUv.at(p);
		const auto actual = afterUv.at({p.x+along.x,p.y+along.y,p.z+along.z,true});
		expect(near(expected.x(),actual.x(),.002) && near(expected.y(),actual.y(),.002),"UV lock keeps the picked face's corner coordinates");
	}
}
void affineFaces()
{
	auto map = fixture(); const int top0 = faceFacing(brush(map,0),{0,0,1,true}), top1 = faceFacing(brush(map,1),{0,0,1,true});
	if (!expect(top0 >= 0 && top1 >= 0,"top faces")) { return; }
	LevelSurfaceFacePicks picks; picks.faces = {{0,top0},{1,top1}};
	LevelComponentTransformRequest scale; scale.operation = LevelComponentTransformKind::Scale; scale.scale = {1,1,2,true};
	scale.pivot = {32,32,0,true}; scale.textureLock = true; scale.allowValve220 = true;
	const auto bytes = serializeLevelMap(map).bytes; const auto history = map.undoStack.size();
	LevelSurfaceFaceTransformReport report; QString error;
	if (!expect(transformLevelMapSurfaceFaces(&map,picks,{},scale,&report,&error),"scale two brushes' independent top faces",error)) { return; }
	expect(report.components.brushes == 2 && report.faces.size() == 2 && report.retiredFaces == 0 && map.undoStack.size() == history+1,"one undo step for both brushes");
	for (const auto& face : report.faces) {
		const auto p = plane(brush(map,face.brushId),face.faceIndex);
		expect(p.normal.z > 0.9999 && near(p.distance,192),"both top faces reach the scaled height");
		expect(near(brush(map,face.brushId).mins.z,0) && near(brush(map,face.brushId).maxs.z,192),"bottom corners stay at the pivot plane");
	}
	expect(undoLevelMapEdit(&map,&error) && serializeLevelMap(map).bytes == bytes,"multi-brush exact undo",error);
	// A half turn maps the face onto itself; its pick survives the rebuild.
	// Without UV lock: the corners trade places, so locked UVs would not fit.
	LevelComponentTransformRequest turn; turn.operation = LevelComponentTransformKind::Rotate; turn.axis = 2; turn.amount = 180;
	expect(transformLevelMapSurfaceFaces(&map,{{{0,top0}},{},{}},{},turn,&report,&error) && report.faces.size() == 1
		&& plane(brush(map,0),report.faces[0].faceIndex).normal.z > 0.9999,"symmetric rotation keeps the pick on the same plane",error);
}
void mergeSets()
{
	const auto map = fixture(); QVector<LevelMapComponentSet> sets; QString error;
	const LevelMapSelectionRef first{LevelMapSelectionKind::QuakeBrush,0}, second{LevelMapSelectionKind::QuakeBrush,1};
	LevelSurfaceFacePicks picks; picks.faces = {{0,0},{1,2},{0,0}};
	expect(levelSurfaceFaceComponentSets(map,picks,{{first,LevelBrushComponent::Face,{3}}},&sets,&error) && sets.size() == 2
		&& sets[0].kind == LevelBrushComponent::Face && sets[0].components == QVector<int>{3,0}
		&& sets[1].object == second && sets[1].components == QVector<int>{2},"faces join an existing face set; new brushes append; duplicates collapse",error);
	LevelBrushTopology topology; levelBrushTopology(brush(map,0),&topology);
	const auto corners = levelBrushComponentVertices(topology,LevelBrushComponent::Face,{0});
	int outside = 0; while (corners.contains(outside)) { ++outside; }
	auto expected = corners; expected.append(outside); std::sort(expected.begin(),expected.end());
	expect(levelSurfaceFaceComponentSets(map,{{{0,0}},{},{}},{{first,LevelBrushComponent::Vertex,{outside}}},&sets,&error)
		&& sets.size() == 1 && sets[0].kind == LevelBrushComponent::Vertex && sets[0].components == expected,"vertex picks receive the face's corners",error);
	const auto edgeCorners = levelBrushComponentVertices(topology,LevelBrushComponent::Edge,{0});
	expect(levelSurfaceFaceComponentSets(map,{{{0,0}},{},{}},{{first,LevelBrushComponent::Edge,{0}}},&sets,&error)
		&& sets[0].kind == LevelBrushComponent::Vertex && std::all_of(edgeCorners.cbegin(),edgeCorners.cend(),[&](int v) { return sets[0].components.contains(v); })
		&& std::all_of(corners.cbegin(),corners.cend(),[&](int v) { return sets[0].components.contains(v); }),"edge picks merge as vertices",error);
	const QVector<LevelMapComponentSet> patch{{{LevelMapSelectionKind::QuakePatch,0},LevelBrushComponent::Vertex,{0,1}}};
	expect(levelSurfaceFaceComponentSets(map,{},patch,&sets,&error) && sets.size() == 1 && sets[0].object == patch[0].object,"no picks leave components unchanged",error);
}
void refusals()
{
	const auto base = fixture(); const auto picks = query(base,"metal");
	for (int scenario = 0; scenario < 12; ++scenario) {
		auto map = base; auto request = picks; QString group, error;
		LevelComponentTransformRequest transform; transform.delta = {0,0,8,true};
		switch (scenario) {
		case 0: request.brushGeometry.insert(0,QByteArray(32,'x')); break;
		case 1: request.faces = {{0,99}}; break;
		case 2: request.faces = {{999,0}}; break;
		case 3: map.hiddenObjects = {"brush:0"}; break;
		case 4: map.hiddenObjects = {"entity:1"}; break;
		case 5: request.excludedObjects = {"brush:0"}; break;
		case 6:
			expect(createLevelSceneNode(&map,LevelSceneNodeKind::Layer,"Locked",{},&group,&error)
				&& assignLevelSceneObjects(&map,group,{"entity:1"},&error) && setLevelSceneLocked(&map,group,true,&error),"lock fixture",error);
			break;
		case 7: expect(createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Closed",{},&group,&error) && assignLevelSceneObjects(&map,group,{"entity:1"},&error),"group fixture",error); break;
		case 8: request.faces.fill({0,0},16385); break;
		case 9: map.format = LevelMapFormat::DoomWad; break;
		case 10: transform.operation = LevelComponentTransformKind::Scale; transform.scale = {0,0,0,true}; break;
		default: transform.delta.x = std::nan(""); break;
		}
		const auto bytes = serializeLevelMap(map).bytes; const auto revision = map.revision; const auto history = map.undoStack.size();
		LevelSurfaceFaceTransformReport sentinel; sentinel.retiredFaces = 77;
		expect(!transformLevelMapSurfaceFaces(&map,request,{},transform,&sentinel,&error) && !error.isEmpty() && sentinel.retiredFaces == 77
			&& serializeLevelMap(map).bytes == bytes && map.revision == revision && map.undoStack.size() == history,"refusal leaves map and report untouched",
			QStringLiteral("scenario %1: %2").arg(scenario).arg(error));
	}
	auto map = base; QString group, error; LevelSurfaceFaceTransformReport report;
	LevelComponentTransformRequest move; move.delta = {0,0,8,true};
	expect(createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Open",{},&group,&error) && assignLevelSceneObjects(&map,group,{"entity:1"},&error)
		&& openLevelSceneGroup(&map,group,&error) && transformLevelMapSurfaceFaces(&map,picks,{},move,&report,&error) && report.faces.size() == 1,"open group context accepts its faces",error);
}
// The component transform accepts at most 256 objects, so picks on 257 brushes
// are refused before any brush is solved past that limit.
void objectLimit()
{
	QString source = QStringLiteral("{\n\"classname\" \"worldspawn\"\n");
	for (int i = 0; i < 257; ++i) {
		const std::array<double,3> lo{32.0*i,0,0}, hi{32.0*i+16,16,16};
		const auto point = [](const std::array<double,3>& v) { return QStringLiteral("( %1 %2 %3 )").arg(v[0]).arg(v[1]).arg(v[2]); };
		source += QStringLiteral("{\n");
		for (int axis = 0; axis < 3; ++axis) {
			for (const bool positive : {false,true}) {
				auto p = lo; p[axis] = positive ? hi[axis] : lo[axis];
				int first = (axis+1)%3, second = (axis+2)%3; if (positive) { std::swap(first,second); }
				auto a = p, b = p; a[first] += 1; b[second] += 1;
				source += QStringLiteral("%1 %2 %3 stone 0 0 0 1 1\n").arg(point(p),point(a),point(b));
			}
		}
		source += QStringLiteral("}\n");
	}
	source += QStringLiteral("}\n");
	LevelMapDocument map; QString error;
	if (!expect(loadLevelMapBytes({"many.map",{},{}},source.toUtf8(),&map,&error) && map.brushes.size() == 257,"parse 257-brush fixture",error)) { return; }
	LevelSurfaceFacePicks picks;
	for (const auto& brush : std::as_const(map.brushes)) { picks.faces.append({brush.id,0}); }
	const auto bytes = serializeLevelMap(map).bytes; const auto history = map.undoStack.size();
	QVector<LevelMapComponentSet> sets; LevelSurfaceFaceTransformReport report; report.retiredFaces = 5;
	LevelComponentTransformRequest move; move.delta = {0,0,8,true};
	expect(!levelSurfaceFaceComponentSets(map,picks,{},&sets,&error) && error.contains(QStringLiteral("256")),"picks on 257 brushes are refused",error);
	expect(!transformLevelMapSurfaceFaces(&map,picks,{},move,&report,&error) && report.retiredFaces == 5
		&& serializeLevelMap(map).bytes == bytes && map.undoStack.size() == history,"the refused transform leaves the map unchanged",error);
	picks.faces.removeLast();
	expect(levelSurfaceFaceComponentSets(map,picks,{},&sets,&error) && sets.size() == 256,"picks on 256 brushes merge into 256 face sets",error);
	expect(transformLevelMapSurfaceFaces(&map,picks,{},move,&report,&error) && report.components.brushes == 256 && report.faces.size() == 256
		&& map.undoStack.size() == history+1,"256 brushes move in one undo step",error);
}
void placement()
{
	const auto map = fixture(); const auto bytes = serializeLevelMap(map).bytes;
	LevelPlacementRequest work; work.operation = LevelPlacementOperation::TransformComponents; work.surfaceFaces = query(map,"metal");
	work.componentTransform.delta = {0,0,8,true};
	auto result = prepareLevelPlacement(map,work);
	expect(result.succeeded && result.surfaceFaceReport.faces.size() == 1 && result.componentReport.brushes == 1 && serializeLevelMap(map).bytes == bytes,"placement worker transforms picked faces on a candidate",result.error);
	for (auto phase : {LevelPlacementPhase::Transforming,LevelPlacementPhase::Finalizing}) {
		bool cancel = false;
		result = prepareLevelPlacement(map,work,{[&] { return cancel; },[&](const auto& progress) { cancel = cancel || progress.phase == phase; }});
		expect(result.cancelled && !result.succeeded && result.surfaceFaceReport.faces.isEmpty() && result.componentReport.brushes == 0
			&& serializeLevelMap(map).bytes == bytes,"cancellation discards candidate and face report");
	}
}
void cli(const QString& executable)
{
	QTemporaryDir temp; if (!expect(temp.isValid(),"CLI temporary directory")) { return; }
	const auto path = temp.filePath("faces.map"), output = temp.filePath("moved.map"), mixed = temp.filePath("mixed.map"), chained = temp.filePath("chained.map");
	const auto source = tests::typeSelectionFixture(); QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(),"CLI fixture")) { return; } file.close();
	const auto read = [](const QString& name) { QFile in(name); return in.open(QIODevice::ReadOnly) ? in.readAll() : QByteArray(); };
	const auto run = [&](const QStringList& args, int code = 0) {
		QProcess process; process.setWorkingDirectory(temp.path());
		process.start(executable,QStringList{"--cli","--json","--settings-file",temp.filePath("settings.ini"),"map"}+args);
		const bool done = process.waitForStarted() && process.waitForFinished(30000); const auto out = process.readAllStandardOutput(), err = process.readAllStandardError();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code,"CLI status",args.join(' ')+QStringLiteral("\n")+QString::fromUtf8(out+err));
		if (!done) { process.kill(); process.waitForFinished(); } return QJsonDocument::fromJson(out).object();
	};
	const auto faces = run({"select-textured",path,"--material","metal"})["faces"].toArray();
	if (!expect(faces == QJsonArray{"face:0:1"},"query hands over the independent face")) { return; }
	QStringList move{"transform-components",path,"--operation","move","--delta","0,0,16","--uv-lock","on","--output",output};
	for (const auto& face : faces) { move << "--face" << face.toString(); }
	auto report = run(move+QStringList{"--dry-run"});
	expect(!QFile::exists(output) && report["brushes"].toInt() == 1 && report["faces"].toArray().size() == 1 && report["retiredFaces"].toInt() == 0
		&& report["parentBrushesSelected"] == false && report["brushGeometrySha256"].toObject()["0"].toString().size() == 64,"dry run reports the remapped pick");
	report = run(move);
	auto expected = fixture(); LevelComponentTransformRequest request; request.delta = {0,0,16,true}; request.textureLock = true;
	LevelSurfaceFaceTransformReport core; QString error;
	expect(transformLevelMapSurfaceFaces(&expected,query(expected,"metal"),{},request,&core,&error),"core oracle",error);
	LevelMapDocument loaded;
	if (expect(loadLevelMap({output,{},{}},&loaded,&error),"reload CLI output",error)) {
		const auto& a = brush(loaded,0); const auto& b = brush(expected,0);
		expect(near(a.mins.x,b.mins.x) && near(a.mins.y,b.mins.y) && near(a.mins.z,b.mins.z) && near(a.maxs.x,b.maxs.x) && near(a.maxs.y,b.maxs.y) && near(a.maxs.z,b.maxs.z),
			"CLI and GUI share the face transform service");
		expect(report["faces"].toArray() == QJsonArray{QStringLiteral("face:0:%1").arg(core.faces.value(0).faceIndex+1)},"CLI returns the pick after the edit");
		expect(loaded.selection.isEmpty() || !loaded.selection.contains({LevelMapSelectionKind::QuakeBrush,0}),"CLI never selects the parent brush");
	}
	QStringList chain{"transform-components",output,"--operation","rotate","--axis","z","--angle","15","--output",chained};
	for (const auto& face : report["faces"].toArray()) { chain << "--face" << face.toString(); }
	run(chain);
	report = run({"transform-components",path,"--set","brush:1/face/0","--face","face:0:1","--operation","move","--delta","0,0,8","--output",mixed});
	expect(report["brushes"].toInt() == 2 && report["faces"].toArray().size() == 1,"--set and --face share one edit");
	const auto moved = read(output);
	for (const auto& bad : QVector<QStringList>{{"--face","patch:0"},{"--face","face:0:0"},{"--face","face:0:99"},{"--face","face:0:1","--context","missing"},
		{"--set","brush:1/face/0","--context","missing"},{"--face","face:0:1","--operation","snap","--target","brush:1/vertex/0"}}) {
		QStringList args{"transform-components",path};
		if (!bad.contains("--operation")) { args << "--operation" << "move" << "--delta" << "0,0,8"; }
		run(args+bad+QStringList{"--output",output,"--overwrite"},2);
	}
	expect(read(path) == source && read(output) == moved,"refusals leave input and output unchanged");
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc,argv); moveFace(); affineFaces(); mergeSets(); refusals(); objectLimit(); placement();
	if (argc == 2) { cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath()); }
	return ok ? 0 : 1;
}
