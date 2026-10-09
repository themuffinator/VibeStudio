#include "core/build_pipeline.h"
#include "core/level_doom_nodes.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>

namespace vibestudio {

namespace {

// Matches the literal produced by buildCompilerCommandPlan() in
// src/core/compiler_profiles.cpp. A chained stage legitimately plans against an
// input its predecessor has not produced yet, so that single plan error is
// downgraded to a warning while planning and while dry-running. If the upstream
// literal ever changes, the comparison simply stops matching and the condition
// is reported as a hard error again.
QString missingInputErrorText()
{
	return QCoreApplication::translate("VibeStudioCompilerProfiles", "Input file does not exist.");
}

QString normalizedId(const QString& value)
{
	return value.trimmed().toLower().replace('_', '-');
}

QString absoluteCleanPath(const QString& path)
{
	if (path.trimmed().isEmpty()) {
		return {};
	}
	return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString nativePath(const QString& path)
{
	return path.isEmpty() ? QCoreApplication::translate("VibeStudioBuildPipeline", "(not resolved)") : QDir::toNativeSeparators(path);
}

void appendUnique(QStringList* values, const QString& value)
{
	if (!values || value.trimmed().isEmpty()) {
		return;
	}
	const QString trimmed = value.trimmed();
	if (!values->contains(trimmed)) {
		values->push_back(trimmed);
	}
}

bool extensionMatches(const QString& path, const QStringList& extensions)
{
	if (extensions.isEmpty()) {
		return true;
	}
	const QString suffix = QFileInfo(path).suffix().toLower();
	for (const QString& extension : extensions) {
		QString normalized = extension.toLower();
		if (normalized.startsWith('.')) {
			normalized.remove(0, 1);
		}
		if (suffix == normalized) {
			return true;
		}
	}
	return false;
}

bool containsNormalizedId(const QStringList& values, const QString& id)
{
	const QString normalized = normalizedId(id);
	for (const QString& value : values) {
		if (normalizedId(value) == normalized) {
			return true;
		}
	}
	return false;
}

QStringList stageArgumentsFor(const QMap<QString, QStringList>& arguments, const QString& stageId)
{
	if (arguments.contains(stageId)) {
		return arguments.value(stageId);
	}
	const QString normalized = normalizedId(stageId);
	for (auto it = arguments.cbegin(); it != arguments.cend(); ++it) {
		if (normalizedId(it.key()) == normalized) {
			return it.value();
		}
	}
	return {};
}

BuildPipelineStage pipelineStage(
	const QString& id,
	const QString& profileId,
	const QString& displayName,
	const QString& description,
	const QString& inputFromStageId,
	bool optional,
	bool enabledByDefault,
	const QStringList& extraArguments = {})
{
	BuildPipelineStage stage;
	stage.id = id;
	stage.profileId = profileId;
	stage.displayName = displayName;
	stage.description = description;
	stage.extraArguments = extraArguments;
	stage.inputFromStageId = inputFromStageId;
	stage.optional = optional;
	stage.enabledByDefault = enabledByDefault;
	return stage;
}

BuildPipelineDescriptor pipelineDescriptor(
	const QString& id,
	const QString& displayName,
	const QString& engineFamily,
	const QString& description,
	const QStringList& inputExtensions,
	const QString& outputExtension,
	const QVector<BuildPipelineStage>& stages)
{
	BuildPipelineDescriptor descriptor;
	descriptor.id = id;
	descriptor.displayName = displayName;
	descriptor.engineFamily = engineFamily;
	descriptor.description = description;
	descriptor.inputExtensions = inputExtensions;
	descriptor.outputExtension = outputExtension;
	descriptor.stages = stages;
	return descriptor;
}

GameLaunchProfile launchProfile(
	const QString& id,
	const QString& displayName,
	const QString& engineFamily,
	const QString& description,
	const QStringList& argumentTemplates,
	bool requiresMap)
{
	GameLaunchProfile profile;
	profile.id = id;
	profile.displayName = displayName;
	profile.engineFamily = engineFamily;
	profile.description = description;
	profile.argumentTemplates = argumentTemplates;
	profile.requiresMap = requiresMap;
	return profile;
}

// Splits a single argument template into concrete process arguments. The
// template is split on whitespace first so that resolved values may contain
// spaces. A template whose tokens do not all resolve is dropped as a unit.
bool resolveArgumentTemplate(
	const QString& templateText,
	const QMap<QString, QString>& tokens,
	QStringList* argumentsOut,
	QStringList* missingTokensOut)
{
	const QStringList parts = templateText.split(' ', Qt::SkipEmptyParts);
	QStringList resolvedParts;
	bool complete = true;
	for (const QString& part : parts) {
		QString value = part;
		for (auto it = tokens.cbegin(); it != tokens.cend(); ++it) {
			const QString token = QStringLiteral("{%1}").arg(it.key());
			if (!value.contains(token)) {
				continue;
			}
			if (it.value().trimmed().isEmpty()) {
				complete = false;
				if (missingTokensOut && !missingTokensOut->contains(it.key())) {
					missingTokensOut->push_back(it.key());
				}
			}
			value.replace(token, it.value());
		}
		// {warp} on its own may stand for two arguments: -warp 2 3 for E2M3.
		if (part == QStringLiteral("{warp}")) {
			resolvedParts += value.split(QLatin1Char(' '), Qt::SkipEmptyParts);
			continue;
		}
		if (!value.isEmpty()) {
			resolvedParts.push_back(value);
		}
	}
	if (!complete || resolvedParts.isEmpty()) {
		return false;
	}
	if (argumentsOut) {
		*argumentsOut = resolvedParts;
	}
	return true;
}

QJsonArray stringArrayJson(const QStringList& values)
{
	QJsonArray array;
	for (const QString& value : values) {
		array.append(value);
	}
	return array;
}

// Splits one stage's plan/run diagnostics into the pipeline aggregate.
// Returns true when at least one genuine (non-downgraded) error was seen.
// When `errorsOut` is given the errors are collected there instead of being merged straight into
// the pipeline errors, so the caller can route them once it knows whether the stage actually failed.
bool appendStageDiagnostics(
	BuildPipelineResult* result,
	const BuildPipelineStage& stage,
	const QStringList& warnings,
	const QStringList& errors,
	const QString& inputPath,
	bool allowMissingInputDowngrade,
	QStringList* errorsOut = nullptr)
{
	if (!result) {
		return false;
	}
	for (const QString& warning : warnings) {
		appendUnique(&result->warnings, QCoreApplication::translate("VibeStudioBuildPipeline", "Stage \"%1\": %2").arg(stage.id, warning));
	}
	bool hadRealError = false;
	const QString missingInput = missingInputErrorText();
	for (const QString& error : errors) {
		if (allowMissingInputDowngrade && error.trimmed() == missingInput) {
			appendUnique(&result->warnings, QCoreApplication::translate("VibeStudioBuildPipeline", "Stage \"%1\" input %2 does not exist yet; it is produced by an earlier stage of this pipeline.").arg(stage.id, nativePath(inputPath)));
			continue;
		}
		const QString entry = QCoreApplication::translate("VibeStudioBuildPipeline", "Stage \"%1\": %2").arg(stage.id, error);
		appendUnique(errorsOut ? errorsOut : &result->errors, entry);
		hadRealError = true;
	}
	return hadRealError;
}

OperationState aggregatePipelineState(const BuildPipelineResult& result)
{
	// A stage that genuinely failed outranks a cancellation, and a cancellation outranks leftover
	// error text: a late cancel must not hide a stage that had already failed, and error-shaped
	// output from a stage that exited 0 must not mask the cancellation either.
	if (result.failedStageCount > 0) {
		return OperationState::Failed;
	}
	if (result.cancelled) {
		return OperationState::Cancelled;
	}
	if (!result.errors.isEmpty()) {
		return OperationState::Failed;
	}
	if (!result.warnings.isEmpty()) {
		return OperationState::Warning;
	}
	for (const BuildPipelineStageResult& stage : result.stages) {
		if (stage.state == OperationState::Warning) {
			return OperationState::Warning;
		}
	}
	return OperationState::Completed;
}

// Resolves stages, inputs, and outputs. `stageRequestsOut` receives one entry
// per stage result (a default-constructed request for skipped stages) so the
// run phase can reuse the resolved commands without re-deriving them.
BuildPipelineResult planPipelineInternal(const BuildPipelineRequest& request, QVector<CompilerCommandRequest>* stageRequestsOut)
{
	BuildPipelineResult result;
	result.dryRun = request.dryRun;
	result.inputPath = absoluteCleanPath(request.inputPath);
	result.finalOutputPath = result.inputPath;

	if (!buildPipelineForId(request.pipelineId, &result.pipeline)) {
		result.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "Build pipeline \"%1\" is not known.").arg(request.pipelineId.trimmed());
		result.state = OperationState::Failed;
		return result;
	}

	if (result.inputPath.isEmpty()) {
		result.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "Pipeline input path is required.");
	} else {
		if (!QFileInfo(result.inputPath).isFile()) {
			result.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "Pipeline input file does not exist: %1").arg(nativePath(result.inputPath));
		}
		if (!extensionMatches(result.inputPath, result.pipeline.inputExtensions)) {
			result.warnings << QCoreApplication::translate("VibeStudioBuildPipeline", "Pipeline input extension does not match the expected source type for \"%1\".").arg(result.pipeline.id);
		}
	}

	const int stageCount = result.pipeline.stages.size();
	QVector<CompilerProfileDescriptor> profiles(stageCount);
	QVector<bool> skipped(stageCount, false);
	QVector<QString> skipReasons(stageCount);

	for (int index = 0; index < stageCount; ++index) {
		const BuildPipelineStage& stage = result.pipeline.stages.at(index);
		if (containsNormalizedId(request.disabledStageIds, stage.id)) {
			skipped[index] = true;
			skipReasons[index] = QCoreApplication::translate("VibeStudioBuildPipeline", "Stage was disabled for this run.");
			continue;
		}
		if (stage.optional && !stage.enabledByDefault) {
			skipped[index] = true;
			skipReasons[index] = QCoreApplication::translate("VibeStudioBuildPipeline", "Optional stage is disabled by default for this pipeline.");
			continue;
		}
		if (!compilerProfileForId(stage.profileId, &profiles[index])) {
			skipped[index] = true;
			skipReasons[index] = QCoreApplication::translate("VibeStudioBuildPipeline", "Compiler profile \"%1\" is not registered in this build.").arg(stage.profileId);
			appendUnique(&result.warnings, QCoreApplication::translate("VibeStudioBuildPipeline", "Stage \"%1\" was skipped: %2").arg(stage.id, skipReasons[index]));
			continue;
		}
	}

	// The pipeline output path belongs to the last surviving stage that can
	// actually be told where to write. Later in-place stages (vis/light) then
	// operate on that artifact.
	int outputStageIndex = -1;
	for (int index = 0; index < stageCount; ++index) {
		if (!skipped[index] && profiles[index].outputPathArgumentSupported) {
			outputStageIndex = index;
		}
	}
	const QString requestedOutputPath = absoluteCleanPath(request.outputPath);
	if (!requestedOutputPath.isEmpty() && outputStageIndex < 0) {
		appendUnique(&result.warnings, QCoreApplication::translate("VibeStudioBuildPipeline", "No stage in this pipeline accepts an explicit output path; the requested output path is ignored."));
	}

	QMap<QString, QString> stageOutputs;
	QString latestOutputPath = result.inputPath;

	for (int index = 0; index < stageCount; ++index) {
		const BuildPipelineStage& stage = result.pipeline.stages.at(index);
		BuildPipelineStageResult stageResult;
		stageResult.stage = stage;

		if (skipped[index]) {
			stageResult.skipped = true;
			stageResult.skipReason = skipReasons[index];
			stageResult.state = OperationState::Idle;
			stageResult.inputPath = latestOutputPath;
			result.skippedStageCount++;
			result.stages.push_back(stageResult);
			if (stageRequestsOut) {
				stageRequestsOut->push_back(CompilerCommandRequest());
			}
			continue;
		}

		QString stageInputPath;
		const QString chainedFrom = normalizedId(stage.inputFromStageId);
		if (chainedFrom.isEmpty()) {
			stageInputPath = result.inputPath;
		} else if (stageOutputs.contains(chainedFrom)) {
			stageInputPath = stageOutputs.value(chainedFrom);
		} else {
			stageInputPath = latestOutputPath;
			appendUnique(&result.warnings, QCoreApplication::translate("VibeStudioBuildPipeline", "Stage \"%1\" now consumes %2 because stage \"%3\" is not part of this run.").arg(stage.id, nativePath(stageInputPath), stage.inputFromStageId));
		}

		CompilerCommandRequest command;
		command.profileId = stage.profileId;
		command.inputPath = stageInputPath;
		command.outputPath = (index == outputStageIndex) ? requestedOutputPath : QString();
		command.workingDirectory = request.workingDirectory;
		command.workspaceRootPath = request.workspaceRootPath;
		command.extraArguments = stage.extraArguments;
		command.extraArguments += stageArgumentsFor(request.stageExtraArguments, stage.id);
		command.extraSearchPaths = request.extraSearchPaths;
		command.executableOverrides = request.executableOverrides;

		stageResult.plan = buildCompilerCommandPlan(command);
		stageResult.inputPath = stageResult.plan.inputPath.isEmpty() ? stageInputPath : stageResult.plan.inputPath;
		stageResult.outputPath = stageResult.plan.expectedOutputPath;

		const bool chainedInput = !chainedFrom.isEmpty();
		const bool hadRealError = appendStageDiagnostics(
			&result,
			stage,
			stageResult.plan.warnings,
			stageResult.plan.errors,
			stageResult.inputPath,
			chainedInput && !QFileInfo(stageResult.inputPath).isFile());
		stageResult.state = stageResult.plan.state();
		if (stageResult.state == OperationState::Failed && !hadRealError) {
			stageResult.state = OperationState::Warning;
		}

		if (!stageResult.outputPath.isEmpty()) {
			stageOutputs.insert(normalizedId(stage.id), stageResult.outputPath);
			latestOutputPath = stageResult.outputPath;
		}
		result.plannedStageCount++;
		result.stages.push_back(stageResult);
		if (stageRequestsOut) {
			stageRequestsOut->push_back(command);
		}
	}

	result.finalOutputPath = latestOutputPath;
	if (result.plannedStageCount == 0) {
		appendUnique(&result.warnings, QCoreApplication::translate("VibeStudioBuildPipeline", "No stage of this pipeline is enabled; nothing would be compiled."));
	}
	result.state = aggregatePipelineState(result);
	return result;
}

} // namespace

bool BuildPipelineResult::succeeded() const
{
	if (failedStageCount > 0 || cancelled) {
		return false;
	}
	return state == OperationState::Completed || state == OperationState::Warning;
}

OperationState GameLaunchPlan::state() const
{
	if (cancelled) { return OperationState::Cancelled; }
	if (!errors.isEmpty()) {
		return OperationState::Failed;
	}
	if (runnable) {
		return warnings.isEmpty() ? OperationState::Completed : OperationState::Warning;
	}
	return profileFound ? OperationState::Warning : OperationState::Idle;
}

QVector<BuildPipelineDescriptor> buildPipelineDescriptors()
{
	return {
		pipelineDescriptor(
			QStringLiteral("quake-full"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake full compile"),
			QStringLiteral("idTech2"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Runs the complete Quake-family loop: BSP, visibility, and lighting through VibeMap2."),
			{QStringLiteral("map")},
			QStringLiteral("bsp"),
			{
				pipelineStage(
					QStringLiteral("qbsp"),
					QStringLiteral("vibemap2-bsp"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "QBSP"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Compiles the .map source into a Quake-family BSP."),
					QString(),
					false,
					true),
				pipelineStage(
					QStringLiteral("vis"),
					QStringLiteral("vibemap2-vis"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "VIS"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Computes the potentially visible set for the compiled BSP, in place."),
					QStringLiteral("qbsp"),
					true,
					true),
				pipelineStage(
					QStringLiteral("light"),
					QStringLiteral("vibemap2-light"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "LIGHT"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Computes lightmaps and light data for the compiled BSP, in place."),
					QStringLiteral("vis"),
					true,
					true),
			}),
		pipelineDescriptor(
			QStringLiteral("quake-fast"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake fast iteration"),
			QStringLiteral("idTech2"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quick edit/test loop: BSP then lighting. Visibility is disabled by default; pass fast-mode switches through stageExtraArguments."),
			{QStringLiteral("map")},
			QStringLiteral("bsp"),
			{
				pipelineStage(
					QStringLiteral("qbsp"),
					QStringLiteral("vibemap2-bsp"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "QBSP"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Compiles the .map source into a Quake-family BSP."),
					QString(),
					false,
					true),
				pipelineStage(
					QStringLiteral("vis"),
					QStringLiteral("vibemap2-vis"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "VIS"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Visibility processing, disabled by default for fast iteration."),
					QStringLiteral("qbsp"),
					true,
					false),
				pipelineStage(
					QStringLiteral("light"),
					QStringLiteral("vibemap2-light"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "LIGHT"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Lighting pass; the caller supplies fast-mode arguments."),
					QStringLiteral("vis"),
					true,
					true),
			}),
		pipelineDescriptor(
			QStringLiteral("quake-bsp-only"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake BSP only"),
			QStringLiteral("idTech2"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Compiles geometry only, without visibility or lighting."),
			{QStringLiteral("map")},
			QStringLiteral("bsp"),
			{
				pipelineStage(
					QStringLiteral("qbsp"),
					QStringLiteral("vibemap2-bsp"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "QBSP"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Compiles the .map source into a Quake-family BSP."),
					QString(),
					false,
					true),
			}),
		pipelineDescriptor(
			QStringLiteral("quake3-full"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake III full compile"),
			QStringLiteral("idTech3"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Runs the complete Quake III-family loop: BSP, visibility, and lighting through VibeMap3."),
			{QStringLiteral("map")},
			QStringLiteral("bsp"),
			{
				pipelineStage(
					QStringLiteral("bsp"),
					QStringLiteral("vibemap3-bsp"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "BSP"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Builds the Quake III-family BSP from the .map source."),
					QString(),
					false,
					true),
				pipelineStage(
					QStringLiteral("vis"),
					QStringLiteral("vibemap3-vis"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "VIS"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Computes the potentially visible set for the compiled BSP, in place."),
					QStringLiteral("bsp"),
					true,
					true),
				pipelineStage(
					QStringLiteral("light"),
					QStringLiteral("vibemap3-light"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "LIGHT"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Computes lightmaps for the compiled BSP, in place."),
					QStringLiteral("vis"),
					true,
					true),
			}),
		pipelineDescriptor(
			QStringLiteral("quake3-bsp-only"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake III BSP only"),
			QStringLiteral("idTech3"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Compiles Quake III-family geometry only, without visibility or lighting."),
			{QStringLiteral("map")},
			QStringLiteral("bsp"),
			{
				pipelineStage(
					QStringLiteral("bsp"),
					QStringLiteral("vibemap3-bsp"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "BSP"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Builds the Quake III-family BSP from the .map source."),
					QString(),
					false,
					true),
			}),
		pipelineDescriptor(
			QStringLiteral("doom-zdbsp"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Doom nodes (ZDBSP)"),
			QStringLiteral("idTech1"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Builds Doom-family map nodes with ZDBSP."),
			{QStringLiteral("wad")},
			QStringLiteral("wad"),
			{
				pipelineStage(
					QStringLiteral("nodes"),
					QStringLiteral("zdbsp-nodes"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Nodes"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Builds nodes, blockmap, and reject data for a Doom-family WAD."),
					QString(),
					false,
					true),
			}),
		pipelineDescriptor(
			QStringLiteral("doom-zokumbsp"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Doom nodes (ZokumBSP)"),
			QStringLiteral("idTech1"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Builds Doom-family map nodes with ZokumBSP."),
			{QStringLiteral("wad")},
			QStringLiteral("wad"),
			{
				pipelineStage(
					QStringLiteral("nodes"),
					QStringLiteral("zokumbsp-nodes"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Nodes"),
					QCoreApplication::translate("VibeStudioBuildPipeline", "Builds nodes, blockmap, and reject data for a Doom-family WAD."),
					QString(),
					false,
					true),
			}),
	};
}

QStringList buildPipelineIds()
{
	QStringList ids;
	for (const BuildPipelineDescriptor& descriptor : buildPipelineDescriptors()) {
		ids.push_back(descriptor.id);
	}
	return ids;
}

bool buildPipelineForId(const QString& id, BuildPipelineDescriptor* out)
{
	const QString normalized = normalizedId(id);
	if (normalized.isEmpty()) {
		return false;
	}
	for (const BuildPipelineDescriptor& descriptor : buildPipelineDescriptors()) {
		if (normalizedId(descriptor.id) == normalized) {
			if (out) {
				*out = descriptor;
			}
			return true;
		}
	}
	return false;
}

QStringList buildPipelineIdsForInput(const QString& inputPath)
{
	QStringList ids;
	if (inputPath.trimmed().isEmpty()) {
		return ids;
	}
	for (const BuildPipelineDescriptor& descriptor : buildPipelineDescriptors()) {
		if (!descriptor.inputExtensions.isEmpty() && extensionMatches(inputPath, descriptor.inputExtensions)) {
			ids.push_back(descriptor.id);
		}
	}
	return ids;
}

BuildPipelineResult planBuildPipeline(const BuildPipelineRequest& request)
{
	return planPipelineInternal(request, nullptr);
}

BuildPipelineResult runBuildPipeline(const BuildPipelineRequest& request, const BuildPipelineCallbacks& callbacks)
{
	QVector<CompilerCommandRequest> stageRequests;
	BuildPipelineResult result = planPipelineInternal(request, &stageRequests);
	if (result.pipeline.id.isEmpty()) {
		return result;
	}

	QElapsedTimer timer;
	timer.start();

	QString manifestDirectory = absoluteCleanPath(request.manifestDirectory);
	if (!manifestDirectory.isEmpty() && !QDir().mkpath(manifestDirectory)) {
		appendUnique(&result.warnings, QCoreApplication::translate("VibeStudioBuildPipeline", "Could not create the manifest directory %1; stage manifests will not be written.").arg(nativePath(manifestDirectory)));
		manifestDirectory.clear();
	}

	bool stopped = false;
	for (int index = 0; index < result.stages.size(); ++index) {
		BuildPipelineStageResult& stageResult = result.stages[index];
		if (stageResult.skipped) {
			continue;
		}

		if (!stopped && callbacks.cancellationRequested && callbacks.cancellationRequested()) {
			result.cancelled = true;
			stopped = true;
		}

		if (stopped) {
			stageResult.skipped = true;
			stageResult.state = OperationState::Cancelled;
			stageResult.skipReason = result.cancelled
				? QCoreApplication::translate("VibeStudioBuildPipeline", "Pipeline was cancelled before this stage started.")
				: QCoreApplication::translate("VibeStudioBuildPipeline", "An earlier stage failed and the pipeline stops on failure.");
			result.plannedStageCount = qMax(0, result.plannedStageCount - 1);
			result.skippedStageCount++;
			if (callbacks.stageFinished) {
				callbacks.stageFinished(index, stageResult);
			}
			continue;
		}

		if (callbacks.stageStarted) {
			callbacks.stageStarted(index, stageResult.stage);
		}
		stageResult.state = OperationState::Running;

		CompilerRunRequest runRequest;
		runRequest.command = stageRequests.value(index);
		runRequest.dryRun = request.dryRun;
		runRequest.registerOutputs = request.registerOutputs;
		runRequest.timeoutMs = request.stageTimeoutMs;
		if (!manifestDirectory.isEmpty()) {
			runRequest.manifestPath = QDir(manifestDirectory).filePath(QStringLiteral("%1.%2.json").arg(result.pipeline.id, stageResult.stage.id));
		}

		CompilerRunCallbacks runCallbacks;
		runCallbacks.cancellationRequested = callbacks.cancellationRequested;
		runCallbacks.logEntry = callbacks.logEntry;

		stageResult.run = runCompilerCommand(runRequest, runCallbacks);
		stageResult.plan = stageResult.run.plan;
		stageResult.manifestPath = stageResult.run.manifestPath;
		if (!stageResult.plan.inputPath.isEmpty()) {
			stageResult.inputPath = stageResult.plan.inputPath;
		}
		if (!stageResult.plan.expectedOutputPath.isEmpty()) {
			stageResult.outputPath = stageResult.plan.expectedOutputPath;
		}

		const bool chainedInput = !stageResult.stage.inputFromStageId.trimmed().isEmpty();
		QStringList stageErrors;
		const bool hadRealError = appendStageDiagnostics(
			&result,
			stageResult.stage,
			stageResult.run.manifest.warnings,
			stageResult.run.manifest.errors,
			stageResult.inputPath,
			request.dryRun && chainedInput && !QFileInfo(stageResult.inputPath).isFile(),
			&stageErrors);

		for (const CompilerDiagnostic& diagnostic : stageResult.run.diagnostics) {
			result.diagnostics.push_back(diagnostic);
		}
		for (const QString& output : stageResult.run.registeredOutputPaths) {
			appendUnique(&result.registeredOutputPaths, output);
		}

		stageResult.state = stageResult.run.state;
		const bool stageCancelled = stageResult.run.cancelled || stageResult.run.state == OperationState::Cancelled;
		bool stageFailed = false;
		if (!stageCancelled) {
			if (!stageResult.run.error.trimmed().isEmpty() || stageResult.run.timedOut) {
				stageFailed = true;
			} else if (stageResult.run.started && stageResult.run.exitCode != 0) {
				stageFailed = true;
			} else if (stageResult.run.state == OperationState::Failed && hadRealError) {
				stageFailed = true;
			} else if (stageResult.run.state == OperationState::Failed) {
				// The only errors were downgraded chained-input notices.
				stageResult.state = OperationState::Warning;
			}
		}

		if (stageCancelled) {
			result.cancelled = true;
			stageResult.state = OperationState::Cancelled;
			stopped = true;
		} else if (stageFailed) {
			stageResult.state = OperationState::Failed;
			result.failedStageCount++;
			if (request.stopOnFailure) {
				stopped = true;
			}
		} else {
			result.completedStageCount++;
		}

		// A stage that exited 0 still produced its artifacts. VibeMap2 prints non-fatal
		// "ERROR: ..." notices and carries on (common/bspfile_common.cc, common/bspxfile.cc), so
		// those lines are reported as warnings instead of failing a pipeline whose every stage
		// succeeded. The same applies to the partial output of a cancelled stage, which would
		// otherwise be reported as a failure rather than a cancellation.
		for (const QString& error : stageErrors) {
			appendUnique(stageFailed ? &result.errors : &result.warnings, error);
		}

		if (callbacks.stageFinished) {
			callbacks.stageFinished(index, stageResult);
		}
	}

	if (!result.cancelled && callbacks.cancellationRequested && callbacks.cancellationRequested()) {
		result.cancelled = true;
	}

	// The final artifact is the output of the last stage that actually survived.
	QString finalOutputPath = result.inputPath;
	for (const BuildPipelineStageResult& stageResult : result.stages) {
		if (!stageResult.skipped && !stageResult.outputPath.isEmpty()) {
			finalOutputPath = stageResult.outputPath;
		}
	}
	result.finalOutputPath = finalOutputPath;
	result.durationMs = timer.elapsed();
	result.state = aggregatePipelineState(result);
	return result;
}

QString buildPipelineResultText(const BuildPipelineResult& result)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Build pipeline result");
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Pipeline: %1").arg(result.pipeline.id.isEmpty() ? QCoreApplication::translate("VibeStudioBuildPipeline", "(unknown)") : result.pipeline.id);
	if (!result.pipeline.displayName.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Name: %1").arg(result.pipeline.displayName);
	}
	if (!result.pipeline.engineFamily.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Engine: %1").arg(result.pipeline.engineFamily);
	}
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "State: %1").arg(operationStateId(result.state));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Succeeded: %1").arg(result.succeeded() ? QCoreApplication::translate("VibeStudioBuildPipeline", "yes") : QCoreApplication::translate("VibeStudioBuildPipeline", "no"));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Dry run: %1").arg(result.dryRun ? QCoreApplication::translate("VibeStudioBuildPipeline", "yes") : QCoreApplication::translate("VibeStudioBuildPipeline", "no"));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Cancelled: %1").arg(result.cancelled ? QCoreApplication::translate("VibeStudioBuildPipeline", "yes") : QCoreApplication::translate("VibeStudioBuildPipeline", "no"));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Input: %1").arg(nativePath(result.inputPath));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Final output: %1").arg(nativePath(result.finalOutputPath));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Stages: %1 planned, %2 completed, %3 failed, %4 skipped")
		.arg(result.plannedStageCount)
		.arg(result.completedStageCount)
		.arg(result.failedStageCount)
		.arg(result.skippedStageCount);
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Duration: %1").arg(result.durationMs >= 0 ? QCoreApplication::translate("VibeStudioBuildPipeline", "%1 ms").arg(result.durationMs) : QCoreApplication::translate("VibeStudioBuildPipeline", "not run"));

	for (const BuildPipelineStageResult& stageResult : result.stages) {
		lines << QString();
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Stage %1 (%2)").arg(stageResult.stage.id, stageResult.stage.profileId);
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "  State: %1").arg(operationStateId(stageResult.state));
		if (stageResult.skipped) {
			lines << QCoreApplication::translate("VibeStudioBuildPipeline", "  Skipped: %1").arg(stageResult.skipReason);
			continue;
		}
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "  Input: %1").arg(nativePath(stageResult.inputPath));
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "  Output: %1").arg(nativePath(stageResult.outputPath));
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "  Command line: %1").arg(stageResult.plan.commandLine);
		if (!stageResult.manifestPath.isEmpty()) {
			lines << QCoreApplication::translate("VibeStudioBuildPipeline", "  Manifest: %1").arg(nativePath(stageResult.manifestPath));
		}
		if (stageResult.run.exitCode >= 0) {
			lines << QCoreApplication::translate("VibeStudioBuildPipeline", "  Exit code: %1").arg(stageResult.run.exitCode);
		}
	}

	if (!result.registeredOutputPaths.isEmpty()) {
		lines << QString();
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Registered outputs");
		for (const QString& output : result.registeredOutputPaths) {
			lines << QStringLiteral("- %1").arg(nativePath(output));
		}
	}
	if (!result.warnings.isEmpty()) {
		lines << QString();
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Warnings");
		for (const QString& warning : result.warnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	if (!result.errors.isEmpty()) {
		lines << QString();
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Errors");
		for (const QString& error : result.errors) {
			lines << QStringLiteral("- %1").arg(error);
		}
	}
	return lines.join('\n');
}

QJsonObject buildPipelineResultJson(const BuildPipelineResult& result)
{
	QJsonObject object;
	object.insert(QStringLiteral("pipelineId"), result.pipeline.id);
	object.insert(QStringLiteral("displayName"), result.pipeline.displayName);
	object.insert(QStringLiteral("engineFamily"), result.pipeline.engineFamily);
	object.insert(QStringLiteral("state"), operationStateId(result.state));
	object.insert(QStringLiteral("succeeded"), result.succeeded());
	object.insert(QStringLiteral("dryRun"), result.dryRun);
	object.insert(QStringLiteral("cancelled"), result.cancelled);
	object.insert(QStringLiteral("inputPath"), result.inputPath);
	object.insert(QStringLiteral("finalOutputPath"), result.finalOutputPath);
	object.insert(QStringLiteral("durationMs"), static_cast<double>(result.durationMs));
	object.insert(QStringLiteral("plannedStageCount"), result.plannedStageCount);
	object.insert(QStringLiteral("completedStageCount"), result.completedStageCount);
	object.insert(QStringLiteral("failedStageCount"), result.failedStageCount);
	object.insert(QStringLiteral("skippedStageCount"), result.skippedStageCount);

	QJsonArray stages;
	for (const BuildPipelineStageResult& stageResult : result.stages) {
		QJsonObject stageObject;
		stageObject.insert(QStringLiteral("id"), stageResult.stage.id);
		stageObject.insert(QStringLiteral("profileId"), stageResult.stage.profileId);
		stageObject.insert(QStringLiteral("displayName"), stageResult.stage.displayName);
		stageObject.insert(QStringLiteral("state"), operationStateId(stageResult.state));
		stageObject.insert(QStringLiteral("skipped"), stageResult.skipped);
		if (!stageResult.skipReason.isEmpty()) {
			stageObject.insert(QStringLiteral("skipReason"), stageResult.skipReason);
		}
		stageObject.insert(QStringLiteral("inputPath"), stageResult.inputPath);
		stageObject.insert(QStringLiteral("outputPath"), stageResult.outputPath);
		stageObject.insert(QStringLiteral("program"), stageResult.plan.program);
		stageObject.insert(QStringLiteral("arguments"), stringArrayJson(stageResult.plan.arguments));
		stageObject.insert(QStringLiteral("commandLine"), stageResult.plan.commandLine);
		stageObject.insert(QStringLiteral("runnable"), stageResult.plan.isRunnable());
		if (!stageResult.manifestPath.isEmpty()) {
			stageObject.insert(QStringLiteral("manifestPath"), stageResult.manifestPath);
		}
		stageObject.insert(QStringLiteral("started"), stageResult.run.started);
		stageObject.insert(QStringLiteral("exitCode"), stageResult.run.exitCode);
		stageObject.insert(QStringLiteral("durationMs"), static_cast<double>(stageResult.run.durationMs));
		stages.append(stageObject);
	}
	object.insert(QStringLiteral("stages"), stages);

	QJsonArray diagnostics;
	for (const CompilerDiagnostic& diagnostic : result.diagnostics) {
		QJsonObject diagnosticObject;
		diagnosticObject.insert(QStringLiteral("level"), diagnostic.level);
		diagnosticObject.insert(QStringLiteral("message"), diagnostic.message);
		if (!diagnostic.filePath.isEmpty()) {
			diagnosticObject.insert(QStringLiteral("filePath"), diagnostic.filePath);
			diagnosticObject.insert(QStringLiteral("line"), diagnostic.line);
			diagnosticObject.insert(QStringLiteral("column"), diagnostic.column);
		}
		diagnostics.append(diagnosticObject);
	}
	object.insert(QStringLiteral("diagnostics"), diagnostics);
	object.insert(QStringLiteral("registeredOutputPaths"), stringArrayJson(result.registeredOutputPaths));
	object.insert(QStringLiteral("warnings"), stringArrayJson(result.warnings));
	object.insert(QStringLiteral("errors"), stringArrayJson(result.errors));
	return object;
}

QVector<GameLaunchProfile> gameLaunchProfiles()
{
	// Command-line switches below are the documented public options of the
	// respective engines and their source ports:
	// - Quake / QuakeWorld engines: https://quakewiki.org/wiki/Command_line_parameters
	// - Quake II engines: https://www.quake2.com/q2guide/q2cmdline.html
	// - Quake III / ioquake3: https://ioquake3.org/help/command-line-options/
	// - Doom source ports (ZDoom family): https://zdoom.org/wiki/Command_line_parameters
	return {
		launchProfile(
			QStringLiteral("quake-source-port"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake source port"),
			QStringLiteral("idTech2"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake-family engines and source ports. {mod} is a game directory name such as id1."),
			{
				QStringLiteral("-basedir {basedir}"),
				QStringLiteral("-game {mod}"),
				QStringLiteral("+map {map}"),
			},
			true),
		launchProfile(
			QStringLiteral("quake2-source-port"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake II source port"),
			QStringLiteral("idTech2"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake II engines and source ports. {mod} is a game directory name such as baseq2."),
			{
				// Quake-2 qcommon/files.c FS_InitFilesystem reads a cvar; common.c
				// applies early +set commands before filesystem initialization.
				QStringLiteral("+set basedir {basedir}"),
				QStringLiteral("+set game {mod}"),
				QStringLiteral("+map {map}"),
			},
			true),
		launchProfile(
			QStringLiteral("quake3-source-port"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake III source port"),
			QStringLiteral("idTech3"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Quake III-family engines and source ports. {mod} is an fs_game directory such as baseq3."),
			{
				QStringLiteral("+set fs_basepath {basedir}"),
				QStringLiteral("+set fs_game {mod}"),
				QStringLiteral("+devmap {map}"),
			},
			true),
		launchProfile(
			QStringLiteral("doom-source-port"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Doom source port"),
			QStringLiteral("idTech1"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Doom-family source ports. {iwad} is the installation's IWAD, {bsp} the built PWAD, and {warp} the map to start on in -warp's numbers: MAP07 becomes 07 and E2M3 becomes 2 3."),
			{
				QStringLiteral("-iwad {iwad}"),
				QStringLiteral("-file {bsp}"),
				QStringLiteral("-warp {warp}"),
			},
			true),
		launchProfile(
			QStringLiteral("custom"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Custom launch"),
			QStringLiteral("unknown"),
			QCoreApplication::translate("VibeStudioBuildPipeline", "Passes only the caller's extra arguments; nothing is added by VibeStudio."),
			{},
			false),
	};
}

QStringList gameLaunchProfileIds()
{
	QStringList ids;
	for (const GameLaunchProfile& profile : gameLaunchProfiles()) {
		ids.push_back(profile.id);
	}
	return ids;
}

bool gameLaunchProfileForId(const QString& id, GameLaunchProfile* out)
{
	const QString normalized = normalizedId(id);
	if (normalized.isEmpty()) {
		return false;
	}
	for (const GameLaunchProfile& profile : gameLaunchProfiles()) {
		if (normalizedId(profile.id) == normalized) {
			if (out) {
				*out = profile;
			}
			return true;
		}
	}
	return false;
}

QString defaultGameLaunchProfileId(GameEngineFamily family)
{
	switch (family) {
	case GameEngineFamily::IdTech1:
		return QStringLiteral("doom-source-port");
	case GameEngineFamily::IdTech2:
		return QStringLiteral("quake-source-port");
	case GameEngineFamily::IdTech3:
		return QStringLiteral("quake3-source-port");
	case GameEngineFamily::Unknown:
		break;
	}
	return QStringLiteral("custom");
}

QString doomIwadPath(const GameInstallationProfile& installation, const QString& baseDirectory)
{
	// A base directory that is itself a file is the IWAD the caller chose.
	if (!baseDirectory.trimmed().isEmpty() && QFileInfo(baseDirectory).isFile()) {
		return absoluteCleanPath(baseDirectory);
	}
	const QDir root(baseDirectory.trimmed().isEmpty() ? installation.rootPath : baseDirectory);
	for (const QString& package : installation.basePackagePaths) {
		const QString candidate = QFileInfo(package).isAbsolute() ? package : root.filePath(package);
		if (QFileInfo(candidate).isFile()) {
			return absoluteCleanPath(candidate);
		}
	}
	// IWAD names differ in case between stores (DOOM2.WAD, doom2.wad), so the
	// folder is matched without regard to case.
	const QStringList present = root.entryList(QDir::Files);
	for (const QString& expected : gameDefinitionForKey(installation.gameKey).expectedBasePackages) {
		for (const QString& name : present) {
			if (name.compare(expected, Qt::CaseInsensitive) == 0) {
				return absoluteCleanPath(root.filePath(name));
			}
		}
	}
	return {};
}

QString doomWarpArguments(const QString& mapName)
{
	// Doom-family ports read -warp as numbers: the map for MAPxx games, the
	// episode and the map for ExMy games (the -warp switch of vanilla Doom and
	// its source ports, https://doomwiki.org/wiki/Parameters#-warp).
	static const QRegularExpression mapLump(QStringLiteral("^MAP(\\d{1,2})$"), QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression episodeLump(QStringLiteral("^E(\\d)M(\\d)$"), QRegularExpression::CaseInsensitiveOption);
	const QString name = mapName.trimmed();
	if (const QRegularExpressionMatch match = mapLump.match(name); match.hasMatch()) {
		return match.captured(1);
	}
	if (const QRegularExpressionMatch match = episodeLump.match(name); match.hasMatch()) {
		return QStringLiteral("%1 %2").arg(match.captured(1), match.captured(2));
	}
	// Already numbers, or a name the user chose on purpose.
	return name;
}

GameLaunchPlan buildGameLaunchPlan(const GameLaunchRequest& request, const GameInstallationProfile& installation,
	const std::function<bool()>& isCancelled, bool deferArtifactValidation)
{
	GameLaunchPlan plan;
	const QString requestedProfileId = request.launchProfileId.trimmed().isEmpty()
		? defaultGameLaunchProfileId(installation.engineFamily)
		: request.launchProfileId;
	plan.profileFound = gameLaunchProfileForId(requestedProfileId, &plan.profile);
	if (!plan.profileFound) {
		plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "Game launch profile \"%1\" is not known.").arg(requestedProfileId.trimmed());
		plan.commandLine = compilerCommandLineText(plan.program, plan.arguments);
		return plan;
	}

	plan.program = absoluteCleanPath(request.executablePath.trimmed().isEmpty() ? installation.executablePath : request.executablePath);
	if (plan.program.isEmpty()) {
		plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "No game executable is configured for this installation.");
	} else {
		const QFileInfo programInfo(plan.program);
		if (!programInfo.exists()) {
			plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "Game executable does not exist: %1").arg(nativePath(plan.program));
		} else if (!programInfo.isFile()) {
			plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "Game executable path is not a file: %1").arg(nativePath(plan.program));
		} else if (!programInfo.isExecutable()) {
			plan.warnings << QCoreApplication::translate("VibeStudioBuildPipeline", "Game executable is not marked executable: %1").arg(nativePath(plan.program));
		}
	}

	const QString baseDirectory = request.baseDirectory.trimmed().isEmpty() ? installation.rootPath.trimmed() : request.baseDirectory.trimmed();
	const QString mapName = request.mapName.trimmed();
	const QString artifactPath = absoluteCleanPath(request.bspPath.trimmed());
	if (plan.profile.requiresMap && mapName.isEmpty()) {
		plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "This launch profile requires a map name.");
	}

	QMap<QString, QString> tokens;
	tokens.insert(QStringLiteral("map"), mapName);
	tokens.insert(QStringLiteral("warp"), doomWarpArguments(mapName));
	tokens.insert(QStringLiteral("iwad"), doomIwadPath(installation, baseDirectory));
	tokens.insert(QStringLiteral("mod"), request.modDirectory.trimmed());
	tokens.insert(QStringLiteral("basedir"), baseDirectory);
	tokens.insert(QStringLiteral("bsp"), artifactPath);

	for (const QString& argumentTemplate : plan.profile.argumentTemplates) {
		QStringList resolved;
		QStringList missingTokens;
		if (resolveArgumentTemplate(argumentTemplate, tokens, &resolved, &missingTokens)) {
			plan.arguments += resolved;
			continue;
		}
		// No mod folder means the base game, which is a choice rather than
		// something missing, so that argument is dropped without a warning.
		if (missingTokens.isEmpty() || missingTokens == QStringList {QStringLiteral("mod")}) {
			continue;
		}
		plan.warnings << QCoreApplication::translate("VibeStudioBuildPipeline", "Argument \"%1\" was dropped because %2 is not set.")
			.arg(argumentTemplate, missingTokens.join(QStringLiteral(", ")));
	}
	plan.arguments += request.extraArguments;

	if (!baseDirectory.isEmpty() && !QFileInfo(baseDirectory).isDir()) {
		plan.warnings << QCoreApplication::translate("VibeStudioBuildPipeline", "Base directory does not exist: %1").arg(nativePath(baseDirectory));
	}
	if (!request.bspPath.trimmed().isEmpty() && !QFileInfo(request.bspPath.trimmed()).isFile()) {
		plan.warnings << QCoreApplication::translate("VibeStudioBuildPipeline", "Built map artifact does not exist yet: %1").arg(nativePath(request.bspPath.trimmed()));
	}

	plan.workingDirectory = request.workingDirectory.trimmed().isEmpty()
		? (plan.program.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(plan.program).absolutePath()))
		: absoluteCleanPath(request.workingDirectory);
	if (plan.workingDirectory.isEmpty()) {
		plan.warnings << QCoreApplication::translate("VibeStudioBuildPipeline", "No working directory could be resolved for the launch.");
	} else if (!QFileInfo(plan.workingDirectory).isDir()) {
		plan.warnings << QCoreApplication::translate("VibeStudioBuildPipeline", "Launch working directory does not exist: %1").arg(nativePath(plan.workingDirectory));
	}

	if (plan.profile.id == QStringLiteral("doom-source-port") && !request.bspPath.trimmed().isEmpty() &&
		QFileInfo(request.bspPath.trimmed()).suffix().compare("wad", Qt::CaseInsensitive) == 0) {
		if (deferArtifactValidation) { plan.artifactValidationPending = true; }
		else {
			// Preserve the native -warp shorthand while inspecting its actual WAD
			// marker. The explicit MAPxx / ExMy spelling remains unchanged.
			QString marker = mapName;
			static const QRegularExpression numeric(QStringLiteral("^[0-9]{1,2}$"));
			static const QRegularExpression episode(QStringLiteral("^([0-9])\\s+([0-9])$"));
			const auto episodeMatch = episode.match(mapName);
			if (numeric.match(mapName).hasMatch()) { marker = QStringLiteral("MAP%1").arg(mapName.toInt(), 2, 10, QLatin1Char('0')); }
			else if (episodeMatch.hasMatch()) { marker = QStringLiteral("E%1M%2").arg(episodeMatch.captured(1), episodeMatch.captured(2)); }
			const auto nodes = inspectLevelDoomWadNodes(artifactPath, marker.isEmpty() ? QStringList{} : QStringList{marker}, isCancelled);
			plan.cancelled = nodes.cancelled;
			plan.errors += nodes.errors;
			plan.warnings += nodes.warnings;
			for (auto it = nodes.maps.cbegin(); it != nodes.maps.cend(); ++it) { plan.nodeBuild.insert(it.key(), levelDoomNodeReportJson(it.value())); }
			if (nodes.errors.isEmpty() && !nodes.sourceHash.isEmpty()) {
				plan.validatedArtifactPath = artifactPath;
				plan.validatedArtifactHash = nodes.sourceHash;
			}
		}
	}
	if (isCancelled && isCancelled()) { plan.cancelled = true; }
	plan.commandLine = compilerCommandLineText(plan.program, plan.arguments);
	plan.runnable = plan.profileFound && plan.errors.isEmpty() && !plan.program.isEmpty() && !plan.artifactValidationPending && !plan.cancelled;
	return plan;
}

bool startGameLaunch(const GameLaunchPlan& plan, qint64* pidOut, QString* error, const std::function<bool()>& isCancelled)
{
	if (pidOut) {
		*pidOut = 0;
	}
	if (!plan.runnable || plan.artifactValidationPending || plan.cancelled) {
		if (error) {
			*error = plan.errors.isEmpty()
				? QCoreApplication::translate("VibeStudioBuildPipeline", "Game launch plan is not runnable.")
				: plan.errors.join(QStringLiteral("; "));
		}
		return false;
	}

	const auto cancelled = [&] {
		if (isCancelled && isCancelled()) {
			if (error) { *error = QCoreApplication::translate("VibeStudioBuildPipeline", "Game launch cancelled."); }
			return true;
		}
		return false;
	};
	if (cancelled()) { return false; }
	if (!plan.validatedArtifactPath.isEmpty()) {
		QFile file(plan.validatedArtifactPath);
		QCryptographicHash hash(QCryptographicHash::Sha256);
		bool complete = file.open(QIODevice::ReadOnly) && file.size() <= kLevelMapMaxDocumentBytes;
		qint64 read = 0;
		while (complete && !file.atEnd()) {
			if (cancelled()) { return false; }
			const auto chunk = file.read(1024 * 1024);
			read += chunk.size();
			complete = !chunk.isEmpty() && file.error() == QFileDevice::NoError && read <= kLevelMapMaxDocumentBytes;
			if (complete) { hash.addData(chunk); }
		}
		if (!complete || plan.validatedArtifactHash.isEmpty() || hash.result() != plan.validatedArtifactHash) {
			if (error) { *error = QCoreApplication::translate("VibeStudioBuildPipeline", "The validated WAD changed or is unavailable. Prepare the launch again before testing."); }
			return false;
		}
	}
	if (cancelled()) { return false; }
	qint64 pid = 0;
	const bool started = QProcess::startDetached(plan.program, plan.arguments, plan.workingDirectory, &pid);
	if (!started) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioBuildPipeline", "Failed to start the game process: %1").arg(nativePath(plan.program));
		}
		return false;
	}
	if (pidOut) {
		*pidOut = pid;
	}
	return true;
}

QString gameLaunchPlanText(const GameLaunchPlan& plan)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Game launch plan");
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Profile: %1").arg(plan.profileFound ? plan.profile.id : QCoreApplication::translate("VibeStudioBuildPipeline", "(unknown)"));
	if (plan.profileFound) {
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Name: %1").arg(plan.profile.displayName);
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Engine: %1").arg(plan.profile.engineFamily);
	}
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "State: %1").arg(operationStateId(plan.state()));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Runnable: %1").arg(plan.runnable ? QCoreApplication::translate("VibeStudioBuildPipeline", "yes") : QCoreApplication::translate("VibeStudioBuildPipeline", "no"));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Program: %1").arg(nativePath(plan.program));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Working directory: %1").arg(nativePath(plan.workingDirectory));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Command line: %1").arg(plan.commandLine);
	if (plan.artifactValidationPending) { lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Node validation is required before launch."); }
	if (!plan.validatedArtifactHash.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Validated WAD SHA-256: %1").arg(QString::fromLatin1(plan.validatedArtifactHash.toHex()));
	}
	if (!plan.warnings.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Warnings");
		for (const QString& warning : plan.warnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	if (!plan.errors.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Errors");
		for (const QString& error : plan.errors) {
			lines << QStringLiteral("- %1").arg(error);
		}
	}
	return lines.join('\n');
}

QJsonObject gameLaunchPlanJson(const GameLaunchPlan& plan)
{
	QJsonObject object;
	object.insert(QStringLiteral("profileId"), plan.profile.id);
	object.insert(QStringLiteral("displayName"), plan.profile.displayName);
	object.insert(QStringLiteral("engineFamily"), plan.profile.engineFamily);
	object.insert(QStringLiteral("profileFound"), plan.profileFound);
	object.insert(QStringLiteral("runnable"), plan.runnable);
	object.insert(QStringLiteral("artifactValidationPending"), plan.artifactValidationPending);
	object.insert(QStringLiteral("cancelled"), plan.cancelled);
	object.insert(QStringLiteral("validatedArtifactPath"), plan.validatedArtifactPath);
	object.insert(QStringLiteral("validatedArtifactSha256"), QString::fromLatin1(plan.validatedArtifactHash.toHex()));
	object.insert(QStringLiteral("nodeBuild"), plan.nodeBuild);
	object.insert(QStringLiteral("state"), operationStateId(plan.state()));
	object.insert(QStringLiteral("program"), plan.program);
	object.insert(QStringLiteral("arguments"), stringArrayJson(plan.arguments));
	object.insert(QStringLiteral("commandLine"), plan.commandLine);
	object.insert(QStringLiteral("workingDirectory"), plan.workingDirectory);
	object.insert(QStringLiteral("warnings"), stringArrayJson(plan.warnings));
	object.insert(QStringLiteral("errors"), stringArrayJson(plan.errors));
	return object;
}

namespace {

// Size first, then a streamed SHA-256, so an unchanged multi-megabyte BSP is
// recognised without holding two copies in memory.
bool sameFileBytes(const QString& left, const QString& right)
{
	const QFileInfo leftInfo(left);
	const QFileInfo rightInfo(right);
	if (!leftInfo.isFile() || !rightInfo.isFile() || leftInfo.size() != rightInfo.size()) {
		return false;
	}
	auto digest = [](const QString& path) {
		QFile file(path);
		QCryptographicHash hash(QCryptographicHash::Sha256);
		if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) {
			return QByteArray();
		}
		return hash.result();
	};
	const QByteArray leftDigest = digest(left);
	return !leftDigest.isEmpty() && leftDigest == digest(right);
}

} // namespace

bool GameMapDeployPlan::upToDate() const
{
	if (!required || files.isEmpty()) {
		return false;
	}
	for (const GameMapDeployFile& file : files) {
		if (!file.upToDate) {
			return false;
		}
	}
	return true;
}

bool GameMapDeployPlan::runnable() const
{
	return required && allowed && errors.isEmpty() && !files.isEmpty();
}

GameMapDeployPlan planGameMapDeploy(const GameInstallationProfile& installation, const QString& modDirectory, const QString& builtMapPath)
{
	GameMapDeployPlan plan;
	// Doom-family ports read the built PWAD in place with -file.
	plan.required = installation.engineFamily == GameEngineFamily::IdTech2 || installation.engineFamily == GameEngineFamily::IdTech3;
	if (!plan.required) {
		return plan;
	}
	plan.allowed = !installation.readOnly;
	if (!plan.allowed) {
		plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "The installation profile is read-only, so the built map cannot be copied into it. Allow test maps for this installation first.");
	}

	const QString builtMap = absoluteCleanPath(builtMapPath);
	if (builtMap.isEmpty()) {
		plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "No built map was given to copy.");
	} else if (!QFileInfo(builtMap).isFile()) {
		plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "The built map does not exist yet: %1").arg(nativePath(builtMap));
	}

	// A single folder name keeps every write inside the installation root.
	plan.gameDirectory = modDirectory.trimmed().isEmpty() ? defaultGameDirectory(installation) : modDirectory.trimmed();
	static const QRegularExpression folderName(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.-]*$"));
	const bool folderValid = folderName.match(plan.gameDirectory).hasMatch() && !plan.gameDirectory.contains(QStringLiteral(".."));
	if (plan.gameDirectory.isEmpty()) {
		plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "Name the game folder the map goes into, for example id1.");
	} else if (!folderValid) {
		plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "The game folder must be a single folder name inside the installation: %1").arg(plan.gameDirectory);
	}
	const QString root = absoluteCleanPath(installation.rootPath);
	const bool rootValid = !root.isEmpty() && QFileInfo(root).isDir();
	if (!rootValid) {
		plan.errors << QCoreApplication::translate("VibeStudioBuildPipeline", "The installation folder does not exist: %1").arg(nativePath(root));
	}
	// Without a map, a root, and a valid folder there is no destination to show.
	if (builtMap.isEmpty() || !rootValid || !folderValid) {
		return plan;
	}

	plan.targetDirectory = QDir::cleanPath(QDir(root).filePath(plan.gameDirectory + QStringLiteral("/maps")));
	if (!QFileInfo(QDir(root).filePath(plan.gameDirectory)).isDir()) {
		plan.warnings << QCoreApplication::translate("VibeStudioBuildPipeline", "The game folder %1 does not exist yet and will be created.").arg(plan.gameDirectory);
	}
	// VibeMap2 writes coloured light to .lit and deluxemaps to .lux beside
	// the BSP; engines look for them beside the copied map.
	const QFileInfo mapInfo(builtMap);
	QStringList sources {builtMap};
	for (const QString& companion : {QStringLiteral("lit"), QStringLiteral("lux")}) {
		const QString path = QDir(mapInfo.absolutePath()).filePath(QStringLiteral("%1.%2").arg(mapInfo.completeBaseName(), companion));
		if (QFileInfo(path).isFile()) {
			sources << QDir::cleanPath(path);
		}
	}
	for (const QString& source : sources) {
		GameMapDeployFile file;
		file.sourcePath = source;
		file.destinationPath = QDir(plan.targetDirectory).filePath(QFileInfo(source).fileName());
		if (QFileInfo(file.destinationPath).exists()) {
			file.upToDate = sameFileBytes(source, file.destinationPath);
			file.replacesExisting = !file.upToDate;
		}
		plan.files.push_back(file);
	}
	return plan;
}

bool deployGameMap(const GameMapDeployPlan& plan, QStringList* written, QString* error)
{
	auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!plan.runnable()) {
		return fail(plan.errors.isEmpty() ? QCoreApplication::translate("VibeStudioBuildPipeline", "The map copy plan is not runnable.") : plan.errors.join(QStringLiteral(" ")));
	}
	if (!QDir().mkpath(plan.targetDirectory)) {
		return fail(QCoreApplication::translate("VibeStudioBuildPipeline", "Unable to create %1.").arg(nativePath(plan.targetDirectory)));
	}
	for (const GameMapDeployFile& file : plan.files) {
		if (file.upToDate) {
			continue;
		}
		QFile source(file.sourcePath);
		if (!source.open(QIODevice::ReadOnly)) {
			return fail(QCoreApplication::translate("VibeStudioBuildPipeline", "Unable to read %1: %2").arg(nativePath(file.sourcePath), source.errorString()));
		}
		QSaveFile destination(file.destinationPath);
		if (!destination.open(QIODevice::WriteOnly)) {
			return fail(QCoreApplication::translate("VibeStudioBuildPipeline", "Unable to write %1: %2").arg(nativePath(file.destinationPath), destination.errorString()));
		}
		while (!source.atEnd()) {
			const QByteArray chunk = source.read(1 << 20);
			if (chunk.isEmpty() && source.error() != QFileDevice::NoError) {
				destination.cancelWriting();
				return fail(QCoreApplication::translate("VibeStudioBuildPipeline", "Unable to read %1: %2").arg(nativePath(file.sourcePath), source.errorString()));
			}
			if (destination.write(chunk) != chunk.size()) {
				destination.cancelWriting();
				return fail(QCoreApplication::translate("VibeStudioBuildPipeline", "Unable to write %1: %2").arg(nativePath(file.destinationPath), destination.errorString()));
			}
		}
		if (!destination.commit()) {
			return fail(QCoreApplication::translate("VibeStudioBuildPipeline", "Unable to write %1: %2").arg(nativePath(file.destinationPath), destination.errorString()));
		}
		if (written) {
			written->push_back(file.destinationPath);
		}
	}
	return true;
}

QString gameMapDeployPlanText(const GameMapDeployPlan& plan)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Test map copy");
	if (!plan.required) {
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Not needed: the game loads the built file directly.");
		return lines.join('\n');
	}
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Game folder: %1").arg(plan.gameDirectory.isEmpty() ? QCoreApplication::translate("VibeStudioBuildPipeline", "(not resolved)") : plan.gameDirectory);
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Target: %1").arg(nativePath(plan.targetDirectory));
	lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Allowed: %1").arg(plan.allowed ? QCoreApplication::translate("VibeStudioBuildPipeline", "yes") : QCoreApplication::translate("VibeStudioBuildPipeline", "no, the installation is read-only"));
	for (const GameMapDeployFile& file : plan.files) {
		const QString status = file.upToDate ? QCoreApplication::translate("VibeStudioBuildPipeline", "already up to date")
			: (file.replacesExisting ? QCoreApplication::translate("VibeStudioBuildPipeline", "replaces the existing file") : QCoreApplication::translate("VibeStudioBuildPipeline", "new"));
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "- %1 -> %2 (%3)").arg(QFileInfo(file.sourcePath).fileName(), nativePath(file.destinationPath), status);
	}
	if (!plan.warnings.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Warnings");
		for (const QString& warning : plan.warnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	if (!plan.errors.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioBuildPipeline", "Errors");
		for (const QString& message : plan.errors) {
			lines << QStringLiteral("- %1").arg(message);
		}
	}
	return lines.join('\n');
}

QJsonObject gameMapDeployPlanJson(const GameMapDeployPlan& plan)
{
	QJsonObject object;
	object.insert(QStringLiteral("required"), plan.required);
	object.insert(QStringLiteral("allowed"), plan.allowed);
	object.insert(QStringLiteral("runnable"), plan.runnable());
	object.insert(QStringLiteral("upToDate"), plan.upToDate());
	object.insert(QStringLiteral("gameDirectory"), plan.gameDirectory);
	object.insert(QStringLiteral("targetDirectory"), plan.targetDirectory);
	QJsonArray files;
	for (const GameMapDeployFile& file : plan.files) {
		QJsonObject entry;
		entry.insert(QStringLiteral("source"), file.sourcePath);
		entry.insert(QStringLiteral("destination"), file.destinationPath);
		entry.insert(QStringLiteral("replacesExisting"), file.replacesExisting);
		entry.insert(QStringLiteral("upToDate"), file.upToDate);
		files.push_back(entry);
	}
	object.insert(QStringLiteral("files"), files);
	object.insert(QStringLiteral("warnings"), stringArrayJson(plan.warnings));
	object.insert(QStringLiteral("errors"), stringArrayJson(plan.errors));
	return object;
}

} // namespace vibestudio
