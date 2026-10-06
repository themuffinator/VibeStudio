#include "core/level_surface_clipboard.h"
#include "core/level_patch.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_surface_test_helpers.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message, const QString& detail = {})
{
	if (!value) { std::cerr << message << ": " << detail.toStdString() << '\n'; } return value;
}
bool load(const QByteArray& bytes, LevelMapDocument* document, QString* error)
{
	return loadLevelMapBytes({"transfer.map", {}, "idtech3"}, bytes, document, error);
}
bool near(QPointF a, QPointF b) { return std::hypot(a.x() - b.x(), a.y() - b.y()) < 1e-5; }
QPointF texels(const LevelMapBrushFace& face, LevelMapVec3 point, QSize size)
{
	const auto uv = levelTextureProjection(face).at(point);
	return face.explicitTextureMatrix ? QPointF(uv.x() * size.width(), uv.y() * size.height()) : uv;
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); bool ok = true; QString error;
	const QStringList dialects{"classic", "valve220", "brushDef", "brushDef3"};
	int samples = 0;
	for (const auto& fromKind : dialects) {
		LevelMapDocument from; ok &= load(tests::surfaceFixture(fromKind), &from, &error);
		LevelSurfaceClipboard clipboard; ok &= copyLevelSurface(from, {0, 0}, &clipboard, &error);
		for (const auto& toKind : dialects) {
			LevelMapDocument target; ok &= load(tests::surfaceFixture(toKind).replace("studio/grid", "studio/target"), &target, &error);
			const auto original = serializeLevelMap(target).bytes;
			LevelSurfacePasteOptions options; options.mode = LevelSurfacePasteMode::Project; options.mappingOnly = true; options.allowValve220 = true;
			options.textureSize = {128, 64}; options.materialSizes.insert("studio/target", {64, 256});
			LevelSurfaceEditPlan plan;
			if (!expect(prepareLevelSurfacePaste(target, {{0, 0}}, clipboard, options, &plan, &error) && commitLevelSurfaceEdit(&target, plan, &error),
				"mapping-only projection across formats", fromKind + " -> " + toKind + ": " + error)) { return 1; }
			LevelMapDocument reopened; ok &= load(serializeLevelMap(target).bytes, &reopened, &error);
			const auto& face = reopened.brushes[0].faces[0];
			ok &= expect(face.textureName == "studio/target" && face.contentFlags == 2 && face.surfaceFlags == 4 && face.surfaceValue == 8, "material and flags retained");
			const auto geometry = solveBrushGeometry(from.brushes[0].faces);
			for (const auto& point : geometry.faces[0].points) {
				ok &= expect(near(texels(clipboard.face(), point, {128, 64}), texels(face, point, {64, 256})), "serialized texel density and offset preserved across different images"); ++samples;
			}
			if (plan.faceCount() > 0) { ok &= expect(undoLevelMapEdit(&target, &error) && serializeLevelMap(target).bytes == original, "mapping-only exact undo"); }
			if (fromKind.startsWith("brushDef") || toKind.startsWith("brushDef")) {
				auto missing = options; missing.textureSize = {}; missing.materialSizes.clear();
				ok &= expect(!prepareLevelSurfacePaste(target, {{0, 0}}, clipboard, missing, &plan, &error) && !plan.ready(), "dimension-dependent mapping refuses unknown sizes");
			}
		}
	}
	LevelMapDocument matrix; ok &= load(tests::surfaceFixture("brushDef"), &matrix, &error);
	LevelSurfaceClipboard matrixCopy; ok &= copyLevelSurface(matrix, {0, 0}, &matrixCopy, &error);
	LevelSurfacePasteOptions matrixOptions; matrixOptions.mode = LevelSurfacePasteMode::RadiantValues; matrixOptions.mappingOnly = true;
	matrixOptions.textureSize = {128, 64}; matrixOptions.materialSizes.insert("studio/grid", {64, 256});
	LevelSurfaceEditPlan plan;
	ok &= expect(prepareLevelSurfacePaste(matrix, {{0, 0}}, matrixCopy, matrixOptions, &plan, &error) && commitLevelSurfaceEdit(&matrix, plan, &error)
		&& std::abs(matrix.brushes[0].faces[0].textureMatrix[0] - matrixCopy.face().textureMatrix[0] * 2) < 1e-12
		&& std::abs(matrix.brushes[0].faces[0].textureMatrix[4] - matrixCopy.face().textureMatrix[4] / 4) < 1e-12,
		"same-named images from different packages retain source texel density", error);
	const auto required = levelSurfaceTransferRequiredMaterials(matrix, {{LevelMaterialKind::BrushFace, 0, 0}}, matrixCopy, matrixOptions);
	ok &= expect(required.sourceSizeRequired && required.targets == QStringList{"studio/grid"}, "dimension lookup separates source and target roles");
	LevelMapDocument valve; ok &= load(tests::surfaceFixture("valve220"), &valve, &error);
	LevelSurfaceClipboard valveCopy; ok &= copyLevelSurface(valve, {0, 0}, &valveCopy, &error);
	const auto previous = valve.brushes[0].faces[2];
	auto modified = QJsonDocument::fromJson(serializeLevelSurfaceClipboard(valveCopy)).object(); modified.insert("material", "studio/copied");
	ok &= parseLevelSurfaceClipboard(QJsonDocument(modified).toJson(), &valveCopy, &error);
	LevelSurfacePasteOptions values; values.mode = LevelSurfacePasteMode::RadiantValues;
	ok &= expect(prepareLevelSurfacePaste(valve, {{0, 2}}, valveCopy, values, &plan, &error) && commitLevelSurfaceEdit(&valve, plan, &error)
		&& valve.brushes[0].faces[2].uAxis.x == previous.uAxis.x && valve.brushes[0].faces[2].uAxis.y == previous.uAxis.y
		&& valve.brushes[0].faces[2].uAxis.z == previous.uAxis.z && valve.brushes[0].faces[2].vAxis.x == previous.vAxis.x
		&& valve.brushes[0].faces[2].vAxis.y == previous.vAxis.y && valve.brushes[0].faces[2].vAxis.z == previous.vAxis.z,
		"native values keeps perpendicular Valve axes", error);
	LevelMapDocument document; ok &= load(tests::surfaceFixture("classic"), &document, &error);
	ok &= addLevelMapBoxBrush(&document, {160, 16, -8, true}, {224, 80, 56, true}, "studio/target", nullptr, &error);
	LevelMapPatch patch; LevelPatchCreateRequest create; create.texture = "studio/patch";
	ok &= createLevelPatch(create, &patch, &error) && addLevelMapPatch(&document, patch, nullptr, &error);
	ok &= setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 1}, {LevelMapSelectionKind::QuakePatch, 0}}, &error);
	LevelSurfaceClipboard clipboard; ok &= copyLevelSurface(document, {0, 0}, &clipboard, &error);
	modified = QJsonDocument::fromJson(serializeLevelSurfaceClipboard(clipboard)).object(); modified.insert("material", "studio/copied"); modified.insert("flags", QJsonArray{32, 64, 128});
	ok &= parseLevelSurfaceClipboard(QJsonDocument(modified).toJson(), &clipboard, &error);
	const auto original = serializeLevelMap(document).bytes;
	const auto patchBefore = levelPatchDefinition(document.patches[0]);
	const auto selection = document.selection; const auto depth = document.undoStack.size();
	values.includeSelection = true;
	const QVector<LevelMaterialTarget> hit{{LevelMaterialKind::BrushFace, 0, 0}};
	ok &= expect(prepareLevelSurfaceTransfer(document, hit, clipboard, values, &plan, &error) && plan.faceCount() == 7 && plan.patchCount() == 1, "hit and selection form one mixed plan", error);
	auto stale = document; stale.selection.clear();
	ok &= expect(!commitLevelSurfaceEdit(&stale, plan, &error) && serializeLevelMap(stale).bytes == original, "selection drift rejects complete transaction");
	stale = document; stale.patches[0].textureColumn++;
	ok &= expect(!commitLevelSurfaceEdit(&stale, plan, &error), "patch source binding drift rejects plan");
	auto locked = document; QString layer;
	ok &= createLevelSceneNode(&locked, LevelSceneNodeKind::Layer, "Locked patch", {}, &layer, &error)
		&& assignLevelSceneObjects(&locked, layer, {"patch:0"}, &error) && setLevelSceneLocked(&locked, layer, true, &error);
	LevelSurfaceEditPlan lockedPlan; const auto lockedBytes = serializeLevelMap(locked).bytes;
	ok &= expect(prepareLevelSurfaceTransfer(locked, hit, clipboard, values, &lockedPlan, &error)
		&& !commitLevelSurfaceEdit(&locked, lockedPlan, &error) && serializeLevelMap(locked).bytes == lockedBytes, "locked selected patch blocks hit and selection atomically");
	ok &= expect(commitLevelSurfaceEdit(&document, plan, &error) && document.undoStack.size() == depth + 1 && document.selection == selection, "mixed paste is one undo step and preserves selection", error);
	ok &= expect(document.brushes[0].faces[0].surfaceFlags == 64 && document.brushes[1].faces[0].surfaceFlags == 0, "only explicit hit receives copied flags");
	auto patched = document.patches[0]; patched.textureName = "studio/patch";
	ok &= expect(levelPatchDefinition(patched) == patchBefore && document.patches[0].textureName == "studio/copied", "selected patch receives only material");
	const auto pasted = serializeLevelMap(document).bytes;
	ok &= expect(document.textureReferences.contains("studio/copied") && !document.textureReferences.contains("studio/patch"), "dependency references update immediately");
	ok &= expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == original && document.textureReferences.contains("studio/patch")
		&& !document.textureReferences.contains("studio/copied"), "undo restores bytes and material dependencies");
	ok &= expect(redoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == pasted, "redo restores mixed transfer");
	ok &= undoLevelMapEdit(&document, &error);
	auto only = values; only.mappingOnly = true;
	ok &= expect(prepareLevelSurfaceTransfer(document, {{LevelMaterialKind::Patch, 0, 0}}, clipboard, only, &plan, &error) && plan.patchCount() == 0, "mapping-only never changes patch UVs or material");
	ok &= expect(!prepareLevelSurfaceTransfer(document, hit, clipboard, values, &plan, &error, [] { return true; }) && !plan.ready(), "cancelled mixed plan is unpublished");
	ok &= expect(!prepareLevelSurfaceTransfer(document, {{LevelMaterialKind::Patch, 99, 0}}, clipboard, values, &plan, &error), "missing patch fails whole batch");
	auto entity = document; ok &= selectLevelMapObject(&entity, "entity:0", &error);
	ok &= expect(levelSurfacePasteSelectionTargets(entity).size() == 13, "selected entity includes owned brush faces and patch");
	if (argc > 1) {
		QTemporaryDir temp; if (!temp.isValid()) { return 1; }
		const auto map = temp.filePath("input.map"), clip = temp.filePath("surface.json"), output = temp.filePath("output.map");
		const auto write = [](const QString& path, const QByteArray& bytes) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(); };
		const auto read = [](const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); };
		ok &= write(map, original) && write(clip, serializeLevelSurfaceClipboard(clipboard));
		const auto cli = [&](QStringList args, int expected) {
			QProcess p; p.start(QString::fromLocal8Bit(argv[1]), QStringList{"--settings-file", temp.filePath("settings.ini"), "--cli", "map", "paste-surface", map, "--clipboard", clip, "--output", output, "--json"} + args);
			const bool done = p.waitForFinished(30000); const auto bytes = p.readAllStandardOutput(), errors = p.readAllStandardError();
			ok &= expect(done && p.exitStatus() == QProcess::NormalExit && p.exitCode() == expected, "CLI exit", QString::fromUtf8(bytes + errors)); return QJsonDocument::fromJson(bytes).object();
		};
		const QStringList args{"--target", "face:0:1", "--object", "brush:1", "--object", "patch:0", "--mode", "radiant-values"};
		auto report = cli(args + QStringList{"--dry-run"}, 0);
		ok &= expect(!QFile::exists(output) && report.value("changedPatches").toInt() == 1 && report.value("changedFaces").toInt() == 7, "CLI dry run reports brush and patch changes");
		cli(args, 0); ok &= expect(read(output) == pasted && read(map) == original, "CLI and GUI service serialize identically");
		cli({"--target", "patch:0", "--mapping-only", "--overwrite"}, 0); ok &= expect(read(output) == original, "patch mapping-only no-op");
		cli({"--target", "face:0:1", "--object", "entity:0", "--mode", "seamless"}, 2);
		cli({"--target", "face:0:1", "--material-size", "studio/grid=0,64"}, 2);
		cli({"--target", "face:0:1", "--material-size", "Studio/Grid=128,64", "--material-size", "studio/grid=64,64"}, 2);
		ok &= write(map, tests::surfaceFixture("brushDef")) && write(clip, serializeLevelSurfaceClipboard(matrixCopy));
		report = cli({"--target", "face:0:1", "--mapping-only", "--mode", "radiant-values", "--texture-size", "128,64", "--material-size", "Studio/Grid=64,256", "--overwrite"}, 0);
		LevelMapDocument saved; ok &= load(read(output), &saved, &error);
		ok &= expect(report.value("mappingOnly").toBool() && saved.brushes[0].faces[0].textureMatrix == matrix.brushes[0].faces[0].textureMatrix, "CLI explicit source and target dimensions preserve density");
		report = cli({"--target", "face:0:1", "--mode", "radiant-values", "--texture-size", "128,64", "--material-size", "studio/grid=64,256", "--overwrite"}, 0);
		ok &= load(read(output), &saved, &error);
		ok &= expect(!report.value("mappingOnly").toBool() && saved.brushes[0].faces[0].textureMatrix == matrix.brushes[0].faces[0].textureMatrix, "CLI full native values uses destination dimensions independently");
	}
	std::cout << "Verified " << samples << " serialized UV samples and selected-object transfer checks.\n"; return ok ? 0 : 1;
}
