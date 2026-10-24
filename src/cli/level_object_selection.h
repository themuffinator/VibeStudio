#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli {
struct LevelObjectSelectionCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelObjectSelectionCliResult runLevelObjectSelection(const QStringList& arguments);
} // namespace vibestudio::cli
