#pragma once

#include "core/compiler_profiles.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

enum class LevelMapFormat {
	Unknown,
	DoomWad,
	QuakeMap,
	Quake3Map,
};

enum class LevelMapIssueSeverity {
	Info,
	Warning,
	Error,
};

enum class LevelMapSelectionKind {
	None,
	Entity,
	DoomVertex,
	DoomLinedef,
	DoomThing,
	QuakeBrush,
};

struct LevelMapVec3 {
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
	bool valid = false;
};

struct LevelMapIssue {
	LevelMapIssueSeverity severity = LevelMapIssueSeverity::Warning;
	QString code;
	QString message;
	QString objectId;
	int line = 0;
};

struct LevelMapProperty {
	QString key;
	QString value;
	int line = 0;
};

struct LevelMapEntity {
	int id = -1;
	QString className;
	QVector<LevelMapProperty> properties;
	LevelMapVec3 origin;
	int startLine = 0;
	int endLine = 0;
	bool selected = false;
};

struct LevelMapBrush {
	int id = -1;
	int entityId = -1;
	int faceCount = 0;
	QStringList textureNames;
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	bool selected = false;
};

struct LevelMapDoomVertex {
	int id = -1;
	double x = 0.0;
	double y = 0.0;
	bool selected = false;
};

struct LevelMapDoomLinedef {
	int id = -1;
	int startVertex = -1;
	int endVertex = -1;
	int flags = 0;
	int special = 0;
	int tag = 0;
	int frontSidedef = -1;
	int backSidedef = -1;
	bool selected = false;
};

struct LevelMapDoomThing {
	int id = -1;
	double x = 0.0;
	double y = 0.0;
	int angle = 0;
	int type = 0;
	int flags = 0;
	bool selected = false;
};

struct LevelMapDoomSidedef {
	int id = -1;
	int sector = -1;
	QString upperTexture;
	QString lowerTexture;
	QString middleTexture;
};

struct LevelMapDoomSector {
	int id = -1;
	int floorHeight = 0;
	int ceilingHeight = 0;
	QString floorTexture;
	QString ceilingTexture;
	int lightLevel = 0;
	int special = 0;
	int tag = 0;
};

struct LevelMapStatistics {
	int entityCount = 0;
	int brushCount = 0;
	int brushFaceCount = 0;
	int doomThingCount = 0;
	int doomVertexCount = 0;
	int doomLinedefCount = 0;
	int doomSidedefCount = 0;
	int doomSectorCount = 0;
	int textureReferenceCount = 0;
	int uniqueTextureCount = 0;
	int issueCount = 0;
	int warningCount = 0;
	int errorCount = 0;
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
};

struct LevelMapUndoCommand {
	QString description;
	QString undoDescription;
	QString commandKind;
	QString objectKind;
	int objectId = -1;
	int entityId = -1;
	QString key;
	QString oldValue;
	QString newValue;
	LevelMapVec3 delta;
};

struct LevelMapDocument {
	QString sourcePath;
	QString outputPath;
	QString mapName;
	QString engineFamily;
	LevelMapFormat format = LevelMapFormat::Unknown;
	QString originalText;
	QStringList textLines;
	QVector<LevelMapEntity> entities;
	QVector<LevelMapBrush> brushes;
	QVector<LevelMapDoomVertex> doomVertices;
	QVector<LevelMapDoomLinedef> doomLinedefs;
	QVector<LevelMapDoomThing> doomThings;
	QVector<LevelMapDoomSidedef> doomSidedefs;
	QVector<LevelMapDoomSector> doomSectors;
	QStringList textureReferences;
	QVector<LevelMapIssue> issues;
	LevelMapSelectionKind selectionKind = LevelMapSelectionKind::None;
	int selectedObjectId = -1;
	QVector<LevelMapUndoCommand> undoStack;
	QVector<LevelMapUndoCommand> redoStack;
	QString editState = QStringLiteral("clean");
	QMap<QString, QByteArray> doomLumps;
	QStringList doomLumpOrder;
};

struct LevelMapLoadRequest {
	QString path;
	QString mapName;
	QString engineHint;
};

struct LevelMapSaveReport {
	QString sourcePath;
	QString outputPath;
	QString mapName;
	LevelMapFormat format = LevelMapFormat::Unknown;
	bool dryRun = false;
	bool written = false;
	QString editState;
	QStringList summaryLines;
	QStringList warnings;
	QStringList errors;

	[[nodiscard]] bool succeeded() const;
};

QString levelMapFormatId(LevelMapFormat format);
QString levelMapFormatDisplayName(LevelMapFormat format);
QString levelMapIssueSeverityId(LevelMapIssueSeverity severity);
QString levelMapSelectionKindId(LevelMapSelectionKind kind);

bool loadLevelMap(const LevelMapLoadRequest& request, LevelMapDocument* document, QString* error = nullptr);
LevelMapStatistics levelMapStatistics(const LevelMapDocument& document);
QStringList levelMapStatisticsLines(const LevelMapDocument& document);
QStringList levelMapEntityLines(const LevelMapDocument& document);
QStringList levelMapTextureLines(const LevelMapDocument& document);
QStringList levelMapValidationLines(const LevelMapDocument& document);
QStringList levelMapViewLines(const LevelMapDocument& document);
QStringList levelMapSelectionLines(const LevelMapDocument& document);
QStringList levelMapPropertyLines(const LevelMapDocument& document);
QStringList levelMapUndoLines(const LevelMapDocument& document);
QString levelMapReportText(const LevelMapDocument& document);

bool selectLevelMapObject(LevelMapDocument* document, const QString& selector, QString* error = nullptr);
bool setLevelMapEntityProperty(LevelMapDocument* document, int entityId, const QString& key, const QString& value, QString* error = nullptr);
bool moveLevelMapObject(LevelMapDocument* document, const QString& objectKind, int objectId, double dx, double dy, double dz, QString* error = nullptr);
bool undoLevelMapEdit(LevelMapDocument* document, QString* error = nullptr);
bool redoLevelMapEdit(LevelMapDocument* document, QString* error = nullptr);
LevelMapSaveReport saveLevelMapAs(const LevelMapDocument& document, const QString& outputPath, bool dryRun = false, bool overwriteExisting = false);
QString levelMapSaveReportText(const LevelMapSaveReport& report);
CompilerCommandRequest compilerRequestForLevelMap(const LevelMapDocument& document, const QString& profileId, const QString& outputPath = QString());

} // namespace vibestudio
