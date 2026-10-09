#pragma once

#include "core/level_map.h"
#include "core/package_archive.h"

#include <QJsonObject>
#include <QJsonArray>

#include <functional>
#include <memory>

namespace vibestudio {

struct ModelMesh;
class GameAssetRegister;

// Stock (appended last so serialized ids stay stable): the game's own packages
// provide the reference, per the game asset register.
enum class LevelDependencyStatus { Resolved, Missing, Builtin, Ambiguous, Unreadable, Unsafe, Stock };

struct LevelDependency {
	QString kind; // texture, shader-image, model, model-material, model-skin, sound, doom-wall/flat/input
	QString reference;
	LevelDependencyStatus status = LevelDependencyStatus::Missing;
	QString resolvedPath;
	QString sourceLayer;
	quint64 sizeBytes = 0;
	QStringList candidates;
	// Selectors refer to the document snapshot used for this report.
	QStringList selectors;
	QStringList requiredBy;
	QString note;
	qint64 sourceOrdinal = -1;
	QString namespaceId;
	// Resolved rows whose path the stock packages also hold: the project file
	// is either a copy of the game's own file or a replacement for it. The
	// caller compares content; the inspector reads metadata only.
	bool stockShadowed = false;
	// The register source providing a Stock row, or shadowed by a Resolved one.
	QString stockSource;
};

struct LevelDependencyReport {
	QString mapPath;
	QString mapName;
	QString packagePath;
	QVector<LevelDependency> dependencies;
	QJsonArray modelAppearances;
	// Unique package files in deterministic order, including declaring scripts.
	// Doom paths may repeat across namespaces; input rows carry exact occurrences.
	QStringList resolvedPaths;
	quint64 totalBytes = 0;
	int missingCount = 0;
	int problemCount = 0;
	int builtinCount = 0;
	// References the game's own packages provide (status Stock).
	int stockCount = 0;
	bool complete = true;
	bool cancelled = false;
	bool exportSupported = true;
	QStringList warnings;
	QStringList limitations;
	[[nodiscard]] bool canExport() const;
};

// Called before/after script reads and between dependencies. Returning false
// cancels. No GUI or event-loop dependency; callbacks run on the caller thread.
using LevelDependencyProgress = std::function<bool(int completed, int total)>;

struct LevelDependencyOptions {
	QString buildTarget;
	// The game's own files, already filtered to the sources a project counts.
	// A reference the archive does not provide resolves against it as Stock
	// and is never expanded: a stock shader's images and a stock model's skins
	// are stock too. Project shaders still win over stock declarations, and a
	// project file shadowing a stock path is reported with stockShadowed.
	std::shared_ptr<const GameAssetRegister> stock;
	// Quake (not Quake II) compiles its textures into the BSP: a release of
	// a built map needs none of them, so texture references and the texture
	// WAD scan are skipped.
	bool quakeTexturesEmbedded = false;
};

LevelDependencyReport inspectLevelDependencies(const LevelMapDocument& document, const PackageArchiveReader& archive,
	LevelDependencyProgress progress = {}, const QString& buildTarget = {});
LevelDependencyReport inspectLevelDependencies(const LevelMapDocument& document, const PackageArchiveReader& archive,
	LevelDependencyProgress progress, const LevelDependencyOptions& options);
// Quake III model/shader tokens are rooted at the game filesystem. Reviews all
// retained external slots without loading geometry or assuming a map instance.
LevelDependencyReport inspectModelMaterialDependencies(const ModelMesh& mesh, const PackageArchiveReader& archive,
	LevelDependencyProgress progress = {}, std::shared_ptr<const GameAssetRegister> stock = {});
QString levelDependencyStatusId(LevelDependencyStatus status);
QString levelDependencyStatusName(LevelDependencyStatus status);
QJsonObject levelDependencyReportJson(const LevelDependencyReport& report);
QString levelDependencyReportText(const LevelDependencyReport& report);

} // namespace vibestudio
