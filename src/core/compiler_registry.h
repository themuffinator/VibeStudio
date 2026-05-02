#pragma once

#include "core/operation_state.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct CompilerToolDescriptor {
	QString id;
	QString integrationId;
	QString displayName;
	QString engineFamily;
	QString role;
	QString sourcePath;
	QStringList executableNames;
	QStringList candidateRelativePaths;
	QStringList versionProbeArguments;
	QStringList capabilityFlags;
	QStringList readinessWarnings;
};

struct CompilerToolPathOverride {
	QString toolId;
	QString executablePath;
};

struct CompilerRegistryOptions {
	QString workspaceRootPath;
	QStringList extraSearchPaths;
	QVector<CompilerToolPathOverride> executableOverrides;
	bool probeVersions = true;
	int versionProbeTimeoutMs = 1500;
};

struct CompilerToolDiscovery {
	CompilerToolDescriptor descriptor;
	bool sourceAvailable = false;
	bool executableAvailable = false;
	bool executablePathOverridden = false;
	bool versionProbeAttempted = false;
	bool versionAvailable = false;
	QString sourcePath;
	QString executablePath;
	QString versionText;
	QString versionProbeCommandLine;
	QStringList capabilityFlags;
	QStringList warnings;

	[[nodiscard]] OperationState state() const;
};

struct CompilerRegistrySummary {
	QVector<CompilerToolDiscovery> tools;
	int sourceAvailableCount = 0;
	int executableAvailableCount = 0;
	int warningCount = 0;

	[[nodiscard]] OperationState overallState() const;
};

QVector<CompilerToolDescriptor> compilerToolDescriptors();
bool compilerToolDescriptorForId(const QString& id, CompilerToolDescriptor* out = nullptr);
CompilerRegistrySummary discoverCompilerTools(const QString& workspaceRootPath = QString(), const QStringList& extraSearchPaths = {});
CompilerRegistrySummary discoverCompilerTools(const CompilerRegistryOptions& options);
QString compilerRegistrySummaryText(const CompilerRegistrySummary& summary);

} // namespace vibestudio
