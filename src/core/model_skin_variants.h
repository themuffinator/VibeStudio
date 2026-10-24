#pragma once

// Named skin variants: an editable source can own skins such as red and blue
// beside its primary materials (ModelMesh::skinVariants). A variant binds a
// material to some surfaces by name; every other surface keeps its primary
// material (skin slot 0), so "default" always means the primary materials
// and is not a variant name.
//
// A variant is written as a Quake III .skin (writeModelQ3Skin in
// core/model_skin_bindings.h, on the model with the variant applied) or as a
// Doom 3 skin declaration. The two engines bind skins differently:
// - Quake III's RE_RegisterSkin (code/renderer/tr_image.c) reads
//   "surface,shader" records, and R_AddMD3Surfaces gives each surface the
//   shader its name is bound to;
// - Doom 3's idDeclSkin (neo/framework/DeclSkin.cpp) maps materials to
//   materials: RemapShaderBySkin uses the first mapping whose source is the
//   surface's material, "*" matches every material, and "model" lines only
//   associate the skin with a model for the editors.
// So a Doom 3 skin changes every surface that uses a material; writing one
// refuses a variant that would change a surface it does not bind.
// Behaviour references only, no code copied (docs/CREDITS.md).
//
// Player packages (core/model_player_bundle.h) publish a part's variant under
// the skin of the same name.

#include "core/model_mesh.h"

#include <QJsonArray>
#include <QStringList>

namespace vibestudio
{
inline constexpr int modelSkinVariantMaxCount = 32;
// 1 to 24 ASCII letters, digits, underscores or hyphens, starting with a
// letter: the skin-name rule of Quake III player packages. "default" (in any
// case) is reserved for the primary materials.
[[nodiscard]] bool validModelSkinVariantName(const QString &name);
// Problems with the source's variants, one message each: names, duplicates,
// materials, and variants on a model whose skins are embedded (MDL). A binding
// to a surface the model does not have is not one of them, so renaming a
// surface keeps its variants: modelSkinVariantMissingSurfaces lists those, and
// applying or writing the variant refuses them.
[[nodiscard]] QStringList validateModelSkinVariants(const ModelMesh &mesh);
// One object per variant: name and bindings ({surface, material} in order).
[[nodiscard]] QJsonArray modelSkinVariantsJson(const QVector<ModelSkinVariant> &variants);
bool parseModelSkinVariants(const QJsonValue &value, QVector<ModelSkinVariant> *variants, QString *error = nullptr);
// The variant named `name` (any case), or -1.
[[nodiscard]] int findModelSkinVariant(const QVector<ModelSkinVariant> &variants, const QString &name);
// The surfaces `variant` binds that `mesh` does not have.
[[nodiscard]] QStringList modelSkinVariantMissingSurfaces(const ModelMesh &mesh, const ModelSkinVariant &variant);
// `mesh` with the variant's material as the primary material of each surface
// it binds (the other slots are kept). Refuses a variant that binds a surface
// the model does not have. `inherited` receives the surfaces that keep their
// primary material.
bool applyModelSkinVariant(const ModelMesh &mesh, const ModelSkinVariant &variant, ModelMesh *result, QString *error = nullptr,
						   QStringList *inherited = nullptr);

enum class ModelSkinVariantAction
{
	Add,
	Rename,
	Remove,
	Set,
};
struct ModelSkinVariantEdit
{
	ModelSkinVariantAction action = ModelSkinVariantAction::Add;
	QString name;
	// Rename: the new name.
	QString newName;
	// Add: the variant's first bindings. Set: the bindings to change; an empty
	// material returns that surface to its primary material.
	QVector<ModelSkinVariantBinding> bindings;
};
// Applies `edit` to `variants`. Refuses, leaving `result` unchanged, an
// unknown or duplicate name, a material for a surface `mesh` does not have
// (returning such a binding to the primary material removes it), a surface
// named twice, and a list validateModelSkinVariants refuses.
bool editModelSkinVariants(const ModelMesh &mesh, const QVector<ModelSkinVariant> &variants, const ModelSkinVariantEdit &edit,
						   QVector<ModelSkinVariant> *result, QString *error = nullptr);
// Each variant with its bindings, the surfaces that keep their primary
// material and any binding to a surface the model lacks, for the CLI.
[[nodiscard]] QJsonArray modelSkinVariantsReportJson(const ModelMesh &mesh);
[[nodiscard]] QStringList modelSkinVariantsReportLines(const ModelMesh &mesh);

struct ModelDoom3SkinMapping
{
	QString from, to;
};
struct ModelDoom3Skin
{
	QString name;
	QStringList models;
	QVector<ModelDoom3SkinMapping> mappings;
};
// "skins/<model path without its extension>_<variant>", with characters a
// bare declaration name cannot hold replaced by underscores.
[[nodiscard]] QString modelDoom3SkinDeclName(const QString &modelPath, const QString &variant);
// A Doom 3 skin declaration named `declName` that maps each bound surface's
// primary material to the variant's, plus a "model" line for `modelPath` when
// it is set. Refuses a variant that changes no material, a bound surface with
// no primary material, two bound surfaces that share a material but not a
// variant material, and a surface the variant does not bind that shares a
// changed material (the skin would change it too). Materials are compared as
// Doom 3 finds them (MakeNameCanonical: case, backslashes and the extension
// do not count).
bool writeModelDoom3Skin(const ModelMesh &mesh, const ModelSkinVariant &variant, const QString &declName, const QString &modelPath,
						 QByteArray *bytes, QString *error = nullptr);
// The skin declarations of a decl file, in order: bare or quoted tokens,
// "//" and "/* */" comments, and other declarations skipped. For checking
// written skins; refuses malformed declarations.
bool parseModelDoom3Skins(const QByteArray &bytes, QVector<ModelDoom3Skin> *skins, QString *error = nullptr);
} // namespace vibestudio
