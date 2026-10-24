#pragma once
#include "tests/model_ik_test_helpers.h"
namespace vibestudio::tests
{
inline ModelMesh curveFixture()
{
	auto mesh = ikFixture();
	mesh.skeleton.clips[0].frames.fill(modelSkeletonBindPose(mesh.skeleton), 5);
	if (!bakeModelSkeleton(&mesh))
		return {};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline ModelEdit captureCurves(const QString &joint = "upper")
{
	ModelEdit edit;
	edit.kind = ModelEditKind::EditAnimationCurves;
	edit.curves.clip = 0;
	edit.curves.joint = joint;
	return edit;
}
inline ModelPoseCurveKey curveKey(int frame, double value, ModelCurveInterpolation mode = ModelCurveInterpolation::Linear,
								  ModelCurveTangent tangent = ModelCurveTangent::Clamped)
{
	ModelPoseCurveKey key;
	key.frame = frame;
	key.value = value;
	key.interpolation = mode;
	key.tangent = tangent;
	return key;
}
} // namespace vibestudio::tests
