#pragma once

#include "core/model_document.h"
#include <cmath>
#include <numbers>

namespace vibestudio::tests
{
// Original disconnected end disks. Joining their perimeters creates a closed
// prism/frustum; no upstream geometry or game assets are used.
inline ModelMesh bridgeDisks(int lower = 4, int upper = 4)
{
	ModelMesh mesh;
	mesh.sourcePath = QStringLiteral("models/bridge.mesh.json");
	mesh.geometryAvailable = true;
	mesh.format = ModelMeshFormat::Quake3Md3;
	mesh.formatId = QStringLiteral("md3");
	mesh.formatName = QStringLiteral("Quake III MD3");
	mesh.version = 15;
	ModelSurface surface;
	surface.name = QStringLiteral("bridge");
	surface.skinPaths = {QStringLiteral("models/bridge.pcx"), QStringLiteral("models/alternate.pcx")};
	ModelFrameGeometry pose;
	for (int ring = 0; ring < 2; ++ring)
	{
		const int count = ring ? upper : lower, first = pose.positions.size();
		for (int i = 0; i < count; ++i)
		{
			const double angle = -3 * std::numbers::pi / 4 + 2 * std::numbers::pi * i / count;
			ModelVec3 p{float(std::sqrt(8.0) * std::cos(angle)), float(std::sqrt(8.0) * std::sin(angle)), ring ? 2.f : -2.f};
			if (count == 4)
				p = {i == 0 || i == 3 ? -2.f : 2.f, i < 2 ? -2.f : 2.f, p.z};
			pose.positions << p;
			pose.normals << ModelVec3{0, 0, ring ? 1.f : -1.f};
			surface.texCoords << ModelTexCoord{p.x / 6 + .5f, p.y / 6 + .5f};
		}
		for (int i = 1; i + 1 < count; ++i)
			surface.triangles << (ring ? ModelTriangle{first, first + i, first + i + 1}
				: ModelTriangle{first, first + i + 1, first + i});
	}
	surface.uvSeams = {{0, 1}, {lower, lower + 1}};
	surface.frames << pose;
	for (auto &p : pose.positions)
		p = {p.x * 1.1f + 3, p.y * .8f - 5, p.z + .2f * p.x};
	surface.frames << pose;
	mesh.surfaces << surface;
	mesh.frames = {{0, QStringLiteral("pose0"), {}, {}, {}}, {1, QStringLiteral("pose1"), {}, {}, {}}};
	mesh.animations = {{QStringLiteral("motion"), 0, 2, 23.976}};
	mesh.tags = {{QStringLiteral("tag_mount"), 0, {0, 0, 4}}, {QStringLiteral("tag_mount"), 1, {3, -5, 4}}};
	mesh.collisionBoxes = {{QStringLiteral("body"), {0, 0, 0}, {4, 4, 4}, {0, 0, 0}}};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline ModelMesh collapsingBridge()
{
	auto mesh = bridgeDisks();
	mesh.surfaces[0].frames[1] = mesh.surfaces[0].frames[0];
	for (int vertex = 4; vertex < 8; ++vertex)
		mesh.surfaces[0].frames[1].positions[vertex].z = -2;
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace vibestudio::tests
