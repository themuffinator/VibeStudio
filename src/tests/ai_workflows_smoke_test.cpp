#include "core/ai_workflows.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

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

	const QVector<vibestudio::AiToolDescriptor> tools = vibestudio::aiToolDescriptors();
	if (tools.size() < 6 || !vibestudio::aiToolIds().contains(QStringLiteral("compiler-command-proposal"))) {
		return fail("Expected AI-callable tool descriptors.");
	}
	vibestudio::AiToolDescriptor stagedEdit;
	if (!vibestudio::aiToolForId(QStringLiteral("STAGED_TEXT_EDIT"), &stagedEdit) || !stagedEdit.requiresApproval || !stagedEdit.writesFiles) {
		return fail("Expected staged text edit tool safety metadata.");
	}

	vibestudio::AiAutomationPreferences preferences;
	preferences.aiFreeMode = false;
	preferences.cloudConnectorsEnabled = true;
	preferences.preferredReasoningConnectorId = QStringLiteral("openai");
	preferences.preferredCodingConnectorId = QStringLiteral("openai");
	preferences.preferredTextModelId = QStringLiteral("openai-text-default");

	const vibestudio::AiWorkflowResult explanation = vibestudio::explainCompilerLogAiExperiment(QStringLiteral("ERROR: maps/start.map:12 leak detected\nwarning: missing texture"), preferences);
	if (explanation.manifest.workflowId != QStringLiteral("explain-compiler-log") || explanation.manifest.toolCalls.isEmpty() || explanation.manifest.stagedOutputs.isEmpty()) {
		return fail("Expected compiler-log explanation manifest.");
	}
	if (!explanation.reviewableText.contains(QStringLiteral("Findings"))) {
		return fail("Expected reviewable compiler-log explanation text.");
	}

	const vibestudio::AiWorkflowResult proposal = vibestudio::proposeCompilerCommandAiExperiment(QStringLiteral("build quake map maps/start.map with qbsp"), QString(), preferences);
	if (!proposal.commandLine.contains(QStringLiteral("compiler plan")) || !proposal.commandLine.contains(QStringLiteral("--dry-run"))) {
		return fail("Expected reviewable compiler command proposal.");
	}

	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected temporary directory.");
	}
	const vibestudio::AiWorkflowResult manifestDraft = vibestudio::draftProjectManifestAiExperiment(tempDir.path(), QStringLiteral("AI Draft"), preferences);
	if (!manifestDraft.reviewableText.contains(QStringLiteral("AI Draft")) || !manifestDraft.manifest.stagedOutputs.front().writesFile) {
		return fail("Expected staged project manifest draft.");
	}

	const QString packageRoot = tempDir.filePath(QStringLiteral("package"));
	if (!QDir().mkpath(QDir(packageRoot).filePath(QStringLiteral("maps")))) {
		return fail("Expected package fixture directory.");
	}
	QFile mapFile(QDir(packageRoot).filePath(QStringLiteral("maps/start.bsp")));
	if (!mapFile.open(QIODevice::WriteOnly)) {
		return fail("Expected package fixture map file.");
	}
	mapFile.write("dummy");
	mapFile.close();
	const vibestudio::AiWorkflowResult deps = vibestudio::suggestPackageDependenciesAiExperiment(packageRoot, preferences);
	if (deps.manifest.workflowId != QStringLiteral("suggest-package-dependencies") || deps.diagnostics.isEmpty()) {
		return fail("Expected package dependency suggestions.");
	}

	const vibestudio::AiWorkflowResult cli = vibestudio::generateCliCommandAiExperiment(QStringLiteral("validate package release.pk3"), preferences);
	if (!cli.commandLine.contains(QStringLiteral("package validate"))) {
		return fail("Expected generated CLI command proposal.");
	}

	const vibestudio::AiWorkflowResult asset = vibestudio::stageAssetGenerationRequestAiExperiment(QStringLiteral("meshy"), QStringLiteral("texture"), QStringLiteral("rust panel"), preferences);
	if (asset.manifest.workflowId != QStringLiteral("stage-asset-generation-request") || asset.manifest.stagedOutputs.isEmpty()) {
		return fail("Expected staged asset generation request.");
	}

	const vibestudio::AiWorkflowManifest retried = vibestudio::retryAiWorkflowManifest(asset.manifest);
	if (retried.retryCount != asset.manifest.retryCount + 1 || !retried.cancellable) {
		return fail("Expected retry state metadata.");
	}
	if (vibestudio::cancelledAiWorkflowManifest(retried).state != vibestudio::OperationState::Cancelled) {
		return fail("Expected cancellation state metadata.");
	}

	return EXIT_SUCCESS;
}
