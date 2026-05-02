#include "core/ai_connectors.h"

#include <QCoreApplication>
#include <QSet>

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);

	const QVector<vibestudio::AiCapabilityDescriptor> capabilities = vibestudio::aiCapabilityDescriptors();
	if (capabilities.size() < 10 || !vibestudio::aiCapabilityIds().contains(QStringLiteral("tool-calls"))) {
		return fail("Expected provider-neutral AI capabilities.");
	}
	if (!vibestudio::aiCapabilityForId(QStringLiteral("THREE_D"))) {
		return fail("Expected AI capability id normalization.");
	}

	const QVector<vibestudio::AiConnectorDescriptor> connectors = vibestudio::aiConnectorDescriptors();
	if (connectors.size() < 7) {
		return fail("Expected all planned AI connector stubs.");
	}

	QSet<QString> ids;
	for (const vibestudio::AiConnectorDescriptor& connector : connectors) {
		if (connector.id.isEmpty() || connector.displayName.isEmpty() || connector.providerFamily.isEmpty()) {
			return fail("Expected connector identity.");
		}
		if (ids.contains(connector.id)) {
			return fail("Expected connector ids to be unique.");
		}
		ids.insert(connector.id);
		if (connector.capabilities.isEmpty() || connector.authRequirement.isEmpty() || connector.privacyNote.isEmpty()) {
			return fail("Expected connector capability, auth, and privacy metadata.");
		}
		if (connector.id == QStringLiteral("openai") && !connector.implemented) {
			return fail("Expected OpenAI connector scaffold to be implemented first.");
		}
		if (connector.id != QStringLiteral("openai") && connector.implemented) {
			return fail("Expected non-OpenAI connectors to remain design stubs.");
		}
	}

	if (!ids.contains(QStringLiteral("openai")) || !ids.contains(QStringLiteral("claude")) || !ids.contains(QStringLiteral("gemini")) || !ids.contains(QStringLiteral("elevenlabs")) || !ids.contains(QStringLiteral("meshy")) || !ids.contains(QStringLiteral("local-offline")) || !ids.contains(QStringLiteral("custom-http"))) {
		return fail("Expected named connector design stubs.");
	}
	if (!vibestudio::aiConnectorSupportsCapability(QStringLiteral("OpenAI"), QStringLiteral("tool_calls"))) {
		return fail("Expected connector capability normalization.");
	}
	if (vibestudio::aiConnectorSupportsCapability(QStringLiteral("elevenlabs"), QStringLiteral("three-d"))) {
		return fail("Expected unsupported connector capability to be rejected.");
	}
	if (vibestudio::aiModelDescriptors().size() < 7 || !vibestudio::aiModelForId(QStringLiteral("OPENAI_TEXT_DEFAULT"))) {
		return fail("Expected configurable AI model descriptors.");
	}
	if (!vibestudio::aiModelSupportsCapability(QStringLiteral("openai-text-default"), QStringLiteral("tool_calls"))) {
		return fail("Expected model capability normalization.");
	}
	if (vibestudio::aiCredentialStatusForConnector(QStringLiteral("local-offline")).configured != true) {
		return fail("Expected local/offline connector not to require cloud credentials.");
	}

	vibestudio::AiAutomationPreferences defaults = vibestudio::defaultAiAutomationPreferences();
	defaults.cloudConnectorsEnabled = true;
	defaults.agenticWorkflowsEnabled = true;
	defaults.preferredReasoningConnectorId = QStringLiteral("openai");
	const vibestudio::AiAutomationPreferences normalizedDefault = vibestudio::normalizedAiAutomationPreferences(defaults);
	if (!normalizedDefault.aiFreeMode || normalizedDefault.cloudConnectorsEnabled || normalizedDefault.agenticWorkflowsEnabled) {
		return fail("Expected AI-free mode to disable cloud and agentic settings.");
	}

	vibestudio::AiAutomationPreferences optIn;
	optIn.aiFreeMode = false;
	optIn.cloudConnectorsEnabled = true;
	optIn.agenticWorkflowsEnabled = true;
	optIn.preferredReasoningConnectorId = QStringLiteral("openai");
	optIn.preferredThreeDConnectorId = QStringLiteral("meshy");
	optIn.preferredVoiceConnectorId = QStringLiteral("meshy");
	optIn.preferredTextModelId = QStringLiteral("openai-text-default");
	optIn.preferredImageModelId = QStringLiteral("openai-text-default");
	const vibestudio::AiAutomationPreferences normalizedOptIn = vibestudio::normalizedAiAutomationPreferences(optIn);
	if (normalizedOptIn.aiFreeMode || !normalizedOptIn.cloudConnectorsEnabled || !normalizedOptIn.agenticWorkflowsEnabled) {
		return fail("Expected explicit AI opt-in preferences to persist.");
	}
	if (normalizedOptIn.preferredReasoningConnectorId != QStringLiteral("openai") || normalizedOptIn.preferredThreeDConnectorId != QStringLiteral("meshy") || !normalizedOptIn.preferredVoiceConnectorId.isEmpty()) {
		return fail("Expected preferred connector capability validation.");
	}
	if (normalizedOptIn.preferredTextModelId != QStringLiteral("openai-text-default") || !normalizedOptIn.preferredImageModelId.isEmpty()) {
		return fail("Expected preferred model capability validation.");
	}
	if (!vibestudio::aiAutomationPreferencesText(normalizedOptIn).contains(QStringLiteral("OpenAI"))) {
		return fail("Expected AI preferences summary text.");
	}

	return EXIT_SUCCESS;
}
