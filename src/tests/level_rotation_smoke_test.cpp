#include "core/level_document.h"
#include "core/level_patch.h"
#include "core/level_texture_mapping.h"
#include "core/map_geometry.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool ok, const char *what, const QString &error = {})
{
	if (!ok) {
		std::cerr << what << ": " << error.toStdString() << '\n';
	}
	return ok;
}
QString number(double n) { return QString::number(n, 'g', 15); }
QString point(const LevelMapVec3 &p) { return QStringLiteral("( %1 %2 %3 )").arg(number(p.x), number(p.y), number(p.z)); }
QByteArray fixture(const QString &kind)
{
	LevelMapDocument doc;
	LevelMapCreateRequest request;
	request.starterRoom = false;
	createLevelMap(request, &doc);
	addLevelMapBoxBrush(&doc, {-32, 16, -8, true}, {96, 80, 56, true}, QStringLiteral("studio/checker"));
	QStringList lines{QStringLiteral("// original source\n{\n\"classname\" \"worldspawn\"\n{")};
	if (kind.startsWith(QStringLiteral("brushDef"))) {
		lines << kind << QStringLiteral("{");
	}
	for (const auto &f : doc.brushes.first().faces) {
		QString geometry = point(f.p0) + ' ' + point(f.p1) + ' ' + point(f.p2);
		const auto p = planeFromPoints(f.p0, f.p1, f.p2);
		if (kind == QStringLiteral("brushDef3")) {
			geometry = QStringLiteral("( %1 %2 %3 %4 )").arg(number(p.normalX), number(p.normalY), number(p.normalZ), number(-p.distance));
		}
		if (kind.startsWith(QStringLiteral("brushDef"))) {
			lines << geometry + QStringLiteral(" ( ( 0.03125 /* uv note */ 0.0078125 0.125 ) ( -0.015625 0.0625 -0.75 ) ) "
											   "\"studio/checker\" 2 4 8 // face note");
		} else if (kind == QStringLiteral("valve220")) {
			const auto projection = levelTextureProjection(f);
			lines << geometry + QStringLiteral(" \"studio/checker\" [ %1 %2 %3 7 ] [ %4 %5 %6 9 ] 15 -0.5 2 2 4 8 // face note")
									.arg(number(projection.u.x), number(projection.u.y), number(projection.u.z), number(projection.v.x),
										 number(projection.v.y), number(projection.v.z));
		} else {
			lines << geometry + QStringLiteral(" \"studio/checker\" 7 /* uv note */ 9 15 -0.5 2 2 4 8 // face note");
		}
	}
	if (kind.startsWith(QStringLiteral("brushDef"))) {
		lines << QStringLiteral("}");
	}
	lines << QStringLiteral("}\n}\n// untouched entity\n{\n\"classname\" \"misc_model\"\n\"origin\" \"200 150 70\"\n\"angles\" \"20 30 "
							"40\"\n\"model\" \"models/test.md3\"\n}\n");
	return lines.join('\n').toUtf8();
}
bool load(const QByteArray &bytes, LevelMapDocument *doc, QString *error)
{
	return loadLevelMapBytes({QStringLiteral("rotation.map"), {}, QStringLiteral("idtech3")}, bytes, doc, error);
}
bool near(const QPointF &a, const QPointF &b, double tolerance = 0.001)
{
	return std::abs(a.x() - b.x()) < tolerance && std::abs(a.y() - b.y()) < tolerance;
}
bool aligned(const LevelMapBrush &before, const LevelMapBrush &after, const LevelMapRotationRequest &request)
{
	const auto geometry = solveBrushGeometry(before.faces);
	if (before.faces.size() != after.faces.size() || !geometry.solved) {
		return false;
	}
	for (int f = 0; f < before.faces.size(); ++f) {
		const auto a = levelTextureProjection(before.faces[f]), b = levelTextureProjection(after.faces[f]);
		if (!a.valid || !b.valid || a.normalizedCoordinates != b.normalizedCoordinates) {
			return false;
		}
		for (const auto &vertex : geometry.faces[f].points) {
			if (!near(a.at(vertex), b.at(rotateLevelPoint(vertex, request.pivot, request.axis, request.degrees)))) {
				return false;
			}
		}
	}
	return true;
}
LevelMapVec3 orient(const LevelMapVec3 &p, const LevelMapVec3 &angles)
{
	return rotateLevelVector(rotateLevelVector(rotateLevelVector(p, 0, angles.z), 1, angles.x), 2, angles.y);
}
bool vecNear(const LevelMapVec3 &a, const LevelMapVec3 &b)
{
	return a.valid && b.valid && std::abs(a.x - b.x) < 1e-8 && std::abs(a.y - b.y) < 1e-8 && std::abs(a.z - b.z) < 1e-8;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	bool ok = temp.isValid();
	QString error;
	for (const QString& kind :
		 {QStringLiteral("classic"), QStringLiteral("valve220"), QStringLiteral("brushDef"), QStringLiteral("brushDef3")}) {
		for (int axis = 0; axis < 3; ++axis) {
			std::cerr << kind.toStdString() << " axis " << axis << '\n';
			LevelMapDocument map;
			const auto original = fixture(kind);
			if (!expect(load(original, &map, &error), "load", error) || map.brushes.size() != 1) {
				return EXIT_FAILURE;
			}
			setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}});
			const auto before = map.brushes.first();
			LevelMapRotationRequest request{axis, 31.75, {12, -26, 8, true}, true, true};
			ok &= expect(rotateLevelMapSelection(&map, request, &error), "arbitrary locked rotation", error);
			ok &= expect(aligned(before, map.brushes.first(), request), "live UV invariance");
			const auto saved = serializeLevelMap(map);
			LevelMapDocument reopened;
			ok &= expect(saved.succeeded() && load(saved.bytes, &reopened, &error) && reopened.brushes.size() == 1, "roundtrip", error);
			if (reopened.brushes.isEmpty()) {
				return EXIT_FAILURE;
			}
			ok &= expect(aligned(before, reopened.brushes.first(), request), "saved UV invariance");
			ok &= expect(saved.bytes.contains("// original source") && saved.bytes.contains("// face note") &&
							 saved.bytes.contains("2 4 8") && saved.bytes.contains("\"angles\" \"20 30 40\""),
						 "comments, material flags and unselected model preserved");
			if (kind != QStringLiteral("valve220")) {
				ok &= expect(saved.bytes.contains("/* uv note */"), "interleaved UV comments preserved");
			}
			if (kind != QStringLiteral("classic")) {
				ok &= expect(reopened.brushes.first().primitiveKind == kind, "existing dialect preserved");
			}
			ok &= expect(map.undoStack.size() == 1 && undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == original,
						 "byte-exact undo", error);
			ok &= expect(redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == saved.bytes, "exact redo", error);
		}
	}
	LevelMapDocument classic;
	load(fixture(QStringLiteral("classic")), &classic, &error);
	setLevelMapSelection(&classic, {{LevelMapSelectionKind::QuakeBrush, 0}});
	const auto original = serializeLevelMap(classic).bytes;
	const auto revision = classic.revision;
	LevelMapRotationRequest request{2, 31.75, {0, 0, 0, true}, true, false};
	ok &= expect(!rotateLevelMapSelection(&classic, request, &error) && error.contains(QStringLiteral("Valve 220")) &&
					 classic.revision == revision && serializeLevelMap(classic).bytes == original,
				 "classic conversion is explicit and atomic", error);
	request.degrees = 90;
	ok &= expect(rotateLevelMapSelection(&classic, request, &error) && classic.brushes.first().primitiveKind == QStringLiteral("classic"),
				 "representable classic lock retains format", error);
	undoLevelMapEdit(&classic);
	request.degrees = 30;
	request.textureLock = false;
	ok &= expect(rotateLevelMapSelection(&classic, request, &error) &&
					 serializeLevelMap(classic).bytes.contains("7 /* uv note */ 9 15 -0.5 2"),
				 "unlocked parameters untouched", error);
	undoLevelMapEdit(&classic);
	for (double degrees : {0.0, 360.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), 360001.0}) {
		request.degrees = degrees;
		ok &= expect(!rotateLevelMapSelection(&classic, request, &error) && serializeLevelMap(classic).bytes == original,
					 "invalid angles atomic");
	}
	request = {2, 45, {32769, 0, 0, true}, true, true};
	ok &= expect(!rotateLevelMapSelection(&classic, request, &error), "invalid pivot rejected");
	// Required dialect conversion covers untouched brushes in the same undo.
	LevelMapDocument mixed;
	load(fixture(QStringLiteral("classic")), &mixed, &error);
	int unselected = -1;
	addLevelMapBoxBrush(&mixed, {300,0,0,true}, {364,64,64,true}, QStringLiteral("studio/other"), &unselected, &error);
	const auto untouched = mixed.brushes.last();
	const auto beforeConversion = serializeLevelMap(mixed).bytes;
	setLevelMapSelection(&mixed, {{LevelMapSelectionKind::QuakeBrush,0}});
	request = {2,31.75,{0,0,0,true},true,true};
	ok &= expect(rotateLevelMapSelection(&mixed,request,&error) && mixed.undoStack.last().brushResults.size() == 2,
		"map-wide conversion joins one undo",error);
	for (const auto& brush : mixed.brushes) {
		for (const auto& face : brush.faces) { ok &= expect(face.explicitTextureAxes,"no mixed classic/Valve faces after conversion"); }
	}
	ok &= expect(aligned(untouched,mixed.brushes.last(),{2,0,{0,0,0,true},true,false})
		&& vecNear(untouched.faces.first().p0,mixed.brushes.last().faces.first().p0),"unselected geometry and UV stay fixed");
	ok &= expect(undoLevelMapEdit(&mixed) && serializeLevelMap(mixed).bytes == beforeConversion,"map-wide conversion undo is exact");
	// A model entity composes orientation; malformed or unsupported scalar
	// orientations and positions cannot silently become a different game object.
	LevelMapDocument model;
	load(fixture(QStringLiteral("classic")), &model, &error);
	setLevelMapSelection(&model, {{LevelMapSelectionKind::Entity,1}});
	request = {0,31.75,{0,0,0,true},true,false};
	ok &= expect(rotateLevelMapSelection(&model,request,&error) && model.undoStack.last().entityResults.size() == 1
		&& model.undoStack.last().brushResults.isEmpty(),"model entity rotation uses shared transform",error);
	ok &= expect(serializeLevelMap(model).bytes.contains("models/test.md3"),"model asset path retained");
	undoLevelMapEdit(&model);
	setLevelMapEntityProperty(&model,1,QStringLiteral("angle"),QStringLiteral("90"));
	const auto scalarSource = serializeLevelMap(model).bytes;
	ok &= expect(!rotateLevelMapSelection(&model,request,&error) && error.contains(QStringLiteral("scalar angle"))
		&& serializeLevelMap(model).bytes == scalarSource,"unsupported scalar orientation rejected atomically",error);
	request.axis = 2; request.pivot = {-32768,-32768,0,true}; request.degrees = 180;
	ok &= expect(!rotateLevelMapSelection(&model,request,&error) && serializeLevelMap(model).bytes == scalarSource,"entity world bounds enforced",error);
	// Verify orientation composition at regular poses and both Euler poles.
	for (const auto &angles : {LevelMapVec3{20, 30, 40, true}, {90, 70, 20, true}, {-90, -120, 15, true}}) {
		for (int axis = 0; axis < 3; ++axis) {
			const auto result = rotateLevelAngles(angles, axis, 31.75);
			for (const auto &basis : {LevelMapVec3{1, 0, 0, true}, {0, 1, 0, true}, {0, 0, 1, true}}) {
				ok &= expect(vecNear(orient(basis, result), rotateLevelVector(orient(basis, angles), axis, 31.75)),
							 "entity orientation basis");
			}
		}
	}
	// Selected owners include their patches; UV travels with control points.
	LevelMapDocument patches;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	createLevelMap(create, &patches);
	LevelMapPatch patch;
	createLevelPatch({}, &patch);
	addLevelMapPatch(&patches, patch);
	setLevelMapSelection(&patches, {{LevelMapSelectionKind::Entity, 0}});
	request = {1, -23.25, {5, 6, 7, true}, true, false};
	ok &= expect(rotateLevelMapSelection(&patches, request, &error) && patches.patches.first().controlU == patch.controlU &&
					 patches.undoStack.last().patchResults.size() == 1,
				 "owner patch rotation", error);
	// Both binary WAD dialects: shared vertices move once, nodes become stale.
	for (const auto& game : {QStringLiteral("doom"), QStringLiteral("hexen")}) {
		LevelMapDocument wad;
		create.game = game;
		create.starterRoom = true;
		createLevelMap(create, &wad, &error);
		setLevelMapSelection(&wad, {{LevelMapSelectionKind::DoomSector, 0}, {LevelMapSelectionKind::DoomLinedef, 0}});
		const auto before = wad.doomVertices;
		const auto baselineNodes = wad.doomGeometryChanged;
		const auto baselineEdits = wad.doomGeometryEdits;
		request = {2, 31.75, {20, 30, 0, true}, true, false};
		ok &= expect(rotateLevelMapSelection(&wad, request, &error) && wad.doomGeometryChanged, "WAD rotation invalidates nodes", error);
		ok &= expect(wad.undoStack.last().vertexResults.size() == before.size(), "shared vertices deduplicated");
		for (int i = 0; i < before.size(); ++i) {
			const auto expected = rotateLevelPoint({before[i].x, before[i].y, 0, true}, request.pivot, 2, request.degrees);
			ok &= expect(wad.doomVertices[i].x == std::round(expected.x) && wad.doomVertices[i].y == std::round(expected.y),
						 "WAD rounds once");
		}
		const auto saved = serializeLevelMap(wad);
		LevelMapDocument reloaded;
		ok &= expect(saved.succeeded() &&
						 loadLevelMapBytes({QStringLiteral("test.wad"), QStringLiteral("MAP01"), {}}, saved.bytes, &reloaded, &error) &&
						 reloaded.doomVertices.first().x == wad.doomVertices.first().x,
					 "WAD save reload", error);
		ok &= expect(undoLevelMapEdit(&wad) && wad.doomGeometryChanged == baselineNodes && wad.doomGeometryEdits == baselineEdits && wad.doomVertices.first().x == before.first().x,
					 "WAD undo restores node state");
		setLevelMapSelection(&wad, {{LevelMapSelectionKind::DoomVertex, 0}});
		const auto a = wad.doomVertices[0], b = wad.doomVertices[1];
		request = {2, 180, {(a.x + b.x) / 2, (a.y + b.y) / 2, 0, true}, true, false};
		ok &= expect(!rotateLevelMapSelection(&wad, request, &error) && error.contains(QStringLiteral("collapse")),
					 "collapsed WAD edge refused", error);
	}
	if (argc > 1) {
		const QString input = QDir(temp.path()).filePath(QStringLiteral("in.map"));
		QFile file(input);
		if (!file.open(QIODevice::WriteOnly)) { return EXIT_FAILURE; }
		file.write(original);
		file.close();
		const QString output = QDir(temp.path()).filePath(QStringLiteral("out.map"));
		const auto run = [&](QStringList options, int expected) {
			QProcess process;
			process.start(QString::fromLocal8Bit(argv[1]),
						  QStringList{QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("rotate"), input,
									  QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--output"), output,
									  QStringLiteral("--json")} +
							  options);
			if (!process.waitForFinished(60000)) {
				process.kill();
				process.waitForFinished();
				return false;
			}
			const auto out = process.readAllStandardOutput();
			if (process.exitCode() != expected) {
				std::cerr << out.toStdString() << process.readAllStandardError().toStdString();
			}
			return process.exitCode() == expected && QJsonDocument::fromJson(out).isObject();
		};
		ok &= expect(run({QStringLiteral("--degrees"), QStringLiteral("30")}, 1) && !QFile::exists(output),
					 "CLI conversion failure writes nothing");
		ok &= expect(
			run({QStringLiteral("--degrees"), QStringLiteral("30"), QStringLiteral("--allow-valve220"), QStringLiteral("--dry-run")}, 0) &&
				!QFile::exists(output),
			"CLI dry run");
		ok &= expect(run({QStringLiteral("--degrees"), QStringLiteral("30"), QStringLiteral("--allow-valve220"), QStringLiteral("--pivot"),
						  QStringLiteral("10,20,30")},
						 0),
					 "CLI save");
		ok &= expect(run({QStringLiteral("--degrees"), QStringLiteral("30"), QStringLiteral("--turns"), QStringLiteral("1")}, 2),
					 "CLI ambiguous angle rejected");
		ok &= expect(run({QStringLiteral("--degrees"), QStringLiteral("nan")}, 2), "CLI nonfinite angle rejected");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
