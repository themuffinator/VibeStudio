#pragma once
#include "tests/model_rig_test_helpers.h"

namespace vibestudio::tests
{
inline ModelMesh transferFixture()
{
	auto mesh = rigFixture();
	auto &s = mesh.surfaces[0];
	s.vertexCount = 7;
	s.triangles = {{0, 1, 2}, {3, 5, 4}};
	s.texCoords = {{0, 0}, {1, 0}, {0, 1}, {0, 0}, {1, 0}, {0, 1}, {.5, .5}};
	s.frames = {{{{2, 0, 0}, {2, 4, 0}, {2, 0, 4}, {-2, 0, 0}, {-2, 4, 0}, {-2, 0, 4}, {0, 0, 0}},
				 {{1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {0, 0, 1}}}};
	mesh.skeleton.joints = {
		{"root", -1, modelJointIdentity()}, {"arm.L", 0, modelJointMatrix({}, {2, 0, 0})}, {"arm.R", 0, modelJointMatrix({}, {-2, 0, 0})}};
	mesh.skeleton.tags.clear();
	mesh.skeleton.bakedClipForFrame.clear();
	mesh.skeleton.clips[0].frames = {modelSkeletonBindPose(mesh.skeleton),
									 {modelJointIdentity(), modelJointMatrix({}, {2, 2, 0}), modelJointMatrix({}, {-2, -3, 0})}};
	if (!modelSkinningFromBindPose(mesh.skeleton, s.frames[0].positions, s.frames[0].normals, {0, 1, 0, 1, 0, 1, 0, 2, 0, 2, 0, 2, 0, 1},
								   {.25, .75, 1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0}, 2, &s.skinning) ||
		!bakeModelSkeleton(&mesh))
		return {};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline ModelEdit mirrorWeights()
{
	ModelEdit edit;
	edit.kind = ModelEditKind::TransferSkinWeights;
	auto &o = edit.weightTransfer;
	o.mirrorAxis = 0;
	o.side = ModelWeightTransferSide::PositiveToNegative;
	o.maximumDistance = .001;
	o.joints = {{"arm.L", "arm.R"}, {"arm.R", "arm.L"}};
	return edit;
}
} // namespace vibestudio::tests
