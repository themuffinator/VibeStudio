#pragma once

#include "core/level_dependencies.h"
#include "core/model_assembly.h"
#include "core/package_staging.h"

#include <array>
#include <optional>

namespace vibestudio
{
// A file (absolute, or relative to the assembly's folder) or a package entry,
// checked as linked skins are (validateModelAssemblySkin).
using ModelPlayerReference = ModelAssemblySkin;

// Quake III's custom player sounds in cg_customSoundNames order
// (code/cgame/cg_players.c), without the leading "*" and ".wav": death1 to
// taunt. CG_LoadClientInfo registers sound/player/<model>/<name>.wav for each
// and falls back to the default model's sound for any that is missing.
[[nodiscard]] const QStringList &modelPlayerSoundNames();

// Another skin beside the package's own (ModelPlayerBundleOptions::skinName).
// In team games cgame looks for lower_red.skin and lower_blue.skin (and the
// same for upper, head and icon) instead of the chosen skin, so red and blue
// are the team skins.
struct ModelPlayerSkinVariant
{
	QString name;
	// An explicit .skin per role, in lower, upper, head order. A role without
	// one uses its part's skin variant of this name (core/model_skin_variants.h),
	// else the part's own materials.
	std::array<std::optional<ModelPlayerReference>, 3> skins;
	// The image for icon_<name>.tga; unset reuses the package icon.
	std::optional<ModelPlayerReference> icon;
};

enum class ModelPlayerDetailLevels
{
	// Full detail at every distance.
	None,
	// `lodLevels` levels per part, each keeping `lodRatio` of the level above.
	Generated,
	// Each part's stored detail-level recipe (ModelMesh::detailLevels); parts
	// without one ship without detail levels.
	Stored,
};

struct ModelPlayerSound
{
	// One of modelPlayerSoundNames().
	QString name;
	ModelPlayerReference source;
};

struct ModelPlayerBundleOptions
{
	QString modelName, skinName = QStringLiteral("default"), headPart;
	QString iconSource;
	ModelAssemblySource iconKind = ModelAssemblySource::File;
	int iconEntryIndex = -1;
	// More skins, in order; names differ from skinName and each other.
	QVector<ModelPlayerSkinVariant> variants;
	// Team skins: red and blue are added when missing, and each needs at least
	// one part with its own red or blue materials.
	bool teamVariants = false;
	ModelPlayerDetailLevels detailLevels = ModelPlayerDetailLevels::None;
	int lodLevels = 2;
	double lodRatio = 0.5;
	// Custom sounds, at most one per name. A sound that is already mono PCM WAV
	// is kept as it is; any other audio the studio reads is converted to
	// 22,050 Hz mono 16-bit PCM (the Quake III audio delivery preset).
	QVector<ModelPlayerSound> sounds;
};
struct ModelPlayerBundleFile
{
	QString path, role;
	QByteArray bytes, sha256;
};
struct ModelPlayerBundle
{
	ModelPlayerBundleOptions options;
	QVector<ModelPlayerBundleFile> files;
	LevelDependencyReport dependencies;
	QJsonObject receipt;
	QStringList notes, protectedPaths;
	// Every published skin, the package's own first.
	QStringList skins;
	std::shared_ptr<const PackageArchiveReader> sourceArchive;
	// Optional caller revision for the immutable package context; never serialized.
	QString contextRevision;
	QByteArray recipeSha256;
	qint64 totalBytes = 0;
};

// Parses "package:PATH" as a package entry and anything else as a file, the
// form the CLI and the Player Package dialog use for skins, icons and sounds.
[[nodiscard]] ModelPlayerReference modelPlayerReference(const QString &text);
[[nodiscard]] QString modelPlayerReferenceText(const ModelPlayerReference &reference);

// Original Quake III player-loader layout; see docs/CREDITS.md. Native frames,
// tags and shader stages remain separate, unlike a composed animation bake.
// All content is owned by the reviewed candidate. Failure never replaces it.
//
// The package is checked for completeness before it is offered: every skin
// covers every surface of its part (and so of the part's detail levels, which
// keep the part's surface names), upper has tag_weapon (cgame holds weapons on
// it), each skin has its icon, and the team skins resolve when asked for.
// Missing custom sounds, a missing tag_flag and unused head tags are notes.
bool prepareModelPlayerBundle(const ModelAssembly &assembly, const ModelAssemblyResolved &resolved, const ModelAssemblyContext &context,
							  const ModelPlayerBundleOptions &options, const QString &sourcePath, ModelPlayerBundle *result,
							  QString *error = nullptr, const ModelWorkControl &control = {});
QJsonObject modelPlayerBundleJson(const ModelPlayerBundle &bundle);
QStringList modelPlayerBundleText(const ModelPlayerBundle &bundle);
// Shared PK3 writer: atomic publication, guarded overwrite, cancellation and a
// second hash-only pass proving determinism. Inputs and generated backup paths
// are protected; no game installation or source package is modified.
PackageWriteReport writeModelPlayerBundle(const ModelPlayerBundle &bundle, const QString &path, bool overwrite, bool dryRun,
										  const ModelWorkControl &control = {});
} // namespace vibestudio
