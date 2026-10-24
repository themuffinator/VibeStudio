#include "core/map_geometry.h"
#include "core/studio_settings.h"
#include "tests/level_geometry_test_helpers.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
int main(int argc,char** argv)
{
	QCoreApplication app(argc,argv); QTemporaryDir temp; if (!temp.isValid() || argc != 2) { return 1; }
	bool ok = true; QString error;
	const auto expect = [&](bool value,const char* message,const QString& detail = {}) { if (!value) { ok = false; std::cerr << message << ": " << detail.toStdString() << '\n'; } return value; };
	const auto read = [](const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; };
	const auto run = [&](QStringList arguments,bool success = true) {
		QProcess p; p.setWorkingDirectory(temp.path()); p.start(QString::fromLocal8Bit(argv[1]),QStringList{"--cli","--json","--settings-file",temp.filePath("settings.ini")}+arguments);
		const bool finished = p.waitForStarted() && p.waitForFinished(30000); const auto bytes = p.readAllStandardOutput();
		expect(finished && p.exitStatus() == QProcess::NormalExit && (p.exitCode() == 0) == success,"CLI exit",QString::fromUtf8(bytes+p.readAllStandardError()));
		if (!finished) { p.kill(); p.waitForFinished(); } return QJsonDocument::fromJson(bytes).object();
	};
	const auto dry = run({"editor","clipper","--set","material=custom","--set","customTexture=studio/cut","--set","resetPoints=off","--dry-run"});
	expect(dry["values"].toObject()["material"] == "custom" && !QFile::exists(temp.filePath("settings.ini")),"preference dry run does not create settings");
	run({"editor","clipper","--set","material=custom","--set","customTexture=studio/cut","--set","resetPoints=off","--set","resetSide=on","--set","twoPointsInPlan=on","--set","doubleClick=split","--set","wrapMerge=on"});
	const auto savedPreferences = read(temp.filePath("settings.ini"));
	const auto values = run({"editor","clipper"})["values"].toObject();
	expect(values["resetPoints"] == false && values["resetSide"] == true && values["doubleClick"] == "split" && values["wrapMerge"] == true,"preference persistence");
	for (const auto& bad : QVector<QStringList>{{"--set","material=caulk"},{"--set","customTexture=bad name"},{"--set","resetPoints=maybe"},{"--set","unknown=on"},
		{"--set","resetSide=on","--set","resetSide=off"},{"--reset","--set","material=inherit"},{"--reset=true"},{"--dry-run"},{"extra"}}) {
		run(QStringList{"editor","clipper"}+bad,false); expect(read(temp.filePath("settings.ini")) == savedPreferences,"invalid preferences leave settings untouched");
	}
	run({"editor","clipper","--reset"}); expect(run({"editor","clipper"})["values"].toObject() == levelClipperPreferencesJson({}),"reset restores defaults");
	LevelClipperPreferences prefs; const auto defaults = prefs;
	expect(!updateLevelClipperPreferences({{"resetSide",true},{"resetPoints","off"}},&prefs,&error) && prefs == defaults,"strict typed updates are atomic");
	LevelMapDocument seed; if (!tests::createGeometryFixture(1,&seed,&error)) { return 1; }
	for (const auto& style : QStringList{"classic","classic-flags","valve220","valve220-flags","brushDef","brushDef3"}) {
		const bool primitive = style.startsWith("brushDef"), flags = primitive || style.endsWith("-flags");
		QString source = "{\n\"classname\" \"worldspawn\"\n{\n";
		if (primitive) { source += style+"\n{\n"; }
		const auto xyz = [](const LevelMapVec3& p) { return QStringLiteral("( %1 %2 %3 )").arg(p.x).arg(p.y).arg(p.z); };
		for (const auto& face : seed.brushes[0].faces) {
			QString plane = xyz(face.p0)+' '+xyz(face.p1)+' '+xyz(face.p2);
			const auto normal = planeFromPoints(face.p0,face.p1,face.p2);
			if (style == "brushDef3") { plane = QStringLiteral("( %1 %2 %3 %4 )").arg(normal.normalX).arg(normal.normalY).arg(normal.normalZ).arg(-normal.distance); }
			source += plane;
			if (primitive) { source += " ( ( 0.0078125 0 0.25 ) ( 0 0.0078125 -0.125 ) ) studio/original"; }
			else { source += " studio/original"; source += style.startsWith("valve") ? " [ 1 0 0 16 ] [ 0 1 0 -8 ] 0 0.5 0.5" : " 16 -8 15 0.5 0.5"; }
			if (flags) { source += " 1 4 7"; } source += '\n';
		}
		if (primitive) { source += "}\n"; } source += "}\n}\n";
		LevelMapDocument document;
		if (!expect(loadLevelMapBytes({temp.filePath(style+".map"),{},"idtech3"},source.toUtf8(),&document,&error),"parse format fixture",style+error)) { continue; }
		setLevelMapSelection(&document,{{LevelMapSelectionKind::QuakeBrush,0}},&error);
		const auto original = serializeLevelMap(document).bytes; const auto primitiveKind = document.brushes[0].primitiveKind; auto inherited = document;
		const LevelMapVec3 a{0,0,0,true},b{0,1,0,true},c{0,0,1,true};
		expect(clipLevelMapSelection(&inherited,a,b,c,LevelMapClipKeep::Both,nullptr,&error),"default cut",style+error);
		for (const auto& brush : inherited.brushes) { for (const auto& f : brush.faces) { expect(f.textureName == "studio/original" && f.surfaceFlags == (flags ? 4 : 0),"inherited material/flags",style); } }
		expect(clipLevelMapSelection(&document,a,b,c,LevelMapClipKeep::Both,nullptr,&error,nullptr,{"studio/cut"}),"explicit material cut",style+error);
		int cuts = 0;
		for (const auto& brush : document.brushes) {
			expect(solveBrushGeometry(brush.faces).solved && brush.primitiveKind == primitiveKind,"closed brush retains source dialect",style);
			for (const auto& f : brush.faces) { if (f.textureName == "studio/cut") { ++cuts; expect(f.contentFlags == (flags ? 1 : 0) && f.surfaceFlags == 0 && f.surfaceValue == 0,"explicit cut clears surface flags, preserves contents",style); } }
			for (const auto& line : brush.sourceLines) { if (line.contains("studio/original")) { expect(source.contains(line),"surviving face source text is exact",style); } }
		}
		const auto edited = serializeLevelMap(document).bytes;
		expect(cuts == 2 && document.undoStack.size() == 1,"two new cut faces, one undo step",style);
		expect(undoLevelMapEdit(&document,&error) && serializeLevelMap(document).bytes == original,"undo original bytes",style+error);
		expect(redoLevelMapEdit(&document,&error) && serializeLevelMap(document).bytes == edited,"redo cut bytes",style+error);
		LevelMapDocument loaded; expect(loadLevelMapBytes({temp.filePath("reload.map"),{},"idtech3"},edited,&loaded,&error) && loaded.brushes.size() == 2,"format round trip",style+error);
		for (const auto& name : QStringList{"bad name","a//b","a/*b","a*/b","a\"b","a\\b","a\nb",QString(1025,'x')}) {
			auto invalid = inherited; const auto before = serializeLevelMap(invalid).bytes; int changed = 99;
			expect(!clipLevelMapSelection(&invalid,a,b,c,LevelMapClipKeep::Back,&changed,&error,nullptr,{name}) && changed == 0 && serializeLevelMap(invalid).bytes == before,"invalid material refuses entire edit",style);
		}
		const auto input = temp.filePath(style+".map"), output = temp.filePath(style+"-cut.map"); QFile f(input);
		if (!expect(f.open(QIODevice::WriteOnly) && f.write(source.toUtf8()) == source.toUtf8().size(),"write CLI fixture",style)) { continue; } f.close();
		const QStringList command{"map","clip",input,"--object","brush:0","--axis","x","--at","0","--keep","both","--cut-texture","studio/cut","--output",output};
		auto report = run(command+QStringList{"--dry-run"}); expect(report["cutTexture"] == "studio/cut" && !QFile::exists(output),"CLI dry run",style);
		run(command); expect(read(output) == edited,"GUI/core/CLI same cut",style);
		for (const auto& invalid : QVector<QStringList>{{"--cut-texture","studio/other"},{"--typo"},{"--points","0,0,0 0,1,0 0,0,1"}}) {
			run(command+QStringList{"--overwrite"}+invalid,false); expect(read(output) == edited && read(input) == source.toUtf8(),"bad CLI options protect files",style);
		}
	}
	return ok ? 0 : 1;
}
