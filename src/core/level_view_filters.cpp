#include "core/level_view_filters.h"

#include "core/entity_builtin_catalogue.h"
#include "core/material_library.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>

namespace vibestudio {

namespace {

// Quake II and Quake III content and surface bits the filters read
// (qfiles.h in the id Software Quake II source and code/game/surfaceflags.h
// in Quake III, both public under the GPL; the values, not the code, are used
// here). Workflow reference for the texture kinds: NRC brushmanip.cpp
// BrushFilters_construct at 449778b3 (GPL-2.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.
constexpr qint64 kContentsLava = 0x8;
constexpr qint64 kContentsSlime = 0x10;
constexpr qint64 kContentsWater = 0x20;
// CONTENTS_FOG in Quake III; Quake II spends the bit on CONTENTS_MIST.
constexpr qint64 kContentsFog = 0x40;
constexpr qint64 kContentsAreaPortal = 0x8000;
constexpr qint64 kContentsPlayerClip = 0x10000;
constexpr qint64 kContentsMonsterClip = 0x20000;
constexpr qint64 kContentsDetail = 0x8000000;
// Quake III only: Quake II spends these bits on water currents.
constexpr qint64 kContentsClusterPortal = 0x100000;
constexpr qint64 kContentsDoNotEnter = 0x200000;
constexpr qint64 kContentsBotClip = 0x400000;
constexpr qint64 kSurfaceSky = 0x4;
// Quake II only: Quake III spends these bits on noimpact and nomarks.
constexpr qint64 kSurfaceTrans33 = 0x10;
constexpr qint64 kSurfaceTrans66 = 0x20;
// SURF_NODRAW in both games.
constexpr qint64 kSurfaceNoDraw = 0x80;
constexpr qint64 kSurfaceHint = 0x100;
constexpr qint64 kSurfaceSkip = 0x200;

// Which formats offer a kind.
enum class FilterFormats {
	QuakeFamily,
	Quake3,
};

struct FilterEntry {
	const char* id;
	const char* title;
	const char* description;
	FilterFormats formats = FilterFormats::QuakeFamily;
};

constexpr FilterEntry kQuakeFilters[] = {
	{"world", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "World Brushes"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Worldspawn, func_group and func_detail with their brushes and patches, the world as Radiant counts it.")},
	{"entities", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Entities"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Every entity but worldspawn, func_group and func_detail: point entities, and brush entities such as doors and triggers.")},
	{"brushes", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Brushes"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Every brush, whether it belongs to the world or to an entity. Patches are not brushes.")},
	{"brush-entities", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Brush Entities"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Doors, platforms, triggers and every other entity made of brushes, other than func_group and func_detail.")},
	{"func-groups", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Func Groups"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "func_group entities with their brushes and patches. Scene groups have their own visibility.")},
	{"patches", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Patches"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Quake III curved surfaces."), FilterFormats::Quake3},
	{"detail", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Detail"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Brushes with the detail content flag, and func_detail entities.")},
	{"structural", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Structural"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Brushes that seal and split the map: on Quake II and III maps every brush without the detail flag outside func_detail, on Quake maps worldspawn and every brush entity but func_detail.")},
	{"clip", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Clip"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Player, monster and weapon clip brushes and patches; on Quake III maps bot clip has a filter of its own.")},
	{"player-clip", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Player Clip"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Brushes and patches made wholly of player clip: the player clip content flag, a Quake III shader with surfaceparm playerclip, or a clip, playerclip or fullclip texture.")},
	{"monster-clip", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Monster Clip"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Brushes and patches made wholly of monster clip: the monster clip content flag, surfaceparm monsterclip, a monsterclip texture, or Quake's clip, which stops monsters too.")},
	{"weapon-clip", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Weapon Clip"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Brushes and patches made wholly of weapon clip, such as Quake III's weapclip and fullclip, which stop shots as well as players.")},
	{"botclip", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Bot Clip"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Quake III brushes made wholly of botclip or donotenter, which only bots see."), FilterFormats::Quake3},
	{"hint", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Hint"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Brushes with a hint face or the hint surface flag, which guide the visibility split.")},
	{"skip", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Skip"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Brushes skipped on every face, and skip faces on other brushes, such as a hint brush's sides, in the camera. On Quake maps skip counts as caulk and only hintskip counts here.")},
	{"caulk", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Caulk"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Brushes caulked on every face, and caulk faces on other brushes in the camera. On Quake maps skip counts as caulk.")},
	{"nodraw", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Nodraw"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Brushes that draw nothing in game: every face has the nodraw surface flag, a Quake III shader with surfaceparm nodraw, or a nodraw texture.")},
	{"areaportals", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Area Portals"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Brushes with an areaportal face or the area portal content flag, and Quake II func_areaportal entities.")},
	{"clusterportals", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Cluster Portals"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Quake III brushes made wholly of clusterportal, which split bot route clusters."), FilterFormats::Quake3},
	{"lightgrid", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Light Grid"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Quake III brushes made wholly of lightgrid, which bound the compiler's light grid."), FilterFormats::Quake3},
	{"sky", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Sky"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Brushes with a sky face: a Quake III shader with skyParms or surfaceparm sky, or else a sky texture or the sky surface flag.")},
	{"liquids", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Liquids"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Water, slime and lava brushes: a Quake III shader with surfaceparm water, slime or lava, or else a liquid texture or content flag.")},
	{"fog", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Fog"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Quake III fog volumes: a brush with a shader that declares surfaceparm fog or fogParms, the fog content flag, or else a texture in a fog or fogs folder or named fog, such as sfx/fog_red."), FilterFormats::Quake3},
	{"translucent", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Translucent"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Brushes and patches with a see-through face: Quake II's 33% and 66% translucent flags, Quake's { fence textures, or a Quake III shader with qer_trans, alphaFunc or fog.")},
	{"decals", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Decals"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Quake III brushes with a face whose texture is under decals/."), FilterFormats::Quake3},
	{"point-entities", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Point Entities"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Point entities such as player starts, items and monsters, but not lights and models, which have filters of their own.")},
	{"lights", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Lights"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Entities whose class begins with light.")},
	{"triggers", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Triggers"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Entities whose class begins with trigger_.")},
	{"monsters", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Monsters"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Entities whose class begins with monster_.")},
	{"items", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Items and Weapons"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Entities whose class begins with item_, weapon_ or ammo_.")},
	{"starts", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Player Starts"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Entities whose class begins with info_player.")},
	{"paths", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Paths"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Path corners and other entities whose class begins with path_.")},
	{"models", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Models"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "misc_model and misc_gamemodel entities.")},
};

QString translated(const char* text)
{
	return QCoreApplication::translate("VibeStudioLevelViewFilters", text);
}

bool isQuakeFamily(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
}

bool offeredFor(const FilterEntry& entry, const LevelMapDocument& document)
{
	return isQuakeFamily(document) && (entry.formats == FilterFormats::QuakeFamily || document.format == LevelMapFormat::Quake3Map);
}

const FilterEntry* entryForId(const QString& id)
{
	for (const FilterEntry& entry : kQuakeFilters) {
		if (id == QLatin1String(entry.id)) {
			return &entry;
		}
	}
	return nullptr;
}

// What the rules need to know about the map's game, worked out once for each
// query: Quake reads skip as caulk and splits structural from detail by
// entity, and only Quake III gives the higher content bits their meaning.
struct FilterGame {
	bool quake = false;
	bool quake3 = false;
	// The open package's shaders, read only on Quake III maps, and what each
	// texture name resolved to during this query, so a map's many faces with
	// one texture look it up once.
	const LevelViewFilterMaterials* materials = nullptr;
	mutable QHash<QString, const LevelViewFilterShader*> resolved;
};

FilterGame filterGame(const LevelMapDocument& document, const LevelViewFilterMaterials& materials)
{
	FilterGame game;
	game.quake3 = document.format == LevelMapFormat::Quake3Map;
	game.quake = document.format == LevelMapFormat::QuakeMap && builtinEntityGameForMap(document) == BuiltinEntityGame::Quake;
	game.materials = game.quake3 && !materials.isEmpty() ? &materials : nullptr;
	return game;
}

// The texture's own name, lower case, without its folders.
QString textureBase(const QString& texture)
{
	QString base = texture.trimmed().toLower();
	const qsizetype slash = base.lastIndexOf(QLatin1Char('/'));
	return slash >= 0 ? base.mid(slash + 1) : base;
}

// The texture's path below textures/, lower case: "decals/blood1".
QString texturePath(const QString& texture)
{
	QString path = texture.trimmed().toLower();
	path.replace(QLatin1Char('\\'), QLatin1Char('/'));
	return path.startsWith(QLatin1String("textures/")) ? path.mid(9) : path;
}

// A fog texture by name, for faces no shader resolves: a part of its path
// that is fog or fogs, or starts with the word fog, so fogs/mist, fog/murk
// and sfx/fog_red count and base_wall/fogged_glass does not.
bool isFogTexturePath(const QString& path)
{
	const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
	return std::any_of(parts.cbegin(), parts.cend(), [](const QString& part) {
		return part.startsWith(QLatin1String("fog")) && (part.size() == 3 || part == QLatin1String("fogs") || !part.at(3).isLetter());
	});
}

bool isBotClipTexture(const QString& texture)
{
	const QString base = textureBase(texture);
	return base == QLatin1String("botclip") || base.startsWith(QLatin1String("donotenter"));
}

// Radiant's caulk faces: a caulk texture, and Quake's skip, which the Quake
// compilers leave out of the map just as idTech3 compilers leave out caulk.
bool isCaulkTexture(const QString& texture, const FilterGame& game)
{
	const QString base = textureBase(texture);
	return base.contains(QLatin1String("caulk")) || (game.quake && base == QLatin1String("skip"));
}

// One surface as the rules see it: a brush face, or a patch, which has a
// shader but no flags of its own.
struct FilterSurface {
	const QString& texture;
	qint64 contentFlags = 0;
	qint64 surfaceFlags = 0;
};

// The shader the open package gives a Quake III surface, or null when the map
// is not a Quake III map or the package neither defines the name nor holds
// its image; then the name rules apply.
const LevelViewFilterShader* shaderFor(const FilterSurface& surface, const FilterGame& game)
{
	if (!game.materials) {
		return nullptr;
	}
	const auto known = game.resolved.constFind(surface.texture);
	if (known != game.resolved.cend()) {
		return known.value();
	}
	const LevelViewFilterShader* shader = game.materials->find(surface.texture);
	game.resolved.insert(surface.texture, shader);
	return shader;
}

// The clip parts, by content bit, Quake III shader and texture name. A
// texture can be more than one: fullclip stops players and shots, and Quake's
// clip, which the compilers turn into hull clipping, stops monsters as well.
bool isPlayerClipSurface(const FilterSurface& surface, const QString& base, const LevelViewFilterShader* shader)
{
	return (surface.contentFlags & kContentsPlayerClip) != 0 || (shader && shader->playerClip) || base == QLatin1String("clip")
		|| base.contains(QLatin1String("playerclip")) || base.contains(QLatin1String("fullclip")) || base.contains(QLatin1String("clipfull"));
}

bool isMonsterClipSurface(const FilterSurface& surface, const QString& base, const LevelViewFilterShader* shader, const FilterGame& game)
{
	return (surface.contentFlags & kContentsMonsterClip) != 0 || (shader && shader->monsterClip) || base.contains(QLatin1String("monsterclip"))
		|| base.contains(QLatin1String("clipmonster")) || base.contains(QLatin1String("monclip")) || (game.quake && base == QLatin1String("clip"));
}

bool isWeaponClipSurface(const QString& base)
{
	return base.contains(QLatin1String("weapclip")) || base.contains(QLatin1String("weaponclip")) || base.contains(QLatin1String("clipweap"))
		|| base.contains(QLatin1String("fullclip")) || base.contains(QLatin1String("clipfull"));
}

bool surfaceMatches(const FilterSurface& surface, const QString& filterId, const FilterGame& game)
{
	const QString base = textureBase(surface.texture);
	const LevelViewFilterShader* shader = levelViewFilterReadsMaterials(filterId) ? shaderFor(surface, game) : nullptr;
	if (filterId == QLatin1String("clip")) {
		// Quake III's bot clip has a kind of its own there.
		const bool clipTexture = base.contains(QLatin1String("clip")) && !(game.quake3 && isBotClipTexture(surface.texture));
		return clipTexture || isPlayerClipSurface(surface, base, shader) || isMonsterClipSurface(surface, base, shader, game) || isWeaponClipSurface(base);
	}
	if (filterId == QLatin1String("player-clip")) {
		return isPlayerClipSurface(surface, base, shader);
	}
	if (filterId == QLatin1String("monster-clip")) {
		return isMonsterClipSurface(surface, base, shader, game);
	}
	if (filterId == QLatin1String("weapon-clip")) {
		return isWeaponClipSurface(base);
	}
	if (filterId == QLatin1String("botclip")) {
		return isBotClipTexture(surface.texture) || (game.quake3 && (surface.contentFlags & (kContentsBotClip | kContentsDoNotEnter)) != 0)
			|| (shader && shader->botClip);
	}
	if (filterId == QLatin1String("caulk")) {
		return isCaulkTexture(surface.texture, game);
	}
	if (filterId == QLatin1String("nodraw")) {
		return (surface.surfaceFlags & kSurfaceNoDraw) != 0 || (shader && shader->nodraw) || base.contains(QLatin1String("nodraw"));
	}
	if (filterId == QLatin1String("hint")) {
		// Hint on its own, as NRC's Hints filter takes a hint face: a hint
		// brush's skip sides belong to Skip.
		return (base.contains(QLatin1String("hint")) && base != QLatin1String("hintskip")) || (surface.surfaceFlags & kSurfaceHint) != 0;
	}
	if (filterId == QLatin1String("skip")) {
		// Quake's skip is caulk; ericw-tools' hintskip is the skip side of a
		// Quake hint brush.
		return base == QLatin1String("hintskip") || (!game.quake && (base == QLatin1String("skip") || (surface.surfaceFlags & kSurfaceSkip) != 0));
	}
	if (filterId == QLatin1String("fog")) {
		// The fog content bit counts either way; otherwise a Quake III shader
		// decides when the package defines it, and the name when it does not.
		const bool contentBit = game.quake3 && (surface.contentFlags & kContentsFog) != 0;
		if (shader) {
			return shader->fog || contentBit;
		}
		return contentBit || isFogTexturePath(texturePath(surface.texture));
	}
	if (filterId == QLatin1String("sky")) {
		// A Quake III shader decides, so sky-named walls stay and skies under
		// other names go, as Radiant's QER_SKY from skyParms has it.
		if (shader) {
			return shader->sky;
		}
		return base.startsWith(QLatin1String("sky")) || texturePath(surface.texture).contains(QLatin1String("skies/")) || base == QLatin1String("skyportal")
			|| (surface.surfaceFlags & kSurfaceSky) != 0;
	}
	if (filterId == QLatin1String("liquids")) {
		// A Quake III shader's surfaceparms come ahead of the name rule, so a
		// lavarock wall stays; Radiant also takes anything under liquids/.
		if (shader) {
			return shader->liquid || texturePath(surface.texture).startsWith(QLatin1String("liquids/"));
		}
		return base.startsWith(QLatin1Char('*')) || texturePath(surface.texture).contains(QLatin1String("liquid")) || base.contains(QLatin1String("water"))
			|| base.contains(QLatin1String("slime")) || base.contains(QLatin1String("lava"))
			|| (surface.contentFlags & (kContentsLava | kContentsSlime | kContentsWater)) != 0;
	}
	if (filterId == QLatin1String("translucent")) {
		// Radiant's QER_TRANS and QER_ALPHATEST on Quake III; Quake II's
		// translucency flags; Quake's and Half-Life's { fence textures, which
		// the engines alpha-test.
		if (shader) {
			return shader->translucent || shader->alphaTested;
		}
		return (!game.quake3 && (surface.surfaceFlags & (kSurfaceTrans33 | kSurfaceTrans66)) != 0) || (!game.quake3 && base.startsWith(QLatin1Char('{')));
	}
	if (filterId == QLatin1String("detail")) {
		return (surface.contentFlags & kContentsDetail) != 0;
	}
	if (filterId == QLatin1String("areaportals")) {
		return base == QLatin1String("areaportal") || (surface.contentFlags & kContentsAreaPortal) != 0;
	}
	if (filterId == QLatin1String("clusterportals")) {
		return base == QLatin1String("clusterportal") || (game.quake3 && (surface.contentFlags & kContentsClusterPortal) != 0);
	}
	if (filterId == QLatin1String("lightgrid")) {
		return base == QLatin1String("lightgrid");
	}
	if (filterId == QLatin1String("decals")) {
		return texturePath(surface.texture).startsWith(QLatin1String("decals/"));
	}
	return false;
}

bool faceMatches(const LevelMapBrushFace& face, const QString& filterId, const FilterGame& game)
{
	return surfaceMatches({face.textureName, face.contentFlags, face.surfaceFlags}, filterId, game);
}

// Kinds that name a brush only when every face matches, as Radiant's
// filter_brush_all_faces does; the other face kinds take any one face.
bool needsEveryFace(const QString& filterId)
{
	return filterId == QLatin1String("caulk") || filterId == QLatin1String("skip") || filterId == QLatin1String("clip")
		|| filterId == QLatin1String("player-clip") || filterId == QLatin1String("monster-clip") || filterId == QLatin1String("weapon-clip")
		|| filterId == QLatin1String("botclip") || filterId == QLatin1String("nodraw") || filterId == QLatin1String("clusterportals")
		|| filterId == QLatin1String("lightgrid");
}

bool isFaceKind(const QString& filterId)
{
	return needsEveryFace(filterId) || filterId == QLatin1String("hint") || filterId == QLatin1String("sky") || filterId == QLatin1String("liquids")
		|| filterId == QLatin1String("fog") || filterId == QLatin1String("translucent") || filterId == QLatin1String("detail")
		|| filterId == QLatin1String("areaportals") || filterId == QLatin1String("decals");
}

// Kinds that also hide their faces one at a time on brushes they do not name
// whole: Radiant's caulk face filter, and skip, whose faces line hint brushes
// as TrenchBroom's Skip tag hides them.
bool hidesFacesOneByOne(const QString& filterId)
{
	return filterId == QLatin1String("caulk") || filterId == QLatin1String("skip");
}

// Kinds that also take patches by their shader, as NRC's PatchFilters_construct
// does for clip and translucent shaders.
bool takesPatches(const QString& filterId)
{
	return filterId == QLatin1String("clip") || filterId == QLatin1String("player-clip") || filterId == QLatin1String("monster-clip")
		|| filterId == QLatin1String("weapon-clip") || filterId == QLatin1String("translucent");
}

bool brushMatches(const LevelMapBrush& brush, const QString& filterId, const FilterGame& game)
{
	if (brush.faces.isEmpty()) {
		return false;
	}
	const bool every = needsEveryFace(filterId);
	for (const LevelMapBrushFace& face : brush.faces) {
		const bool match = faceMatches(face, filterId, game);
		if (every && !match) {
			return false;
		}
		if (!every && match) {
			return true;
		}
	}
	return every;
}

bool classStarts(const QString& className, std::initializer_list<const char*> prefixes)
{
	const QString lowered = className.toLower();
	for (const char* prefix : prefixes) {
		if (lowered.startsWith(QLatin1String(prefix))) {
			return true;
		}
	}
	return false;
}

bool isLightClass(const QString& className)
{
	return classStarts(className, {"light"});
}

bool isModelClass(const QString& className)
{
	return classStarts(className, {"misc_model", "misc_gamemodel"});
}

// Whether the built-in Quake, Quake II or Quake III catalogue defines a class
// as made of brushes. An entity of such a class is a brush entity even before
// it has brushes, as Radiant's entity classes make it a container; a class no
// catalogue knows is judged by whether it owns brushes or patches. No class is
// a point class in one catalogue and a brush class in another.
bool isBuiltinBrushClass(const QString& className)
{
	static const QSet<QString> classes = [] {
		QSet<QString> names;
		for (const BuiltinEntityGame game : {BuiltinEntityGame::Quake, BuiltinEntityGame::Quake2, BuiltinEntityGame::Quake3}) {
			for (const EntityClassDefinition& definition : builtinEntityDefinitions(game).classes) {
				if (definition.kind == EntityClassKind::Brush) {
					names.insert(definition.className.toLower());
				}
			}
		}
		return names;
	}();
	return classes.contains(className.trimmed().toLower());
}

int worldEntityId(const LevelMapDocument& document)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0) {
			return entity.id;
		}
	}
	return document.entities.isEmpty() ? -1 : document.entities.first().id;
}

// Doom things file under the categories of the format's thing table, in the
// order the table first names them; anything else is Other.
QStringList doomCategories(const LevelMapDocument& document)
{
	QStringList categories;
	for (const LevelMapDoomThingType& type : levelMapDoomThingTypes(document.doomFormat)) {
		if (!categories.contains(type.category)) {
			categories << type.category;
		}
	}
	return categories;
}

QString doomCategoryOf(const LevelMapDocument& document, int type)
{
	for (const LevelMapDoomThingType& entry : levelMapDoomThingTypes(document.doomFormat)) {
		if (entry.type == type) {
			return entry.category;
		}
	}
	return QString();
}

QVector<LevelMapSelectionRef> doomMatches(const LevelMapDocument& document, const QString& filterId)
{
	QVector<LevelMapSelectionRef> matches;
	if (!filterId.startsWith(QLatin1String("things:"))) {
		return matches;
	}
	const QString which = filterId.mid(7);
	const QStringList categories = doomCategories(document);
	bool numbered = false;
	const int index = which.toInt(&numbered);
	const QString category = numbered && index >= 0 && index < categories.size() ? categories.at(index) : QString();
	if (numbered && category.isEmpty()) {
		return matches;
	}
	for (const LevelMapDoomThing& thing : document.doomThings) {
		const QString thingCategory = doomCategoryOf(document, thing.type);
		if ((numbered && thingCategory == category) || (!numbered && which == QLatin1String("other") && thingCategory.isEmpty())) {
			matches.push_back({LevelMapSelectionKind::DoomThing, thing.id});
		}
	}
	return matches;
}

// Structural, the inverse of Detail among brushes. Quake II and III maps
// mark detail with a content flag, so every brush without it outside
// func_detail is structural (NRC's inverted filter_brush_all_faces on
// BRUSH_DETAIL_MASK, here the inverse of Detail's any-face rule so the two
// kinds never share a brush). Quake maps mark detail with func_detail
// entities, so worldspawn's brushes and every brush entity but func_detail*
// are structural, as NRC's filter_entity_not_func_detail has it.
QVector<LevelMapSelectionRef> structuralMatches(const LevelMapDocument& document, const FilterGame& game, int world, const QSet<int>& owners)
{
	QVector<LevelMapSelectionRef> matches;
	QSet<int> detailEntities;
	for (const LevelMapEntity& entity : document.entities) {
		if (classStarts(entity.className, {"func_detail"})) {
			detailEntities.insert(entity.id);
		}
	}
	if (!game.quake) {
		for (const LevelMapBrush& brush : document.brushes) {
			if (!brush.faces.isEmpty() && !detailEntities.contains(brush.entityId) && !brushMatches(brush, QStringLiteral("detail"), game)) {
				matches.push_back({LevelMapSelectionKind::QuakeBrush, brush.id});
			}
		}
		return matches;
	}
	for (const LevelMapBrush& brush : document.brushes) {
		if (brush.entityId == world) {
			matches.push_back({LevelMapSelectionKind::QuakeBrush, brush.id});
		}
	}
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.id != world && !detailEntities.contains(entity.id) && (owners.contains(entity.id) || isBuiltinBrushClass(entity.className))) {
			matches.push_back({LevelMapSelectionKind::Entity, entity.id});
		}
	}
	return matches;
}

QVector<LevelMapSelectionRef> quakeMatches(const LevelMapDocument& document, const QString& filterId, const FilterGame& game)
{
	QVector<LevelMapSelectionRef> matches;
	const FilterEntry* entry = entryForId(filterId);
	if (!entry || !offeredFor(*entry, document)) {
		return matches;
	}
	const int world = worldEntityId(document);
	QSet<int> owners;
	for (const LevelMapBrush& brush : document.brushes) {
		owners.insert(brush.entityId);
	}
	for (const LevelMapPatch& patch : document.patches) {
		owners.insert(patch.entityId);
	}
	if (filterId == QLatin1String("world")) {
		// Worldspawn's own brushes and patches, then the func_group and
		// func_detail entities, whose brushes and patches go with them.
		for (const LevelMapBrush& brush : document.brushes) {
			if (brush.entityId == world) {
				matches.push_back({LevelMapSelectionKind::QuakeBrush, brush.id});
			}
		}
		for (const LevelMapPatch& patch : document.patches) {
			if (patch.entityId == world) {
				matches.push_back({LevelMapSelectionKind::QuakePatch, patch.id});
			}
		}
		for (const LevelMapEntity& entity : document.entities) {
			if (entity.id != world && levelViewFilterWorldClass(entity.className)) {
				matches.push_back({LevelMapSelectionKind::Entity, entity.id});
			}
		}
		return matches;
	}
	if (filterId == QLatin1String("brushes")) {
		for (const LevelMapBrush& brush : document.brushes) {
			matches.push_back({LevelMapSelectionKind::QuakeBrush, brush.id});
		}
		return matches;
	}
	if (filterId == QLatin1String("patches")) {
		for (const LevelMapPatch& patch : document.patches) {
			matches.push_back({LevelMapSelectionKind::QuakePatch, patch.id});
		}
		return matches;
	}
	if (filterId == QLatin1String("structural")) {
		return structuralMatches(document, game, world, owners);
	}
	if (isFaceKind(filterId)) {
		QSet<int> detailEntities;
		if (filterId == QLatin1String("detail")) {
			for (const LevelMapEntity& entity : document.entities) {
				if (classStarts(entity.className, {"func_detail"})) {
					detailEntities.insert(entity.id);
				}
			}
		}
		for (const LevelMapBrush& brush : document.brushes) {
			if (brushMatches(brush, filterId, game) || detailEntities.contains(brush.entityId)) {
				matches.push_back({LevelMapSelectionKind::QuakeBrush, brush.id});
			}
		}
		if (filterId == QLatin1String("detail")) {
			for (const LevelMapPatch& patch : document.patches) {
				if (detailEntities.contains(patch.entityId)) {
					matches.push_back({LevelMapSelectionKind::QuakePatch, patch.id});
				}
			}
		}
		if (takesPatches(filterId)) {
			for (const LevelMapPatch& patch : document.patches) {
				if (surfaceMatches({patch.textureName}, filterId, game)) {
					matches.push_back({LevelMapSelectionKind::QuakePatch, patch.id});
				}
			}
		}
		if (filterId == QLatin1String("areaportals")) {
			// Quake II marks an area portal with an entity, whatever the
			// textures of its brushes.
			for (const LevelMapEntity& entity : document.entities) {
				if (entity.className.trimmed().compare(QStringLiteral("func_areaportal"), Qt::CaseInsensitive) == 0) {
					matches.push_back({LevelMapSelectionKind::Entity, entity.id});
				}
			}
		}
		return matches;
	}
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.id == world) {
			continue;
		}
		// A brush entity owns brushes or patches, or has a brush class.
		const bool brushEntity = owners.contains(entity.id) || isBuiltinBrushClass(entity.className);
		const bool worldClass = levelViewFilterWorldClass(entity.className);
		bool match = false;
		if (filterId == QLatin1String("entities")) {
			match = !worldClass;
		} else if (filterId == QLatin1String("brush-entities")) {
			match = brushEntity && !worldClass;
		} else if (filterId == QLatin1String("func-groups")) {
			match = entity.className.trimmed().compare(QStringLiteral("func_group"), Qt::CaseInsensitive) == 0;
		} else if (filterId == QLatin1String("point-entities")) {
			match = !brushEntity && !worldClass && !isLightClass(entity.className) && !isModelClass(entity.className);
		} else if (filterId == QLatin1String("lights")) {
			match = isLightClass(entity.className);
		} else if (filterId == QLatin1String("triggers")) {
			match = classStarts(entity.className, {"trigger_"});
		} else if (filterId == QLatin1String("monsters")) {
			match = classStarts(entity.className, {"monster_"});
		} else if (filterId == QLatin1String("items")) {
			match = classStarts(entity.className, {"item_", "weapon_", "ammo_"});
		} else if (filterId == QLatin1String("starts")) {
			match = classStarts(entity.className, {"info_player"});
		} else if (filterId == QLatin1String("paths")) {
			match = classStarts(entity.className, {"path_"});
		} else if (filterId == QLatin1String("models")) {
			match = isModelClass(entity.className);
		}
		if (match) {
			matches.push_back({LevelMapSelectionKind::Entity, entity.id});
		}
	}
	return matches;
}

QVector<LevelMapSelectionRef> matchesFor(const LevelMapDocument& document, const QString& filterId, const FilterGame& game)
{
	if (document.format == LevelMapFormat::DoomWad) {
		return doomMatches(document, filterId);
	}
	return isQuakeFamily(document) ? quakeMatches(document, filterId, game) : QVector<LevelMapSelectionRef>();
}

QVector<LevelSurfaceFace> faceMatchesFor(const LevelMapDocument& document, const QString& filterId, const FilterGame& game)
{
	QVector<LevelSurfaceFace> faces;
	// Caulk is the one kind Radiant also filters face by face; Skip does as
	// TrenchBroom's Skip tag does.
	const FilterEntry* entry = entryForId(filterId);
	if (!hidesFacesOneByOne(filterId) || !entry || !offeredFor(*entry, document)) {
		return faces;
	}
	for (const LevelMapBrush& brush : document.brushes) {
		// A brush caulked all over is hidden whole.
		if (brushMatches(brush, filterId, game)) {
			continue;
		}
		for (int index = 0; index < brush.faces.size(); ++index) {
			if (faceMatches(brush.faces.at(index), filterId, game)) {
				faces.push_back({brush.id, index});
			}
		}
	}
	return faces;
}

} // namespace

QVector<LevelViewFilter> levelViewFilterKinds()
{
	QVector<LevelViewFilter> kinds;
	for (const FilterEntry& entry : kQuakeFilters) {
		kinds.push_back({QString::fromLatin1(entry.id), translated(entry.title), translated(entry.description)});
	}
	return kinds;
}

QString levelViewFilterCommandId(const QString& filterId)
{
	// Doom categories are numbered from the open map's thing table.
	if (filterId.isEmpty() || filterId.contains(QLatin1Char(':'))) {
		return QString();
	}
	QString id = QStringLiteral("map.filter");
	for (const QString& part : filterId.split(QLatin1Char('-'), Qt::SkipEmptyParts)) {
		id += part.left(1).toUpper() + part.mid(1);
	}
	return id;
}

bool levelViewFilterWorldClass(const QString& className)
{
	const QString name = className.trimmed();
	return name.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0 || name.compare(QStringLiteral("func_group"), Qt::CaseInsensitive) == 0
		|| name.startsWith(QStringLiteral("func_detail"), Qt::CaseInsensitive);
}

QVector<LevelViewFilter> levelViewFilters(const LevelMapDocument& document)
{
	QVector<LevelViewFilter> filters;
	if (isQuakeFamily(document)) {
		for (const FilterEntry& entry : kQuakeFilters) {
			if (offeredFor(entry, document)) {
				filters.push_back({QString::fromLatin1(entry.id), translated(entry.title), translated(entry.description)});
			}
		}
	} else if (document.format == LevelMapFormat::DoomWad) {
		const QStringList categories = doomCategories(document);
		for (int index = 0; index < categories.size(); ++index) {
			filters.push_back({QStringLiteral("things:%1").arg(index), categories.at(index),
				QCoreApplication::translate("VibeStudioLevelViewFilters", "Things filed under %1.").arg(categories.at(index))});
		}
		filters.push_back({QStringLiteral("things:other"), QCoreApplication::translate("VibeStudioLevelViewFilters", "Other Things"),
			QCoreApplication::translate("VibeStudioLevelViewFilters", "Things of types the editor does not name.")});
	}
	return filters;
}

bool levelViewFilterForId(const LevelMapDocument& document, const QString& id, LevelViewFilter* out)
{
	for (const LevelViewFilter& filter : levelViewFilters(document)) {
		if (filter.id == id) {
			if (out) {
				*out = filter;
			}
			return true;
		}
	}
	return false;
}

QVector<LevelMapSelectionRef> levelViewFilterMatches(const LevelMapDocument& document, const QString& filterId, const LevelViewFilterMaterials& materials)
{
	return matchesFor(document, filterId, filterGame(document, materials));
}

QHash<QString, int> levelViewFilterCounts(const LevelMapDocument& document, const LevelViewFilterMaterials& materials)
{
	QHash<QString, int> counts;
	const FilterGame game = filterGame(document, materials);
	for (const LevelViewFilter& filter : levelViewFilters(document)) {
		counts.insert(filter.id, static_cast<int>(matchesFor(document, filter.id, game).size()));
	}
	return counts;
}

QVector<LevelMapSelectionRef> levelViewFilteredObjects(const LevelMapDocument& document, const QStringList& hiddenFilterIds,
	const LevelViewFilterMaterials& materials)
{
	QVector<LevelMapSelectionRef> objects;
	if (hiddenFilterIds.isEmpty()) {
		return objects;
	}
	const FilterGame game = filterGame(document, materials);
	QSet<QPair<int, int>> seen;
	for (const QString& id : hiddenFilterIds) {
		for (const LevelMapSelectionRef& ref : matchesFor(document, id, game)) {
			if (!seen.contains({static_cast<int>(ref.kind), ref.objectId})) {
				seen.insert({static_cast<int>(ref.kind), ref.objectId});
				objects.push_back(ref);
			}
		}
	}
	return objects;
}

QVector<LevelSurfaceFace> levelViewFilterFaceMatches(const LevelMapDocument& document, const QString& filterId, const LevelViewFilterMaterials& materials)
{
	return faceMatchesFor(document, filterId, filterGame(document, materials));
}

QVector<LevelSurfaceFace> levelViewFilteredFaces(const LevelMapDocument& document, const QStringList& hiddenFilterIds,
	const LevelViewFilterMaterials& materials)
{
	QVector<LevelSurfaceFace> faces;
	const bool oneByOne = std::any_of(hiddenFilterIds.cbegin(), hiddenFilterIds.cend(), [](const QString& id) { return hidesFacesOneByOne(id); });
	if (!oneByOne || !isQuakeFamily(document)) {
		return faces;
	}
	const FilterGame game = filterGame(document, materials);
	// Brushes a switched-off kind hides whole, directly or with their entity,
	// have no faces left to hide.
	QSet<int> hiddenBrushes;
	QSet<int> hiddenEntities;
	for (const QString& id : hiddenFilterIds) {
		for (const LevelMapSelectionRef& ref : matchesFor(document, id, game)) {
			if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
				hiddenBrushes.insert(ref.objectId);
			} else if (ref.kind == LevelMapSelectionKind::Entity) {
				hiddenEntities.insert(ref.objectId);
			}
		}
	}
	QHash<int, int> ownerOf;
	for (const LevelMapBrush& brush : document.brushes) {
		ownerOf.insert(brush.id, brush.entityId);
	}
	QSet<QPair<int, int>> seen;
	for (const QString& id : hiddenFilterIds) {
		if (!hidesFacesOneByOne(id)) {
			continue;
		}
		for (const LevelSurfaceFace& face : faceMatchesFor(document, id, game)) {
			if (!hiddenBrushes.contains(face.brushId) && !hiddenEntities.contains(ownerOf.value(face.brushId, -1)) && !seen.contains({face.brushId, face.faceIndex})) {
				seen.insert({face.brushId, face.faceIndex});
				faces.push_back(face);
			}
		}
	}
	return faces;
}

QStringList levelViewFiltersInverted(const LevelMapDocument& document, const QStringList& hiddenFilterIds)
{
	const QVector<LevelViewFilter> offered = levelViewFilters(document);
	QSet<QString> offeredIds;
	for (const LevelViewFilter& filter : offered) {
		offeredIds.insert(filter.id);
	}
	QStringList inverted;
	for (const QString& id : hiddenFilterIds) {
		if (!offeredIds.contains(id) && !inverted.contains(id)) {
			inverted << id;
		}
	}
	for (const LevelViewFilter& filter : offered) {
		if (!hiddenFilterIds.contains(filter.id)) {
			inverted << filter.id;
		}
	}
	return inverted;
}

QString levelViewFiltersSettingsKey()
{
	return QStringLiteral("levelView/filtersOff");
}

QStringList levelViewFiltersFromSaved(const QByteArray& saved)
{
	QStringList ids;
	for (const QString& part : QString::fromUtf8(saved).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
		const QString id = part.trimmed();
		if (!id.isEmpty() && !ids.contains(id)) {
			ids << id;
		}
	}
	return ids;
}

QByteArray levelViewFiltersSavedValue(const QStringList& hiddenFilterIds)
{
	return levelViewFiltersFromSaved(hiddenFilterIds.join(QLatin1Char(',')).toUtf8()).join(QLatin1Char(',')).toUtf8();
}

const LevelViewFilterShader* LevelViewFilterMaterials::find(const QString& textureName) const
{
	if (shaders.isEmpty() || textureName.trimmed().isEmpty()) {
		return nullptr;
	}
	// Quake III maps name shaders without their textures/ folder.
	const QString key = materialLookupKey(textureName);
	const auto prefixed = shaders.constFind(key.startsWith(QLatin1String("textures/")) ? key : QStringLiteral("textures/") + key);
	if (prefixed != shaders.cend()) {
		return &prefixed.value();
	}
	const auto bare = shaders.constFind(key);
	return bare != shaders.cend() ? &bare.value() : nullptr;
}

// Radiant's editor flags from a Quake III shader (NRC plugins/shaders/shaders.cpp
// at 449778b3: qer_trans and surfaceparm fog set QER_TRANS, qer_alphafunc
// QER_ALPHATEST, skyparms QER_SKY, surfaceparm water, lava and slime
// QER_LIQUID, playerclip QER_CLIP, botclip QER_BOTCLIP and nodraw
// QER_NODRAW). VibeStudio also counts a stage's alphaFunc, which the game
// alpha-tests, monsterclip and donotenter. Original implementation over the
// studio's own parser; see docs/CREDITS.md.
LevelViewFilterShader levelViewFilterShader(const MaterialDefinition& definition)
{
	LevelViewFilterShader shader;
	const auto parm = [&definition](const char* name) { return definition.surfaceParms.contains(QLatin1String(name)); };
	for (const MaterialDirective& directive : definition.directives) {
		if (directive.keyword.compare(QLatin1String("qer_trans"), Qt::CaseInsensitive) == 0) {
			shader.translucent = true;
		} else if (directive.keyword.compare(QLatin1String("qer_alphafunc"), Qt::CaseInsensitive) == 0) {
			shader.alphaTested = true;
		}
	}
	shader.translucent = shader.translucent || definition.isFog() || definition.translucent;
	for (const MaterialStage& stage : definition.stages) {
		shader.alphaTested = shader.alphaTested || stage.alphaTest != MaterialAlphaTest::None;
	}
	shader.liquid = parm("water") || parm("slime") || parm("lava");
	shader.playerClip = parm("playerclip");
	shader.monsterClip = parm("monsterclip");
	shader.botClip = parm("botclip") || parm("donotenter");
	shader.sky = definition.isSky();
	shader.nodraw = parm("nodraw");
	shader.fog = definition.isFog();
	for (const QString& name : definition.surfaceParms) {
		const QString lowered = name.trimmed().toLower();
		if (!lowered.isEmpty() && !shader.surfaceParms.contains(lowered)) {
			shader.surfaceParms << lowered;
		}
	}
	return shader;
}

LevelViewFilterMaterials levelViewFilterMaterials(const MaterialLibrary& library)
{
	LevelViewFilterMaterials materials;
	materials.sourcePath = library.sourcePath;
	materials.complete = library.complete;
	materials.cancelled = library.cancelled;
	materials.warnings = library.warnings;
	if (library.cancelled) {
		return materials;
	}
	for (const MaterialScript& script : library.scripts) {
		materials.scriptCount += script.engine == MaterialEngine::Quake3 ? 1 : 0;
	}
	for (int index = 0; index < library.entries.size(); ++index) {
		const MaterialLibraryEntry& entry = library.entries.at(index);
		// The definition the game uses: a shadowed one never draws.
		if (entry.engine != MaterialEngine::Quake3 || entry.shadowed) {
			continue;
		}
		const QString key = materialLookupKey(entry.name);
		if (const MaterialScript* script = library.script(index); script && entry.materialIndex >= 0 && entry.materialIndex < script->materials.size()) {
			materials.shaders.insert(key, levelViewFilterShader(script->materials.at(entry.materialIndex)));
			++materials.scriptedShaderCount;
		} else if (entry.scriptIndex == -2 && !materials.shaders.contains(key)) {
			// An image no script defines draws with the default shader, which
			// has no flags, so names say nothing about it.
			materials.shaders.insert(key, LevelViewFilterShader());
		}
	}
	return materials;
}

LevelViewFilterMaterials scanLevelViewFilterMaterials(const PackageArchiveReader& reader, const std::function<bool()>& cancelled)
{
	MaterialLibraryOptions options;
	options.includeClassic = false;
	options.engines = {MaterialEngine::Quake3};
	const MaterialLibrary library = scanMaterialLibrary(reader, options, [&cancelled](int, int, const QString&) { return !cancelled || !cancelled(); });
	return levelViewFilterMaterials(library);
}

bool levelViewFilterReadsMaterials(const QString& filterId)
{
	return filterId == QLatin1String("translucent") || filterId == QLatin1String("liquids") || filterId == QLatin1String("clip")
		|| filterId == QLatin1String("player-clip") || filterId == QLatin1String("monster-clip") || filterId == QLatin1String("botclip")
		|| filterId == QLatin1String("sky") || filterId == QLatin1String("nodraw") || filterId == QLatin1String("fog");
}

} // namespace vibestudio
