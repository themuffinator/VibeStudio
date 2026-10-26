// Create Brush from Points and Copy Vertex Coordinates (core/level_brush_points.h):
// every accepted point form and its errors; the envelope's corners, faces and
// absorbed points, and flat, short and out-of-range lists; the new brush in
// worldspawn, selected, in the map's face format as one exact undo and redo
// step, in an open group, refused in a locked destination and followed by a
// linked copy; full-precision listings of brushes, faces, edges, vertices and
// patch control points that read back exactly; and, with the vibestudio
// binary as the first argument, `map add-brush --points` and
// `map brush-components` with several brushes. Synthetic maps and owned
// temporary files only.
#include "core/level_brush_points.h"
#include "core/level_document.h"
#include "core/level_linked_groups.h"
#include "core/level_patch.h"
#include "core/level_placement_control_p.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_surface_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace vibestudio;

namespace {

bool ok = true;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << label << ": " << qPrintable(detail) << std::endl;
	}
	return value;
}

bool same(const QVector<LevelMapVec3>& a, const QVector<LevelMapVec3>& b)
{
	if (a.size() != b.size()) {
		return false;
	}
	for (qsizetype i = 0; i < a.size(); ++i) {
		if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z) {
			return false;
		}
	}
	return true;
}

QString boxBrush(double x0, double y0, double z0, double x1, double y1, double z1)
{
	const auto p = [](double x, double y, double z) { return QStringLiteral("( %1 %2 %3 )").arg(x).arg(y).arg(z); };
	const auto face = [&](const QString& a, const QString& b, const QString& c) { return QStringLiteral("%1 %2 %3 base/box 0 0 0 1 1").arg(a, b, c); };
	return QStringList {QStringLiteral("{"), face(p(x0 + 1, y0, z1), p(x0, y0, z1), p(x0, y0 + 1, z1)),
		face(p(x0, y0 + 1, z0), p(x0, y0, z0), p(x0 + 1, y0, z0)), face(p(x1, y0 + 1, z0), p(x1, y0, z0), p(x1, y0, z0 + 1)),
		face(p(x0, y0, z0 + 1), p(x0, y0, z0), p(x0, y0 + 1, z0)), face(p(x0, y1, z0 + 1), p(x0, y1, z0), p(x0 + 1, y1, z0)),
		face(p(x0 + 1, y0, z0), p(x0, y0, z0), p(x0, y0, z0 + 1)), QStringLiteral("}")}
		.join(QLatin1Char('\n'));
}

// Worldspawn brushes 0 (a 64 cube) and 1, and brush 2 in a func_door.
QByteArray fixture()
{
	return QStringList {QStringLiteral("// brush points fixture"), QStringLiteral("{"), QStringLiteral("\"classname\" \"worldspawn\""),
		boxBrush(0, 0, 0, 64, 64, 64), boxBrush(128, 0, 0, 192, 32, 64), QStringLiteral("}"), QStringLiteral("{"),
		QStringLiteral("\"classname\" \"func_door\""), boxBrush(0, 128, 0, 64, 192, 64), QStringLiteral("}")}
		.join(QStringLiteral("\r\n"))
		.toUtf8()
		+ "\r\n";
}

QByteArray bytes(const LevelMapDocument& document)
{
	return serializeLevelMap(document).bytes;
}

const LevelMapBrush* brushById(const LevelMapDocument& document, int id)
{
	for (const LevelMapBrush& brush : document.brushes) {
		if (brush.id == id) {
			return &brush;
		}
	}
	return nullptr;
}

// A wedge: a 128 by 128 right triangle 64 high, six corners and five faces.
QVector<LevelMapVec3> wedge()
{
	return {{0, 0, 0, true}, {128, 0, 0, true}, {0, 128, 0, true}, {0, 0, 64, true}, {128, 0, 64, true}, {0, 128, 64, true}};
}

QJsonObject runCli(const QString& binary, const QStringList& args, int expected, QByteArray* text = nullptr)
{
	QProcess process;
	process.start(binary, args);
	const bool finished = process.waitForFinished(60000);
	const QByteArray out = process.readAllStandardOutput();
	expect(finished && process.exitCode() == expected, "CLI exit code",
		QStringLiteral("%1 for %2: %3 %4").arg(process.exitCode()).arg(args.join(QLatin1Char(' ')), QString::fromUtf8(out),
			QString::fromUtf8(process.readAllStandardError())));
	if (text) {
		*text = out;
	}
	return QJsonDocument::fromJson(out).object();
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QString error;

	// Point forms: x,y,z groups, x y z lines, TrenchBroom's (x y z), comments.
	QVector<LevelMapVec3> points;
	const QVector<LevelMapVec3> four {{0, 0, 0, true}, {64, 0, 0, true}, {0, 64, 0, true}, {0, 0, -64.5, true}};
	for (const QString& text : {QStringLiteral("0,0,0 64,0,0 0,64,0 0,0,-64.5"), QStringLiteral("0, 0, 0; 64,0,0\n0,64,0  0,0,-64.5"),
			 QStringLiteral("0 0 0\n64 0 0\n0 64 0\n0 0 -64.5\n"), QStringLiteral("0 0 0 64 0 0\r\n0 64 0 0 0 -64.5"),
			 QStringLiteral("(0 0 0) (64 0 0) (0 64 0) (0 0 -64.5)"), QStringLiteral("( 0, 0, 0 )\n( 64 0 0 ) ( 0 64 0 ), (0 0 -6.45e1)"),
			 QStringLiteral("// brush:0\n0,0,0 64,0,0 // first two\n0,64,0 0,0,-64.5")}) {
		expect(parseLevelBrushPoints(text, &points, &error) && same(points, four), "point form reads", text + QStringLiteral(" | ") + error);
	}
	expect(parseLevelBrushPoints(QStringLiteral(" \n// nothing\n"), &points, &error) && points.isEmpty() && error.isEmpty(), "empty text has no points");
	for (const QString& bad : {QStringLiteral("0,0 64,0,0"), QStringLiteral("0 0 0 64 0"), QStringLiteral("0,0,x 1,1,1"), QStringLiteral("(0 0 0) 64 0 0"),
			 QStringLiteral("(0 0) (1 1 1)"), QStringLiteral("0,0,0 1,1,1,1"), QStringLiteral("(0 0 0"), QStringLiteral("0,0,inf 1,2,3"), QStringLiteral("0,0,nan 1,2,3")}) {
		points = four;
		expect(!parseLevelBrushPoints(bad, &points, &error) && points.isEmpty() && !error.isEmpty(), "malformed points refused", bad);
	}
	QString many;
	for (int i = 0; i <= kLevelBrushPointsMaximum; ++i) {
		many += QStringLiteral("%1,0,0 ").arg(i % 64);
	}
	expect(!parseLevelBrushPoints(many, &points, &error) && error.contains(QString::number(kLevelBrushPointsMaximum)), "at most 4096 points", error);

	// The envelope: corners, faces and absorbed points.
	LevelBrushTopology topology;
	expect(levelBrushPointsHull(four, QStringLiteral("studio/new"), &topology, &error) && topology.vertices.size() == 4 && topology.faces.size() == 4,
		"four points make a tetrahedron", error);
	QVector<LevelMapVec3> cube;
	for (int i = 0; i < 8; ++i) {
		cube << LevelMapVec3 {i & 1 ? 64.0 : 0.0, i & 2 ? 64.0 : 0.0, i & 4 ? 64.0 : 0.0, true};
	}
	QVector<LevelMapVec3> crowded = cube;
	crowded << LevelMapVec3 {32, 32, 32, true} << LevelMapVec3 {0, 0, 0, true} << LevelMapVec3 {32, 0, 0, true} << LevelMapVec3 {32, 32, 64, true};
	expect(levelBrushPointsHull(crowded, QStringLiteral("studio/new"), &topology, &error) && topology.vertices.size() == 8 && topology.faces.size() == 6,
		"interior, duplicate, edge and face points are absorbed", error);
	expect(levelBrushPointsHull(wedge(), QStringLiteral("studio/new"), &topology, &error) && topology.vertices.size() == 6 && topology.faces.size() == 5,
		"a wedge", error);
	const QVector<LevelMapVec3> flat {{0, 0, 0, true}, {64, 0, 0, true}, {0, 64, 0, true}, {64, 64, 0.01, true}};
	const QVector<LevelMapVec3> line {{0, 0, 0, true}, {64, 0, 0, true}, {128, 0, 0, true}, {32, 0.01, 0, true}};
	const QVector<LevelMapVec3> spot {{8, 8, 8, true}, {8, 8, 8, true}, {8, 8, 8.01, true}, {8, 8, 8, true}};
	expect(!levelBrushPointsHull(flat, QStringLiteral("x"), &topology, &error) && error.contains(QStringLiteral("plane")), "flat points refused", error);
	expect(!levelBrushPointsHull(line, QStringLiteral("x"), &topology, &error) && error.contains(QStringLiteral("line")), "points on a line refused", error);
	expect(!levelBrushPointsHull(spot, QStringLiteral("x"), &topology, &error) && error.contains(QStringLiteral("place")), "one place refused", error);
	expect(!levelBrushPointsHull(four.mid(0, 3), QStringLiteral("x"), &topology, &error) && error.contains(QStringLiteral("3")), "three points refused", error);
	QVector<LevelMapVec3> far = four;
	far[2].y = 40000;
	expect(!levelBrushPointsHull(far, QStringLiteral("x"), &topology, &error) && error.contains(QStringLiteral("3")), "out of range point named", error);

	// Into a map: worldspawn, selected, the map's face format, exact undo/redo.
	LevelMapDocument document;
	expect(loadLevelMapBytes({QStringLiteral("points.map"), {}, QStringLiteral("idtech2")}, fixture(), &document, &error), "load fixture", error);
	setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 1}});
	const QByteArray before = bytes(document);
	const auto depth = document.undoStack.size();
	LevelBrushPointsRequest request;
	request.points = crowded;
	for (auto& point : request.points) {
		point.x += 256;
	}
	request.texture = QStringLiteral(" studio/new ");
	LevelBrushPointsReport report;
	expect(addLevelMapBrushFromPoints(&document, request, &report, &error) && report.brushId == 3 && report.inputPoints == 12 && report.corners == 8
			&& report.unusedPoints == 4 && report.faces == 6 && report.faceDialect == QStringLiteral("classic"),
		"brush from points report", error);
	const LevelMapBrush* made = brushById(document, report.brushId);
	expect(made && made->entityId == brushById(document, 0)->entityId && document.undoStack.size() == depth + 1 && document.selection.size() == 1
			&& document.selection.first().objectId == report.brushId && std::abs(made->mins.x - 256) < 0.01 && std::abs(made->maxs.z - 64) < 0.01
			&& std::all_of(made->faces.cbegin(), made->faces.cend(), [](const LevelMapBrushFace& face) { return face.textureName == QStringLiteral("studio/new"); }),
		"new brush in worldspawn, selected, in the material", error);
	const QByteArray after = bytes(document);
	expect(undoLevelMapEdit(&document, &error) && bytes(document) == before, "exact undo", error);
	expect(redoLevelMapEdit(&document, &error) && bytes(document) == after, "exact redo", error);
	expect(undoLevelMapEdit(&document, &error), "undo again", error);

	// Refusals leave the map alone.
	const QByteArray untouched = bytes(document);
	bool cancelled = false;
	try {
		const LevelPlacementControl control {[] { return true; }, {}};
		const detail::PlacementControlScope scope(control);
		addLevelMapBrushFromPoints(&document, request, nullptr, &error);
	} catch (const detail::PlacementCancelled&) {
		cancelled = true;
	}
	expect(cancelled && bytes(document) == untouched && document.undoStack.size() == depth,
		"a cancelled point hull leaves the map and undo history unchanged");
	request.texture = QStringLiteral("two words");
	expect(!addLevelMapBrushFromPoints(&document, request, nullptr, &error) && bytes(document) == untouched, "a bad material is refused", error);
	request.texture = QStringLiteral("studio/new");
	LevelBrushPointsRequest flatRequest {flat, QStringLiteral("studio/new")};
	expect(!addLevelMapBrushFromPoints(&document, flatRequest, nullptr, &error) && bytes(document) == untouched && document.undoStack.size() == depth,
		"flat points are refused atomically", error);
	LevelMapDocument doom;
	doom.format = LevelMapFormat::DoomWad;
	expect(!addLevelMapBrushFromPoints(&doom, request, nullptr, &error), "Doom maps take no brushes", error);

	// Each face format keeps its own dialect.
	for (const QString& dialect : {QStringLiteral("valve220"), QStringLiteral("brushDef"), QStringLiteral("brushDef3")}) {
		LevelMapDocument styled;
		const QByteArray original = tests::surfaceFixture(dialect);
		if (!expect(loadLevelMapBytes({QStringLiteral("styled.map"), {}, QStringLiteral("idtech3")}, original, &styled, &error), "dialect fixture", error)) {
			continue;
		}
		LevelBrushPointsRequest styledRequest {wedge(), QStringLiteral("studio/new")};
		expect(addLevelMapBrushFromPoints(&styled, styledRequest, &report, &error) && report.faceDialect == dialect && report.faces == 5,
			"face format follows the map", dialect + QStringLiteral(": ") + error);
		LevelMapDocument back;
		expect(loadLevelMapBytes({QStringLiteral("styled.map"), {}, QStringLiteral("idtech3")}, bytes(styled), &back, &error)
				&& brushById(back, report.brushId) && brushById(back, report.brushId)->faceCount == 5,
			"reopens with the new brush", dialect + QStringLiteral(": ") + error);
		expect(undoLevelMapEdit(&styled, &error) && bytes(styled) == original, "byte-exact undo in every format", dialect);
	}

	// An open group receives it; a locked destination refuses it.
	{
		LevelMapDocument grouped = document;
		QString group;
		expect(createLevelSceneNode(&grouped, LevelSceneNodeKind::Group, QStringLiteral("Pillars"), {}, &group, &error)
				&& assignLevelSceneObjects(&grouped, group, {QStringLiteral("brush:1")}, &error) && openLevelSceneGroup(&grouped, group, &error),
			"open a group", error);
		const QByteArray groupBefore = bytes(grouped);
		expect(addLevelMapBrushFromPoints(&grouped, request, &report, &error)
				&& levelSceneMembership(grouped.scene, QStringLiteral("brush:%1").arg(report.brushId)) == group,
			"the open group receives the new brush", error);
		expect(undoLevelMapEdit(&grouped, &error) && bytes(grouped) == groupBefore, "group undo", error);

		LevelMapDocument locked = document;
		QString layer;
		expect(createLevelSceneNode(&locked, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
				&& setLevelSceneLocked(&locked, layer, true, &error),
			"locked layer", error);
		locked.activeSceneNode = layer;
		const QByteArray lockedBefore = bytes(locked);
		const auto lockedDepth = locked.undoStack.size();
		expect(!addLevelMapBrushFromPoints(&locked, request, nullptr, &error) && bytes(locked) == lockedBefore && locked.undoStack.size() == lockedDepth,
			"a locked destination refuses the brush", error);
	}

	// Inside an open linked group, the linked copy gets the brush too.
	{
		LevelMapDocument linked = document;
		QString original;
		QString copy;
		expect(createLevelSceneNode(&linked, LevelSceneNodeKind::Group, QStringLiteral("Linked"), {}, &original, &error)
				&& assignLevelSceneObjects(&linked, original, {QStringLiteral("brush:1")}, &error)
				&& createLinkedLevelGroup(&linked, original, {0, 512, 0, true}, &copy, &error) && openLevelSceneGroup(&linked, original, &error),
			"linked fixture", error);
		const QByteArray linkedBefore = bytes(linked);
		const auto linkedBrushes = linked.brushes.size();
		const auto linkedDepth = linked.undoStack.size();
		expect(addLevelMapBrushFromPoints(&linked, request, &report, &error) && linked.brushes.size() == linkedBrushes + 2
				&& linked.undoStack.size() == linkedDepth + 1 && levelSceneNode(linked.scene, copy)->objects.size() == 2,
			"the linked copy follows in one undo step", error);
		expect(undoLevelMapEdit(&linked, &error) && bytes(linked) == linkedBefore, "linked undo", error);
	}

	// Listings at full precision that read back exactly.
	{
		LevelMapDocument precise = document;
		LevelBrushPointsRequest fine {{{0.1, 0.2, 0.3, true}, {64.7, 0.2, 0.3, true}, {0.1, 33.3, 0.3, true}, {0.1, 0.2, 17.9, true}},
			QStringLiteral("studio/new")};
		expect(addLevelMapBrushFromPoints(&precise, fine, &report, &error), "fractional brush", error);
		const int fineId = report.brushId;
		QVector<LevelVertexListing> listings;
		LevelBrushTopology solved;
		expect(levelBrushTopology(*brushById(precise, fineId), &solved, &error)
				&& levelMapVertexListings(precise, {0, fineId}, {}, &listings, &error) && listings.size() == 2 && listings[0].points.size() == 8
				&& same(listings[1].points, solved.vertices),
			"whole brushes list their corners", error);
		const QString text = levelVertexListingText(listings);
		expect(text.startsWith(QStringLiteral("// brush:0\n(0 0 0) (0 0 64)")) && text.contains(QStringLiteral("// brush:%1\n").arg(fineId))
				&& levelVertexListingPointCount(listings) == 12,
			"listing text", text);
		expect(parseLevelBrushPoints(text, &points, &error) && same(points, listings[0].points + listings[1].points),
			"listing text reads back exactly", error);
		expect(levelMapCoordinateText(0.1) == QStringLiteral("0.1") && levelMapCoordinateText(-0.0) == QStringLiteral("0")
				&& levelMapCoordinateText(1.0 / 3.0).toDouble() == 1.0 / 3.0 && levelMapPointText({-8, 0.5, 1e-7, true}) == QStringLiteral("(-8 0.5 1e-07)"),
			"coordinates are shortest and exact", levelMapPointText({-8, 0.5, 1e-7, true}));
		// Faces, edges and vertices of brush 0, then a patch's control points.
		LevelBrushTopology box;
		levelBrushTopology(*brushById(precise, 0), &box);
		const QVector<LevelMapComponentSet> sets {{{LevelMapSelectionKind::QuakeBrush, 0}, LevelBrushComponent::Face, {0, 2}},
			{{LevelMapSelectionKind::QuakeBrush, 0}, LevelBrushComponent::Edge, {3}}, {{LevelMapSelectionKind::QuakeBrush, 0}, LevelBrushComponent::Vertex, {7, 0}}};
		expect(levelMapVertexListings(precise, {}, sets, &listings, &error) && listings.size() == 4 && listings[0].points.size() == 4
				&& listings[0].texture == QStringLiteral("base/box") && listings[2].points.size() == 2 && listings[3].points.size() == 2
				&& listings[3].points[0].x == 64 && listings[3].points[1].x == 0,
			"faces, edges and picked vertices", error);
		const QString components = levelVertexListingText(listings);
		expect(components.contains(QStringLiteral("// brush:0 face:0 base/box\n")) && components.contains(QStringLiteral("// brush:0 edge:3\n"))
				&& components.contains(QStringLiteral("// brush:0 vertex:7,0\n(64 64 64) (0 0 0)\n")),
			"component headers", components);
		expect(!levelMapVertexListings(precise, {99}, {}, &listings, &error) && error.contains(QStringLiteral("99")), "a missing brush is named", error);
		expect(!levelMapVertexListings(precise, {}, {{{LevelMapSelectionKind::QuakeBrush, 0}, LevelBrushComponent::Face, {6}}}, &listings, &error),
			"a missing face is refused", error);
		LevelMapDocument curved;
		LevelMapCreateRequest create;
		create.starterRoom = false;
		createLevelMap(create, &curved);
		LevelMapPatch patch;
		int patchId = -1;
		expect(createLevelPatch({}, &patch, &error) && addLevelMapPatch(&curved, patch, &patchId, &error), "patch fixture", error);
		expect(levelMapVertexListings(curved, {}, {{{LevelMapSelectionKind::QuakePatch, patchId}, LevelBrushComponent::Vertex, {0, 4}}}, &listings, &error)
				&& listings.size() == 1 && listings[0].points.size() == 2
				&& levelVertexListingText(listings).startsWith(QStringLiteral("// patch:%1 control:0,4\n").arg(patchId)),
			"patch control points", error + levelVertexListingText(listings));
		QVector<int> lots(4097, 0);
		expect(!levelMapVertexListings(precise, lots, {}, &listings, &error) && error.contains(QStringLiteral("4096")), "at most 4096 listings", error);
	}

	// The CLI: map add-brush --points and map brush-components.
	if (argc > 1) {
		const QString binary = QString::fromLocal8Bit(argv[1]);
		QTemporaryDir temporary;
		if (!expect(temporary.isValid(), "temporary folder")) {
			return 1;
		}
		const QString input = QDir(temporary.path()).filePath(QStringLiteral("input.map"));
		const QString output = QDir(temporary.path()).filePath(QStringLiteral("output.map"));
		QFile file(input);
		if (!expect(file.open(QIODevice::WriteOnly) && file.write(fixture()) == fixture().size(), "CLI fixture")) {
			return 1;
		}
		file.close();
		const QStringList base {QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("add-brush"), input, QStringLiteral("--points"),
			QStringLiteral("256,0,0 384,0,0 256,128,0 256,0,64"), QStringLiteral("--points"), QStringLiteral("(384 0 64) (256 128 64) (300 10 10)"),
			QStringLiteral("--texture"), QStringLiteral("studio/new"), QStringLiteral("--output"), output};
		runCli(binary, base + QStringList {QStringLiteral("--dry-run"), QStringLiteral("--json")}, 0);
		expect(!QFile::exists(output), "dry run writes nothing");
		const QJsonObject result = runCli(binary, base + QStringList {QStringLiteral("--json")}, 0);
		const QJsonObject pointsJson = result.value(QStringLiteral("points")).toObject();
		expect(result.value(QStringLiteral("shape")).toString() == QStringLiteral("points") && result.value(QStringLiteral("faceCount")).toInt() == 5
				&& result.value(QStringLiteral("brushId")).toInt() == 3 && pointsJson.value(QStringLiteral("inputPoints")).toInt() == 7
				&& pointsJson.value(QStringLiteral("corners")).toInt() == 6 && pointsJson.value(QStringLiteral("unusedPoints")).toInt() == 1,
			"structured add-brush --points result", QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
		LevelMapDocument direct;
		expect(loadLevelMap({input, {}, QStringLiteral("idtech2")}, &direct, &error), "reload CLI input", error);
		LevelBrushPointsRequest cliRequest;
		parseLevelBrushPoints(QStringLiteral("256,0,0 384,0,0 256,128,0 256,0,64\n(384 0 64) (256 128 64) (300 10 10)"), &cliRequest.points);
		cliRequest.texture = QStringLiteral("studio/new");
		QFile written(output);
		expect(addLevelMapBrushFromPoints(&direct, cliRequest, nullptr, &error) && written.open(QIODevice::ReadOnly) && written.readAll() == bytes(direct),
			"the CLI writes what the core service builds", error);
		written.close();
		QByteArray narration;
		runCli(binary, base + QStringList {QStringLiteral("--overwrite")}, 0, &narration);
		expect(narration.contains("Added brush:3") && narration.contains("7 given, 6 corners, 5 faces, 1 unused"), "plain add-brush narration", QString::fromUtf8(narration));
		// Usage errors: a shape with --points, too few, flat or malformed points.
		const auto variant = [&](const QString& points) {
			QStringList args {QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("add-brush"), input, QStringLiteral("--points"), points,
				QStringLiteral("--texture"), QStringLiteral("studio/new"), QStringLiteral("--output"), output, QStringLiteral("--overwrite"), QStringLiteral("--json")};
			return args;
		};
		runCli(binary, variant(QStringLiteral("0,0,0 64,0,0 0,64,0 0,0,64")) + QStringList {QStringLiteral("--shape"), QStringLiteral("box")}, 2);
		runCli(binary, variant(QStringLiteral("0,0,0 64,0,0 0,64,0")), 2);
		runCli(binary, variant(QStringLiteral("0,0,0 64,0,0 0,64,0 64,64,0")), 2);
		runCli(binary, variant(QStringLiteral("0,0,0 64,0 0,64,0 0,0,64")), 2);
		runCli(binary, {QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("add-brush"), input, QStringLiteral("--texture"), QStringLiteral("studio/new"),
						   QStringLiteral("--output"), output, QStringLiteral("--overwrite")},
			2);

		// brush-components: repeated and comma-separated --brush, face coordinates.
		// Reopened, the new worldspawn brush is brush 2 and the door's brush 3.
		const QJsonObject listed = runCli(binary, {QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("brush-components"), output, QStringLiteral("--brush"),
													  QStringLiteral("0,2"), QStringLiteral("--brush"), QStringLiteral("3"), QStringLiteral("--brush"), QStringLiteral("0"),
													  QStringLiteral("--json")},
			0);
		const QJsonArray brushes = listed.value(QStringLiteral("brushes")).toArray();
		const QJsonArray faces = brushes.at(1).toObject().value(QStringLiteral("faces")).toArray();
		expect(brushes.size() == 3 && brushes.at(0).toObject().value(QStringLiteral("brushId")).toInt() == 0
				&& brushes.at(1).toObject().value(QStringLiteral("brushId")).toInt() == 2 && brushes.at(2).toObject().value(QStringLiteral("brushId")).toInt() == 3
				&& listed.value(QStringLiteral("brush")).toObject() == brushes.at(0).toObject() && faces.size() == 5
				&& std::all_of(faces.cbegin(), faces.cend(), [](const QJsonValue& face) {
					   const QJsonObject object = face.toObject();
					   return object.value(QStringLiteral("positions")).toArray().size() == object.value(QStringLiteral("vertices")).toArray().size()
						   && object.value(QStringLiteral("positions")).toArray().size() >= 3;
				   }),
			"several brushes with face coordinates", QString::fromUtf8(QJsonDocument(listed).toJson(QJsonDocument::Compact)));
		QByteArray plain;
		runCli(binary, {QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("brush-components"), output, QStringLiteral("--brush"), QStringLiteral("0"),
						   QStringLiteral("--brush"), QStringLiteral("2")},
			0, &plain);
		expect(plain.contains("brush 0:") && plain.contains("brush 2:") && plain.contains("vertex 7: 64, 64, 64")
				&& plain.contains(" at (0 0 64) (0 64 64) (64 64 64) (64 0 64)"),
			"plain listing with coordinates", QString::fromUtf8(plain));
		runCli(binary, {QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("brush-components"), output, QStringLiteral("--brush"), QStringLiteral("0,x")}, 2);
		runCli(binary, {QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("brush-components"), output, QStringLiteral("--brush"), QStringLiteral("0"),
						   QStringLiteral("--brush"), QStringLiteral("42")},
			2);
		// Editing commands still take exactly one brush.
		runCli(binary, {QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("move-components"), output, QStringLiteral("--brush"), QStringLiteral("0"),
						   QStringLiteral("--brush"), QStringLiteral("3"), QStringLiteral("--kind"), QStringLiteral("vertex"), QStringLiteral("--component"),
						   QStringLiteral("0"), QStringLiteral("--delta"), QStringLiteral("1,0,0"), QStringLiteral("--output"), output, QStringLiteral("--overwrite")},
			2);
	}
	std::cout << (ok ? "level brush points smoke passed" : "level brush points smoke FAILED") << std::endl;
	return ok ? 0 : 1;
}
