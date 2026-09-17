#include "core/build_pipeline.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>

namespace vibestudio {

namespace {

QString pipelineText(const char* source)
{
	return QCoreApplication::translate("VibeStudioBuildPipeline", source);
}

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
	return path.isEmpty() ? pipelineText("(not resolved)") : QDir::toNativeSeparators(path);
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
		appendUnique(&result->warnings, pipelineText("Stage \"%1\": %2").arg(stage.id, warning));
	}
	bool hadRealError = false;
	const QString missingInput = missingInputErrorText();
	for (const QString& error : errors) {
		if (allowMissingInputDowngrade && error.trimmed() == missingInput) {
			appendUnique(&result->warnings, pipelineText("Stage \"%1\" input %2 does not exist yet; it is produced by an earlier stage of this pipeline.").arg(stage.id, nativePath(inputPath)));
			continue;
		}
		const QString entry = pipelineText("Stage \"%1\": %2").arg(stage.id, error);
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
		result.errors << pipelineText("Build pipeline \"%1\" is not known.").arg(request.pipelineId.trimmed());
		result.state = OperationState::Failed;
		return result;
	}

	if (result.inputPath.isEmpty()) {
		result.errors << pipelineText("Pipeline input path is required.");
	} else {
		if (!QFileInfo(result.inputPath).isFile()) {
			result.errors << pipelineText("Pipeline input file does not exist: %1").arg(nativePath(result.inputPath));
		}
		if (!extensionMatches(result.inputPath, result.pipeline.inputExtensions)) {
			result.warnings << pipelineText("Pipeline input extension does not match the expected source type for \"%1\".").arg(result.pipeline.id);
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
			skipReasons[index] = pipelineText("Stage was disabled for this run.");
			continue;
		}
		if (stage.optional && !stage.enabledByDefault) {
			skipped[index] = true;
			skipReasons[index] = pipelineText("Optional stage is disabled by default for this pipeline.");
			continue;
		}
		if (!compilerProfileForId(stage.profileId, &profiles[index])) {
			skipped[index] = true;
			skipReasons[index] = pipelineText("Compiler profile \"%1\" is not registered in this build.").arg(stage.profileId);
			appendUnique(&result.warnings, pipelineText("Stage \"%1\" was skipped: %2").arg(stage.id, skipReasons[index]));
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
		appendUnique(&result.warnings, pipelineText("No stage in this pipeline accepts an explicit output path; the requested output path is ignored."));
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
			appendUnique(&result.warnings, pipelineText("Stage \"%1\" now consumes %2 because stage \"%3\" is not part of this run.").arg(stage.id, nativePath(stageInputPath), stage.inputFromStageId));
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
		appendUnique(&result.warnings, pipelineText("No stage of this pipeline is enabled; nothing would be compiled."));
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
			pipelineText("Quake full compile"),
			QStringLiteral("idTech2"),
			pipelineText("Runs the complete Quake-family loop: BSP, visibility, and lighting through ericw-tools."),
			{QStringLiteral("map")},
			QStringLiteral("bsp"),
			{
				pipelineStage(
					QStringLiteral("qbsp"),
					QStringLiteral("ericw-qbsp"),
					pipelineText("QBSP"),
					pipelineText("Compiles the .map source into a Quake-family BSP."),
					QString(),
					false,
					true),
				pipelineStage(
					QStringLiteral("vis"),
					QStringLiteral("ericw-vis"),
					pipelineText("VIS"),
					pipelineText("Computes the potentially visible set for the compiled BSP, in place."),
					QStringLiteral("qbsp"),
					true,
					true),
				pipelineStage(
					QStringLiteral("light"),
					QStringLiteral("ericw-light"),
					pipelineText("LIGHT"),
					pipelineText("Computes lightmaps and light data for the compiled BSP, in place."),
					QStringLiteral("vis"),
					true,
					true),
			}),
		pipelineDescriptor(
			QStringLiteral("quake-fast"),
			pipelineText("Quake fast iteration"),
			QStringLiteral("idTech2"),
			pipelineText("Quick edit/test loop: BSP then lighting. Visibility is disabled by default; pass fast-mode switches through stageExtraArguments."),
			{QStringLiteral("map")},
			QStringLiteral("bsp"),
			{
				pipelineStage(
					QStringLiteral("qbsp"),
					QStringLiteral("ericw-qbsp"),
					pipelineText("QBSP"),
					pipelineText("Compiles the .map source into a Quake-family BSP."),
					QString(),
					false,
					true),
				pipelineStage(
					QStringLiteral("vis"),
					QStringLiteral("ericw-vis"),
					pipelineText("VIS"),
					pipelineText("Visibility processing, disabled by default for fast iteration."),
					QStringLiteral("qbsp"),
					true,
					false),
				pipelineStage(
					QStringLiteral("light"),
					QStringLiteral("ericw-light"),
					pipelineText("LIGHT"),
					pipelineText("Lighting pass; the caller supplies fast-mode arguments."),
					QStringLiteral("vis"),
					true,
					true),
			}),
		pipelineDescriptor(
			QStringLiteral("quake-bsp-only"),
			pipelineText("Quake BSP only"),
			QStringLiteral("idTech2"),
			pipelineText("Compiles geometry only, without visibility or lighting."),
			{QStringLiteral("map")},
			QStringLiteral("bsp"),
			{
				pipelineStage(
					QStringLiteral("qbsp"),
					QStringLiteral("ericw-qbsp"),
					pipelineText("QBSP"),
					pipelineText("Compiles the .map source into a Quake-family BSP."),
					QString(),
					false,
					true),
			}),
		pipelineDescriptor(
			QStringLiteral("quake3-full"),
			pipelineText("Quake III full compile"),
			QStringLiteral("idTech3"),
			pipelineText("Runs the complete Quake III-family loop: BSP, visibility, and lighting through q3map2."),
			{QStringLiteral("map")},
			QStringLiteral("bsp"),
			{
				pipelineStage(
					QStringLiteral("bsp"),
					QStringLiteral("q3map2-bsp"),
					pipelineText("BSP"),
					pipelineText("Builds the Quake III-family BSP from the .map source."),
					QString(),
					false,
					true),
				pipelineStage(
					QStringLiteral("vis"),
					QStringLiteral("q3map2-vis"),
					pipelineText("VIS"),
					pipelineText("Computes the potentially visible set for the compiled BSP, in place."),
					QStringLiteral("bsp"),
					true,
					true),
				pipelineStage(
					QStringLiteral("light"),
					QStringLiteral("q3map2-light"),
					pipelineText("LIGHT"),
					pipelineText("Computes lightmaps for the compiled BSP, in place."),
					QStringLiteral("vis"),
					true,
					true),
			}),
		pipelineDescriptor(
			QStringLiteral("quake3-bsp-only"),
			pipelineText("Quake III BSP only"),
			QStringLiteral("idTech3"),
			pipelineText("Compiles Quake III-family geometry only, without visibility or lighting."),
			{QStringLiteral("map")},
			QStringLiteral("bsp"),
			{
				pipelineStage(
					QStringLiteral("bsp"),
					QStringLiteral("q3map2-bsp"),
					pipelineText("BSP"),
					pipelineText("Builds the Quake III-family BSP from the .map source."),
					QString(),
					false,
					true),
			}),
		pipelineDescriptor(
			QStringLiteral("doom-zdbsp"),
			pipelineText("Doom nodes (ZDBSP)"),
			QStringLiteral("idTech1"),
			pipelineText("Builds Doom-family map nodes with ZDBSP."),
			{QStringLiteral("wad")},
			QStringLiteral("wad"),
			{
				pipelineStage(
					QStringLiteral("nodes"),
					QStringLiteral("zdbsp-nodes"),
					pipelineText("Nodes"),
					pipelineText("Builds nodes, blockmap, and reject data for a Doom-family WAD."),
					QString(),
					false,
					true),
			}),
		pipelineDescriptor(
			QStringLiteral("doom-zokumbsp"),
			pipelineText("Doom nodes (ZokumBSP)"),
			QStringLiteral("idTech1"),
			pipelineText("Builds Doom-family map nodes with ZokumBSP."),
			{QStringLiteral("wad")},
			QStringLiteral("wad"),
			{
				pipelineStage(
					QStringLiteral("nodes"),
					QStringLiteral("zokumbsp-nodes"),
					pipelineText("Nodes"),
					pipelineText("Builds nodes, blockmap, and reject data for a Doom-family WAD."),
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
		appendUnique(&result.warnings, pipelineText("Could not create the manifest directory %1; stage manifests will not be written.").arg(nativePath(manifestDirectory)));
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
				? pipelineText("Pipeline was cancelled before this stage started.")
				: pipelineText("An earlier stage failed and the pipeline stops on failure.");
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

		// A stage that exited 0 still produced its artifacts. ericw-tools prints non-fatal
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
	lines << pipelineText("Build pipeline result");
	lines << pipelineText("Pipeline: %1").arg(result.pipeline.id.isEmpty() ? pipelineText("(unknown)") : result.pipeline.id);
	if (!result.pipeline.displayName.isEmpty()) {
		lines << pipelineText("Name: %1").arg(result.pipeline.displayName);
	}
	if (!result.pipeline.engineFamily.isEmpty()) {
		lines << pipelineText("Engine: %1").arg(result.pipeline.engineFamily);
	}
	lines << pipelineText("State: %1").arg(operationStateId(result.state));
	lines << pipelineText("Succeeded: %1").arg(result.succeeded() ? pipelineText("yes") : pipelineText("no"));
	lines << pipelineText("Dry run: %1").arg(result.dryRun ? pipelineText("yes") : pipelineText("no"));
	lines << pipelineText("Cancelled: %1").arg(result.cancelled ? pipelineText("yes") : pipelineText("no"));
	lines << pipelineText("Input: %1").arg(nativePath(result.inputPath));
	lines << pipelineText("Final output: %1").arg(nativePath(result.finalOutputPath));
	lines << pipelineText("Stages: %1 planned, %2 completed, %3 failed, %4 skipped")
		.arg(result.plannedStageCount)
		.arg(result.completedStageCount)
		.arg(result.failedStageCount)
		.arg(result.skippedStageCount);
	lines << pipelineText("Duration: %1").arg(result.durationMs >= 0 ? pipelineText("%1 ms").arg(result.durationMs) : pipelineText("not run"));

	for (const BuildPipelineStageResult& stageResult : result.stages) {
		lines << QString();
		lines << pipelineText("Stage %1 (%2)").arg(stageResult.stage.id, stageResult.stage.profileId);
		lines << pipelineText("  State: %1").arg(operationStateId(stageResult.state));
		if (stageResult.skipped) {
			lines << pipelineText("  Skipped: %1").arg(stageResult.skipReason);
			continue;
		}
		lines << pipelineText("  Input: %1").arg(nativePath(stageResult.inputPath));
		lines << pipelineText("  Output: %1").arg(nativePath(stageResult.outputPath));
		lines << pipelineText("  Command line: %1").arg(stageResult.plan.commandLine);
		if (!stageResult.manifestPath.isEmpty()) {
			lines << pipelineText("  Manifest: %1").arg(nativePath(stageResult.manifestPath));
		}
		if (stageResult.run.exitCode >= 0) {
			lines << pipelineText("  Exit code: %1").arg(stageResult.run.exitCode);
		}
	}

	if (!result.registeredOutputPaths.isEmpty()) {
		lines << QString();
		lines << pipelineText("Registered outputs");
		for (const QString& output : result.registeredOutputPaths) {
			lines << QStringLiteral("- %1").arg(nativePath(output));
		}
	}
	if (!result.warnings.isEmpty()) {
		lines << QString();
		lines << pipelineText("Warnings");
		for (const QString& warning : result.warnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	if (!result.errors.isEmpty()) {
		lines << QString();
		lines << pipelineText("Errors");
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
			pipelineText("Quake source port"),
			QStringLiteral("idTech2"),
			pipelineText("Quake-family engines and source ports. {mod} is a game directory name such as id1."),
			{
				QStringLiteral("-basedir {basedir}"),
				QStringLiteral("-game {mod}"),
				QStringLiteral("+map {map}"),
			},
			true),
		launchProfile(
			QStringLiteral("quake2-source-port"),
			pipelineText("Quake II source port"),
			QStringLiteral("idTech2"),
			pipelineText("Quake II engines and source ports. {mod} is a game directory name such as baseq2."),
			{
				QStringLiteral("-basedir {basedir}"),
				QStringLiteral("+set game {mod}"),
				QStringLiteral("+map {map}"),
			},
			true),
		launchProfile(
			QStringLiteral("quake3-source-port"),
			pipelineText("Quake III source port"),
			QStringLiteral("idTech3"),
			pipelineText("Quake III-family engines and source ports. {mod} is an fs_game directory such as baseq3."),
			{
				QStringLiteral("+set fs_basepath {basedir}"),
				QStringLiteral("+set fs_game {mod}"),
				QStringLiteral("+devmap {map}"),
			},
			true),
		launchProfile(
			QStringLiteral("doom-source-port"),
			pipelineText("Doom source port"),
			QStringLiteral("idTech1"),
			pipelineText("Doom-family source ports. {basedir} is the IWAD path, {bsp} the built PWAD, {map} a warp target such as MAP01."),
			{
				QStringLiteral("-iwad {basedir}"),
				QStringLiteral("-file {bsp}"),
				QStringLiteral("-warp {map}"),
			},
			true),
		launchProfile(
			QStringLiteral("custom"),
			pipelineText("Custom launch"),
			QStringLiteral("unknown"),
			pipelineText("Passes only the caller's extra arguments; nothing is added by VibeStudio."),
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

GameLaunchPlan buildGameLaunchPlan(const GameLaunchRequest& request, const GameInstallationProfile& installation)
{
	GameLaunchPlan plan;
	const QString requestedProfileId = request.launchProfileId.trimmed().isEmpty()
		? defaultGameLaunchProfileId(installation.engineFamily)
		: request.launchProfileId;
	plan.profileFound = gameLaunchProfileForId(requestedProfileId, &plan.profile);
	if (!plan.profileFound) {
		plan.errors << pipelineText("Game launch profile \"%1\" is not known.").arg(requestedProfileId.trimmed());
		plan.commandLine = compilerCommandLineText(plan.program, plan.arguments);
		return plan;
	}

	plan.program = absoluteCleanPath(request.executablePath.trimmed().isEmpty() ? installation.executablePath : request.executablePath);
	if (plan.program.isEmpty()) {
		plan.errors << pipelineText("No game executable is configured for this installation.");
	} else {
		const QFileInfo programInfo(plan.program);
		if (!programInfo.exists()) {
			plan.errors << pipelineText("Game executable does not exist: %1").arg(nativePath(plan.program));
		} else if (!programInfo.isFile()) {
			plan.errors << pipelineText("Game executable path is not a file: %1").arg(nativePath(plan.program));
		} else if (!programInfo.isExecutable()) {
			plan.warnings << pipelineText("Game executable is not marked executable: %1").arg(nativePath(plan.program));
		}
	}

	const QString baseDirectory = request.baseDirectory.trimmed().isEmpty() ? installation.rootPath.trimmed() : request.baseDirectory.trimmed();
	const QString mapName = request.mapName.trimmed();
	if (plan.profile.requiresMap && mapName.isEmpty()) {
		plan.errors << pipelineText("This launch profile requires a map name.");
	}

	QMap<QString, QString> tokens;
	tokens.insert(QStringLiteral("map"), mapName);
	tokens.insert(QStringLiteral("mod"), request.modDirectory.trimmed());
	tokens.insert(QStringLiteral("basedir"), baseDirectory);
	tokens.insert(QStringLiteral("bsp"), request.bspPath.trimmed());

	for (const QString& argumentTemplate : plan.profile.argumentTemplates) {
		QStringList resolved;
		QStringList missingTokens;
		if (resolveArgumentTemplate(argumentTemplate, tokens, &resolved, &missingTokens)) {
			plan.arguments += resolved;
			continue;
		}
		if (missingTokens.isEmpty()) {
			continue;
		}
		plan.warnings << pipelineText("Argument \"%1\" was dropped because %2 is not set.")
			.arg(argumentTemplate, missingTokens.join(QStringLiteral(", ")));
	}
	plan.arguments += request.extraArguments;

	if (!baseDirectory.isEmpty() && !QFileInfo(baseDirectory).isDir()) {
		plan.warnings << pipelineText("Base directory does not exist: %1").arg(nativePath(baseDirectory));
	}
	if (!request.bspPath.trimmed().isEmpty() && !QFileInfo(request.bspPath.trimmed()).isFile()) {
		plan.warnings << pipelineText("Built map artifact does not exist yet: %1").arg(nativePath(request.bspPath.trimmed()));
	}

	plan.workingDirectory = request.workingDirectory.trimmed().isEmpty()
		? (plan.program.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(plan.program).absolutePath()))
		: absoluteCleanPath(request.workingDirectory);
	if (plan.workingDirectory.isEmpty()) {
		plan.warnings << pipelineText("No working directory could be resolved for the launch.");
	} else if (!QFileInfo(plan.workingDirectory).isDir()) {
		plan.warnings << pipelineText("Launch working directory does not exist: %1").arg(nativePath(plan.workingDirectory));
	}

	plan.commandLine = compilerCommandLineText(plan.program, plan.arguments);
	plan.runnable = plan.profileFound && plan.errors.isEmpty() && !plan.program.isEmpty();
	return plan;
}

bool startGameLaunch(const GameLaunchPlan& plan, qint64* pidOut, QString* error)
{
	if (pidOut) {
		*pidOut = 0;
	}
	if (!plan.runnable) {
		if (error) {
			*error = plan.errors.isEmpty()
				? pipelineText("Game launch plan is not runnable.")
				: plan.errors.join(QStringLiteral("; "));
		}
		return false;
	}

	qint64 pid = 0;
	const bool started = QProcess::startDetached(plan.program, plan.arguments, plan.workingDirectory, &pid);
	if (!started) {
		if (error) {
			*error = pipelineText("Failed to start the game process: %1").arg(nativePath(plan.program));
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
	lines << pipelineText("Game launch plan");
	lines << pipelineText("Profile: %1").arg(plan.profileFound ? plan.profile.id : pipelineText("(unknown)"));
	if (plan.profileFound) {
		lines << pipelineText("Name: %1").arg(plan.profile.displayName);
		lines << pipelineText("Engine: %1").arg(plan.profile.engineFamily);
	}
	lines << pipelineText("State: %1").arg(operationStateId(plan.state()));
	lines << pipelineText("Runnable: %1").arg(plan.runnable ? pipelineText("yes") : pipelineText("no"));
	lines << pipelineText("Program: %1").arg(nativePath(plan.program));
	lines << pipelineText("Working directory: %1").arg(nativePath(plan.workingDirectory));
	lines << pipelineText("Command line: %1").arg(plan.commandLine);
	if (!plan.warnings.isEmpty()) {
		lines << pipelineText("Warnings");
		for (const QString& warning : plan.warnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	if (!plan.errors.isEmpty()) {
		lines << pipelineText("Errors");
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
	object.insert(QStringLiteral("state"), operationStateId(plan.state()));
	object.insert(QStringLiteral("program"), plan.program);
	object.insert(QStringLiteral("arguments"), stringArrayJson(plan.arguments));
	object.insert(QStringLiteral("commandLine"), plan.commandLine);
	object.insert(QStringLiteral("workingDirectory"), plan.workingDirectory);
	object.insert(QStringLiteral("warnings"), stringArrayJson(plan.warnings));
	object.insert(QStringLiteral("errors"), stringArrayJson(plan.errors));
	return object;
}

} // namespace vibestudio
