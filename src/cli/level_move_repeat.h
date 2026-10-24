#pragma once
#include <QJsonObject>
#include <QStringList>
namespace vibestudio::cli {
struct LevelMoveRepeatCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// map move --camera: the Levels page's Move to Camera (core/level_paste.h).
LevelMoveRepeatCliResult runLevelMoveToCamera(const QStringList& arguments);
// map repeat-transforms: NetRadiant Custom's Repeat Transforms
// (core/level_repeat.h) with the transforms named on the command line.
LevelMoveRepeatCliResult runLevelRepeatTransforms(const QStringList& arguments);
} // namespace vibestudio::cli
