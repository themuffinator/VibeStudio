#include "core/level_brush.h"
#include "core/level_merge.h"
#include "core/level_linked_groups.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_placement_control_p.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/level_surface_test_helpers.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <limits>

using namespace vibestudio;
int main(int argc, char** argv)
{
	QCoreApplication app(argc,argv); QTemporaryDir temp; if (!temp.isValid() || argc != 2) { return 1; }
	bool ok = true; QString error;
	const auto expect = [&](bool value,const char* label,const QString& detail = {}) { if (!value) { ok = false; std::cerr << label << ": " << detail.toStdString() << '\n'; } return value; };
	const auto bytes = [](const auto& doc) { return serializeLevelMap(doc).bytes; };
	const auto put = [](const QString& path,const QByteArray& data) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size(); };
	const auto read = [](const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; };
	const auto run = [&](const QStringList& args,bool success = true) {
		QProcess p; p.setWorkingDirectory(temp.path()); p.start(QString::fromLocal8Bit(argv[1]),QStringList{"--cli","--json","--settings-file",temp.filePath("settings.ini")}+args);
		const bool finished = p.waitForStarted() && p.waitForFinished(30000); const auto output = p.readAllStandardOutput();
		expect(finished && p.exitStatus() == QProcess::NormalExit && (p.exitCode() == 0) == success,"CLI status",QString::fromUtf8(output+p.readAllStandardError()));
		if (!finished) { p.kill(); p.waitForFinished(); } return QJsonDocument::fromJson(output).object();
	};
	const QVector<LevelMapVec3> point{{160,48,24,true}};
	const LevelMapClipOptions options{"studio/cut",true,"studio/wrap"};
	for (const auto& dialect : QStringList{"classic","valve220","brushDef","brushDef3"}) {
		LevelMapDocument source;
		if (!expect(loadLevelMapBytes({temp.filePath(dialect+".map"),{},"idtech3"},tests::surfaceFixture(dialect),&source,&error),"load source",error)) { continue; }
		setLevelMapSelection(&source,{{LevelMapSelectionKind::QuakeBrush,0}});
		const auto before = bytes(source); auto wrapped = source; LevelMapClipReport report;
		if (!expect(clipLevelMapPoints(&wrapped,point,2,1,LevelMapClipKeep::Back,options,&report,&error),"one-point wrap",dialect+error)) { continue; }
		expect(report.wrapped && report.changed == 1 && report.inheritedFaces == 5 && report.newFaces == 4,"hull replaces one exterior plane with four triangles",dialect);
		expect(wrapped.brushes.size() == 1 && wrapped.brushes[0].id == 0 && wrapped.undoStack.size() == 1,"wrap retains brush identity, one undo",dialect);
		LevelBrushTopology topology; expect(levelBrushTopology(wrapped.brushes[0],&topology,&error) && topology.vertices.size() == 9,"closed hull has nine vertices",dialect+error);
		for (const auto& face : wrapped.brushes[0].faces) {
			if (face.textureName == "studio/wrap") { expect(face.contentFlags == 2 && face.surfaceFlags == 0 && face.surfaceValue == 0,"new hull material ignores cut material and surface flags",dialect); continue; }
			expect(face.textureName == "studio/grid" && face.contentFlags == 2 && face.surfaceFlags == 4 && face.surfaceValue == 8,"exterior material and flags retained",dialect);
			const auto plane = planeFromPoints(face.p0,face.p1,face.p2);
			bool matched = false;
			for (const auto& original : source.brushes[0].faces) {
				const auto previous = planeFromPoints(original.p0,original.p1,original.p2);
				if (std::abs(plane.distance-previous.distance)>1e-7 || std::abs(plane.normalX-previous.normalX)>1e-7
					|| std::abs(plane.normalY-previous.normalY)>1e-7 || std::abs(plane.normalZ-previous.normalZ)>1e-7) { continue; }
				matched = true;
				for (const auto& p : topology.vertices) { const auto delta = levelTextureProjection(original).at(p)-levelTextureProjection(face).at(p); expect(std::abs(delta.x())<1e-6 && std::abs(delta.y())<1e-6,"world-space UVs retained",dialect); }
			}
			expect(matched,"inherited plane matches source",dialect);
		}
		const auto after = bytes(wrapped);
		expect(after.count("// flags") == before.count("// flags") && after.count("/* retained */") == before.count("/* retained */"),"all face comments retained once",dialect);
		expect(undoLevelMapEdit(&wrapped,&error) && bytes(wrapped) == before && wrapped.selection == source.selection,"exact undo and selection",dialect+error);
		expect(redoLevelMapEdit(&wrapped,&error) && bytes(wrapped) == after,"exact redo",dialect+error);
		LevelMapDocument reloaded; expect(loadLevelMapBytes({temp.filePath("reload.map"),{},"idtech3"},after,&reloaded,&error) && reloaded.brushes.size() == 1 && reloaded.brushes[0].primitiveKind == source.brushes[0].primitiveKind,"save/reload retains format",dialect+error);
		auto inside = source; expect(!clipLevelMapPoints(&inside,{{0,32,0,true}},2,1,LevelMapClipKeep::Both,options,&report,&error) && bytes(inside) == before && inside.undoStack.isEmpty() && report.changed == 0,"interior point is a refused no-op",dialect+error);
		auto noFallback = source; expect(!clipLevelMapPoints(&noFallback,point,2,1,LevelMapClipKeep::Both,{},nullptr,&error) && bytes(noFallback) == before,"wrap is explicit",dialect);
		auto two = source;
		expect(clipLevelMapPoints(&two,{{160,16,0,true},{160,80,0,true}},2,1,LevelMapClipKeep::Both,options,&report,&error)
			&& report.wrapped && two.brushes[0].maxs.z == 56,"no-change plane wraps actual points, never synthetic third",dialect+error);
		auto collinear = source;
		expect(clipLevelMapPoints(&collinear,{{160,16,24,true},{160,32,24,true},{160,48,24,true}},2,1,LevelMapClipKeep::Back,options,&report,&error) && report.wrapped,"collinear points wrap",dialect+error);
		auto cut = source;
		expect(clipLevelMapPoints(&cut,{{0,0,0,true},{0,1,0,true},{0,0,1,true}},2,1,LevelMapClipKeep::Both,options,&report,&error)
			&& !report.wrapped && report.changed == 1 && cut.brushes.size() == 2,"real cut takes precedence",dialect+error);
		auto removed = source;
		expect(clipLevelMapPoints(&removed,{{160,16,0,true},{160,80,0,true}},2,1,LevelMapClipKeep::Front,options,&report,&error)
			&& !report.wrapped && removed.brushes.isEmpty(),"discarded-side removal takes precedence",dialect+error);
		const auto input = temp.filePath(dialect+".map"), output = temp.filePath(dialect+"-wrap.map"); expect(put(input,before),"write CLI source");
		const QStringList command{"map","clip",input,"--object","brush:0","--points","160,48,24","--wrap-merge","--wrap-texture","studio/wrap","--cut-texture","studio/cut","--output",output};
		const auto dry = run(command+QStringList{"--dry-run"}); expect(dry["operation"] == "wrap-merge" && dry["newFaces"] == 4 && !QFile::exists(output),"CLI dry run reports wrap");
		run(command); expect(read(output) == after,"CLI and core produce identical output",dialect);
		for (const auto& extra : QVector<QStringList>{{"--wrap-merge"},{"--view-axis","z"},{"--unknown"},{"--points","nan,0,0"}}) {
			run(command+QStringList{"--overwrite"}+extra,false); expect(read(output) == after && read(input) == before,"invalid CLI preserves files",dialect);
		}
	}
	LevelMapDocument pair; tests::createGeometryFixture(2,&pair,&error); setLevelMapSelection(&pair,{{LevelMapSelectionKind::QuakeBrush,0},{LevelMapSelectionKind::QuakeBrush,1}});
	const auto originalPair = bytes(pair); auto exact = pair; LevelBrushMergePlan plan;
	expect(!prepareLevelBrushMerge(exact,{},&plan,&error) && bytes(exact) == originalPair,"exact merge still refuses separated brushes");
	LevelMapClipReport report; auto joined = pair;
	expect(wrapLevelMapSelection(&joined,{},"studio/wrap",&report,&error) && report.wrapped && joined.brushes.size() == 1
		&& joined.brushes[0].id == 1 && joined.brushes[0].mins.x == -16 && joined.brushes[0].maxs.x == 64,"explicit wrap fills gap, retains last selected identity",error);
	auto reversed = pair; setLevelMapSelection(&reversed,{{LevelMapSelectionKind::QuakeBrush,1},{LevelMapSelectionKind::QuakeBrush,0}});
	expect(wrapLevelMapSelection(&reversed,{},"studio/wrap",nullptr,&error) && reversed.brushes[0].id == 0,"destination follows selection order rather than document order",error);
	auto overlapping = pair; setLevelMapSelection(&overlapping,{{LevelMapSelectionKind::Entity,0},{LevelMapSelectionKind::QuakeBrush,0}});
	expect(wrapLevelMapSelection(&overlapping,{},"studio/wrap",nullptr,&error) && overlapping.brushes[0].id == 0,"explicit primary brush wins after entity expansion",error);
	auto linkedPair = pair; QString pairGroup, pairCopy;
	expect(createLevelSceneNode(&linkedPair,LevelSceneNodeKind::Group,"pair",{},&pairGroup,&error)
		&& assignLevelSceneObjects(&linkedPair,pairGroup,{"brush:0","brush:1"},&error)
		&& createLinkedLevelGroup(&linkedPair,pairGroup,{0,128,0,true},&pairCopy,&error),"create linked pair",error);
	setLevelMapSelection(&linkedPair,pair.selection); const auto pairBefore = bytes(linkedPair); const auto pairHistory = linkedPair.undoStack.size();
	expect(wrapLevelMapSelection(&linkedPair,{},"studio/wrap",nullptr,&error) && linkedPair.brushes.size() == 2 && linkedPair.undoStack.size() == pairHistory+1,"multi-brush wrap propagates topology to linked copy",error);
	expect(undoLevelMapEdit(&linkedPair,&error) && bytes(linkedPair) == pairBefore,"multi-brush linked wrap undo",error);
	for (const int count : {64,65}) {
		LevelMapDocument many; tests::createGeometryFixture(count,&many,&error); QVector<LevelMapSelectionRef> selection;
		for (const auto& brush : many.brushes) { selection << LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush,brush.id}; }
		setLevelMapSelection(&many,selection); const auto before = bytes(many);
		const bool made = wrapLevelMapSelection(&many,{},"studio/wrap",&report,&error);
		expect(count == 64 ? made && many.brushes.size() == 1 && report.changed == count
			: !made && bytes(many) == before && many.undoStack.isEmpty(),"whole-brush budget boundary",error);
	}
	QString group; expect(createLevelSceneNode(&pair,LevelSceneNodeKind::Group,"destination",{},&group,&error)
		&& assignLevelSceneObjects(&pair,group,{"brush:1"},&error),"assign destination group",error);
	auto grouped = pair; expect(wrapLevelMapSelection(&grouped,{},"studio/wrap",nullptr,&error) && levelSceneNode(grouped.scene,group)->objects.contains("brush:1"),"destination group survives wrap",error);
	expect(setLevelSceneLocked(&pair,group,true,&error),"lock destination",error); const auto lockedBytes = bytes(pair); const auto history = pair.undoStack.size();
	expect(!clipLevelMapPoints(&pair,{{100,0,0,true}},2,1,LevelMapClipKeep::Back,options,&report,&error)
		&& bytes(pair) == lockedBytes && pair.undoStack.size() == history && report.changed == 0,"locked source refuses whole wrap",error);
	expect(!clipLevelMapPoints(&pair,{{48,0,0,true},{48,1,0,true},{48,0,1,true}},2,1,LevelMapClipKeep::Both,options,&report,&error)
		&& bytes(pair) == lockedBytes && pair.undoStack.size() == history,"failed protected cut never falls back",error);
	LevelMapDocument seed; tests::createGeometryFixture(1,&seed,&error); const auto brushText = seed.brushes[0].sourceLines.join('\n');
	const auto entityMap = QString("{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"func_wall\"\n%1\n}\n{\n\"classname\" \"func_detail\"\n%1\n}\n").arg(brushText);
	LevelMapDocument owners; expect(loadLevelMapBytes({temp.filePath("owners.map"),{},"idtech3"},entityMap.toUtf8(),&owners,&error),"load different owners",error);
	setLevelMapSelection(&owners,{{LevelMapSelectionKind::QuakeBrush,0},{LevelMapSelectionKind::QuakeBrush,1}}); const auto ownersBefore = bytes(owners);
	expect(wrapLevelMapSelection(&owners,{{64,0,0,true}},"studio/wrap",nullptr,&error) && owners.brushes.size() == 1 && owners.brushes[0].entityId == 2 && owners.entities.size() == 2,"last owner retained, emptied brush entity removed",error);
	expect(undoLevelMapEdit(&owners,&error) && bytes(owners) == ownersBefore,"cross-owner undo restores entities",error);
	auto linked = seed; QString originalGroup, copyGroup;
	expect(createLevelSceneNode(&linked,LevelSceneNodeKind::Group,"hull",{},&originalGroup,&error)
		&& assignLevelSceneObjects(&linked,originalGroup,{"brush:0"},&error)
		&& createLinkedLevelGroup(&linked,originalGroup,{0,128,0,true},&copyGroup,&error),"create linked hull fixture",error);
	setLevelMapSelection(&linked,{{LevelMapSelectionKind::QuakeBrush,0}}); const auto linkedBefore = bytes(linked); const auto linkedDepth = linked.undoStack.size();
	expect(wrapLevelMapSelection(&linked,{{64,0,0,true}},"studio/wrap",nullptr,&error) && linked.brushes.size() == 2
		&& linked.brushes[0].maxs.x == 64 && linked.brushes[1].maxs.x == 64 && linked.undoStack.size() == linkedDepth+1,"hull propagates to linked instance in one undo",error);
	expect(undoLevelMapEdit(&linked,&error) && bytes(linked) == linkedBefore,"linked wrap undo is exact",error);
	auto invalid = seed; setLevelMapSelection(&invalid,{{LevelMapSelectionKind::QuakeBrush,0}}); const auto invalidBefore = bytes(invalid);
	for (const auto& p : QVector<LevelMapVec3>{{32769,0,0,true},{0,std::numeric_limits<double>::infinity(),0,true},{0,0,0,false}}) {
		expect(!wrapLevelMapSelection(&invalid,{p},"studio/wrap",nullptr,&error) && bytes(invalid) == invalidBefore && invalid.undoStack.isEmpty(),"invalid wrap coordinates are atomic",error);
	}
	int checkpoints = 0; bool cancelled = false; const LevelPlacementControl control{[&] { return ++checkpoints >= 3; },{}};
	try { const detail::PlacementControlScope scope(control); wrapLevelMapSelection(&invalid,{{64,0,0,true}},"studio/wrap",nullptr,&error); }
	catch (const detail::PlacementCancelled&) { cancelled = true; }
	expect(cancelled && bytes(invalid) == invalidBefore && invalid.undoStack.isEmpty(),"cancelled hull leaves source and history untouched");
	return ok ? 0 : 1;
}
