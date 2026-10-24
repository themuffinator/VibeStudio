#include "cli/model_tools.h"
#include "core/model_intersections.h"
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
		std::cerr << "FAIL: " << message << ": " << error.toStdString() << '\n';
		++failures;
	}
	return value;
}
bool near(ModelVec3 a, ModelVec3 b, double tolerance = 1e-4)
{
	return model_geometry::length(model_geometry::point(a) - model_geometry::point(b)) < tolerance;
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
	expect(bakeModelSkeleton(&mesh, {}, &error), "bake two independently moving joints", error);
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
	QTemporaryDir temp(QDir(root).filePath("edge-bevel-XXXXXX"));
	if (!temp.isValid())
		return 1;
	const auto source = bevelCube();
	QString error;
	expect(bevelCubeEdges(source).size() == 12 && closedBevel(source), "cube fixture has twelve geometric sides");
	for (int edges : {1, 2, 3, 4, 12})
		for (int segments : {1, 3, 8, 32})
			for (double roundness : {0., .5, 1.})
			{
				auto mesh = source;
				auto edit = bevelCubeEdit(mesh, edges, segments);
				edit.tool.bevelRoundness = roundness;
				const auto label = QString("edges=%1 segments=%2 roundness=%3").arg(edges).arg(segments).arg(roundness);
				if (expect(applyModelEdit(&mesh, edit, nullptr, &error), "bevel joined cube edges in every pose", error))
				{
					expect(closedBevel(mesh), "bevel is closed, consistently wound and has Euler characteristic two", label);
					ModelIntersectionReport report;
					expect(inspectModelIntersections(mesh, {}, &report, &error) && report.findings.isEmpty(),
						   "cube bevel has no self-intersections in either pose", label);
				}
			}
	const auto original = bevelBytes(source);
	// Triangle-only fans do not depend on inferred quads.
	auto tetra = source;
	auto &ts = tetra.surfaces[0];
	ts.vertexCount = 4;
	ts.texCoords = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	ts.triangles = {{0, 1, 2}, {0, 3, 1}, {0, 2, 3}, {1, 3, 2}};
	ts.frames[0].positions = {{32, 32, 32}, {32, -32, -32}, {-32, 32, -32}, {-32, -32, 32}};
	for (auto &triangle : ts.triangles)
	{
		using namespace model_geometry;
		const auto a = point(ts.frames[0].positions[triangle.a]), b = point(ts.frames[0].positions[triangle.b]),
				   c = point(ts.frames[0].positions[triangle.c]);
		if (dot(cross(b - a, c - a), a + b + c) < 0)
			std::swap(triangle.b, triangle.c);
	}
	ts.frames[0].normals = modelFaceNormals(ts, ts.frames[0].positions);
	ts.frames[1] = ts.frames[0];
	for (auto &p : ts.frames[1].positions)
		p = {-p.y + 10, p.x, p.z};
	ts.frames[1].normals = modelFaceNormals(ts, ts.frames[1].positions);
	updateEditableModelMetadata(&tetra);
	for (int edgeCount : {1, 6})
		for (int segments : {1, 4})
		{
			auto candidate = tetra;
			expect(applyModelEdit(&candidate, bevelCubeEdit(tetra, edgeCount, segments), nullptr, &error) && closedBevel(candidate),
				   "triangle fans bevel as a closed manifold", error);
		}
	const auto edit = bevelCubeEdit(source, 12, 4);
	ModelDocument document;
	expect(document.setMesh(source, &error) && document.edit(edit, &error), "transaction commits complete edge bevel", error);
	const auto changed = document.mesh();
	const auto bytes = bevelBytes(changed);
	const auto selected = document.selection();
	expect(!selected.faces.isEmpty() && selected.edges.isEmpty() && selected.vertices.isEmpty(),
		   "new bands and caps become face selection");
	expect(document.undo() && bevelBytes(document.mesh()) == original && document.redo() && bevelBytes(document.mesh()) == bytes,
		   "undo and redo retain geometry, all poses, UVs and seam marks");
	bool correspondence = true, insetUv = false;
	const auto &surface = changed.surfaces[0];
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		const auto p = surface.frames[0].positions[v];
		correspondence &= near(surface.frames[1].positions[v], {-p.y * 1.5f + 10, p.x * 2, p.z * .75f + 3});
		insetUv |= std::abs(surface.texCoords[v].u - .125) < 1e-5 || std::abs(surface.texCoords[v].v - .125) < 1e-5;
	}
	expect(correspondence, "every new vertex follows identical affine provenance through rotated and scaled poses");
	expect(insetUv && !surface.uvSeams.isEmpty(), "retained face UVs interpolate while band and cap seams remain explicit");
	// A single cube edge has two rails exactly eight units from the old edge.
	auto single = source;
	auto one = bevelCubeEdit(source);
	expect(applyModelEdit(&single, one, nullptr, &error), "single chamfer for analytic width check", error);
	const auto edge = *one.selection.edges.cbegin();
	const auto a = model_geometry::point(source.surfaces[0].frames[0].positions[edge.first]);
	const auto b = model_geometry::point(source.surfaces[0].frames[0].positions[edge.second]);
	const auto axis = (b - a) * (1 / model_geometry::length(b - a));
	int railPoints = 0;
	for (auto p : single.surfaces[0].frames[0].positions)
	{
		const auto delta = model_geometry::point(p) - a;
		const double distance = model_geometry::length(delta - axis * model_geometry::dot(delta, axis));
		railPoints += std::abs(distance - 8) < 1e-5;
	}
	expect(railPoints >= 4, "single-edge offset equals requested model units");
	// Selecting an existing diagonal deliberately keeps its triangles separate.
	ModelQuadTopology originalTopology;
	buildModelQuadTopology(source.surfaces[0], 0, 40, &originalTopology);
	auto diagonalEdit = one;
	const auto &quad = originalTopology.elements.first();
	diagonalEdit.selection.edges = {modelEdge(quad.corners[0], quad.corners[2])};
	auto diagonal = source;
	expect(applyModelEdit(&diagonal, diagonalEdit, nullptr, &error) && closedBevel(diagonal),
		   "explicit diagonal survives quad pairing and bevels as a planar edge", error);
	// An open wedge keeps its original opening; the new strip ends are borders.
	auto wedge = source;
	const auto geometric = originalTopology.geometry.geometricEdge(edge.first, edge.second);
	QVector<ModelTriangle> wedgeFaces;
	for (auto use : originalTopology.sideUses.value(geometric))
		for (int face : originalTopology.elements[use.first].faces)
			if (face >= 0)
				wedgeFaces.append(source.surfaces[0].triangles[face]);
	wedge.surfaces[0].triangles = wedgeFaces;
	updateEditableModelMetadata(&wedge);
	auto wedgeEdit = one;
	wedgeEdit.tool.bevelSegments = 5;
	expect(applyModelEdit(&wedge, wedgeEdit, nullptr, &error), "open-ended wedge accepts an interior edge", error);
	ModelGeometricTopology wedgeTopology;
	expect(buildModelGeometricTopology(wedge.surfaces[0], &wedgeTopology) && !closedBevel(wedge), "wedge retains open boundaries");
	auto reversed = source;
	for (auto &t : reversed.surfaces[0].triangles)
		std::swap(t.b, t.c);
	expect(applyModelEdit(&reversed, edit, nullptr, &error) && closedBevel(reversed), "consistently reversed winding bevels consistently",
		   error);
	// Metadata and surfaces outside the active authoring context are untouched.
	auto authored = source;
	authored.surfaces.append(source.surfaces[0]);
	authored.surfaces[1].name = "untouched";
	authored.tags = {{"tag_test", 0, {3, 4, 5}}, {"tag_test", 1, {6, 7, 8}}};
	authored.animations = {{"motion", 0, 2, 12}};
	authored.collisionBoxes = {{"body", {0, 0, 0}, {64, 64, 64}}};
	updateEditableModelMetadata(&authored);
	const auto authoredBefore = editableModelJson(authored);
	expect(applyModelEdit(&authored, edit, nullptr, &error), "bevel authored model context", error);
	const auto authoredAfter = editableModelJson(authored);
	bool metadata = authoredBefore["surfaces"].toArray()[1] == authoredAfter["surfaces"].toArray()[1];
	for (const auto &key : {"tags", "animations", "collisionBoxes", "mdl"})
		metadata &= authoredBefore[key] == authoredAfter[key];
	expect(metadata, "unrelated surface, tags, animation ranges and collision volumes survive exactly");
	// Admission, capacity and late failures all preserve the original candidate.
	QList<ModelEdit> invalid;
	for (double width : {-1., 32., 64., std::numeric_limits<double>::infinity()})
	{
		auto bad = edit;
		bad.tool.bevelWidth = width;
		invalid << bad;
	}
	for (int segments : {0, 33, 1000000})
	{
		auto bad = edit;
		bad.tool.bevelSegments = segments;
		invalid << bad;
	}
	for (double roundness : {-1., 1.01, std::numeric_limits<double>::quiet_NaN()})
	{
		auto bad = edit;
		bad.tool.bevelRoundness = roundness;
		invalid << bad;
	}
	auto bad = edit;
	bad.frame = 0;
	invalid << bad;
	bad = edit;
	bad.tool.referenceFrame = 2;
	invalid << bad;
	bad = edit;
	bad.selection.edges.clear();
	invalid << bad;
	bad = edit;
	bad.selection.edges = {modelEdge(0, 23)};
	invalid << bad;
	bad = edit;
	bad.kind = ModelEditKind::BevelVertices;
	invalid << bad;
	for (const auto &op : invalid)
	{
		auto candidate = source;
		ModelSelection selection;
		selection.vertices = {0};
		const auto saved = selection;
		expect(!applyModelEdit(&candidate, op, &selection, &error) && !error.isEmpty() && bevelBytes(candidate) == original &&
				   selection == saved,
			   "invalid edit refuses atomically including caller selection");
	}
	auto nonmanifold = source;
	nonmanifold.surfaces[0].triangles.append(nonmanifold.surfaces[0].triangles.first());
	updateEditableModelMetadata(&nonmanifold);
	const auto nonmanifoldBytes = bevelBytes(nonmanifold);
	expect(!applyModelEdit(&nonmanifold, edit, nullptr, &error) && bevelBytes(nonmanifold) == nonmanifoldBytes,
		   "nonmanifold endpoint refuses without publication");
	auto folded = source;
	const int foldedA = originalTopology.geometry.group(edge.first), foldedB = originalTopology.geometry.group(edge.second);
	const auto positionA = folded.surfaces[0].frames[1].positions[foldedA], positionB = folded.surfaces[0].frames[1].positions[foldedB];
	for (int v : originalTopology.geometry.copies[foldedA])
		folded.surfaces[0].frames[1].positions[v] = positionB;
	for (int v : originalTopology.geometry.copies[foldedB])
		folded.surfaces[0].frames[1].positions[v] = positionA;
	updateEditableModelMetadata(&folded);
	const auto foldedBytes = bevelBytes(folded);
	expect(!applyModelEdit(&folded, edit, nullptr, &error) && bevelBytes(folded) == foldedBytes, "later-pose fold refuses entire edit");
	auto zero = edit;
	zero.tool.bevelWidth = 0;
	ModelDocument unchanged;
	expect(unchanged.setMesh(source, &error) && unchanged.edit(zero, &error) && !unchanged.canUndo() && !unchanged.isModified(),
		   "zero width creates no history");
	for (int stage : {0, 5, 20})
	{
		auto candidate = source;
		int callbacks = 0;
		ModelWorkControl cancel;
		cancel.cancelled = [&] { return callbacks >= stage; };
		cancel.progress = [&](ModelWorkPhase phase, qint64, qint64)
		{
			if (phase == ModelWorkPhase::Editing)
				++callbacks;
		};
		expect(!applyModelEdit(&candidate, bevelCubeEdit(source, 12, 32), nullptr, &error, cancel) && bevelBytes(candidate) == original,
			   "early and mid-operation cancellation never publishes partial topology");
	}
	auto deterministic = source;
	auto orderedEdit = edit;
	QList<ModelEdge> order(orderedEdit.selection.edges.cbegin(), orderedEdit.selection.edges.cend());
	std::sort(order.begin(), order.end(), std::greater<ModelEdge>());
	orderedEdit.selection.edges.clear();
	for (auto e : order)
		orderedEdit.selection.edges.insert(e);
	expect(applyModelEdit(&deterministic, orderedEdit, nullptr, &error) && bevelBytes(deterministic) == bytes,
		   "selection insertion order cannot change bevel output");
	const auto recovery = writeModelRecovery({changed, selected}, temp.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ModelRecoverySnapshot restored;
	expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() && bevelBytes(restored.mesh) == bytes &&
			   restored.selection == selected,
		   "recovery restores all bevel poses, attributes and new-face selection");
	for (const auto &format : QStringList{"mdl", "md2", "md3"})
	{
		auto native = changed;
		native.surfaces[0].skinPaths = {"models/bevel/skin.pcx"};
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
		for (int face = 0; face < native.surfaces[0].triangles.size(); ++face)
			pack.selection.faces.insert(face);
		expect(applyModelEdit(&native, pack, nullptr, &error), "pack new UV islands into target skin tile", error);
		const auto data = exportEditableModel(native, format, 0, &error);
		const auto decoded = decodeModelMesh("bevel." + format, data);
		expect(!data.isEmpty() && decoded.isValid() && decoded.frames.size() == 2 &&
				   decoded.surfaces[0].triangles.size() == surface.triangles.size(),
			   "native export retains bevel faces and sampled poses", error);
	}
	auto skinned = skinnedCube();
	const auto skinEdit = bevelCubeEdit(skinned, 12, 4);
	expect(applyModelEdit(&skinned, skinEdit, nullptr, &error) && closedBevel(skinned), "bevel interpolates multi-joint skinning", error);
	const auto &skin = skinned.surfaces[0];
	bool mixed = false, skinMatches = true, unitNormals = true;
	for (int frame = 0; frame < skinned.frames.size(); ++frame)
	{
		ModelFrameGeometry baked;
		const auto pose = frame == 0 ? modelSkeletonBindPose(skinned.skeleton) : skinned.skeleton.clips[0].frames[frame - 1];
		skinMatches &= skinModelSurface(skin, pose, &baked, &error);
		for (int v = 0; v < skin.vertexCount; ++v)
		{
			mixed |= skin.skinning.count[v] > 1;
			skinMatches &= near(baked.positions[v], skin.frames[frame].positions[v]);
			unitNormals &= std::abs(model_geometry::length(model_geometry::point(baked.normals[v])) - 1) < 1e-4;
		}
	}
	expect(mixed && skinMatches && unitNormals, "new vertices mix actual influences and reproduce both retained poses with usable normals");
	for (const auto &format : QStringList{"md5mesh", "iqm"})
	{
		const auto data = exportEditableModel(skinned, format, 0, &error);
		const auto decoded = decodeModelMesh("bevel." + format, data);
		expect(!data.isEmpty() && decoded.isValid() && !decoded.skeleton.isEmpty(), "skeletal export retains bevel and rig", error);
	}
	auto negative = skinnedCube();
	negative.surfaces[0].skinning.influences[0].weight = -1;
	const auto negativeBytes = bevelBytes(negative);
	expect(!applyModelEdit(&negative, skinEdit, nullptr, &error) && error.contains("negative skin weights") &&
			   bevelBytes(negative) == negativeBytes,
		   "negative source influences refuse explicitly and atomically");
	// Dense authoring fixture: many disconnected pieces in a single surface.
	auto dense = source;
	auto &ds = dense.surfaces[0];
	const auto seed = source.surfaces[0];
	auto denseEdit = edit;
	for (int copy = 1; copy < 128; ++copy)
	{
		const int base = ds.vertexCount;
		for (auto t : seed.triangles)
			ds.triangles.append({t.a + base, t.b + base, t.c + base});
		ds.texCoords += seed.texCoords;
		for (int frame = 0; frame < 2; ++frame)
		{
			for (auto p : seed.frames[frame].positions)
			{
				if (frame == 0)
					p.x += copy * 100;
				else
					p.y += copy * 200;
				ds.frames[frame].positions.append(p);
			}
			ds.frames[frame].normals += seed.frames[frame].normals;
		}
		for (auto e : edit.selection.edges)
			denseEdit.selection.edges.insert(modelEdge(e.first + base, e.second + base));
		ds.vertexCount += seed.vertexCount;
	}
	updateEditableModelMetadata(&dense);
	const auto denseBytes = bevelBytes(dense);
	int checkpoints = 0;
	ModelWorkControl cancelDense;
	cancelDense.cancelled = [&] { return checkpoints > 100; };
	cancelDense.progress = [&](ModelWorkPhase phase, qint64, qint64)
	{
		if (phase == ModelWorkPhase::Editing)
			++checkpoints;
	};
	expect(!applyModelEdit(&dense, denseEdit, nullptr, &error, cancelDense) && checkpoints > 100 && bevelBytes(dense) == denseBytes,
		   "dense multi-component cancellation leaves all components unchanged");
	QElapsedTimer timer;
	timer.start();
	expect(applyModelEdit(&dense, denseEdit, nullptr, &error), "bevel 128 disconnected cubes in one two-pose surface", error);
	std::cout << "128 cubes, 3,072 input vertices, 1,536 edges, two poses: " << timer.elapsed() << " ms; " << dense.vertexCount
			  << " result vertices\n";
	const auto path = temp.filePath("cube.mesh.json"), output = temp.filePath("bevel.mesh.json");
	ModelDocument saved;
	expect(saved.setMesh(source, &error) && saved.save(path, false, &error), "save source for CLI parity");
	QStringList selectors;
	for (auto e : order)
		selectors << QString("%1:%2").arg(e.first).arg(e.second);
	const QStringList args{
		"vibestudio", "--cli", "model",		 "tool", path,			"--tool", "bevel-edges", "--edges", selectors.join(','),
		"--width",	  "8",	   "--segments", "4",	 "--roundness", "1",	  "--output",	 output};
	expect(cli::runModelTool(args + QStringList{"--dry-run"}).exitCode == 0 && !QFile::exists(output), "CLI dry-run writes nothing");
	expect(cli::runModelTool(args).exitCode == 0 && saved.load(output, &error) && bevelBytes(saved.mesh()) == bytes,
		   "CLI and document bevel output are identical");
	expect(cli::runModelTool(args).exitCode != 0 && cli::runModelTool(args + QStringList{"--overwrite"}).exitCode == 0,
		   "CLI protects existing output");
	for (const auto &extra :
		 QList<QStringList>{{"--width", "2"}, {"--vertices", "0"}, {"--frame", "0"}, {"--slide", ".2"}, {"--unknown", "1"}})
		expect(cli::runModelTool(args + extra).exitCode != 0, "CLI rejects repeated and inapplicable flags");
	for (const auto &pair : QList<QPair<QString, QString>>{{"--segments", "0"},
														   {"--segments", "33"},
														   {"--segments", "2.2"},
														   {"--roundness", "nan"},
														   {"--roundness", "1.1"},
														   {"--width", "-1"}})
	{
		auto invalidArgs = args;
		invalidArgs[invalidArgs.indexOf(pair.first) + 1] = pair.second;
		expect(cli::runModelTool(invalidArgs + QStringList{"--dry-run"}).exitCode != 0, "CLI rejects malformed numeric values");
	}
	if (argc > 1)
	{
		QProcess process;
		process.start(QString::fromLocal8Bit(argv[1]), args.mid(1) + QStringList{"--overwrite", "--json"});
		expect(process.waitForFinished(30000) && process.exitCode() == 0 &&
				   QJsonDocument::fromJson(process.readAllStandardOutput()).object()["tool"] == "bevel-edges",
			   "application dispatches bevel CLI");
	}
	std::cout << checks << " edge bevel checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
