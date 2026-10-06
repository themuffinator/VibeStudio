#pragma once

#include "core/model_assembly.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>

namespace vibestudio::tests
{
inline ModelQ3AnimationConfig q3Config()
{
	ModelQ3AnimationConfig config;
	config.footsteps = "boot";
	config.sex = "f";
	config.headOffset = {1, -2, 3};
	config.fixedLegs = true;
	for (int i = 0; i < modelQ3AnimationCount; ++i)
		config.clips[i] = {i >= 13 && i <= 24 ? 104 : 1, 5, 2, 10, false};
	config.clips[6].firstFrame = 2;
	config.clips[13].firstFrame = 102;
	config.clips[11].loopFrames = 0;
	return config;
}
inline ModelMesh q3Model()
{
	ModelMesh mesh;
	mesh.geometryAvailable = true;
	ModelSurface surface;
	surface.name = "synthetic";
	surface.vertexCount = 3;
	surface.triangles = {{0, 1, 2}};
	surface.texCoords = {{0, 0}, {1, 0}, {0, 1}};
	for (int i = 0; i < 12; ++i)
	{
		ModelFrameGeometry geometry;
		geometry.positions = {{0, 0, float(i)}, {10, 0, float(i)}, {0, 10, float(i)}};
		geometry.normals.fill({0, 0, 1}, 3);
		surface.frames.append(geometry);
		ModelFrameInfo frame;
		frame.name = QStringLiteral("pose%1").arg(i);
		mesh.frames.append(frame);
		ModelTag tag;
		tag.name = "tag_torso";
		tag.frameIndex = i;
		tag.origin = {float(i), 0, 20};
		mesh.tags.append(tag);
	}
	mesh.surfaces.append(surface);
	mesh.animations.append({"authored", 4, 5, 15});
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline ModelAssembly q3Assembly(const QString &source, bool native = true)
{
	ModelAssembly assembly;
	assembly.name = "Synthetic Q3 player";
	ModelAssemblyPart lower;
	lower.id = "lower";
	lower.source = source;
	ModelAssemblyPart upper = lower;
	upper.id = "upper";
	upper.parent = lower.id;
	upper.tag = "tag_torso";
	assembly.parts = {upper, lower}; // Exercise topological order independent of storage.
	if (native)
		assembly.q3Animation = ModelAssemblyQ3Animation{q3Config(), "lower", "upper", 15, 11};
	return assembly;
}
inline bool q3Write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
inline QByteArray q3Read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace vibestudio::tests
