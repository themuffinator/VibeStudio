#include "cli/model_tools.h"
#include "core/model_geometry_helpers.h"
#include "core/model_recovery.h"
#include "tests/model_mdl_test_helpers.h"
#include "tests/model_rip_test_helpers.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <iostream>
#include <limits>

using namespace vibestudio;
using namespace vibestudio::cli;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool ok, const char *message, const QString &error = {})
{
	++checks;
	if (!ok)
	{
		++failures;
		std::cerr << "FAIL: " << message << ": " << error.toStdString() << '\n';
	}
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("rip-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto source = ripGrid();
	QString error;
	expect(validateEditableModel(source).isEmpty(), "valid two-pose triangulated grid");
	for (bool edge : {false, true})
		for (bool fill : {false, true})
			for (bool opposite : {false, true})
			{
				auto mesh = source;
				auto edit = ripEdit(mesh, edge, fill);
				edit.tool.ripOpposite = opposite;
				edit.translation.x = opposite ? -4.f : 4.f;
				ModelRipPlan plan;
				if (!expect(planModelRip(mesh.surfaces[0], edit.selection, edit.tool, &plan, &error),
							"plan one coherent side of a vertex/edge rip", error))
					continue;
				expect(plan.cuts.size() == 2 && !plan.movingFaces.isEmpty(), "rip extends to two neighbours or the open border");
				ModelSelection selected;
				ModelEditReport report;
				if (!expect(applyModelEdit(&mesh, edit, &selected, &error, {}, &report),
							"apply open and filled vertex/edge rips to either side", error))
					continue;
				expect(validateEditableModel(mesh).isEmpty() && mesh.surfaces[0].triangles.size() == 8 + (fill ? edge ? 3 : 2 : 0),
					   "filled rips make only nondegenerate gap triangles");
				expect(edge ? selected.edges.size() == 1 && selected.vertices.isEmpty()
							: selected.vertices.size() == 1 && selected.edges.isEmpty(),
					   "result selects the moved components");
				expect(report.sourceFaces.value(0).size() == mesh.surfaces[0].triangles.size(),
					   "face provenance covers originals and fill");
				bool valid = true;
				ModelGeometricTopology topology;
				buildModelGeometricTopology(mesh.surfaces[0], &topology);
				for (auto i = topology.edgeFaces.cbegin(); i != topology.edgeFaces.cend(); ++i)
					valid &= i->size() <= 2;
				for (const auto &pose : mesh.surfaces[0].frames)
					for (const auto &t : mesh.surfaces[0].triangles)
						valid &= !model_geometry::collapsedTriangle(pose.positions[t.a], pose.positions[t.b], pose.positions[t.c]);
				expect(valid, "all poses retain manifold noncollapsed geometry");
			}
	ModelDocument document;
	expect(document.setMesh(source, &error) && document.edit(ripEdit(source), &error), "rip through document service", error);
	const auto result = ripBytes(document.mesh());
	expect(document.undo() && ripBytes(document.mesh()) == ripBytes(source), "undo restores original topology and attributes");
	expect(document.redo() && ripBytes(document.mesh()) == result, "redo restores the rip");
	const auto near = [](ModelVec3 a, ModelVec3 b)
	{ return model_geometry::length(model_geometry::point(a) - model_geometry::point(b)) < 2e-4; };
	for (bool fill : {false, true})
	{
		auto mesh = ripSeams();
		auto edit = ripEdit(source, false, fill);
		edit.selection.vertices = {ripVertex(mesh, 0, 0)};
		const auto before = mesh;
		expect(applyModelEdit(&mesh, edit, nullptr, &error), "rip respects geometric adjacency through UV corner splits", error);
		bool preserved = true;
		for (int v = 0; v < before.surfaces[0].vertexCount; ++v)
			preserved &= mesh.surfaces[0].texCoords[v].u == before.surfaces[0].texCoords[v].u &&
						 mesh.surfaces[0].texCoords[v].v == before.surfaces[0].texCoords[v].v;
		expect(preserved && validateEditableModel(mesh).isEmpty(), "original corner UVs remain distinct and seam marks valid");
	}
	for (bool fill : {false, true})
	{
		auto mesh = ripGrid(5);
		auto edit = ripEdit(mesh, true, fill);
		edit.selection.edges.insert(modelEdge(ripVertex(mesh, 0, 0), ripVertex(mesh, 0, 16)));
		const auto before = mesh;
		ModelSelection selected;
		ModelEditReport report;
		expect(applyModelEdit(&mesh, edit, &selected, &error, {}, &report) && selected.edges.size() == 2,
			   "rip a longer chain with two interior endpoints", error);
		bool unmoved = true;
		for (int f = 0; f < before.surfaces[0].triangles.size(); ++f)
			unmoved &= report.sourceFaces.value(0).value(f, -1) == f;
		expect(unmoved && validateEditableModel(mesh).isEmpty(), "long-chain originals retain face provenance");
	}
	{
		auto mesh = source;
		auto other = mesh.surfaces[0];
		other.name = "other";
		mesh.surfaces.append(other);
		updateEditableModelMetadata(&mesh);
		const auto before = mesh;
		expect(applyModelEdit(&mesh, ripEdit(source), nullptr, &error), "rip within a multi-surface document", error);
		mesh.surfaces[0] = before.surfaces[0];
		updateEditableModelMetadata(&mesh);
		expect(ripBytes(mesh) == ripBytes(before), "unselected surface and document metadata stay intact");
	}
	for (bool nonmanifold : {false, true})
	{
		auto mesh = source;
		if (nonmanifold)
			mesh.surfaces[0].triangles.append(mesh.surfaces[0].triangles[ripEdit(source).tool.ripFace]);
		else
			std::swap(mesh.surfaces[0].triangles[0].a, mesh.surfaces[0].triangles[0].b);
		updateEditableModelMetadata(&mesh);
		const auto before = ripBytes(mesh);
		expect(!applyModelEdit(&mesh, ripEdit(source), nullptr, &error) && ripBytes(mesh) == before,
			   "winding and nonmanifold neighbourhoods refuse atomically", error);
	}
	{
		auto mesh = ripGrid(256);
		const auto before = ripBytes(mesh);
		expect(mesh.surfaces[0].vertexCount == 65536 && validateEditableModel(mesh).isEmpty(), "valid maximum-vertex fixture");
		expect(!applyModelEdit(&mesh, ripEdit(mesh), nullptr, &error) && ripBytes(mesh) == before,
			   "capacity overflow refuses before appending topology", error);
	}
	{
		auto mesh = source;
		auto edit = ripEdit(mesh);
		edit.kind = ModelEditKind::SmoothVertices;
		expect(!applyModelEdit(&mesh, edit, nullptr, &error) && ripBytes(mesh) == ripBytes(source),
			   "unrelated operations reject rip settings");
		ModelRipPlan sentinel;
		sentinel.sideFace = 987;
		auto plan = sentinel;
		auto surface = source.surfaces[0];
		surface.frames[0].positions.removeLast();
		expect(!planModelRip(surface, ripEdit(source).selection, ripEdit(source).tool, &plan, &error) && plan.sideFace == 987,
			   "standalone planner validates malformed poses without publishing a partial plan", error);
	}
	const auto rig = ripRig();
	expect(rig.isValid() && validateEditableModel(rig).isEmpty(), "valid two-joint fixture with unequal weights and offsets");
	for (int reference : {0, 1})
		for (bool fill : {false, true})
		{
			auto mesh = rig;
			auto edit = ripEdit(source, false, fill);
			edit.tool.referenceFrame = reference;
			if (!expect(applyModelEdit(&mesh, edit, nullptr, &error), "rip a retained rig from bind or animated reference", error))
				continue;
			auto baked = mesh;
			expect(bakeModelSkeleton(&baked, {}, &error), "rebake modified influence offsets", error);
			bool same = true, weights = true;
			const auto &s = mesh.surfaces[0];
			for (int frame = 0; frame < mesh.frames.size(); ++frame)
				for (int v = 0; v < s.vertexCount; ++v)
				{
					same &= near(s.frames[frame].positions[v], baked.surfaces[0].frames[frame].positions[v]);
					same &= near(s.frames[frame].normals[v], baked.surfaces[0].frames[frame].normals[v]);
					const int first = s.skinning.first[v];
					weights &= s.skinning.count[v] == 2 && s.skinning.influences[first].joint == 0 &&
							   s.skinning.influences[first].weight == .25f && s.skinning.influences[first + 1].joint == 1 &&
							   s.skinning.influences[first + 1].weight == .75f;
				}
			expect(same && weights, "all retained poses and normals reproduce from preserved joint ownership and weights");
			for (const auto &format : QStringList{"md5mesh", "iqm"})
			{
				const auto data = exportEditableModel(mesh, format, 0, &error);
				const auto restored = decodeModelMesh("rip." + format, data);
				expect(!data.isEmpty() && restored.isValid() && restored.skeleton.joints.size() == 2 &&
						   restored.surfaces[0].triangles.size() == s.triangles.size(),
					   "skeletal exports preserve ripped topology and rig", error);
			}
		}
	{
		auto mesh = rig;
		mesh.frames.append(source.frames[1]);
		mesh.surfaces[0].frames.append(source.surfaces[0].frames[1]);
		mesh.skeleton.bakedClipForFrame.append(-2);
		updateEditableModelMetadata(&mesh);
		ModelSelection selected;
		expect(applyModelEdit(&mesh, ripEdit(source), &selected, &error), "rip retains independent baked poses alongside rig", error);
		const int moved = selected.vertices.isEmpty() ? -1 : *selected.vertices.cbegin();
		expect(moved >= 0 && near(mesh.surfaces[0].frames[2].positions[moved], {4, 0, 0}),
			   "independent pose receives the explicit world displacement");
		const auto bytes = ripBytes(mesh);
		auto edit = ripEdit(source);
		edit.tool.referenceFrame = 2;
		expect(!applyModelEdit(&mesh, edit, nullptr, &error) && ripBytes(mesh) == bytes,
			   "independent rig reference refuses without guessing bindings");
	}
	for (int invalid = 0; invalid < 9; ++invalid)
	{
		auto mesh = source;
		auto edit = ripEdit(source);
		if (invalid == 0)
		{
			edit.translation = {};
			edit.tool.ripFill = true;
		}
		if (invalid == 1)
			edit.tool.ripFace = -1;
		if (invalid == 2)
			edit.selection.faces = {0};
		if (invalid == 3)
			edit.selection.vertices = {999};
		if (invalid == 4)
			edit.selection.vertices << ripVertex(source, -16, 16);
		if (invalid == 5)
			edit.translation.x = 40;
		if (invalid == 6)
			edit.translation.x = std::numeric_limits<float>::quiet_NaN();
		if (invalid == 7)
			edit.frame = 0;
		if (invalid == 8)
		{
			mesh.surfaces[0].frames[1].positions[ripVertex(source, 16, 0)].x = 2;
		}
		const auto bytes = ripBytes(mesh);
		ModelSelection sentinel;
		sentinel.vertices = {888};
		auto selected = sentinel;
		expect(!applyModelEdit(&mesh, edit, &selected, &error) && !error.isEmpty() && ripBytes(mesh) == bytes && selected == sentinel,
			   "invalid selection, movement, pose or topology refuses atomically", error);
	}
	{
		auto mesh = source;
		auto edit = ripEdit(mesh);
		edit.selection.edges = {modelEdge(ripVertex(mesh, 0, 0), ripVertex(mesh, 0, -16)),
								modelEdge(ripVertex(mesh, 0, 0), ripVertex(mesh, 16, 0)),
								modelEdge(ripVertex(mesh, 0, 0), ripVertex(mesh, 0, 16))};
		expect(!applyModelEdit(&mesh, edit, nullptr, &error) && ripBytes(mesh) == ripBytes(source), "branching rip junction is refused");
	}
	{
		auto mesh = source;
		int polls = 0;
		ModelWorkControl control;
		control.cancelled = [&]
		{
			++polls;
			return false;
		};
		expect(applyModelEdit(&mesh, ripEdit(source, false, true), nullptr, &error, control) && polls > 10,
			   "filled rip exposes cancellation checkpoints");
		for (int at : {1, polls / 2, polls - 1})
		{
			mesh = source;
			int count = 0;
			control.cancelled = [&] { return ++count == at; };
			expect(!applyModelEdit(&mesh, ripEdit(source, false, true), nullptr, &error, control) && ripBytes(mesh) == ripBytes(source),
				   "one-shot early, middle and late cancellation leaves source intact", error);
		}
	}
	for (const auto &format : QStringList{"mdl", "md2", "md3"})
	{
		auto native = source;
		expect(applyModelEdit(&native, ripEdit(source, false, true), nullptr, &error), "prepare filled native export");
		native.surfaces[0].skinPaths = {"models/rip/skin.pcx"};
		if (format == "mdl")
		{
			const auto fixture = decodeModelMesh("synthetic.mdl", groupedMdlFixture().bytes);
			native.mdl = fixture.mdl;
			native.mdl.frameGroups = {{0, {.1f, .2f}}};
			native.embeddedSkins = fixture.embeddedSkins;
			updateEditableModelMetadata(&native);
		}
		ModelEdit pack;
		pack.kind = ModelEditKind::PackUv;
		for (int f = 0; f < native.surfaces[0].triangles.size(); ++f)
			pack.selection.faces.insert(f);
		expect(applyModelEdit(&native, pack, nullptr, &error), "pack fill charts for native skin budgets", error);
		const auto data = exportEditableModel(native, format, 0, &error);
		const auto decoded = decodeModelMesh("rip." + format, data);
		expect(!data.isEmpty() && decoded.isValid() && decoded.frames.size() == 2 && decoded.surfaces[0].triangles.size() == 10,
			   "Quake-family exports retain filled rip and both poses", error);
	}
	{
		const auto recovery = writeModelRecovery({document.mesh(), document.selection()}, temporary.path(),
												 QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
		ModelRecoverySnapshot restored;
		expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() && ripBytes(restored.mesh) == result &&
				   restored.selection == document.selection(),
			   "recovery retains rip geometry, poses, UVs and selection", error);
	}
	const auto input = temporary.filePath("grid.mesh.json"), output = temporary.filePath("ripped.mesh.json");
	expect(document.setMesh(source, &error) && document.save(input, false, &error), "save actual CLI fixture", error);
	const auto edit = ripEdit(source);
	const QStringList args{"model",
						   "tool",
						   input,
						   "--tool",
						   "rip",
						   "--vertices",
						   QString::number(*edit.selection.vertices.cbegin()),
						   "--side-face",
						   QString::number(edit.tool.ripFace),
						   "--offset",
						   "4,0,0",
						   "--output",
						   output,
						   "--json"};
	const auto dryRun = runModelTool(QStringList{"vibestudio"} + args + QStringList{"--dry-run", "--fill"});
	expect(dryRun.exitCode == 0 && !QFile::exists(output), "CLI dry run validates fill without writing", dryRun.error);
	expect(runModelTool(QStringList{"vibestudio"} + args + QStringList{"--steps", "4"}).exitCode == 2, "CLI rejects unrelated options");
	if (argc > 1)
	{
		const auto run = [&](QStringList extra, int expected, QStringList command = {})
		{
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(QString::fromLocal8Bit(argv[1]),
						  QStringList{"--cli", "--settings-file", temporary.filePath("settings.ini")} + (command.isEmpty() ? args : command) + extra);
			if (!process.waitForFinished(30000))
			{
				process.kill();
				process.waitForFinished();
				return false;
			}
			const auto bytes = process.readAllStandardOutput();
			if (process.exitCode() != expected)
				std::cerr << bytes.toStdString() << process.readAllStandardError().toStdString();
			return process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected &&
				   QJsonDocument::fromJson(bytes).isObject();
		};
		expect(run({}, 0), "actual CLI writes ripped editable model");
		ModelDocument restored;
		expect(restored.load(output, &error) && ripBytes(restored.mesh()) == result, "GUI service and actual CLI agree", error);
		expect(run({}, 1), "actual CLI refuses an existing output");
		expect(run({"--overwrite", "--fill"}, 0), "actual CLI explicitly replaces output with filled rip");
		auto zero = args;
		zero[zero.indexOf("--offset") + 1] = "0,0,0";
		auto zeroMesh = source;
		auto zeroEdit = edit; zeroEdit.translation = {};
		expect(applyModelEdit(&zeroMesh, zeroEdit, nullptr, &error) && run({"--overwrite"}, 0, zero) &&
			restored.load(output, &error) && ripBytes(restored.mesh()) == ripBytes(zeroMesh),
			"actual CLI persists a zero-distance open split", error);
		expect(run({"--overwrite", "--fill"}, 4, zero) && restored.load(output, &error) && ripBytes(restored.mesh()) == ripBytes(zeroMesh),
			"zero-distance fill refusal preserves the saved split", error);
	}
	std::cout << checks << " rip core checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
