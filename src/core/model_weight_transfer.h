#pragma once

#include "core/model_mesh.h"
#include "core/model_work.h"
#include <QMap>

namespace vibestudio
{
struct ModelEdit;
enum class ModelWeightTransferMethod
{
	Vertex,
	Triangle
};
enum class ModelWeightTransferSide
{
	All,
	PositiveToNegative,
	NegativeToPositive
};
struct ModelWeightTransferOptions
{
	int sourceSurface = 0;
	ModelWeightTransferMethod method = ModelWeightTransferMethod::Triangle;
	// -1 copies in bind space; 0/1/2 reflects target queries through X/Y/Z.
	int mirrorAxis = -1;
	double plane = 0, centreTolerance = .0001, maximumDistance = 1, normalAngle = 180, strength = 1;
	ModelWeightTransferSide side = ModelWeightTransferSide::All;
	// Unlisted names retain their identity. Mapping never guesses a naming convention.
	QMap<QString, QString> joints;
	QStringList lockedJoints;
	bool skipUnmatched = false;
	bool operator==(const ModelWeightTransferOptions &) const = default;
};
// Empty component selection targets the active surface. Source queries always
// use a frozen bind snapshot, including same-surface and bidirectional mirrors.
// Equidistant samples with different weights are refused instead of picking a seam.
bool applyModelWeightTransfer(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control = {});
} // namespace vibestudio
