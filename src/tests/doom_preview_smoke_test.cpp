#include "core/doom_preview_geometry.h"
#include "core/level_dependencies.h"
#include "core/package_staging.h"
#include "tests/doom_preview_test_helpers.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace d = vibestudio::tests::doom;
bool expect(bool condition, const char* label, const QString& error = {}) { if (!condition) { std::cerr << label << ": " << error.toStdString() << '\n'; } return condition; }
bool near(double a, double b) { return std::abs(a-b) < 1e-5; }
double area(const DoomPreviewGeometry& geometry)
{
	double result = 0;
	for (const auto& p : geometry.polygons) {
		if (p.target.kind != LevelMaterialKind::SectorFloor) { continue; }
		for (int i = 1; i + 1 < p.points.size(); ++i) { const auto& a = p.points[0]; const auto& b = p.points[i]; const auto& c = p.points[i+1]; result += ((b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x)) * 0.5; }
	}
	return result;
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temp; bool ok = temp.isValid(); QString error;
	auto map = d::topology(); d::loop(map, {{0,0},{0,128},{128,128},{128,0}});
	auto geometry = buildDoomPreviewGeometry(map);
	ok &= expect(geometry.walls == 4 && geometry.floors == 1 && geometry.ceilings == 1 && near(area(geometry), 16384) && geometry.warnings.isEmpty(), "closed room floors and ceilings");
	d::loop(map, {{32,32},{96,32},{96,96},{32,96}}); // hole, reverse winding
	d::loop(map, {{160,0},{160,64},{224,64},{224,0}}); // disconnected island
	geometry = buildDoomPreviewGeometry(map);
	ok &= expect(near(area(geometry), 16384) && geometry.warnings.isEmpty(), "holes and islands retain exact area");
	for (const auto& p : geometry.polygons) {
		if (p.target.kind != LevelMaterialKind::SectorFloor) { continue; }
		double x=0,y=0; for (const auto& v : p.points) { x+=v.x; y+=v.y; }
		x/=p.points.size(); y/=p.points.size(); ok &= expect(!(x>32 && x<96 && y>32 && y<96), "no floor polygons inside hole");
		for (int i=0;i<p.points.size();++i) { ok &= expect(near(p.uv[i].x(),p.points[i].x/64) && near(p.uv[i].y(),-p.points[i].y/64), "world aligned flat UVs"); }
	}
	auto concave = d::topology(); d::loop(concave, {{0,0},{0,128},{64,128},{64,64},{128,64},{128,0}});
	ok &= expect(near(area(buildDoomPreviewGeometry(concave)),12288), "concave sector");
	auto slant = d::topology(); d::loop(slant, {{0,0},{32,64},{64,0}});
	ok &= expect(near(area(buildDoomPreviewGeometry(slant)),2048), "triangular slab endpoints");
	auto open = map; open.doomLinedefs.removeFirst();
	const auto omitted = buildDoomPreviewGeometry(open);
	ok &= expect(omitted.floors == 0 && !omitted.warnings.isEmpty(), "open boundary cannot silently fill");
	auto crossing = d::topology(); d::loop(crossing, {{0,0},{64,64},{0,64},{64,0}});
	ok &= expect(buildDoomPreviewGeometry(crossing).floors == 0, "crossing boundary rejected");
	d::loop(crossing,{{128,32},{128,64},{160,64},{160,32}});
	ok &= expect(buildDoomPreviewGeometry(crossing).floors == 0,"crossing exactly on another event level rejected");
	auto horizontalCrossing=d::topology(); d::loop(horizontalCrossing,{{0,0},{0,64},{64,64},{64,32},{-32,32},{-32,0}});
	ok &= expect(buildDoomPreviewGeometry(horizontalCrossing).floors==0,"horizontal edge crossing rejected");
	auto internal = map; auto line = internal.doomLinedefs[0]; line.id = 99; line.startVertex = 0; line.endVertex = 2; line.frontSidedef = line.backSidedef = 0;
	internal.doomSidedefs[0].middleTexture = QStringLiteral("-"); internal.doomLinedefs << line;
	ok &= expect(near(area(buildDoomPreviewGeometry(internal)),16384), "same-sector internal line is not a hole");
	ok &= expect(buildLevelMapPreviewMesh(map, {.triangleLimit=2}).truncated, "triangle budget");
	LevelMapPreviewMeshOptions cancelled; cancelled.isCancelled=[] { return true; };
	ok &= expect(buildLevelMapPreviewMesh(map,cancelled).cancelled,"geometry cancellation");
	const auto mesh = buildLevelMapPreviewMesh(map);
	ok &= expect(mesh.floors == 1 && mesh.ceilings == 1 && mesh.materialTargets.size() == mesh.triangles,"flattened source provenance");
	for (const auto& target : mesh.materialTargets) { QString value; ok &= expect(sampleLevelMaterial(map,target,&value,&error),"all camera targets sample",error); }
	auto uv = d::topology(); d::loop(uv, {{0,0},{0,128},{128,128},{128,0}});
	uv.doomSidedefs[0].offsetX=8; uv.doomSidedefs[0].offsetY=16;
	LevelMapPreviewMeshOptions sizes; sizes.textureSizes.insert(QStringLiteral("textures/stone"),QSize(64,64));
	geometry = buildDoomPreviewGeometry(uv,sizes);
	ok &= expect(near(geometry.polygons[0].uv[0].x(),.125) && near(geometry.polygons[0].uv[0].y(),2.25) && near(geometry.polygons[0].uv[2].y(),.25),"wall offsets and top anchor");
	uv.doomLinedefs[0].flags=16; geometry=buildDoomPreviewGeometry(uv,sizes);
	ok &= expect(near(geometry.polygons[0].uv[0].y(),1.25) && near(geometry.polygons[0].uv[2].y(),-.75),"bottom unpegged anchor");
	// Both visible sides of a masked middle wall retain independent materials.
	auto portal = d::topology(); auto second = portal.doomSectors[0]; second.id=1; second.floorHeight=32; second.ceilingHeight=96; portal.doomSectors << second;
	d::loop(portal, {{0,0},{0,128},{128,128},{128,0}});
	portal.doomLinedefs.resize(1); auto back = portal.doomSidedefs[0]; back.id=4; back.sector=1; back.middleTexture=QStringLiteral("BACKMID"); portal.doomSidedefs << back;
	portal.doomLinedefs[0].backSidedef=4; portal.doomSidedefs[0].middleTexture=QStringLiteral("FENCE");
	portal.doomSidedefs[0].upperTexture=portal.doomSidedefs[0].lowerTexture=QStringLiteral("STONE");
	sizes.textureSizes.insert(QStringLiteral("textures/fence"),QSize(64,32)); sizes.textureSizes.insert(QStringLiteral("textures/backmid"),QSize(64,32));
	const auto masked=buildDoomPreviewGeometry(portal,sizes); int middleCount=0;
	for (const auto& p : masked.polygons) { if (p.target.kind==LevelMaterialKind::WallMiddle) { ++middleCount; ok &= expect(near(p.points[0].z,64) && near(p.points[2].z,96) && near(p.uv[0].y(),1) && near(p.uv[2].y(),0),"masked wall clipped to one texture height and opening"); } }
	ok &= expect(middleCount==2 && masked.walls==4,"two independent middle sides plus front steps");
	for (const auto& game : {QStringLiteral("doom"),QStringLiteral("hexen")}) {
		const auto path=temp.filePath(game+QStringLiteral(".wad")); const auto bytes=d::fixture(game);
		ok &= expect(d::write(path,bytes),"write fixture");
		LevelMapDocument loaded; PackageArchive archive;
		ok &= expect(loadLevelMap({path,QStringLiteral("MAP01"),{}},&loaded,&error) && archive.load(path,&error),"real WAD fixture",error);
		const auto assets=resolveLevelPreviewAssets(loaded,archive);
		const auto* flat=d::material(assets,QStringLiteral("flats/stone")); const auto* wall=d::material(assets,QStringLiteral("textures/stone"));
		ok &= expect(assets.complete && assets.readyCount()==3 && flat && wall,"flat and composite namespaces",levelPreviewAssetsText(assets));
		if (flat && wall) {
			ok &= expect(flat->image.pixelColor(0,0).red()==120 && wall->image.pixelColor(0,0).red()==20 && wall->image.pixelColor(8,8).red()==220 && wall->image.pixelColor(12,8).red()==20,"palette, patch order, offsets and transparent holes");
			ok &= expect(wall->sourceSize==QSize(64,64) && wall->status==QStringLiteral("composite"),"composite metadata");
		}
		const auto textured=buildLevelMapPreviewMesh(loaded,{.textureSizes=levelPreviewTextureSizes(assets)});
		ok &= expect(textured.floors==1 && textured.ceilings==1 && levelPreviewSurfaceImages(textured.mesh,assets).size()==3,"camera image handoff");
		const auto dependencies=inspectLevelDependencies(loaded,archive);
		ok &= expect(dependencies.complete && dependencies.problemCount==0 && dependencies.resolvedPaths.size()==7 && !dependencies.exportSupported && !dependencies.canExport(),"Doom dependency review records inputs and withholds unsupported subset export",levelDependencyReportText(dependencies));
		QSet<qint64> stoneOccurrences;
		for (const auto& dependency : dependencies.dependencies) {
			if (dependency.kind==QStringLiteral("doom-input") && dependency.reference==QStringLiteral("STONE")) { stoneOccurrences.insert(dependency.sourceOrdinal); }
		}
		ok &= expect(stoneOccurrences.size()==2,"dependency review distinguishes flat and patch occurrences");
		ok &= expect(wall && wall->inputs.size()==5 && levelPreviewAssetsText(assets).contains(QStringLiteral("PNAMES")),"material details retain texture tables, patches and palette");
		PackageStagingModel staging;
		ok &= expect(staging.loadBaseArchive(archive,&error),"staging load",error);
		const auto replacement=temp.filePath(QStringLiteral("flat-replacement.bin"));
		ok &= d::write(replacement,QByteArray(4096,char(42)));
		int flatOrdinal=-1,patchOrdinal=-1;
		for (const auto& entry : archive.entries()) { if (entry.virtualPath==QStringLiteral("STONE")) { if (entry.typeHint==QStringLiteral("wad-flat")) {flatOrdinal=int(entry.sourceOrdinal);} if (entry.typeHint==QStringLiteral("wad-patch")) {patchOrdinal=int(entry.sourceOrdinal);} } }
		ok &= expect(flatOrdinal>=0 && patchOrdinal>=0 && staging.replaceOccurrence(flatOrdinal,replacement,&error),"stage exact flat occurrence",error);
		const auto patchReplacement=temp.filePath(QStringLiteral("patch-replacement.bin")); ok &= d::write(patchReplacement,d::patch(64,64,60));
		ok &= expect(staging.replaceOccurrence(patchOrdinal,patchReplacement,&error),"stage exact patch occurrence",error);
		const auto staged=resolveLevelPreviewAssets(loaded,PackageStagingArchive(staging));
		const auto* stagedFlat=d::material(staged,QStringLiteral("flats/stone")); const auto* stagedWall=d::material(staged,QStringLiteral("textures/stone"));
		ok &= expect(stagedFlat && stagedWall && stagedFlat->image.pixelColor(0,0).red()==42 && stagedWall->image.pixelColor(0,0).red()==60,"independent staged flat and wall composition",levelPreviewAssetsText(staged));
		ok &= expect(inspectLevelDependencies(loaded,PackageStagingArchive(staging)).problemCount==0,"dependency review uses staged occurrences");
		ok &= expect(flat && wall && flat->image.pixelColor(0,0).red()==120 && wall->image.pixelColor(0,0).red()==20,"base package images unchanged");
		LevelMaterialPaintPlan paint;
		ok &= expect(prepareLevelMaterialPaint(loaded,{{LevelMaterialKind::SectorFloor,0}},QStringLiteral("ALTFLAT"),&paint,&error) && commitLevelMaterialPaint(&loaded,paint,&error),"floor camera edit service",error);
		const auto after=resolveLevelPreviewAssets(loaded,archive); const auto* alternate=d::material(after,QStringLiteral("flats/altflat"));
		ok &= expect(alternate && alternate->image.pixelColor(0,0).red()==200 && undoLevelMapEdit(&loaded,&error) && serializeLevelMap(loaded).bytes==bytes,"flat edit, resolve and exact undo",error);
		LevelPreviewAssetOptions tiny; tiny.totalReadByteLimit=4;
		const auto capped=resolveLevelPreviewAssets(loaded,archive,tiny);
		ok &= expect(!capped.complete && capped.readyCount()==0 && capped.readBytes<=4,"shared aggregate read budget");
		tiny={}; tiny.imageByteLimit=1;
		ok &= expect(resolveLevelPreviewAssets(loaded,archive,tiny).readyCount()==0,"image budget");
		int ticks=0;
		ok &= expect(resolveLevelPreviewAssets(loaded,archive,{},[&](int,int) {return ++ticks<5;}).cancelled,"material cancellation");
		if (argc>1) {
			QProcess cli; cli.start(QString::fromLocal8Bit(argv[1]),{QStringLiteral("--cli"),QStringLiteral("map"),QStringLiteral("materials"),path,QStringLiteral("--map-name"),QStringLiteral("MAP01"),QStringLiteral("--package"),path,QStringLiteral("--geometry"),QStringLiteral("--json")});
			ok &= expect(cli.waitForFinished(30000) && cli.exitCode()==0,"real Doom material CLI",QString::fromUtf8(cli.readAllStandardError()));
			const auto result=QJsonDocument::fromJson(cli.readAllStandardOutput()).object();
			const auto json=result.value(QStringLiteral("materials")).toObject();
			ok &= expect(json.value(QStringLiteral("readyMaterials")).toInt()==3,"CLI resolves all Doom materials");
			ok &= expect(result.value(QStringLiteral("geometry")).toObject().value(QStringLiteral("floors")).toInt()==1,"CLI shares camera geometry");
			cli.start(QString::fromLocal8Bit(argv[1]),{QStringLiteral("--cli"),QStringLiteral("map"),QStringLiteral("dependencies"),path,QStringLiteral("--map-name"),QStringLiteral("MAP01"),QStringLiteral("--package"),path,QStringLiteral("--json")});
			ok &= expect(cli.waitForFinished(30000) && cli.exitCode()==0,"Doom dependency CLI succeeds",QString::fromUtf8(cli.readAllStandardError()));
			const auto dependencyJson=QJsonDocument::fromJson(cli.readAllStandardOutput()).object();
			ok &= expect(QString::fromUtf8(QJsonDocument(dependencyJson).toJson()).contains(QStringLiteral("PNAMES")),"CLI dependency evidence includes composition inputs");
		}
		if (game==QStringLiteral("doom")) {
			for (int mode=0;mode<6;++mode) {
				auto broken=d::assets();
				if (mode==0) { d::put32(broken.last().bytes,4,0x7fffffff); }
				if (mode==1) { d::put16(broken.last().bytes,12+26,99); }
				if (mode==2) { d::put16(broken.last().bytes,12+12,5000); }
				if (mode==3) { broken[7].bytes=QByteArray(9,'x'); }
				if (mode==4) { auto duplicate=broken.last(); duplicate.name="TEXTURE2"; broken << duplicate; }
				if (mode==5) { broken.insert(3,broken[2]); }
				const auto invalidPath=temp.filePath(QStringLiteral("invalid-%1.wad").arg(mode)); ok &= d::write(invalidPath,d::wad(broken));
				PackageArchive invalid; ok &= invalid.load(invalidPath,&error);
				const auto failed=resolveLevelPreviewAssets(loaded,invalid); const auto* failedMaterial=d::material(failed,mode==5?QStringLiteral("flats/stone"):QStringLiteral("textures/stone"));
				ok &= expect(failedMaterial && !failedMaterial->ready() && failed.problemCount()>0,"malformed or ambiguous asset withholds entire result",levelPreviewAssetsText(failed));
			}
		}
	}
	return ok?0:1;
}
