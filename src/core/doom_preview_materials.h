#pragma once

#include "core/level_materials.h"

namespace vibestudio {

// Called with the shared bounded package reader. All palette/definition/patch
// reads consume the same budgets and use the same immutable staging snapshot.
void resolveDoomPreviewAssets(const LevelMapDocument& document, const PackageArchiveReader& reader,
	const LevelPreviewAssetOptions& options, LevelPreviewAssets* report, const std::function<bool()>& tick);

} // namespace vibestudio
