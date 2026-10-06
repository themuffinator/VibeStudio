#include "core/level_brush.h"
#include "core/level_document.h"
#include "core/map_preview_mesh.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message, const QString &error = {})
{
	if (!value) {
		std::cerr << message << ": " << error.toStdString() << '\n';
	}
	return value;
}
QString number(double v) { return QString::number(v, 'g', 12); }
QString point(const LevelMapVec3 &p) { return QStringLiteral("( %1 %2 %3 )").arg(number(p.x), number(p.y), number(p.z)); }
QByteArray fixture(const QString &kind)
{
	LevelMapDocument document;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	createLevelMap(create, &document);
	addLevelMapBoxBrush(&document, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("studio/stone"));
	QStringList lines{QStringLiteral("// Outside comment"), QStringLiteral("{"), QStringLiteral("\"classname\" \"worldspawn\""),
					  QStringLiteral("\"custom\" \"keep this\""), QStringLiteral("{")};
	if (kind.startsWith(QStringLiteral("brushDef"))) {
		lines << kind << QStringLiteral("{");
	}
	lines << QStringLiteral("// inside brush") << QStringLiteral("/* block comment retained */");
	int i = 0;
	for (const auto &f : document.brushes.first().faces) {
		const auto p = planeFromPoints(f.p0, f.p1, f.p2);
		QString geometry = QStringLiteral("%1 %2 %3").arg(point(f.p0), point(f.p1), point(f.p2));
		const QString material = QStringLiteral("studio/face%1").arg(i++);
		if (kind == QStringLiteral("brushDef3")) {
			geometry = QStringLiteral("( %1 %2 %3 %4 )").arg(number(p.normalX), number(p.normalY), number(p.normalZ), number(-p.distance));
		}
		if (kind.startsWith(QStringLiteral("brushDef"))) {
			lines << geometry + QStringLiteral(" ( ( 0.125 0.025 0.25 ) ( 0.05 0.25 0.75 ) "
											   ") \"%1\" 2 4 8 // face note %2")
									.arg(material)
									.arg(i);
		} else if (kind == QStringLiteral("valve220")) {
			lines << geometry + QStringLiteral(" %1 [ 1 0 0 7 ] [ 0 0 -1 9 ] 15 0.5 "
											   "2 2 4 8 // face note %2")
									.arg(material)
									.arg(i);
		} else {
			lines << geometry + QStringLiteral(" %1 7 9 15 0.5 2 2 4 8 // face note %2").arg(material).arg(i);
		}
	}
	if (kind.startsWith(QStringLiteral("brushDef"))) {
		lines << QStringLiteral("}");
	}
	lines << QStringLiteral("}") << QStringLiteral("}") << QStringLiteral("// untouched entity")
		  << QStringLiteral("{\n\"classname\" \"info_player_start\"\n\"origin\" "
							"\"128 128 128\"\n}")
		  << QString();
	return lines.join(QLatin1Char('\n')).toUtf8();
}
bool load(const QByteArray &bytes, LevelMapDocument *map, QString *error)
{
	return loadLevelMapBytes({QStringLiteral("components.map"), {}, QStringLiteral("idtech3")}, bytes, map, error);
}
bool sameGeometry(const LevelMapBrush &a, const LevelMapBrush &b)
{
	LevelBrushTopology x, y;
	if (!levelBrushTopology(a, &x) || !levelBrushTopology(b, &y) || x.vertices.size() != y.vertices.size()) {
		return false;
	}
	for (const auto &p : x.vertices) {
		if (std::none_of(y.vertices.cbegin(), y.vertices.cend(), [&](const auto &q) {
				return std::abs(p.x - q.x) < 0.02 && std::abs(p.y - q.y) < 0.02 && std::abs(p.z - q.z) < 0.02;
			})) {
			return false;
		}
	}
	return true;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return EXIT_FAILURE;
	}
	bool ok = true;
	QString error;
	for (const auto &kind :
		 {QStringLiteral("classic"), QStringLiteral("valve220"), QStringLiteral("brushDef"), QStringLiteral("brushDef3")}) {
		LevelMapDocument map;
		const auto original = fixture(kind);
		ok &= expect(load(original, &map, &error), "load dialect fixture", error);
		if (map.brushes.size() != 1) {
			return EXIT_FAILURE;
		}
		LevelBrushTopology topology;
		auto brush = map.brushes.first();
		ok &= expect(levelBrushTopology(brush, &topology, &error) && topology.vertices.size() == 8 && topology.edges.size() == 12 &&
						 topology.faces.size() == 6,
					 "cube components", error);
		LevelBrushEditReport edit;
		ok &= expect(moveLevelBrushComponents(&brush, LevelBrushComponent::Vertex, {7}, {16, 8, 12, true}, 0, false, &edit, &error),
					 "bend corner through convex hull", error);
		ok &= expect(edit.changed && edit.facesAfter > 6 && edit.verticesAfter == 8 && edit.collapsedVertices == 0,
					 "bent faces split without losing vertices");
		ok &= expect(replaceLevelMapBrushGeometry(&map, 0, brush, &error), "commit authored brush", error);
		const auto saved = serializeLevelMap(map);
		LevelMapDocument reloaded;
		ok &= expect(saved.succeeded() && load(saved.bytes, &reloaded, &error) && reloaded.brushes.size() == 1 &&
						 sameGeometry(brush, reloaded.brushes.first()),
					 "saved hull matches live geometry", error);
		ok &= expect(reloaded.brushes.first().primitiveKind == kind, "primitive dialect retained");
		ok &= expect(saved.bytes.contains("// Outside comment") && saved.bytes.contains("/* block comment retained */") &&
						 saved.bytes.contains("\"custom\" \"keep this\"") && saved.bytes.contains("// untouched entity"),
					 "surrounding source and comments survive");
		for (int note = 1; note <= 6; ++note) {
			ok &= expect(saved.bytes.count(QByteArray("// face note ") + QByteArray::number(note)) == 1, "face comments retained once");
		}
		for (const auto &face : reloaded.brushes.first().faces) {
			ok &= expect(face.contentFlags == 2 && face.surfaceFlags == 4 && face.surfaceValue == 8 &&
							 face.textureName.startsWith(QStringLiteral("studio/face")),
						 "source face flags and material retained");
			if (face.explicitTextureMatrix) {
				ok &= expect(face.textureMatrix[0] == 0.125 && face.textureMatrix[5] == 0.75, "matrix survives new plane");
			} else {
				ok &= expect(face.scaleX == 0.5 && face.scaleY == 2 && face.rotation == 15, "texture parameters survive new plane");
			}
		}
		ok &=
			expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == original, "undo restores exact original text", error);
		ok &= expect(redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == saved.bytes, "redo restores authored text", error);
		const auto depth = map.undoStack.size();
		ok &= expect(replaceLevelMapBrushGeometry(&map, 0, map.brushes.first(), &error) && map.undoStack.size() == depth,
					 "no-op commit adds no undo", error);
		ok &= expect(setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error), "select edited brush", error);
		ok &= expect(setLevelMapBrushFaceProperty(&map, 0, 0, QStringLiteral("texture"), QStringLiteral("studio/new"), &error),
					 "material editor works after topology edit", error);
		ok &= expect(duplicateLevelMapSelection(&map, 128, 0, 0, &error) && map.brushes.size() == 2, "duplicate edited brush", error);
		const auto copy = serializeLevelMap(map);
		ok &= expect(load(copy.bytes, &reloaded, &error) && reloaded.brushes.size() == 2 && reloaded.brushes.last().boundsSolved,
					 "copied hull reloads", error);
		const auto recovery = writeLevelMapRecovery(map, temp.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
		LevelMapDocument recovered;
		ok &= expect(!recovery.isEmpty() && restoreLevelMapRecovery(recovery, &recovered, &error) &&
						 serializeLevelMap(recovered).bytes == copy.bytes,
					 "recovery preserves authored hull and copy", error);
		ok &= expect(buildLevelMapPreviewMesh(map).triangles > 12, "shared model preview triangulates authored brush");
		setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}});
		const auto clipboard = levelMapSelectionText(map, &error);
		LevelMapDocument pasted;
		LevelMapCreateRequest empty;
		empty.starterRoom = false;
		createLevelMap(empty, &pasted);
		ok &= expect(!clipboard.isEmpty() && pasteLevelMapText(&pasted, clipboard, &error) && pasted.brushes.size() == 1 &&
						 sameGeometry(pasted.brushes.first(), map.brushes.first()),
					 "clipboard preserves authored hull", error);
		const auto beforeDelete = serializeLevelMap(map).bytes;
		ok &= expect(deleteLevelMapSelection(&map, &error) && map.brushes.size() == 1 && undoLevelMapEdit(&map, &error) &&
						 serializeLevelMap(map).bytes == beforeDelete,
					 "delete and undo preserve component source replacement", error);
		setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}});
		int clipped = 0;
		ok &= expect(clipLevelMapSelection(&map, {32, -128, -128, true}, {32, -128, 128, true}, {32, 128, 128, true},
										   LevelMapClipKeep::Both, &clipped, &error) &&
						 clipped == 1,
					 "CSG clip accepts authored face bindings", error);
		ok &= expect(load(serializeLevelMap(map).bytes, &reloaded, &error) && reloaded.brushes.size() == 3,
					 "clipped authored pieces reload", error);
	}
	LevelMapDocument map;
	ok &= expect(load(fixture(QStringLiteral("classic")), &map, &error), "load operations fixture", error);
	const auto original = map.brushes.first();
	LevelBrushTopology base;
	levelBrushTopology(original, &base);
	for (int edge = 0; edge < base.edges.size(); ++edge) {
		auto brush = original;
		ok &= expect(moveLevelBrushComponents(&brush, LevelBrushComponent::Edge, {edge}, {8, 12, 16, true}, 0, false, nullptr, &error),
					 "every cube edge can be reshaped", error);
	}
	for (int face = 0; face < base.faces.size(); ++face) {
		auto brush = original;
		ok &= expect(moveLevelBrushComponents(&brush, LevelBrushComponent::Face, {face}, {8, 12, 16, true}, 0, false, nullptr, &error),
					 "every cube face can be moved", error);
	}
	for (int vertex = 0; vertex < 8; ++vertex) {
		for (int axis = 0; axis < 3; ++axis) {
			for (double step : {-16.0, -0.25, 0.125, 16.0}) {
				auto brush = original;
				LevelMapVec3 d{0, 0, 0, true};
				if (axis == 0) {
					d.x = step;
				} else if (axis == 1) {
					d.y = step;
				} else {
					d.z = step;
				}
				const bool moved = moveLevelBrushComponents(&brush, LevelBrushComponent::Vertex, {vertex}, d, 0, false, nullptr, &error);
				ok &= expect(moved, "axis and fractional corner edits",
							 QStringLiteral("vertex %1 axis %2 step %3: %4").arg(vertex).arg(axis).arg(step).arg(error));
			}
		}
	}
	auto brush = original;
	LevelBrushEditReport edit;
	ok &= expect(!moveLevelBrushComponents(&brush, LevelBrushComponent::Vertex, {0}, {48, 48, 48, true}, 0, false, &edit, &error) &&
					 sameGeometry(brush, original),
				 "interior vertex collapse requires explicit choice", error);
	ok &= expect(moveLevelBrushComponents(&brush, LevelBrushComponent::Vertex, {0}, {48, 48, 48, true}, 0, true, &edit, &error) &&
					 edit.collapsedVertices == 1 && edit.verticesAfter == 7,
				 "explicit collapse reports disappeared corner", error);
	brush = original;
	ok &= expect(!moveLevelBrushComponents(&brush, LevelBrushComponent::Vertex, {0, 2, 4, 6}, {0, 0, 64, true}, 0, true, nullptr, &error) &&
					 sameGeometry(brush, original),
				 "flattening rejected atomically", error);
	for (double invalid : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), 1e9}) {
		ok &= expect(!moveLevelBrushComponents(&brush, LevelBrushComponent::Vertex, {0}, {invalid, 0, 0, true}, 0, true, nullptr, &error),
					 "invalid numeric movement rejected");
	}
	ok &= expect(!moveLevelBrushComponents(&brush, LevelBrushComponent::Edge, {99}, {1, 0, 0, true}, 0, false, nullptr, &error),
				 "unknown component rejected");
	if (argc > 1) {
		const QString source = QDir(temp.path()).filePath(QStringLiteral("input.map"));
		QFile file(source);
		if (!file.open(QIODevice::WriteOnly)) {
			return EXIT_FAILURE;
		}
		file.write(fixture(QStringLiteral("classic")));
		file.close();
		const QString output = QDir(temp.path()).filePath(QStringLiteral("output.map"));
		const auto cli = [&](const QStringList &args, int expected, bool json = true) {
			QProcess process;
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"),
																	   QDir(temp.path()).filePath(QStringLiteral("settings.ini"))} +
														   args + (json ? QStringList{QStringLiteral("--json")} : QStringList{}));
			const bool complete = process.waitForFinished(30000);
			const auto bytes = process.readAllStandardOutput();
			ok &= expect(complete && process.exitCode() == expected, "CLI command exit", QString::fromUtf8(bytes));
			if (!json && args.contains(QStringLiteral("--quiet"))) {
				ok &= expect(bytes.isEmpty(), "quiet inspection suppresses narration");
			}
			return QJsonDocument::fromJson(bytes).object();
		};
		const auto inspected =
			cli({QStringLiteral("map"), QStringLiteral("brush-components"), source, QStringLiteral("--brush"), QStringLiteral("0")}, 0);
		ok &= expect(inspected.value(QStringLiteral("brush")).toObject().value(QStringLiteral("vertices")).toArray().size() == 8,
					 "CLI exposes solved vertex IDs");
		cli({QStringLiteral("map"), QStringLiteral("brush-components"), source, QStringLiteral("--brush"), QStringLiteral("0"),
		     QStringLiteral("--quiet")}, 0, false);
		const QStringList args{QStringLiteral("map"),
							   QStringLiteral("move-components"),
							   source,
							   QStringLiteral("--brush"),
							   QStringLiteral("0"),
							   QStringLiteral("--kind"),
							   QStringLiteral("vertex"),
							   QStringLiteral("--component"),
							   QStringLiteral("7"),
							   QStringLiteral("--delta"),
							   QStringLiteral("16,8,12"),
							   QStringLiteral("--output"),
							   output};
		cli(args + QStringList{QStringLiteral("--dry-run")}, 0);
		ok &= expect(!QFileInfo::exists(output), "CLI dry-run creates no output");
		cli(args, 0);
		LevelMapDocument loaded;
		ok &= expect(loadLevelMap({output, {}, {}}, &loaded, &error) && loaded.brushes.first().faceCount > 6,
					 "CLI movement persists split faces", error);
		cli(args, 4); // Existing output is protected (validation failure).
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
