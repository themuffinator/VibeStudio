#pragma once

#include "core/compiler_profiles.h"

#include <QMap>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

#include <array>

namespace vibestudio {

enum class LevelMapFormat {
	Unknown,
	DoomWad,
	QuakeMap,
	Quake3Map,
};

// Binary map-lump dialects used by idTech1 WAD maps.
//
// `Doom` is the original 14-byte linedef / 10-byte thing layout, `Hexen` is the
// 16-byte linedef / 20-byte thing layout introduced by Hexen and kept by ZDoom,
// and `Udmf` is the textual `TEXTMAP` universal format.
// References: https://doomwiki.org/wiki/Linedef,
// https://doomwiki.org/wiki/Thing, https://doomwiki.org/wiki/UDMF
enum class LevelMapDoomFormat {
	Doom,
	Hexen,
	Udmf,
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
	DoomSector,
	QuakeBrush,
	QuakePatch,
};

// One member of the selection set. `objectId` is the identifier inside `kind`,
// so a reference serializes to the same `kind:id` selector `selectLevelMapObject`
// accepts.
struct LevelMapSelectionRef {
	LevelMapSelectionKind kind = LevelMapSelectionKind::None;
	int objectId = -1;
};

inline bool operator==(const LevelMapSelectionRef& left, const LevelMapSelectionRef& right)
{
	return left.kind == right.kind && left.objectId == right.objectId;
}

inline bool operator!=(const LevelMapSelectionRef& left, const LevelMapSelectionRef& right)
{
	return !(left == right);
}

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
	// Source lines of keys that were removed by an edit. Save-back deletes them.
	QVector<int> removedPropertyLines;
};

// One brush face as written in a Quake-family `.map` file. The three points
// define the face plane; texture placement is either the classic Quake/Quake II
// shift/rotate/scale form or the Valve 220 / Quake III `brushDef` explicit axis
// form.
struct LevelMapBrushFace {
	int id = -1;
	LevelMapVec3 p0;
	LevelMapVec3 p1;
	LevelMapVec3 p2;
	QString textureName;
	double shiftX = 0.0;
	double shiftY = 0.0;
	double rotation = 0.0;
	double scaleX = 1.0;
	double scaleY = 1.0;
	bool explicitTextureAxes = false;
	LevelMapVec3 uAxis;
	double uOffset = 0.0;
	LevelMapVec3 vAxis;
	double vOffset = 0.0;
	qint64 contentFlags = 0;
	qint64 surfaceFlags = 0;
	qint64 surfaceValue = 0;
	int line = 0;
	// `brushDef3` faces carry a plane instead of three points. The plane is
	// stored exactly as written: `( nx ny nz d )` means nx*x + ny*y + nz*z + d = 0,
	// matching the idTech4 `brushDef3` primitive that GtkRadiant and Doom 3 share.
	bool explicitPlane = false;
	LevelMapVec3 planeNormal;
	double planeDistance = 0.0;
	// `brushDef` / `brushDef3` 2x3 texture matrix, row major.
	bool explicitTextureMatrix = false;
	std::array<double, 6> textureMatrix {{1.0, 0.0, 0.0, 0.0, 1.0, 0.0}};
};

struct LevelMapBrush {
	int id = -1;
	int entityId = -1;
	int faceCount = 0;
	QStringList textureNames;
	QVector<LevelMapBrushFace> faces;
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	bool boundsSolved = false;
	bool selected = false;
	int startLine = 0;
	int endLine = 0;
	// "classic", "valve220", "brushDef" or "brushDef3".
	QString primitiveKind;
	// True once an edit has moved this brush, so serialization rewrites its face
	// lines instead of re-emitting the original text.
	bool geometryDirty = false;
};

// Quake III `patchDef2` / `patchDef3` control mesh.
struct LevelMapPatch {
	int id = -1;
	int entityId = -1;
	QString textureName;
	int width = 0;
	int height = 0;
	// Row major, `height` rows of `width` columns: `controlPoints[row * width +
	// column]`. The `.map` file stores the grid width-major, so the parser
	// transposes it; `controlGridNormalized` records whether that succeeded.
	QVector<LevelMapVec3> controlPoints;
	QVector<double> controlU;
	QVector<double> controlV;
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	int startLine = 0;
	int endLine = 0;
	bool selected = false;
	// patchDef3 carries explicit subdivision counts in its header tuple.
	bool fixedSubdivisions = false;
	int subdivisionsX = 0;
	int subdivisionsY = 0;
	// True once an edit has moved this patch, so serialization rewrites its
	// control rows instead of re-emitting the original text.
	bool geometryDirty = false;
	// Source line of each parenthesised control group, one per grid column in
	// file order, so a moved patch can be written back in place. Empty when the
	// patch body could not be attributed to lines.
	QVector<int> controlRowLines;
	// False when the parsed grid was ragged or disagreed with the header, in
	// which case controlPoints keeps raw file order and must not be written back.
	bool controlGridNormalized = false;
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
	// Hexen-format linedefs replace the tag with five special arguments.
	std::array<int, 5> args {{0, 0, 0, 0, 0}};
};

struct LevelMapDoomThing {
	int id = -1;
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
	int angle = 0;
	int type = 0;
	int flags = 0;
	bool selected = false;
	// Hexen-format things add a thing id, an action special and five arguments.
	int tid = 0;
	int special = 0;
	std::array<int, 5> args {{0, 0, 0, 0, 0}};
};

struct LevelMapDoomSidedef {
	int id = -1;
	int sector = -1;
	int offsetX = 0;
	int offsetY = 0;
	QString upperTexture;
	QString lowerTexture;
	QString middleTexture;
	bool selected = false;
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
	bool selected = false;
};

struct LevelMapStatistics {
	int entityCount = 0;
	int brushCount = 0;
	int brushFaceCount = 0;
	int solvedBrushCount = 0;
	int degenerateBrushCount = 0;
	int patchCount = 0;
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

// One object inside a compound move. A compound command replays its steps in
// order and undoes them in reverse, so several objects move as a single undo
// step instead of one step per object.
struct LevelMapMoveStep {
	// "entity", "vertex", "linedef", "thing", "brush" or "patch".
	QString objectKind;
	int objectId = -1;
	// Entity moves rewrite the `origin` key, so the before/after text is kept.
	QString oldValue;
	QString newValue;
	bool originSynthesized = false;
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
	// True when the edited key already existed before the command ran. Undo of a
	// command with `keyExisted == false` removes the key instead of blanking it.
	bool keyExisted = true;
	// Source line of a removed key, so undo can put it back in place.
	int propertyLine = 0;
	// True when a move had to synthesize an `origin` key on an entity that had
	// none. Undo removes the synthesized key again.
	bool originSynthesized = false;
	// Before/after snapshots for edits that touch binary Doom records.
	bool hasThingSnapshot = false;
	LevelMapDoomThing oldThing;
	LevelMapDoomThing newThing;
	bool hasSectorSnapshot = false;
	LevelMapDoomSector oldSector;
	LevelMapDoomSector newSector;
	bool hasSidedefSnapshot = false;
	LevelMapDoomSidedef oldSidedef;
	LevelMapDoomSidedef newSidedef;
	// Steps of a compound move (`commandKind == "move-selection"`): one entry per
	// moved object, all sharing `delta`. Empty for every other command kind.
	QVector<LevelMapMoveStep> moveSteps;
};

struct LevelMapDocument {
	QString sourcePath;
	QString outputPath;
	QString mapName;
	QString engineFamily;
	LevelMapFormat format = LevelMapFormat::Unknown;
	LevelMapDoomFormat doomFormat = LevelMapDoomFormat::Doom;
	QString originalText;
	QStringList textLines;
	// Line ending detected when the source text was loaded, written back as-is.
	QString lineEnding = QStringLiteral("\n");
	QVector<LevelMapEntity> entities;
	QVector<LevelMapBrush> brushes;
	QVector<LevelMapPatch> patches;
	QVector<LevelMapDoomVertex> doomVertices;
	QVector<LevelMapDoomLinedef> doomLinedefs;
	QVector<LevelMapDoomThing> doomThings;
	QVector<LevelMapDoomSidedef> doomSidedefs;
	QVector<LevelMapDoomSector> doomSectors;
	QStringList textureReferences;
	QVector<LevelMapIssue> issues;
	// Primary selection. These two fields are the original single-selection API
	// and stay authoritative for every existing reader; they always mirror the
	// primary member of `selection` (its last element), or None/-1 when the set
	// is empty.
	LevelMapSelectionKind selectionKind = LevelMapSelectionKind::None;
	int selectedObjectId = -1;
	// Multi-selection set, in the order members were added. The last element is
	// the primary member. Never contains duplicates and never contains a
	// reference to an object that was missing when it was added.
	QVector<LevelMapSelectionRef> selection;
	QVector<LevelMapUndoCommand> undoStack;
	QVector<LevelMapUndoCommand> redoStack;
	int undoLimit = 200;
	// Undo depth that matches the last save, or -1 when no save is reachable.
	int savedUndoDepth = -1;
	QString editState = QStringLiteral("clean");
	QMap<QString, QByteArray> doomLumps;
	QStringList doomLumpOrder;
	// Magic of the source WAD ("IWAD" or "PWAD"), preserved on save.
	QString doomWadMagic = QStringLiteral("PWAD");
	// Set when an edit invalidates the precomputed node/blockmap lumps.
	bool doomGeometryChanged = false;
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
	// Lumps that no longer match the edited geometry and need a node build.
	QStringList staleLumps;

	[[nodiscard]] bool succeeded() const;
};

QString levelMapFormatId(LevelMapFormat format);
QString levelMapFormatDisplayName(LevelMapFormat format);
QString levelMapDoomFormatId(LevelMapDoomFormat format);
QString levelMapDoomFormatDisplayName(LevelMapDoomFormat format);
QString levelMapIssueSeverityId(LevelMapIssueSeverity severity);
QString levelMapSelectionKindId(LevelMapSelectionKind kind);
// Inverse of levelMapSelectionKindId. Unknown or unselectable ids (including
// "sidedef" and "none") map back to LevelMapSelectionKind::None.
LevelMapSelectionKind levelMapSelectionKindFromId(const QString& id);
QString levelMapSelectionRefId(const LevelMapSelectionRef& ref);
// False for kinds that carry no position of their own, currently only sectors,
// which move by moving their vertices.
bool levelMapSelectionKindIsMovable(LevelMapSelectionKind kind);

// Grid snapping lives here rather than in the viewport so that a mouse drag, an
// arrow-key nudge and a CLI move all land on the same coordinates.
//
// `gridSize` is in map units. A non-positive or non-finite grid, or a
// non-finite value, returns the value unchanged. Rounding is half away from
// zero, so the grid is symmetric about the origin and a negative coordinate
// snaps exactly the way its positive mirror does.
double snapLevelMapCoordinate(double value, double gridSize);
LevelMapVec3 snapLevelMapPosition(const LevelMapVec3& position, double gridSize);
LevelMapVec3 snapLevelMapDelta(const LevelMapVec3& delta, double gridSize);

bool loadLevelMap(const LevelMapLoadRequest& request, LevelMapDocument* document, QString* error = nullptr);
LevelMapStatistics levelMapStatistics(const LevelMapDocument& document);
QStringList levelMapStatisticsLines(const LevelMapDocument& document);
QStringList levelMapEntityLines(const LevelMapDocument& document);
QStringList levelMapTextureLines(const LevelMapDocument& document);
QStringList levelMapValidationLines(const LevelMapDocument& document);
QStringList levelMapViewLines(const LevelMapDocument& document);
QStringList levelMapSelectionLines(const LevelMapDocument& document);
QStringList levelMapPropertyLines(const LevelMapDocument& document);
QStringList levelMapSectorLines(const LevelMapDocument& document);
QStringList levelMapSidedefLines(const LevelMapDocument& document);
QStringList levelMapPatchLines(const LevelMapDocument& document);
QStringList levelMapUndoLines(const LevelMapDocument& document);
QString levelMapReportText(const LevelMapDocument& document);

// Replaces the whole selection with a single object, exactly as before. The
// selector is `kind:id`.
bool selectLevelMapObject(LevelMapDocument* document, const QString& selector, QString* error = nullptr);

// Multi-selection. Every mutator keeps `selectionKind`, `selectedObjectId` and
// each record's `selected` flag in step with the set, so single-selection
// readers never see a stale primary.
[[nodiscard]] int levelMapSelectionCount(const LevelMapDocument& document);
[[nodiscard]] bool levelMapSelectionContains(const LevelMapDocument& document, LevelMapSelectionKind kind, int objectId);
[[nodiscard]] LevelMapSelectionRef levelMapPrimarySelection(const LevelMapDocument& document);
QStringList levelMapSelectionSetLines(const LevelMapDocument& document);
// Adds a member and makes it primary. Re-adding a member that is already in the
// set only promotes it. Fails when the object does not exist.
bool addLevelMapSelection(LevelMapDocument* document, LevelMapSelectionKind kind, int objectId, QString* error = nullptr);
bool removeLevelMapSelection(LevelMapDocument* document, LevelMapSelectionKind kind, int objectId, QString* error = nullptr);
bool toggleLevelMapSelection(LevelMapDocument* document, LevelMapSelectionKind kind, int objectId, QString* error = nullptr);
void clearLevelMapSelection(LevelMapDocument* document);
// Replaces the set wholesale. Duplicates collapse to their last occurrence, so
// the caller's final entry becomes the primary member.
bool setLevelMapSelection(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& selection, QString* error = nullptr);
bool setLevelMapEntityProperty(LevelMapDocument* document, int entityId, const QString& key, const QString& value, QString* error = nullptr);
bool removeLevelMapEntityProperty(LevelMapDocument* document, int entityId, const QString& key, QString* error = nullptr);
bool setLevelMapSectorProperty(LevelMapDocument* document, int sectorId, const QString& key, const QString& value, QString* error = nullptr);
bool setLevelMapSidedefProperty(LevelMapDocument* document, int sidedefId, const QString& key, const QString& value, QString* error = nullptr);
bool moveLevelMapObject(LevelMapDocument* document, const QString& objectKind, int objectId, double dx, double dy, double dz, QString* error = nullptr);
// Moves every movable member of the selection set and records the whole thing as
// a single undo command, so one undo puts all of them back. Members that cannot
// move (sectors) are skipped; the call fails only when nothing at all moved.
// The selection set survives the move unchanged.
bool moveLevelMapSelection(LevelMapDocument* document, double dx, double dy, double dz, QString* error = nullptr);
// Snaps the delta to `gridSize` first (a non-positive grid disables snapping)
// and then performs the same compound move. A delta that snaps to zero is a
// no-op and fails rather than pushing an empty command.
bool moveLevelMapSelectionSnapped(LevelMapDocument* document, double dx, double dy, double dz, double gridSize, QString* error = nullptr);
bool undoLevelMapEdit(LevelMapDocument* document, QString* error = nullptr);
bool redoLevelMapEdit(LevelMapDocument* document, QString* error = nullptr);
// Records the current undo depth as the saved state so that later undo/redo can
// report "saved" versus "modified" instead of guessing.
void markLevelMapSaved(LevelMapDocument* document);
LevelMapSaveReport saveLevelMapAs(const LevelMapDocument& document, const QString& outputPath, bool dryRun = false, bool overwriteExisting = false);
QString levelMapSaveReportText(const LevelMapSaveReport& report);
CompilerCommandRequest compilerRequestForLevelMap(const LevelMapDocument& document, const QString& profileId, const QString& outputPath = QString());

} // namespace vibestudio

// Registered so that a selection set can cross a queued signal connection, not
// only a direct one.
Q_DECLARE_METATYPE(vibestudio::LevelMapSelectionRef)
