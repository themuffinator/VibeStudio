#include "core/model_fingerprint.h"
#include "core/model_vertex_identity.h"
#include "tests/model_animation_curves_test_helpers.h"
#include "tests/model_edge_bevel_test_helpers.h"
#include "tests/model_rip_test_helpers.h"
#include "tests/model_spin_test_helpers.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <iostream>
#include <numeric>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool ok, const QString &message, const QString &error = {})
{
	++checks;
	if (!ok)
	{
		++failures;
		std::cerr << "FAIL: " << message.toStdString() << ": " << error.toStdString() << '\n';
	}
	return ok;
}
QSet<int> faces(const ModelMesh &mesh)
{
	QSet<int> result;
	for (int i = 0; i < mesh.surfaces[0].triangles.size(); ++i)
		result.insert(i);
	return result;
}
ModelEdit faceEdit(ModelEditKind kind, const ModelMesh &mesh)
{
	ModelEdit edit;
	edit.kind = kind;
	edit.selection.faces = faces(mesh);
	return edit;
}
ModelMesh quadSeams()
{
	auto mesh = ripGrid();
	auto &surface = mesh.surfaces[0];
	const auto source = surface;
	surface.texCoords.clear();
	surface.triangles.clear();
	surface.uvSeams.clear();
	for (auto &pose : surface.frames)
	{
		pose.positions.clear();
		pose.normals.clear();
	}
	for (int first = 0; first < source.triangles.size(); first += 2)
	{
		QHash<int, int> copies;
		for (int f = first; f < first + 2; ++f)
		{
			auto t = source.triangles[f];
			for (int *v : {&t.a, &t.b, &t.c})
			{
				if (!copies.contains(*v))
				{
					copies.insert(*v, surface.texCoords.size());
					auto uv = source.texCoords[*v];
					uv.u += first;
					surface.texCoords.append(uv);
					for (int p = 0; p < surface.frames.size(); ++p)
					{
						surface.frames[p].positions.append(source.frames[p].positions[*v]);
						surface.frames[p].normals.append(source.frames[p].normals[*v]);
					}
				}
				*v = copies.value(*v);
			}
			surface.triangles.append(t);
		}
	}
	surface.vertexCount = surface.texCoords.size();
	updateEditableModelMetadata(&mesh);
	return mesh;
}
QVector<int> groups(const ModelSurface &surface)
{
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology))
		return {};
	return topology.vertexGroup;
}
int components(const ModelSurface &surface)
{
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology))
		return -1;
	QSet<int> visited;
	int count = 0;
	for (int seed = 0; seed < surface.triangles.size(); ++seed)
	{
		if (visited.contains(seed))
			continue;
		++count;
		QVector<int> queue{seed};
		visited.insert(seed);
		for (int i = 0; i < queue.size(); ++i)
		{
			const auto t = surface.triangles[queue[i]];
			for (int v : {t.a, t.b, t.c})
				for (int other : topology.groupFaces[topology.group(v)])
					if (!visited.contains(other))
					{
						visited.insert(other);
						queue.append(other);
					}
		}
	}
	return count;
}
// Compare authoring operations on a legacy surface and its explicit equivalent,
// then repeat with an untouched, coincident island in the same surface.
void operation(const QString &name, const ModelMesh &source, const ModelEdit &edit, bool isolated = true)
{
	QString error;
	auto legacy = source, explicitMesh = source;
	expect(materialiseModelVertexIdentities(&explicitMesh.surfaces[0], &error), name + " materialise", error);
	if (!expect(applyModelEdit(&legacy, edit, nullptr, &error), name + " legacy", error) ||
		!expect(applyModelEdit(&explicitMesh, edit, nullptr, &error), name + " explicit", error))
		return;
	expect(validateEditableModel(explicitMesh).isEmpty(), name + " valid identities", validateEditableModel(explicitMesh).join("; "));
	expect(groups(legacy.surfaces[0]) == groups(explicitMesh.surfaces[0]), name + " keeps UV/normal connectivity");
	for (auto &s : legacy.surfaces)
		s.topologyVertices.clear();
	for (auto &s : explicitMesh.surfaces)
		s.topologyVertices.clear();
	expect(ripBytes(legacy) == ripBytes(explicitMesh), name + " retains geometry, poses and attributes");
	if (!isolated)
		return;
	auto twins = source;
	auto duplicate = faceEdit(ModelEditKind::DuplicateFaces, source);
	if (!expect(applyModelEdit(&twins, duplicate, nullptr, &error) && components(twins.surfaces[0]) == 2, name + " coincident fixture",
				error))
		return;
	if (!expect(applyModelEdit(&twins, edit, nullptr, &error), name + " edit coincident fixture", error))
		return;
	expect(validateEditableModel(twins).isEmpty() && components(twins.surfaces[0]) >= 2, name + " retains detached coincident island",
		   validateEditableModel(twins).join("; "));
	ModelMesh decoded;
	expect(parseEditableModel(ripBytes(twins), &decoded, &error) && groups(decoded.surfaces[0]) == groups(twins.surfaces[0]),
		   name + " source round trip", error);
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	const auto source = ripSeams();
	auto mesh = source;
	const auto inferred = groups(mesh.surfaces[0]);
	expect(materialiseModelVertexIdentities(&mesh.surfaces[0], &error) && groups(mesh.surfaces[0]) == inferred,
		   "materialisation preserves legacy all-pose seam adjacency", error);
	const auto explicitBytes = ripBytes(mesh);
	expect(editableModelJson(mesh).value("version").toInt() == 11 && editableModelJson(source).value("version").toInt() == 3,
		   "only explicit authoring topology upgrades the source version");
	ModelMesh reloaded;
	expect(parseEditableModel(explicitBytes, &reloaded, &error) && ripBytes(reloaded) == explicitBytes, "unskinned version 11 round trip",
		   error);
	expect(modelStateFingerprint(mesh) != modelStateFingerprint(source), "connectivity participates in revision identity");
	for (int invalid = 0; invalid < 6; ++invalid)
	{
		auto object = editableModelJson(mesh);
		auto surfaces = object.value("surfaces").toArray();
		auto surface = surfaces[0].toObject();
		auto ids = surface.value("topologyVertices").toArray();
		if (invalid == 0)
			ids.removeLast();
		if (invalid == 1)
			ids[0] = -1;
		if (invalid == 2)
			ids[0] = mesh.surfaces[0].vertexCount;
		if (invalid == 3)
			ids[0] = .5;
		if (invalid == 4)
			ids[0] = "0";
		surface.insert("topologyVertices", ids);
		surfaces[0] = surface;
		object.insert("surfaces", surfaces);
		if (invalid == 5)
			object.insert("version", 10);
		ModelMesh invalidMesh;
		expect(!parseEditableModel(QJsonDocument(object).toJson(), &invalidMesh, &error), "malformed identity metadata refused");
	}
	for (bool seamed : {false, true})
		for (bool rigged : {false, true})
		{
			if (seamed && rigged)
				continue;
			auto input = rigged ? ripRig() : seamed ? ripSeams() : ripGrid();
			auto split = ripEdit(ripGrid());
			if (seamed)
				split.selection.vertices = {ripVertex(input, 0, 0)};
			split.translation = {};
			const int original = *split.selection.vertices.cbegin();
			ModelDocument document;
			if (!expect(document.setMesh(input, &error) && document.edit(split, &error), "zero-distance rip", error))
				continue;
			const auto accepted = ripBytes(document.mesh());
			const auto topology = groups(document.mesh().surfaces[0]);
			QSet<int> selectedGroups;
			for (int v : document.selection().vertices)
				selectedGroups.insert(topology[v]);
			expect(selectedGroups.size() == 1, "all selected UV copies retain the moving side's identity");
			ModelGeometricTopology after;
			buildModelGeometricTopology(document.mesh().surfaces[0], &after);
			int coincidentGroups = 0;
			const auto p = input.surfaces[0].frames[0].positions[original];
			for (int v = 0; v < after.copies.size(); ++v)
			{
				const auto q = document.mesh().surfaces[0].frames[0].positions[v];
				if (!after.copies[v].isEmpty() && p.x == q.x && p.y == q.y && p.z == q.z)
					++coincidentGroups;
			}
			expect(coincidentGroups == 2, "exactly two geometric vertices at the ripped point");
			expect(document.undo() && ripBytes(document.mesh()) == ripBytes(input) && document.redo() &&
					   ripBytes(document.mesh()) == accepted,
				   "undo and redo preserve explicit connectivity");
			ModelMesh decoded;
			expect(parseEditableModel(accepted, &decoded, &error) && groups(decoded.surfaces[0]) == topology,
				   "split survives source reload, including skinning", error);
			if (!rigged)
			{
				ModelEdit move;
				move.kind = ModelEditKind::Transform;
				move.selection = document.selection();
				move.translation = {2, 0, 0};
				expect(document.edit(move, &error), "move the detached side", error);
				move.translation = {-2, 0, 0};
				expect(document.edit(move, &error) && groups(document.mesh().surfaces[0]) == topology,
					   "returning to coincidence does not reconnect", error);
			}
		}
	const auto grid = ripGrid();
	for (bool holdNext : {false, true})
	{
		auto curved = curveFixture();
		if (!expect(applyModelEdit(&curved, captureCurves(), nullptr, &error), "capture connectivity curve fixture", error))
			continue;
		if (holdNext)
			curved.skeleton.clips[0].curves[0].channels[0].first().interpolation = ModelCurveInterpolation::StepNext;
		for (auto &surface : curved.surfaces)
			expect(materialiseModelVertexIdentities(&surface, &error), "materialise curved source", error);
		const auto saved = ripBytes(curved);
		ModelMesh decoded;
		expect(editableModelJson(curved).value("version").toInt() == 11 && parseEditableModel(saved, &decoded, &error) &&
				   ripBytes(decoded) == saved,
			   "version 11 preserves ordinary and hold-next skeletal curves", error);
	}
	for (auto kind : {ModelEditKind::Subdivide, ModelEditKind::PokeFaces, ModelEditKind::ShadeFlat, ModelEditKind::ShadeSmooth,
					  ModelEditKind::ShadeAutoSmooth, ModelEditKind::DetachUv, ModelEditKind::ProjectUv, ModelEditKind::UnwrapUv,
					  ModelEditKind::PackUv})
		operation(QString("tool %1").arg(int(kind)), source, faceEdit(kind, source));
	for (bool individual : {false, true})
	{
		auto edit = faceEdit(ModelEditKind::InsetFaces, source);
		edit.tool.insetThickness = 1;
		edit.tool.inset = individual ? ModelInsetMode::Individual : ModelInsetMode::Region;
		operation(individual ? "individual inset" : "region inset", source, edit);
	}
	{
		auto edit = faceEdit(ModelEditKind::Extrude, grid);
		edit.translation = {0, 0, 3};
		operation("extrude", grid, edit);
		edit = faceEdit(ModelEditKind::Solidify, source);
		edit.tool.solidifyThickness = 2;
		operation("solidify", source, edit);
		edit = faceEdit(ModelEditKind::Bisect, source);
		edit.tool.point = {5, 0, 0};
		edit.tool.normal = {1, 0, 0};
		edit.tool.keep = ModelBisectKeep::Front;
		operation("bisect", source, edit);
		edit = faceEdit(ModelEditKind::Symmetrize, source);
		edit.tool.axis = 0;
		operation("symmetrize", source, edit);
		edit = {};
		edit.kind = ModelEditKind::LoopCut;
		const auto quads = quadSeams();
		const auto &surface = quads.surfaces[0];
		for (const auto &triangle : surface.triangles)
			for (auto edge : {modelEdge(triangle.a, triangle.b), modelEdge(triangle.b, triangle.c), modelEdge(triangle.c, triangle.a)})
			{
				const auto a = surface.frames[0].positions[edge.first], b = surface.frames[0].positions[edge.second];
				if (a.x == -16 && b.x == -16 && std::min(a.y, b.y) == -16)
					edit.selection.edges = {edge};
			}
		edit.tool.cuts = 3;
		operation("loop cut", quads, edit);
		edit = {};
		edit.kind = ModelEditKind::Knife;
		edit.tool.knife.segments = {{{-20, 5}, {20, 5}}};
		operation("knife", source, edit, false);
	}
	for (int primitive = int(ModelPrimitive::Plane); primitive <= int(ModelPrimitive::Torus); ++primitive)
	{
		ModelEdit edit;
		edit.kind = ModelEditKind::AddPrimitive;
		edit.tool.primitive = ModelPrimitive(primitive);
		edit.tool.point = {100, 0, 0};
		edit.tool.size = {48, 48, 16};
		edit.tool.segments = 12;
		edit.tool.rings = 3;
		operation(QString("primitive %1").arg(primitive), grid, edit);
	}
	{
		const auto cube = bevelCube();
		operation("edge bevel", cube, bevelCubeEdit(cube, 3, 3));
		ModelEdit edit;
		edit.kind = ModelEditKind::BevelVertices;
		edit.selection.vertices = {0};
		edit.tool.bevelWidth = 2;
		operation("vertex bevel", cube, edit);
		const auto dense = ripGrid(7);
		edit = faceEdit(ModelEditKind::Decimate, dense);
		edit.tool.ratio = .5;
		operation("decimate", dense, edit);
	}
	for (bool poles : {false, true})
		for (bool closed : {false, true})
		{
			auto edit = spinEdit(closed ? 360 : 90);
			edit.tool.spinClose = closed;
			operation(QString("spin %1 %2").arg(poles).arg(closed), spinProfile(poles), edit);
			edit.tool.spinDuplicate = true;
			operation(QString("radial copies %1 %2").arg(poles).arg(closed), spinProfile(poles), edit);
		}
	{
		auto joined = grid;
		joined.surfaces.append(joined.surfaces[0]);
		joined.surfaces[1].name = "second";
		updateEditableModelMetadata(&joined);
		ModelEdit join;
		join.kind = ModelEditKind::JoinSurfaces;
		join.targetSurface = 0;
		join.surfaces = {0, 1};
		expect(applyModelEdit(&joined, join, nullptr, &error) && components(joined.surfaces[0]) == 2,
			   "join preserves coincident surface islands", error);
		ModelEdit separate;
		separate.kind = ModelEditKind::SeparateFaces;
		separate.selection.faces = faces(grid);
		separate.text = "separate";
		expect(applyModelEdit(&joined, separate, nullptr, &error) && validateEditableModel(joined).isEmpty() && joined.surfaces.size() == 2,
			   "separate remaps and compacts identity labels", error);
	}
	{
		auto ripped = grid;
		auto split = ripEdit(grid);
		split.translation = {};
		ModelSelection selection;
		expect(applyModelEdit(&ripped, split, &selection, &error), "weld fixture", error);
		ModelEdit weld;
		weld.kind = ModelEditKind::WeldVertices;
		weld.weldDistance = 0;
		weld.preserveSeams = false;
		weld.selection.vertices = split.selection.vertices | selection.vertices;
		auto merged = ripped;
		ModelEdit merge;
		merge.kind = ModelEditKind::MergeVertices;
		merge.selection = weld.selection;
		expect(applyModelEdit(&merged, merge, nullptr, &error) && groups(merged.surfaces[0])[*split.selection.vertices.cbegin()] ==
																	  groups(merged.surfaces[0])[*selection.vertices.cbegin()],
			   "merge reconnects coincident identities while retaining corner attributes", error);
		expect(applyModelEdit(&ripped, weld, nullptr, &error) && groups(ripped.surfaces[0]) == groups(grid.surfaces[0]),
			   "explicit weld reconnects a zero-distance split", error);
	}
	{
		auto twins = source;
		auto overlapping = grid;
		ModelEdit add;
		add.kind = ModelEditKind::AddPrimitive;
		add.tool.primitive = ModelPrimitive::Plane;
		add.tool.size = {32, 32, 0};
		expect(applyModelEdit(&overlapping, add, nullptr, &error) && components(overlapping.surfaces[0]) == 2,
			   "new coincident primitive stays detached from a legacy surface", error);
		expect(applyModelEdit(&twins, faceEdit(ModelEditKind::DuplicateFaces, source), nullptr, &error), "whole-island UV fixture", error);
		for (auto kind : {ModelEditKind::UnwrapUv, ModelEditKind::PackUv, ModelEditKind::ShadeSmooth})
			expect(applyModelEdit(&twins, faceEdit(kind, twins), nullptr, &error) && components(twins.surfaces[0]) == 2,
				   "editing both coincident islands preserves their connectivity", error);
		auto repair = grid;
		repair.surfaces[0].triangles = {{0, 1, 2}, {0, 3, 4}};
		updateEditableModelMetadata(&repair);
		ModelEdit edit;
		edit.kind = ModelEditKind::SplitDisconnectedFans;
		expect(applyModelEdit(&repair, edit, nullptr, &error) && components(repair.surfaces[0]) == 2,
			   "fan repair persists its coincident split", error);
		edit.kind = ModelEditKind::RemoveUnusedVertices;
		expect(applyModelEdit(&repair, edit, nullptr, &error) && validateEditableModel(repair).isEmpty() &&
				   components(repair.surfaces[0]) == 2,
			   "unused-vertex repair retains and compacts split identities", error);
	}
	{
		auto many = grid.surfaces[0];
		many.vertexCount = 65536;
		many.texCoords.fill({}, many.vertexCount);
		many.topologyVertices.resize(many.vertexCount);
		std::iota(many.topologyVertices.begin(), many.topologyVertices.end(), 0);
		for (auto &frame : many.frames)
		{
			frame.positions.fill({}, many.vertexCount);
			frame.normals.fill({0, 0, 1}, many.vertexCount);
		}
		QElapsedTimer timer;
		timer.start();
		const auto all = groups(many);
		expect(all == many.topologyVertices && timer.elapsed() < 5000,
			   "maximum distinct coincident identities avoid quadratic position buckets");
		auto invalid = grid;
		invalid.surfaces[0].topologyVertices = {0};
		const auto before = ripBytes(invalid);
		expect(!applyModelEdit(&invalid, faceEdit(ModelEditKind::Subdivide, grid), nullptr, &error) && ripBytes(invalid) == before,
			   "malformed identity arrays refuse atomically", error);
		ModelWorkControl cancel;
		cancel.cancelled = [] { return true; };
		auto cancelled = grid;
		expect(!applyModelEdit(&cancelled, ripEdit(grid), nullptr, &error, cancel) && ripBytes(cancelled) == ripBytes(grid),
			   "cancelled materialisation is atomic");
	}
	std::cout << checks << " vertex identity checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
