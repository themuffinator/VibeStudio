#include "core/level_generation.h"

#include "core/level_generation_p.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace vibestudio {

namespace levelgen {

quint64 LevelRandom::next()
{
	// splitmix64: the same numbers on every compiler and standard library.
	quint64 z = (m_state += 0x9E3779B97F4A7C15ull);
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	return z ^ (z >> 31);
}

int LevelRandom::range(int low, int high)
{
	if (high <= low) {
		return low;
	}
	return low + int(next() % quint64(high - low + 1));
}

bool LevelRandom::chance(double probability)
{
	return double(next() % 1000000ull) / 1000000.0 < probability;
}

const QVector<ItemEntry>& itemEntries()
{
	// Class names from each game's released source (Quake's progs, Quake
	// II's g_spawn.c and g_items.c, Quake III's bg_misc.c); Doom things are
	// DoomEd numbers from https://doomwiki.org/wiki/Thing_types.
	static const QVector<ItemEntry> entries = {
		// Quake.
		{QStringLiteral("quake"), QStringLiteral("player-start"), QString(), QStringLiteral("info_player_start"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("deathmatch-start"), QString(), QStringLiteral("info_player_deathmatch"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("exit"), QString(), QStringLiteral("trigger_changelevel"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("weapon"), QStringLiteral("supershotgun"), QStringLiteral("weapon_supershotgun"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("weapon"), QStringLiteral("nailgun"), QStringLiteral("weapon_nailgun"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("weapon"), QStringLiteral("supernailgun"), QStringLiteral("weapon_supernailgun"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("weapon"), QStringLiteral("grenadelauncher"), QStringLiteral("weapon_grenadelauncher"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("weapon"), QStringLiteral("rocketlauncher"), QStringLiteral("weapon_rocketlauncher"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("weapon"), QStringLiteral("lightning"), QStringLiteral("weapon_lightning"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("ammo"), QStringLiteral("shells"), QStringLiteral("item_shells"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("ammo"), QStringLiteral("nails"), QStringLiteral("item_spikes"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("ammo"), QStringLiteral("rockets"), QStringLiteral("item_rockets"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("ammo"), QStringLiteral("cells"), QStringLiteral("item_cells"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("health"), QStringLiteral("health"), QStringLiteral("item_health"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("health"), QStringLiteral("megahealth"), QStringLiteral("item_health"), 0, QStringLiteral("spawnflags=2")},
		{QStringLiteral("quake"), QStringLiteral("armor"), QStringLiteral("green"), QStringLiteral("item_armor1"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("armor"), QStringLiteral("yellow"), QStringLiteral("item_armor2"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("armor"), QStringLiteral("red"), QStringLiteral("item_armorInv"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("powerup"), QStringLiteral("quad"), QStringLiteral("item_artifact_super_damage"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("powerup"), QStringLiteral("pentagram"), QStringLiteral("item_artifact_invulnerability"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("powerup"), QStringLiteral("ring"), QStringLiteral("item_artifact_invisibility"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("powerup"), QStringLiteral("biosuit"), QStringLiteral("item_artifact_envirosuit"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("monster"), QStringLiteral("grunt"), QStringLiteral("monster_army"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("monster"), QStringLiteral("rottweiler"), QStringLiteral("monster_dog"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("monster"), QStringLiteral("enforcer"), QStringLiteral("monster_enforcer"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("monster"), QStringLiteral("knight"), QStringLiteral("monster_knight"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("monster"), QStringLiteral("zombie"), QStringLiteral("monster_zombie"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("monster"), QStringLiteral("scrag"), QStringLiteral("monster_wizard"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("monster"), QStringLiteral("ogre"), QStringLiteral("monster_ogre"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("monster"), QStringLiteral("fiend"), QStringLiteral("monster_demon1"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("monster"), QStringLiteral("deathknight"), QStringLiteral("monster_hell_knight"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("monster"), QStringLiteral("vore"), QStringLiteral("monster_shalrath"), 0, QString()},
		{QStringLiteral("quake"), QStringLiteral("monster"), QStringLiteral("shambler"), QStringLiteral("monster_shambler"), 0, QString()},
		// Quake II.
		{QStringLiteral("quake2"), QStringLiteral("player-start"), QString(), QStringLiteral("info_player_start"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("deathmatch-start"), QString(), QStringLiteral("info_player_deathmatch"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("exit"), QString(), QStringLiteral("target_changelevel"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("weapon"), QStringLiteral("shotgun"), QStringLiteral("weapon_shotgun"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("weapon"), QStringLiteral("supershotgun"), QStringLiteral("weapon_supershotgun"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("weapon"), QStringLiteral("machinegun"), QStringLiteral("weapon_machinegun"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("weapon"), QStringLiteral("chaingun"), QStringLiteral("weapon_chaingun"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("weapon"), QStringLiteral("grenadelauncher"), QStringLiteral("weapon_grenadelauncher"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("weapon"), QStringLiteral("rocketlauncher"), QStringLiteral("weapon_rocketlauncher"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("weapon"), QStringLiteral("hyperblaster"), QStringLiteral("weapon_hyperblaster"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("weapon"), QStringLiteral("railgun"), QStringLiteral("weapon_railgun"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("weapon"), QStringLiteral("bfg"), QStringLiteral("weapon_bfg"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("ammo"), QStringLiteral("shells"), QStringLiteral("ammo_shells"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("ammo"), QStringLiteral("bullets"), QStringLiteral("ammo_bullets"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("ammo"), QStringLiteral("grenades"), QStringLiteral("ammo_grenades"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("ammo"), QStringLiteral("rockets"), QStringLiteral("ammo_rockets"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("ammo"), QStringLiteral("cells"), QStringLiteral("ammo_cells"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("ammo"), QStringLiteral("slugs"), QStringLiteral("ammo_slugs"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("health"), QStringLiteral("stimpack"), QStringLiteral("item_health_small"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("health"), QStringLiteral("health"), QStringLiteral("item_health"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("health"), QStringLiteral("largehealth"), QStringLiteral("item_health_large"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("health"), QStringLiteral("megahealth"), QStringLiteral("item_health_mega"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("armor"), QStringLiteral("shard"), QStringLiteral("item_armor_shard"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("armor"), QStringLiteral("jacket"), QStringLiteral("item_armor_jacket"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("armor"), QStringLiteral("combat"), QStringLiteral("item_armor_combat"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("armor"), QStringLiteral("body"), QStringLiteral("item_armor_body"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("powerup"), QStringLiteral("quad"), QStringLiteral("item_quad"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("powerup"), QStringLiteral("invulnerability"), QStringLiteral("item_invulnerability"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("powerup"), QStringLiteral("silencer"), QStringLiteral("item_silencer"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("powerup"), QStringLiteral("rebreather"), QStringLiteral("item_breather"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("powerup"), QStringLiteral("envirosuit"), QStringLiteral("item_enviro"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("monster"), QStringLiteral("guard"), QStringLiteral("monster_soldier_light"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("monster"), QStringLiteral("shotgunguard"), QStringLiteral("monster_soldier"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("monster"), QStringLiteral("machinegunguard"), QStringLiteral("monster_soldier_ss"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("monster"), QStringLiteral("enforcer"), QStringLiteral("monster_infantry"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("monster"), QStringLiteral("gunner"), QStringLiteral("monster_gunner"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("monster"), QStringLiteral("berserker"), QStringLiteral("monster_berserk"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("monster"), QStringLiteral("parasite"), QStringLiteral("monster_parasite"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("monster"), QStringLiteral("flyer"), QStringLiteral("monster_flyer"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("monster"), QStringLiteral("technician"), QStringLiteral("monster_floater"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("monster"), QStringLiteral("icarus"), QStringLiteral("monster_hover"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("monster"), QStringLiteral("brains"), QStringLiteral("monster_brain"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("monster"), QStringLiteral("medic"), QStringLiteral("monster_medic"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("monster"), QStringLiteral("mutant"), QStringLiteral("monster_mutant"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("monster"), QStringLiteral("ironmaiden"), QStringLiteral("monster_chick"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("monster"), QStringLiteral("gladiator"), QStringLiteral("monster_gladiator"), 0, QString()},
		{QStringLiteral("quake2"), QStringLiteral("monster"), QStringLiteral("tank"), QStringLiteral("monster_tank"), 0, QString()},
		// Quake III Arena.
		{QStringLiteral("quake3"), QStringLiteral("player-start"), QString(), QStringLiteral("info_player_deathmatch"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("deathmatch-start"), QString(), QStringLiteral("info_player_deathmatch"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("weapon"), QStringLiteral("shotgun"), QStringLiteral("weapon_shotgun"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("weapon"), QStringLiteral("machinegun"), QStringLiteral("weapon_machinegun"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("weapon"), QStringLiteral("grenadelauncher"), QStringLiteral("weapon_grenadelauncher"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("weapon"), QStringLiteral("rocketlauncher"), QStringLiteral("weapon_rocketlauncher"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("weapon"), QStringLiteral("lightning"), QStringLiteral("weapon_lightning"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("weapon"), QStringLiteral("railgun"), QStringLiteral("weapon_railgun"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("weapon"), QStringLiteral("plasmagun"), QStringLiteral("weapon_plasmagun"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("weapon"), QStringLiteral("bfg"), QStringLiteral("weapon_bfg"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("ammo"), QStringLiteral("shells"), QStringLiteral("ammo_shells"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("ammo"), QStringLiteral("bullets"), QStringLiteral("ammo_bullets"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("ammo"), QStringLiteral("grenades"), QStringLiteral("ammo_grenades"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("ammo"), QStringLiteral("rockets"), QStringLiteral("ammo_rockets"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("ammo"), QStringLiteral("lightning"), QStringLiteral("ammo_lightning"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("ammo"), QStringLiteral("slugs"), QStringLiteral("ammo_slugs"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("ammo"), QStringLiteral("cells"), QStringLiteral("ammo_cells"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("ammo"), QStringLiteral("bfg"), QStringLiteral("ammo_bfg"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("health"), QStringLiteral("small"), QStringLiteral("item_health_small"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("health"), QStringLiteral("health"), QStringLiteral("item_health"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("health"), QStringLiteral("large"), QStringLiteral("item_health_large"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("health"), QStringLiteral("mega"), QStringLiteral("item_health_mega"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("armor"), QStringLiteral("shard"), QStringLiteral("item_armor_shard"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("armor"), QStringLiteral("combat"), QStringLiteral("item_armor_combat"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("armor"), QStringLiteral("body"), QStringLiteral("item_armor_body"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("powerup"), QStringLiteral("quad"), QStringLiteral("item_quad"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("powerup"), QStringLiteral("haste"), QStringLiteral("item_haste"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("powerup"), QStringLiteral("invisibility"), QStringLiteral("item_invis"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("powerup"), QStringLiteral("regeneration"), QStringLiteral("item_regen"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("powerup"), QStringLiteral("battlesuit"), QStringLiteral("item_enviro"), 0, QString()},
		{QStringLiteral("quake3"), QStringLiteral("powerup"), QStringLiteral("flight"), QStringLiteral("item_flight"), 0, QString()},
		// Doom (DoomEd numbers; the ones marked 2 are Doom II's).
		{QStringLiteral("doom"), QStringLiteral("player-start"), QString(), QString(), 1, QString()},
		{QStringLiteral("doom"), QStringLiteral("deathmatch-start"), QString(), QString(), 11, QString()},
		{QStringLiteral("doom"), QStringLiteral("exit"), QString(), QString(), 0, QString()},
		{QStringLiteral("doom"), QStringLiteral("weapon"), QStringLiteral("chainsaw"), QString(), 2005, QString()},
		{QStringLiteral("doom"), QStringLiteral("weapon"), QStringLiteral("shotgun"), QString(), 2001, QString()},
		{QStringLiteral("doom"), QStringLiteral("weapon"), QStringLiteral("supershotgun"), QString(), 82, QStringLiteral("2")},
		{QStringLiteral("doom"), QStringLiteral("weapon"), QStringLiteral("chaingun"), QString(), 2002, QString()},
		{QStringLiteral("doom"), QStringLiteral("weapon"), QStringLiteral("rocketlauncher"), QString(), 2003, QString()},
		{QStringLiteral("doom"), QStringLiteral("weapon"), QStringLiteral("plasmarifle"), QString(), 2004, QString()},
		{QStringLiteral("doom"), QStringLiteral("weapon"), QStringLiteral("bfg"), QString(), 2006, QString()},
		{QStringLiteral("doom"), QStringLiteral("ammo"), QStringLiteral("clip"), QString(), 2007, QString()},
		{QStringLiteral("doom"), QStringLiteral("ammo"), QStringLiteral("bulletbox"), QString(), 2048, QString()},
		{QStringLiteral("doom"), QStringLiteral("ammo"), QStringLiteral("shells"), QString(), 2008, QString()},
		{QStringLiteral("doom"), QStringLiteral("ammo"), QStringLiteral("shellbox"), QString(), 2049, QString()},
		{QStringLiteral("doom"), QStringLiteral("ammo"), QStringLiteral("rocket"), QString(), 2010, QString()},
		{QStringLiteral("doom"), QStringLiteral("ammo"), QStringLiteral("rocketbox"), QString(), 2046, QString()},
		{QStringLiteral("doom"), QStringLiteral("ammo"), QStringLiteral("cell"), QString(), 2047, QString()},
		{QStringLiteral("doom"), QStringLiteral("ammo"), QStringLiteral("cellpack"), QString(), 17, QString()},
		{QStringLiteral("doom"), QStringLiteral("health"), QStringLiteral("bonus"), QString(), 2014, QString()},
		{QStringLiteral("doom"), QStringLiteral("health"), QStringLiteral("stimpack"), QString(), 2011, QString()},
		{QStringLiteral("doom"), QStringLiteral("health"), QStringLiteral("medikit"), QString(), 2012, QString()},
		{QStringLiteral("doom"), QStringLiteral("health"), QStringLiteral("soulsphere"), QString(), 2013, QString()},
		{QStringLiteral("doom"), QStringLiteral("armor"), QStringLiteral("bonus"), QString(), 2015, QString()},
		{QStringLiteral("doom"), QStringLiteral("armor"), QStringLiteral("green"), QString(), 2018, QString()},
		{QStringLiteral("doom"), QStringLiteral("armor"), QStringLiteral("blue"), QString(), 2019, QString()},
		{QStringLiteral("doom"), QStringLiteral("powerup"), QStringLiteral("berserk"), QString(), 2023, QString()},
		{QStringLiteral("doom"), QStringLiteral("powerup"), QStringLiteral("invulnerability"), QString(), 2022, QString()},
		{QStringLiteral("doom"), QStringLiteral("powerup"), QStringLiteral("invisibility"), QString(), 2024, QString()},
		{QStringLiteral("doom"), QStringLiteral("powerup"), QStringLiteral("radsuit"), QString(), 2025, QString()},
		{QStringLiteral("doom"), QStringLiteral("powerup"), QStringLiteral("lightamp"), QString(), 2045, QString()},
		{QStringLiteral("doom"), QStringLiteral("monster"), QStringLiteral("zombieman"), QString(), 3004, QString()},
		{QStringLiteral("doom"), QStringLiteral("monster"), QStringLiteral("shotgunguy"), QString(), 9, QString()},
		{QStringLiteral("doom"), QStringLiteral("monster"), QStringLiteral("chaingunner"), QString(), 65, QStringLiteral("2")},
		{QStringLiteral("doom"), QStringLiteral("monster"), QStringLiteral("imp"), QString(), 3001, QString()},
		{QStringLiteral("doom"), QStringLiteral("monster"), QStringLiteral("demon"), QString(), 3002, QString()},
		{QStringLiteral("doom"), QStringLiteral("monster"), QStringLiteral("spectre"), QString(), 58, QString()},
		{QStringLiteral("doom"), QStringLiteral("monster"), QStringLiteral("lostsoul"), QString(), 3006, QString()},
		{QStringLiteral("doom"), QStringLiteral("monster"), QStringLiteral("cacodemon"), QString(), 3005, QString()},
		{QStringLiteral("doom"), QStringLiteral("monster"), QStringLiteral("hellknight"), QString(), 69, QStringLiteral("2")},
		{QStringLiteral("doom"), QStringLiteral("monster"), QStringLiteral("baron"), QString(), 3003, QString()},
		{QStringLiteral("doom"), QStringLiteral("monster"), QStringLiteral("revenant"), QString(), 66, QStringLiteral("2")},
		{QStringLiteral("doom"), QStringLiteral("monster"), QStringLiteral("mancubus"), QString(), 67, QStringLiteral("2")},
		{QStringLiteral("doom"), QStringLiteral("monster"), QStringLiteral("arachnotron"), QString(), 68, QStringLiteral("2")},
		{QStringLiteral("doom"), QStringLiteral("monster"), QStringLiteral("painelemental"), QString(), 71, QStringLiteral("2")},
		{QStringLiteral("doom"), QStringLiteral("monster"), QStringLiteral("archvile"), QString(), 64, QStringLiteral("2")},
	};
	return entries;
}

bool itemEntryFor(const QString& game, const QString& kind, const QString& item, ItemEntry* out)
{
	const QString wanted = normalizedItemId(item);
	for (const ItemEntry& entry : itemEntries()) {
		if (entry.game == game && entry.kind == kind && (entry.id == wanted || (wanted.isEmpty() && entry.id.isEmpty()))) {
			if (out) {
				*out = entry;
			}
			return true;
		}
	}
	return false;
}

QString normalizedItemId(const QString& item)
{
	QString id = item.trimmed().toLower();
	static const QRegularExpression separators(QStringLiteral("[\\s_\\-]+"));
	id.remove(separators);
	// Words people (and models) use for the ids above.
	static const QHash<QString, QString> aliases = {
		{QStringLiteral("rl"), QStringLiteral("rocketlauncher")},
		{QStringLiteral("rocket"), QStringLiteral("rocketlauncher")},
		{QStringLiteral("rockets"), QStringLiteral("rockets")},
		{QStringLiteral("gl"), QStringLiteral("grenadelauncher")},
		{QStringLiteral("lg"), QStringLiteral("lightning")},
		{QStringLiteral("lightninggun"), QStringLiteral("lightning")},
		{QStringLiteral("thunderbolt"), QStringLiteral("lightning")},
		{QStringLiteral("ssg"), QStringLiteral("supershotgun")},
		{QStringLiteral("doublebarrel"), QStringLiteral("supershotgun")},
		{QStringLiteral("sng"), QStringLiteral("supernailgun")},
		{QStringLiteral("perforator"), QStringLiteral("supernailgun")},
		{QStringLiteral("rail"), QStringLiteral("railgun")},
		{QStringLiteral("plasma"), QStringLiteral("plasmagun")},
		{QStringLiteral("mega"), QStringLiteral("mega")},
		{QStringLiteral("quaddamage"), QStringLiteral("quad")},
		{QStringLiteral("soldier"), QStringLiteral("grunt")},
		{QStringLiteral("dog"), QStringLiteral("rottweiler")},
		{QStringLiteral("hellknight"), QStringLiteral("hellknight")},
		{QStringLiteral("caco"), QStringLiteral("cacodemon")},
		{QStringLiteral("pinky"), QStringLiteral("demon")},
		{QStringLiteral("baronofhell"), QStringLiteral("baron")},
	};
	return aliases.value(id, id);
}

int levelForHeight(const QString& height)
{
	return height == QStringLiteral("low") ? 128 : height == QStringLiteral("tall") ? 256 : 192;
}

} // namespace levelgen

using namespace levelgen;

namespace {

const QStringList kRoles = {
	QStringLiteral("start"),
	QStringLiteral("exit"),
	QStringLiteral("arena"),
	QStringLiteral("hub"),
	QStringLiteral("hall"),
	QStringLiteral("storage"),
	QStringLiteral("secret"),
	QStringLiteral("overlook"),
};
const QStringList kSizes = {QStringLiteral("small"), QStringLiteral("medium"), QStringLiteral("large")};
const QStringList kHeights = {QStringLiteral("low"), QStringLiteral("normal"), QStringLiteral("tall")};
const QStringList kShapes = {QStringLiteral("box"), QStringLiteral("pillars"), QStringLiteral("pit"), QStringLiteral("platform")};
const QStringList kLiquids = {QStringLiteral("none"), QStringLiteral("water"), QStringLiteral("slime"), QStringLiteral("lava")};
const QStringList kLighting = {QStringLiteral("dim"), QStringLiteral("normal"), QStringLiteral("bright")};
const QStringList kConnectionKinds = {QStringLiteral("corridor"), QStringLiteral("stairs"), QStringLiteral("open")};
const QStringList kPlacementKinds = {
	QStringLiteral("player-start"),
	QStringLiteral("deathmatch-start"),
	QStringLiteral("weapon"),
	QStringLiteral("ammo"),
	QStringLiteral("health"),
	QStringLiteral("armor"),
	QStringLiteral("powerup"),
	QStringLiteral("monster"),
	QStringLiteral("exit"),
};
constexpr int kMaxRooms = 16;

QJsonObject stringEnum(const QStringList& values, const QString& description)
{
	return QJsonObject {
		{QStringLiteral("type"), QStringLiteral("string")},
		{QStringLiteral("enum"), QJsonArray::fromStringList(values)},
		{QStringLiteral("description"), description},
	};
}

QJsonObject closedObject(const QJsonObject& properties)
{
	QJsonArray required;
	for (auto it = properties.constBegin(); it != properties.constEnd(); ++it) {
		required.append(it.key());
	}
	return QJsonObject {
		{QStringLiteral("type"), QStringLiteral("object")},
		{QStringLiteral("properties"), properties},
		{QStringLiteral("required"), required},
		{QStringLiteral("additionalProperties"), false},
	};
}

QString wordsOf(const QString& prompt)
{
	return QStringLiteral(" %1 ").arg(prompt.toLower().replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral(" ")));
}

bool hasWord(const QString& words, const QStringList& options)
{
	for (const QString& option : options) {
		if (words.contains(QStringLiteral(" %1 ").arg(option))) {
			return true;
		}
	}
	return false;
}

int numberBefore(const QString& prompt, const QString& nouns)
{
	const QRegularExpression pattern(QStringLiteral("(\\d+)\\s*[- ]?\\s*(?:%1)\\b").arg(nouns), QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch match = pattern.match(prompt);
	return match.hasMatch() ? match.captured(1).toInt() : 0;
}

QString titleFor(const LevelGenerationSpec& spec, LevelRandom* random)
{
	static const QHash<QString, QStringList> nouns = {
		{QStringLiteral("base"), {QStringLiteral("Outpost"), QStringLiteral("Facility"), QStringLiteral("Installation"), QStringLiteral("Complex"), QStringLiteral("Relay")}},
		{QStringLiteral("medieval"), {QStringLiteral("Keep"), QStringLiteral("Crypt"), QStringLiteral("Sanctum"), QStringLiteral("Bastion"), QStringLiteral("Cloister")}},
		{QStringLiteral("metal"), {QStringLiteral("Forge"), QStringLiteral("Foundry"), QStringLiteral("Vault"), QStringLiteral("Engine"), QStringLiteral("Furnace")}},
		{QStringLiteral("hell"), {QStringLiteral("Pit"), QStringLiteral("Abyss"), QStringLiteral("Altar"), QStringLiteral("Furnace"), QStringLiteral("Gate")}},
		{QStringLiteral("cave"), {QStringLiteral("Hollow"), QStringLiteral("Grotto"), QStringLiteral("Warren"), QStringLiteral("Delve"), QStringLiteral("Chasm")}},
	};
	static const QStringList adjectives = {
		QStringLiteral("Rusted"), QStringLiteral("Sunken"), QStringLiteral("Forgotten"), QStringLiteral("Shattered"), QStringLiteral("Burning"),
		QStringLiteral("Silent"), QStringLiteral("Hollow"), QStringLiteral("Iron"), QStringLiteral("Black"), QStringLiteral("Drowned"),
	};
	const QStringList& options = nouns.contains(spec.theme) ? nouns[spec.theme] : nouns[QStringLiteral("base")];
	return QStringLiteral("The %1 %2").arg(random->pick(adjectives), random->pick(options));
}

} // namespace

QStringList levelGenerationGameIds()
{
	return {QStringLiteral("quake"), QStringLiteral("quake2"), QStringLiteral("quake3"), QStringLiteral("doom")};
}

QStringList levelGenerationModeIds()
{
	return {QStringLiteral("single-player"), QStringLiteral("deathmatch"), QStringLiteral("duel")};
}

QStringList levelGenerationThemeIds()
{
	return {QStringLiteral("base"), QStringLiteral("medieval"), QStringLiteral("metal"), QStringLiteral("hell"), QStringLiteral("cave")};
}

LevelGenerationSpec normalizedLevelGenerationSpec(const LevelGenerationSpec& input)
{
	LevelGenerationSpec spec = input;
	const QString words = wordsOf(spec.prompt);

	QString game = spec.game.trimmed().toLower();
	if (game == QStringLiteral("q1") || game == QStringLiteral("quake1")) {
		game = QStringLiteral("quake");
	} else if (game == QStringLiteral("q2")) {
		game = QStringLiteral("quake2");
	} else if (game == QStringLiteral("q3") || game == QStringLiteral("quake3arena")) {
		game = QStringLiteral("quake3");
	} else if (game == QStringLiteral("doom2") || game == QStringLiteral("idtech1")) {
		game = QStringLiteral("doom");
	}
	if (!levelGenerationGameIds().contains(game)) {
		if (words.contains(QStringLiteral(" quake 3 ")) || words.contains(QStringLiteral(" quake iii ")) || hasWord(words, {QStringLiteral("q3"), QStringLiteral("quake3"), QStringLiteral("q3a")})) {
			game = QStringLiteral("quake3");
		} else if (words.contains(QStringLiteral(" quake 2 ")) || words.contains(QStringLiteral(" quake ii ")) || hasWord(words, {QStringLiteral("q2"), QStringLiteral("quake2"), QStringLiteral("strogg")})) {
			game = QStringLiteral("quake2");
		} else if (hasWord(words, {QStringLiteral("doom"), QStringLiteral("doom2"), QStringLiteral("idtech1")})) {
			game = QStringLiteral("doom");
		} else {
			game = QStringLiteral("quake");
		}
	}
	spec.game = game;

	if (!levelGenerationModeIds().contains(spec.mode)) {
		if (hasWord(words, {QStringLiteral("duel"), QStringLiteral("1v1"), QStringLiteral("tourney"), QStringLiteral("tournament")})) {
			spec.mode = QStringLiteral("duel");
		} else if (hasWord(words, {QStringLiteral("deathmatch"), QStringLiteral("dm"), QStringLiteral("multiplayer"), QStringLiteral("ffa"), QStringLiteral("frag")})) {
			spec.mode = QStringLiteral("deathmatch");
		} else if (hasWord(words, {QStringLiteral("single"), QStringLiteral("sp"), QStringLiteral("campaign"), QStringLiteral("monsters"), QStringLiteral("coop")})) {
			spec.mode = QStringLiteral("single-player");
		} else {
			// Quake III has no monsters: its levels are arenas.
			spec.mode = game == QStringLiteral("quake3") ? QStringLiteral("deathmatch") : QStringLiteral("single-player");
		}
	}
	if (game == QStringLiteral("quake3") && spec.mode == QStringLiteral("single-player")) {
		spec.mode = QStringLiteral("deathmatch");
	}

	if (!levelGenerationThemeIds().contains(spec.theme)) {
		if (hasWord(words, {QStringLiteral("hell"), QStringLiteral("hellish"), QStringLiteral("demonic"), QStringLiteral("infernal"), QStringLiteral("inferno")})) {
			spec.theme = QStringLiteral("hell");
		} else if (hasWord(words, {QStringLiteral("castle"), QStringLiteral("medieval"), QStringLiteral("gothic"), QStringLiteral("dungeon"), QStringLiteral("crypt"),
					   QStringLiteral("cathedral"), QStringLiteral("temple"), QStringLiteral("keep"), QStringLiteral("fortress")})) {
			spec.theme = QStringLiteral("medieval");
		} else if (hasWord(words, {QStringLiteral("cave"), QStringLiteral("cavern"), QStringLiteral("caves"), QStringLiteral("mine"), QStringLiteral("grotto"), QStringLiteral("rock")})) {
			spec.theme = QStringLiteral("cave");
		} else if (hasWord(words, {QStringLiteral("metal"), QStringLiteral("runic"), QStringLiteral("rune"), QStringLiteral("forge"), QStringLiteral("foundry")})) {
			spec.theme = QStringLiteral("metal");
		} else {
			spec.theme = QStringLiteral("base");
		}
	}

	if (!kLiquids.contains(spec.liquid)) {
		if (hasWord(words, {QStringLiteral("lava"), QStringLiteral("magma"), QStringLiteral("molten")})) {
			spec.liquid = QStringLiteral("lava");
		} else if (hasWord(words, {QStringLiteral("slime"), QStringLiteral("toxic"), QStringLiteral("nukage"), QStringLiteral("acid"), QStringLiteral("sewer")})) {
			spec.liquid = QStringLiteral("slime");
		} else if (hasWord(words, {QStringLiteral("water"), QStringLiteral("flooded"), QStringLiteral("sunken"), QStringLiteral("aqueduct")})) {
			spec.liquid = QStringLiteral("water");
		} else {
			spec.liquid = spec.theme == QStringLiteral("hell") ? QStringLiteral("lava") : spec.theme == QStringLiteral("base") ? QStringLiteral("slime") : QStringLiteral("water");
		}
	}

	if (!kSizes.contains(spec.size)) {
		if (hasWord(words, {QStringLiteral("tiny"), QStringLiteral("small"), QStringLiteral("compact"), QStringLiteral("tight")})) {
			spec.size = QStringLiteral("small");
		} else if (hasWord(words, {QStringLiteral("large"), QStringLiteral("big"), QStringLiteral("huge"), QStringLiteral("sprawling"), QStringLiteral("epic")})) {
			spec.size = QStringLiteral("large");
		} else {
			spec.size = QStringLiteral("medium");
		}
	}
	if (spec.rooms <= 0) {
		spec.rooms = numberBefore(spec.prompt, QStringLiteral("rooms?|areas?|arenas?|chambers?"));
	}
	if (spec.rooms <= 0) {
		const bool duel = spec.mode == QStringLiteral("duel");
		spec.rooms = spec.size == QStringLiteral("small") ? (duel ? 3 : 4) : spec.size == QStringLiteral("large") ? 10 : (duel ? 4 : 7);
	}
	spec.rooms = std::clamp(spec.rooms, 2, kMaxRooms);

	if (!QStringList {QStringLiteral("flat"), QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high")}.contains(spec.verticality)) {
		if (hasWord(words, {QStringLiteral("flat"), QStringLiteral("level"), QStringLiteral("single floor")})) {
			spec.verticality = QStringLiteral("flat");
		} else if (hasWord(words, {QStringLiteral("vertical"), QStringLiteral("verticality"), QStringLiteral("tall"), QStringLiteral("towers"), QStringLiteral("multilevel"),
					   QStringLiteral("height")})) {
			spec.verticality = QStringLiteral("high");
		} else {
			spec.verticality = QStringLiteral("medium");
		}
	}
	if (spec.players <= 0) {
		spec.players = numberBefore(spec.prompt, QStringLiteral("players?|people|spawns?"));
	}
	if (spec.players <= 0) {
		spec.players = spec.mode == QStringLiteral("duel") ? 2 : spec.mode == QStringLiteral("deathmatch") ? 8 : 1;
	}
	spec.players = std::clamp(spec.players, 1, 32);

	if (!QStringList {QStringLiteral("none"), QStringLiteral("light"), QStringLiteral("normal"), QStringLiteral("heavy")}.contains(spec.monsters)) {
		if (spec.mode != QStringLiteral("single-player") || hasWord(words, {QStringLiteral("peaceful"), QStringLiteral("empty")})
			|| words.contains(QStringLiteral(" no monsters "))) {
			spec.monsters = QStringLiteral("none");
		} else if (hasWord(words, {QStringLiteral("horde"), QStringLiteral("hard"), QStringLiteral("nightmare"), QStringLiteral("swarming")})) {
			spec.monsters = QStringLiteral("heavy");
		} else if (hasWord(words, {QStringLiteral("easy"), QStringLiteral("quiet"), QStringLiteral("few")})) {
			spec.monsters = QStringLiteral("light");
		} else {
			spec.monsters = QStringLiteral("normal");
		}
	}

	if (spec.seed < 0) {
		static const QRegularExpression seedPattern(QStringLiteral("\\bseed\\s*[:=#]?\\s*(\\d{1,12})\\b"), QRegularExpression::CaseInsensitiveOption);
		const QRegularExpressionMatch match = seedPattern.match(spec.prompt);
		if (match.hasMatch()) {
			spec.seed = match.captured(1).toLongLong();
		} else {
			// The same words make the same level.
			const QByteArray digest = QCryptographicHash::hash(spec.prompt.simplified().toLower().toUtf8(), QCryptographicHash::Sha256);
			quint32 value = 0;
			for (int index = 0; index < 4; ++index) {
				value = (value << 8) | quint8(digest.at(index));
			}
			spec.seed = qint64(value % 1000000000u);
		}
	}

	if (spec.title.trimmed().isEmpty()) {
		static const QRegularExpression quoted(QStringLiteral("(?:called|named|titled)\\s+[\"']([^\"']{2,60})[\"']"), QRegularExpression::CaseInsensitiveOption);
		const QRegularExpressionMatch match = quoted.match(spec.prompt);
		if (match.hasMatch()) {
			spec.title = match.captured(1).trimmed();
		} else {
			LevelRandom random(quint64(spec.seed) ^ 0x7469746c65ull);
			spec.title = titleFor(spec, &random);
		}
	}
	return spec;
}

QStringList levelGenerationItemIds(const QString& game, const QString& kind)
{
	QStringList ids;
	for (const ItemEntry& entry : itemEntries()) {
		if (entry.game == game && entry.kind == kind && !entry.id.isEmpty() && !ids.contains(entry.id)) {
			ids << entry.id;
		}
	}
	return ids;
}

QJsonObject levelSemanticPlanSchema(const QString& game)
{
	QStringList items {QString()};
	for (const QString& kind : kPlacementKinds) {
		for (const QString& id : levelGenerationItemIds(game, kind)) {
			if (!items.contains(id)) {
				items << id;
			}
		}
	}
	const QJsonObject room = closedObject({
		{QStringLiteral("id"), QJsonObject {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 24},
								   {QStringLiteral("description"), QStringLiteral("Short unique id such as r1.")}}},
		{QStringLiteral("name"), QJsonObject {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 60},
									 {QStringLiteral("description"), QStringLiteral("Evocative room name, for the designer.")}}},
		{QStringLiteral("role"), stringEnum(kRoles, QStringLiteral("What the room is for in the level's flow."))},
		{QStringLiteral("size"), stringEnum(kSizes, QStringLiteral("Floor area: small about 224 units across, medium 320, large 480."))},
		{QStringLiteral("height"), stringEnum(kHeights, QStringLiteral("Ceiling height: low 128 units, normal 192, tall 256."))},
		{QStringLiteral("level"), QJsonObject {{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 3},
									  {QStringLiteral("description"), QStringLiteral("Floor level, 0 to 3; each level is 64 units higher, joined by stairs.")}}},
		{QStringLiteral("shape"), stringEnum(kShapes, QStringLiteral("box; pillars to break sight lines; pit, a sunken liquid pool with walkways; platform, a stepped central dais."))},
		{QStringLiteral("liquid"), stringEnum(kLiquids, QStringLiteral("The pit's liquid; none unless the shape is pit."))},
		{QStringLiteral("lighting"), stringEnum(kLighting, QStringLiteral("Mood lighting."))},
	});
	const QJsonObject connection = closedObject({
		{QStringLiteral("from"), QJsonObject {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("A room id.")}}},
		{QStringLiteral("to"), QJsonObject {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Another room id.")}}},
		{QStringLiteral("kind"), stringEnum(kConnectionKinds, QStringLiteral("corridor; stairs where levels differ; open for a wide archway."))},
	});
	const QJsonObject placement = closedObject({
		{QStringLiteral("room"), QJsonObject {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("The room id it goes in.")}}},
		{QStringLiteral("kind"), stringEnum(kPlacementKinds, QStringLiteral("What is placed."))},
		{QStringLiteral("item"), stringEnum(items, QStringLiteral("Which weapon, ammo, health, armor, powerup, or monster; empty for starts and the exit."))},
		{QStringLiteral("count"), QJsonObject {{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 8}}},
	});
	return closedObject({
		{QStringLiteral("title"), QJsonObject {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 60}}},
		{QStringLiteral("summary"), QJsonObject {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 600},
										{QStringLiteral("description"), QStringLiteral("Two or three sentences on the level's flow and highlights.")}}},
		{QStringLiteral("theme"), stringEnum(levelGenerationThemeIds(), QStringLiteral("Visual theme."))},
		{QStringLiteral("rooms"), QJsonObject {{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("minItems"), 2}, {QStringLiteral("maxItems"), kMaxRooms},
									  {QStringLiteral("items"), room}}},
		{QStringLiteral("connections"), QJsonObject {{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("minItems"), 1}, {QStringLiteral("maxItems"), 32},
											{QStringLiteral("items"), connection}}},
		{QStringLiteral("placements"), QJsonObject {{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("maxItems"), 96}, {QStringLiteral("items"), placement}}},
	});
}

QJsonObject levelSemanticPlanJson(const LevelSemanticPlan& plan)
{
	QJsonArray rooms;
	for (const LevelPlanRoom& room : plan.rooms) {
		rooms.append(QJsonObject {
			{QStringLiteral("id"), room.id},
			{QStringLiteral("name"), room.name},
			{QStringLiteral("role"), room.role},
			{QStringLiteral("size"), room.size},
			{QStringLiteral("height"), room.height},
			{QStringLiteral("level"), room.level},
			{QStringLiteral("shape"), room.shape},
			{QStringLiteral("liquid"), room.liquid},
			{QStringLiteral("lighting"), room.lighting},
		});
	}
	QJsonArray connections;
	for (const LevelPlanConnection& connection : plan.connections) {
		connections.append(QJsonObject {{QStringLiteral("from"), connection.from}, {QStringLiteral("to"), connection.to}, {QStringLiteral("kind"), connection.kind}});
	}
	QJsonArray placements;
	for (const LevelPlanPlacement& placement : plan.placements) {
		placements.append(QJsonObject {
			{QStringLiteral("room"), placement.room},
			{QStringLiteral("kind"), placement.kind},
			{QStringLiteral("item"), placement.item},
			{QStringLiteral("count"), placement.count},
		});
	}
	QJsonObject object {
		{QStringLiteral("title"), plan.title},
		{QStringLiteral("summary"), plan.summary},
		{QStringLiteral("theme"), plan.theme},
		{QStringLiteral("rooms"), rooms},
		{QStringLiteral("connections"), connections},
		{QStringLiteral("placements"), placements},
	};
	return object;
}

bool levelSemanticPlanFromJson(const QJsonObject& object, LevelSemanticPlan* plan, QStringList* problems)
{
	if (!plan) {
		return false;
	}
	LevelSemanticPlan parsed;
	parsed.title = object.value(QStringLiteral("title")).toString().trimmed();
	parsed.summary = object.value(QStringLiteral("summary")).toString().trimmed();
	parsed.theme = object.value(QStringLiteral("theme")).toString().trimmed();
	for (const QJsonValue& value : object.value(QStringLiteral("rooms")).toArray()) {
		const QJsonObject room = value.toObject();
		LevelPlanRoom entry;
		entry.id = room.value(QStringLiteral("id")).toString().trimmed();
		entry.name = room.value(QStringLiteral("name")).toString().trimmed();
		entry.role = room.value(QStringLiteral("role")).toString().trimmed().toLower();
		entry.size = room.value(QStringLiteral("size")).toString(entry.size).trimmed().toLower();
		entry.height = room.value(QStringLiteral("height")).toString(entry.height).trimmed().toLower();
		entry.level = room.value(QStringLiteral("level")).toInt(0);
		entry.shape = room.value(QStringLiteral("shape")).toString(entry.shape).trimmed().toLower();
		entry.liquid = room.value(QStringLiteral("liquid")).toString(entry.liquid).trimmed().toLower();
		entry.lighting = room.value(QStringLiteral("lighting")).toString(entry.lighting).trimmed().toLower();
		parsed.rooms.push_back(entry);
	}
	for (const QJsonValue& value : object.value(QStringLiteral("connections")).toArray()) {
		const QJsonObject connection = value.toObject();
		parsed.connections.push_back({connection.value(QStringLiteral("from")).toString().trimmed(), connection.value(QStringLiteral("to")).toString().trimmed(),
			connection.value(QStringLiteral("kind")).toString(QStringLiteral("corridor")).trimmed().toLower()});
	}
	for (const QJsonValue& value : object.value(QStringLiteral("placements")).toArray()) {
		const QJsonObject placement = value.toObject();
		parsed.placements.push_back({placement.value(QStringLiteral("room")).toString().trimmed(), placement.value(QStringLiteral("kind")).toString().trimmed().toLower(),
			placement.value(QStringLiteral("item")).toString().trimmed(), placement.value(QStringLiteral("count")).toInt(1)});
	}
	if (problems) {
		// The game is unknown here; any game's item ids may appear, so items are
		// checked by the repair, not the schema.
		QJsonObject schema = levelSemanticPlanSchema(QStringLiteral("quake"));
		QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
		QJsonObject placements = properties.value(QStringLiteral("placements")).toObject();
		QJsonObject placementItems = placements.value(QStringLiteral("items")).toObject();
		QJsonObject placementProperties = placementItems.value(QStringLiteral("properties")).toObject();
		placementProperties.insert(QStringLiteral("item"), QJsonObject {{QStringLiteral("type"), QStringLiteral("string")}});
		placementItems.insert(QStringLiteral("properties"), placementProperties);
		placements.insert(QStringLiteral("items"), placementItems);
		properties.insert(QStringLiteral("placements"), placements);
		schema.insert(QStringLiteral("properties"), properties);
		*problems = aiJsonSchemaProblems(object, schema);
	}
	*plan = parsed;
	return !parsed.rooms.isEmpty();
}

LevelSemanticPlan rulesLevelPlan(const LevelGenerationSpec& input)
{
	const LevelGenerationSpec spec = normalizedLevelGenerationSpec(input);
	LevelRandom random(quint64(spec.seed) * 0x2545F4914F6CDD1Dull + 0x706c616eull);
	LevelSemanticPlan plan;
	plan.planner = QStringLiteral("rules/v1");
	plan.title = spec.title;
	plan.theme = spec.theme;
	const bool singlePlayer = spec.mode == QStringLiteral("single-player");
	const int count = spec.rooms;
	const int maxLevel = spec.verticality == QStringLiteral("flat") ? 0 : spec.verticality == QStringLiteral("low") ? 1 : spec.verticality == QStringLiteral("medium") ? 2 : 3;

	// Feature rooms take what the brief asks for: pits or pools, pillars or
	// columns, platforms or a dais; any of them otherwise.
	const QString words = wordsOf(spec.prompt);
	QStringList features;
	if (hasWord(words, {QStringLiteral("pit"), QStringLiteral("pits"), QStringLiteral("pool"), QStringLiteral("pools"), QStringLiteral("moat")})) {
		features << QStringLiteral("pit");
	}
	if (hasWord(words, {QStringLiteral("pillar"), QStringLiteral("pillars"), QStringLiteral("column"), QStringLiteral("columns"), QStringLiteral("pillared")})) {
		features << QStringLiteral("pillars");
	}
	if (hasWord(words, {QStringLiteral("platform"), QStringLiteral("platforms"), QStringLiteral("dais"), QStringLiteral("altar"), QStringLiteral("ziggurat")})) {
		features << QStringLiteral("platform");
	}
	if (features.isEmpty()) {
		features = {QStringLiteral("pillars"), QStringLiteral("pit"), QStringLiteral("platform")};
	}
	// Rooms: a start and an exit for single player, an arena at the heart of
	// a deathmatch level, and a mix between.
	int level = 0;
	for (int index = 0; index < count; ++index) {
		LevelPlanRoom room;
		room.id = QStringLiteral("r%1").arg(index + 1);
		if (singlePlayer && index == 0) {
			room.role = QStringLiteral("start");
			room.size = QStringLiteral("small");
		} else if (singlePlayer && index == count - 1) {
			room.role = QStringLiteral("exit");
			room.size = QStringLiteral("small");
		} else if (index == count / 2) {
			room.role = QStringLiteral("arena");
			room.size = QStringLiteral("large");
		} else {
			static const QStringList middle = {QStringLiteral("hall"), QStringLiteral("hub"), QStringLiteral("storage"), QStringLiteral("overlook"), QStringLiteral("hall")};
			room.role = random.pick(middle);
			room.size = room.role == QStringLiteral("storage") ? QStringLiteral("small") : random.chance(0.3) ? QStringLiteral("large") : QStringLiteral("medium");
		}
		if (index > 0 && maxLevel > 0) {
			// High verticality changes level more often than it stays.
			const bool change = spec.verticality == QStringLiteral("high") ? random.chance(0.7) : random.chance(0.4);
			const int step = change ? (random.chance(0.5) ? 1 : -1) : 0;
			level = std::clamp(level + (level + step < 0 || level + step > maxLevel ? -step : step), 0, maxLevel);
		}
		room.level = room.role == QStringLiteral("overlook") ? std::min(maxLevel, level + 1) : level;
		room.height = room.size == QStringLiteral("large") ? QStringLiteral("tall") : random.chance(0.25) ? QStringLiteral("low") : QStringLiteral("normal");
		if (room.role == QStringLiteral("arena") || (room.size == QStringLiteral("large") && random.chance(0.6))) {
			room.shape = random.pick(features);
		} else if (room.size == QStringLiteral("medium") && random.chance(0.35)) {
			room.shape = QStringLiteral("pillars");
		}
		room.liquid = room.shape == QStringLiteral("pit") ? spec.liquid : QStringLiteral("none");
		if (room.liquid == QStringLiteral("none") && room.shape == QStringLiteral("pit")) {
			room.liquid = QStringLiteral("water");
		}
		room.lighting = room.role == QStringLiteral("secret") || random.chance(0.2) ? QStringLiteral("dim") : room.role == QStringLiteral("arena") ? QStringLiteral("bright")
																															: QStringLiteral("normal");
		plan.rooms.push_back(room);
	}

	// A main route through every room, then loops back so routes branch.
	for (int index = 1; index < count; ++index) {
		const LevelPlanRoom& from = plan.rooms[index - 1];
		const LevelPlanRoom& to = plan.rooms[index];
		plan.connections.push_back({from.id, to.id, from.level != to.level ? QStringLiteral("stairs") : QStringLiteral("corridor")});
	}
	// Loops never reach a single-player exit, which stays at the end.
	const int last = singlePlayer ? count - 2 : count - 1;
	const int loops = singlePlayer ? std::max(1, count / 4) : std::max(1, count / 2);
	QSet<QPair<int, int>> looped;
	for (int loop = 0; loop < loops * 6 && looped.size() < loops && last >= 2; ++loop) {
		const int a = random.range(0, last - 2);
		const int b = random.range(a + 2, last);
		// A loop between floors needs a long straight run for its stairs;
		// prefer rooms on one level, as long routes rarely run straight.
		if (looped.contains({a, b}) || (plan.rooms[a].level != plan.rooms[b].level && loop < loops * 4)) {
			continue;
		}
		looped.insert({a, b});
		plan.connections.push_back({plan.rooms[a].id, plan.rooms[b].id, plan.rooms[a].level != plan.rooms[b].level ? QStringLiteral("stairs") : QStringLiteral("corridor")});
	}

	// What goes where.
	const QString game = spec.game;
	if (singlePlayer) {
		plan.placements.push_back({plan.rooms.first().id, QStringLiteral("player-start"), QString(), 1});
		if (game != QStringLiteral("quake3")) {
			plan.placements.push_back({plan.rooms.last().id, QStringLiteral("exit"), QString(), 1});
		}
		const QStringList weapons = levelGenerationItemIds(game, QStringLiteral("weapon"));
		QStringList pool;
		if (game == QStringLiteral("quake")) {
			pool = spec.theme == QStringLiteral("base") ? QStringList {QStringLiteral("grunt"), QStringLiteral("rottweiler"), QStringLiteral("enforcer")}
				 : spec.theme == QStringLiteral("hell")   ? QStringList {QStringLiteral("fiend"), QStringLiteral("deathknight"), QStringLiteral("vore"), QStringLiteral("ogre")}
														   : QStringList {QStringLiteral("knight"), QStringLiteral("zombie"), QStringLiteral("ogre"), QStringLiteral("scrag")};
		} else if (game == QStringLiteral("quake2")) {
			pool = {QStringLiteral("guard"), QStringLiteral("shotgunguard"), QStringLiteral("enforcer"), QStringLiteral("gunner"), QStringLiteral("berserker"), QStringLiteral("parasite")};
		} else if (game == QStringLiteral("doom")) {
			pool = {QStringLiteral("zombieman"), QStringLiteral("shotgunguy"), QStringLiteral("imp"), QStringLiteral("demon"), QStringLiteral("cacodemon")};
		}
		const int perRoom = spec.monsters == QStringLiteral("none") ? 0 : spec.monsters == QStringLiteral("light") ? 1 : spec.monsters == QStringLiteral("heavy") ? 4 : 2;
		int weaponIndex = 0;
		for (int index = 1; index < count; ++index) {
			const LevelPlanRoom& room = plan.rooms[index];
			const int sizeBonus = room.size == QStringLiteral("large") ? 2 : room.size == QStringLiteral("small") ? -1 : 0;
			const int monsters = std::max(0, perRoom + sizeBonus);
			if (monsters > 0 && !pool.isEmpty()) {
				// Tougher monsters further in.
				const int strongest = std::clamp(1 + index * int(pool.size()) / count, 1, int(pool.size()));
				plan.placements.push_back({room.id, QStringLiteral("monster"), pool[random.range(0, strongest - 1)], monsters});
			}
			if ((index % 2 == 1 || room.role == QStringLiteral("arena")) && weaponIndex < weapons.size() && room.role != QStringLiteral("exit")) {
				plan.placements.push_back({room.id, QStringLiteral("weapon"), weapons[weaponIndex], 1});
				++weaponIndex;
			}
			plan.placements.push_back({room.id, random.chance(0.5) ? QStringLiteral("ammo") : QStringLiteral("health"), QString(), random.range(1, 2)});
			if (room.role == QStringLiteral("overlook") || room.role == QStringLiteral("secret")) {
				plan.placements.push_back({room.id, QStringLiteral("armor"), QString(), 1});
			}
		}
	} else {
		// Spread the starts so every room has some; weapons in every room,
		// the strongest where the fighting is.
		for (int start = 0; start < std::max(spec.players, spec.mode == QStringLiteral("duel") ? 2 : 4); ++start) {
			plan.placements.push_back({plan.rooms[start % count].id, QStringLiteral("deathmatch-start"), QString(), 1});
		}
		QStringList weapons = levelGenerationItemIds(game, QStringLiteral("weapon"));
		weapons.removeAll(QStringLiteral("machinegun"));
		weapons.removeAll(QStringLiteral("chainsaw"));
		weapons.removeAll(QStringLiteral("bfg"));
		for (int index = 0; index < count; ++index) {
			const LevelPlanRoom& room = plan.rooms[index];
			if (!weapons.isEmpty()) {
				plan.placements.push_back({room.id, QStringLiteral("weapon"), weapons[index % weapons.size()], 1});
			}
			plan.placements.push_back({room.id, QStringLiteral("ammo"), QString(), random.range(1, 3)});
			plan.placements.push_back({room.id, QStringLiteral("health"), QString(), random.range(1, 2)});
		}
		const LevelPlanRoom& arena = plan.rooms[count / 2];
		plan.placements.push_back({arena.id, QStringLiteral("armor"), QString(), 1});
		plan.placements.push_back({plan.rooms[(count / 2 + count / 3 + 1) % count].id, QStringLiteral("armor"), QString(), 1});
		if (spec.mode == QStringLiteral("deathmatch")) {
			plan.placements.push_back({plan.rooms[(count / 2 + 1) % count].id, QStringLiteral("powerup"), QString(), 1});
		}
	}
	plan.summary = singlePlayer
		? QStringLiteral("%1 rooms from the start to the exit, with %2 loop(s) back along the way.").arg(count).arg(int(plan.connections.size()) - (count - 1))
		: QStringLiteral("%1 rooms around a central arena, linked in a ring with cross routes for %2 players.").arg(count).arg(spec.players);
	repairLevelSemanticPlan(&plan, spec);
	return plan;
}

QStringList repairLevelSemanticPlan(LevelSemanticPlan* plan, const LevelGenerationSpec& input)
{
	QStringList repairs;
	if (!plan) {
		return repairs;
	}
	const LevelGenerationSpec spec = normalizedLevelGenerationSpec(input);
	const bool singlePlayer = spec.mode == QStringLiteral("single-player");
	const auto note = [&repairs](const QString& text) { repairs << text; };

	if (plan->title.trimmed().isEmpty()) {
		plan->title = spec.title;
	}
	if (!levelGenerationThemeIds().contains(plan->theme)) {
		if (!plan->theme.isEmpty()) {
			note(QCoreApplication::translate("VibeStudioLevelGeneration", "Theme \"%1\" is unknown; used %2.").arg(plan->theme, spec.theme));
		}
		plan->theme = spec.theme;
	}

	// Rooms: unique ids, known values, within bounds.
	if (plan->rooms.size() > kMaxRooms) {
		note(QCoreApplication::translate("VibeStudioLevelGeneration", "Kept the first %1 of %2 rooms.").arg(kMaxRooms).arg(plan->rooms.size()));
		plan->rooms.resize(kMaxRooms);
	}
	QSet<QString> ids;
	QHash<QString, QString> renamed;
	for (int index = 0; index < plan->rooms.size(); ++index) {
		LevelPlanRoom& room = plan->rooms[index];
		const QString original = room.id;
		QString id = room.id.simplified();
		id.replace(QLatin1Char(' '), QLatin1Char('_'));
		if (id.isEmpty() || ids.contains(id)) {
			int suffix = index + 1;
			while (ids.contains(QStringLiteral("r%1").arg(suffix))) {
				++suffix;
			}
			id = QStringLiteral("r%1").arg(suffix);
			note(QCoreApplication::translate("VibeStudioLevelGeneration", "Room \"%1\" was renamed %2 to be unique.").arg(original, id));
		}
		if (!original.isEmpty() && !renamed.contains(original)) {
			renamed.insert(original, id);
		}
		room.id = id;
		ids.insert(id);
		const auto fix = [&](QString& field, const QStringList& allowed, const QString& fallback, const QString& what) {
			if (!allowed.contains(field)) {
				note(QCoreApplication::translate("VibeStudioLevelGeneration", "Room %1's %2 \"%3\" is unknown; used %4.").arg(id, what, field, fallback));
				field = fallback;
			}
		};
		fix(room.role, kRoles, QStringLiteral("hall"), QStringLiteral("role"));
		fix(room.size, kSizes, QStringLiteral("medium"), QStringLiteral("size"));
		fix(room.height, kHeights, QStringLiteral("normal"), QStringLiteral("height"));
		fix(room.shape, kShapes, QStringLiteral("box"), QStringLiteral("shape"));
		fix(room.liquid, kLiquids, QStringLiteral("none"), QStringLiteral("liquid"));
		fix(room.lighting, kLighting, QStringLiteral("normal"), QStringLiteral("lighting"));
		if (room.level < 0 || room.level > 3) {
			note(QCoreApplication::translate("VibeStudioLevelGeneration", "Room %1's level %2 was clamped to 0-3.").arg(id).arg(room.level));
			room.level = std::clamp(room.level, 0, 3);
		}
		if (spec.verticality == QStringLiteral("flat") && room.level != 0) {
			room.level = 0;
		}
		if (room.shape == QStringLiteral("pit") && room.liquid == QStringLiteral("none")) {
			room.liquid = spec.liquid;
		}
		if (room.shape != QStringLiteral("pit") && room.liquid != QStringLiteral("none")) {
			room.liquid = QStringLiteral("none");
		}
		if (room.name.isEmpty()) {
			room.name = room.role.left(1).toUpper() + room.role.mid(1);
		}
	}
	while (plan->rooms.size() < 2) {
		LevelPlanRoom room;
		int suffix = int(plan->rooms.size()) + 1;
		while (ids.contains(QStringLiteral("r%1").arg(suffix))) {
			++suffix;
		}
		room.id = QStringLiteral("r%1").arg(suffix);
		room.role = QStringLiteral("hall");
		room.name = QStringLiteral("Hall");
		ids.insert(room.id);
		plan->rooms.push_back(room);
		note(QCoreApplication::translate("VibeStudioLevelGeneration", "Added room %1: a level needs at least two.").arg(room.id));
	}
	const auto roomIndex = [plan](const QString& id) {
		for (int index = 0; index < plan->rooms.size(); ++index) {
			if (plan->rooms[index].id == id) {
				return index;
			}
		}
		return -1;
	};
	const auto resolved = [&renamed, &ids](const QString& id) { return ids.contains(id) ? id : renamed.value(id, id); };

	// Connections between real rooms, once each.
	QVector<LevelPlanConnection> connections;
	QSet<QString> seen;
	for (LevelPlanConnection connection : plan->connections) {
		connection.from = resolved(connection.from);
		connection.to = resolved(connection.to);
		if (!ids.contains(connection.from) || !ids.contains(connection.to) || connection.from == connection.to) {
			note(QCoreApplication::translate("VibeStudioLevelGeneration", "Dropped the link %1 to %2: it does not join two rooms.").arg(connection.from, connection.to));
			continue;
		}
		const QString key = connection.from < connection.to ? connection.from + QLatin1Char('|') + connection.to : connection.to + QLatin1Char('|') + connection.from;
		if (seen.contains(key)) {
			continue;
		}
		seen.insert(key);
		if (!kConnectionKinds.contains(connection.kind)) {
			connection.kind = QStringLiteral("corridor");
		}
		connections.push_back(connection);
	}
	plan->connections = connections;

	// A start room, and every room reachable from it.
	int start = -1;
	for (int index = 0; index < plan->rooms.size() && start < 0; ++index) {
		if (plan->rooms[index].role == QStringLiteral("start")) {
			start = index;
		}
	}
	if (start < 0) {
		start = 0;
		if (singlePlayer) {
			note(QCoreApplication::translate("VibeStudioLevelGeneration", "Room %1 became the start: no room had that role.").arg(plan->rooms[0].id));
			plan->rooms[0].role = QStringLiteral("start");
		}
	}
	const auto components = [&]() {
		QVector<int> component(plan->rooms.size(), -1);
		int next = 0;
		for (int seed = 0; seed < plan->rooms.size(); ++seed) {
			if (component[seed] >= 0) {
				continue;
			}
			QVector<int> stack {seed};
			component[seed] = next;
			while (!stack.isEmpty()) {
				const int current = stack.takeLast();
				for (const LevelPlanConnection& connection : plan->connections) {
					const int a = roomIndex(connection.from);
					const int b = roomIndex(connection.to);
					const int other = a == current ? b : b == current ? a : -1;
					if (other >= 0 && component[other] < 0) {
						component[other] = next;
						stack.push_back(other);
					}
				}
			}
			++next;
		}
		return component;
	};
	QVector<int> component = components();
	for (int index = 0; index < plan->rooms.size(); ++index) {
		if (component[index] != component[start]) {
			// Join it to the nearest room in plan order that the start reaches.
			int best = start;
			for (int other = 0; other < plan->rooms.size(); ++other) {
				if (component[other] == component[start] && std::abs(other - index) < std::abs(best - index)) {
					best = other;
				}
			}
			plan->connections.push_back({plan->rooms[best].id, plan->rooms[index].id, QStringLiteral("corridor")});
			note(QCoreApplication::translate("VibeStudioLevelGeneration", "Linked %1 to %2 so the start reaches it.").arg(plan->rooms[best].id, plan->rooms[index].id));
			component = components();
		}
	}

	// Placements in real rooms, of things the game has.
	QVector<LevelPlanPlacement> placements;
	for (LevelPlanPlacement placement : plan->placements) {
		placement.room = resolved(placement.room);
		if (!ids.contains(placement.room)) {
			note(QCoreApplication::translate("VibeStudioLevelGeneration", "Dropped a %1 placed in unknown room \"%2\".").arg(placement.kind, placement.room));
			continue;
		}
		if (!kPlacementKinds.contains(placement.kind)) {
			note(QCoreApplication::translate("VibeStudioLevelGeneration", "Dropped a placement of unknown kind \"%1\".").arg(placement.kind));
			continue;
		}
		if (placement.kind == QStringLiteral("monster") && (spec.game == QStringLiteral("quake3") || !singlePlayer)) {
			note(QCoreApplication::translate("VibeStudioLevelGeneration", "Dropped monsters from %1: this is not a single-player level.").arg(placement.room));
			continue;
		}
		if (placement.kind == QStringLiteral("exit") && (!singlePlayer || spec.game == QStringLiteral("quake3"))) {
			continue;
		}
		placement.count = std::clamp(placement.count, 1, 8);
		const QStringList items = levelGenerationItemIds(spec.game, placement.kind);
		if (!items.isEmpty()) {
			const QString item = normalizedItemId(placement.item);
			if (!item.isEmpty() && !items.contains(item)) {
				note(QCoreApplication::translate("VibeStudioLevelGeneration", "%1 has no %2 \"%3\"; the generator chooses one.").arg(spec.game, placement.kind, placement.item));
				placement.item.clear();
			} else {
				placement.item = item;
			}
		} else {
			placement.item.clear();
		}
		placements.push_back(placement);
	}
	plan->placements = placements;

	const auto count = [plan](const QString& kind) {
		int total = 0;
		for (const LevelPlanPlacement& placement : plan->placements) {
			total += placement.kind == kind ? placement.count : 0;
		}
		return total;
	};
	if (singlePlayer || spec.game == QStringLiteral("quake")) {
		if (count(QStringLiteral("player-start")) == 0) {
			plan->placements.push_back({plan->rooms[start].id, QStringLiteral("player-start"), QString(), 1});
			note(QCoreApplication::translate("VibeStudioLevelGeneration", "Added a player start in %1.").arg(plan->rooms[start].id));
		}
	}
	if (singlePlayer && spec.game != QStringLiteral("quake3") && count(QStringLiteral("exit")) == 0) {
		// The room furthest from the start, by links.
		QVector<int> distance(plan->rooms.size(), -1);
		QVector<int> queue {start};
		distance[start] = 0;
		for (int head = 0; head < queue.size(); ++head) {
			for (const LevelPlanConnection& connection : plan->connections) {
				const int a = roomIndex(connection.from);
				const int b = roomIndex(connection.to);
				const int other = a == queue[head] ? b : b == queue[head] ? a : -1;
				if (other >= 0 && distance[other] < 0) {
					distance[other] = distance[queue[head]] + 1;
					queue.push_back(other);
				}
			}
		}
		const int furthest = int(std::max_element(distance.begin(), distance.end()) - distance.begin());
		plan->placements.push_back({plan->rooms[furthest].id, QStringLiteral("exit"), QString(), 1});
		note(QCoreApplication::translate("VibeStudioLevelGeneration", "Added the exit in %1, the room furthest from the start.").arg(plan->rooms[furthest].id));
	}
	if (!singlePlayer) {
		const int wanted = std::max(spec.players, spec.mode == QStringLiteral("duel") ? 2 : 4);
		const int had = count(QStringLiteral("deathmatch-start"));
		for (int index = 0, have = had; have < wanted; ++index, ++have) {
			plan->placements.push_back({plan->rooms[index % plan->rooms.size()].id, QStringLiteral("deathmatch-start"), QString(), 1});
		}
		if (had < wanted) {
			note(QCoreApplication::translate("VibeStudioLevelGeneration", "Added %1 deathmatch start(s) for %2 players.").arg(wanted - had).arg(wanted));
		}
	}
	plan->repairs += repairs;
	return repairs;
}

QString levelPlanSystemPrompt(const LevelGenerationSpec& input)
{
	const LevelGenerationSpec spec = normalizedLevelGenerationSpec(input);
	QStringList items;
	for (const QString& kind : kPlacementKinds) {
		const QStringList ids = levelGenerationItemIds(spec.game, kind);
		if (!ids.isEmpty()) {
			items << QStringLiteral("- %1: %2").arg(kind, ids.join(QStringLiteral(", ")));
		}
	}
	// Kept in English: it instructs the model, and the user reviews it as sent.
	return QStringLiteral(
		"You are a veteran level designer for classic id Software games, planning a level for VibeStudio's level generator. "
		"You write only the plan: rooms, how they connect, and what is placed in each. The generator lays the plan out on a grid, "
		"builds sealed geometry with stairs between floor levels, lights it, and places the entities, so never give coordinates.\n\n"
		"Design for flow and pacing: a readable main route with loops back so the player is not forced down dead ends; "
		"vary room sizes, heights and floor levels; give arenas features (pillars, pits, platforms); reward exploration "
		"(armor and powerups on overlooks or side rooms). For single player, place a start, an exit far from it, and a "
		"weapon progression that matches the monsters; for deathmatch and duel, spread the starts so no two face each other at "
		"close range, put every weapon in a different room and keep the strongest items contested.\n\n"
		"Rules: 2 to 16 rooms with unique short ids (r1, r2...). Connections name two room ids; use stairs where levels differ. "
		"A pit room must name its liquid; other rooms use none. Placement counts are 1 to 8. Use only these item ids for %1:\n%2\n"
		"Starts and the exit take an empty item. Answer with the JSON plan only.")
		.arg(spec.game, items.join(QLatin1Char('\n')));
}

QString levelPlanUserPrompt(const LevelGenerationSpec& input)
{
	const LevelGenerationSpec spec = normalizedLevelGenerationSpec(input);
	return QStringLiteral(
		"Brief: %1\n\nGame: %2. Mode: %3. Theme: %4. About %5 rooms. Verticality: %6. Players: %7. Monsters: %8. Preferred liquid: %9. Title: %10.")
		.arg(spec.prompt.trimmed().isEmpty() ? QStringLiteral("(none; design something classic)") : spec.prompt.trimmed(), spec.game, spec.mode, spec.theme)
		.arg(spec.rooms)
		.arg(spec.verticality)
		.arg(spec.players)
		.arg(spec.monsters, spec.liquid, spec.title);
}

AiChatRequest levelPlanAiRequest(const LevelGenerationSpec& input, const QString& connectorId, const QString& model, const QString& endpoint,
	const QString& previousAnswer, const QStringList& revision)
{
	const LevelGenerationSpec spec = normalizedLevelGenerationSpec(input);
	AiChatRequest request;
	request.connectorId = connectorId;
	request.model = model;
	request.endpoint = endpoint;
	request.system = levelPlanSystemPrompt(spec);
	request.messages.push_back({QStringLiteral("user"), levelPlanUserPrompt(spec)});
	if (!previousAnswer.isEmpty() && !revision.isEmpty()) {
		request.messages.push_back({QStringLiteral("assistant"), previousAnswer});
		QStringList lines;
		for (const QString& problem : revision.mid(0, 40)) {
			lines << QStringLiteral("- %1").arg(problem);
		}
		request.messages.push_back({QStringLiteral("user"),
			QStringLiteral("That plan has problems:\n%1\nAnswer with the whole corrected JSON plan.").arg(lines.join(QLatin1Char('\n')))});
	}
	request.maxOutputTokens = 8000;
	request.responseSchema = levelSemanticPlanSchema(spec.game);
	request.responseSchemaName = QStringLiteral("level_plan");
	return request;
}

QStringList levelSemanticPlanLines(const LevelSemanticPlan& plan)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioLevelGeneration", "%1, planned by %2").arg(plan.title, plan.planner);
	if (!plan.summary.isEmpty()) {
		lines << plan.summary;
	}
	for (const LevelPlanRoom& room : plan.rooms) {
		QStringList things;
		for (const LevelPlanPlacement& placement : plan.placements) {
			if (placement.room != room.id) {
				continue;
			}
			const QString what = placement.item.isEmpty() ? placement.kind : QStringLiteral("%1 (%2)").arg(placement.kind, placement.item);
			things << (placement.count > 1 ? QStringLiteral("%1 x%2").arg(what).arg(placement.count) : what);
		}
		const QString feature = room.liquid == QStringLiteral("none") ? room.shape : QStringLiteral("%1 of %2").arg(room.shape, room.liquid);
		lines << QCoreApplication::translate("VibeStudioLevelGeneration", "%1 \"%2\": %3 %4 room, level %5, %6, %7 light. %8")
					 .arg(room.id, room.name, room.size, room.role)
					 .arg(room.level)
					 .arg(feature, room.lighting, things.isEmpty() ? QCoreApplication::translate("VibeStudioLevelGeneration", "Empty.") : things.join(QStringLiteral(", ")));
	}
	for (const LevelPlanConnection& connection : plan.connections) {
		lines << QCoreApplication::translate("VibeStudioLevelGeneration", "%1 to %2 by %3").arg(connection.from, connection.to, connection.kind);
	}
	return lines;
}

bool levelSemanticPlanFromAnswer(const QString& answer, const LevelGenerationSpec& spec, LevelSemanticPlan* plan, QStringList* problems)
{
	QJsonObject object;
	QString error;
	if (!extractAiJsonObject(answer, &object, &error)) {
		if (problems) {
			*problems = QStringList {error};
		}
		return false;
	}
	QStringList found;
	LevelSemanticPlan parsed;
	if (!levelSemanticPlanFromJson(object, &parsed, &found)) {
		if (problems) {
			*problems = found.isEmpty() ? QStringList {QCoreApplication::translate("VibeStudioLevelGeneration", "The plan has no rooms.")} : found;
		}
		return false;
	}
	// Items are checked against the game, the rest against the schema.
	const LevelGenerationSpec normalized = normalizedLevelGenerationSpec(spec);
	for (const LevelPlanPlacement& placement : parsed.placements) {
		const QStringList items = levelGenerationItemIds(normalized.game, placement.kind);
		if (!placement.item.isEmpty() && !items.isEmpty() && !items.contains(normalizedItemId(placement.item))) {
			found << QCoreApplication::translate("VibeStudioLevelGeneration", "\"%1\" is not a %2 in %3.").arg(placement.item, placement.kind, normalized.game);
		}
	}
	if (problems) {
		*problems = found;
	}
	if (plan) {
		*plan = parsed;
	}
	return true;
}

} // namespace vibestudio
