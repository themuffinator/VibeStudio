#pragma once

#include "core/compiler_profiles.h"
#include "core/level_scene_types.h"

#include "core/studio_query.h"

#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace vibestudio {

struct LevelDoomNodeReport;
struct LevelUdmfDocument;

inline constexpr qint64 kLevelMapMaxDocumentBytes = 512LL * 1024LL * 1024LL;

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

// Source lines `first` to `last`, both inclusive and 1-based.
struct LevelMapLineRange {
	int first = 0;
	int last = 0;
};

inline bool operator==(const LevelMapLineRange& left, const LevelMapLineRange& right)
{
	return left.first == right.first && left.last == right.last;
}

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
	// True once the face's shift, rotation, or scale was edited, so save-back
	// rewrites those numbers on its line instead of keeping them as written.
	bool textureParametersDirty = false;
	// True once the face's texture was replaced, so save-back rewrites the name
	// on its line. Per face: a face left alone keeps its line as written.
	bool textureDirty = false;
	// True when the face's line carries Quake II style contents, surface and
	// value numbers after its texture placement.
	bool flagsWritten = false;
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
	// A brush copied in the editor (startLine 0) keeps the text of the brush it
	// was copied from, braces included, and save-back writes that text with the
	// copy's own points. `face.line - sourceFirstLine` indexes a face's line.
	QStringList sourceLines;
	int sourceFirstLine = 0;
	// True once any face's texture was replaced; the faces that changed carry
	// their own `textureDirty`, and only their names are rewritten.
	bool texturesDirty = false;
	// True once a rotation turned the Valve 220 texture axes, so save-back
	// rewrites them; a move leaves them exactly as written.
	bool textureAxesDirty = false;
	// True once any face's shift, rotation, or scale was edited.
	bool textureParametersDirty = false;
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
	// Format extension fields after dimensions (and patchDef3 subdivisions).
	QVector<double> headerTail {0.0, 0.0, 0.0};
	// Authoring a control grid rewrites its entire owned block, allowing
	// multiline source groups and topology changes without stale dimensions.
	bool definitionDirty = false;
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
	// As for LevelMapBrush: the copied patch's text, indexed by
	// `controlRowLines[column] - sourceFirstLine`.
	QStringList sourceLines;
	int sourceFirstLine = 0;
	// The line holding the patch's shader name, where on it the name starts
	// (its opening quote when quoted), and whether it was replaced.
	int textureLine = 0;
	int textureColumn = -1;
	bool textureDirty = false;
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
	double offsetX = 0;
	double offsetY = 0;
	QString upperTexture;
	QString lowerTexture;
	QString middleTexture;
	bool selected = false;
};

struct LevelMapDoomSector {
	int id = -1;
	double floorHeight = 0;
	double ceilingHeight = 0;
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
	// This step's own move when it differs from the command's, as when
	// snapping moves each object by its own amount; invalid means the
	// command's delta.
	LevelMapVec3 delta;
};

// One texture a replace-texture command changed: a brush face, a patch, a
// Doom sidedef's upper, lower, or middle texture, or a sector's floor or
// ceiling.
struct LevelMapTextureChange {
	// "face", "patch", "sidedef", or "sector".
	QString kind;
	int objectId = -1;
	// Face index; sidedef 0 upper, 1 lower, 2 middle; sector 0 floor, 1 ceiling.
	int part = 0;
	QString oldName;
	QString newName;
};

// How many times a texture is used, for choosing one to replace.
struct LevelMapTextureUse {
	QString name;
	int count = 0;
};

// Read-only values for one document snapshot. Sharing a summary within a UI
// refresh avoids repeating whole-map material scans; it is not a document cache.
struct LevelMapInspectionSummary {
	LevelMapStatistics statistics;
	QStringList textureNames;
};

// For a `doom-topology` command: one kind of Doom record as the edit changed
// it, against the records before it: the ones it changed (their places, and
// their content before and after), the ones it appended, and the ones it then
// took out (their places among all of those, and their content there), so
// undo and redo rebuild either side from the other without keeping copies of
// the whole map. References in the records held here use the ids of that
// middle stage, before anything was taken out.
template <typename Record>
struct LevelMapRecordDelta {
	int originalSize = 0;
	QVector<int> changedIndexes;
	QVector<Record> changedBefore;
	QVector<Record> changedAfter;
	QVector<Record> appended;
	QVector<int> removedIndexes;
	QVector<Record> removed;
};

// One entity of a `set-properties` command: the key as it was on the entity
// before, and as the command left it.
struct LevelMapPropertyStep {
	int entityId = -1;
	bool keyExisted = true;
	QString oldValue;
	QString newValue;
	// True when the command took the key off; undo puts it back on its line.
	bool removed = false;
	int propertyLine = 0;
	// The Doom thing the entity mirrors, before and after.
	bool hasThingSnapshot = false;
	LevelMapDoomThing oldThing;
	LevelMapDoomThing newThing;
};

struct LevelMapUndoCommand {
	QByteArray udmfBefore, udmfAfter;
	// Native UDMF edits distinguish ordinary things from node-builder inputs.
	bool udmfNodeInputsChanged = false;
	bool hasSceneSnapshot = false;
	LevelSceneState sceneBefore;
	LevelSceneState sceneAfter;
	// Structural edits identify surviving/created objects before ids are reused.
	// Remap is old -> new (empty means removed); origins is new -> source.
	QHash<QString, QString> sceneObjectRemap;
	QHash<QString, QString> sceneObjectOrigins;
	QStringList sceneAddedObjects;
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
	// Objects an `add-entity` or `delete-objects` command adds or removes, with
	// the position each held in its vector, so undo puts it back in place.
	QVector<LevelMapEntity> entitySnapshots;
	QVector<int> entityIndexes;
	QVector<LevelMapBrush> brushSnapshots;
	QVector<int> brushIndexes;
	QVector<LevelMapPatch> patchSnapshots;
	QVector<int> patchIndexes;
	QVector<LevelMapDoomThing> thingSnapshots;
	QVector<int> thingIndexes;
	// Validation issues about deleted objects, withdrawn with them.
	QVector<LevelMapIssue> issueSnapshots;
	QVector<int> issueIndexes;
	// Source lines a deletion drops from the saved text.
	QVector<LevelMapLineRange> deletedLineRanges;
	// Textures a `replace-texture` command changed.
	QVector<LevelMapTextureChange> textureChanges;
	// Entities a `set-properties` command changed, one step each, all for `key`.
	QVector<LevelMapPropertyStep> propertySteps;
	// For `transform-objects`: the objects after the change; the *Snapshots
	// vectors above hold them before it.
	QVector<LevelMapEntity> entityResults;
	QVector<LevelMapBrush> brushResults;
	QVector<LevelMapPatch> patchResults;
	QVector<LevelMapDoomThing> thingResults;
	// For `replace-objects`: where the brushResults go once the brushes they
	// replace are out.
	QVector<int> brushResultIndexes;
	// For `doom-geometry`: Doom records before and after a topology edit. A
	// result whose id has no snapshot was appended, and goes again on undo;
	// the lumps address records by index, so ids stay equal to positions.
	QVector<LevelMapDoomVertex> vertexSnapshots;
	QVector<LevelMapDoomVertex> vertexResults;
	QVector<LevelMapDoomLinedef> linedefSnapshots;
	QVector<LevelMapDoomLinedef> linedefResults;
	QVector<LevelMapDoomSidedef> sidedefSnapshots;
	QVector<LevelMapDoomSidedef> sidedefResults;
	// For `doom-topology`: what an edit that adds, removes, or renumbers Doom
	// records did to each kind of record, as deltas; the things and their
	// entities whole when it deleted things (`hasThingSnapshot`); and the
	// issues and the selection before and after, whose ids renumbering moves.
	LevelMapRecordDelta<LevelMapDoomVertex> vertexDelta;
	LevelMapRecordDelta<LevelMapDoomLinedef> linedefDelta;
	LevelMapRecordDelta<LevelMapDoomSidedef> sidedefDelta;
	LevelMapRecordDelta<LevelMapDoomSector> sectorDelta;
	QVector<LevelMapIssue> issueResults;
	QVector<LevelMapSelectionRef> selectionSnapshot;
	QVector<LevelMapSelectionRef> selectionResult;
	// For `batch`: the commands it is made of, in the order they were done;
	// undo replays them backwards (collapseLevelMapUndoSteps).
	std::vector<LevelMapUndoCommand> children;
	// Edits this one caused, done after it and undone before it, such as the
	// copies of a linked group taking on an edit to one of them
	// (foldLevelMapFollowUpEdits). The command keeps its own kind and details.
	std::vector<LevelMapUndoCommand> followers;
};

// Owned source archive records keep saving/recovery independent of later disk
// changes and preserve other maps, resources, duplicate names, and ordering.
struct LevelMapWadSourceLump {
	QString name;
	QByteArray bytes;
};

struct LevelMapDocument {
	LevelSceneState scene;
	// Transient creation destination. Missing/deleted nodes fall back to Default.
	QString activeSceneNode;
	QString sourcePath;
	QByteArray sourceContentHash;
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
	QVector<LevelMapWadSourceLump> doomArchiveLumps;
	QStringList doomLumpOrder;
	std::shared_ptr<const LevelDoomNodeReport> doomNodeReport;
	std::shared_ptr<const LevelUdmfDocument> doomUdmf;
	// Magic of the source WAD ("IWAD" or "PWAD"), preserved on save.
	QString doomWadMagic = QStringLiteral("PWAD");
	// Set while applied edits leave the precomputed node, blockmap, and reject
	// lumps out of date: `doomGeometryEdits` counts those edits, net of undo,
	// so undoing back to the loaded geometry clears it again.
	bool doomGeometryChanged = false;
	int doomGeometryEdits = 0;
	// Bumped whenever an edit that renumbers Doom geometry is applied or
	// undone, so a view holding vertex, linedef, or sector ids knows to let
	// go of them.
	int doomTopologyRevision = 0;
	// Bumped by every edit, undo, and redo, so a view keyed on it sees each
	// change even once the undo stack stops growing at its limit.
	quint64 revision = 0;
	// Source lines of deleted entities, brushes, and patches. Save-back drops
	// them; undoing the deletion takes its ranges out again.
	QVector<LevelMapLineRange> deletedLineRanges;
};

// An arrow from an entity's target-style key to an entity whose targetname
// matches: the way Quake-family maps chain triggers, doors, lights, and paths.
struct LevelMapTargetLink {
	int sourceEntityId = -1;
	int targetEntityId = -1;
	// target, killtarget, pathtarget, combattarget, or deathtarget.
	QString key;
	QString name;
	// Where each end is drawn; see levelMapEntityAnchor().
	LevelMapVec3 from;
	LevelMapVec3 to;
};

// A Doom linedef whose tag names a sector: what Doom Builder draws as an
// association arrow, from the line that acts to the sector it acts on.
struct LevelMapTagLink {
	int linedefId = -1;
	int sectorId = -1;
	int tag = 0;
};

class MapBrushGeometryCache;
enum class LevelMapLoadPhase { Reading, Indexing, Tokenizing, Parsing, Solving, Validating, Hashing, Complete };

struct LevelMapLoadRequest {
	QString path;
	QString mapName;
	QString engineHint;
	// Callbacks execute on the loading thread. Progress describes the current
	// phase, not an estimated total duration; a zero total means indeterminate.
	std::function<bool()> isCancelled {};
	std::function<void(LevelMapLoadPhase, qint64 completed, qint64 total)> progress {};
	// Optional caller-owned cache populated while solving loaded brush bounds.
	// Use a separate value for each worker; partial cache data is disposable.
	MapBrushGeometryCache* brushGeometryCache = nullptr;
};

struct LevelMapSaveReport {
	QString sourcePath;
	QString outputPath;
	QString backupPath;
	QByteArray contentHash;
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
// Uses the same parsers without scratch files. A failed load preserves the
// caller's document. `request.path` supplies format/source identity only.
bool loadLevelMapBytes(const LevelMapLoadRequest& request, const QByteArray& bytes, LevelMapDocument* document, QString* error = nullptr);
struct LevelMapSerialized {
	QByteArray bytes;
	QStringList errors;
	QStringList warnings;
	QStringList staleLumps;
	[[nodiscard]] bool succeeded() const { return errors.isEmpty() && !bytes.isEmpty(); }
};
LevelMapSerialized serializeLevelMap(const LevelMapDocument& document);
LevelMapStatistics levelMapStatistics(const LevelMapDocument& document);
LevelMapInspectionSummary levelMapInspectionSummary(const LevelMapDocument& document);
// Sorted, nonempty material names; ignore Doom's "-" marker, trim whitespace
// and merge case-insensitive duplicates while retaining the first spelling.
QStringList levelMapTextureNames(const LevelMapDocument& document);
// The map markers of a Doom WAD in directory order (MAP01, E1M1, or named
// UDMF maps), so a caller can offer them before opening one.
QStringList levelMapNamesInWad(const QString& path, QString* error = nullptr);
// Lists the already loaded WAD without reading or duplicating its payload again.
QStringList levelMapNamesInWadDocument(const LevelMapDocument& document);
QStringList levelMapStatisticsLines(const LevelMapDocument& document);
// Format statistics already computed for this document, without rescanning it.
QStringList levelMapStatisticsLines(const LevelMapDocument& document, const LevelMapStatistics& statistics);
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
// Sets `key` on several entities (Doom things through their mirrors) as one
// undo step: to `values.first()` on all of them, or each to its own value when
// `values` has one per entity. Entities that already hold their value are
// left out. Every entity is checked first, so the edit is all or nothing, and
// the selection stays as it is.
bool setLevelMapEntitiesProperty(LevelMapDocument* document, const QVector<int>& entityIds, const QString& key, const QStringList& values,
	QString* error = nullptr);
// Takes `key` off each of the entities that has it, as one undo step; fails
// when none of them has it.
bool removeLevelMapEntitiesProperty(LevelMapDocument* document, const QVector<int>& entityIds, const QString& key, QString* error = nullptr);
bool setLevelMapSectorProperty(LevelMapDocument* document, int sectorId, const QString& key, const QString& value, QString* error = nullptr);
bool setLevelMapSidedefProperty(LevelMapDocument* document, int sidedefId, const QString& key, const QString& value, QString* error = nullptr);
// Links the selected entities the way Radiant's Connect Entities does: each
// selected entity but the primary gets a `target` naming the primary, whose
// `targetname` is kept, or else made up as `t<N>`, the lowest number no entity
// uses. A selected brush or patch stands for its entity; worldspawn cannot
// take part. One undo command; `name` reports the targetname linked to.
bool connectLevelMapEntities(LevelMapDocument* document, QString* name = nullptr, QString* error = nullptr);
// The entities the selection's entities target, and the ones that target them.
QVector<LevelMapSelectionRef> levelMapLinkedEntities(const LevelMapDocument& document, bool targets);
// Every Doom-format linedef with a special and a tag, to each sector carrying
// that tag. Hexen lines are left out: the first argument the loader mirrors
// into their tag is a sector tag for some specials and a script or thing
// number for others.
QVector<LevelMapTagLink> levelMapTagLinks(const LevelMapDocument& document);
// Sets one texture field of a brush face in a Quake-family .map as one undo
// command: `texture`, `shiftx`, `shifty`, `rotation`, `scalex`, or `scaley`
// (`faceIndex` counts from 0), rewritten on the face's own line. A Valve 220
// face shifts through its axis offsets, and turning it turns its axes about
// the face, as TrenchBroom does; a face written as a texture matrix
// (brushDef) takes a new texture but not new numbers. A face sharing its
// line with another is refused. A value the face already has records nothing.
bool setLevelMapBrushFaceProperty(LevelMapDocument* document, int brushId, int faceIndex, const QString& field, const QString& value,
	QString* error = nullptr);
// Sets one field of a Doom or Hexen linedef as one undo command: `flags` (the
// whole word), `special`, and `tag` on a Doom map or `arg0` to `arg4` on a
// Hexen one; `front.<field>` and `back.<field>` set a field of that side, as
// setLevelMapLinedefSideProperty() does. A value the line already has changes
// nothing and records no step.
bool setLevelMapLinedefProperty(LevelMapDocument* document, int linedefId, const QString& key, const QString& value, QString* error = nullptr);
// Sets one field of a linedef's front or back side: `sector`, `offsetx`,
// `offsety`, `upper`, `middle`, or `lower`, a texture of at most eight
// characters, or `-` for none. A sidedef other linedefs share is copied
// first, so only this line changes, as Doom Builder keeps each line's sides
// its own. One undo command; only a new sector leaves the node lumps stale.
bool setLevelMapLinedefSideProperty(LevelMapDocument* document, int linedefId, bool front, const QString& key, const QString& value,
	QString* error = nullptr);
// Texture lock preserves UVs at corresponding brush points. Disabled retains
// the written projection parameters. Patches always carry their control UVs;
// Doom wall/flat offsets are outside this Quake-family policy.
struct LevelMapTextureLockOptions {
	bool enabled = true;
	// A required classic-to-Valve conversion covers the whole map in the same
	// undo step, preserving unselected geometry and appearance.
	bool allowValve220 = false;
};
bool moveLevelMapObject(LevelMapDocument* document, const QString& objectKind, int objectId, double dx, double dy, double dz, QString* error = nullptr);
bool moveLevelMapObject(LevelMapDocument* document, const QString& objectKind, int objectId, double dx, double dy, double dz,
	const LevelMapTextureLockOptions& textures, QString* error = nullptr);
// Moves every movable member of the selection set and records the whole thing as
// a single undo command, so one undo puts all of them back. Members that cannot
// move (binary Doom sectors) are skipped; the call fails only when nothing moved.
// UDMF lines/sectors expand to unique XY vertices. Things also support height.
// UDMF changes only native value spans and retains exact source for undo.
// The selection set survives the move unchanged.
bool moveLevelMapSelection(LevelMapDocument* document, double dx, double dy, double dz, QString* error = nullptr);
// Explicit policy overloads share affine projection locking with rotation,
// mirroring and resizing. The older move overload retains source parameters.
bool moveLevelMapSelection(LevelMapDocument* document, double dx, double dy, double dz,
	const LevelMapTextureLockOptions& textures, QString* error = nullptr);
// Snaps the delta to `gridSize` first (a non-positive grid disables snapping)
// and then performs the same compound move. A delta that snaps to zero is a
// no-op and fails rather than pushing an empty command.
bool moveLevelMapSelectionSnapped(LevelMapDocument* document, double dx, double dy, double dz, double gridSize, QString* error = nullptr);
bool moveLevelMapSelectionSnapped(LevelMapDocument* document, double dx, double dy, double dz, double gridSize,
	const LevelMapTextureLockOptions& textures, QString* error = nullptr);
// Adds a point entity to a Quake-family .map as one undo command and selects
// it. Save-back writes it after the last entity. `properties` adds keys beyond
// the `classname` and `origin` the arguments set.
bool addLevelMapEntity(LevelMapDocument* document, const QString& className, const LevelMapVec3& origin,
	const QVector<LevelMapProperty>& properties = {}, int* entityId = nullptr, QString* error = nullptr);
// Deletes entities, brushes, and patches from a Quake-family .map, or
// things, vertices, linedefs, and sectors from a Doom or Hexen map, as one
// undo command. An entity takes its brushes and patches with it, and a brush
// entity left with none of either goes too. The worldspawn entity holds the
// world and is never deleted. On a Doom map, as in Doom Builder: a sector
// takes its sides with it, a line left with one side becoming a wall facing
// the sector that stays and a line left with none going; a vertex between
// exactly two linedefs dissolves, joining them into one; any other vertex
// goes with its linedefs; and vertices, sides, and sectors nothing uses any
// more go too, the rest renumbered to keep the lumps packed. An object whose
// first or last source line it shares with other map text is refused, since
// dropping those lines would damage the file.
bool deleteLevelMapObjects(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& objects, QString* error = nullptr);
bool deleteLevelMapSelection(LevelMapDocument* document, QString* error = nullptr);
// True when the selection holds something deleteLevelMapSelection() may remove.
bool levelMapSelectionIsDeletable(const LevelMapDocument& document);
// True when the selection holds what Duplicate copies: as deletable, except
// that a Doom map's geometry is drawn, not copied, so only things qualify.
bool levelMapSelectionIsDuplicable(const LevelMapDocument& document);
// True when the object `ref` names is in the map.
bool levelMapObjectExists(const LevelMapDocument& document, const LevelMapSelectionRef& ref);
// What Select All picks: a Doom map's things, or a Quake-family map's point
// entities, brushes, and patches, a brush entity through its brushes and
// patches as Radiant selects primitives (the entity as well would move and
// delete it twice over). `invert` leaves out what is selected now, a selected
// entity's brushes and patches with it. `shown`, when given, leaves out what
// it returns false for, such as objects the view hides.
QVector<LevelMapSelectionRef> levelMapSelectAllObjects(const LevelMapDocument& document, bool invert,
	const std::function<bool(const LevelMapSelectionRef&)>& shown = {});
// A DoomEd number with the name of what it places, for choosing a thing type,
// and the group a palette files it under.
struct LevelMapDoomThingType {
	int type = 0;
	QString name;
	QString category;
};

// Common thing types for the map's format: the player and deathmatch starts
// and teleport destination every idTech1 game shares, plus Doom's usual
// monsters, weapons, and pickups for a Doom-format map. Numbers follow the
// Doom Wiki thing tables (https://doomwiki.org/wiki/Thing_types).
QVector<LevelMapDoomThingType> levelMapDoomThingTypes(LevelMapDoomFormat format);
// Adds a thing to a Doom or Hexen map as one undo command and selects it. It
// appears on every skill (and, for Hexen, every class and game mode).
bool addLevelMapDoomThing(LevelMapDocument* document, int type, double x, double y, int angle = 0, int* thingId = nullptr,
	QString* error = nullptr);
// Adds an axis-aligned box brush from `mins` to `maxs` to worldspawn, with
// `texture` on every face, as one undo command, and selects it. The faces are
// written in the map's own style (classic, Valve 220, brushDef, or brushDef3,
// with or without the Quake II and III flags), read from its existing
// brushes, with each face's points wound so its plane faces out of the box.
bool addLevelMapBoxBrush(LevelMapDocument* document, const LevelMapVec3& mins, const LevelMapVec3& maxs, const QString& texture,
	int* brushId = nullptr, QString* error = nullptr);
// Copies entities, brushes, and patches of a Quake-family .map, moved by
// (dx, dy, dz), as one undo command, and selects the copies. An entity's copy
// brings copies of its brushes and patches; a copied brush or patch joins the
// entity that holds the original. worldspawn itself is never copied. On a
// Doom map, things are copied instead. UDMF copies preserve native thing blocks,
// extension properties and fractional coordinates, including height.
bool duplicateLevelMapObjects(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& objects,
	double dx, double dy, double dz, QString* error = nullptr);
bool duplicateLevelMapObjects(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& objects,
	double dx, double dy, double dz, const LevelMapTextureLockOptions& textures, QString* error = nullptr);
bool duplicateLevelMapSelection(LevelMapDocument* document, double dx, double dy, double dz, QString* error = nullptr);
bool duplicateLevelMapSelection(LevelMapDocument* document, double dx, double dy, double dz,
	const LevelMapTextureLockOptions& textures, QString* error = nullptr);
inline constexpr int kLevelMapMaxArrayCopies = 256;
inline constexpr int kLevelMapMaxArrayRecords = 32768;
inline constexpr int kLevelMapMaxArrayComponents = 262144;
// Builds copies at offset, 2*offset, ... copies*offset from the original
// selection, without accumulating rounding error. Owners and source scene
// membership follow ordinary duplication. Selects every inserted copy and
// records one undo command; a failed copy leaves the complete source intact.
// Bounds native records (including Doom entity mirrors) and brush faces/patch
// control points before expansion. UDMF supports thing arrays only.
bool arrayLevelMapSelection(LevelMapDocument* document, const LevelMapVec3& offset, int copies,
	const LevelMapTextureLockOptions& textures, QString* error = nullptr);
// Where an entity is drawn: its origin, or the centre of its brushes and
// patches for a brush entity. False when it has neither.
bool levelMapEntityAnchor(const LevelMapDocument& document, int entityId, LevelMapVec3* anchor = nullptr);
// Every target link in the map, in entity order and then target id. Entities
// with no anchor are left out, and a Doom map has none: its linedef-to-sector
// tags are a different relation.
QVector<LevelMapTargetLink> levelMapTargetLinks(const LevelMapDocument& document);
// Shared by connection rendering and reusable-prefab target remapping.
const QStringList& levelMapTargetPropertyKeys();
// The selection as .map text, the way Quake-family editors put it on the
// clipboard: each selected entity as a block with its brushes and patches, and
// other selected brushes and patches as bare blocks, which paste into
// worldspawn. Each primitive keeps its source format. Empty for Doom maps or
// an empty selection.
// A primitive whose braces share lines with other text cannot be copied as
// text; `error` then says which, and the result is empty.
QString levelMapSelectionText(const LevelMapDocument& document, QString* error = nullptr);
// Adds the objects in .map text, as copied by levelMapSelectionText() or by
// TrenchBroom or Radiant: entity blocks become entities, and bare brush and
// patch blocks, or those of a worldspawn block, join the open map's
// worldspawn. The pasted objects keep their coordinates and are selected. One
// undo command.
bool pasteLevelMapText(LevelMapDocument* document, const QString& text, QString* error = nullptr);
// Offset the whole pasted assembly, retaining ownership and relative positions.
// Placement and texture validation finish before the single insertion command.
inline constexpr qsizetype kLevelMapClipboardMaxCharacters = 4 * 1024 * 1024;
bool pasteLevelMapText(LevelMapDocument* document, const QString& text, const LevelMapVec3& offset,
	const LevelMapTextureLockOptions& textures, QString* error = nullptr);
// Every texture the map uses, with how often: brush faces and patches, or
// Doom wall textures and flats. `selectionOnly` counts the selected objects'
// textures: selected brushes and patches and those of selected entities, or
// selected linedefs' sidedefs and selected sectors. Sorted by name.
QVector<LevelMapTextureUse> levelMapTextureUsage(const LevelMapDocument& document, bool selectionOnly = false);
// Every object that uses `texture` (compared without case): brushes with a
// face that does and patches, or on a Doom map linedefs whose sides do and
// sectors whose floor or ceiling does. In map order.
QVector<LevelMapSelectionRef> levelMapObjectsUsingTexture(const LevelMapDocument& document, const QString& texture);

// A map object's properties as a query reads them, by lower-case key; a key
// can hold several values, such as a brush's textures. Entities answer to
// their own keys and "class"; brushes and patches to "texture" and "entity";
// Every object also answers to "kind" (entity, brush, patch, thing, linedef,
// sector, vertex) and "selector" ("entity:3"); a term written the way map find
// prints an object, entity:3, names that object.
// Doom things to "type", "name" (what the type places, when known), "angle",
// "flags", "x", and "y", and in Hexen "tid", "special", and "action";
// linedefs to "special", "action", "tag", "flags", "front", "back", and
// "texture" (every side's); sectors to "floor", "ceiling", "floortex",
// "ceiltex", "texture" (both flats), "light", "special", "effect", and "tag";
// vertices to "x" and "y".
using LevelMapObjectProperties = StudioQueryProperties;
// Every object's properties, keyed by its selector ("entity:3", "linedef:12").
// Cancellation discards the incomplete index; callers can inspect their token
// to distinguish cancellation from an empty document.
QHash<QString, LevelMapObjectProperties> levelMapQueryProperties(const LevelMapDocument& document,
	const std::function<bool()>& isCancelled = {});

// Map object queries are the studio's shared queries (core/studio_query.h).
using LevelMapQueryTerm = StudioQueryTerm;
using LevelMapQuery = StudioQuery;
LevelMapQuery parseLevelMapQuery(const QString& text);
bool levelMapQueryMatches(const LevelMapQuery& query, const LevelMapObjectProperties& properties, const QString& description);
// The selectors of the objects `query` matches, in map order. A bare word
// matches the selector or any property value.
QStringList levelMapObjectsMatchingQuery(const LevelMapDocument& document, const QString& query);
// Replaces every use of `from` (compared without case) with `to`, across the
// map or only the selected objects, as one undo command. `replaced` reports
// how many uses changed.
bool replaceLevelMapTexture(LevelMapDocument* document, const QString& from, const QString& to, bool selectionOnly,
	int* replaced = nullptr, QString* error = nullptr);
// Puts `texture` on every face of the selected brushes and on the selected
// patches, a selected entity's included, the way a texture browser applies a
// texture, as one undo command. Quake-family maps only; a Doom map's walls
// and flats take Replace Texture. `applied` reports how many faces and patches
// changed.
// allowUnchanged supports asset restaging without inventing a map undo command.
bool applyLevelMapTexture(LevelMapDocument* document, const QString& texture, int* applied = nullptr, QString* error = nullptr, bool allowUnchanged = false);
// Turns the selection by `quarterTurns` quarter turns about `axis` (0 x, 1 y,
// 2 z) through the centre of the selection's bounds; positive turns follow
// the right-hand rule. This delegates to numeric rotation with texture lock
// enabled and conversion disabled: classic, Valve and primitive UVs stay on
// the faces. Entity angles use the same model-orientation rules. Doom objects
// rotate in XY only. UDMF retains fractional positions and whole-degree headings.
// One undo command; unrepresentable mappings fail atomically.
bool rotateLevelMapSelection(LevelMapDocument* document, int axis, int quarterTurns, QString* error = nullptr);
struct LevelMapRotationRequest {
	int axis = 2;
	double degrees = 90;
	// Invalid means the centre of the selection's bounds.
	LevelMapVec3 pivot;
	bool textureLock = true;
	// If conversion is needed, all classic faces in the document convert so
	// compilers see one dialect. Unselected geometry and appearance stay fixed.
	bool allowValve220 = false;
};
// Arbitrary right-handed rotation, validated and committed as one undo step.
// An unrepresentable classic texture lock fails atomically unless conversion
// is explicitly enabled. Turning texture lock off retains source parameters.
bool rotateLevelMapSelection(LevelMapDocument* document, const LevelMapRotationRequest& request, QString* error = nullptr);
// Mirrors the selection across the plane through the centre of its bounds
// that is perpendicular to `axis`. Faces keep facing outward: each point-
// defined face swaps two of its points and each patch reverses its columns;
// brushDef3 planes and all brush texture projections mirror as well. Mirroring x or
// y mirrors entity angles and Doom things' headings; Doom geometry and things
// do not flip along z. Doom reflections require complete incident vertex sets,
// reverse linedef endpoints and retain native sides/offsets. One undo command.
// The default locks Quake brush UVs without dialect conversion.
bool flipLevelMapSelection(LevelMapDocument* document, int axis, QString* error = nullptr);
bool flipLevelMapSelection(LevelMapDocument* document, int axis, const LevelMapTextureLockOptions& textures, QString* error = nullptr);
// The box around what a transform of the selection moves: selected entities'
// origins, brushes and patches (a selected entity's included), and things.
// False when nothing selected has a position.
bool levelMapSelectionBounds(const LevelMapDocument& document, LevelMapVec3* mins, LevelMapVec3* maxs);
// The same box for any set of objects, such as a viewport's own selection.
bool levelMapObjectsBounds(const LevelMapDocument& document, const QVector<LevelMapSelectionRef>& objects, LevelMapVec3* mins,
	LevelMapVec3* maxs);
// Resizes the selection so that its bounds (levelMapSelectionBounds()) become
// `mins` to `maxs`: brush points and planes, patch control points, entity
// origins, and thing positions map from the old box to the new one, axis by
// axis. The default retains brush texture parameters; plane-dependent bases
// may change on oblique faces. Patches keep their own texture coordinates.
// Along an axis the selection has no size, it only
// moves. The new size must stay above zero along every axis the selection
// spans. UDMF boundary vertices resize in XY; heights use the property editor.
// One undo command.
bool resizeLevelMapSelection(LevelMapDocument* document, const LevelMapVec3& mins, const LevelMapVec3& maxs, QString* error = nullptr);
// Locked resizing stretches the texture with the brush. The overload above
// keeps its existing default of retaining source texture parameters.
bool resizeLevelMapSelection(LevelMapDocument* document, const LevelMapVec3& mins, const LevelMapVec3& maxs,
	const LevelMapTextureLockOptions& textures, QString* error = nullptr);
// Which part of a brush a clip keeps: the part behind the plane (the side
// its normal points away from), the part in front, or both, as two brushes.
enum class LevelMapClipKeep {
	Back,
	Front,
	Both,
};
// Cuts the selected brushes, and the brushes of selected entities, with the
// plane through `a`, `b`, and `c`, whose normal (b - a) x (c - a) points to
// its front. Each brush the plane passes through is replaced by the part
// kept, or by both parts: its own face lines, less those the cut leaves
// without an edge, and a new face on the plane, written the way the brush's
// faces are, with the texture and flags of its most used face. Brushes the
// plane misses stay as they are, and so do brushes whose faces share lines
// with other text, each with its reason in `skipped`. The pieces are
// selected. One undo command; `clipped` reports how many brushes were cut.
bool clipLevelMapSelection(LevelMapDocument* document, const LevelMapVec3& a, const LevelMapVec3& b, const LevelMapVec3& c,
	LevelMapClipKeep keep, int* clipped = nullptr, QString* error = nullptr, QStringList* skipped = nullptr);
// Turns each selected brush, a selected entity's included, into walls
// `thickness` units thick, one for each face, so a block becomes a room: each
// wall is the brush with one more face, the face it grew from pulled in by
// the thickness and turned to face the inside, textured like that face. The
// walls meet at the edges and corners, as Radiant's Make Hollow leaves them. A
// brush too thin for two walls along one of its faces is refused. The walls
// are selected. One undo command; `hollowed` reports how many brushes changed,
// and `skipped` why any other selected brush was left as it was.
bool hollowLevelMapSelection(LevelMapDocument* document, double thickness, int* hollowed = nullptr, QString* error = nullptr,
	QStringList* skipped = nullptr);
// Carves the selected brushes out of every other brush they overlap, the way
// CSG Subtract does in Radiant and TrenchBroom: a brush they cut gives way to
// the parts of it outside them, split along their faces, and the faces the
// carve opens take the carving faces' textures. A brush inside them goes. The
// carving brushes stay, still selected, to be deleted or moved on; brushes
// written on shared lines are left whole, each with its reason in `skipped`,
// and so are the `spared` brushes, which the editor passes for hidden ones. A
// brush entity whose every brush the carve takes goes with them. One undo
// command; `carved` reports how many brushes were cut.
bool carveLevelMapSelection(LevelMapDocument* document, int* carved = nullptr, QString* error = nullptr, QStringList* skipped = nullptr,
	const QVector<int>& spared = {});
// CSG Intersect, as TrenchBroom has it: the selected brushes give way to the
// one brush where they all overlap, in the first brush's entity and with each
// face textured like the face it came from. Fails, changing nothing, when they
// do not overlap. One undo command.
bool intersectLevelMapSelection(LevelMapDocument* document, QString* error = nullptr);

// Brush entities, the way Radiant's entity menu and Hammer's Tie to Entity
// make them: the selected brushes and patches, with those of selected brush
// entities, leave the entities they belong to (an entity left with none goes
// too) and become the brushes of a new `className` entity with `properties`.
// The new entity is selected; one undo command.
bool tieLevelMapSelectionToEntity(LevelMapDocument* document, const QString& className, const QVector<LevelMapProperty>& properties = {},
	int* entityId = nullptr, QString* error = nullptr);
// Hammer's Move to World and Radiant's Ungroup: the selected brush entities,
// and the entities of selected brushes and patches, give their brushes and
// patches back to worldspawn and go; their keys go with them. The brushes
// stay selected; one undo command. `moved` counts brushes and patches.
bool moveLevelMapSelectionToWorld(LevelMapDocument* document, int* moved = nullptr, QString* error = nullptr);

// Radiant's region selections, using the selection's bounds as the region:
// objects wholly inside it, objects that touch it, and, looking down the
// `axis` a 2D view hides (2 for Top), objects whose footprint lies wholly
// inside the region's (complete tall) or overlaps it (partial tall). Brushes,
// patches, point entities and Doom things are found; what is selected now is
// not. The caller selects the result.
enum class LevelMapRegionSelection {
	Inside,
	Touching,
	CompleteTall,
	PartialTall,
};
QVector<LevelMapSelectionRef> levelMapRegionSelection(const LevelMapDocument& document, LevelMapRegionSelection mode, int axis = 2,
	QString* error = nullptr);

// True when the map's brush faces carry Quake II style content, surface and
// value numbers: Quake III maps, and Quake-format maps written with them.
bool levelMapUsesFaceFlags(const LevelMapDocument& document);
// Radiant's Make Detail and Make Structural. With face flags the selected
// brushes' faces gain or lose the detail content bit (0x8000000); Quake's
// compilers take func_detail entities instead, so there the brushes are tied
// to func_detail, or func_detail entities move back to the world. One undo
// command; `changed` counts brushes.
bool setLevelMapSelectionDetail(LevelMapDocument* document, bool detail, int* changed = nullptr, QString* error = nullptr);

// Drop to Floor: each selected point entity or Doom thing moves straight down
// until its bounds rest on the highest brush or patch surface beneath it.
// `bounds` gives a class's mins and maxs when the caller knows them (from the
// loaded definitions); without them an entity's origin rests on the floor.
// Entities with nothing beneath them stay. One undo command.
bool dropLevelMapSelectionToFloor(LevelMapDocument* document,
	const std::function<bool(const QString& className, LevelMapVec3* mins, LevelMapVec3* maxs)>& bounds = {}, int* dropped = nullptr,
	QString* error = nullptr);

// Makes the last `count` undo commands follow-ups of the command before
// them: done after it, undone before it, as one step that keeps that
// command's description and details. False, changing nothing, when the
// stack holds no command before them.
bool foldLevelMapFollowUpEdits(LevelMapDocument* document, int count, QString* error = nullptr);

// Folds the last `count` undo commands into one, so a tool made of several
// edits undoes and redoes as one step under `description`. False, changing
// nothing, when the stack holds fewer.
bool collapseLevelMapUndoSteps(LevelMapDocument* document, int count, const QString& description, const QString& undoDescription,
	QString* error = nullptr);
// Slants the selection: every point slides along `axis` by `factor` times
// its distance from the selection's centre along `along`, as TrenchBroom's
// shear tool does. Texture lock follows `textures`; Doom maps shear in the
// top view only. One undo step.
bool shearLevelMapSelection(LevelMapDocument* document, int axis, int along, double factor, const LevelMapTextureLockOptions& textures = {},
	QString* error = nullptr);
// The same about the line at `anchor` along `along`, which stays where it is,
// as the edge opposite the one dragged in a view's shear mode does.
bool shearLevelMapSelectionAbout(LevelMapDocument* document, int axis, int along, double factor, double anchor,
	const LevelMapTextureLockOptions& textures = {}, QString* error = nullptr);
// Turns the selection `quarterTurns` quarter turns anticlockwise, seen from
// above, about the vertical line through `pivot`, mirrored first across the
// line through `pivot` along x when `mirror` is set: the frames of linked
// group copies (core/level_linked_groups.h). Entity angles turn with it. One
// undo step; nothing to do (no turn, no mirror) succeeds without one.
bool turnLevelMapSelection(LevelMapDocument* document, int quarterTurns, bool mirror, const LevelMapVec3& pivot,
	const LevelMapTextureLockOptions& textures = {}, QString* error = nullptr);
// Bends each selected Doom linedef into `segments` linedefs along a circular
// arc bulging `bulge` units towards its front side (negative towards the
// back), as Doom Builder's curve mode does. New vertices are rounded to whole
// units; every piece keeps the line's flags, special and tag, with its own
// sides whose texture offsets carry on along the curve. One undo step that
// leaves the nodes to rebuild.
bool curveLevelMapLinedefs(LevelMapDocument* document, int segments, double bulge, int* curved = nullptr, QString* error = nullptr);
// The entities of a Quake-family map whose `key` (any case) holds `find`:
// the whole value, or anywhere in it; among all entities or only the
// selected ones.
QVector<int> levelMapEntitiesWithValue(const LevelMapDocument& document, const QString& key, const QString& find, bool wholeValue, bool selectionOnly);
// Find and replace on one key, as Hammer's entity search and Radiant's key
// find-and-replace do: the whole value, or every occurrence of `find` in it,
// becomes `replacement` on each matching entity, as one undo step.
bool replaceLevelMapEntityValues(LevelMapDocument* document, const QString& key, const QString& find, const QString& replacement, bool wholeValue,
	bool selectionOnly, int* replaced = nullptr, QString* error = nullptr);
// Which edge of each object Align lines up with the same edge of the whole
// selection's bounds.
enum class LevelMapAlignEdge { Minimum, Centre, Maximum };
// Moves each selected object along one axis so its minimum, centre or
// maximum meets the selection's, as Hammer's Align Objects does: entities
// with their brushes and patches, world brushes and patches, and Doom things
// (which have no Z). One undo step; the selection stays as it was.
bool alignLevelMapSelection(LevelMapDocument* document, int axis, LevelMapAlignEdge edge, int* moved = nullptr, QString* error = nullptr);
// A region, as Radiant's regions and Hammer's cordons make: the objects a box
// keeps. World brushes and patches touching it, point entities whose origin
// lies in it, and brush entities with a brush or patch touching it.
QVector<LevelMapSelectionRef> levelMapRegionObjects(const LevelMapDocument& document, const LevelMapVec3& mins, const LevelMapVec3& maxs);
// What a region leaves out: every other entity (taking its brushes and
// patches) and world brush and patch; worldspawn itself never.
QVector<LevelMapSelectionRef> levelMapOutsideRegionObjects(const LevelMapDocument& document, const LevelMapVec3& mins, const LevelMapVec3& maxs);
struct LevelMapRegionReport {
	int kept = 0;
	int removed = 0;
	int sealBrushes = 0;
	bool playerStartAdded = false;
};
// The region of a Quake-family map as a map of its own, for a quick compile:
// everything outside removed, the box sealed by six brushes of `sealTexture`
// just outside it (rounded out to whole units), and an info_player_start at
// `start` when the region keeps no player start.
bool levelMapRegionDocument(const LevelMapDocument& source, const LevelMapVec3& mins, const LevelMapVec3& maxs, const LevelMapVec3& start,
	const QString& sealTexture, LevelMapDocument* region, LevelMapRegionReport* report = nullptr, QString* error = nullptr);
// Splits each selected linedef of a Doom or Hexen map at its middle: a new
// vertex there, rounded to whole units, the linedef ending at it, and a new
// linedef from it to the old end with the same flags, special, and tag or
// arguments, and its own copies of the sides. Texture offsets carry on across
// the split, as Ultimate Doom Builder's default split leaves them, so each
// half shows the part of the texture it showed before. The halves are
// selected. One undo command; `split` reports how many linedefs were split.
bool splitLevelMapLinedefs(LevelMapDocument* document, int* split = nullptr, QString* error = nullptr);
// Draws a sector on a Doom or Hexen map from its corners, in order, the way
// Doom Builder closes a drawn shape. Corners are rounded to whole units; one
// on an existing vertex joins it, and one on a linedef splits the linedef
// there. An edge along an existing linedef shares it, and the rest become new
// linedefs whose fronts face the new sector. Drawn inside a sector, the new
// one copies it and its new lines are two-sided; drawn in the void, they are
// walls in the map's most used wall texture, and a line the shape shares
// opens onto the new sector. Lines the shape surrounds that faced the area it
// was drawn in face the new sector instead. A shape that crosses a linedef or
// itself, or has no area, is refused. The new sector is selected and its id
// reported. One undo command.
// `newLinedefs` reports the lines drawn, not the pieces splits added. A corner
// on a vertex a linedef uses joins it exactly, whole units or not; vertices no
// linedef uses, such as the ones node builders add, are passed over. A shape
// whose edges look into different areas (a sector on one side, the void on
// another) is refused rather than drawn over both.
bool drawLevelMapDoomSector(LevelMapDocument* document, const QVector<LevelMapVec3>& corners, int* sectorId = nullptr,
	QString* error = nullptr, int* newLinedefs = nullptr);
// Doom Builder's Make Sectors mode: the area of existing lines around (x, y),
// with any islands of lines inside it, becomes one new sector. Every side
// facing that area faces the new sector; a line that faced nothing there
// opens onto it. The sector copies the one the area belonged to, or else a
// neighbour, or the map's usual flats; when it takes over the whole of an
// old sector, that sector's tag and special come across. A point outside
// every closed shape of lines is refused. The new sector is selected and its
// id reported. One undo command.
bool makeLevelMapDoomSectorAt(LevelMapDocument* document, double x, double y, int* sectorId = nullptr, QString* error = nullptr);
// Joins the selected vertices of a Doom or Hexen map into the primary one:
// linedefs that ended at the others end there, a linedef whose ends meet
// goes, and two linedefs left between the same vertices become one, two
// one-sided lines facing apart joining into a two-sided line, the way Doom
// Builder stitches lines drawn over each other. One undo command; `merged`
// reports how many vertices went.
bool mergeLevelMapVertices(LevelMapDocument* document, int* merged = nullptr, QString* error = nullptr);
// Joins the selected sectors of a Doom or Hexen map into the primary one, as
// Doom Builder's Join Sectors does: every side that faced another faces it,
// and the others go. With `merge`, as Merge Sectors, the lines that divided
// the joined sectors go too, except ones that carry a special or a tag. One
// undo command; `joined` reports how many sectors went.
bool joinLevelMapSectors(LevelMapDocument* document, bool merge, int* joined = nullptr, QString* error = nullptr);

// What Make Door puts on a door: its textures, and whether texture offsets go
// back to 0. The defaults are Doom's own door and track.
struct LevelMapDoorOptions {
	QString doorTexture = QStringLiteral("BIGDOOR2");
	QString trackTexture = QStringLiteral("DOORTRAK");
	// Empty keeps each door's ceiling flat.
	QString ceilingFlat;
	bool resetOffsets = true;
};

// Makes each selected Doom or Hexen sector a door, the way Doom Builder's Make
// Door does: its ceiling comes down to its floor; every line to another sector
// faces out, opens the door when used from outside (special 1, or on a Hexen
// map Door_Raise, 12, with speed 16 and delay 150, repeatable on use) and shows
// `doorTexture` above it; every wall of the door's own becomes a track showing
// `trackTexture`, lower unpegged so it stays put as the door rises. A line
// between two of the doors is left alone. Sides packed sidedefs share are
// copied first. One undo step; the sectors stay selected.
bool makeLevelMapDoors(LevelMapDocument* document, const LevelMapDoorOptions& options, int* doors = nullptr, QString* error = nullptr);

// A field of a Doom sector the quick sector actions change.
enum class LevelMapSectorField {
	Floor,
	Ceiling,
	Light,
};
QString levelMapSectorFieldId(LevelMapSectorField field);
bool levelMapSectorFieldFromId(const QString& id, LevelMapSectorField* field);

// Shifts the selected Doom sectors' floors, ceilings, or light levels by
// `delta`, as Doom Builder's raise and lower actions do; heights stay within
// -32768 to 32767 and light within 0 to 255. One undo step that leaves the
// node lumps current, since no line moves.
bool shiftLevelMapSectors(LevelMapDocument* document, LevelMapSectorField field, int delta, int* changed = nullptr, QString* error = nullptr);
// Spreads a field evenly over three or more selected sectors in the order they
// were picked, from the first's value to the last's, as Doom Builder's
// Gradient Floors, Ceilings, and Brightness do. One undo step.
bool gradientLevelMapSectors(LevelMapDocument* document, LevelMapSectorField field, int* changed = nullptr, QString* error = nullptr);
// Turns each selected linedef around, as Doom Builder's Flip Linedefs does: its
// ends swap and, on a two-sided line, so do its sides, so each sector keeps its
// side. A one-sided line's side turns to face the other way, which is how a
// line facing the void is put right. One undo command.
bool flipLevelMapLinedefs(LevelMapDocument* document, int* flipped = nullptr, QString* error = nullptr);
// Moves each selected entity, brush, patch, thing, and vertex by its own
// amount so its reference point lands on the grid: an entity's origin, a
// brush's or patch's lower corner, a thing's or vertex's position. A brush
// entity without an origin uses its combined lower corner. Selected owners
// move their children together, exactly once. Lines/sectors expand to unique
// vertices; a collapsed line is refused. Doom/Hexen use an integer XY grid.
// UDMF also accepts fractional XY grids and leaves thing heights unchanged.
// The legacy overload keeps source texture parameters; UI/CLI use explicit
// texture policy. Validation, selection and native snapshots form one undo.
bool snapLevelMapSelectionToGrid(LevelMapDocument* document, double gridSize, QString* error = nullptr);
bool snapLevelMapSelectionToGrid(LevelMapDocument* document, double gridSize,
	const LevelMapTextureLockOptions& textures, QString* error = nullptr);
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
