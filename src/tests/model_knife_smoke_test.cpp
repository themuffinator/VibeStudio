#include "cli/model_tools.h"
#include "tests/model_output_test_helpers.h"
#include "core/model_knife.h"
#include "core/model_recovery.h"
#include "core/model_skeleton.h"
#include "tests/model_edge_bevel_test_helpers.h"
#include "tests/model_mdl_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <iostream>
#include <limits>
#include <random>

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
ModelMesh plane()
{
	auto mesh = bevelCube();
	auto &s = mesh.surfaces[0];
	s.vertexCount = 4;
	s.texCoords = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	s.triangles = {{0, 1, 2}, {0, 2, 3}};
	s.frames[0].positions = {{0, 0, 0}, {64, 0, 0}, {64, 64, 0}, {0, 64, 0}};
	s.frames[1].positions = {{3, 5, 7}, {3, 133, 7}, {-93, 133, 7}, {-93, 5, 7}};
	for (auto &frame : s.frames)
		frame.normals = QVector<ModelVec3>(4, {0, 0, 1});
	s.uvSeams.clear();
	updateEditableModelMetadata(&mesh);
	return mesh;
}
ModelEdit knife(const QVector<ModelKnifeSegment> &segments, bool through = false)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::Knife;
	edit.tool.knife.segments = segments;
	edit.tool.knife.through = through;
	return edit;
}
bool areaPreserved(const ModelMesh &mesh)
{
	using namespace model_geometry;
	double sum = 0;
	const auto &s = mesh.surfaces[0];
	for (const auto &t : s.triangles)
	{
		const auto &p = s.frames[0].positions;
		const auto n = cross(point(p[t.b]) - point(p[t.a]), point(p[t.c]) - point(p[t.a]));
		if (n.z <= 0)
			return false;
		sum += n.z / 2;
	}
	return std::abs(sum - 4096) < 1e-3;
}
bool near(ModelVec3 a, ModelVec3 b, double tolerance = 1e-4)
{
	return model_geometry::length(model_geometry::point(a) - model_geometry::point(b)) < tolerance;
}
ModelVec3 cutPoint(const ModelMesh &mesh, const ModelKnifeCut &cut, const std::array<double, 2> &p)
{
	using namespace model_geometry;
	const auto &s = mesh.surfaces[0];
	const auto t = s.triangles[cut.face];
	const auto &v = s.frames[0].positions;
	const auto hit = point(v[t.a]) * (1 - p[0] - p[1]) + point(v[t.b]) * p[0] + point(v[t.c]) * p[1];
	return {float(hit.x), float(hit.y), float(hit.z)};
}
double plannedLength(const ModelMesh &mesh, const QVector<ModelKnifeCut> &cuts)
{
	double result = 0;
	for (const auto &cut : cuts)
		result +=
			model_geometry::length(model_geometry::point(cutPoint(mesh, cut, cut.a)) - model_geometry::point(cutPoint(mesh, cut, cut.b)));
	return result;
}
ModelMesh skinnedCube()
{
	auto mesh = bevelCube();
	mesh.frames.resize(1);
	mesh.frames[0].name = "bindpose";
	auto &s = mesh.surfaces[0];
	s.frames.resize(1);
	ModelJoint root, moving;
	root.name = "root";
	moving.name = "moving";
	root.bind = moving.bind = modelJointIdentity();
	mesh.skeleton.joints = {root, moving};
	ModelSkeletalClip clip;
	clip.name = "bend";
	clip.frames = {{root.bind, modelJointFromQuakeAngles(0, 15, 0, {8, 4, 2})}};
	mesh.skeleton.clips = {clip};
	for (int v = 0; v < s.vertexCount; ++v)
	{
		s.skinning.first.append(s.skinning.influences.size());
		s.skinning.count.append(1);
		s.skinning.influences.append({s.frames[0].positions[v].x < 0 ? 0 : 1, 1, s.frames[0].positions[v], s.frames[0].normals[v]});
	}
	QString error;
	expect(bakeModelSkeleton(&mesh, {}, &error), "bake independently moving knife fixture", error);
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
	QTemporaryDir temp(QDir(root).filePath("knife-XXXXXX"));
	if (!temp.isValid())
		return 1;
	QString error;
	ModelSelection selection;
	const auto source = plane();
	expect(validateEditableModel(source).isEmpty(), "valid two-pose plane fixture");
	const QVector<QVector<ModelKnifeSegment>> paths{
		{{{-8, 32}, {72, 32}}},
		{{{12, 8}, {52, 8}}},
		{{{8, 8}, {48, 8}}, {{48, 8}, {32, 48}}, {{32, 48}, {8, 8}}},
		{{{-1, 32}, {65, 32}}, {{32, -1}, {32, 65}}},
		{{{0, 0}, {64, 64}}},
		{{{0, 32}, {32, 32}}, {{32, 32}, {64, 32}}},
		{{{8, 8}, {56, 56}}, {{16, 16}, {48, 48}}},
	};
	for (const auto &path : paths)
	{
		auto mesh = source;
		if (!expect(applyModelEdit(&mesh, knife(path), &selection, &error), "cut finite/interior/crossing/closed/collinear path", error))
			continue;
		expect(areaPreserved(mesh) && validateEditableModel(mesh).isEmpty(), "cut retains area and valid triangles in both poses");
		expect(!selection.edges.isEmpty(), "cut selects actual path edges");
		const auto &s = mesh.surfaces[0];
		bool affine = true;
		for (int i = 0; i < s.vertexCount; ++i)
		{
			const auto p = s.frames[0].positions[i], q = s.frames[1].positions[i];
			affine &= std::abs(q.x - (3 - 1.5 * p.y)) < 1e-4 && std::abs(q.y - (5 + 2 * p.x)) < 1e-4 && q.z == 7;
			affine &= std::abs(s.texCoords[i].u - p.x / 64.) < 1e-6 && std::abs(s.texCoords[i].v - p.y / 64.) < 1e-6;
		}
		expect(affine, "cuts retain pose correspondence and UV interpolation");
	}
	for (bool through : {false, true})
	{
		auto mesh = bevelCube();
		expect(applyModelEdit(&mesh, knife({{{-80, 3}, {80, 3}}}, through), &selection, &error), "cube visible/through cut", error);
		expect(closedBevel(mesh), "cube remains geometrically closed across seam copies");
	}
	{
		auto mesh = source;
		QVector<ModelKnifeSegment> grid;
		for (int i = 1; i < 10; ++i)
		{
			grid.append(ModelKnifeSegment{{-1, double(i * 6)}, {65, double(i * 6)}});
			grid.append(ModelKnifeSegment{{double(i * 6), -1}, {double(i * 6), 65}});
		}
		expect(applyModelEdit(&mesh, knife(grid), &selection, &error), "intersecting grid constraints", error);
		expect(areaPreserved(mesh), "grid retains area");
	}
	// Continuous visibility: the nearer plane switches inside an overlap.
	const auto stroke = knife({{{-1, 16}, {65, 16}}});
	for (bool perspective : {false, true})
	{
		auto mesh = source;
		auto occluder = source.surfaces[0];
		occluder.name = "occluder";
		for (auto &p : occluder.frames[0].positions)
			p.z = p.x - 32;
		mesh.surfaces.append(occluder);
		updateEditableModelMetadata(&mesh);
		auto options = stroke.tool.knife;
		if (perspective)
		{
			options.perspective = true;
			options.origin = {32, 32, 128};
			options.segments = {{{-.3, -.125}, {.3, -.125}}};
		}
		QVector<ModelKnifeCut> cuts;
		expect(planModelKnife(mesh, 0, 0, {}, options, &cuts, &error) && std::abs(plannedLength(mesh, cuts) - 32) < 1e-4,
			   "depth crossing splits a stroke at the true visibility transition", error);
		bool frontHalf = true;
		for (const auto &cut : cuts)
			for (auto p : {cut.a, cut.b})
				frontHalf &= cutPoint(mesh, cut, p).x <= 32.0001;
		expect(frontHalf, "only the front half of the active surface is cut");
		options.hiddenFaces[1] = {0, 1};
		expect(planModelKnife(mesh, 0, 0, {}, options, &cuts, &error) && std::abs(plannedLength(mesh, cuts) - 64) < 1e-4,
			   "hidden surfaces do not occlude", error);
		options.hiddenFaces.clear();
		options.through = true;
		expect(planModelKnife(mesh, 0, 0, {}, options, &cuts, &error) && std::abs(plannedLength(mesh, cuts) - 64) < 1e-4,
			   "cut through ignores occluders on other surfaces", error);
		auto edit = stroke;
		edit.tool.knife = options;
		const QJsonValue before = editableModelJson(mesh)["surfaces"].toArray()[1];
		expect(applyModelEdit(&mesh, edit, nullptr, &error) && areaPreserved(mesh) &&
				   editableModelJson(mesh)["surfaces"].toArray()[1] == before,
			   "orthographic and perspective cuts preserve the other surface", error);
	}
	{
		auto mesh = source;
		auto cover = source.surfaces[0];
		for (auto &p : cover.frames[0].positions)
		{
			p.x = 20 + p.x * 20 / 64;
			p.z = 8;
		}
		mesh.surfaces.append(cover);
		QVector<ModelKnifeCut> cuts;
		expect(planModelKnife(mesh, 0, 0, {}, stroke.tool.knife, &cuts, &error) && std::abs(plannedLength(mesh, cuts) - 44) < 1e-4,
			   "occluder leaves two separated visible spans", error);
		cover = source.surfaces[0];
		for (auto &p : cover.frames[0].positions)
			p.z = 16 - p.x;
		mesh.surfaces[1] = cover;
		for (auto &p : cover.frames[0].positions)
			p.z = p.x - 32;
		mesh.surfaces.append(cover);
		expect(planModelKnife(mesh, 0, 0, {}, stroke.tool.knife, &cuts, &error) && std::abs(plannedLength(mesh, cuts) - 16) < 1e-4,
			   "multiple crossing depth envelopes preserve a narrow visible interval", error);
	}
	{
		auto mesh = source;
		auto &s = mesh.surfaces[0];
		s.vertexCount = 3;
		s.texCoords.resize(3);
		s.triangles = {{0, 1, 2}};
		for (auto &f : s.frames)
		{
			f.positions = {{-2, -2, -2}, {2, -2, -2}, {0, 2, 1}};
			f.normals = modelFaceNormals(s, f.positions);
		}
		updateEditableModelMetadata(&mesh);
		auto edit = knife({{{-.5, -.5}, {.5, -.5}}});
		edit.tool.knife.perspective = true;
		expect(applyModelEdit(&mesh, edit, nullptr, &error), "perspective clips a triangle crossing the camera near plane", error);
	}
	// Provenance covers every child face, including hidden boundary neighbours.
	for (bool hidden : {false, true})
	{
		auto mesh = source;
		auto edit = stroke;
		edit.tool.knife.selectedOnly = true;
		edit.selection.faces = {0};
		if (hidden)
			edit.tool.knife.hiddenFaces[0] = {1};
		ModelEditReport report;
		expect(applyModelEdit(&mesh, edit, &selection, &error, {}, &report), "selected face cut with optional hidden neighbour", error);
		const auto &s = mesh.surfaces[0];
		const auto mapping = report.sourceFaces.value(0);
		expect(mapping.size() == s.triangles.size() && mapping.count(0) > 1 && mapping.count(1) == 2 && areaPreserved(mesh),
			   "neighbour subdivisions are conforming and retain exact face provenance");
		bool selectedOnly = true;
		for (auto edge : selection.edges)
			for (int v : {edge.first, edge.second})
				selectedOnly &= s.frames[0].positions[v].x >= 15.9999;
		expect(selectedOnly, "boundary propagation does not add neighbour path edges to selection");
	}
	// Explicit seam marks and independently authored corner normals survive.
	const auto cube = bevelCube();
	const auto cubeEdit = knife({{{-80, 3}, {80, 3}}}, true);
	auto changed = cube;
	for (auto edge : bevelCubeEdges(cube))
		changed.surfaces[0].uvSeams.insert(edge);
	const auto seamsBefore = changed.surfaces[0].uvSeams.size();
	const auto seamSource = changed;
	expect(applyModelEdit(&changed, cubeEdit, &selection, &error), "knife preserves explicit UV seam markings", error);
	expect(changed.surfaces[0].uvSeams.size() > seamsBefore && closedBevel(changed), "seam marks split into closed boundary chains");
	bool originalCorners = true;
	for (int i = 0; i < cube.surfaces[0].vertexCount; ++i)
		for (int f = 0; f < 2; ++f)
			originalCorners &= near(changed.surfaces[0].frames[f].positions[i], cube.surfaces[0].frames[f].positions[i], 1e-8) &&
							   near(changed.surfaces[0].frames[f].normals[i], cube.surfaces[0].frames[f].normals[i], 1e-8);
	expect(originalCorners, "original corners and hard normals remain exact");
	auto reversed = cube;
	for (auto &t : reversed.surfaces[0].triangles)
		std::swap(t.b, t.c);
	expect(applyModelEdit(&reversed, cubeEdit, nullptr, &error) && closedBevel(reversed), "reversed winding stays consistent", error);
	// Seeded adversarial paths exercise constraints with interior endpoints.
	std::mt19937 random(31841);
	for (int trial = 0; trial < 24; ++trial)
	{
		auto mesh = source;
		QVector<ModelKnifeSegment> segments;
		for (int i = 0; i < 10; ++i)
		{
			const auto coordinate = [&] { return 1. + double(random() % 62000) / 1000; };
			segments.append(ModelKnifeSegment{{coordinate(), coordinate()}, {coordinate(), coordinate()}});
		}
		auto edit = knife(segments);
		if (expect(applyModelEdit(&mesh, edit, nullptr, &error), "seeded intersecting path triangulates",
				   QString::number(trial) + ": " + error))
		{
			expect(areaPreserved(mesh), "seeded path has no missing or reversed triangle area");
			auto repeat = source;
			std::reverse(edit.tool.knife.segments.begin(), edit.tool.knife.segments.end());
			expect(applyModelEdit(&repeat, edit, nullptr, &error) && bevelBytes(repeat) == bevelBytes(mesh),
				   "path ordering is deterministic", error);
		}
	}
	// Invalid and cancelled edits publish neither geometry, selection nor provenance.
	QList<ModelEdit> invalid;
	auto bad = stroke;
	bad.tool.knife.segments.clear();
	invalid << bad;
	bad = stroke;
	bad.tool.knife.segments.fill(stroke.tool.knife.segments[0], 257);
	invalid << bad;
	bad = stroke;
	bad.tool.knife.segments = {{{2, 2}, {2, 2}}};
	invalid << bad;
	bad = stroke;
	bad.tool.knife.segments[0].a[0] = std::numeric_limits<double>::infinity();
	invalid << bad;
	bad = stroke;
	bad.tool.knife.segments[0].a[0] = 1e10;
	invalid << bad;
	bad = stroke;
	bad.tool.knife.up = {1, 0, 0};
	invalid << bad;
	bad = stroke;
	bad.tool.knife.right = {2, 0, 0};
	invalid << bad;
	bad = stroke;
	bad.tool.knife.origin[0] = 1e7;
	invalid << bad;
	bad = stroke;
	bad.tool.knife.hiddenFaces[0] = {2};
	invalid << bad;
	bad = stroke;
	bad.tool.knife.hiddenFaces[3] = {0};
	invalid << bad;
	bad = stroke;
	bad.tool.knife.selectedOnly = true;
	invalid << bad;
	bad = stroke;
	bad.tool.referenceFrame = 2;
	invalid << bad;
	bad = stroke;
	bad.frame = 0;
	invalid << bad;
	bad = stroke;
	bad.kind = ModelEditKind::Subdivide;
	invalid << bad;
	const auto original = bevelBytes(source);
	for (const auto &edit : invalid)
	{
		auto mesh = source;
		ModelSelection selected{0, {0}, {}};
		const auto before = selected;
		ModelEditReport report;
		report.sourceFaces[9] = {42};
		expect(!applyModelEdit(&mesh, edit, &selected, &error, {}, &report) && !error.isEmpty() && bevelBytes(mesh) == original &&
				   selected == before && report.sourceFaces == QHash<int, QVector<int>>{{9, {42}}},
			   "invalid knife refuses atomically with caller outputs intact");
	}
	QVector<ModelKnifeSegment> denseSegments;
	for (int i = 1; i < 16; ++i)
	{
		denseSegments.append(ModelKnifeSegment{{-1, double(i * 4)}, {65, double(i * 4)}});
		denseSegments.append(ModelKnifeSegment{{double(i * 4), -1}, {double(i * 4), 65}});
	}
	for (int stage : {0, 5, 20, 80})
	{
		auto mesh = source;
		int callbacks = 0;
		ModelWorkControl control;
		control.cancelled = [&] { return callbacks >= stage; };
		control.progress = [&](ModelWorkPhase phase, qint64, qint64)
		{
			if (phase == ModelWorkPhase::Editing)
				++callbacks;
		};
		ModelEditReport report;
		report.sourceFaces[9] = {42};
		expect(!applyModelEdit(&mesh, knife(denseSegments), nullptr, &error, control, &report) && bevelBytes(mesh) == original &&
				   report.sourceFaces.contains(9),
			   "cancellation during planning and triangulation preserves the whole transaction");
	}
	QElapsedTimer timer;
	timer.start();
	auto dense = source;
	expect(applyModelEdit(&dense, knife(denseSegments), nullptr, &error) && areaPreserved(dense),
		   "dense crossing grid finishes with exact area", error);
	std::cout << "30 crossing segments, two poses: " << timer.elapsed() << " ms; " << dense.vertexCount << " vertices\n";
	ModelDocument document;
	ModelEditReport report;
	expect(document.setMesh(seamSource, &error) && document.edit(cubeEdit, &error, {}, &report), "document knife transaction", error);
	const auto bytes = bevelBytes(changed);
	expect(bevelBytes(document.mesh()) == bytes && document.selection().edges == selection.edges, "document and direct edit agree");
	expect(document.undo() && bevelBytes(document.mesh()) == bevelBytes(seamSource) && document.redo() &&
			   bevelBytes(document.mesh()) == bytes,
		   "undo and redo preserve all poses, seams and cut-edge selection");
	const auto recovery = writeModelRecovery({changed, selection}, temp.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ModelRecoverySnapshot restored;
	expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() && bevelBytes(restored.mesh) == bytes &&
			   restored.selection == selection,
		   "recovery restores knife data and selection");
	for (const auto &format : QStringList{"mdl", "md2", "md3"})
	{
		auto native = changed;
		native.surfaces[0].skinPaths = {"models/knife/skin.pcx"};
		if (format == "mdl")
		{
			const auto fixture = decodeModelMesh("synthetic.mdl", groupedMdlFixture().bytes);
			native.mdl = fixture.mdl;
			native.mdl.frameGroups = {{0, {.1f, .2f}}};
			native.embeddedSkins = fixture.embeddedSkins;
			updateEditableModelMetadata(&native);
		}
		const auto data = exportEditableModel(native, format, 0, &error);
		const auto decoded = decodeModelMesh("knife." + format, data);
		expect(!data.isEmpty() && decoded.isValid() && decoded.frames.size() == 2 &&
				   decoded.surfaces[0].triangles.size() == changed.surfaces[0].triangles.size(),
			   "native export retains knife faces and poses", error);
	}
	auto skinned = skinnedCube();
	expect(applyModelEdit(&skinned, cubeEdit, nullptr, &error) && closedBevel(skinned), "knife interpolates skin influences", error);
	bool mixed = false, reproduced = true;
	const auto &skin = skinned.surfaces[0];
	for (int frame = 0; frame < skinned.frames.size(); ++frame)
	{
		ModelFrameGeometry baked;
		const auto pose = frame == 0 ? modelSkeletonBindPose(skinned.skeleton) : skinned.skeleton.clips[0].frames[frame - 1];
		reproduced &= skinModelSurface(skin, pose, &baked, &error);
		for (int v = 0; v < skin.vertexCount; ++v)
		{
			mixed |= skin.skinning.count[v] > 1;
			reproduced &= near(baked.positions[v], skin.frames[frame].positions[v]);
		}
	}
	expect(mixed && reproduced, "two-joint rebake reproduces every new vertex in every retained pose");
	for (const auto &format : QStringList{"md5mesh", "iqm"})
	{
		const auto data = exportEditableModel(skinned, format, 0, &error);
		const auto decoded = decodeModelMesh("knife." + format, data);
		expect(!data.isEmpty() && decoded.isValid() && !decoded.skeleton.isEmpty(), "skeletal export retains cut mesh and rig", error);
	}
	auto negative = skinnedCube();
	for (auto &influence : negative.surfaces[0].skinning.influences)
		influence.weight = -1;
	const auto negativeBytes = bevelBytes(negative);
	expect(!applyModelEdit(&negative, cubeEdit, nullptr, &error) && error.contains("negative skin weights") &&
			   bevelBytes(negative) == negativeBytes,
		   "negative skin weights refuse with explicit repair guidance");
	{
		auto mesh = source;
		auto &s = mesh.surfaces[0];
		mesh.frames.resize(1);
		s.frames.resize(1);
		for (int i = 0; i < 66; ++i)
		{
			ModelJoint joint;
			joint.name = QString("joint_%1").arg(i);
			joint.bind = modelJointIdentity();
			mesh.skeleton.joints.append(joint);
		}
		for (int v = 0; v < s.vertexCount; ++v)
		{
			s.skinning.first.append(s.skinning.influences.size());
			s.skinning.count.append(33);
			for (int i = 0; i < 33; ++i)
				s.skinning.influences.append({i + (v % 2) * 33, 1.f / 33, s.frames[0].positions[v], {0, 0, 1}});
		}
		updateEditableModelMetadata(&mesh);
		const auto before = bevelBytes(mesh);
		expect(validateEditableModel(mesh).isEmpty(), "valid source with separate large influence sets");
		expect(!applyModelEdit(&mesh, stroke, nullptr, &error) && error.contains("1–64") && bevelBytes(mesh) == before,
			   "combined influences above 64 refuse without truncation");
	}
	{
		auto mesh = source;
		auto &s = mesh.surfaces[0];
		s.texCoords.resize(modelDocumentMaxVertices);
		for (auto &frame : s.frames)
		{
			frame.positions.resize(modelDocumentMaxVertices);
			frame.normals.resize(modelDocumentMaxVertices);
			for (int v = 4; v < modelDocumentMaxVertices; ++v)
			{
				frame.positions[v] = frame.positions[0];
				frame.normals[v] = {0, 0, 1};
			}
		}
		updateEditableModelMetadata(&mesh);
		const auto before = bevelBytes(mesh);
		expect(validateEditableModel(mesh).isEmpty(), "valid source at the document vertex limit");
		expect(!applyModelEdit(&mesh, stroke, nullptr, &error) && bevelBytes(mesh) == before, "knife capacity refusal is atomic");
	}
	{
		auto mesh = cube;
		mesh.tags = {{"tag_test", 0, {3, 4, 5}}, {"tag_test", 1, {6, 7, 8}}};
		mesh.animations = {{"motion", 0, 2, 12}};
		mesh.collisionBoxes = {{"body", {0, 0, 0}, {64, 64, 64}}};
		updateEditableModelMetadata(&mesh);
		const auto before = editableModelJson(mesh);
		expect(applyModelEdit(&mesh, cubeEdit, nullptr, &error), "cut authored model metadata", error);
		const auto after = editableModelJson(mesh);
		bool same = true;
		for (const auto &key : {"tags", "animations", "collisionBoxes", "mdl"})
			same &= before[key] == after[key];
		expect(same, "knife keeps attachments, animation ranges and collision tracks");
	}
	{
		ModelDocument cancelled;
		expect(cancelled.setMesh(source, &error), "prepare late cancellation document");
		bool serializing = false;
		ModelWorkControl control;
		control.cancelled = [&] { return serializing; };
		control.progress = [&](ModelWorkPhase phase, qint64, qint64) { serializing |= phase == ModelWorkPhase::Serializing; };
		ModelEditReport report;
		report.sourceFaces[9] = {42};
		expect(!cancelled.edit(stroke, &error, control, &report) && serializing && !cancelled.canUndo() &&
				   bevelBytes(cancelled.mesh()) == original && report.sourceFaces.contains(9),
			   "late document preparation cancellation publishes no report or history");
		QVector<ModelKnifeCut> plan{{9, {1, 2}, {3, 4}}};
		control.cancelled = [] { return true; };
		expect(!planModelKnife(source, 0, 0, {}, stroke.tool.knife, &plan, &error, control) && plan.size() == 1 && plan[0].face == 9,
			   "planner cancellation leaves caller output intact");
	}
	const auto sourcePath = temp.filePath("cube.mesh.json"), output = temp.filePath("cut.mesh.json");
	ModelDocument saved;
	expect(saved.setMesh(cube, &error) && saved.save(sourcePath, false, &error), "save knife CLI source");
	auto expected = cube;
	expect(applyModelEdit(&expected, cubeEdit, nullptr, &error), "prepare CLI expected output");
	const QStringList args{"vibestudio", "--cli",  "model",		 "tool",	  sourcePath, "--tool",
						   "knife",		 "--path", "-80,3;80,3", "--through", "--output", output};
	expect(cli::runModelTool(args + QStringList{"--dry-run"}).exitCode == 0 && !QFile::exists(output), "knife CLI dry-run writes nothing");
	expect(cli::runModelTool(args).exitCode == 0 && saved.load(output, &error) && bevelBytes(saved.mesh()) == bevelBytes(expected),
		   "CLI and core knife agree");
	expect(cli::runModelTool(args).exitCode != 0 && cli::runModelTool(args + QStringList{"--overwrite"}).exitCode == 0,
		   "knife CLI protects existing output");
	auto inputOutput = args;
	inputOutput[inputOutput.indexOf("--output") + 1] = sourcePath;
	expect(cli::runModelTool(inputOutput + QStringList{"--overwrite"}).exitCode != 0, "knife CLI protects authoring source");
	for (const auto &extra : QList<QStringList>{{"--path", "0,0;1,1"},
												{"--frame", "0"},
												{"--width", "2"},
												{"--vertices", "0"},
												{"--perspective"},
												{"--camera-right", "1,0,0"},
												{"--view", "bad"},
												{"--unknown"}})
		expect(cli::runModelTool(args + extra + QStringList{"--dry-run"}).exitCode != 0,
			   "knife CLI rejects repeated or incompatible flags");
	for (const auto &path : QStringList{"1,2", "1,2;1,2", "nan,0;1,2", "1e10,0;1,2", "0,0;1", "0,0;1,1|", "0,0,0;1,1"})
	{
		auto malformed = args;
		malformed[malformed.indexOf("--path") + 1] = path;
		expect(cli::runModelTool(malformed + QStringList{"--dry-run"}).exitCode != 0, "knife CLI refuses malformed paths");
	}
	const auto pathFile = temp.filePath("path.json");
	QFile file(pathFile);
	expect(file.open(QIODevice::WriteOnly) && file.write("[[[-80,3],[80,3]]]") > 0, "write path fixture");
	file.close();
	auto fileArgs = args;
	fileArgs[fileArgs.indexOf("--path") + 1] = pathFile;
	fileArgs[fileArgs.indexOf("--path")] = "--path-file";
	checkModelAuxiliaryOutput(fileArgs, "--path-file", cli::runModelTool,
							  [](bool value, const char *message) { return expect(value, message); });
	expect(cli::runModelTool(fileArgs + QStringList{"--overwrite"}).exitCode == 0 && saved.load(output, &error) &&
			   bevelBytes(saved.mesh()) == bevelBytes(expected),
		   "JSON path and inline path agree");
	expect(cli::runModelTool(args + QStringList{"--path-file", pathFile, "--dry-run"}).exitCode != 0,
		   "path mechanisms are mutually exclusive");
	for (const auto &data : QList<QByteArray>{"{}", "[]", "[[[1,2,3],[4,5]]]", "[[[\"1\",2],[4,5]]]", QByteArray(65537, ' ')})
	{
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
			return 1;
		file.write(data);
		file.close();
		expect(cli::runModelTool(fileArgs + QStringList{"--dry-run"}).exitCode != 0, "malformed or oversized JSON paths refuse");
	}
	if (argc > 1)
	{
		QProcess process;
		process.start(QString::fromLocal8Bit(argv[1]), args.mid(1) + QStringList{"--overwrite", "--json"});
		expect(process.waitForFinished(30000) && process.exitCode() == 0 &&
				   QJsonDocument::fromJson(process.readAllStandardOutput()).object()["tool"] == "knife",
			   "application dispatches knife CLI");
	}
	std::cout << checks << " knife checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
