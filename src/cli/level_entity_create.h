#pragma once
#include <QJsonObject>
#include <QStringList>
namespace vibestudio::cli {
struct LevelEntityCreateCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// map create-entity: TrenchBroom's Create <class> through the Levels page's
// service (core/level_entity_create.h).
LevelEntityCreateCliResult runLevelEntityCreate(const QStringList& arguments);
} // namespace vibestudio::cli
