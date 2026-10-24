#pragma once

// Display filters for the level views: kinds of objects the views can stop
// drawing without changing the map, the way Radiant's Filter menu and
// TrenchBroom's view options hide clip brushes, triggers or lights.
//
// A filter names objects; the views hide what any switched-off filter names,
// on top of what the user hid by hand and what hidden scene layers hold. The
// Levels View tab, the View > Filters commands and their profile keys switch
// them; `map filters` reports the same matches.
//
// World Brushes and Entities split the map as Radiant's World and Entities
// filters do: worldspawn, func_group and func_detail count as world. Point
// Entities leaves lights and models to their own filters, and Brushes names
// every brush whatever its owner, as TrenchBroom's Show Brushes does. An
// entity is a brush entity when it owns brushes or patches or the built-in
// catalogues define its class as made of brushes, so a door with no brushes
// yet is never a point entity.
// Workflow reference: NRC filters.cpp and plugins/entity/entity.cpp at 449778b3
// (GPL-2.0-or-later) / TB ActionManager "View Filter > Toggle show brushes" at
// 4c756be1 (GPL-3.0-or-later). Original Qt implementation; see docs/CREDITS.md.

#include "core/level_map.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct LevelViewFilter {
	QString id;
	QString title;
	QString description;
};

// Every Quake-family kind in menu order, whatever the open map's format; the
// shell gives each one a checkable command. Doom thing categories come from
// the open map's thing table, so they have none.
[[nodiscard]] QVector<LevelViewFilter> levelViewFilterKinds();
// The command that shows or hides one kind: map.filterWorld for "world",
// map.filterBrushEntities for "brush-entities"; empty for Doom categories.
[[nodiscard]] QString levelViewFilterCommandId(const QString& filterId);
// Whether a class counts as world for World Brushes and Entities, as
// Radiant counts it: worldspawn, func_group and func_detail*.
[[nodiscard]] bool levelViewFilterWorldClass(const QString& className);

// The filters that apply to a map's format, in menu order. Quake-family maps
// filter brush and entity kinds; Doom maps filter things by category.
[[nodiscard]] QVector<LevelViewFilter> levelViewFilters(const LevelMapDocument& document);
[[nodiscard]] bool levelViewFilterForId(const LevelMapDocument& document, const QString& id, LevelViewFilter* out = nullptr);

// What one filter names: whole entities (their brushes and patches go with
// them), brushes, patches, and Doom things.
[[nodiscard]] QVector<LevelMapSelectionRef> levelViewFilterMatches(const LevelMapDocument& document, const QString& filterId);
// How many objects each filter names, keyed by filter id.
[[nodiscard]] QHash<QString, int> levelViewFilterCounts(const LevelMapDocument& document);
// Everything the switched-off filters name, without repeats.
[[nodiscard]] QVector<LevelMapSelectionRef> levelViewFilteredObjects(const LevelMapDocument& document, const QStringList& hiddenFilterIds);

// The switched-off list after Invert Filters: every kind the document's format
// offers flips, as Radiant's Invert filters toggles each filter in turn. Ids
// the format does not offer, kept for maps of other formats, stay as they are.
[[nodiscard]] QStringList levelViewFiltersInverted(const LevelMapDocument& document, const QStringList& hiddenFilterIds);
// The switched-off kinds the studio settings keep for the View tab, under
// this shell layout key, as a comma-separated list.
[[nodiscard]] QString levelViewFiltersSettingsKey();
[[nodiscard]] QStringList levelViewFiltersFromSaved(const QByteArray& saved);
[[nodiscard]] QByteArray levelViewFiltersSavedValue(const QStringList& hiddenFilterIds);

} // namespace vibestudio
