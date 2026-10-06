#pragma once
#include "core/model_design.h"
#include "core/model_document.h"

namespace vibestudio::tests
{
inline ModelMesh skinBindingMesh()
{
	ModelDesign design;
	design.parts.append(ModelDesignPart{});
	auto mesh = buildModelDesignMesh(design);
	mesh.surfaces[0].name = QStringLiteral("BODY_1");
	mesh.surfaces[0].skinPaths = {QStringLiteral("models/old_body"), QStringLiteral("models/alternate")};
	auto head = mesh.surfaces[0];
	head.name = QStringLiteral("head");
	head.skinPaths = {QStringLiteral("models/old_head")};
	for (auto &position : head.frames[0].positions)
	{
		position.x += 64;
	}
	mesh.surfaces.append(head);
	mesh.frames[0].name = QStringLiteral("rest");
	mesh.frames.append(mesh.frames[0]);
	mesh.frames[1].name = QStringLiteral("lift");
	for (auto &surface : mesh.surfaces)
	{
		surface.frames.append(surface.frames[0]);
		for (auto &position : surface.frames[1].positions)
		{
			position.z += 8;
		}
	}
	for (int frame = 0; frame < 2; ++frame)
	{
		ModelTag tag;
		tag.name = QStringLiteral("tag_mount");
		tag.frameIndex = frame;
		tag.origin = {0, 0, float(8 * frame)};
		mesh.tags.append(tag);
	}
	mesh.collisionBoxes.append({QStringLiteral("body"), {}, {16, 16, 16}, {}});
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline QByteArray skinBindings()
{
	return "// Original surface/shader fixture\nBoDy,models/new_body\n\"head\",\"models/new_head\"\n"
		   "tag_mount,\n/* for another mesh */ other,models/unused\n";
}
} // namespace vibestudio::tests
