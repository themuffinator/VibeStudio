#pragma once

#include "core/level_document.h"
#include "core/level_map.h"

#include <QHash>
#include <QStringList>

namespace vibestudio::tests {

// The map the entity ownership checks start from, built through the editor's
// own services: worldspawn with brushes studio/floor and studio/w1, a
// func_door ("speed" "100", "targetname" "door1") holding studio/d1, d2 and
// d3, a func_wall holding studio/c1, and a light. Saved text, so a reload
// gives every object its own source lines.
inline QByteArray entityOwnershipFixtureBytes(const QString& game = QStringLiteral("quake"))
{
	LevelMapDocument document;
	LevelMapCreateRequest create;
	create.game = game;
	create.starterRoom = false;
	QString error;
	if (!createLevelMap(create, &document, &error)) {
		return {};
	}
	const QStringList names {QStringLiteral("floor"), QStringLiteral("w1"), QStringLiteral("d1"), QStringLiteral("d2"), QStringLiteral("d3"), QStringLiteral("c1")};
	QHash<QString, int> ids;
	for (int index = 0; index < names.size(); ++index) {
		int id = -1;
		const double x = index * 96.0;
		if (!addLevelMapBoxBrush(&document, {x, 0, 0, true}, {x + 64, 64, 64, true}, QStringLiteral("studio/") + names.at(index), &id, &error)) {
			return {};
		}
		ids.insert(names.at(index), id);
	}
	const auto brush = [&ids](const char* name) { return LevelMapSelectionRef {LevelMapSelectionKind::QuakeBrush, ids.value(QLatin1String(name))}; };
	setLevelMapSelection(&document, {brush("d1"), brush("d2"), brush("d3")});
	if (!tieLevelMapSelectionToEntity(&document, QStringLiteral("func_door"),
			{{QStringLiteral("speed"), QStringLiteral("100"), 0}, {QStringLiteral("targetname"), QStringLiteral("door1"), 0}}, nullptr, &error)) {
		return {};
	}
	// Tying gives the brushes new ids; find c1 again by its texture.
	for (const LevelMapBrush& value : document.brushes) {
		if (value.faces.first().textureName == QStringLiteral("studio/c1")) {
			setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, value.id}});
		}
	}
	if (!tieLevelMapSelectionToEntity(&document, QStringLiteral("func_wall"), {}, nullptr, &error)
		|| !addLevelMapEntity(&document, QStringLiteral("light"), {16, 16, 200, true}, {}, nullptr, &error)) {
		return {};
	}
	return serializeLevelMap(document).bytes;
}

inline int entityOwnershipBrush(const LevelMapDocument& document, const QString& name)
{
	for (const LevelMapBrush& value : document.brushes) {
		if (!value.faces.isEmpty() && value.faces.first().textureName == QStringLiteral("studio/") + name) {
			return value.id;
		}
	}
	return -1;
}

inline int entityOwnershipEntity(const LevelMapDocument& document, const QString& className)
{
	for (const LevelMapEntity& value : document.entities) {
		if (value.className == className) {
			return value.id;
		}
	}
	return -1;
}

} // namespace vibestudio::tests
