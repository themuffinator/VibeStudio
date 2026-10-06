#pragma once

#include "core/model_document.h"

namespace vibestudio::tests
{
// Original open prism fixtures; no game assets or external geometry.
inline ModelMesh boundaryPrism(QVector<ModelVec3> top = {{-2, -2, 2}, {2, -2, 2}, {2, 2, 2}, {-2, 2, 2}}, bool bottom = true)
{
	ModelMesh mesh;
	mesh.sourcePath = QStringLiteral("models/boundary.mesh.json");
	mesh.geometryAvailable = true;
	mesh.format = ModelMeshFormat::Quake3Md3;
	mesh.formatId = QStringLiteral("md3");
	mesh.formatName = QStringLiteral("Quake III MD3");
	mesh.version = 15;
	ModelSurface surface;
	surface.name = QStringLiteral("boundary");
	surface.skinPaths = {QStringLiteral("models/boundary.tga"), QStringLiteral("models/alternate.tga")};
	const int size = top.size();
	ModelFrameGeometry pose;
	for (auto p : top)
	{
		pose.positions << ModelVec3{p.x, p.y, -2};
	}
	pose.positions += top;
	for (int i = 0; i < 2 * size; ++i)
	{
		pose.normals << ModelVec3{.2f, .1f, .95f};
		surface.texCoords << ModelTexCoord{float(i % size) / size, i < size ? 0.f : 1.f};
	}
	for (int i = 0; i < size; ++i)
	{
		const int next = (i + 1) % size;
		surface.triangles << ModelTriangle{i, next, size + next} << ModelTriangle{i, size + next, size + i};
	}
	if (bottom)
	{
		for (int i = 1; i + 1 < size; ++i)
		{
			surface.triangles << ModelTriangle{0, i + 1, i};
		}
	}
	surface.uvSeams = {{size, size + 1}};
	surface.frames << pose;
	for (auto &p : pose.positions)
	{
		p = {1.1f * p.x + 4, .9f * p.y - 3, p.z + .25f * p.x + 1};
	}
	surface.frames << pose;
	mesh.surfaces << surface;
	mesh.frames = {{0, QStringLiteral("idle0"), {}, {}, {}}, {1, QStringLiteral("idle1"), {}, {}, {}}};
	mesh.animations = {{QStringLiteral("idle"), 0, 2}};
	mesh.tags = {{QStringLiteral("tag_mount"), 0, {0, 0, 4}}, {QStringLiteral("tag_mount"), 1, {4, -3, 5}}};
	mesh.collisionBoxes = {{QStringLiteral("body"), {0, 0, 0}, {4, 4, 4}, {0, 0, 15}}};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline ModelMesh boundaryChangingConcavity()
{
	auto mesh = boundaryPrism();
	mesh.surfaces[0].frames[1] = mesh.surfaces[0].frames[0];
	mesh.surfaces[0].frames[1].positions[6] = {-1, -1, 2};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline void appendBoundaryObstacle(ModelMesh *mesh, QVector<ModelVec3> points)
{
	auto &surface = mesh->surfaces[0];
	const int start = surface.vertexCount;
	for (auto &pose : surface.frames)
	{
		pose.positions += points;
		for (int i = 0; i < points.size(); ++i)
		{
			pose.normals << ModelVec3{0, 0, 1};
		}
	}
	for (int i = 0; i < points.size(); ++i)
	{
		surface.texCoords << ModelTexCoord{0, 0};
	}
	surface.triangles << ModelTriangle{start, start + 1, start + 2};
	updateEditableModelMetadata(mesh);
}
} // namespace vibestudio::tests
