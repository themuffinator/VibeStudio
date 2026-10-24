#pragma once
#include "tests/model_animation_curves_test_helpers.h"
namespace vibestudio::tests
{
inline ModelMesh curveKeysFixture(bool dense = false)
{
	auto mesh = curveFixture();
	mesh.skeleton.clips[0].frames.fill(modelSkeletonBindPose(mesh.skeleton), 9);
	if (!bakeModelSkeleton(&mesh))
		return {};
	updateEditableModelMetadata(&mesh);
	for (const auto &joint : QStringList{"upper", "lower"})
	{
		if (!applyModelEdit(&mesh, captureCurves(joint), nullptr, nullptr))
			return {};
		auto edit = captureCurves(joint);
		edit.curves.action = ModelCurveAction::Replace;
		edit.curves.group = mesh.skeleton.clips[0].curves.last();
		auto &keys = edit.curves.group.channels[1];
		keys.clear();
		if (dense)
			for (int i = 0; i < 9; ++i)
				keys.append(curveKey(i, joint == "upper" ? i : 0));
		else
			keys = {curveKey(0, 0), curveKey(2, joint == "upper" ? 4 : 2), curveKey(4, 0)};
		if (!applyModelEdit(&mesh, edit, nullptr, nullptr))
			return {};
	}
	return mesh;
}
inline ModelEdit curveKeysEdit(ModelCurveKeyAction action, const ModelMesh &mesh, bool all = false)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::EditCurveKeys;
	edit.curveKeys.action = action;
	edit.curveKeys.clip = 0;
	for (const auto &group : mesh.skeleton.clips[0].curves)
		for (int c = 0; c < 9; ++c)
			if (all || c == 1)
			{
				ModelCurveKeySelection selection{group.joint, c, {}};
				for (const auto &key : group.channels[c])
					selection.samples.append(key.frame);
				edit.curveKeys.selection.append(selection);
			}
	return edit;
}
} // namespace vibestudio::tests
