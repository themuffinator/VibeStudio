#include "core/level_surface_clipboard.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_surface_test_helpers.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <cmath>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message, const QString& detail = {})
{
	if (!value) { std::cerr << message << ": " << detail.toStdString() << '\n'; }
	return value;
}
bool load(const QByteArray& bytes, LevelMapDocument* document, QString* error)
{
	return loadLevelMapBytes({"clipboard.map", {}, "idtech3"}, bytes, document, error);
}
bool near(QPointF a, QPointF b) { return std::abs(a.x() - b.x()) < 1e-7 && std::abs(a.y() - b.y()) < 1e-7; }
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true; QString error;
	const QStringList dialects{"classic", "valve220", "brushDef", "brushDef3"};
	for (const auto& dialect : dialects) {
		for (bool existingFlags : {true, false}) {
			auto original = tests::surfaceFixture(dialect);
			if (!existingFlags) { original.replace(" 2 4 8 // flags", " // flags"); }
			LevelMapDocument map;
			if (!expect(load(original, &map, &error), "load fixture", error)) { return 1; }
			setLevelMapSelection(&map, {{LevelMapSelectionKind::Entity, 1}});
			LevelSurfaceClipboard clipboard;
			ok &= expect(copyLevelSurface(map, {0, 0}, &clipboard, &error), "copy", error);
			auto json = QJsonDocument::fromJson(serializeLevelSurfaceClipboard(clipboard)).object();
			json.insert("material", "studio/copied"); json.insert("flags", QJsonArray{4294967295LL, 2147483648LL, -27});
			auto mapping = json.value("mapping").toObject();
			const auto key = dialect.startsWith("brushDef") ? QStringLiteral("matrix") : dialect == "valve220" ? QStringLiteral("u") : QStringLiteral("shift");
			auto values = mapping.value(key).toArray(); const int offset = key == "matrix" ? 2 : key == "u" ? 3 : 0;
			values[offset] = values[offset].toDouble() + 11; mapping.insert(key, values); json.insert("mapping", mapping);
			ok &= expect(parseLevelSurfaceClipboard(QJsonDocument(json).toJson(), &clipboard, &error), "portable definition", error);
			const auto before = map;
			LevelSurfaceEditPlan plan;
			ok &= expect(prepareLevelSurfacePaste(map, {{0, 0}, {0, 0}}, clipboard, {}, &plan, &error) && plan.faceCount() == 1
				&& serializeLevelMap(map).bytes == original && commitLevelSurfaceEdit(&map, plan, &error), "atomic deduplicated paste", error);
			const auto bytes = serializeLevelMap(map).bytes;
			ok &= expect(map.undoStack.size() == before.undoStack.size() + 1 && map.selection == before.selection
				&& map.brushes[0].faces[0].p0.x == before.brushes[0].faces[0].p0.x
				&& map.brushes[0].faces[0].p0.y == before.brushes[0].faces[0].p0.y
				&& map.brushes[0].faces[0].p0.z == before.brushes[0].faces[0].p0.z, "one undo; geometry and selection retained");
			ok &= expect(bytes.contains("// flags") && bytes.contains("// untouched point entity") && bytes.contains("/* retained */") == original.contains("/* retained */")
				&& bytes.count("\r\n") == original.count("\r\n"), "comments and original line endings retained", dialect);
			LevelMapDocument reopened;
			ok &= expect(load(bytes, &reopened, &error), "reload", error);
			const auto& face = reopened.brushes[0].faces[0];
			ok &= expect(face.textureName == "studio/copied" && face.contentFlags == 4294967295LL
				&& face.surfaceFlags == 2147483648LL && face.surfaceValue == -27, "material and exact unsigned flags survive save", dialect);
			const auto winding = solveBrushGeometry(before.brushes[0].faces).faces[0];
			for (const auto& point : winding.points) {
				ok &= expect(near(levelTextureProjection(face).at(point), levelTextureProjection(clipboard.face()).at(point)), "mapping survives save", dialect);
			}
			ok &= expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == original
				&& redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == bytes, "byte-exact undo and redo", error);
			const auto revision = map.revision; const auto history = map.undoStack.size();
			ok &= expect(prepareLevelSurfacePaste(map, {{0, 0}}, clipboard, {}, &plan, &error) && plan.faceCount() == 0
				&& commitLevelSurfaceEdit(&map, plan, &error) && map.revision == revision && map.undoStack.size() == history, "no-op paste leaves history intact", error);
			ok &= expect(!prepareLevelSurfacePaste(map, {{0, 1}, {0, 99}}, clipboard, {}, &plan, &error) && !plan.ready()
				&& serializeLevelMap(map).bytes == bytes, "invalid batch cannot partly mutate");
			int checks = 0;
			ok &= expect(!prepareLevelSurfacePaste(map, {{0, 0}, {0, 1}}, clipboard, {}, &plan, &error, [&] { return ++checks > 2; })
				&& !plan.ready(), "paste preparation is cancellable");
			map = before;
			ok &= prepareLevelSurfacePaste(map, {{0, 0}}, clipboard, {}, &plan, &error);
			map.brushes[0].faces[0].textureName = "studio/changed";
			ok &= expect(!commitLevelSurfaceEdit(&map, plan, &error), "changed face invalidates a plan even without a revision change");
			map = before; QString layer;
			ok &= createLevelSceneNode(&map, LevelSceneNodeKind::Layer, "Locked", {}, &layer, &error);
			ok &= assignLevelSceneObjects(&map, layer, {"brush:0"}, &error);
			ok &= setLevelSceneLocked(&map, layer, true, &error);
			const auto locked = serializeLevelMap(map).bytes;
			ok &= expect(prepareLevelSurfacePaste(map, {{0, 0}}, clipboard, {}, &plan, &error) && !commitLevelSurfaceEdit(&map, plan, &error)
				&& serializeLevelMap(map).bytes == locked, "scene lock rejects complete surface paste", error);
			for (const auto& bad : {QByteArray("{}"), QByteArray(16385, ' '), QByteArray("{\"version\":1e309}"),
				QJsonDocument([&] { auto value = json; value.insert("version", 2); return value; }()).toJson(),
				QJsonDocument([&] { auto value = json; value.insert("unexpected", true); return value; }()).toJson(),
				QJsonDocument([&] { auto value = json; value.insert("flags", QJsonArray{0.5, 2, 3}); return value; }()).toJson(),
				QJsonDocument([&] { auto value = json; value.insert("material", "bad/*name"); return value; }()).toJson()}) {
				ok &= expect(!parseLevelSurfaceClipboard(bad, &clipboard, &error) && !clipboard.ready(), "malformed clipboard clears stale data");
			}
		}
	}
	for (const auto& sourceKind : dialects) {
		LevelMapDocument source; load(tests::surfaceFixture(sourceKind), &source, &error);
		LevelSurfaceClipboard clipboard; ok &= copyLevelSurface(source, {0, 0}, &clipboard, &error);
		for (const auto& targetKind : dialects) {
			LevelMapDocument target; load(tests::surfaceFixture(targetKind), &target, &error);
			setLevelMapSelection(&target, {{LevelMapSelectionKind::QuakeBrush, 0}});
			LevelMapRotationRequest rotation; rotation.axis = 1; rotation.degrees = 31.75; rotation.textureLock = false;
			ok &= expect(moveLevelMapSelection(&target, 37, -15, 24, &error) && rotateLevelMapSelection(&target, rotation, &error)
				&& load(serializeLevelMap(target).bytes, &target, &error), "translated and oblique projection target", error);
			LevelSurfaceEditPlan plan;
			LevelSurfacePasteOptions options; options.mode = LevelSurfacePasteMode::Project; options.allowValve220 = true; options.textureSize = {128, 64};
			ok &= expect(prepareLevelSurfacePaste(target, {{0, 0}}, clipboard, options, &plan, &error)
				&& commitLevelSurfaceEdit(&target, plan, &error), "all format pairs project", sourceKind + " to " + targetKind + ": " + error);
			LevelMapDocument reopened; load(serializeLevelMap(target).bytes, &reopened, &error);
			const auto from = levelTextureProjection(source.brushes[0].faces[0]);
			const auto to = levelTextureProjection(reopened.brushes[0].faces[0]);
			const auto geometry = solveBrushGeometry(reopened.brushes[0].faces);
			for (const auto& point : geometry.faces[0].points) {
				auto a = from.at(point), b = to.at(point);
				if (from.normalizedCoordinates) { a = {a.x() * 128, a.y() * 64}; }
				if (to.normalizedCoordinates) { b = {b.x() * 128, b.y() * 64}; }
				ok &= expect(near(a, b), "projected UVs agree at every winding vertex", sourceKind + " to " + targetKind);
			}
			if (source.brushes[0].faces[0].explicitTextureMatrix != target.brushes[0].faces[0].explicitTextureMatrix) {
				options.textureSize = {};
				ok &= expect(!prepareLevelSurfacePaste(target, {{0, 0}}, clipboard, options, &plan, &error), "cross-unit projection refuses guessed image dimensions");
			}
		}
	}
	{
		LevelMapDocument source, target;
		ok &= load(tests::surfaceFixture("brushDef").replace("0.0078125", "0.015625"), &source, &error)
			&& load(tests::surfaceFixture("classic"), &target, &error);
		ok &= addLevelMapBoxBrush(&target, {150, 16, -8, true}, {220, 80, 56, true}, "studio/unpasted", nullptr, &error);
		ok &= load(serializeLevelMap(target).bytes, &target, &error);
		const auto original = serializeLevelMap(target).bytes; const auto before = target;
		LevelSurfaceClipboard clipboard; ok &= copyLevelSurface(source, {0, 0}, &clipboard, &error);
		LevelSurfaceEditPlan plan; LevelSurfacePasteOptions options; options.mode = LevelSurfacePasteMode::Project; options.textureSize = {128, 64};
		ok &= expect(!prepareLevelSurfacePaste(target, {{0, 0}}, clipboard, options, &plan, &error) && !plan.ready()
			&& serializeLevelMap(target).bytes == original, "sheared projection requires explicit conversion consent");
		options.allowValve220 = true;
		ok &= expect(prepareLevelSurfacePaste(target, {{0, 0}}, clipboard, options, &plan, &error)
			&& plan.convertedFaceCount() == 12 && commitLevelSurfaceEdit(&target, plan, &error), "conversion is map-wide and atomic", error);
		const auto converted = serializeLevelMap(target).bytes;
		LevelMapDocument reopened; ok &= load(converted, &reopened, &error);
		for (int b = 0; b < reopened.brushes.size(); ++b) {
			const auto geometry = solveBrushGeometry(reopened.brushes[b].faces);
			for (int f = 0; f < reopened.brushes[b].faces.size(); ++f) {
				const auto& face = reopened.brushes[b].faces[f];
				ok &= expect(face.explicitTextureAxes, "no mixed classic/Valve source reaches the compiler");
				if (b == 0 && f == 0) { continue; }
				const auto& old = before.brushes[b].faces[f];
				ok &= expect(face.textureName == old.textureName && face.contentFlags == old.contentFlags
					&& face.surfaceFlags == old.surfaceFlags && face.surfaceValue == old.surfaceValue, "unpasted surface definition is preserved");
				for (const auto& point : geometry.faces[f].points) {
					ok &= expect(near(levelTextureProjection(face).at(point), levelTextureProjection(old).at(point)), "unpasted UVs survive map-wide conversion");
				}
			}
		}
		ok &= expect(target.undoStack.size() == 1 && undoLevelMapEdit(&target, &error) && serializeLevelMap(target).bytes == original
			&& redoLevelMapEdit(&target, &error) && serializeLevelMap(target).bytes == converted, "one exact undo/redo covers paste and conversion");
		target = before; QString layer;
		ok &= createLevelSceneNode(&target, LevelSceneNodeKind::Layer, "Locked unpasted brush", {}, &layer, &error)
			&& assignLevelSceneObjects(&target, layer, {"brush:1"}, &error) && setLevelSceneLocked(&target, layer, true, &error);
		const auto locked = serializeLevelMap(target).bytes;
		ok &= expect(prepareLevelSurfacePaste(target, {{0, 0}}, clipboard, options, &plan, &error) && !commitLevelSurfaceEdit(&target, plan, &error)
			&& serializeLevelMap(target).bytes == locked, "conversion respects unpasted scene locks");
	}
	if (argc > 1) {
		QTemporaryDir temp; if (!expect(temp.isValid(), "CLI fixture directory")) { return 1; }
		const auto mapPath = temp.filePath("input.map"), copied = temp.filePath("surface.json"), output = temp.filePath("pasted.map");
		const auto write = [&](const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); };
		const auto read = [&](const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); };
		const auto original = tests::surfaceFixture("classic"); ok &= write(mapPath, original);
		const auto cli = [&](QStringList arguments, int expected) {
			QProcess process;
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--settings-file", temp.filePath("settings.ini"), "--cli", "map"} + arguments + QStringList{"--json"});
			const bool done = process.waitForFinished(30000);
			const auto stdoutBytes = process.readAllStandardOutput(), stderrBytes = process.readAllStandardError();
			ok &= expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected, "CLI result", QString::fromUtf8(stdoutBytes + stderrBytes));
			return QJsonDocument::fromJson(stdoutBytes).object();
		};
		cli({"copy-surface", mapPath, "--target", "face:0:1", "--output", copied, "--dry-run"}, 0);
		ok &= expect(!QFile::exists(copied) && read(mapPath) == original, "CLI copy dry run changes nothing");
		cli({"copy-surface", mapPath, "--target", "face:0:1", "--output", copied}, 0);
		LevelSurfaceClipboard clipboard;
		ok &= expect(parseLevelSurfaceClipboard(read(copied), &clipboard, &error), "CLI export is the shared portable format", error);
		cli({"copy-surface", mapPath, "--target", "face:0:1", "--output", copied}, 4);
		cli({"copy-surface", mapPath, "--target", "face:0:1", "--output", mapPath, "--overwrite"}, 2);
		ok &= expect(read(mapPath) == original, "copy cannot overwrite input map with JSON");
		auto definition = QJsonDocument::fromJson(read(copied)).object(); definition.insert("material", "studio/cli");
		definition.insert("flags", QJsonArray{32, 64, 128}); ok &= write(copied, QJsonDocument(definition).toJson());
		cli({"paste-surface", mapPath, "--clipboard", copied, "--target", "brush:0", "--output", output, "--dry-run"}, 0);
		ok &= expect(!QFile::exists(output) && read(mapPath) == original, "paste dry run changes nothing");
		const auto pasted = cli({"paste-surface", mapPath, "--clipboard", copied, "--target", "brush:0", "--output", output}, 0);
		LevelMapDocument map;
		ok &= expect(load(read(output), &map, &error) && pasted.value("changedFaces").toInt() == 6, "CLI whole-brush paste", error);
		for (const auto& face : map.brushes[0].faces) {
			ok &= expect(face.textureName == "studio/cli" && face.contentFlags == 32 && face.surfaceFlags == 64 && face.surfaceValue == 128, "CLI persists every surface field");
		}
		const auto saved = read(output);
		const auto wrapOutput = temp.filePath("wrapped.map");
		const auto wrapped = cli({"paste-surface", mapPath, "--clipboard", copied, "--target", "face:0:3", "--mode", "seamless", "--output", wrapOutput}, 0);
		ok &= expect(load(read(wrapOutput), &map, &error) && wrapped.value("mode") == "seamless" && wrapped.value("changedFaces").toInt() == 1,
			"CLI seamless wrap persists one face", error);
		LevelMapDocument originalMap; ok &= load(original, &originalMap, &error);
		const auto a = levelTextureProjection(originalMap.brushes[0].faces[0]), b = levelTextureProjection(map.brushes[0].faces[2]);
		const auto edgePlane = solveBrushGeometry(originalMap.brushes[0].faces).faces[0].plane;
		int seamPoints = 0;
		const auto wrappedGeometry = solveBrushGeometry(map.brushes[0].faces);
		for (const auto& point : wrappedGeometry.faces[2].points) {
			if (std::abs(point.x * edgePlane.normalX + point.y * edgePlane.normalY + point.z * edgePlane.normalZ - edgePlane.distance) < 1e-6) {
				++seamPoints; ok &= expect(near(a.at(point), b.at(point)), "CLI wrapped corner remains aligned");
			}
		}
		ok &= expect(seamPoints == 2 && read(mapPath) == original, "CLI wrapped edge covered without changing input");
		cli({"paste-surface", mapPath, "--clipboard", copied, "--target", "face:0:1", "--target", "face:0:99", "--output", output, "--overwrite"}, 4);
		ok &= expect(read(output) == saved, "invalid CLI batch leaves existing output intact");
		cli({"paste-surface", mapPath, "--clipboard", copied, "--target", "face:0:1", "--mode", "project", "--texture-size", "64,0", "--output", output}, 2);
		cli({"paste-surface", mapPath, "--clipboard", copied, "--target", "face:0:1", "--allow-valve220", "--output", output}, 2);
		cli({"paste-surface", mapPath, "--clipboard", copied, "--target", "face:0:1", "--unknown", "--output", output}, 2);
		ok &= write(copied, "{\"version\":999}");
		cli({"paste-surface", mapPath, "--clipboard", copied, "--target", "face:0:1", "--output", output, "--overwrite"}, 4);
		ok &= expect(read(output) == saved, "invalid portable data is not published");
	}
	std::cout << (ok ? "Surface clipboard checks passed.\n" : "Surface clipboard checks failed.\n");
	return ok ? 0 : 1;
}
