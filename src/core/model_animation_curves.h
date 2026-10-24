#pragma once
#include "core/model_mesh.h"
#include <QJsonArray>
#include <QJsonObject>
namespace vibestudio
{
struct ModelEdit;
inline constexpr qint64 modelCurveMaxKeys = 1048576;
enum class ModelCurveAction
{
	Capture,
	Replace,
	SetKey,
	RemoveKey,
	MoveKey,
	Bake
};
struct ModelCurveEditOptions
{
	ModelCurveAction action = ModelCurveAction::Capture;
	int clip = -1, channel = -1, frame = 0, newFrame = 0;
	QString joint;
	ModelPoseCurveKey key;
	ModelPoseCurves group;
	bool operator==(const ModelCurveEditOptions &) const = default;
};
QString modelCurveChannelId(int channel);
int modelCurveChannelIndex(const QString &id);
// Only validated nonempty, strictly ordered curves should be evaluated.
double evaluateModelCurve(const QVector<ModelPoseCurveKey> &keys, double frame);
QPair<double, double> modelCurveSlopes(const QVector<ModelPoseCurveKey> &keys, int key);
QPair<double, double> modelCurveRange(const QVector<ModelPoseCurveKey> &keys);
// Analytic supremum of the absolute scalar difference on a closed time range,
// including both sides of step discontinuities. Inputs must be validated.
bool modelCurveDifferenceBound(const QVector<ModelPoseCurveKey> &a, const QVector<ModelPoseCurveKey> &b, double first, double last,
							   double *maximum, QString *error, const ModelWorkControl &control = {});
ModelJointMatrix evaluateModelPoseCurves(const ModelPoseCurves &group, double frame);
bool validateModelCurveGroup(const ModelPoseCurves &group, int frameCount, QString *error);
bool hasModelAnimationCurves(const ModelMesh &mesh);
// Zero without curves, 9 for the original modes, 10 when hold-next is present.
int modelAnimationCurveVersion(const ModelMesh &mesh);
// Return the exact value and free slopes at an integer time on a validated curve.
ModelPoseCurveKey sampleModelCurveKey(const QVector<ModelPoseCurveKey> &keys, int frame);
// Crop a validated channel with exact boundary values/slopes, then map its
// time range to targetFirst. Reversal swaps one-sided holds and tangent sides.
bool sliceModelCurve(const QVector<ModelPoseCurveKey> &keys, int first, int last, int targetFirst, double factor, bool reverse,
					 int targetFrames, QVector<ModelPoseCurveKey> *result, QString *error, const ModelWorkControl &control = {});
// Called after ordinary skeleton validation. Also verifies curves agree with
// retained local poses, preventing unrelated edits from silently staling them.
bool validateModelAnimationCurves(const ModelMesh &mesh, QString *error, const ModelWorkControl &control = {});
bool captureModelPoseCurves(const ModelMesh &mesh, int clip, const QString &joint, ModelPoseCurves *result, QString *error,
							const ModelWorkControl &control = {});
// Numeric/graphical key editors and the CLI share the same group mutation.
bool editModelCurveGroup(ModelPoseCurves *group, int frameCount, const ModelCurveEditOptions &options, QString *error);
bool applyModelAnimationCurves(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control = {});
QJsonObject modelPoseCurvesJson(const ModelPoseCurves &group);
bool modelPoseCurvesFromJson(const QJsonObject &object, int frameCount, ModelPoseCurves *group, QString *error);
QJsonArray modelClipCurvesJson(const QVector<ModelPoseCurves> &groups);
bool modelClipCurvesFromJson(const QJsonValue &value, int frameCount, QVector<ModelPoseCurves> *groups, QString *error);
} // namespace vibestudio
