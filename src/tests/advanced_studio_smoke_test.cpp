#include "core/advanced_studio.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

bool writeText(const QString& path, const QString& text)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	file.write(text.toUtf8());
	return true;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "temporary directory should be valid")) {
		return EXIT_FAILURE;
	}

	bool ok = true;
	const QString shaderText = QStringLiteral(
		"textures/base/wall\n"
		"{\n"
		"\tqer_editorimage textures/base/wall_editor\n"
		"\t{\n"
		"\t\tmap textures/base/wall\n"
		"\t\trgbGen identity\n"
		"\t}\n"
		"\t{\n"
		"\t\tmap textures/base/glow\n"
		"\t\tblendFunc GL_ONE GL_ONE\n"
		"\t}\n"
		"}\n");
	const QString shaderPath = QDir(temp.path()).filePath(QStringLiteral("scripts/test.shader"));
	ok &= expect(writeText(shaderPath, shaderText), "shader fixture should write");
	ShaderDocument shader;
	QString error;
	ok &= expect(loadShaderScript(shaderPath, &shader, &error), "shader should load");
	ok &= expect(shader.shaders.size() == 1, "one shader should parse");
	ok &= expect(shader.shaders.first().stages.size() == 2, "two shader stages should parse");
	ok &= expect(shaderGraphLines(shader).join('\n').contains(QStringLiteral("textures/base/glow")), "shader graph should list texture dependencies");
	ok &= expect(setShaderStageDirective(&shader, QStringLiteral("textures/base/wall"), 1, QStringLiteral("blendFunc"), QStringLiteral("GL_DST_COLOR GL_ZERO"), &error), "shader stage directive should edit");
	ok &= expect(shaderDocumentText(shader).contains(QStringLiteral("GL_DST_COLOR GL_ZERO")), "shader round-trip text should include edit");

	const QString packageRoot = QDir(temp.path()).filePath(QStringLiteral("baseq3"));
	ok &= expect(writeText(QDir(packageRoot).filePath(QStringLiteral("textures/base/wall.tga")), QStringLiteral("placeholder")), "package texture should write");
	QStringList validationWarnings;
	const QVector<ShaderReferenceValidation> validation = validateShaderReferences(shader, {packageRoot}, &validationWarnings);
	const bool foundWall = std::any_of(validation.cbegin(), validation.cend(), [](const ShaderReferenceValidation& result) {
		return result.textureReference == QStringLiteral("textures/base/wall") && result.found;
	});
	ok &= expect(foundWall, "shader reference validation should find mounted package texture");
	const ShaderSaveReport shaderSave = saveShaderScriptAs(shader, QDir(temp.path()).filePath(QStringLiteral("out/test-edited.shader")), false, false);
	ok &= expect(shaderSave.succeeded(), "shader save-as should succeed");

	SpriteWorkflowRequest doomSprite;
	doomSprite.engineFamily = QStringLiteral("doom");
	doomSprite.spriteName = QStringLiteral("TROO");
	doomSprite.frameCount = 2;
	doomSprite.rotations = 8;
	const SpriteWorkflowPlan doomPlan = buildSpriteWorkflowPlan(doomSprite);
	ok &= expect(doomPlan.frames.size() == 16, "doom sprite plan should include frame/rotation lumps");
	ok &= expect(doomPlan.sequenceLines.join('\n').contains(QStringLiteral("TROOA1")), "doom sprite naming should include lump names");

	SpriteWorkflowRequest quakeSprite;
	quakeSprite.engineFamily = QStringLiteral("quake");
	quakeSprite.spriteName = QStringLiteral("torch");
	quakeSprite.frameCount = 3;
	const SpriteWorkflowPlan quakePlan = buildSpriteWorkflowPlan(quakeSprite);
	ok &= expect(quakePlan.frames.size() == 3, "quake sprite plan should include frame sequence");
	ok &= expect(quakePlan.stagingLines.join('\n').contains(QStringLiteral(".spr")), "quake sprite plan should stage spr manifest");

	const QString projectRoot = QDir(temp.path()).filePath(QStringLiteral("project"));
	ok &= expect(writeText(QDir(projectRoot).filePath(QStringLiteral("progs/monster.qc")), QStringLiteral("void monster_spawn() {\n\t// TODO test\n}\n")), "code fixture should write");
	ok &= expect(writeText(QDir(projectRoot).filePath(QStringLiteral("scripts/local.shader")), shaderText), "shader source fixture should write");
	CodeWorkspaceIndexRequest indexRequest;
	indexRequest.rootPath = projectRoot;
	indexRequest.symbolQuery = QStringLiteral("monster");
	const CodeWorkspaceIndex codeIndex = indexCodeWorkspace(indexRequest);
	ok &= expect(codeIndex.files.size() >= 2, "code index should scan source files");
	ok &= expect(!codeIndex.symbols.isEmpty(), "code index should find symbols");
	ok &= expect(!codeIndex.languageHooks.isEmpty(), "language service hooks should be described");
	ok &= expect(!codeIndex.buildTaskLines.isEmpty(), "build task integration should be described");
	ok &= expect(!codeIndex.launchProfileLines.isEmpty(), "launch profiles should be described");

	const QString extensionRoot = QDir(temp.path()).filePath(QStringLiteral("extensions/sample"));
	const QString manifestPath = QDir(extensionRoot).filePath(QStringLiteral("vibestudio.extension.json"));
	const QString manifestText = QStringLiteral(
		"{\n"
		"  \"schemaVersion\": 1,\n"
		"  \"id\": \"sample-tools\",\n"
		"  \"displayName\": \"Sample Tools\",\n"
		"  \"version\": \"1.0.0\",\n"
		"  \"trustLevel\": \"workspace\",\n"
		"  \"sandbox\": \"staged-files\",\n"
		"  \"capabilities\": [\"shader\", \"sprite\"],\n"
		"  \"commands\": [{\n"
		"    \"id\": \"make-file\",\n"
		"    \"displayName\": \"Make File\",\n"
		"    \"program\": \"echo\",\n"
		"    \"arguments\": [\"generated\"],\n"
		"    \"generatedFiles\": [{\"virtualPath\": \"scripts/generated.shader\", \"summary\": \"Generated shader\"}]\n"
		"  }]\n"
		"}\n");
	ok &= expect(writeText(manifestPath, manifestText), "extension manifest should write");
	ExtensionManifest extension;
	ok &= expect(loadExtensionManifest(manifestPath, &extension, &error), "extension manifest should load");
	ok &= expect(extension.commands.size() == 1, "extension command should parse");
	const ExtensionDiscoveryResult discovery = discoverExtensions({QDir(temp.path()).filePath(QStringLiteral("extensions"))});
	ok &= expect(discovery.manifests.size() == 1, "extension discovery should find manifest");
	const ExtensionCommandPlan commandPlan = buildExtensionCommandPlan(extension, QStringLiteral("make-file"), {}, true, false);
	ok &= expect(commandPlan.stagingLines.join('\n').contains(QStringLiteral("scripts/generated.shader")), "extension generated files should be staged");

	AiAutomationPreferences preferences;
	const AiWorkflowResult shaderProposal = promptToShaderScaffoldAiExperiment(QStringLiteral("glowing gothic wall"), preferences);
	ok &= expect(shaderProposal.reviewableText.contains(QStringLiteral("blendFunc")), "prompt-to-shader should produce shader text");
	const AiWorkflowResult entityProposal = promptToEntityDefinitionAiExperiment(QStringLiteral("trigger that starts a lift"), preferences);
	ok &= expect(entityProposal.reviewableText.contains(QStringLiteral("classname")), "prompt-to-entity should produce entity text");
	const AiWorkflowResult packageProposal = promptToPackageValidationPlanAiExperiment(QStringLiteral("validate release"), QStringLiteral("release.pk3"), preferences);
	ok &= expect(packageProposal.reviewableText.contains(QStringLiteral("package validate")), "prompt-to-package-validation should produce commands");
	const AiWorkflowResult conversionProposal = promptToBatchConversionRecipeAiExperiment(QStringLiteral("convert doom sprites"), preferences);
	ok &= expect(conversionProposal.reviewableText.contains(QStringLiteral("asset convert")), "prompt-to-batch-conversion should produce recipe");
	ok &= expect(aiProposalReviewSurfaceText(shaderProposal).contains(QStringLiteral("AI Proposal Review")), "AI review surface should include proposal sections");

	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
