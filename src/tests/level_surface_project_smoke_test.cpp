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
#include <numbers>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message, const QString& detail = {})
{
	if (!value) { std::cerr << message << ": " << detail.toStdString() << '\n'; } return value;
}
bool load(const QByteArray& bytes, LevelMapDocument* document, QString* error)
{
	return loadLevelMapBytes({"project.map", {}, "idtech3"}, bytes, document, error);
}
bool near(double a, double b) { return std::abs(a - b) < 1e-5; }
double dot(LevelMapVec3 a, LevelMapVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
// Independent evaluation from serialized native fields. No production
// projection solver or setter participates in the coordinate oracle.
QPointF imageUv(const LevelMapBrushFace& face, LevelMapVec3 point, QSize size)
{
	if (face.explicitTextureAxes) { return {(dot(face.uAxis, point) / face.scaleX + face.uOffset) / size.width(), (dot(face.vAxis, point) / face.scaleY + face.vOffset) / size.height()}; }
	const auto p = face.explicitPlane ? MapPlane{face.planeNormal.x, face.planeNormal.y, face.planeNormal.z, -face.planeDistance, true}
		: planeFromPoints(face.p0, face.p1, face.p2);
	if (face.explicitTextureMatrix) {
		const double horizontal = std::hypot(p.normalX, p.normalY);
		const LevelMapVec3 s = horizontal < 1e-6 ? LevelMapVec3{0, 1, 0, true} : LevelMapVec3{-p.normalY / horizontal, p.normalX / horizontal, 0, true};
		const LevelMapVec3 t = horizontal < 1e-6 ? LevelMapVec3{p.normalZ > 0 ? 1.0 : -1.0, 0, 0, true}
			: LevelMapVec3{p.normalX * p.normalZ / horizontal, p.normalY * p.normalZ / horizontal, -horizontal, true};
		const auto& m = face.textureMatrix; return {m[0] * dot(s, point) + m[1] * dot(t, point) + m[2], m[3] * dot(s, point) + m[4] * dot(t, point) + m[5]};
	}
	double first = point.x, second = point.y;
	if (std::abs(p.normalX) > std::abs(p.normalZ) + 0.0001 && std::abs(p.normalX) >= std::abs(p.normalY) - 0.0001) { first = point.y; second = point.z; }
	else if (std::abs(p.normalY) > std::abs(p.normalZ) + 0.0001) { second = point.z; }
	const double radians = face.rotation * std::numbers::pi / 180, c = std::cos(radians), s = std::sin(radians);
	return {((first * c + second * s) / face.scaleX + face.shiftX) / size.width(), ((first * s - second * c) / face.scaleY + face.shiftY) / size.height()};
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); bool ok = true; QString error; int patchSamples = 0, faceSamples = 0;
	QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	for (const auto& dialect : {"classic", "valve220", "brushDef", "brushDef3"}) {
		for (bool fixed : {false, true}) {
			LevelMapDocument base; ok &= load(tests::surfaceFixture(dialect), &base, &error);
			LevelMapPatch patch; LevelPatchCreateRequest create; create.texture = "studio/patch"; create.center = {8, -16, 32, true};
			ok &= createLevelPatch(create, &patch, &error); patch.fixedSubdivisions = fixed;
			if (fixed) { patch.textureName = "textures/studio/patch"; patch.subdivisionsX = 3; patch.subdivisionsY = 4; }
			patch.controlPoints[4].z += 24;
			ok &= addLevelMapPatch(&base, patch, nullptr, &error);
			// Exercise actual loaded source bindings and unrelated CRLF text.
			auto sourceBytes = serializeLevelMap(base).bytes;
			sourceBytes.replace(fixed ? "patchDef3" : "patchDef2", fixed ? "patchDef3 // patch comment" : "patchDef2 // patch comment");
			ok &= load(sourceBytes, &base, &error);
			ok &= setLevelMapSelection(&base, {{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakePatch, 0}}, &error);
			LevelSurfaceClipboard clipboard; ok &= copyLevelSurface(base, {0, 0}, &clipboard, &error);
			auto json = QJsonDocument::fromJson(serializeLevelSurfaceClipboard(clipboard)).object();
			json.insert("material", "studio/copied"); json.insert("flags", QJsonArray{32, 64, 128});
			ok &= parseLevelSurfaceClipboard(QJsonDocument(json).toJson(), &clipboard, &error);
			const auto original = serializeLevelMap(base).bytes;
			for (bool mappingOnly : {false, true}) {
				auto document = base; LevelSurfaceEditPlan plan;
				LevelSurfacePasteOptions options; options.mode = LevelSurfacePasteMode::RadiantProject; options.mappingOnly = mappingOnly; options.includeSelection = true;
				options.textureSize = {128, 64};
				for (const auto& name : {"studio/grid", "studio/patch", "textures/studio/patch", "studio/copied"}) { options.materialSizes.insert(name, {64, 256}); }
				const QVector<LevelMaterialTarget> hit{{LevelMaterialKind::BrushFace, 0, 0}};
				const auto required = levelSurfaceTransferRequiredMaterials(document, hit, clipboard, options);
				ok &= expect(required.sourceSizeRequired == clipboard.face().explicitTextureMatrix
					&& required.targets.contains(mappingOnly ? patch.textureName : QStringLiteral("studio/copied")), "patch dimensions participate in shared lookup");
				if (!expect(prepareLevelSurfaceTransfer(document, hit, clipboard, options, &plan, &error) && commitLevelSurfaceEdit(&document, plan, &error), "native project", error)) { return 1; }
				const auto saved = serializeLevelMap(document).bytes;
				ok &= expect(saved.contains("// patch comment") && saved.contains("// untouched point entity")
					&& saved.contains("/* retained */") == original.contains("/* retained */"), "projection preserves source comments and unrelated objects", dialect);
				LevelMapDocument reopened; ok &= load(saved, &reopened, &error);
				const auto& projected = reopened.patches[0]; const auto& before = base.patches[0];
				ok &= expect(projected.fixedSubdivisions == fixed && projected.subdivisionsX == before.subdivisionsX && projected.subdivisionsY == before.subdivisionsY
					&& projected.headerTail == before.headerTail && projected.entityId == before.entityId, "patch topology, subdivisions and owner preserved");
				ok &= expect(projected.textureName == (mappingOnly ? before.textureName : QStringLiteral("studio/copied")), "patch material option respected");
				for (int i = 0; i < projected.controlPoints.size(); ++i) {
					const auto point = before.controlPoints[i]; const auto expected = imageUv(clipboard.face(), point, {128, 64});
					ok &= expect(near(projected.controlPoints[i].x, point.x) && near(projected.controlPoints[i].y, point.y) && near(projected.controlPoints[i].z, point.z)
						&& near(projected.controlU[i], expected.x() * 2) && near(projected.controlV[i], expected.y() / 4), "serialized patch UVs follow copied world texels at every control point", dialect); ++patchSamples;
				}
				const auto geometry = solveBrushGeometry(reopened.brushes[0].faces);
				for (int f = 0; f < reopened.brushes[0].faces.size(); ++f) {
					const auto& face = reopened.brushes[0].faces[f]; const auto& source = clipboard.face();
					ok &= expect(face.textureName == (mappingOnly ? "studio/grid" : "studio/copied")
						&& face.surfaceFlags == (!mappingOnly && f == 0 ? 64 : 4), "hit-only flags and target material policy");
					if (source.explicitTextureMatrix) {
						ok &= expect(face.textureMatrix[2] >= 0 && face.textureMatrix[2] < 1 && face.textureMatrix[5] >= 0 && face.textureMatrix[5] < 1, "native matrix repeat normalization");
						QPointF offset; bool first = true;
						for (const auto& point : geometry.faces[f].points) {
							const auto from = imageUv(source, point, {128, 64}), to = imageUv(face, point, {64, 256});
							const QPointF difference(to.x() - from.x() * 2, to.y() - from.y() / 4);
							if (first) { offset = difference; first = false; }
							ok &= expect(near(difference.x(), std::round(offset.x())) && near(difference.y(), std::round(offset.y())), "serialized brush matrix world projection modulo constant repeats"); ++faceSamples;
						}
					} else {
						ok &= expect(face.rotation == source.rotation && face.scaleX == source.scaleX && face.scaleY == source.scaleY, "native projection retains stored rotation and scale");
						if (source.explicitTextureAxes) {
							ok &= expect(face.uAxis.x == source.uAxis.x && face.uAxis.y == source.uAxis.y && face.uAxis.z == source.uAxis.z
								&& face.vAxis.x == source.vAxis.x && face.vAxis.y == source.vAxis.y && face.vAxis.z == source.vAxis.z
								&& face.uOffset == source.uOffset && face.vOffset == source.vOffset, "native projection copies Valve axes including edge-on faces");
						} else { ok &= expect(face.shiftX == source.shiftX && face.shiftY == source.shiftY, "classic projection is face-relative parameter paste"); }
					}
				}
				ok &= expect(plan.edgeOnFaceCount() == (QLatin1String(dialect) == QLatin1String("classic") ? 0 : 4), "edge-on projections remain usable and are reported");
				ok &= expect(document.selection == base.selection && document.undoStack.size() == base.undoStack.size() + 1 && plan.patchCount() == 1, "one mixed undo transaction");
				ok &= expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == original
					&& redoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == saved, "exact mixed projection undo and redo");
				auto stale = base; stale.patches[0].controlU[0] += 1;
				ok &= expect(!commitLevelSurfaceEdit(&stale, plan, &error), "changed patch UVs invalidate prepared projection");
				auto missing = options; missing.textureSize = {}; missing.materialSizes.clear();
				ok &= expect(!prepareLevelSurfaceTransfer(base, hit, clipboard, missing, &plan, &error) && !plan.ready(), "missing patch dimensions reject entire selection");
				ok &= expect(!prepareLevelSurfaceTransfer(base, hit, clipboard, options, &plan, &error, [] { return true; }), "cancelled projection remains unpublished");
				auto shared = base; shared.textLines[shared.patches[0].startLine - 1] = QStringLiteral("{ /* shared boundary */");
				const auto sharedBytes = serializeLevelMap(shared).bytes;
				ok &= expect(prepareLevelSurfaceTransfer(shared, hit, clipboard, options, &plan, &error)
					&& !commitLevelSurfaceEdit(&shared, plan, &error) && serializeLevelMap(shared).bytes == sharedBytes, "unowned patch boundary refuses mixed rewrite atomically");
				auto otherFamily = clipboard;
				LevelMapDocument other; ok &= load(tests::surfaceFixture(QLatin1String(dialect) == QLatin1String("classic") ? "brushDef" : "classic"), &other, &error)
					&& copyLevelSurface(other, {0, 0}, &otherFamily, &error);
				ok &= expect(!prepareLevelSurfaceTransfer(base, hit, otherFamily, options, &plan, &error), "native projection never silently changes brush format");
				if (!mappingOnly && !fixed && argc > 1) {
					const auto write = [](const QString& path, const QByteArray& bytes) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(); };
					const auto input = temp.filePath(QString::fromLatin1(dialect) + ".map"), clip = temp.filePath("surface.json"), output = temp.filePath("projected.map");
					ok &= write(input, original) && write(clip, serializeLevelSurfaceClipboard(clipboard));
					const auto cli = [&](bool dry) {
						QProcess process; QStringList args{"--settings-file", temp.filePath("settings.ini"), "--cli", "map", "paste-surface", input, "--clipboard", clip,
							"--target", "face:0:1", "--object", "brush:0", "--object", "patch:0", "--mode", "radiant-project", "--texture-size", "128,64",
							"--material-size", "studio/copied=64,256", "--output", output, "--overwrite", "--json"};
						if (dry) { args << "--dry-run"; } process.start(QString::fromLocal8Bit(argv[1]), args);
						const bool done = process.waitForFinished(30000); const auto bytes = process.readAllStandardOutput();
						ok &= expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, "CLI native projection", QString::fromUtf8(bytes + process.readAllStandardError()));
						const auto report = QJsonDocument::fromJson(bytes).object();
						ok &= expect(report.value("changedPatches").toInt() == 1 && report.value("edgeOnFaces").toInt() == (QLatin1String(dialect) == QLatin1String("classic") ? 0 : 4), "CLI reports native projection results");
					};
					cli(true); cli(false); QFile file(output); ok &= expect(file.open(QIODevice::ReadOnly) && file.readAll() == saved, "CLI and core serialize identical mixed projection");
				}
			}
			QString layer; ok &= createLevelSceneNode(&base, LevelSceneNodeKind::Layer, "Locked patch", {}, &layer, &error)
				&& assignLevelSceneObjects(&base, layer, {"patch:0"}, &error) && setLevelSceneLocked(&base, layer, true, &error);
			LevelSurfacePasteOptions options; options.mode = LevelSurfacePasteMode::RadiantProject; options.textureSize = {128, 64}; options.includeSelection = true;
			LevelSurfaceEditPlan plan; const auto locked = serializeLevelMap(base).bytes;
			ok &= expect(prepareLevelSurfaceTransfer(base, {{LevelMaterialKind::BrushFace, 0, 0}}, clipboard, options, &plan, &error)
				&& !commitLevelSurfaceEdit(&base, plan, &error) && serializeLevelMap(base).bytes == locked, "selected patch lock prevents entire projection");
		}
	}
	std::cout << "Verified " << patchSamples << " serialized patch UVs and " << faceSamples << " serialized matrix samples.\n";
	return ok ? 0 : 1;
}
