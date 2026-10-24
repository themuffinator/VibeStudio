#pragma once
#include "tests/model_rig_test_helpers.h"
namespace vibestudio::tests
{
inline ModelMesh ikFixture()
{
	auto mesh = rigFixture();
	mesh.skeleton.joints = {{"world", -1, modelJointMatrix({}, {-4, 0, 0})},	  {"upper", 0, modelJointIdentity()},
							{"lower", 1, modelJointMatrix({}, {4, 0, 0})},		  {"hand", 2, modelJointMatrix({}, {8, 0, 0})},
							{"upper_branch", 1, modelJointMatrix({}, {0, 2, 0})}, {"lower_branch", 2, modelJointMatrix({}, {4, 2, 0})},
							{"finger", 3, modelJointMatrix({}, {8, 2, 0})}};
	const auto bind = modelSkeletonBindPose(mesh.skeleton);
	ModelSkeletalClip clip;
	clip.name = "reach";
	clip.framesPerSecond = 12;
	clip.frames = {bind, bind, bind};
	for (int j = 1; j < clip.frames[2].size(); ++j)
		clip.frames[2][j].m[7] += 1;
	mesh.skeleton.clips = {clip};
	mesh.skeleton.tags = {{"tag_hand", 3, modelJointIdentity()}};
	auto &s = mesh.surfaces[0];
	s.vertexCount = 6;
	s.triangles = {{0, 1, 3}, {1, 4, 3}, {1, 2, 4}, {2, 5, 4}};
	s.texCoords = {{0, 0}, {.5f, 0}, {1, 0}, {0, 1}, {.5f, 1}, {1, 1}};
	s.frames = {{{{0, 0, 0}, {4, 0, 0}, {8, 0, 0}, {0, 1, 0}, {4, 1, 0}, {8, 1, 0}}, QVector<ModelVec3>(6, {0, 0, 1})}};
	if (!modelSkinningFromBindPose(mesh.skeleton, s.frames[0].positions, s.frames[0].normals, {1, 2, 3, 1, 2, 3}, {1, 1, 1, 1, 1, 1}, 1,
								   &s.skinning) ||
		!bakeModelSkeleton(&mesh))
		return {};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline ModelEdit ikEdit()
{
	ModelEdit e;
	e.kind = ModelEditKind::SolveTwoBoneIk;
	e.ik.clip = 0;
	e.ik.endJoint = "hand";
	e.ik.target = {4, 4, 0};
	e.ik.pole = {0, 8, 0};
	return e;
}
} // namespace vibestudio::tests
