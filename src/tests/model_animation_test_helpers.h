#pragma once

#include "tests/model_uv_test_helpers.h"

namespace vibestudio::tests
{
inline ModelMesh animationFixture()
{
	auto mesh = uvSquare();
	mesh.frames << mesh.frames[1];
	mesh.frames[0].name = QStringLiteral("start");
	mesh.frames[1].name = QStringLiteral("end");
	mesh.frames[2].name = QStringLiteral("later");
	for (int f = 0; f < 3; ++f)
	{
		mesh.frames[f].origin = {float(f * 4), float(f * 4 + 2), float(f * 4 + 4)};
	}
	auto &surface = mesh.surfaces[0];
	surface.frames << surface.frames[1];
	surface.skinPaths = {QStringLiteral("models/test/body.tga")};
	surface.uvSeams = {{0, 2}};
	for (int f = 0; f < 3; ++f)
	{
		for (auto &p : surface.frames[f].positions)
		{
			p.z = float(f * 8);
		}
		surface.frames[f].normals.fill(f == 0 ? ModelVec3{0, 0, 2} : f == 1 ? ModelVec3{0, 3, 0} : ModelVec3{2, 0, 0}, 4);
	}
	auto second = surface;
	second.name = QStringLiteral("second_surface");
	second.skinPaths = {QStringLiteral("models/test/trim.tga")};
	for (auto &frame : second.frames)
	{
		for (auto &p : frame.positions)
		{
			p.x += 20;
		}
	}
	mesh.surfaces << second;
	mesh.tags.clear();
	for (int f = 0; f < 3; ++f)
	{
		ModelTag tag;
		tag.name = QStringLiteral("tag_mount");
		tag.frameIndex = f;
		tag.origin = {1, 2, float(3 + f * 8)};
		const float cosine = f == 0 ? 1.f : f == 1 ? 0.f : -1.f, sine = f == 1 ? 1.f : 0.f;
		tag.axis[0] = cosine;
		tag.axis[1] = sine;
		tag.axis[3] = -sine;
		tag.axis[4] = cosine;
		mesh.tags << tag;
		tag.name = QStringLiteral("tag_reflected");
		tag.axis[8] = -1;
		mesh.tags << tag;
	}
	mesh.animations = {{QStringLiteral("span"), 0, 2},
					   {QStringLiteral("first_only"), 0, 1},
					   {QStringLiteral("second_only"), 1, 1},
					   {QStringLiteral("later_only"), 2, 1},
					   {QStringLiteral("tail"), 1, 2}};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace vibestudio::tests
