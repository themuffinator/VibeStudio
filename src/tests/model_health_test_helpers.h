#pragma once

#include "tests/model_uv_test_helpers.h"

namespace vibestudio::tests
{
inline ModelMesh healthFixture()
{
	auto mesh = uvSquare();
	auto &s = mesh.surfaces[0];
	s.triangles = {{0, 1, 2}, {0, 3, 2}, {2, 1, 0}, {0, 4, 5}};
	s.texCoords << ModelTexCoord{-.5f, 0} << ModelTexCoord{0, -.5f} << ModelTexCoord{7, 9};
	for (int f = 0; f < s.frames.size(); ++f)
	{
		auto &pose = s.frames[f];
		pose.positions << ModelVec3{-10, 0, float(f * 8)} << ModelVec3{0, -10, float(f * 8)} << ModelVec3{88, 88, float(f * 8)};
		pose.normals.fill({.2f, .1f, .95f}, 7);
	}
	s.uvSeams = {{0, 2}, {0, 4}};
	s.skinPaths = {QStringLiteral("models/props/health.png")};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace vibestudio::tests
