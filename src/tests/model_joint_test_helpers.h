#pragma once

#include "tests/model_rig_test_helpers.h"
#include <cmath>

namespace vibestudio::tests
{
inline ModelMesh jointFixture()
{
	auto mesh = rigFixture();
	mesh.skeleton.joints.append({"end", 1, modelJointMatrix({}, {8, 4, 2})});
	mesh.skeleton.joints.append({"hidden", 0, modelJointMatrix({}, {2, 2, -2})});
	for (auto &pose : mesh.skeleton.clips[0].frames)
	{
		pose.append(mesh.skeleton.joints[2].bind);
		pose.append(mesh.skeleton.joints[3].bind);
	}
	if (!bakeModelSkeleton(&mesh))
		return {};
	ModelEdit weight;
	weight.kind = ModelEditKind::SetJointWeight;
	weight.rig.joint = "tip";
	weight.selection.vertices = {1};
	if (!applyModelEdit(&mesh, weight))
		return {};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline bool jointNear(ModelVec3 a, ModelVec3 b, double tolerance = .0003)
{
	return std::abs(double(a.x) - b.x) < tolerance && std::abs(double(a.y) - b.y) < tolerance && std::abs(double(a.z) - b.z) < tolerance;
}
} // namespace vibestudio::tests
