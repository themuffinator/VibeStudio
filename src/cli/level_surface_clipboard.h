#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli {
struct LevelSurfaceClipboardCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelSurfaceClipboardCliResult runLevelSurfaceClipboard(const QStringList& arguments);
} // namespace vibestudio::cli
