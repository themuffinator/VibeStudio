#pragma once

// Linked groups, after TrenchBroom's: groups that share their content. Create
// Linked Duplicate copies a group and links the copy with it; together the
// groups sharing a link id form a link set. None of them is the original: an
// edit to the content of any one is made to every other, each keeping its own
// place and frame, while moving, turning, mirroring or scaling a whole closed
// group changes only that group.
//
// Every map edit passes through the undo stack, and syncLevelLinkedGroups runs
// after each one there, so the studio and the CLI behave alike. It sorts each
// linked group into unchanged, transformed whole or changed:
//  - a closed group (not on the open editing path) that the edit's one affine
//    map moved entirely (LevelMapUndoCommand::hasAffine) is transformed whole:
//    its frame takes the map's linear part, and nothing is copied;
//  - a closed group whose content only moved is transformed whole too, for
//    edits that record no map;
//  - anything else that changed its content, including any edit inside an
//    open group, is a change to the shared content.
// The one changed group's content then replaces the content of the others,
// taken out of its frame and into theirs, inside the edit's own undo step.
// Changes are compared relative to the content's bounds; each group's frame
// (LevelSceneNode::linkFrame) says exactly where its content goes.
//
// Groups nest: a group inside a linked group is copied with it, and its copies
// are linked with it, as TrenchBroom links nested groups. Outer link sets are
// updated before the sets nested in them; a group an outer update copied
// already matches what it copies, and is updated whenever that group is. When
// members of one set changed in different ways at once (keys a member protects
// aside), nothing is copied over them: the set is marked out of sync and the
// edit says so, until a member is edited again or Update Linked Groups copies
// one over the rest. A locked group is left as it is and marked out of sync,
// and so is the copy an outer update made of it; what it holds is copied all
// the same.
//
// Quake-family maps only.

#include "core/level_map.h"

#include <array>

namespace vibestudio {

// What one group holds, as compared and copied.
struct LevelLinkedContent {
	// What copying or deleting the group acts on: its member entities with
	// their brushes and patches, and its brushes and patches not owned by one.
	QVector<LevelMapSelectionRef> roots;
	bool hasPoints = false;
	// The bounds of the content's points, origins and control points, and
	// their centre, which every digest is relative to.
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	LevelMapVec3 anchor;
	// Digests: where things are and what they are; that with texture scales,
	// turns and axes; and the texture offsets.
	QByteArray geometry;
	QByteArray shape;
	QByteArray surface;
};

// The objects of a group and of every group nested in it, and those nested
// groups, in scene order.
[[nodiscard]] QStringList levelLinkedGroupMembers(const LevelSceneState& state, const QString& nodeId);
[[nodiscard]] QStringList levelLinkedGroupDescendants(const LevelSceneState& state, const QString& nodeId);

// Content made of `members` alone, and a scene group's whole content with the
// groups nested in it.
[[nodiscard]] LevelLinkedContent levelLinkedContent(const LevelMapDocument& document, const QStringList& members);
[[nodiscard]] LevelLinkedContent levelLinkedGroupContent(const LevelMapDocument& document, const QString& nodeId);
[[nodiscard]] LevelLinkedContent levelLinkedGroupContent(const LevelMapDocument& document, const LevelSceneState& state, const QString& nodeId);

// The bounds of a group's content: brushes, patches and entity origins. False
// for a group with none.
bool levelLinkedGroupBounds(const LevelMapDocument& document, const QStringList& members, LevelMapVec3* mins, LevelMapVec3* maxs);

// Where Create Linked Duplicate puts the new group: beside the group along x,
// the group's width rounded up to whole 16-unit steps away.
[[nodiscard]] LevelMapVec3 levelLinkedDuplicateOffset(const LevelMapDocument& document, const QString& nodeId);

// Fills every linked node's baselines from what it holds now, or only those
// of `nodes`.
void refreshLevelLinkBaselines(const LevelMapDocument& document, LevelSceneState* state);
void refreshLevelLinkBaselines(const LevelMapDocument& document, LevelSceneState* state, const QSet<QString>& nodes);

// True when linked groups `a` and `b` hold the same content, each in its own
// frame: every object has a counterpart by identity that agrees with it in
// what it is and, within a hundredth of a unit, where it is. Texture alignment
// is not compared.
[[nodiscard]] bool levelLinkedGroupsAgree(const LevelMapDocument& document, const QString& a, const QString& b);

// The groups linked with `nodeId`, itself included, in scene order; empty when
// it is not linked.
[[nodiscard]] QStringList levelLinkedGroupNodes(const LevelSceneState& state, const QString& nodeId);

// The closed groups the selection holds whole, at the open editing level, in
// scene order: what Create Linked Duplicate and Separate Linked Groups act on.
[[nodiscard]] QStringList levelSelectedSceneGroups(const LevelMapDocument& document);
// The linked groups the selection reaches: the closed groups it touches, else
// the innermost open linked group holding a selected object.
[[nodiscard]] QStringList levelLinkedGroupsOfSelection(const LevelMapDocument& document);
// Every member of the link sets of `groups`, as objects to select.
[[nodiscard]] QVector<LevelMapSelectionRef> levelLinkedGroupsSelection(const LevelMapDocument& document, const QStringList& groups);

// Create Linked Duplicate: copies a group, its nested groups and content moved
// by `offset`, as a sibling named after it, linked with it and any group it is
// already linked with; groups nested in it are linked with their copies. The
// new group is selected. One undo step.
bool createLinkedLevelGroup(LevelMapDocument* document, const QString& nodeId, const LevelMapVec3& offset, QString* createdId = nullptr,
	QString* error = nullptr);

// Update Linked Groups: makes every other unlocked member of `nodeId`'s link
// set match it now, whether or not it changed, for groups out of sync. One
// undo step.
bool updateLinkedLevelGroups(LevelMapDocument* document, const QString& nodeId, int* updated = nullptr, QString* error = nullptr);

// Separate Linked Groups: the chosen members of each link set leave it,
// keeping their content. Several chosen from one set stay linked with each
// other as a new set; one alone is no longer linked, nor is a member left on
// its own. One undo step.
bool separateLinkedLevelGroups(LevelMapDocument* document, const QStringList& nodeIds, int* separated = nullptr, QString* error = nullptr);
bool unlinkLevelGroup(LevelMapDocument* document, const QString& nodeId, QString* error = nullptr);

// A linked group's frame in words ("Turned 90 degrees", "Mirrored", ...);
// empty when it only moves the content.
[[nodiscard]] QString levelLinkFrameText(const std::array<double, 12>& frame);
// The frame `frame` takes when its group's content moves by an affine map.
[[nodiscard]] std::array<double, 12> levelLinkFrameAfter(const std::array<double, 12>& map, const std::array<double, 12>& frame);

// Runs after every edit on the undo stack (level_map.cpp); see above.
void syncLevelLinkedGroups(LevelMapDocument* document);
// Only automatic follow-up edits may cross an open group's editing boundary.
bool levelLinkedGroupPropagationActive(const LevelMapDocument* document);

} // namespace vibestudio
