#include "core/asset_tools.h"
#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_obj.h"
#include "core/package_archive.h"
#include "core/package_staging.h"
#include "tests/model_skin_source_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
QByteArray source(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
bool write(const QString &path, const QByteArray &bytes)
{
	QFile f(path);
	return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
QByteArray read(const QString &path)
{
	QFile f(path);
	return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{};
}
const QByteArray triangle("v 0 0 0\nv 8 0 0\nv 0 8 0\nf 1 2 3\n");
double dot(ModelVec3 a, ModelVec3 b) { return double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z; }
bool sameCorners(const ModelMesh &a, const ModelMesh &b, double tolerance = 1e-5)
{
	if (a.surfaces.size() != b.surfaces.size())
	{
		return false;
	}
	for (int i = 0; i < a.surfaces.size(); ++i)
	{
		const auto &s = a.surfaces[i], &t = b.surfaces[i];
		if (s.skinPaths != t.skinPaths || s.triangles.size() != t.triangles.size())
		{
			return false;
		}
		for (int f = 0; f < s.triangles.size(); ++f)
		{
			const int ai[]{s.triangles[f].a, s.triangles[f].b, s.triangles[f].c};
			const int bi[]{t.triangles[f].a, t.triangles[f].b, t.triangles[f].c};
			for (int c = 0; c < 3; ++c)
			{
				const auto p = s.frames[0].positions[ai[c]], q = t.frames[0].positions[bi[c]];
				const auto u = s.texCoords[ai[c]], v = t.texCoords[bi[c]];
				if (std::hypot(double(p.x) - q.x, double(p.y) - q.y, double(p.z) - q.z) > tolerance || std::abs(u.u - v.u) > tolerance ||
					std::abs(u.v - v.v) > tolerance || dot(s.frames[0].normals[ai[c]], t.frames[0].normals[bi[c]]) < 0.999)
				{
					return false;
				}
			}
		}
	}
	return true;
}
bool polygonOracle(int count, int axis, bool reverse)
{
	QVector<QPointF> polygon;
	QByteArray bytes;
	for (int i = 0; i < count; ++i)
	{
		const double angle = (reverse ? -1 : 1) * i * 2 * std::numbers::pi / count;
		const double radius = i % 2 ? 12 : 30;
		const float x = float(radius * std::cos(angle)), y = float(radius * std::sin(angle));
		polygon.append({x, y});
		const double xyz[]{axis == 0 ? 7 : x, axis == 0 ? x : (axis == 1 ? 7 : y), axis == 2 ? 7 : y};
		bytes += "v " + QByteArray::number(xyz[0], 'g', 9) + " " + QByteArray::number(xyz[1], 'g', 9) + " " +
				 QByteArray::number(xyz[2], 'g', 9) + "\n";
	}
	bytes += "f";
	for (int i = 1; i <= count; ++i)
	{
		bytes += " " + QByteArray::number(i);
	}
	bytes += '\n';
	const auto mesh = decodeModelObj("star.obj", bytes);
	if (!mesh.isValid() || mesh.triangleCount != count - 2 || !validateEditableModel(mesh).isEmpty())
	{
		std::cerr << mesh.error.toStdString() << '\n';
		return false;
	}
	const auto project = [axis](ModelVec3 p)
	{ return axis == 0 ? QPointF(p.y, p.z) : (axis == 1 ? QPointF(p.x, p.z) : QPointF(p.x, p.y)); };
	const auto cross2 = [](QPointF a, QPointF b) { return a.x() * b.y() - b.x() * a.y(); };
	double expectedArea = 0, actualArea = 0;
	for (int i = 0; i < count; ++i)
	{
		expectedArea += cross2(polygon[i], polygon[(i + 1) % count]);
	}
	const auto &surface = mesh.surfaces[0];
	for (const auto &t : surface.triangles)
	{
		const auto a = project(surface.frames[0].positions[t.a]), b = project(surface.frames[0].positions[t.b]),
				   c = project(surface.frames[0].positions[t.c]);
		const double area = cross2(b - a, c - a);
		if (area * expectedArea <= 0)
		{
			return false;
		}
		actualArea += area;
		const QPointF p = (a + b + c) / 3;
		bool inside = false;
		for (int i = 0, j = count - 1; i < count; j = i++)
		{
			const auto u = polygon[i], v = polygon[j];
			if ((u.y() > p.y()) != (v.y() > p.y()) && p.x() < (v.x() - u.x()) * (p.y() - u.y()) / (v.y() - u.y()) + u.x())
			{
				inside = !inside;
			}
		}
		if (!inside)
		{
			return false;
		}
	}
	return std::abs(actualArea - expectedArea) < std::abs(expectedArea) * 1e-6;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QElapsedTimer elapsed;
	elapsed.start();
	bool ok = true;
	QString error;
	ModelMesh mesh;
	ok &= expect(importEditableModel("triangle.OBJ", triangle, &mesh, &error), "shared OBJ import accepts a triangle");
	ok &= expect(mesh.format == ModelMeshFormat::WavefrontObj && mesh.frameCount == 1 && mesh.triangleCount == 1 && mesh.vertexCount == 3 &&
					 mesh.mins.x == 0 && mesh.maxs.x == 8 && mesh.surfaces[0].frames[0].normals[0].z == 1,
				 "OBJ format, bounds, pose and flat normal metadata");
	ok &= expect(detectModelMeshFormat("other.bin", triangle) == ModelMeshFormat::Unknown, "text content alone cannot masquerade as OBJ");
	const QByteArray polygon(
		"\xEF\xBB\xBF# generated fixture\r\no Prop\r\ng Face\r\nv 0 0 0 1\r\nv 8 0 0\r\nv 8 8 0\r\nv 4 4 0\r\nv 0 8 0\r\n"
		"vt 0 0 0\nvt 1 0\nvt 1 1\nvt .5 .5\nvt 0 1\nvn 0 0 2\nusemtl models/prop.png\nf -5/-5/-1 -4/-4/-1 \\\n -3/-3/-1 -2/-2/-1 -1/-1/-1 "
		"# concave\n");
	ok &= expect(importEditableModel("models/concave.obj", polygon, &mesh, &error),
				 "BOM, CRLF, continuation, relative indices and concave polygon intake");
	if (!error.isEmpty())
	{
		std::cerr << error.toStdString() << '\n';
	}
	ok &= expect(mesh.triangleCount == 3 && mesh.vertexCount == 5 && mesh.surfaces[0].name == "Face" &&
					 mesh.skinPaths == QStringList{"models/prop.png"} && mesh.surfaces[0].texCoords[0].v == 1 &&
					 mesh.surfaces[0].texCoords[2].v == 0,
				 "concave corners, group and direct material assignment retained; V changes origin once");
	ModelMesh restored;
	ok &= expect(parseEditableModel(source(mesh), &restored, &error) && source(restored) == source(mesh),
				 "editable source preserves imported geometry");
	const auto obj = exportEditableModel(mesh, "obj", 0, &error);
	ok &= expect(importEditableModel("roundtrip.obj", obj, &restored, &error) && sameCorners(mesh, restored),
				 "OBJ export/reimport preserves every triangle corner");
	const auto md3 = exportEditableModel(mesh, "md3", 0, &error);
	ok &= expect(!md3.isEmpty() && importEditableModel("prop.md3", md3, &restored, &error) && sameCorners(mesh, restored, 1.0 / 64),
				 "OBJ reaches native MD3 with UV and material fidelity");
	for (int count : {5, 16, 65})
	{
		for (int axis = 0; axis < 3; ++axis)
		{
			for (bool reverse : {false, true})
			{
				ok &= expect(polygonOracle(count, axis, reverse),
							 "concave triangulation satisfies independent area, winding and interior oracles");
			}
		}
	}
	const auto collinear = decodeModelObj("collinear.obj", "v 0 0 0\nv 4 0 0\nv 8 0 0\nv 8 8 0\nv 0 8 0\nf 1 2 3 4 5\n");
	ok &= expect(collinear.isValid() && collinear.vertexCount == 5 && collinear.triangleCount == 3,
				 "boundary collinear corners are retained in triangulation");
	const auto thin = decodeModelObj("thin.obj", "v 0 0 0\nv 1000000 0 0\nv 0 .000000001 0\nf 1 2 3\n");
	ok &= expect(thin.isValid() && validateEditableModel(thin).isEmpty(), "long thin faces survive scale-aware orientation checks");
	const QByteArray pair("v 0 0 0\nv 8 0 0\nv 0 8 0\nv 0 0 8\nvt 0 0\nvt 1 0\nvt 0 1\nvt .25 .25\n");
	const auto smooth = decodeModelObj("smooth.obj", pair + "s 1\nf 1/1 2/2 3/3\nf 1/4 4/2 2/1\n");
	const auto flat = decodeModelObj("flat.obj", pair + "s off\nf 1/1 2/2 3/3\nf 1/4 4/2 2/1\n");
	ok &= expect(smooth.isValid() && flat.isValid() && smooth.vertexCount == 6 && flat.vertexCount == 6 &&
					 dot(smooth.surfaces[0].frames[0].normals[0], smooth.surfaces[0].frames[0].normals[3]) > .999 &&
					 dot(flat.surfaces[0].frames[0].normals[0], flat.surfaces[0].frames[0].normals[3]) < .001,
				 "UV splits share smooth normals while s off preserves hard edges");
	const auto explicitNormals = decodeModelObj("normals.obj", pair + "vn 1 0 0\nvn -1 0 0\ns 9\nf 1//1 2//1 3//1\nf 1//2 4//2 2//2\n");
	ok &= expect(explicitNormals.isValid() && explicitNormals.vertexCount == 6 && explicitNormals.surfaces[0].frames[0].normals[0].x == 1 &&
					 explicitNormals.surfaces[0].frames[0].normals[3].x == -1,
				 "explicit normal seams override smoothing groups");
	const auto splitMaterials = decodeModelObj("materials.obj", pair + "s 1\ng part\nusemtl textures/first\nf 1 2 3\nusemtl\nf 1 4 2\n");
	ok &= expect(splitMaterials.isValid() && splitMaterials.surfaces.size() == 2 && splitMaterials.surfaces[1].name == "part_2" &&
					 splitMaterials.surfaces[1].skinPaths.isEmpty() &&
					 dot(splitMaterials.surfaces[0].frames[0].normals[0], splitMaterials.surfaces[1].frames[0].normals[0]) > .999,
				 "material boundary creates unique surfaces without a normal discontinuity");
	ok &= expect(importEditableModel("materials.obj", exportEditableModel(splitMaterials, "obj", 0, &error), &restored, &error) &&
					 sameCorners(splitMaterials, restored),
				 "exported unassigned surface does not inherit the preceding material");
	const auto grouped = decodeModelObj("groups.obj", "o one\ng shared\n" + triangle + "o two\nf 1 2 3\n");
	ok &= expect(grouped.isValid() && grouped.surfaces.size() == 2 && grouped.surfaces[0].name != grouped.surfaces[1].name,
				 "objects partition surfaces even when group names repeat");
	const auto untouched = source(mesh);
	const auto rejects = [&](const QByteArray &bytes, const char *message)
	{
		const auto result = decodeModelObj("bad.obj", bytes);
		QString diagnostic;
		const bool imported = importEditableModel("bad.obj", bytes, &mesh, &diagnostic);
		return expect(!result.isValid() && result.surfaces.isEmpty() && !imported && diagnostic.startsWith("OBJ line ") &&
						  source(mesh) == untouched,
					  message);
	};
	for (const QByteArray &bad : {QByteArray("0"), QByteArray("4"), QByteArray("-4"), QByteArray("1.5"), QByteArray("9223372036854775808"),
								  QByteArray("1/"), QByteArray("1//"), QByteArray("1/1/1/1")})
	{
		ok &= rejects("v 0 0 0\nv 8 0 0\nv 0 8 0\nf " + bad + " 2 3\n", "invalid index forms rejected atomically");
	}
	for (const QByteArray &record :
		 {QByteArray("mtllib skin.mtl"), QByteArray("csh echo forbidden"), QByteArray("call other.obj"), QByteArray("curv 0 1 1 2"),
		  QByteArray("p 1"), QByteArray("l 1 2"), QByteArray("v 1 2 3 1 0 0"), QByteArray("v 1 2 3 2"), QByteArray("vt 1 2 3"),
		  QByteArray("vn 0 0 0"), QByteArray("v nan 0 0"), QByteArray("v inf 0 0"), QByteArray("v 1000001 0 0"), QByteArray("s -1"),
		  QByteArray("s"), QByteArray("usemtl ../outside"), QByteArray("g ") + QByteArray(129, 'x'), QByteArray("g \xff")})
	{
		ok &= rejects(triangle + record + '\n', "unsupported or malformed attributes cannot be silently dropped");
	}
	ok &= rejects(triangle + "v 7 7 7\n", "loose geometry requires explicit cleanup before import");
	ok &= rejects("v 0 0 0\nv 8 8 0\nv 0 8 0\nv 8 0 0\nf 1 2 3 4\n", "bow-tie polygon rejected");
	ok &= rejects("v 0 0 0\nv 8 0 0\nv 8 8 1\nv 0 8 0\nf 1 2 3 4\n", "non-planar polygon rejected");
	ok &= rejects("v 0 0 0\nv 8 0 0\nv 0 8 0\nf 1 2 3 1\n", "repeated polygon corner rejected");
	ok &= rejects("v 0 0 0\nv 8 0 0\nv 16 0 0\nf 1 2 3\n", "collapsed face rejected");
	ok &= rejects(triangle + "g a\\", "dangling continuation rejected");
	ok &= rejects(triangle + QByteArray(1, '\0'), "binary bytes rejected");
	ok &= rejects(triangle + "#" + QByteArray(65536, 'x'), "oversized line rejected before token allocation");
	QByteArray tooManyGroups;
	for (int i = 0; i < 33; ++i)
	{
		tooManyGroups += "g part" + QByteArray::number(i) + '\n' + (i ? QByteArray("f 1 2 3\n") : triangle);
	}
	ok &= rejects(tooManyGroups, "surface expansion is bounded");
	QByteArray tooManyPositions;
	for (int i = 0; i <= modelDocumentMaxVertices; ++i)
	{
		tooManyPositions += "v 0 0 0\n";
	}
	ok &= rejects(tooManyPositions, "source attribute counts are bounded before face assembly");
	ok &= rejects(triangle + "f" + QByteArray(" 1").repeated(1025) + '\n', "polygon corner count is bounded");
	// A real complexity cap, not just a cancellation test: repeated large
	// convex polygons exceed the shared intersection/triangulation work budget.
	QByteArray expensive;
	for (int i = 0; i < 1024; ++i)
	{
		const double angle = i * 2 * std::numbers::pi / 1024;
		expensive +=
			"v " + QByteArray::number(100 * std::cos(angle), 'g', 9) + " " + QByteArray::number(100 * std::sin(angle), 'g', 9) + " 0\n";
	}
	QByteArray ring("f");
	for (int i = 1; i <= 1024; ++i)
	{
		ring += " " + QByteArray::number(i);
	}
	ring += '\n';
	expensive += ring.repeated(24);
	ok &= rejects(expensive, "adversarial polygon work is bounded across the whole file");
	{
		tests::SkinReader reader;
		reader.add("fixture.obj", polygon);
		ok &= expect(decodeModelObjFromArchive(reader, "fixture.obj").geometryAvailable, "verified package stream decodes OBJ");
		reader.lateFailure = true;
		ok &= expect(!decodeModelObjFromArchive(reader, "fixture.obj").isValid(), "late checksum failure rejects all decoded bytes");
		reader.lateFailure = false;
		reader.metadata[0].sizeBytes -= 1;
		ok &= expect(!decodeModelObjFromArchive(reader, "fixture.obj").isValid(), "stream cannot exceed its advertised size");
		reader.metadata[0].sizeBytes += 2;
		ok &= expect(!decodeModelObjFromArchive(reader, "fixture.obj").isValid(), "short stream cannot be adopted");
		reader.metadata[0].sizeBytes = quint64(modelDocumentMaxSourceBytes) + 1;
		reader.entered = false;
		ok &= expect(!decodeModelObjFromArchive(reader, "fixture.obj").isValid() && !reader.entered,
					 "oversized package payload is rejected before I/O");
		reader.metadata[0].sizeBytes = polygon.size();
		reader.add("FIXTURE.OBJ", polygon);
		ok &= expect(!decodeModelObjFromArchive(reader, "fixture.obj").isValid() && !reader.entered,
					 "repeated names never select an arbitrary package occurrence");
	}
	QByteArray large;
	for (int i = 0; i < 8000; ++i)
	{
		large += "v " + QByteArray::number(i) + " 0 0\nv " + QByteArray::number(i) + " 2 0\nv " + QByteArray::number(i) + " 0 2\n";
		large += "f -3 -2 -1\n";
	}
	for (int stop : {1, 4, 16, 128, 1024})
	{
		int polls = 0;
		ModelWorkControl control;
		control.cancelled = [&] { return ++polls >= stop; };
		ok &= expect(!importEditableModel("cancel.obj", large, &mesh, &error, nullptr, control) && source(mesh) == untouched &&
						 error.contains("cancel", Qt::CaseInsensitive),
					 "OBJ parsing cancellation preserves caller output");
	}
	const auto largeMesh = decodeModelObj("large.obj", large);
	ok &= expect(largeMesh.isValid() && largeMesh.vertexCount == 24000 && largeMesh.triangleCount == 8000,
				 "large bounded polygon source imports successfully");
	ModelDocument document;
	ok &= expect(document.setMesh(mesh, &error), "imported mesh enters editable document");
	const auto before = document.revisionFingerprint();
	ModelEdit edit;
	edit.kind = ModelEditKind::Transform;
	edit.selection.faces = {0};
	edit.translation = {0, 0, 4};
	ok &= expect(document.edit(edit, &error) && document.undo() && document.revisionFingerprint() == before,
				 "imported geometry edits and undoes normally");
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("mesh-obj-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	ok &= expect(write(path("prop.obj"), polygon), "write independent OBJ fixture");
	PackageArchive archive;
	ok &= expect(archive.load(temporary.path(), &error) && decodeModelMeshFromArchive(archive, "prop.obj").geometryAvailable,
				 "folder/package reader shares OBJ geometry decoder");
	const auto analysis = analyzeAssetBytes("prop.obj", polygon.left(20), polygon.size());
	ok &= expect(analysis.kind == AssetPreviewKind::Model && analysis.modelFormat == "OBJ" && analysis.modelCountsPartial,
				 "sampled OBJ metadata never reports fabricated geometry counts");
	if (argc > 1)
	{
		QJsonObject output;
		const auto run = [&](QStringList args, bool success = true)
		{
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(QString::fromLocal8Bit(argv[1]),
						  QStringList{"--cli", "--settings-file", path("settings.ini")} + args + QStringList{"--json"});
			if (!process.waitForFinished(30000))
			{
				process.kill();
				process.waitForFinished();
				return false;
			}
			const auto bytes = process.readAllStandardOutput();
			output = QJsonDocument::fromJson(bytes).object();
			const bool passed = process.exitStatus() == QProcess::NormalExit && (process.exitCode() == 0) == success && !output.isEmpty();
			if (!passed)
			{
				std::cerr << bytes.toStdString() << process.readAllStandardError().toStdString() << '\n';
			}
			return passed;
		};
		ok &= expect(run({"model", "import", path("prop.obj"), "--output", path("prop.mesh.json"), "--dry-run"}) &&
						 !QFile::exists(path("prop.mesh.json")),
					 "CLI OBJ import dry-run leaves disk untouched");
		ok &= expect(run({"model", "import", path("prop.obj"), "--output", path("prop.mesh.json")}) && read(path("prop.obj")) == polygon,
					 "CLI OBJ import writes editable source and protects original");
		ok &= expect(run({"model", "inspect", "--file", path("prop.obj")}) &&
						 output.value("model").toObject().value("format").toString() == "obj",
					 "CLI loose-file OBJ inspection shares the bounded decoder");
		ok &= expect(run({"model", "inspect", temporary.path(), "prop.obj"}), "CLI package OBJ inspection uses verified streaming");
		ok &= expect(run({"model", "build", path("prop.mesh.json"), "--output", path("prop.md3")}) &&
						 decodeModelMesh("prop.md3", read(path("prop.md3"))).geometryAvailable,
					 "CLI imported source builds native MD3");
		ok &= expect(run({"model", "import", path("prop.obj"), "--output", path("prop.mesh.json")}, false),
					 "CLI import refuses unapproved overwrite");
		ok &= expect(write(path("bad.obj"), "mtllib prop.mtl\n" + polygon) &&
						 run({"model", "import", path("bad.obj"), "--output", path("bad.mesh.json")}, false) &&
						 !QFile::exists(path("bad.mesh.json")),
					 "CLI unsupported MTL fails before output creation");
	}
	std::cout << "OBJ geometry, seams, polygon oracles, rejection, cancellation, source/native/package/CLI tests: " << elapsed.elapsed()
			  << " ms\n";
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
