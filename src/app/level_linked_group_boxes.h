#pragma once

// Linked groups as the Levels views draw them, after TrenchBroom: a box
// around each linked group, the groups in use (open, or holding the
// selection whole), the link sets they belong to, and what a view says about
// them. The 2D plan views (app/map_viewport.cpp) and the 3D camera
// (app/model_viewport_linked_groups.cpp) share these, so both draw and
// describe the same boxes in the same way.

#include "core/level_map.h"
#include "core/map_geometry.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

// One linked group's box, in map units.
struct LevelLinkedGroupBox {
	QString id;
	QString link;
	QString name;
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	bool outOfSync = false;
};

bool operator==(const LevelLinkedGroupBox& left, const LevelLinkedGroupBox& right);

// Each linked group's box: the brushes, patches and entity origins of the
// group and of the groups nested in it, found in one pass over the scene, with
// brushes as `brushes` solved them. A group with nothing solved has no box.
// Empty for Doom maps and for maps without linked groups, which cost one scan
// of the scene nodes.
[[nodiscard]] QVector<LevelLinkedGroupBox> levelLinkedGroupBoxes(const LevelMapDocument& document,
	const QVector<MapBrushGeometry>& brushes);

// The groups in use: open in `openGroups`, or holding part of `selection`
// (a closed group is only ever selected whole).
[[nodiscard]] QSet<QString> levelLinkedGroupsInUse(const QVector<LevelLinkedGroupBox>& boxes, const QStringList& openGroups,
	const QHash<QString, QVector<LevelMapSelectionRef>>& groupSelections, const QVector<LevelMapSelectionRef>& selection);

// The link sets the groups in use belong to. Their groups are named, and
// arrows run to them from the groups in use.
[[nodiscard]] QSet<QString> levelLinkedSetsInUse(const QVector<LevelLinkedGroupBox>& boxes, const QSet<QString>& inUse);

// How a box is outlined without relying on colour: solid for a group in use,
// dashed for the others, dash-dotted while out of sync.
[[nodiscard]] Qt::PenStyle levelLinkedGroupPenStyle(const LevelLinkedGroupBox& box, bool inUse);

// The name drawn beside a box, saying when the group is out of sync.
[[nodiscard]] QString levelLinkedGroupLabel(const LevelLinkedGroupBox& box);

// What a view's accessible description says of them: the arrows from the
// selected group to the others of its set and how many are out of sync, and,
// with `showAll` (Show > Linked Groups), that every linked group is boxed.
// Empty when the view draws none.
[[nodiscard]] QString levelLinkedGroupsDescription(const QVector<LevelLinkedGroupBox>& boxes, const QSet<QString>& inUse,
	bool showAll);

// Everything a view needs to draw the linked groups, for a view that does not
// track the selection itself (the 3D camera).
struct LevelLinkedGroupDisplay {
	QVector<LevelLinkedGroupBox> boxes;
	QSet<QString> inUse;
	// The open groups in use, whose boxes stay put while the selection inside
	// them is dragged; a closed one moves with it.
	QSet<QString> open;
	bool showAll = false;

	// Whether a view draws anything at all.
	[[nodiscard]] bool drawsAnything() const;
};

bool operator==(const LevelLinkedGroupDisplay& left, const LevelLinkedGroupDisplay& right);

[[nodiscard]] LevelLinkedGroupDisplay levelLinkedGroupDisplay(const QVector<LevelLinkedGroupBox>& boxes, const QStringList& openGroups,
	const QHash<QString, QVector<LevelMapSelectionRef>>& groupSelections, const QVector<LevelMapSelectionRef>& selection, bool showAll);

} // namespace vibestudio
