#include "core/level_primitive.h"
#include "core/level_surface.h"
#include "tests/level_surface_test_helpers.h"
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
#include <numbers>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *label, const QString &error = {})
{
	if (!value) {
		std::cerr << label << ": " << error.toStdString() << '\n';
	}
	return value;
}
bool near(const LevelMapVec3 &a, const LevelMapVec3 &b)
{
	return a.valid && b.valid && std::max({std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)}) < 0.02;
}
double volume(const LevelMapBrush &brush)
{
	const auto geometry = solveBrushGeometry(brush.faces);
	double result = 0;
	for (const auto &face : geometry.faces) {
		if (face.points.size() < 3) {
			continue;
		}
		const auto a = face.points[0];
		for (int i = 1; i + 1 < face.points.size(); ++i) {
			const auto b = face.points[i], c = face.points[i + 1];
			result += a.x * (b.y * c.z - b.z * c.y) + a.y * (b.z * c.x - b.x * c.z) + a.z * (b.x * c.y - b.y * c.x);
		}
	}
	return std::abs(result / 6);
}
double expectedVolume(const QString &shape, double box)
{
	if (shape == QStringLiteral("box")) {
		return box;
	}
	if (shape == QStringLiteral("wedge")) {
		return box / 2;
	}
	// An octagonal cross section in the requested rectangle, with exact
	// frustum volumes for each sphere latitude interval. Independent of hulls.
	const double areaFactor = std::sin(std::numbers::pi / 4);
	if (shape == QStringLiteral("cylinder")) {
		return box * areaFactor;
	}
	if (shape == QStringLiteral("cone")) {
		return box * areaFactor / 3;
	}
	double sum = 0;
	for (int b = 0; b < 4; ++b) {
		const double t0 = std::numbers::pi * b / 4, t1 = std::numbers::pi * (b + 1) / 4;
		const double a = std::sin(t0), c = std::sin(t1);
		sum += (std::cos(t0) - std::cos(t1)) / 2 * (a * a + c * c + a * c) / 3;
	}
	return box * areaFactor * sum;
}
bool put(const QString &path, const QByteArray &data)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temporary;
	if (!temporary.isValid()) {
		return EXIT_FAILURE;
	}
	bool ok = true;
	QString error;
	const QStringList shapes{QStringLiteral("box"), QStringLiteral("wedge"), QStringLiteral("cylinder"), QStringLiteral("cone"),
							 QStringLiteral("sphere")};
	for (const auto &dialect :
		 {QStringLiteral("classic"), QStringLiteral("valve220"), QStringLiteral("brushDef"), QStringLiteral("brushDef3")}) {
		for (const auto &shape : shapes) {
			for (int axis = 0; axis < 3; ++axis) {
				std::cerr << dialect.toStdString() << ' ' << shape.toStdString() << " axis " << axis << '\n';
				LevelMapDocument doc;
				const auto original = tests::surfaceFixture(dialect);
				if (!expect(loadLevelMapBytes({QStringLiteral("primitive.map"), {}, QStringLiteral("idtech3")}, original, &doc, &error),
							"load", error)) {
					return EXIT_FAILURE;
				}
				const auto untouched = doc.brushes.first();
				LevelBrushPrimitiveRequest request;
				request.shape = shape;
				request.axis = axis;
				request.mins = {220, -96, 16, true};
				request.maxs = {348, 96, 80, true};
				request.texture = QStringLiteral("studio/new");
				int id = -1;
				if (!expect(addLevelMapBrushPrimitive(&doc, request, &id, &error), "create in map", error)) {
					ok = false;
					continue;
				}
				const auto &brush = doc.brushes.last();
				LevelBrushTopology topology;
				const int faces = shape == "box" ? 6 : shape == "wedge" ? 5 : shape == "cylinder" ? 10 : shape == "cone" ? 9 : 32;
				const int vertices = shape == "box" ? 8 : shape == "wedge" ? 6 : shape == "cylinder" ? 16 : shape == "cone" ? 9 : 26;
				ok &= expect(levelBrushTopology(brush, &topology, &error) && brush.faceCount == faces &&
								 topology.vertices.size() == vertices && vertices - topology.edges.size() + faces == 2,
							 "closed expected topology", error);
				ok &= expect(brush.primitiveKind == dialect && near(brush.mins, request.mins) && near(brush.maxs, request.maxs),
							 "dialect and bounds");
				const double expected = expectedVolume(shape, 128 * 192 * 64);
				ok &= expect(std::abs(volume(brush) - expected) < expected * 0.0001, "analytic volume", QString::number(volume(brush)));
				ok &= expect(id == brush.id && brush.entityId == 0 &&
								 std::all_of(brush.faces.cbegin(), brush.faces.cend(),
											 [&](const auto &face) { return face.textureName == request.texture; }) &&
								 doc.selection == QVector<LevelMapSelectionRef>{{LevelMapSelectionKind::QuakeBrush, id}} &&
								 doc.undoStack.size() == 1,
							 "owner, selection, material and single undo");
				const auto saved = serializeLevelMap(doc);
				LevelMapDocument reopened;
				ok &= expect(
					saved.succeeded() &&
						loadLevelMapBytes({QStringLiteral("saved.map"), {}, QStringLiteral("idtech3")}, saved.bytes, &reopened, &error) &&
						reopened.brushes.size() == 2,
					"save/reopen", error);
				if (reopened.brushes.size() != 2) {
					return EXIT_FAILURE;
				}
				ok &= expect(std::none_of(reopened.issues.cbegin(), reopened.issues.cend(),
										  [](const auto &issue) { return issue.code == QStringLiteral("non-integer-brush-coordinate"); }),
							 "Quake III primitives do not inherit qbsp fractional-coordinate advisories");
				ok &= expect(near(reopened.brushes.last().mins, request.mins) && reopened.brushes.last().faceCount == faces &&
								 reopened.brushes.last().primitiveKind == dialect,
							 "persisted primitive");
				ok &= expect(reopened.brushes.first().faces.first().textureMatrix == untouched.faces.first().textureMatrix &&
								 saved.bytes.contains("// flags") && saved.bytes.contains("// untouched point entity\r\n"),
							 "untouched source retained");
				ok &= expect(undoLevelMapEdit(&doc, &error) && serializeLevelMap(doc).bytes == original, "byte-exact undo", error);
				ok &= expect(redoLevelMapEdit(&doc, &error) && serializeLevelMap(doc).bytes == saved.bytes, "byte-exact redo", error);
				// Newly created faces immediately participate in the shared surface service.
				LevelSurfaceEditPlan plan;
				ok &= expect(prepareLevelSurfaceEdit(doc, {{id, 0}}, {LevelSurfaceOperation::Fit, 2, 1}, {{request.texture, {128, 64}}},
													 &plan, &error) &&
								 commitLevelSurfaceEdit(&doc, plan, &error),
							 "surface handoff", error);
			}
		}
	}
	// Odd cross sections, both low and maximum supported detail, and all axes.
	for (const auto &shape : {QStringLiteral("cylinder"), QStringLiteral("cone"), QStringLiteral("sphere")}) {
		for (int sides : {3, 5, 16, 64}) {
			LevelBrushPrimitiveRequest request;
			request.shape = shape;
			request.sides = sides;
			request.bands = sides == 64 ? 2 : 5;
			LevelMapBrush brush;
			ok &= expect(createLevelBrushPrimitive(request, &brush, &error) && near(brush.mins, request.mins) &&
							 near(brush.maxs, request.maxs),
						 "detail bounds", shape + QString::number(sides) + ": " + error);
		}
	}
	LevelMapDocument doc;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	createLevelMap(create, &doc);
	LevelBrushPrimitiveRequest fractional;
	fractional.shape = QStringLiteral("sphere");
	ok &= expect(addLevelMapBrushPrimitive(&doc, fractional, nullptr, &error), "fractional brush fixture", error);
	LevelMapDocument quake;
	ok &= expect(loadLevelMapBytes({QStringLiteral("quake.map"), {}, QStringLiteral("idtech2")}, serializeLevelMap(doc).bytes, &quake, &error),
		"Quake-family target fixture", error);
	ok &= expect(std::any_of(quake.issues.cbegin(), quake.issues.cend(),
							 [](const auto &issue) { return issue.code == QStringLiteral("non-integer-brush-coordinate"); }),
				 "qbsp fractional-coordinate advisory retained for Quake targets");
	createLevelMap(create, &doc);
	const auto baseline = serializeLevelMap(doc).bytes;
	const auto revision = doc.revision;
	for (int bad = 0; bad < 11; ++bad) {
		LevelBrushPrimitiveRequest request;
		if (bad == 0) {
			request.shape = QStringLiteral("torus");
		}
		if (bad == 1) {
			request.mins.x = std::numeric_limits<double>::quiet_NaN();
		}
		if (bad == 2) {
			request.maxs.z = 32769;
		}
		if (bad == 3) {
			request.maxs.x = request.mins.x;
		}
		if (bad == 4) {
			request.shape = QStringLiteral("cylinder");
			request.sides = 65;
		}
		if (bad == 5) {
			request.shape = QStringLiteral("sphere");
			request.sides = 64;
			request.bands = 16;
		}
		if (bad == 6) {
			request.texture = QStringLiteral("bad\nmaterial");
		}
		if (bad == 7) {
			request.texture = QStringLiteral("bad/*comment");
		}
		if (bad == 8) {
			request.texture = QStringLiteral("bad\"texture");
		}
		if (bad == 9) {
			request.axis = 3;
		}
		if (bad == 10) {
			request.mins.valid = false;
		}
		ok &= expect(!addLevelMapBrushPrimitive(&doc, request, nullptr, &error) && doc.revision == revision &&
						 serializeLevelMap(doc).bytes == baseline,
					 "invalid request is atomic", error);
	}
	// The first brush may have been created or dialect-converted without saving.
	loadLevelMapBytes({QStringLiteral("fresh.map"), {}, QStringLiteral("idtech3")}, tests::surfaceFixture(QStringLiteral("classic")), &doc);
	setLevelMapSelection(&doc, {{LevelMapSelectionKind::QuakeBrush, 0}});
	LevelMapRotationRequest rotation{2, 31.75, {0, 0, 0, true}, true, true};
	ok &= expect(rotateLevelMapSelection(&doc, rotation, &error), "pending dialect conversion", error);
	LevelBrushPrimitiveRequest request;
	request.shape = QStringLiteral("cone");
	ok &=
		expect(addLevelMapBrushPrimitive(&doc, request, nullptr, &error) && doc.brushes.last().primitiveKind == QStringLiteral("valve220"),
			   "new brush follows pending Valve conversion", error);
	LevelMapDocument copied;
	createLevelMap(create, &copied);
	setLevelMapSelection(&doc, {{LevelMapSelectionKind::QuakeBrush, 0}});
	ok &= expect(pasteLevelMapText(&copied, levelMapSelectionText(doc), &error) &&
					 addLevelMapBrushPrimitive(&copied, request, nullptr, &error) &&
					 copied.brushes.last().primitiveKind == QStringLiteral("valve220"),
				 "unsaved first brush supplies dialect", error);
	// Document format and malformed insertion bindings fail without source loss.
	LevelMapDocument wad;
	create.game = QStringLiteral("doom");
	createLevelMap(create, &wad);
	ok &= expect(!addLevelMapBrushPrimitive(&wad, request, nullptr, &error), "Doom does not accept solid brushes");
	if (argc > 1) {
		const auto input = QDir(temporary.path()).filePath(QStringLiteral("input.map"));
		const auto output = QDir(temporary.path()).filePath(QStringLiteral("output.map"));
		put(input, tests::surfaceFixture(QStringLiteral("brushDef")));
		const QStringList base{
			QStringLiteral("--cli"),	 QStringLiteral("map"),		   QStringLiteral("add-brush"), input,
			QStringLiteral("--shape"),	 QStringLiteral("sphere"),	   QStringLiteral("--sides"),	QStringLiteral("8"),
			QStringLiteral("--bands"),	 QStringLiteral("4"),		   QStringLiteral("--axis"),	QStringLiteral("y"),
			QStringLiteral("--mins"),	 QStringLiteral("0,0,0"),	   QStringLiteral("--maxs"),	QStringLiteral("64,128,96"),
			QStringLiteral("--texture"), QStringLiteral("studio/new"), QStringLiteral("--output"),	output,
			QStringLiteral("--json")};
		const auto run = [&](QStringList args, int expected) {
			QProcess process;
			process.start(QString::fromLocal8Bit(argv[1]), args);
			const bool finished = process.waitForFinished(30000);
			const auto bytes = process.readAllStandardOutput();
			ok &= expect(finished && process.exitCode() == expected, "CLI exit",
						 QString::fromUtf8(bytes) + QString::fromUtf8(process.readAllStandardError()));
			return QJsonDocument::fromJson(bytes).object();
		};
		run(base + QStringList{QStringLiteral("--dry-run")}, 0);
		ok &= expect(!QFile::exists(output), "dry-run writes no file");
		const auto result = run(base, 0);
		ok &= expect(result.value(QStringLiteral("shape")) == QStringLiteral("sphere") &&
						 result.value(QStringLiteral("faceCount")).toInt() == 32 &&
						 result.value(QStringLiteral("faceDialect")) == QStringLiteral("brushDef"),
					 "CLI structured primitive result");
		run(base, 4);
		auto invalid = base;
		invalid[invalid.indexOf(QStringLiteral("--sides")) + 1] = QStringLiteral("65");
		invalid << QStringLiteral("--overwrite");
		run(invalid, 1);
		auto badOption = base;
		badOption[badOption.indexOf(QStringLiteral("--bands")) + 1] = QStringLiteral("bad");
		run(badOption, 2);
		LevelMapDocument reopened;
		ok &= expect(loadLevelMap({output, {}, QStringLiteral("idtech3")}, &reopened, &error) && reopened.brushes.last().faceCount == 32,
					 "failed CLI preserves destination", error);
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
