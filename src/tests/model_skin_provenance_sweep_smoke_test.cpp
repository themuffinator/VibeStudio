// A sweep over every topology, UV, surface and frame edit on a skinned
// fixture whose coincident seam copies carry different weights
// (core/model_vertex_origins.h). For each edit:
// - every vertex that matches an original keeps that vertex's weights, and
//   anything else is counted in the edit report (interpolated, mirrored,
//   rebound, merged or bound), so nothing is guessed silently;
// - the coincident pair the edit does not touch keeps its distinct weights,
//   joints and offsets bit for bit;
// - retained skeletal poses still equal the skin and the frame-to-clip map is
//   unchanged, so weight painting, weight transfer and binding still accept
//   the result;
// - undo restores the input byte for byte and the edit is deterministic.
// Frame edits refuse the bind frame and clip frames and keep every other
// frame's clip mapping. Every edit kind is swept, or named with the reason
// it keeps vertex data untouched or writes influences itself.

#include "core/model_document.h"
#include "core/model_mesh_tools.h"
#include "core/model_rig.h"
#include "core/model_skeletal_edit.h"
#include "core/model_skinning_tools.h"
#include "core/model_vertex_origins.h"
#include "tests/model_skin_provenance_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;

namespace
{
int checks = 0, failures = 0;
bool expect(bool ok, const QString &name)
{
	++checks;
	if (!ok)
	{
		++failures;
		std::cerr << "FAIL: " << name.toStdString() << '\n';
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

ModelEdit kindEdit(ModelEditKind kind, const ModelSelection &selection = {0, {}, {}})
{
	ModelEdit edit;
	edit.kind = kind;
	edit.selection = selection;
	return edit;
}

// What the sweep requires of an edit's result.
enum class Expect
{
	// Retained skeletal poses follow the skin; later skin edits are accepted.
	Follow,
	// A position tool: every weight is kept by index, and the moved positions
	// become the tool's own geometry in the edited frames.
	Positions,
	// The edit is refused and the document is unchanged.
	Refused,
};

struct Case
{
	QString name;
	ModelEdit edit;
	Expect result = Expect::Follow;
	// The (8, 8) seam pair lies outside the edit and must stay exact.
	bool remotePair = true;
	// Expected text in a refusal.
	QString refusal;
	// Empty uses the plain fixture.
	ModelMesh fixture{};
};

// Influences equal bit for bit, wherever the vertex now lives.
struct Located
{
	int surface = -1, vertex = -1;
};
Located locate(const ModelMesh &mesh, ModelVec3 position, ModelTexCoord uv)
{
	for (int s = 0; s < mesh.surfaces.size(); ++s)
	{
		if (mesh.surfaces[s].skinning.isEmpty())
			continue;
		const int v = findSkinVertex(mesh, s, position, uv);
		if (v >= 0)
			return {s, v};
	}
	return {};
}

bool remotePairExact(const ModelMesh &fixture, const ModelMesh &after)
{
	// Variants keep the body's vertex order, though some move its UVs.
	static const auto plain = skinFixture();
	const auto &source = fixture.surfaces[0];
	for (const bool seam : {false, true})
	{
		const int original = body(plain, 8, 8, seam);
		if (original < 0)
			return false;
		const auto where = locate(after, bindPosition(fixture, 0, original), source.texCoords[original]);
		if (where.surface < 0 || !sameInfluences(source, original, after.surfaces[where.surface], where.vertex, false))
			return false;
	}
	return true;
}

// Vertices that sit where an original vertex sat, with its UV, but carry
// other weights. Only merged and mirrored vertices may.
int mismatchedOriginals(const ModelMesh &fixture, const ModelMesh &after, QString *detail)
{
	int mismatched = 0;
	for (int s = 0; s < after.surfaces.size(); ++s)
	{
		const auto &surface = after.surfaces[s];
		if (surface.skinning.isEmpty())
			continue;
		for (int v = 0; v < surface.vertexCount; ++v)
		{
			const auto where = locate(fixture, bindPosition(after, s, v), surface.texCoords[v]);
			if (where.surface < 0 || sameSkinWeights(skinWeightsOf(surface, v), skinWeightsOf(fixture.surfaces[where.surface], where.vertex)))
				continue;
			++mismatched;
			*detail += QStringLiteral(" %1:%2").arg(s).arg(v);
		}
	}
	return mismatched;
}

// Every retained skeletal pose (the bind frame and clip frames) equals the
// skin of every skinned surface. Independent frames are not compared.
QVector<QVector<ModelJointMatrix>> retainedPoses(const ModelMesh &mesh);
bool retainedFramesFollow(const ModelMesh &mesh, QString *error)
{
	const auto poses = retainedPoses(mesh);
	for (const auto &surface : mesh.surfaces)
	{
		if (surface.skinning.isEmpty())
			continue;
		for (int frame = 0; frame < mesh.frames.size(); ++frame)
		{
			if (poses[frame].isEmpty())
				continue;
			ModelFrameGeometry skinned;
			if (!skinModelSurface(surface, poses[frame], &skinned, error))
				return false;
			for (int v = 0; v < surface.vertexCount; ++v)
				if (!nearPoint(skinned.positions[v], surface.frames[frame].positions[v], 1e-3))
				{
					*error = QStringLiteral("surface %1 vertex %2 frame %3").arg(surface.name).arg(v).arg(frame);
					return false;
				}
		}
	}
	return true;
}

int surfaceNamed(const ModelMesh &mesh, const QString &name)
{
	for (int s = 0; s < mesh.surfaces.size(); ++s)
		if (mesh.surfaces[s].name == name)
			return s;
	return -1;
}

// A vertex with several influences that all share its bind position, as
// painting needs.
int paintableVertex(const ModelMesh &mesh, int s)
{
	const auto &surface = mesh.surfaces[s];
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		bool canonical = surface.skinning.count[v] > 1;
		for (int i = surface.skinning.first[v]; canonical && i < surface.skinning.first[v] + surface.skinning.count[v]; ++i)
		{
			const auto &influence = surface.skinning.influences[i];
			canonical &= influence.weight <= 0 ||
						 nearPoint(modelJointTransformPoint(mesh.skeleton.joints[influence.joint].bind, influence.offset), bindPosition(mesh, s, v));
		}
		if (canonical)
			return v;
	}
	return -1;
}

// Weight painting, weight transfer and binding accept the result, as they
// accepted the fixture.
void laterSkinEdits(const QString &name, const ModelMesh &after)
{
	const int s = surfaceNamed(after, QStringLiteral("body")), head = surfaceNamed(after, QStringLiteral("head"));
	if (s < 0 || after.surfaces[s].skinning.isEmpty())
		return;
	QString error;
	if (const int v = paintableVertex(after, s); v >= 0)
	{
		auto painted = after;
		ModelEdit paint = kindEdit(ModelEditKind::PaintSkinWeights, {s, {}, {}});
		ModelWeightStroke stroke;
		stroke.surface = s;
		stroke.joint = QStringLiteral("spine");
		stroke.value = .5;
		stroke.strength = .5;
		stroke.samples = {{v, 1}};
		paint.weightStrokes = {stroke};
		if (!expect(applyModelEdit(&painted, paint, nullptr, &error), name + QStringLiteral(": weight painting accepts the result")))
			std::cerr << "  " << error.toStdString() << '\n';
	}
	if (const int v = findSkinVertex(after, s, {0, 0, 0}, {.5f, 0}); v >= 0 && head >= 0)
	{
		auto transferred = after;
		ModelEdit transfer = kindEdit(ModelEditKind::TransferSkinWeights, {s, {v}, {}});
		transfer.weightTransfer.sourceSurface = head;
		transfer.weightTransfer.maximumDistance = 100;
		if (!expect(applyModelEdit(&transferred, transfer, nullptr, &error), name + QStringLiteral(": weight transfer accepts the result")))
			std::cerr << "  " << error.toStdString() << '\n';
	}
	auto bound = after;
	ModelEdit bind = kindEdit(ModelEditKind::BindSkin, {s, {}, {}});
	for (int v = 0; v < after.surfaces[s].vertexCount; ++v)
		bind.selection.vertices.insert(v);
	bind.skin.method = ModelSkinBindingMethod::Rigid;
	bind.skin.joints = {QStringLiteral("spine")};
	bind.skin.replaceExisting = true;
	if (!expect(applyModelEdit(&bound, bind, nullptr, &error), name + QStringLiteral(": binding accepts the result")))
		std::cerr << "  " << error.toStdString() << '\n';
}

QSet<int> swept;

void runCase(const Case &item)
{
	const auto fixture = item.fixture.surfaces.isEmpty() ? skinFixture() : item.fixture;
	swept.insert(int(item.edit.kind));
	if (qEnvironmentVariableIsSet("VIBESTUDIO_SWEEP_TRACE"))
		std::cout << "sweep: " << item.name.toStdString() << std::endl;
	QString error;
	ModelDocument document;
	if (!expect(document.setMesh(fixture, &error), item.name + QStringLiteral(": the fixture opens")))
	{
		std::cerr << "  " << error.toStdString() << '\n';
		return;
	}
	const auto opened = skinBytes(document.mesh());
	ModelEditReport report;
	const bool applied = document.edit(item.edit, &error, {}, &report);
	if (item.result == Expect::Refused)
	{
		if (!expect(!applied && error.contains(item.refusal) && skinBytes(document.mesh()) == opened && !document.canUndo(),
				item.name + QStringLiteral(": refused without changing the document")))
			std::cerr << "  " << error.toStdString() << '\n';
		return;
	}
	if (!expect(applied, item.name + QStringLiteral(": applies to a skinned surface")))
	{
		std::cerr << "  " << error.toStdString() << '\n';
		return;
	}
	const auto after = document.mesh();
	// Every skinned surface is reported, and its counts cover every vertex.
	bool accounted = true;
	ModelSkinProvenance total;
	for (int s = 0; s < after.surfaces.size(); ++s)
	{
		if (after.surfaces[s].skinning.isEmpty())
			continue;
		const auto entry = report.skin.value(s);
		accounted &= report.skin.contains(s) &&
					 (entry.authored ||
						 entry.kept + entry.interpolated + entry.mirrored + entry.rebound + entry.merged + entry.bound == after.surfaces[s].vertexCount);
		total.merged += entry.merged;
		total.mirrored += entry.mirrored;
	}
	expect(accounted, item.name + QStringLiteral(": the report accounts for every skinned vertex"));
	QString detail;
	const int mismatched = mismatchedOriginals(fixture, after, &detail);
	if (!expect(mismatched <= total.merged + total.mirrored,
			item.name + QStringLiteral(": vertices at original positions keep the original weights unless merged or mirrored")))
		std::cerr << "  mismatched" << detail.toStdString() << '\n';
	if (item.remotePair)
		expect(remotePairExact(fixture, after), item.name + QStringLiteral(": the untouched coincident pair keeps its distinct weights"));
	if (item.result == Expect::Positions)
	{
		bool kept = true;
		for (int s = 0; s < after.surfaces.size(); ++s)
			for (int v = 0; !after.surfaces[s].skinning.isEmpty() && v < after.surfaces[s].vertexCount; ++v)
				kept &= sameInfluences(fixture.surfaces[s], v, after.surfaces[s], v, false);
		expect(kept && report.skin.value(0).kept == after.surfaces[0].vertexCount,
			   item.name + QStringLiteral(": a position tool keeps every influence by index"));
	}
	else
	{
		if (!expect(posesFollowSkeleton(after, &error), item.name + QStringLiteral(": retained skeletal poses follow the skin")))
		{
			QString positions;
			const bool placed = retainedFramesFollow(after, &positions);
			std::cerr << "  " << error.toStdString() << (placed ? " (positions agree; normals differ)" : " (") << positions.toStdString()
					  << (placed ? "" : ")") << '\n';
		}
		expect(after.skeleton.bakedClipForFrame == fixture.skeleton.bakedClipForFrame,
			   item.name + QStringLiteral(": every frame keeps its clip mapping"));
		laterSkinEdits(item.name, after);
	}
	ModelDocument again;
	expect(again.setMesh(fixture, &error) && again.edit(item.edit, &error) && skinBytes(again.mesh()) == skinBytes(after),
		   item.name + QStringLiteral(": the edit is deterministic"));
	expect(document.undo() && skinBytes(document.mesh()) == opened && document.redo() && skinBytes(document.mesh()) == skinBytes(after),
		   item.name + QStringLiteral(": undo and redo are byte-exact"));
}

// The joint pose of every retained skeletal frame (the bind frame and clip
// frames, in clip order), or empty for independent frames.
QVector<QVector<ModelJointMatrix>> retainedPoses(const ModelMesh &mesh)
{
	QVector<QVector<ModelJointMatrix>> poses(mesh.frames.size());
	QVector<int> counts(mesh.skeleton.clips.size(), 0);
	for (int frame = 0; frame < mesh.frames.size(); ++frame)
	{
		const int clip = mesh.skeleton.bakedClipForFrame.value(frame, -2);
		if (clip == -1)
			poses[frame] = modelSkeletonBindPose(mesh.skeleton);
		else if (clip >= 0 && clip < mesh.skeleton.clips.size())
			poses[frame] = mesh.skeleton.clips[clip].frames.value(counts[clip]++);
	}
	return poses;
}

// Poses every retained skeletal frame of every skinned surface from its
// influences, after a fixture's influences were changed by hand.
void repose(ModelMesh *mesh)
{
	const auto poses = retainedPoses(*mesh);
	for (auto &surface : mesh->surfaces)
		for (int frame = 0; !surface.skinning.isEmpty() && frame < surface.frames.size(); ++frame)
		{
			ModelFrameGeometry posed;
			if (!poses[frame].isEmpty() && skinModelSurface(surface, poses[frame], &posed))
				surface.frames[frame] = std::move(posed);
		}
	updateEditableModelMetadata(mesh);
}

// Moves fixture vertices in the bind pose and binds them again with the same
// weights; the other vertices keep their influences exactly.
void moveFixtureVertices(ModelMesh *mesh, int s, const QHash<int, ModelVec3> &moves, ModelVec3 normal = {0, 0, 1})
{
	auto &surface = mesh->surfaces[s];
	const int bind = std::max(0, modelBindPoseFrame(*mesh));
	ModelSurfaceSkinning skinning;
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		const auto &old = surface.skinning;
		if (!moves.contains(v))
		{
			skinning.first.append(skinning.influences.size());
			skinning.count.append(old.count[v]);
			skinning.influences += old.influences.mid(old.first[v], old.count[v]);
			continue;
		}
		QVector<QPair<int, float>> weights;
		for (int i = old.first[v]; i < old.first[v] + old.count[v]; ++i)
			weights.append({old.influences[i].joint, old.influences[i].weight});
		surface.frames[bind].positions[v] = moves.value(v);
		surface.frames[bind].normals[v] = normal;
		appendFixtureSkin(skinning, mesh->skeleton, moves.value(v), normal, weights, v + 1);
	}
	surface.skinning = std::move(skinning);
	repose(mesh);
}

// The fixture with the seam copy at (-8, 8) given its partner's UV, so the
// coincident pair differs only in its weights.
ModelMesh sameUvPair()
{
	auto mesh = skinFixture();
	auto &surface = mesh.surfaces[0];
	surface.texCoords[body(mesh, -8, 8, true)] = surface.texCoords[body(mesh, -8, 8)];
	updateEditableModelMetadata(&mesh);
	return mesh;
}

// Only the bind frame, so vertices with different weights can coincide in
// every retained pose (Weld by Distance needs that).
ModelMesh bindOnly(ModelMesh mesh)
{
	const int bind = modelBindPoseFrame(mesh);
	mesh.frames = {mesh.frames[bind]};
	for (auto &surface : mesh.surfaces)
		surface.frames = {surface.frames[bind]};
	mesh.skeleton.bakedClipForFrame = {-1};
	mesh.animations.clear();
	mesh.tags.removeIf([&](const ModelTag &tag) { return tag.frameIndex != bind; });
	for (auto &tag : mesh.tags)
		tag.frameIndex = 0;
	updateEditableModelMetadata(&mesh);
	return mesh;
}

// A seam copy given its partner's influences exactly.
ModelMesh matchingPair()
{
	const auto plain = skinFixture();
	auto mesh = bindOnly(sameUvPair());
	auto &skinning = mesh.surfaces[0].skinning;
	const int a = body(plain, -8, 8), b = body(plain, -8, 8, true);
	ModelSurfaceSkinning rebuilt;
	for (int v = 0; v < mesh.surfaces[0].vertexCount; ++v)
	{
		const int from = v == b ? a : v;
		rebuilt.first.append(rebuilt.influences.size());
		rebuilt.count.append(skinning.count[from]);
		rebuilt.influences += skinning.influences.mid(skinning.first[from], skinning.count[from]);
	}
	skinning = std::move(rebuilt);
	repose(&mesh);
	return mesh;
}

// The fixture with an unused skinned vertex and a duplicate face for the repairs.
ModelMesh damaged()
{
	auto mesh = skinFixture();
	auto &surface = mesh.surfaces[0];
	surface.triangles.append(surface.triangles[0]);
	surface.texCoords.append({.9f, .9f});
	for (auto &frame : surface.frames)
	{
		frame.positions.append({40, 40, 0});
		frame.normals.append({0, 0, 1});
	}
	surface.skinning.first.append(surface.skinning.influences.size());
	surface.skinning.count.append(1);
	surface.skinning.influences.append({fixtureSpine, 1, {40, 40, 0}, {0, 0, 1}});
	++surface.vertexCount;
	repose(&mesh);
	return mesh;
}

// Face 2 wound the other way, for Orient Faces.
ModelMesh flipped()
{
	auto mesh = skinFixture();
	auto &t = mesh.surfaces[0].triangles[2];
	std::swap(t.b, t.c);
	updateEditableModelMetadata(&mesh);
	return mesh;
}

// A third face on the edge between (0, 0) and (0, 8), for Split Nonmanifold Edges.
ModelMesh nonmanifold()
{
	auto mesh = skinFixture();
	mesh.surfaces[0].triangles.append({body(mesh, 0, 0), body(mesh, 0, 8), body(mesh, -16, 0)});
	updateEditableModelMetadata(&mesh);
	return mesh;
}

// The (-16, 8) corner pulled out so face 0 and 1's diagonal is the long one.
ModelMesh skewed()
{
	auto mesh = skinFixture();
	moveFixtureVertices(&mesh, 0, {{body(mesh, -16, 8), {-24, 12, 0}}});
	return mesh;
}

// The gem's second loose triangle lifted over the first and facing it, so
// the two loops bridge into a prism.
ModelMesh facingLoops()
{
	auto mesh = skinFixture();
	moveFixtureVertices(&mesh, 3, {{7, {-30, 0, 10}}, {8, {-28, 0, 10}}, {9, {-30, 2, 10}}}, {0, 0, -1});
	auto &t = mesh.surfaces[3].triangles[5];
	std::swap(t.b, t.c);
	updateEditableModelMetadata(&mesh);
	return mesh;
}

// The gem's tetrahedron with one face removed, leaving a triangular hole.
ModelMesh openGem()
{
	auto mesh = skinFixture();
	mesh.surfaces[3].triangles.removeAt(3);
	updateEditableModelMetadata(&mesh);
	return mesh;
}

// The body's UVs in the lower-left quarter of the tile, leaving room to pack.
ModelMesh quarterUvs()
{
	auto mesh = skinFixture();
	for (auto &uv : mesh.surfaces[0].texCoords)
		uv = {uv.u * .5f, uv.v * .5f};
	// Other surfaces use other textures, so their UVs are not obstacles.
	for (int s = 1; s < mesh.surfaces.size(); ++s)
		mesh.surfaces[s].skinPaths = {QStringLiteral("models/other%1.tga").arg(s)};
	updateEditableModelMetadata(&mesh);
	return mesh;
}

// The fixture with a UV seam marked on the edge from (-8, 0) to (-8, 8).
ModelMesh seamed()
{
	auto mesh = skinFixture();
	mesh.surfaces[0].uvSeams.insert(modelEdge(body(mesh, -8, 0), body(mesh, -8, 8)));
	updateEditableModelMetadata(&mesh);
	return mesh;
}

void checkSweep()
{
	const auto fixture = skinFixture();
	QString error;
	expect(posesFollowSkeleton(fixture, &error), QStringLiteral("the fixture's poses follow its skeleton"));
	expect(!sameSkinWeights(skinWeightsOf(fixture.surfaces[0], body(fixture, 8, 8)), skinWeightsOf(fixture.surfaces[0], body(fixture, 8, 8, true))) &&
			   !sameSkinWeights(skinWeightsOf(fixture.surfaces[0], body(fixture, -8, 8)), skinWeightsOf(fixture.surfaces[0], body(fixture, -8, 8, true))),
		   QStringLiteral("both coincident seam pairs carry different weights"));
	// Faces 0-3 lie at x <= 0 and include the (-8, 8) pair; the (8, 8) pair
	// belongs to faces 4, 5 and 7.
	const ModelSelection left{0, {}, {0, 1, 2, 3}};
	const auto edge = [&](float ax, float ay, float bx, float by)
	{ return modelEdge(body(fixture, ax, ay), body(fixture, bx, by)); };
	QVector<Case> cases;
	const auto add = [&](const QString &name, ModelEdit edit, Expect result = Expect::Follow, bool remote = true)
	{
		Case item;
		item.name = name;
		item.edit = std::move(edit);
		item.result = result;
		item.remotePair = remote;
		cases.append(std::move(item));
	};
	const auto refuse = [&](const QString &name, ModelEdit edit, const QString &text, const ModelMesh &mesh = {})
	{
		Case item;
		item.name = name;
		item.edit = std::move(edit);
		item.result = Expect::Refused;
		item.refusal = text;
		item.fixture = mesh;
		cases.append(std::move(item));
	};
	// An edit on a fixture variant built for it.
	const auto on = [&](const QString &name, const ModelMesh &mesh, ModelEdit edit)
	{
		Case item;
		item.name = name;
		item.edit = std::move(edit);
		item.fixture = mesh;
		cases.append(std::move(item));
	};
	// Topology from the document service.
	{
		auto e = kindEdit(ModelEditKind::Extrude, {0, {}, {0, 1}});
		e.translation = {0, 0, 2};
		add(QStringLiteral("Extrude"), e);
		e = kindEdit(ModelEditKind::DuplicateFaces, {0, {}, {0, 1}});
		add(QStringLiteral("Duplicate Faces in place"), e);
		e.translation = {0, 0, 3};
		add(QStringLiteral("Duplicate Faces moved"), e);
		add(QStringLiteral("Subdivide"), kindEdit(ModelEditKind::Subdivide, {0, {}, {0, 1}}));
		add(QStringLiteral("Delete Faces"), kindEdit(ModelEditKind::DeleteFaces, {0, {}, {0}}));
		add(QStringLiteral("Flip Faces"), kindEdit(ModelEditKind::FlipFaces, left));
		add(QStringLiteral("Recalculate Normals"), kindEdit(ModelEditKind::RecalculateNormals, left));
		add(QStringLiteral("Split Edges"), kindEdit(ModelEditKind::SplitEdges, {0, {}, {}, {edge(-16, 0, -8, 0)}}));
		// Weld needs the pair to coincide in every retained pose, which
		// differently weighted vertices only do in the bind pose.
		e = kindEdit(ModelEditKind::WeldVertices, {0, {body(fixture, -8, 8), body(fixture, -8, 8, true)}, {}});
		e.preserveSeams = false;
		on(QStringLiteral("Weld by Distance across seams"), bindOnly(skinFixture()), e);
		e.preserveSeams = true;
		refuse(QStringLiteral("Weld by Distance with seams kept"), e, QStringLiteral("different skin weights"), bindOnly(sameUvPair()));
		on(QStringLiteral("Weld by Distance of matching weights"), matchingPair(), e);
		refuse(QStringLiteral("Weld by Distance of posed vertices apart"), e, QStringLiteral("every frame"), sameUvPair());
		e = kindEdit(ModelEditKind::MarkUvSeams, {0, {}, {}, {edge(-8, 0, -8, 8)}});
		add(QStringLiteral("Mark UV Seams"), e);
		e.kind = ModelEditKind::ClearUvSeams;
		on(QStringLiteral("Clear UV Seams"), seamed(), e);
		add(QStringLiteral("Detach UV"), kindEdit(ModelEditKind::DetachUv, {0, {}, {0, 1}}));
		e = kindEdit(ModelEditKind::TransformUv, {0, {}, {0, 1}});
		e.uvOffset = {.25f, 0};
		add(QStringLiteral("Transform UVs"), e);
		e.uvIslands = true;
		e.uvPivotMode = ModelUvPivot::IndividualIslands;
		add(QStringLiteral("Transform UVs per island"), e);
		e = kindEdit(ModelEditKind::ProjectUv, {0, {}, {0, 1}});
		add(QStringLiteral("Project UVs"), e);
		e = kindEdit(ModelEditKind::UnwrapUv, left);
		add(QStringLiteral("Unwrap UVs"), e);
		e.kind = ModelEditKind::PackUv;
		add(QStringLiteral("Pack UVs"), e);
		e = kindEdit(ModelEditKind::PackUvAround, {0, {}, {0, 1}});
		e.uvAtlasPadding = 1;
		on(QStringLiteral("Pack UVs Around"), quarterUvs(), e);
		e = kindEdit(ModelEditKind::SetUvDensity, {0, {}, {0, 1}});
		e.uvDensity.textureSize = {64, 64};
		e.uvDensity.target = 2;
		add(QStringLiteral("Set UV Density"), e);
		e = kindEdit(ModelEditKind::SetMaterial);
		e.text = QStringLiteral("models/other.tga");
		add(QStringLiteral("Set Material"), e);
		e = kindEdit(ModelEditKind::SetMaterialSlots);
		e.materialSlots = {QStringLiteral("models/fixture.tga"), QStringLiteral("models/alt.tga")};
		add(QStringLiteral("Set Material Slots"), e);
		e = kindEdit(ModelEditKind::RenameFrame);
		e.frame = 1;
		e.text = QStringLiteral("wave_start");
		add(QStringLiteral("Rename Frame"), e);
		e = kindEdit(ModelEditKind::AddAnimation);
		e.text = QStringLiteral("wave_again");
		e.rangeFirst = 1;
		e.rangeLast = 2;
		add(QStringLiteral("Add Animation"), e);
		// Repairs rebuild the vertex arrays through their vertex map. The
		// fixture's seam corners already split their fans.
		add(QStringLiteral("Split Disconnected Fans"), kindEdit(ModelEditKind::SplitDisconnectedFans));
		on(QStringLiteral("Remove Duplicate Faces"), damaged(), kindEdit(ModelEditKind::RemoveDuplicateFaces));
		on(QStringLiteral("Remove Unused Vertices"), damaged(), kindEdit(ModelEditKind::RemoveUnusedVertices));
		on(QStringLiteral("Orient Faces"), flipped(), kindEdit(ModelEditKind::OrientFaces));
		on(QStringLiteral("Split Nonmanifold Edges"), nonmanifold(), kindEdit(ModelEditKind::SplitNonmanifoldEdges));
	}
	// Boundary fill and bridge add faces only: the gem's two facing loose
	// triangles bridge into a prism, and its opened tetrahedron fills again.
	{
		// Bridged in the bind pose only: the posed loops would cross.
		on(QStringLiteral("Bridge Boundary Loops"), bindOnly(facingLoops()),
			kindEdit(ModelEditKind::BridgeBoundaryLoops, {3, {}, {}, {modelEdge(0, 1), modelEdge(7, 8)}}));
		on(QStringLiteral("Fill Boundary Loops"), openGem(), kindEdit(ModelEditKind::FillBoundaryLoops, {3, {}, {}, {modelEdge(4, 5)}}));
	}
	// Surfaces.
	{
		auto e = kindEdit(ModelEditKind::SeparateFaces, {0, {}, {0, 1}});
		e.text = QStringLiteral("left arm");
		add(QStringLiteral("Separate Faces"), e);
		e = kindEdit(ModelEditKind::MoveFacesToSurface, {0, {}, {0, 1}});
		e.targetSurface = 1;
		add(QStringLiteral("Move Faces to Surface"), e);
		e = kindEdit(ModelEditKind::DuplicateSurface);
		e.text = QStringLiteral("body copy");
		add(QStringLiteral("Duplicate Surface"), e);
		e = kindEdit(ModelEditKind::DeleteSurface, {1, {}, {}});
		add(QStringLiteral("Delete Surface"), e);
		e = kindEdit(ModelEditKind::JoinSurfaces, {0, {}, {}});
		e.surfaces = {0, 1, 3};
		e.targetSurface = 0;
		add(QStringLiteral("Join Surfaces"), e);
		e = kindEdit(ModelEditKind::RenameSurface);
		e.text = QStringLiteral("torso");
		Case renamed;
		renamed.name = QStringLiteral("Rename Surface");
		renamed.edit = e;
		renamed.remotePair = true;
		cases.append(renamed);
	}
	// Edit-mode mesh tools.
	{
		auto e = kindEdit(ModelEditKind::RotateEdges, {0, {}, {}, {edge(-8, 0, -16, 8)}});
		add(QStringLiteral("Rotate Edges"), e);
		for (const auto mode : {ModelMergeMode::Centre, ModelMergeMode::Point, ModelMergeMode::Collapse})
		{
			e = kindEdit(ModelEditKind::MergeVertices, {0, {body(fixture, -16, 0), body(fixture, -8, 0)}, {}});
			e.tool.merge = mode;
			e.tool.point = {-12, -1, 0};
			add(QStringLiteral("Merge Vertices (mode %1)").arg(int(mode)), e);
		}
		// Not connected by an edge: the coincident results share one weight set.
		e = kindEdit(ModelEditKind::MergeVertices, {0, {body(fixture, -16, 0), body(fixture, 0, 8)}, {}});
		add(QStringLiteral("Merge Vertices not connected"), e);
		add(QStringLiteral("Dissolve Vertices"), kindEdit(ModelEditKind::DissolveVertices, {0, {body(fixture, 0, 0)}, {}}));
		add(QStringLiteral("Dissolve Faces"), kindEdit(ModelEditKind::DissolveFaces, {0, {}, {0, 1}}));
		add(QStringLiteral("Poke Faces"), kindEdit(ModelEditKind::PokeFaces, {0, {}, {0}}));
		e = kindEdit(ModelEditKind::PokeFaces, {0, {}, {0}});
		e.tool.offset = 1.5;
		add(QStringLiteral("Poke Faces lifted"), e);
		on(QStringLiteral("Beautify Faces"), skewed(), kindEdit(ModelEditKind::BeautifyFaces, {0, {}, {0, 1}}));
		add(QStringLiteral("Make Face"), kindEdit(ModelEditKind::MakeFace, {3, {2, 7, 9}, {}}));
		e = kindEdit(ModelEditKind::ExtrudeEdges, {0, {}, {}, {edge(-16, 0, -8, 0)}});
		e.translation = {0, -4, 0};
		add(QStringLiteral("Extrude Edges"), e);
		// The quad between x = -8 and 0, clear of the seam corners.
		e = kindEdit(ModelEditKind::InsetFaces, {0, {}, {2, 3}});
		e.tool.insetThickness = 1;
		e.tool.insetDepth = .5;
		add(QStringLiteral("Inset Faces"), e);
		e.tool.inset = ModelInsetMode::Individual;
		e.tool.insetThickness = .25;
		add(QStringLiteral("Inset Faces individually"), e);
		e = kindEdit(ModelEditKind::ShrinkFatten, left);
		e.tool.offset = .5;
		add(QStringLiteral("Shrink/Fatten"), e, Expect::Positions);
		e = kindEdit(ModelEditKind::SmoothVertices, left);
		add(QStringLiteral("Smooth Vertices"), e, Expect::Positions);
		e = kindEdit(ModelEditKind::WeightedTransform, left);
		e.translation = {0, 0, 1};
		add(QStringLiteral("Weighted Transform"), e, Expect::Positions);
		e = kindEdit(ModelEditKind::Transform, left);
		e.translation = {0, 0, 1};
		add(QStringLiteral("Transform"), e, Expect::Positions);
		e = kindEdit(ModelEditKind::SlideEdges, {0, {}, {}, {edge(-8, 0, -8, 8)}});
		e.tool.slide = .25;
		add(QStringLiteral("Slide Edges"), e, Expect::Positions);
		for (const auto kind : {ModelEditKind::ShadeFlat, ModelEditKind::ShadeSmooth, ModelEditKind::ShadeAutoSmooth})
			add(QStringLiteral("Shade (kind %1)").arg(int(kind)), kindEdit(kind, left));
		e = kindEdit(ModelEditKind::Bisect, left);
		e.tool.point = {-4, 0, 0};
		e.tool.normal = {1, 0, 0};
		add(QStringLiteral("Bisect"), e);
		e = kindEdit(ModelEditKind::Symmetrize, {0, {}, {0, 1, 2, 3, 4, 5, 6, 7}});
		e.tool.axis = 0;
		e.tool.mirrorJoints = {{QStringLiteral("arm.L"), QStringLiteral("arm.R")}};
		add(QStringLiteral("Symmetrize"), e);
		e.tool.mirrorJoints.clear();
		refuse(QStringLiteral("Symmetrize without a joint map"), e, QStringLiteral("arm.L"));
		e = kindEdit(ModelEditKind::LoopCut, {0, {}, {}, {edge(-8, 0, -8, 8)}});
		add(QStringLiteral("Loop Cut"), e);
		e = kindEdit(ModelEditKind::Decimate, left);
		e.tool.ratio = .5;
		e.tool.preserveBoundary = false;
		add(QStringLiteral("Decimate"), e);
		add(QStringLiteral("Bevel Vertices"), kindEdit(ModelEditKind::BevelVertices, {0, {body(fixture, -16, 0)}, {}}));
		e = kindEdit(ModelEditKind::Solidify, left);
		e.tool.solidifyThickness = 2;
		add(QStringLiteral("Solidify"), e);
		e = kindEdit(ModelEditKind::ProjectUvMapping, {0, {}, {0, 1}});
		e.tool.uvProjection = ModelUvProjection::Cube;
		add(QStringLiteral("Project UV Mapping"), e);
		// New geometry follows the binding the edit names.
		e = kindEdit(ModelEditKind::AddPrimitive);
		e.tool.primitive = ModelPrimitive::Cube;
		e.tool.point = {0, 4, 8};
		e.tool.size = {4, 4, 4};
		e.tool.axis = 2;
		refuse(QStringLiteral("Add Primitive without a binding"), e, QStringLiteral("new surface"));
		e.tool.newGeometryBinding = ModelNewGeometryBinding::Joint;
		e.tool.bindJoint = QStringLiteral("head");
		add(QStringLiteral("Add Primitive bound to a joint"), e);
		e.tool.newGeometryBinding = ModelNewGeometryBinding::NearestBones;
		e.tool.bindJoint.clear();
		add(QStringLiteral("Add Primitive bound to the nearest bones"), e);
		e.tool.newGeometryBinding = ModelNewGeometryBinding::None;
		e.tool.newSurface = QStringLiteral("helmet");
		add(QStringLiteral("Add Primitive as a new surface"), e);
		// Tools that write complete influences themselves.
		e = kindEdit(ModelEditKind::BevelEdges, {0, {}, {}, {edge(0, 0, -8, 8)}});
		e.tool.bevelWidth = .25;
		add(QStringLiteral("Bevel Edges"), e);
		e = kindEdit(ModelEditKind::Spin, {0, {}, {0}});
		e.tool.spinAngle = 90;
		e.tool.spinSteps = 2;
		e.tool.point = {-20, 0, 0};
		e.tool.normal = {0, 1, 0};
		add(QStringLiteral("Spin"), e);
	}
	for (const auto &item : cases)
		runCase(item);
}

// Two parallel strips one unit apart: A on arm.L at z = 0 and B on arm.R at
// z = 1, with different UVs. Copies of A pushed towards or onto B must keep
// A's joints, which a guess by position would take from B.
ModelMesh strips()
{
	auto mesh = skinFixture();
	QVector<ModelVec3> positions;
	QVector<ModelTexCoord> uv;
	for (const float z : {0.0f, 1.0f})
		for (const float y : {0.0f, 4.0f})
			for (const float x : {20.0f, 24.0f, 28.0f})
			{
				positions.append({x, y, z});
				uv.append({(x - 20) / 8, y / 4 + z * .5f});
			}
	QVector<ModelTriangle> triangles;
	for (const int base : {0, 6})
		triangles << ModelTriangle{base + 0, base + 1, base + 4} << ModelTriangle{base + 0, base + 4, base + 3}
				  << ModelTriangle{base + 1, base + 2, base + 5} << ModelTriangle{base + 1, base + 5, base + 4};
	auto surface = fixtureSurface(QStringLiteral("strips"), positions, uv, triangles, {0, 0, 1});
	surface.index = mesh.surfaces.size();
	// A copy: resize() may reallocate before it copies the value it is given.
	const auto firstFrame = surface.frames[0];
	surface.frames.resize(mesh.frames.size(), firstFrame);
	for (int v = 0; v < positions.size(); ++v)
		appendFixtureSkin(surface.skinning, mesh.skeleton, positions[v], {0, 0, 1},
			v < 6 ? QVector<QPair<int, float>>{{fixtureArmL, .8f}, {fixtureSpine, .2f}} : QVector<QPair<int, float>>{{fixtureArmR, .9f}, {fixtureSpine, .1f}},
			v + 11);
	mesh.surfaces.append(surface);
	repose(&mesh);
	return mesh;
}

void checkCopiesFollowSources()
{
	const auto fixture = strips();
	const int s = fixture.surfaces.size() - 1;
	QString error;
	if (!expect(posesFollowSkeleton(fixture, &error), QStringLiteral("the strips follow their skeleton")))
	{
		std::cerr << "  " << error.toStdString() << '\n';
		return;
	}
	const SkinWeights a{{fixtureArmL, .8}, {fixtureSpine, .2}}, b{{fixtureArmR, .9}, {fixtureSpine, .1}};
	const ModelSelection stripA{s, {}, {0, 1, 2, 3}};
	QVector<QPair<QString, ModelEdit>> edits;
	auto e = kindEdit(ModelEditKind::Extrude, stripA);
	e.translation = {0, 0, 2};
	edits.append({QStringLiteral("Extrude past the other strip"), e});
	e = kindEdit(ModelEditKind::DuplicateFaces, stripA);
	e.translation = {0, 0, 1};
	edits.append({QStringLiteral("Duplicate Faces onto the other strip"), e});
	e = kindEdit(ModelEditKind::ExtrudeEdges, {s, {}, {}, {modelEdge(3, 4), modelEdge(4, 5)}});
	e.translation = {0, 0, 1};
	edits.append({QStringLiteral("Extrude Edges onto the other strip"), e});
	e = kindEdit(ModelEditKind::InsetFaces, stripA);
	e.tool.insetThickness = .5;
	e.tool.insetDepth = 1;
	edits.append({QStringLiteral("Inset Faces raised to the other strip"), e});
	e.tool.inset = ModelInsetMode::Individual;
	edits.append({QStringLiteral("Inset Faces individually raised"), e});
	e = kindEdit(ModelEditKind::Solidify, stripA);
	e.tool.solidifyThickness = -1;
	edits.append({QStringLiteral("Solidify onto the other strip"), e});
	for (const auto &[name, edit] : edits)
	{
		ModelDocument document;
		ModelEditReport report;
		if (!expect(document.setMesh(fixture, &error) && document.edit(edit, &error, {}, &report), name + QStringLiteral(": applies")))
		{
			std::cerr << "  " << error.toStdString() << '\n';
			continue;
		}
		const auto &after = document.mesh();
		const auto &surface = after.surfaces[s];
		int copies = 0;
		bool own = true;
		for (int v = 0; v < surface.vertexCount; ++v)
		{
			// B's own vertices keep B's weights; everything else came from A.
			const bool fromB = findSkinVertex(fixture, s, bindPosition(after, s, v), surface.texCoords[v]) >= 6;
			copies += !fromB && bindPosition(after, s, v).z > .25f;
			if (!sameSkinWeights(skinWeightsOf(surface, v), fromB ? b : a))
			{
				own = false;
				std::cerr << "  " << name.toStdString() << ": vertex " << v << " has another strip's weights\n";
			}
		}
		expect(own && copies > 0, name + QStringLiteral(": copies keep their source strip's joints, not the neighbour's"));
		expect(report.skin.value(s).rebound > 0, name + QStringLiteral(": the moved copies are reported as rebound"));
		if (!expect(posesFollowSkeleton(after, &error), name + QStringLiteral(": moved copies turn with their joints in every retained pose")))
			std::cerr << "  " << error.toStdString() << '\n';
		auto painted = after;
		ModelEdit paint = kindEdit(ModelEditKind::PaintSkinWeights, {s, {}, {}});
		ModelWeightStroke stroke;
		stroke.surface = s;
		stroke.joint = QStringLiteral("spine");
		stroke.value = stroke.strength = .5;
		stroke.samples = {{surface.vertexCount - 1, 1}};
		paint.weightStrokes = {stroke};
		if (!expect(applyModelEdit(&painted, paint, nullptr, &error), name + QStringLiteral(": weight painting accepts a moved copy")))
			std::cerr << "  " << error.toStdString() << '\n';
	}
}

// The fixture plus one independent frame (a copy of the bind pose), after
// the bind frame and the clip's frames.
ModelMesh framesFixture()
{
	auto mesh = skinFixture();
	const int bind = modelBindPoseFrame(mesh);
	auto info = mesh.frames[bind];
	info.name = QStringLiteral("edit");
	mesh.frames.append(info);
	for (auto &surface : mesh.surfaces)
		surface.frames.append(surface.frames[bind]);
	mesh.skeleton.bakedClipForFrame.append(-2);
	updateEditableModelMetadata(&mesh);
	return mesh;
}

ModelEdit frameEdit(ModelEditKind kind, int frame)
{
	ModelEdit edit = kindEdit(kind);
	edit.frame = frame;
	if (kind == ModelEditKind::CopyFramePose)
		edit.sourceFrame = frame == 3 ? 1 : 3;
	return edit;
}

void checkFrames()
{
	const auto mesh = framesFixture();
	const auto map = mesh.skeleton.bakedClipForFrame;
	QString error;
	if (!expect(map.size() == 4 && map[0] == -1 && map[1] == 0 && map[2] == 0 && map[3] == -2,
			QStringLiteral("the frames fixture holds a bind frame, two clip frames and an independent frame")))
		return;
	for (const auto kind : {ModelEditKind::DuplicateFrame, ModelEditKind::DeleteFrame, ModelEditKind::InsertInbetweens, ModelEditKind::CopyFramePose})
		swept.insert(int(kind));
	// The bind frame and clip frames are refused, unchanged.
	for (const auto &[kind, frame, text] :
		{std::tuple{ModelEditKind::DuplicateFrame, 0, "bind pose"}, std::tuple{ModelEditKind::DuplicateFrame, 1, "skeletal clip wave"},
			std::tuple{ModelEditKind::DeleteFrame, 0, "bind pose"}, std::tuple{ModelEditKind::DeleteFrame, 2, "Edit Skeletal Animation"},
			std::tuple{ModelEditKind::InsertInbetweens, 0, "follow the skeleton"}, std::tuple{ModelEditKind::InsertInbetweens, 1, "interpolate"},
			std::tuple{ModelEditKind::CopyFramePose, 0, "bind geometry"}, std::tuple{ModelEditKind::CopyFramePose, 2, "Copy whole pose"}})
	{
		ModelDocument document;
		const bool opened = document.setMesh(mesh, &error);
		const auto bytes = skinBytes(document.mesh());
		const bool applied = document.edit(frameEdit(kind, frame), &error);
		if (!expect(opened && !applied && error.contains(QString::fromLatin1(text)) && skinBytes(document.mesh()) == bytes && !document.canUndo(),
				QStringLiteral("frame edit %1 on retained frame %2 is refused with a pointer to the skeletal tools").arg(int(kind)).arg(frame)))
			std::cerr << "  " << error.toStdString() << '\n';
		expect(modelSkeletalFrameEditRefusal(mesh, kind, frame) == error, QStringLiteral("the refusal is the shared one the editor shows"));
	}
	// Independent frames edit freely and every clip frame keeps its mapping.
	const auto run = [&](const ModelEdit &edit, const QVector<int> &expected, const char *name)
	{
		ModelDocument document;
		ModelEditReport report;
		if (!expect(document.setMesh(mesh, &error) && document.edit(edit, &error, {}, &report), QString::fromLatin1(name)))
		{
			std::cerr << "  " << error.toStdString() << '\n';
			return ModelMesh{};
		}
		const auto after = document.mesh();
		if (!expect(after.skeleton.bakedClipForFrame == expected, QStringLiteral("%1 keeps every clip frame's mapping").arg(QString::fromLatin1(name))))
		{
			for (int clip : after.skeleton.bakedClipForFrame)
				std::cerr << ' ' << clip;
			std::cerr << '\n';
		}
		bool kept = true;
		for (int s = 0; s < after.surfaces.size(); ++s)
			for (int v = 0; !after.surfaces[s].skinning.isEmpty() && v < after.surfaces[s].vertexCount; ++v)
				kept &= sameInfluences(mesh.surfaces[s], v, after.surfaces[s], v);
		expect(kept && report.skin.value(0).kept == after.surfaces[0].vertexCount,
			   QStringLiteral("%1 keeps every influence").arg(QString::fromLatin1(name)));
		if (!expect(retainedFramesFollow(after, &error), QStringLiteral("%1 keeps clip frames skeleton-driven").arg(QString::fromLatin1(name))))
			std::cerr << "  " << error.toStdString() << '\n';
		expect(document.undo() && document.mesh().skeleton.bakedClipForFrame == map, QStringLiteral("undo restores the clip mapping"));
		return after;
	};
	run(frameEdit(ModelEditKind::DuplicateFrame, 3), {-1, 0, 0, -2, -2}, "duplicating an independent frame");
	auto inserted = frameEdit(ModelEditKind::InsertInbetweens, 2);
	inserted.inbetweenCount = 2;
	run(inserted, {-1, 0, 0, -2, -2, -2}, "inserting in-betweens after the clip");
	run(frameEdit(ModelEditKind::CopyFramePose, 3), map, "copying a clip pose into an independent frame");
	// Skeletal authoring needs every frame to come from the skeleton:
	// refused with the independent frame, accepted once it is deleted.
	ModelEdit pose = kindEdit(ModelEditKind::TransformSkeletalPose);
	pose.skeletal.clip = 0;
	pose.skeletal.joint = QStringLiteral("arm.L");
	pose.skeletal.first = pose.skeletal.last = 0;
	pose.skeletal.rotation = {0, 0, 15};
	auto blocked = mesh;
	expect(!applyModelEdit(&blocked, pose, nullptr, &error), QStringLiteral("skeletal authoring refuses independent frames"));
	ModelEdit paint = kindEdit(ModelEditKind::PaintSkinWeights);
	ModelWeightStroke stroke;
	stroke.joint = QStringLiteral("spine");
	stroke.value = stroke.strength = .5;
	stroke.samples = {{0, 1}};
	paint.weightStrokes = {stroke};
	blocked = mesh;
	expect(!applyModelEdit(&blocked, paint, nullptr, &error), QStringLiteral("skin authoring refuses independent frames as before"));
	auto deleted = run(frameEdit(ModelEditKind::DeleteFrame, 3), {-1, 0, 0}, "deleting an independent frame");
	if (!deleted.frames.isEmpty())
	{
		if (!expect(applyModelEdit(&deleted, pose, nullptr, &error) && retainedFramesFollow(deleted, &error),
				QStringLiteral("skeletal authoring works again once the independent frame is deleted")))
			std::cerr << "  " << error.toStdString() << '\n';
		laterSkinEdits(QStringLiteral("after Delete Frame"), deleted);
	}
	// Once every surface is unbound nothing follows the skeleton: frame edits
	// apply everywhere, and a clip that loses a frame, or has one
	// overwritten, no longer claims the frames it no longer matches.
	auto unbound = mesh;
	for (auto &surface : unbound.surfaces)
		surface.skinning = {};
	updateEditableModelMetadata(&unbound);
	bool allowed = true;
	for (const auto kind : {ModelEditKind::DuplicateFrame, ModelEditKind::DeleteFrame, ModelEditKind::InsertInbetweens, ModelEditKind::CopyFramePose})
		for (int frame : {0, 1})
			allowed &= modelSkeletalFrameEditRefusal(unbound, kind, frame).isEmpty();
	expect(allowed, QStringLiteral("frame edits on an unbound skeletal model are not refused"));
	const auto runUnbound = [&](const ModelEdit &edit, const QVector<int> &expected, const char *name)
	{
		ModelDocument document;
		if (!expect(document.setMesh(unbound, &error) && document.edit(edit, &error), QString::fromLatin1(name)))
		{
			std::cerr << "  " << error.toStdString() << '\n';
			return;
		}
		if (!expect(document.mesh().skeleton.bakedClipForFrame == expected, QStringLiteral("%1 maps the frames that still match a clip").arg(QString::fromLatin1(name))))
		{
			for (int clip : document.mesh().skeleton.bakedClipForFrame)
				std::cerr << ' ' << clip;
			std::cerr << '\n';
		}
		expect(document.undo() && document.mesh().skeleton.bakedClipForFrame == map, QStringLiteral("undo restores the clip mapping"));
	};
	runUnbound(frameEdit(ModelEditKind::DuplicateFrame, 1), {-1, 0, -2, 0, -2}, "duplicating a clip frame of an unbound model");
	auto between = frameEdit(ModelEditKind::InsertInbetweens, 1);
	between.inbetweenCount = 1;
	runUnbound(between, {-1, 0, -2, 0, -2}, "inserting in-betweens inside a clip of an unbound model");
	runUnbound(frameEdit(ModelEditKind::DeleteFrame, 2), {-1, -2, -2}, "deleting a clip frame of an unbound model");
	runUnbound(frameEdit(ModelEditKind::CopyFramePose, 0), {-2, 0, 0, -2}, "copying a pose into the bind frame of an unbound model");
}

// Kinds that keep vertex data untouched, or write influences themselves.
void checkCoverage()
{
	const QHash<int, const char *> unswept{
		{int(ModelEditKind::SetMd2SkinSize), "MD2 skin size only"},
		{int(ModelEditKind::AddTag), "attachment tags only"},
		{int(ModelEditKind::DuplicateTag), "attachment tags only"},
		{int(ModelEditKind::RenameTag), "attachment tags only"},
		{int(ModelEditKind::DeleteTag), "attachment tags only"},
		{int(ModelEditKind::SetTagOrigin), "attachment tags only"},
		{int(ModelEditKind::ResetTagOrientation), "attachment tags only"},
		{int(ModelEditKind::TransformTag), "attachment tags only"},
		{int(ModelEditKind::CopyTagPose), "attachment tags only"},
		{int(ModelEditKind::RenameAnimation), "clip ranges only"},
		{int(ModelEditKind::SetAnimationRange), "clip ranges only"},
		{int(ModelEditKind::DeleteAnimation), "clip ranges only"},
		{int(ModelEditKind::SetAnimationRate), "clip ranges only"},
		{int(ModelEditKind::AddMdlSkin), "Quake MDL sources only"},
		{int(ModelEditKind::ReplaceMdlSkinMember), "Quake MDL sources only"},
		{int(ModelEditKind::AppendMdlSkinMember), "Quake MDL sources only"},
		{int(ModelEditKind::RemoveMdlSkin), "Quake MDL sources only"},
		{int(ModelEditKind::RemoveMdlSkinMember), "Quake MDL sources only"},
		{int(ModelEditKind::SetMdlSkinDuration), "Quake MDL sources only"},
		{int(ModelEditKind::SetMdlHeader), "Quake MDL sources only"},
		{int(ModelEditKind::SetMdlPalette), "Quake MDL sources only"},
		{int(ModelEditKind::GroupMdlFrames), "Quake MDL sources only"},
		{int(ModelEditKind::UngroupMdlFrames), "Quake MDL sources only"},
		{int(ModelEditKind::SetMdlFrameDuration), "Quake MDL sources only"},
		{int(ModelEditKind::AddCollisionBox), "collision boxes only"},
		{int(ModelEditKind::FitCollisionBox), "collision boxes only"},
		{int(ModelEditKind::UpdateCollisionBox), "collision boxes only"},
		{int(ModelEditKind::DuplicateCollisionBox), "collision boxes only"},
		{int(ModelEditKind::DeleteCollisionBox), "collision boxes only"},
		{int(ModelEditKind::TransformCollisionBox), "collision boxes only"},
		{int(ModelEditKind::AnimateCollisionBox), "collision boxes only"},
		{int(ModelEditKind::FreezeCollisionBox), "collision boxes only"},
		{int(ModelEditKind::FitAnimatedCollisionBox), "collision boxes only"},
		{int(ModelEditKind::ApplySkinBindings), "MD3 .skin material bindings only"},
		{int(ModelEditKind::Knife), "writes its own influences; model-knife-smoke"},
		{int(ModelEditKind::Rip), "writes its own influences; model-rip-smoke"},
	};
	for (int value = 0; value <= int(ModelEditKind::SetSkeletalWorkingSet); ++value)
	{
		const auto kind = ModelEditKind(value);
		const bool direct = modelEditSkinHandling(kind) == ModelEditSkinHandling::Direct;
		if (!expect(direct || swept.contains(value) || unswept.contains(value),
				QStringLiteral("edit kind %1 is swept, writes influences directly or keeps vertex data untouched").arg(value)))
			continue;
		expect(!(direct && swept.contains(value)), QStringLiteral("direct skin edits are not swept as provenance edits"));
	}
	expect(swept.size() >= 60, QStringLiteral("the sweep covers the topology, UV, surface and frame tools"));
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
	QTemporaryDir temporary(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("skin-sweep-XXXXXX"));
	if (!expect(temporary.isValid(), QStringLiteral("temporary directory")))
		return;
	const QDir dir(temporary.path());
	const auto settings = dir.filePath("settings.ini");
	const auto source = dir.filePath("frames.mesh.json");
	ModelDocument document;
	QString error;
	if (!expect(document.setMesh(framesFixture(), &error) && document.save(source, false, &error), QStringLiteral("the frames fixture saves")))
		return;
	const auto edit = [&](const QStringList &arguments, const QString &output, int *code, QString *text)
	{ return runCli(program, QStringList{"--cli", "model", "edit", source} + arguments + QStringList{"--output", output, "--json", "--settings-file", settings}, code, text); };
	int code = 0;
	QString text;
	auto json = edit({"--operation", "duplicate-frame", "--frame", "1"}, dir.filePath("refused.mesh.json"), &code, &text);
	expect(code == 4 && text.contains("skeletal clip wave") && text.contains("Edit Skeletal Animation") &&
			   text.contains("model pose --operation resize") && !QFile::exists(dir.filePath("refused.mesh.json")),
		   QStringLiteral("CLI duplicate-frame on a clip frame is refused with the skeletal alternative in the editor and on the command line"));
	// Unbind every skinned surface, then edit the clip frames as vertex animation.
	runCli(program,
		QStringList{"--cli", "model", "skinning", source, "--operation", "unbind", "--surfaces", "0,1,3", "--output", dir.filePath("unbound.mesh.json"),
			"--json", "--settings-file", settings},
		&code, &text);
	if (expect(code == 0, QStringLiteral("CLI unbind of every skinned surface applies")))
	{
		json = runCli(program,
			QStringList{"--cli", "model", "edit", dir.filePath("unbound.mesh.json"), "--operation", "duplicate-frame", "--frame", "1", "--output",
				dir.filePath("unbound-duplicated.mesh.json"), "--json", "--settings-file", settings},
			&code, &text);
		ModelDocument unboundDuplicate;
		expect(code == 0 && unboundDuplicate.load(dir.filePath("unbound-duplicated.mesh.json"), &error) &&
				   unboundDuplicate.mesh().skeleton.bakedClipForFrame == QVector<int>({-1, 0, -2, 0, -2}),
			   QStringLiteral("CLI duplicate-frame on a clip frame of an unbound model applies and keeps the clip mapping"));
	}
	if (code != 0)
		std::cerr << text.toStdString() << '\n';
	json = edit({"--operation", "duplicate-frame", "--frame", "3"}, dir.filePath("duplicated.mesh.json"), &code, &text);
	ModelDocument duplicated;
	expect(code == 0 && json.value("frames").toInt() == 5 && json.value("skinProvenance").toObject().value("kept").toInt() > 0 &&
			   duplicated.load(dir.filePath("duplicated.mesh.json"), &error) && duplicated.mesh().skeleton.bakedClipForFrame == QVector<int>({-1, 0, 0, -2, -2}),
		   QStringLiteral("CLI duplicate-frame on an independent frame keeps the clip mapping"));
	if (code != 0)
		std::cerr << text.toStdString() << '\n';
	// A UV unwrap from the CLI gives the document service's exact weights.
	const auto skinned = dir.filePath("skinned.mesh.json");
	ModelDocument skinnedDocument, unwrapped, expected;
	expect(skinnedDocument.setMesh(skinFixture(), &error) && skinnedDocument.save(skinned, false, &error), QStringLiteral("the fixture saves"));
	json = runCli(program,
		QStringList{"--cli", "model", "edit", skinned, "--operation", "uv-unwrap", "--surface", "0", "--faces", "0,1,2,3", "--output",
			dir.filePath("unwrapped.mesh.json"), "--json", "--settings-file", settings},
		&code, &text);
	expect(code == 0 && json.value("skinProvenance").toObject().value("kept").toInt() > 0 &&
			   unwrapped.load(dir.filePath("unwrapped.mesh.json"), &error) && expected.setMesh(skinFixture(), &error) &&
			   expected.edit(kindEdit(ModelEditKind::UnwrapUv, {0, {}, {0, 1, 2, 3}}), &error) && skinBytes(unwrapped.mesh()) == skinBytes(expected.mesh()),
		   QStringLiteral("CLI uv-unwrap keeps the same weights as the document service"));
	if (code != 0)
		std::cerr << text.toStdString() << '\n';
	const auto plain = bindOnly(skinFixture());
	const auto still = dir.filePath("still.mesh.json");
	ModelDocument bindDocument;
	expect(bindDocument.setMesh(plain, &error) && bindDocument.save(still, false, &error), QStringLiteral("the bind-only fixture saves"));
	const int a = body(plain, -8, 8), b = body(plain, -8, 8, true);
	json = runCli(program, QStringList{"--cli", "model", "edit", still, "--operation", "weld", "--surface", "0", "--vertices", QStringLiteral("%1,%2").arg(a).arg(b), "--weld-distance", "0.001", "--merge-seams",
						 "--output", dir.filePath("welded.mesh.json"), "--json", "--settings-file", settings},
		&code, &text);
	expect(code == 0 && json.value("skinProvenance").toObject().value("merged").toInt() == 1,
		   QStringLiteral("CLI weld across seams reports the merged weights"));
	if (code != 0)
		std::cerr << text.toStdString() << '\n';
	QProcess process;
	process.start(program, {"--cli", "model", "edit", still, "--operation", "weld", "--surface", "0", "--vertices", QStringLiteral("%1,%2").arg(a).arg(b),
							"--weld-distance", "0.001", "--merge-seams", "--output", dir.filePath("welded-text.mesh.json"), "--settings-file", settings});
	process.waitForFinished(60000);
	expect(QString::fromUtf8(process.readAllStandardOutput()).contains("averaged weights"), QStringLiteral("CLI text output names merged weights"));
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	checkSweep();
	checkCopiesFollowSources();
	checkFrames();
	checkCoverage();
	if (argc > 1)
		checkCli(QString::fromLocal8Bit(argv[1]));
	std::cout << checks << " skin provenance sweep checks, " << failures << " failures\n";
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
