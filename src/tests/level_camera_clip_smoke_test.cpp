#include "core/level_clip_camera.h"
#include "core/level_gestures.h"
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
int main(int argc,char** argv)
{
	QCoreApplication app(argc,argv); QTemporaryDir temp; if (!temp.isValid() || argc != 2) { return 1; }
	bool ok = true; QString error;
	const auto expect = [&](bool value,const char* label,const QString& detail = {}) { if (!value) { ok = false; std::cerr << label << ": " << detail.toStdString() << '\n'; } return value; };
	const auto bytes = [](const auto& map) { return serializeLevelMap(map).bytes; };
	const auto put = [](const QString& path,const QByteArray& data) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(data) == data.size(); };
	const auto read = [](const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; };
	const auto run = [&](const QStringList& args,bool success = true) {
		QProcess p; p.setWorkingDirectory(temp.path()); p.start(QString::fromLocal8Bit(argv[1]),QStringList{"--cli","--json","--settings-file",temp.filePath("settings.ini")}+args);
		const bool finished = p.waitForStarted() && p.waitForFinished(30000); const auto output = p.readAllStandardOutput();
		expect(finished && p.exitStatus() == QProcess::NormalExit && (p.exitCode() == 0) == success,"CLI status",QString::fromUtf8(output+p.readAllStandardError()));
		if (!finished) { p.kill(); p.waitForFinished(); } return QJsonDocument::fromJson(output).object();
	};
	const LevelMapVec3 low{-16,-16,-16,true},high{16,16,16,true},eye{-128,-64,32,true},forward{1,0,0,true};
	int axis = 2,direction = 1;
	for (const double z : {0.,16.,-16.}) {
		const QVector<LevelMapVec3> points{{0,-8,z,true},{0,8,z,true}};
		expect(levelCameraClipAxis(points,eye,forward,low,high,&axis,&direction,false,&error),"choose view or outer-face plane",error);
		expect(axis == (z == 0 ? 0 : 2) && direction == (z == 16 ? 1 : -1),"NRC view direction and cut-worthy outer-face oracle");
	}
	const QVector<LevelMapVec3> parallel{{-8,0,0,true},{8,0,0,true}};
	expect(levelCameraClipAxis(parallel,eye,forward,low,high,&axis,&direction,false,&error) && axis == 1 && direction == -1,"parallel points use near-plane screen direction",error);
	const QVector<LevelMapVec3> vertical{{0,0,-8,true},{0,0,8,true}}; axis = 2; direction = 1;
	expect(levelCameraClipAxis(vertical,eye,forward,low,high,&axis,&direction,true,&error) && axis == 0 && direction == -1,"parallel stored axis repairs from current camera",error);
	axis = 0; direction = 1; const QVector<LevelMapVec3> across{{0,-8,0,true},{0,8,0,true}};
	expect(levelCameraClipAxis(across,eye,{0,0,-1,true},low,high,&axis,&direction,true,&error) && axis == 0 && direction == 1,"existing non-parallel axis survives a changed camera",error);
	expect(levelCameraClipAxis(across,eye,{1,1,1,true},{},{},&axis,&direction,false,&error) && axis == 0 && direction == -1,"dominant ties match upstream X-first convention",error);
	const auto refuse = [&](const QVector<LevelMapVec3>& points,LevelMapVec3 e,LevelMapVec3 f,LevelMapVec3 l,LevelMapVec3 h) {
		axis = 1; direction = -1;
		expect(!levelCameraClipAxis(points,e,f,l,h,&axis,&direction,false,&error) && axis == 1 && direction == -1 && !error.isEmpty(),"invalid camera plane leaves outputs unchanged",error);
	};
	refuse(parallel,{-128,0,0,true},forward,low,high);
	refuse(parallel,{128,0,0,true},forward,low,high);
	refuse(across,eye,{0,0,0,true},low,high);
	refuse(across,eye,{std::numeric_limits<double>::infinity(),0,0,true},low,high);
	refuse({across[0],across[0]},eye,forward,low,high);
	refuse(across,eye,forward,high,low);
	refuse(across,eye,forward,low,{});
	refuse(across,eye,forward,{-1e308,-1e308,-1e308,true},{1e308,1e308,1e308,true});
	refuse(parallel,{-1e308,-1e308,0,true},{1,1,0,true},low,high);
	// Both output directions are reproducible independently of a live camera.
	for (const auto& dialect : QStringList{"classic","valve220","brushDef","brushDef3"}) {
		LevelMapDocument source; expect(loadLevelMapBytes({temp.filePath(dialect+".map"),{},"idtech3"},tests::surfaceFixture(dialect),&source,&error),"load format fixture",error);
		setLevelMapSelection(&source,{{LevelMapSelectionKind::QuakeBrush,0}}); const auto before = bytes(source);
		const QVector<LevelMapVec3> points{{32,32,56,true},{32,64,56,true}}; LevelMapVec3 mins,maxs; levelMapSelectionBounds(source,&mins,&maxs);
		axis = 0; direction = 1;
		expect(levelCameraClipAxis(points,{-200,48,100,true},forward,mins,maxs,&axis,&direction,false,&error) && axis == 2 && direction == 1,"top face redirects plane through brush",dialect+error);
		auto expected = source; expect(clipLevelMapPoints(&expected,points,axis,direction,LevelMapClipKeep::Both,{},nullptr,&error),"shared cut with chosen plane",dialect+error);
		expect(expected.brushes.size() == 2 && expected.undoStack.size() == 1,"two valid pieces, one undo",dialect);
		const auto after = bytes(expected); expect(undoLevelMapEdit(&expected,&error) && bytes(expected) == before && redoLevelMapEdit(&expected,&error) && bytes(expected) == after,"camera-plane exact undo redo",dialect);
		const auto input = temp.filePath(dialect+".map"), output = temp.filePath(dialect+"-split.map"), replay = temp.filePath(dialect+"-replay.map"); put(input,before);
		const QStringList command{"map","clip",input,"--object","brush:0","--points","32,32,56 32,64,56","--camera-eye","-200,48,100","--camera-forward","1,0,0","--keep","both","--output",output};
		const auto dry = run(command+QStringList{"--dry-run"});
		expect(dry["cameraPlane"] == true && dry["planeAxis"] == "z" && dry["planeDirection"] == 1 && !QFile::exists(output),"camera dry run reports exact extrusion");
		run(command); expect(read(output) == after,"CLI camera cut matches shared geometry",dialect);
		run({"map","clip",input,"--object","brush:0","--points","32,32,56 32,64,56","--view-axis","z","--view-sign","1","--keep","both","--output",replay});
		expect(read(replay) == after,"explicit axis/sign reproduces camera result",dialect);
		for (const auto& extra : QVector<QStringList>{{"--camera-forward","0,0,0"},{"--view-axis","z"},{"--view-sign","-1"},{"--unknown"}}) {
			run(command+QStringList{"--overwrite"}+extra,false); expect(read(output) == after && read(input) == before,"malformed camera options preserve files",dialect);
		}
		for (const auto& bad : QVector<QStringList>{{"--camera-eye","-200,48,100"},{"--camera-forward","1,0,0"},
			{"--camera-eye","-200,48,100","--camera-forward","0,0,0"},{"--camera-eye","nan,0,0","--camera-forward","1,0,0"},
			{"--view-axis","z","--view-sign","0"},{"--view-sign","1"}}) {
			run(QStringList{"map","clip",input,"--object","brush:0","--points","32,32,56 32,64,56","--keep","both","--output",output,"--overwrite"}+bad,false);
			expect(read(output) == after,"incomplete or degenerate plane cannot overwrite output",dialect);
		}
		auto reversed = source; expect(clipLevelMapPoints(&reversed,points,2,-1,LevelMapClipKeep::Back,{},nullptr,&error),"negative extrusion cut",error);
		run({"map","clip",input,"--object","brush:0","--points","32,32,56 32,64,56","--view-axis","z","--view-sign","-1","--keep","back","--output",replay,"--overwrite"});
		expect(read(replay) == bytes(reversed) && reversed.brushes[0].mins.x == 32,"negative view sign retains the opposite side",dialect);
	}
	expect(netRadiantCustomLevelControls().camera.clipViewAxis && !trenchBroomLevelControls().camera.clipViewAxis
		&& !familiarLevelControls("netradiant").camera.clipViewAxis,"only audited NRC profile defaults to camera planes");
	const auto preferences = run({"editor","gestures","netradiant-custom"});
	expect(preferences["values"].toObject()["camera.clipViewAxis"] == "on","camera policy is discoverable through shared gesture catalogue");
	const auto dry = run({"editor","gestures","netradiant-custom","--set","camera.clipViewAxis=off","--dry-run"});
	expect(dry["values"].toObject()["camera.clipViewAxis"] == "off"
		&& run({"editor","gestures","netradiant-custom"})["values"].toObject()["camera.clipViewAxis"] == "on","gesture dry run does not write settings");
	run({"editor","gestures","netradiant-custom","--set","camera.clipViewAxis=off"});
	expect(run({"editor","gestures","netradiant-custom"})["values"].toObject()["camera.clipViewAxis"] == "off","camera policy override persists");
	LevelEditorControls effective; QJsonObject normalized;
	expect(applyLevelGestureOverrides(netRadiantCustomLevelControls(),{{"camera.clipViewAxis","off"}},&effective,&normalized,&error)
		&& !effective.camera.clipViewAxis && normalized["camera.clipViewAxis"] == "off","GUI and CLI policy use same typed override",error);
	return ok ? 0 : 1;
}
