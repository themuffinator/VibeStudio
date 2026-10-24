#pragma once

// A skinned fixture for skin-weight provenance tests. Weights vary across
// every vertex, coincident seam copies carry different weights, the vertex
// order is interleaved, each influence has its own offset (as MD5 weights
// do), and a clip turns the arm joints, so any position-based weight guess
// or stale index mapping changes a measured result.

#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_skeleton.h"
#include "core/model_skinning_work.h"

#include <QJsonDocument>
#include <QMap>
#include <QPair>

#include <cmath>

namespace vibestudio::tests
{
using SkinWeights = QMap<int, double>;

// Joint indices of skinFixture().
inline constexpr int fixtureSpine = 0, fixtureArmL = 1, fixtureArmR = 2, fixtureHead = 3;

// Appends one vertex's influences: the bind position is exactly p while each
// joint's offset differs (offsets p + d_j with sum w_j d_j = 0). A negative
// salt gives every joint the same bind point, as weight painting requires.
inline void appendFixtureSkin(ModelSurfaceSkinning &skinning, const ModelSkeleton &skeleton, ModelVec3 p, ModelVec3 n,
	const QVector<QPair<int, float>> &weights, int salt)
{
	skinning.first.append(skinning.influences.size());
	skinning.count.append(weights.size());
	double sx = 0, sy = 0, sz = 0;
	for (int i = 0; i < weights.size(); ++i)
	{
		const auto [joint, weight] = weights[i];
		ModelVec3 d{float(1 + (salt + i) % 3), float(.5 * (i + 1)), float(-.25 * ((salt + i) % 5))};
		if (i + 1 == weights.size())
			d = {float(-sx / weight), float(-sy / weight), float(-sz / weight)};
		if (salt < 0)
			d = {0, 0, 0};
		sx += weight * d.x;
		sy += weight * d.y;
		sz += weight * d.z;
		const auto inverse = modelJointInverse(skeleton.joints[joint].bind);
		skinning.influences.append(
			{joint, weight, modelJointTransformPoint(inverse, {p.x + d.x, p.y + d.y, p.z + d.z}), modelJointTransformVector(inverse, n)});
	}
}

inline ModelSurface fixtureSurface(const QString &name, const QVector<ModelVec3> &positions, const QVector<ModelTexCoord> &uv,
	const QVector<ModelTriangle> &triangles, ModelVec3 normal)
{
	ModelSurface surface;
	surface.name = name;
	surface.vertexCount = positions.size();
	surface.texCoords = uv;
	surface.triangles = triangles;
	surface.skinPaths = {QStringLiteral("models/fixture.tga")};
	surface.frames = {{positions, QVector<ModelVec3>(positions.size(), normal)}};
	return surface;
}

// Surfaces: 0 "body" (a symmetric strip across X with seam copies at
// (+-8, 8, 0) whose weights differ), 1 "head" (skinned to spine and head
// only), 2 "prop" (unskinned) and 3 "gem" (a closed tetrahedron in the
// middle of its vertex array, between two loose triangles).
inline ModelMesh skinFixture()
{
	ModelDesign design;
	design.parts.append(ModelDesignPart{});
	auto mesh = buildModelDesignMesh(design);
	auto &skeleton = mesh.skeleton;
	skeleton.joints = {{QStringLiteral("spine"), -1, modelJointIdentity()},
					   {QStringLiteral("arm.L"), 0, modelJointMatrix({}, {8, 0, 0})},
					   {QStringLiteral("arm.R"), 0, modelJointMatrix({}, {-8, 0, 0})},
					   {QStringLiteral("head"), 0, modelJointMatrix({}, {0, 0, 16})}};
	const auto turn = [](float degrees, ModelVec3 axis)
	{
		const float half = degrees * 3.14159265f / 360.0f, s = std::sin(half);
		return ModelQuat{axis.x * s, axis.y * s, axis.z * s, std::cos(half)};
	};
	ModelSkeletalClip clip;
	clip.name = QStringLiteral("wave");
	clip.framesPerSecond = 10;
	clip.frames = {{modelJointIdentity(), modelJointMatrix(turn(30, {0, 0, 1}), {8, 0, 0}), modelJointMatrix(turn(-45, {0, 0, 1}), {-8, 0, 0}),
					modelJointMatrix(turn(20, {1, 0, 0}), {0, 0, 16})},
				   {modelJointMatrix(turn(10, {0, 1, 0}), {0, 0, 1}), modelJointMatrix(turn(-60, {0, 1, 0}), {8, 1, 0}),
					modelJointMatrix(turn(25, {1, 0, 0}), {-8, 0, 2}), modelJointMatrix(turn(-15, {0, 0, 1}), {0, 1, 16})}};
	skeleton.clips = {clip};
	skeleton.tags.clear();
	skeleton.bakedClipForFrame.clear();

	// Logical body vertices; seam copies L10 (8, 8) and L11 (-8, 8).
	const QVector<ModelVec3> positions{{-16, 0, 0}, {-8, 0, 0}, {0, 0, 0}, {8, 0, 0}, {16, 0, 0}, {-16, 8, 0},
									   {-8, 8, 0},	{0, 8, 0},	{8, 8, 0}, {16, 8, 0}, {8, 8, 0},	{-8, 8, 0}};
	QVector<ModelTexCoord> uv;
	for (const auto &p : positions)
		uv.append({p.x / 32 + .5f, p.y / 8});
	uv[10].u += .125f;
	uv[11].u -= .125f;
	const QVector<QVector<QPair<int, float>>> weights{
		{{fixtureArmR, 1}},
		{{fixtureArmR, .7f}, {fixtureSpine, .3f}},
		{{fixtureSpine, 1}},
		{{fixtureArmL, .7f}, {fixtureSpine, .3f}},
		{{fixtureArmL, 1}},
		{{fixtureArmR, .9f}, {fixtureSpine, .1f}},
		{{fixtureArmR, .5f}, {fixtureSpine, .5f}},
		{{fixtureSpine, .6f}, {fixtureHead, .4f}},
		{{fixtureArmL, .5f}, {fixtureSpine, .5f}},
		{{fixtureArmL, .9f}, {fixtureSpine, .1f}},
		{{fixtureArmL, .2f}, {fixtureSpine, .8f}},
		{{fixtureArmR, .2f}, {fixtureSpine, .8f}}};
	const QVector<ModelTriangle> logical{{0, 1, 5}, {1, 11, 5}, {1, 2, 6}, {2, 7, 6}, {2, 3, 8}, {2, 8, 7}, {3, 4, 9}, {3, 9, 10}};
	// Storage position i holds logical vertex order[i].
	const QVector<int> order{7, 2, 11, 0, 5, 9, 3, 10, 1, 6, 4, 8};
	QVector<int> storage(order.size());
	for (int i = 0; i < order.size(); ++i)
		storage[order[i]] = i;
	QVector<ModelVec3> stored;
	QVector<ModelTexCoord> storedUv;
	for (int logicalVertex : order)
	{
		stored.append(positions[logicalVertex]);
		storedUv.append(uv[logicalVertex]);
	}
	QVector<ModelTriangle> triangles;
	for (const auto &t : logical)
		triangles.append({storage[t.a], storage[t.b], storage[t.c]});
	auto body = fixtureSurface(QStringLiteral("body"), stored, storedUv, triangles, {0, 0, 1});
	// Body vertex 0, on the mirror plane, has canonical offsets so painting it
	// checks the whole surface's poses.
	for (int i = 0; i < order.size(); ++i)
		appendFixtureSkin(body.skinning, skeleton, stored[i], {0, 0, 1}, weights[order[i]], i == 0 ? -1 : i);

	auto head = fixtureSurface(QStringLiteral("head"), {{-4, 0, 14}, {4, 0, 14}, {4, 0, 18}, {-4, 0, 18}},
		{{0, 0}, {1, 0}, {1, 1}, {0, 1}}, {{0, 1, 2}, {0, 2, 3}}, {0, -1, 0});
	const QVector<QVector<QPair<int, float>>> headWeights{{{fixtureHead, .8f}, {fixtureSpine, .2f}},
														  {{fixtureHead, 1}},
														  {{fixtureHead, .6f}, {fixtureSpine, .4f}},
														  {{fixtureHead, .9f}, {fixtureSpine, .1f}}};
	for (int v = 0; v < 4; ++v)
		appendFixtureSkin(head.skinning, skeleton, head.frames[0].positions[v], {0, -1, 0}, headWeights[v], v + 7);

	auto prop = fixtureSurface(QStringLiteral("prop"), {{20, 0, 0}, {24, 0, 0}, {20, 4, 0}}, {{0, 0}, {1, 0}, {0, 1}}, {{0, 1, 2}},
		{0, 0, 1});

	// A loose triangle, the closed tetrahedron, then another loose triangle.
	const QVector<ModelVec3> gemPositions{{-30, 0, 0}, {-28, 0, 0}, {-30, 2, 0}, {0, -20, 0}, {4, -20, 0}, {2, -16, 0},
										  {2, -18, 4}, {30, 0, 0},	{32, 0, 0},	 {30, 2, 0}};
	QVector<ModelTexCoord> gemUv;
	for (int v = 0; v < gemPositions.size(); ++v)
		gemUv.append({float(v) / 10, float(v % 3) / 3});
	auto gem = fixtureSurface(QStringLiteral("gem"), gemPositions, gemUv, {{0, 1, 2}, {3, 5, 4}, {3, 4, 6}, {4, 5, 6}, {5, 3, 6}, {7, 8, 9}},
		{0, 0, 1});
	const QVector<QVector<QPair<int, float>>> gemWeights{
		{{fixtureArmR, 1}},
		{{fixtureArmR, .6f}, {fixtureSpine, .4f}},
		{{fixtureArmR, .3f}, {fixtureSpine, .7f}},
		{{fixtureSpine, .9f}, {fixtureArmL, .1f}},
		{{fixtureArmL, .4f}, {fixtureSpine, .6f}},
		{{fixtureHead, .5f}, {fixtureSpine, .5f}},
		{{fixtureHead, .25f}, {fixtureArmL, .25f}, {fixtureSpine, .5f}},
		{{fixtureArmL, 1}},
		{{fixtureArmL, .8f}, {fixtureSpine, .2f}},
		{{fixtureArmL, .3f}, {fixtureSpine, .7f}}};
	for (int v = 0; v < gemPositions.size(); ++v)
		appendFixtureSkin(gem.skinning, skeleton, gemPositions[v], {0, 0, 1}, gemWeights[v], v + 3);

	mesh.surfaces = {body, head, prop, gem};
	for (int s = 0; s < mesh.surfaces.size(); ++s)
		mesh.surfaces[s].index = s;
	mesh.frames.resize(1);
	if (!bakeModelSkeleton(&mesh))
		return {};
	updateEditableModelMetadata(&mesh);
	return mesh;
}

inline QByteArray skinBytes(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}

inline SkinWeights skinWeightsOf(const ModelSurface &surface, int vertex)
{
	SkinWeights weights;
	if (surface.skinning.isEmpty())
		return weights;
	for (int i = surface.skinning.first[vertex]; i < surface.skinning.first[vertex] + surface.skinning.count[vertex]; ++i)
		weights[surface.skinning.influences[i].joint] += surface.skinning.influences[i].weight;
	return weights;
}

inline bool sameSkinWeights(const SkinWeights &a, const SkinWeights &b, double tolerance = 1e-6)
{
	if (a.keys() != b.keys())
		return false;
	for (auto i = a.cbegin(); i != a.cend(); ++i)
		if (std::abs(i.value() - b.value(i.key())) > tolerance)
			return false;
	return true;
}

inline SkinWeights blendSkinWeights(const QVector<QPair<SkinWeights, double>> &parts)
{
	SkinWeights result;
	for (const auto &[weights, part] : parts)
		for (auto i = weights.cbegin(); i != weights.cend(); ++i)
			result[i.key()] += i.value() * part;
	return result;
}

// Joints, weights and offsets equal bit for bit.
inline bool sameInfluences(const ModelSurface &a, int va, const ModelSurface &b, int vb, bool normals = true)
{
	if (a.skinning.count[va] != b.skinning.count[vb])
		return false;
	for (int i = 0; i < a.skinning.count[va]; ++i)
	{
		const auto &x = a.skinning.influences[a.skinning.first[va] + i], &y = b.skinning.influences[b.skinning.first[vb] + i];
		if (x.joint != y.joint || x.weight != y.weight || x.offset.x != y.offset.x || x.offset.y != y.offset.y || x.offset.z != y.offset.z)
			return false;
		if (normals && (x.normalOffset.x != y.normalOffset.x || x.normalOffset.y != y.normalOffset.y || x.normalOffset.z != y.normalOffset.z))
			return false;
	}
	return true;
}

inline bool nearPoint(ModelVec3 a, ModelVec3 b, double tolerance = 1e-4)
{
	return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.z - b.z) <= tolerance;
}

inline bool nearUv(ModelTexCoord a, ModelTexCoord b, double tolerance = 1e-5)
{
	return std::abs(a.u - b.u) <= tolerance && std::abs(a.v - b.v) <= tolerance;
}

inline ModelVec3 bindPosition(const ModelMesh &mesh, int surface, int vertex)
{
	return mesh.surfaces[surface].frames[std::max(0, modelBindPoseFrame(mesh))].positions[vertex];
}

// The vertex at this bind position with this UV, or -1.
inline int findSkinVertex(const ModelMesh &mesh, int surface, ModelVec3 position, ModelTexCoord uv)
{
	for (int v = 0; v < mesh.surfaces[surface].vertexCount; ++v)
		if (nearPoint(bindPosition(mesh, surface, v), position) && nearUv(mesh.surfaces[surface].texCoords[v], uv))
			return v;
	return -1;
}

// Every retained skeletal pose of every skinned surface equals its skinning:
// the condition skin authoring (Bind, Paint, Transfer) checks before editing.
inline bool posesFollowSkeleton(const ModelMesh &mesh, QString *error)
{
	ModelSkinningWork work;
	if (!work.prepare(mesh, error))
		return false;
	for (int s = 0; s < mesh.surfaces.size(); ++s)
		if (!mesh.surfaces[s].skinning.isEmpty() && !work.validateSurface(mesh, s, error, {}))
			return false;
	return true;
}
} // namespace vibestudio::tests
