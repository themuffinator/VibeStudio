#pragma once

#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli {

// `release plan|publish|notes|changelog|history|catalog` and
// `install register build|info|check|export`. The router prints the result:
// JSON payload keys merge into the command's JSON object, and lines form the
// text output.
struct ReleaseCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};

ReleaseCliResult runReleaseCommand(const QStringList& arguments);
ReleaseCliResult runInstallRegisterCommand(const QStringList& arguments);

} // namespace vibestudio::cli
