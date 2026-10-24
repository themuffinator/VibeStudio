#pragma once

#include "core/model_mesh.h"
#include "core/model_work.h"

namespace vibestudio
{
// Pose lookup for topology edits that retain independent baked frames. Unlike
// weight-authoring rebakes, these operations do not replace the frame set.
struct ModelMeshPoseBinding
{
	int referenceFrame = 0;
	QVector<ModelJointMatrix> bindPose, inverseReference;
	QVector<int> localFrames;
	bool prepare(const ModelMesh &mesh, int reference, QString *error, const ModelWorkControl &control);
	const QVector<ModelJointMatrix> &pose(const ModelMesh &mesh, int frame) const;
};
} // namespace vibestudio
