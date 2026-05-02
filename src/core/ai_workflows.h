#pragma once

#include "core/ai_connectors.h"
#include "core/operation_state.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct AiToolDescriptor {
	QString id;
	QString displayName;
	QString description;
	QStringList capabilities;
	QStringList contextInputs;
	QStringList stagedOutputs;
	bool requiresApproval = false;
	bool writesFiles = false;
};

struct AiWorkflowToolCall {
	QString toolId;
	QString summary;
	OperationState state = OperationState::Completed;
	QStringList inputs;
	QStringList outputs;
	QStringList warnings;
	bool requiresApproval = false;
	bool writesFiles = false;
};

struct AiStagedOutput {
	QString id;
	QString kind;
	QString label;
	QString proposedPath;
	QString summary;
	QStringList previewLines;
	bool writesFile = false;
};

struct AiWorkflowManifest {
	static constexpr int kSchemaVersion = 1;

	int schemaVersion = kSchemaVersion;
	QString manifestId;
	QDateTime createdUtc;
	QString workflowId;
	QString providerId;
	QString modelId;
	QString prompt;
	QString contextSummary;
	QString redactionSummary;
	QVector<AiWorkflowToolCall> toolCalls;
	QVector<AiStagedOutput> stagedOutputs;
	QString approvalState = QStringLiteral("preview");
	QString validationSummary;
	QString costUsageSummary;
	OperationState state = OperationState::Completed;
	bool cancellable = false;
	int retryCount = 0;
	bool writesApplied = false;
	QStringList warnings;
};

struct AiWorkflowResult {
	QString title;
	QString summary;
	QString reviewableText;
	QString commandLine;
	QStringList nextActions;
	QStringList diagnostics;
	AiWorkflowManifest manifest;
};

QVector<AiToolDescriptor> aiToolDescriptors();
QStringList aiToolIds();
bool aiToolForId(const QString& id, AiToolDescriptor* out = nullptr);

AiWorkflowManifest defaultAiWorkflowManifest(const QString& workflowId, const QString& providerId, const QString& modelId, const QString& prompt);
AiWorkflowManifest cancelledAiWorkflowManifest(AiWorkflowManifest manifest);
AiWorkflowManifest retryAiWorkflowManifest(AiWorkflowManifest manifest);

QJsonObject aiToolDescriptorJson(const AiToolDescriptor& tool);
QJsonObject aiWorkflowManifestJson(const AiWorkflowManifest& manifest);
QString aiWorkflowManifestText(const AiWorkflowManifest& manifest);
bool saveAiWorkflowManifest(const AiWorkflowManifest& manifest, const QString& path, QString* error = nullptr);

AiWorkflowResult explainCompilerLogAiExperiment(const QString& logText, const AiAutomationPreferences& preferences, const QString& providerId = QString(), const QString& modelId = QString());
AiWorkflowResult proposeCompilerCommandAiExperiment(const QString& prompt, const QString& workspaceRootPath, const AiAutomationPreferences& preferences, const QString& providerId = QString(), const QString& modelId = QString());
AiWorkflowResult draftProjectManifestAiExperiment(const QString& projectRootPath, const QString& displayName, const AiAutomationPreferences& preferences, const QString& providerId = QString(), const QString& modelId = QString());
AiWorkflowResult suggestPackageDependenciesAiExperiment(const QString& packagePath, const AiAutomationPreferences& preferences, const QString& providerId = QString(), const QString& modelId = QString());
AiWorkflowResult generateCliCommandAiExperiment(const QString& prompt, const AiAutomationPreferences& preferences, const QString& providerId = QString(), const QString& modelId = QString());
AiWorkflowResult fixAndRetryPlanAiExperiment(const QString& compilerLogText, const QString& commandLine, const AiAutomationPreferences& preferences, const QString& providerId = QString(), const QString& modelId = QString());
AiWorkflowResult stageAssetGenerationRequestAiExperiment(const QString& connectorId, const QString& kind, const QString& prompt, const AiAutomationPreferences& preferences, const QString& modelId = QString());
AiWorkflowResult compareProviderOutputsAiExperiment(const QString& prompt, const QString& providerA, const QString& providerB, const AiAutomationPreferences& preferences, const QString& modelA = QString(), const QString& modelB = QString());

} // namespace vibestudio
