#pragma once

#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli
{
struct ModelAppearanceCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject data;
	QString text;
};
ModelAppearanceCliResult runModelAppearance(const QStringList &arguments);
} // namespace vibestudio::cli
