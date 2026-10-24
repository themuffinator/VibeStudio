#pragma once
#include <QJsonObject>
#include <QStringList>
namespace vibestudio::cli {
struct LevelGridRoundCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// map round-to-grid: Radiant's Snap To Grid through the GUI's service.
LevelGridRoundCliResult runLevelGridRound(const QStringList& arguments);
} // namespace vibestudio::cli
