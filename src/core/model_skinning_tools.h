#pragma once

#include "core/model_mesh.h"
#include "core/model_work.h"
#include <QMap>
#include <QSet>

namespace vibestudio
{
struct ModelEdit;
struct ModelSelection;
enum class ModelEditKind;

enum class ModelSkinBindingMethod
{
	Rigid,
	NearestJoints,
	NearestBones
};
struct ModelSkinOptions
{
	ModelSkinBindingMethod method = ModelSkinBindingMethod::NearestBones;
	// Empty considers every joint; rigid binding requires one exact name.
	QStringList joints;
	int maxInfluences = 4;
	double falloff = 2, minimumWeight = .001;
	bool replaceExisting = false;
	bool operator==(const ModelSkinOptions &) const = default;
};

struct ModelSkinWeight
{
	int joint = -1;
	double weight = 0;
};
struct ModelSkinVertex
{
	int surface = -1, vertex = -1;
	QVector<ModelSkinWeight> weights;
	double sum = 0;
	int positiveInfluences = 0;
	bool unbound = false, negative = false, unnormalised = false, duplicate = false, noncanonical = false, overLimit = false;
};
struct ModelSkinReport
{
	// Retain the COW source snapshot for stale-report detection and selection.
	ModelMesh source;
	QVector<ModelSkinVertex> vertices;
	int unbound = 0, negative = 0, unnormalised = 0, duplicate = 0, noncanonical = 0, overLimit = 0;
	bool hasBindFrame = false;
};
enum class ModelSkinFilter
{
	Weighted,
	Unbound,
	Unnormalised,
	Negative,
	Duplicate,
	Noncanonical,
	OverLimit
};

// Empty component selection means the whole active surface. A surface selection
// spans those complete surfaces; mixed/non-mesh selections are refused.
bool inspectModelSkinning(const ModelMesh &mesh, const ModelSelection &selection, int influenceLimit, ModelSkinReport *report,
						  QString *error = nullptr, const ModelWorkControl &control = {});
bool selectModelSkinVertices(const ModelMesh &mesh, const ModelSkinReport &report, int surface, ModelSkinFilter filter,
							 const QString &joint, double minimum, double maximum, ModelSelection *selection, QString *error = nullptr);
// Binding weights for one bind-pose position, before normalisation. Rigid
// gives joints[0] weight 1; NearestJoints and NearestBones weight the
// maxInfluences closest joint origins or parent-to-joint bones by
// (closest / distance)^(falloff / 2). Shared by Bind vertices and by new
// geometry bound to the nearest bones. False only when cancelled.
bool modelSkinBindingWeights(const ModelSkeleton &skeleton, const QVector<ModelJointMatrix> &bindPose, const QVector<int> &joints,
	ModelSkinBindingMethod method, int maxInfluences, double falloff, ModelVec3 position, QMap<int, double> *weights,
	ModelWorkProgress *work = nullptr);
bool isModelSkinEdit(ModelEditKind kind);
// Candidate-only mutation, adopted atomically by ModelDocument. Bind/rebind/
// reduction preserve the bind frame and rebake affected surfaces only.
bool applyModelSkinEdit(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control = {});
} // namespace vibestudio
