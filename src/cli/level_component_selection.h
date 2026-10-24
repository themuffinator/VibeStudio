#pragma once
#include <QJsonObject>
#include <QStringList>
namespace vibestudio::cli {
struct LevelComponentSelectionCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelComponentSelectionCliResult runLevelComponentSelection(const QStringList& arguments);
}
