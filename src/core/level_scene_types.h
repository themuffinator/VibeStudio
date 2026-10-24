#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <array>

namespace vibestudio {
enum class LevelSceneNodeKind { Layer, Group };

// How a Quake-family map keeps its layers, groups and linked groups:
// VibeStudio's own scene note at the end of the file, or TrenchBroom's
// func_group entities as well, so TrenchBroom opens it with the same groups
// (core/level_scene_trenchbroom.h).
enum class LevelSceneStorage { VibeStudio, TrenchBroom };

struct LevelSceneNode {
	QString id;
	QString name;
	// Empty names the implicit Default layer. Layers always have an empty parent.
	QString parentId;
	LevelSceneNodeKind kind = LevelSceneNodeKind::Group;
	bool visible = true;
	bool locked = false;
	// Canonical shared selectors (entity:3, brush:8, sector:2, ...). An object
	// occurs in one node only; unlisted objects belong to Default.
	QStringList objects;
	// Groups sharing a link id are linked groups, a link set: an edit to the
	// content of one is made to every other (core/level_linked_groups.h).
	// Empty for a group that is not linked.
	QString linkId;
	// Native selector -> persistent object UUID, scoped to linkId. Corresponding
	// objects in different linked groups share a UUID; implicit entity children
	// and the content of nested groups are included. Empty on legacy links until
	// correspondence is established by a real copy/update, never inferred from
	// object order or geometry.
	QMap<QString, QString> linkObjects;
	// A linked group's frame: the affine map that places the link set's shared
	// content where this group has it, moved, turned, mirrored, scaled or
	// sheared, row by row (3x4, translation last), as TrenchBroom's group
	// transformation. Saved with the link.
	std::array<double, 12> linkFrame {1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0};
	// True once the group stopped matching the rest of its link set: when
	// members changed in different ways at once, or an update passed it by
	// because it was locked. Any later update from a member clears it. Saved.
	bool linkOutOfSync = false;
	// A linked group as of the last edit: the centre of its content's bounds,
	// which the digests are measured from, and digests of where things are, of
	// that with texture scales and turns, and of its texture offsets. Worked
	// out when the map loads and kept up by every edit; never saved.
	std::array<double, 3> linkAnchor {0.0, 0.0, 0.0};
	QByteArray linkGeometry;
	QByteArray linkShape;
	QByteArray linkSurface;
	// Keys of the TrenchBroom layer or group entity this node was read from
	// that VibeStudio does not edit, such as "_tb_id", "_tb_layer_color" and
	// "_tb_layer_omit_from_export", written back when the map is saved with
	// TrenchBroom groups (core/level_scene_trenchbroom.h). Saved.
	QMap<QString, QString> trenchBroomProperties;
	bool operator==(const LevelSceneNode&) const = default;
};

struct LevelSceneState {
	QVector<LevelSceneNode> nodes;
	// Entity selector -> protected property keys. Independent of groups so
	// unlinking/regrouping preserves instance intent, including absent keys.
	// Keys use the native property editor's case-insensitive canonical spelling.
	QMap<QString, QStringList> protectedProperties;
	// Unknown/stale metadata is preserved, never interpreted as live membership.
	QByteArray opaqueMetadata;
	QString problem;
	bool operator==(const LevelSceneState&) const = default;
};

// Session context recorded by compound edits and undoable group navigation.
struct LevelSceneEditingContext {
	QStringList openGroups;
	QString activeNode, previousCreationNode;
	bool operator==(const LevelSceneEditingContext&) const = default;
};
} // namespace vibestudio
