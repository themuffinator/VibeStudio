#pragma once

#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_skeleton.h"
#include <QJsonDocument>

namespace vibestudio::tests
{
inline ModelMesh rigFixture()
{
	ModelDesign design;
	design.parts.append(ModelDesignPart{});
	auto mesh = buildModelDesignMesh(design);
	auto &surface = mesh.surfaces[0];
	surface.name = QStringLiteral("body");
	surface.vertexCount = 3;
	surface.triangles = {{0, 1, 2}};
	surface.texCoords = {{0, 0}, {1, 0}, {0, 1}};
	surface.frames = {{{{0, 0, 0}, {8, 0, 0}, {0, 8, 0}}, {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}}}};
	surface.skinPaths = {QStringLiteral("models/body.tga")};
	mesh.skeleton.joints = {{QStringLiteral("root"), -1, modelJointIdentity()},
							{QStringLiteral("tip"), 0, modelJointMatrix({}, {8, 0, 0})}};
	ModelSkeletalClip clip;
	clip.name = QStringLiteral("lift");
	clip.framesPerSecond = 12;
	clip.frames = {modelSkeletonBindPose(mesh.skeleton), {modelJointIdentity(), modelJointMatrix({}, {8, 4, 0})}};
	mesh.skeleton.clips = {clip};
	mesh.skeleton.tags = {{QStringLiteral("tag_tip"), 1, modelJointIdentity()}};
	if (!modelSkinningFromBindPose(mesh.skeleton, surface.frames[0].positions, surface.frames[0].normals, {0, 0, 0}, {1, 1, 1}, 1,
								   &surface.skinning) ||
		!bakeModelSkeleton(&mesh))
		return {};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline QByteArray rigBytes(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}
} // namespace vibestudio::tests
