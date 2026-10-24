#include "cli/model_tools.h"
#include "core/model_recovery.h"
#include "core/model_skeleton.h"
#include "tests/model_mdl_test_helpers.h"
#include "tests/model_spin_test_helpers.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
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
bool near(ModelVec3 a, ModelVec3 b, double tolerance = 2e-4)
{
	return model_geometry::length(model_geometry::point(a) - model_geometry::point(b)) < tolerance;
}
ModelMesh riggedProfile()
{
	auto mesh = spinProfile();
	mesh.frames.resize(1);
	mesh.frames[0].name = "bindpose";
	auto &surface = mesh.surfaces[0];
	surface.frames.resize(1);
	ModelJoint root, moving;
	root.name = "root";
	moving.name = "moving";
	root.bind = modelJointFromQuakeAngles(0, 0, 0, {2, 3, 0});
	moving.bind = modelJointFromQuakeAngles(0, 15, 0, {-3, 4, 2});
	mesh.skeleton.joints = {root, moving};
	ModelSkeletalClip clip;
	clip.name = "bend";
	clip.frames = {{root.bind, modelJointFromQuakeAngles(0, 30, 0, {-2, 4, 4})}};
	mesh.skeleton.clips = {clip};
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		surface.skinning.first.append(surface.skinning.influences.size());
		surface.skinning.count.append(2);
		for (int j = 0; j < 2; ++j)
		{
			const float weight = j == 0 ? .25f : .75f;
			const auto inverse = modelJointInverse(mesh.skeleton.joints[j].bind);
			auto position = surface.frames[0].positions[v];
			// Deliberately distinct joint offsets, whose weighted displacement cancels.
			position.x += j == 0 ? 3.f : -1.f;
			surface.skinning.influences.append(
				{j, weight, modelJointTransformPoint(inverse, position), modelJointTransformVector(inverse, surface.frames[0].normals[v])});
		}
	}
	QString error;
	expect(bakeModelSkeleton(&mesh, {}, &error), "bake two-joint profile with independent influence offsets", error);
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("spin-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	QString error;
	const auto source = spinProfile();
	const auto original = spinBytes(source);
	expect(validateEditableModel(source).isEmpty(), "valid independent two-pose profile");
	ModelDocument document;
	expect(document.setMesh(source, &error), "adopt profile", error);
	expect(document.edit(spinEdit(), &error), "sweep a partial curved face extrusion", error);
	expect(document.mesh().surfaces[0].triangles.size() == 66 && document.selection().faces.size() == 2,
		   "partial extrusion selects the end cap and creates all bands");
	const auto changed = document.mesh();
	const auto partial = spinBytes(document.mesh());
	expect(document.undo() && spinBytes(document.mesh()) == original, "undo restores the whole source", error);
	expect(document.redo() && spinBytes(document.mesh()) == partial, "redo reproduces the whole curved extrusion", error);
	for (bool poles : {false, true})
		for (double angle : {360.0, -360.0})
		{
			auto mesh = spinProfile(poles);
			ModelSelection selection;
			ModelEditReport report;
			expect(applyModelEdit(&mesh, spinEdit(angle), &selection, &error, {}, &report), "close a complete profile spin", error);
			expect(spinClosed(mesh.surfaces[0], poles ? 2 : 0), "complete spin is closed, consistently wound and has expected topology");
			expect(mesh.surfaces[0].triangles.size() == (poles ? 32 : 64), "axis poles make fans without degenerate triangles");
			expect(report.sourceFaces.value(0).size() == mesh.surfaces[0].triangles.size(), "every output face has source provenance");
			expect(!mesh.surfaces[0].uvSeams.isEmpty() && validateEditableModel(mesh).isEmpty(),
				   "spun UV boundaries and both poses validate");
		}
	{
		auto mesh = source;
		auto edit = spinEdit(360, 4);
		edit.tool.spinDuplicate = true;
		const auto seam = modelEdge(mesh.surfaces[0].triangles[0].a, mesh.surfaces[0].triangles[0].b);
		mesh.surfaces[0].uvSeams.insert(seam);
		ModelSelection selection;
		expect(applyModelEdit(&mesh, edit, &selection, &error), "radial face copies close without an overlapping endpoint", error);
		expect(mesh.surfaces[0].vertexCount == source.surfaces[0].vertexCount * 4 && mesh.surfaces[0].triangles.size() == 8 &&
				   selection.faces.size() == 6,
			   "four-step closed duplication yields four total islands");
		expect(mesh.surfaces[0].uvSeams.size() == 4, "each radial copy keeps authored seams");
		bool unchanged = true;
		for (int f = 0; f < source.frames.size(); ++f)
			for (int v = 0; v < source.surfaces[0].vertexCount; ++v)
			{
				const auto a = source.surfaces[0].frames[f].positions[v], b = mesh.surfaces[0].frames[f].positions[v];
				unchanged &= a.x == b.x && a.y == b.y && a.z == b.z;
			}
		expect(unchanged, "duplication preserves every original pose vertex");
	}
	{
		auto mesh = source;
		auto edit = spinEdit();
		edit.selection.faces.clear();
		const auto &s = mesh.surfaces[0];
		for (const auto &t : s.triangles)
			for (auto edge : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
				if (s.frames[0].positions[edge.first].x == 24 && s.frames[0].positions[edge.second].x == 24)
					edit.selection.edges.insert(edge);
		ModelSelection selection;
		expect(applyModelEdit(&mesh, edit, &selection, &error), "spin open border edges", error);
		expect(mesh.surfaces[0].triangles.size() == 18 && selection.edges.size() == 1 && selection.faces.isEmpty(),
			   "edge spin selects its leading edge");
	}
	QVector<ModelEdit> bad;
	for (double angle : {361.0, -361.0, std::numeric_limits<double>::quiet_NaN()})
	{
		auto e = spinEdit(angle);
		bad.append(e);
	}
	for (int steps : {0, 257})
	{
		auto e = spinEdit(90, steps);
		bad.append(e);
	}
	{
		auto e = spinEdit();
		e.tool.normal = {0, 0, 0};
		bad.append(e);
	}
	{
		auto e = spinEdit();
		e.frame = 0;
		bad.append(e);
	}
	{
		auto e = spinEdit(360, 2);
		bad.append(e);
	}
	{
		auto e = spinEdit(180, 1);
		bad.append(e);
	}
	{
		auto e = spinEdit();
		e.selection = {};
		bad.append(e);
	}
	for (const auto &edit : bad)
	{
		auto mesh = source;
		ModelSelection selection;
		selection.faces = {99};
		expect(!applyModelEdit(&mesh, edit, &selection, &error) && spinBytes(mesh) == original && selection.faces.contains(99),
			   "invalid spin retains geometry and caller selection");
	}
	{
		auto mesh = source;
		bool pulse = false, observed = false;
		ModelWorkControl control;
		control.progress = [&](ModelWorkPhase phase, qint64 done, qint64)
		{
			if (phase == ModelWorkPhase::Editing && done >= 256)
				pulse = true;
		};
		control.cancelled = [&]
		{
			if (!pulse || observed)
				return false;
			observed = true;
			return true;
		};
		expect(!applyModelEdit(&mesh, spinEdit(360, 128), nullptr, &error, control) && observed && spinBytes(mesh) == original,
			   "one-shot cancellation during geometry generation preserves the source", error);
	}
	{
		auto mesh = source;
		for (auto &p : mesh.surfaces[0].frames[1].positions)
			if (p.x == 20)
				p.x = 0;
		updateEditableModelMetadata(&mesh);
		const auto before = spinBytes(mesh);
		expect(validateEditableModel(mesh).isEmpty(), "late-pose pole fixture is valid before spinning");
		expect(!applyModelEdit(&mesh, spinEdit(), nullptr, &error) && spinBytes(mesh) == before,
			   "pole appearing in only one pose refuses collapsed late-pose triangles atomically");
	}
	{
		auto mesh = source;
		auto &s = mesh.surfaces[0];
		s.vertexCount = modelDocumentMaxVertices;
		s.texCoords.resize(s.vertexCount);
		for (auto &frame : s.frames)
		{
			frame.positions.resize(s.vertexCount);
			frame.normals.resize(s.vertexCount, {0, 1, 0});
		}
		updateEditableModelMetadata(&mesh);
		const auto before = spinBytes(mesh);
		expect(validateEditableModel(mesh).isEmpty(), "capacity fixture is a valid maximum-vertex source");
		expect(!applyModelEdit(&mesh, spinEdit(), nullptr, &error) && error.contains("limit") && spinBytes(mesh) == before,
			   "document vertex capacity refuses before allocating a sweep", error);
	}
	{
		auto mesh = source;
		mesh.surfaces.append(source.surfaces[0]);
		mesh.surfaces.last().name = "unselected";
		mesh.surfaces[0].skinPaths = {"models/spin/authored.tga"};
		mesh.tags = {{"tag_tool", 0, {3, 4, 5}}, {"tag_tool", 1, {5, 7, 9}}};
		mesh.collisionBoxes = {{"collision", {1, 2, 3}, {8, 16, 32}, {0, 15, 0}}};
		updateEditableModelMetadata(&mesh);
		const auto before = editableModelJson(mesh);
		expect(applyModelEdit(&mesh, spinEdit(), nullptr, &error), "spin a surface with auxiliary asset data", error);
		const auto after = editableModelJson(mesh);
		expect(before["surfaces"].toArray()[1] == after["surfaces"].toArray()[1] && before["tags"] == after["tags"] &&
				   before["collisionBoxes"] == after["collisionBoxes"] && before["animations"] == after["animations"] &&
				   mesh.surfaces[0].skinPaths == QStringList{"models/spin/authored.tga"},
			   "spin preserves unselected surfaces, materials, tags, collision and animation definitions");
	}
	{
		auto mesh = source;
		QElapsedTimer timer;
		timer.start();
		expect(applyModelEdit(&mesh, spinEdit(360, 256), nullptr, &error) && spinClosed(mesh.surfaces[0], 0),
			   "maximum step count closes a valid two-pose ring", error);
		std::cout << "256-step ring, four input vertices, two poses: " << timer.elapsed() << " ms; " << mesh.vertexCount
				  << " output vertices\n";
	}
	// Rotate around an oblique axis through a translated centre. A half turn
	// swaps X and Y and negates Z around this particular axis, independently
	// checked here rather than repeating the implementation's rotation formula.
	{
		auto mesh = source;
		auto edit = spinEdit(180, 1);
		edit.tool.spinDuplicate = true;
		edit.tool.normal = {1, 1, 0};
		edit.tool.point = {3, 7, 11};
		expect(applyModelEdit(&mesh, edit, nullptr, &error), "duplicate about a translated oblique axis", error);
		bool exact = true;
		for (int f = 0; f < source.frames.size(); ++f)
			for (int v = 0; v < source.surfaces[0].vertexCount; ++v)
			{
				const auto p = source.surfaces[0].frames[f].positions[v];
				exact &= near(mesh.surfaces[0].frames[f].positions[v + source.surfaces[0].vertexCount], {p.y - 4, p.x + 4, 22 - p.z});
			}
		expect(exact, "oblique-axis copies transform every independently authored pose");
	}
	for (bool duplicate : {false, true})
		for (int reference : {0, 1})
		{
			const auto rig = riggedProfile();
			auto mesh = rig;
			auto edit = spinEdit(90, 4);
			edit.tool.spinDuplicate = duplicate;
			edit.tool.referenceFrame = reference;
			if (!expect(applyModelEdit(&mesh, edit, nullptr, &error), "spin from bind and animated reference poses", error))
				continue;
			const auto &s = mesh.surfaces[0];
			bool matches = true, weights = true;
			for (int f = 0; f < mesh.frames.size(); ++f)
			{
				ModelFrameGeometry baked;
				const auto pose = f == 0 ? modelSkeletonBindPose(mesh.skeleton) : mesh.skeleton.clips[0].frames[f - 1];
				matches &= skinModelSurface(s, pose, &baked, &error);
				for (int v = 0; v < s.vertexCount; ++v)
					matches &= near(baked.positions[v], s.frames[f].positions[v]) && near(baked.normals[v], s.frames[f].normals[v]) &&
							   std::abs(model_geometry::length(model_geometry::point(baked.normals[v])) - 1) < 1e-4;
			}
			for (int v = 0; v < s.vertexCount; ++v)
			{
				weights &= s.skinning.count[v] == 2;
				const auto first = s.skinning.first[v];
				weights &= s.skinning.influences[first].joint == 0 && s.skinning.influences[first].weight == .25f &&
						   s.skinning.influences[first + 1].joint == 1 && s.skinning.influences[first + 1].weight == .75f;
			}
			expect(matches && weights, "spin retains actual joint ownership and weights and reproduces every skinned pose and normal",
				   error);
			if (duplicate)
			{
				bool untouched = true;
				for (int i = 0; i < rig.surfaces[0].skinning.influences.size(); ++i)
				{
					const auto a = rig.surfaces[0].skinning.influences[i], b = s.skinning.influences[i];
					untouched &= a.joint == b.joint && a.weight == b.weight && near(a.offset, b.offset, 1e-8) &&
								 near(a.normalOffset, b.normalOffset, 1e-8);
				}
				expect(untouched, "radial copies leave original authored influence offsets untouched");
			}
			for (const auto &format : QStringList{"md5mesh", "iqm"})
			{
				const auto data = exportEditableModel(mesh, format, 0, &error);
				const auto restored = decodeModelMesh("spin." + format, data);
				expect(!data.isEmpty() && restored.isValid() && restored.skeleton.joints.size() == 2 &&
						   restored.surfaces[0].triangles.size() == s.triangles.size(),
					   "skeletal formats retain spun mesh and rig", error);
			}
		}
	{
		auto mesh = riggedProfile();
		mesh.frames.append(mesh.frames[0]);
		mesh.frames.last().name = "authored";
		mesh.skeleton.bakedClipForFrame.append(-2);
		mesh.surfaces[0].frames.append(source.surfaces[0].frames[1]);
		updateEditableModelMetadata(&mesh);
		auto edit = spinEdit(90, 1);
		edit.tool.spinDuplicate = true;
		if (!expect(applyModelEdit(&mesh, edit, nullptr, &error), "spin preserves independent poses alongside retained skeletal poses",
					error))
			return 1;
		bool authored = true;
		for (int v = 0; v < source.surfaces[0].vertexCount; ++v)
		{
			const auto p = source.surfaces[0].frames[1].positions[v];
			authored &= near(mesh.surfaces[0].frames[2].positions[v + source.surfaces[0].vertexCount], {-p.y, p.x, p.z});
		}
		expect(authored && mesh.skeleton.bakedClipForFrame[2] == -2, "independent baked poses rotate their own geometry");
		const auto before = spinBytes(mesh);
		edit.tool.referenceFrame = 2;
		expect(!applyModelEdit(&mesh, edit, nullptr, &error) && spinBytes(mesh) == before,
			   "independent skeletal reference is refused atomically");
	}
	{
		auto mesh = riggedProfile();
		mesh.surfaces[0].skinning.influences[0].weight = -1;
		const auto before = spinBytes(mesh);
		expect(!applyModelEdit(&mesh, spinEdit(), nullptr, &error) && spinBytes(mesh) == before,
			   "negative skin weights refuse before modifying source");
	}
	for (bool splitRange : {false, true})
	{
		auto mesh = riggedProfile();
		mesh.skeleton.clips[0].frames.append(mesh.skeleton.clips[0].frames[0]);
		if (splitRange)
		{
			mesh.frames.append(mesh.frames[0]);
			mesh.frames.append(mesh.frames[1]);
			mesh.surfaces[0].frames.append(mesh.surfaces[0].frames[0]);
			mesh.surfaces[0].frames.append(mesh.surfaces[0].frames[1]);
			mesh.skeleton.bakedClipForFrame = {-1, 0, -2, 0};
		}
		updateEditableModelMetadata(&mesh);
		const auto before = spinBytes(mesh);
		expect(!applyModelEdit(&mesh, spinEdit(), nullptr, &error) && error.contains("contiguous") && spinBytes(mesh) == before,
			   "partial or interrupted clip mapping refuses without guessing a skeletal pose", error);
	}
	for (const auto &format : QStringList{"mdl", "md2", "md3"})
	{
		auto native = changed;
		native.surfaces[0].skinPaths = {"models/spin/skin.pcx"};
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
		expect(applyModelEdit(&native, pack, nullptr, &error), "pack generated strip charts for native skin budgets", error);
		const auto data = exportEditableModel(native, format, 0, &error);
		const auto restored = decodeModelMesh("spin." + format, data);
		expect(!data.isEmpty() && restored.isValid() && restored.frames.size() == 2 &&
				   restored.surfaces[0].triangles.size() == changed.surfaces[0].triangles.size(),
			   "Quake-family exports retain spun topology and both poses", error);
	}
	{
		ModelSelection selected;
		selected.faces = {0};
		const auto recovery =
			writeModelRecovery({changed, selected}, temporary.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
		ModelRecoverySnapshot restored;
		expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() && spinBytes(restored.mesh) == partial &&
				   restored.selection == selected,
			   "recovery retains spin geometry, UVs, poses and selection", error);
	}
	const auto input = temporary.filePath("profile.mesh.json"), output = temporary.filePath("spun.mesh.json");
	expect(document.setMesh(source, &error) && document.save(input, false, &error), "save source for actual CLI", error);
	if (argc > 1)
	{
		const auto run = [&](QStringList extra, int expected)
		{
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(QString::fromLocal8Bit(argv[1]),
						  QStringList{"--cli", "--settings-file", temporary.filePath("settings.ini"), "model", "tool", input, "--tool",
									  "spin", "--faces", "all", "--output", output, "--json"} +
							  extra);
			if (!process.waitForFinished(30000))
			{
				process.kill();
				process.waitForFinished();
				return false;
			}
			const auto bytes = process.readAllStandardOutput();
			if (process.exitCode() != expected)
				std::cerr << bytes.toStdString() << process.readAllStandardError().toStdString();
			return process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected;
		};
		expect(run({"--angle", "360", "--axis", "z", "--dry-run"}, 0) && !QFileInfo::exists(output),
			   "actual CLI validates a closed spin without writing");
		expect(run({"--angle", "360", "--axis-vector", "0,0,5"}, 0), "actual CLI publishes the same spin with an arbitrary axis vector");
		ModelDocument loaded;
		expect(loaded.load(output, &error) && spinClosed(loaded.mesh().surfaces[0], 0), "CLI output reloads as a closed animated ring",
			   error);
		expect(run({"--angle", "360", "--overwrite", "--axis", "z", "--axis-vector", "1,0,0"}, 2), "CLI rejects conflicting axis forms");
		expect(run({"--steps", "257", "--overwrite"}, 2), "CLI enforces the step bound before publication");
		expect(run({"--angle", "360", "--steps", "4", "--duplicate", "--no-close", "--overwrite"}, 0),
			   "CLI can explicitly keep the coincident endpoint for radial copies");
		expect(loaded.load(output, &error) && loaded.mesh().surfaces[0].triangles.size() == 10,
			   "disabled closure yields the original plus four copied face islands", error);
		expect(run({"--width", "2", "--overwrite"}, 2), "CLI refuses unrelated spin options");
	}
	std::cout << checks << " spin core/CLI checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
