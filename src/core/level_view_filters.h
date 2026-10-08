#pragma once

// Display filters for the level views: kinds of objects the views can stop
// drawing without changing the map, the way Radiant's Filter menu and
// TrenchBroom's view options hide clip brushes, triggers or lights.
//
// A filter names objects; the views hide what any switched-off filter names,
// on top of what the user hid by hand and what hidden scene layers hold. The
// Levels View tab switches them; `map filters` reports the same matches.

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

} // namespace vibestudio
