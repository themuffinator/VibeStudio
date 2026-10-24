#pragma once

// Dense skeletal fixtures for the editable frame budget and the skeletal
// working set (core/model_skeletal_working_set.h): a two-joint strip with
// any number of vertices and clips, baked with only the chosen clips so it
// fits an editable source even when all of its clips would not.

#include "core/model_animation_curves.h"
#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_skeletal_working_set.h"
#include "core/model_skeleton.h"

#include <QPair>
#include <QString>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace vibestudio::tests
{
// `vertices` (even, at least 4) along a strip; the left half follows "root",
// the right half "tip", whose child "hand" ends a two-bone chain. Each clip
// lifts the tip by its own amount per frame, so every sample of every clip is
// distinct. `baked` lists the clips baked into frames after the bind pose.
inline ModelMesh denseSkeletalFixture(int vertices, const QVector<QPair<QString, int>> &clips, const QVector<int> &baked)
{
	ModelDesign design;
	design.parts.append(ModelDesignPart{});
	auto mesh = buildModelDesignMesh(design);
	mesh.collisionBoxes.clear();
	mesh.tags.clear();
	auto &surface = mesh.surfaces[0];
	surface = ModelSurface{};
	surface.name = QStringLiteral("body");
	surface.skinPaths = {QStringLiteral("models/body.tga")};
	const int columns = vertices / 2;
	ModelFrameGeometry bind;
	QVector<int> joints;
	for (int column = 0; column < columns; ++column)
		for (int row = 0; row < 2; ++row)
		{
			bind.positions.append({float(column), float(row * 8), 0});
			bind.normals.append({0, 0, 1});
			surface.texCoords.append({float(column) / float(columns), float(row)});
			joints.append(column < columns / 2 ? 0 : 1);
		}
	for (int column = 0; column + 1 < columns; ++column)
	{
		const int a = column * 2, b = column * 2 + 2, c = column * 2 + 3, d = column * 2 + 1;
		surface.triangles.append({a, b, c});
		surface.triangles.append({a, c, d});
	}
	surface.vertexCount = int(surface.texCoords.size());
	surface.frames = {bind};
	const float half = float(columns / 2);
	const float end = float(columns - 1);
	mesh.skeleton.joints = {{QStringLiteral("root"), -1, modelJointIdentity()},
							{QStringLiteral("tip"), 0, modelJointMatrix({}, {half, 0, 0})},
							{QStringLiteral("hand"), 1, modelJointMatrix({}, {end, 0, 0})}};
	mesh.skeleton.tags = {{QStringLiteral("tag_tip"), 1, modelJointIdentity()}};
	mesh.skeleton.sourceFormat = QStringLiteral("iqm");
	for (int index = 0; index < clips.size(); ++index)
	{
		ModelSkeletalClip clip;
		clip.name = clips[index].first;
		clip.framesPerSecond = 24;
		for (int frame = 0; frame < clips[index].second; ++frame)
		{
			const float lift = float(index + 1) + float(frame % 97) * 0.05f;
			clip.frames.append({modelJointIdentity(), modelJointMatrix({}, {half, lift, 0}), modelJointMatrix({}, {end, lift, 0})});
		}
		mesh.skeleton.clips.append(clip);
	}
	if (!modelSkinningFromBindPose(mesh.skeleton, bind.positions, bind.normals, joints, QVector<float>(joints.size(), 1.0f), 1,
								   &surface.skinning))
		return {};
	ModelSkeletonBakeOptions options;
	options.byIndex = true;
	options.clipIndexes = baked;
	options.maxVertexSlots = 64LL * 1024 * 1024;
	if (!bakeModelSkeleton(&mesh, options))
		return {};
	updateEditableModelMetadata(&mesh);
	return mesh;
}

// A character-scale skinned model as an MD5 or IQM import leaves it, for the
// source size and history budgets: 8,192 vertices on a 128 x 64 tube with
// four weighted influences each, a 64-joint chain with rotated binds, and
// `clipCount` clips of `framesPerClip` frames in which every joint turns, all
// kept in the skeleton. The clips that fit the automatic working set
// (core/model_skeletal_working_set.h) are baked into frames, and the first
// `curveClips` clips carry captured curves for every joint. Every number has
// a fractional part, as real joint matrices and vertex offsets do.
inline ModelMesh realisticSkeletalDocument(int clipCount = 40, int framesPerClip = 45, int curveClips = 2)
{
	constexpr int columns = 128, rows = 64, jointCount = 64, perVertex = 4;
	constexpr float height = 92.0f, spacing = height / jointCount;
	ModelMesh mesh;
	mesh.geometryAvailable = true;
	mesh.sourcePath = QStringLiteral("tests/realistic-skeletal.md5mesh");
	ModelSurface surface;
	surface.name = QStringLiteral("body");
	surface.skinPaths = {QStringLiteral("models/characters/body.tga")};
	ModelFrameGeometry bind;
	QVector<int> joints;
	QVector<float> weights;
	for (int row = 0; row < rows; ++row)
		for (int column = 0; column < columns; ++column)
		{
			const float angle = float(column) * 6.2831853f / float(columns);
			const float radius = 12.0f + std::sin(float(row) * 0.37f) * 2.3f;
			const float z = float(row) * (height / float(rows - 1)) + 0.01f * std::sin(float(column));
			bind.positions.append({std::cos(angle) * radius, std::sin(angle) * radius, z});
			const float nz = 0.05f * std::sin(float(row));
			const float length = std::sqrt(1.0f + nz * nz);
			bind.normals.append({std::cos(angle) / length, std::sin(angle) / length, nz / length});
			surface.texCoords.append({float(column) / float(columns - 1), float(row) / float(rows - 1)});
			const int first = std::clamp(int(z / spacing) - 1, 0, jointCount - perVertex);
			float total = 0;
			for (int i = 0; i < perVertex; ++i)
			{
				joints.append(first + i);
				weights.append(1.0f / (1.0f + std::abs(z - float(first + i) * spacing)));
				total += weights.last();
			}
			for (int i = 0; i < perVertex; ++i)
				weights[weights.size() - 1 - i] /= total;
		}
	for (int row = 0; row + 1 < rows; ++row)
		for (int column = 0; column + 1 < columns; ++column)
		{
			const int a = row * columns + column, b = a + 1, c = a + columns, d = c + 1;
			surface.triangles.append({a, b, d});
			surface.triangles.append({a, d, c});
		}
	surface.vertexCount = int(surface.texCoords.size());
	surface.frames = {bind};
	auto &skeleton = mesh.skeleton;
	skeleton.sourceFormat = QStringLiteral("md5");
	for (int joint = 0; joint < jointCount; ++joint)
	{
		const ModelVec3 origin{std::sin(float(joint)) * 0.731f, std::cos(float(joint)) * 0.417f, float(joint) * spacing};
		skeleton.joints.append({QStringLiteral("joint%1").arg(joint), joint - 1,
								modelJointFromQuakeAngles(float(joint) * 1.3f, float(joint) * 2.1f, float(joint) * 0.7f, origin)});
	}
	QVector<ModelJointMatrix> locals;
	for (int joint = 0; joint < jointCount; ++joint)
		locals.append(joint == 0 ? skeleton.joints[0].bind
								 : modelJointMultiply(modelJointInverse(skeleton.joints[joint - 1].bind), skeleton.joints[joint].bind));
	skeleton.tags = {{QStringLiteral("tag_head"), jointCount - 1, modelJointIdentity()}};
	for (int index = 0; index < clipCount; ++index)
	{
		ModelSkeletalClip clip;
		clip.name = QStringLiteral("clip%1").arg(index);
		clip.framesPerSecond = 24;
		for (int frame = 0; frame < framesPerClip; ++frame)
		{
			QVector<ModelJointMatrix> pose;
			for (int joint = 0; joint < jointCount; ++joint)
			{
				const float t = float(frame) + float(index) * 7.0f;
				const auto turn = modelJointFromQuakeAngles(std::sin(t * 0.21f + float(joint) * 0.17f) * 12.0f,
															std::cos(t * 0.13f + float(joint) * 0.11f) * 9.0f,
															std::sin(t * 0.07f + float(joint) * 0.23f) * 5.0f,
															joint == 0 ? ModelVec3{std::sin(t * 0.05f) * 3.1f, float(frame) * 0.731f, 0} : ModelVec3{});
				const auto local = modelJointMultiply(locals[joint], turn);
				pose.append(joint == 0 ? local : modelJointMultiply(pose[joint - 1], local));
			}
			clip.frames.append(pose);
		}
		skeleton.clips.append(clip);
	}
	if (!modelSkinningFromBindPose(skeleton, bind.positions, bind.normals, joints, weights, perVertex, &surface.skinning))
		return {};
	mesh.surfaces.append(surface);
	ModelFrameInfo info;
	info.name = QStringLiteral("bind");
	mesh.frames.append(info);
	skeleton.bakedClipForFrame = {-1};
	updateEditableModelMetadata(&mesh);
	if (!fitModelSkeletalWorkingSet(&mesh, nullptr))
		return {};
	for (int index = 0; index < std::min(curveClips, clipCount); ++index)
		for (int joint = 0; joint < jointCount; ++joint)
		{
			ModelPoseCurves group;
			if (!captureModelPoseCurves(mesh, index, mesh.skeleton.joints[joint].name, &group, nullptr))
				return {};
			mesh.skeleton.clips[index].curves.append(group);
		}
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace vibestudio::tests
