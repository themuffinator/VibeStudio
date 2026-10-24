#pragma once
#include "core/level_sweep.h"
#include <QStringList>
namespace vibestudio::cli
{
struct LevelSweepCliRequest {
	LevelSweepRequest sweep;
	QString path, output, engine, mapName;
	bool dryRun = false, overwrite = false;
};
bool parseLevelSweepArguments(const QStringList& arguments, LevelSweepCliRequest* request, QString* error);
} // namespace vibestudio::cli
