#pragma once
#include "tests/model_curve_keys_test_helpers.h"
namespace vibestudio::tests
{
inline ModelMesh curveTransferFixture()
{
	auto mesh = curveKeysFixture(true);
	ModelEdit timing;
	timing.kind = ModelEditKind::SetSkeletalClipTiming;
	timing.skeletal.clip = 0;
	timing.skeletal.rate = 24;
	if (!applyModelEdit(&mesh, timing, nullptr, nullptr))
		return {};
	ModelEdit add;
	add.kind = ModelEditKind::AddSkeletalClip;
	add.skeletal.name = "destination";
	add.skeletal.frameCount = 17;
	add.skeletal.rate = 48;
	if (!applyModelEdit(&mesh, add, nullptr, nullptr))
		return {};
	return mesh;
}
inline ModelEdit curveTransferEdit()
{
	ModelEdit edit;
	edit.kind = ModelEditKind::TransferAnimationCurves;
	edit.curveTransfer = {0, 1, 2, 6, 4, 2, true, {"upper", "lower"}, {1}};
	return edit;
}
} // namespace vibestudio::tests
