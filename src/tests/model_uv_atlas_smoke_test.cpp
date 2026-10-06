#include "core/model_recovery.h"
#include "core/model_uv_atlas.h"
#include "tests/model_uv_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>

using namespace vibestudio;
namespace
{
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return condition;
}
QByteArray bytes(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
bool equal(ModelVec3 a, ModelVec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool equal(ModelTexCoord a, ModelTexCoord b) { return a.u == b.u && a.v == b.v; }
bool samePoses(const ModelMesh &a, const ModelMesh &b)
{
	if (a.frames.size() != b.frames.size() || a.surfaces.size() != b.surfaces.size())
	{
		return false;
	}
	for (int s = 0; s < a.surfaces.size(); ++s)
	{
		const auto &sa = a.surfaces[s], &sb = b.surfaces[s];
		if (sa.triangles.size() != sb.triangles.size() || sa.skinPaths != sb.skinPaths)
		{
			return false;
		}
		for (int frame = 0; frame < a.frames.size(); ++frame)
		{
			for (int face = 0; face < sa.triangles.size(); ++face)
			{
				const auto ta = sa.triangles[face], tb = sb.triangles[face];
				const int ia[]{ta.a, ta.b, ta.c}, ib[]{tb.a, tb.b, tb.c};
				for (int i = 0; i < 3; ++i)
				{
					if (!equal(sa.frames[frame].positions[ia[i]], sb.frames[frame].positions[ib[i]]) ||
						!equal(sa.frames[frame].normals[ia[i]], sb.frames[frame].normals[ib[i]]))
					{
						return false;
					}
				}
			}
		}
	}
	return true;
}
ModelMesh cylinder(int count = 24, int rings = 6)
{
	auto mesh = tests::uvSquare();
	auto &s = mesh.surfaces[0];
	s.texCoords.clear();
	s.triangles.clear();
	s.frames = {{}, {}};
	for (int z = 0; z <= rings; ++z)
	{
		for (int i = 0; i < count; ++i)
		{
			const double angle = 2 * std::numbers::pi * i / count;
			const ModelVec3 n{float(std::cos(angle)), float(std::sin(angle)), 0};
			const ModelVec3 p{n.x * (12 + z), n.y * (12 + z), float(z * 8)};
			s.frames[0].positions.append(p);
			s.frames[0].normals.append(n);
			s.frames[1].positions.append({p.x + z, p.y, p.z + 8});
			s.frames[1].normals.append(n);
			s.texCoords.append({0, 0});
		}
	}
	for (int z = 0; z < rings; ++z)
	{
		for (int i = 0; i < count; ++i)
		{
			const int a = z * count + i, b = z * count + (i + 1) % count, c = a + count, d = b + count;
			s.triangles.append({a, b, d});
			s.triangles.append({a, d, c});
		}
		s.uvSeams.insert({z * count, (z + 1) * count});
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}
QSet<int> allFaces(const ModelMesh &mesh)
{
	QSet<int> result;
	for (int i = 0; i < mesh.surfaces[0].triangles.size(); ++i)
	{
		result.insert(i);
	}
	return result;
}
ModelMesh closedBox()
{
	auto mesh = tests::uvSquare();
	auto &s = mesh.surfaces[0];
	s.frames[0].positions = {{-8, -8, -8}, {8, -8, -8}, {8, 8, -8}, {-8, 8, -8}, {-8, -8, 8}, {8, -8, 8}, {8, 8, 8}, {-8, 8, 8}};
	s.frames[0].normals.clear();
	for (const auto &p : s.frames[0].positions)
	{
		s.frames[0].normals.append({p.x / float(std::sqrt(192.0)), p.y / float(std::sqrt(192.0)), p.z / float(std::sqrt(192.0))});
	}
	s.frames[1] = s.frames[0];
	for (auto &p : s.frames[1].positions)
	{
		p.z += 8;
	}
	s.texCoords.fill({}, 8);
	s.triangles = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
				   {3, 7, 6}, {3, 6, 2}, {0, 4, 7}, {0, 7, 3}, {1, 2, 6}, {1, 6, 5}};
	s.uvSeams = {{0, 1}};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
double uvArea(const ModelSurface &s, int face)
{
	const auto t = s.triangles[face];
	const auto a = s.texCoords[t.a], b = s.texCoords[t.b], c = s.texCoords[t.c];
	return std::abs((double(b.u) - a.u) * (double(c.v) - a.v) - (double(b.v) - a.v) * (double(c.u) - a.u));
}
bool bounded(const ModelMesh &mesh, int size, int padding)
{
	for (const auto &uv : mesh.surfaces[0].texCoords)
	{
		if (uv.u < float(padding) / size || uv.v < float(padding) / size || uv.u > 1 - float(padding) / size ||
			uv.v > 1 - float(padding) / size)
		{
			return false;
		}
	}
	return true;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	QString error;
	const auto original = cylinder();
	ok &= expect(validateEditableModel(original).isEmpty(), "curved animated fixture is valid");
	ModelDocument document;
	ok &= expect(document.setMesh(original, &error), "open animated fixture");
	ModelEdit edit;
	edit.kind = ModelEditKind::UnwrapUv;
	edit.selection.faces = allFaces(original);
	edit.uvAtlasResolution = 256;
	edit.uvAtlasPadding = 3;
	QElapsedTimer timer;
	timer.start();
	ok &= expect(document.edit(edit, &error), "unwrap a curved surface using marked seams");
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
		return EXIT_FAILURE;
	}
	std::cout << "Curved atlas: " << timer.elapsed() << " ms\n";
	const auto mapped = document.mesh();
	ok &= expect(samePoses(mapped, original) && mapped.tagCount == original.tagCount &&
					 mapped.tags[1].origin.z == original.tags[1].origin.z && bounded(mapped, 256, 3) &&
					 mapped.vertexCount > original.vertexCount,
				 "all poses, normals, materials and tags survive chart splits with border padding");
	ModelUvTopology topology;
	ok &= expect(buildModelUvTopology(mapped.surfaces[0], &topology, &error) && topology.islands.size() > 1 &&
					 topology.islands.size() < original.triangleCount / 2,
				 "automatic charts join faces instead of mapping every triangle separately");
	ok &= expect(document.undo() && bytes(document.mesh()) == bytes(original) && document.redo() && bytes(document.mesh()) == bytes(mapped),
				 "atlas undo and redo are atomic");
	auto repeated = original;
	ok &= expect(applyModelEdit(&repeated, edit, nullptr, &error) && bytes(repeated) == bytes(mapped), "atlas output is deterministic");
	ModelMesh parsed;
	ok &= expect(parseEditableModel(bytes(mapped), &parsed, &error) && bytes(parsed) == bytes(mapped),
				 "atlas source round-trips all poses and seams");
	ModelMesh exported;
	const auto md3 = exportEditableModel(mapped, "md3", 0, &error);
	ok &= expect(!md3.isEmpty() && importEditableModel("curved.md3", md3, &exported, &error) && exported.frameCount == 2 &&
					 exported.tagCount == mapped.tagCount,
				 "packed animated model exports and reimports MD3 with tags");

	auto square = tests::uvSquare();
	square.surfaces[0].uvSeams = {{0, 2}};
	edit.selection.faces = {0, 1};
	ok &= expect(applyModelEdit(&square, edit, nullptr, &error) && square.vertexCount == 6 && square.surfaces[0].uvSeams.size() == 2 &&
					 samePoses(square, tests::uvSquare()),
				 "coplanar seam remains split and is remapped on both sides");
	const auto box = closedBox();
	auto closed = box;
	edit.selection.faces = allFaces(box);
	ok &= expect(applyModelEdit(&closed, edit, nullptr, &error) && samePoses(closed, box) && closed.surfaces[0].uvSeams.size() == 2 &&
					 bounded(closed, 256, 3),
				 "closed surface unwrap adds cuts while retaining an isolated interior seam");
	auto partial = tests::uvSquare();
	edit.selection.faces = {0};
	ok &= expect(applyModelEdit(&partial, edit, nullptr, &error) && partial.vertexCount == 6 && samePoses(partial, tests::uvSquare()),
				 "partial unwrap preserves other face geometry");
	const auto untouched = partial.surfaces[0].triangles[1];
	ok &= expect(equal(partial.surfaces[0].texCoords[untouched.a], {0, 0}) && equal(partial.surfaces[0].texCoords[untouched.b], {1, 1}) &&
					 equal(partial.surfaces[0].texCoords[untouched.c], {0, 1}),
				 "unselected UV corners remain exact");
	edit.kind = ModelEditKind::PackUv;
	edit.selection.faces = {0, 1};
	auto stretched = tests::uvSquare();
	stretched.surfaces[0].texCoords = {{2, -3}, {4, -3}, {4, -2.37f}, {2, -2.37f}};
	ok &= expect(applyModelEdit(&stretched, edit, nullptr, &error), "pack existing UVs");
	const auto &st = stretched.surfaces[0].texCoords;
	ok &= expect(st.size() == 4 && st[1].u > st[0].u && st[3].v > st[0].v &&
					 std::abs((st[1].u - st[0].u) / (st[3].v - st[0].v) - 2 / .63) < 1e-5,
				 "packing preserves shape, orientation and aspect without lightmap rounding");
	auto twoScales = square;
	auto &scaledSurface = twoScales.surfaces[0];
	const auto scaledFace = scaledSurface.triangles[1];
	for (int v : {scaledFace.a, scaledFace.b, scaledFace.c})
	{
		scaledSurface.texCoords[v].u *= 4;
		scaledSurface.texCoords[v].v *= 4;
	}
	const double ratio = uvArea(scaledSurface, 1) / uvArea(scaledSurface, 0);
	ok &= expect(applyModelEdit(&twoScales, edit, nullptr, &error) &&
					 std::abs(uvArea(twoScales.surfaces[0], 1) / uvArea(twoScales.surfaces[0], 0) - ratio) < 1e-4,
				 "packing preserves relative texel density across differently scaled islands");
	auto overlapping = tests::uvSquare();
	overlapping.surfaces[0].texCoords[3] = {1, 0};
	const auto before = bytes(overlapping);
	ok &= expect(!applyModelEdit(&overlapping, edit, nullptr, &error) && bytes(overlapping) == before && error.contains("overlap"),
				 "pack refuses internal island overlap atomically");
	edit.kind = ModelEditKind::UnwrapUv;
	for (const auto &values : {QPair<int, int>{0, 4}, {4097, 4}, {32, 4}, {512, -1}, {512, 65}})
	{
		edit.uvAtlasResolution = values.first;
		edit.uvAtlasPadding = values.second;
		ok &= expect(!applyModelEdit(&overlapping, edit, nullptr, &error) && bytes(overlapping) == before,
					 "invalid atlas settings never mutate the source");
	}
	edit.uvAtlasResolution = 256;
	edit.uvAtlasPadding = 3;
	edit.selection.faces = allFaces(original);
	ModelWorkControl cancelled;
	int events = 0;
	bool stop = false;
	cancelled.progress = [&](ModelWorkPhase, qint64 done, qint64 total)
	{
		if (total == 400 && done >= 100)
		{
			++events;
			stop = true;
		}
	};
	cancelled.cancelled = [&] { return stop; };
	auto cancelledMesh = original;
	ok &= expect(!applyModelEdit(&cancelledMesh, edit, nullptr, &error, cancelled) && events > 0 && bytes(cancelledMesh) == bytes(original),
				 "cancel during library work leaves all source data intact");
	ModelUvAtlasOptions options;
	options.memoryLimit = 65536;
	ModelSurface result = original.surfaces[0];
	ok &= expect(!atlasModelUv(original.surfaces[0], allFaces(original), 0, true, options, &result, &error) &&
					 error.contains("memory budget"),
				 "allocation ceiling produces a recoverable failure");
	options.memoryLimit = 256 * 1024 * 1024;
	options.vertexLimit = original.vertexCount;
	ok &= expect(!atlasModelUv(original.surfaces[0], allFaces(original), 0, true, options, &result, &error) &&
					 error.contains("storage limit"),
				 "split budget checked before copying all poses");
	options.vertexLimit = 65536;
	ModelUvAtlasReport report;
	ok &= expect(atlasModelUv(original.surfaces[0], allFaces(original), 0, true, options, &result, &error, {}, &report) &&
					 report.peakBytes < options.memoryLimit && report.charts > 0,
				 "atlas succeeds after cancellation and allocation failure");
	std::cout << "Atlas peak allocation: " << report.peakBytes << " bytes; " << report.charts << " charts\n";
	auto larger = cylinder(96, 24);
	const auto largerSource = larger;
	edit.selection.faces = allFaces(larger);
	timer.restart();
	ok &= expect(applyModelEdit(&larger, edit, nullptr, &error) && samePoses(larger, largerSource),
				 "larger curved atlas preserves all animated face corners");
	std::cout << "Larger atlas: " << larger.triangleCount << " triangles, " << timer.elapsed() << " ms\n";

	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("mesh-atlas-XXXXXX"));
	ok &= expect(!root.isEmpty() && temporary.isValid(), "isolated atlas test workspace");
	if (temporary.isValid())
	{
		ModelRecoverySnapshot snapshot;
		snapshot.mesh = mapped;
		snapshot.selection = document.selection();
		snapshot.frame = 1;
		const auto path = writeModelRecovery(snapshot, temporary.path(), "11223344-5566-4788-99aa-bbccddeeff00", &error);
		ModelRecoverySnapshot restored;
		ok &= expect(!path.isEmpty() && inspectModelRecovery(path, &restored).isValid() && bytes(restored.mesh) == bytes(mapped) &&
						 restored.selection.faces == snapshot.selection.faces,
					 "recovery retains generated charts, poses, tags and face selection");
	}
	if (temporary.isValid() && argc > 1)
	{
		ModelDocument saved;
		saved.setMesh(original, &error);
		const auto sourcePath = QDir(temporary.path()).filePath("source.mesh.json"),
				   target = QDir(temporary.path()).filePath("mapped.mesh.json");
		ok &= expect(saved.save(sourcePath, false, &error), "write CLI source fixture");
		auto run = [&](QStringList args, int expected, const QString &inputOverride = QString())
		{
			QProcess process;
			process.start(QString::fromLocal8Bit(argv[1]),
						  QStringList{"--cli", "--settings-file", QDir(temporary.path()).filePath("settings.ini"), "model", "edit",
									  inputOverride.isEmpty() ? sourcePath : inputOverride} +
							  args + QStringList{"--output", target, "--json"});
			if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != expected)
			{
				std::cerr << process.readAllStandardError().toStdString() << process.readAllStandardOutput().toStdString();
				return false;
			}
			return QJsonDocument::fromJson(process.readAllStandardOutput()).isObject();
		};
		ok &= expect(run({"--operation", "uv-unwrap", "--faces", "all", "--uv-atlas-size", "256", "--uv-padding", "3", "--dry-run"}, 0) &&
						 !QFileInfo::exists(target),
					 "CLI unwrap dry run performs real validation without writing");
		ok &= expect(run({"--operation", "uv-unwrap", "--faces", "all", "--uv-atlas-size", "256", "--uv-padding", "3"}, 0) &&
						 saved.load(target, &error) && bytes(saved.mesh()) == bytes(mapped),
					 "CLI and GUI document service produce identical atlases");
		ok &= expect(run({"--operation", "uv-unwrap", "--faces", "all", "--uv-padding", "-1", "--dry-run"}, 2),
					 "CLI rejects invalid atlas options");
		ok &= expect(
			run({"--operation", "uv-pack", "--faces", "all", "--uv-atlas-size", "256", "--uv-padding", "3", "--overwrite", "--dry-run"}, 0,
				target),
			"CLI packs generated charts through normal output guards");
		ok &= expect(run({"--operation", "transform", "--faces", "all", "--uv-atlas-size", "256", "--dry-run"}, 2),
					 "CLI rejects atlas flags on unrelated edits");
	}
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
