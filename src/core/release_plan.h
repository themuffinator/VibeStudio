#pragma once

// Release planning: what a map, model, texture set or whole project needs to
// ship, and nothing the game already has.
//
// The planner reads the project through ProjectContentReader, plus any extra
// package layer such as the open draft. It finds what each selected item needs
// with the shared dependency resolver, adds the files a game loads beside a
// map (compiled BSP, lighting, levelshot, arena script, bot navigation,
// external lightmaps, sky images), and sorts every file against the game asset
// register:
//   - included: the project's own content;
//   - provided by the game, or by a declared requirement: left out;
//   - replaces a game file: included, with a warning;
//   - missing: blocks publication until it is found.
// Planning never writes. publishRelease (release_publish.h) writes the plan.

#include "core/game_asset_register.h"
#include "core/level_dependencies.h"
#include "core/package_archive.h"
#include "core/project_content.h"
#include "core/project_manifest.h"

#include <QDateTime>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace vibestudio {

enum class ReleaseScope {
	// Every content file of the project that the game does not already have.
	Project,
	// The chosen maps and everything they use.
	Maps,
	// The chosen models with their skins, shaders and images.
	Models,
	// The chosen textures (files or folders) with their shader scripts.
	Textures,
};

QString releaseScopeId(ReleaseScope scope);
ReleaseScope releaseScopeFromId(const QString& id, bool* ok = nullptr);
QString releaseScopeDisplayName(ReleaseScope scope);

// Role ids are stable and untranslated: map, map-companion, map-source,
// texture, shader, model, skin, sound, music, script, code, content.
QString releaseRoleDisplayName(const QString& role);

struct ReleaseEntry {
	// Path inside the package (or distribution folder, for loose files).
	QString virtualPath;
	QString role;
	// The file on disk, for project content and compiled maps.
	QString sourcePath;
	// Otherwise the plan reader's layer and entry index it comes from.
	int layer = -1;
	qsizetype layerIndex = -1;
	// Doom WAD lumps: the index into ReleasePlan::lumpSources, with
	// layerIndex as the lump's directory position there.
	int lumpSource = -1;
	quint64 sizeBytes = 0;
	// Items that need it: map names, model paths, "project".
	QStringList requiredBy;
	// True when the game ships a different file at the same path.
	bool replacesStock = false;
	QString stockSource;
	// Shipped beside the package rather than inside it (native game code the
	// engine cannot load from a package).
	bool loose = false;
	QString note;
};

// A reference the release relies on but does not ship.
struct ReleaseReference {
	QString kind;
	QString reference;
	QString path;
	// Which stock package or requirement provides it.
	QString source;
	QStringList requiredBy;
	// True for project files left out because they match the game's own copy.
	bool identicalCopy = false;
};

struct ReleaseProblem {
	// missing, ambiguous, unreadable, unsafe, unbuilt-map, stale-map,
	// outside-project, unverified, incomplete, empty, format
	QString kind;
	QString reference;
	QString message;
	QStringList requiredBy;
	bool blocking = true;
};

struct ReleaseMapInfo {
	QString name;
	QString sourcePath;
	QString compiledPath;
	// The map's own title (worldspawn "message"), when it has one.
	QString title;
	QString formatId;
	bool built = false;
	bool stale = false;
	int entityCount = 0;
	int brushCount = 0;
	// Map names inside a Doom WAD.
	QStringList doomMaps;
};

struct ReleaseCompositionRow {
	QString role;
	int count = 0;
	quint64 bytes = 0;
};

struct ReleasePlan {
	QString gameKey;
	ReleaseScope scope = ReleaseScope::Project;
	QStringList items;
	ProjectReleaseSettings release;
	PackageArchiveFormat format = PackageArchiveFormat::Unknown;
	// "zip" when the package is a plain archive of loose game files.
	QString formatId;
	// The package file the release writes, e.g. "arena.pk3" or "pak0.pak".
	QString packageFileName;
	QString packageFolder;
	QVector<ReleaseEntry> entries;
	QVector<ReleaseReference> stock;
	QVector<ReleaseProblem> problems;
	QVector<ReleaseMapInfo> maps;
	QStringList warnings;
	QStringList limitations;
	quint64 totalBytes = 0;
	int overrideCount = 0;
	// Whether a game asset register took part.
	bool stockChecked = false;
	QString stockDescription;
	bool complete = true;
	bool cancelled = false;
	QDateTime plannedUtc;
	// The reader the plan resolved against; publication reads entries that are
	// not plain project files through it. Not serialized.
	std::shared_ptr<const LayeredPackageReader> reader;
	// The WADs whose lumps a Doom WAD release merges, in output order.
	QVector<std::shared_ptr<const PackageArchiveReader>> lumpSources;

	[[nodiscard]] int blockingCount() const;
	[[nodiscard]] bool canPublish() const;
	[[nodiscard]] QVector<ReleaseCompositionRow> composition() const;
	[[nodiscard]] const ReleaseEntry* entry(const QString& virtualPath) const;
};

struct ReleaseRequest {
	ProjectManifest manifest;
	// The project's effective game.
	QString gameKey;
	ReleaseScope scope = ReleaseScope::Project;
	// Maps (.map, .bsp, .wad), models, texture files or folders: absolute or
	// project-relative paths.
	QStringList items;
	// Effective release settings (see effectiveProjectReleaseSettings).
	ProjectReleaseSettings release;
	// The game's own files, filtered to the sources that count, merged with
	// any requirement registers. Null means the game's files are unknown:
	// references the project lacks are reported unverified, not missing.
	std::shared_ptr<const GameAssetRegister> stock;
	// Packages layered over the project, such as the open draft.
	QVector<LayeredPackageReader::Layer> extraLayers;
	ProjectContentOptions contentOptions;
	// Called between phases; returning false cancels.
	std::function<bool(const QString& phase, int completed, int total)> progress;
};

ReleasePlan planRelease(const ReleaseRequest& request);

// What a project offers for release, for choosers: its maps with build state,
// its models, and its texture folders.
struct ReleaseCatalog {
	QVector<ReleaseMapInfo> maps;
	QStringList models;
	QStringList textureFolders;
	QStringList warnings;
};
ReleaseCatalog releaseCatalog(const ProjectManifest& manifest, const QString& gameKey, const ProjectContentOptions& options = {});

// The package format a scope ships by default: pk3 for Quake III, a WAD for
// Doom, loose files (zip) for single Quake and Quake II maps, a PAK for whole
// Quake and Quake II projects.
QString defaultReleaseFormatId(const QString& gameKey, ReleaseScope scope);

// Merges registers: the stock register first, then each requirement's.
std::shared_ptr<const GameAssetRegister> mergeGameAssetRegisters(const QVector<std::shared_ptr<const GameAssetRegister>>& registers);

// The game's own files for one release, as the GUI and CLI both prepare them.
struct ReleaseStockContext {
	std::shared_ptr<const GameAssetRegister> stock;
	// "Quake III Arena: baseq3/pak0.pk3 ... pak8.pk3 (3,539 files)".
	QString description;
	QStringList warnings;
	GameAssetRegisterStatus status;
	// False when no installation or register file could vouch for the game.
	bool available = false;
};

// The installation's cached register (or `registerFile`, for machines without
// the game), filtered to the release's stock sources and merged with every
// requirement. A stale register is still used, with a warning; a missing one
// leaves `stock` empty with a warning that says how to index the game.
ReleaseStockContext prepareReleaseStock(const GameInstallationProfile* installation, const ProjectReleaseSettings& release, const QString& gameKey,
	const QString& registerFile = {}, const PackageReadControl& control = {});

// The installation a project's release checks against, the same in the GUI and
// the CLI: the one the manifest links; else, for a manifest that names its
// game, the selected installation when it plays that game, then the first
// saved one that does; else the selected installation. False when none fits.
bool releaseInstallationFor(const ProjectManifest& manifest, const QVector<GameInstallationProfile>& installations, const QString& selectedId,
	GameInstallationProfile* installation);

// Indexes a requirement (a folder or package) once and caches the result
// beside the asset registers, keyed by path, size and modification time.
std::shared_ptr<const GameAssetRegister> requirementAssetRegister(const ProjectReleaseRequirement& requirement, const QString& installationRoot,
	QString* error = nullptr, const PackageReadControl& control = {});

QJsonObject releasePlanJson(const ReleasePlan& plan);
QString releasePlanText(const ReleasePlan& plan);

} // namespace vibestudio
