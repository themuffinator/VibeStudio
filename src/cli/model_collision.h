#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli
{
struct ModelCollisionCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
ModelCollisionCliResult runModelCollision(const QStringList &arguments);
} // namespace vibestudio::cli
