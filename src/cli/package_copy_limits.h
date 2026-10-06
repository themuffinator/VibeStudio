#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli {
struct PackageCopyLimitsCliResult {
	int exitCode = 0;
	QString error, summary;
	QJsonObject payload;
};
PackageCopyLimitsCliResult runPackageCopyLimits(const QStringList& arguments);
PackageCopyLimitsCliResult runPackageCopyStoreLimits(const QStringList& arguments);
} // namespace vibestudio::cli
