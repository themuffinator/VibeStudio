#pragma once

#include "core/level_map.h"

#include <QDateTime>
#include <functional>

namespace vibestudio
{

// A harmless map comment keeps a deliberately chosen Quake II target distinct
// from Quake's identical text grammar after reopening in GUI/CLI or recovery.
inline constexpr auto kQuake2MapTargetHeader = "// VibeStudio target: quake2\n";

struct LevelMapCreateRequest {
	// quake, quake2, quake3, doom, or hexen. Stable CLI/profile identifiers.
	QString game = QStringLiteral("quake3");
	QString name = QStringLiteral("Untitled");
	QString mapName = QStringLiteral("MAP01");
	bool starterRoom = true;
	QString wallTexture;
	QString floorTexture;
	QString ceilingTexture;
};

bool createLevelMap(const LevelMapCreateRequest& request, LevelMapDocument* document, QString* error = nullptr);

struct LevelDocumentSaveRequest {
	QString path;
	bool dryRun = false;
	bool overwrite = false;
	bool keepBackup = true;
	// Empty selects <destination folder>/.vibestudio/map-backups.
	QString backupDirectory;
	std::function<bool()> isCancelled;
};

// Serializes an immutable snapshot, checks source conflicts, backs up replaced
// content, and commits atomically. No source or session state changes on failure.
LevelMapSaveReport writeLevelDocument(const LevelMapDocument& document, const LevelDocumentSaveRequest& request);
void adoptLevelDocumentSave(LevelMapDocument* document, const LevelMapSaveReport& report);

struct LevelMapRecovery {
	QString path;
	QString sourcePath;
	QString mapName;
	QString engineFamily;
	QString format;
	QDateTime writtenUtc;
	quint64 revision = 0;
	qint64 payloadBytes = 0;
	QByteArray contentHash;
	QByteArray sourceContentHash;
	bool doomGeometryChanged = false;
	QString error;
	[[nodiscard]] bool isValid() const { return error.isEmpty() && !path.isEmpty() && payloadBytes > 0; }
};

// One self-contained, versioned record: bounded JSON metadata plus map bytes.
// The UUID is caller-owned so separate documents/instances cannot overwrite one
// another. A record never writes to its stored source path while being restored.
QString writeLevelMapRecovery(const LevelMapDocument& document, const QString& directory, const QString& id, QString* error = nullptr,
							  std::function<bool()> isCancelled = {});
LevelMapRecovery inspectLevelMapRecovery(const QString& path);
QVector<LevelMapRecovery> listLevelMapRecoveries(const QString& directory);
bool restoreLevelMapRecovery(const QString& path, LevelMapDocument* document, QString* error = nullptr);
bool removeLevelMapRecovery(const QString& directory, const QString& id, QString* error = nullptr);

} // namespace vibestudio
