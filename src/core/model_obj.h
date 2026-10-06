#pragma once

#include "core/model_mesh.h"

namespace vibestudio
{
// Bounded polygonal OBJ intake. No filesystem access: material libraries,
// external calls, free-form geometry and unrepresentable attributes fail with
// a line diagnostic. usemtl without mtllib names a package-relative material.
ModelMesh decodeModelObj(const QString &path, const QByteArray &bytes, const ModelWorkControl &control = {});
// Exact, verified streaming from an immutable reader. Ambiguous names fail.
ModelMesh decodeModelObjFromArchive(const PackageArchiveReader &archive, const QString &path, const ModelWorkControl &control = {});
} // namespace vibestudio
