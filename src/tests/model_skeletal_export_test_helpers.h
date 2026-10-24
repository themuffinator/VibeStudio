#pragma once

// Skeletal fixtures for the MD5, IQM and ASE export fidelity tests.

#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_skeleton.h"

#include <cmath>
#include <numbers>

namespace vibestudio::tests
{
inline ModelQuat exportAboutZ(double degrees)
{
	const double half = degrees * std::numbers::pi / 360.0;
	return {0.0f, 0.0f, float(std::sin(half)), float(std::cos(half))};
}

// Model-space poses from parent-relative ones.
inline QVector<ModelJointMatrix> exportPose(const ModelSkeleton &skeleton, const QVector<ModelJointMatrix> &local)
{
	return modelJointsToModelSpace(modelJointParents(skeleton), local);
}

// A skinned surface `name` over `positions` (a strip of quads, two rows),
// with the given joint indexes and weights per vertex.
inline ModelSurface exportStrip(const QString &name, int columns)
{
	ModelSurface surface;
	surface.name = name;
	for (int row = 0; row < 2; ++row)
		for (int column = 0; column < columns; ++column)
			surface.texCoords.append({float(column) / float(columns - 1), float(row)});
	ModelFrameGeometry frame;
	for (int row = 0; row < 2; ++row)
		for (int column = 0; column < columns; ++column)
			frame.positions.append({float(column * 8), float(row * 8), 0.0f});
	frame.normals.fill({0, 0, 1}, frame.positions.size());
	surface.vertexCount = int(frame.positions.size());
	for (int column = 0; column + 1 < columns; ++column)
	{
		const int a = column, b = column + 1, c = columns + column + 1, d = columns + column;
		surface.triangles.append({a, b, c});
		surface.triangles.append({a, c, d});
	}
	surface.frames = {frame};
	return surface;
}

// An arm: root at the origin, mid at x = 8 and tip at x = 16 over a strip
// from x = 0 to 24. Clip "idle" bends mid (3 frames at 24 fps), clip "wave"
// turns tip (4 frames at 30 fps). Joint tag "tag_hand" sits past the tip.
// Every joint is rigid.
inline ModelMesh exportArmFixture()
{
	ModelDesign design;
	design.parts.append(ModelDesignPart{});
	ModelMesh mesh = buildModelDesignMesh(design);
	mesh.surfaces = {exportStrip(QStringLiteral("arm"), 4)};
	mesh.surfaces[0].skinPaths = {QStringLiteral("models/test/arm")};
	mesh.frames.resize(1);
	mesh.frames[0].name = QStringLiteral("base");
	auto &skeleton = mesh.skeleton;
	skeleton.sourceFormat = QStringLiteral("test");
	const QVector<ModelJointMatrix> rest{modelJointIdentity(), modelJointMatrix({}, {8, 0, 0}), modelJointMatrix({}, {8, 0, 0})};
	skeleton.joints = {{QStringLiteral("root"), -1, {}}, {QStringLiteral("mid"), 0, {}}, {QStringLiteral("tip"), 1, {}}};
	const auto bind = exportPose(skeleton, rest);
	for (int joint = 0; joint < bind.size(); ++joint)
		skeleton.joints[joint].bind = bind[joint];
	ModelSkeletalClip idle;
	idle.name = QStringLiteral("idle");
	idle.framesPerSecond = 24;
	for (int frame = 0; frame < 3; ++frame)
		idle.frames.append(exportPose(skeleton, {rest[0], modelJointMatrix(exportAboutZ(30.0 * frame), {8, 0, 0}), rest[2]}));
	ModelSkeletalClip wave;
	wave.name = QStringLiteral("wave");
	wave.framesPerSecond = 30;
	for (int frame = 0; frame < 4; ++frame)
		wave.frames.append(exportPose(skeleton, {modelJointMatrix({}, {0, 0, float(frame)}), rest[1], modelJointMatrix(exportAboutZ(20.0 * frame), {8, 0, 0})}));
	skeleton.clips = {idle, wave};
	ModelSkeletalTag tag;
	tag.name = QStringLiteral("tag_hand");
	tag.joint = 2;
	tag.offset = modelJointMatrix({}, {4, 0, 0});
	skeleton.tags = {tag};
	QVector<int> joints;
	QVector<float> weights;
	for (const auto &p : mesh.surfaces[0].frames[0].positions)
	{
		// Root, then shared by root and mid, then mid, then tip.
		const int first = p.x < 4 ? 0 : p.x < 20 ? (p.x < 12 ? 0 : 1) : 2;
		const bool shared = p.x > 4 && p.x < 12;
		joints << first << (shared ? 1 : 0);
		weights << (shared ? 0.25f : 1.0f) << (shared ? 0.75f : 0.0f);
	}
	auto &surface = mesh.surfaces[0];
	if (!modelSkinningFromBindPose(skeleton, surface.frames[0].positions, surface.frames[0].normals, joints, weights, 2, &surface.skinning) ||
		!bakeModelSkeleton(&mesh))
		return {};
	updateEditableModelMetadata(&mesh);
	return mesh;
}

} // namespace vibestudio::tests
