#pragma once

#include "core/model_skin_bindings.h"

#include <QHash>
#include <optional>

namespace vibestudio
{
// View state, not authored bindings. Model/material files and slot order are
// never changed. Package entry indexes belong to the caller's immutable reader.
struct ModelAppearance
{
	QHash<int, int> materialSlots;
	int mdlSkin = -1, mdlMember = -1;
	std::optional<ModelSkinSourceReference> skin;
};

struct ModelAppearanceSnapshot
{
	// Lightweight input for resolveModelPreviewAssets: surface material names
	// and the selected embedded image, without geometry or unused skin pixels.
	ModelMesh materials;
	QJsonObject receipt;
	QStringList details;
};

QJsonObject modelAppearanceJson(const ModelAppearance &appearance);
// All reads and indexed image decoding belong on a worker. Failed or cancelled
// preparation leaves both the original mesh and the output unchanged.
bool prepareModelAppearance(const ModelMesh &mesh, const ModelAppearance &appearance,
							const PackageArchiveReader *archive, ModelAppearanceSnapshot *output,
							QString *error = nullptr, const ModelWorkControl &control = {});
} // namespace vibestudio
