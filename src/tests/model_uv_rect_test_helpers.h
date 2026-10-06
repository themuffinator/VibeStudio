#pragma once

#include "tests/model_uv_test_helpers.h"

namespace vibestudio::tests
{
// Original independent rectangular panels with two poses and authored UVs.
inline ModelMesh atlasPanels(int count = 5)
{
	auto mesh = uvSquare();
	auto &s = mesh.surfaces[0];
	s.triangles.clear();
	s.texCoords.clear();
	s.frames = {{}, {}};
	s.skinPaths = {QStringLiteral("models/atlas.png")};
	for (int i = 0; i < count; ++i)
	{
		const float width = i % 3 == 0 ? 10.f : i % 3 == 1 ? 20.f : 5.f;
		const int base = s.texCoords.size();
		const ModelVec3 origin{float(i * 30), 0, 0};
		for (auto p : {ModelVec3{0, 0, 0}, {width, 0, 0}, {width, 10, 0}, {0, 10, 0}})
		{
			s.texCoords << ModelTexCoord{p.x / 20 + i * .1f, p.y / 20 + i * .2f};
			s.frames[0].positions << ModelVec3{p.x + origin.x, p.y, 0};
			s.frames[1].positions << ModelVec3{p.x + origin.x + 3, p.y * 1.2f, 8};
			s.frames[0].normals << ModelVec3{0, 0, 1};
			s.frames[1].normals << ModelVec3{0, 0, 1};
		}
		s.triangles << ModelTriangle{base, base + 1, base + 2} << ModelTriangle{base, base + 2, base + 3};
	}
	mesh.animations[0].framesPerSecond = 23.976;
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline QSet<int> atlasFaces(const ModelMesh &mesh)
{
	QSet<int> faces;
	for (int i = 0; i < mesh.surfaces[0].triangles.size(); ++i) faces.insert(i);
	return faces;
}
} // namespace vibestudio::tests
