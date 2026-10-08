#include "core/level_sidebar.h"

#include "core/editor_profiles.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QSet>

namespace vibestudio {

namespace {

struct TabEntry {
	const char* id;
	const char* icon;
	const char* title;
	const char* description;
};

// The studio's own order and names. Titles and descriptions are translated
// where they are read.
constexpr TabEntry kTabs[] = {
	{"outliner", "tree", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Outliner"),
		QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Every entity, brush, patch and Doom object, filtered by name or by key=value, with the map's layers and groups.")},
	{"entities", "entity", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Entities"),
		QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Entity classes or Doom things to place: drag one onto a view, or place it at the view's centre or on the camera's surface.")},
	{"shapes", "shapes", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Shapes"),
		QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Brush shapes to draw, add or turn the selected brushes into: boxes, wedges, cylinders, cones and spheres, and arches, rings, stairs and rooms of several brushes; Doom sectors on Doom maps.")},
	{"textures", "image", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Textures"),
		QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "The map's textures and the package's, to put on the selection or paint in the camera.")},
	{"models", "cube", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Models"),
		QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Package models to place as model entities or to give the selected entity.")},
	{"sounds", "speaker", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Sounds"),
		QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Package sounds to listen to, place as speakers or give the selected entity.")},
	{"prefabs", "prefab", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Prefabs"),
		QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Saved prefabs to insert, and the selection to save as one.")},
	{"tools", "hammer", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Tools"),
		QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Every editing tool in one place, grouped: brushes, transforms, alignment, selection, entities, surfaces, patches, Doom sectors and regions.")},
	{"inspector", "properties", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Inspector"),
		QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "The selection's keys, fields, position and definition, edited in place.")},
	{"surfaces", "uv", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Surfaces"),
		QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Texture alignment for the inspected face: shift, scale, rotate, fit, copy and paste.")},
	{"map", "map", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Map"),
		QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Worldspawn keys, a pre-build checklist, statistics and the entity definitions in use.")},
	{"view", "eye", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "View"),
		QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "What the views draw, filters by kind, grid and snapping, the camera and the view layout.")},
	{"health", "validate", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Health"),
		QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Validation, missing assets and leaks; each leads to the object concerned.")},
	{"history", "history", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "History"),
		QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Every edit to the map, oldest first; choose one to undo or redo to it.")},
};

struct FamilyEntry {
	const char* family;
	const char* leading;
	const char* trailing;
	const char* groupStarts;
	const char* leadingCurrent;
	const char* trailingCurrent;
};

// Where each family of editors keeps these tools. Studio follows Blender's
// split: browsing on one side, properties on the other.
constexpr FamilyEntry kFamilies[] = {
	{"studio", "outliner shapes entities textures models sounds prefabs", "inspector tools surfaces map view health history", "shapes map health", "outliner",
		"inspector"},
	// Radiant: the texture browser and the entity inspector's class list come
	// first; the Entity List is a window of its own.
	{"radiant", "textures entities outliner shapes models sounds prefabs", "inspector tools surfaces view map health history", "shapes view health", "textures",
		"inspector"},
	// TrenchBroom: one inspector with Map, Entity and Face tabs, its browsers
	// inside them.
	{"trenchbroom", "", "map inspector surfaces tools entities shapes textures models sounds prefabs outliner view health history", "tools outliner", "",
		"inspector"},
	// Hammer and J.A.C.K.: the object bar and texture group on the right.
	{"hammer", "outliner view", "textures shapes entities models sounds prefabs inspector surfaces tools map health history", "view inspector map", "outliner",
		"textures"},
	// Doom Builder: one docker of properties, things and textures.
	{"doom-builder", "", "inspector entities shapes textures surfaces tools outliner view map health history models sounds prefabs", "outliner health models", "",
		"inspector"},
	{"quark", "outliner entities shapes textures models sounds prefabs", "inspector tools surfaces view map health history", "models view health", "outliner",
		"inspector"},
	{"blender", "entities shapes textures models sounds prefabs", "outliner inspector tools surfaces view map health history", "inspector view health", "entities",
		"inspector"},
	{"unity", "outliner shapes entities textures models sounds prefabs", "inspector tools surfaces map view health history", "shapes map health", "outliner",
		"inspector"},
	{"unreal", "entities shapes textures models sounds prefabs", "outliner inspector tools surfaces view map health history", "inspector view health", "entities",
		"inspector"},
	{"godot", "outliner shapes entities textures models sounds prefabs", "inspector tools surfaces view map health history", "shapes view health", "outliner",
		"inspector"},
};

struct TitleEntry {
	const char* family;
	const char* tab;
	const char* title;
};

// The names each family uses for the same tools.
constexpr TitleEntry kTitles[] = {
	{"radiant", "outliner", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Entity List")},
	{"radiant", "inspector", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Entity")},
	{"radiant", "surfaces", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Surface")},
	{"radiant", "view", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Filters")},
	{"radiant", "health", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Issues")},
	{"radiant", "history", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Undo")},
	{"radiant", "shapes", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Brush")},
	{"trenchbroom", "inspector", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Entity")},
	{"trenchbroom", "surfaces", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Face")},
	{"trenchbroom", "health", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Issues")},
	{"hammer", "outliner", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Objects")},
	{"hammer", "inspector", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Properties")},
	{"hammer", "surfaces", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Face Edit")},
	{"hammer", "health", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Problems")},
	{"hammer", "shapes", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Primitives")},
	{"doom-builder", "inspector", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Properties")},
	{"doom-builder", "entities", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Things")},
	{"doom-builder", "health", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Analysis")},
	{"doom-builder", "history", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Undo")},
	{"doom-builder", "shapes", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Draw")},
	{"quark", "outliner", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Map Tree")},
	{"quark", "inspector", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Specifics")},
	{"quark", "surfaces", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Faces")},
	{"blender", "inspector", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Item")},
	{"blender", "tools", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Tool")},
	{"blender", "entities", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Add")},
	{"blender", "textures", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Materials")},
	{"unity", "outliner", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Hierarchy")},
	{"unity", "textures", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Materials")},
	{"unreal", "inspector", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Details")},
	{"unreal", "entities", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Place")},
	{"unreal", "textures", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Materials")},
	{"godot", "outliner", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Scene")},
	{"godot", "textures", QT_TRANSLATE_NOOP("VibeStudioLevelSidebar", "Materials")},
};

QString translated(const char* text)
{
	return QCoreApplication::translate("VibeStudioLevelSidebar", text);
}

QStringList words(const char* text)
{
	return QString::fromLatin1(text).split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

const FamilyEntry& familyEntry(const QString& family)
{
	for (const FamilyEntry& entry : kFamilies) {
		if (family == QLatin1String(entry.family)) {
			return entry;
		}
	}
	return kFamilies[0];
}

} // namespace

QVector<LevelSidebarTab> levelSidebarTabs()
{
	QVector<LevelSidebarTab> tabs;
	tabs.reserve(static_cast<int>(std::size(kTabs)));
	for (const TabEntry& entry : kTabs) {
		const QString id = QString::fromLatin1(entry.id);
		tabs.push_back({id, QString::fromLatin1(entry.icon), translated(entry.title), translated(entry.description), QStringLiteral("map.sidebar.") + id});
	}
	return tabs;
}

QStringList levelSidebarTabIds()
{
	QStringList ids;
	for (const TabEntry& entry : kTabs) {
		ids << QString::fromLatin1(entry.id);
	}
	return ids;
}

bool levelSidebarTabForId(const QString& id, LevelSidebarTab* out)
{
	for (const LevelSidebarTab& tab : levelSidebarTabs()) {
		if (tab.id == id) {
			if (out) {
				*out = tab;
			}
			return true;
		}
	}
	return false;
}

QString levelSidebarFamilyForProfile(const QString& profileId)
{
	static const QHash<QString, QString> families {
		{QStringLiteral("gtkradiant-1-6"), QStringLiteral("radiant")},
		{QStringLiteral("gtkradiant-1-5"), QStringLiteral("radiant")},
		{QStringLiteral("gtkradiant-1-4"), QStringLiteral("radiant")},
		{QStringLiteral("qeradiant"), QStringLiteral("radiant")},
		{QStringLiteral("q3radiant"), QStringLiteral("radiant")},
		{QStringLiteral("netradiant"), QStringLiteral("radiant")},
		{QStringLiteral("netradiant-custom"), QStringLiteral("radiant")},
		{QStringLiteral("darkradiant"), QStringLiteral("radiant")},
		// DoomEdit is id's Radiant for Doom 3, not a Doom map editor.
		{QStringLiteral("doomedit"), QStringLiteral("radiant")},
		{QStringLiteral("trenchbroom"), QStringLiteral("trenchbroom")},
		{QStringLiteral("hammer"), QStringLiteral("hammer")},
		{QStringLiteral("jack"), QStringLiteral("hammer")},
		{QStringLiteral("sledge"), QStringLiteral("hammer")},
		{QStringLiteral("bsp"), QStringLiteral("hammer")},
		{QStringLiteral("doom-builder"), QStringLiteral("doom-builder")},
		{QStringLiteral("ultimate-doom-builder"), QStringLiteral("doom-builder")},
		{QStringLiteral("slade"), QStringLiteral("doom-builder")},
		{QStringLiteral("eureka"), QStringLiteral("doom-builder")},
		{QStringLiteral("quark"), QStringLiteral("quark")},
		{QStringLiteral("blender"), QStringLiteral("blender")},
		{QStringLiteral("unity"), QStringLiteral("unity")},
		{QStringLiteral("unreal"), QStringLiteral("unreal")},
		{QStringLiteral("godot"), QStringLiteral("godot")},
	};
	return families.value(normalizedEditorProfileId(profileId), QStringLiteral("studio"));
}

QStringList levelSidebarFamilies()
{
	QStringList families;
	for (const FamilyEntry& entry : kFamilies) {
		families << QString::fromLatin1(entry.family);
	}
	return families;
}

LevelSidebarArrangement levelSidebarArrangementForProfile(const QString& profileId)
{
	const FamilyEntry& entry = familyEntry(levelSidebarFamilyForProfile(profileId));
	LevelSidebarArrangement arrangement;
	arrangement.leading = words(entry.leading);
	arrangement.trailing = words(entry.trailing);
	arrangement.groupStarts = words(entry.groupStarts);
	arrangement.leadingCurrent = QString::fromLatin1(entry.leadingCurrent);
	arrangement.trailingCurrent = QString::fromLatin1(entry.trailingCurrent);
	return arrangement;
}

QString levelSidebarTabTitle(const QString& tabId, const QString& profileId, bool doomMap)
{
	const QString family = levelSidebarFamilyForProfile(profileId);
	for (const TitleEntry& entry : kTitles) {
		if (family == QLatin1String(entry.family) && tabId == QLatin1String(entry.tab)) {
			return translated(entry.title);
		}
	}
	// Doom calls its entities things, whichever editor's names are in use.
	if (doomMap && tabId == QLatin1String("entities")) {
		return QCoreApplication::translate("VibeStudioLevelSidebar", "Things");
	}
	LevelSidebarTab tab;
	return levelSidebarTabForId(tabId, &tab) ? tab.title : tabId;
}

LevelSidebarArrangement normalizedLevelSidebarArrangement(const LevelSidebarArrangement& arrangement, const QString& profileId)
{
	const QStringList known = levelSidebarTabIds();
	QSet<QString> placed;
	const auto clean = [&known, &placed](const QStringList& ids) {
		QStringList kept;
		for (const QString& id : ids) {
			if (known.contains(id) && !placed.contains(id)) {
				placed.insert(id);
				kept << id;
			}
		}
		return kept;
	};
	LevelSidebarArrangement normalized;
	normalized.leading = clean(arrangement.leading);
	normalized.trailing = clean(arrangement.trailing);
	const LevelSidebarArrangement defaults = levelSidebarArrangementForProfile(profileId);
	for (const QString& id : known) {
		if (!placed.contains(id)) {
			(defaults.leading.contains(id) ? normalized.leading : normalized.trailing) << id;
			placed.insert(id);
		}
	}
	for (const QString& id : arrangement.groupStarts) {
		if (known.contains(id) && !normalized.groupStarts.contains(id)) {
			normalized.groupStarts << id;
		}
	}
	normalized.leadingCurrent = normalized.leading.contains(arrangement.leadingCurrent) ? arrangement.leadingCurrent
		: (normalized.leading.isEmpty() ? QString() : normalized.leading.first());
	normalized.trailingCurrent = normalized.trailing.contains(arrangement.trailingCurrent) ? arrangement.trailingCurrent
		: (normalized.trailing.isEmpty() ? QString() : normalized.trailing.first());
	return normalized;
}

bool validateLevelSidebarArrangement(const LevelSidebarArrangement& arrangement, QString* error)
{
	const QStringList known = levelSidebarTabIds();
	QSet<QString> seen;
	for (const QString& id : arrangement.leading + arrangement.trailing) {
		if (!known.contains(id)) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelSidebar", "Unknown sidebar tab: %1").arg(id);
			}
			return false;
		}
		if (seen.contains(id)) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelSidebar", "The %1 tab is placed twice.").arg(id);
			}
			return false;
		}
		seen.insert(id);
	}
	for (const QString& id : known) {
		if (!seen.contains(id)) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelSidebar", "The %1 tab is on neither sidebar.").arg(id);
			}
			return false;
		}
	}
	for (const QString& id : arrangement.groupStarts) {
		if (!known.contains(id)) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelSidebar", "Unknown sidebar tab: %1").arg(id);
			}
			return false;
		}
	}
	if ((!arrangement.leadingCurrent.isEmpty() && !arrangement.leading.contains(arrangement.leadingCurrent))
		|| (!arrangement.trailingCurrent.isEmpty() && !arrangement.trailing.contains(arrangement.trailingCurrent))) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelSidebar", "A current tab is not on its sidebar.");
		}
		return false;
	}
	return true;
}

QJsonObject levelSidebarArrangementJson(const LevelSidebarArrangement& arrangement, const QString& profileId)
{
	const auto side = [&arrangement, &profileId](const QStringList& ids) {
		QJsonArray tabs;
		for (const QString& id : ids) {
			LevelSidebarTab tab;
			if (!levelSidebarTabForId(id, &tab)) {
				continue;
			}
			tabs.append(QJsonObject {
				{QStringLiteral("id"), id},
				{QStringLiteral("title"), levelSidebarTabTitle(id, profileId)},
				{QStringLiteral("icon"), tab.iconName},
				{QStringLiteral("command"), tab.commandId},
				{QStringLiteral("startsGroup"), arrangement.groupStarts.contains(id)},
			});
		}
		return tabs;
	};
	return QJsonObject {
		{QStringLiteral("profile"), normalizedEditorProfileId(profileId)},
		{QStringLiteral("family"), levelSidebarFamilyForProfile(profileId)},
		{QStringLiteral("leading"), side(arrangement.leading)},
		{QStringLiteral("trailing"), side(arrangement.trailing)},
		{QStringLiteral("leadingCurrent"), arrangement.leadingCurrent},
		{QStringLiteral("trailingCurrent"), arrangement.trailingCurrent},
	};
}

bool levelSidebarArrangementFromJson(const QJsonObject& object, LevelSidebarArrangement* out, QString* error)
{
	LevelSidebarArrangement arrangement;
	const auto read = [&arrangement](const QJsonValue& value, QStringList* ids) {
		if (!value.isArray()) {
			return false;
		}
		for (const QJsonValue& entry : value.toArray()) {
			const QString id = entry.isObject() ? entry.toObject().value(QStringLiteral("id")).toString() : entry.toString();
			if (id.isEmpty()) {
				return false;
			}
			*ids << id;
			if (entry.isObject() && entry.toObject().value(QStringLiteral("startsGroup")).toBool()) {
				arrangement.groupStarts << id;
			}
		}
		return true;
	};
	if (!read(object.value(QStringLiteral("leading")), &arrangement.leading) || !read(object.value(QStringLiteral("trailing")), &arrangement.trailing)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelSidebar", "A sidebar arrangement needs leading and trailing lists of tab ids.");
		}
		return false;
	}
	arrangement.leadingCurrent = object.value(QStringLiteral("leadingCurrent")).toString();
	arrangement.trailingCurrent = object.value(QStringLiteral("trailingCurrent")).toString();
	if (!validateLevelSidebarArrangement(arrangement, error)) {
		return false;
	}
	if (out) {
		*out = arrangement;
	}
	return true;
}

QStringList levelSidebarArrangementLines(const LevelSidebarArrangement& arrangement, const QString& profileId)
{
	const auto side = [&arrangement, &profileId](const QStringList& ids, const QString& current) {
		QStringList titles;
		for (const QString& id : ids) {
			QString title = levelSidebarTabTitle(id, profileId);
			if (arrangement.groupStarts.contains(id) && !titles.isEmpty()) {
				title.prepend(QStringLiteral("| "));
			}
			if (id == current) {
				title = QCoreApplication::translate("VibeStudioLevelSidebar", "%1 (open)").arg(title);
			}
			titles << title;
		}
		return titles.isEmpty() ? QCoreApplication::translate("VibeStudioLevelSidebar", "(none)") : titles.join(QStringLiteral("  "));
	};
	return {
		QCoreApplication::translate("VibeStudioLevelSidebar", "Profile: %1 (%2 family)").arg(normalizedEditorProfileId(profileId), levelSidebarFamilyForProfile(profileId)),
		QCoreApplication::translate("VibeStudioLevelSidebar", "Leading sidebar: %1").arg(side(arrangement.leading, arrangement.leadingCurrent)),
		QCoreApplication::translate("VibeStudioLevelSidebar", "Trailing sidebar: %1").arg(side(arrangement.trailing, arrangement.trailingCurrent)),
	};
}

} // namespace vibestudio
