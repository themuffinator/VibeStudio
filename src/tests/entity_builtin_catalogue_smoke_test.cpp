// Checks the built-in Quake, Quake II and Quake III entity catalogues: each
// holds the classes a map needs, with sizes, colours, keys and spawnflags,
// and the game is chosen from the map itself.

#include "core/entity_builtin_catalogue.h"

#include "core/level_document.h"

#include <QCoreApplication>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <initializer_list>
#include <iostream>
#include <utility>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << "\n";
	}
	return condition;
}

// Names the class as well, so a slip in the tables points at its row.
bool expectIn(bool condition, const EntityClassDefinition& definition, const char* message)
{
	if (!condition) {
		std::cerr << "FAIL: " << definition.className.toStdString() << ": " << message << "\n";
	}
	return condition;
}

QString latin1(const char* text)
{
	return QString::fromLatin1(text);
}

bool hasNumbers(const QString& value, qsizetype count)
{
	const QStringList parts = value.split(QLatin1Char(' '), Qt::SkipEmptyParts);
	if (parts.size() != count) {
		return false;
	}
	return std::all_of(parts.cbegin(), parts.cend(), [](const QString& part) {
		bool ok = false;
		static_cast<void>(part.toDouble(&ok));
		return ok;
	});
}

// A default has to be a value the key's own type accepts.
bool defaultFitsType(const EntityKeyDefinition& entry)
{
	const QString& value = entry.defaultValue;
	if (value.isEmpty()) {
		return true;
	}
	bool ok = false;
	switch (entry.type) {
	case EntityKeyType::Integer:
		static_cast<void>(value.toLongLong(&ok));
		return ok;
	case EntityKeyType::Real:
		static_cast<void>(value.toDouble(&ok));
		return ok;
	case EntityKeyType::Boolean:
		return value == QStringLiteral("0") || value == QStringLiteral("1");
	case EntityKeyType::Choices:
		return std::any_of(entry.choices.cbegin(), entry.choices.cend(), [&value](const EntityKeyChoice& option) {
			return option.value == value;
		});
	case EntityKeyType::Vector:
	case EntityKeyType::Color:
		return hasNumbers(value, 3);
	case EntityKeyType::Angle:
		return hasNumbers(value, 1) || hasNumbers(value, 3);
	case EntityKeyType::String:
	case EntityKeyType::Flags:
	case EntityKeyType::TargetSource:
	case EntityKeyType::TargetDestination:
	case EntityKeyType::Sound:
	case EntityKeyType::Model:
	case EntityKeyType::Texture:
		break;
	}
	return true;
}

bool keyIs(const EntityClassDefinition& definition, const char* name, EntityKeyType type, EntityKeyDefinition* out = nullptr)
{
	EntityKeyDefinition entry;
	if (!definition.keyForName(latin1(name), &entry) || entry.type != type) {
		return false;
	}
	if (out) {
		*out = entry;
	}
	return true;
}

QString spawnflagName(const EntityClassDefinition& definition, int bit)
{
	for (const EntitySpawnflagDefinition& entry : definition.spawnflags) {
		if (entry.bit == bit) {
			return entry.name;
		}
	}
	return QString();
}

bool boxIs(const EntityClassDefinition& definition, std::initializer_list<double> mins, std::initializer_list<double> maxs)
{
	if (!definition.hasSize) {
		return false;
	}
	int axis = 0;
	for (const double value : mins) {
		if (definition.mins[axis++] != value) {
			return false;
		}
	}
	axis = 0;
	for (const double value : maxs) {
		if (definition.maxs[axis++] != value) {
			return false;
		}
	}
	return true;
}

bool colorIs(const EntityClassDefinition& definition, int red, int green, int blue)
{
	return definition.hasColor && definition.color[0] == red && definition.color[1] == green && definition.color[2] == blue;
}

// The checks every class of every catalogue has to pass.
bool checkClass(const EntityClassDefinition& definition, const QString& sourcePath)
{
	bool ok = true;
	ok &= expectIn(!definition.className.isEmpty(), definition, "has a class name");
	ok &= expectIn(definition.kind == EntityClassKind::Point || definition.kind == EntityClassKind::Brush, definition, "is a point or brush class");
	ok &= expectIn(!definition.description.trimmed().isEmpty(), definition, "is described");
	ok &= expectIn(definition.sourcePath == sourcePath && definition.sourceLine == 0, definition, "names the built-in catalogue as its source");
	ok &= expectIn(definition.hasColor, definition, "has an editor colour");
	for (const int component : definition.color) {
		ok &= expectIn(component >= 0 && component <= 255, definition, "colour components are 0-255");
	}
	if (definition.kind == EntityClassKind::Point) {
		ok &= expectIn(definition.hasSize, definition, "point classes carry their size");
		for (int axis = 0; axis < 3; ++axis) {
			ok &= expectIn(definition.mins[axis] < definition.maxs[axis], definition, "the box has volume");
		}
	} else {
		ok &= expectIn(!definition.hasSize, definition, "brush classes take their size from their brushes");
	}
	if (!definition.modelHint.isEmpty()) {
		const QString hint = definition.modelHint;
		ok &= expectIn(hint.endsWith(QStringLiteral(".mdl")) || hint.endsWith(QStringLiteral(".bsp")) || hint.endsWith(QStringLiteral(".spr"))
				|| hint.endsWith(QStringLiteral(".md2")) || hint.endsWith(QStringLiteral(".md3")),
			definition, "the model hint is a model file");
	}

	QSet<QString> keys;
	for (const EntityKeyDefinition& entry : definition.keys) {
		const QString lower = entry.key.toLower();
		ok &= expectIn(!entry.key.isEmpty() && !keys.contains(lower), definition, "keys are named and unique");
		keys.insert(lower);
		ok &= expectIn(!entry.description.trimmed().isEmpty(), definition, "every key is described");
		ok &= expectIn(entry.typeId == entityKeyTypeId(entry.type) && entry.displayName == entry.key, definition, "keys carry their type id and display name");
		ok &= expectIn(defaultFitsType(entry), definition, "key defaults suit their types");
		ok &= expectIn(!entry.required || entry.defaultValue.isEmpty(), definition, "required keys have no default");
		if (entry.type == EntityKeyType::Choices) {
			QSet<QString> values;
			ok &= expectIn(entry.choices.size() >= 2, definition, "choice keys offer choices");
			for (const EntityKeyChoice& option : entry.choices) {
				ok &= expectIn(!option.value.isEmpty() && !option.label.trimmed().isEmpty() && !values.contains(option.value), definition,
					"choices are labelled and unique");
				values.insert(option.value);
			}
		} else {
			ok &= expectIn(entry.choices.isEmpty(), definition, "only choice keys have choices");
		}
	}

	QSet<int> bits;
	int previousBit = -1;
	for (const EntitySpawnflagDefinition& entry : definition.spawnflags) {
		ok &= expectIn(entry.bit >= 0 && entry.bit <= 31, definition, "spawnflag bits are 0-31");
		ok &= expectIn(!bits.contains(entry.bit), definition, "spawnflag bits are unique");
		ok &= expectIn(entry.bit > previousBit, definition, "spawnflags are sorted by bit");
		// Bits 8-11 are the skill and deathmatch filters every entity honours;
		// the validator knows them, so no class declares them.
		ok &= expectIn(entry.bit < 8 || entry.bit > 11, definition, "classes leave the skill bits alone");
		ok &= expectIn(!entry.name.isEmpty() && !entry.description.trimmed().isEmpty(), definition, "spawnflags are named and described");
		bits.insert(entry.bit);
		previousBit = entry.bit;
	}
	return ok;
}

bool checkCatalogue(BuiltinEntityGame game, const EntityDefinitionCatalogue& catalogue)
{
	bool ok = true;
	const QString sourcePath = QStringLiteral("builtin:%1").arg(builtinEntityGameId(game));
	ok &= expect(catalogue.classes.size() >= 80, "each game has a full starter catalogue");
	ok &= expect(catalogue.sourcePaths == QStringList{sourcePath}, "the catalogue names its built-in source");
	ok &= expect(catalogue.error.isEmpty() && catalogue.warnings.isEmpty(), "building a catalogue reports nothing");
	for (const char* name : {"worldspawn", "info_player_start", "info_player_deathmatch", "light", "func_door", "func_button", "func_plat", "func_train",
			 "trigger_multiple", "trigger_hurt", "trigger_push", "path_corner", "info_null", "info_notnull"}) {
		ok &= expect(catalogue.classForName(latin1(name)), "each game defines the classes every map uses");
	}

	QSet<QString> names;
	int points = 0;
	int brushes = 0;
	for (const EntityClassDefinition& definition : catalogue.classes) {
		ok &= expectIn(!names.contains(definition.className.toLower()), definition, "class names are unique");
		names.insert(definition.className.toLower());
		ok &= checkClass(definition, sourcePath);
		points += definition.kind == EntityClassKind::Point ? 1 : 0;
		brushes += definition.kind == EntityClassKind::Brush ? 1 : 0;
	}
	ok &= expect(points == catalogue.pointClassCount && brushes == catalogue.brushClassCount && catalogue.baseClassCount == 0,
		"point and brush counts match the classes");
	ok &= expect(points > 0 && brushes > 0, "each game has point and brush classes");
	ok &= expect(std::is_sorted(catalogue.classes.cbegin(), catalogue.classes.cend(),
					 [](const EntityClassDefinition& left, const EntityClassDefinition& right) { return left.className < right.className; }),
		"classes are sorted by name");

	EntityClassDefinition worldspawn;
	ok &= expect(catalogue.classForName(QStringLiteral("worldspawn"), &worldspawn) && worldspawn.kind == EntityClassKind::Brush
			&& keyIs(worldspawn, "message", EntityKeyType::String),
		"worldspawn is the brush class that names the level");
	EntityClassDefinition door;
	ok &= expect(catalogue.classForName(QStringLiteral("func_door"), &door) && door.kind == EntityClassKind::Brush && !door.spawnflags.isEmpty()
			&& spawnflagName(door, 0) == QStringLiteral("START_OPEN") && keyIs(door, "targetname", EntityKeyType::TargetSource)
			&& keyIs(door, "speed", EntityKeyType::Real) && keyIs(door, "angle", EntityKeyType::Angle),
		"func_door is a brush class with START_OPEN, a speed and a targetname");
	EntityClassDefinition pathCorner;
	EntityKeyDefinition pathName;
	ok &= expect(catalogue.classForName(QStringLiteral("path_corner"), &pathCorner) && keyIs(pathCorner, "targetname", EntityKeyType::TargetSource, &pathName)
			&& pathName.required && keyIs(pathCorner, "target", EntityKeyType::TargetDestination),
		"path_corner needs the targetname the previous corner targets");
	ok &= expect(builtinEntityGameFromId(builtinEntityGameId(game), nullptr), "game ids round trip");
	ok &= expect(!builtinEntityGameDisplayName(game).isEmpty(), "games have display names");
	return ok;
}

bool checkQuake(const EntityDefinitionCatalogue& catalogue)
{
	bool ok = true;
	EntityClassDefinition ogre;
	ok &= expect(catalogue.classForName(QStringLiteral("monster_ogre"), &ogre) && ogre.kind == EntityClassKind::Point
			&& boxIs(ogre, {-32, -32, -24}, {32, 32, 64}) && colorIs(ogre, 255, 0, 0) && spawnflagName(ogre, 0) == QStringLiteral("Ambush")
			&& ogre.modelHint == QStringLiteral("progs/ogre.mdl") && keyIs(ogre, "target", EntityKeyType::TargetDestination),
		"Quake's monster_ogre has its box, red colour, Ambush flag and model");
	EntityClassDefinition supernailgun;
	ok &= expect(catalogue.classForName(QStringLiteral("weapon_supernailgun"), &supernailgun) && boxIs(supernailgun, {-16, -16, 0}, {16, 16, 32})
			&& colorIs(supernailgun, 0, 128, 204),
		"Quake's weapon_supernailgun keeps its QUAKED box and colour");
	EntityClassDefinition armor;
	ok &= expect(catalogue.classForName(QStringLiteral("item_armorinv"), &armor) && armor.className == QStringLiteral("item_armorInv")
			&& armor.kind == EntityClassKind::Point,
		"Quake's item_armorInv keeps its spelling and is found case-insensitively");
	EntityClassDefinition door;
	EntityKeyDefinition sounds;
	EntityKeyDefinition speed;
	ok &= expect(catalogue.classForName(QStringLiteral("func_door"), &door) && door.spawnflags.size() == 5 && spawnflagName(door, 1).isEmpty()
			&& spawnflagName(door, 2) == QStringLiteral("DOOR_DONT_LINK") && spawnflagName(door, 3) == QStringLiteral("GOLD_KEY")
			&& spawnflagName(door, 4) == QStringLiteral("SILVER_KEY") && spawnflagName(door, 5) == QStringLiteral("TOGGLE")
			&& keyIs(door, "sounds", EntityKeyType::Choices, &sounds) && sounds.choices.size() == 5 && keyIs(door, "speed", EntityKeyType::Real, &speed)
			&& speed.defaultValue == QStringLiteral("100") && keyIs(door, "killtarget", EntityKeyType::TargetDestination),
		"Quake's func_door has START_OPEN, DOOR_DONT_LINK, the key flags and TOGGLE, five sound sets and a speed of 100");
	EntityClassDefinition worldspawn;
	EntityKeyDefinition worldtype;
	ok &= expect(catalogue.classForName(QStringLiteral("worldspawn"), &worldspawn) && keyIs(worldspawn, "worldtype", EntityKeyType::Choices, &worldtype)
			&& worldtype.choices.size() == 3 && worldtype.defaultValue == QStringLiteral("0") && keyIs(worldspawn, "wad", EntityKeyType::String),
		"Quake's worldspawn has the three world types and the wad key");
	EntityClassDefinition zombie;
	ok &= expect(catalogue.classForName(QStringLiteral("monster_zombie"), &zombie) && spawnflagName(zombie, 0) == QStringLiteral("Crucified")
			&& spawnflagName(zombie, 1) == QStringLiteral("ambush"),
		"Quake's zombie keeps Crucified before ambush");
	EntityClassDefinition changelevel;
	EntityKeyDefinition map;
	ok &= expect(catalogue.classForName(QStringLiteral("trigger_changelevel"), &changelevel) && changelevel.kind == EntityClassKind::Brush
			&& keyIs(changelevel, "map", EntityKeyType::String, &map) && map.required,
		"Quake's trigger_changelevel needs a map");
	EntityClassDefinition spark;
	EntityKeyDefinition style;
	EntityKeyDefinition brightness;
	ok &= expect(catalogue.classForName(QStringLiteral("light_fluorospark"), &spark) && keyIs(spark, "style", EntityKeyType::Integer, &style)
			&& style.defaultValue == QStringLiteral("10") && keyIs(spark, "light", EntityKeyType::Integer, &brightness) && brightness.defaultValue == QStringLiteral("300"),
		"a class can give a shared key its own default");
	EntityClassDefinition secret;
	EntityKeyDefinition message;
	ok &= expect(catalogue.classForName(QStringLiteral("trigger_secret"), &secret) && keyIs(secret, "message", EntityKeyType::String, &message)
			&& message.defaultValue == QStringLiteral("You found a secret area!"),
		"Quake's trigger_secret has its default message");
	EntityClassDefinition health;
	ok &= expect(catalogue.classForName(QStringLiteral("item_health"), &health) && health.keys.size() == 4 && spawnflagName(health, 0) == QStringLiteral("rotten")
			&& spawnflagName(health, 1) == QStringLiteral("megahealth"),
		"shared rows add their keys once");
	ok &= expect(catalogue.classForName(QStringLiteral("monster_ogre_marksman")) && catalogue.classForName(QStringLiteral("trap_spikeshooter"))
			&& catalogue.classForName(QStringLiteral("ambient_swamp2")) && catalogue.classForName(QStringLiteral("event_lightning")),
		"Quake keeps the base game's less common classes");
	ok &= expect(!catalogue.classForName(QStringLiteral("func_areaportal")) && !catalogue.classForName(QStringLiteral("monster_soldier"))
			&& !catalogue.classForName(QStringLiteral("test_fodder")) && !catalogue.classForName(QStringLiteral("item_deathball")),
		"Quake leaves out other games' and test classes");
	return ok;
}

bool checkQuake2(const EntityDefinitionCatalogue& catalogue)
{
	bool ok = true;
	EntityClassDefinition teleporter;
	EntityKeyDefinition target;
	ok &= expect(catalogue.classForName(QStringLiteral("misc_teleporter"), &teleporter) && teleporter.kind == EntityClassKind::Point
			&& boxIs(teleporter, {-32, -32, -24}, {32, 32, -16}) && keyIs(teleporter, "target", EntityKeyType::TargetDestination, &target) && target.required,
		"Quake II's misc_teleporter keeps its pad box and needs a target");
	EntityClassDefinition speaker;
	EntityKeyDefinition noise;
	ok &= expect(catalogue.classForName(QStringLiteral("target_speaker"), &speaker) && keyIs(speaker, "noise", EntityKeyType::Sound, &noise) && noise.required
			&& keyIs(speaker, "attenuation", EntityKeyType::Real) && spawnflagName(speaker, 2) == QStringLiteral("reliable"),
		"Quake II's target_speaker has a Sound-typed noise key and its loop flags");
	EntityClassDefinition areaportal;
	ok &= expect(catalogue.classForName(QStringLiteral("func_areaportal"), &areaportal) && areaportal.kind == EntityClassKind::Brush,
		"Quake II's func_areaportal is a brush class");
	EntityClassDefinition soldier;
	ok &= expect(catalogue.classForName(QStringLiteral("monster_soldier"), &soldier) && spawnflagName(soldier, 0) == QStringLiteral("Ambush")
			&& spawnflagName(soldier, 1) == QStringLiteral("Trigger_Spawn") && spawnflagName(soldier, 2) == QStringLiteral("Sight")
			&& keyIs(soldier, "item", EntityKeyType::String) && keyIs(soldier, "combattarget", EntityKeyType::TargetDestination)
			&& keyIs(soldier, "deathtarget", EntityKeyType::TargetDestination),
		"Quake II monsters share their flags and keys");
	EntityClassDefinition rotating;
	EntityKeyDefinition distance;
	ok &= expect(catalogue.classForName(QStringLiteral("func_door_rotating"), &rotating) && spawnflagName(rotating, 6) == QStringLiteral("X_AXIS")
			&& spawnflagName(rotating, 7) == QStringLiteral("Y_AXIS") && keyIs(rotating, "distance", EntityKeyType::Integer, &distance)
			&& distance.defaultValue == QStringLiteral("90"),
		"Quake II's func_door_rotating turns 90 degrees about a chosen axis");
	EntityClassDefinition train;
	EntityKeyDefinition damage;
	ok &= expect(catalogue.classForName(QStringLiteral("func_train"), &train) && keyIs(train, "dmg", EntityKeyType::Integer, &damage)
			&& damage.defaultValue == QStringLiteral("100") && keyIs(train, "noise", EntityKeyType::Sound),
		"Quake II's func_train follows the code's default damage");
	EntityClassDefinition combat;
	ok &= expect(catalogue.classForName(QStringLiteral("item_armor_combat"), &combat) && combat.modelHint == QStringLiteral("models/items/armor/combat/tris.md2")
			&& keyIs(combat, "team", EntityKeyType::String),
		"Quake II items carry their models");
	EntityClassDefinition always;
	EntityKeyDefinition delay;
	ok &= expect(catalogue.classForName(QStringLiteral("trigger_always"), &always) && keyIs(always, "delay", EntityKeyType::Real, &delay)
			&& delay.defaultValue == QStringLiteral("0.2"),
		"Quake II's trigger_always waits at least 0.2 seconds");
	EntityClassDefinition timer;
	ok &= expect(catalogue.classForName(QStringLiteral("func_timer"), &timer) && timer.kind == EntityClassKind::Point, "Quake II's func_timer is a point class");
	ok &= expect(!catalogue.classForName(QStringLiteral("item_armorInv")) && !catalogue.classForName(QStringLiteral("monster_ogre"))
			&& !catalogue.classForName(QStringLiteral("viewthing")),
		"Quake II leaves out Quake's classes and debugging ones");
	return ok;
}

bool checkQuake3(const EntityDefinitionCatalogue& catalogue)
{
	bool ok = true;
	EntityClassDefinition model;
	EntityKeyDefinition path;
	ok &= expect(catalogue.classForName(QStringLiteral("misc_model"), &model) && model.kind == EntityClassKind::Point
			&& keyIs(model, "model", EntityKeyType::Model, &path) && path.required,
		"Quake III's misc_model needs a Model-typed model key");
	EntityClassDefinition speaker;
	EntityKeyDefinition noise;
	ok &= expect(catalogue.classForName(QStringLiteral("target_speaker"), &speaker) && keyIs(speaker, "noise", EntityKeyType::Sound, &noise) && noise.required
			&& spawnflagName(speaker, 3) == QStringLiteral("activator"),
		"Quake III's target_speaker needs its noise");
	EntityClassDefinition redFlag;
	ok &= expect(catalogue.classForName(QStringLiteral("team_CTF_redflag"), &redFlag) && redFlag.kind == EntityClassKind::Point && colorIs(redFlag, 255, 0, 0)
			&& redFlag.modelHint == QStringLiteral("models/flags/r_flag.md3"),
		"Quake III's team_CTF_redflag is red and shows the flag model");
	EntityClassDefinition railgun;
	ok &= expect(catalogue.classForName(QStringLiteral("weapon_railgun"), &railgun) && railgun.modelHint.startsWith(QStringLiteral("models/weapons2/"))
			&& spawnflagName(railgun, 0) == QStringLiteral("suspended") && keyIs(railgun, "notfree", EntityKeyType::Boolean),
		"Quake III weapons show their models and take the item keys");
	EntityClassDefinition quad;
	ok &= expect(catalogue.classForName(QStringLiteral("item_quad"), &quad) && keyIs(quad, "noglobalsound", EntityKeyType::Boolean)
			&& keyIs(quad, "wait", EntityKeyType::Real),
		"Quake III powerups add noglobalsound to the item keys");
	EntityClassDefinition door;
	EntityKeyDefinition speed;
	ok &= expect(catalogue.classForName(QStringLiteral("func_door"), &door) && keyIs(door, "speed", EntityKeyType::Real, &speed)
			&& speed.defaultValue == QStringLiteral("400") && keyIs(door, "model2", EntityKeyType::Model) && keyIs(door, "color", EntityKeyType::Color)
			&& spawnflagName(door, 2) == QStringLiteral("CRUSHER"),
		"Quake III doors take the mover keys and the code's speed");
	EntityClassDefinition worldspawn;
	EntityKeyDefinition grid;
	ok &= expect(catalogue.classForName(QStringLiteral("worldspawn"), &worldspawn) && keyIs(worldspawn, "gridsize", EntityKeyType::Vector, &grid)
			&& grid.defaultValue == QStringLiteral("64 64 128") && keyIs(worldspawn, "enableDust", EntityKeyType::Boolean),
		"Quake III's worldspawn has the light grid and dust keys");
	const bool monsters = std::any_of(catalogue.classes.cbegin(), catalogue.classes.cend(), [](const EntityClassDefinition& definition) {
		return definition.className.startsWith(QStringLiteral("monster_"));
	});
	ok &= expect(!monsters, "Quake III has no monsters");
	ok &= expect(!catalogue.classForName(QStringLiteral("weapon_nailgun")) && !catalogue.classForName(QStringLiteral("holdable_kamikaze"))
			&& !catalogue.classForName(QStringLiteral("team_CTF_neutralflag")),
		"Quake III leaves out the Team Arena classes");
	return ok;
}

// ---------------------------------------------------------------------------
// Picking the game for a map
// ---------------------------------------------------------------------------

LevelMapEntity entity(int id, const char* className, std::initializer_list<std::pair<const char*, const char*>> properties = {})
{
	LevelMapEntity result;
	result.id = id;
	result.className = latin1(className);
	LevelMapProperty classProperty;
	classProperty.key = QStringLiteral("classname");
	classProperty.value = result.className;
	result.properties.append(classProperty);
	for (const auto& [key, value] : properties) {
		LevelMapProperty property;
		property.key = latin1(key);
		property.value = latin1(value);
		result.properties.append(property);
	}
	return result;
}

LevelMapBrush brushWithTextures(int entityId, std::initializer_list<const char*> textures)
{
	LevelMapBrush brush;
	brush.entityId = entityId;
	for (const char* name : textures) {
		LevelMapBrushFace face;
		face.textureName = latin1(name);
		brush.faces.append(face);
	}
	brush.faceCount = static_cast<int>(brush.faces.size());
	return brush;
}

LevelMapDocument quakeMap(std::initializer_list<LevelMapEntity> entities, std::initializer_list<LevelMapBrush> brushes = {})
{
	LevelMapDocument document;
	document.format = LevelMapFormat::QuakeMap;
	for (const LevelMapEntity& item : entities) {
		document.entities.append(item);
	}
	for (const LevelMapBrush& item : brushes) {
		document.brushes.append(item);
	}
	return document;
}

bool checkGameForMap()
{
	bool ok = true;
	LevelMapDocument quake3;
	quake3.format = LevelMapFormat::Quake3Map;
	quake3.entities.append(entity(0, "worldspawn", {{"wad", "gfx/base.wad"}}));
	ok &= expect(builtinEntityGameForMap(quake3) == BuiltinEntityGame::Quake3, "Quake III maps use the Quake III catalogue");

	const LevelMapEntity flatWorld = entity(0, "worldspawn", {{"wad", "gfx/base.wad"}});
	const LevelMapBrush flatBrush = brushWithTextures(0, {"city2_3", "city2_3", "sky4", "*water0", "+0basebtn", "city2_3"});
	ok &= expect(builtinEntityGameForMap(quakeMap({flatWorld, entity(1, "info_player_start")}, {flatBrush})) == BuiltinEntityGame::Quake,
		"a wad key and flat texture names mean Quake");

	for (const char* className : {"func_areaportal", "target_speaker", "misc_teleporter", "item_armor_combat", "monster_soldier", "FUNC_AREAPORTAL"}) {
		ok &= expect(builtinEntityGameForMap(quakeMap({flatWorld, entity(1, className)}, {flatBrush})) == BuiltinEntityGame::Quake2,
			"a class only Quake II has means Quake II, whatever its case");
	}
	ok &= expect(builtinEntityGameForMap(quakeMap({flatWorld, entity(1, "func_group")}, {flatBrush})) == BuiltinEntityGame::Quake,
		"func_group is not Quake II evidence");

	const LevelMapEntity bareWorld = entity(0, "worldspawn", {{"message", "Outer Base"}});
	const LevelMapBrush folderBrush = brushWithTextures(0, {"e1u1/floor1_3", "e1u1/wall1_1", "e1u1/clip", "e1u1/floor1_3", "e1u1/sky1", "city2_3"});
	ok &= expect(builtinEntityGameForMap(quakeMap({bareWorld, entity(1, "info_player_start")}, {folderBrush})) == BuiltinEntityGame::Quake2,
		"folder texture names without a wad key mean Quake II");
	ok &= expect(builtinEntityGameForMap(quakeMap({flatWorld}, {folderBrush})) == BuiltinEntityGame::Quake, "a wad key outweighs folder texture names");
	ok &= expect(builtinEntityGameForMap(quakeMap({entity(0, "worldspawn", {{"_wad", "gfx/base.wad"}})}, {folderBrush})) == BuiltinEntityGame::Quake,
		"the _wad spelling counts as a wad key");
	ok &= expect(builtinEntityGameForMap(quakeMap({entity(0, "worldspawn", {{"wad", ""}})}, {folderBrush})) == BuiltinEntityGame::Quake2,
		"an empty wad key is no wad key");
	ok &= expect(builtinEntityGameForMap(quakeMap({bareWorld, entity(1, "weapon_nailgun")}, {folderBrush})) == BuiltinEntityGame::Quake,
		"a class only Quake has outweighs folder texture names");
	ok &= expect(builtinEntityGameForMap(quakeMap({entity(0, "worldspawn", {{"worldtype", "2"}})}, {folderBrush})) == BuiltinEntityGame::Quake,
		"Quake's worldtype key outweighs folder texture names");
	ok &= expect(builtinEntityGameForMap(quakeMap({bareWorld, entity(1, "func_detail")}, {folderBrush})) == BuiltinEntityGame::Quake2,
		"compiler classes are no evidence of the game");
	ok &= expect(builtinEntityGameForMap(quakeMap({bareWorld, entity(1, "weapon_nailgun"), entity(2, "monster_soldier")}, {flatBrush}))
			== BuiltinEntityGame::Quake2,
		"a class only Quake II has wins over one only Quake has");
	const LevelMapBrush mostlyFlat = brushWithTextures(0, {"city2_3", "sky4", "city2_3", "e1u1/floor1_3"});
	ok &= expect(builtinEntityGameForMap(quakeMap({bareWorld}, {mostlyFlat})) == BuiltinEntityGame::Quake, "a few folder names do not make a map Quake II");
	LevelMapBrush namesOnly;
	namesOnly.entityId = 0;
	namesOnly.textureNames = {QStringLiteral("e2u3/metal1_1"), QStringLiteral("e2u3/metal1_1")};
	ok &= expect(builtinEntityGameForMap(quakeMap({bareWorld}, {namesOnly})) == BuiltinEntityGame::Quake2, "texture name lists count when faces are absent");

	LevelMapDocument header = quakeMap({flatWorld}, {flatBrush});
	header.originalText = QString::fromLatin1(kQuake2MapTargetHeader) + QStringLiteral("{\n\"classname\" \"worldspawn\"\n}\n");
	ok &= expect(builtinEntityGameForMap(header) == BuiltinEntityGame::Quake2, "the Quake II target header means Quake II");
	header.originalText.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
	ok &= expect(builtinEntityGameForMap(header) == BuiltinEntityGame::Quake2, "the target header is recognised with CRLF line endings");
	header.originalText = QStringLiteral("// Game: Quake 2\n// Format: Quake2\n{\n}\n");
	ok &= expect(builtinEntityGameForMap(header) == BuiltinEntityGame::Quake2, "TrenchBroom's Quake 2 header means Quake II");
	header.originalText = QStringLiteral("// Game: Quake\n// Format: Standard\n{\n}\n");
	ok &= expect(builtinEntityGameForMap(header) == BuiltinEntityGame::Quake, "TrenchBroom's Quake header is not mistaken for Quake II");
	LevelMapDocument trenchbroom = quakeMap({bareWorld}, {folderBrush});
	trenchbroom.originalText = QStringLiteral("// Game: Quake\n// Format: Valve\n{\n}\n");
	ok &= expect(builtinEntityGameForMap(trenchbroom) == BuiltinEntityGame::Quake, "TrenchBroom's Quake header outweighs folder texture names");
	header.originalText = QStringLiteral("{\n// VibeStudio target: quake2\n}\n");
	ok &= expect(builtinEntityGameForMap(header) == BuiltinEntityGame::Quake, "only the first line carries the header");

	ok &= expect(builtinEntityGameForMap(quakeMap({})) == BuiltinEntityGame::Quake, "an empty Quake-format map is Quake");
	LevelMapDocument doom;
	doom.format = LevelMapFormat::DoomWad;
	doom.entities.append(entity(0, "func_areaportal"));
	ok &= expect(builtinEntityGameForMap(doom) == BuiltinEntityGame::Quake, "maps of other formats fall back to Quake");
	return ok;
}

// ---------------------------------------------------------------------------
// Validating a map against a built-in catalogue
// ---------------------------------------------------------------------------

bool hasIssue(const EntityValidationReport& report, const char* code, const char* key = nullptr)
{
	return std::any_of(report.issues.cbegin(), report.issues.cend(), [code, key](const EntityValidationIssue& issue) {
		return issue.code == QLatin1StringView(code) && (!key || issue.key == QLatin1StringView(key));
	});
}

bool checkValidation(const EntityDefinitionCatalogue& quake)
{
	bool ok = true;
	LevelMapDocument document = quakeMap(
		{
			entity(0, "worldspawn", {{"wad", "gfx/base.wad"}, {"worldtype", "1"}, {"message", "Test"}}),
			entity(1, "info_player_start", {{"angle", "90"}}),
			// 257 is Ambush plus the not-in-easy skill bit every entity honours.
			entity(2, "monster_ogre", {{"spawnflags", "257"}, {"angle", "180"}}),
			entity(3, "func_door", {{"sounds", "4"}, {"targetname", "door1"}, {"speed", "120"}, {"spawnflags", "36"}}),
			entity(4, "trigger_once", {{"target", "door1"}, {"sounds", "1"}}),
			entity(5, "light", {{"light", "200"}, {"style", "5"}, {"_color", "1 0.5 0.5"}}),
		},
		{brushWithTextures(0, {"city2_3"}), brushWithTextures(3, {"door02_1"}), brushWithTextures(4, {"trigger"})});
	document.mapName = QStringLiteral("test");
	const EntityValidationReport clean = validateLevelMapEntities(document, quake);
	ok &= expect(clean.unknownClassCount == 0 && clean.knownClassCount == 6, "a Quake map's classes are all known");
	if (!expect(clean.errorCount == 0 && clean.warningCount == 0, "a correct Quake map validates without warnings")) {
		ok = false;
		for (const EntityValidationIssue& issue : clean.issues) {
			std::cerr << "  " << issue.code.toStdString() << ": " << issue.message.toStdString() << "\n";
		}
	}

	document.entities[3] = entity(3, "func_door", {{"sounds", "9"}, {"targetname", "door1"}, {"spawnflags", "2"}});
	document.entities.append(entity(6, "trigger_changelevel"));
	document.brushes.append(brushWithTextures(6, {"trigger"}));
	const EntityValidationReport broken = validateLevelMapEntities(document, quake);
	ok &= expect(hasIssue(broken, "entity-key-value-invalid", "sounds"), "a sound set the door does not have is reported");
	ok &= expect(hasIssue(broken, "entity-unknown-spawnflag-bit", "spawnflags"), "the door's unused spawnflag bit is reported");
	ok &= expect(hasIssue(broken, "entity-required-key-missing", "map"), "a changelevel trigger without a map is reported");
	return ok;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	for (const BuiltinEntityGame game : {BuiltinEntityGame::Quake, BuiltinEntityGame::Quake2, BuiltinEntityGame::Quake3}) {
		const EntityDefinitionCatalogue catalogue = builtinEntityDefinitions(game);
		std::cout << builtinEntityGameId(game).toStdString() << ": " << catalogue.classes.size() << " classes (" << catalogue.pointClassCount << " point, "
				  << catalogue.brushClassCount << " brush)\n";
		ok &= checkCatalogue(game, catalogue);
		switch (game) {
		case BuiltinEntityGame::Quake:
			ok &= checkQuake(catalogue);
			ok &= checkValidation(catalogue);
			break;
		case BuiltinEntityGame::Quake2:
			ok &= checkQuake2(catalogue);
			break;
		case BuiltinEntityGame::Quake3:
			ok &= checkQuake3(catalogue);
			break;
		}
	}
	BuiltinEntityGame parsed = BuiltinEntityGame::Quake;
	ok &= expect(builtinEntityGameFromId(QStringLiteral(" Quake2 "), &parsed) && parsed == BuiltinEntityGame::Quake2, "game ids ignore case and spaces");
	ok &= expect(!builtinEntityGameFromId(QStringLiteral("doom"), &parsed) && parsed == BuiltinEntityGame::Quake2, "unknown game ids are rejected");
	ok &= checkGameForMap();
	std::cout << (ok ? "entity builtin catalogue smoke passed" : "entity builtin catalogue smoke failed") << "\n";
	return ok ? 0 : 1;
}
