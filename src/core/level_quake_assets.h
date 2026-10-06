#pragma once
#include "core/package_archive.h"
#include <QMap>

namespace vibestudio {
struct LevelQuakeTexture {
	QString name, sourcePath;
	QByteArray miptex;
	bool ambiguous = false;
};
struct LevelQuakeTextures {
	QMap<QString, LevelQuakeTexture> textures;
	QString error;
	bool cancelled = false;
};
// Bounded, immutable WAD2 and loose miptexture lookup. Never opens paths from
// worldspawn's wad key; every byte comes from the selected package snapshot.
LevelQuakeTextures inspectLevelQuakeTextures(const PackageArchiveReader& archive, const PackageReadControl& control = {},
											 bool includeLooseMiptex = false);
QByteArray encodeLevelQuakeTextureWad(const LevelQuakeTextures& textures, QString* error, const PackageReadControl& control = {});
} // namespace vibestudio
