#pragma once

#include "core/ai_workflows.h"
#include "core/operation_state.h"
#include "core/package_archive.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct AdvancedStudioIssue {
	QString severity = QStringLiteral("warning");
	QString code;
	QString message;
	QString objectId;
	int line = 0;
};

struct ShaderStage {
	int id = -1;
	QString shaderName;
	int startLine = 0;
	int endLine = 0;
	QString mapDirective;
	QString blendFunc;
	QString rgbGen;
	QString alphaGen;
	QString tcMod;
	QStringList directives;
	QStringList textureReferences;
	QString rawText;
	bool selected = false;
};

struct ShaderDefinition {
	int id = -1;
	QString name;
	int startLine = 0;
	int endLine = 0;
	QStringList directives;
	QVector<ShaderStage> stages;
	QStringList textureReferences;
	QString rawText;
	bool selected = false;
};

struct ShaderDocument {
	QString sourcePath;
	QString originalText;
	QVector<ShaderDefinition> shaders;
	QVector<AdvancedStudioIssue> issues;
	QString editState = QStringLiteral("clean");
};

struct ShaderReferenceValidation {
	QString textureReference;
	QStringList candidatePaths;
	bool found = false;
	QString foundInPackage;
};

struct ShaderSaveReport {
	QString sourcePath;
	QString outputPath;
	bool dryRun = false;
	bool written = false;
	QString editState;
	QStringList summaryLines;
	QStringList warnings;
	QStringList errors;

	[[nodiscard]] bool succeeded() const;
};

struct SpriteFramePlan {
	int index = 0;
	QString frameId;
	QString rotationId;
	QString lumpName;
	QString sourcePath;
	QString virtualPath;
	QString paletteAction;
};

struct SpriteWorkflowRequest {
	QString engineFamily = QStringLiteral("doom");
	QString spriteName;
	int frameCount = 1;
	int rotations = 1;
	QString paletteId = QStringLiteral("generic");
	QString outputPackageRoot = QStringLiteral("sprites");
	QStringList sourceFramePaths;
	bool stageForPackage = true;
};

struct SpriteWorkflowPlan {
	QString engineFamily;
	QString spriteName;
	QString paletteId;
	QString outputPackageRoot;
	QVector<SpriteFramePlan> frames;
	QStringList palettePreviewLines;
	QStringList sequenceLines;
	QStringList stagingLines;
	QStringList warnings;
	OperationState state = OperationState::Completed;
};

struct CodeSourceFile {
	QString filePath;
	QString relativePath;
	QString languageId;
	qint64 bytes = 0;
	int lineCount = 0;
};

struct CodeSymbol {
	QString name;
	QString kind;
	QString filePath;
	QString relativePath;
	int line = 0;
	int column = 0;
};

struct LanguageServiceHook {
	QString languageId;
	QString displayName;
	QStringList extensions;
	QStringList capabilities;
	QString serverCommand;
	bool available = false;
	QString status;
};

struct CodeDiagnostic {
	QString severity = QStringLiteral("info");
	QString message;
	QString filePath;
	QString relativePath;
	int line = 0;
	int column = 0;
};

struct CodeWorkspaceIndexRequest {
	QString rootPath;
	QStringList extensions;
	QString symbolQuery;
	int maxFiles = 512;
};

struct CodeWorkspaceIndex {
	QString rootPath;
	QVector<CodeSourceFile> files;
	QVector<CodeSymbol> symbols;
	QVector<LanguageServiceHook> languageHooks;
	QVector<CodeDiagnostic> diagnostics;
	QStringList treeLines;
	QStringList buildTaskLines;
	QStringList launchProfileLines;
	QStringList warnings;
	OperationState state = OperationState::Completed;
};

struct ExtensionGeneratedFile {
	QString virtualPath;
	QString sourceDescription;
	QString summary;
	bool staged = true;
};

struct ExtensionCommandDescriptor {
	QString id;
	QString displayName;
	QString description;
	QString program;
	QStringList arguments;
	QString workingDirectory;
	QStringList capabilities;
	QVector<ExtensionGeneratedFile> generatedFiles;
	bool requiresApproval = true;
};

struct ExtensionManifest {
	static constexpr int kSchemaVersion = 1;

	int schemaVersion = kSchemaVersion;
	QString manifestPath;
	QString rootPath;
	QString id;
	QString displayName;
	QString version;
	QString description;
	QString trustLevel = QStringLiteral("workspace");
	QString sandboxModel = QStringLiteral("command-plan");
	QStringList capabilities;
	QVector<ExtensionCommandDescriptor> commands;
	QStringList warnings;
};

struct ExtensionDiscoveryResult {
	QStringList searchRoots;
	QVector<ExtensionManifest> manifests;
	QStringList warnings;
	OperationState state = OperationState::Completed;
};

struct ExtensionCommandPlan {
	ExtensionManifest manifest;
	ExtensionCommandDescriptor command;
	QString program;
	QStringList arguments;
	QString workingDirectory;
	bool dryRun = true;
	bool executionAllowed = false;
	OperationState state = OperationState::Warning;
	QStringList warnings;
	QStringList stagingLines;
};

struct ExtensionCommandResult {
	ExtensionCommandPlan plan;
	bool started = false;
	bool dryRun = true;
	int exitCode = -1;
	QString stdoutText;
	QString stderrText;
	QString error;
	OperationState state = OperationState::Warning;
	QDateTime finishedUtc;
};

QStringList advancedStudioCapabilityLines();

ShaderDocument parseShaderScriptText(const QString& text, const QString& sourcePath = QString());
bool loadShaderScript(const QString& path, ShaderDocument* document, QString* error = nullptr);
bool setShaderStageDirective(ShaderDocument* document, const QString& shaderName, int stageIndex, const QString& directive, const QString& value, QString* error = nullptr);
QString shaderDocumentText(const ShaderDocument& document);
QStringList shaderGraphLines(const ShaderDocument& document);
QStringList shaderStagePreviewLines(const ShaderDocument& document, const QString& shaderName = QString(), int stageIndex = -1);
QVector<ShaderReferenceValidation> validateShaderReferences(const ShaderDocument& document, const QStringList& mountedPackagePaths, QStringList* warnings = nullptr);
QStringList shaderReferenceValidationLines(const QVector<ShaderReferenceValidation>& validation, const QStringList& warnings = {});
ShaderSaveReport saveShaderScriptAs(const ShaderDocument& document, const QString& outputPath, bool dryRun = false, bool overwriteExisting = false);
QString shaderDocumentReportText(const ShaderDocument& document, const QVector<ShaderReferenceValidation>& validation = {}, const QStringList& validationWarnings = {});
QString shaderSaveReportText(const ShaderSaveReport& report);

SpriteWorkflowPlan buildSpriteWorkflowPlan(const SpriteWorkflowRequest& request);
QString spriteWorkflowPlanText(const SpriteWorkflowPlan& plan);

QVector<LanguageServiceHook> defaultLanguageServiceHooks();
CodeWorkspaceIndex indexCodeWorkspace(const CodeWorkspaceIndexRequest& request);
QString codeWorkspaceIndexText(const CodeWorkspaceIndex& index);

QStringList extensionTrustModelLines();
bool loadExtensionManifest(const QString& manifestPath, ExtensionManifest* manifest, QString* error = nullptr);
ExtensionDiscoveryResult discoverExtensions(const QStringList& searchRoots);
ExtensionCommandPlan buildExtensionCommandPlan(const ExtensionManifest& manifest, const QString& commandId, const QStringList& extraArguments = {}, bool dryRun = true, bool allowExecution = false);
ExtensionCommandResult runExtensionCommand(const ExtensionCommandPlan& plan, int timeoutMs = 30000);
QString extensionManifestText(const ExtensionManifest& manifest);
QString extensionDiscoveryText(const ExtensionDiscoveryResult& result);
QString extensionCommandPlanText(const ExtensionCommandPlan& plan);
QString extensionCommandResultText(const ExtensionCommandResult& result);

AiWorkflowResult promptToShaderScaffoldAiExperiment(const QString& prompt, const AiAutomationPreferences& preferences, const QString& providerId = QString(), const QString& modelId = QString());
AiWorkflowResult promptToEntityDefinitionAiExperiment(const QString& prompt, const AiAutomationPreferences& preferences, const QString& providerId = QString(), const QString& modelId = QString());
AiWorkflowResult promptToPackageValidationPlanAiExperiment(const QString& prompt, const QString& packagePath, const AiAutomationPreferences& preferences, const QString& providerId = QString(), const QString& modelId = QString());
AiWorkflowResult promptToBatchConversionRecipeAiExperiment(const QString& prompt, const AiAutomationPreferences& preferences, const QString& providerId = QString(), const QString& modelId = QString());
QString aiProposalReviewSurfaceText(const AiWorkflowResult& result);

} // namespace vibestudio
