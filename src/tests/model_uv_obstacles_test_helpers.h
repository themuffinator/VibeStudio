#pragma once
#include "tests/model_uv_rect_test_helpers.h"

namespace vibestudio::tests
{
inline void obstacleRect(ModelSurface &s, int first, float x, float y, float w, float h)
{
	s.texCoords[first] = {x, y};
	s.texCoords[first + 1] = {x + w, y};
	s.texCoords[first + 2] = {x + w, y + h};
	s.texCoords[first + 3] = {x, y + h};
}
// Original animated islands, fixed painted panels, a shared alternate material
// and an unrelated full-tile material. The selected islands initially overlap.
inline ModelMesh obstaclePanels()
{
	auto mesh = atlasPanels(4);
	auto &s = mesh.surfaces[0];
	s.skinPaths << QStringLiteral("models/alternate.png");
	obstacleRect(s, 0, .05f, .1f, .18f, .2f);
	obstacleRect(s, 4, .05f, .1f, .1f, .15f);
	obstacleRect(s, 8, 0, 0, .35f, 1);
	obstacleRect(s, 12, .5f, .75f, .5f, .25f);
	auto related = atlasPanels(1).surfaces[0];
	related.name = QStringLiteral("shared_painted_panel");
	related.skinPaths = {QStringLiteral("MODELS\\ALTERNATE.PNG")};
	obstacleRect(related, 0, .8f, 0, .2f, .5f);
	mesh.surfaces << related;
	auto unrelated = related;
	unrelated.name = QStringLiteral("separate_material");
	unrelated.skinPaths = {QStringLiteral("models/separate.png")};
	obstacleRect(unrelated, 0, 0, 0, 1, 1);
	mesh.surfaces << unrelated;
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline QSet<int> obstacleFaces()
{
	return {0, 1, 2, 3};
}

inline ModelMesh maximumObstacleStrip()
{
	// Exactly 65,536 vertices and 1,048,576 pose vertices, including a fixed
	// painted panel. A long, finely tessellated strip avoids making the unrelated
	// overlap-comparison ceiling the bottleneck of the responsiveness workload.
	constexpr int columns = 32766, poses = 16;
	ModelMesh mesh;
	mesh.geometryAvailable = true;
	mesh.sourcePath = QStringLiteral("tests/maximum-obstacle-strip.mesh.json");
	ModelSurface s;
	s.name = QStringLiteral("strip_and_fixed_panel");
	s.skinPaths = {QStringLiteral("models/atlas.png")};
	for (int row = 0; row < 2; ++row)
		for (int x = 0; x < columns; ++x)
			s.texCoords.append({float(x) / (columns - 1) * .45f, row * .3f});
	for (int x = 0; x + 1 < columns; ++x)
	{
		s.triangles.append({x, x + 1, x + columns});
		s.triangles.append({x + 1, x + columns + 1, x + columns});
	}
	const int first = s.texCoords.size();
	s.texCoords << ModelTexCoord{0, 0} << ModelTexCoord{.4f, 0} << ModelTexCoord{.4f, 1} << ModelTexCoord{0, 1};
	s.triangles << ModelTriangle{first, first + 1, first + 2} << ModelTriangle{first, first + 2, first + 3};
	for (int f = 0; f < poses; ++f)
	{
		ModelFrameGeometry frame;
		for (int row = 0; row < 2; ++row)
			for (int x = 0; x < columns; ++x)
				frame.positions.append({float(x), row * 100.f, float(f)});
		frame.positions << ModelVec3{-20, 0, float(f)} << ModelVec3{-10, 0, float(f)} << ModelVec3{-10, 10, float(f)}
						<< ModelVec3{-20, 10, float(f)};
		frame.normals.fill({0, 0, 1}, s.texCoords.size());
		s.frames.append(frame);
		ModelFrameInfo info;
		info.name = QStringLiteral("rise%1").arg(f);
		mesh.frames.append(info);
	}
	mesh.surfaces.append(s);
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace vibestudio::tests
