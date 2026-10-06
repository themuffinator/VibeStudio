#pragma once
#include "core/model_mesh.h"
#include <QHash>

namespace vibestudio
{
inline constexpr int modelMaxMaterialSlots = 256;

enum class ModelMaterialSlotAction
{
	Replace,
	Set,
	Insert,
	Remove,
	Move,
	Clear
};
struct ModelMaterialSlotEdit
{
	ModelMaterialSlotAction action = ModelMaterialSlotAction::Replace;
	int slot = -1, destination = -1;
	QStringList materials;
};

bool supportsModelMaterialSlots(const ModelMesh &mesh);
// Ordered bindings may repeat. Move's destination is the final zero-based index.
// Outputs remain untouched on failure; an empty replacement explicitly clears.
bool editModelMaterialSlots(const QStringList &source, const ModelMaterialSlotEdit &edit, QStringList *result, QString *error = nullptr);
// Read-only, geometry-free material request from a decoded or validated mesh.
// This snapshot is not an editable model. Overrides select an existing slot
// per surface; unspecified surfaces retain their primary or embedded preview.
bool modelMaterialPreviewSnapshot(const ModelMesh &mesh, const QHash<int, int> &selectedSlots, ModelMesh *result, QString *error = nullptr);
} // namespace vibestudio
