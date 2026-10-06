#pragma once

#include "core/model_document.h"
#include <array>

namespace vibestudio::tests
{
inline ModelMesh intersectionFixture(bool self = false)
{
	ModelMesh mesh;
	mesh.geometryAvailable = true;
	mesh.sourcePath = QStringLiteral("tests/intersections.mesh.json");
	const std::array<ModelVec3, 3> floor{{{-2, -2, 0}, {2, -2, 0}, {0, 2, 0}}};
	const std::array<std::array<ModelVec3, 3>, 3> moving{
		{{{{0, -1, 3}, {0, -1, 5}, {0, 1, 4}}}, {{{0, -1, -1}, {0, -1, 1}, {0, 1, 0}}}, {{{-1, -1, 0}, {1, -1, 0}, {0, 1, 0}}}}};
	for (int index = 0; index < 3; ++index)
	{
		ModelSurface surface;
		surface.name = index == 0 ? QStringLiteral("floor") : index == 1 ? QStringLiteral("moving") : QStringLiteral("distant");
		surface.skinPaths = {QStringLiteral("models/props/grid.pcx")};
		surface.vertexCount = 3;
		surface.triangles = {{0, 1, 2}};
		surface.texCoords = {{0, 0}, {1, 0}, {.5f, 1}};
		surface.uvSeams = {{0, 1}};
		for (int frame = 0; frame < 3; ++frame)
		{
			ModelFrameGeometry pose;
			for (const auto vertex : index == 1 ? moving[frame] : floor)
				pose.positions.append({vertex.x + (index == 2 ? 12 : 0), vertex.y, vertex.z});
			pose.normals.fill({0, 0, 1}, 3);
			surface.frames.append(pose);
		}
		mesh.surfaces.append(surface);
	}
	for (int frame = 0; frame < 3; ++frame)
	{
		ModelFrameInfo info;
		info.name = QStringLiteral("scan%1").arg(frame);
		mesh.frames.append(info);
		ModelTag tag;
		tag.name = QStringLiteral("tag_mount");
		tag.frameIndex = frame;
		tag.origin = {0, 0, float(frame)};
		mesh.tags.append(tag);
	}
	mesh.animations = {{QStringLiteral("scan"), 0, 3, 12.5}};
	mesh.collisionBoxes = {{QStringLiteral("body"), {0, 0, 0}, {1, 1, 1}, {0, 0, 0}}};
	if (self)
	{
		auto &surface = mesh.surfaces[0];
		surface.texCoords += mesh.surfaces[1].texCoords;
		surface.triangles.append({3, 4, 5});
		for (int frame = 0; frame < 3; ++frame)
		{
			surface.frames[frame].positions += mesh.surfaces[1].frames[frame].positions;
			surface.frames[frame].normals += mesh.surfaces[1].frames[frame].normals;
		}
		mesh.surfaces.removeAt(1);
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline ModelMesh intersectionCrowd(int faces = 362)
{
	auto mesh = intersectionFixture();
	mesh.surfaces.resize(1);
	mesh.surfaces[0].frames.resize(1);
	mesh.surfaces[0].triangles.fill({0, 1, 2}, faces);
	mesh.frames.resize(1);
	mesh.tags.clear();
	mesh.animations.clear();
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace vibestudio::tests
