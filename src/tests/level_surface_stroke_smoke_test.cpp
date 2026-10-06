#include "core/level_surface_clipboard.h"
#include "core/level_patch.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_surface_test_helpers.h"
#include <QCoreApplication>
#include <QFile>
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
	return loadLevelMapBytes({"stroke.map", {}, "idtech3"}, bytes, document, error);
}
QPointF texels(const LevelMapBrushFace& face, LevelMapVec3 point, QSize size)
{
	const auto uv = levelTextureProjection(face).at(point);
	return face.explicitTextureMatrix ? QPointF(uv.x() * size.width(), uv.y() * size.height()) : uv;
}
bool near(QPointF a, QPointF b) { return std::hypot(a.x() - b.x(), a.y() - b.y()) < 1e-4; }
bool put(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); bool ok = true; QString error; int seamSamples = 0;
	for (const auto& dialect : QStringList{"classic", "valve220", "brushDef", "brushDef3"}) {
		LevelMapDocument source, map; ok &= load(tests::surfaceFixture(dialect), &source, &error);
		auto bytes = tests::surfaceFixture(dialect);
		for (int f = 0; f < 6; ++f) { const int at = bytes.indexOf("studio/grid"); bytes.replace(at, 11, "studio/face" + QByteArray::number(f)); }
		ok &= load(bytes, &map, &error);
		LevelSurfaceClipboard clipboard; ok &= copyLevelSurface(source, {0, 0}, &clipboard, &error);
		const auto before = serializeLevelMap(map).bytes; const auto revision = map.revision; const auto depth = map.undoStack.size();
		LevelSurfaceStroke stroke(map, clipboard);
		LevelSurfacePasteOptions options; options.mode = LevelSurfacePasteMode::Seamless; options.mappingOnly = true; options.allowValve220 = true;
		options.textureSize = {128, 64};
		for (int f = 0; f < 6; ++f) { options.materialSizes.insert("studio/face" + QString::number(f), {32 * (f + 1), 48 * (f + 1)}); }
		const auto geometry = solveBrushGeometry(map.brushes[0].faces);
		auto previous = clipboard.face(); QSize previousSize{128, 64}; int previousFace = 0;
		for (int target : {2, 4, 1, 3, 5, 0, 2}) {
			if (!expect(stroke.append({{LevelMaterialKind::BrushFace, 0, target}}, options, &error), "append ordered wrap", dialect + ": " + error)) { return 1; }
			const auto& face = stroke.document().brushes[0].faces[target]; const auto targetSize = options.materialSizes.value(face.textureName);
			const auto& plane = geometry.faces[previousFace].plane; int shared = 0;
			for (const auto& point : geometry.faces[target].points) {
				if (std::abs(point.x * plane.normalX + point.y * plane.normalY + point.z * plane.normalZ - plane.distance) > 1e-5) { continue; }
				ok &= expect(near(texels(previous, point, previousSize), texels(face, point, targetSize)), "stroke retains texel continuity after source dimensions change", dialect);
				++shared; ++seamSamples;
			}
			ok &= expect(shared == 2 && stroke.clipboardFromDocument() && stroke.document().undoStack.isEmpty(), "private preview advances source without accumulating history");
			previous = face; previousSize = targetSize; previousFace = target;
		}
		ok &= expect(map.revision == revision && serializeLevelMap(map).bytes == before && map.undoStack.size() == depth, "preview never mutates source document");
		const auto preview = serializeLevelMap(stroke.document()).bytes;
		ok &= expect(stroke.commit(&map, &error) && map.undoStack.size() == depth + 1 && map.revision == revision + 1
			&& serializeLevelMap(map).bytes == preview, "ordered wrap publishes one undo command", error);
		LevelMapDocument reopened; ok &= load(serializeLevelMap(map).bytes, &reopened, &error);
		ok &= expect(reopened.brushes.size() == 1 && reopened.brushes[0].faces.size() == 6 && reopened.entities.size() == 2, "serialized stroke preserves topology and entities");
		ok &= expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == before
			&& redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == preview, "stroke has exact undo and redo", error);
		auto stale = map; const auto staleBytes = serializeLevelMap(stale).bytes;
		ok &= expect(!stroke.commit(&stale, &error) && serializeLevelMap(stale).bytes == staleBytes, "stroke rejects changed revision");
		const auto last = serializeLevelMap(stroke.document()).bytes; const auto stepCount = stroke.stepCount();
		ok &= expect(!stroke.append({{LevelMaterialKind::BrushFace, 0, 99}}, options, &error) && stroke.stepCount() == stepCount
			&& serializeLevelMap(stroke.document()).bytes == last, "invalid later hit leaves previous preview intact");
		int checkpoints = 0;
		ok &= expect(!stroke.append({{LevelMaterialKind::BrushFace, 0, 4}}, options, &error, [&] { return ++checkpoints > 3; })
			&& stroke.stepCount() == stepCount && serializeLevelMap(stroke.document()).bytes == last, "cancelled hit is atomic");
	}
	LevelMapDocument map; ok &= load(tests::surfaceFixture("classic"), &map, &error);
	ok &= addLevelMapBoxBrush(&map, {160, 16, -8, true}, {224, 80, 56, true}, "studio/target", nullptr, &error);
	LevelMapPatch patch; LevelPatchCreateRequest create; create.texture = "studio/patch";
	ok &= createLevelPatch(create, &patch, &error) && addLevelMapPatch(&map, patch, nullptr, &error);
	ok &= setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 1}, {LevelMapSelectionKind::QuakePatch, 0}}, &error);
	LevelSurfaceClipboard clipboard; ok &= copyLevelSurface(map, {0, 0}, &clipboard, &error);
	auto json = QJsonDocument::fromJson(serializeLevelSurfaceClipboard(clipboard)).object(); json.insert("material", "studio/copied");
	ok &= parseLevelSurfaceClipboard(QJsonDocument(json).toJson(), &clipboard, &error);
	const auto before = serializeLevelMap(map).bytes; const auto selection = map.selection; const auto depth = map.undoStack.size();
	LevelSurfaceStroke stroke(map, clipboard); LevelSurfacePasteOptions options;
	options.mode = LevelSurfacePasteMode::RadiantValues; options.includeSelection = true;
	ok &= expect(stroke.append({{LevelMaterialKind::BrushFace, 0, 0}}, options, &error) && stroke.plan().faceCount() == 7 && stroke.plan().patchCount() == 1, "first hit includes selected brush and patch", error);
	const auto selectedPatch = levelPatchDefinition(stroke.document().patches[0]);
	options.mode = LevelSurfacePasteMode::RadiantProject; options.materialSizes.insert("studio/copied", {128, 64});
	ok &= expect(stroke.append({{LevelMaterialKind::BrushFace, 0, 2}}, options, &error)
		&& levelPatchDefinition(stroke.document().patches[0]) == selectedPatch, "modifier change uses hit only after mouse-down", error);
	ok &= expect(stroke.append({{LevelMaterialKind::Patch, 0, 0}}, options, &error) && levelPatchDefinition(stroke.document().patches[0]) != selectedPatch, "later explicit patch hit projects UVs", error);
	ok &= expect(stroke.commit(&map, &error) && map.selection == selection && map.undoStack.size() == depth + 1
		&& undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == before, "mixed brush/patch stroke is one undo step and preserves selection", error);
	// A no-op must retain the redo branch and saved position left by that undo.
	LevelSurfaceClipboard same; ok &= copyLevelSurface(map, {0, 0}, &same, &error);
	LevelSurfaceStroke noOp(map, same); const auto revision = map.revision; const auto redo = map.redoStack.size(); const auto saved = map.savedUndoDepth;
	ok &= expect(noOp.append({{LevelMaterialKind::BrushFace, 0, 0}}, {}, &error) && noOp.plan().faceCount() == 0
		&& noOp.commit(&map, &error) && map.revision == revision && map.redoStack.size() == redo && map.savedUndoDepth == saved, "no-op stroke preserves redo and save point", error);
	for (int step = noOp.stepCount(); step < 4096; ++step) {
		if (!expect(noOp.append({{LevelMaterialKind::BrushFace, 0, 0}}, {}, &error), "bounded no-op traversal", error)) { return 1; }
	}
	ok &= expect(!noOp.append({{LevelMaterialKind::BrushFace, 0, 0}}, {}, &error) && noOp.stepCount() == 4096
		&& noOp.plan().faceCount() == 0, "stroke hit limit rejects overflow without changing the preview");
	QString layer; ok &= createLevelSceneNode(&map, LevelSceneNodeKind::Layer, "Protected", {}, &layer, &error)
		&& assignLevelSceneObjects(&map, layer, {"brush:1"}, &error) && setLevelSceneLocked(&map, layer, true, &error);
	LevelSurfaceStroke locked(map, clipboard); options.mode = LevelSurfacePasteMode::RadiantValues;
	ok &= expect(!locked.append({{LevelMaterialKind::BrushFace, 0, 0}}, options, &error) && locked.stepCount() == 0, "locked member rejects complete first hit");
	if (app.arguments().size() > 1) {
		QTemporaryDir temp; ok &= temp.isValid();
		const auto input = temp.filePath("input.map"), surface = temp.filePath("surface.json"), output = temp.filePath("output.map");
		LevelMapDocument cliMap; ok &= load(tests::surfaceFixture("brushDef"), &cliMap, &error) && copyLevelSurface(cliMap, {0, 0}, &same, &error);
		ok &= put(input, tests::surfaceFixture("brushDef")) && put(surface, serializeLevelSurfaceClipboard(same));
		const QStringList arguments{"--cli", "map", "paste-surface", input, "--clipboard", surface, "--stroke", "--mode", "seamless", "--mapping-only",
			"--texture-size", "128,64", "--material-size", "studio/grid=128,64", "--target", "face:0:3", "--target", "face:0:5", "--output", output, "--json"};
		QProcess process; process.start(app.arguments()[1], arguments); ok &= process.waitForFinished(30000);
		const auto payload = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
		ok &= expect(process.exitCode() == 0 && QFile::exists(output) && payload.value("strokeHits").toInt() == 2 && payload.value("sourceAdvanced").toBool(), "CLI replays ordered stroke", QString::fromUtf8(process.readAllStandardError()));
		options = {}; options.mode = LevelSurfacePasteMode::Seamless; options.mappingOnly = true; options.textureSize = {128, 64}; options.materialSizes.insert("studio/grid", {128, 64});
		LevelSurfaceStroke expected(cliMap, same); ok &= expected.append({{LevelMaterialKind::BrushFace, 0, 2}}, options, &error)
			&& expected.append({{LevelMaterialKind::BrushFace, 0, 4}}, options, &error) && expected.commit(&cliMap, &error);
		QFile file(output); ok &= file.open(QIODevice::ReadOnly);
		ok &= expect(file.readAll() == serializeLevelMap(cliMap).bytes, "CLI and authoring service serialize the same ordered result");
		auto bad = arguments; bad[bad.indexOf("face:0:5")] = "face:0:99"; bad[bad.indexOf(output)] = temp.filePath("bad.map");
		process.start(app.arguments()[1], bad); ok &= process.waitForFinished(30000);
		ok &= expect(process.exitCode() != 0 && !QFile::exists(temp.filePath("bad.map")), "bad later CLI hit publishes no partial file");
	}
	std::cout << seamSamples << " ordered cross-material seam samples verified.\n";
	return ok ? 0 : 1;
}
