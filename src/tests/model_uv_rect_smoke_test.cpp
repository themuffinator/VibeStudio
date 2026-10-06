#include "core/model_recovery.h"
#include "core/model_uv_atlas.h"
#include "tests/model_uv_rect_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QSize>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace
{
int checks = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value) std::cerr << "FAIL: " << message << '\n';
	return value;
}
QByteArray bytes(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
bool retained(const ModelMesh &before, const ModelMesh &after)
{
	if (before.triangleCount != after.triangleCount || before.frameCount != after.frameCount) return false;
	const auto &a = before.surfaces[0], &b = after.surfaces[0];
	if (a.triangles.size() != b.triangles.size() || a.frames.size() != b.frames.size() || a.index != b.index ||
		a.name != b.name || a.skinPaths != b.skinPaths || a.warnings != b.warnings) return false;
	for (int face = 0; face < a.triangles.size(); ++face)
	{
		const auto t = a.triangles[face], u = b.triangles[face];
		const int old[]{t.a, t.b, t.c}, now[]{u.a, u.b, u.c};
		for (int pose = 0; pose < a.frames.size(); ++pose)
			for (int k = 0; k < 3; ++k)
				for (const auto &pair : {qMakePair(a.frames[pose].positions[old[k]], b.frames[pose].positions[now[k]]),
					qMakePair(a.frames[pose].normals[old[k]], b.frames[pose].normals[now[k]])})
					if (pair.first.x != pair.second.x || pair.first.y != pair.second.y || pair.first.z != pair.second.z) return false;
	}
	auto restored = after;
	restored.surfaces = before.surfaces;
	updateEditableModelMetadata(&restored);
	return bytes(restored) == bytes(before);
}
bool proportions(const ModelMesh &source, const ModelMesh &mapped, int width, int height, bool unwrap)
{
	double uniformScale = -1;
	const auto &a = source.surfaces[0], &b = mapped.surfaces[0];
	for (int face = 0; face < a.triangles.size(); ++face)
	{
		const auto t = a.triangles[face], u = b.triangles[face];
		const int old[]{t.a, t.b, t.c}, now[]{u.a, u.b, u.c};
		for (int i = 0; i < 3; ++i)
		{
			const int j = (i + 1) % 3;
			const auto p = a.frames[0].positions[old[i]], q = a.frames[0].positions[old[j]];
			const auto x = a.texCoords[old[i]], y = a.texCoords[old[j]], s = b.texCoords[now[i]], v = b.texCoords[now[j]];
			const double original = unwrap ? std::hypot(double(p.x) - q.x, double(p.y) - q.y, double(p.z) - q.z)
				: std::hypot((double(x.u) - y.u) * width, (double(x.v) - y.v) * height);
			const double changed = std::hypot((double(s.u) - v.u) * width, (double(s.v) - v.v) * height);
			const double scale = changed / original;
			if (!std::isfinite(scale) || scale <= 0) return false;
			if (uniformScale < 0) uniformScale = scale;
			if (std::abs(scale / uniformScale - 1) > .002) return false;
			if (!unwrap && (std::abs((s.u - v.u) - (x.u - y.u) * scale) > .0001 ||
				std::abs((s.v - v.v) - (x.v - y.v) * scale) > .0001)) return false;
		}
	}
	return true;
}
double area(const ModelSurface &s, int width, int height)
{
	double result = 0;
	for (auto t : s.triangles)
	{
		const auto a = s.texCoords[t.a], b = s.texCoords[t.b], c = s.texCoords[t.c];
		result += std::abs((double(b.u) - a.u) * (double(c.v) - a.v) - (double(b.v) - a.v) * (double(c.u) - a.u)) * .5;
	}
	return result * width * height;
}
bool borders(const ModelSurface &s, int width, int height, int padding)
{
	for (auto uv : s.texCoords)
		if (uv.u * width < padding || uv.v * height < padding || uv.u * width > width - padding || uv.v * height > height - padding)
			return false;
	return true;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root) || argc != 2) return 1;
	QTemporaryDir temporary(QDir(root).filePath("rectangular-atlas-XXXXXX"));
	if (!temporary.isValid()) return 1;
	QString error;
	bool ok = true;
	const auto original = tests::atlasPanels();
	for (const auto dimensions : {QSize{512, 128}, QSize{128, 512}, QSize{320, 200}, QSize{4096, 32}, QSize{32, 4096}})
		for (bool unwrap : {false, true})
		{
			auto mesh = original;
			ModelEdit edit;
			edit.kind = unwrap ? ModelEditKind::UnwrapUv : ModelEditKind::PackUv;
			edit.selection.faces = tests::atlasFaces(mesh);
			edit.uvAtlasResolution = dimensions.width();
			edit.uvAtlasHeight = dimensions.height();
			edit.uvAtlasPadding = 2;
			const bool success = applyModelEdit(&mesh, edit, nullptr, &error);
			ok &= expect(success && retained(original, mesh), "rectangular atlas retains every pose, tag, timing and material");
			if (!success) { std::cerr << dimensions.width() << 'x' << dimensions.height() << ' ' << error.toStdString() << '\n'; continue; }
			ok &= expect(borders(mesh.surfaces[0], dimensions.width(), dimensions.height(), 2), "padding uses both pixel dimensions");
			ok &= expect(proportions(original, mesh, dimensions.width(), dimensions.height(), unwrap), "chart shape and relative density survive in pixel space");
			auto repeated = original;
			ok &= expect(applyModelEdit(&repeated, edit, nullptr, &error) && bytes(repeated) == bytes(mesh), "rectangular results are deterministic");
		}
	ModelEdit edit;
	edit.kind = ModelEditKind::UnwrapUv;
	edit.selection.faces = tests::atlasFaces(original);
	edit.uvAtlasResolution = 256;
	edit.uvAtlasPadding = 2;
	auto square = original, explicitSquare = original;
	ok &= expect(applyModelEdit(&square, edit, nullptr, &error), "legacy square atlas still works");
	edit.uvAtlasHeight = 256;
	ok &= expect(applyModelEdit(&explicitSquare, edit, nullptr, &error) && bytes(square) == bytes(explicitSquare), "explicit square matches the legacy API exactly");
	auto dense = tests::atlasPanels(12);
	edit.selection.faces = tests::atlasFaces(dense);
	edit.uvAtlasResolution = 512;
	edit.uvAtlasHeight = 128;
	ok &= expect(applyModelEdit(&dense, edit, nullptr, &error) && area(dense.surfaces[0], 512, 128) > 512 * 128 * .35,
		"rectangular packing uses meaningful long-axis area instead of a squeezed square");
	edit.selection.faces = tests::atlasFaces(original);
	ModelDocument document;
	ok &= expect(document.setMesh(original, &error), "open document");
	document.setSelection(edit.selection);
	ok &= expect(document.edit(edit, &error), "document adopts rectangular mapping");
	const auto mapped = bytes(document.mesh());
	ok &= expect(document.undo() && bytes(document.mesh()) == bytes(original) && document.selection() == edit.selection && !document.canUndo(), "one undo restores source and selection");
	ok &= expect(document.redo() && bytes(document.mesh()) == mapped, "redo restores exact mapping");
	const auto sourcePath = QDir(temporary.path()).filePath("mapped.mesh.json");
	ModelDocument loaded;
	ok &= expect(document.save(sourcePath, false, &error) && loaded.load(sourcePath, &error) && bytes(loaded.mesh()) == mapped, "source save and reopen retain rectangular mapping");
	ModelRecoverySnapshot snapshot{document.mesh(), document.selection(), 0, "Atlas", {}, {}};
	const auto recoveryPath = writeModelRecovery(snapshot, temporary.path(), "5b8d5b81-e1c6-4669-b13b-611a5878f5e4", &error);
	ModelRecoverySnapshot recovered;
	ok &= expect(!recoveryPath.isEmpty() && inspectModelRecovery(recoveryPath, &recovered).isValid() && bytes(recovered.mesh) == mapped,
		"recovery preserves rectangular UVs and animation");
	ModelMesh native;
	const auto md3 = exportEditableModel(document.mesh(), "md3", 0, &error);
	ok &= expect(!md3.isEmpty() && importEditableModel("atlas.md3", md3, &native, &error) && native.frameCount == 2 && native.tagCount == 1 &&
		native.surfaces[0].texCoords.size() == document.mesh().surfaces[0].texCoords.size(), "native MD3 carries rectangular UVs, poses and tags");
	for (int height : {-1, 1, 31, 4097})
	{
		auto mesh = original;
		edit.uvAtlasHeight = height;
		ok &= expect(!applyModelEdit(&mesh, edit, nullptr, &error) && bytes(mesh) == bytes(original), "invalid height refuses without mutation");
	}
	edit.uvAtlasHeight = 32;
	edit.uvAtlasPadding = 4;
	auto rejected = original;
	ok &= expect(!applyModelEdit(&rejected, edit, nullptr, &error) && bytes(rejected) == bytes(original), "short-axis padding is validated atomically");
	ModelUvAtlasOptions options;
	options.resolution = 512; options.height = 128;
	ModelSurface result = original.surfaces[0];
	ModelUvAtlasReport report{77, 88};
	const auto unchanged = [&] {
		auto candidate = original;
		candidate.surfaces[0] = result;
		return bytes(candidate) == bytes(original) && report.charts == 77 && report.peakBytes == 88;
	};
	options.memoryLimit = 65536;
	ok &= expect(!atlasModelUv(original.surfaces[0], tests::atlasFaces(original), 0, true, options, &result, &error, {}, &report) &&
		unchanged(), "allocation failure retains the result and receipt");
	options.memoryLimit = 256 * 1024 * 1024;
	bool stop = false;
	ModelWorkControl control{[&] { return stop; }, [&](ModelWorkPhase, qint64 done, qint64 total) { if (total == 400 && done >= 201) stop = true; }};
	ok &= expect(!atlasModelUv(original.surfaces[0], tests::atlasFaces(original), 0, true, options, &result, &error, control, &report) && stop &&
		unchanged(), "cancellation during library packing does not publish a result");
	if (app.arguments()[1] != "--core-only")
	{
		const auto input = QDir(temporary.path()).filePath("input.mesh.json"), output = QDir(temporary.path()).filePath("output.mesh.json");
		ModelDocument fixture;
		ok &= expect(fixture.setMesh(original, &error) && fixture.save(input, false, &error), "write CLI source");
		const auto run = [&](QStringList options, int code) {
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(app.arguments()[1], QStringList{"--cli", "model", "edit", input, "--faces", "all", "--output", output,
				"--json", "--settings-file", QDir(temporary.path()).filePath("settings.ini")} + options);
			const bool done = process.waitForStarted(10000) && process.waitForFinished(30000);
			const auto data = process.readAllStandardOutput();
			if (!done || process.exitCode() != code) std::cerr << data.constData() << process.readAllStandardError().constData();
			return done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code && QJsonDocument::fromJson(data).isObject();
		};
		const QStringList command{"--operation", "uv-unwrap", "--uv-atlas-size", "512x128", "--uv-padding", "2"};
		ok &= expect(run(command + QStringList{"--dry-run"}, 0) && !QFileInfo::exists(output), "CLI rectangular dry run does not publish");
		ok &= expect(run(command, 0) && fixture.load(output, &error) && bytes(fixture.mesh()) == mapped, "CLI rectangular output matches core exactly");
		ok &= expect(run(command, 1) && run(command + QStringList{"--overwrite"}, 0), "CLI keeps explicit overwrite protection");
		for (auto bad : {"0x128", "512x0", "512x31", "512x4097", "512x128x64", "512.0x128", "512,128", "512x", "x128", "512x128\n", "512x999999999999"})
			ok &= expect(run({"--operation", "uv-pack", "--uv-atlas-size", bad}, 2), "CLI malformed dimensions fail as usage errors");
		ok &= expect(run(command + QStringList{"--uv-atlas-size", "512"}, 2), "repeated atlas size is rejected");
		ok &= expect(run(command + QStringList{"--uv-padding", "3"}, 2), "repeated padding is rejected");
		ok &= expect(run({"--operation", "uv-pack", "--uv-atlas-size", "512X128", "--overwrite", "--dry-run"}, 0), "uppercase dimension separator is accepted");
		ok &= expect(run({"--operation", "uv-pack", "--uv-atlas-size", "512x32", "--uv-padding", "4"}, 2), "CLI checks padding on the short axis");
		ok &= expect(run({"--operation", "transform", "--uv-atlas-size", "512x128"}, 2), "rectangular flags remain exclusive to atlas operations");
		ok &= expect(fixture.load(input, &error) && bytes(fixture.mesh()) == bytes(original), "CLI source remains intact");
	}
	if (!ok) std::cerr << error.toStdString() << '\n';
	std::cout << checks << " rectangular atlas core/CLI checks\n";
	return ok ? 0 : 1;
}
