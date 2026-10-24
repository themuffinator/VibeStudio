#pragma once
#include "core/model_curve_key_tools.h"
namespace vibestudio
{
struct ModelCurveTransferOptions
{
	int sourceClip = -1, targetClip = -1;
	int first = 0, last = 0, targetFirst = 0;
	double factor = 1;
	bool reverse = false;
	// Empty selectors mean every joint/channel. Names are exact and distinct.
	QStringList joints;
	QVector<int> channels;
	bool operator==(const ModelCurveTransferOptions &) const = default;
};
bool applyModelCurveTransfer(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control = {});
bool describeModelCurveTransfer(const ModelMesh &before, const ModelMesh &after, const ModelCurveTransferOptions &options,
								ModelCurveKeyReport *report, QString *error, const ModelWorkControl &control = {});
} // namespace vibestudio
