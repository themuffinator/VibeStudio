#include "core/level_patch.h"
#include "tests/level_transform_test_helpers.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <limits>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
bool expect(bool ok, const char *what, const QString &error = {})
{
	if (!ok) {
		std::cerr << what << ": " << error.toStdString() << '\n';
	}
	return ok;
}
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
LevelMapVec3 mirror(LevelMapVec3 p, int axis)
{
	double &coordinate = axis == 0 ? p.x : axis == 1 ? p.y : p.z;
	coordinate = 2 * (axis == 0 ? 32 : axis == 1 ? 48 : 24) - coordinate;
	return p;
}
bool sameParameters(const LevelMapBrushFace &a, const LevelMapBrushFace &b)
{
	return a.shiftX == b.shiftX && a.shiftY == b.shiftY && a.rotation == b.rotation && a.scaleX == b.scaleX && a.scaleY == b.scaleY &&
		   a.textureMatrix == b.textureMatrix && a.uOffset == b.uOffset && a.vOffset == b.vOffset && a.uAxis.x == b.uAxis.x &&
		   a.uAxis.y == b.uAxis.y && a.uAxis.z == b.uAxis.z && a.vAxis.x == b.vAxis.x && a.vAxis.y == b.vAxis.y && a.vAxis.z == b.vAxis.z;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	bool ok = temp.isValid();
	QString error;
	const auto resized = [](const auto &p) {
		return LevelMapVec3{100 + (p.x + 32) * 2, -20 + (p.y - 16) * 0.5, 12 + (p.z + 8) * 1.25, true};
	};
	const auto moved = [](const auto &p) { return LevelMapVec3{p.x + 19.25, p.y - 8.5, p.z + 3.75, true}; };
	for (const auto &kind :
		 {QStringLiteral("classic"), QStringLiteral("valve220"), QStringLiteral("brushDef"), QStringLiteral("brushDef3")}) {
		std::cerr << "Affine texture tests: " << kind.toStdString() << '\n';
		for (int operation = 0; operation < 8; ++operation) {
			LevelMapDocument map;
			const auto original = transformFixture(kind);
			if (!expect(loadTransformFixture(original, &map, &error), "fixture", error)) {
				return EXIT_FAILURE;
			}
			setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}});
			const auto before = map.brushes.first();
			const auto transform = [&](const LevelMapVec3 &p) {
				if (operation == 0) {
					return moved(p);
				}
				if (operation == 1) {
					return resized(p);
				}
				if (operation < 5) {
					return mirror(p, operation - 2);
				}
				return rotateLevelPoint(p, {32, 48, 24, true}, operation - 5, 90);
			};
			bool applied = false;
			if (operation == 0) {
				applied = moveLevelMapSelection(&map, 19.25, -8.5, 3.75, {true, false}, &error);
			} else if (operation == 1) {
				applied = resizeLevelMapSelection(&map, {100, -20, 12, true}, {356, 12, 92, true}, {true, true}, &error);
			} else if (operation < 5) {
				applied = flipLevelMapSelection(&map, operation - 2, {true, false}, &error);
			} else {
				applied = rotateLevelMapSelection(&map, operation - 5, 1, &error);
			}
			if (!expect(applied, "transform applies", error)) {
				ok = false;
				continue;
			}
			ok &= expect(transformUvsMatch(before, map.brushes.first(), transform), "live UV invariance");
			ok &= expect(map.undoStack.size() == 1 && map.entities[1].origin.x == 200 && map.entities[1].origin.y == 150,
						 "one undo and unselected model untouched");
			const auto saved = serializeLevelMap(map);
			LevelMapDocument reloaded;
			ok &= expect(saved.succeeded() && loadTransformFixture(saved.bytes, &reloaded, &error) &&
							 transformUvsMatch(before, reloaded.brushes.first(), transform),
						 "saved UV invariance", error);
			ok &= expect(saved.bytes.contains("// untouched model") && saved.bytes.count("// face") == 6 && saved.bytes.count("2 4 8") == 6,
						 "source comments and surface flags preserved");
			ok &= expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == original, "exact undo source", error);
			ok &= expect(redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == saved.bytes, "exact redo result", error);
			// All disabled policies retain the source parameters in every dialect.
			loadTransformFixture(original, &map);
			setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}});
			if (operation == 0) {
				applied = moveLevelMapSelection(&map, 19.25, -8.5, 3.75, {false, false}, &error);
			} else if (operation == 1) {
				applied = resizeLevelMapSelection(&map, {100, -20, 12, true}, {356, 12, 92, true}, {false, false}, &error);
			} else if (operation < 5) {
				applied = flipLevelMapSelection(&map, operation - 2, {false, false}, &error);
			} else {
				applied = rotateLevelMapSelection(&map, {operation - 5, 90, {}, false, false}, &error);
			}
			ok &= expect(applied, "unlocked transform", error);
			for (int f = 0; f < before.faces.size(); ++f) {
				ok &= expect(sameParameters(before.faces[f], map.brushes[0].faces[f]), "unlocked parameters unchanged");
			}
		}
	}
	LevelMapDocument map;
	loadTransformFixture(transformFixture(QStringLiteral("classic")), &map);
	addLevelMapBoxBrush(&map, {256, 256, 0, true}, {320, 320, 64, true}, QStringLiteral("studio/other"));
	setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}});
	const auto original = serializeLevelMap(map).bytes;
	const auto history = map.undoStack.size();
	const auto revision = map.revision;
	const auto unselected = map.brushes[1];
	ok &= expect(!resizeLevelMapSelection(&map, {100, -20, 12, true}, {356, 12, 92, true}, {true, false}, &error) &&
					 error.contains(QStringLiteral("Valve 220")) && serializeLevelMap(map).bytes == original && map.revision == revision &&
					 map.undoStack.size() == history,
				 "required conversion refused atomically", error);
	ok &= expect(resizeLevelMapSelection(&map, {100, -20, 12, true}, {356, 12, 92, true}, {true, true}, &error) &&
					 map.brushes[1].faces[0].explicitTextureAxes && map.brushes[0].faces[0].explicitTextureAxes &&
					 transformUvsMatch(unselected, map.brushes[1], [](const auto &p) { return p; }),
				 "map-wide conversion leaves unselected appearance fixed", error);
	ok &= expect(undoLevelMapEdit(&map) && serializeLevelMap(map).bytes == original, "conversion and resizing undo together");
	const double nan = std::numeric_limits<double>::quiet_NaN();
	ok &= expect(!resizeLevelMapSelection(&map, {nan, 0, 0, true}, {200, 200, 200, true}, {true, true}, &error) &&
					 !resizeLevelMapSelection(&map, {0, 0, 0, true}, {0, 64, 64, true}, {true, true}, &error) &&
					 !moveLevelMapSelection(&map, nan, 0, 0, {true, false}, &error) &&
					 !moveLevelMapSelection(&map, 40000, 0, 0, {true, false}, &error) &&
					 !moveLevelMapSelectionSnapped(&map, 1, 0, 0, 16, {true, false}, &error) && serializeLevelMap(map).bytes == original &&
					 map.undoStack.size() == history,
				 "invalid transforms leave map and history intact");
	map.selection << LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush, 999};
	ok &= expect(!moveLevelMapSelection(&map, 8, 0, 0, {true, false}, &error) && serializeLevelMap(map).bytes == original,
				 "stale selection rejected");
	setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}});
	const auto beforeSnap = map.brushes[0];
	ok &= expect(moveLevelMapSelectionSnapped(&map, 20, 0, 0, 16, {true, false}, &error) &&
					 transformUvsMatch(beforeSnap, map.brushes[0], [](const auto &p) { return LevelMapVec3{p.x + 16, p.y, p.z, true}; }),
				 "snapped move locks the actual snapped delta", error);
	// Generic solver refuses singular/nonfinite input without modifying output.
	auto face = map.brushes[0].faces[0], copy = face;
	LevelTextureAffine singular;
	singular.columns[1] = singular.columns[0];
	ok &= expect(!lockTransformedLevelTexture(face, &copy, singular, true, &error) && sameParameters(face, copy),
				 "singular texture mapping is atomic");
	singular = {};
	singular.translation.x = nan;
	ok &= expect(!lockTransformedLevelTexture(face, &copy, singular, true, &error) && sameParameters(face, copy),
				 "nonfinite texture mapping rejected");
	// Shared entity+child selection transforms the child once and keeps assets.
	loadTransformFixture(transformFixture(QStringLiteral("valve220")), &map);
	setLevelMapSelection(&map,
						 {{LevelMapSelectionKind::Entity, 0}, {LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::Entity, 1}});
	const auto beforeGroup = map.brushes[0];
	ok &= expect(moveLevelMapSelection(&map, 19.25, -8.5, 3.75, {true, false}, &error) &&
					 transformUvsMatch(beforeGroup, map.brushes[0], moved) && map.entities[1].origin.x == 219.25 &&
					 serializeLevelMap(map).bytes.contains("models/test.md3"),
				 "group move preserves model reference and deduplicates brush", error);
	// Patches carry their UVs; mirroring reverses their columns and normals.
	LevelPatchCreateRequest patchRequest;
	patchRequest.texture = QStringLiteral("studio/checker");
	LevelMapPatch patch;
	ok &= expect(createLevelPatch(patchRequest, &patch, &error), "patch fixture", error);
	int patchId = -1;
	ok &= expect(addLevelMapPatch(&map, patch, &patchId, &error), "insert patch", error);
	setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakePatch, patchId}});
	const auto patchBefore = map.patches.last();
	ok &= expect(flipLevelMapSelection(&map, 0, {true, false}, &error), "flip patch", error);
	for (int row = 0; row < patchBefore.height; ++row) {
		for (int col = 0; col < patchBefore.width; ++col) {
			const int a = row * patchBefore.width + col, b = row * patchBefore.width + patchBefore.width - 1 - col;
			ok &= expect(map.patches.last().controlU[b] == patchBefore.controlU[a] &&
							 map.patches.last().controlV[b] == patchBefore.controlV[a],
						 "patch UV follows reversed column");
		}
	}
	if (argc > 1) {
		const QDir root(temp.path());
		const auto input = root.filePath(QStringLiteral("source.map"));
		ok &= write(input, transformFixture(QStringLiteral("classic")));
		const auto run = [&](const QString &command, const QStringList &arguments, int expected, const QString &policy) {
			QProcess process;
			process.start(QString::fromLocal8Bit(argv[1]),
						  QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"),
									  root.filePath(QStringLiteral("settings.ini")), QStringLiteral("map"), command} +
							  arguments +
							  QStringList{input, QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--output"),
										  root.filePath(QStringLiteral("out.map")), QStringLiteral("--dry-run"), QStringLiteral("--json")});
			if (!process.waitForFinished(30000)) {
				process.kill();
				process.waitForFinished();
				return expect(false, "CLI timeout");
			}
			const auto bytes = process.readAllStandardOutput();
			const auto json = QJsonDocument::fromJson(bytes).object();
			return expect(process.exitCode() == expected && !json.isEmpty() &&
							  (policy.isEmpty() || json.value(QStringLiteral("textureLockPolicy")).toString() == policy),
						  "CLI result", QString::fromUtf8(bytes) + QString::fromUtf8(process.readAllStandardError()));
		};
		ok &= run(QStringLiteral("move"), {QStringLiteral("--delta"), QStringLiteral("16,0,0")}, 0, QStringLiteral("locked"));
		ok &= run(QStringLiteral("flip"),
				  {QStringLiteral("--axis"), QStringLiteral("y"), QStringLiteral("--texture-lock"), QStringLiteral("off")}, 0,
				  QStringLiteral("source-parameters"));
		ok &= run(QStringLiteral("rotate"), {QStringLiteral("--turns"), QStringLiteral("-1")}, 0, QStringLiteral("locked"));
		ok &=
			run(QStringLiteral("resize"), {QStringLiteral("--size"), QStringLiteral("256,32,80")}, 0, QStringLiteral("source-parameters"));
		ok &= run(QStringLiteral("resize"),
				  {QStringLiteral("--size"), QStringLiteral("256,32,80"), QStringLiteral("--texture-lock"), QStringLiteral("on")}, 1, {});
		ok &= run(QStringLiteral("resize"),
				  {QStringLiteral("--size"), QStringLiteral("256,32,80"), QStringLiteral("--texture-lock"), QStringLiteral("on"),
				   QStringLiteral("--allow-valve220")},
				  0, QStringLiteral("locked"));
		for (const auto &command : {QStringLiteral("move"), QStringLiteral("flip"), QStringLiteral("rotate"), QStringLiteral("resize")}) {
			ok &= run(command,
					  {QStringLiteral("--texture-lock"), QStringLiteral("on"), QStringLiteral("--texture-lock"), QStringLiteral("off")}, 2,
					  {});
			ok &= run(command, {QStringLiteral("--texture-lock"), QStringLiteral("invalid")}, 2, {});
		}
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
