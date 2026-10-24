#pragma once

#include "core/model_transform.h"

namespace vibestudio
{
struct ModelSelection;
struct ModelJointFrame
{
	int clip = -1, local = 0;
	QVector<ModelJointMatrix> pose;
};

// -1 explicitly requests the bind pose. Other indices must refer to a retained
// skeletal sample; independently authored/rearranged frames have no joint pose.
bool modelJointFrame(const ModelMesh &mesh, int frame, ModelJointFrame *result, QString *error = nullptr);
bool validModelJointSelection(const ModelMesh &mesh, const ModelSelection &selection);
// One model-space affine delta is shared by the viewport, CLI and document.
// Singular, reflected and unbounded deltas are rejected before changing output.
bool validModelJointDelta(const ModelJointMatrix &delta, QString *error = nullptr);
bool modelJointTransformDelta(const ModelTransform &transform, ModelJointMatrix *result, QString *error = nullptr);
bool applyModelJointDelta(QVector<ModelJointMatrix> *pose, const ModelSkeleton &skeleton, int joint, const ModelJointMatrix &delta,
						  bool descendants, QString *error = nullptr, const ModelWorkControl &control = {});

struct ModelJointPreview
{
	QVector<ModelJointMatrix> pose;
	// One current-frame geometry per surface, and only this frame's tags.
	QVector<ModelFrameGeometry> surfaces;
	QVector<ModelTag> tags;
};
// Transient, frame-only deformation for render workers. Bind manipulation keeps
// the skin and attachments in place. Pose manipulation protects independent
// edits by the same tolerance as the document's skeletal authoring service.
bool previewModelJointTransform(const ModelMesh &mesh, int frame, const QString &joint, const ModelJointMatrix &delta, bool descendants,
								ModelJointPreview *result, QString *error = nullptr, const ModelWorkControl &control = {});
} // namespace vibestudio
