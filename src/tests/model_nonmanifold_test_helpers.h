#pragma once

#include "tests/model_uv_test_helpers.h"

#include <cmath>
#include <numbers>

namespace vibestudio::tests
{
// Original radial pages sharing one indexed spine. Every pose stays valid;
// the two spine vertices need independent copies for every page after repair.
inline ModelMesh nonmanifoldBook(int pages = 3, int frames = 2, bool unused = true)
{
	auto mesh = uvSquare();
	mesh.tags.clear();
	mesh.frames.clear();
	mesh.animations = {{QStringLiteral("book"), 0, frames, 7.5}};
	auto &surface = mesh.surfaces[0];
	surface.name = QStringLiteral("book");
	surface.skinPaths = {QStringLiteral("models/props/repair.pcx")};
	surface.texCoords = {{0, 0}, {1, 0}};
	surface.frames.clear();
	surface.triangles.clear();
	surface.uvSeams = {{0, 1}, {0, 2}, {0, 3}};
	ModelFrameGeometry pose;
	pose.positions = {{0, 0, 0}, {20, 0, 0}};
	for (int i = 0; i < pages; ++i)
	{
		const double angle = 2 * std::numbers::pi * i / pages;
		pose.positions << ModelVec3{5, float(10 * std::cos(angle)), float(10 * std::sin(angle))};
		surface.texCoords << ModelTexCoord{.5f, float(i) / (pages - 1)};
		surface.triangles << (i % 2 ? ModelTriangle{1, 0, i + 2} : ModelTriangle{0, 1, i + 2});
	}
	if (unused)
	{
		pose.positions << ModelVec3{99, 99, 99};
		surface.texCoords << ModelTexCoord{.75f, .75f};
	}
	pose.normals.fill({0, 0, 1}, pose.positions.size());
	for (int f = 0; f < frames; ++f)
	{
		ModelFrameInfo info;
		info.name = QStringLiteral("book%1").arg(f);
		mesh.frames << info;
		auto animated = pose;
		for (auto &point : animated.positions)
		{
			point.x *= 1 + f * .01f;
			point.y *= 1 + f * .02f;
			point.z += f * 8;
		}
		surface.frames << animated;
		ModelTag tag;
		tag.name = QStringLiteral("tag_mount");
		tag.frameIndex = f;
		tag.origin = {1, 2, float(3 + f * 8)};
		mesh.tags << tag;
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace vibestudio::tests
