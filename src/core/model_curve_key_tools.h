#pragma once
#include "core/model_animation_curves.h"
namespace vibestudio
{
enum class ModelCurveKeyAction
{
	Retime,
	Duplicate,
	Remove,
	Reduce
};
struct ModelCurveKeySelection
{
	QString joint;
	int channel = -1;
	QVector<int> samples;
	bool operator==(const ModelCurveKeySelection &) const = default;
};
struct ModelCurveKeyOptions
{
	ModelCurveKeyAction action = ModelCurveKeyAction::Retime;
	int clip = -1;
	QVector<ModelCurveKeySelection> selection;
	double pivot = 0, factor = 1, offset = 0;
	bool snap = false;
	std::array<double, 3> tolerance{.001, .01, .0001};
	bool operator==(const ModelCurveKeyOptions &) const = default;
};
struct ModelCurveKeyChange
{
	QString joint;
	int channel = -1, selected = 0, before = 0, after = 0;
	double maximumChange = 0;
};
struct ModelCurveKeyReport
{
	QVector<ModelCurveKeyChange> channels;
};
// Selectors name existing keys exactly. Changes are atomic, including failures
// and cancellation. Reduction bounds the continuous scalar curves, not just samples.
bool editModelCurveKeys(QVector<ModelPoseCurves> *groups, int frameCount, const ModelCurveKeyOptions &options, ModelCurveKeyReport *report,
						QString *error, const ModelWorkControl &control = {});
bool applyModelCurveKeys(ModelMesh *candidate, const ModelEdit &edit, ModelCurveKeyReport *report, QString *error,
						 const ModelWorkControl &control = {});
// Candidate-only combined rebake. Groups must match the current clip's joint
// order; callers capture missing destination groups before invoking it.
bool replaceModelCurveGroups(ModelMesh *candidate, int clip, const QVector<ModelPoseCurves> &groups, QString *error,
							 const ModelWorkControl &control = {});
QJsonArray modelCurveKeyReportJson(const ModelCurveKeyReport &report);
bool describeModelCurveKeyChanges(const QVector<ModelPoseCurves> &before, const QVector<ModelPoseCurves> &after, int frameCount,
								  const QVector<ModelCurveKeySelection> &selection, ModelCurveKeyReport *report, QString *error,
								  const ModelWorkControl &control = {});
} // namespace vibestudio
