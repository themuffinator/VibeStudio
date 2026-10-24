// Exact skin-weight provenance through mesh, UV and surface edits
// (core/model_vertex_origins.h): interpolating cuts blend their endpoints'
// weights, surface splits, moves and joins keep each vertex's own weights,
// Symmetrize maps joints through an explicit mirror map, and new geometry
// follows the binding the edit names or is refused. Fixtures carry
// non-uniform weights and coincident seam copies with different weights, so a
// nearest-vertex guess fails.

#include "core/model_document.h"
#include "core/model_mesh_tools.h"
#include "core/model_rig.h"
#include "core/model_skeletal_edit.h"
#include "core/model_skinning_tools.h"
#include "core/model_vertex_origins.h"
#include "tests/model_scale_test_helpers.h"
#include "tests/model_skin_provenance_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;

namespace
{
int checks = 0, failures = 0;
bool expect(bool ok, const char *name)
{
	++checks;
	if (!ok)
	{
		++failures;
		std::cerr << "FAIL: " << name << '\n';
	}
	return ok;
}

// The body vertex at (x, y); seam picks the coincident copy with shifted UVs.
int body(const ModelMesh &mesh, float x, float y, bool seam = false)
{
	ModelTexCoord uv{x / 32 + .5f, y / 8};
	if (seam)
		uv.u += x > 0 ? .125f : -.125f;
	return findSkinVertex(mesh, 0, {x, y, 0}, uv);
}

// What an exact tool gives vertex v of after.surfaces[s], derived from the
// original body only: an original vertex keeps its weights; a vertex whose
// bind position and UV interpolate an original edge (or a triangle's centre)
// takes the same interpolation of the endpoints' weights.
bool expectedWeights(const ModelMesh &before, const ModelMesh &after, int s, int v, SkinWeights *out, int *original)
{
	const auto &source = before.surfaces[0];
	const auto p = bindPosition(after, s, v);
	const auto uv = after.surfaces[s].texCoords[v];
	*original = findSkinVertex(before, 0, p, uv);
	if (*original >= 0)
	{
		*out = skinWeightsOf(source, *original);
		return true;
	}
	for (const auto &t : source.triangles)
	{
		for (const auto &[a, b] : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
		{
			const auto pa = bindPosition(before, 0, a), pb = bindPosition(before, 0, b);
			const double dx = pb.x - pa.x, dy = pb.y - pa.y, dz = pb.z - pa.z, length = dx * dx + dy * dy + dz * dz;
			if (length <= 0)
				continue;
			const double t = ((p.x - pa.x) * dx + (p.y - pa.y) * dy + (p.z - pa.z) * dz) / length;
			if (t <= 1e-6 || t >= 1 - 1e-6)
				continue;
			const ModelVec3 along{float(pa.x + t * dx), float(pa.y + t * dy), float(pa.z + t * dz)};
			const auto ua = source.texCoords[a], ub = source.texCoords[b];
			const ModelTexCoord uvAlong{float(ua.u + t * (ub.u - ua.u)), float(ua.v + t * (ub.v - ua.v))};
			if (nearPoint(along, p) && nearUv(uvAlong, uv, 1e-4))
			{
				*out = blendSkinWeights({{skinWeightsOf(source, a), 1 - t}, {skinWeightsOf(source, b), t}});
				return true;
			}
		}
		const auto pa = bindPosition(before, 0, t.a), pb = bindPosition(before, 0, t.b), pc = bindPosition(before, 0, t.c);
		const ModelVec3 centre{(pa.x + pb.x + pc.x) / 3, (pa.y + pb.y + pc.y) / 3, (pa.z + pb.z + pc.z) / 3};
		const auto ua = source.texCoords[t.a], ub = source.texCoords[t.b], uc = source.texCoords[t.c];
		if (nearPoint(centre, p) && nearUv({(ua.u + ub.u + uc.u) / 3, (ua.v + ub.v + uc.v) / 3}, uv, 1e-4))
		{
			*out = blendSkinWeights({{skinWeightsOf(source, t.a), 1.0 / 3}, {skinWeightsOf(source, t.b), 1.0 / 3}, {skinWeightsOf(source, t.c), 1.0 / 3}});
			return true;
		}
	}
	return false;
}

ModelEdit paintAfter()
{
	ModelEdit edit;
	edit.kind = ModelEditKind::PaintSkinWeights;
	edit.selection.surface = 0;
	ModelWeightStroke stroke;
	stroke.surface = 0;
	stroke.joint = QStringLiteral("spine");
	stroke.value = .5;
	stroke.strength = .5;
	stroke.samples = {{0, 1}};
	edit.weightStrokes = {stroke};
	return edit;
}

SkinWeights mirrored(const SkinWeights &weights)
{
	SkinWeights result;
	for (auto i = weights.cbegin(); i != weights.cend(); ++i)
		result[i.key() == fixtureArmL ? fixtureArmR : i.key() == fixtureArmR ? fixtureArmL : i.key()] += i.value();
	return result;
}

// The other surfaces of the fixture are untouched by body-only edits.
bool othersExact(const ModelMesh &fixture, const ModelMesh &after)
{
	for (int s = 1; s < fixture.surfaces.size(); ++s)
	{
		if (after.surfaces[s].vertexCount != fixture.surfaces[s].vertexCount ||
			after.surfaces[s].skinning.isEmpty() != fixture.surfaces[s].skinning.isEmpty())
			return false;
		for (int v = 0; !fixture.surfaces[s].skinning.isEmpty() && v < fixture.surfaces[s].vertexCount; ++v)
			if (!sameInfluences(fixture.surfaces[s], v, after.surfaces[s], v))
				return false;
	}
	return true;
}

void checkClassification()
{
	int authored = 0, binding = 0, frames = 0;
	for (int value = 0; value <= int(ModelEditKind::SetSkeletalWorkingSet); ++value)
	{
		const auto kind = ModelEditKind(value);
		const auto handling = modelEditSkinHandling(kind);
		const bool direct = isModelRigEdit(kind) || isModelSkeletalEdit(kind) || isModelSkinEdit(kind) ||
							kind == ModelEditKind::PaintSkinWeights || kind == ModelEditKind::TransferSkinWeights;
		if (!expect((handling == ModelEditSkinHandling::Direct) == direct, "direct skin handling matches the edits that bypass reconciliation"))
			std::cerr << "  kind " << value << '\n';
		authored += handling == ModelEditSkinHandling::Authored;
		binding += handling == ModelEditSkinHandling::Binding;
		frames += handling == ModelEditSkinHandling::Frames;
	}
	expect(modelEditSkinHandling(ModelEditKind::Knife) == ModelEditSkinHandling::Authored && authored == 4,
		   "edge bevel, knife, spin and rip are the tools that write their own influences");
	expect(modelEditSkinHandling(ModelEditKind::AddPrimitive) == ModelEditSkinHandling::Binding && binding == 1,
		   "only new geometry without a source needs a binding");
	expect(modelEditSkinHandling(ModelEditKind::DuplicateFrame) == ModelEditSkinHandling::Frames && frames == 4,
		   "duplicate, delete, in-between and copy-pose frame edits keep the clip mapping");
	expect(modelEditSkinHandling(ModelEditKind::Symmetrize) == ModelEditSkinHandling::Provenance &&
			   modelEditSkinHandling(ModelEditKind::JoinSurfaces) == ModelEditSkinHandling::Provenance,
		   "symmetrize and surface management follow provenance");
}

void checkJointMaps()
{
	ModelSkeleton skeleton;
	for (const char *name : {"spine", "Bip01 L Thigh", "Bip01 R Thigh", "Lhand", "Rhand", "l_foot", "r_foot", "LeftArm", "RightArm",
							 "hand.L", "hand.R", "Lspine", "lonely_l"})
		skeleton.joints.append({QString::fromLatin1(name), -1, modelJointIdentity()});
	const auto pairs = detectModelMirrorJointPairs(skeleton);
	expect(pairs.value("Bip01 L Thigh") == "Bip01 R Thigh" && pairs.value("Rhand") == "Lhand" && pairs.value("l_foot") == "r_foot" &&
			   pairs.value("RightArm") == "LeftArm" && pairs.value("hand.L") == "hand.R",
		   "pairs are detected from Biped, Doom 3, prefix, word and suffix naming");
	expect(!pairs.contains("spine") && !pairs.contains("Lspine") && !pairs.contains("lonely_l") && pairs.size() == 10,
		   "joints without an existing partner are never paired");
	expect(modelMirrorJointMapText({{"arm.L", "arm.R"}, {"arm.R", "arm.L"}}) == "arm.L=arm.R", "a symmetric map prints each pair once");
	QMap<QString, QString> map;
	QString error;
	expect(parseModelMirrorJointMap(" arm.L = arm.R ", &map, &error) && map.value("arm.R") == "arm.L" && map.size() == 2,
		   "a parsed pair maps both ways");
	expect(!parseModelMirrorJointMap("a=b,a=c", &map, &error) && error.contains("a"), "conflicting partners are refused");
	expect(!parseModelMirrorJointMap("a", &map, &error), "malformed pairs are refused");
	const auto mesh = skinFixture();
	QMap<QString, QString> complete;
	expect(completeModelMirrorJointMap(mesh.skeleton, {{"arm.L", "arm.R"}}, &complete, &error) && complete.value("arm.R") == "arm.L",
		   "a one-way map is completed in both directions");
	expect(!completeModelMirrorJointMap(mesh.skeleton, {{"arm.L", "wing"}}, &complete, &error) && error.contains("wing"),
		   "names the skeleton lacks are refused");
	expect(modelOffCentreJoints(mesh, 0, 0) == QStringList({"arm.L", "arm.R"}) && modelOffCentreJoints(mesh, 1, 0).isEmpty(),
		   "off-centre joints are the ones that weight the surface away from the plane");
}

void checkBlend()
{
	auto mesh = skinFixture();
	const auto &surface = mesh.surfaces[0];
	const int a = body(mesh, 0, 0), b = body(mesh, 8, 8, true);
	QVector<ModelJointInfluence> influences;
	expect(blendModelSkinInfluences({{&surface.skinning, a, .25}, {&surface.skinning, b, .75}}, &influences) == ModelSkinBlendStatus::Blended,
		   "two vertices blend");
	ModelSurface probe = surface;
	probe.skinning.first.append(probe.skinning.influences.size());
	probe.skinning.count.append(influences.size());
	probe.skinning.influences += influences;
	probe.vertexCount += 1;
	probe.triangles = {{0, 1, 2}};
	bool exact = true;
	for (const auto &pose : mesh.skeleton.clips[0].frames)
	{
		ModelFrameGeometry skinned;
		if (!skinModelSurface(probe, pose, &skinned))
			exact = false;
		else
		{
			const auto pa = skinned.positions[a], pb = skinned.positions[b], blend = skinned.positions.last();
			exact &= nearPoint(blend, {.25f * pa.x + .75f * pb.x, .25f * pa.y + .75f * pb.y, .25f * pa.z + .75f * pb.z}, 1e-5);
		}
	}
	expect(exact, "blended influences pose to the same blend of the posed sources in every clip frame");
	auto negative = surface.skinning;
	negative.influences[negative.first[a]].weight = -1;
	expect(blendModelSkinInfluences({{&negative, a, 1}}, &influences) == ModelSkinBlendStatus::NegativeWeight, "negative weights are refused");
	ModelSurfaceSkinning wide;
	wide.first = {0};
	wide.count = {65};
	for (int j = 0; j < 65; ++j)
		wide.influences.append({j, 1.f / 65, {}, {}});
	expect(blendModelSkinInfluences({{&wide, 0, 1}}, &influences) == ModelSkinBlendStatus::InfluenceCount, "more than 64 joints are refused");
}

struct Cut
{
	const char *name;
	ModelEdit edit;
	int created;
};

void checkInterpolatingCuts()
{
	const auto fixture = skinFixture();
	QString error;
	expect(posesFollowSkeleton(fixture, &error), "the fixture's poses follow its skeleton");
	expect(!sameSkinWeights(skinWeightsOf(fixture.surfaces[0], body(fixture, 8, 8)), skinWeightsOf(fixture.surfaces[0], body(fixture, 8, 8, true))),
		   "coincident seam copies carry different weights");
	QVector<Cut> cuts;
	{
		ModelEdit edit;
		edit.kind = ModelEditKind::Subdivide;
		edit.selection = {0, {}, {4, 5}};
		cuts.append({"Subdivide", edit, 5});
	}
	{
		ModelEdit edit;
		edit.kind = ModelEditKind::SplitEdges;
		edit.selection = {0, {}, {}, {modelEdge(body(fixture, 0, 0), body(fixture, 8, 0))}};
		cuts.append({"Split Edges", edit, 1});
	}
	for (double slide : {0.0, -.5, .5})
	{
		ModelEdit edit;
		edit.kind = ModelEditKind::LoopCut;
		edit.selection = {0, {}, {}, {modelEdge(body(fixture, 0, 0), body(fixture, 0, 8))}};
		edit.tool.slide = slide;
		cuts.append({"Loop Cut", edit, -1});
	}
	{
		ModelEdit edit;
		edit.kind = ModelEditKind::Bisect;
		edit.selection = {0, {}, {4, 5}};
		edit.tool.point = {4, 0, 0};
		edit.tool.normal = {1, 0, 0};
		cuts.append({"Bisect", edit, 3});
	}
	{
		ModelEdit edit;
		// The corner beside the seam copy: one cut runs towards it.
		edit.kind = ModelEditKind::BevelVertices;
		edit.selection = {0, {body(fixture, 16, 8)}, {}};
		cuts.append({"Bevel Vertices", edit, 3});
	}
	{
		ModelEdit edit;
		edit.kind = ModelEditKind::PokeFaces;
		edit.selection = {0, {}, {7}};
		cuts.append({"Poke Faces", edit, 1});
	}
	for (const auto &cut : cuts)
	{
		ModelDocument document;
		ModelEditReport report;
		if (!expect(document.setMesh(fixture, &error), "the fixture opens"))
			return;
		const auto opened = skinBytes(document.mesh());
		if (!expect(document.edit(cut.edit, &error, {}, &report), cut.name))
		{
			std::cerr << "  " << cut.name << ": " << error.toStdString() << '\n';
			continue;
		}
		const auto &after = document.mesh();
		int created = 0, matched = 0, exactKept = 0, originals = 0;
		for (int v = 0; v < after.surfaces[0].vertexCount; ++v)
		{
			SkinWeights expected;
			int original = -1;
			if (!expectedWeights(fixture, after, 0, v, &expected, &original))
				continue;
			++matched;
			created += original < 0;
			originals += original >= 0;
			if (sameSkinWeights(skinWeightsOf(after.surfaces[0], v), expected))
				exactKept += original >= 0 ? 0 : 1;
			else
				std::cerr << "  " << cut.name << ": vertex " << v << " has unexpected weights\n";
		}
		bool originalsExact = true;
		for (int v = 0; v < after.surfaces[0].vertexCount; ++v)
		{
			const int original = findSkinVertex(fixture, 0, bindPosition(after, 0, v), after.surfaces[0].texCoords[v]);
			if (original >= 0)
				originalsExact &= sameInfluences(fixture.surfaces[0], original, after.surfaces[0], v, false);
		}
		const auto provenance = report.skin.value(0);
		if (!expect(matched == after.surfaces[0].vertexCount && exactKept == created && originalsExact,
				"every vertex keeps its own or the interpolated weights of the vertices it came from"))
			std::cerr << "  " << cut.name << ": matched " << matched << " of " << after.surfaces[0].vertexCount << ", created " << created << '\n';
		if (!expect(created > 0 && (cut.created < 0 || created == cut.created) && provenance.interpolated == created &&
				provenance.bound == 0 && provenance.merged == 0 && provenance.kept == originals,
				"the edit report counts interpolated and kept vertices exactly"))
			std::cerr << "  " << cut.name << ": interpolated " << provenance.interpolated << " kept " << provenance.kept << '\n';
		expect(othersExact(fixture, after), "other surfaces keep their influences bit for bit");
		if (!expect(posesFollowSkeleton(after, &error), "retained skeletal poses still follow the skin after the cut"))
			std::cerr << "  " << cut.name << ": " << error.toStdString() << '\n';
		const bool painted = document.edit(paintAfter(), &error);
		if (!expect(painted, "weight painting accepts the cut surface"))
			std::cerr << "  " << cut.name << ": " << error.toStdString() << '\n';
		expect((!painted || document.undo()) && document.undo() && skinBytes(document.mesh()) == opened, "undo restores the input byte for byte");
		expect(modelSkinProvenanceSummary(report.skin).contains("interpolated"), "the summary names interpolated weights");
	}
}

void checkCoincidentSurvivors()
{
	const auto fixture = skinFixture();
	ModelDocument document;
	QString error;
	ModelEditReport report;
	ModelEdit remove;
	remove.kind = ModelEditKind::DeleteFaces;
	remove.selection = {0, {}, {0, 1}};
	if (!expect(document.setMesh(fixture, &error) && document.edit(remove, &error, {}, &report), "faces delete from a skinned surface"))
		return;
	const auto &after = document.mesh();
	bool exact = after.surfaces[0].vertexCount == fixture.surfaces[0].vertexCount - 3;
	for (int v = 0; v < after.surfaces[0].vertexCount; ++v)
	{
		const int original = findSkinVertex(fixture, 0, bindPosition(after, 0, v), after.surfaces[0].texCoords[v]);
		exact &= original >= 0 && sameInfluences(fixture.surfaces[0], original, after.surfaces[0], v);
	}
	expect(exact, "survivors of a compaction keep their own influences, including coincident seam copies");
	expect(report.skin.value(0).kept == after.surfaces[0].vertexCount && report.skin.value(0).bound == 0, "survivors are reported as kept");
}

// Every vertex of the edited mesh matches a skinned fixture vertex at the same
// bind position and UV, with bit-identical influences.
bool everyVertexKept(const ModelMesh &fixture, const ModelMesh &after, const char *name)
{
	bool exact = true;
	for (int s = 0; s < after.surfaces.size(); ++s)
	{
		if (after.surfaces[s].name == QLatin1String("prop"))
		{
			exact &= after.surfaces[s].skinning.isEmpty();
			continue;
		}
		exact &= !after.surfaces[s].skinning.isEmpty();
		for (int v = 0; exact && v < after.surfaces[s].vertexCount; ++v)
		{
			bool found = false;
			for (int f = 0; f < fixture.surfaces.size() && !found; ++f)
			{
				if (fixture.surfaces[f].skinning.isEmpty())
					continue;
				const int original = findSkinVertex(fixture, f, bindPosition(after, s, v), after.surfaces[s].texCoords[v]);
				found = original >= 0 && sameInfluences(fixture.surfaces[f], original, after.surfaces[s], v);
			}
			if (!found)
			{
				std::cerr << "  " << name << ": surface " << s << " vertex " << v << " lost its own influences\n";
				exact = false;
			}
		}
	}
	return exact;
}

void checkSurfaces()
{
	const auto fixture = skinFixture();
	QString error;
	const auto run = [&](const char *name, ModelEdit edit, ModelEditReport *report, ModelMesh *result)
	{
		ModelDocument document;
		if (!document.setMesh(fixture, &error))
			return false;
		const auto opened = skinBytes(document.mesh());
		if (!document.edit(edit, &error, {}, report))
		{
			std::cerr << "  " << name << ": " << error.toStdString() << '\n';
			return false;
		}
		*result = document.mesh();
		const bool kept = everyVertexKept(fixture, *result, name);
		const bool poses = posesFollowSkeleton(*result, &error);
		if (!poses)
			std::cerr << "  " << name << ": " << error.toStdString() << '\n';
		const auto edited = skinBytes(*result);
		return kept && poses && document.undo() && skinBytes(document.mesh()) == opened && document.redo() && skinBytes(document.mesh()) == edited;
	};
	ModelEditReport report;
	ModelMesh result;
	ModelEdit separate;
	separate.kind = ModelEditKind::SeparateFaces;
	separate.selection = {0, {}, {6, 7}};
	separate.text = QStringLiteral("arm");
	expect(run("separate", separate, &report, &result) && result.surfaces.size() == 5 && !result.surfaces[4].skinning.isEmpty(),
		   "separated faces stay skinned with their own weights");
	expect(report.skin.value(4).kept == result.surfaces[4].vertexCount && report.skin.value(4).bound == 0 &&
			   modelSkinProvenanceSummary(report.skin, true).contains("kept exactly"),
		   "the report confirms the separated surface's weights were kept");
	{
		// The separated part still exports its weights as a skeletal mesh.
		auto exportable = result;
		exportable.surfaces.removeAt(2);
		updateEditableModelMetadata(&exportable);
		const auto md5 = exportEditableModel(exportable, QStringLiteral("md5mesh"), 0, &error);
		const auto decoded = decodeModelMesh(QStringLiteral("provenance.md5mesh"), md5);
		bool weighted = !md5.isEmpty() && decoded.error.isEmpty() && decoded.surfaces.size() == exportable.surfaces.size();
		for (int s = 0; weighted && s < decoded.surfaces.size(); ++s)
		{
			const auto &surface = decoded.surfaces[s];
			for (int v = 0; weighted && v < surface.vertexCount; ++v)
			{
				const auto position = surface.frames[std::max(0, modelBindPoseFrame(decoded))].positions[v];
				int source = -1, sourceSurface = -1;
				for (int e = 0; e < exportable.surfaces.size() && source < 0; ++e)
					if ((source = findSkinVertex(exportable, e, position, surface.texCoords[v])) >= 0)
						sourceSurface = e;
				SkinWeights byName, expected;
				const auto decodedWeights = skinWeightsOf(surface, v);
				for (auto i = decodedWeights.cbegin(); i != decodedWeights.cend(); ++i)
					byName[modelJointIndex(exportable.skeleton, decoded.skeleton.joints[i.key()].name)] += i.value();
				if (source >= 0)
					expected = skinWeightsOf(exportable.surfaces[sourceSurface], source);
				weighted = source >= 0 && sameSkinWeights(byName, expected, 1e-4);
				if (!weighted)
					std::cerr << "  md5 surface " << s << " vertex " << v << " at " << position.x << ',' << position.y << ',' << position.z
							  << " uv " << surface.texCoords[v].u << ',' << surface.texCoords[v].v << " source " << source << '\n';
			}
		}
		if (!expect(weighted, "MD5 export and re-import keep every separated vertex's weights"))
			std::cerr << "  " << error.toStdString() << " bytes " << md5.size() << " decoded " << decoded.error.toStdString() << " surfaces "
					  << decoded.surfaces.size() << " of " << exportable.surfaces.size() << '\n';
	}
	ModelEdit move;
	move.kind = ModelEditKind::MoveFacesToSurface;
	move.selection = {0, {}, {6, 7}};
	move.targetSurface = 1;
	expect(run("move part", move, &report, &result) && result.surfaces[1].vertexCount == 4 + 4,
		   "faces moved into another skinned surface keep the weights of the surface they came from");
	move.selection = {1, {}, {0, 1}};
	move.targetSurface = 0;
	expect(run("move all down", move, &report, &result) && result.surfaces.size() == 3 && result.surfaces[0].vertexCount == 16,
		   "moving a whole surface to an earlier surface keeps both surfaces' weights");
	move.selection = {0, {}, {0, 1, 2, 3, 4, 5, 6, 7}};
	move.targetSurface = 3;
	expect(run("move all up", move, &report, &result) && result.surfaces.size() == 3 && result.surfaces[2].name == "gem" &&
			   result.surfaces[2].vertexCount == 22,
		   "moving a whole surface past its own index keeps weights after the index shift");
	for (const auto &[surfaces, target] : {qMakePair(QSet<int>{0, 1}, 1), qMakePair(QSet<int>{0, 1, 3}, 0), qMakePair(QSet<int>{0, 1, 3}, 1)})
	{
		ModelEdit join;
		join.kind = ModelEditKind::JoinSurfaces;
		join.selection = {target, {}, {}};
		join.surfaces = surfaces;
		join.targetSurface = target;
		expect(run("join", join, &report, &result) && result.surfaces.size() == 4 - surfaces.size() + 1,
			   "joined surfaces keep every vertex's weights wherever the target sits");
		expect(!report.skin.isEmpty() && std::all_of(report.skin.cbegin(), report.skin.cend(),
											 [](const ModelSkinProvenance &entry) { return entry.bound == 0 && entry.merged == 0 && entry.interpolated == 0; }),
			   "joins blend nothing");
	}
	{
		ModelDocument document;
		ModelEdit join;
		join.kind = ModelEditKind::JoinSurfaces;
		join.selection = {0, {}, {}};
		join.surfaces = {0, 2};
		join.targetSurface = 0;
		const bool opened = document.setMesh(fixture, &error);
		const auto bytes = skinBytes(document.mesh());
		expect(opened && !document.edit(join, &error) && error.contains("prop") && error.contains("Bind") && skinBytes(document.mesh()) == bytes &&
				   !document.canUndo(),
			   "joining an unskinned surface into a skinned one is refused without changing the document");
		move.selection = {0, {}, {6, 7}};
		move.targetSurface = 2;
		expect(!document.edit(move, &error) && error.contains("prop") && skinBytes(document.mesh()) == bytes,
			   "moving skinned faces into an unskinned surface is refused");
	}
	ModelEdit duplicate;
	duplicate.kind = ModelEditKind::DuplicateSurface;
	duplicate.selection = {0, {}, {}};
	duplicate.text = QStringLiteral("body copy");
	expect(run("duplicate", duplicate, &report, &result) && result.surfaces.size() == 5, "a duplicated surface keeps its weights");
	ModelEdit remove;
	remove.kind = ModelEditKind::DeleteSurface;
	remove.selection = {0, {}, {}};
	expect(run("delete", remove, &report, &result) && result.surfaces[0].name == "head", "surfaces after a deleted one keep their weights");
}

ModelEdit symmetrizeEdit(const QMap<QString, QString> &joints = {}, bool keep = false)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::Symmetrize;
	edit.selection = {0, {}, {0, 1, 2, 3, 4, 5, 6, 7}};
	edit.tool.axis = 0;
	edit.tool.positiveToNegative = true;
	edit.tool.mirrorJoints = joints;
	edit.tool.keepJointNames = keep;
	return edit;
}

// Mirrored vertices (x < 0) take their +X source's weights through `map`.
bool mirroredFromSources(const ModelMesh &after, bool mapJoints)
{
	bool ok = true;
	const auto &surface = after.surfaces[0];
	int mirroredCount = 0;
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		const auto p = bindPosition(after, 0, v);
		if (p.x > -1e-4)
			continue;
		++mirroredCount;
		const int source = findSkinVertex(after, 0, {-p.x, p.y, p.z}, surface.texCoords[v]);
		const auto expected = source >= 0 ? (mapJoints ? mirrored(skinWeightsOf(surface, source)) : skinWeightsOf(surface, source)) : SkinWeights{};
		if (source < 0 || !sameSkinWeights(skinWeightsOf(surface, v), expected))
		{
			std::cerr << "  mirrored vertex " << v << " at " << p.x << ',' << p.y << " does not carry its source's weights\n";
			ok = false;
		}
	}
	return ok && mirroredCount > 0;
}

void checkSymmetrize()
{
	const auto fixture = skinFixture();
	QString error;
	{
		ModelDocument document;
		ModelEditReport report;
		if (!expect(document.setMesh(fixture, &error), "the symmetric fixture opens"))
			return;
		const auto opened = skinBytes(document.mesh());
		if (!expect(document.edit(symmetrizeEdit({{"arm.L", "arm.R"}}), &error, {}, &report), "a mapped symmetrize applies"))
			std::cerr << "  " << error.toStdString() << '\n';
		const auto &after = document.mesh();
		expect(after.surfaces[0].vertexCount == fixture.surfaces[0].vertexCount,
			   "a symmetric mesh keeps its vertex count, the case a size check cannot see");
		expect(mirroredFromSources(after, true), "mirrored vertices take their source's weights with left and right joints swapped");
		// The fixture is symmetric, so each position holds the same weight sets as before.
		bool sameSets = true;
		for (int v = 0; v < after.surfaces[0].vertexCount; ++v)
		{
			const auto p = bindPosition(after, 0, v);
			QList<SkinWeights> now, then;
			for (int w = 0; w < after.surfaces[0].vertexCount; ++w)
				if (nearPoint(bindPosition(after, 0, w), p))
					now.append(skinWeightsOf(after.surfaces[0], w));
			for (int w = 0; w < fixture.surfaces[0].vertexCount; ++w)
				if (nearPoint(bindPosition(fixture, 0, w), p))
					then.append(skinWeightsOf(fixture.surfaces[0], w));
			bool matched = now.size() == then.size();
			for (const auto &weights : now)
				matched &= std::any_of(then.cbegin(), then.cend(), [&](const SkinWeights &other) { return sameSkinWeights(weights, other); });
			sameSets &= matched;
		}
		expect(sameSets, "every position of the symmetric fixture holds its original weight sets, seam copies included");
		bool survivors = true;
		for (int v = 0; v < after.surfaces[0].vertexCount; ++v)
		{
			const auto p = bindPosition(after, 0, v);
			if (p.x < 1e-4)
				continue;
			const int original = findSkinVertex(fixture, 0, p, after.surfaces[0].texCoords[v]);
			survivors &= original >= 0 && sameInfluences(fixture.surfaces[0], original, after.surfaces[0], v, false);
		}
		expect(survivors, "source-side vertices keep their joints, weights and offsets bit for bit");
		expect(report.skin.value(0).mirrored == 5 && report.skin.value(0).bound == 0, "the report counts the mirrored vertices");
		expect(modelSkinProvenanceSummary(report.skin).contains("mirrored"), "the summary names mirrored weights");
		if (!expect(posesFollowSkeleton(after, &error), "the mirrored half follows its own joints in every retained pose"))
			std::cerr << "  " << error.toStdString() << '\n';
		expect(othersExact(fixture, after), "other surfaces are untouched");
		const bool painted = document.edit(paintAfter(), &error);
		if (!expect(painted, "weight painting accepts the symmetrized surface"))
			std::cerr << "  " << error.toStdString() << '\n';
		expect((!painted || document.undo()) && document.undo() && skinBytes(document.mesh()) == opened, "undo restores the unsymmetrized source");
	}
	{
		ModelDocument document;
		const bool opened = document.setMesh(fixture, &error);
		const auto bytes = skinBytes(document.mesh());
		expect(opened && !document.edit(symmetrizeEdit(), &error) && error.contains("arm.L") && skinBytes(document.mesh()) == bytes,
			   "symmetrize without a mirror map for off-centre joints is refused");
		expect(!document.edit(symmetrizeEdit({{"arm.L", "wing"}}), &error) && error.contains("wing"), "an unknown mirror joint is refused");
		expect(document.edit(symmetrizeEdit({}, true), &error) && mirroredFromSources(document.mesh(), false),
			   "keeping joint names binds the mirrored half to the same joints");
		ModelEdit other;
		other.kind = ModelEditKind::PokeFaces;
		other.selection = {0, {}, {4}};
		other.tool.mirrorJoints = {{"arm.L", "arm.R"}};
		expect(!document.edit(other, &error) && error.contains("Symmetrize"), "mirror maps are refused for other tools");
	}
	{
		// A half-modelled character: only the +X half exists before mirroring.
		ModelDocument document;
		ModelEdit half;
		half.kind = ModelEditKind::DeleteFaces;
		half.selection = {0, {}, {0, 1, 2, 3}};
		ModelEditReport report;
		if (!expect(document.setMesh(fixture, &error) && document.edit(half, &error), "the -X half is removed"))
			return;
		const int halfCount = document.mesh().surfaces[0].vertexCount;
		ModelEdit edit = symmetrizeEdit(detectModelMirrorJointPairs(fixture.skeleton));
		edit.selection.faces = {0, 1, 2, 3};
		expect(document.edit(edit, &error, {}, &report) && document.mesh().surfaces[0].vertexCount > halfCount &&
				   mirroredFromSources(document.mesh(), true),
			   "detected pairs give the new half its own side's joints");
		expect(posesFollowSkeleton(document.mesh(), &error), "the new half follows the mirrored joints in every retained pose");
	}
	{
		// A closed component in the middle of the vertex array extrudes into
		// copies that replace its originals without changing the count.
		ModelDocument document;
		ModelEdit extrude;
		extrude.kind = ModelEditKind::Extrude;
		extrude.selection = {3, {}, {1, 2, 3, 4}};
		extrude.translation = {0, 0, 2};
		ModelEditReport report;
		if (!expect(document.setMesh(fixture, &error) && document.edit(extrude, &error, {}, &report), "the closed tetrahedron extrudes"))
		{
			std::cerr << "  " << error.toStdString() << '\n';
			return;
		}
		const auto &after = document.mesh();
		const auto &gem = after.surfaces[3];
		bool follow = gem.vertexCount == fixture.surfaces[3].vertexCount;
		for (int v = 0; follow && v < gem.vertexCount; ++v)
		{
			auto p = bindPosition(after, 3, v);
			int original = findSkinVertex(fixture, 3, p, gem.texCoords[v]);
			if (original >= 0)
			{
				follow &= sameInfluences(fixture.surfaces[3], original, gem, v);
				continue;
			}
			p.z -= 2;
			original = findSkinVertex(fixture, 3, p, gem.texCoords[v]);
			follow &= original >= 0 && sameSkinWeights(skinWeightsOf(gem, v), skinWeightsOf(fixture.surfaces[3], original));
		}
		expect(follow, "each extruded copy keeps its source's weights and untouched vertices keep theirs");
		expect(report.skin.value(3).rebound == 4 && report.skin.value(3).bound == 0, "the moved copies are reported as rebound");
		bool posed = true;
		ModelSkinningWork work;
		posed &= work.prepare(after, &error);
		for (int frame = 0; posed && frame < after.frames.size(); ++frame)
		{
			ModelFrameGeometry skinned;
			posed &= skinModelSurface(gem, work.pose(after, frame), &skinned);
			for (int v = 0; posed && v < gem.vertexCount; ++v)
				posed &= nearPoint(skinned.positions[v], gem.frames[frame].positions[v], 1e-3);
		}
		expect(posed, "the extruded copies follow the skeleton in every retained pose");
	}
}

ModelEdit primitiveEdit(int surface)
{
	ModelEdit add;
	add.kind = ModelEditKind::AddPrimitive;
	add.selection = {surface, {}, {}};
	add.tool.primitive = ModelPrimitive::Cube;
	add.tool.point = {0, 4, 8};
	add.tool.size = {4, 4, 4};
	add.tool.axis = 2;
	return add;
}

void checkNewGeometry()
{
	const auto fixture = skinFixture();
	QString error;
	ModelEditReport report;
	{
		ModelDocument document;
		const bool opened = document.setMesh(fixture, &error);
		const auto bytes = skinBytes(document.mesh());
		expect(opened && !document.edit(primitiveEdit(0), &error, {}, &report) && error.contains("body") && error.contains("joint") &&
				   error.contains("new surface") && skinBytes(document.mesh()) == bytes && !document.canUndo(),
			   "new geometry on a skinned surface without a binding is refused and names the choices");
	}
	{
		// Rigidly to the head: one full-weight influence, posed with the head.
		ModelDocument document;
		auto add = primitiveEdit(0);
		add.tool.newGeometryBinding = ModelNewGeometryBinding::Joint;
		add.tool.bindJoint = QStringLiteral("head");
		if (!expect(document.setMesh(fixture, &error) && document.edit(add, &error, {}, &report), "a primitive binds rigidly to a joint"))
		{
			std::cerr << "  " << error.toStdString() << '\n';
			return;
		}
		const auto &after = document.mesh();
		const int added = after.surfaces[0].vertexCount - fixture.surfaces[0].vertexCount;
		bool rigid = added > 0;
		for (int v = fixture.surfaces[0].vertexCount; rigid && v < after.surfaces[0].vertexCount; ++v)
			rigid &= sameSkinWeights(skinWeightsOf(after.surfaces[0], v), {{fixtureHead, 1.0}});
		bool originals = true;
		for (int v = 0; v < fixture.surfaces[0].vertexCount; ++v)
			originals &= sameInfluences(fixture.surfaces[0], v, after.surfaces[0], v);
		expect(rigid && originals, "every new vertex follows the head alone and the originals keep their influences");
		expect(report.skin.value(0).bound == added && report.skin.value(0).kept == fixture.surfaces[0].vertexCount,
			   "the report counts the bound and kept vertices");
		if (!expect(posesFollowSkeleton(after, &error), "the bound primitive follows the head in every retained pose"))
			std::cerr << "  " << error.toStdString() << '\n';
		const auto summary = modelSkinProvenanceSummary(report.skin);
		expect(summary.contains("bound"), "the summary names the bound vertices");
		const auto json = modelSkinProvenanceJson(after, report.skin);
		expect(json.value("bound").toInt() == added && json.value("surfaces").toArray().first().toObject().value("name") == "body",
			   "the JSON report carries bound counts per surface");
		expect(document.edit(paintAfter(), &error), "weight painting accepts the bound primitive");
	}
	{
		ModelDocument document;
		auto add = primitiveEdit(0);
		add.tool.newGeometryBinding = ModelNewGeometryBinding::NearestBones;
		bool nearest = document.setMesh(fixture, &error) && document.edit(add, &error, {}, &report);
		const auto &after = document.mesh();
		for (int v = fixture.surfaces[0].vertexCount; nearest && v < after.surfaces[0].vertexCount; ++v)
		{
			double total = 0;
			for (double weight : skinWeightsOf(after.surfaces[0], v))
				total += weight;
			nearest &= after.surfaces[0].skinning.count[v] >= 1 && after.surfaces[0].skinning.count[v] <= 4 && std::abs(total - 1) < 1e-5;
		}
		expect(nearest && posesFollowSkeleton(after, &error), "nearest-bone binding gives each new vertex up to four normalised influences");
	}
	{
		ModelDocument document;
		auto add = primitiveEdit(0);
		add.tool.newSurface = QStringLiteral("helmet");
		expect(document.setMesh(fixture, &error) && document.edit(add, &error, {}, &report) && document.mesh().surfaces.size() == 5 &&
				   document.mesh().surfaces[4].name == "helmet" && document.mesh().surfaces[4].skinning.isEmpty() &&
				   document.mesh().surfaces[0].vertexCount == fixture.surfaces[0].vertexCount && othersExact(fixture, document.mesh()),
			   "a primitive added as a new surface without a binding stays unskinned");
		add.tool.newSurface = QStringLiteral("visor");
		add.tool.newGeometryBinding = ModelNewGeometryBinding::Joint;
		add.tool.bindJoint = QStringLiteral("head");
		expect(document.edit(add, &error, {}, &report) && document.mesh().surfaces[5].name == "visor" &&
				   !document.mesh().surfaces[5].skinning.isEmpty() && report.skin.value(5).bound == document.mesh().surfaces[5].vertexCount,
			   "a new surface can be bound rigidly to a joint");
		add.tool.newSurface = QStringLiteral("helmet");
		expect(!document.edit(add, &error) && error.contains("helmet"), "a new surface needs an unused name");
	}
	{
		ModelDocument document;
		expect(document.setMesh(fixture, &error) && document.edit(primitiveEdit(2), &error, {}, &report) &&
				   document.mesh().surfaces[2].skinning.isEmpty() && !report.skin.contains(2),
			   "new geometry on an unskinned surface stays unskinned");
		auto add = primitiveEdit(2);
		add.tool.newGeometryBinding = ModelNewGeometryBinding::Joint;
		add.tool.bindJoint = QStringLiteral("head");
		expect(!document.edit(add, &error) && error.contains("prop"), "binding new geometry on an unskinned surface is refused");
		add.tool.bindJoint = QStringLiteral("tail");
		expect(!document.edit(add, &error) && error.contains("tail"), "an unknown binding joint is refused");
		auto other = primitiveEdit(0);
		other.kind = ModelEditKind::PokeFaces;
		other.tool = {};
		other.tool.bindJoint = QStringLiteral("head");
		other.tool.newGeometryBinding = ModelNewGeometryBinding::Joint;
		other.selection = {0, {}, {4}};
		expect(!document.edit(other, &error) && error.contains("Add Primitive"), "bindings are refused for other tools");
	}
}

// Joints on the mirror plane keep their names whatever a map says, and a
// mapped pair must sit on opposite sides of the plane.
void checkMirrorPlanes()
{
	ModelMesh rig;
	rig.skeleton.joints = {{QStringLiteral("spine"), -1, modelJointIdentity()},
						   {QStringLiteral("Lhand"), 0, modelJointMatrix({}, {8, 0, 32})},
						   {QStringLiteral("Rhand"), 0, modelJointMatrix({}, {-8, 0, 32})}};
	const auto pairs = detectModelMirrorJointPairs(rig.skeleton);
	expect(pairs.size() == 2 && modelMirrorJointPairsForAxis(rig, pairs, 0) == pairs, "a left/right pair applies across X");
	expect(modelMirrorJointPairsForAxis(rig, pairs, 1).isEmpty(), "joints on the Y plane are not proposed as partners across it");
	expect(modelMirrorJointPairsForAxis(rig, pairs, 2).isEmpty(), "joints on the same side of the Z plane are not proposed as partners across it");
	expect(modelJointsMirrorAcross(rig.skeleton, 0, 0, 1) && !modelJointsMirrorAcross(rig.skeleton, 1, 1, 1) &&
			   modelJointsMirrorAcross(rig.skeleton, 1, 2, 1) && !modelJointsMirrorAcross(rig.skeleton, 1, 2, 4) &&
			   !modelJointsMirrorAcross(rig.skeleton, 0, 1, 1),
		   "a pair mirrors only from opposite sides, and a centred joint only to itself");
	const auto fixture = skinFixture();
	QString error;
	{
		// The arms sit on the Y = 0 plane, so mirroring across Y keeps them.
		auto across = symmetrizeEdit({{"arm.L", "arm.R"}});
		across.tool.axis = 1;
		auto plain = across;
		plain.tool.mirrorJoints.clear();
		ModelDocument mapped, unmapped;
		ModelEditReport report;
		if (!expect(mapped.setMesh(fixture, &error) && mapped.edit(across, &error, {}, &report), "symmetrize across Y with an arm map applies"))
		{
			std::cerr << "  " << error.toStdString() << '\n';
			return;
		}
		expect(unmapped.setMesh(fixture, &error) && unmapped.edit(plain, &error) && skinBytes(mapped.mesh()) == skinBytes(unmapped.mesh()),
			   "a map naming joints on the mirror plane changes nothing");
		const auto &after = mapped.mesh();
		const auto &surface = after.surfaces[0];
		bool own = report.skin.value(0).mirrored > 0;
		for (int v = 0; v < surface.vertexCount; ++v)
		{
			const auto p = bindPosition(after, 0, v);
			if (p.y > -1e-4)
				continue;
			const int source = findSkinVertex(after, 0, {p.x, -p.y, p.z}, surface.texCoords[v]);
			own &= source >= 0 && sameSkinWeights(skinWeightsOf(surface, v), skinWeightsOf(surface, source));
		}
		expect(own, "across Y each mirrored vertex keeps its own side's arm");
		expect(posesFollowSkeleton(after, &error), "the vertices mirrored across Y follow the skeleton");
	}
	{
		// The head sits above the Z = 0 plane and the spine on it: not partners.
		auto head = symmetrizeEdit({{"head", "spine"}});
		head.selection = {1, {}, {0, 1}};
		head.tool.axis = 2;
		ModelDocument document;
		const bool opened = document.setMesh(fixture, &error);
		const auto bytes = skinBytes(document.mesh());
		expect(opened && !document.edit(head, &error) && error.contains("head=spine") && error.contains("Z = 0") &&
				   skinBytes(document.mesh()) == bytes && !document.canUndo(),
			   "a mapped pair that is not on opposite sides of the plane is refused and named");
		head.tool.keepJointNames = true;
		expect(!document.edit(head, &error) && error.contains("head=spine"), "keeping joint names does not excuse a wrong pair");
		head.tool.mirrorJoints.clear();
		expect(document.edit(head, &error) && posesFollowSkeleton(document.mesh(), &error), "kept joint names mirror the head across Z");
	}
}

// Edge bevel, knife, spin and rip write their own influences. The skin's
// normal replaces a retained pose's normal only on vertices the tool made or
// rewrote; a normal edited by hand away from the tool stays.
void checkAuthoredNormals()
{
	auto fixture = skinFixture();
	const int remote = body(fixture, 16, 8), bind = modelBindPoseFrame(fixture);
	const ModelVec3 custom{.6f, 0, .8f};
	const auto remoteUv = fixture.surfaces[0].texCoords[remote];
	for (int frame : {bind, bind + 1})
		fixture.surfaces[0].frames[frame].normals[remote] = custom;
	updateEditableModelMetadata(&fixture);
	QString error;
	ModelEdit spin;
	spin.kind = ModelEditKind::Spin;
	spin.selection = {0, {}, {0}};
	spin.tool.spinAngle = 90;
	spin.tool.spinSteps = 2;
	spin.tool.point = {-20, 0, 0};
	spin.tool.normal = {0, 1, 0};
	ModelEdit bevel;
	bevel.kind = ModelEditKind::BevelEdges;
	bevel.selection = {0, {}, {}, {modelEdge(body(fixture, 0, 0), body(fixture, -8, 8))}};
	bevel.tool.bevelWidth = .25;
	for (const auto &[name, edit] : {qMakePair("spin", spin), qMakePair("bevel edges", bevel)})
	{
		ModelDocument document;
		ModelEditReport report;
		if (!expect(document.setMesh(fixture, &error) && document.edit(edit, &error, {}, &report), "an authored tool applies beside a hand-edited normal"))
		{
			std::cerr << "  " << name << ": " << error.toStdString() << '\n';
			continue;
		}
		const auto &after = document.mesh();
		const auto &surface = after.surfaces[0];
		const int kept = findSkinVertex(after, 0, {16, 8, 0}, remoteUv);
		bool untouched = kept >= 0;
		for (int frame : {bind, bind + 1})
			untouched &= kept >= 0 && surface.frames[frame].normals[kept].x == custom.x && surface.frames[frame].normals[kept].y == custom.y &&
						 surface.frames[frame].normals[kept].z == custom.z;
		if (!expect(untouched && report.skin.value(0).authored, "a normal edited by hand away from the tool keeps its value"))
			std::cerr << "  " << name << '\n';
		ModelSkinningWork work;
		bool follows = work.prepare(after, &error);
		for (int frame = 0; follows && frame < after.frames.size(); ++frame)
		{
			ModelFrameGeometry skinned;
			follows &= skinModelSurface(surface, work.pose(after, frame), &skinned);
			for (int v = 0; follows && v < surface.vertexCount; ++v)
				if (v != kept && nearPoint(skinned.positions[v], surface.frames[frame].positions[v], 1e-3))
					follows &= nearPoint(skinned.normals[v], surface.frames[frame].normals[v], 1e-4);
		}
		if (!expect(follows, "the vertices the tool made or rewrote take the skin's normal in retained poses"))
			std::cerr << "  " << name << '\n';
	}
}

// The legacy fixture without a retained bind frame: only the clip's poses.
ModelMesh withoutBindFrame()
{
	auto mesh = skinFixture();
	const int bind = modelBindPoseFrame(mesh);
	mesh.frames.removeAt(bind);
	for (auto &surface : mesh.surfaces)
		surface.frames.removeAt(bind);
	mesh.skeleton.bakedClipForFrame.removeAt(bind);
	mesh.animations.clear();
	mesh.tags.removeIf([&](const ModelTag &tag) { return tag.frameIndex == bind; });
	for (auto &tag : mesh.tags)
		if (tag.frameIndex > bind)
			--tag.frameIndex;
	updateEditableModelMetadata(&mesh);
	return mesh;
}

// Nothing is guessed: surfaces without a record, records that point nowhere,
// and rebinding without a bind frame are refused.
void checkStrictProvenance()
{
	const auto fixture = skinFixture();
	QString error;
	QMap<int, ModelSkinProvenance> report;
	{
		// A tool that permuted a skinned surface without the origin helpers,
		// keeping the vertex count and the old influence arrays.
		auto after = fixture;
		seedModelVertexOrigins(&after);
		auto &surface = after.surfaces[0];
		std::swap(surface.texCoords[1], surface.texCoords[2]);
		for (auto &frame : surface.frames)
		{
			std::swap(frame.positions[1], frame.positions[2]);
			std::swap(frame.normals[1], frame.normals[2]);
		}
		for (auto &t : surface.triangles)
			for (int *corner : {&t.a, &t.b, &t.c})
				*corner = *corner == 1 ? 2 : *corner == 2 ? 1 : *corner;
		surface.editOrigins = {};
		expect(!reconcileModelSkinningFromOrigins(fixture, &after, {}, &report, &error) && error.contains("without a record"),
			   "a skinned surface rebuilt without origins is refused, even at the same vertex count");
	}
	{
		auto after = fixture;
		seedModelVertexOrigins(&after);
		after.surfaces[0].editOrigins.flags.removeLast();
		expect(!reconcileModelSkinningFromOrigins(fixture, &after, {}, &report, &error) && error.contains("without a record"),
			   "origins that no longer cover every vertex are refused");
	}
	{
		auto after = fixture;
		seedModelVertexOrigins(&after);
		after.surfaces[0].editOrigins.sources[0].surface = 99;
		expect(!reconcileModelSkinningFromOrigins(fixture, &after, {}, &report, &error) && error.contains("lost track"),
			   "an origin that names no vertex of the model is refused");
	}
	{
		auto after = fixture;
		seedModelVertexOrigins(&after);
		markModelSurfaceOriginsAuthored(&after.surfaces[0]);
		expect(reconcileModelSkinningFromOrigins(fixture, &after, {}, &report, &error) && report.value(0).authored &&
				   skinBytes(after) == skinBytes(fixture),
			   "influences a tool marked as its own stand");
	}
	const auto legacy = withoutBindFrame();
	if (!expect(modelBindPoseFrame(legacy) < 0 && legacy.frames.size() == 2, "the legacy fixture has no retained bind frame"))
		return;
	ModelEdit extrude;
	extrude.kind = ModelEditKind::Extrude;
	extrude.selection = {3, {}, {1, 2, 3, 4}};
	extrude.translation = {0, 0, 2};
	ModelEdit merge;
	merge.kind = ModelEditKind::MergeVertices;
	merge.selection = {0, {body(fixture, -16, 0), body(fixture, -8, 0)}, {}};
	merge.tool.merge = ModelMergeMode::Centre;
	for (const auto &[name, edit] :
		{qMakePair("moved copies", extrude), qMakePair("mirrored vertices", symmetrizeEdit({{"arm.L", "arm.R"}})), qMakePair("merged vertices", merge)})
	{
		ModelDocument document;
		const bool opened = document.setMesh(legacy, &error);
		const auto bytes = skinBytes(document.mesh());
		if (!expect(opened && !document.edit(edit, &error) && error.contains("retained bind frame") && skinBytes(document.mesh()) == bytes &&
						!document.canUndo(),
				"rebinding without a retained bind frame is refused without changing the document"))
			std::cerr << "  " << name << ": " << error.toStdString() << '\n';
	}
	ModelEdit remove;
	remove.kind = ModelEditKind::DeleteFaces;
	remove.selection = {0, {}, {0}};
	ModelDocument document;
	ModelEditReport edited;
	expect(document.setMesh(legacy, &error) && document.edit(remove, &error, {}, &edited) &&
			   edited.skin.value(0).kept == document.mesh().surfaces[0].vertexCount,
		   "edits that only keep vertices still apply without a bind frame");
}

// Blends can give a vertex more joints than the surface had; the report
// counts them so formats with a lower influence limit are not a surprise.
void checkInfluenceGrowth()
{
	const auto fixture = skinFixture();
	int limit = 0;
	for (int count : fixture.surfaces[0].skinning.count)
		limit = std::max(limit, count);
	QString error;
	ModelDocument document;
	ModelEditReport report;
	ModelEdit subdivide;
	subdivide.kind = ModelEditKind::Subdivide;
	subdivide.selection = {0, {}, {4, 5}};
	if (!expect(limit == 2 && document.setMesh(fixture, &error) && document.edit(subdivide, &error, {}, &report), "subdivide applies"))
		return;
	int wider = 0, widest = 0;
	for (int count : document.mesh().surfaces[0].skinning.count)
	{
		wider += count > limit;
		widest = std::max(widest, count);
	}
	const auto entry = report.skin.value(0);
	if (!expect(wider > 0 && entry.widened == wider && entry.widest == widest && entry.previousLimit == limit,
			"the report counts blended vertices with more joints than the surface had"))
		std::cerr << "  widened " << entry.widened << " of " << wider << ", widest " << entry.widest << ", previously " << entry.previousLimit << '\n';
	const auto json = modelSkinProvenanceJson(document.mesh(), report.skin);
	expect(modelSkinProvenanceSummary(report.skin).contains("more joint influences") && json.value("widened").toInt() == wider &&
			   json.value("surfaces").toArray().first().toObject().value("previousLimit").toInt() == limit,
		   "the summary and the JSON name them with the previous limit");
	// The fixture's per-joint offsets need a rebind before reducing, as the
	// reduction itself says.
	ModelEdit rebind;
	rebind.kind = ModelEditKind::RebindSkin;
	rebind.selection = {0, {}, {}};
	ModelEdit prune;
	prune.kind = ModelEditKind::PruneSkinWeights;
	prune.selection = {0, {}, {}};
	prune.skin.maxInfluences = limit;
	bool pruned = document.edit(rebind, &error) && document.edit(prune, &error);
	for (int count : document.mesh().surfaces[0].skinning.count)
		pruned &= count <= limit;
	if (!expect(pruned, "Reduce and normalise weights limits them again"))
		std::cerr << "  " << error.toStdString() << '\n';
	ModelEdit split;
	split.kind = ModelEditKind::SplitEdges;
	split.selection = {0, {}, {}, {modelEdge(body(fixture, -16, 0), body(fixture, -8, 0))}};
	ModelDocument same;
	expect(same.setMesh(fixture, &error) && same.edit(split, &error, {}, &report) && report.skin.value(0).widened == 0 &&
			   report.skin.value(0).interpolated == 1,
		   "a blend of the same joints is not counted");
}

void checkCancellation()
{
	const auto fixture = skinFixture();
	const auto bytes = skinBytes(fixture);
	bool unchanged = true, cancelledOnce = false;
	for (int limit : {1, 40, 400, 4000, 40000})
	{
		auto mesh = fixture;
		int calls = 0;
		ModelWorkControl control;
		control.cancelled = [&] { return ++calls > limit; };
		QString error;
		ModelSelection selection;
		ModelEditReport report;
		if (!applyModelEdit(&mesh, symmetrizeEdit({{"arm.L", "arm.R"}}), &selection, &error, control, &report))
		{
			cancelledOnce = true;
			unchanged &= skinBytes(mesh) == bytes;
		}
	}
	expect(cancelledOnce && unchanged, "a cancelled skinned edit leaves the input byte for byte");
}

// The editable vertex limit with a skeleton: time an edit that keeps every
// vertex in place and one that compacts them, both reconciling all 65,536.
void checkScale()
{
	auto mesh = maximumEditableGrid();
	auto &surface = mesh.surfaces[0];
	surface.frames.resize(1);
	mesh.frames.resize(1);
	mesh.frames[0].name = QStringLiteral("base");
	mesh.skeleton.joints = {{QStringLiteral("root"), -1, modelJointIdentity()}, {QStringLiteral("tip"), 0, modelJointMatrix({}, {128, 0, 0})}};
	ModelSkeletalClip clip;
	clip.name = QStringLiteral("sway");
	for (int f = 0; f < 15; ++f)
		clip.frames.append({modelJointIdentity(), modelJointMatrix({}, {128, float(f), 0})});
	mesh.skeleton.clips = {clip};
	QVector<int> joints;
	QVector<float> weights;
	for (const auto &p : surface.frames[0].positions)
	{
		const float tip = std::clamp((p.x + 128) / 255, 0.0f, 1.0f);
		joints << 0 << 1;
		weights << 1 - tip << tip;
	}
	QString error;
	if (!expect(modelSkinningFromBindPose(mesh.skeleton, surface.frames[0].positions, surface.frames[0].normals, joints, weights, 2,
					&surface.skinning, &error) &&
				   bakeModelSkeleton(&mesh, {}, &error) && mesh.frames.size() == 16,
			"the maximum grid binds and bakes 16 poses"))
	{
		std::cerr << "  " << error.toStdString() << '\n';
		return;
	}
	updateEditableModelMetadata(&mesh);
	ModelEdit move;
	move.kind = ModelEditKind::Transform;
	move.selection = {0, {0}, {}};
	move.translation = {0, 0, 1};
	ModelEdit remove;
	remove.kind = ModelEditKind::DeleteFaces;
	remove.selection = {0, {}, {0, 1}};
	const int budget = qEnvironmentVariableIntValue("VIBESTUDIO_MODELLER_MAX_SKIN_RECONCILE_MS");
	for (const auto &[name, edit] : {qMakePair("transform", move), qMakePair("delete faces", remove)})
	{
		auto edited = mesh;
		ModelSelection selection;
		ModelEditReport report;
		QElapsedTimer timer;
		timer.start();
		const bool applied = applyModelEdit(&edited, edit, &selection, &error, {}, &report);
		const auto milliseconds = timer.nsecsElapsed() / 1e6;
		std::cout << "skinned maximum grid " << name << ": " << milliseconds << " ms\n";
		expect(applied && report.skin.value(0).bound == 0 && report.skin.value(0).kept >= 65536 - 2,
			   "edits at the vertex limit reconcile every vertex from provenance");
		if (budget > 0)
			expect(milliseconds <= budget, "configured skin reconciliation budget");
	}
}

QJsonObject runCli(const QString &program, const QStringList &arguments, int *exitCode, QString *output)
{
	QProcess process;
	process.start(program, arguments);
	const bool finished = process.waitForStarted(10000) && process.waitForFinished(60000);
	*exitCode = finished ? process.exitCode() : -1;
	const auto out = process.readAllStandardOutput();
	*output = QString::fromUtf8(out + process.readAllStandardError());
	return QJsonDocument::fromJson(out).object();
}

void checkCli(const QString &program)
{
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	QTemporaryDir temporary(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("skin-provenance-XXXXXX"));
	if (!expect(temporary.isValid(), "temporary directory"))
		return;
	const QDir dir(temporary.path());
	const auto settings = dir.filePath("settings.ini");
	const auto source = dir.filePath("fixture.mesh.json");
	ModelDocument document;
	QString error;
	if (!expect(document.setMesh(skinFixture(), &error) && document.save(source, false, &error), "the fixture saves"))
		return;
	const auto common = QStringList{"--cli", "model", "tool", source, "--json", "--settings-file", settings};
	int code = 0;
	QString text;
	auto json = runCli(program,
		common + QStringList{"--tool", "symmetrize", "--faces", "all", "--axis", "x", "--direction", "positive", "--joint-map", "arm.L=arm.R",
							 "--output", dir.filePath("mapped.mesh.json")},
		&code, &text);
	expect(code == 0 && json.value("skinProvenance").toObject().value("mirrored").toInt() == 5 &&
			   json.value("jointMap").toObject().value("arm.R").toString() == "arm.L",
		   "CLI symmetrize reports mirrored weights and the joint map it used");
	if (code != 0)
		std::cerr << text.toStdString() << '\n';
	ModelDocument service, mapped;
	expect(service.load(source, &error) && service.edit(symmetrizeEdit({{"arm.L", "arm.R"}}), &error) &&
			   mapped.load(dir.filePath("mapped.mesh.json"), &error) && skinBytes(mapped.mesh()) == skinBytes(service.mesh()),
		   "the CLI result equals the document service result");
	json = runCli(program,
		common + QStringList{"--tool", "symmetrize", "--faces", "all", "--axis", "x", "--joint-map", "detect", "--output", dir.filePath("detected.mesh.json")},
		&code, &text);
	ModelDocument detected;
	expect(code == 0 && detected.load(dir.filePath("detected.mesh.json"), &error) && skinBytes(detected.mesh()) == skinBytes(service.mesh()),
		   "--joint-map detect finds the same pairs");
	// Across Y the arms sit on the plane, so the detected pair does not apply.
	json = runCli(program,
		common + QStringList{"--tool", "symmetrize", "--faces", "all", "--axis", "y", "--joint-map", "detect", "--output", dir.filePath("across-y.mesh.json")},
		&code, &text);
	auto acrossY = symmetrizeEdit();
	acrossY.tool.axis = 1;
	ModelDocument yService, yCli;
	expect(code == 0 && json.value("jointMap").toObject().isEmpty() && yService.load(source, &error) && yService.edit(acrossY, &error) &&
			   yCli.load(dir.filePath("across-y.mesh.json"), &error) && skinBytes(yCli.mesh()) == skinBytes(yService.mesh()),
		   "--joint-map detect keeps only pairs that mirror across the chosen plane");
	if (code != 0)
		std::cerr << text.toStdString() << '\n';
	// The surface listing says which surfaces follow the skeleton.
	QProcess listing;
	listing.start(program, {"--cli", "model", "surfaces", source, "--settings-file", settings});
	listing.waitForFinished(60000);
	bool marked = false, unmarked = false;
	for (const auto &line : QString::fromUtf8(listing.readAllStandardOutput()).split(QLatin1Char('\n')))
	{
		marked |= line.contains(": body ") && line.contains(" skinned ") && !line.contains("not skinned");
		unmarked |= line.contains(": prop ") && line.contains("not skinned");
	}
	expect(marked && unmarked, "the model surfaces listing marks skinned and unskinned surfaces");
	runCli(program, common + QStringList{"--tool", "symmetrize", "--faces", "all", "--axis", "x", "--output", dir.filePath("refused.mesh.json")}, &code,
		&text);
	expect(code == 4 && text.contains("arm.L") && !QFile::exists(dir.filePath("refused.mesh.json")),
		   "CLI symmetrize without a map is refused with the joints to map");
	ModelDocument original;
	original.load(source, &error);
	const int a = body(original.mesh(), 0, 0), b = body(original.mesh(), 0, 8);
	json = runCli(program,
		common + QStringList{"--tool", "loop-cut", "--edges", QStringLiteral("%1:%2").arg(std::min(a, b)).arg(std::max(a, b)), "--output",
							 dir.filePath("loop.mesh.json")},
		&code, &text);
	expect(code == 0 && json.value("skinProvenance").toObject().value("interpolated").toInt() > 0 &&
			   json.value("skinProvenance").toObject().value("bound").toInt() == 0 && text.isEmpty() == false,
		   "CLI loop cut reports interpolated weights");
	QProcess plain;
	plain.start(program, {"--cli", "model", "tool", source, "--settings-file", settings, "--tool", "loop-cut", "--edges",
						  QStringLiteral("%1:%2").arg(std::min(a, b)).arg(std::max(a, b)), "--output", dir.filePath("loop-text.mesh.json")});
	plain.waitForFinished(60000);
	expect(QString::fromUtf8(plain.readAllStandardOutput()).contains("interpolated"), "CLI text output names interpolated weights");
	json = runCli(program,
		{"--cli", "model", "surfaces", source, "--operation", "separate", "--surface", "0", "--faces", "6,7", "--name", "arm", "--output",
		 dir.filePath("separate.mesh.json"), "--json", "--settings-file", settings},
		&code, &text);
	const auto surfaces = json.value("skinProvenance").toObject().value("surfaces").toArray();
	bool separated = false;
	for (const auto &entry : surfaces)
		separated |= entry.toObject().value("name") == "arm" && entry.toObject().value("kept").toInt() == 4;
	expect(code == 0 && separated && json.value("skinProvenance").toObject().value("bound").toInt() == 0,
		   "CLI surface separation reports the separated surface's kept weights");
	if (code != 0)
		std::cerr << text.toStdString() << '\n';
	// Add Primitive: refused without a binding, then the same rigid binding as the service.
	const auto add = common + QStringList{"--tool", "add", "--primitive", "cube", "--at", "0,4,8", "--size", "4,4,4"};
	runCli(program, add + QStringList{"--output", dir.filePath("unbound.mesh.json")}, &code, &text);
	expect(code == 4 && text.contains("body") && !QFile::exists(dir.filePath("unbound.mesh.json")),
		   "CLI add on a skinned surface without a binding is refused");
	json = runCli(program, add + QStringList{"--bind-joint", "head", "--output", dir.filePath("bound.mesh.json")}, &code, &text);
	ModelDocument bound, expected;
	auto rigid = primitiveEdit(0);
	rigid.tool.newGeometryBinding = ModelNewGeometryBinding::Joint;
	rigid.tool.bindJoint = QStringLiteral("head");
	expect(code == 0 && json.value("binding").toObject().value("method") == "joint" && json.value("skinProvenance").toObject().value("bound").toInt() > 0 &&
			   bound.load(dir.filePath("bound.mesh.json"), &error) && expected.load(source, &error) && expected.edit(rigid, &error) &&
			   skinBytes(bound.mesh()) == skinBytes(expected.mesh()),
		   "CLI --bind-joint equals the document service result");
	if (code != 0)
		std::cerr << text.toStdString() << '\n';
	json = runCli(program, add + QStringList{"--bind", "nearest-bones", "--output", dir.filePath("nearest.mesh.json")}, &code, &text);
	expect(code == 0 && json.value("binding").toObject().value("method") == "nearest-bones", "CLI --bind nearest-bones binds the new vertices");
	json = runCli(program, add + QStringList{"--new-surface", "helmet", "--output", dir.filePath("helmet.mesh.json")}, &code, &text);
	expect(code == 0 && json.value("surface").toObject().value("name") == "helmet" && json.value("binding").toObject().value("skinned") == false,
		   "CLI --new-surface adds an unskinned surface and reports it");
	runCli(program, add + QStringList{"--bind", "closest", "--output", dir.filePath("bad.mesh.json")}, &code, &text);
	expect(code == 2 && text.contains("nearest-bones"), "an unknown binding method is a usage error");
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	checkClassification();
	checkJointMaps();
	checkBlend();
	checkInterpolatingCuts();
	checkCoincidentSurvivors();
	checkSurfaces();
	checkSymmetrize();
	checkMirrorPlanes();
	checkNewGeometry();
	checkAuthoredNormals();
	checkStrictProvenance();
	checkInfluenceGrowth();
	checkCancellation();
	checkScale();
	if (argc > 1)
		checkCli(QString::fromLocal8Bit(argv[1]));
	std::cout << checks << " skin provenance checks, " << failures << " failures\n";
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
