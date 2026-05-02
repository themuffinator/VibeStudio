#include "core/ai_workflows.h"

#include "core/compiler_profiles.h"
#include "core/package_archive.h"
#include "core/project_manifest.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>

namespace vibestudio {

namespace {

QString workflowText(const char* source)
{
	return QCoreApplication::translate("VibeStudioAiWorkflows", source);
}

QString workflowId()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QJsonArray stringArrayJson(const QStringList& values)
{
	QJsonArray array;
	for (const QString& value : values) {
		array.append(value);
	}
	return array;
}

QStringList firstNonEmptyLines(const QString& text, int limit)
{
	QStringList lines;
	for (const QString& line : text.split('\n')) {
		const QString trimmed = line.trimmed();
		if (!trimmed.isEmpty()) {
			lines.push_back(trimmed);
		}
		if (lines.size() >= limit) {
			break;
		}
	}
	return lines;
}

bool containsAny(const QString& text, const QStringList& needles)
{
	for (const QString& needle : needles) {
		if (text.contains(needle, Qt::CaseInsensitive)) {
			return true;
		}
	}
	return false;
}

QString selectedProviderId(const AiAutomationPreferences& preferences, const QString& requestedProviderId, const QString& capabilityId)
{
	AiConnectorDescriptor connector;
	if (!requestedProviderId.trimmed().isEmpty() && aiConnectorForId(requestedProviderId, &connector) && connector.capabilities.contains(normalizedAiId(capabilityId))) {
		return connector.id;
	}

	const AiAutomationPreferences normalized = normalizedAiAutomationPreferences(preferences);
	QString preferred;
	const QString capability = normalizedAiId(capabilityId);
	if (capability == QStringLiteral("coding")) {
		preferred = normalized.preferredCodingConnectorId;
	} else if (capability == QStringLiteral("vision")) {
		preferred = normalized.preferredVisionConnectorId;
	} else if (capability == QStringLiteral("image")) {
		preferred = normalized.preferredImageConnectorId;
	} else if (capability == QStringLiteral("audio")) {
		preferred = normalized.preferredAudioConnectorId;
	} else if (capability == QStringLiteral("voice")) {
		preferred = normalized.preferredVoiceConnectorId;
	} else if (capability == QStringLiteral("three-d")) {
		preferred = normalized.preferredThreeDConnectorId;
	} else if (capability == QStringLiteral("embeddings")) {
		preferred = normalized.preferredEmbeddingsConnectorId;
	} else {
		preferred = normalized.preferredReasoningConnectorId;
	}
	if (!preferred.isEmpty() && aiConnectorSupportsCapability(preferred, capability)) {
		return preferred;
	}
	if (normalized.aiFreeMode && aiConnectorSupportsCapability(QStringLiteral("local-offline"), capability)) {
		return QStringLiteral("local-offline");
	}
	return aiConnectorSupportsCapability(defaultAiReasoningConnectorId(), capability) ? defaultAiReasoningConnectorId() : QStringLiteral("local-offline");
}

QString selectedModelId(const AiAutomationPreferences& preferences, const QString& providerId, const QString& requestedModelId, const QString& capabilityId)
{
	const QString capability = normalizedAiId(capabilityId);
	if (!requestedModelId.trimmed().isEmpty() && aiModelSupportsCapability(requestedModelId, capability)) {
		return normalizedAiModelId(requestedModelId, capability);
	}

	const AiAutomationPreferences normalized = normalizedAiAutomationPreferences(preferences);
	QString preferred;
	if (capability == QStringLiteral("coding")) {
		preferred = normalized.preferredCodingModelId;
	} else if (capability == QStringLiteral("vision")) {
		preferred = normalized.preferredVisionModelId;
	} else if (capability == QStringLiteral("image")) {
		preferred = normalized.preferredImageModelId;
	} else if (capability == QStringLiteral("audio")) {
		preferred = normalized.preferredAudioModelId;
	} else if (capability == QStringLiteral("voice")) {
		preferred = normalized.preferredVoiceModelId;
	} else if (capability == QStringLiteral("three-d")) {
		preferred = normalized.preferredThreeDModelId;
	} else if (capability == QStringLiteral("embeddings")) {
		preferred = normalized.preferredEmbeddingsModelId;
	} else {
		preferred = normalized.preferredTextModelId;
	}
	if (!preferred.isEmpty() && aiModelSupportsCapability(preferred, capability)) {
		return normalizedAiModelId(preferred, capability);
	}
	return defaultAiModelId(providerId, capability);
}

AiWorkflowToolCall toolCall(const QString& toolId, const QString& summary, const QStringList& inputs = {}, const QStringList& outputs = {}, const QStringList& warnings = {})
{
	AiWorkflowToolCall call;
	call.toolId = normalizedAiId(toolId);
	call.summary = summary;
	call.inputs = inputs;
	call.outputs = outputs;
	call.warnings = warnings;
	AiToolDescriptor descriptor;
	if (aiToolForId(call.toolId, &descriptor)) {
		call.requiresApproval = descriptor.requiresApproval;
		call.writesFiles = descriptor.writesFiles;
	}
	call.state = warnings.isEmpty() ? OperationState::Completed : OperationState::Warning;
	return call;
}

void applyCredentialWarning(AiWorkflowManifest* manifest, const AiAutomationPreferences& preferences)
{
	if (!manifest) {
		return;
	}
	AiConnectorDescriptor connector;
	if (!aiConnectorForId(manifest->providerId, &connector) || !connector.cloudBased) {
		return;
	}
	const AiCredentialStatus status = aiCredentialStatusForConnector(connector.id, preferences);
	if (!status.configured) {
		manifest->warnings.push_back(workflowText("Provider credential is not configured; the workflow remains a local, reviewable proposal."));
		manifest->state = OperationState::Warning;
	}
}

QString inferredInputPath(const QString& prompt)
{
	static const QRegularExpression pathPattern(QStringLiteral(R"((\"[^\"]+\.(?:map|bsp|wad|pak|pk3|zip)\"|'[^']+\.(?:map|bsp|wad|pak|pk3|zip)'|\S+\.(?:map|bsp|wad|pak|pk3|zip)))"), QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch match = pathPattern.match(prompt);
	if (!match.hasMatch()) {
		return {};
	}
	return match.captured(1).trimmed().remove('"').remove('\'');
}

QString chooseCompilerProfileId(const QString& prompt)
{
	const QString lower = prompt.toLower();
	auto firstProfile = [](const QStringList& ids) {
		for (const QString& id : ids) {
			CompilerProfileDescriptor descriptor;
			if (compilerProfileForId(id, &descriptor)) {
				return descriptor.id;
			}
		}
		return QString();
	};

	if (containsAny(lower, {QStringLiteral("q3"), QStringLiteral("quake 3"), QStringLiteral("q3map2")})) {
		if (lower.contains(QStringLiteral("light"))) {
			return firstProfile({QStringLiteral("q3map2-light")});
		}
		if (lower.contains(QStringLiteral("vis"))) {
			return firstProfile({QStringLiteral("q3map2-vis")});
		}
		return firstProfile({QStringLiteral("q3map2-bsp"), QStringLiteral("q3map2")});
	}
	if (containsAny(lower, {QStringLiteral("doom"), QStringLiteral("node"), QStringLiteral("zdbsp")})) {
		return firstProfile({QStringLiteral("zdbsp"), QStringLiteral("zokumbsp")});
	}
	if (lower.contains(QStringLiteral("zokum"))) {
		return firstProfile({QStringLiteral("zokumbsp"), QStringLiteral("zdbsp")});
	}
	if (lower.contains(QStringLiteral("light"))) {
		return firstProfile({QStringLiteral("ericw-light")});
	}
	if (lower.contains(QStringLiteral("vis"))) {
		return firstProfile({QStringLiteral("ericw-vis")});
	}
	return firstProfile({QStringLiteral("ericw-qbsp")});
}

QString quotedCliPath(const QString& value)
{
	if (value.trimmed().isEmpty()) {
		return {};
	}
	QString escaped = value;
	escaped.replace('"', QStringLiteral("\\\""));
	return QStringLiteral("\"%1\"").arg(escaped);
}

QString projectManifestPreview(const ProjectManifest& manifest)
{
	QStringList lines;
	lines << QStringLiteral("{");
	lines << QStringLiteral("  \"schemaVersion\": %1,").arg(ProjectManifest::kSchemaVersion);
	lines << QStringLiteral("  \"projectId\": \"%1\",").arg(manifest.projectId);
	lines << QStringLiteral("  \"displayName\": \"%1\",").arg(manifest.displayName);
	lines << QStringLiteral("  \"sourceFolders\": [\"%1\"],").arg(manifest.sourceFolders.value(0, QStringLiteral(".")));
	lines << QStringLiteral("  \"packageFolders\": [],");
	lines << QStringLiteral("  \"outputFolder\": \"%1\",").arg(manifest.outputFolder);
	lines << QStringLiteral("  \"tempFolder\": \"%1\"").arg(manifest.tempFolder);
	lines << QStringLiteral("}");
	return lines.join('\n');
}

} // namespace

QVector<AiToolDescriptor> aiToolDescriptors()
{
	return {
		{QStringLiteral("project-summary"), workflowText("Project Summary"), workflowText("Summarizes project manifests, folders, overrides, and health without mutating project files."), {QStringLiteral("reasoning"), QStringLiteral("tool-calls")}, {QStringLiteral("ProjectManifest")}, {QStringLiteral("summary")}, false, false},
		{QStringLiteral("package-metadata-search"), workflowText("Package Metadata Search"), workflowText("Searches package entries, warnings, types, and likely dependency hints without extracting content."), {QStringLiteral("reasoning"), QStringLiteral("tool-calls")}, {QStringLiteral("PackageArchive")}, {QStringLiteral("dependency-hints")}, false, false},
		{QStringLiteral("compiler-profile-list"), workflowText("Compiler Profile Listing"), workflowText("Lists registered compiler profiles and tool readiness for command proposals."), {QStringLiteral("coding"), QStringLiteral("tool-calls")}, {QStringLiteral("CompilerProfileDescriptor")}, {QStringLiteral("profile-summary")}, false, false},
		{QStringLiteral("compiler-command-proposal"), workflowText("Compiler Command Proposal"), workflowText("Builds reviewable compiler commands and manifests before any process is started."), {QStringLiteral("coding"), QStringLiteral("tool-calls")}, {QStringLiteral("prompt"), QStringLiteral("CompilerCommandRequest")}, {QStringLiteral("command-line"), QStringLiteral("manifest")}, true, false},
		{QStringLiteral("staged-text-edit"), workflowText("Staged Text Edit"), workflowText("Produces text edits as staged previews and diffs before any file write is applied."), {QStringLiteral("coding"), QStringLiteral("tool-calls")}, {QStringLiteral("prompt"), QStringLiteral("file-summary")}, {QStringLiteral("staged-diff")}, true, true},
		{QStringLiteral("staged-asset-generation-request"), workflowText("Staged Asset Generation Request"), workflowText("Creates provider-specific asset generation requests that remain staged until imported by the user."), {QStringLiteral("image"), QStringLiteral("audio"), QStringLiteral("voice"), QStringLiteral("three-d"), QStringLiteral("tool-calls")}, {QStringLiteral("prompt"), QStringLiteral("asset-kind")}, {QStringLiteral("staged-asset-request")}, true, true},
	};
}

QStringList aiToolIds()
{
	QStringList ids;
	for (const AiToolDescriptor& descriptor : aiToolDescriptors()) {
		ids.push_back(descriptor.id);
	}
	return ids;
}

bool aiToolForId(const QString& id, AiToolDescriptor* out)
{
	const QString normalized = normalizedAiId(id);
	for (const AiToolDescriptor& descriptor : aiToolDescriptors()) {
		if (descriptor.id == normalized) {
			if (out) {
				*out = descriptor;
			}
			return true;
		}
	}
	return false;
}

AiWorkflowManifest defaultAiWorkflowManifest(const QString& workflowIdValue, const QString& providerId, const QString& modelId, const QString& prompt)
{
	AiWorkflowManifest manifest;
	manifest.manifestId = workflowId();
	manifest.createdUtc = QDateTime::currentDateTimeUtc();
	manifest.workflowId = normalizedAiId(workflowIdValue);
	manifest.providerId = normalizedAiId(providerId);
	manifest.modelId = normalizedAiModelId(modelId);
	manifest.prompt = prompt.trimmed();
	manifest.redactionSummary = workflowText("Secrets are not stored; credentials are represented only by source and redacted lookup status.");
	manifest.validationSummary = workflowText("Preview generated. No files were written and no compiler/package mutation was started.");
	manifest.costUsageSummary = workflowText("No provider cost is recorded for deterministic local preview output.");
	return manifest;
}

AiWorkflowManifest cancelledAiWorkflowManifest(AiWorkflowManifest manifest)
{
	manifest.state = OperationState::Cancelled;
	manifest.cancellable = false;
	manifest.validationSummary = workflowText("Workflow was cancelled before staged outputs were applied.");
	return manifest;
}

AiWorkflowManifest retryAiWorkflowManifest(AiWorkflowManifest manifest)
{
	manifest.state = OperationState::Queued;
	manifest.cancellable = true;
	++manifest.retryCount;
	manifest.validationSummary = workflowText("Workflow is queued for supervised retry.");
	return manifest;
}

QJsonObject aiToolDescriptorJson(const AiToolDescriptor& tool)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), tool.id);
	object.insert(QStringLiteral("displayName"), tool.displayName);
	object.insert(QStringLiteral("description"), tool.description);
	object.insert(QStringLiteral("capabilities"), stringArrayJson(tool.capabilities));
	object.insert(QStringLiteral("contextInputs"), stringArrayJson(tool.contextInputs));
	object.insert(QStringLiteral("stagedOutputs"), stringArrayJson(tool.stagedOutputs));
	object.insert(QStringLiteral("requiresApproval"), tool.requiresApproval);
	object.insert(QStringLiteral("writesFiles"), tool.writesFiles);
	return object;
}

QJsonObject aiWorkflowManifestJson(const AiWorkflowManifest& manifest)
{
	QJsonObject object;
	object.insert(QStringLiteral("schemaVersion"), manifest.schemaVersion);
	object.insert(QStringLiteral("manifestId"), manifest.manifestId);
	object.insert(QStringLiteral("createdUtc"), manifest.createdUtc.toUTC().toString(Qt::ISODate));
	object.insert(QStringLiteral("workflowId"), manifest.workflowId);
	object.insert(QStringLiteral("providerId"), manifest.providerId);
	object.insert(QStringLiteral("modelId"), manifest.modelId);
	object.insert(QStringLiteral("prompt"), manifest.prompt);
	object.insert(QStringLiteral("contextSummary"), manifest.contextSummary);
	object.insert(QStringLiteral("redactionSummary"), manifest.redactionSummary);
	object.insert(QStringLiteral("approvalState"), manifest.approvalState);
	object.insert(QStringLiteral("validationSummary"), manifest.validationSummary);
	object.insert(QStringLiteral("costUsageSummary"), manifest.costUsageSummary);
	object.insert(QStringLiteral("state"), operationStateId(manifest.state));
	object.insert(QStringLiteral("cancellable"), manifest.cancellable);
	object.insert(QStringLiteral("retryCount"), manifest.retryCount);
	object.insert(QStringLiteral("writesApplied"), manifest.writesApplied);
	object.insert(QStringLiteral("warnings"), stringArrayJson(manifest.warnings));

	QJsonArray calls;
	for (const AiWorkflowToolCall& call : manifest.toolCalls) {
		QJsonObject callObject;
		callObject.insert(QStringLiteral("toolId"), call.toolId);
		callObject.insert(QStringLiteral("summary"), call.summary);
		callObject.insert(QStringLiteral("state"), operationStateId(call.state));
		callObject.insert(QStringLiteral("inputs"), stringArrayJson(call.inputs));
		callObject.insert(QStringLiteral("outputs"), stringArrayJson(call.outputs));
		callObject.insert(QStringLiteral("warnings"), stringArrayJson(call.warnings));
		callObject.insert(QStringLiteral("requiresApproval"), call.requiresApproval);
		callObject.insert(QStringLiteral("writesFiles"), call.writesFiles);
		calls.append(callObject);
	}
	object.insert(QStringLiteral("toolCalls"), calls);

	QJsonArray staged;
	for (const AiStagedOutput& output : manifest.stagedOutputs) {
		QJsonObject outputObject;
		outputObject.insert(QStringLiteral("id"), output.id);
		outputObject.insert(QStringLiteral("kind"), output.kind);
		outputObject.insert(QStringLiteral("label"), output.label);
		outputObject.insert(QStringLiteral("proposedPath"), output.proposedPath);
		outputObject.insert(QStringLiteral("summary"), output.summary);
		outputObject.insert(QStringLiteral("previewLines"), stringArrayJson(output.previewLines));
		outputObject.insert(QStringLiteral("writesFile"), output.writesFile);
		staged.append(outputObject);
	}
	object.insert(QStringLiteral("stagedOutputs"), staged);
	return object;
}

QString aiWorkflowManifestText(const AiWorkflowManifest& manifest)
{
	QStringList lines;
	lines << workflowText("AI workflow manifest: %1").arg(manifest.workflowId);
	lines << workflowText("Provider/model: %1 / %2").arg(manifest.providerId, manifest.modelId.isEmpty() ? workflowText("provider default") : manifest.modelId);
	lines << workflowText("State: %1").arg(operationStateId(manifest.state));
	lines << workflowText("Approval: %1").arg(manifest.approvalState);
	lines << workflowText("Context: %1").arg(manifest.contextSummary);
	lines << workflowText("Redaction: %1").arg(manifest.redactionSummary);
	lines << workflowText("Validation: %1").arg(manifest.validationSummary);
	lines << workflowText("Cost/usage: %1").arg(manifest.costUsageSummary);
	lines << workflowText("Tool calls:");
	for (const AiWorkflowToolCall& call : manifest.toolCalls) {
		lines << QStringLiteral("- %1: %2").arg(call.toolId, call.summary);
	}
	lines << workflowText("Staged outputs:");
	for (const AiStagedOutput& output : manifest.stagedOutputs) {
		lines << QStringLiteral("- %1 [%2]: %3").arg(output.label, output.kind, output.summary);
		if (!output.proposedPath.isEmpty()) {
			lines << QStringLiteral("  %1").arg(QDir::toNativeSeparators(output.proposedPath));
		}
	}
	if (!manifest.warnings.isEmpty()) {
		lines << workflowText("Warnings:");
		for (const QString& warning : manifest.warnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	return lines.join('\n');
}

bool saveAiWorkflowManifest(const AiWorkflowManifest& manifest, const QString& path, QString* error)
{
	if (error) {
		error->clear();
	}
	const QString trimmedPath = path.trimmed();
	if (trimmedPath.isEmpty()) {
		if (error) {
			*error = workflowText("Manifest path is empty.");
		}
		return false;
	}

	QFileInfo info(trimmedPath);
	QDir dir = info.absoluteDir();
	if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
		if (error) {
			*error = workflowText("Unable to create manifest directory.");
		}
		return false;
	}

	QFile file(info.absoluteFilePath());
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		if (error) {
			*error = workflowText("Unable to write AI workflow manifest.");
		}
		return false;
	}
	file.write(QJsonDocument(aiWorkflowManifestJson(manifest)).toJson(QJsonDocument::Indented));
	return true;
}

AiWorkflowResult explainCompilerLogAiExperiment(const QString& logText, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	const QString provider = selectedProviderId(preferences, providerId, QStringLiteral("reasoning"));
	const QString model = selectedModelId(preferences, provider, modelId, QStringLiteral("reasoning"));
	AiWorkflowResult result;
	result.title = workflowText("Compiler Log Explanation");
	result.manifest = defaultAiWorkflowManifest(QStringLiteral("explain-compiler-log"), provider, model, workflowText("Explain compiler log"));
	result.manifest.contextSummary = workflowText("Compiler log text only; no files are read or written.");
	result.manifest.toolCalls.push_back(toolCall(QStringLiteral("compiler-profile-list"), workflowText("Referenced compiler profile families to make diagnostics actionable."), {}, {workflowText("Compiler profiles available: %1").arg(compilerProfileDescriptors().size())}));

	const QStringList lines = firstNonEmptyLines(logText, 8);
	const QString lower = logText.toLower();
	const int errorCount = lower.count(QStringLiteral("error")) + lower.count(QStringLiteral("fatal"));
	const int warningCount = lower.count(QStringLiteral("warn"));
	QStringList findings;
	if (errorCount > 0) {
		findings << workflowText("The log contains blocking error or fatal markers.");
	}
	if (warningCount > 0) {
		findings << workflowText("The log contains warning markers that may still produce usable output.");
	}
	if (containsAny(lower, {QStringLiteral("leak"), QStringLiteral("portal") })) {
		findings << workflowText("Leak or portal wording suggests a map sealing/visibility problem.");
	}
	if (containsAny(lower, {QStringLiteral("texture"), QStringLiteral("shader"), QStringLiteral("wad") })) {
		findings << workflowText("Texture, shader, or WAD wording suggests a missing asset or mount/dependency issue.");
	}
	if (containsAny(lower, {QStringLiteral("entity"), QStringLiteral("classname"), QStringLiteral("spawn") })) {
		findings << workflowText("Entity wording suggests checking class names, definitions, or map entity keys.");
	}
	if (findings.isEmpty()) {
		findings << workflowText("No obvious fatal marker was found. Review the first compiler messages and rerun with verbose output if the tool failed.");
	}

	result.summary = workflowText("%1 findings from %2 non-empty log lines.").arg(findings.size()).arg(lines.size());
	result.reviewableText = QStringLiteral("%1\n\n%2\n\n%3")
		.arg(workflowText("Findings:"), QStringLiteral("- %1").arg(findings.join(QStringLiteral("\n- "))), workflowText("First log lines:"));
	result.reviewableText += QStringLiteral("\n- %1").arg(lines.isEmpty() ? workflowText("No log text supplied.") : lines.join(QStringLiteral("\n- ")));
	result.nextActions = {
		workflowText("Open the compiler manifest or captured stdout/stderr for the full context."),
		workflowText("Run ai propose-command with the desired target to get a reviewable next command."),
		workflowText("Keep AI-free mode enabled if you want deterministic local explanations only."),
	};
	result.diagnostics = findings;
	result.manifest.stagedOutputs.push_back({QStringLiteral("compiler-log-explanation"), QStringLiteral("text"), workflowText("Compiler log explanation"), QString(), result.summary, findings, false});
	applyCredentialWarning(&result.manifest, preferences);
	return result;
}

AiWorkflowResult proposeCompilerCommandAiExperiment(const QString& prompt, const QString& workspaceRootPath, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	const QString provider = selectedProviderId(preferences, providerId, QStringLiteral("coding"));
	const QString model = selectedModelId(preferences, provider, modelId, QStringLiteral("coding"));
	const QString profileId = chooseCompilerProfileId(prompt);
	const QString input = inferredInputPath(prompt).isEmpty() ? QStringLiteral("<map-or-wad-path>") : inferredInputPath(prompt);

	AiWorkflowResult result;
	result.title = workflowText("Compiler Command Proposal");
	result.manifest = defaultAiWorkflowManifest(QStringLiteral("propose-compiler-command"), provider, model, prompt);
	result.manifest.contextSummary = workspaceRootPath.trimmed().isEmpty()
		? workflowText("Natural-language prompt and compiler profile registry only.")
		: workflowText("Natural-language prompt, compiler profile registry, and workspace root path.");
	result.manifest.toolCalls.push_back(toolCall(QStringLiteral("compiler-profile-list"), workflowText("Matched prompt intent to a compiler profile."), {prompt}, {profileId}));
	result.manifest.toolCalls.push_back(toolCall(QStringLiteral("compiler-command-proposal"), workflowText("Produced a reviewable CLI command without starting a compiler process."), {profileId, input}, {}, {}));

	result.commandLine = QStringLiteral("vibestudio --cli compiler plan %1 --input %2 --dry-run").arg(profileId, quotedCliPath(input));
	if (!workspaceRootPath.trimmed().isEmpty()) {
		result.commandLine += QStringLiteral(" --workspace-root %1").arg(quotedCliPath(workspaceRootPath));
	}
	result.summary = workflowText("Reviewable compiler command proposal for profile %1.").arg(profileId);
	result.reviewableText = QStringLiteral("%1\n%2").arg(result.summary, result.commandLine);
	result.nextActions = {
		workflowText("Review the profile and input path."),
		workflowText("Run the plan command first; add --manifest when you want a reproducible command record."),
		workflowText("Switch to compiler run only after the plan is correct."),
	};
	result.manifest.stagedOutputs.push_back({QStringLiteral("compiler-command"), QStringLiteral("command"), workflowText("Compiler command"), QString(), result.commandLine, {result.commandLine}, false});
	applyCredentialWarning(&result.manifest, preferences);
	return result;
}

AiWorkflowResult draftProjectManifestAiExperiment(const QString& projectRootPath, const QString& displayName, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	const QString provider = selectedProviderId(preferences, providerId, QStringLiteral("reasoning"));
	const QString model = selectedModelId(preferences, provider, modelId, QStringLiteral("reasoning"));
	const QString rootPath = projectRootPath.trimmed().isEmpty() ? QDir::currentPath() : projectRootPath;
	const ProjectManifest draft = defaultProjectManifest(rootPath, displayName);

	AiWorkflowResult result;
	result.title = workflowText("Project Manifest Draft");
	result.summary = workflowText("Draft .vibestudio/project.json preview for %1.").arg(QDir::toNativeSeparators(draft.rootPath));
	result.reviewableText = projectManifestPreview(draft);
	result.manifest = defaultAiWorkflowManifest(QStringLiteral("draft-project-manifest"), provider, model, workflowText("Draft project manifest"));
	result.manifest.contextSummary = workflowText("Project root path and folder naming only; manifest is staged and not written.");
	result.manifest.toolCalls.push_back(toolCall(QStringLiteral("project-summary"), workflowText("Built a default project manifest draft from the requested folder."), {draft.rootPath}, {draft.projectId}));
	result.manifest.stagedOutputs.push_back({QStringLiteral("project-manifest-draft"), QStringLiteral("json"), workflowText("Project manifest draft"), projectManifestPath(draft.rootPath), result.summary, result.reviewableText.split('\n'), true});
	result.nextActions = {
		workflowText("Review source, package, output, temp, and AI-free settings before saving."),
		workflowText("Use project init when you want VibeStudio to write the manifest."),
	};
	applyCredentialWarning(&result.manifest, preferences);
	return result;
}

AiWorkflowResult suggestPackageDependenciesAiExperiment(const QString& packagePath, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	const QString provider = selectedProviderId(preferences, providerId, QStringLiteral("reasoning"));
	const QString model = selectedModelId(preferences, provider, modelId, QStringLiteral("reasoning"));

	AiWorkflowResult result;
	result.title = workflowText("Package Dependency Suggestions");
	result.manifest = defaultAiWorkflowManifest(QStringLiteral("suggest-package-dependencies"), provider, model, workflowText("Suggest missing package dependencies"));
	result.manifest.contextSummary = workflowText("Package metadata only; entries are listed without extraction.");

	QStringList suggestions;
	PackageArchive archive;
	QString error;
	if (!packagePath.trimmed().isEmpty() && archive.load(packagePath, &error)) {
		bool hasMaps = false;
		bool hasShaders = false;
		bool hasTextures = false;
		bool hasSounds = false;
		for (const PackageEntry& entry : archive.entries()) {
			const QString path = entry.virtualPath.toLower();
			hasMaps = hasMaps || path.endsWith(QStringLiteral(".bsp")) || path.endsWith(QStringLiteral(".map"));
			hasShaders = hasShaders || path.endsWith(QStringLiteral(".shader"));
			hasTextures = hasTextures || path.startsWith(QStringLiteral("textures/")) || path.endsWith(QStringLiteral(".wad"));
			hasSounds = hasSounds || path.startsWith(QStringLiteral("sound/")) || path.startsWith(QStringLiteral("sounds/"));
		}
		if (hasMaps && !hasTextures) {
			suggestions << workflowText("Map entries were found but no obvious texture folder or WAD reference was present; verify base texture packages.");
		}
		if (hasShaders && !hasTextures) {
			suggestions << workflowText("Shader scripts were found without obvious texture assets; verify shader texture dependencies.");
		}
		if (hasMaps && !hasSounds) {
			suggestions << workflowText("Map entries were found without sound assets; verify whether the map expects base-game ambient or trigger sounds.");
		}
		if (!archive.warnings().isEmpty()) {
			suggestions << workflowText("Package loader warnings should be reviewed before release.");
		}
		if (suggestions.isEmpty()) {
			suggestions << workflowText("No obvious missing package dependency pattern was detected from metadata alone.");
		}
		result.manifest.toolCalls.push_back(toolCall(QStringLiteral("package-metadata-search"), workflowText("Scanned package metadata for map, shader, texture, and sound hints."), {packagePath}, suggestions));
	} else {
		suggestions << (packagePath.trimmed().isEmpty() ? workflowText("No package path was supplied.") : workflowText("Package could not be loaded: %1").arg(error));
		result.manifest.toolCalls.push_back(toolCall(QStringLiteral("package-metadata-search"), workflowText("Package metadata scan could not complete."), {packagePath}, {}, suggestions));
	}

	result.summary = workflowText("Package dependency suggestions: %1.").arg(suggestions.size());
	result.reviewableText = QStringLiteral("- %1").arg(suggestions.join(QStringLiteral("\n- ")));
	result.diagnostics = suggestions;
	result.nextActions = {
		workflowText("Open package info/list for the full entry set."),
		workflowText("Add detected base packages through project or installation settings before release validation."),
	};
	result.manifest.stagedOutputs.push_back({QStringLiteral("package-dependency-suggestions"), QStringLiteral("text"), workflowText("Package dependency suggestions"), QString(), result.summary, suggestions, false});
	applyCredentialWarning(&result.manifest, preferences);
	return result;
}

AiWorkflowResult generateCliCommandAiExperiment(const QString& prompt, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	const QString provider = selectedProviderId(preferences, providerId, QStringLiteral("coding"));
	const QString model = selectedModelId(preferences, provider, modelId, QStringLiteral("coding"));
	const QString lower = prompt.toLower();
	const QString input = inferredInputPath(prompt);
	QString command = QStringLiteral("vibestudio --cli --help");
	if (containsAny(lower, {QStringLiteral("validate package"), QStringLiteral("check package")})) {
		command = QStringLiteral("vibestudio --cli package validate %1").arg(quotedCliPath(input.isEmpty() ? QStringLiteral("<package-path>") : input));
	} else if (containsAny(lower, {QStringLiteral("list package"), QStringLiteral("show package")})) {
		command = QStringLiteral("vibestudio --cli package list %1").arg(quotedCliPath(input.isEmpty() ? QStringLiteral("<package-path>") : input));
	} else if (containsAny(lower, {QStringLiteral("project info"), QStringLiteral("show project")})) {
		command = QStringLiteral("vibestudio --cli project info %1").arg(quotedCliPath(input.isEmpty() ? QStringLiteral("<project-root>") : input));
	} else if (containsAny(lower, {QStringLiteral("compiler"), QStringLiteral("compile"), QStringLiteral("build map")})) {
		const QString profileId = chooseCompilerProfileId(prompt);
		command = QStringLiteral("vibestudio --cli compiler plan %1 --input %2 --dry-run").arg(profileId, quotedCliPath(input.isEmpty() ? QStringLiteral("<map-or-wad-path>") : input));
	} else if (containsAny(lower, {QStringLiteral("explain log"), QStringLiteral("compiler log")})) {
		command = QStringLiteral("vibestudio --cli ai explain-log --log %1").arg(quotedCliPath(QStringLiteral("<compiler-log-path>")));
	}

	AiWorkflowResult result;
	result.title = workflowText("CLI Command Proposal");
	result.summary = workflowText("Reviewable CLI command generated from the requested workflow.");
	result.commandLine = command;
	result.reviewableText = command;
	result.manifest = defaultAiWorkflowManifest(QStringLiteral("generate-cli-command"), provider, model, prompt);
	result.manifest.contextSummary = workflowText("Prompt text and CLI command registry only.");
	result.manifest.toolCalls.push_back(toolCall(QStringLiteral("compiler-command-proposal"), workflowText("Mapped user intent to a safe CLI command proposal."), {prompt}, {command}));
	result.manifest.stagedOutputs.push_back({QStringLiteral("cli-command"), QStringLiteral("command"), workflowText("CLI command"), QString(), command, {command}, false});
	result.nextActions = {workflowText("Review placeholder paths before running the command."), workflowText("Add --json for machine-readable output or --verbose for timing diagnostics.")};
	applyCredentialWarning(&result.manifest, preferences);
	return result;
}

AiWorkflowResult fixAndRetryPlanAiExperiment(const QString& compilerLogText, const QString& commandLine, const AiAutomationPreferences& preferences, const QString& providerId, const QString& modelId)
{
	AiWorkflowResult explanation = explainCompilerLogAiExperiment(compilerLogText, preferences, providerId, modelId);
	AiWorkflowResult result = explanation;
	result.title = workflowText("Supervised Fix And Retry Plan");
	result.manifest.workflowId = QStringLiteral("fix-and-retry-plan");
	result.manifest.prompt = workflowText("Generate supervised fix and retry plan");
	result.manifest.contextSummary = workflowText("Compiler log text and optional prior command line. No files are written.");
	result.manifest.toolCalls.push_back(toolCall(QStringLiteral("compiler-command-proposal"), workflowText("Prepared a retry command review step without executing it."), {commandLine}, {commandLine.trimmed().isEmpty() ? workflowText("No prior command supplied.") : commandLine}));
	QStringList steps;
	steps << workflowText("1. Inspect the first fatal/error message and linked file path, if present.");
	steps << workflowText("2. Fix the smallest likely cause: missing asset mount, map leak, bad entity key, or unsupported compiler argument.");
	steps << workflowText("3. Re-run the command as a dry-run plan before executing.");
	steps << workflowText("4. Save a compiler manifest when the retry command is ready.");
	if (!commandLine.trimmed().isEmpty()) {
		steps << workflowText("Retry command to review: %1").arg(commandLine);
	}
	result.summary = workflowText("Supervised retry plan generated from compiler diagnostics.");
	result.reviewableText = steps.join('\n');
	result.nextActions = steps;
	result.manifest.stagedOutputs.push_back({QStringLiteral("fix-and-retry-plan"), QStringLiteral("text"), workflowText("Fix and retry plan"), QString(), result.summary, steps, false});
	return result;
}

AiWorkflowResult stageAssetGenerationRequestAiExperiment(const QString& connectorId, const QString& kind, const QString& prompt, const AiAutomationPreferences& preferences, const QString& modelId)
{
	const QString normalizedKind = normalizedAiId(kind);
	const QString capability = (normalizedKind == QStringLiteral("voice")) ? QStringLiteral("voice")
		: (normalizedKind == QStringLiteral("sound") || normalizedKind == QStringLiteral("audio")) ? QStringLiteral("audio")
		: (normalizedKind == QStringLiteral("model") || normalizedKind == QStringLiteral("mesh") || normalizedKind == QStringLiteral("three-d")) ? QStringLiteral("three-d")
		: QStringLiteral("image");
	const QString provider = selectedProviderId(preferences, connectorId, capability);
	const QString model = selectedModelId(preferences, provider, modelId, capability);

	AiWorkflowResult result;
	result.title = workflowText("Staged Asset Generation Request");
	result.summary = workflowText("Provider-specific asset request staged for review; no generated file is imported.");
	result.reviewableText = QStringLiteral("%1: %2\n%3: %4").arg(workflowText("Provider"), provider, workflowText("Prompt"), prompt);
	result.manifest = defaultAiWorkflowManifest(QStringLiteral("stage-asset-generation-request"), provider, model, prompt);
	result.manifest.contextSummary = workflowText("Asset prompt, requested kind, connector metadata, and credential redaction status.");
	result.manifest.toolCalls.push_back(toolCall(QStringLiteral("staged-asset-generation-request"), workflowText("Prepared generated asset request for review before import."), {provider, normalizedKind, prompt}, {workflowText("staged request")}));
	const QString proposedPath = QDir::cleanPath(QStringLiteral(".vibestudio/ai-staged/%1-%2-request.json").arg(provider, normalizedKind.isEmpty() ? QStringLiteral("asset") : normalizedKind));
	result.manifest.stagedOutputs.push_back({QStringLiteral("asset-generation-request"), normalizedKind.isEmpty() ? QStringLiteral("asset") : normalizedKind, workflowText("Asset generation request"), proposedPath, result.summary, {result.reviewableText}, true});
	result.nextActions = {
		workflowText("Review provider, prompt, cost expectations, and generated-asset provenance before sending."),
		workflowText("Import generated assets only through a staged package/project workflow."),
	};
	applyCredentialWarning(&result.manifest, preferences);
	return result;
}

AiWorkflowResult compareProviderOutputsAiExperiment(const QString& prompt, const QString& providerA, const QString& providerB, const AiAutomationPreferences& preferences, const QString& modelA, const QString& modelB)
{
	const QString firstProvider = selectedProviderId(preferences, providerA, QStringLiteral("reasoning"));
	const QString secondProvider = selectedProviderId(preferences, providerB, QStringLiteral("reasoning"));
	const QString firstModel = selectedModelId(preferences, firstProvider, modelA, QStringLiteral("reasoning"));
	const QString secondModel = selectedModelId(preferences, secondProvider, modelB, QStringLiteral("reasoning"));

	AiWorkflowResult result;
	result.title = workflowText("Provider Output Comparison");
	result.summary = workflowText("Comparison manifest prepared for two reasoning providers.");
	result.reviewableText = QStringLiteral("%1 / %2\n- %3\n\n%4 / %5\n- %6")
		.arg(firstProvider, firstModel, workflowText("Would answer with the selected primary provider once configured."), secondProvider, secondModel, workflowText("Would answer with the selected comparison provider once configured."));
	result.manifest = defaultAiWorkflowManifest(QStringLiteral("compare-provider-outputs"), firstProvider, firstModel, prompt);
	result.manifest.contextSummary = workflowText("Prompt text and provider/model selections for side-by-side review.");
	result.manifest.toolCalls.push_back(toolCall(QStringLiteral("project-summary"), workflowText("Captured prompt context for provider comparison."), {prompt}, {firstProvider, secondProvider}));
	result.manifest.stagedOutputs.push_back({QStringLiteral("provider-a-output"), QStringLiteral("text"), workflowText("Primary provider output placeholder"), QString(), firstProvider, {workflowText("Awaiting configured provider call.")}, false});
	result.manifest.stagedOutputs.push_back({QStringLiteral("provider-b-output"), QStringLiteral("text"), workflowText("Comparison provider output placeholder"), QString(), secondProvider, {workflowText("Awaiting configured provider call.")}, false});
	applyCredentialWarning(&result.manifest, preferences);
	return result;
}

} // namespace vibestudio
