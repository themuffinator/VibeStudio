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
	// A deferred preview is never runnable. Real preparation validates a WAD
	// snapshot; startGameLaunch rechecks its hash before starting the process.
	bool artifactValidationPending = false;
	bool cancelled = false;
	QString validatedArtifactPath;
	QByteArray validatedArtifactHash;
	QJsonObject nodeBuild;

	[[nodiscard]] OperationState state() const;
};

QVector<GameLaunchProfile> gameLaunchProfiles();
QStringList gameLaunchProfileIds();
bool gameLaunchProfileForId(const QString& id, GameLaunchProfile* out = nullptr);
QString defaultGameLaunchProfileId(GameEngineFamily family);

// The IWAD a Doom-family launch loads: `baseDirectory` when it names a file,
// else the installation's first base package that exists, else the first of
// its game's known IWADs found in the folder. Empty when there is none.
QString doomIwadPath(const GameInstallationProfile& installation, const QString& baseDirectory);
// The -warp arguments for a Doom map lump: "07" for MAP07, "2 3" for E2M3.
// Anything else comes back as given.
QString doomWarpArguments(const QString& mapName);
GameLaunchPlan buildGameLaunchPlan(const GameLaunchRequest& request, const GameInstallationProfile& installation,
	const std::function<bool()>& isCancelled = {}, bool deferArtifactValidation = false);
// Starts the planned process detached. Never runs when `plan.runnable` is false.
bool startGameLaunch(const GameLaunchPlan& plan, qint64* pidOut = nullptr, QString* error = nullptr,
	const std::function<bool()>& isCancelled = {});
QString gameLaunchPlanText(const GameLaunchPlan& plan);
QJsonObject gameLaunchPlanJson(const GameLaunchPlan& plan);

// ---------------------------------------------------------------------------
// Test-map deployment
// ---------------------------------------------------------------------------
//
// Quake-family engines load a map only from a game folder's maps directory
// (`<root>/id1/maps/start.bsp`), so testing a build means copying it there.
// Doom-family ports load the built PWAD directly with -file, so nothing is
// copied for them. Copying writes into the user's game installation, so a plan
// is only runnable for an installation profile that is not read-only.

struct GameMapDeployFile {
	QString sourcePath;
	QString destinationPath;
	// A file of that name is already there with different bytes.
	bool replacesExisting = false;
	// The destination already holds these exact bytes.
	bool upToDate = false;
};

struct GameMapDeployPlan {
	// The engine loads maps from a game folder, so a copy is needed at all.
	bool required = false;
	// The installation profile allows the studio to write into it.
	bool allowed = false;
	QString gameDirectory;
	QString targetDirectory;
	QVector<GameMapDeployFile> files;
	QStringList warnings;
	QStringList errors;

	[[nodiscard]] bool upToDate() const;
	[[nodiscard]] bool runnable() const;
};

// Plans copying `builtMapPath` and its lighting companions (`.lit`, `.lux`)
// into `<root>/<game folder>/maps`. The game folder is `modDirectory` when it
// is given, else the game's base folder; it must be a single folder name.
GameMapDeployPlan planGameMapDeploy(const GameInstallationProfile& installation, const QString& modDirectory, const QString& builtMapPath);
// Copies every file of a runnable plan through a temporary file and a rename,
// so a failed copy never leaves a half-written map. Nothing else in the
// installation is touched. `written` receives the destinations written.
bool deployGameMap(const GameMapDeployPlan& plan, QStringList* written = nullptr, QString* error = nullptr);
QString gameMapDeployPlanText(const GameMapDeployPlan& plan);
QJsonObject gameMapDeployPlanJson(const GameMapDeployPlan& plan);

} // namespace vibestudio
