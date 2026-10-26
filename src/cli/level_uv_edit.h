#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli {
struct LevelUvEditCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// map edit-uv: lists one brush face's winding or one patch's control points
// in texture space, or moves a point, offsets or turns them, through the UV
// Editor's service (core/level_uv_edit.h).
LevelUvEditCliResult runLevelUvEdit(const QStringList& arguments);
} // namespace vibestudio::cli
