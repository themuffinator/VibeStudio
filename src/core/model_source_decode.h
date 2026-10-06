#pragma once

#include "core/model_mesh.h"

namespace vibestudio
{
// Internal bounded structural decoder for the import-repair quarantine. The
// result is NOT an editable document: indices, normals and metadata still need
// validation. Normal source loading always uses parseEditableModel instead.
bool decodeEditableModelSource(const QByteArray &bytes, ModelMesh *mesh, QString *error, const ModelWorkControl &control);
} // namespace vibestudio
