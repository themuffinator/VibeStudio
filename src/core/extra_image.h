#pragma once

#include "core/idtech_image.h"

namespace vibestudio {

// PakFu-derived DDS/FTX/SWL codecs; see docs/CREDITS.md for provenance.
QImage decodeDdsImage(const QByteArray& bytes, const IdTechImageDecodeContext& context, QString* error);
IdTechImageDecodeResult decodeExtraImage(IdTechImageFormat format, const QByteArray& bytes, const IdTechImageDecodeContext& context);
// Lossless, single-surface DDS BGRA8 / FTX RGBA8. No block compression.
QByteArray encodeExtraImage(const QImage& image, bool dds, QString* error, const std::function<bool()>& isCancelled = {});

} // namespace vibestudio
