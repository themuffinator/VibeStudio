#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli {
struct LevelFaceFlagsCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// map face-flags: reads, sets or clears the content and surface flags and
// value of Quake II and Quake III brush faces through the Levels Surfaces
// tab's service (core/level_face_flags.h).
LevelFaceFlagsCliResult runLevelFaceFlags(const QStringList& arguments);
} // namespace vibestudio::cli
