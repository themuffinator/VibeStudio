#pragma once
#include <QJsonObject>
#include <QStringList>
namespace vibestudio::cli
{
struct ModelImportRepairCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
ModelImportRepairCliResult runModelImportRepair(const QStringList &arguments);
} // namespace vibestudio::cli
