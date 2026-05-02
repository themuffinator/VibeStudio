#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct AiCapabilityDescriptor {
	QString id;
	QString displayName;
	QString description;
};

struct AiConnectorDescriptor {
	QString id;
	QString displayName;
	QString providerFamily;
	QString endpointKind;
	QString authRequirement;
	QString privacyNote;
	QStringList capabilities;
	QStringList configurationNotes;
	bool cloudBased = true;
	bool implemented = false;
};

struct AiModelDescriptor {
	QString id;
	QString connectorId;
	QString displayName;
	QString description;
	QStringList capabilities;
	bool defaultForConnector = false;
};

struct AiCredentialStatus {
	QString connectorId;
	bool configured = false;
	QString source;
	QString lookupKey;
	QString redactedValue;
	QStringList warnings;
};

struct AiAutomationPreferences {
	bool aiFreeMode = true;
	bool cloudConnectorsEnabled = false;
	bool agenticWorkflowsEnabled = false;
	QString preferredReasoningConnectorId;
	QString preferredCodingConnectorId;
	QString preferredVisionConnectorId;
	QString preferredImageConnectorId;
	QString preferredAudioConnectorId;
	QString preferredVoiceConnectorId;
	QString preferredThreeDConnectorId;
	QString preferredEmbeddingsConnectorId;
	QString preferredLocalConnectorId;
	QString preferredTextModelId;
	QString preferredCodingModelId;
	QString preferredVisionModelId;
	QString preferredImageModelId;
	QString preferredAudioModelId;
	QString preferredVoiceModelId;
	QString preferredThreeDModelId;
	QString preferredEmbeddingsModelId;
	QString openAiCredentialEnvironmentVariable = QStringLiteral("OPENAI_API_KEY");
	QString elevenLabsCredentialEnvironmentVariable = QStringLiteral("ELEVENLABS_API_KEY");
	QString meshyCredentialEnvironmentVariable = QStringLiteral("MESHY_API_KEY");
	QString customHttpCredentialEnvironmentVariable;
};

QVector<AiCapabilityDescriptor> aiCapabilityDescriptors();
QStringList aiCapabilityIds();
bool aiCapabilityForId(const QString& id, AiCapabilityDescriptor* out = nullptr);

QVector<AiConnectorDescriptor> aiConnectorDescriptors();
QStringList aiConnectorIds();
bool aiConnectorForId(const QString& id, AiConnectorDescriptor* out = nullptr);
bool aiConnectorSupportsCapability(const QString& connectorId, const QString& capabilityId);

QString defaultAiReasoningConnectorId();
QString normalizedAiId(const QString& id);
AiAutomationPreferences defaultAiAutomationPreferences();

QVector<AiModelDescriptor> aiModelDescriptors();
QVector<AiModelDescriptor> aiModelDescriptorsForConnector(const QString& connectorId);
QStringList aiModelIds();
bool aiModelForId(const QString& id, AiModelDescriptor* out = nullptr);
bool aiModelSupportsCapability(const QString& modelId, const QString& capabilityId);
QString defaultAiModelId(const QString& connectorId, const QString& capabilityId = QStringLiteral("reasoning"));
QString normalizedAiModelId(const QString& modelId, const QString& capabilityId = QString());

QString aiCredentialEnvironmentVariableForConnector(const QString& connectorId, const AiAutomationPreferences& preferences = defaultAiAutomationPreferences());
AiCredentialStatus aiCredentialStatusForConnector(const QString& connectorId, const AiAutomationPreferences& preferences = defaultAiAutomationPreferences());
QVector<AiCredentialStatus> aiCredentialStatuses(const AiAutomationPreferences& preferences = defaultAiAutomationPreferences());

AiAutomationPreferences normalizedAiAutomationPreferences(const AiAutomationPreferences& preferences);
QString aiAutomationPreferencesText(const AiAutomationPreferences& preferences);
QString aiConnectorSummaryText(const AiConnectorDescriptor& connector);

} // namespace vibestudio
