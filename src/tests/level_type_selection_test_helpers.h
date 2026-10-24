#pragma once
#include "tests/level_entity_selection_test_helpers.h"
namespace vibestudio::tests {
inline QByteArray typeSelectionFixture()
{
	auto source = entitySelectionFixture(); LevelMapDocument map;
	loadLevelMapBytes({"types.map",{},{}},source,&map);
	auto lines = QString::fromUtf8(source).split('\n');
	lines[map.brushes.first().faces.first().line-1].replace("studio/checker","metal");
	return (lines.join('\n')+QStringLiteral(
		"{\n\"classname\" \"func_door\"\n}\n"
		"{\n\"CLASSNAME\" \"FUNC_DOOR\"\n}\n"
		"{\n\"origin\" \"64 64 64\"\n}\n"
		"{\n\"classname\" \"\"\n}\n"
		"{\n\"classname\" \"FUNC_DOOR\"\n}\n")).toUtf8();
}
inline QVector<LevelMapSelectionRef> doorTypeSelection()
{
	return {{LevelMapSelectionKind::Entity,1},{LevelMapSelectionKind::QuakePatch,1},
		{LevelMapSelectionKind::QuakeBrush,0},{LevelMapSelectionKind::QuakeBrush,1},{LevelMapSelectionKind::Entity,10}};
}
inline QVector<LevelMapSelectionRef> materialTypeSelection()
{
	return {{LevelMapSelectionKind::QuakeBrush,0},{LevelMapSelectionKind::QuakeBrush,1},
		{LevelMapSelectionKind::QuakePatch,0},{LevelMapSelectionKind::QuakePatch,1},{LevelMapSelectionKind::QuakePatch,2}};
}
}
