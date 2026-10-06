#pragma once

#include "core/level_map.h"
#include <QJsonObject>
#include <functional>

namespace vibestudio
{
inline constexpr qint64 levelPrefabByteLimit = 8 * 1024 * 1024;
using LevelPrefabCancellation = std::function<bool()>;

// Portable authoring asset: map geometry and entity properties are embedded;
// material/model/sound paths stay relative to the project's normal asset set.
// No source paths or executable instructions are stored in this format.
struct LevelPrefab {
	QString name, description, engineFamily;
	LevelMapVec3 anchor;
	QString mapText;
};
struct LevelPrefabCreateRequest {
	QString name, description;
	// Invalid chooses the bottom centre of the exported selection bounds.
	LevelMapVec3 anchor;
};
struct LevelPrefabPlacement {
	LevelMapVec3 position{0, 0, 0, true};
	// Applied around the prefab anchor, X then Y then Z, in degrees.
	LevelMapVec3 rotation{0, 0, 0, true};
	bool textureLock = true;
	// Empty automatically chooses prefab1_, prefab2_, ... without collisions.
	// A supplied prefix must also be unique against destination names/references.
	QString targetPrefix;
};
struct LevelPrefabReport {
	LevelMapStatistics statistics;
	QString dialect, prefix;
	QMap<QString, QString> renamedTargets;
	QStringList externalTargets, warnings;
	QVector<LevelMapSelectionRef> inserted;
};
struct LevelPrefabWriteReport {
	QString path, backupPath;
	QStringList warnings, recoveryPaths;
	bool committed = false, dryRun = false;
};

// A selected primitive in a brush entity exports that whole owner, preserving
// its properties, sibling geometry and internal links. worldspawn properties
// are not included. The source and its selection/history are never modified.
bool createLevelPrefab(const LevelMapDocument &source, const LevelPrefabCreateRequest &request, LevelPrefab *prefab,
					   LevelPrefabReport *report = nullptr, QString *error = nullptr, LevelPrefabCancellation cancelled = {});
bool inspectLevelPrefab(const LevelPrefab &prefab, LevelMapDocument *definition, LevelPrefabReport *report = nullptr,
						QString *error = nullptr, LevelPrefabCancellation cancelled = {});
QByteArray serializeLevelPrefab(const LevelPrefab &prefab, QString *error = nullptr, LevelPrefabCancellation cancelled = {});
bool parseLevelPrefab(const QByteArray &bytes, LevelPrefab *prefab, QString *error = nullptr, LevelPrefabCancellation cancelled = {});
bool readLevelPrefab(const QString &path, LevelPrefab *prefab, QString *error = nullptr, LevelPrefabCancellation cancelled = {});
// Shared journalled publication protects existing files with a verified backup.
bool writeLevelPrefab(const LevelPrefab &prefab, const QString &path, bool overwrite = false, bool dryRun = false,
					  LevelPrefabWriteReport *report = nullptr, QString *error = nullptr, LevelPrefabCancellation cancelled = {});
// Validate/transform an isolated prefab, namespace internal target links, then
// insert through the clipboard service as one undo step. On failure, every part
// of the destination and the caller's report stay unchanged. Mixed brush
// dialects and cross-engine insertion are refused before modifying the map.
bool insertLevelPrefab(LevelMapDocument *destination, const LevelPrefab &prefab, const LevelPrefabPlacement &placement,
					   LevelPrefabReport *report = nullptr, QString *error = nullptr, LevelPrefabCancellation cancelled = {});
QJsonObject levelPrefabReportJson(const LevelPrefabReport &report);

} // namespace vibestudio
