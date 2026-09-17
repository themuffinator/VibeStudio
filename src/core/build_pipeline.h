#pragma once

// Chained build pipelines and game launch plans.
//
// A pipeline turns the single-profile compiler wrappers into the loop a mapper
// actually runs: source map -> node/BSP stage -> visibility stage -> lighting
// stage -> packaged output -> launch in the configured game. Every stage still
// goes through the shared compiler runner, so logs, diagnostics, hashes, and
// command manifests stay identical to a single-profile run.

#include "core/compiler_profiles.h"
#include "core/compiler_runner.h"
#include "core/game_installation.h"
#include "core/operation_state.h"

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace vibestudio {

struct BuildPipelineStage {
	QString id;
	QString profileId;
	QString displayName;
	QString description;
	QStringList extraArguments;
	// Empty means "use the pipeline input". Otherwise the id of an earlier
	// stage whose expected output feeds this stage.
	QString inputFromStageId;
	bool optional = false;
	bool enabledByDefault = true;
};

struct BuildPipelineDescriptor {
	QString id;
	QString displayName;
	QString engineFamily;
	QString description;
	QStringList inputExtensions;
	QString outputExtension;
	QVector<BuildPipelineStage> stages;
};

struct BuildPipelineRequest {
	QString pipelineId;
	QString inputPath;
	QString outputPath;
	QString workingDirectory;
	QString workspaceRootPath;
	QStringList disabledStageIds;
	QMap<QString, QStringList> stageExtraArguments;
	QStringList extraSearchPaths;
	QVector<CompilerToolPathOverride> executableOverrides;
	QString manifestDirectory;
	bool dryRun = false;
	bool stopOnFailure = true;
	bool registerOutputs = false;
	int stageTimeoutMs = 10 * 60 * 1000;
};

struct BuildPipelineStageResult {
	BuildPipelineStage stage;
	CompilerCommandPlan plan;
	CompilerRunResult run;
	OperationState state = OperationState::Idle;
	bool skipped = false;
	QString skipReason;
	QString inputPath;
	QString outputPath;
	QString manifestPath;
};

struct BuildPipelineResult {
	BuildPipelineDescriptor pipeline;
	QString inputPath;
	QString finalOutputPath;
	OperationState state = OperationState::Idle;
	bool dryRun = false;
	bool cancelled = false;
	qint64 durationMs = -1;
	int plannedStageCount = 0;
	int completedStageCount = 0;
	int failedStageCount = 0;
	int skippedStageCount = 0;
	QVector<BuildPipelineStageResult> stages;
	QVector<CompilerDiagnostic> diagnostics;
	QStringList registeredOutputPaths;
	QStringList warnings;
	QStringList errors;

	[[nodiscard]] bool succeeded() const;
};

struct BuildPipelineCallbacks {
	std::function<bool()> cancellationRequested;
	std::function<void(int stageIndex, const BuildPipelineStage& stage)> stageStarted;
	std::function<void(int stageIndex, const BuildPipelineStageResult& result)> stageFinished;
	std::function<void(const CompilerTaskLogEntry& entry)> logEntry;
};

QVector<BuildPipelineDescriptor> buildPipelineDescriptors();
QStringList buildPipelineIds();
bool buildPipelineForId(const QString& id, BuildPipelineDescriptor* out = nullptr);
QStringList buildPipelineIdsForInput(const QString& inputPath);

// Resolves stages, inputs, and outputs without running anything.
BuildPipelineResult planBuildPipeline(const BuildPipelineRequest& request);
BuildPipelineResult runBuildPipeline(const BuildPipelineRequest& request, const BuildPipelineCallbacks& callbacks = {});

QString buildPipelineResultText(const BuildPipelineResult& result);
QJsonObject buildPipelineResultJson(const BuildPipelineResult& result);

// ---------------------------------------------------------------------------
// Launch / in-game testing
// ---------------------------------------------------------------------------

struct GameLaunchProfile {
	QString id;
	QString displayName;
	QString engineFamily;
	QString description;
	// Argument templates. Supported tokens: {map}, {mod}, {basedir}, {bsp}.
	QStringList argumentTemplates;
	bool requiresMap = true;
};

struct GameLaunchRequest {
	QString launchProfileId;
	QString executablePath;
	QString mapName;
	QString modDirectory;
	QString baseDirectory;
	QString bspPath;
	QString workingDirectory;
	QStringList extraArguments;
	bool dryRun = true;
};

struct GameLaunchPlan {
	GameLaunchProfile profile;
	bool profileFound = false;
	bool runnable = false;
	QString program;
	QStringList arguments;
	QString commandLine;
	QString workingDirectory;
	QStringList warnings;
	QStringList errors;

	[[nodiscard]] OperationState state() const;
};

QVector<GameLaunchProfile> gameLaunchProfiles();
QStringList gameLaunchProfileIds();
bool gameLaunchProfileForId(const QString& id, GameLaunchProfile* out = nullptr);
QString defaultGameLaunchProfileId(GameEngineFamily family);

GameLaunchPlan buildGameLaunchPlan(const GameLaunchRequest& request, const GameInstallationProfile& installation);
// Starts the planned process detached. Never runs when `plan.runnable` is false.
bool startGameLaunch(const GameLaunchPlan& plan, qint64* pidOut = nullptr, QString* error = nullptr);
QString gameLaunchPlanText(const GameLaunchPlan& plan);
QJsonObject gameLaunchPlanJson(const GameLaunchPlan& plan);

} // namespace vibestudio
