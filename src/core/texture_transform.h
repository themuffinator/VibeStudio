#pragma once

#include "core/texture_paint.h"

namespace vibestudio {

enum class TextureAnchor { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };
QString textureAnchorId(TextureAnchor anchor);
bool textureAnchorFromId(const QString& id, TextureAnchor* anchor);
// Place content within a container. Center offsets snap toward negative infinity.
QPoint textureAnchorOffset(QSize content, QSize container, TextureAnchor anchor);

// Internal raster operations on validated document images. Raw copies preserve
// palette indices and hidden RGB. Only smooth resampling introduces new colors.
QImage resizeTexturePixels(const QImage& source, QSize size, bool smooth, QString* error, const TextureProgress& progress = {});
QImage rotateTexturePixelsClockwise(const QImage& source, QString* error, const TextureProgress& progress = {});
QImage resizeTextureCanvas(const QImage& source, QSize size, QPoint offset, QString* error, const TextureProgress& progress = {});
QImage replaceTexturePixels(const QImage& source, QRect clearArea, const QImage& replacement, QPoint position, QString* error, const TextureProgress& progress = {});

} // namespace vibestudio
