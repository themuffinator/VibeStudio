#pragma once
#include <QJsonObject>
#include <QStringList>
namespace vibestudio::cli {
struct LevelNudgeCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// map nudge: the Levels page's directional and depth nudges (core/
// level_nudge.h) through the same move and component transform services.
LevelNudgeCliResult runLevelNudge(const QStringList& arguments);
} // namespace vibestudio::cli
