#pragma once

#include "core/model_mesh.h"
#include "core/model_work.h"

namespace vibestudio
{
// Shared frame validation/rebaking for binding and weight authoring. Keeps
// independent tracks and original bind bytes intact.
struct ModelSkinningWork
{
	int bindFrame = -1;
	QVector<ModelJointMatrix> bindPose;
	QVector<int> localFrames;
	bool prepare(const ModelMesh &mesh, QString *error);
	const QVector<ModelJointMatrix> &pose(const ModelMesh &mesh, int frame) const;
	bool validateSurface(const ModelMesh &mesh, int surface, QString *error, const ModelWorkControl &control) const;
	bool bakeSurface(ModelMesh *mesh, int surface, QString *error, const ModelWorkControl &control) const;
};
} // namespace vibestudio
