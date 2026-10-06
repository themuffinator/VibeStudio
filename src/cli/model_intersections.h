#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli
{
struct ModelIntersectionsCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
ModelIntersectionsCliResult runModelIntersections(const QStringList &arguments);
} // namespace vibestudio::cli
