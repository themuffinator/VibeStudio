#pragma once

#include "core/operation_state.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

// How much confidence VibeStudio has in the version text it recorded for a tool.
enum class CompilerVersionProbeOutcome {
	// The probe was not run (disabled, no executable, or unsafe to launch).
	NotAttempted,
	// A recognisable version banner was captured.
	Probed,
	// The tool produced output but no recognisable version banner; the first useful line was kept.
	Inferred,
	// The probe could not start, timed out, crashed, or produced nothing usable.
	Failed,
};

struct CompilerToolDescriptor {
	QString id;
	QString integrationId;
	QString displayName;
	QString engineFamily;
	QString role;
	QString sourcePath;
	QStringList executableNames;
	QStringList candidateRelativePaths;
	// Arguments the probe passes. An empty list still runs the tool with no arguments when
	// versionProbeSupported is true: VibeMap2 (like ericw-tools) has no --version and treats an unknown option as a
	// parse error, while running with no arguments prints the banner and usage.
	QStringList versionProbeArguments;
	QStringList capabilityFlags;
	QStringList readinessWarnings;
	// False for tools that must never be launched during discovery (GUI helpers).
	bool versionProbeSupported = true;
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
	// Time allowed for the process to appear; a missing or broken executable fails fast.
	int versionProbeStartTimeoutMs = 750;
	// Time allowed for the probe to finish printing after it started.
	int versionProbeTimeoutMs = 1500;
};

struct CompilerToolDiscovery {
	CompilerToolDescriptor descriptor;
	bool sourceAvailable = false;
	bool executableAvailable = false;
	bool executablePathOverridden = false;
	bool versionProbeAttempted = false;
	bool versionAvailable = false;
	CompilerVersionProbeOutcome versionProbeOutcome = CompilerVersionProbeOutcome::NotAttempted;
	int versionProbeExitCode = -1;
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

QString compilerVersionProbeOutcomeId(CompilerVersionProbeOutcome outcome);
QString compilerVersionProbeOutcomeText(CompilerVersionProbeOutcome outcome);

QVector<CompilerToolDescriptor> compilerToolDescriptors();
bool compilerToolDescriptorForId(const QString& id, CompilerToolDescriptor* out = nullptr);
// The current tool or profile id for one retired by the move from ericw-tools and q3map2 to
// VibeMap2 and VibeMap3 (for example "ericw-qbsp" -> "vibemap2-bsp"), or an empty string.
// Retired ids are never resolved silently; callers use this to explain the rename.
QString renamedCompilerId(const QString& retiredId);
// "Unknown compiler tool id: <id>", plus the rename when the id was retired.
QString unknownCompilerToolIdText(const QString& id);
CompilerRegistrySummary discoverCompilerTools(const QString& workspaceRootPath = QString(), const QStringList& extraSearchPaths = {});
CompilerRegistrySummary discoverCompilerTools(const CompilerRegistryOptions& options);
QString compilerRegistrySummaryText(const CompilerRegistrySummary& summary);

} // namespace vibestudio
