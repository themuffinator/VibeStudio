#pragma once
#include "core/model_delivery.h"

#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli
{
struct ModelToolsCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// `model tool`: one edit-mode mesh tool through the shared document service.
ModelToolsCliResult runModelTool(const QStringList &arguments);
// `model select`: read-only selection operators that print component indices.
ModelToolsCliResult runModelSelect(const QStringList &arguments);
// `model lod`: Quake III detail levels written beside a base MD3.
ModelToolsCliResult runModelLod(const QStringList &arguments);
// `model targets`: the delivery targets, or a model's preflight against them;
// with --project, the targets a project's exports use, and --save/--clear.
ModelToolsCliResult runModelTargets(const QStringList &arguments);
// `model prepare`: Prepare for target (core/model_target_prepare.h) saved as
// a new editable source.
ModelToolsCliResult runModelPrepare(const QStringList &arguments);

// The delivery defaults of a model command (core/model_delivery.h): the
// targets and strict setting saved with the project (--project, or the
// project folder holding `source`), and what the project's installation
// suggests from its engine.
struct ModelCliDelivery
{
	QString projectRoot;
	ModelDeliveryPreferences project;
	ModelDeliveryPreferences installation;
	QString installationName;
	QStringList warnings;
	[[nodiscard]] ModelDeliveryChoice target(const QString &format, const QString &explicitTarget) const;
	// --strict or --no-strict, else the project's saved setting, else off.
	[[nodiscard]] bool strict(bool strictFlag, bool noStrictFlag, ModelDeliveryOrigin *origin = nullptr) const;
	[[nodiscard]] QJsonObject json(const QString &format, const ModelDeliveryChoice &choice, bool strict, ModelDeliveryOrigin strictOrigin) const;
};
[[nodiscard]] ModelCliDelivery modelCliDelivery(const QString &projectOption, const QString &source);
// `model rig`: joint hierarchy and selected vertex influence authoring.
ModelToolsCliResult runModelRig(const QStringList &arguments);
ModelToolsCliResult runModelWeightPaint(const QStringList &arguments);
ModelToolsCliResult runModelWeightTransfer(const QStringList &arguments);
ModelToolsCliResult runModelIk(const QStringList &arguments);
ModelToolsCliResult runModelCurves(const QStringList &arguments);
ModelToolsCliResult runModelCurveKeys(const QStringList &arguments);
ModelToolsCliResult runModelCurveTransfer(const QStringList &arguments);
ModelToolsCliResult runModelUvDensity(const QStringList &arguments);
ModelToolsCliResult runModelSkinning(const QStringList &arguments);
// `model pose`: skeletal bind, clip and sampled-pose authoring.
ModelToolsCliResult runModelPose(const QStringList &arguments);
} // namespace vibestudio::cli
