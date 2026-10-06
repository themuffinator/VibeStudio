#pragma once

#include "core/model_mesh.h"
#include "core/model_work.h"

namespace vibestudio
{
// Opaque content identity for a validated editable model. This is an internal
// revision key, not a file checksum or a substitute for source serialization.
QByteArray modelStateFingerprint(const ModelMesh &mesh, QString *error = nullptr, const ModelWorkControl &control = {});
} // namespace vibestudio
