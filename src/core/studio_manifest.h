#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct StudioModule {
	QString id;
	QString name;
	QString category;
	QString maturity;
	QString description;
	QStringList engines;
};

struct CompilerIntegration {
	QString id;
	QString displayName;
	QString engines;
	QString role;
	QString sourcePath;
	QString upstreamUrl;
	QString pinnedRevision;
	QString license;
};

QVector<StudioModule> plannedModules();
QVector<CompilerIntegration> compilerIntegrations();
QString versionString();

} // namespace vibestudio
