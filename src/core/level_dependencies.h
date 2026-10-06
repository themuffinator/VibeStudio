#pragma once

#include "core/level_map.h"
#include "core/package_archive.h"

#include <QJsonObject>
#include <QJsonArray>

#include <functional>

namespace vibestudio {

struct ModelMesh;

enum class LevelDependencyStatus { Resolved, Missing, Builtin, Ambiguous, Unreadable, Unsafe };

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

LevelDependencyReport inspectLevelDependencies(const LevelMapDocument& document, const PackageArchiveReader& archive,
	LevelDependencyProgress progress = {}, const QString& buildTarget = {});
// Quake III model/shader tokens are rooted at the game filesystem. Reviews all
// retained external slots without loading geometry or assuming a map instance.
LevelDependencyReport inspectModelMaterialDependencies(const ModelMesh& mesh, const PackageArchiveReader& archive,
	LevelDependencyProgress progress = {});
QString levelDependencyStatusId(LevelDependencyStatus status);
QString levelDependencyStatusName(LevelDependencyStatus status);
QJsonObject levelDependencyReportJson(const LevelDependencyReport& report);
QString levelDependencyReportText(const LevelDependencyReport& report);

} // namespace vibestudio
