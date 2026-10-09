#pragma once

// The game asset register: what a game installation already ships with.
//
// Packaging a map or mod has to include the author's own files and leave the
// game's own out. Shipping pak0's textures again is wasteful at best and a
// licence breach at worst, and a release that silently depends on a file the
// player does not have is broken. The register lists every file in the stock
// packages of one installation (path, size and CRC-32), plus the names a map
// can use without a file of that name: Quake III shader declarations, and Doom
// texture, flat, patch, sprite, sound and music names.
//
// It is built read-only from packages the user already owns, cached beside the
// studio settings, and reported stale when a stock package changes. No game
// data or file list ships with VibeStudio.
//
// Format knowledge comes from public sources:
// - Stock package names per game: the released id Software engine sources
//   (Quake `COM_InitFilesystem`, Quake II `FS_InitFilesystem`, Quake III
//   `FS_Startup` and its `pak*.pk3` search) and their documentation.
// - Doom TEXTURE1/TEXTURE2/PNAMES layouts: Chocolate Doom 3.1.0 r_data.c
//   (GPL-2.0-or-later), already credited for the Doom preview.
// - Doom namespaces and lump conventions: https://doomwiki.org/wiki/WAD

#include "core/game_installation.h"
#include "core/package_archive.h"
#include "core/package_content.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

// One package the register indexed. The id is the path relative to the
// installation root, lower-cased with forward slashes, so it stays the same
// on every machine ("baseq3/pak0.pk3", "doom2.wad").
struct GameAssetRegisterSource {
	QString id;
	QString relativePath;
	QString absolutePath;
	// "base" sources are what every player of the game has. "expansion" and
	// "alternative" sources (mission packs, other IWADs) count only when a
	// project says it targets them.
	QString role;
	QString label;
	PackageArchiveFormat format = PackageArchiveFormat::Unknown;
	qint64 sizeBytes = 0;
	QDateTime modifiedUtc;
	int fileCount = 0;
};

struct GameAssetRegisterFile {
	// As the package stores it: forward slashes, case preserved. Doom lumps
	// are bare lump names.
	QString path;
	quint64 sizeBytes = 0;
	quint32 crc32 = 0;
	int source = -1;
	// Doom namespace hints (wad-flat, wad-patch...), empty for other packages.
	QString typeHint;
};

// How a project file relates to the stock file at the same path.
enum class GameAssetMatch {
	// The game has no file at this path.
	None,
	// Same size and CRC-32: the project holds a copy of the game's own file.
	Identical,
	// Same path, different content: the project replaces a game file.
	Different,
};

struct GameAssetRegisterName {
	QString name;
	int source = -1;
	// File index of the declaring script, for shader declarations; -1 otherwise.
	int file = -1;
};

class GameAssetRegister final {
public:
	static constexpr int kFormatVersion = 1;
	// Bounds for building and loading, shared with package admission.
	static constexpr int kMaximumSources = 64;
	static constexpr qsizetype kMaximumFiles = PackageIndexLimits::entryCeiling;
	static constexpr qsizetype kMaximumNames = 500000;
	static constexpr qint64 kMaximumFileBytes = 96ll * 1024 * 1024;

	QString gameKey;
	QString installationId;
	QString installationName;
	QString installationRoot;
	QDateTime createdUtc;
	QString studioVersion;
	QVector<GameAssetRegisterSource> sources;
	QVector<GameAssetRegisterFile> files;
	// Quake III-family shader declarations from scripts/*.shader.
	QVector<GameAssetRegisterName> shaders;
	// Doom names by namespace: texture (TEXTURE1/TEXTURE2), patch (PNAMES and
	// the patch namespace), flat, sprite, sound, music and map.
	QHash<QString, QVector<GameAssetRegisterName>> doomNames;

	[[nodiscard]] bool isEmpty() const;
	// Rebuilds the lookup tables. Call after changing the public vectors.
	void rebuildIndex();
	// A copy holding only the named sources (ids as in GameAssetRegisterSource::id),
	// keeping file and name order. Unknown ids are ignored.
	[[nodiscard]] GameAssetRegister filtered(const QStringList& sourceIds) const;
	// The source ids a project of this register's game uses unless it says
	// otherwise: every "base" source.
	[[nodiscard]] QStringList defaultSourceIds() const;

	// Case-insensitive exact path lookup; later sources shadow earlier ones.
	[[nodiscard]] const GameAssetRegisterFile* file(const QString& virtualPath) const;
	[[nodiscard]] GameAssetMatch match(const QString& virtualPath, quint64 sizeBytes, quint32 crc32) const;
	// The declaring script's file, or nullptr.
	[[nodiscard]] const GameAssetRegisterFile* shaderScript(const QString& shaderName) const;
	[[nodiscard]] bool hasShader(const QString& shaderName) const;
	[[nodiscard]] bool hasDoomName(const QString& namespaceId, const QString& name) const;
	// Source index providing a Doom name, or -1.
	[[nodiscard]] int doomNameSource(const QString& namespaceId, const QString& name) const;
	[[nodiscard]] const GameAssetRegisterSource* source(int index) const;
	[[nodiscard]] QString sourceLabel(int index) const;

private:
	QHash<QString, int> m_pathIndex;
	QHash<QString, int> m_shaderIndex;
	QHash<QString, QHash<QString, int>> m_doomIndex;
};

// A stock package an installation can have, relative to its root.
struct GameStockPackage {
	QString relativePath;
	QString role;
	QString label;
};

// Every stock package the game can ship, in engine search order (later files
// override earlier ones). Data only: nothing is read.
QVector<GameStockPackage> gameStockPackageCandidates(const QString& gameKey);
// The candidates that exist under the installation root, matched without
// regard to case, followed by any extra base packages saved on the profile.
QVector<GameAssetRegisterSource> discoverGameStockSources(const GameInstallationProfile& installation, QStringList* warnings = nullptr);

struct GameAssetRegisterBuildRequest {
	GameInstallationProfile installation;
	// Explicit packages override discovery: absolute paths, or paths relative
	// to the installation root.
	QStringList packagePaths;
	// Cancellation, plus "<package>" phase progress with byte counts.
	PackageReadControl control;
};

struct GameAssetRegisterBuildResult {
	GameAssetRegister registerData;
	bool succeeded = false;
	bool cancelled = false;
	QString error;
	QStringList warnings;
	quint64 bytesRead = 0;
};

// Opens each stock package read-only and indexes it. ZIP/PK3 CRCs come from
// the central directory; PAK and WAD entries are streamed once to compute
// theirs. Cancellation or any unreadable package fails the whole build.
GameAssetRegisterBuildResult buildGameAssetRegister(const GameAssetRegisterBuildRequest& request);

// Storage: <app data or the --settings-file folder>/asset-registers/<id>.json
QString gameAssetRegisterDirectory();
QString gameAssetRegisterPath(const QString& installationId);
bool saveGameAssetRegister(const GameAssetRegister& registerData, const QString& path, QString* error = nullptr);
bool loadGameAssetRegister(const QString& path, GameAssetRegister* registerData, QString* error = nullptr);
QByteArray gameAssetRegisterJsonBytes(const GameAssetRegister& registerData);
bool parseGameAssetRegister(const QByteArray& bytes, GameAssetRegister* registerData, QString* error = nullptr);

struct GameAssetRegisterStatus {
	QString installationId;
	QString path;
	bool exists = false;
	bool loaded = false;
	// False when a stock package changed, vanished or appeared since the build.
	bool fresh = false;
	int sourceCount = 0;
	int fileCount = 0;
	QDateTime createdUtc;
	QString error;
	QStringList staleReasons;
	[[nodiscard]] bool usable() const { return loaded && fresh; }
};

// Compares a loaded register with the packages the installation has now.
// Reads directory metadata only.
GameAssetRegisterStatus gameAssetRegisterFreshness(const GameAssetRegister& registerData, const GameInstallationProfile& installation);
// Loads the cached register for an installation and checks it.
GameAssetRegisterStatus gameAssetRegisterStatus(const GameInstallationProfile& installation, GameAssetRegister* loaded = nullptr);

QString gameAssetMatchId(GameAssetMatch match);
QJsonObject gameAssetRegisterSummaryJson(const GameAssetRegister& registerData);
QJsonObject gameAssetRegisterStatusJson(const GameAssetRegisterStatus& status);
QString gameAssetRegisterSummaryText(const GameAssetRegister& registerData);

// CRC-32 of a file on disk, streamed in 64 KiB chunks.
bool fileCrc32(const QString& path, quint32* crc, QString* error = nullptr, const std::function<bool()>& isCancelled = {});

} // namespace vibestudio
