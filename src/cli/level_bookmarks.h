#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli {
struct LevelBookmarksCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelBookmarksCliResult runLevelBookmarks(const QStringList& arguments);
} // namespace vibestudio::cli
