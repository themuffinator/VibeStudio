#pragma once

#include "core/level_surface_clipboard.h"

namespace vibestudio {
// Plain .map face records, without entity or brush braces. The last record is
// the attribute source, matching TB Map_CopyPaste.cpp (see CREDITS.md).
bool looksLikeLevelFaceText(const QString& text);
bool parseLevelFaceText(const QString& text, QVector<LevelMapBrushFace>* faces, QString* error = nullptr,
	const std::function<bool()>& cancelled = {});
bool parseLevelFaceClipboard(const QString& text, LevelSurfaceClipboard* clipboard, QString* error = nullptr,
	const std::function<bool()>& cancelled = {});
QString levelFaceSelectionText(const LevelMapDocument& document, const QVector<LevelSurfaceFace>& faces, QString* error = nullptr);
} // namespace vibestudio
