#pragma once

#include "core/model_assembly.h"

namespace vibestudio::tests
{
inline ModelMesh assemblyModel()
{
	ModelMesh mesh;
	mesh.geometryAvailable = true;
	ModelSurface surface;
	surface.name = QStringLiteral("triangle");
	surface.vertexCount = 3;
	surface.triangles = {{0, 1, 2}};
	surface.texCoords = {{0, 0}, {1, 0}, {0, 1}};
	surface.skinPaths = {QStringLiteral("textures/assembly.png")};
	for (int i = 0; i < 2; ++i)
	{
		ModelFrameGeometry frame;
		frame.positions = {{0, 0, float(i)}, {4, 0, float(i)}, {0, 4, float(i)}};
		frame.normals.fill({0, 0, 1}, 3);
		surface.frames.append(frame);
		ModelFrameInfo info;
		info.name = QStringLiteral("pose%1").arg(i);
		mesh.frames.append(info);
		ModelTag tag;
		tag.name = QStringLiteral("tag_link");
		tag.frameIndex = i;
		tag.origin = {float(10 + 10 * i), 0, 0};
		mesh.tags.append(tag);
	}
	mesh.surfaces.append(surface);
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline ModelAssembly assemblyRecipe(const QString &path)
{
	ModelAssembly recipe;
	recipe.name = QStringLiteral("Synthetic assembly");
	ModelAssemblyPart root;
	root.id = QStringLiteral("root");
	root.source = path;
	root.framesPerSecond = 1;
	ModelAssemblyPart child = root;
	child.id = QStringLiteral("child");
	child.parent = root.id;
	child.tag = QStringLiteral("tag_link");
	child.framesPerSecond = 2;
	recipe.parts = {root, child};
	return recipe;
}
} // namespace vibestudio::tests
