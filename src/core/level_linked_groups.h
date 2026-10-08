#pragma once

// Linked groups, after TrenchBroom's: scene groups that are copies of one
// another. An edit to the content of one copy is made to every copy, each
// keeping its own place and turn, while moving, turning or mirroring a whole
// copy changes only that copy.
//
// Copies share a link id on their scene nodes (LevelSceneNode::linkId), and
// each has a frame: quarter turns about the vertical and a mirror across x
// (linkTurn, linkMirror). Every map edit passes through the undo stack, and
// syncLevelLinkedGroups runs after each one there, so the studio and the CLI
// behave alike: it compares each copy with the digests kept on its node, makes
// the one copy that changed the content of the others, turned from its frame
// into theirs, and folds that into the edit's own undo step.
//
// Content is compared relative to the centre of a copy's points, which moves,
// turns and mirrors with it, so a copy moved as a whole is recognised and only
// its centre follows, and one turned or mirrored as a whole (in quarter turns)
// only changes its frame. Texture offsets are compared apart from shape,
// because texture lock changes them when a copy moves; texture scales and
// turns apart from where things are, so a whole copy turned without texture
// lock is still recognised. Turns other than quarter turns about the vertical
// reach every copy. When several copies changed in different ways at once,
// none is copied over the others; each keeps what it has until one of them is
// edited again or updated on purpose.
//
// Quake-family maps only. A linked group holds brushes, patches and entities,
// not other layers or groups. A locked copy is left as it is.

#include "core/level_map.h"

namespace vibestudio {

// What one copy holds, as compared and copied.
struct LevelLinkedContent {
	// What copying or deleting the copy acts on: its member entities with
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

[[nodiscard]] LevelLinkedContent levelLinkedContent(const LevelMapDocument& document, const QStringList& members);

// The bounds of a group's content: brushes, patches and entity origins. False
// for a group with none.
bool levelLinkedGroupBounds(const LevelMapDocument& document, const QStringList& members, LevelMapVec3* mins, LevelMapVec3* maxs);

// Where a new linked copy goes by default: beside the group along x, the
// group's width rounded up to whole 16-unit steps away.
[[nodiscard]] LevelMapVec3 levelLinkedCopyOffset(const LevelMapDocument& document, const QString& nodeId);

// Fills every linked node's anchor and digests from what it holds now.
void refreshLevelLinkBaselines(const LevelMapDocument& document, LevelSceneState* state);

// The groups linked with `nodeId`, itself included, in scene order; empty when
// it is not linked.
[[nodiscard]] QStringList levelLinkedGroupNodes(const LevelSceneState& state, const QString& nodeId);

// The linked groups that hold the selected objects, or the entities that own
// them, in scene order.
[[nodiscard]] QStringList levelLinkedGroupsOfSelection(const LevelMapDocument& document);

// Copies a group, its content moved by `offset`, as a sibling named after it,
// and links the copy with it and any copies it already has. One undo step.
bool createLinkedLevelGroup(LevelMapDocument* document, const QString& nodeId, const LevelMapVec3& offset, QString* createdId = nullptr,
	QString* error = nullptr);

// Makes every other unlocked copy of `nodeId` match it now, whether or not it
// changed: for copies that differ after edits made elsewhere. One undo step.
bool updateLinkedLevelGroups(LevelMapDocument* document, const QString& nodeId, int* updated = nullptr, QString* error = nullptr);

// Separates a copy from the others, keeping its content; when one copy would be
// left on its own it is unlinked too. One undo step.
bool unlinkLevelGroup(LevelMapDocument* document, const QString& nodeId, QString* error = nullptr);

// Runs after every edit on the undo stack (level_map.cpp); see above.
void syncLevelLinkedGroups(LevelMapDocument* document);

} // namespace vibestudio
