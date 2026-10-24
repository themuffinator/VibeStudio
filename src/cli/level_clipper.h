#pragma once
#include <QJsonObject>
#include <QStringList>
namespace vibestudio::cli {
struct LevelClipperCliResult { int exitCode = 0; QString error; QJsonObject payload; QStringList lines; };
LevelClipperCliResult runLevelClipper(const QStringList& arguments);
}
