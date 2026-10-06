#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli {
struct LevelBuildCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
LevelBuildCliResult runLevelBuildWorkspaceCommand(const QStringList& arguments);
} // namespace vibestudio::cli
