#pragma once
#include "tests/level_prefab_test_helpers.h"
namespace vibestudio::tests {
inline QByteArray entitySelectionFixture()
{
	LevelMapDocument map; loadPrefabFixture(&map);
	const auto panel = levelPatchDefinition(map.patches.first()).join('\n')+QLatin1Char('\n');
	auto source = QString::fromUtf8(prefabFixture());
	source.replace("\"speed\" \"160\"\n","\"speed\" \"160\"\n\"target\" \"beyond\"\n"+panel);
	source.replace("\"killtarget\" \"outside\"\n","\"killtarget\" \"outside\"\n"+panel);
	source.replace("\"message\" \"Do not export project settings\"","\"target\" \"relay\"");
	source.replace("\"classname\" \"misc_model\"\n\"origin\" \"32 32 112\"\n\"model\" \"models/test.md3\"","\"classname\" \"info_null\"\n\"target\" \"relay\"");
	source.replace("\"noise\" \"sound/test.wav\"","\"targetname\" \"beyond\"");
	source += "{\n\"classname\" \"light\"\n\"origin\" \"80 80 80\"\n\"targetname\" \"outside\"\n}\n"
		"{\n\"classname\" \"light\"\n\"origin\" \"96 80 80\"\n\"targetname\" \"gate\"\n}\n"
		"{\n\"classname\" \"info_null\"\n\"TARGET\" \"relay\"\n\"TARGETNAME\" \"gate\"\n}\n"
		"{\n\"classname\" \"light\"\n\"targetname\" \"Gate\"\n}\n"
		"{\n\"classname\" \"light\"\n\"targetname\" \" gate \"\n}\n";
	return source.toUtf8();
}
inline QVector<LevelMapSelectionRef> connectedEntityFixtureResult()
{
	return {{LevelMapSelectionKind::QuakePatch,2},{LevelMapSelectionKind::Entity,3},{LevelMapSelectionKind::Entity,1},
		{LevelMapSelectionKind::QuakePatch,1},{LevelMapSelectionKind::QuakeBrush,0},{LevelMapSelectionKind::QuakeBrush,1},{LevelMapSelectionKind::Entity,6}};
}
}
