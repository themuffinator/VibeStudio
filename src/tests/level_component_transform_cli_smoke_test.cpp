#include "core/level_brush.h"
#include "core/level_patch.h"
#include "tests/level_geometry_test_helpers.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
using namespace vibestudio;
int main(int argc,char** argv)
{
	QCoreApplication app(argc,argv); QTemporaryDir temp; if (argc != 2 || !temp.isValid()) { return 1; }
	bool ok = true; QString error;
	const auto expect = [&](bool value,const char* label,const QString& detail = {}) { if (!value) { ok = false; std::cerr << label << ": " << detail.toStdString() << '\n'; } return value; };
	const auto read = [](const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); };
	const auto run = [&](const QStringList& arguments,bool success = true) {
		QProcess process; process.setWorkingDirectory(temp.path());
		process.start(QString::fromLocal8Bit(argv[1]),QStringList{"--cli","--json","--settings-file",temp.filePath("settings.ini"),"map","transform-components"}+arguments);
		const bool finished = process.waitForStarted() && process.waitForFinished(30000); const auto output = process.readAllStandardOutput();
		expect(finished && process.exitStatus() == QProcess::NormalExit && ((process.exitCode() == 0) == success),"CLI result",QString::fromUtf8(output+process.readAllStandardError()));
		if (!finished) { process.kill(); process.waitForFinished(); } return QJsonDocument::fromJson(output).object();
	};
	LevelMapDocument source; if (!tests::createGeometryFixture(2,&source,&error)) { return 1; }
	LevelMapPatch patch; createLevelPatch({},&patch,&error); addLevelMapPatch(&source,patch,nullptr,&error);
	const auto input = temp.filePath("input.map"), output = temp.filePath("transformed.map");
	expect(saveLevelMapAs(source,input).succeeded(),"save source"); const auto original = read(input);
	const QStringList move{input,"--set","brush:0/face/0","--set","brush:1/edge/0","--set","patch:0/vertex/0,1","--operation","move","--delta=-8,0,0","--output",output};
	auto report = run(move+QStringList{"--dry-run"});
	expect(!QFile::exists(output) && read(input) == original && report["brushes"].toInt() == 2 && report["patches"].toInt() == 1,"mixed dry run");
	run(move); const auto changed = read(output); LevelMapDocument loaded;
	expect(loadLevelMap({output,{},"idtech3"},&loaded,&error),"reload batch",error);
	expect(loaded.patches.first().controlPoints.first().x == source.patches.first().controlPoints.first().x-8 &&
		loaded.patches.first().controlU == source.patches.first().controlU,"patch geometry and UV persistence");
	run(move,false); expect(read(output) == changed,"overwrite protection");
	for (const auto& invalid : QVector<QStringList>{{"--typo"},{"--angle","15"},{"--operation","scale"},{"--uv-lock","maybe"},{"--set","brush:0/vertex/1"},
		{"--set","patch:0/face/0"},{"--set","brush:999/vertex/0"},{"--grid","nan"},{"--grid","-1"},{"--allow-valve220"},{"--set","brush:1/edge/999"},{"extra"}}) {
		run(move+QStringList{"--overwrite"}+invalid,false); expect(read(input) == original && read(output) == changed,"refusal leaves both files intact");
	}
	for (const auto& operation : QVector<QStringList>{{"rotate","--axis","z","--angle","90"},{"scale","--factors","1.1,1.2,1.3"},{"shear","--axis","x","--along","z","--factor","0.25"}}) {
		run(QStringList{input,"--set","brush:0/vertex/0,1,2,3,4,5,6,7","--operation"}+operation+QStringList{"--output",output,"--overwrite","--uv-lock","on","--allow-valve220"});
		expect(loadLevelMap({output,{},"idtech3"},&loaded,&error),"reload affine operation",error);
	}
	report = run(QStringList{input,"--set","brush:0/vertex/0,1,2,3,4,5,6,7","--operation","move","--delta","16,0,0","--output",output,"--overwrite","--uv-lock","fit"});
	expect(report["uvLockMode"].toString() == "fit" && report["uvLock"].toBool() && report["fittedFaces"].toInt() == 0,"fit mode reports an exact lock as exact");
	run({"--input",input,"--set=patch:0/vertex/0","--operation=move","--delta=0,0,4","--output",input,"--overwrite"});
	expect(read(input) != original,"explicit same-file save");
	return ok ? 0 : 1;
}
