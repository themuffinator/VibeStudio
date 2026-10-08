#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <array>

namespace vibestudio {
enum class LevelSceneNodeKind { Layer, Group };

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
	// Groups sharing a link id are linked copies of one another: an edit to
	// one copy's content is made to every copy (core/level_linked_groups.h).
	// Empty for a group that is not linked.
	QString linkId;
	// A linked copy's frame: quarter turns anticlockwise about the vertical,
	// after a mirror across x when set. Saved with the link.
	int linkTurn = 0;
	bool linkMirror = false;
	// A linked copy as of the last edit: the centre and size of its content's
	// bounds, and digests of where things are, of that with texture scales and
	// turns, and of its texture offsets. Worked out when the map loads and
	// kept up by every edit; never saved.
	std::array<double, 3> linkAnchor {0.0, 0.0, 0.0};
	std::array<double, 3> linkExtent {0.0, 0.0, 0.0};
	QByteArray linkGeometry;
	QByteArray linkShape;
	QByteArray linkSurface;
	bool operator==(const LevelSceneNode&) const = default;
};

struct LevelSceneState {
	QVector<LevelSceneNode> nodes;
	// Unknown/stale metadata is preserved, never interpreted as live membership.
	QByteArray opaqueMetadata;
	QString problem;
	bool operator==(const LevelSceneState&) const = default;
};
} // namespace vibestudio
