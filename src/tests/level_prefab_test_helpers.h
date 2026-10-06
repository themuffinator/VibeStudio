#pragma once
#include "core/level_document.h"
#include "core/level_patch.h"
#include "core/level_prefab.h"

namespace vibestudio::tests
{
inline QString prefabProperty(const LevelMapEntity &entity, const QString &key)
{
	for (const auto &p : entity.properties) {
		if (p.key == key) {
			return p.value;
		}
	}
	return {};
}
// Original fixture: a two-brush door, a curved panel, a relay, a model and a
// speaker. References intentionally include an external destination.
inline QByteArray prefabFixture()
{
	LevelMapDocument draft;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	createLevelMap(create, &draft);
	addLevelMapBoxBrush(&draft, {0, 0, 0, true}, {32, 64, 96, true}, QStringLiteral("studio/checker"));
	const auto first = levelMapSelectionText(draft);
	addLevelMapBoxBrush(&draft, {32, 0, 0, true}, {64, 64, 96, true}, QStringLiteral("studio/checker"));
	const auto second = levelMapSelectionText(draft);
	LevelPatchCreateRequest patchRequest;
	patchRequest.center = {32, 32, 100, true};
	patchRequest.texture = QStringLiteral("studio/checker");
	LevelMapPatch patch;
	createLevelPatch(patchRequest, &patch);
	addLevelMapPatch(&draft, patch);
	const auto panel = levelMapSelectionText(draft);
	return (QStringLiteral("{\n\"classname\" \"worldspawn\"\n\"message\" \"Do not export project settings\"\n") + panel +
			QStringLiteral("}\n{\n\"classname\" \"func_door\"\n\"targetname\" \"gate\"\n\"speed\" \"160\"\n") + first + second +
			QStringLiteral("}\n{\n\"classname\" \"target_relay\"\n\"origin\" \"16 16 32\"\n\"targetname\" \"relay\"\n\"target\" "
						   "\"gate\"\n\"killtarget\" \"outside\"\n}\n"
						   "{\n\"classname\" \"misc_model\"\n\"origin\" \"32 32 112\"\n\"model\" \"models/test.md3\"\n\"angle\" \"45\"\n}\n"
						   "{\n\"classname\" \"target_speaker\"\n\"origin\" \"48 32 64\"\n\"noise\" \"sound/test.wav\"\n}\n"))
		.toUtf8();
}
inline bool loadPrefabFixture(LevelMapDocument *map, QString *error = nullptr)
{
	if (!loadLevelMapBytes({QStringLiteral("assembly.map"), {}, QStringLiteral("idTech3")}, prefabFixture(), map, error)) {
		return false;
	}
	// Select only one door brush to exercise complete ownership promotion.
	return setLevelMapSelection(map,
								{{LevelMapSelectionKind::QuakeBrush, 0},
								 {LevelMapSelectionKind::QuakePatch, 0},
								 {LevelMapSelectionKind::Entity, 2},
								 {LevelMapSelectionKind::Entity, 3},
								 {LevelMapSelectionKind::Entity, 4}},
								error);
}
} // namespace vibestudio::tests
