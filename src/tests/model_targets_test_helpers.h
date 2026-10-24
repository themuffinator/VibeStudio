#pragma once

// Fixtures at the delivery targets' limits: flat grids of distinct vertices
// (so no writer can share any), an MDL version of a grid, an MD2-ready quad
// with many frames, and the skeletal arm with extra joints.

#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_mdl.h"
#include "core/model_skeleton.h"
#include "tests/model_mdl_test_helpers.h"
#include "tests/model_skeletal_export_test_helpers.h"

namespace vibestudio::tests
{
// A surface of `columns` by `rows` vertices, 4 units apart, with
// (columns - 1) * (rows - 1) * 2 counter-clockwise triangles and UVs inside the
// tile.
inline ModelSurface targetGridSurface(const QString &name, int columns, int rows, float z = 0)
{
	ModelSurface surface;
	surface.name = name;
	ModelFrameGeometry frame;
	for (int y = 0; y < rows; ++y)
		for (int x = 0; x < columns; ++x)
		{
			frame.positions.append({float(x * 4), float(y * 4), z});
			frame.normals.append({0, 0, 1});
			surface.texCoords.append({(x + 0.5f) / float(columns), (y + 0.5f) / float(rows)});
			if (x + 1 < columns && y + 1 < rows)
			{
				const int a = y * columns + x;
				surface.triangles.append({a, a + 1, a + columns + 1});
				surface.triangles.append({a, a + columns + 1, a + columns});
			}
		}
	surface.vertexCount = int(frame.positions.size());
	surface.frames = {frame};
	surface.skinPaths = {QStringLiteral("models/test/%1").arg(name)};
	return surface;
}

// An editable model from surfaces of one frame each.
inline ModelMesh targetMesh(const QVector<ModelSurface> &surfaces)
{
	ModelDesign design;
	design.parts.append(ModelDesignPart{});
	ModelMesh mesh = buildModelDesignMesh(design);
	mesh.surfaces = surfaces;
	mesh.frames.resize(1);
	mesh.frames[0] = {};
	mesh.frames[0].name = QStringLiteral("base");
	mesh.animations.clear();
	mesh.tags.clear();
	mesh.skeleton = {};
	mesh.collisionBoxes.clear();
	updateEditableModelMetadata(&mesh);
	return mesh;
}

// The decoded grouped MDL fixture with its surface replaced by a grid, one
// pose and one native frame: the indexed skins, palette and header remain.
inline ModelMesh targetMdlGrid(int columns, int rows)
{
	const auto fixture = groupedMdlFixture();
	auto palette = generatedIdTechPalette("quake");
	palette.colors[1] = palette.colors[0];
	palette.generated = false;
	auto mesh = decodeModelMesh("fixture.mdl", fixture.bytes, &palette);
	mesh.frames.resize(1);
	mesh.animations.clear();
	mesh.mdl.frameGroups = {{0, {}}};
	const auto grid = targetGridSurface(mesh.surfaces[0].name, columns, rows);
	auto &surface = mesh.surfaces[0];
	surface.frames = grid.frames;
	surface.texCoords = grid.texCoords;
	surface.triangles = grid.triangles;
	surface.vertexCount = grid.vertexCount;
	surface.uvSeams.clear();
	updateEditableModelMetadata(&mesh);
	return mesh;
}

// One quad with a PCX skin, moved a little in each of `frames` frames.
inline ModelMesh targetMd2Quad(int frames)
{
	auto mesh = targetMesh({targetGridSurface(QStringLiteral("quad"), 2, 2)});
	auto &surface = mesh.surfaces[0];
	surface.skinPaths = {QStringLiteral("models/test/quad.pcx")};
	mesh.md2SkinSize = {64, 64};
	const auto base = surface.frames[0];
	surface.frames.clear();
	mesh.frames.clear();
	for (int frame = 0; frame < frames; ++frame)
	{
		auto pose = base;
		for (auto &p : pose.positions)
			p.z = float(frame % 7);
		surface.frames.append(pose);
		ModelFrameInfo info;
		info.name = QStringLiteral("f%1").arg(frame);
		mesh.frames.append(info);
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}

// The arm with `extra` more joints hanging from the root, unused by any
// vertex, so the skeleton reaches a joint limit without changing the poses.
inline ModelMesh targetArmWithJoints(int extra)
{
	auto mesh = exportArmFixture();
	auto &skeleton = mesh.skeleton;
	for (int joint = 0; joint < extra; ++joint)
	{
		skeleton.joints.append({QStringLiteral("extra%1").arg(joint), 0, modelJointMatrix({}, {0, float(joint), 0})});
		for (auto &clip : skeleton.clips)
			for (auto &frame : clip.frames)
				frame.append(modelJointMultiply(frame[0], modelJointMatrix({}, {0, float(joint), 0})));
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace vibestudio::tests
