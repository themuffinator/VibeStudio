#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli
{
struct ModelSurfacesCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
ModelSurfacesCliResult runModelSurfaces(const QStringList &arguments);
} // namespace vibestudio::cli
