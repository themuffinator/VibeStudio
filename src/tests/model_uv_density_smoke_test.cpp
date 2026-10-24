#include "cli/model_tools.h"
#include "core/model_recovery.h"
#include "tests/model_mdl_test_helpers.h"
#include "tests/model_uv_density_test_helpers.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <iostream>
#include <limits>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *message, const QString &error = {})
{
	++checks;
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << message << ": " << error.toStdString() << '\n';
	}
	return value;
}
bool near(double a, double b, double tolerance = .0001) { return std::abs(a - b) <= tolerance * std::max(1., std::abs(b)); }
bool write(const QString &path, const QByteArray &data)
{
	QFile f(path);
	return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}
ModelEdit operation()
{
	ModelEdit e;
	e.kind = ModelEditKind::SetUvDensity;
	e.selection.faces = {0, 2};
	e.uvDensity.textureSize = {100, 100};
	e.uvDensity.target = 5;
	return e;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temp(QDir(root).filePath("uv-density-XXXXXX"));
	if (!temp.isValid())
		return 1;
	const auto source = densityFixture();
	const auto bytes = uvIslandBytes(source);
	QString error;
	ModelUvDensityReport report;
	expect(inspectModelUvDensity(source.surfaces[0], {}, {100, 100}, 0, &report, &error), "measure independent islands", error);
	expect(report.islands.size() == 2 && near(report.modelArea, 200) && near(report.pixelArea, 50000), "sum actual triangle areas");
	expect(near(report.density, std::sqrt(250.)) && near(report.islands[0].density, 10) && near(report.islands[1].density, 20),
		   "square-root aggregate density, not arithmetic mean");
	expect(inspectModelUvDensity(source.surfaces[0], {0}, {200, 100}, 0, &report, &error) && report.faces == QSet<int>{0, 1} &&
			   near(report.density, std::sqrt(200.)),
		   "rectangular image and whole-island expansion", error);
	expect(inspectModelUvDensity(source.surfaces[0], {0}, {100, 100}, 1, &report, &error) && near(report.density, 5),
		   "reference-pose area changes density");
	auto mirrored = source.surfaces[0];
	for (auto &uv : mirrored.texCoords)
		uv.u = -uv.u;
	expect(inspectModelUvDensity(mirrored, {}, {100, 100}, 0, &report, &error) && report.islands[0].mirroredFaces == 2 &&
			   near(report.islands[1].density, 20),
		   "mirrored UVs use unsigned area without cancellation");
	auto collapsed = source;
	collapsed.surfaces[0].texCoords[2] = collapsed.surfaces[0].texCoords[0];
	expect(inspectModelUvDensity(collapsed.surfaces[0], {0}, {100, 100}, 0, &report, &error) && !report.islands[0].scalable() &&
			   report.islands[0].collapsedUvs == 2,
		   "report collapsed UVs for repair");
	const auto collapsedBytes = uvIslandBytes(collapsed);
	expect(!applyModelEdit(&collapsed, operation(), nullptr, &error) && uvIslandBytes(collapsed) == collapsedBytes,
		   "collapsed UV adjustment refuses atomically", error);
	// Inspection preserves caller output on malformed input and cancellation.
	const auto reportBytes = QJsonDocument(modelUvDensityJson(report)).toJson();
	for (int kind = 0; kind < 5; ++kind)
	{
		auto surface = source.surfaces[0];
		QSize size(100, 100);
		int frame = 0;
		QSet<int> faces;
		if (kind == 0)
			size = {0, 100};
		if (kind == 1)
			frame = 2;
		if (kind == 2)
			faces = {99};
		if (kind == 3)
			surface.triangles[0].a = 99;
		if (kind == 4)
			surface.frames[0].positions[0].x = std::numeric_limits<float>::quiet_NaN();
		expect(!inspectModelUvDensity(surface, faces, size, frame, &report, &error) &&
				   QJsonDocument(modelUvDensityJson(report)).toJson() == reportBytes,
			   "invalid inspection preserves report", error);
	}
	ModelWorkControl stop;
	stop.cancelled = [] { return true; };
	expect(!inspectModelUvDensity(source.surfaces[0], {}, {100, 100}, 0, &report, &error, stop) &&
			   QJsonDocument(modelUvDensityJson(report)).toJson() == reportBytes,
		   "cancelled inspection preserves report");
	auto noop = uvIslandsFixture();
	const auto noopBytes = uvIslandBytes(noop);
	auto noopEdit = operation();
	noopEdit.uvDensity.target = 10;
	noopEdit.selection.faces = {0};
	expect(applyModelEdit(&noop, noopEdit, nullptr, &error) && uvIslandBytes(noop) == noopBytes,
		   "matching density never splits or rewrites corners", error);
	for (bool edge : {false, true})
	{
		auto mesh = source;
		auto e = operation();
		e.selection.faces.clear();
		if (edge)
			e.selection.edges = {modelEdge(0, 1)};
		else
			e.selection.vertices = {0};
		ModelSelection selected;
		expect(applyModelEdit(&mesh, e, &selected, &error) && selected.faces == QSet<int>{0, 1},
			   "vertex and edge scope expand to complete islands", error);
	}
	auto precise = source;
	for (auto &uv : precise.surfaces[0].texCoords)
	{
		uv.u += 10000;
		uv.v += 10000;
	}
	const auto precisionBytes = uvIslandBytes(precise);
	auto tiny = operation();
	tiny.uvDensity.target = .000001;
	expect(!applyModelEdit(&precise, tiny, nullptr, &error) && uvIslandBytes(precise) == precisionBytes,
		   "unrepresentable stored density refuses atomically", error);
	for (bool individual : {true, false})
	{
		auto mesh = source;
		auto e = operation();
		e.uvDensity.individual = individual;
		ModelSelection selected;
		expect(applyModelEdit(&mesh, e, &selected, &error), "apply density", error);
		expect(inspectModelUvDensity(mesh.surfaces[0], {}, {100, 100}, 0, &report, &error), "inspect stored UV result", error);
		expect(near(report.density, 5) && (individual ? near(report.islands[0].density, 5) && near(report.islands[1].density, 5)
													  : near(report.islands[1].density / report.islands[0].density, 2)),
			   "individual and group modes honour their density contract");
		expect(uvSamePoses(source.surfaces[0], mesh.surfaces[0]) && selected.faces == QSet<int>{0, 1, 2, 3},
			   "all poses and corner normals survive; result selects complete islands");
		ModelUvTopology before, after;
		buildModelUvTopology(source.surfaces[0], &before);
		buildModelUvTopology(mesh.surfaces[0], &after);
		for (int i = 0; i < 2; ++i)
			expect(near(before.islands[i].mins.u + before.islands[i].maxs.u, after.islands[i].mins.u + after.islands[i].maxs.u) &&
					   near(before.islands[i].mins.v + before.islands[i].maxs.v, after.islands[i].mins.v + after.islands[i].maxs.v),
				   "island centre stays fixed");
		ModelMesh parsed;
		expect(parseEditableModel(uvIslandBytes(mesh), &parsed, &error) && uvIslandBytes(parsed) == uvIslandBytes(mesh),
			   "editable round trip", error);
	}
	// Selection leaves neighbouring islands and other surfaces untouched, splitting
	// only shared corners and retaining explicit seams and all pose attributes.
	auto shared = uvIslandsFixture();
	const auto beforeShared = shared;
	auto selectedEdit = operation();
	selectedEdit.selection.faces = {0};
	expect(applyModelEdit(&shared, selectedEdit, nullptr, &error), "selected shared-corner islands", error);
	expect(shared.surfaces[0].vertexCount == beforeShared.surfaces[0].vertexCount + 3 &&
			   uvSamePoses(beforeShared.surfaces[0], shared.surfaces[0]),
		   "shared corners split without moving geometry", error);
	for (int f : {2, 3, 4})
	{
		const auto a = beforeShared.surfaces[0].triangles[f], b = shared.surfaces[0].triangles[f];
		expect(a.a == b.a && a.b == b.b && a.c == b.c &&
				   uvNear(shared.surfaces[0].texCoords[b.a], beforeShared.surfaces[0].texCoords[a.a]) &&
				   uvNear(shared.surfaces[0].texCoords[b.b], beforeShared.surfaces[0].texCoords[a.b]) &&
				   uvNear(shared.surfaces[0].texCoords[b.c], beforeShared.surfaces[0].texCoords[a.c]),
			   "unselected UV corners remain exact");
	}
	expect(uvSamePoses(beforeShared.surfaces[1], shared.surfaces[1]) &&
			   shared.surfaces[0].uvSeams.size() > beforeShared.surfaces[0].uvSeams.size(),
		   "other surface and subdivided seam marks retained");
	// Coincident bind vertices on different bones must never use nearest-copy recovery.
	auto skinned = densitySkinnedFixture();
	const auto beforeSkin = skinned;
	expect(!skinned.skeleton.isEmpty() && validateEditableModel(skinned).isEmpty(), "valid coincident skinned fixture");
	auto skinEdit = operation();
	skinEdit.selection.faces = {5};
	expect(applyModelEdit(&skinned, skinEdit, nullptr, &error) && uvSamePoses(beforeSkin.surfaces[0], skinned.surfaces[0]),
		   "skin UV edit retains posed geometry", error);
	for (int v = beforeSkin.surfaces[0].vertexCount; v < skinned.surfaces[0].vertexCount; ++v)
	{
		const auto &skin = skinned.surfaces[0].skinning;
		expect(skin.count[v] == 1 && skin.influences[skin.first[v]].joint == 1, "new seam corner retains exact original influence");
	}
	auto rebaked = skinned;
	expect(rebakeModelSkeleton(&rebaked, &error) && uvSamePoses(skinned.surfaces[0], rebaked.surfaces[0]),
		   "rebaking preserves split-corner animation", error);
	auto transformed = beforeSkin;
	auto transformEdit = skinEdit;
	transformEdit.kind = ModelEditKind::TransformUv;
	transformEdit.uvDensity = {};
	transformEdit.uvIslands = true;
	transformEdit.selection.faces = {5, 6};
	transformEdit.uvScale = {.5f, .5f};
	expect(applyModelEdit(&transformed, transformEdit, nullptr, &error), "shared island transform retains skin bindings", error);
	for (int v = beforeSkin.surfaces[0].vertexCount; v < transformed.surfaces[0].vertexCount; ++v)
	{
		const auto &skin = transformed.surfaces[0].skinning;
		expect(skin.count[v] == 1 && skin.influences[skin.first[v]].joint == 1,
			   "existing UV transform copies exact coincident-corner influences");
	}
	for (const auto &format : QStringList{"md5mesh", "iqm"})
	{
		const auto data = exportEditableModel(skinned, format, 0, &error);
		ModelMesh parsed;
		expect(!data.isEmpty() && importEditableModel("density." + format, data, &parsed, &error) && !parsed.skeleton.isEmpty(),
			   "skeletal native round trip", error);
	}
	for (const auto &format : QStringList{"mdl", "md2", "md3"})
	{
		auto native = source;
		native.tags.clear();
		native.tagCount = 0;
		native.animations.clear();
		native.surfaces[0].skinPaths = {"models/density.pcx"};
		native.md2SkinSize = {100, 100};
		if (format == "mdl")
		{
			const auto fixture = decodeModelMesh("density.mdl", groupedMdlFixture().bytes);
			native.mdl = fixture.mdl;
			native.mdl.frameGroups = {{0, {.1f, .2f}}};
			native.embeddedSkins = fixture.embeddedSkins;
			updateEditableModelMetadata(&native);
		}
		expect(applyModelEdit(&native, operation(), nullptr, &error), "prepare native UV density", error);
		if (format == "mdl" || format == "md2")
		{
			expect(exportEditableModel(native, format, 0, &error).isEmpty(), "native tile limits still refuse islands beyond the base tile",
				   error);
			// Position the second island inside the base tile without changing density.
			for (int v = 4; v < 8; ++v)
			{
				native.surfaces[0].texCoords[v].u -= 3.5f;
				native.surfaces[0].texCoords[v].v -= .5f;
			}
		}
		const auto data = exportEditableModel(native, format, 0, &error);
		ModelMesh parsed;
		expect(!data.isEmpty() && importEditableModel("density." + format, data, &parsed, &error) &&
				   parsed.frames.size() == native.frames.size(),
			   "vertex-animated native round trip", error);
	}
	QVector<ModelEdit> invalid;
	auto bad = operation();
	bad.uvDensity.target = 0;
	invalid << bad;
	bad = operation();
	bad.uvDensity.target = std::numeric_limits<double>::quiet_NaN();
	invalid << bad;
	bad = operation();
	bad.uvDensity.textureSize = {0, 100};
	invalid << bad;
	bad = operation();
	bad.uvDensity.textureSize = {100, 65537};
	invalid << bad;
	bad = operation();
	bad.uvDensity.referenceFrame = 2;
	invalid << bad;
	bad = operation();
	bad.selection.faces = {};
	invalid << bad;
	bad = operation();
	bad.selection.faces = {99};
	invalid << bad;
	bad = operation();
	bad.frame = 0;
	invalid << bad;
	bad = operation();
	bad.kind = ModelEditKind::Subdivide;
	invalid << bad;
	for (const auto &e : invalid)
	{
		auto mesh = source;
		ModelSelection selected{0, {1}, {}};
		const auto unchanged = selected;
		expect(!applyModelEdit(&mesh, e, &selected, &error) && !error.isEmpty() && uvIslandBytes(mesh) == bytes && selected == unchanged,
			   "invalid density preserves outputs", error);
	}
	for (int stage : {0, 3, 10, 20})
	{
		auto mesh = source;
		int ticks = 0;
		ModelWorkControl control;
		control.progress = [&](ModelWorkPhase, qint64, qint64) { ++ticks; };
		control.cancelled = [&] { return ticks >= stage; };
		expect(!applyModelEdit(&mesh, operation(), nullptr, &error, control) && uvIslandBytes(mesh) == bytes, "cancelled density is atomic",
			   error);
	}
	ModelDocument document;
	expect(document.setMesh(source, &error) && document.edit(operation(), &error), "document density transaction", error);
	const auto changed = uvIslandBytes(document.mesh());
	const auto recovery = writeModelRecovery({document.mesh(), document.selection()}, temp.path(),
											 QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ModelRecoverySnapshot restored;
	expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() && uvIslandBytes(restored.mesh) == changed &&
			   restored.selection == document.selection(),
		   "recovery retains adjusted UVs and island selection", error);
	expect(document.undo() && uvIslandBytes(document.mesh()) == bytes && document.redo() && uvIslandBytes(document.mesh()) == changed,
		   "undo and redo density");
	bool serializing = false;
	ModelWorkControl late;
	late.progress = [&](ModelWorkPhase phase, qint64, qint64) { serializing |= phase == ModelWorkPhase::Serializing; };
	late.cancelled = [&] { return serializing; };
	ModelDocument cancelled;
	cancelled.setMesh(source);
	expect(!cancelled.edit(operation(), &error, late) && serializing && !cancelled.canUndo() && uvIslandBytes(cancelled.mesh()) == bytes,
		   "late document cancellation retains history", error);
	const auto input = temp.filePath("source.mesh.json"), output = temp.filePath("result.mesh.json");
	expect(write(input, bytes), "write CLI source");
	const QStringList base{"vibestudio", "--cli", "model", "uv-density", input, "--texture-size", "100"};
	auto inspected = cli::runModelUvDensity(base);
	expect(inspected.exitCode == 0 && near(inspected.payload["before"].toObject()["pixelsPerUnit"].toDouble(), std::sqrt(250.)),
		   "CLI inspection parity", inspected.error);
	const auto args = base + QStringList{"--target", "5", "--output", output};
	auto dry = cli::runModelUvDensity(args + QStringList{"--dry-run"});
	expect(dry.exitCode == 0 && !QFile::exists(output), "CLI dry run", dry.error);
	auto saved = cli::runModelUvDensity(args);
	ModelDocument loaded;
	expect(saved.exitCode == 0 && loaded.load(output, &error) && uvIslandBytes(loaded.mesh()) == changed, "CLI and document output parity",
		   saved.error);
	expect(cli::runModelUvDensity(args).exitCode != 0 && cli::runModelUvDensity(args + QStringList{"--overwrite"}).exitCode == 0,
		   "explicit output overwrite");
	for (const auto &suffix : QList<QStringList>{{"--target", "5", "--output", input, "--overwrite"},
												 {"--target", "nan", "--output", output},
												 {"--faces", "0,0"},
												 {"--surface", "99"},
												 {"--frame", "1"},
												 {"--mode", "group"},
												 {"--dry-run"},
												 {"--texture-size", "64"},
												 {"--faces", "99"}})
		expect(cli::runModelUvDensity(base + suffix).exitCode != 0, "CLI rejects unrelated, malformed and destructive options");
	QFile sourceFile(input);
	expect(sourceFile.open(QIODevice::ReadOnly) && sourceFile.readAll() == bytes, "CLI never rewrites input");
	if (argc > 1)
	{
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(QString::fromLocal8Bit(argv[1]),
					  base.mid(1) + QStringList{"--json", "--settings-file", temp.filePath("settings.ini")});
		expect(process.waitForFinished(30000) && process.exitCode() == 0 &&
				   QJsonDocument::fromJson(process.readAllStandardOutput()).object().contains("before"),
			   "application dispatch reaches density inspector", QString::fromUtf8(process.readAllStandardError()));
	}
	// Many independent islands exercise one batched transform rather than N full
	// surface rewrites. This is a repeatable observation, not a maximum-size bound.
	auto dense = source;
	auto &s = dense.surfaces[0];
	const auto seed = s;
	s = {};
	s.name = "dense";
	s.frames.resize(2);
	for (int copy = 0; copy < 1024; ++copy)
	{
		const int offset = s.texCoords.size();
		s.texCoords += seed.texCoords;
		for (auto t : seed.triangles)
			s.triangles.append({t.a + offset, t.b + offset, t.c + offset});
		for (int f = 0; f < 2; ++f)
		{
			for (auto p : seed.frames[f].positions)
			{
				p.x += copy * 100;
				s.frames[f].positions.append(p);
			}
			s.frames[f].normals += seed.frames[f].normals;
		}
	}
	s.vertexCount = s.texCoords.size();
	updateEditableModelMetadata(&dense);
	auto denseEdit = operation();
	denseEdit.selection.faces.clear();
	for (int f = 0; f < s.triangles.size(); ++f)
		denseEdit.selection.faces.insert(f);
	QElapsedTimer timer;
	timer.start();
	expect(applyModelEdit(&dense, denseEdit, nullptr, &error), "batched 2048-island adjustment", error);
	std::cout << "2048 islands, two poses: " << timer.elapsed() << " ms; " << dense.surfaces[0].vertexCount << " vertices\n";
	std::cout << checks << " UV density checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
