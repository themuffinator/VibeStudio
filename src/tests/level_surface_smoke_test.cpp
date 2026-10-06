#include "core/level_surface.h"
#include "tests/level_material_test_helpers.h"
#include "tests/level_surface_test_helpers.h"
#include <QCoreApplication>
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
bool expect(bool value, const char *message, const QString &error = {})
{
	if (!value) {
		std::cerr << message << ": " << error.toStdString() << '\n';
	}
	return value;
}
bool near(const QPointF &a, const QPointF &b, double tolerance = 0.002)
{
	return std::abs(a.x() - b.x()) <= tolerance && std::abs(a.y() - b.y()) <= tolerance;
}
bool load(const QByteArray &bytes, LevelMapDocument *document, QString *error)
{
	return loadLevelMapBytes({QStringLiteral("surfaces.map"), {}, QStringLiteral("idtech3")}, bytes, document, error);
}
QPair<QPointF, QPointF> bounds(const LevelMapBrushFace &face, const MapFacePolygon &polygon)
{
	QPointF lo(1e30, 1e30), hi(-1e30, -1e30);
	for (const auto &point : polygon.points) {
		const auto uv = levelTextureProjection(face).at(point);
		lo.setX(std::min(lo.x(), uv.x()));
		lo.setY(std::min(lo.y(), uv.y()));
		hi.setX(std::max(hi.x(), uv.x()));
		hi.setY(std::max(hi.y(), uv.y()));
	}
	return {lo, hi};
}
LevelMapVec3 center(const MapFacePolygon &polygon)
{
	LevelMapVec3 result{0, 0, 0, true};
	for (const auto &p : polygon.points) {
		result.x += p.x / polygon.points.size();
		result.y += p.y / polygon.points.size();
		result.z += p.z / polygon.points.size();
	}
	return result;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	bool ok = temp.isValid();
	QString error;
	const QHash<QString, QSize> sizes{{QStringLiteral("STUDIO\\GRID"), QSize(128, 64)}};
	for (const auto &dialect :
		 {QStringLiteral("classic"), QStringLiteral("valve220"), QStringLiteral("brushDef"), QStringLiteral("brushDef3")}) {
		std::cerr << "Surface dialect " << dialect.toStdString() << '\n';
		for (const auto op : {LevelSurfaceOperation::Fit, LevelSurfaceOperation::Shift, LevelSurfaceOperation::Scale,
							  LevelSurfaceOperation::Rotate, LevelSurfaceOperation::Align}) {
			LevelMapDocument map;
			const auto original = tests::surfaceFixture(dialect);
			if (!expect(load(original, &map, &error), "load original dialect", error)) {
				return EXIT_FAILURE;
			}
			setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::Entity, 0}});
			const auto faces = levelMapSelectedSurfaces(map);
			ok &= expect(faces.size() == 6, "owned and direct brushes are deduplicated");
			const auto before = map.brushes.first();
			const auto geometry = solveBrushGeometry(before.faces);
			LevelSurfaceRequest request{op, 2, 3, 27.5, LevelSurfaceAlignment::Center, LevelSurfaceAlignment::Maximum};
			if (op == LevelSurfaceOperation::Shift) {
				request.x = 12;
				request.y = -7;
			}
			if (op == LevelSurfaceOperation::Scale) {
				request.x = 2;
				request.y = -0.5;
			}
			LevelSurfaceEditPlan plan;
			ok &= expect(prepareLevelSurfaceEdit(map, faces, request, sizes, &plan, &error) && commitLevelSurfaceEdit(&map, plan, &error),
						 "prepare and commit", error);
			ok &= expect(plan.faceCount() == 6 && plan.brushCount() == 1 && map.undoStack.size() == 1, "one command for all six faces");
			for (int i = 0; i < 6; ++i) {
				const auto a = levelTextureProjection(before.faces[i]);
				const auto b = levelTextureProjection(map.brushes.first().faces[i]);
				const auto anchor = center(geometry.faces[i]);
				const double width = a.normalizedCoordinates ? 1 : 128, height = a.normalizedCoordinates ? 1 : 64;
				if (op == LevelSurfaceOperation::Fit) {
					const auto range = bounds(map.brushes.first().faces[i], geometry.faces[i]);
					ok &= expect(near(range.first, {0, 0}) && near(range.second, {2 * width, 3 * height}),
								 "fit produces requested UV bounds");
				} else if (op == LevelSurfaceOperation::Align) {
					const auto range = bounds(map.brushes.first().faces[i], geometry.faces[i]);
					ok &= expect(near({(range.first.x() + range.second.x()) / 2, range.second.y()}, {width / 2, height}),
								 "align center U and maximum V");
				} else if (op == LevelSurfaceOperation::Rotate || op == LevelSurfaceOperation::Scale) {
					ok &= expect(near(a.at(anchor), b.at(anchor)), "rotation and scaling keep face-centre UV fixed");
				}
				for (const auto &point : geometry.faces[i].points) {
					if (op == LevelSurfaceOperation::Shift) {
						ok &= expect(near(b.at(point) - a.at(point), {12 * width / 128, -7 * height / 64}),
									 "shift uses texels including matrix conversion");
					}
					if (op == LevelSurfaceOperation::Scale) {
						const auto delta = a.at(point) - a.at(anchor);
						ok &= expect(near(b.at(point), a.at(anchor) + QPointF(delta.x() / 2, delta.y() / -0.5)),
									 "signed scale has defined UV semantics");
					}
				}
				if (op == LevelSurfaceOperation::Rotate && a.normalizedCoordinates) {
					// Independently verify preservation of texel-space lengths for a non-square image.
					for (const auto &point : geometry.faces[i].points) {
						const auto da = a.at(point) - a.at(anchor), db = b.at(point) - b.at(anchor);
						ok &= expect(std::abs(std::hypot(da.x() * 128, da.y() * 64) - std::hypot(db.x() * 128, db.y() * 64)) < 0.015,
									 "matrix rotation avoids non-square stretch");
					}
				}
			}
			const auto saved = serializeLevelMap(map);
			LevelMapDocument reopened;
			ok &= expect(saved.succeeded() && load(saved.bytes, &reopened, &error), "save/reload", error);
			ok &= expect(saved.bytes.contains("// untouched point entity\r\n") && saved.bytes.contains("2 4 8 // flags") &&
							 !saved.bytes.contains("\n{\n"),
						 "comments flags and CRLF retained");
			ok &= expect(dialect == QStringLiteral("valve220") || saved.bytes.contains("/* retained */"), "interleaved comments survive");
			if (!reopened.brushes.isEmpty()) {
				ok &= expect(reopened.brushes.first().primitiveKind == before.primitiveKind, "surface edits retain dialect");
				for (int i = 0; i < 6; ++i) {
					for (const auto &point : geometry.faces[i].points) {
						ok &= expect(near(levelTextureProjection(reopened.brushes.first().faces[i]).at(point),
										  levelTextureProjection(map.brushes.first().faces[i]).at(point), 1e-9),
									 "live and persisted UV agree");
					}
				}
			}
			ok &= expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == original, "byte exact undo", error);
			ok &= expect(redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == saved.bytes, "byte exact redo", error);
			ok &= expect(!commitLevelSurfaceEdit(&map, plan, &error), "stale plan rejected after undo/redo");
		}
	}
	for (const auto &dialect :
		 {QStringLiteral("classic"), QStringLiteral("valve220"), QStringLiteral("brushDef"), QStringLiteral("brushDef3")}) {
		LevelMapDocument slope;
		load(tests::surfaceFixture(dialect), &slope, &error);
		setLevelMapSelection(&slope, {{LevelMapSelectionKind::QuakeBrush, 0}});
		ok &= expect(rotateLevelMapSelection(&slope, {1, 23, {0, 0, 0, true}, false, false}, &error), "oblique brush fixture", error);
		const auto geometry = solveBrushGeometry(slope.brushes.first().faces);
		LevelSurfaceEditPlan slopePlan;
		ok &= expect(prepareLevelSurfaceEdit(slope, levelMapSelectedSurfaces(slope), {LevelSurfaceOperation::Fit, 1, 2}, sizes, &slopePlan,
											 &error) &&
						 commitLevelSurfaceEdit(&slope, slopePlan, &error),
					 "fit sloped brush without changing dialect", error);
		for (int f = 0; f < slope.brushes.first().faces.size(); ++f) {
			const auto &face = slope.brushes.first().faces[f];
			const auto range = bounds(face, geometry.faces[f]);
			ok &= expect(near(range.first, {0, 0}) && near(range.second, face.explicitTextureMatrix ? QPointF(1, 2) : QPointF(128, 128)),
						 "sloped face fit UV bounds");
		}
	}
	LevelMapDocument map;
	load(tests::surfaceFixture(QStringLiteral("classic")), &map, &error);
	const auto unchanged = serializeLevelMap(map).bytes;
	LevelSurfaceRequest request;
	LevelSurfaceEditPlan plan;
	ok &= expect(!prepareLevelSurfaceEdit(map, {{0, 0}}, request, {}, &plan, &error) && !plan.ready(),
				 "fit refuses guessed image dimensions", error);
	ok &= expect(!prepareLevelSurfaceEdit(map, {{0, 0}, {0, 42}}, request, sizes, &plan, &error) && !plan.ready() &&
					 serializeLevelMap(map).bytes == unchanged,
				 "invalid batch remains atomic");
	ok &= expect(!prepareLevelSurfaceEdit(map, {{0, 0}}, request, sizes, &plan, &error, [] { return true; }) && !plan.ready(),
				 "cancelled preparation cannot commit");
	request.operation = LevelSurfaceOperation::Scale;
	for (double value : {0.0, 1e-7, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
		request.x = value;
		ok &= expect(!prepareLevelSurfaceEdit(map, {{0, 0}}, request, sizes, &plan, &error), "invalid scale rejected");
	}
	request = {LevelSurfaceOperation::Shift, 5, 7};
	ok &= expect(prepareLevelSurfaceEdit(map, {{0, 0}, {0, 0}}, request, {}, &plan, &error) && plan.faceCount() == 1 &&
					 commitLevelSurfaceEdit(&map, plan, &error),
				 "duplicate face commits once and classic shift needs no image", error);
	undoLevelMapEdit(&map);
	const auto revision = map.revision;
	request.x = request.y = 0;
	ok &= expect(prepareLevelSurfaceEdit(map, {{0, 0}}, request, {}, &plan, &error) && plan.faceCount() == 0 &&
					 commitLevelSurfaceEdit(&map, plan, &error) && map.revision == revision && map.redoStack.size() == 1,
				 "no-op preserves redo and revision", error);
	load(tests::surfaceFixture(QStringLiteral("brushDef3")), &map, &error);
	ok &= expect(
		setLevelMapBrushFaceProperty(&map, 0, 0, QStringLiteral("matrix"), QStringLiteral("0,0.015625,0.25,-0.03125,0,0.75"), &error),
		"atomic matrix field", error);
	ok &= expect(setLevelMapBrushFaceProperty(&map, 0, 0, QStringLiteral("matrix02"), QStringLiteral("0.5"), &error),
				 "individual matrix offset", error);
	const auto matrixBytes = serializeLevelMap(map).bytes;
	ok &= expect(!setLevelMapBrushFaceProperty(&map, 0, 0, QStringLiteral("matrix"), QStringLiteral("1,1,0,1,1,0"), &error) &&
					 serializeLevelMap(map).bytes == matrixBytes,
				 "collapsed matrix rejected atomically");
	ok &= expect(!setLevelMapBrushFaceProperty(&map, 0, 0, QStringLiteral("matrix00"), QStringLiteral("nan"), &error),
				 "nonfinite matrix rejected");
	LevelMapDocument matrixReopened;
	ok &= expect(load(matrixBytes, &matrixReopened, &error) && matrixReopened.brushes.first().faces.first().textureMatrix[2] == 0.5,
				 "matrix field survives save/reopen", error);
	if (argc > 1) {
		const auto source = QDir(temp.path()).filePath(QStringLiteral("cli.map"));
		const auto output = QDir(temp.path()).filePath(QStringLiteral("fitted.map"));
		const auto assets = QDir(temp.path()).filePath(QStringLiteral("assets"));
		tests::putMaterialFile(source, tests::surfaceFixture(QStringLiteral("classic")));
		tests::putMaterialFile(QDir(assets).filePath(QStringLiteral("textures/studio/grid.png")), tests::materialImage(128, 64));
		const auto cli = [&](const QStringList &arguments, bool success) {
			QProcess process;
			process.start(QString::fromLocal8Bit(argv[1]),
						  QStringList{QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("align-textures"), source} +
							  arguments + QStringList{QStringLiteral("--json")});
			const bool ended = process.waitForFinished(45000);
			const auto bytes = process.readAllStandardOutput();
			ok &= expect(ended && process.exitStatus() == QProcess::NormalExit && (process.exitCode() == 0) == success &&
							 QJsonDocument::fromJson(bytes).isObject(),
						 "CLI result",
						 QStringLiteral("ended=%1 status=%2 exit=%3 JSON=%4 ")
								 .arg(ended)
								 .arg(process.exitStatus())
								 .arg(process.exitCode())
								 .arg(QJsonDocument::fromJson(bytes).isObject()) +
							 QString::fromUtf8(bytes + process.readAllStandardError()));
			return QJsonDocument::fromJson(bytes).object();
		};
		const QStringList common{QStringLiteral("--engine"), QStringLiteral("idTech3"), QStringLiteral("--face"),	QStringLiteral("0:1"),
								 QStringLiteral("--fit"),	 QStringLiteral("2,3"),		QStringLiteral("--output"), output};
		cli(common, false);
		ok &= expect(!QFile::exists(output), "failed CLI writes nothing");
		cli(common + QStringList{QStringLiteral("--package"), assets, QStringLiteral("--dry-run")}, true);
		ok &= expect(!QFile::exists(output), "dry-run writes nothing");
		cli(common + QStringList{QStringLiteral("--package"), assets}, true);
		ok &= expect(QFile::exists(output), "CLI writes requested copy");
		cli(common + QStringList{QStringLiteral("--texture-size"), QStringLiteral("128,64")}, false);
		cli(common + QStringList{QStringLiteral("--degrees"), QStringLiteral("15")}, false);
		cli(common + QStringList{QStringLiteral("--texture-size"), QStringLiteral("128,0")}, false);
		const auto primitive = QDir(temp.path()).filePath(QStringLiteral("primitive.map"));
		const auto edited = QDir(temp.path()).filePath(QStringLiteral("primitive-edited.map"));
		tests::putMaterialFile(primitive, tests::surfaceFixture(QStringLiteral("brushDef3")));
		QProcess process;
		process.start(QString::fromLocal8Bit(argv[1]),
					  {QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("edit"), primitive, QStringLiteral("--select"),
					   QStringLiteral("brush:0"), QStringLiteral("--set"), QStringLiteral("face1.matrix=0,0.015625,0.5,-0.03125,0,0.25"),
					   QStringLiteral("--output"), edited, QStringLiteral("--json")});
		const bool ended = process.waitForFinished(45000);
		LevelMapDocument written;
		ok &= expect(ended && process.exitCode() == 0 && QJsonDocument::fromJson(process.readAllStandardOutput()).isObject() &&
						 loadLevelMap({edited, {}, QStringLiteral("idtech3")}, &written, &error) &&
						 written.brushes.first().faces.first().textureMatrix[2] == 0.5,
					 "CLI atomic matrix edit persists", error);
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
