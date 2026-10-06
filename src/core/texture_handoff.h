#pragma once

#include "core/level_map.h"
#include "core/texture_export.h"

namespace vibestudio {

// A compiler-compatible map token for this package destination. Empty on error.
// File paths must retain the portable, lower-case textures/ prefix and suffix.
QString textureExportMapReference(const TextureExportOptions& options, const QString& path,
	LevelMapFormat mapFormat, const QString& wadMagic, QString* error = nullptr);

// Stage pixels and apply their reference to the selection as one in-memory
// transaction. Neither document changes on failure; their saves remain explicit.
// Restaging pixels already used by the selection does not add a map undo step.
bool stageAndApplyTextureExport(const TextureExportResult& result, const TextureExportOptions& options, const QString& path,
	PackageStagingModel* staging, bool replaceExisting, LevelMapDocument* map,
	QString* mapReference = nullptr, QString* error = nullptr);

} // namespace vibestudio
