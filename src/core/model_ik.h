#pragma once
#include "core/model_mesh.h"
#include "core/model_work.h"

namespace vibestudio
{
struct ModelEdit;
struct ModelIkOptions
{
	int clip = -1, first = 0, last = 0;
	// The end joint's parent and grandparent define the two-bone chain.
	QString endJoint;
	ModelVec3 target, pole;
	bool nearestReachable = false, keepEndOrientation = true;
	bool operator==(const ModelIkOptions &other) const;
};
struct ModelIkSample
{
	int frame = 0;
	ModelVec3 end;
	double error = 0, tolerance = 0;
	bool reached = false;
};
// Read-only diagnostics from a solved mesh. Empty for invalid clip/chain/range.
QVector<ModelIkSample> modelIkSamples(const ModelMesh &mesh, const ModelIkOptions &options);
// Candidate-only: use applyModelEdit/ModelDocument for transactional publication.
bool applyModelIk(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control = {});
} // namespace vibestudio
