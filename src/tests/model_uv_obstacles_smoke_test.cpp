#include "core/model_recovery.h"
#include "core/model_topology.h"
#include "core/model_uv_obstacles.h"
#include "tests/model_uv_obstacles_test_helpers.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QSize>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace
{
int checks = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value)
		std::cerr << "FAIL: " << message << '\n';
	return value;
}
QByteArray bytes(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}
std::array<int, 3> corners(ModelTriangle t)
{
	return {t.a, t.b, t.c};
}
bool retained(const ModelMesh &before, const ModelMesh &after, const QSet<int> &selected)
{
	const auto &a = before.surfaces[0], &b = after.surfaces[0];
	if (a.triangles.size() != b.triangles.size() || a.frames.size() != b.frames.size())
		return false;
	for (int face = 0; face < a.triangles.size(); ++face)
	{
		const auto old = corners(a.triangles[face]), now = corners(b.triangles[face]);
		for (int k = 0; k < 3; ++k)
		{
			if (!selected.contains(face) &&
				(old[k] != now[k] || a.texCoords[old[k]].u != b.texCoords[now[k]].u || a.texCoords[old[k]].v != b.texCoords[now[k]].v))
				return false;
			for (int pose = 0; pose < a.frames.size(); ++pose)
				for (const auto &pair : {qMakePair(a.frames[pose].positions[old[k]], b.frames[pose].positions[now[k]]),
										qMakePair(a.frames[pose].normals[old[k]], b.frames[pose].normals[now[k]])})
					if (pair.first.x != pair.second.x || pair.first.y != pair.second.y || pair.first.z != pair.second.z)
						return false;
		}
	}
	auto restored = after;
	restored.surfaces[0] = before.surfaces[0];
	updateEditableModelMetadata(&restored);
	return bytes(restored) == bytes(before);
}
bool scaleMatches(const ModelSurface &a, const ModelSurface &b, const QSet<int> &faces, double scale)
{
	for (int face : faces)
	{
		const auto old = corners(a.triangles[face]), now = corners(b.triangles[face]);
		for (int i = 0; i < 3; ++i)
		{
			const int j = (i + 1) % 3;
			if (std::abs((b.texCoords[now[i]].u - b.texCoords[now[j]].u) - scale * (a.texCoords[old[i]].u - a.texCoords[old[j]].u)) >
					2e-6 ||
				std::abs((b.texCoords[now[i]].v - b.texCoords[now[j]].v) - scale * (a.texCoords[old[i]].v - a.texCoords[old[j]].v)) > 2e-6)
				return false;
		}
	}
	return true;
}
struct Point
{
	double x, y;
};
using Triangle = std::array<Point, 3>;
double cross(Point a, Point b, Point c)
{
	return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
bool contains(const Triangle &t, Point p)
{
	const double a = cross(t[0], t[1], p), b = cross(t[1], t[2], p), c = cross(t[2], t[0], p);
	return (a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0);
}
double pointSegment(Point p, Point a, Point b)
{
	const double dx = b.x - a.x, dy = b.y - a.y, length = dx * dx + dy * dy;
	const double t = length ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length, 0., 1.) : 0;
	return std::hypot(p.x - a.x - t * dx, p.y - a.y - t * dy);
}
double distance(const Triangle &a, const Triangle &b)
{
	if (contains(a, b[0]) || contains(b, a[0]))
		return 0;
	double minimum = 1e100;
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
		{
			const auto p = a[i], q = a[(i + 1) % 3], r = b[j], s = b[(j + 1) % 3];
			if (cross(p, q, r) * cross(p, q, s) < 0 && cross(r, s, p) * cross(r, s, q) < 0)
				return 0;
			minimum = std::min({minimum, pointSegment(p, r, s), pointSegment(r, p, q)});
		}
	return minimum;
}
Triangle pixelTriangle(const ModelSurface &s, int face, QSize size)
{
	Triangle t;
	const auto indices = corners(s.triangles[face]);
	for (int i = 0; i < 3; ++i)
		t[i] = {double(s.texCoords[indices[i]].u) * size.width(), double(s.texCoords[indices[i]].v) * size.height()};
	return t;
}
// Analytic continuous geometry oracle, independent of the packer's texel masks.
bool clearance(const ModelMesh &mesh, QSize size, int padding)
{
	const auto &s = mesh.surfaces[0];
	for (int selected : tests::obstacleFaces())
	{
		const auto a = pixelTriangle(s, selected, size);
		for (auto p : a)
			if (p.x < padding || p.y < padding || p.x > size.width() - padding || p.y > size.height() - padding)
				return false;
		for (int face = 4; face < s.triangles.size(); ++face)
			if (distance(a, pixelTriangle(s, face, size)) + 1e-5 < std::max(.001, double(padding)))
				return false;
		for (int face = 0; face < mesh.surfaces[1].triangles.size(); ++face)
			if (distance(a, pixelTriangle(mesh.surfaces[1], face, size)) + 1e-5 < std::max(.001, double(padding)))
				return false;
		for (int other = (selected < 2 ? 2 : 0); other < (selected < 2 ? 4 : 2); ++other)
			if (distance(a, pixelTriangle(s, other, size)) + 1e-5 < std::max(.001, double(padding)))
				return false;
	}
	return true;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root) || argc != 2)
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("uv-obstacles-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	QString error;
	bool ok = true;
	const auto original = tests::obstaclePanels();
	const auto faces = tests::obstacleFaces();
	ok &= expect(validateEditableModel(original).isEmpty(), "animated fixture is an admitted editable model");
	for (auto dimensions : {QSize{128, 128}, QSize{512, 128}, QSize{128, 512}, QSize{65, 127}, QSize{32, 128}})
		for (int padding : {0, 3})
			for (bool preserve : {false, true})
			{
				ModelUvObstacleOptions options;
				options.atlas.resolution = dimensions.width();
				options.atlas.height = dimensions.height();
				options.atlas.padding = padding;
				options.preserveScale = preserve;
				ModelSurface mapped, repeated;
				ModelUvObstacleReport report;
				const bool success = packModelUvAround(original, 0, faces, options, &mapped, &error, {}, &report);
				ok &= expect(success, "pack around fixed active and shared-material regions");
				if (!success)
				{
					std::cerr << dimensions.width() << 'x' << dimensions.height() << " pad=" << padding << " preserve=" << preserve << ' '
							  << error.toStdString() << '\n';
					continue;
				}
				auto mesh = original;
				mesh.surfaces[0] = mapped;
				updateEditableModelMetadata(&mesh);
				ok &=
					expect(retained(original, mesh, faces), "fixed UVs, all pose corners, other surfaces, tags and metadata remain exact");
				ok &= expect(clearance(mesh, dimensions, padding),
							 "independent analytic oracle confirms continuous border and inter-island gaps");
				ok &= expect(report.charts == 2 && report.fixedFaces == 6 && report.relatedSurfaces == 1 && report.workUnits > 0 &&
								 report.rasterBytes > 0 && (!preserve || report.scale == 1) &&
								 scaleMatches(original.surfaces[0], mapped, faces, report.scale),
							 "orientation and relative pixel density match the operation receipt");
				ok &= expect(
					packModelUvAround(original, 0, faces, options, &repeated, &error) &&
						[&] {
							auto m = original;
							m.surfaces[0] = repeated;
							updateEditableModelMetadata(&m);
							return bytes(m) == bytes(mesh);
						}(),
					"packing is deterministic");
			}
	// Sloped islands and obstacles exercise continuous triangle gaps rather than
	// merely the axis-aligned bounding boxes used by the simple panel fixture.
	for (int shape = 0; shape < 3; ++shape)
	{
		auto source = original;
		for (int i = 0; i < 8; ++i)
		{
			auto &uv = source.surfaces[0].texCoords[i];
			const auto old = uv;
			uv = shape == 2 ? ModelTexCoord{old.u + 20, old.v - 20} : ModelTexCoord{old.u + old.v * .3f, old.v - old.u * .25f};
		}
		if (shape == 0)
		{
			source.surfaces[0].texCoords[10].u = .15f;
			source.surfaces[1].texCoords[3].u = .65f;
		}
		if (shape == 1)
		{
			// Existing fixed-region overlap remains legal.
			tests::obstacleRect(source.surfaces[0], 12, .2f, .8f, .8f, .2f);
		}
		ModelUvObstacleOptions options;
		options.atlas.resolution = 257;
		options.atlas.height = 191;
		options.atlas.padding = 3;
		ModelSurface mappedSurface;
		ModelUvObstacleReport receipt;
		const bool success = packModelUvAround(source, 0, faces, options, &mappedSurface, &error, {}, &receipt);
		auto mappedMesh = source;
		if (success)
			mappedMesh.surfaces[0] = mappedSurface;
		updateEditableModelMetadata(&mappedMesh);
		ok &= expect(success && retained(source, mappedMesh, faces) && clearance(mappedMesh, {257, 191}, 3) &&
						 scaleMatches(source.surfaces[0], mappedSurface, faces, receipt.scale),
					 "sloped, overlapping fixed and distant authored charts preserve exact attributes and analytic gaps");
	}
	ModelEdit edit;
	edit.kind = ModelEditKind::PackUvAround;
	{
		auto aliases = original;
		aliases.surfaces[1].skinPaths[0] = QStringLiteral(" MODELS//ALTERNATE.PNG ");
		ModelUvObstacleOptions options;
		ModelSurface mappedSurface;
		ModelUvObstacleReport receipt;
		ok &=
			expect(validateEditableModel(aliases).isEmpty() &&
					   packModelUvAround(aliases, 0, faces, options, &mappedSurface, &error, {}, &receipt) && receipt.relatedSurfaces == 1,
				   "fixed material identity uses the shared package path normalization rules");
	}
	edit.selection.faces = faces;
	edit.uvAtlasResolution = 512;
	edit.uvAtlasHeight = 128;
	edit.uvAtlasPadding = 3;
	ModelDocument document;
	ok &= expect(document.setMesh(original, &error), "open document");
	document.setSelection(edit.selection);
	ok &= expect(document.edit(edit, &error), "document packs in one transaction");
	const auto mapped = bytes(document.mesh());
	ok &= expect(document.undo() && bytes(document.mesh()) == bytes(original) && document.selection() == edit.selection &&
					 !document.canUndo(),
				 "one undo restores source and selection");
	ok &= expect(document.redo() && bytes(document.mesh()) == mapped, "redo restores exact mapping");
	const auto sourcePath = QDir(temporary.path()).filePath("mapped.mesh.json");
	ModelDocument loaded;
	ok &= expect(document.save(sourcePath, false, &error) && loaded.load(sourcePath, &error) && bytes(loaded.mesh()) == mapped,
				 "save and reopen retain obstacle mapping");
	ModelRecoverySnapshot snapshot{document.mesh(), document.selection(), 0, "Atlas", {}, {}};
	const auto recoveryPath = writeModelRecovery(snapshot, temporary.path(), "21a2b67d-3b72-4cee-af79-cd0036f46f38", &error);
	ModelRecoverySnapshot recovered;
	ok &= expect(!recoveryPath.isEmpty() && inspectModelRecovery(recoveryPath, &recovered).isValid() && bytes(recovered.mesh) == mapped,
				 "recovery retains every fixed and moved UV");
	ModelMesh native;
	const auto md3 = exportEditableModel(document.mesh(), "md3", 0, &error);
	ok &= expect(!md3.isEmpty() && importEditableModel("obstacles.md3", md3, &native, &error) && native.frameCount == 2 &&
					 native.tagCount == 1 && scaleMatches(document.mesh().surfaces[0], native.surfaces[0], faces, 1),
				 "native MD3 retains packed corner UVs, poses and tags");
	// Shared corners separated by an authored seam must split in every pose.
	auto seam = tests::uvSquare();
	for (auto &uv : seam.surfaces[0].texCoords)
	{
		uv.u *= .25f;
		uv.v *= .25f;
	}
	seam.surfaces[0].uvSeams.insert(modelEdge(0, 2));
	const auto seamSource = seam;
	edit.selection.faces = {0};
	edit.uvPreserveScale = true;
	ok &= expect(applyModelEdit(&seam, edit, nullptr, &error) && seam.vertexCount == 6 && seam.surfaces[0].uvSeams.size() == 2 &&
					 retained(seamSource, seam, {0}),
				 "seam-shared corners split across all poses and both seam sides survive");
	edit.selection.faces = {0};
	auto partial = original;
	ok &= expect(!applyModelEdit(&partial, edit, nullptr, &error) && bytes(partial) == bytes(original),
				 "partial-island selection fails atomically");
	edit.uvIslands = true;
	ok &= expect(applyModelEdit(&partial, edit, nullptr, &error) && retained(original, partial, {0, 1}),
				 "shared island expansion includes the complete selected island");
	edit.uvIslands = false;
	edit.selection.faces = faces;
	for (int failure = 0; failure < 4; ++failure)
	{
		auto mesh = original;
		if (failure == 0)
			mesh.surfaces[0].texCoords[8].u = -.001f;
		if (failure == 1)
			mesh.surfaces[1].texCoords[0].v = 1.01f;
		if (failure == 2)
			mesh.surfaces[0].texCoords[1] = mesh.surfaces[0].texCoords[0];
		if (failure == 3)
			mesh.surfaces[0].texCoords[3] = {.2f, .15f};
		const auto before = bytes(mesh);
		ok &= expect(!applyModelEdit(&mesh, edit, nullptr, &error) && bytes(mesh) == before,
					 "out-of-tile fixed UVs and invalid selected charts fail without mutation");
	}
	auto full = original;
	tests::obstacleRect(full.surfaces[0], 8, 0, 0, 1, 1);
	for (bool preserve : {false, true})
	{
		edit.uvPreserveScale = preserve;
		auto m = full;
		ok &=
			expect(!applyModelEdit(&m, edit, nullptr, &error) && bytes(m) == bytes(full), "full atlas refuses both modes without mutation");
	}
	ModelUvObstacleOptions options;
	options.atlas.resolution = 512;
	options.atlas.height = 128;
	options.atlas.padding = 3;
	ModelSurface result = original.surfaces[0];
	ModelUvObstacleReport report{77, 88, 99, 2, 123, 456};
	const auto unchanged = [&] {
		auto mesh = original;
		mesh.surfaces[0] = result;
		return bytes(mesh) == bytes(original) && report.charts == 77 && report.fixedFaces == 88 && report.relatedSurfaces == 99 &&
			   report.scale == 2 && report.workUnits == 123 && report.rasterBytes == 456;
	};
	options.workLimit = 256;
	ok &= expect(!packModelUvAround(original, 0, faces, options, &result, &error, {}, &report) && unchanged(),
				 "work budget refuses atomically");
	options.workLimit = 250000000;
	options.atlas.resolution = 4096;
	options.atlas.height = 4096;
	options.atlas.memoryLimit = 65536;
	ok &= expect(!packModelUvAround(original, 0, faces, options, &result, &error, {}, &report) && unchanged(),
				 "raster allocation preflight retains result and receipt");
	options.atlas.resolution = 512;
	options.atlas.height = 128;
	options.atlas.memoryLimit = 256 * 1024 * 1024;
	bool stop = false;
	ModelWorkControl control{[&] { return stop; },
							 [&](ModelWorkPhase phase, qint64 done, qint64) {
								 if (phase == ModelWorkPhase::Editing && done >= 1024)
									 stop = true;
							 }};
	ok &= expect(!packModelUvAround(original, 0, faces, options, &result, &error, control, &report) && stop && unchanged(),
				 "mid-raster cancellation publishes no partial result");
	int checkpoints = 0;
	ModelSurface counted;
	ok &= expect(packModelUvAround(original, 0, faces, options, &counted, &error,
								   {[&] {
										++checkpoints;
										return false;
									},
									{}}),
				 "record packing checkpoints for late-cancellation coverage");
	for (int at : {1, checkpoints / 2, checkpoints})
	{
		int visits = 0;
		ok &= expect(!packModelUvAround(original, 0, faces, options, &result, &error, {[&] { return ++visits == at; }, {}}, &report) &&
						 visits == at && unchanged(),
					 "cancellation at admission, mid-work and final publication retains result and receipt");
	}
	edit.uvPreserveScale = true;
	edit.kind = ModelEditKind::PackUv;
	auto irrelevant = original;
	ok &= expect(!applyModelEdit(&irrelevant, edit, nullptr, &error) && bytes(irrelevant) == bytes(original),
				 "preserve-scale option is rejected by unrelated operations");
	if (app.arguments()[1] != "--core-only")
	{
		const auto input = QDir(temporary.path()).filePath("input.mesh.json"), output = QDir(temporary.path()).filePath("output.mesh.json");
		ModelDocument fixture;
		ok &= expect(fixture.setMesh(original, &error) && fixture.save(input, false, &error), "write CLI source");
		const auto run = [&](QStringList extra, int code) {
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(app.arguments()[1], QStringList{"--cli", "model", "edit", input, "--faces", "0,1,2,3", "--output", output,
														  "--json", "--settings-file", QDir(temporary.path()).filePath("settings.ini")} +
												  extra);
			const bool done = process.waitForStarted(10000) && process.waitForFinished(30000);
			const auto data = process.readAllStandardOutput();
			if (!done || process.exitCode() != code)
				std::cerr << data.constData() << process.readAllStandardError().constData();
			return done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code &&
				   QJsonDocument::fromJson(data).isObject();
		};
		const QStringList command{"--operation", "uv-pack-around", "--uv-atlas-size", "512x128", "--uv-padding", "3"};
		ok &= expect(run(command + QStringList{"--dry-run"}, 0) && !QFileInfo::exists(output), "CLI dry-run does not publish");
		ok &= expect(run(command, 0) && fixture.load(output, &error) && bytes(fixture.mesh()) == mapped,
					 "CLI default-fit mapping matches core exactly");
		ok &= expect(run(command, 1) && run(command + QStringList{"--overwrite", "--uv-pack-scale=fit"}, 0),
					 "CLI overwrite and inline scale option behave explicitly");
		ok &=
			expect(run(command + QStringList{"--overwrite", "--uv-pack-scale", "preserve"}, 0) && fixture.load(output, &error) &&
					   scaleMatches(original.surfaces[0], fixture.mesh().surfaces[0], faces, 1) && clearance(fixture.mesh(), {512, 128}, 3),
				   "CLI preserve scale retains density and fixed-region gaps");
		for (auto bad : {"", "grow", "FIT", "1"})
			ok &= expect(run(command + QStringList{"--uv-pack-scale", bad}, 2), "invalid scale mode is a usage error");
		ok &= expect(run(command + QStringList{"--uv-pack-scale", "fit", "--uv-pack-scale", "preserve"}, 2),
					 "duplicate scale option is rejected");
		ok &= expect(run(command + QStringList{"--uv-pack-scale=fit", "--uv-pack-scale", "preserve"}, 2),
					 "mixed inline and separate scale duplicates are rejected");
		ok &= expect(
			run({"--operation=uv-pack-around", "--uv-atlas-size=512x128", "--uv-padding=3", "--uv-pack-scale=fit", "--overwrite"}, 0) &&
				fixture.load(output, &error) && bytes(fixture.mesh()) == mapped,
			"inline atlas options match ordinary CLI syntax");
		ok &= expect(run(command + QStringList{"--uv-atlas-size=128"}, 2) && run(command + QStringList{"--uv-padding=1"}, 2),
					 "mixed inline atlas duplicates are rejected");
		ok &= expect(run({"--operation", "uv-pack", "--uv-pack-scale", "fit"}, 2), "scale option rejects unrelated operation");
		ok &= expect(fixture.load(input, &error) && bytes(fixture.mesh()) == bytes(original), "CLI preserves original source");
	}
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	std::cout << checks << " UV obstacle core/CLI checks\n";
	return ok ? 0 : 1;
}
