#pragma once

#include "core/level_document.h"
#include "core/level_map.h"
#include "core/level_scene.h"

#include <QStringList>

namespace vibestudio::tests {

// The map the layer command checks start from, built through the editor's own
// services and saved as text:
//   Architecture (layer): world brushes w1 and w2, and a func_door holding d1
//     and d2; its Pillars group holds world brushes g1 and g2.
//   Lights (layer): a light.
//   Default: world brush floor, an info_player_start, and the root group
//     Crates holding world brush h1.
inline QByteArray sceneLayersFixtureBytes()
{
	LevelMapDocument document;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake");
	create.starterRoom = false;
	QString error;
	if (!createLevelMap(create, &document, &error)) {
		return {};
	}
	const QStringList names {QStringLiteral("floor"), QStringLiteral("w1"), QStringLiteral("w2"), QStringLiteral("g1"), QStringLiteral("g2"),
		QStringLiteral("h1"), QStringLiteral("d1"), QStringLiteral("d2")};
	for (int index = 0; index < names.size(); ++index) {
		const double x = index * 96.0;
		if (!addLevelMapBoxBrush(&document, {x, 0, 0, true}, {x + 64, 64, 64, true}, QStringLiteral("studio/") + names.at(index), nullptr, &error)) {
			return {};
		}
	}
	const auto brushNamed = [&document](const QString& name) {
		for (const LevelMapBrush& value : document.brushes) {
			if (!value.faces.isEmpty() && value.faces.first().textureName == QStringLiteral("studio/") + name) {
				return LevelMapSelectionRef {LevelMapSelectionKind::QuakeBrush, value.id};
			}
		}
		return LevelMapSelectionRef {};
	};
	setLevelMapSelection(&document, {brushNamed(QStringLiteral("d1")), brushNamed(QStringLiteral("d2"))});
	if (!tieLevelMapSelectionToEntity(&document, QStringLiteral("func_door"), {}, nullptr, &error)
		|| !addLevelMapEntity(&document, QStringLiteral("light"), {16, 16, 200, true}, {}, nullptr, &error)
		|| !addLevelMapEntity(&document, QStringLiteral("info_player_start"), {32, 32, 24, true}, {}, nullptr, &error)) {
		return {};
	}
	setLevelMapSelection(&document, {});
	const auto key = [](const LevelMapSelectionRef& ref) { return levelMapSelectionRefId(ref); };
	QString door, light;
	for (const LevelMapEntity& value : document.entities) {
		if (value.className == QStringLiteral("func_door")) {
			door = QStringLiteral("entity:%1").arg(value.id);
		} else if (value.className == QStringLiteral("light")) {
			light = QStringLiteral("entity:%1").arg(value.id);
		}
	}
	QString architecture, pillars, lights, crates;
	if (!createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Architecture"), {}, &architecture, &error)
		|| !createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("Pillars"), architecture, &pillars, &error)
		|| !createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Lights"), {}, &lights, &error)
		|| !createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("Crates"), {}, &crates, &error)
		|| !assignLevelSceneObjects(&document, architecture, {key(brushNamed(QStringLiteral("w1"))), key(brushNamed(QStringLiteral("w2"))), door}, &error)
		|| !assignLevelSceneObjects(&document, pillars, {key(brushNamed(QStringLiteral("g1"))), key(brushNamed(QStringLiteral("g2")))}, &error)
		|| !assignLevelSceneObjects(&document, lights, {light}, &error)
		|| !assignLevelSceneObjects(&document, crates, {key(brushNamed(QStringLiteral("h1")))}, &error)) {
		return {};
	}
	return serializeLevelMap(document).bytes;
}

inline int sceneLayersBrush(const LevelMapDocument& document, const QString& name)
{
	for (const LevelMapBrush& value : document.brushes) {
		if (!value.faces.isEmpty() && value.faces.first().textureName == QStringLiteral("studio/") + name) {
			return value.id;
		}
	}
	return -1;
}

inline int sceneLayersEntity(const LevelMapDocument& document, const QString& className)
{
	for (const LevelMapEntity& value : document.entities) {
		if (value.className == className) {
			return value.id;
		}
	}
	return -1;
}

inline QString sceneLayersNode(const LevelMapDocument& document, const QString& name)
{
	for (const LevelSceneNode& node : document.scene.nodes) {
		if (node.name == name) {
			return node.id;
		}
	}
	return {};
}

} // namespace vibestudio::tests
