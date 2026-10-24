#pragma once

#include "core/model_player_bundle.h"

#include <array>

namespace vibestudio
{
// One player file in a package snapshot. Paths match case-insensitively, as
// the game's filesystem does; `count` above one means the path is repeated
// and so ambiguous.
struct ModelQ3PlayerFile
{
	QString path;
	qsizetype entryIndex = -1;
	int count = 0;
	[[nodiscard]] bool present() const
	{
		return count == 1;
	}
};

// <role>_<name>.skin for lower, upper and head, and icon_<name>.tga.
struct ModelQ3PlayerSkinSet
{
	QString name;
	std::array<ModelQ3PlayerFile, 3> skins;
	ModelQ3PlayerFile icon;
	// The game loads a skin only when all three .skin files exist.
	[[nodiscard]] bool complete() const;
};

struct ModelQ3PlayerSoundFile
{
	// One of modelPlayerSoundNames().
	QString name;
	ModelQ3PlayerFile file;
};

// One models/players/<name>/ directory, read from entry metadata only.
struct ModelQ3PlayerListing
{
	QString name, directory;
	// lower.md3, upper.md3 and head.md3.
	std::array<ModelQ3PlayerFile, 3> models;
	ModelQ3PlayerFile animation;
	// The default skin first, then the others by name.
	QVector<ModelQ3PlayerSkinSet> skins;
	// lower_1.md3, upper_2.md3 and so on.
	QStringList detailLevels;
	// sound/player/<name>/<sound>.wav, in modelPlayerSoundNames() order.
	QVector<ModelQ3PlayerSoundFile> sounds;
	// Why the player cannot be opened; empty when it can.
	QStringList problems;
	[[nodiscard]] bool canOpen() const
	{
		return problems.isEmpty();
	}
	[[nodiscard]] QStringList completeSkins() const;
};

struct ModelQ3PlayerImport
{
	ModelQ3PlayerListing player;
	QString skin;
	// Package parts lower (the root), upper on lower's tag_torso and head on
	// upper's tag_head, each with the chosen skin linked from the package, and
	// native animation from the package's animation.cfg.
	ModelAssembly assembly;
	// What Player Package needs to publish the player again: its ID, skin and
	// icon, the other complete skins with their .skin files and icons, and
	// the custom sounds, all as package references.
	ModelPlayerBundleOptions publication;
	QStringList notes;
};

// Original reader for the Quake III player layout: models/players/<name>/
// lower.md3, upper.md3, head.md3, animation.cfg, <role>_<skin>.skin and
// icon_<skin>.tga, and sound/player/<name>/<sound>.wav. The Team Arena
// models/players/characters/ and models/players/heads/ folders are not read.
// Layout from id Software's cg_players.c (CG_RegisterClientModelname,
// CG_RegisterClientSkin, CG_LoadClientInfo); see docs/CREDITS.md.
bool listModelQ3Players(const PackageArchiveReader &archive, QVector<ModelQ3PlayerListing> *players, QString *error = nullptr,
						const ModelWorkControl &control = {});

// Reads animation.cfg bytes from a package entry: a unique path, or an exact
// entry index (whose path must also match when one is given). Refuses
// repeated paths without an index and entries over 19,998 bytes.
bool readModelQ3AnimationEntry(const PackageArchiveReader &archive, const QString &path, qsizetype entryIndex, QByteArray *bytes,
							   qsizetype *selected = nullptr, QString *error = nullptr, const ModelWorkControl &control = {});

// Builds a new assembly from one player. It reads only entry metadata and
// animation.cfg; the package is never changed. An empty `skin` takes default,
// else the first complete skin. Refuses players with missing models, a
// missing, repeated or invalid animation.cfg, or no complete skin, and a skin
// that is not complete. The parts are resolved, as any assembly is, when it
// is previewed or saved.
bool importModelQ3Player(const PackageArchiveReader &archive, const QString &player, const QString &skin, ModelQ3PlayerImport *result,
						 QString *error = nullptr, const ModelWorkControl &control = {});

QJsonObject modelQ3PlayerListingJson(const ModelQ3PlayerListing &player);
QStringList modelQ3PlayerListingText(const ModelQ3PlayerListing &player);
QJsonObject modelQ3PlayerImportJson(const ModelQ3PlayerImport &result);
QStringList modelQ3PlayerImportText(const ModelQ3PlayerImport &result);
// The `model assembly --operation player-review` options that reproduce
// `options`, in a stable order (CLI references use package:PATH).
QStringList modelPlayerBundleArguments(const ModelPlayerBundleOptions &options);
} // namespace vibestudio
