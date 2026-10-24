#pragma once

#include "core/model_document.h"

#include <cmath>

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

// A deterministic generator that gives the same values on every compiler and
// standard library (std::uniform_real_distribution does not).
class ScaleTestRandom
{
  public:
	explicit ScaleTestRandom(quint32 seed) : m_state(seed) {}
	// A float in [0, 1) with 24 random bits.
	float unit()
	{
		m_state = m_state * 1664525u + 1013904223u;
		return float(m_state >> 8) / float(1 << 24);
	}

  private:
	quint32 m_state;
};

inline ModelMesh realisticMaximumEditableGrid()
{
	// The same limits as maximumEditableGrid, with the numbers a real model
	// stores: positions with fractional parts across +-4096 units and unit
	// normals at arbitrary angles, so every number needs its full float
	// precision in a source. Integer positions and constant normals print in
	// a few characters each and hide the size of a realistic source.
	constexpr int side = 256;
	auto mesh = maximumEditableGrid();
	ScaleTestRandom random(0x9e3779b9u);
	auto &surface = mesh.surfaces[0];
	for (int f = 0; f < surface.frames.size(); ++f)
	{
		auto &pose = surface.frames[f];
		for (int y = 0; y < side; ++y)
		{
			for (int x = 0; x < side; ++x)
			{
				const int v = y * side + x;
				pose.positions[v] = {float(x - 128) * 32.0f + random.unit() * 8.0f, float(y - 128) * 32.0f + random.unit() * 8.0f,
									 float(f) * 3.7f + random.unit() * 64.0f};
				const float nx = random.unit() - 0.5f, ny = random.unit() - 0.5f;
				const float length = std::sqrt(nx * nx + ny * ny + 1.0f);
				pose.normals[v] = {nx / length, ny / length, 1.0f / length};
			}
		}
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace vibestudio::tests
