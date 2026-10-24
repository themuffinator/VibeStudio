#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli {
struct LevelViewFiltersCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// `map filters`: the Levels view filters a map's format offers, how many
// objects each names, and what a switched-off list (the View tab's saved one
// with --saved, or --off) would hide, optionally after Invert Filters. A query:
// no map or setting is written.
LevelViewFiltersCliResult runLevelViewFilters(const QStringList& arguments);
} // namespace vibestudio::cli
