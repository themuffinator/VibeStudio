#pragma once
#include "core/model_uv_atlas.h"

namespace vibestudio
{
// Internal shared atlas output admission. Original face/corner identities are
// retained; chart cuts copy every pose and remap authored seam marks.
bool validateModelUvMapping(const QVector<ModelTexCoord> &uv, const QVector<uint32_t> &indices, QString *error,
							const ModelWorkControl &control);
bool applyModelUvMapping(const ModelSurface &source, const QVector<int> &faces, const QVector<int> &sourceVertices,
						 const QVector<ModelTexCoord> &uv, const QVector<uint32_t> &indices, int vertexLimit, ModelSurface *result,
						 QString *error, const ModelWorkControl &control);
} // namespace vibestudio
