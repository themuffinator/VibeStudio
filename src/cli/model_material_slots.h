#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli
{
struct ModelMaterialSlotsCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
ModelMaterialSlotsCliResult runModelMaterialSlots(const QStringList &arguments);
} // namespace vibestudio::cli
