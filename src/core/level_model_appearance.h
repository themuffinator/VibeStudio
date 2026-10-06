#pragma once

#include "core/level_map.h"
#include "core/model_mesh.h"
#include "core/package_archive.h"

#include <QJsonObject>

namespace vibestudio {

// q3map2 misc_model appearances are material mappings, not native player .skin
// surface bindings. Keys are pure functions of the map snapshot; package content
// identity remains the responsibility of the caller's immutable reader/cache.
struct LevelModelRemap {
	QString key, from, to;
};
struct LevelModelAppearance {
	QString modelPath, cacheKey, skin, skinPath, defaultSkinPath, error;
	int frame = 0;
	bool compiler = false;
	QVector<LevelModelRemap> remaps;
};
struct LevelModelAppearanceResult {
	ModelMesh mesh;
	QJsonObject receipt;
	QString error;
	bool cancelled = false;
	[[nodiscard]] bool succeeded() const { return error.isEmpty() && !cancelled; }
};

LevelModelAppearance levelModelAppearance(const LevelMapDocument& document, const LevelMapEntity& entity);
// Original source geometry is never changed. Skin reads are exact, unambiguous,
// bounded to 256 KiB, cancellable before/after IO, and recorded with SHA-256.
// MD3 compiler skins/remaps are supported. The referenced Assimp MD3 importer
// ignores nonzero frame selection, so those requests are refused with a static
// pose export remedy. Other formats reject unverified compiler overrides.
LevelModelAppearanceResult prepareLevelModelAppearance(const ModelMesh& source, const LevelModelAppearance& appearance,
	const PackageArchiveReader& archive, const ModelWorkControl& control = {});

} // namespace vibestudio
