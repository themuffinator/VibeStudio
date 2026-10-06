#pragma once

#include "core/model_uv.h"
#include <QPainterPath>
#include <QTransform>

namespace vibestudio
{
// Selection is a union, including overlapping or reversed UV faces. Build its
// winding contours from a validated immutable surface before painting once.
// Only exactly coincident indexed borders cancel; wire/pick topology is untouched.
// Clipping precedes QPainter and failure leaves the caller's path unchanged.
bool buildModelUvSelectionPath(const ModelSurface &surface, const QSet<int> &faces, const QTransform &camera, const ModelTexCoord &offset,
							   const QRectF &clip, QPainterPath *result, QString *error, const ModelWorkControl &control = {});
} // namespace vibestudio
