#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

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
