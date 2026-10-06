#pragma once

#include "core/model_collision.h"
#include "tests/model_animation_test_helpers.h"

namespace vibestudio::tests
{
inline ModelMesh collisionAnimationFixture()
{
	auto mesh = animationFixture();
	ModelCollisionBox body{"body", {0, 0, 0}, {16, 24, 32}, {0, 0, 350}};
	setModelCollisionFrames(
		&body, {{body.centre, body.size, body.rotation}, {{20, 4, 8}, {24, 32, 40}, {0, 0, 10}}, {{40, 8, 16}, {32, 40, 48}, {0, 0, 90}}});
	mesh.collisionBoxes = {body, {"static", {-32, 0, 0}, {8, 8, 8}, {}}};
	return mesh;
}
inline ModelMesh collisionMaximumFixture()
{
	auto mesh = collisionAnimationFixture();
	mesh.tags.clear();
	mesh.animations.clear();
	const auto first = mesh.frames[0];
	mesh.frames.fill(first, modelDocumentMaxFrames);
	for (auto &surface : mesh.surfaces)
	{
		const auto pose = surface.frames[0];
		surface.frames.fill(pose, modelDocumentMaxFrames);
	}
	mesh.collisionBoxes.clear();
	for (int i = 0; i < modelCollisionBoxLimit; ++i)
	{
		ModelCollisionBox box;
		box.name = QStringLiteral("box_%1").arg(i);
		QVector<ModelCollisionPose> poses;
		for (int frame = 0; frame < modelDocumentMaxFrames; ++frame)
			poses << ModelCollisionPose{{float(frame), float(i), 0}, {4, 6, 8}, {0, 0, float(frame)}};
		setModelCollisionFrames(&box, std::move(poses));
		mesh.collisionBoxes << std::move(box);
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace vibestudio::tests
