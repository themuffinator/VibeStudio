#pragma once

#include "tests/model_uv_test_helpers.h"

#include <QJsonDocument>
#include <cmath>

namespace vibestudio::tests
{
inline ModelMesh uvIslandsFixture()
{
	auto mesh = uvSquare();
	auto &surface = mesh.surfaces[0];
	surface.texCoords = {{0, 0}, {2, 0}, {2, 2}, {0, 2}, {4, 0}, {4, 2}, {2, 4}, {7, 9}};
	surface.triangles = {{0, 1, 2}, {0, 2, 3}, {1, 4, 5}, {1, 5, 2}, {3, 2, 6}};
	surface.uvSeams = {{0, 1}, {1, 2}, {2, 3}};
	surface.skinPaths = {QStringLiteral("textures/uv/checker"), QStringLiteral("textures/uv/alternate")};
	surface.vertexCount = 8;
	for (int pose = 0; pose < surface.frames.size(); ++pose)
	{
		auto &frame = surface.frames[pose];
		frame.positions.clear();
		frame.normals.clear();
		for (int vertex = 0; vertex < 8; ++vertex)
		{
			const auto uv = surface.texCoords[vertex];
			frame.positions.append({uv.u * 10 + pose * 2.f, uv.v * 10 - pose * 3.f, float(pose * vertex)});
			frame.normals.append(vertex % 2 ? ModelVec3{0, .6f, .8f} : ModelVec3{0, 0, 1});
		}
	}
	auto other = surface;
	other.name = QStringLiteral("untouched");
	mesh.surfaces.append(other);
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline QByteArray uvIslandBytes(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}
inline bool uvNear(ModelTexCoord a, ModelTexCoord b)
{
	return std::hypot(double(a.u) - b.u, double(a.v) - b.v) < .00001;
}
inline bool uvSamePoint(ModelVec3 a, ModelVec3 b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}
inline bool uvSamePoses(const ModelSurface &a, const ModelSurface &b)
{
	if (a.frames.size() != b.frames.size() || a.triangles.size() != b.triangles.size())
		return false;
	for (int frame = 0; frame < a.frames.size(); ++frame)
		for (int face = 0; face < a.triangles.size(); ++face)
		{
			const auto ta = a.triangles[face], tb = b.triangles[face];
			const int ia[]{ta.a, ta.b, ta.c}, ib[]{tb.a, tb.b, tb.c};
			for (int corner = 0; corner < 3; ++corner)
				if (!uvSamePoint(a.frames[frame].positions[ia[corner]], b.frames[frame].positions[ib[corner]]) ||
					!uvSamePoint(a.frames[frame].normals[ia[corner]], b.frames[frame].normals[ib[corner]]))
					return false;
		}
	return true;
}
} // namespace vibestudio::tests
