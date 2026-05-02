#pragma once

#include "core/compiler_registry.h"
#include "core/operation_state.h"

#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct CompilerProfileDescriptor {
	QString id;
	QString toolId;
	QString displayName;
	QString engineFamily;
	QString stageId;
	QString inputDescription;
	QStringList inputExtensions;
	QString defaultOutputExtension;
	QString description;
	QStringList defaultArguments;
	bool inputRequired = true;
	bool outputPathArgumentSupported = false;
};

struct CompilerCommandRequest {
	QString profileId;
	QString inputPath;
	QString outputPath;
	QString workingDirectory;
	QString workspaceRootPath;
	QStringList extraArguments;
	QStringList extraSearchPaths;
	QVector<CompilerToolPathOverride> executableOverrides;
};

struct CompilerCommandPlan {
	CompilerProfileDescriptor profile;
	CompilerToolDiscovery tool;
	bool profileFound = false;
	bool toolFound = false;
	bool executableAvailable = false;
	QString program;
	QStringList arguments;
	QString workingDirectory;
	QString inputPath;
	QString expectedOutputPath;
	QString commandLine;
	QStringList warnings;
	QStringList errors;

	[[nodiscard]] bool isRunnable() const;
	[[nodiscard]] OperationState state() const;
};

struct CompilerTaskLogEntry {
	QDateTime timestampUtc;
	QString level;
	QString message;
};

struct CompilerFileHash {
	QString path;
	bool exists = false;
	qint64 sizeBytes = 0;
	QString sha256;
};

struct CompilerDiagnostic {
	QString level;
	QString message;
	QString filePath;
	int line = 0;
	int column = 0;
	QString rawLine;
};

struct CompilerCommandManifest {
	static constexpr int kSchemaVersion = 2;

	int schemaVersion = kSchemaVersion;
	QString manifestId;
	QDateTime createdUtc;
	QDateTime startedUtc;
	QDateTime finishedUtc;
	QString profileId;
	QString toolId;
	QString stageId;
	QString engineFamily;
	bool runnable = false;
	OperationState state = OperationState::Idle;
	int exitCode = -1;
	qint64 durationMs = -1;
	QString program;
	QStringList arguments;
	QString commandLine;
	QString workingDirectory;
	QMap<QString, QString> environmentSubset;
	QStringList inputPaths;
	QStringList expectedOutputPaths;
	QStringList registeredOutputPaths;
	QVector<CompilerFileHash> inputHashes;
	QVector<CompilerFileHash> outputHashes;
	QVector<CompilerDiagnostic> diagnostics;
	QString stdoutText;
	QString stderrText;
	QStringList warnings;
	QStringList errors;
	QVector<CompilerTaskLogEntry> taskLog;
};

QVector<CompilerProfileDescriptor> compilerProfileDescriptors();
QStringList compilerProfileIds();
bool compilerProfileForId(const QString& id, CompilerProfileDescriptor* out = nullptr);

CompilerCommandPlan buildCompilerCommandPlan(const CompilerCommandRequest& request);
QString compilerCommandLineText(const QString& program, const QStringList& arguments);
QString compilerCommandPlanText(const CompilerCommandPlan& plan);
CompilerCommandManifest compilerCommandManifestFromPlan(const CompilerCommandPlan& plan);
QJsonObject compilerCommandManifestJson(const CompilerCommandManifest& manifest);
QString compilerCommandManifestText(const CompilerCommandManifest& manifest);
bool saveCompilerCommandManifest(const CompilerCommandManifest& manifest, const QString& path, QString* error = nullptr);
bool loadCompilerCommandManifest(const QString& path, CompilerCommandManifest* manifest, QString* error = nullptr);

} // namespace vibestudio
