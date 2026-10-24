#pragma once
#include "tests/level_prefab_test_helpers.h"

namespace vibestudio::tests {
// Original fixture: the door owns a patch followed by two brushes. The world
// retains its own patch so tests distinguish copied children from siblings.
inline QByteArray objectClipboardFixture()
{
	LevelMapDocument source;
	loadLevelMapBytes({"objects.map", {}, "idtech3"}, prefabFixture(), &source);
	setLevelMapSelection(&source, {{LevelMapSelectionKind::QuakePatch, 0}});
	auto bytes = prefabFixture();
	bytes.replace("\"speed\" \"160\"\n", "\"speed\" \"160\"\n" + levelMapSelectionText(source).toUtf8());
	return bytes;
}
} // namespace vibestudio::tests
