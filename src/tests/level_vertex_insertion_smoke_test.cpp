#include "core/level_brush.h"
#include "core/level_document.h"
#include "core/level_linked_groups.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_geometry_test_helpers.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value,const char* label,const QString& error = {}) { if (!value) { ok = false; std::cerr << label << ": " << error.toStdString() << '\n'; } return value; }
QByteArray bytes(const LevelMapDocument& map) { return serializeLevelMap(map).bytes; }
LevelMapDocument fixture(const QString& style,QString* error)
{
	LevelMapDocument seed; tests::createGeometryFixture(1,&seed,error);
	const bool primitive = style.startsWith("brushDef"),flags = primitive || style.endsWith("-flags");
	QString source = "// insertion fixture\n{\n\"classname\" \"worldspawn\"\n\"custom\" \"keep me\"\n";
	const auto xyz = [](LevelMapVec3 p) { return QStringLiteral("( %1 %2 %3 )").arg(p.x).arg(p.y).arg(p.z); };
	for (int brush = 0; brush < 2; ++brush) {
		source += "{\n"; if (primitive) { source += style+"\n{\n"; }
		for (auto face : seed.brushes[0].faces) {
			for (auto* point : {&face.p0,&face.p1,&face.p2}) { point->z += brush*32; }
			const auto plane = planeFromPoints(face.p0,face.p1,face.p2);
			source += style == "brushDef3" ? QStringLiteral("( %1 %2 %3 %4 )").arg(plane.normalX).arg(plane.normalY).arg(plane.normalZ).arg(-plane.distance)
				: xyz(face.p0)+' '+xyz(face.p1)+' '+xyz(face.p2);
			if (primitive) { source += " ( ( 0.0078125 0 0.25 ) ( 0 0.0078125 -0.125 ) ) studio/stone"; }
			else { source += " studio/stone"; source += style.startsWith("valve") ? " [ 1 0 0 16 ] [ 0 1 0 -8 ] 0 0.5 0.5" : " 16 -8 15 0.5 0.5"; }
			if (flags) { source += " 1 4 7"; } source += " // surface\n";
		}
		if (primitive) { source += "}\n"; } source += "}\n";
	}
	source += "}\n// untouched tail\n";
	LevelMapDocument result; expect(loadLevelMapBytes({"insertion.map",{},"idtech3"},source.toUtf8(),&result,error),"load dialect",*error); return result;
}
}
int main(int argc,char** argv)
{
	QCoreApplication app(argc,argv); QTemporaryDir temp; if (!temp.isValid() || argc != 2) { return 1; } QString error;
	const QVector<LevelMapVertexInsertion> edits{{0,{{-32,8,16,true}}},{1,{{-32,8,16,true}}}};
	for (const auto& style : QStringList{"classic","classic-flags","valve220","valve220-flags","brushDef","brushDef3"}) {
		auto map = fixture(style,&error); if (map.brushes.size() != 2) { return 1; }
		setLevelMapSelection(&map,{{LevelMapSelectionKind::QuakeBrush,0},{LevelMapSelectionKind::QuakeBrush,1}},&error);
		const auto selection = map.selection; const auto original = bytes(map); const auto kind = map.brushes[0].primitiveKind;
		LevelMapComponentReport report;
		expect(insertLevelMapVertices(&map,edits,0,false,&report,&error),"insert shared edge corner",style+error);
		expect(report.brushes == 2 && report.insertedVertices == 2 && report.collapsedVertices == 0 && map.undoStack.size() == 1 && map.selection == selection,"one atomic batch",style);
		for (const auto& brush : map.brushes) {
			LevelBrushTopology topology; expect(levelBrushTopology(brush,&topology) && topology.vertices.size() == 9 && brush.primitiveKind == kind,"valid hull and dialect",style);
			for (const auto& face : brush.faces) { expect(face.textureName == "studio/stone" && face.surfaceFlags == (style.endsWith("-flags") || style.startsWith("brushDef") ? 4 : 0),"donor material and flags",style); }
		}
		const auto edited = bytes(map); expect(edited.contains("\"custom\" \"keep me\"") && edited.endsWith("// untouched tail\n"),"unrelated source preserved",style);
		LevelMapDocument reloaded; expect(loadLevelMapBytes({"reloaded.map",{},"idtech3"},edited,&reloaded,&error),"round trip",style+error);
		for (const auto& brush : reloaded.brushes) { LevelBrushTopology topology; expect(levelBrushTopology(brush,&topology) && topology.vertices.size() == 9,"saved inserted corners survive",style); }
		expect(undoLevelMapEdit(&map,&error) && bytes(map) == original && redoLevelMapEdit(&map,&error) && bytes(map) == edited,"exact undo redo",style+error);
	}
	auto map = fixture("classic",&error); const auto original = bytes(map); LevelMapComponentReport report;
	for (auto invalid : QVector<QVector<LevelMapVertexInsertion>>{{},{edits[0],edits[0]},{{999,{{0,0,0,true}}}},
		{edits[0],{1,{{0,0,32,true}}}},{edits[0],{1,{{-32,8,std::numeric_limits<double>::infinity(),true}}}}}) {
		report.insertedVertices = 999;
		expect(!insertLevelMapVertices(&map,invalid,0,false,&report,&error) && bytes(map) == original && map.undoStack.isEmpty() && report.insertedVertices == 0,"invalid batch is atomic",error);
	}
	expect(!insertLevelMapVertices(&map,edits,-1,false,&report,&error) && bytes(map) == original,"invalid grid is atomic");
	QString group;
	expect(createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Protected",{},&group,&error) && assignLevelSceneObjects(&map,group,{"brush:1"},&error)
		&& setLevelSceneLocked(&map,group,true,&error),"lock fixture",error);
	const auto locked = bytes(map); const auto history = map.undoStack.size();
	expect(!insertLevelMapVertices(&map,edits,0,false,&report,&error) && bytes(map) == locked && map.undoStack.size() == history && report.brushes == 0,"locked second brush rolls back first",error);
	map = fixture("classic",&error); QString copy;
	expect(createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Linked",{},&group,&error) && assignLevelSceneObjects(&map,group,{"brush:0"},&error)
		&& createLinkedLevelGroup(&map,group,{96,0,0,true},&copy,&error),"linked fixture",error);
	const auto linked = bytes(map); const auto linkedHistory = map.undoStack.size();
	expect(insertLevelMapVertices(&map,{edits[0]},0,false,&report,&error) && map.undoStack.size() == linkedHistory+1,"linked insertion",error);
	LevelBrushTopology topology; expect(levelBrushTopology(map.brushes.last(),&topology) && topology.vertices.size() == 9,"linked copy receives inserted corner");
	expect(undoLevelMapEdit(&map,&error) && bytes(map) == linked,"linked undo exact",error);
	map = fixture("classic",&error); const auto source = temp.filePath("input.map"),output = temp.filePath("output.map");
	QFile input(source); expect(input.open(QIODevice::WriteOnly) && input.write(bytes(map)) > 0,"write input"); input.close();
	const auto run = [&](QStringList arguments,bool success = true) {
		QProcess process; process.setWorkingDirectory(temp.path()); process.start(QString::fromLocal8Bit(argv[1]),QStringList{"--cli","--json","--settings-file",temp.filePath("settings.ini")}+arguments);
		const bool finished = process.waitForStarted() && process.waitForFinished(30000); const auto data = process.readAllStandardOutput();
		expect(finished && process.exitStatus() == QProcess::NormalExit && (process.exitCode() == 0) == success,"CLI status",QString::fromUtf8(data+process.readAllStandardError()));
		if (!finished) { process.kill(); process.waitForFinished(); } return QJsonDocument::fromJson(data).object();
	};
	const QStringList arguments{"map","insert-vertices",source,"--brush","0","--brush","1","--position","-32,8,16","--output",output};
	run(arguments+QStringList{"--dry-run"}); expect(!QFile::exists(output),"batch dry run creates no output");
	const auto result = run(arguments);
	expect(result["brushes"].toInt() == 2 && result["insertedVertices"].toInt() == 2 && result["brushIds"].toArray().size() == 2,"CLI batch counts");
	QFile saved(output); expect(saved.open(QIODevice::ReadOnly),"read output"); const auto savedBytes = saved.readAll(); saved.close();
	expect(insertLevelMapVertices(&map,edits,0,false,&report,&error) && savedBytes == bytes(map),"CLI equals shared service",error);
	for (auto extra : QVector<QStringList>{{},{"--brush","0"},{"--brush","999"},{"--position","0,0,32"},{"--grid","nan"},{"--typo"},{"--dry-run=true"}}) {
		run(arguments+extra,false); QFile f(output); expect(f.open(QIODevice::ReadOnly) && f.readAll() == savedBytes,"refused CLI preserves output");
	}
	return ok ? 0 : 1;
}
