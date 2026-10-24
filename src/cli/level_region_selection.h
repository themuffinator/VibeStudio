#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli {
struct LevelRegionSelectionCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelRegionSelectionCliResult runLevelRegionSelection(const QStringList& arguments);
} // namespace vibestudio::cli
