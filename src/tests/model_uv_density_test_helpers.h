#pragma once
#include "tests/model_rig_test_helpers.h"
#include "tests/model_uv_islands_test_helpers.h"

namespace vibestudio::tests
{
inline ModelMesh densityFixture()
{
	auto mesh = uvSquare();
	auto &s = mesh.surfaces[0];
	const auto source = s;
	for (const auto uv : source.texCoords)
		s.texCoords.append({uv.u * 2 + 3, uv.v * 2});
	for (const auto t : source.triangles)
		s.triangles.append({t.a + 4, t.b + 4, t.c + 4});
	for (int f = 0; f < s.frames.size(); ++f)
	{
		for (auto p : source.frames[f].positions)
		{
			p.x += 20;
			s.frames[f].positions.append(p);
		}
		for (auto n : source.frames[f].normals)
			s.frames[f].normals.append(n);
		if (f)
			for (auto &p : s.frames[f].positions)
			{
				p.x *= 2;
				p.y *= 2;
			}
	}
	s.vertexCount = s.texCoords.size();
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline ModelMesh densitySkinnedFixture()
{
	auto mesh = uvIslandsFixture();
	mesh.surfaces.resize(1);
	auto &s = mesh.surfaces[0];
	const auto source = s;
	for (const auto uv : source.texCoords)
		s.texCoords.append(uv);
	for (const auto t : source.triangles)
		s.triangles.append({t.a + 8, t.b + 8, t.c + 8});
	for (auto e : source.uvSeams)
		s.uvSeams.insert(modelEdge(e.first + 8, e.second + 8));
	for (int f = 0; f < s.frames.size(); ++f)
	{
		s.frames[f].positions += source.frames[f].positions;
		s.frames[f].normals += source.frames[f].normals;
	}
	s.vertexCount = s.texCoords.size();
	mesh.skeleton = rigFixture().skeleton;
	QVector<int> joints(16, 0);
	for (int v = 8; v < 16; ++v)
		joints[v] = 1;
	if (!modelSkinningFromBindPose(mesh.skeleton, s.frames[0].positions, s.frames[0].normals, joints, QVector<float>(16, 1), 1,
								   &s.skinning) ||
		!bakeModelSkeleton(&mesh))
		return {};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace vibestudio::tests
