#include "core/ai_connectors.h"

#include <QCoreApplication>
#include <QtGlobal>

namespace vibestudio {

namespace {

QString aiText(const char* source)
{
	return QCoreApplication::translate("VibeStudioAiConnectors", source);
}

AiCapabilityDescriptor capability(const QString& id, const QString& displayName, const QString& description)
{
	return {id, displayName, description};
}

AiConnectorDescriptor connector(
	const QString& id,
	const QString& displayName,
	const QString& providerFamily,
	const QString& endpointKind,
	const QString& authRequirement,
	const QString& privacyNote,
	const QStringList& capabilities,
	const QStringList& configurationNotes,
	bool cloudBased,
	bool implemented = false)
{
	return {
		id,
		displayName,
		providerFamily,
		endpointKind,
		authRequirement,
		privacyNote,
		capabilities,
		configurationNotes,
		cloudBased,
		implemented,
	};
}

AiModelDescriptor model(
	const QString& id,
	const QString& connectorId,
	const QString& displayName,
	const QString& description,
	const QStringList& capabilities,
	bool defaultForConnector = false)
{
	return {
		id,
		normalizedAiId(connectorId),
		displayName,
		description,
		capabilities,
		defaultForConnector,
	};
}

QString normalizedPreferredConnector(const QString& connectorId, const QString& capabilityId)
{
	AiConnectorDescriptor descriptor;
	if (!aiConnectorForId(connectorId, &descriptor)) {
		return {};
	}
	if (!descriptor.capabilities.contains(normalizedAiId(capabilityId))) {
		return {};
	}
	return descriptor.id;
}

QString preferredConnectorText(const QString& label, const QString& connectorId)
{
	if (connectorId.isEmpty()) {
		return QCoreApplication::translate("VibeStudioAiConnectors", "%1: not selected").arg(label);
	}
	AiConnectorDescriptor connector;
	aiConnectorForId(connectorId, &connector);
	return QCoreApplication::translate("VibeStudioAiConnectors", "%1: %2 [%3]").arg(label, connector.displayName, connector.id);
}

QString preferredModelText(const QString& label, const QString& modelId)
{
	if (modelId.isEmpty()) {
		return QCoreApplication::translate("VibeStudioAiConnectors", "%1: provider default").arg(label);
	}
	AiModelDescriptor model;
	aiModelForId(modelId, &model);
	return QCoreApplication::translate("VibeStudioAiConnectors", "%1: %2 [%3]").arg(label, model.displayName, model.id);
}

QString normalizedPreferredModel(const QString& modelId, const QString& capabilityId)
{
	return normalizedAiModelId(modelId, capabilityId);
}

} // namespace

QString normalizedAiId(const QString& id)
{
	QString normalized = id.trimmed().toLower().replace('_', '-');
	normalized.replace(QStringLiteral(" "), QStringLiteral("-"));
	while (normalized.contains(QStringLiteral("--"))) {
		normalized.replace(QStringLiteral("--"), QStringLiteral("-"));
	}
	return normalized;
}

QVector<AiCapabilityDescriptor> aiCapabilityDescriptors()
{
	return {
		capability(QStringLiteral("reasoning"), aiText("Reasoning"), aiText("General planning, explanation, project summarization, and review assistance.")),
		capability(QStringLiteral("coding"), aiText("Coding"), aiText("Code, script, shader, config, and command assistance through reviewable proposals.")),
		capability(QStringLiteral("vision"), aiText("Vision"), aiText("Image and screenshot understanding for assets, UI, maps, and visual QA.")),
		capability(QStringLiteral("image"), aiText("Image Generation"), aiText("Optional generated concept art, sprites, textures, and placeholder imagery.")),
		capability(QStringLiteral("audio"), aiText("Audio Generation"), aiText("Optional generated sound, music, and audio ideation workflows.")),
		capability(QStringLiteral("voice"), aiText("Voice"), aiText("Optional narration, speech, and voice workflow experiments.")),
		capability(QStringLiteral("three-d"), aiText("3D Generation"), aiText("Optional model, texture, and concept-to-asset generation experiments.")),
		capability(QStringLiteral("embeddings"), aiText("Embeddings"), aiText("Semantic search, clustering, retrieval, and context ranking.")),
		capability(QStringLiteral("tool-calls"), aiText("Tool Calls"), aiText("Reviewable use of VibeStudio project, package, compiler, and validation services.")),
		capability(QStringLiteral("streaming"), aiText("Streaming"), aiText("Incremental response, progress, and cancellation-friendly provider output.")),
		capability(QStringLiteral("cost-usage"), aiText("Cost/Usage"), aiText("Provider cost, quota, token, credit, or usage reporting where available.")),
		capability(QStringLiteral("local-offline"), aiText("Local/Offline"), aiText("User-configured local runtimes that do not require cloud services.")),
	};
}

QStringList aiCapabilityIds()
{
	QStringList ids;
	for (const AiCapabilityDescriptor& descriptor : aiCapabilityDescriptors()) {
		ids.push_back(descriptor.id);
	}
	return ids;
}

bool aiCapabilityForId(const QString& id, AiCapabilityDescriptor* out)
{
	const QString normalized = normalizedAiId(id);
	for (const AiCapabilityDescriptor& descriptor : aiCapabilityDescriptors()) {
		if (descriptor.id == normalized) {
			if (out) {
				*out = descriptor;
			}
			return true;
		}
	}
	return false;
}

QVector<AiConnectorDescriptor> aiConnectorDescriptors()
{
	return {
		connector(
			QStringLiteral("openai"),
			aiText("OpenAI"),
			QStringLiteral("OpenAI"),
			QStringLiteral("https"),
			aiText("User-provided API key or organization policy."),
			aiText("Cloud connector; project context is sent only after explicit user opt-in and review."),
			{
				QStringLiteral("reasoning"),
				QStringLiteral("coding"),
				QStringLiteral("vision"),
				QStringLiteral("image"),
				QStringLiteral("audio"),
				QStringLiteral("voice"),
				QStringLiteral("embeddings"),
				QStringLiteral("tool-calls"),
				QStringLiteral("streaming"),
				QStringLiteral("cost-usage"),
			},
			{
				aiText("First general-purpose connector for credential discovery, configurable model routing, and manifest-backed reviewable experiments."),
				aiText("Provider calls stay opt-in; VibeStudio records prompts, context summaries, tool calls, staged outputs, approval state, validation, and cost placeholders without logging secrets."),
			},
			true,
			true),
		connector(
			QStringLiteral("claude"),
			aiText("Claude"),
			QStringLiteral("Anthropic"),
			QStringLiteral("https"),
			aiText("User-provided API key or organization policy."),
			aiText("Cloud connector; long-context project data must be explicitly reviewed before sending."),
			{
				QStringLiteral("reasoning"),
				QStringLiteral("coding"),
				QStringLiteral("vision"),
				QStringLiteral("tool-calls"),
				QStringLiteral("streaming"),
				QStringLiteral("cost-usage"),
			},
			{
				aiText("Design stub for long-context reasoning, review, scripts, and shader assistance."),
			},
			true),
		connector(
			QStringLiteral("gemini"),
			aiText("Gemini"),
			QStringLiteral("Google"),
			QStringLiteral("https"),
			aiText("User-provided API key or organization policy."),
			aiText("Cloud connector; multimodal project context requires explicit user review."),
			{
				QStringLiteral("reasoning"),
				QStringLiteral("coding"),
				QStringLiteral("vision"),
				QStringLiteral("image"),
				QStringLiteral("embeddings"),
				QStringLiteral("tool-calls"),
				QStringLiteral("streaming"),
				QStringLiteral("cost-usage"),
			},
			{
				aiText("Design stub for multimodal and large-context model routing."),
			},
			true),
		connector(
			QStringLiteral("elevenlabs"),
			aiText("ElevenLabs"),
			QStringLiteral("ElevenLabs"),
			QStringLiteral("https"),
			aiText("User-provided API key."),
			aiText("Cloud connector for generated audio and voice experiments; asset provenance must stay visible."),
			{
				QStringLiteral("audio"),
				QStringLiteral("voice"),
				QStringLiteral("streaming"),
				QStringLiteral("cost-usage"),
			},
			{
				aiText("Design stub for narration, voice, speech-to-text, sound effects, and audio ideation."),
			},
			true),
		connector(
			QStringLiteral("meshy"),
			aiText("Meshy"),
			QStringLiteral("Meshy"),
			QStringLiteral("https"),
			aiText("User-provided API key."),
			aiText("Cloud connector for generated 3D and texture concepts; generated-asset provenance must stay visible."),
			{
				QStringLiteral("three-d"),
				QStringLiteral("image"),
				QStringLiteral("cost-usage"),
			},
			{
				aiText("Design stub for prompt/image-to-3D, texture generation, and placeholder model concepts."),
			},
			true),
		connector(
			QStringLiteral("local-offline"),
			aiText("Local/Offline Runtime"),
			QStringLiteral("Local"),
			QStringLiteral("local-process"),
			aiText("User-configured executable or local service; no cloud credential required."),
			aiText("Local connector; users control runtime, model files, and data locality."),
			{
				QStringLiteral("reasoning"),
				QStringLiteral("coding"),
				QStringLiteral("embeddings"),
				QStringLiteral("tool-calls"),
				QStringLiteral("streaming"),
				QStringLiteral("local-offline"),
			},
			{
				aiText("Design stub for user-supplied local model runtimes, local services, or command-line tools."),
			},
			false),
		connector(
			QStringLiteral("custom-http"),
			aiText("Custom HTTP Connector"),
			QStringLiteral("Custom"),
			QStringLiteral("https"),
			aiText("User-defined endpoint, headers, and authentication policy."),
			aiText("User-defined connector; VibeStudio must show endpoint, privacy, and capability declarations before use."),
			{
				QStringLiteral("reasoning"),
				QStringLiteral("coding"),
				QStringLiteral("vision"),
				QStringLiteral("image"),
				QStringLiteral("audio"),
				QStringLiteral("voice"),
				QStringLiteral("three-d"),
				QStringLiteral("embeddings"),
				QStringLiteral("tool-calls"),
				QStringLiteral("streaming"),
				QStringLiteral("cost-usage"),
			},
			{
				aiText("Design stub for teams that proxy or self-host provider-compatible endpoints."),
			},
			true),
	};
}

QStringList aiConnectorIds()
{
	QStringList ids;
	for (const AiConnectorDescriptor& descriptor : aiConnectorDescriptors()) {
		ids.push_back(descriptor.id);
	}
	return ids;
}

bool aiConnectorForId(const QString& id, AiConnectorDescriptor* out)
{
	const QString normalized = normalizedAiId(id);
	for (const AiConnectorDescriptor& descriptor : aiConnectorDescriptors()) {
		if (descriptor.id == normalized) {
			if (out) {
				*out = descriptor;
			}
			return true;
		}
	}
	return false;
}

bool aiConnectorSupportsCapability(const QString& connectorId, const QString& capabilityId)
{
	AiConnectorDescriptor connector;
	return aiConnectorForId(connectorId, &connector) && connector.capabilities.contains(normalizedAiId(capabilityId));
}

QVector<AiModelDescriptor> aiModelDescriptors()
{
	return {
		model(
			QStringLiteral("openai-text-default"),
			QStringLiteral("openai"),
			aiText("OpenAI configurable text model"),
			aiText("Resolved from user settings or provider environment for reasoning, coding, tool calls, and compiler-log explanation."),
			{QStringLiteral("reasoning"), QStringLiteral("coding"), QStringLiteral("vision"), QStringLiteral("tool-calls"), QStringLiteral("streaming"), QStringLiteral("cost-usage")},
			true),
		model(
			QStringLiteral("openai-image-default"),
			QStringLiteral("openai"),
			aiText("OpenAI configurable image model"),
			aiText("Resolved from user settings for concept art, sprite, and texture generation requests staged before import."),
			{QStringLiteral("image"), QStringLiteral("vision"), QStringLiteral("cost-usage")}),
		model(
			QStringLiteral("openai-audio-default"),
			QStringLiteral("openai"),
			aiText("OpenAI configurable audio model"),
			aiText("Resolved from user settings for optional sound, transcription, and voice-adjacent experiments."),
			{QStringLiteral("audio"), QStringLiteral("voice"), QStringLiteral("cost-usage")}),
		model(
			QStringLiteral("claude-text-default"),
			QStringLiteral("claude"),
			aiText("Claude configurable text model"),
			aiText("Placeholder for long-context reasoning and code review once the connector is implemented."),
			{QStringLiteral("reasoning"), QStringLiteral("coding"), QStringLiteral("vision"), QStringLiteral("tool-calls"), QStringLiteral("streaming"), QStringLiteral("cost-usage")},
			true),
		model(
			QStringLiteral("gemini-text-default"),
			QStringLiteral("gemini"),
			aiText("Gemini configurable text model"),
			aiText("Placeholder for multimodal reasoning and large-context routing once the connector is implemented."),
			{QStringLiteral("reasoning"), QStringLiteral("coding"), QStringLiteral("vision"), QStringLiteral("image"), QStringLiteral("embeddings"), QStringLiteral("tool-calls"), QStringLiteral("streaming"), QStringLiteral("cost-usage")},
			true),
		model(
			QStringLiteral("elevenlabs-voice-default"),
			QStringLiteral("elevenlabs"),
			aiText("ElevenLabs configurable voice model"),
			aiText("Placeholder for staged sound and voice generation requests when ElevenLabs is configured."),
			{QStringLiteral("audio"), QStringLiteral("voice"), QStringLiteral("streaming"), QStringLiteral("cost-usage")},
			true),
		model(
			QStringLiteral("meshy-asset-default"),
			QStringLiteral("meshy"),
			aiText("Meshy configurable asset model"),
			aiText("Placeholder for staged model and texture concept requests when Meshy is configured."),
			{QStringLiteral("three-d"), QStringLiteral("image"), QStringLiteral("cost-usage")},
			true),
		model(
			QStringLiteral("local-offline-default"),
			QStringLiteral("local-offline"),
			aiText("Local runtime default model"),
			aiText("User-supplied local model or service for AI-free-friendly experimentation."),
			{QStringLiteral("reasoning"), QStringLiteral("coding"), QStringLiteral("embeddings"), QStringLiteral("tool-calls"), QStringLiteral("streaming"), QStringLiteral("local-offline")},
			true),
		model(
			QStringLiteral("custom-http-default"),
			QStringLiteral("custom-http"),
			aiText("Custom HTTP default model"),
			aiText("Team-defined provider-compatible model declared by custom connector configuration."),
			{QStringLiteral("reasoning"), QStringLiteral("coding"), QStringLiteral("vision"), QStringLiteral("image"), QStringLiteral("audio"), QStringLiteral("voice"), QStringLiteral("three-d"), QStringLiteral("embeddings"), QStringLiteral("tool-calls"), QStringLiteral("streaming"), QStringLiteral("cost-usage")},
			true),
	};
}

QVector<AiModelDescriptor> aiModelDescriptorsForConnector(const QString& connectorId)
{
	const QString normalized = normalizedAiId(connectorId);
	QVector<AiModelDescriptor> models;
	for (const AiModelDescriptor& descriptor : aiModelDescriptors()) {
		if (descriptor.connectorId == normalized) {
			models.push_back(descriptor);
		}
	}
	return models;
}

QStringList aiModelIds()
{
	QStringList ids;
	for (const AiModelDescriptor& descriptor : aiModelDescriptors()) {
		ids.push_back(descriptor.id);
	}
	return ids;
}

bool aiModelForId(const QString& id, AiModelDescriptor* out)
{
	const QString normalized = normalizedAiId(id);
	for (const AiModelDescriptor& descriptor : aiModelDescriptors()) {
		if (descriptor.id == normalized) {
			if (out) {
				*out = descriptor;
			}
			return true;
		}
	}
	return false;
}

bool aiModelSupportsCapability(const QString& modelId, const QString& capabilityId)
{
	AiModelDescriptor descriptor;
	return aiModelForId(modelId, &descriptor) && descriptor.capabilities.contains(normalizedAiId(capabilityId));
}

QString defaultAiModelId(const QString& connectorId, const QString& capabilityId)
{
	const QString normalizedConnector = normalizedAiId(connectorId);
	const QString normalizedCapability = normalizedAiId(capabilityId);
	QString fallback;
	for (const AiModelDescriptor& descriptor : aiModelDescriptors()) {
		if (descriptor.connectorId != normalizedConnector || !descriptor.capabilities.contains(normalizedCapability)) {
			continue;
		}
		if (fallback.isEmpty()) {
			fallback = descriptor.id;
		}
		if (descriptor.defaultForConnector) {
			return descriptor.id;
		}
	}
	return fallback;
}

QString normalizedAiModelId(const QString& modelId, const QString& capabilityId)
{
	const QString normalized = normalizedAiId(modelId);
	if (normalized.isEmpty()) {
		return {};
	}
	AiModelDescriptor descriptor;
	if (!aiModelForId(normalized, &descriptor)) {
		return {};
	}
	if (!capabilityId.trimmed().isEmpty() && !descriptor.capabilities.contains(normalizedAiId(capabilityId))) {
		return {};
	}
	return descriptor.id;
}

QString aiCredentialEnvironmentVariableForConnector(const QString& connectorId, const AiAutomationPreferences& preferences)
{
	const QString normalized = normalizedAiId(connectorId);
	if (normalized == QStringLiteral("openai")) {
		return preferences.openAiCredentialEnvironmentVariable.trimmed().isEmpty() ? QStringLiteral("OPENAI_API_KEY") : preferences.openAiCredentialEnvironmentVariable.trimmed();
	}
	if (normalized == QStringLiteral("elevenlabs")) {
		return preferences.elevenLabsCredentialEnvironmentVariable.trimmed().isEmpty() ? QStringLiteral("ELEVENLABS_API_KEY") : preferences.elevenLabsCredentialEnvironmentVariable.trimmed();
	}
	if (normalized == QStringLiteral("meshy")) {
		return preferences.meshyCredentialEnvironmentVariable.trimmed().isEmpty() ? QStringLiteral("MESHY_API_KEY") : preferences.meshyCredentialEnvironmentVariable.trimmed();
	}
	if (normalized == QStringLiteral("custom-http")) {
		return preferences.customHttpCredentialEnvironmentVariable.trimmed();
	}
	if (normalized == QStringLiteral("claude")) {
		return QStringLiteral("ANTHROPIC_API_KEY");
	}
	if (normalized == QStringLiteral("gemini")) {
		return QStringLiteral("GEMINI_API_KEY");
	}
	return {};
}

AiCredentialStatus aiCredentialStatusForConnector(const QString& connectorId, const AiAutomationPreferences& preferences)
{
	AiCredentialStatus status;
	AiConnectorDescriptor connector;
	if (!aiConnectorForId(connectorId, &connector)) {
		status.connectorId = normalizedAiId(connectorId);
		status.source = QStringLiteral("unknown");
		status.warnings.push_back(aiText("Unknown AI connector."));
		return status;
	}

	status.connectorId = connector.id;
	if (!connector.cloudBased) {
		status.source = QStringLiteral("not-required");
		status.configured = true;
		status.redactedValue = aiText("not required");
		return status;
	}

	status.lookupKey = aiCredentialEnvironmentVariableForConnector(connector.id, preferences);
	if (status.lookupKey.trimmed().isEmpty()) {
		status.source = QStringLiteral("not-configured");
		status.warnings.push_back(aiText("No credential environment variable is configured."));
		return status;
	}

	status.source = QStringLiteral("environment");
	status.configured = qEnvironmentVariableIsSet(status.lookupKey.toUtf8().constData());
	status.redactedValue = status.configured ? QStringLiteral("%1=***").arg(status.lookupKey) : QStringLiteral("%1=<missing>").arg(status.lookupKey);
	if (!status.configured) {
		status.warnings.push_back(aiText("Credential was not found in the environment."));
	}
	return status;
}

QVector<AiCredentialStatus> aiCredentialStatuses(const AiAutomationPreferences& preferences)
{
	QVector<AiCredentialStatus> statuses;
	for (const AiConnectorDescriptor& connector : aiConnectorDescriptors()) {
		statuses.push_back(aiCredentialStatusForConnector(connector.id, preferences));
	}
	return statuses;
}

QString defaultAiReasoningConnectorId()
{
	return QStringLiteral("openai");
}

AiAutomationPreferences defaultAiAutomationPreferences()
{
	return {};
}

AiAutomationPreferences normalizedAiAutomationPreferences(const AiAutomationPreferences& preferences)
{
	AiAutomationPreferences normalized = preferences;
	if (normalized.aiFreeMode) {
		normalized.cloudConnectorsEnabled = false;
		normalized.agenticWorkflowsEnabled = false;
	}
	if (!normalized.cloudConnectorsEnabled) {
		normalized.agenticWorkflowsEnabled = false;
	}
	normalized.preferredReasoningConnectorId = normalizedPreferredConnector(normalized.preferredReasoningConnectorId, QStringLiteral("reasoning"));
	normalized.preferredCodingConnectorId = normalizedPreferredConnector(normalized.preferredCodingConnectorId, QStringLiteral("coding"));
	normalized.preferredVisionConnectorId = normalizedPreferredConnector(normalized.preferredVisionConnectorId, QStringLiteral("vision"));
	normalized.preferredImageConnectorId = normalizedPreferredConnector(normalized.preferredImageConnectorId, QStringLiteral("image"));
	normalized.preferredAudioConnectorId = normalizedPreferredConnector(normalized.preferredAudioConnectorId, QStringLiteral("audio"));
	normalized.preferredVoiceConnectorId = normalizedPreferredConnector(normalized.preferredVoiceConnectorId, QStringLiteral("voice"));
	normalized.preferredThreeDConnectorId = normalizedPreferredConnector(normalized.preferredThreeDConnectorId, QStringLiteral("three-d"));
	normalized.preferredEmbeddingsConnectorId = normalizedPreferredConnector(normalized.preferredEmbeddingsConnectorId, QStringLiteral("embeddings"));
	normalized.preferredLocalConnectorId = normalizedPreferredConnector(normalized.preferredLocalConnectorId, QStringLiteral("local-offline"));
	normalized.preferredTextModelId = normalizedPreferredModel(normalized.preferredTextModelId, QStringLiteral("reasoning"));
	normalized.preferredCodingModelId = normalizedPreferredModel(normalized.preferredCodingModelId, QStringLiteral("coding"));
	normalized.preferredVisionModelId = normalizedPreferredModel(normalized.preferredVisionModelId, QStringLiteral("vision"));
	normalized.preferredImageModelId = normalizedPreferredModel(normalized.preferredImageModelId, QStringLiteral("image"));
	normalized.preferredAudioModelId = normalizedPreferredModel(normalized.preferredAudioModelId, QStringLiteral("audio"));
	normalized.preferredVoiceModelId = normalizedPreferredModel(normalized.preferredVoiceModelId, QStringLiteral("voice"));
	normalized.preferredThreeDModelId = normalizedPreferredModel(normalized.preferredThreeDModelId, QStringLiteral("three-d"));
	normalized.preferredEmbeddingsModelId = normalizedPreferredModel(normalized.preferredEmbeddingsModelId, QStringLiteral("embeddings"));
	normalized.openAiCredentialEnvironmentVariable = normalized.openAiCredentialEnvironmentVariable.trimmed().isEmpty() ? QStringLiteral("OPENAI_API_KEY") : normalized.openAiCredentialEnvironmentVariable.trimmed();
	normalized.elevenLabsCredentialEnvironmentVariable = normalized.elevenLabsCredentialEnvironmentVariable.trimmed().isEmpty() ? QStringLiteral("ELEVENLABS_API_KEY") : normalized.elevenLabsCredentialEnvironmentVariable.trimmed();
	normalized.meshyCredentialEnvironmentVariable = normalized.meshyCredentialEnvironmentVariable.trimmed().isEmpty() ? QStringLiteral("MESHY_API_KEY") : normalized.meshyCredentialEnvironmentVariable.trimmed();
	normalized.customHttpCredentialEnvironmentVariable = normalized.customHttpCredentialEnvironmentVariable.trimmed();
	return normalized;
}

QString aiAutomationPreferencesText(const AiAutomationPreferences& preferences)
{
	const AiAutomationPreferences normalized = normalizedAiAutomationPreferences(preferences);
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioAiConnectors", "AI-free mode: %1").arg(normalized.aiFreeMode ? aiText("enabled") : aiText("disabled"));
	lines << QCoreApplication::translate("VibeStudioAiConnectors", "Cloud connectors: %1").arg(normalized.cloudConnectorsEnabled ? aiText("enabled") : aiText("disabled"));
	lines << QCoreApplication::translate("VibeStudioAiConnectors", "Agentic workflows: %1").arg(normalized.agenticWorkflowsEnabled ? aiText("enabled") : aiText("disabled"));
	lines << preferredConnectorText(aiText("Reasoning connector"), normalized.preferredReasoningConnectorId);
	lines << preferredConnectorText(aiText("Coding connector"), normalized.preferredCodingConnectorId);
	lines << preferredConnectorText(aiText("Vision connector"), normalized.preferredVisionConnectorId);
	lines << preferredConnectorText(aiText("Image connector"), normalized.preferredImageConnectorId);
	lines << preferredConnectorText(aiText("Audio connector"), normalized.preferredAudioConnectorId);
	lines << preferredConnectorText(aiText("Voice connector"), normalized.preferredVoiceConnectorId);
	lines << preferredConnectorText(aiText("3D connector"), normalized.preferredThreeDConnectorId);
	lines << preferredConnectorText(aiText("Embeddings connector"), normalized.preferredEmbeddingsConnectorId);
	lines << preferredConnectorText(aiText("Local connector"), normalized.preferredLocalConnectorId);
	lines << preferredModelText(aiText("Text model"), normalized.preferredTextModelId);
	lines << preferredModelText(aiText("Coding model"), normalized.preferredCodingModelId);
	lines << preferredModelText(aiText("Vision model"), normalized.preferredVisionModelId);
	lines << preferredModelText(aiText("Image model"), normalized.preferredImageModelId);
	lines << preferredModelText(aiText("Audio model"), normalized.preferredAudioModelId);
	lines << preferredModelText(aiText("Voice model"), normalized.preferredVoiceModelId);
	lines << preferredModelText(aiText("3D model"), normalized.preferredThreeDModelId);
	lines << preferredModelText(aiText("Embeddings model"), normalized.preferredEmbeddingsModelId);
	lines << QCoreApplication::translate("VibeStudioAiConnectors", "OpenAI credential source: environment variable %1").arg(normalized.openAiCredentialEnvironmentVariable);
	lines << QCoreApplication::translate("VibeStudioAiConnectors", "ElevenLabs credential source: environment variable %1").arg(normalized.elevenLabsCredentialEnvironmentVariable);
	lines << QCoreApplication::translate("VibeStudioAiConnectors", "Meshy credential source: environment variable %1").arg(normalized.meshyCredentialEnvironmentVariable);
	return lines.join('\n');
}

QString aiConnectorSummaryText(const AiConnectorDescriptor& connector)
{
	QStringList lines;
	lines << QStringLiteral("%1 [%2]").arg(connector.displayName, connector.id);
	lines << QCoreApplication::translate("VibeStudioAiConnectors", "Provider: %1").arg(connector.providerFamily);
	lines << QCoreApplication::translate("VibeStudioAiConnectors", "Endpoint: %1").arg(connector.endpointKind);
	lines << QCoreApplication::translate("VibeStudioAiConnectors", "Location: %1").arg(connector.cloudBased ? aiText("cloud") : aiText("local/offline"));
	lines << QCoreApplication::translate("VibeStudioAiConnectors", "Implemented: %1").arg(connector.implemented ? aiText("yes") : aiText("design stub"));
	lines << QCoreApplication::translate("VibeStudioAiConnectors", "Capabilities: %1").arg(connector.capabilities.join(QStringLiteral(", ")));
	lines << QCoreApplication::translate("VibeStudioAiConnectors", "Authentication: %1").arg(connector.authRequirement);
	lines << QCoreApplication::translate("VibeStudioAiConnectors", "Privacy: %1").arg(connector.privacyNote);
	for (const QString& note : connector.configurationNotes) {
		lines << QStringLiteral("- %1").arg(note);
	}
	return lines.join('\n');
}

} // namespace vibestudio
