#pragma once

// Generated levels for idTech games: from a prompt to an editable map.
//
// The pipeline follows Quake-MapGen's (https://github.com/themuffinator/Quake-MapGen,
// MIT, main at 1252548 of 2026-08-26 and its v0.4.0/M3 README and
// docs/ARCHITECTURE.md, reviewed 2026-10-06; see docs/CREDITS.md):
//  1. normalize the prompt into a spec (game, mode, theme, size, verticality,
//     players, seed), the same prompt always giving the same spec;
//  2. make a coordinate-free semantic plan: rooms with roles, connections,
//     and placements. A deterministic rules planner always can; a text model
//     can instead, asked for JSON against levelSemanticPlanSchema (structured
//     output where the provider has it), with its answer checked against the
//     schema and repaired by the same rules;
//  3. lay the plan out on an integer grid: rooms placed breadth-first beside
//     the room they connect to, straight corridors with stairs between floor
//     levels, and further links routed around the rooms;
//  4. build 2.5D cells (floor and ceiling heights, liquids) into sealed
//     brushes: every open cell has a floor slab and a ceiling slab and every
//     solid cell beside one is a full column, so no cell can leak;
//  5. place entities, check every room is reachable from the start, and write
//     the game's own map: .map text for Quake, Quake II and Quake III, or a
//     Doom-format PWAD (sectors traced from the same cells) for Doom.
// No code is copied from Quake-MapGen; its plan/layout/lowering split, its
// fail-closed validation, and its deterministic seeds are what this follows.

#include "core/ai_transport.h"

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct LevelGenerationSpec {
	// The user's words; the fields below, when empty, are read from them.
	QString prompt;
	// quake, quake2, quake3, or doom.
	QString game;
	// single-player, deathmatch, or duel.
	QString mode;
	// base, medieval, metal, hell, or cave.
	QString theme;
	// Room count; 0 takes it from the prompt or the size.
	int rooms = 0;
	// small, medium, or large.
	QString size;
	// flat, low, medium, or high.
	QString verticality;
	// Deathmatch starts wanted; 0 takes them from the prompt or the mode.
	int players = 0;
	// none, light, normal, or heavy (single player).
	QString monsters;
	// none, water, slime, or lava: the liquid pits use.
	QString liquid;
	// The map's title (worldspawn message).
	QString title;
	// -1 takes it from the prompt: the same words give the same map.
	qint64 seed = -1;
	// Texture names by role (wall, floor, ceiling, trim, light, liquid, sky)
	// that replace the theme's.
	QHash<QString, QString> textures;
	// Textures the project has; when given, the theme's choices are matched
	// against them so the map uses textures that exist.
	QStringList availableTextures;
	// Quake: the WAD for worldspawn's "wad" key.
	QString wad;
};

// Fills what the spec leaves empty from its prompt and defaults, the same
// way every time: "Quake 2 deathmatch, 8 rooms, lava, seed 42".
[[nodiscard]] LevelGenerationSpec normalizedLevelGenerationSpec(const LevelGenerationSpec& spec);
[[nodiscard]] QStringList levelGenerationGameIds();
[[nodiscard]] QStringList levelGenerationModeIds();
[[nodiscard]] QStringList levelGenerationThemeIds();

struct LevelPlanRoom {
	QString id;
	QString name;
	// start, exit, arena, hub, hall, storage, secret, or overlook.
	QString role;
	// small, medium, or large.
	QString size = QStringLiteral("medium");
	// low, normal, or tall.
	QString height = QStringLiteral("normal");
	// Floor level, 0 to 3; each is 64 units.
	int level = 0;
	// box, pillars, pit, or platform.
	QString shape = QStringLiteral("box");
	// none, water, slime, or lava, for a pit.
	QString liquid = QStringLiteral("none");
	// dim, normal, or bright.
	QString lighting = QStringLiteral("normal");
};

struct LevelPlanConnection {
	QString from;
	QString to;
	// corridor, stairs, or open.
	QString kind = QStringLiteral("corridor");
};

struct LevelPlanPlacement {
	QString room;
	// player-start, deathmatch-start, weapon, ammo, health, armor, powerup,
	// monster, or exit.
	QString kind;
	// What exactly: an item id from levelGenerationItemIds (empty for starts
	// and the exit, or to let the generator choose).
	QString item;
	int count = 1;
};

// A level as a plan, without coordinates.
struct LevelSemanticPlan {
	static constexpr int kSchemaVersion = 1;

	QString title;
	QString summary;
	QString theme;
	QVector<LevelPlanRoom> rooms;
	QVector<LevelPlanConnection> connections;
	QVector<LevelPlanPlacement> placements;
	// rules/v1, or the connector and model that drew it.
	QString planner;
	// What repairLevelSemanticPlan changed, in order.
	QStringList repairs;
};

// The item ids a placement kind takes in a game, as plans name them.
[[nodiscard]] QStringList levelGenerationItemIds(const QString& game, const QString& kind);
// The JSON Schema a text model's plan must match, with the game's items.
[[nodiscard]] QJsonObject levelSemanticPlanSchema(const QString& game);
[[nodiscard]] QJsonObject levelSemanticPlanJson(const LevelSemanticPlan& plan);
// Reads a plan; problems are the schema's (aiJsonSchemaProblems). A plan with
// problems can still be read where its shape allows, then repaired.
bool levelSemanticPlanFromJson(const QJsonObject& object, LevelSemanticPlan* plan, QStringList* problems = nullptr);
// The deterministic planner: rooms, a main route, loops, and placements from
// the spec and its seed.
[[nodiscard]] LevelSemanticPlan rulesLevelPlan(const LevelGenerationSpec& spec);
// Makes a plan buildable: unique ids, known values, links between real rooms,
// one connected level, a start, an exit for single player, enough
// deathmatch starts, items the game has. Returns what it changed (also
// appended to plan->repairs).
QStringList repairLevelSemanticPlan(LevelSemanticPlan* plan, const LevelGenerationSpec& spec);

// The text model's instructions and question for a plan.
[[nodiscard]] QString levelPlanSystemPrompt(const LevelGenerationSpec& spec);
[[nodiscard]] QString levelPlanUserPrompt(const LevelGenerationSpec& spec);
// A structured request for a plan. `revision` lists problems with an earlier
// answer, asking for a corrected plan.
[[nodiscard]] AiChatRequest levelPlanAiRequest(const LevelGenerationSpec& spec, const QString& connectorId, const QString& model, const QString& endpoint,
	const QString& previousAnswer = QString(), const QStringList& revision = {});
// Reads a model's answer into a plan: the JSON object in it, checked against
// the schema. False with the problems when there is nothing usable.
bool levelSemanticPlanFromAnswer(const QString& answer, const LevelGenerationSpec& spec, LevelSemanticPlan* plan, QStringList* problems);
// The plan as a person reads it: the title and planner, each room with what
// it holds, then the links.
[[nodiscard]] QStringList levelSemanticPlanLines(const LevelSemanticPlan& plan);

// One square of the layout grid.
struct LevelCell {
	bool open = false;
	int floor = 0;
	int ceiling = 0;
	// Liquid surface height; below `floor` means none.
	int liquidTop = -32768;
	QString liquid;
	// Which room or corridor the cell belongs to (room id, or "c:<n>").
	QString owner;
	// Doom: a separate sector (an exit pad, a lit spot).
	int tag = 0;
};

struct LevelLayoutRoom {
	QString id;
	QString role;
	// In cells.
	QRect rect;
	int floor = 0;
	int ceiling = 0;
	QString shape;
	QString liquid;
	QString lighting;
	QString name;
};

struct LevelLayoutCorridor {
	QString from;
	QString to;
	// Centre-line cells, in order from `from` to `to`.
	QVector<QPoint> path;
	int width = 3;
};

struct LevelPlacedEntity {
	QString kind;
	QString item;
	QString room;
	// In map units.
	double x = 0;
	double y = 0;
	double z = 0;
	int angle = 0;
	QString className;
	int doomType = 0;
	QHash<QString, QString> keys;
};

struct LevelLayout {
	// Units per cell.
	int cellSize = 32;
	// Cell (0, 0)'s lower corner in map units.
	QPoint origin;
	int width = 0;
	int height = 0;
	QVector<LevelCell> cells;
	QVector<LevelLayoutRoom> rooms;
	QVector<LevelLayoutCorridor> corridors;
	QVector<LevelPlacedEntity> entities;
	// Links the layout could not route, and rooms it could not place.
	QStringList dropped;

	[[nodiscard]] bool contains(int x, int y) const
	{
		return x >= 0 && y >= 0 && x < width && y < height;
	}
	[[nodiscard]] const LevelCell& cell(int x, int y) const
	{
		return cells[y * width + x];
	}
	LevelCell& cell(int x, int y)
	{
		return cells[y * width + x];
	}
};

struct LevelGenerationStatistics {
	int rooms = 0;
	int corridors = 0;
	int openCells = 0;
	int brushes = 0;
	int entities = 0;
	int lights = 0;
	int monsters = 0;
	int items = 0;
	int sectors = 0;
	int linedefs = 0;
	// Rooms the start reaches on foot (or swimming), of all rooms.
	int reachableRooms = 0;
	QSize extentUnits;
};

struct LevelGenerationResult {
	bool ok = false;
	QString error;
	LevelGenerationSpec spec;
	LevelSemanticPlan plan;
	LevelLayout layout;
	// .map text, or a PWAD for Doom.
	QByteArray mapBytes;
	// quake-map, quake2-map, quake3-map, or doom-wad.
	QString format;
	// For example generated_medieval_42.map.
	QString suggestedFileName;
	// Doom: the map lump name.
	QString mapName;
	LevelGenerationStatistics statistics;
	QStringList notes;
	QStringList warnings;
};

// Steps 3-5 for a plan; with no plan, the rules planner makes one. The plan is
// repaired first either way.
[[nodiscard]] LevelGenerationResult generateLevel(const LevelGenerationSpec& spec, const LevelSemanticPlan* plan = nullptr);
struct LevelMapDocument;
// The generated level as a new, unsaved editor document (no file yet, its
// title as the map name), the way a new map starts.
bool levelGenerationDocument(const LevelGenerationResult& result, LevelMapDocument* document, QString* error = nullptr);
// A top-down picture of the layout: floors shaded by height, liquids, room
// names, links, and entities. For the generator dialog and `map generate
// --preview`.
[[nodiscard]] QImage renderLevelLayoutPreview(const LevelGenerationResult& result, QSize size);
[[nodiscard]] QJsonObject levelGenerationSpecJson(const LevelGenerationSpec& spec);
[[nodiscard]] QJsonObject levelGenerationReportJson(const LevelGenerationResult& result);
// Human-readable summary lines for the CLI and the dialog.
[[nodiscard]] QStringList levelGenerationSummaryLines(const LevelGenerationResult& result);

} // namespace vibestudio
