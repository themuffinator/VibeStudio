#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli {
struct LevelClipboardCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelClipboardCliResult runLevelClipboard(const QStringList& arguments);
} // namespace vibestudio::cli
