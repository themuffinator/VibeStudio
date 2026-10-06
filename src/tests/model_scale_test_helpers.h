#pragma once

#include "core/model_document.h"

namespace vibestudio::tests
{
inline ModelMesh maximumEditableGrid()
{
	// Original, nondegenerate indexed geometry at the editable vertex and
	// frame-vertex limits, within 1% of the triangle limit. Every pose differs.
	constexpr int side = 256, frames = 16;
	ModelMesh mesh;
	mesh.geometryAvailable = true;
	mesh.sourcePath = QStringLiteral("tests/maximum-grid.mesh.json");
	ModelSurface surface;
	surface.vertexCount = side * side;
	surface.name = QStringLiteral("grid");
	for (int y = 0; y < side; ++y)
	{
		for (int x = 0; x < side; ++x)
		{
			surface.texCoords.append({float(x) / (side - 1), float(y) / (side - 1)});
			if (x + 1 < side && y + 1 < side)
			{
				const int a = y * side + x, b = a + 1, c = a + side, d = c + 1;
				surface.triangles.append({a, b, c});
				surface.triangles.append({b, d, c});
			}
		}
	}
	for (int f = 0; f < frames; ++f)
	{
		ModelFrameGeometry pose;
		for (int y = 0; y < side; ++y)
		{
			for (int x = 0; x < side; ++x)
			{
				pose.positions.append({float(x - 128), float(y - 128), float(f)});
			}
		}
		pose.normals.fill({0, 0, 1}, side * side);
		surface.frames.append(pose);
		ModelFrameInfo info;
		info.name = QStringLiteral("rise%1").arg(f);
		mesh.frames.append(info);
	}
	mesh.surfaces.append(surface);
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace vibestudio::tests
