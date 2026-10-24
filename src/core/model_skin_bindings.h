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
// The name Quake III binds a surface by: lower case, with a final underscore
// and character dropped (R_LoadMD3, so body_1 is body).
[[nodiscard]] QString modelQ3SkinSurfaceName(const QString &surfaceName);
// The Quake III .skin of `mesh`'s primary materials (skin slot 0): one
// "surface","shader" record per engine surface name, in surface order.
// Surfaces that share an engine name are written once and must share a
// material. Refuses surface names and materials a .skin cannot hold and
// surfaces without a material; the written text plans (planModelSkinBindings)
// to exactly the primary materials. For a skin variant, apply it first
// (applyModelSkinVariant in core/model_skin_variants.h).
bool writeModelQ3Skin(const ModelMesh &mesh, QByteArray *bytes, QString *error = nullptr, const ModelWorkControl &control = {});
QJsonObject modelSkinBindingPlanJson(const ModelSkinBindingPlan &plan);
QStringList modelSkinBindingPlanText(const ModelSkinBindingPlan &plan);
} // namespace vibestudio
