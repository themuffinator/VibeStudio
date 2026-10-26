#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli {
struct LevelSmartTagsCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelSmartTagsCliResult runLevelSmartTags(const QStringList& arguments);
} // namespace vibestudio::cli
