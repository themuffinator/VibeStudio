#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli {
struct LevelCameraFocusCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelCameraFocusCliResult runLevelCameraFocus(const QStringList& arguments);
} // namespace vibestudio::cli
