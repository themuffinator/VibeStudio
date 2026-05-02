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

struct AboutDocument {
	QString id;
	QString title;
	QString path;
	QString description;
};

QVector<StudioModule> plannedModules();
QVector<CompilerIntegration> compilerIntegrations();
QVector<AboutDocument> aboutDocuments();
QString versionString();
QString githubRepository();
QString updateChannel();
QString projectLicenseSummary();
QString aboutSurfaceText();

} // namespace vibestudio
