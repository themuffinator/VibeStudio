#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli
{
struct ModelAssemblyCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
ModelAssemblyCliResult runModelAssembly(const QStringList &arguments);
} // namespace vibestudio::cli
