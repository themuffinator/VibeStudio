#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli
{
struct ModelMdlCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
ModelMdlCliResult runModelMdl(const QStringList &arguments);
} // namespace vibestudio::cli
