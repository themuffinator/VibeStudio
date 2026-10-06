#pragma once
#include <QJsonObject>
#include <QStringList>
namespace vibestudio::cli {
struct LevelSceneCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelSceneCliResult runLevelScene(const QStringList& arguments);
} // namespace vibestudio::cli
