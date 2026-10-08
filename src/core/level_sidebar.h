#pragma once

// The Levels page's two tabbed sidebars, as data.
//
// Every tool the level editor offers beside its views lives on a sidebar tab:
// the outliner, the entity, texture, model, sound and prefab browsers, the
// inspector, surfaces, map, view, health and history. Which tabs sit on the
// leading sidebar and which on the trailing one, the order they come in, where
// the groups break, and what each tab is called follow the editor profile, so
// a TrenchBroom user finds one inspector with Map, Entity and Face tabs, a
// Radiant user an Entity List and a Surface inspector, and a Doom Builder user
// Things. The shell lays the tabs out from these values; the CLI reports them.
//
// "Leading" and "trailing" are reading-order sides: leading is the left side
// in a left-to-right layout and the right side in a right-to-left one.

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct LevelSidebarTab {
	QString id;
	QString iconName;
	// The neutral title and what the tab is for, already translated.
	QString title;
	QString description;
	// The command that shows the tab.
	QString commandId;
};

struct LevelSidebarArrangement {
	QStringList leading;
	QStringList trailing;
	// Tabs that start a new group: a gap and a rule set them apart.
	QStringList groupStarts;
	QString leadingCurrent;
	QString trailingCurrent;
};

// Every tab, in the studio's own order.
[[nodiscard]] QVector<LevelSidebarTab> levelSidebarTabs();
[[nodiscard]] QStringList levelSidebarTabIds();
[[nodiscard]] bool levelSidebarTabForId(const QString& id, LevelSidebarTab* out = nullptr);

// The family of editors a profile follows for its sidebars: "studio",
// "radiant", "trenchbroom", "hammer", "doom-builder", "quark", "blender",
// "unity", "unreal" or "godot". Unknown profiles take "studio".
[[nodiscard]] QString levelSidebarFamilyForProfile(const QString& profileId);
[[nodiscard]] QStringList levelSidebarFamilies();
[[nodiscard]] LevelSidebarArrangement levelSidebarArrangementForProfile(const QString& profileId);
// What a tab is called under a profile, translated. `doomMap` names the
// entity browser for things when a Doom or Hexen map is open.
[[nodiscard]] QString levelSidebarTabTitle(const QString& tabId, const QString& profileId, bool doomMap = false);

// Puts every known tab on exactly one side once: unknown and repeated ids go,
// missing tabs join the side the profile gives them, and the current tabs
// fall back to the first of their side.
[[nodiscard]] LevelSidebarArrangement normalizedLevelSidebarArrangement(const LevelSidebarArrangement& arrangement, const QString& profileId);
// False, with the reason, when a tab is unknown, repeated or missing.
[[nodiscard]] bool validateLevelSidebarArrangement(const LevelSidebarArrangement& arrangement, QString* error = nullptr);

[[nodiscard]] QJsonObject levelSidebarArrangementJson(const LevelSidebarArrangement& arrangement, const QString& profileId);
// Reads what levelSidebarArrangementJson() wrote; the titles are ignored.
[[nodiscard]] bool levelSidebarArrangementFromJson(const QJsonObject& object, LevelSidebarArrangement* out, QString* error = nullptr);
[[nodiscard]] QStringList levelSidebarArrangementLines(const LevelSidebarArrangement& arrangement, const QString& profileId);

} // namespace vibestudio
