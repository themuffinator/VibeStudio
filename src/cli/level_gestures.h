#pragma once
#include <QJsonObject>
#include <QStringList>
namespace vibestudio::cli {
struct LevelGesturesCliResult { int exitCode = 0; QString error; QJsonObject payload; QStringList lines; };
LevelGesturesCliResult runLevelGestures(const QStringList& arguments);
}
