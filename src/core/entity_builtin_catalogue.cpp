// Built-in entity catalogues for Quake, Quake II and Quake III Arena.
//
// The facts in these tables (class names, key names and their default
// values, spawnflag bits, editor sizes and editor colours) come from id
// Software's GPL releases (GNU General Public License, version 2 or later).
// Only facts were taken: every description is VibeStudio's own wording, and
// no comment text from those sources is reproduced.
//
// - Quake: id's GPL QuakeC release (v1.01,
//   https://github.com/id-Software/Quake-Tools/tree/c0d1b91c74eb654365ac7755bc837e497caaca73/qcc/v101qc),
//   checked against the 1.06 progs (https://github.com/maddes-b/QuakeC-releases,
//   progs/ at commit 2811c02 "v1.06"), whose QUAKED blocks and defaults
//   match. Compiler keys come from the same Quake-Tools revision:
//   qutils/QBSP/WRITEBSP.C (wad) and qutils/LIGHT/ENTITIES.C and LTFACE.C
//   (lights).
// - Quake II: the game DLL source,
//   https://github.com/id-Software/Quake-2/tree/372afde46e7defc9dd2d719a1732b8ace1fa096e/game,
//   and its light compiler,
//   https://github.com/id-Software/Quake-2-Tools/blob/707e849167cb520a5592aa2181308ab947f2a2fd/bsp/qrad3/lightmap.c.
// - Quake III Arena: the game module source,
//   https://github.com/id-Software/Quake-III-Arena/tree/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/game,
//   with q3map (q3map/light.c, q3map/misc_model.c) and the bot library
//   (code/botlib/be_ai_goal.c) from the same revision. Compiler keys that
//   id's q3map does not read (_blocksize, _ambient, the light noincidence
//   flag, and the misc_model angles and modelscale keys) follow q3map2 from
//   NetRadiant Custom (GPL-2.0 or later), which VibeStudio ships in
//   external/compilers/q3map2-nrc.
//
// - Quake compiler classes (func_group and func_detail with its _illusionary,
//   _wall and _fence variants) follow the qbsp documentation of ericw-tools
//   (GPL-2.0 or later), docs/qbsp.rst at
//   https://github.com/ericwa/ericw-tools/blob/f80b1e216a415581aea7475cb52b16b8c4859084/docs/qbsp.rst,
//   which VibeStudio ships in external/compilers/ericw-tools.
//
// Where a QUAKED comment and the code disagree, the tables follow the code:
// for example the Quake II func_button wait of 3 and func_train dmg of 100.
// Mission pack classes and test classes the progs never compile are left
// out; Quake's viewthing and misc_noisemaker stay because the shipped progs
// still spawn them.

#include "core/entity_builtin_catalogue.h"

#include "core/level_document.h"

#include <QCoreApplication>
#include <QSet>
#include <QStringView>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <utility>

namespace vibestudio {

namespace {

using Type = EntityKeyType;

// ---------------------------------------------------------------------------
// Table rows
//
// Each game's catalogue is one flat table read in order, much like a
// definition file. A class row opens a class; the key, choice, flag, default
// and shared-set rows after it belong to that class until the next class row.
// A choice row adds a choice to the key row just before it, a default row
// replaces the default of a key declared earlier, and a shared-set row splices
// in a small table of rows that many classes have in common.
//
// The first declaration of a key name or spawnflag bit in a class wins, so a
// class lists its own versions before the shared sets it uses.
//
// Descriptions and choice labels are marked with QT_TRANSLATE_NOOP and
// translated when the catalogue is built; class, key and flag names are
// technical identifiers and stay as the games spell them.
// ---------------------------------------------------------------------------

enum class RowKind {
	Class,
	Key,
	Choice,
	Default,
	Flag,
	Shared,
};

// A QUAKED editor colour: red, green and blue from 0 to 1.
struct Rgb {
	float red = 0.0f;
	float green = 0.0f;
	float blue = 0.0f;
};

// A point class's editor box relative to its origin.
struct Box {
	float mins[3] = {};
	float maxs[3] = {};
};

struct Row {
	RowKind kind = RowKind::Class;
	const char* name = "";
	// A QT_TRANSLATE_NOOP description, or a choice's label.
	const char* text = "";
	// A key's default value, or a class's model hint.
	const char* value = "";
	EntityClassKind classKind = EntityClassKind::Unknown;
	Type type = Type::String;
	bool required = false;
	int bit = 0;
	Rgb color;
	Box box;
	const Row* rows = nullptr;
	std::size_t rowCount = 0;
};

constexpr Row point(const char* name, Rgb color, Box box, const char* description, const char* model = "")
{
	Row row;
	row.kind = RowKind::Class;
	row.classKind = EntityClassKind::Point;
	row.name = name;
	row.text = description;
	row.value = model;
	row.color = color;
	row.box = box;
	return row;
}

constexpr Row brush(const char* name, Rgb color, const char* description)
{
	Row row;
	row.kind = RowKind::Class;
	row.classKind = EntityClassKind::Brush;
	row.name = name;
	row.text = description;
	row.color = color;
	return row;
}

constexpr Row key(const char* name, Type type, const char* defaultValue, const char* description)
{
	Row row;
	row.kind = RowKind::Key;
	row.name = name;
	row.type = type;
	row.value = defaultValue;
	row.text = description;
	return row;
}

// A key the class cannot work without: the game rejects or ignores the entity
// (or the compiler skips it) when the key is missing.
constexpr Row needs(const char* name, Type type, const char* description)
{
	Row row = key(name, type, "", description);
	row.required = true;
	return row;
}

constexpr Row choice(const char* value, const char* label)
{
	Row row;
	row.kind = RowKind::Choice;
	row.name = value;
	row.text = label;
	return row;
}

// Gives a key that a shared set declared this class's own default value.
constexpr Row byDefault(const char* name, const char* value)
{
	Row row;
	row.kind = RowKind::Default;
	row.name = name;
	row.value = value;
	return row;
}

constexpr Row flag(int bit, const char* name, const char* description)
{
	Row row;
	row.kind = RowKind::Flag;
	row.bit = bit;
	row.name = name;
	row.text = description;
	return row;
}

template <std::size_t Count>
constexpr Row uses(const Row (&rows)[Count])
{
	Row row;
	row.kind = RowKind::Shared;
	row.rows = rows;
	row.rowCount = Count;
	return row;
}

// ---------------------------------------------------------------------------
// Rows several games share
// ---------------------------------------------------------------------------

constexpr Row kTriggered[] = {
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name that other entities use to trigger this one.")),
};

constexpr Row kAimPoint[] = {
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name that other entities use to aim at this point.")),
};

constexpr Row kFacing[] = {
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Direction it faces, in degrees.")),
};

constexpr Row kTarget[] = {
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Entities this one triggers.")),
};

// The keys Quake's SUB_UseTargets and Quake II's G_UseTargets read whenever
// an entity fires its targets.
constexpr Row kFires[] = {
	uses(kTarget),
	key("killtarget", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Entities this one removes from the level when it fires.")),
	key("delay", Type::Real, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds to wait before firing the targets.")),
	key("message", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Text shown to the player who set it off.")),
};

// The twelve light styles Quake and Quake II both set up in worldspawn.
constexpr Row kLightStyle[] = {
	key("style", Type::Integer, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Animation pattern: 0 steady, 1 flicker, 2 slow strong pulse, 3 candle, 4 fast strobe, 5 gentle pulse, 6 second flicker, 7 second candle, 8 third candle, 9 slow strobe, 10 fluorescent flicker, 11 slow pulse that never goes dark. Switchable lights get their own style from the compiler.")),
};

constexpr Row kStartOff[] = {
	flag(0, "START_OFF", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Starts switched off; each trigger toggles it.")),
};

constexpr Row kMoveAngle[] = {
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Direction it moves, in degrees; -1 is up and -2 is down.")),
};

constexpr Row kTriggerSounds[] = {
	key("sounds", Type::Choices, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Sound it plays when it fires.")),
	choice("0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "None")),
	choice("1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Secret")),
	choice("2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Beep beep")),
	choice("3", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Large switch")),
};

// ---------------------------------------------------------------------------
// Quake
// ---------------------------------------------------------------------------

constexpr Row kQuakeLight[] = {
	key("light", Type::Integer, "300", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Brightness the light compiler gives it.")),
	uses(kLightStyle),
};

constexpr Row kQuakeMonsterKeys[] = {
	uses(kFacing),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name that triggers use to wake the monster and set it on whoever fired them.")),
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A path_corner to walk to, or entities to trigger when the monster dies.")),
	key("killtarget", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Entities removed when the monster dies; needs a target as well.")),
};

constexpr Row kQuakeMonster[] = {
	uses(kQuakeMonsterKeys),
	flag(0, "Ambush", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Wakes only on seeing the player itself, not when another monster raises the alarm.")),
};

constexpr Row kQuakeBigBox[] = {
	flag(0, "big", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A larger box holding twice as much.")),
};

// trigger_multiple and trigger_once.
constexpr Row kQuakeTouchTrigger[] = {
	key("health", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "If set, shooting it activates it instead of touching it.")),
	uses(kTriggerSounds),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "If set, it fires only for someone facing this way; use 360 for east.")),
	uses(kTriggered),
	uses(kFires),
	flag(0, "notouch", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires only when another entity triggers it, never by touch.")),
};

constexpr Row kQuakeShooter[] = {
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Firing direction, in degrees; -1 is up and -2 is down.")),
	uses(kTriggered),
	flag(0, "superspike", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires heavier super nails.")),
	flag(1, "laser", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires laser bolts instead (registered version only).")),
};

constexpr Row kQuakeRows[] = {
	brush("worldspawn", {0, 0, 0}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The world itself: every map has exactly one, holding the level's settings, and its brushes form the level's structure.")),
	key("message", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Level name shown to players.")),
	key("worldtype", Type::Choices, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Theme of the level, which picks the key models and the sounds of locked doors.")),
	choice("0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Medieval")),
	choice("1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Metal")),
	choice("2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Base")),
	key("sounds", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "CD audio track to play.")),
	key("wad", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Texture WAD the compiler takes textures from.")),

	// Player starts and markers.
	point("info_player_start", {1, 0, 0}, {{-16, -16, -24}, {16, 16, 24}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Where the player starts in single player.")),
	uses(kFacing),
	point("info_player_start2", {1, 0, 0}, {{-16, -16, -24}, {16, 16, 24}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Second start on the start map, used when the player comes back from a finished episode.")),
	uses(kFacing),
	point("info_player_deathmatch", {1, 0, 1}, {{-16, -16, -24}, {16, 16, 24}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A deathmatch spawn point; place several.")),
	uses(kFacing),
	point("info_player_coop", {1, 0, 1}, {{-16, -16, -24}, {16, 16, 24}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A co-operative spawn point.")),
	uses(kFacing),
	point("info_intermission", {1, 0.5f, 0.5f}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A camera for the end-of-level statistics screen; one is picked at random when there are several.")),
	key("mangle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "View direction as pitch, yaw and roll.")),
	point("info_teleport_destination", {0.5f, 0.5f, 0.5f}, {{-8, -8, -8}, {8, 8, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Where a trigger_teleport sends whatever it teleports.")),
	needs("targetname", Type::TargetSource, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name the teleporter's target refers to.")),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Direction a teleported player faces.")),
	point("info_null", {0, 0.5f, 0}, {{-4, -4, -4}, {4, 4, 4}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A position for the compilers to aim at, such as a spotlight target; removed when the level starts.")),
	uses(kAimPoint),
	point("info_notnull", {0, 0.5f, 0}, {{-4, -4, -4}, {4, 4, 4}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A position marker that stays in the game, for entities that aim at a point.")),
	uses(kAimPoint),

	// Lights. The light compiler treats every class whose name starts with
	// "light" as a light source.
	point("light", {0, 1, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An invisible light that the light compiler bakes into the level. Give it a targetname to make it switchable in the game.")),
	uses(kQuakeLight),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name that makes the light switchable; each trigger toggles it.")),
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An entity to aim at, which makes this a spotlight.")),
	key("angle", Type::Real, "40", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Width of the spotlight cone in degrees.")),
	uses(kStartOff),
	point("light_fluoro", {0, 1, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An invisible light that also hums like a fluorescent tube.")),
	uses(kQuakeLight),
	uses(kTriggered),
	uses(kStartOff),
	point("light_fluorospark", {0, 1, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An invisible light with the buzz of a sparking, broken fluorescent tube.")),
	uses(kQuakeLight),
	byDefault("style", "10"),
	point("light_globe", {0, 1, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A light shown in the game as a small glowing sprite."), "progs/s_light.spr"),
	uses(kQuakeLight),
	point("light_torch_small_walltorch", {0, 0.5f, 0}, {{-10, -10, -20}, {10, 10, 20}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A short wall torch: a burning flame model with its sound, plus light."), "progs/flame.mdl"),
	uses(kQuakeLight),
	point("light_flame_large_yellow", {0, 1, 0}, {{-10, -10, -12}, {12, 12, 18}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A large yellow flame with its light and fire sound."), "progs/flame2.mdl"),
	uses(kQuakeLight),
	point("light_flame_small_yellow", {0, 1, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A small yellow flame with its light and fire sound."), "progs/flame2.mdl"),
	uses(kQuakeLight),
	uses(kStartOff),
	point("light_flame_small_white", {0, 1, 0}, {{-10, -10, -40}, {10, 10, 40}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A small white flame with its light and fire sound."), "progs/flame2.mdl"),
	uses(kQuakeLight),
	uses(kStartOff),

	// Monsters.
	point("monster_army", {1, 0, 0}, {{-16, -16, -24}, {16, 16, 40}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Grunt: a soldier armed with a shotgun."), "progs/soldier.mdl"),
	uses(kQuakeMonster),
	point("monster_dog", {1, 0, 0}, {{-32, -32, -24}, {32, 32, 40}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Rottweiler: a fast dog that bites and leaps at the player."), "progs/dog.mdl"),
	uses(kQuakeMonster),
	point("monster_ogre", {1, 0, 0}, {{-32, -32, -24}, {32, 32, 64}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Ogre: swings a chainsaw up close and lobs grenades from afar."), "progs/ogre.mdl"),
	uses(kQuakeMonster),
	// No QUAKED block of its own: the progs spawn an ordinary ogre for it.
	point("monster_ogre_marksman", {1, 0, 0}, {{-32, -32, -24}, {32, 32, 64}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Ogre marksman: the original game code spawns an ordinary ogre for it."), "progs/ogre.mdl"),
	uses(kQuakeMonster),
	point("monster_knight", {1, 0, 0}, {{-16, -16, -24}, {16, 16, 40}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Knight: a sword-wielding melee fighter."), "progs/knight.mdl"),
	uses(kQuakeMonster),
	point("monster_hell_knight", {1, 0, 0}, {{-16, -16, -24}, {16, 16, 40}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Death Knight: a heavily armoured knight that also hurls fire."), "progs/hknight.mdl"),
	uses(kQuakeMonster),
	point("monster_wizard", {1, 0, 0}, {{-16, -16, -24}, {16, 16, 40}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Scrag: a floating wizard that spits acid."), "progs/wizard.mdl"),
	uses(kQuakeMonster),
	point("monster_demon1", {1, 0, 0}, {{-32, -32, -24}, {32, 32, 64}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fiend: a demon that leaps at the player and slashes."), "progs/demon.mdl"),
	uses(kQuakeMonster),
	point("monster_shambler", {1, 0, 0}, {{-32, -32, -24}, {32, 32, 64}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Shambler: a huge beast that claws and casts lightning."), "progs/shambler.mdl"),
	uses(kQuakeMonster),
	point("monster_zombie", {1, 0, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Zombie: hurls chunks of flesh and gets back up unless gibbed."), "progs/zombie.mdl"),
	uses(kQuakeMonsterKeys),
	flag(0, "Crucified", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Pinned to a wall as harmless scenery; sink its box 12 units into the wall so it looks right.")),
	flag(1, "ambush", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Wakes only on seeing the player itself, not when another monster raises the alarm.")),
	point("monster_enforcer", {1, 0, 0}, {{-16, -16, -24}, {16, 16, 40}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Enforcer: a soldier firing laser bolts."), "progs/enforcer.mdl"),
	uses(kQuakeMonster),
	point("monster_shalrath", {1, 0, 0}, {{-32, -32, -24}, {32, 32, 48}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Vore: launches slow homing pods."), "progs/shalrath.mdl"),
	uses(kQuakeMonster),
	point("monster_tarbaby", {1, 0, 0}, {{-16, -16, -24}, {16, 16, 24}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Spawn: a bouncing blob that explodes when it dies."), "progs/tarbaby.mdl"),
	uses(kQuakeMonster),
	point("monster_fish", {1, 0, 0}, {{-16, -16, -24}, {16, 16, 24}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Rotfish: a biting fish for underwater areas."), "progs/fish.mdl"),
	uses(kQuakeMonster),
	point("monster_oldone", {1, 0, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Shub-Niggurath, the final boss: only a telefrag kills it. Removed in deathmatch."), "progs/oldone.mdl"),
	uses(kFacing),
	point("monster_boss", {1, 0, 0}, {{-128, -128, -24}, {128, 128, 256}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Chthon, the lava boss: rises when triggered and is hurt only by event_lightning. Removed in deathmatch."), "progs/boss.mdl"),
	uses(kTriggered),

	// Weapons.
	point("weapon_supershotgun", {0, 0.5f, 0.8f}, {{-16, -16, 0}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the double-barrelled shotgun."), "progs/g_shot.mdl"),
	uses(kFires),
	point("weapon_nailgun", {0, 0.5f, 0.8f}, {{-16, -16, 0}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the nailgun."), "progs/g_nail.mdl"),
	uses(kFires),
	point("weapon_supernailgun", {0, 0.5f, 0.8f}, {{-16, -16, 0}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the super nailgun."), "progs/g_nail2.mdl"),
	uses(kFires),
	point("weapon_grenadelauncher", {0, 0.5f, 0.8f}, {{-16, -16, 0}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the grenade launcher."), "progs/g_rock.mdl"),
	uses(kFires),
	point("weapon_rocketlauncher", {0, 0.5f, 0.8f}, {{-16, -16, 0}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the rocket launcher."), "progs/g_rock2.mdl"),
	uses(kFires),
	point("weapon_lightning", {0, 0.5f, 0.8f}, {{-16, -16, 0}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the Thunderbolt lightning gun."), "progs/g_light.mdl"),
	uses(kFires),

	// Items.
	point("item_health", {0.3f, 0.3f, 1}, {{0, 0, 0}, {32, 32, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Health box worth 25 health, or less or more with its flags."), "maps/b_bh25.bsp"),
	uses(kFires),
	flag(0, "rotten", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A smaller box worth 15 health.")),
	flag(1, "megahealth", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Worth 100 health, even past the maximum; the extra slowly wears off.")),
	point("item_armor1", {0, 0.5f, 0.8f}, {{-16, -16, 0}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Green armour: 100 points that absorb 30 per cent of damage."), "progs/armor.mdl"),
	uses(kFires),
	point("item_armor2", {0, 0.5f, 0.8f}, {{-16, -16, 0}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Yellow armour: 150 points that absorb 60 per cent of damage."), "progs/armor.mdl"),
	uses(kFires),
	point("item_armorInv", {0, 0.5f, 0.8f}, {{-16, -16, 0}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Red armour: 200 points that absorb 80 per cent of damage."), "progs/armor.mdl"),
	uses(kFires),
	point("item_shells", {0, 0.5f, 0.8f}, {{0, 0, 0}, {32, 32, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Box of shells for both shotguns: 20, or 40 when big."), "maps/b_shell0.bsp"),
	uses(kFires),
	uses(kQuakeBigBox),
	point("item_spikes", {0, 0.5f, 0.8f}, {{0, 0, 0}, {32, 32, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Box of nails for both nailguns: 25, or 50 when big."), "maps/b_nail0.bsp"),
	uses(kFires),
	uses(kQuakeBigBox),
	point("item_rockets", {0, 0.5f, 0.8f}, {{0, 0, 0}, {32, 32, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Box of rockets for both launchers: 5, or 10 when big."), "maps/b_rock0.bsp"),
	uses(kFires),
	uses(kQuakeBigBox),
	point("item_cells", {0, 0.5f, 0.8f}, {{0, 0, 0}, {32, 32, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Battery of cells for the Thunderbolt: 6, or 12 when big."), "maps/b_batt0.bsp"),
	uses(kFires),
	uses(kQuakeBigBox),
	point("item_weapon", {0, 0.5f, 0.8f}, {{0, 0, 0}, {32, 32, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Obsolete ammunition box whose contents come from its flags; id marked it for removal, so prefer the item_shells, item_spikes and item_rockets classes.")),
	uses(kFires),
	flag(0, "shotgun", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Holds shells.")),
	flag(1, "rocket", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Holds rockets.")),
	flag(2, "spikes", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Holds nails.")),
	flag(3, "big", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A larger box holding twice as much.")),
	point("item_key1", {0, 0.5f, 0.8f}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Silver key for doors with the SILVER_KEY flag; its model and sound follow the worldspawn worldtype."), "progs/w_s_key.mdl"),
	uses(kFires),
	point("item_key2", {0, 0.5f, 0.8f}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Gold key for doors with the GOLD_KEY flag; its model and sound follow the worldspawn worldtype."), "progs/w_g_key.mdl"),
	uses(kFires),
	point("item_sigil", {0, 0.5f, 0.8f}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Episode rune: picking it up marks its episode as complete for the rest of the game. Set exactly one episode flag."), "progs/end1.mdl"),
	uses(kFires),
	flag(0, "E1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The rune of episode 1.")),
	flag(1, "E2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The rune of episode 2.")),
	flag(2, "E3", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The rune of episode 3.")),
	flag(3, "E4", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The rune of episode 4.")),
	point("item_artifact_invulnerability", {0, 0.5f, 0.8f}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Pentagram of Protection: 30 seconds of invulnerability."), "progs/invulner.mdl"),
	uses(kFires),
	point("item_artifact_envirosuit", {0, 0.5f, 0.8f}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Biosuit: for 30 seconds the player breathes under water, ignores slime and is burnt by lava less often."), "progs/suit.mdl"),
	uses(kFires),
	point("item_artifact_invisibility", {0, 0.5f, 0.8f}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Ring of Shadows: 30 seconds of near invisibility."), "progs/invisibl.mdl"),
	uses(kFires),
	point("item_artifact_super_damage", {0, 0.5f, 0.8f}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Quad Damage: 30 seconds of four times the damage."), "progs/quaddama.mdl"),
	uses(kFires),

	// Brush entities.
	brush("func_door", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A sliding door. Touching doors open as one unless DOOR_DONT_LINK is set; a door with a targetname or health opens only when triggered or shot.")),
	uses(kMoveAngle),
	key("speed", Type::Real, "100", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Movement speed in units per second.")),
	key("wait", Type::Real, "3", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds before it closes again; -1 keeps it open. Key doors always stay open.")),
	key("lip", Type::Integer, "8", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "How much of it still shows at the end of its move.")),
	key("dmg", Type::Integer, "2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage dealt to anything that blocks it.")),
	key("health", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "If set, shooting it activates it instead of touching it.")),
	key("sounds", Type::Choices, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Sounds it makes while moving.")),
	choice("0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "None")),
	choice("1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Stone")),
	choice("2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Base")),
	choice("3", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Stone chain")),
	choice("4", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Screechy metal")),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name that buttons and triggers use to open it; a named door has no touch trigger of its own.")),
	key("message", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Text shown to a player who touches it.")),
	uses(kFires),
	flag(0, "START_OPEN", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Spawns open and works in reverse, so triggering it closes the way; it is lit in its closed position.")),
	flag(2, "DOOR_DONT_LINK", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Does not join doors it touches into one.")),
	flag(3, "GOLD_KEY", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Needs the gold key, then stays open.")),
	flag(4, "SILVER_KEY", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Needs the silver key, then stays open.")),
	flag(5, "TOGGLE", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Waits at each end for another trigger instead of moving back by itself.")),
	brush("func_door_secret", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A secret door that moves in two steps: first sideways (or down), then along its angle. Without a targetname, shooting it opens it.")),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Direction of the second move, in degrees.")),
	key("wait", Type::Real, "5", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds before it closes again.")),
	key("t_width", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Length of the first move; by default the door's size in that direction.")),
	key("t_length", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Length of the second move; by default the door's size along its angle.")),
	key("dmg", Type::Integer, "2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage dealt to anything that blocks it.")),
	key("sounds", Type::Choices, "3", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Sounds it makes while moving.")),
	choice("1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Medieval")),
	choice("2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Metal")),
	choice("3", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Base")),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name of the button or trigger that opens it; a named door ignores shots unless always_shoot is set.")),
	key("message", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Text shown to a player who touches it.")),
	uses(kFires),
	flag(0, "open_once", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Never closes again.")),
	flag(1, "1st_left", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The first move goes left of the angle instead of right.")),
	flag(2, "1st_down", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The first move goes down instead of sideways.")),
	flag(3, "no_shoot", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Meant to make it open only from a trigger; the original game code does not enforce it.")),
	flag(4, "always_shoot", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Stays shootable even with a targetname.")),
	brush("func_button", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A button that moves along its angle when pressed, or when shot if it has health, fires its targets and then moves back.")),
	uses(kMoveAngle),
	key("speed", Type::Real, "40", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Movement speed in units per second.")),
	key("wait", Type::Real, "1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds before it moves back; -1 keeps it pressed.")),
	key("lip", Type::Integer, "4", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "How much of it still shows at the end of its move.")),
	key("health", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "If set, shooting it activates it instead of touching it.")),
	key("sounds", Type::Choices, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Sound it makes when pressed.")),
	choice("0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Steam metal")),
	choice("1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Wooden clunk")),
	choice("2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Metallic click")),
	choice("3", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "In-out")),
	uses(kTriggered),
	uses(kFires),
	brush("func_plat", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A lift that waits at the bottom and rises when someone stands on it. With a targetname it waits at the top until triggered once.")),
	key("speed", Type::Real, "150", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Movement speed in units per second.")),
	key("height", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Travel distance; by default its own height minus 8.")),
	key("sounds", Type::Choices, "2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Sounds it makes while moving.")),
	choice("1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Base fast")),
	choice("2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Chain slow")),
	uses(kTriggered),
	flag(0, "PLAT_LOW_TRIGGER", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Starts only when someone stands on it at its lowest point.")),
	brush("func_train", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A moving platform that travels between path_corner entities, starting at the first one it targets; each corner marks where the train's minimum bounding-box corner goes. A named train waits for a trigger before it starts.")),
	needs("target", Type::TargetDestination, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The first path_corner.")),
	key("speed", Type::Real, "100", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Movement speed in units per second.")),
	key("dmg", Type::Integer, "2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage dealt to anything that blocks it.")),
	key("sounds", Type::Choices, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Sounds it makes while moving.")),
	choice("0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "None")),
	choice("1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Ratchet metal")),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name that starts the train when triggered.")),
	brush("func_wall", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A solid brush entity that is not part of the world; triggering it switches its animated textures to their alternate frames.")),
	uses(kTriggered),
	brush("func_illusionary", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Looks solid but has no collision: anything passes through it.")),
	// Classes the compiler folds into the world, so the progs never see them.
	brush("func_group", {0, 0, 0}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Groups brushes for editing convenience; the compiler merges them into the world.")),
	brush("func_detail", {0.5f, 0.5f, 0.9f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Solid detail: the compiler merges these brushes into the world without letting them seal the map or split its visibility.")),
	brush("func_detail_illusionary", {0.5f, 0.5f, 0.9f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Detail with no collision that does not split world faces, for vines and foliage.")),
	brush("func_detail_wall", {0.5f, 0.5f, 0.9f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Solid detail that does not split the world faces it touches.")),
	brush("func_detail_fence", {0.5f, 0.5f, 0.9f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Solid detail for fence and grate textures that never clips world faces away.")),
	brush("func_episodegate", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A barrier that appears once a chosen episode is complete, so players cannot enter it again.")),
	flag(0, "E1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Appears once episode 1 is complete.")),
	flag(1, "E2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Appears once episode 2 is complete.")),
	flag(2, "E3", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Appears once episode 3 is complete.")),
	flag(3, "E4", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Appears once episode 4 is complete.")),
	brush("func_bossgate", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A barrier that blocks the way to the final boss until all four episode runes have been collected.")),

	// Triggers.
	brush("trigger_multiple", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An invisible trigger brush that fires its targets each time it is touched, at most once per wait.")),
	key("wait", Type::Real, "0.2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds before it can fire again.")),
	uses(kQuakeTouchTrigger),
	brush("trigger_once", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An invisible trigger brush that fires its targets once and then removes itself.")),
	uses(kQuakeTouchTrigger),
	point("trigger_relay", {0.5f, 0.5f, 0.5f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A relay with no size: when triggered it fires its own targets, optionally after a delay.")),
	uses(kTriggered),
	uses(kFires),
	brush("trigger_secret", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Counts a found secret when a player first touches it, then fires its targets and removes itself.")),
	key("health", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "If set, shooting it activates it instead of touching it.")),
	key("sounds", Type::Choices, "1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Sound it plays when it fires.")),
	choice("1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Secret")),
	choice("2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Beep beep")),
	uses(kTriggered),
	uses(kFires),
	byDefault("message", "You found a secret area!"),
	brush("trigger_counter", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires its targets once it has been triggered count times, showing a countdown unless nomessage is set.")),
	key("count", Type::Integer, "2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Number of triggers it needs.")),
	uses(kTriggered),
	uses(kFires),
	flag(0, "nomessage", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Hides the countdown messages.")),
	brush("trigger_teleport", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Teleports whatever touches it to the info_teleport_destination it targets. With a targetname it works only briefly after each trigger.")),
	needs("target", Type::TargetDestination, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The info_teleport_destination to send things to.")),
	uses(kTriggered),
	flag(0, "PLAYER_ONLY", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Teleports only players.")),
	flag(1, "SILENT", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Leaves out the teleporter's hum.")),
	brush("trigger_changelevel", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Ends the level when a player touches it and loads the named map.")),
	needs("map", Type::String, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name of the map to load next, such as e1m2.")),
	uses(kFires),
	flag(0, "NO_INTERMISSION", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Skips the statistics screen in single player.")),
	brush("trigger_setskill", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Sets the skill level when a player touches it; used on the start map.")),
	key("message", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Skill level to set: 0 easy, 1 normal, 2 hard, 3 nightmare.")),
	brush("trigger_onlyregistered", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires its targets when touched in the registered game; in the shareware version it only shows its message.")),
	key("message", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Text shown in the shareware version.")),
	uses(kFires),
	brush("trigger_hurt", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Hurts anything inside it, at most once a second.")),
	key("dmg", Type::Integer, "5", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage per hit.")),
	brush("trigger_push", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Pushes players and objects along its angle, like a wind tunnel.")),
	key("speed", Type::Real, "1000", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Push speed.")),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Direction of the push, in degrees; -1 is up and -2 is down.")),
	flag(0, "PUSH_ONCE", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Removes itself after the first push.")),
	brush("trigger_monsterjump", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Makes walking monsters that touch it jump along its angle.")),
	key("speed", Type::Real, "200", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Forward speed of the jump.")),
	key("height", Type::Real, "200", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Upward speed of the jump.")),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Direction of the jump, in degrees.")),

	// Paths, sounds and the rest.
	point("path_corner", {0.5f, 0.3f, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A waypoint for patrolling monsters and trains; each one targets the next.")),
	needs("targetname", Type::TargetSource, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name the previous corner, train or monster targets.")),
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The next path_corner.")),
	key("wait", Type::Real, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds a train waits here.")),
	point("ambient_comp_hum", {0.3f, 0.1f, 0.6f}, {{-10, -10, -8}, {10, 10, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Static ambient sound: computer hum.")),
	point("ambient_drone", {0.3f, 0.1f, 0.6f}, {{-10, -10, -8}, {10, 10, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Static ambient sound: low drone.")),
	point("ambient_suck_wind", {0.3f, 0.1f, 0.6f}, {{-10, -10, -8}, {10, 10, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Static ambient sound: rushing wind.")),
	point("ambient_drip", {0.3f, 0.1f, 0.6f}, {{-10, -10, -8}, {10, 10, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Static ambient sound: dripping water.")),
	point("ambient_flouro_buzz", {0.3f, 0.1f, 0.6f}, {{-10, -10, -8}, {10, 10, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Static ambient sound: fluorescent light buzz.")),
	point("ambient_light_buzz", {0.3f, 0.1f, 0.6f}, {{-10, -10, -8}, {10, 10, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Static ambient sound: electric light buzz.")),
	point("ambient_swamp1", {0.3f, 0.1f, 0.6f}, {{-10, -10, -8}, {10, 10, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Static ambient sound: swamp.")),
	point("ambient_swamp2", {0.3f, 0.1f, 0.6f}, {{-10, -10, -8}, {10, 10, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Static ambient sound: a second swamp variation.")),
	point("ambient_thunder", {0.3f, 0.1f, 0.6f}, {{-10, -10, -8}, {10, 10, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Static ambient sound: rumbling thunder.")),
	point("misc_fireball", {0, 0.5f, 0.8f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Throws lava balls into the air every few seconds; they burn whatever they hit.")),
	key("speed", Type::Real, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Upward speed of each ball, before a random extra.")),
	point("misc_explobox", {0, 0.5f, 0.8f}, {{0, 0, 0}, {32, 32, 64}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An exploding barrel that blows up when shot."), "maps/b_explob.bsp"),
	point("misc_explobox2", {0, 0.5f, 0.8f}, {{0, 0, 0}, {32, 32, 64}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A smaller exploding barrel (registered version only)."), "maps/b_exbox2.bsp"),
	point("misc_teleporttrain", {0, 0.5f, 0.8f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The spinning teleporter sphere from the final level, which travels between path_corner entities like a train."), "progs/teleport.mdl"),
	needs("target", Type::TargetDestination, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The first path_corner.")),
	key("speed", Type::Real, "100", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Movement speed in units per second.")),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name that starts the train when triggered.")),
	point("misc_noisemaker", {1, 0.5f, 0}, {{-10, -10, -10}, {10, 10, 10}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A test entity that keeps starting sounds, for profiling; not for real maps.")),
	point("trap_spikeshooter", {0, 0.5f, 0.8f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires a nail along its angle each time it is triggered.")),
	uses(kQuakeShooter),
	point("trap_shooter", {0, 0.5f, 0.8f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires nails along its angle on a repeating timer.")),
	key("wait", Type::Real, "1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds between shots.")),
	key("nextthink", Type::Real, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Delay before the first shot, to stagger several shooters.")),
	uses(kQuakeShooter),
	point("air_bubbles", {0, 0.5f, 0.8f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Releases a steady stream of bubbles under water; removed in deathmatch.")),
	point("viewthing", {0, 0.5f, 0.8f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A debugging model viewer; not for real maps."), "progs/player.mdl"),
	point("event_lightning", {0, 1, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Part of the Chthon fight: when triggered, fires lightning between the two func_door electrodes whose target is lightning, hurting the boss.")),
	uses(kTriggered),
};

// ---------------------------------------------------------------------------
// Quake II
// ---------------------------------------------------------------------------

constexpr Row kQuake2MonsterKeys[] = {
	uses(kFacing),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name that triggers use to make the monster appear (with Trigger_Spawn) or to wake it.")),
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A path_corner to walk to, or a point_combat to head for when it wakes.")),
	key("combattarget", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A point_combat the monster runs to when it first gets angry.")),
	key("deathtarget", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Entities triggered when the monster dies.")),
	key("killtarget", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Entities removed when the monster dies, if it also has a target or deathtarget.")),
	key("item", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Class name of an item the monster drops when killed, such as ammo_bullets.")),
};

constexpr Row kQuake2Monster[] = {
	uses(kQuake2MonsterKeys),
	flag(0, "Ambush", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Ignores noises and other monsters' alerts; wakes only on seeing the player.")),
	flag(1, "Trigger_Spawn", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Absent until triggered, then appears.")),
	flag(2, "Sight", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The game code turns this into Ambush.")),
};

constexpr Row kQuake2Pickup[] = {
	uses(kFires),
	key("team", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Items with the same team name take turns: only one is present at a time, picked at random.")),
};

constexpr Row kQuake2Accel[] = {
	key("accel", Type::Real, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Acceleration; the speed by default.")),
	key("decel", Type::Real, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Deceleration; the speed by default.")),
};

constexpr Row kQuake2Animated[] = {
	flag(0, "TRIGGER_SPAWN", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Absent until triggered, then appears.")),
	flag(1, "ANIMATED", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Animates its textures.")),
	flag(2, "ANIMATED_FAST", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Animates its textures quickly.")),
};

// The keys and flags func_door and func_door_rotating share.
constexpr Row kQuake2Door[] = {
	uses(kQuake2Accel),
	key("wait", Type::Real, "3", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds before it closes again; -1 keeps it open.")),
	key("dmg", Type::Integer, "2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage dealt to anything that blocks it.")),
	key("health", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "If set, shooting it activates it instead of touching it.")),
	key("message", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Text shown to a player who touches it while it waits to be triggered elsewhere.")),
	key("team", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Doors with the same team name move together.")),
	key("sounds", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "1 makes it silent; any other value plays the usual sounds.")),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name that buttons and triggers use to open it; a named door has no touch trigger of its own.")),
	uses(kFires),
	flag(0, "START_OPEN", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Spawns open and works in reverse, so triggering it closes the way; it is lit in its closed position.")),
	flag(2, "CRUSHER", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Keeps crushing whatever blocks it instead of reversing.")),
	flag(3, "NOMONSTER", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Monsters cannot open it.")),
	flag(4, "ANIMATED", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Animates its textures.")),
	flag(5, "TOGGLE", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Waits at each end for another trigger instead of moving back by itself.")),
};

constexpr Row kQuake2CrossLevelFlags[] = {
	flag(0, "trigger1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Cross-level trigger 1.")),
	flag(1, "trigger2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Cross-level trigger 2.")),
	flag(2, "trigger3", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Cross-level trigger 3.")),
	flag(3, "trigger4", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Cross-level trigger 4.")),
	flag(4, "trigger5", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Cross-level trigger 5.")),
	flag(5, "trigger6", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Cross-level trigger 6.")),
	flag(6, "trigger7", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Cross-level trigger 7.")),
	flag(7, "trigger8", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Cross-level trigger 8.")),
};

// target_secret and target_goal.
constexpr Row kQuake2Counted[] = {
	key("noise", Type::Sound, "misc/secret.wav", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Sound played when it is triggered.")),
	uses(kTriggered),
	uses(kFires),
};

constexpr Row kQuake2Rows[] = {
	brush("worldspawn", {0, 0, 0}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The world itself: every map has exactly one, holding the level's settings, and its brushes form the level's structure.")),
	key("message", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Level name shown to players.")),
	key("sky", Type::String, "unit1_", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name of the sky environment map.")),
	key("skyaxis", Type::Vector, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Axis the sky turns around.")),
	key("skyrotate", Type::Real, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Sky rotation speed in degrees per second.")),
	key("sounds", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "CD audio track to play.")),
	key("gravity", Type::Real, "800", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Gravity strength.")),
	key("nextmap", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Map to load after this one in deathmatch.")),

	// Player starts and markers.
	point("info_player_start", {1, 0, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Where the player starts in single player; with a targetname it is the arrival point a target_changelevel can name.")),
	uses(kFacing),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Spawn point name that a target_changelevel can ask for after a $ in its map.")),
	point("info_player_deathmatch", {1, 0, 1}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A deathmatch spawn point; place several.")),
	uses(kFacing),
	point("info_player_coop", {1, 0, 1}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A co-operative spawn point; a targetname ties it to a named arrival point.")),
	uses(kFacing),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Spawn point name that a target_changelevel can ask for after a $ in its map.")),
	point("info_player_intermission", {1, 0, 1}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A camera for the intermission screen between levels.")),
	key("angles", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "View direction as pitch, yaw and roll.")),
	point("info_null", {0, 0.5f, 0}, {{-4, -4, -4}, {4, 4, 4}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A position for the compilers to aim at, such as a spotlight target; removed when the level starts.")),
	uses(kAimPoint),
	point("info_notnull", {0, 0.5f, 0}, {{-4, -4, -4}, {4, 4, 4}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A position marker that stays in the game, for entities that aim at a point.")),
	uses(kAimPoint),

	// Lights. The light compiler treats every class whose name starts with
	// "light" as a light source.
	point("light", {0, 1, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An invisible light that the light compiler bakes into the level. Give it a targetname to make it switchable in single player.")),
	key("light", Type::Integer, "300", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Brightness the light compiler gives it.")),
	key("_color", Type::Color, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Light colour as red, green and blue; the compiler normalises it.")),
	uses(kLightStyle),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name that makes the light switchable; each trigger toggles it.")),
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An entity to aim at, which makes this a spotlight.")),
	key("_cone", Type::Real, "10", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Spotlight cone size in degrees, measured from its centre line.")),
	uses(kStartOff),
	point("light_mine1", {0, 1, 0}, {{-2, -2, -12}, {2, 2, 12}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A mine light fixture model. Its class name starts with light, so the light compiler also bakes a light here."), "models/objects/minelite/light1/tris.md2"),
	point("light_mine2", {0, 1, 0}, {{-2, -2, -12}, {2, 2, 12}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A second mine light fixture model. Its class name starts with light, so the light compiler also bakes a light here."), "models/objects/minelite/light2/tris.md2"),

	// Armour, powerups and health.
	point("item_armor_body", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Body armour, the heaviest: 100 points, up to 200."), "models/items/armor/body/tris.md2"),
	uses(kQuake2Pickup),
	point("item_armor_combat", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Combat armour: 50 points, up to 100."), "models/items/armor/combat/tris.md2"),
	uses(kQuake2Pickup),
	point("item_armor_jacket", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Jacket armour, the lightest: 25 points, up to 50."), "models/items/armor/jacket/tris.md2"),
	uses(kQuake2Pickup),
	point("item_armor_shard", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Armour shard: adds 2 points to any armour."), "models/items/armor/shard/tris.md2"),
	uses(kQuake2Pickup),
	point("item_power_screen", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Power screen: uses cells to block damage from the front."), "models/items/armor/screen/tris.md2"),
	uses(kQuake2Pickup),
	point("item_power_shield", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Power shield: uses cells to block damage from every side."), "models/items/armor/shield/tris.md2"),
	uses(kQuake2Pickup),
	point("item_quad", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Quad Damage: 30 seconds of four times the damage."), "models/items/quaddama/tris.md2"),
	uses(kQuake2Pickup),
	point("item_invulnerability", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Invulnerability: 30 seconds of immunity to damage."), "models/items/invulner/tris.md2"),
	uses(kQuake2Pickup),
	point("item_silencer", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Silencer: muffles the next 30 shots."), "models/items/silencer/tris.md2"),
	uses(kQuake2Pickup),
	point("item_breather", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Rebreather: 30 seconds of air under water."), "models/items/breather/tris.md2"),
	uses(kQuake2Pickup),
	point("item_enviro", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Environment suit: 30 seconds of air under water, no slime damage and much less lava damage."), "models/items/enviro/tris.md2"),
	uses(kQuake2Pickup),
	point("item_ancient_head", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Ancient head: permanently raises maximum health by 2."), "models/items/c_head/tris.md2"),
	uses(kQuake2Pickup),
	point("item_adrenaline", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Adrenaline: restores full health, and outside deathmatch raises maximum health by 1."), "models/items/adrenal/tris.md2"),
	uses(kQuake2Pickup),
	point("item_bandolier", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Bandolier: raises the ammunition limits and adds some bullets and shells."), "models/items/band/tris.md2"),
	uses(kQuake2Pickup),
	point("item_pack", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Ammo pack: raises every ammunition limit and adds some of each type."), "models/items/pack/tris.md2"),
	uses(kQuake2Pickup),
	point("item_health", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Medkit worth 10 health."), "models/items/healing/medium/tris.md2"),
	uses(kQuake2Pickup),
	point("item_health_small", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Stimpack worth 2 health, even past the maximum."), "models/items/healing/stimpack/tris.md2"),
	uses(kQuake2Pickup),
	point("item_health_large", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Large medkit worth 25 health."), "models/items/healing/large/tris.md2"),
	uses(kQuake2Pickup),
	point("item_health_mega", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Mega health: 100 health past the maximum; the extra wears off over time."), "models/items/mega_h/tris.md2"),
	uses(kQuake2Pickup),

	// Ammunition and weapons.
	point("ammo_shells", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Shells for the shotgun and super shotgun."), "models/items/ammo/shells/medium/tris.md2"),
	uses(kQuake2Pickup),
	point("ammo_bullets", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Bullets for the machine gun and chaingun."), "models/items/ammo/bullets/medium/tris.md2"),
	uses(kQuake2Pickup),
	point("ammo_cells", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Cells for the HyperBlaster, the BFG10K and power armour."), "models/items/ammo/cells/medium/tris.md2"),
	uses(kQuake2Pickup),
	point("ammo_rockets", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Rockets for the rocket launcher."), "models/items/ammo/rockets/medium/tris.md2"),
	uses(kQuake2Pickup),
	point("ammo_slugs", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Slugs for the railgun."), "models/items/ammo/slugs/medium/tris.md2"),
	uses(kQuake2Pickup),
	point("ammo_grenades", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Hand grenades, which are also the grenade launcher's ammunition."), "models/items/ammo/grenades/medium/tris.md2"),
	uses(kQuake2Pickup),
	point("weapon_shotgun", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the shotgun."), "models/weapons/g_shotg/tris.md2"),
	uses(kQuake2Pickup),
	point("weapon_supershotgun", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the super shotgun."), "models/weapons/g_shotg2/tris.md2"),
	uses(kQuake2Pickup),
	point("weapon_machinegun", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the machine gun."), "models/weapons/g_machn/tris.md2"),
	uses(kQuake2Pickup),
	point("weapon_chaingun", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the chaingun."), "models/weapons/g_chain/tris.md2"),
	uses(kQuake2Pickup),
	point("weapon_grenadelauncher", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the grenade launcher."), "models/weapons/g_launch/tris.md2"),
	uses(kQuake2Pickup),
	point("weapon_rocketlauncher", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the rocket launcher."), "models/weapons/g_rocket/tris.md2"),
	uses(kQuake2Pickup),
	point("weapon_hyperblaster", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the HyperBlaster."), "models/weapons/g_hyperb/tris.md2"),
	uses(kQuake2Pickup),
	point("weapon_railgun", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the railgun."), "models/weapons/g_rail/tris.md2"),
	uses(kQuake2Pickup),
	point("weapon_bfg", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the BFG10K."), "models/weapons/g_bfg/tris.md2"),
	uses(kQuake2Pickup),

	// Key items; trigger_key checks for them.
	point("key_data_cd", {0, 0.5f, 0.8f}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Key item: the data CD, used at computer centres."), "models/items/keys/data_cd/tris.md2"),
	uses(kQuake2Pickup),
	point("key_power_cube", {0, 0.5f, 0.8f}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Key item: a power cube for powering circuits. The only item that accepts spawnflags."), "models/items/keys/power/tris.md2"),
	uses(kQuake2Pickup),
	flag(0, "TRIGGER_SPAWN", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Absent until triggered, then appears.")),
	flag(1, "NO_TOUCH", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Cannot be picked up.")),
	point("key_pyramid", {0, 0.5f, 0.8f}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Key item: the pyramid key, used at a jail entrance."), "models/items/keys/pyramid/tris.md2"),
	uses(kQuake2Pickup),
	point("key_data_spinner", {0, 0.5f, 0.8f}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Key item: the data spinner, used with the city computer."), "models/items/keys/spinner/tris.md2"),
	uses(kQuake2Pickup),
	point("key_pass", {0, 0.5f, 0.8f}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Key item: the security pass."), "models/items/keys/pass/tris.md2"),
	uses(kQuake2Pickup),
	point("key_blue_key", {0, 0.5f, 0.8f}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Key item: the blue key for ordinary locked doors."), "models/items/keys/key/tris.md2"),
	uses(kQuake2Pickup),
	point("key_red_key", {0, 0.5f, 0.8f}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Key item: the red key for ordinary locked doors."), "models/items/keys/red_key/tris.md2"),
	uses(kQuake2Pickup),
	point("key_commander_head", {0, 0.5f, 0.8f}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Key item: the tank commander's head."), "models/monsters/commandr/head/tris.md2"),
	uses(kQuake2Pickup),
	point("key_airstrike_target", {0, 0.5f, 0.8f}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Key item: the airstrike marker."), "models/items/keys/target/tris.md2"),
	uses(kQuake2Pickup),

	// Brush entities.
	brush("func_areaportal", {0, 0, 0}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Splits the level into areas the engine can hide from each other. Put it inside a door and make it the door's target, so the areas join only while the door is open.")),
	uses(kTriggered),
	brush("func_button", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A button that moves along its angle when pressed, or when shot if it has health, fires its targets and then moves back.")),
	uses(kMoveAngle),
	key("speed", Type::Real, "40", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Movement speed in units per second.")),
	uses(kQuake2Accel),
	key("wait", Type::Real, "3", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds before it moves back; -1 keeps it pressed.")),
	key("lip", Type::Integer, "4", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "How much of it still shows at the end of its move.")),
	key("health", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "If set, shooting it activates it instead of touching it.")),
	key("sounds", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "1 makes it silent; any other value plays the usual sounds.")),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name that lets other entities press it; a named button ignores touch.")),
	uses(kFires),
	brush("func_door", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A sliding door. Doors that share a team move together; a door with a targetname or health opens only when triggered or shot.")),
	uses(kMoveAngle),
	key("speed", Type::Real, "100", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Movement speed in units per second; doubled in deathmatch.")),
	key("lip", Type::Integer, "8", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "How much of it still shows at the end of its move.")),
	uses(kQuake2Door),
	flag(6, "ANIMATED_FAST", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Animates its textures quickly.")),
	brush("func_door_rotating", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A door that swings around its origin brush, about the vertical axis unless X_AXIS or Y_AXIS is set.")),
	key("distance", Type::Integer, "90", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "How far it turns, in degrees.")),
	key("speed", Type::Real, "100", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Turning speed in degrees per second.")),
	uses(kQuake2Door),
	flag(1, "REVERSE", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Turns the other way.")),
	flag(6, "X_AXIS", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Turns about the X axis.")),
	flag(7, "Y_AXIS", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Turns about the Y axis.")),
	brush("func_door_secret", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A secret door that moves in two steps: first sideways (or down), then along its angle. Without a targetname, shooting it opens it.")),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Direction of the second move, in degrees.")),
	key("wait", Type::Real, "5", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds before it closes again.")),
	key("dmg", Type::Integer, "2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage dealt to anything that blocks it.")),
	key("message", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Text shown to a player who touches it while it waits to be triggered elsewhere.")),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name of the button or trigger that opens it; a named door ignores shots unless always_shoot is set.")),
	uses(kFires),
	flag(0, "always_shoot", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Stays shootable even with a targetname.")),
	flag(1, "1st_left", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The first move goes left of the angle instead of right.")),
	flag(2, "1st_down", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The first move goes down instead of sideways.")),
	brush("func_plat", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A lift that waits at the bottom and rises when someone stands on it. With a targetname it waits at the top until triggered once.")),
	key("speed", Type::Real, "200", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Movement speed in units per second.")),
	key("accel", Type::Real, "50", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Acceleration.")),
	key("decel", Type::Real, "50", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Deceleration.")),
	key("lip", Type::Integer, "8", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "How much of it still shows at the end of its move.")),
	key("height", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Travel distance; by default its own height minus the lip.")),
	key("dmg", Type::Integer, "2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage dealt to anything that blocks it.")),
	uses(kTriggered),
	flag(0, "PLAT_LOW_TRIGGER", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Starts only when someone stands on it at its lowest point.")),
	brush("func_rotating", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A brush that spins around its origin brush, about the vertical axis unless X_AXIS or Y_AXIS is set.")),
	key("speed", Type::Real, "100", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Turning speed in degrees per second.")),
	key("dmg", Type::Integer, "2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage dealt to anything that blocks it.")),
	uses(kTriggered),
	flag(0, "START_ON", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Spins from the start instead of waiting for a trigger.")),
	flag(1, "REVERSE", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Turns the other way.")),
	flag(2, "X_AXIS", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Turns about the X axis.")),
	flag(3, "Y_AXIS", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Turns about the Y axis.")),
	flag(4, "TOUCH_PAIN", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Hurts anything that touches it while it spins.")),
	flag(5, "STOP", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Stops when blocked instead of pushing.")),
	flag(6, "ANIMATED", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Animates its textures.")),
	flag(7, "ANIMATED_FAST", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Animates its textures quickly.")),
	brush("func_train", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A moving platform that travels between path_corner entities, starting at the first one it targets; each corner marks where the train's minimum bounding-box corner goes. A named train waits for a trigger before it starts.")),
	needs("target", Type::TargetDestination, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The first path_corner.")),
	key("speed", Type::Real, "100", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Movement speed in units per second.")),
	key("dmg", Type::Integer, "100", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage dealt to anything that blocks it.")),
	key("noise", Type::Sound, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Looping sound while it moves.")),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name that starts the train when triggered.")),
	flag(0, "START_ON", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Starts moving at once, even with a targetname.")),
	flag(1, "TOGGLE", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Triggering it again stops it.")),
	flag(2, "BLOCK_STOPS", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Stops when blocked instead of hurting the blocker.")),
	brush("func_water", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A movable body of water that rises or falls when triggered; give it water or lava textures.")),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Direction it moves: -1 for up or -2 for down.")),
	key("speed", Type::Real, "25", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Movement speed in units per second.")),
	key("wait", Type::Real, "-1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds before it moves back; -1 makes each trigger toggle it.")),
	key("lip", Type::Integer, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "How much of it still shows at the end of its move.")),
	key("sounds", Type::Choices, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Sounds it makes while moving.")),
	choice("0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "None")),
	choice("1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Water")),
	choice("2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Lava")),
	uses(kTriggered),
	flag(0, "START_OPEN", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Spawns at its destination and works in reverse.")),
	brush("func_wall", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A solid brush entity that is not part of the world. With TRIGGER_SPAWN it appears only when triggered, killing anything in its way.")),
	uses(kTriggered),
	flag(0, "TRIGGER_SPAWN", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Absent until triggered, then appears.")),
	flag(1, "TOGGLE", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Each trigger makes it appear or vanish (with TRIGGER_SPAWN).")),
	flag(2, "START_ON", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Present from the start (with TRIGGER_SPAWN and TOGGLE).")),
	flag(3, "ANIMATED", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Animates its textures.")),
	flag(4, "ANIMATED_FAST", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Animates its textures quickly.")),
	brush("func_object", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A solid brush entity that falls when whatever supports it is removed, crushing what it lands on.")),
	key("dmg", Type::Integer, "100", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage dealt to whatever it lands on.")),
	uses(kTriggered),
	uses(kQuake2Animated),
	brush("func_explosive", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A brush that bursts into debris when destroyed; removed in deathmatch. With a targetname it explodes when triggered instead of when shot.")),
	key("health", Type::Integer, "100", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage it takes before breaking.")),
	key("dmg", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Radius damage of the explosion; none if unset.")),
	key("mass", Type::Integer, "75", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Amount of debris: one large chunk per 100 and one small chunk per 25.")),
	uses(kTriggered),
	uses(kFires),
	flag(0, "Trigger_Spawn", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Absent until triggered, then appears and can be shot.")),
	uses(kQuake2Animated),
	point("func_timer", {0.3f, 0.1f, 0.6f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires its targets repeatedly at a varying interval; triggering it turns it on or off.")),
	key("wait", Type::Real, "1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Base seconds between firings.")),
	key("random", Type::Real, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Random variation, in seconds either way.")),
	key("pausetime", Type::Real, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Extra delay before the very first firing, used only with START_ON.")),
	uses(kTriggered),
	uses(kFires),
	flag(0, "START_ON", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Runs from the start.")),
	brush("func_conveyor", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A stationary brush that carries whatever stands on it; its faces need a current content flag. Triggering it starts or stops it.")),
	key("speed", Type::Real, "100", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Conveyor speed.")),
	uses(kTriggered),
	flag(0, "START_ON", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Runs from the start.")),
	flag(1, "TOGGLE", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Each trigger starts or stops it; without it, it cannot restart once stopped.")),
	brush("func_killbox", {1, 0, 0}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Kills everything inside it when triggered, whatever protection they have.")),
	uses(kTriggered),
	brush("func_group", {0, 0, 0}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Groups brushes for editing convenience; the compiler merges them into the world.")),
	point("func_clock", {0, 0, 1}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Drives a target_string with the time of day, or with a timer counting up or down.")),
	needs("target", Type::TargetDestination, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The target_string that shows the time.")),
	key("count", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds to count; required for TIMER_DOWN, an hour by default for TIMER_UP.")),
	key("pathtarget", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Entities triggered when the timer runs out.")),
	key("style", Type::Choices, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "How the time is written.")),
	choice("0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds (xx)")),
	choice("1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Minutes and seconds (xx:xx)")),
	choice("2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Hours, minutes and seconds (xx:xx:xx)")),
	uses(kTriggered),
	flag(0, "TIMER_UP", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Counts up from zero.")),
	flag(1, "TIMER_DOWN", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Counts down to zero.")),
	flag(2, "START_OFF", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Waits for a trigger before it starts.")),
	flag(3, "MULTI_USE", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Can be started again after it runs out.")),

	// Triggers.
	brush("trigger_multiple", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An invisible trigger brush that fires its targets each time it is touched, at most once per wait.")),
	key("wait", Type::Real, "0.2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds before it can fire again.")),
	uses(kTriggerSounds),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "If set, it fires only for someone facing this way; use 360 for east.")),
	uses(kTriggered),
	uses(kFires),
	flag(0, "MONSTER", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Monsters can set it off too.")),
	flag(1, "NOT_PLAYER", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Players cannot set it off.")),
	flag(2, "TRIGGERED", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Inactive until triggered once.")),
	brush("trigger_once", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An invisible trigger brush that fires its targets once and then removes itself.")),
	uses(kTriggerSounds),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "If set, it fires only for someone facing this way; use 360 for east.")),
	uses(kTriggered),
	uses(kFires),
	flag(1, "NOT_PLAYER", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Players cannot set it off.")),
	flag(2, "TRIGGERED", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Inactive until triggered once.")),
	point("trigger_relay", {0.5f, 0.5f, 0.5f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A relay with no size: when triggered it fires its own targets, optionally after a delay.")),
	uses(kTriggered),
	uses(kFires),
	point("trigger_key", {0.5f, 0.5f, 0.5f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A relay that fires its targets only for a player carrying the right key item, which it then takes.")),
	needs("item", Type::String, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Class name of the key the player must carry, such as key_blue_key.")),
	needs("target", Type::TargetDestination, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Entities triggered once the key is used.")),
	uses(kTriggered),
	uses(kFires),
	brush("trigger_counter", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires its targets once it has been triggered count times, showing a countdown unless nomessage is set.")),
	key("count", Type::Integer, "2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Number of triggers it needs.")),
	uses(kTriggered),
	uses(kFires),
	flag(0, "nomessage", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Hides the countdown messages.")),
	point("trigger_always", {0.5f, 0.5f, 0.5f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires its targets once when the level starts, after at least 0.2 seconds.")),
	uses(kFires),
	byDefault("delay", "0.2"),
	brush("trigger_push", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Pushes players and objects along its angle, like a wind tunnel.")),
	key("speed", Type::Real, "1000", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Push speed.")),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Direction of the push, in degrees; -1 is up and -2 is down.")),
	flag(0, "PUSH_ONCE", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Removes itself after the first push.")),
	brush("trigger_hurt", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Hurts anything inside it every frame, or once a second with SLOW.")),
	key("dmg", Type::Integer, "5", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage per hit.")),
	uses(kTriggered),
	flag(0, "START_OFF", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Starts switched off.")),
	flag(1, "TOGGLE", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Each trigger switches it on or off.")),
	flag(2, "SILENT", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Makes no sound.")),
	flag(3, "NO_PROTECTION", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Nothing protects against the damage.")),
	flag(4, "SLOW", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Hurts once a second instead of every frame.")),
	brush("trigger_gravity", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Changes the gravity of anything touching it.")),
	needs("gravity", Type::Real, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Gravity multiplier, 1 being normal; the game keeps only the whole-number part.")),
	brush("trigger_monsterjump", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Makes walking monsters that touch it jump along its angle.")),
	key("speed", Type::Real, "200", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Forward speed of the jump.")),
	key("height", Type::Integer, "200", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Upward speed of the jump.")),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Direction of the jump, in degrees.")),
	point("trigger_elevator", {0.3f, 0.1f, 0.6f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Sends the func_train it targets to the path_corner named by the pathtarget of whatever triggered it, for lifts that serve several floors.")),
	needs("target", Type::TargetDestination, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The func_train to move.")),
	uses(kTriggered),

	// Targets.
	point("target_temp_entity", {1, 0, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Shows a temporary effect at its position when triggered.")),
	key("style", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Number of the temporary effect.")),
	uses(kTriggered),
	point("target_speaker", {1, 0, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Plays a sound when triggered, or loops one that triggers switch on and off.")),
	needs("noise", Type::Sound, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Sound file to play; .wav is added when missing.")),
	key("volume", Type::Real, "1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Volume from 0 to 1.")),
	key("attenuation", Type::Real, "1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "How fast it fades with distance: -1 heard everywhere, 1 normal, 2 idle, 3 static.")),
	uses(kTriggered),
	flag(0, "looped-on", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Loops, starting on.")),
	flag(1, "looped-off", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Loops, starting off.")),
	flag(2, "reliable", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Always delivered, for important voice-overs.")),
	point("target_help", {1, 0, 1}, {{-16, -16, -24}, {16, 16, 24}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Changes the help computer's message and flashes its icon when triggered; removed in deathmatch.")),
	needs("message", Type::String, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The new help computer message.")),
	uses(kTriggered),
	flag(0, "help1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Sets the main help message instead of the second one.")),
	point("target_secret", {1, 0, 1}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Counts a found secret when triggered; it works once.")),
	uses(kQuake2Counted),
	point("target_goal", {1, 0, 1}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Counts a completed goal when triggered; it works once.")),
	uses(kQuake2Counted),
	point("target_explosion", {1, 0, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Makes an explosion when triggered.")),
	key("delay", Type::Real, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds to wait before exploding.")),
	key("dmg", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Radius damage of the explosion; none if unset.")),
	uses(kTriggered),
	point("target_changelevel", {1, 0, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Ends the level and loads another when triggered.")),
	needs("map", Type::String, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Map to load; add $ and a name to arrive at the spawn point with that targetname.")),
	uses(kTriggered),
	point("target_splash", {1, 0, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Sprays a burst of particles along its angle when triggered.")),
	key("sounds", Type::Choices, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Kind of splash.")),
	choice("1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Sparks")),
	choice("2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Blue water")),
	choice("3", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Brown water")),
	choice("4", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Slime")),
	choice("5", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Lava")),
	choice("6", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Blood")),
	key("count", Type::Integer, "32", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Number of particles.")),
	key("dmg", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Radius damage where it splashes; none if unset.")),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Firing direction, in degrees; -1 is up and -2 is down.")),
	uses(kTriggered),
	point("target_spawner", {1, 0, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Creates an entity of the class named by its target when triggered, such as the monsters and gibs of the factory levels.")),
	needs("target", Type::String, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Class name of the entity to create, such as monster_infantry.")),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Facing of a created monster, or the direction to throw gibs.")),
	key("speed", Type::Real, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Speed to throw created gibs at; unset, they are dropped.")),
	uses(kTriggered),
	point("target_blaster", {1, 0, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires a blaster bolt along its angle when triggered.")),
	key("dmg", Type::Integer, "15", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage per bolt.")),
	key("speed", Type::Real, "1000", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Bolt speed.")),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Firing direction, in degrees; -1 is up and -2 is down.")),
	uses(kTriggered),
	flag(0, "NOTRAIL", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Uses the HyperBlaster effect, which has no trail.")),
	flag(1, "NOEFFECTS", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires the bolt without any visual effect.")),
	point("target_crosslevel_trigger", {0.5f, 0.5f, 0.5f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "When triggered, records its cross-level trigger numbers for target_crosslevel_target entities in later levels of the same unit.")),
	uses(kTriggered),
	uses(kQuake2CrossLevelFlags),
	point("target_crosslevel_target", {0.5f, 0.5f, 0.5f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires its targets as a level of the unit starts, if every cross-level trigger number it checks has been recorded.")),
	uses(kFires),
	byDefault("delay", "1"),
	uses(kQuake2CrossLevelFlags),
	point("target_laser", {0, 0.5f, 0.8f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A laser beam aimed at its target or along its angle; triggering it switches it on or off.")),
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An entity to aim at; otherwise it follows its angle.")),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Firing direction, in degrees; -1 is up and -2 is down.")),
	key("dmg", Type::Integer, "1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage per frame to anything in the beam.")),
	uses(kTriggered),
	flag(0, "START_ON", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Switched on from the start.")),
	flag(1, "RED", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Red beam.")),
	flag(2, "GREEN", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Green beam.")),
	flag(3, "BLUE", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Blue beam.")),
	flag(4, "YELLOW", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Yellow beam.")),
	flag(5, "ORANGE", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Orange beam.")),
	flag(6, "FAT", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A wider beam.")),
	point("target_lightramp", {0, 0.5f, 0.8f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fades a switchable light between two brightness levels when triggered.")),
	needs("message", Type::String, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Two different letters from a to z: the starting and ending brightness.")),
	key("speed", Type::Real, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds the fade takes.")),
	needs("target", Type::TargetDestination, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The switchable light to fade.")),
	uses(kTriggered),
	flag(0, "TOGGLE", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Each trigger fades the other way.")),
	point("target_earthquake", {1, 0, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Shakes every player and monster in the level when triggered.")),
	key("count", Type::Integer, "5", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Duration in seconds.")),
	key("speed", Type::Real, "200", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Severity.")),
	uses(kTriggered),
	brush("target_character", {0, 0, 1}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "One character of a target_string display, made of brushes.")),
	key("team", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Team name shared with its target_string.")),
	key("count", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Position of this character in the string, from 1.")),
	point("target_string", {0, 0, 1}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Shows text across the target_character brushes that share its team.")),
	key("message", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Text to show.")),
	key("team", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Team name shared with its target_character brushes.")),
	uses(kTriggered),
	point("target_actor", {0.5f, 0.3f, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A waypoint for misc_actor characters, which can make them jump or attack on arrival.")),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name the actor or the previous target_actor targets.")),
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The next target_actor.")),
	key("pathtarget", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Entity to attack here, or, without SHOOT or ATTACK, entities triggered on arrival.")),
	key("message", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Line the actor says on arrival.")),
	key("speed", Type::Real, "200", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Forward speed of the jump.")),
	key("height", Type::Integer, "200", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Upward speed of the jump.")),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Direction of the jump, in degrees.")),
	flag(0, "JUMP", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Jumps along its angle on arrival.")),
	flag(1, "SHOOT", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Meant to fire once at the pathtarget; the original game code leaves it empty.")),
	flag(2, "ATTACK", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Attacks the pathtarget until one of them dies.")),
	flag(4, "HOLD", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Stands its ground while attacking.")),
	flag(5, "BRUTAL", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Keeps attacking until the target is gibbed.")),

	// Scenery, effects and special characters.
	point("misc_explobox", {0, 0.5f, 0.8f}, {{-16, -16, 0}, {16, 16, 40}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An exploding barrel that can be pushed about; removed in deathmatch."), "models/objects/barrels/tris.md2"),
	key("mass", Type::Integer, "400", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weight; heavier barrels are harder to push.")),
	key("health", Type::Integer, "10", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage it takes before exploding.")),
	key("dmg", Type::Integer, "150", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Explosion damage.")),
	point("misc_banner", {1, 0.5f, 0}, {{-4, -4, -4}, {4, 4, 4}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A waving banner 128 units tall whose origin is at its bottom."), "models/objects/banner/tris.md2"),
	point("misc_satellite_dish", {1, 0.5f, 0}, {{-64, -64, 0}, {64, 64, 128}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A satellite dish that plays its turning animation when triggered."), "models/objects/satellite/tris.md2"),
	uses(kTriggered),
	point("misc_actor", {1, 0.5f, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A friendly scripted character that walks between target_actor points; removed in deathmatch.")),
	needs("targetname", Type::TargetSource, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name that triggers use to set it walking.")),
	needs("target", Type::TargetDestination, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The first target_actor to walk to.")),
	key("health", Type::Integer, "100", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Starting health.")),
	uses(kFacing),
	point("misc_gib_arm", {1, 0, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A severed arm, meant to be created by a target_spawner."), "models/objects/gibs/arm/tris.md2"),
	point("misc_gib_leg", {1, 0, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A severed leg, meant to be created by a target_spawner."), "models/objects/gibs/leg/tris.md2"),
	point("misc_gib_head", {1, 0, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A severed head, meant to be created by a target_spawner."), "models/objects/gibs/head/tris.md2"),
	point("misc_insane", {1, 0.5f, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An insane marine prisoner: friendly, harmless and moving about; removed in deathmatch."), "models/monsters/insane/tris.md2"),
	uses(kQuake2MonsterKeys),
	flag(0, "Ambush", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Ignores noises and other monsters' alerts; wakes only on seeing the player.")),
	flag(1, "Trigger_Spawn", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Absent until triggered, then appears.")),
	flag(2, "CRAWL", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Crawls on the floor.")),
	flag(3, "CRUCIFIED", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Hangs crucified.")),
	flag(4, "STAND_GROUND", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Stays where it is placed.")),
	flag(5, "ALWAYS_STAND", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Never sits or crawls.")),
	point("misc_deadsoldier", {1, 0.5f, 0}, {{-16, -16, 0}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A dead marine posed as scenery; it can be gibbed. Removed in deathmatch."), "models/deadbods/dude/tris.md2"),
	uses(kFacing),
	flag(0, "ON_BACK", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Lying on its back.")),
	flag(1, "ON_STOMACH", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Lying on its stomach.")),
	flag(2, "BACK_DECAP", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "On its back, headless.")),
	flag(3, "FETAL_POS", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Curled up.")),
	flag(4, "SIT_DECAP", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Sitting, headless.")),
	flag(5, "IMPALED", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Impaled.")),
	point("misc_viper", {1, 0.5f, 0}, {{-16, -16, 0}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A Viper fighter for fly-bys: hidden until triggered, then flies along its path_corner entities."), "models/ships/viper/tris.md2"),
	needs("target", Type::TargetDestination, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The first path_corner.")),
	key("speed", Type::Real, "300", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Flight speed.")),
	uses(kTriggered),
	point("misc_bigviper", {1, 0.5f, 0}, {{-176, -120, -24}, {176, 120, 72}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A large, stationary Viper model."), "models/ships/bigviper/tris.md2"),
	point("misc_viper_bomb", {1, 0, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A bomb dropped from the passing misc_viper when triggered."), "models/objects/bomb/tris.md2"),
	key("dmg", Type::Integer, "1000", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Explosion damage.")),
	uses(kTriggered),
	point("misc_strogg_ship", {1, 0.5f, 0}, {{-16, -16, 0}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A Strogg ship for fly-bys: hidden until triggered, then flies along its path_corner entities."), "models/ships/strogg1/tris.md2"),
	needs("target", Type::TargetDestination, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The first path_corner.")),
	key("speed", Type::Real, "300", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Flight speed.")),
	uses(kTriggered),
	point("misc_teleporter", {1, 0, 0}, {{-32, -32, -24}, {32, 32, -16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A teleporter pad that sends players who step on it to the misc_teleporter_dest it targets."), "models/objects/dmspot/tris.md2"),
	needs("target", Type::TargetDestination, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The misc_teleporter_dest to send players to.")),
	point("misc_teleporter_dest", {1, 0, 0}, {{-32, -32, -24}, {32, 32, -16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The arrival pad for a misc_teleporter."), "models/objects/dmspot/tris.md2"),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name the teleporter's target refers to.")),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Direction a teleported player faces.")),
	point("misc_blackhole", {1, 0.5f, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A swirling black hole effect; triggering it removes it."), "models/objects/black/tris.md2"),
	uses(kTriggered),
	point("misc_eastertank", {1, 0.5f, 0}, {{-32, -32, -16}, {32, 32, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A posed tank model, an easter egg."), "models/monsters/tank/tris.md2"),
	point("misc_easterchick", {1, 0.5f, 0}, {{-32, -32, 0}, {32, 32, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A posed Iron Maiden model, an easter egg."), "models/monsters/bitch/tris.md2"),
	point("misc_easterchick2", {1, 0.5f, 0}, {{-32, -32, 0}, {32, 32, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A second posed Iron Maiden model, an easter egg."), "models/monsters/bitch/tris.md2"),
	point("monster_commander_body", {1, 0.5f, 0}, {{-32, -32, 0}, {32, 32, 48}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The tank commander's headless body: a prop that plays its animation when triggered, usually by picking up key_commander_head."), "models/monsters/commandr/tris.md2"),
	uses(kTriggered),
	brush("turret_breach", {0, 0, 0}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The moving barrel of a turret, turning in pitch and yaw. Team it with a turret_base and let a turret_driver target it.")),
	needs("target", Type::TargetDestination, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An info_notnull at the muzzle, where shots start.")),
	key("speed", Type::Real, "50", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Turning speed in degrees per second.")),
	key("dmg", Type::Integer, "10", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage per shot.")),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Direction it points at the start.")),
	key("minpitch", Type::Real, "-30", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Lowest pitch it aims at, in degrees.")),
	key("maxpitch", Type::Real, "30", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Highest pitch it aims at, in degrees.")),
	key("minyaw", Type::Real, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Start of the yaw range it can turn through, in degrees.")),
	key("maxyaw", Type::Real, "360", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "End of the yaw range it can turn through, in degrees.")),
	key("team", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Team name shared with its turret_base.")),
	brush("turret_base", {0, 0, 0}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The base of a turret, turning in yaw only; it must share a team with the turret_breach.")),
	key("team", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Team name shared with its turret_breach.")),
	point("turret_driver", {1, 0.5f, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The soldier who works a turret: it must target the turret_breach and must not share its team. Removed in deathmatch."), "models/monsters/infantry/tris.md2"),
	needs("target", Type::TargetDestination, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The turret_breach to operate.")),
	key("item", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Class name of an item the monster drops when killed, such as ammo_bullets.")),

	// Paths.
	point("path_corner", {0.5f, 0.3f, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A waypoint for patrolling monsters and trains; each one targets the next.")),
	needs("targetname", Type::TargetSource, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name the previous corner, train or monster targets.")),
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The next path_corner.")),
	key("pathtarget", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Entities triggered when something arrives here.")),
	key("wait", Type::Real, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds a monster or train waits here.")),
	flag(0, "TELEPORT", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Anything heading here arrives instantly instead of travelling.")),
	point("point_combat", {0.5f, 0.3f, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A spot a monster runs to when it first gets angry, before it goes after the player; removed in deathmatch.")),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name a monster's target or combattarget refers to.")),
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The next point_combat to run to.")),
	key("pathtarget", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Entities triggered when the monster arrives.")),
	flag(0, "Hold", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The monster stays here once it arrives.")),

	// Monsters.
	point("monster_soldier_light", {1, 0.5f, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Light guard: a soldier with a blaster."), "models/monsters/soldier/tris.md2"),
	uses(kQuake2Monster),
	point("monster_soldier", {1, 0.5f, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Shotgun guard: a soldier with a shotgun."), "models/monsters/soldier/tris.md2"),
	uses(kQuake2Monster),
	point("monster_soldier_ss", {1, 0.5f, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Machine gun guard: a soldier with a machine gun."), "models/monsters/soldier/tris.md2"),
	uses(kQuake2Monster),
	point("monster_infantry", {1, 0.5f, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Enforcer: a heavier soldier with a chaingun and a melee strike."), "models/monsters/infantry/tris.md2"),
	uses(kQuake2Monster),
	point("monster_gunner", {1, 0.5f, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Gunner: fires a chaingun and grenades."), "models/monsters/gunner/tris.md2"),
	uses(kQuake2Monster),
	point("monster_berserk", {1, 0.5f, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Berserker: a melee brute with a spike and a hammer."), "models/monsters/berserk/tris.md2"),
	uses(kQuake2Monster),
	point("monster_gladiator", {1, 0.5f, 0}, {{-32, -32, -24}, {32, 32, 64}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Gladiator: a heavy walker with a railgun and a claw."), "models/monsters/gladiatr/tris.md2"),
	uses(kQuake2Monster),
	point("monster_medic", {1, 0.5f, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Medic: fights with a blaster and brings dead monsters back to life."), "models/monsters/medic/tris.md2"),
	uses(kQuake2Monster),
	point("monster_mutant", {1, 0.5f, 0}, {{-32, -32, -24}, {32, 32, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Mutant: a leaping melee beast."), "models/monsters/mutant/tris.md2"),
	uses(kQuake2Monster),
	point("monster_parasite", {1, 0.5f, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Parasite: drains health with its tongue."), "models/monsters/parasite/tris.md2"),
	uses(kQuake2Monster),
	point("monster_flyer", {1, 0.5f, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Flyer: a small flying drone with blasters."), "models/monsters/flyer/tris.md2"),
	uses(kQuake2Monster),
	point("monster_hover", {1, 0.5f, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Icarus: a hovering attack craft with a blaster."), "models/monsters/hover/tris.md2"),
	uses(kQuake2Monster),
	point("monster_chick", {1, 0.5f, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Iron Maiden: fires rockets and slashes up close."), "models/monsters/bitch/tris.md2"),
	uses(kQuake2Monster),
	point("monster_floater", {1, 0.5f, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Technician: a floating machine with a blaster and a probe."), "models/monsters/float/tris.md2"),
	uses(kQuake2Monster),
	point("monster_tank", {1, 0.5f, 0}, {{-32, -32, -16}, {32, 32, 72}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Tank: a huge walker with blasters, a machine gun and rockets."), "models/monsters/tank/tris.md2"),
	uses(kQuake2Monster),
	point("monster_tank_commander", {1, 0.5f, 0}, {{-32, -32, -16}, {32, 32, 72}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Tank commander: a tougher, differently painted tank."), "models/monsters/tank/tris.md2"),
	uses(kQuake2Monster),
	point("monster_brain", {1, 0.5f, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Brains: attacks with tentacles and claws behind a power screen."), "models/monsters/brain/tris.md2"),
	uses(kQuake2Monster),
	point("monster_flipper", {1, 0.5f, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Barracuda shark: a biting fish for underwater areas."), "models/monsters/flipper/tris.md2"),
	uses(kQuake2Monster),
	point("monster_supertank", {1, 0.5f, 0}, {{-64, -64, 0}, {64, 64, 72}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Super tank: a boss with a chaingun and rockets."), "models/monsters/boss1/tris.md2"),
	uses(kQuake2Monster),
	point("monster_boss2", {1, 0.5f, 0}, {{-56, -56, 0}, {56, 56, 80}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Hornet: a flying boss with chainguns and rockets."), "models/monsters/boss2/tris.md2"),
	uses(kQuake2Monster),
	point("monster_jorg", {1, 0.5f, 0}, {{-80, -80, 0}, {90, 90, 140}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Jorg: the war machine the Makron rides into the final battle."), "models/monsters/boss3/jorg/tris.md2"),
	uses(kQuake2Monster),
	point("monster_makron", {1, 0.5f, 0}, {{-30, -30, 0}, {30, 30, 90}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Makron: the final boss."), "models/monsters/boss3/rider/tris.md2"),
	uses(kQuake2Monster),
	point("monster_boss3_stand", {1, 0.5f, 0}, {{-32, -32, 0}, {32, 32, 90}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The Makron standing still as scenery until triggered, when it teleports away."), "models/monsters/boss3/rider/tris.md2"),
	uses(kTriggered),
};

// ---------------------------------------------------------------------------
// Quake III Arena
//
// Every entity the game module spawns honours the game type filter keys, so
// they are listed on each such class; compiler-only classes (worldspawn,
// func_group, info_null, light, misc_model) leave them out.
// ---------------------------------------------------------------------------

constexpr Row kQuake3GameTypes[] = {
	key("notfree", Type::Boolean, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "1 leaves it out of free-for-all, tournament and single-player games.")),
	key("notteam", Type::Boolean, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "1 leaves it out of team deathmatch and capture the flag.")),
	key("notsingle", Type::Boolean, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "1 leaves it out of single-player games.")),
	key("notq3a", Type::Boolean, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "1 leaves it out of Quake III Arena itself, for maps shared with Team Arena.")),
	key("gametype", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Game types it appears in, as names such as ffa, tournament, single, team and ctf; empty means all.")),
};

constexpr Row kQuake3Item[] = {
	key("wait", Type::Real, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds before it respawns, instead of the item's usual time; -1 means it never respawns on its own.")),
	key("random", Type::Real, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Random variation, in seconds either way.")),
	key("count", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Overrides the amount or the duration the item gives.")),
	key("team", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Items with the same team name take turns: only one is present at a time, picked at random.")),
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Entities triggered when the item is picked up.")),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An item with a targetname stays hidden until triggered.")),
	uses(kQuake3GameTypes),
	flag(0, "suspended", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Floats where it is placed instead of dropping to the floor.")),
};

constexpr Row kQuake3Powerup[] = {
	uses(kQuake3Item),
	key("noglobalsound", Type::Boolean, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "1 plays its respawn sound only nearby instead of to every player.")),
};

// The keys every Quake III mover reads in InitMover.
constexpr Row kQuake3Mover[] = {
	key("model2", Type::Model, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An extra .md3 model drawn with its brushes.")),
	key("color", Type::Color, "1 1 1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Colour of the constant light it casts, as values from 0 to 1.")),
	key("light", Type::Integer, "100", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Radius of the constant light it casts; used once either this or color is set.")),
	key("noise", Type::Sound, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Looping sound it plays.")),
	uses(kQuake3GameTypes),
};

constexpr Row kQuake3SpawnPoint[] = {
	uses(kFacing),
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Entities triggered whenever someone spawns here.")),
	uses(kQuake3GameTypes),
};

// info_player_start and info_player_deathmatch.
constexpr Row kQuake3PlayerSpawn[] = {
	uses(kQuake3SpawnPoint),
	key("nobots", Type::Boolean, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "1 keeps bots from spawning here.")),
	key("nohumans", Type::Boolean, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "1 keeps human players from spawning here.")),
	flag(0, "initial", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Used for players' first spawn of the match.")),
};

constexpr Row kQuake3Shooter[] = {
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Entity to aim at; otherwise it fires along its angle.")),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Firing direction, in degrees; -1 is up and -2 is down.")),
	key("random", Type::Real, "1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Random spread of each shot, in degrees.")),
	uses(kTriggered),
	uses(kQuake3GameTypes),
};

constexpr Row kQuake3Unused[] = {
	flag(0, "START_ON", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Declared by the game's definition but not used by its code.")),
};

constexpr Row kQuake3Rows[] = {
	brush("worldspawn", {0, 0, 0}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The world itself: every map has exactly one, holding the level's settings, and its brushes form the level's structure.")),
	key("message", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Level name shown to players.")),
	key("music", Type::Sound, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Music file to play; a second file name after it loops once the first has played.")),
	key("gravity", Type::Real, "800", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Gravity strength.")),
	key("enableDust", Type::Boolean, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "1 raises dust when players land on dusty surfaces.")),
	key("enableBreath", Type::Boolean, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "1 shows players' breath, for cold levels.")),
	key("_blocksize", Type::String, "1024", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Size of the blocks the compiler first splits the world into; one number, or three for the three axes (q3map2).")),
	key("gridsize", Type::Vector, "64 64 128", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Spacing of the light grid that lights players and models.")),
	key("_ambient", Type::Real, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Light the compiler adds to every surface (q3map2).")),
	key("ambient", Type::Real, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The original q3map's name for _ambient.")),
	key("_color", Type::Color, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Colour of the ambient light, as values from 0 to 1.")),

	// Spawn points and markers.
	point("info_player_start", {1, 0, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A spawn point; the game turns it into an info_player_deathmatch.")),
	uses(kQuake3PlayerSpawn),
	point("info_player_deathmatch", {1, 0, 1}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A spawn point; its targets fire whenever someone spawns here.")),
	uses(kQuake3PlayerSpawn),
	point("info_player_intermission", {1, 0, 1}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A camera for the intermission at the end of a match.")),
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An info_notnull or target_position to look at.")),
	key("angles", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "View direction as pitch, yaw and roll.")),
	uses(kQuake3GameTypes),
	point("info_null", {0, 0.5f, 0}, {{-4, -4, -4}, {4, 4, 4}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A position for the compilers to aim at, such as a spotlight target; removed when the level starts.")),
	uses(kAimPoint),
	point("info_notnull", {0, 0.5f, 0}, {{-4, -4, -4}, {4, 4, 4}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A position marker kept in the game, such as the top of a jump pad's arc; the same as target_position.")),
	uses(kAimPoint),
	uses(kQuake3GameTypes),
	point("info_camp", {0, 0.5f, 0}, {{-4, -4, -4}, {4, 4, 4}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A camping spot for bots, read by the bot library; it also serves as a position marker.")),
	uses(kAimPoint),
	uses(kQuake3GameTypes),
	point("light", {0, 1, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An invisible light that the light compiler bakes into the level; removed when the level starts. Aim it at a target to make a spotlight.")),
	key("light", Type::Integer, "300", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Brightness the light compiler gives it.")),
	key("_color", Type::Color, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Light colour, as values from 0 to 1.")),
	key("radius", Type::Integer, "64", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Radius of a spotlight's pool of light at its target.")),
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An entity to aim at, which makes this a spotlight.")),
	flag(0, "linear", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fades in a straight line with distance instead of by the inverse square.")),
	flag(1, "noincidence", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Lights surfaces evenly whatever angle it strikes them at (q3map2).")),
	point("misc_model", {1, 0, 0}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A static model the compiler bakes into the map's geometry; it does not exist in the game.")),
	needs("model", Type::Model, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The .md3 model to place.")),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Yaw rotation in degrees.")),
	key("angles", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Pitch, yaw and roll (q3map2).")),
	key("modelscale", Type::Real, "1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Uniform scale (q3map2).")),
	point("misc_teleporter_dest", {1, 0, 0}, {{-32, -32, -24}, {32, 32, -16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Where a teleporter sends players; in practice the same as an info_notnull.")),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name the teleporter's target refers to.")),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Direction a teleported player faces.")),
	uses(kQuake3GameTypes),
	point("misc_portal_surface", {0, 0, 1}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Makes the nearest portal surface, which must be within 64 units, show the view from the misc_portal_camera it targets, or a mirror when it has no target.")),
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The misc_portal_camera to show.")),
	uses(kQuake3GameTypes),
	point("misc_portal_camera", {0, 0, 1}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The viewpoint a misc_portal_surface shows.")),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name the portal surface's target refers to.")),
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An entity to look at; otherwise its angles set the view.")),
	key("angles", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "View direction as pitch, yaw and roll.")),
	key("roll", Type::Real, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Roll of the view about its direction, in degrees.")),
	uses(kQuake3GameTypes),
	flag(0, "slowrotate", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Rotates the view slowly.")),
	flag(1, "fastrotate", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Rotates the view quickly.")),
	flag(2, "noswing", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Keeps the view from swinging.")),
	point("shooter_rocket", {1, 0, 0}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires a rocket at its target, or along its angle, when triggered.")),
	uses(kQuake3Shooter),
	point("shooter_grenade", {1, 0, 0}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires a grenade at its target, or along its angle, when triggered.")),
	uses(kQuake3Shooter),
	point("shooter_plasma", {1, 0, 0}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires a plasma bolt at its target, or along its angle, when triggered.")),
	uses(kQuake3Shooter),

	// Targets.
	point("target_give", {1, 0, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Gives the activator every item it targets.")),
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The items to give.")),
	uses(kTriggered),
	uses(kQuake3GameTypes),
	point("target_remove_powerups", {1, 0, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Takes away all of the activator's powerups, for example to drop a flight powerup into a death pit.")),
	uses(kTriggered),
	uses(kQuake3GameTypes),
	point("target_delay", {1, 0, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires its targets after a delay.")),
	key("wait", Type::Real, "1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds to wait.")),
	key("delay", Type::Real, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Used instead of wait when set.")),
	key("random", Type::Real, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Random variation, in seconds either way.")),
	uses(kTarget),
	uses(kTriggered),
	uses(kQuake3GameTypes),
	point("target_speaker", {1, 0, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Plays a sound when triggered, or loops one that triggers switch on and off.")),
	needs("noise", Type::Sound, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Sound to play; a name starting with * plays the activating player's own version of it.")),
	key("wait", Type::Real, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds between automatic plays; 0 plays only when triggered.")),
	key("random", Type::Real, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Random variation, in seconds either way.")),
	uses(kTriggered),
	uses(kQuake3GameTypes),
	flag(0, "looped-on", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Loops, starting on.")),
	flag(1, "looped-off", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Loops, starting off.")),
	flag(2, "global", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Heard at full volume everywhere.")),
	flag(3, "activator", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Heard only by the player who set it off.")),
	point("target_print", {1, 0, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Prints its message on screen when triggered.")),
	key("message", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Text to print.")),
	uses(kTriggered),
	uses(kQuake3GameTypes),
	flag(0, "redteam", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Only the red team sees it.")),
	flag(1, "blueteam", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Only the blue team sees it.")),
	flag(2, "private", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Only the activator sees it.")),
	point("target_laser", {0, 0.5f, 0.8f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A laser beam aimed at its target or along its angle; triggering it switches it on or off.")),
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An entity to aim at; otherwise it follows its angle.")),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Firing direction, in degrees; -1 is up and -2 is down.")),
	key("dmg", Type::Integer, "1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage per frame to anything in the beam.")),
	uses(kTriggered),
	uses(kQuake3GameTypes),
	flag(0, "START_ON", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Switched on from the start.")),
	point("target_teleporter", {1, 0, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Teleports the activator to the entity it targets.")),
	needs("target", Type::TargetDestination, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Destination, such as a misc_teleporter_dest or target_position.")),
	uses(kTriggered),
	uses(kQuake3GameTypes),
	point("target_relay", {0.5f, 0.5f, 0.5f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires its targets when triggered, optionally only for one team or for one target picked at random.")),
	uses(kTarget),
	uses(kTriggered),
	uses(kQuake3GameTypes),
	flag(0, "RED_ONLY", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Only red team players set it off.")),
	flag(1, "BLUE_ONLY", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Only blue team players set it off.")),
	flag(2, "RANDOM", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires just one of its targets, picked at random.")),
	point("target_kill", {0.5f, 0.5f, 0.5f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Kills the activator.")),
	uses(kTriggered),
	uses(kQuake3GameTypes),
	point("target_position", {0, 0.5f, 0}, {{-4, -4, -4}, {4, 4, 4}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A position marker kept in the game, such as the top of a jump pad's arc.")),
	uses(kAimPoint),
	uses(kQuake3GameTypes),
	point("target_location", {0, 0.5f, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Names part of the level for team chat: a player's location is the nearest one in sight.")),
	key("message", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name of the location.")),
	key("count", Type::Choices, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Colour of the name.")),
	choice("0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "White")),
	choice("1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Red")),
	choice("2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Green")),
	choice("3", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Yellow")),
	choice("4", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Blue")),
	choice("5", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Cyan")),
	choice("6", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Magenta")),
	choice("7", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "White")),
	uses(kQuake3GameTypes),
	point("target_push", {0.5f, 0.5f, 0.5f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Launches the activator along its angle, or towards a target at the top of the arc; unlike trigger_push, clients do not predict it.")),
	key("speed", Type::Real, "1000", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Launch speed when aiming by angle.")),
	key("angle", Type::Angle, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Direction of the push, in degrees; -1 is up and -2 is down.")),
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Top of the arc to aim for, instead of the angle.")),
	uses(kTriggered),
	uses(kQuake3GameTypes),
	flag(0, "bouncepad", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Plays the jump pad sound instead of the wind sound.")),
	point("target_score", {1, 0, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Gives the activator points when triggered.")),
	key("count", Type::Integer, "1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Points to give.")),
	uses(kTriggered),
	uses(kQuake3GameTypes),

	// Triggers.
	brush("trigger_multiple", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "An invisible trigger brush that fires its targets each time it is touched, at most once per wait.")),
	key("wait", Type::Real, "0.5", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds before it can fire again; -1 fires only once.")),
	key("random", Type::Real, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Random variation, in seconds either way.")),
	uses(kTarget),
	uses(kTriggered),
	uses(kQuake3GameTypes),
	flag(0, "RED_ONLY", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Only red team players set it off.")),
	flag(1, "BLUE_ONLY", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Only blue team players set it off.")),
	point("trigger_always", {0.5f, 0.5f, 0.5f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires its targets once, shortly after the level starts.")),
	uses(kTarget),
	uses(kQuake3GameTypes),
	brush("trigger_push", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A jump pad: launches players towards the entity it targets, which marks the top of the arc. Clients predict it.")),
	needs("target", Type::TargetDestination, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The target_position or info_notnull at the top of the arc.")),
	uses(kQuake3GameTypes),
	brush("trigger_teleport", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A teleporter brush that clients predict: it sends players to the entity it targets.")),
	needs("target", Type::TargetDestination, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Destination, such as a misc_teleporter_dest or target_position.")),
	uses(kQuake3GameTypes),
	flag(0, "SPECTATOR", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Only spectators can use it; the game makes these near doors by itself.")),
	brush("trigger_hurt", {0.5f, 0.5f, 0.5f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Hurts anything inside it every frame, or once a second with SLOW.")),
	key("dmg", Type::Integer, "5", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage per hit.")),
	uses(kTriggered),
	uses(kQuake3GameTypes),
	flag(0, "START_OFF", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Starts switched off.")),
	flag(1, "TOGGLE", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Each trigger switches it on or off.")),
	flag(2, "SILENT", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Makes no sound.")),
	flag(3, "NO_PROTECTION", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Nothing protects against the damage.")),
	flag(4, "SLOW", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Hurts once a second instead of every frame.")),

	// Movers and other brush entities.
	brush("func_door", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A sliding door. Doors that share a team move together; a door with a targetname or health opens only when triggered or shot.")),
	uses(kMoveAngle),
	key("speed", Type::Real, "400", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Movement speed in units per second.")),
	key("wait", Type::Real, "2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds before it closes again; -1 keeps it open.")),
	key("lip", Type::Integer, "8", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "How much of it still shows at the end of its move.")),
	key("dmg", Type::Integer, "2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage dealt to anything that blocks it.")),
	key("health", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "If set, shooting it activates it instead of touching it.")),
	key("team", Type::String, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Doors with the same team name move together.")),
	key("targetname", Type::TargetSource, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name that buttons and triggers use to open it; a named door has no touch trigger of its own.")),
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Entities triggered when it finishes opening.")),
	uses(kQuake3Mover),
	flag(0, "START_OPEN", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Spawns open and works in reverse, so triggering it closes the way; it is lit in its closed position.")),
	flag(2, "CRUSHER", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Keeps crushing whatever blocks it instead of reversing.")),
	brush("func_button", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A button that moves along its angle when pressed, or when shot if it has health, fires its targets and then moves back.")),
	uses(kMoveAngle),
	key("speed", Type::Real, "40", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Movement speed in units per second.")),
	key("wait", Type::Real, "1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds before it moves back; -1 keeps it pressed.")),
	key("lip", Type::Integer, "4", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "How much of it still shows at the end of its move.")),
	key("health", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "If set, shooting it activates it instead of touching it.")),
	uses(kTarget),
	uses(kTriggered),
	uses(kQuake3Mover),
	brush("func_plat", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A lift that waits at the bottom and rises when someone stands on it; a named lift has no trigger of its own and moves only when triggered.")),
	key("speed", Type::Real, "200", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Movement speed in units per second.")),
	key("lip", Type::Integer, "8", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "How much of it still shows at the end of its move.")),
	key("height", Type::Integer, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Travel distance; by default its own height minus the lip.")),
	key("dmg", Type::Integer, "2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage dealt to anything that blocks it.")),
	uses(kTriggered),
	uses(kQuake3Mover),
	brush("func_train", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A mover that travels between path_corner entities, starting at the first one it targets. It needs an origin brush, which is the point that moves onto each corner.")),
	needs("target", Type::TargetDestination, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The first path_corner.")),
	key("speed", Type::Real, "100", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Movement speed in units per second.")),
	key("dmg", Type::Integer, "2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage dealt to anything that blocks it.")),
	uses(kQuake3Mover),
	uses(kQuake3Unused),
	flag(1, "TOGGLE", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Declared by the game's definition but not used by its code.")),
	flag(2, "BLOCK_STOPS", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Stops when blocked instead of hurting the blocker.")),
	brush("func_static", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A brush entity that never moves, for adding a model2, a constant light or a looping sound.")),
	uses(kQuake3Mover),
	brush("func_rotating", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A brush that spins around its origin brush, about the vertical axis unless X_AXIS or Y_AXIS is set.")),
	key("speed", Type::Real, "100", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Turning speed in degrees per second.")),
	key("dmg", Type::Integer, "2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage dealt to anything that blocks it.")),
	uses(kQuake3Mover),
	uses(kQuake3Unused),
	flag(2, "X_AXIS", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Turns about the X axis.")),
	flag(3, "Y_AXIS", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Turns about the Y axis.")),
	brush("func_bobbing", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A brush that bobs up and down, or along the X or Y axis with those flags.")),
	key("height", Type::Real, "32", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "How far it travels each way.")),
	key("speed", Type::Real, "4", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds for one full cycle.")),
	key("phase", Type::Real, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Where in the cycle it starts, from 0 to 1.")),
	key("dmg", Type::Integer, "2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage dealt to anything that blocks it.")),
	uses(kQuake3Mover),
	flag(0, "X_AXIS", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Moves along the X axis.")),
	flag(1, "Y_AXIS", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Moves along the Y axis.")),
	brush("func_pendulum", {0, 0.5f, 0.8f}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Swings like a pendulum from its origin brush; its length and the gravity set how fast.")),
	key("speed", Type::Real, "30", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "How far it swings each way, in degrees.")),
	key("phase", Type::Real, "0", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Where in the cycle it starts, from 0 to 1.")),
	key("dmg", Type::Integer, "2", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Damage dealt to anything that blocks it.")),
	uses(kQuake3Mover),
	point("func_timer", {0.3f, 0.1f, 0.6f}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Fires its targets repeatedly at a varying interval; triggering it turns it on or off.")),
	key("wait", Type::Real, "1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Base seconds between firings.")),
	key("random", Type::Real, "1", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Random variation, in seconds either way.")),
	uses(kTarget),
	uses(kTriggered),
	uses(kQuake3GameTypes),
	flag(0, "START_ON", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Runs from the start.")),
	brush("func_group", {0, 0, 0}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Groups brushes for editing convenience; the compiler merges them into the world.")),
	point("path_corner", {0.5f, 0.3f, 0}, {{-8, -8, -8}, {8, 8, 8}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "A waypoint for trains; each one targets the next.")),
	needs("targetname", Type::TargetSource, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Name the previous corner or the train targets.")),
	key("target", Type::TargetDestination, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The next path_corner; anything else it names is triggered when a train arrives.")),
	key("speed", Type::Real, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Train speed from here to the next corner, instead of the train's own.")),
	key("wait", Type::Real, "", QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Seconds a train waits here.")),
	uses(kQuake3GameTypes),

	// Capture the flag.
	point("team_CTF_redflag", {1, 0, 0}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The red team's flag, used only in capture the flag."), "models/flags/r_flag.md3"),
	uses(kQuake3GameTypes),
	point("team_CTF_blueflag", {0, 0, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "The blue team's flag, used only in capture the flag."), "models/flags/b_flag.md3"),
	uses(kQuake3GameTypes),
	point("team_CTF_redplayer", {1, 0, 0}, {{-16, -16, -16}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Where red players start a capture the flag match.")),
	uses(kFacing),
	uses(kQuake3GameTypes),
	point("team_CTF_blueplayer", {0, 0, 1}, {{-16, -16, -16}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Where blue players start a capture the flag match.")),
	uses(kFacing),
	uses(kQuake3GameTypes),
	point("team_CTF_redspawn", {1, 0, 0}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Where red players respawn in capture the flag; its targets fire whenever someone spawns here.")),
	uses(kQuake3SpawnPoint),
	point("team_CTF_bluespawn", {0, 0, 1}, {{-16, -16, -24}, {16, 16, 32}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Where blue players respawn in capture the flag; its targets fire whenever someone spawns here.")),
	uses(kQuake3SpawnPoint),

	// Armour and health.
	point("item_armor_shard", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Armour shard worth 5 points."), "models/powerups/armor/shard.md3"),
	uses(kQuake3Item),
	point("item_armor_combat", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Yellow armour worth 50 points."), "models/powerups/armor/armor_yel.md3"),
	uses(kQuake3Item),
	point("item_armor_body", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Red heavy armour worth 100 points."), "models/powerups/armor/armor_red.md3"),
	uses(kQuake3Item),
	point("item_health_small", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Small health worth 5, even past the maximum."), "models/powerups/health/small_cross.md3"),
	uses(kQuake3Item),
	point("item_health", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Health worth 25."), "models/powerups/health/medium_cross.md3"),
	uses(kQuake3Item),
	point("item_health_large", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Large health worth 50."), "models/powerups/health/large_cross.md3"),
	uses(kQuake3Item),
	point("item_health_mega", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Mega health worth 100, even past the maximum."), "models/powerups/health/mega_cross.md3"),
	uses(kQuake3Item),

	// Weapons.
	point("weapon_gauntlet", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the Gauntlet."), "models/weapons2/gauntlet/gauntlet.md3"),
	uses(kQuake3Item),
	point("weapon_shotgun", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the shotgun."), "models/weapons2/shotgun/shotgun.md3"),
	uses(kQuake3Item),
	point("weapon_machinegun", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the machine gun."), "models/weapons2/machinegun/machinegun.md3"),
	uses(kQuake3Item),
	point("weapon_grenadelauncher", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the grenade launcher."), "models/weapons2/grenadel/grenadel.md3"),
	uses(kQuake3Item),
	point("weapon_rocketlauncher", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the rocket launcher."), "models/weapons2/rocketl/rocketl.md3"),
	uses(kQuake3Item),
	point("weapon_lightning", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the lightning gun."), "models/weapons2/lightning/lightning.md3"),
	uses(kQuake3Item),
	point("weapon_railgun", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the railgun."), "models/weapons2/railgun/railgun.md3"),
	uses(kQuake3Item),
	point("weapon_plasmagun", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the plasma gun."), "models/weapons2/plasma/plasma.md3"),
	uses(kQuake3Item),
	point("weapon_bfg", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the BFG10K."), "models/weapons2/bfg/bfg.md3"),
	uses(kQuake3Item),
	point("weapon_grapplinghook", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Weapon pickup: the grappling hook."), "models/weapons2/grapple/grapple.md3"),
	uses(kQuake3Item),

	// Ammunition.
	point("ammo_shells", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Shells for the shotgun."), "models/powerups/ammo/shotgunam.md3"),
	uses(kQuake3Item),
	point("ammo_bullets", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Bullets for the machine gun."), "models/powerups/ammo/machinegunam.md3"),
	uses(kQuake3Item),
	point("ammo_grenades", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Grenades for the grenade launcher."), "models/powerups/ammo/grenadeam.md3"),
	uses(kQuake3Item),
	point("ammo_cells", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Cells for the plasma gun."), "models/powerups/ammo/plasmaam.md3"),
	uses(kQuake3Item),
	point("ammo_lightning", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Ammunition for the lightning gun."), "models/powerups/ammo/lightningam.md3"),
	uses(kQuake3Item),
	point("ammo_rockets", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Rockets for the rocket launcher."), "models/powerups/ammo/rocketam.md3"),
	uses(kQuake3Item),
	point("ammo_slugs", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Slugs for the railgun."), "models/powerups/ammo/railgunam.md3"),
	uses(kQuake3Item),
	point("ammo_bfg", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Ammunition for the BFG10K."), "models/powerups/ammo/bfgam.md3"),
	uses(kQuake3Item),

	// Holdable items and powerups.
	point("holdable_teleporter", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Personal teleporter: carried until used, then sends the player to a spawn point."), "models/powerups/holdable/teleporter.md3"),
	uses(kQuake3Item),
	point("holdable_medkit", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Medkit: carried until used, then sets health to 25 above the maximum."), "models/powerups/holdable/medkit.md3"),
	uses(kQuake3Item),
	point("item_quad", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Quad Damage: 30 seconds of extra damage, three times normal by default."), "models/powerups/instant/quad.md3"),
	uses(kQuake3Powerup),
	point("item_enviro", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Battle Suit: for 30 seconds, no splash, falling, slime, lava or drowning damage, and half damage from anything else."), "models/powerups/instant/enviro.md3"),
	uses(kQuake3Powerup),
	point("item_haste", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Haste: 30 seconds of faster running and firing."), "models/powerups/instant/haste.md3"),
	uses(kQuake3Powerup),
	point("item_invis", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Invisibility: 30 seconds of near invisibility."), "models/powerups/instant/invis.md3"),
	uses(kQuake3Powerup),
	point("item_regen", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Regeneration: 30 seconds of steadily regaining health."), "models/powerups/instant/regen.md3"),
	uses(kQuake3Powerup),
	point("item_flight", {0.3f, 0.3f, 1}, {{-16, -16, -16}, {16, 16, 16}}, QT_TRANSLATE_NOOP("VibeStudioEntityCatalogue", "Flight: 60 seconds of free flight."), "models/powerups/instant/flight.md3"),
	uses(kQuake3Powerup),
};

// ---------------------------------------------------------------------------
// Building a catalogue
// ---------------------------------------------------------------------------

std::span<const Row> tableFor(BuiltinEntityGame game)
{
	switch (game) {
	case BuiltinEntityGame::Quake:
		return kQuakeRows;
	case BuiltinEntityGame::Quake2:
		return kQuake2Rows;
	case BuiltinEntityGame::Quake3:
		return kQuake3Rows;
	}
	return kQuakeRows;
}

// A QUAKED colour component, 0 to 1, as the 0-255 value the catalogue keeps;
// rounded the way the Radiant .def reader rounds it.
int colorByte(float unit)
{
	return static_cast<int>(std::lround(std::clamp(static_cast<double>(unit), 0.0, 1.0) * 255.0));
}

qsizetype keyIndex(const EntityClassDefinition& definition, const char* name)
{
	const QLatin1StringView wanted(name);
	for (qsizetype index = 0; index < definition.keys.size(); ++index) {
		if (definition.keys.at(index).key.compare(wanted, Qt::CaseInsensitive) == 0) {
			return index;
		}
	}
	return -1;
}

bool hasSpawnflagBit(const EntityClassDefinition& definition, int bit)
{
	return std::any_of(definition.spawnflags.cbegin(), definition.spawnflags.cend(), [bit](const EntitySpawnflagDefinition& entry) {
		return entry.bit == bit;
	});
}

// Applies the key, choice, default, flag and shared-set rows of one class.
void applyClassRows(EntityClassDefinition& definition, std::span<const Row> rows)
{
	// The key the following choice rows belong to; -1 after a key that an
	// earlier row of the class already declared, so its choices are skipped too.
	qsizetype choiceKey = -1;
	for (const Row& row : rows) {
		switch (row.kind) {
		case RowKind::Key: {
			choiceKey = -1;
			if (keyIndex(definition, row.name) >= 0) {
				break;
			}
			EntityKeyDefinition entry;
			entry.key = QString::fromLatin1(row.name);
			entry.type = row.type;
			entry.typeId = entityKeyTypeId(row.type);
			entry.displayName = entry.key;
			entry.description = QCoreApplication::translate("VibeStudioEntityCatalogue", row.text);
			entry.defaultValue = QString::fromLatin1(row.value);
			entry.required = row.required;
			definition.keys.append(entry);
			choiceKey = definition.keys.size() - 1;
			break;
		}
		case RowKind::Choice:
			if (choiceKey >= 0) {
				EntityKeyChoice entry;
				entry.value = QString::fromLatin1(row.name);
				entry.label = QCoreApplication::translate("VibeStudioEntityCatalogue", row.text);
				definition.keys[choiceKey].choices.append(entry);
			}
			break;
		case RowKind::Default: {
			const qsizetype index = keyIndex(definition, row.name);
			if (index >= 0) {
				definition.keys[index].defaultValue = QString::fromLatin1(row.value);
			}
			break;
		}
		case RowKind::Flag: {
			if (hasSpawnflagBit(definition, row.bit)) {
				break;
			}
			EntitySpawnflagDefinition entry;
			entry.bit = row.bit;
			entry.name = QString::fromLatin1(row.name);
			entry.description = QCoreApplication::translate("VibeStudioEntityCatalogue", row.text);
			definition.spawnflags.append(entry);
			break;
		}
		case RowKind::Shared:
			choiceKey = -1;
			applyClassRows(definition, std::span<const Row>(row.rows, row.rowCount));
			break;
		case RowKind::Class:
			// Shared sets never open a class.
			break;
		}
	}
}

EntityDefinitionCatalogue buildCatalogue(BuiltinEntityGame game)
{
	const std::span<const Row> table = tableFor(game);
	const QString sourcePath = QStringLiteral("builtin:%1").arg(builtinEntityGameId(game));

	EntityDefinitionCatalogue catalogue;
	catalogue.sourcePaths << sourcePath;
	std::size_t index = 0;
	while (index < table.size()) {
		const Row& row = table[index];
		std::size_t end = index + 1;
		while (end < table.size() && table[end].kind != RowKind::Class) {
			++end;
		}
		if (row.kind != RowKind::Class) {
			index = end;
			continue;
		}

		EntityClassDefinition definition;
		definition.className = QString::fromLatin1(row.name);
		definition.kind = row.classKind;
		definition.description = QCoreApplication::translate("VibeStudioEntityCatalogue", row.text);
		definition.hasColor = true;
		definition.color[0] = colorByte(row.color.red);
		definition.color[1] = colorByte(row.color.green);
		definition.color[2] = colorByte(row.color.blue);
		if (row.classKind == EntityClassKind::Point) {
			definition.hasSize = true;
			for (int axis = 0; axis < 3; ++axis) {
				definition.mins[axis] = row.box.mins[axis];
				definition.maxs[axis] = row.box.maxs[axis];
			}
		}
		definition.modelHint = QString::fromLatin1(row.value);
		definition.sourcePath = sourcePath;
		applyClassRows(definition, table.subspan(index + 1, end - index - 1));
		std::sort(definition.spawnflags.begin(), definition.spawnflags.end(), [](const EntitySpawnflagDefinition& left, const EntitySpawnflagDefinition& right) {
			return left.bit < right.bit;
		});
		if (definition.kind == EntityClassKind::Point) {
			++catalogue.pointClassCount;
		} else {
			++catalogue.brushClassCount;
		}
		catalogue.classes.append(std::move(definition));
		index = end;
	}

	// The same order the definition-file readers give: case-insensitive, with
	// the exact spelling breaking ties.
	std::sort(catalogue.classes.begin(), catalogue.classes.end(), [](const EntityClassDefinition& left, const EntityClassDefinition& right) {
		const int compared = QString::compare(left.className, right.className, Qt::CaseInsensitive);
		return compared != 0 ? compared < 0 : left.className < right.className;
	});
	return catalogue;
}

// ---------------------------------------------------------------------------
// Choosing a game for a map
// ---------------------------------------------------------------------------

// Lower-case class names the Quake II table declares and the Quake table does
// not. func_group is left out: Quake editors and compilers use it as well, so
// it says nothing about the game.
const QSet<QString>& quake2OnlyClassNames()
{
	static const QSet<QString> names = [] {
		QSet<QString> quake;
		for (const Row& row : kQuakeRows) {
			if (row.kind == RowKind::Class) {
				quake.insert(QString::fromLatin1(row.name).toLower());
			}
		}
		QSet<QString> onlyQuake2;
		for (const Row& row : kQuake2Rows) {
			if (row.kind != RowKind::Class) {
				continue;
			}
			const QString name = QString::fromLatin1(row.name).toLower();
			if (!quake.contains(name)) {
				onlyQuake2.insert(name);
			}
		}
		onlyQuake2.remove(QStringLiteral("func_group"));
		return onlyQuake2;
	}();
	return names;
}

// Classes only Quake has. The compiler's own classes are left out: they
// say which compiler the map is for, not which game.
const QSet<QString>& quakeOnlyClassNames()
{
	static const QSet<QString> names = [] {
		QSet<QString> quake2;
		for (const Row& row : kQuake2Rows) {
			if (row.kind == RowKind::Class) {
				quake2.insert(QString::fromLatin1(row.name).toLower());
			}
		}
		QSet<QString> onlyQuake;
		for (const Row& row : kQuakeRows) {
			if (row.kind != RowKind::Class) {
				continue;
			}
			const QString name = QString::fromLatin1(row.name).toLower();
			if (!quake2.contains(name) && !name.startsWith(QStringLiteral("func_detail")) && name != QStringLiteral("func_group")) {
				onlyQuake.insert(name);
			}
		}
		return onlyQuake;
	}();
	return names;
}

bool usesQuakeOnlyClass(const LevelMapDocument& document)
{
	const QSet<QString>& names = quakeOnlyClassNames();
	return std::any_of(document.entities.cbegin(), document.entities.cend(), [&names](const LevelMapEntity& entity) {
		return names.contains(entity.className.toLower());
	});
}

// TrenchBroom heads a Quake map with "// Game: Quake".
bool startsWithQuakeHeader(const QString& text)
{
	const qsizetype newline = text.indexOf(QLatin1Char('\n'));
	const QStringView firstLine = QStringView(text).first(newline < 0 ? text.size() : newline).trimmed();
	return firstLine.compare(QLatin1StringView("// Game: Quake")) == 0;
}

// Quake's worldspawn keys: worldtype picks the key models and sounds.
bool worldspawnHasQuakeKey(const LevelMapDocument& document)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.className.compare(QLatin1StringView("worldspawn"), Qt::CaseInsensitive) == 0) {
			return std::any_of(entity.properties.cbegin(), entity.properties.cend(), [](const LevelMapProperty& property) {
				return property.key.compare(QLatin1StringView("worldtype"), Qt::CaseInsensitive) == 0;
			});
		}
	}
	return false;
}

bool startsWithQuake2Header(const QString& text)
{
	// Only the first line is examined, without copying a document that can be
	// hundreds of megabytes long.
	const qsizetype newline = text.indexOf(QLatin1Char('\n'));
	const QStringView firstLine = QStringView(text).first(newline < 0 ? text.size() : newline).trimmed();
	return firstLine.compare(QLatin1StringView(kQuake2MapTargetHeader).trimmed()) == 0
		|| firstLine.compare(QLatin1StringView("// Game: Quake 2")) == 0;
}

bool usesQuake2OnlyClass(const LevelMapDocument& document)
{
	const QSet<QString>& names = quake2OnlyClassNames();
	return std::any_of(document.entities.cbegin(), document.entities.cend(), [&names](const LevelMapEntity& entity) {
		return names.contains(entity.className.toLower());
	});
}

bool worldspawnNamesWad(const LevelMapDocument& document)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.className.compare(QLatin1StringView("worldspawn"), Qt::CaseInsensitive) != 0) {
			continue;
		}
		return std::any_of(entity.properties.cbegin(), entity.properties.cend(), [](const LevelMapProperty& property) {
			return (property.key.compare(QLatin1StringView("wad"), Qt::CaseInsensitive) == 0
					   || property.key.compare(QLatin1StringView("_wad"), Qt::CaseInsensitive) == 0)
				&& !property.value.trimmed().isEmpty();
		});
	}
	return false;
}

bool mostTexturesUseFolders(const LevelMapDocument& document)
{
	qsizetype textured = 0;
	qsizetype inFolders = 0;
	const auto count = [&textured, &inFolders](const QString& name) {
		if (name.isEmpty()) {
			return;
		}
		++textured;
		if (name.contains(QLatin1Char('/'))) {
			++inFolders;
		}
	};
	for (const LevelMapBrush& brush : document.brushes) {
		if (brush.faces.isEmpty()) {
			for (const QString& name : brush.textureNames) {
				count(name);
			}
			continue;
		}
		for (const LevelMapBrushFace& face : brush.faces) {
			count(face.textureName);
		}
	}
	return textured > 0 && inFolders * 2 > textured;
}

} // namespace

QString builtinEntityGameId(BuiltinEntityGame game)
{
	switch (game) {
	case BuiltinEntityGame::Quake:
		return QStringLiteral("quake");
	case BuiltinEntityGame::Quake2:
		return QStringLiteral("quake2");
	case BuiltinEntityGame::Quake3:
		return QStringLiteral("quake3");
	}
	return QStringLiteral("quake");
}

QString builtinEntityGameDisplayName(BuiltinEntityGame game)
{
	switch (game) {
	case BuiltinEntityGame::Quake:
		return QCoreApplication::translate("VibeStudioEntityCatalogue", "Quake");
	case BuiltinEntityGame::Quake2:
		return QCoreApplication::translate("VibeStudioEntityCatalogue", "Quake II");
	case BuiltinEntityGame::Quake3:
		return QCoreApplication::translate("VibeStudioEntityCatalogue", "Quake III Arena");
	}
	return QString();
}

bool builtinEntityGameFromId(const QString& id, BuiltinEntityGame* out)
{
	for (const BuiltinEntityGame game : {BuiltinEntityGame::Quake, BuiltinEntityGame::Quake2, BuiltinEntityGame::Quake3}) {
		if (builtinEntityGameId(game) == id.trimmed().toLower()) {
			if (out) {
				*out = game;
			}
			return true;
		}
	}
	return false;
}

// Quake and Quake II share one text map grammar, so a QuakeMap document is
// Quake II when any of these holds, checked in this order:
// 1. Its first line is a Quake II header: VibeStudio's target comment
//    (kQuake2MapTargetHeader, written when a map is created or built for
//    Quake II) or TrenchBroom's "// Game: Quake 2".
// 2. An entity uses a class only the Quake II catalogue defines, such as
//    func_areaportal, target_speaker, misc_teleporter, item_armor_combat or
//    monster_soldier (func_group excepted; see quake2OnlyClassNames()).
// 3. Worldspawn has no wad or _wad key, and more than half of the textured
//    brush faces name their texture with a folder ("e1u1/floor1_3"). Quake
//    textures come from a WAD and never contain a slash.
// Otherwise it is Quake. A Quake III map is Quake III, and any other format
// (a Doom WAD, an unknown format) falls back to Quake.
BuiltinEntityGame builtinEntityGameForMap(const LevelMapDocument& document)
{
	if (document.format == LevelMapFormat::Quake3Map) {
		return BuiltinEntityGame::Quake3;
	}
	if (document.format != LevelMapFormat::QuakeMap) {
		return BuiltinEntityGame::Quake;
	}
	if (startsWithQuake2Header(document.originalText) || usesQuake2OnlyClass(document)) {
		return BuiltinEntityGame::Quake2;
	}
	// Anything only Quake has outweighs texture names, which Quake maps for
	// newer compilers also keep in folders.
	if (startsWithQuakeHeader(document.originalText) || usesQuakeOnlyClass(document) || worldspawnNamesWad(document)
		|| worldspawnHasQuakeKey(document)) {
		return BuiltinEntityGame::Quake;
	}
	return mostTexturesUseFolders(document) ? BuiltinEntityGame::Quake2 : BuiltinEntityGame::Quake;
}

EntityDefinitionCatalogue builtinEntityDefinitions(BuiltinEntityGame game)
{
	return buildCatalogue(game);
}

} // namespace vibestudio
