#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli
{
struct ModelToolsCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// `model tool`: one edit-mode mesh tool through the shared document service.
ModelToolsCliResult runModelTool(const QStringList &arguments);
// `model select`: read-only selection operators that print component indices.
ModelToolsCliResult runModelSelect(const QStringList &arguments);
// `model lod`: Quake III detail levels written beside a base MD3.
ModelToolsCliResult runModelLod(const QStringList &arguments);
} // namespace vibestudio::cli
