#include "core/level_surface_clipboard.h"
#include "core/level_face_clipboard.h"
#include "tests/level_type_selection_test_helpers.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) { ok = false; std::cerr << "FAIL: " << label << ": " << detail.toStdString() << '\n'; } return value;
}
LevelMapDocument fixture()
{
	LevelMapDocument map; QString error;
	expect(loadLevelMapBytes({"components.map",{},"idtech3"},tests::typeSelectionFixture(),&map,&error),"fixture",error);
	setLevelMapSelection(&map,{{LevelMapSelectionKind::Entity,1}}); return map;
}
void capturedTargets()
{
	auto map = fixture(), source = map; const auto baseline = map; QString error;
	auto& face = source.brushes[0].faces[0]; face.textureName = "studio/copied"; face.shiftX = 37;
	face.contentFlags = 512; face.surfaceFlags = 128; face.surfaceValue = 77; face.flagsWritten = true;
	LevelSurfaceClipboard clipboard; expect(copyLevelSurface(source,{0,0},&clipboard,&error),"copy source",error);
	LevelSurfacePasteOptions options; options.mode = LevelSurfacePasteMode::RadiantValues; options.includeSelection = true;
	options.selectionTargets = QVector<LevelMaterialTarget>{{LevelMaterialKind::BrushFace,0,4},{LevelMaterialKind::BrushFace,1,3}};
	const auto original = serializeLevelMap(map).bytes;
	LevelSurfaceEditPlan plan;
	expect(prepareLevelSurfaceTransfer(map,{{LevelMaterialKind::BrushFace,0,0}},clipboard,options,&plan,&error)
		&& plan.faceCount() == 3 && plan.patchCount() == 0 && commitLevelSurfaceEdit(&map,plan,&error),"hit plus captured face subset",error);
	for (const auto& brush : map.brushes) {
		for (int i = 0; i < brush.faces.size(); ++i) {
			const auto& actual = brush.faces[i]; const auto& before = baseline.brushes[brush.id].faces[i];
			const bool hit = brush.id == 0 && i == 0, selected = (brush.id == 0 && i == 4) || (brush.id == 1 && i == 3);
			expect(actual.textureName == (hit || selected ? "studio/copied" : before.textureName),"only hit and selected faces receive material");
			expect(actual.contentFlags == (hit ? 512 : before.contentFlags) && actual.surfaceFlags == (hit ? 128 : before.surfaceFlags)
				&& actual.surfaceValue == (hit ? 77 : before.surfaceValue),"selected faces retain flags; hit receives copied flags");
		}
	}
	expect(undoLevelMapEdit(&map,&error) && serializeLevelMap(map).bytes == original,"exact transfer undo",error);
	options.selectionTargets = QVector<LevelMaterialTarget>{};
	expect(prepareLevelSurfaceTransfer(map,{{LevelMaterialKind::BrushFace,0,0}},clipboard,options,&plan,&error) && plan.faceCount() == 1,
		"explicit empty component selection never expands whole objects",error);
	options.selectionTargets = QVector<LevelMaterialTarget>{{LevelMaterialKind::BrushFace,999,0}};
	expect(!prepareLevelSurfaceTransfer(map,{{LevelMaterialKind::BrushFace,0,0}},clipboard,options,&plan,&error) && !plan.ready()
		&& serializeLevelMap(map).bytes == original,"invalid captured face fails atomically");
	options.selectionTargets = QVector<LevelMaterialTarget>(16385,{LevelMaterialKind::BrushFace,0,1});
	expect(!prepareLevelSurfaceTransfer(map,{{LevelMaterialKind::BrushFace,0,0}},clipboard,options,&plan,&error),"bounded captured selection");
}
void cli(const QString& executable)
{
	QTemporaryDir temp; if (!expect(temp.isValid(),"CLI directory")) { return; }
	const auto path = temp.filePath("types.map"), clip = temp.filePath("face.map"), output = temp.filePath("pasted.map");
	QFile file(path); const auto source = tests::typeSelectionFixture();
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(),"CLI fixture")) { return; } file.close();
	const auto run = [&](const QStringList& args) {
		QProcess process; process.setWorkingDirectory(temp.path());
		process.start(executable,QStringList{"--cli","--json","--settings-file",temp.filePath("settings.ini"),"map"}+args);
		const bool done = process.waitForStarted() && process.waitForFinished(30000);
		const auto out = process.readAllStandardOutput(), err = process.readAllStandardError();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,"CLI handoff",QString::fromUtf8(out+err));
		if (!done) { process.kill(); process.waitForFinished(); } return QJsonDocument::fromJson(out).object();
	};
	const auto query = run({"select-type",path,"--mode","faces","--object","entity:1","--material","studio/checker"});
	const auto targets = query["surfaceTargets"].toArray();
	if (!expect(targets.size() == 11 && targets.first() == "face:0:2" && targets.last() == "face:1:6","canonical one-based surface selectors")) { return; }
	run({"copy-surface",path,"--target","face:0:1","--format","map","--output",clip});
	QStringList paste{"paste-surface",path,"--clipboard",clip,"--output",output};
	for (const auto& target : targets) { paste << "--target" << target.toString(); }
	const auto report = run(paste); expect(report["changedFaces"].toInt() == 11,"query selectors feed surface paste directly");
	LevelMapDocument result; QString error; expect(loadLevelMap({output,{},"idtech3"},&result,&error),"pasted output",error);
	for (const auto& brush : result.brushes) { for (const auto& face : brush.faces) { expect(face.textureName == "metal","all query matches pasted, original metal source retained"); } }
	expect(file.open(QIODevice::ReadOnly) && file.readAll() == source,"query and handoff preserve original map");
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc,argv); capturedTargets();
	if (argc == 2) { cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath()); } return ok ? 0 : 1;
}
