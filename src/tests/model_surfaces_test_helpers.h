#pragma once
#include "core/model_document.h"
#include <QJsonDocument>

namespace vibestudio::tests
{
inline ModelMesh surfaceFixture()
{
	ModelMesh mesh;
	mesh.geometryAvailable = true;
	mesh.frames = {{0, "pose0", {}, {}, {}}, {1, "pose1", {}, {}, {}}};
	mesh.animations = {{"move", 0, 2}};
	mesh.tags = {{"tag_mount", 0, {}}, {"tag_mount", 1, {1, 2, 3}}};
	mesh.collisionBoxes = {{"body", {}, {2, 4, 6}, {}}};
	mesh.md2SkinSize = {128, 64};
	for (int s = 0; s < 3; ++s)
	{
		ModelSurface surface;
		surface.name = QStringLiteral("part%1").arg(s);
		surface.vertexCount = 6;
		// Last vertex intentionally unused. Faces share edges 0:2 and 2:3.
		surface.triangles = {{0, 1, 2}, {0, 2, 3}, {3, 2, 4}};
		surface.texCoords = {{.1f, .2f}, {.2f, .3f}, {.3f, .4f}, {.4f, .5f}, {.5f, .6f}, {.8f, .9f}};
		surface.skinPaths = {"models/body.tga", "models/body_alt.tga"};
		surface.uvSeams = {{0, 1}, {0, 2}, {2, 3}, {2, 4}};
		for (int f = 0; f < 2; ++f)
		{
			ModelFrameGeometry pose;
			pose.positions = {{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}, {0, 4, 0}, {8, 7, 6}};
			for (int v = 0; v < 6; ++v)
			{
				pose.positions[v].x += 10.f * s;
				pose.positions[v].z += f * (.5f + .125f * v);
				pose.normals.append({.1f * v, .2f * f, 1.f});
			}
			surface.frames.append(pose);
		}
		mesh.surfaces.append(surface);
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline QByteArray surfaceBytes(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}
inline bool exactVertex(const ModelSurface &a, int av, const ModelSurface &b, int bv)
{
	if (a.texCoords[av].u != b.texCoords[bv].u || a.texCoords[av].v != b.texCoords[bv].v || a.frames.size() != b.frames.size())
		return false;
	for (int f = 0; f < a.frames.size(); ++f)
	{
		const auto ap = a.frames[f].positions[av], bp = b.frames[f].positions[bv];
		const auto an = a.frames[f].normals[av], bn = b.frames[f].normals[bv];
		if (ap.x != bp.x || ap.y != bp.y || ap.z != bp.z || an.x != bn.x || an.y != bn.y || an.z != bn.z)
			return false;
	}
	return true;
}
inline bool exactFace(const ModelSurface &a, int af, const ModelSurface &b, int bf)
{
	const auto at = a.triangles[af], bt = b.triangles[bf];
	return exactVertex(a, at.a, b, bt.a) && exactVertex(a, at.b, b, bt.b) && exactVertex(a, at.c, b, bt.c);
}
} // namespace vibestudio::tests
