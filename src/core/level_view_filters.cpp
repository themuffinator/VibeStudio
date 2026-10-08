#include "core/level_view_filters.h"

#include <QCoreApplication>
#include <QSet>

namespace vibestudio {

namespace {

// Quake II and Quake III content and surface bits the filters read
// (qfiles.h in the id Software Quake II source and q_shared.h in Quake III,
// both public under the GPL; the values, not the code, are used here).
constexpr qint64 kContentsLava = 0x8;
constexpr qint64 kContentsSlime = 0x10;
constexpr qint64 kContentsWater = 0x20;
constexpr qint64 kContentsPlayerClip = 0x10000;
constexpr qint64 kContentsMonsterClip = 0x20000;
constexpr qint64 kContentsDetail = 0x8000000;
constexpr qint64 kSurfaceSky = 0x4;
constexpr qint64 kSurfaceHint = 0x100;
constexpr qint64 kSurfaceSkip = 0x200;

struct FilterEntry {
	const char* id;
	const char* title;
	const char* description;
};

constexpr FilterEntry kQuakeFilters[] = {
	{"world", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "World Brushes"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Brushes and patches that belong to worldspawn.")},
	{"brush-entities", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Brush Entities"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Doors, platforms, func_groups and every other entity made of brushes.")},
	{"patches", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Patches"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Quake III curved surfaces.")},
	{"detail", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Detail"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Brushes with the detail content flag, and func_detail entities.")},
	{"clip", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Clip"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Player, monster and weapon clip brushes.")},
	{"hint", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Hint and Skip"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Brushes with hint or skip faces, which guide visibility.")},
	{"caulk", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Caulk"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Brushes caulked on every face.")},
	{"sky", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Sky"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Brushes with a sky face.")},
	{"liquids", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Liquids"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Water, slime and lava brushes.")},
	{"point-entities", QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Point Entities"),
		QT_TRANSLATE_NOOP("VibeStudioLevelViewFilters", "Every entity with no brushes of its own.")},
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

// The texture's own name, lower case, without its folders.
QString textureBase(const QString& texture)
{
	QString base = texture.trimmed().toLower();
	const qsizetype slash = base.lastIndexOf(QLatin1Char('/'));
	return slash >= 0 ? base.mid(slash + 1) : base;
}

bool isClipTexture(const QString& texture)
{
	return textureBase(texture).contains(QStringLiteral("clip"));
}

bool isCaulkTexture(const QString& texture)
{
	return textureBase(texture).contains(QStringLiteral("caulk"));
}

bool isHintTexture(const QString& texture)
{
	const QString base = textureBase(texture);
	return base.contains(QStringLiteral("hint")) || base == QStringLiteral("skip");
}

bool isSkyTexture(const QString& texture)
{
	const QString lowered = texture.trimmed().toLower();
	const QString base = textureBase(texture);
	return base.startsWith(QStringLiteral("sky")) || lowered.contains(QStringLiteral("skies/")) || base == QStringLiteral("skyportal");
}

bool isLiquidTexture(const QString& texture)
{
	const QString lowered = texture.trimmed().toLower();
	const QString base = textureBase(texture);
	return base.startsWith(QLatin1Char('*')) || lowered.contains(QStringLiteral("liquid")) || base.contains(QStringLiteral("water"))
		|| base.contains(QStringLiteral("slime")) || base.contains(QStringLiteral("lava"));
}

bool brushMatches(const LevelMapBrush& brush, const QString& filterId)
{
	if (brush.faces.isEmpty()) {
		return false;
	}
	bool all = true;
	bool any = false;
	for (const LevelMapBrushFace& face : brush.faces) {
		bool match = false;
		if (filterId == QLatin1String("clip")) {
			match = isClipTexture(face.textureName) || (face.contentFlags & (kContentsPlayerClip | kContentsMonsterClip)) != 0;
		} else if (filterId == QLatin1String("caulk")) {
			match = isCaulkTexture(face.textureName);
		} else if (filterId == QLatin1String("hint")) {
			match = isHintTexture(face.textureName) || (face.surfaceFlags & (kSurfaceHint | kSurfaceSkip)) != 0;
		} else if (filterId == QLatin1String("sky")) {
			match = isSkyTexture(face.textureName) || (face.surfaceFlags & kSurfaceSky) != 0;
		} else if (filterId == QLatin1String("liquids")) {
			match = isLiquidTexture(face.textureName) || (face.contentFlags & (kContentsLava | kContentsSlime | kContentsWater)) != 0;
		} else if (filterId == QLatin1String("detail")) {
			match = (face.contentFlags & kContentsDetail) != 0;
		}
		all = all && match;
		any = any || match;
	}
	// Caulk and clip name brushes made wholly of them; the rest any brush with
	// one such face.
	return (filterId == QLatin1String("caulk") || filterId == QLatin1String("clip")) ? all : any;
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

} // namespace

QVector<LevelViewFilter> levelViewFilters(const LevelMapDocument& document)
{
	QVector<LevelViewFilter> filters;
	if (isQuakeFamily(document)) {
		const bool quake3 = document.format == LevelMapFormat::Quake3Map;
		for (const FilterEntry& entry : kQuakeFilters) {
			const QString id = QString::fromLatin1(entry.id);
			if (id == QLatin1String("patches") && !quake3) {
				continue;
			}
			filters.push_back({id, translated(entry.title), translated(entry.description)});
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

QVector<LevelMapSelectionRef> levelViewFilterMatches(const LevelMapDocument& document, const QString& filterId)
{
	QVector<LevelMapSelectionRef> matches;
	if (document.format == LevelMapFormat::DoomWad) {
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
	if (!isQuakeFamily(document)) {
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
		return matches;
	}
	if (filterId == QLatin1String("patches")) {
		for (const LevelMapPatch& patch : document.patches) {
			matches.push_back({LevelMapSelectionKind::QuakePatch, patch.id});
		}
		return matches;
	}
	if (filterId == QLatin1String("clip") || filterId == QLatin1String("caulk") || filterId == QLatin1String("hint") || filterId == QLatin1String("sky")
		|| filterId == QLatin1String("liquids") || filterId == QLatin1String("detail")) {
		QSet<int> detailEntities;
		if (filterId == QLatin1String("detail")) {
			for (const LevelMapEntity& entity : document.entities) {
				if (classStarts(entity.className, {"func_detail"})) {
					detailEntities.insert(entity.id);
				}
			}
		}
		for (const LevelMapBrush& brush : document.brushes) {
			if (brushMatches(brush, filterId) || detailEntities.contains(brush.entityId)) {
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
		return matches;
	}
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.id == world) {
			continue;
		}
		const bool ownsGeometry = owners.contains(entity.id);
		bool match = false;
		if (filterId == QLatin1String("brush-entities")) {
			match = ownsGeometry;
		} else if (filterId == QLatin1String("point-entities")) {
			match = !ownsGeometry;
		} else if (filterId == QLatin1String("lights")) {
			match = classStarts(entity.className, {"light"});
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
			match = classStarts(entity.className, {"misc_model", "misc_gamemodel"});
		}
		if (match) {
			matches.push_back({LevelMapSelectionKind::Entity, entity.id});
		}
	}
	return matches;
}

QHash<QString, int> levelViewFilterCounts(const LevelMapDocument& document)
{
	QHash<QString, int> counts;
	for (const LevelViewFilter& filter : levelViewFilters(document)) {
		counts.insert(filter.id, static_cast<int>(levelViewFilterMatches(document, filter.id).size()));
	}
	return counts;
}

QVector<LevelMapSelectionRef> levelViewFilteredObjects(const LevelMapDocument& document, const QStringList& hiddenFilterIds)
{
	QVector<LevelMapSelectionRef> objects;
	QSet<QPair<int, int>> seen;
	for (const QString& id : hiddenFilterIds) {
		for (const LevelMapSelectionRef& ref : levelViewFilterMatches(document, id)) {
			if (!seen.contains({static_cast<int>(ref.kind), ref.objectId})) {
				seen.insert({static_cast<int>(ref.kind), ref.objectId});
				objects.push_back(ref);
			}
		}
	}
	return objects;
}

} // namespace vibestudio
