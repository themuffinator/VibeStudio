#pragma once

#include "core/model_design.h"
#include "core/model_document.h"

namespace vibestudio::tests
{
inline ModelMesh uvSquare()
{
	ModelDesign design;
	ModelDesignPart part;
	part.name = QStringLiteral("uv_panel");
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	auto mesh = buildModelDesignMesh(design);
	mesh.frames << mesh.frames[0];
	mesh.frames[1].name = QStringLiteral("pose2");
	mesh.animations = {{QStringLiteral("poses"), 0, 2}};
	auto &surface = mesh.surfaces[0];
	surface.triangles = {{0, 1, 2}, {0, 2, 3}};
	surface.texCoords = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	ModelFrameGeometry frame;
	frame.positions = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
	frame.normals.fill({0, 0, 1}, 4);
	surface.frames = {frame, frame};
	for (auto &point : surface.frames[1].positions)
	{
		point.z += 8;
	}
	ModelTag tag;
	tag.name = QStringLiteral("tag_mount");
	tag.origin = {1, 2, 3};
	mesh.tags << tag;
	tag.frameIndex = 1;
	tag.origin.z += 8;
	mesh.tags << tag;
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline ModelSurface uvGrid(int cells, int rowCells = -1)
{
	if (rowCells < 0)
	{
		rowCells = cells;
	}
	ModelSurface surface;
	surface.name = QStringLiteral("uv_grid");
	for (int y = 0; y <= rowCells; ++y)
	{
		for (int x = 0; x <= cells; ++x)
		{
			surface.texCoords.append({float(x) / cells, float(y) / rowCells});
		}
	}
	for (int y = 0; y < rowCells; ++y)
	{
		for (int x = 0; x < cells; ++x)
		{
			const int a = y * (cells + 1) + x, b = a + 1, c = a + cells + 2, d = c - 1;
			surface.triangles.append({a, b, c});
			surface.triangles.append({a, c, d});
		}
	}
	surface.vertexCount = surface.texCoords.size();
	return surface;
}
} // namespace vibestudio::tests
