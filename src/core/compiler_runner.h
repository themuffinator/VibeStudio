#pragma once

#include "core/compiler_profiles.h"

#include <functional>

namespace vibestudio {

struct CompilerRunRequest {
	CompilerCommandRequest command;
	QString manifestPath;
	bool dryRun = false;
	bool registerOutputs = false;
	int timeoutMs = 5 * 60 * 1000;
};

struct CompilerRunCallbacks {
	std::function<bool()> cancellationRequested;
	std::function<void(const CompilerTaskLogEntry&)> logEntry;
};

struct CompilerRunResult {
	CompilerCommandPlan plan;
	CompilerCommandManifest manifest;
	OperationState state = OperationState::Idle;
	bool started = false;
	bool timedOut = false;
	bool cancelled = false;
	int exitCode = -1;
	qint64 durationMs = -1;
	QString stdoutText;
	QString stderrText;
	QVector<CompilerDiagnostic> diagnostics;
	QStringList registeredOutputPaths;
	QString manifestPath;
	QString error;
};

CompilerRunResult runCompilerCommand(const CompilerRunRequest& request, const CompilerRunCallbacks& callbacks = {});
CompilerRunResult rerunCompilerCommandManifest(const CompilerCommandManifest& manifest, const CompilerRunCallbacks& callbacks = {}, const QString& manifestPath = QString());
QString compilerRunResultText(const CompilerRunResult& result);

} // namespace vibestudio
