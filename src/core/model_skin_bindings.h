#pragma once

#include "core/model_mesh.h"
#include "core/model_skin_source.h"

#include <QJsonObject>

namespace vibestudio
{
inline constexpr qint64 modelSkinBindingByteLimit = 64 * 1024;
struct ModelSkinAssignment
{
	int surface = -1;
	QString name, engineName, previousMaterial, material;
};
struct ModelSkinBindingPlan
{
	QVector<ModelSkinAssignment> assignments;
	QStringList ignoredTags, unusedSurfaces;
};
struct ModelSkinBindingInput
{
	QString path;
	qsizetype entryIndex = -1;
	QByteArray bytes;
};
bool readModelSkinBindings(const QString &path, ModelSkinBindingInput *input, QString *error = nullptr,
						   const ModelWorkControl &control = {});
bool readModelSkinBindings(const PackageArchiveReader &archive, const ModelSkinSourceReference &reference, ModelSkinBindingInput *input,
						   QString *error = nullptr, const ModelWorkControl &control = {});

// Original bounded Quake III .skin reader. No filesystem or shader execution.
// Every mesh surface must be covered. Extra bindings/tags are reported; duplicate
// bindings, malformed data and unsafe paths fail without changing the output.
// Surface matching follows Quake III's lowercase and trailing _x normalization.
bool planModelSkinBindings(const ModelMesh &mesh, const QByteArray &bytes, ModelSkinBindingPlan *plan, QString *error = nullptr,
						   const ModelWorkControl &control = {});
QJsonObject modelSkinBindingPlanJson(const ModelSkinBindingPlan &plan);
QStringList modelSkinBindingPlanText(const ModelSkinBindingPlan &plan);
} // namespace vibestudio
