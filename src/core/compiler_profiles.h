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

// How a profile's tool accepts an explicit output path.
enum class CompilerOutputArgumentStyle {
	// The tool has no way to receive an output path (ericw-tools vis/light rewrite their input).
	None,
	// The output path is a trailing positional argument (ericw-tools qbsp: "sourcefile.map [destfile.bsp]").
	Positional,
	// The output path follows a flag, for example ZDBSP's "-o/--output=FILE" (see external/compilers/zdbsp/main.cpp).
	Flag,
};

// Where a tool writes when VibeStudio does not request an explicit output path.
enum class CompilerDefaultOutputMode {
	// "<input base>.<defaultOutputExtension>" beside the input.
	DerivedFromInput,
	// The tool rewrites its input file (ericw-tools vis/light, q3map2 -vis/-light).
	InPlace,
	// A fixed file name in the working directory (ZDBSP writes "tmp.wad" when -o is absent).
	WorkingDirectoryFile,
	// The destination cannot be predicted from the command line (q3map2 -pk3 writes into the engine path).
	Unknown,
	// The tool reports to its console channels and writes no artifact (bsputil --check, q3map2 -help).
	NoArtifact,
};

struct CompilerArgumentPreset {
	QString id;
	QString displayName;
	QString description;
	QStringList arguments;
	// True when the caller must append a value (directory, mod name, thread count) after `arguments`.
	bool requiresValue = false;
	QString valuePlaceholder;
};

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
	// Always emitted as argument 0, before default and user arguments. q3map2 dispatches on the first
	// remaining token (external/compilers/q3map2-nrc/tools/quake3/q3map2/main.cpp), so the stage token
	// must never be pushed behind user extras.
	QString leadingStageArgument;
	CompilerOutputArgumentStyle outputArgumentStyle = CompilerOutputArgumentStyle::None;
	// Flag that introduces the output path when outputArgumentStyle is Flag, for example "-o".
	QString outputArgumentFlag;
	// True when the output flag has to follow the input path (ZokumBSP parses "-o" after the level list).
	bool outputArgumentAfterInput = false;
	CompilerDefaultOutputMode defaultOutputMode = CompilerDefaultOutputMode::DerivedFromInput;
	// File name used with CompilerDefaultOutputMode::WorkingDirectoryFile.
	QString defaultOutputFileName;
	// Optional sibling artifacts produced beside the main output (qbsp's .prt/.pts). These are registered
	// when present and never treated as required.
	QStringList relatedOutputExtensions;
	// Sibling inputs the stage needs beside its input file (vis needs the qbsp-written .prt).
	QStringList requiredCompanionInputExtensions;
	// Arguments that add a required sibling output, for example light's "-lit" writing "<base>.lit".
	QMap<QString, QString> argumentTriggeredOutputExtensions;
	QVector<CompilerArgumentPreset> argumentPresets;
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
	// False when the destination cannot be derived from the command line; artifact validation then skips it.
	bool expectedOutputKnown = true;
	// Further required artifacts, for example light's "<base>.lit".
	QStringList additionalExpectedOutputPaths;
	// Artifacts that may or may not appear (qbsp's .prt portal file and .pts leak file).
	QStringList relatedOutputPaths;
	QString commandLine;
	// Informational known-issue context. Notes never change the run state.
	QStringList knownIssueNotes;
	QStringList knownIssueWarnings;
	QStringList preflightWarnings;
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
	// "stdout" or "stderr"; kept so callers can tell the channels apart.
	QString channel;
};

struct CompilerCommandManifest {
	static constexpr int kSchemaVersion = 4;

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
	bool expectedOutputKnown = true;
	QStringList optionalOutputPaths;
	QStringList registeredOutputPaths;
	QVector<CompilerFileHash> inputHashes;
	QVector<CompilerFileHash> outputHashes;
	QVector<CompilerDiagnostic> diagnostics;
	QStringList knownIssueNotes;
	QStringList knownIssueWarnings;
	QStringList preflightWarnings;
	QString stdoutText;
	QString stderrText;
	QStringList warnings;
	QStringList errors;
	QVector<CompilerTaskLogEntry> taskLog;
};

QVector<CompilerProfileDescriptor> compilerProfileDescriptors();
QStringList compilerProfileIds();
bool compilerProfileForId(const QString& id, CompilerProfileDescriptor* out = nullptr);
QVector<CompilerArgumentPreset> compilerArgumentPresetsForProfile(const QString& profileId);
bool compilerArgumentPresetForId(const QString& profileId, const QString& presetId, CompilerArgumentPreset* out = nullptr);

CompilerCommandPlan buildCompilerCommandPlan(const CompilerCommandRequest& request);
QString compilerCommandLineText(const QString& program, const QStringList& arguments);
QString compilerCommandPlanText(const CompilerCommandPlan& plan);
CompilerCommandManifest compilerCommandManifestFromPlan(const CompilerCommandPlan& plan);
QJsonObject compilerCommandManifestJson(const CompilerCommandManifest& manifest);
QString compilerCommandManifestText(const CompilerCommandManifest& manifest);
bool saveCompilerCommandManifest(const CompilerCommandManifest& manifest, const QString& path, QString* error = nullptr);
bool loadCompilerCommandManifest(const QString& path, CompilerCommandManifest* manifest, QString* error = nullptr);

} // namespace vibestudio
