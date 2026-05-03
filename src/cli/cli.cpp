#include "cli/cli.h"

#include "app/ui_primitives.h"
#include "core/advanced_studio.h"
#include "core/ai_connectors.h"
#include "core/ai_workflows.h"
#include "core/asset_tools.h"
#include "core/compiler_profiles.h"
#include "core/compiler_registry.h"
#include "core/compiler_runner.h"
#include "core/editor_profiles.h"
#include "core/level_map.h"
#include "core/localization.h"
#include "core/operation_state.h"
#include "core/package_archive.h"
#include "core/package_preview.h"
#include "core/package_staging.h"
#include "core/project_manifest.h"
#include "core/studio_semantics.h"
#include "core/studio_manifest.h"
#include "core/studio_settings.h"
#include "vibestudio_config.h"

#include <QCoreApplication>
#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QSysInfo>

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <streambuf>
#include <string>

namespace vibestudio::cli {

namespace {

enum class CliExitCode {
	Success = 0,
	Failure = 1,
	Usage = 2,
	NotFound = 3,
	ValidationFailed = 4,
	Unavailable = 5,
};

enum class CliOutputFormat {
	Text,
	Json,
};

struct NullOutputBuffer final : std::streambuf {
	int overflow(int character) override { return character; }
};

QStringList& currentCliArgs()
{
	static QStringList args;
	return args;
}

bool quietOutput()
{
	return currentCliArgs().contains(QStringLiteral("--quiet")) && !currentCliArgs().contains(QStringLiteral("--json"));
}

bool verboseOutput()
{
	return currentCliArgs().contains(QStringLiteral("--verbose")) && !quietOutput() && !currentCliArgs().contains(QStringLiteral("--json"));
}

struct CliExitCodeDescriptor {
	CliExitCode code = CliExitCode::Success;
	QString id;
	QString label;
	QString description;
};

struct CliCommandDescriptor {
	QString family;
	QString command;
	QString summary;
	QStringList examples;
	bool json = true;
	bool quiet = true;
	bool dryRun = false;
	bool watch = false;
};

std::string text(const QString& value)
{
	const QByteArray bytes = value.toUtf8();
	return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

int exitCodeValue(CliExitCode code)
{
	return static_cast<int>(code);
}

QVector<CliExitCodeDescriptor> cliExitCodeDescriptors()
{
	return {
		{CliExitCode::Success, QStringLiteral("success"), QStringLiteral("Success"), QStringLiteral("The command completed successfully.")},
		{CliExitCode::Failure, QStringLiteral("failure"), QStringLiteral("Operation failure"), QStringLiteral("The command was understood but the operation failed.")},
		{CliExitCode::Usage, QStringLiteral("usage-error"), QStringLiteral("Usage error"), QStringLiteral("Arguments were missing, malformed, or incompatible.")},
		{CliExitCode::NotFound, QStringLiteral("not-found"), QStringLiteral("Not found"), QStringLiteral("A requested project, package, entry, installation, or tool was not found.")},
		{CliExitCode::ValidationFailed, QStringLiteral("validation-failed"), QStringLiteral("Validation failed"), QStringLiteral("Validation completed and found blocking problems.")},
		{CliExitCode::Unavailable, QStringLiteral("unavailable"), QStringLiteral("Unavailable"), QStringLiteral("The workflow is recognized but no capable implementation or tool is available yet.")},
	};
}

CliExitCodeDescriptor cliExitCodeDescriptor(CliExitCode code)
{
	for (const CliExitCodeDescriptor& descriptor : cliExitCodeDescriptors()) {
		if (descriptor.code == code) {
			return descriptor;
		}
	}
	return {code, QStringLiteral("unknown"), QStringLiteral("Unknown"), QStringLiteral("Unknown exit code.")};
}

QVector<CliCommandDescriptor> cliCommandDescriptors()
{
	return {
		{QStringLiteral("cli"), QStringLiteral("exit-codes"), QStringLiteral("Print stable exit-code identifiers."), {QStringLiteral("vibestudio --cli cli exit-codes --json")}},
		{QStringLiteral("cli"), QStringLiteral("commands"), QStringLiteral("Print the registered command surface used for help and docs validation."), {QStringLiteral("vibestudio --cli cli commands --json")}},
		{QStringLiteral("ui"), QStringLiteral("semantics"), QStringLiteral("Print status chip, shortcut registry, and command palette metadata."), {QStringLiteral("vibestudio --cli ui semantics --json")}},
		{QStringLiteral("project"), QStringLiteral("init"), QStringLiteral("Create or refresh a project manifest."), {QStringLiteral("vibestudio --cli project init C:\\Games\\Quake\\mymod"), QStringLiteral("vibestudio --cli project init ./mymod --project-ai-free on")}},
		{QStringLiteral("project"), QStringLiteral("info"), QStringLiteral("Print project manifest and health summary."), {QStringLiteral("vibestudio --cli project info ./mymod --json")}},
		{QStringLiteral("project"), QStringLiteral("validate"), QStringLiteral("Validate project health and return validation-failed for blocking issues."), {QStringLiteral("vibestudio --cli project validate ./mymod")}},
		{QStringLiteral("package"), QStringLiteral("info"), QStringLiteral("Print package/archive summary."), {QStringLiteral("vibestudio --cli package info ./id1/pak0.pak")}},
		{QStringLiteral("package"), QStringLiteral("list"), QStringLiteral("List package entries."), {QStringLiteral("vibestudio --cli package list ./baseq3/pak0.pk3 --json")}},
		{QStringLiteral("package"), QStringLiteral("preview"), QStringLiteral("Preview a package entry."), {QStringLiteral("vibestudio --cli package preview ./pak0.pak maps/start.bsp")}},
		{QStringLiteral("package"), QStringLiteral("extract"), QStringLiteral("Extract staged package entries with dry-run support."), {QStringLiteral("vibestudio --cli package extract ./pak0.pak --output ./out --entry maps/start.bsp --dry-run")}, true, true, true},
		{QStringLiteral("package"), QStringLiteral("validate"), QStringLiteral("Validate package loading."), {QStringLiteral("vibestudio --cli package validate ./pak0.pak")}},
		{QStringLiteral("package"), QStringLiteral("stage"), QStringLiteral("Preview staged package add, replace, rename, and delete operations."), {QStringLiteral("vibestudio --cli package stage ./pak0.pak --add-file ./autoexec.cfg --as scripts/autoexec.cfg --json")}, true, true, true},
		{QStringLiteral("package"), QStringLiteral("save-as"), QStringLiteral("Write a staged package to a new PAK, ZIP, PK3, or tested PWAD path."), {QStringLiteral("vibestudio --cli package save-as ./pak0.pak ./rebuilt.pk3 --format pk3 --manifest ./rebuilt.manifest.json")}, true, true, true},
		{QStringLiteral("package"), QStringLiteral("manifest"), QStringLiteral("Export a package staging manifest without writing a package."), {QStringLiteral("vibestudio --cli package manifest ./pak0.pak --output ./stage.manifest.json")}, true, true, true},
		{QStringLiteral("asset"), QStringLiteral("inspect"), QStringLiteral("Inspect package entry asset metadata, including image, model, audio, and script details."), {QStringLiteral("vibestudio --cli asset inspect ./pak0.pk3 textures/base/wall.png --json")}},
		{QStringLiteral("asset"), QStringLiteral("convert"), QStringLiteral("Batch-convert package image entries with crop, resize, palette, and dry-run previews."), {QStringLiteral("vibestudio --cli asset convert ./pak0.pk3 --entry textures/base/wall.png --output ./converted --format png --resize 128x128 --dry-run")}, true, true, true},
		{QStringLiteral("asset"), QStringLiteral("audio-wav"), QStringLiteral("Export readable WAV/PCM package audio entries to a WAV file."), {QStringLiteral("vibestudio --cli asset audio-wav ./pak0.pk3 sound/items/pickup.wav --output ./pickup.wav --dry-run")}, true, true, true},
		{QStringLiteral("asset"), QStringLiteral("find"), QStringLiteral("Search project text/script assets with compiler-style locations."), {QStringLiteral("vibestudio --cli asset find ./mymod --find \"seta\" --json")}},
		{QStringLiteral("asset"), QStringLiteral("replace"), QStringLiteral("Preview or write project-wide text/script replacements with save-state reporting."), {QStringLiteral("vibestudio --cli asset replace ./mymod --find \"devmap\" --replace \"map\" --dry-run")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("inspect"), QStringLiteral("Inspect Doom WAD maps and Quake-family .map files with entities, textures, statistics, validation, and preview lines."), {QStringLiteral("vibestudio --cli map inspect ./maps/start.map --select entity:0 --json")}},
		{QStringLiteral("map"), QStringLiteral("edit"), QStringLiteral("Edit map entity key/value pairs and write the result to a non-destructive save-as path."), {QStringLiteral("vibestudio --cli map edit ./maps/start.map --entity 1 --set targetname=lift --output ./maps/start-edited.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("move"), QStringLiteral("Move a selected Doom vertex/linedef/thing or Quake entity and save to a new path."), {QStringLiteral("vibestudio --cli map move ./maps/start.map --object entity:1 --delta 16,0,0 --output ./maps/start-moved.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("compile-plan"), QStringLiteral("Build a compiler command plan from the inspected map and selected compiler profile."), {QStringLiteral("vibestudio --cli map compile-plan ./maps/start.map --profile ericw-qbsp --json")}, true, true, true},
		{QStringLiteral("shader"), QStringLiteral("inspect"), QStringLiteral("Parse idTech3 shader scripts into an editable graph model and validate texture references."), {QStringLiteral("vibestudio --cli shader inspect ./scripts/common.shader --package ./baseq3 --json")}},
		{QStringLiteral("shader"), QStringLiteral("set-stage"), QStringLiteral("Edit a shader stage directive and write a round-tripped shader script to a save-as path."), {QStringLiteral("vibestudio --cli shader set-stage ./scripts/common.shader --shader textures/base/wall --stage 1 --directive blendFunc --value \"GL_ONE GL_ONE\" --output ./scripts/common-edited.shader")}, true, true, true},
		{QStringLiteral("sprite"), QStringLiteral("plan"), QStringLiteral("Create Doom or Quake sprite frame, palette, sequencing, and package staging plans."), {QStringLiteral("vibestudio --cli sprite plan --engine doom --name TROO --frames 2 --rotations 8 --palette doom --json")}, true, true, true},
		{QStringLiteral("code"), QStringLiteral("index"), QStringLiteral("Index a project source tree with language hooks, diagnostics, symbols, build tasks, and launch profiles."), {QStringLiteral("vibestudio --cli code index ./mymod --find monster --json")}},
		{QStringLiteral("localization"), QStringLiteral("report"), QStringLiteral("Print localization targets, pseudo-localization, RTL smoke coverage, locale formatting, and catalog status."), {QStringLiteral("vibestudio --cli localization report --locale ar --json")}},
		{QStringLiteral("localization"), QStringLiteral("targets"), QStringLiteral("List the documented localization target set."), {QStringLiteral("vibestudio --cli localization targets")}},
		{QStringLiteral("diagnostics"), QStringLiteral("bundle"), QStringLiteral("Export a redacted diagnostic bundle for support, QA, and reproducibility."), {QStringLiteral("vibestudio --cli diagnostics bundle --output ./diagnostics")}, true},
		{QStringLiteral("extension"), QStringLiteral("discover"), QStringLiteral("Discover VibeStudio extension manifests and report trust and sandbox metadata."), {QStringLiteral("vibestudio --cli extension discover ./extensions --json")}},
		{QStringLiteral("extension"), QStringLiteral("inspect"), QStringLiteral("Inspect a VibeStudio extension manifest."), {QStringLiteral("vibestudio --cli extension inspect ./extensions/tool/vibestudio.extension.json")}},
		{QStringLiteral("extension"), QStringLiteral("run"), QStringLiteral("Build or execute an approved extension command plan with generated-file staging."), {QStringLiteral("vibestudio --cli extension run ./extensions/tool/vibestudio.extension.json build --dry-run --json")}, true, true, true},
		{QStringLiteral("compiler"), QStringLiteral("list"), QStringLiteral("Print compiler registry and executable discovery."), {QStringLiteral("vibestudio --cli compiler list --json")}},
		{QStringLiteral("compiler"), QStringLiteral("profiles"), QStringLiteral("Print compiler wrapper profiles."), {QStringLiteral("vibestudio --cli compiler profiles")}},
		{QStringLiteral("compiler"), QStringLiteral("plan"), QStringLiteral("Build a reviewable compiler command plan."), {QStringLiteral("vibestudio --cli compiler plan ericw-qbsp --input ./maps/start.map --dry-run")}, true, true, true},
		{QStringLiteral("compiler"), QStringLiteral("manifest"), QStringLiteral("Print or write a compiler command manifest."), {QStringLiteral("vibestudio --cli compiler manifest ericw-qbsp --input ./maps/start.map --manifest ./build/start.compiler.json")}, true, true, true},
		{QStringLiteral("compiler"), QStringLiteral("run"), QStringLiteral("Execute a compiler command with logs, diagnostics, task state, and manifest capture."), {QStringLiteral("vibestudio --cli compiler run ericw-qbsp --input ./maps/start.map --watch --manifest ./build/start.run.json")}, true, true, true, true},
		{QStringLiteral("compiler"), QStringLiteral("rerun"), QStringLiteral("Re-run a saved compiler command manifest."), {QStringLiteral("vibestudio --cli compiler rerun ./build/start.run.json --watch")}, true, true, false, true},
		{QStringLiteral("compiler"), QStringLiteral("copy-command"), QStringLiteral("Print shell-ready command line from a manifest or profile."), {QStringLiteral("vibestudio --cli compiler copy-command ericw-qbsp --input ./maps/start.map")}},
		{QStringLiteral("ai"), QStringLiteral("status"), QStringLiteral("Print AI preferences, credentials, models, tools, and connector metadata."), {QStringLiteral("vibestudio --cli ai status --json")}},
		{QStringLiteral("ai"), QStringLiteral("tools"), QStringLiteral("Print AI-callable VibeStudio tool descriptors."), {QStringLiteral("vibestudio --cli ai tools")}},
		{QStringLiteral("ai"), QStringLiteral("explain-log"), QStringLiteral("Explain a compiler log as a reviewable, no-write AI workflow."), {QStringLiteral("vibestudio --cli ai explain-log --log ./build/qbsp.log --json")}},
		{QStringLiteral("ai"), QStringLiteral("propose-command"), QStringLiteral("Propose a reviewable compiler command from natural language."), {QStringLiteral("vibestudio --cli ai propose-command --prompt \"build quake map maps/start.map\"")}},
		{QStringLiteral("ai"), QStringLiteral("propose-manifest"), QStringLiteral("Draft a project manifest without writing files."), {QStringLiteral("vibestudio --cli ai propose-manifest ./mymod --name \"My Mod\"")}},
		{QStringLiteral("ai"), QStringLiteral("package-deps"), QStringLiteral("Suggest missing package dependencies from metadata."), {QStringLiteral("vibestudio --cli ai package-deps ./release.pk3")}},
		{QStringLiteral("ai"), QStringLiteral("cli-command"), QStringLiteral("Generate a safe CLI command proposal."), {QStringLiteral("vibestudio --cli ai cli-command --prompt \"validate pak0.pak\"")}},
		{QStringLiteral("ai"), QStringLiteral("fix-plan"), QStringLiteral("Generate a supervised fix-and-retry plan from compiler output."), {QStringLiteral("vibestudio --cli ai fix-plan --log ./build/qbsp.log --command \"vibestudio --cli compiler run ericw-qbsp --input maps/start.map\"")}},
		{QStringLiteral("ai"), QStringLiteral("asset-request"), QStringLiteral("Stage an ElevenLabs/Meshy/OpenAI asset generation request before import."), {QStringLiteral("vibestudio --cli ai asset-request --provider meshy --kind texture --prompt \"rusty sci-fi panel\"")}, true, true, true},
		{QStringLiteral("ai"), QStringLiteral("compare"), QStringLiteral("Prepare side-by-side provider output comparison metadata."), {QStringLiteral("vibestudio --cli ai compare --provider-a openai --provider-b claude --prompt \"explain this build failure\"")}},
		{QStringLiteral("ai"), QStringLiteral("shader-scaffold"), QStringLiteral("Generate a staged prompt-to-shader scaffold proposal."), {QStringLiteral("vibestudio --cli ai shader-scaffold --prompt \"glowing gothic wall\" --json")}, true, true, true},
		{QStringLiteral("ai"), QStringLiteral("entity-snippet"), QStringLiteral("Generate a staged prompt-to-entity-definition snippet."), {QStringLiteral("vibestudio --cli ai entity-snippet --prompt \"trigger starts lift\" --json")}, true, true, true},
		{QStringLiteral("ai"), QStringLiteral("package-plan"), QStringLiteral("Generate a staged prompt-to-package-validation plan."), {QStringLiteral("vibestudio --cli ai package-plan --prompt \"release check\" --package ./release.pk3 --json")}, true, true, true},
		{QStringLiteral("ai"), QStringLiteral("batch-recipe"), QStringLiteral("Generate a staged prompt-to-batch-conversion recipe."), {QStringLiteral("vibestudio --cli ai batch-recipe --prompt \"convert doom sprites\" --json")}, true, true, true},
		{QStringLiteral("ai"), QStringLiteral("review"), QStringLiteral("Render the AI proposal review surface for a generated workflow."), {QStringLiteral("vibestudio --cli ai review --prompt \"glowing shader\" --kind shader --json")}, true, true, true},
		{QStringLiteral("credits"), QStringLiteral("validate"), QStringLiteral("Validate README/docs credits, compiler pins, .gitmodules, and checked-out submodule revisions."), {QStringLiteral("vibestudio --cli credits validate --json")}},
	};
}

CliOutputFormat outputFormat(const QStringList& args)
{
	return args.contains(QStringLiteral("--json")) ? CliOutputFormat::Json : CliOutputFormat::Text;
}

QString nativePath(const QString& path)
{
	return QDir::toNativeSeparators(path);
}

QString settingsStatusText(QSettings::Status status)
{
	switch (status) {
	case QSettings::NoError:
		return QStringLiteral("ready");
	case QSettings::AccessError:
		return QStringLiteral("access-error");
	case QSettings::FormatError:
		return QStringLiteral("format-error");
	}
	return QStringLiteral("unknown");
}

QString optionValue(const QStringList& args, const QString& option)
{
	const int index = args.indexOf(option);
	if (index < 0 || index + 1 >= args.size()) {
		return {};
	}
	return args[index + 1];
}

QStringList optionValues(const QStringList& args, const QString& option)
{
	QStringList values;
	for (int index = 0; index < args.size(); ++index) {
		if (args.at(index) == option && index + 1 < args.size()) {
			values.push_back(args.at(index + 1));
			++index;
		}
	}
	return values;
}

QString normalizedOptionId(const QString& id)
{
	return id.trimmed().toLower().replace('_', '-');
}

bool hasOption(const QStringList& args, const QString& option)
{
	return args.contains(option);
}

QStringList commandTokens(const QStringList& args)
{
	QStringList tokens;
	for (int i = 1; i < args.size(); ++i) {
		const QString token = args.at(i);
		if (token == QStringLiteral("--cli") || token == QStringLiteral("--json") || token == QStringLiteral("--quiet") || token == QStringLiteral("--verbose") || token == QStringLiteral("--dry-run") || token == QStringLiteral("--write") || token == QStringLiteral("--overwrite") || token == QStringLiteral("--case-sensitive") || token == QStringLiteral("--watch") || token == QStringLiteral("--task-state") || token == QStringLiteral("--execute") || token == QStringLiteral("--allow-execution") || token == QStringLiteral("--no-stage")) {
			continue;
		}
		if (token == QStringLiteral("--installation") || token == QStringLiteral("--project-installation") || token == QStringLiteral("--set-editor-profile") || token == QStringLiteral("--set-ai-free") || token == QStringLiteral("--set-ai-cloud") || token == QStringLiteral("--set-ai-agentic") || token == QStringLiteral("--set-ai-reasoning") || token == QStringLiteral("--set-ai-text-model") || token == QStringLiteral("--provider") || token == QStringLiteral("--provider-a") || token == QStringLiteral("--provider-b") || token == QStringLiteral("--model") || token == QStringLiteral("--model-a") || token == QStringLiteral("--model-b") || token == QStringLiteral("--prompt") || token == QStringLiteral("--text") || token == QStringLiteral("--log") || token == QStringLiteral("--command") || token == QStringLiteral("--kind") || token == QStringLiteral("--name") || token == QStringLiteral("--sprite-name") || token == QStringLiteral("--manifest") || token == QStringLiteral("--workspace-root") || token == QStringLiteral("--working-directory") || token == QStringLiteral("--input") || token == QStringLiteral("--output") || token == QStringLiteral("--format") || token == QStringLiteral("--crop") || token == QStringLiteral("--resize") || token == QStringLiteral("--palette") || token == QStringLiteral("--find") || token == QStringLiteral("--symbol") || token == QStringLiteral("--replace") || token == QStringLiteral("--extensions") || token == QStringLiteral("--add-file") || token == QStringLiteral("--import-file") || token == QStringLiteral("--as") || token == QStringLiteral("--replace-file") || token == QStringLiteral("--replace-entry") || token == QStringLiteral("--entry") || token == QStringLiteral("--rename") || token == QStringLiteral("--to") || token == QStringLiteral("--delete") || token == QStringLiteral("--remove-entry") || token == QStringLiteral("--resolve") || token == QStringLiteral("--map") || token == QStringLiteral("--map-name") || token == QStringLiteral("--engine") || token == QStringLiteral("--engine-hint") || token == QStringLiteral("--shader") || token == QStringLiteral("--stage") || token == QStringLiteral("--directive") || token == QStringLiteral("--value") || token == QStringLiteral("--frames") || token == QStringLiteral("--rotations") || token == QStringLiteral("--package") || token == QStringLiteral("--mounted-package") || token == QStringLiteral("--package-root") || token == QStringLiteral("--source-frame") || token == QStringLiteral("--extension-root") || token == QStringLiteral("--root") || token == QStringLiteral("--select") || token == QStringLiteral("--entity") || token == QStringLiteral("--set") || token == QStringLiteral("--property") || token == QStringLiteral("--object") || token == QStringLiteral("--delta") || token == QStringLiteral("--profile") || token == QStringLiteral("--compiler-profile") || token == QStringLiteral("--extra-args") || token == QStringLiteral("--compiler-search-paths") || token == QStringLiteral("--timeout-ms") || token == QStringLiteral("--executable") || token == QStringLiteral("--locale") || token == QStringLiteral("--catalog-root")) {
			++i;
			continue;
		}
		tokens.push_back(token);
	}
	return tokens;
}

bool boolOptionValue(const QString& value, bool* parsed)
{
	const QString normalized = normalizedOptionId(value);
	if (normalized == QStringLiteral("1") || normalized == QStringLiteral("true") || normalized == QStringLiteral("yes") || normalized == QStringLiteral("on") || normalized == QStringLiteral("enabled")) {
		*parsed = true;
		return true;
	}
	if (normalized == QStringLiteral("0") || normalized == QStringLiteral("false") || normalized == QStringLiteral("no") || normalized == QStringLiteral("off") || normalized == QStringLiteral("disabled")) {
		*parsed = false;
		return true;
	}
	return false;
}

bool localeOptionIsSupported(const QString& value)
{
	const QString requested = value.trimmed().replace('_', '-');
	if (requested.isEmpty()) {
		return false;
	}
	const QString normalized = normalizedLocaleName(requested);
	return normalized != QStringLiteral("en") || requested.startsWith(QStringLiteral("en"), Qt::CaseInsensitive);
}

void printHelp()
{
	std::cout << "VibeStudio " << text(versionString()) << "\n";
	std::cout << "Usage: vibestudio --cli [options]\n\n";
	std::cout << "       vibestudio --cli [--json] <family> <command> [arguments]\n\n";
	std::cout << "Options:\n";
	std::cout << "  --version           Print the application version.\n";
	std::cout << "  --help              Print this help text.\n";
	std::cout << "  --about             Print version, repository, credits, and license pointers.\n";
	std::cout << "  --credits           Print credits and license pointers.\n";
	std::cout << "  --json              Emit machine-readable JSON for supported commands.\n";
	std::cout << "  --quiet             Suppress successful human-readable narration. Errors still go to stderr.\n";
	std::cout << "  --verbose           Print timing and extra diagnostics for supported text commands.\n";
	std::cout << "  --exit-codes        Print stable CLI exit-code identifiers.\n";
	std::cout << "  --studio-report     Print planned studio modules.\n";
	std::cout << "  --compiler-report   Print imported compiler integrations.\n";
	std::cout << "  --compiler-registry Print compiler tool registry and executable discovery.\n";
	std::cout << "  --platform-report   Print platform and Qt runtime details.\n";
	std::cout << "  --project-init <path>\n";
	std::cout << "                      Create or refresh .vibestudio/project.json for a project folder.\n";
	std::cout << "  --project-info <path>\n";
	std::cout << "                      Print project manifest and health summary.\n";
	std::cout << "  --project-validate <path>\n";
	std::cout << "                      Validate project manifest, folders, install linkage, and health checks.\n";
	std::cout << "  --project-installation <id>\n";
	std::cout << "                      Optional selected installation id for --project-init.\n";
	std::cout << "  --project-editor-profile <id>\n";
	std::cout << "                      Optional project-local editor profile override for --project-init.\n";
	std::cout << "  --project-palette <id>\n";
	std::cout << "                      Optional project-local palette override for --project-init.\n";
	std::cout << "  --project-compiler-profile <id>\n";
	std::cout << "                      Optional project-local compiler profile override for --project-init.\n";
	std::cout << "  --project-compiler-search-paths <paths>\n";
	std::cout << "                      Semicolon-separated project-local compiler executable search paths.\n";
	std::cout << "  --project-compiler-tool <id> --project-compiler-executable <path>\n";
	std::cout << "                      Optional project-local compiler executable override for --project-init.\n";
	std::cout << "  --project-ai-free <on|off>\n";
	std::cout << "                      Optional project-local AI-free mode override for --project-init.\n";
	std::cout << "  --operation-states  Print reusable operation state identifiers.\n";
	std::cout << "  --ui-primitives     Print reusable UI primitive identifiers.\n";
	std::cout << "  --ui-semantics      Print status chip, shortcut, and command palette metadata.\n";
	std::cout << "  --package-formats   Print package/archive interface descriptors.\n";
	std::cout << "  --check-package-path <path>\n";
	std::cout << "                      Normalize and validate a package virtual path.\n";
	std::cout << "  --info <path>       Print read-only package summary for a folder, PAK, WAD, ZIP, or PK3.\n";
	std::cout << "  --list <path>       List entries in a folder, PAK, WAD, ZIP, or PK3 package.\n";
	std::cout << "  --preview-package <path>\n";
	std::cout << "                      Open a package and preview --preview-entry text, image, or binary metadata.\n";
	std::cout << "  --preview-entry <virtual-path>\n";
	std::cout << "                      Virtual package entry path for --preview-package.\n";
	std::cout << "  --extract <path>    Extract a package to --output. Defaults to all entries unless --extract-entry is passed.\n";
	std::cout << "  --extract-entry <virtual-path>\n";
	std::cout << "                      Virtual package entry to extract. Repeat for multiple entries.\n";
	std::cout << "  --extract-all       Extract every readable package entry.\n";
	std::cout << "  --validate-package <path>\n";
	std::cout << "                      Validate a package can be opened without loader warnings.\n";
	std::cout << "  --output <path>     Output folder for package extraction or output path for compiler planning.\n";
	std::cout << "  --dry-run           Report package extraction/save-as writes or compiler command plans without touching the file system.\n";
	std::cout << "  --watch             Stream compiler task log entries while a long-running command is active.\n";
	std::cout << "  --task-state        Include machine-readable task state objects in JSON output where supported.\n";
	std::cout << "  --overwrite         Allow package extraction or save-as to replace existing output files.\n";
	std::cout << "  --settings-report   Print settings storage and recent projects.\n";
	std::cout << "  --setup-report      Print first-run setup status and summary.\n";
	std::cout << "  --setup-start       Start or resume first-run setup.\n";
	std::cout << "  --setup-step <id>   Resume setup at a specific step.\n";
	std::cout << "  --setup-next        Advance setup to the next step.\n";
	std::cout << "  --setup-skip        Skip setup for now without completing it.\n";
	std::cout << "  --setup-complete    Mark setup complete.\n";
	std::cout << "  --setup-reset       Reset setup progress.\n";
	std::cout << "  --preferences-report\n";
	std::cout << "                      Print accessibility and language preferences.\n";
	std::cout << "  --localization-report\n";
	std::cout << "                      Print localization target, pseudo-localization, RTL, formatting, and catalog status.\n";
	std::cout << "  --set-locale <code> Set the preferred UI locale. Supported: " << text(supportedLocaleNames().join(", ")) << "\n";
	std::cout << "  --set-theme <id>    Set theme: " << text(themeIds().join(", ")) << "\n";
	std::cout << "  --set-text-scale <percent>\n";
	std::cout << "                      Set text scale from 100 to 200.\n";
	std::cout << "  --set-density <id>  Set density: " << text(densityIds().join(", ")) << "\n";
	std::cout << "  --editor-profiles   Print routed editor interaction profiles.\n";
	std::cout << "  --set-editor-profile <id>\n";
	std::cout << "                      Select editor profile: " << text(editorProfileIds().join(", ")) << "\n";
	std::cout << "  --ai-status         Print AI-free mode, opt-in settings, and connector stubs.\n";
	std::cout << "  --set-ai-text-model <id>\n";
	std::cout << "                      Preferred text model: " << text(aiModelIds().join(", ")) << "\n";
	std::cout << "  --set-ai-free <on|off>\n";
	std::cout << "                      Enable or disable AI-free mode. Default is on.\n";
	std::cout << "  --set-ai-cloud <on|off>\n";
	std::cout << "                      Opt in or out of experimental cloud AI connector settings.\n";
	std::cout << "  --set-ai-agentic <on|off>\n";
	std::cout << "                      Opt in or out of future supervised agentic workflows.\n";
	std::cout << "  --set-ai-reasoning <id>\n";
	std::cout << "                      Preferred reasoning connector: " << text(aiConnectorIds().join(", ")) << "\n";
	std::cout << "  --set-reduced-motion <on|off>\n";
	std::cout << "                      Store the reduced-motion preference.\n";
	std::cout << "  --set-tts <on|off>  Store the OS-backed text-to-speech preference.\n";
	std::cout << "  --installations-report\n";
	std::cout << "                      Print saved manual game installation profiles.\n";
	std::cout << "  --detect-installations\n";
	std::cout << "                      Detect Steam and GOG installation candidates without saving them.\n";
	std::cout << "  --detect-install-root <path>\n";
	std::cout << "                      Extra Steam/GOG library or game root for --detect-installations.\n";
	std::cout << "  --add-installation <root>\n";
	std::cout << "                      Add or update a manual game installation profile.\n";
	std::cout << "  --install-game <key>\n";
	std::cout << "                      Game key for --add-installation. Known: " << text(knownGameKeys().join(", ")) << "\n";
	std::cout << "  --install-engine <id>\n";
	std::cout << "                      Engine family for --add-installation. Known: " << text(gameEngineFamilyIds().join(", ")) << "\n";
	std::cout << "  --install-name <name>\n";
	std::cout << "                      Display name for --add-installation.\n";
	std::cout << "  --install-executable <path>\n";
	std::cout << "                      Optional executable path for --add-installation.\n";
	std::cout << "  --install-base-packages <paths>\n";
	std::cout << "                      Semicolon-separated base package paths for --add-installation.\n";
	std::cout << "  --install-mod-packages <paths>\n";
	std::cout << "                      Semicolon-separated mod/package paths for --add-installation.\n";
	std::cout << "  --install-read-only <on|off>\n";
	std::cout << "                      Store whether the installation should be treated read-only.\n";
	std::cout << "  --install-hidden <on|off>\n";
	std::cout << "                      Store whether the installation profile is hidden from default views.\n";
	std::cout << "  --select-installation <id>\n";
	std::cout << "                      Mark an installation profile as selected.\n";
	std::cout << "  --validate-installation <id>\n";
	std::cout << "                      Validate a saved installation profile read-only.\n";
	std::cout << "  --remove-installation <id>\n";
	std::cout << "                      Remove a saved installation profile without touching files.\n";
	std::cout << "  --recent-projects   Print recent projects only.\n";
	std::cout << "  --add-recent-project <path>\n";
	std::cout << "                      Remember a project folder in persistent settings.\n";
	std::cout << "  --remove-recent-project <path>\n";
	std::cout << "                      Forget a project folder without touching files.\n";
	std::cout << "  --clear-recent-projects\n";
	std::cout << "                      Clear remembered project folders without touching files.\n";
	std::cout << "\nSubcommands:\n";
	std::cout << "  cli exit-codes\n";
	std::cout << "                      Print the stable exit-code contract.\n";
	std::cout << "  cli commands        Print testable command registration metadata and shell examples.\n";
	std::cout << "  ui semantics        Print status chip, shortcut registry, and command palette metadata.\n";
	std::cout << "  credits validate    Validate credits coverage and compiler submodule pins.\n";
	std::cout << "  about               Print version, repository, credits, and license pointers.\n";
	std::cout << "  project init <path> [--installation <id>] [--editor-profile <id>]\n";
	std::cout << "                      Create or refresh .vibestudio/project.json.\n";
	std::cout << "  project info <path> Print project manifest and health summary.\n";
	std::cout << "  project validate <path>\n";
	std::cout << "                      Validate project health and return validation-failed for blocking issues.\n";
	std::cout << "  install list        List saved manual game installation profiles.\n";
	std::cout << "  install detect [--root <path>]\n";
	std::cout << "                      Detect Steam/GOG candidates read-only. Repeat --root for fixtures or custom libraries.\n";
	std::cout << "  package info <path> Print read-only package summary.\n";
	std::cout << "  package list <path> List package entries and metadata.\n";
	std::cout << "  package preview <path> <virtual-path>\n";
	std::cout << "                      Preview a text, image, or binary package entry.\n";
	std::cout << "  package extract <path> --output <folder> [--entry <virtual-path>] [--dry-run] [--overwrite]\n";
	std::cout << "                      Extract selected entries or every entry when no entry is provided.\n";
	std::cout << "  package validate <path>\n";
	std::cout << "                      Validate package loading and return validation-failed for warnings.\n";
	std::cout << "  package stage <path> [--add-file <file> --as <virtual-path>] [--replace-file <file> --replace-entry <virtual-path>] [--rename <old> --to <new>] [--delete <virtual-path>] [--resolve block|replace-existing|skip]\n";
	std::cout << "                      Preview staged changes, conflicts, and before/after composition without writing.\n";
	std::cout << "  package save-as <path> <output> [--format pak|zip|pk3|wad] [stage options] [--dry-run] [--overwrite] [--manifest <path>]\n";
	std::cout << "                      Write staged changes to a new package path; in-place overwrite is blocked.\n";
	std::cout << "  package manifest <path> --output <manifest.json> [stage options]\n";
	std::cout << "                      Export a reproducible staged package manifest without writing a package.\n";
	std::cout << "  asset inspect <package> <virtual-path>\n";
	std::cout << "                      Inspect texture/image, model, audio, or text/script metadata for one package entry.\n";
	std::cout << "  asset convert <package> --output <folder> [--entry <virtual-path>] [--format png|jpg|bmp] [--crop x,y,w,h] [--resize WxH] [--palette grayscale|indexed] [--dry-run] [--overwrite]\n";
	std::cout << "                      Batch-convert package image entries with before/after size previews.\n";
	std::cout << "  asset audio-wav <package> <virtual-path> --output <file.wav> [--dry-run] [--overwrite]\n";
	std::cout << "                      Export readable WAV/PCM audio entries without decoding compressed codecs.\n";
	std::cout << "  asset find <project-root> --find <text> [--extensions cfg;shader;qc]\n";
	std::cout << "                      Search local text/script assets and print file, line, and column matches.\n";
	std::cout << "  asset replace <project-root> --find <text> --replace <text> [--dry-run|--write] [--extensions cfg;shader;qc]\n";
	std::cout << "                      Preview or write text/script replacements with save-state reporting.\n";
	std::cout << "  map inspect <path> [--map MAP01] [--engine idtech2|idtech3] [--select entity:0]\n";
	std::cout << "                      Inspect entities, textures/materials, map statistics, validation, and preview lines.\n";
	std::cout << "  map edit <path> --entity <id> --set key=value --output <path> [--dry-run] [--overwrite]\n";
	std::cout << "                      Edit entity key/value pairs and write a non-destructive save-as path.\n";
	std::cout << "  map move <path> --object entity:0|vertex:0|linedef:0|thing:0 --delta x,y,z --output <path>\n";
	std::cout << "                      Move safe MVP selections and write a non-destructive save-as path.\n";
	std::cout << "  map compile-plan <path> [--profile <compiler-profile>] [--output <path>]\n";
	std::cout << "                      Build a compiler command plan from the inspected map.\n";
	std::cout << "  compiler list       Print compiler registry and executable discovery.\n";
	std::cout << "  compiler profiles   Print available compiler wrapper profiles.\n";
	std::cout << "  compiler set-path <tool> --executable <path>\n";
	std::cout << "                      Save a user-configured compiler executable path.\n";
	std::cout << "  compiler clear-path <tool>\n";
	std::cout << "                      Remove a saved compiler executable path override.\n";
	std::cout << "  compiler plan <profile> --input <path>\n";
	std::cout << "                      Build a reviewable compiler command plan. Optional: --output <path>, --extra-args <args>, --workspace-root <path>.\n";
	std::cout << "  compiler manifest <profile> --input <path>\n";
	std::cout << "                      Print or write a schema-versioned compiler command manifest. Optional: --manifest <path>.\n";
	std::cout << "  compiler run <profile> --input <path>\n";
	std::cout << "                      Execute a compiler command, capture logs, diagnostics, hashes, and an optional --manifest record.\n";
	std::cout << "  compiler rerun <manifest-path>\n";
	std::cout << "                      Re-run a previously saved compiler command manifest. Optional: --manifest <path>.\n";
	std::cout << "  compiler copy-command <manifest-path|profile> [--input <path>]\n";
	std::cout << "                      Print the shell-ready command line from a manifest or planned profile.\n";
	std::cout << "  editor profiles     Print routed editor interaction profiles.\n";
	std::cout << "  editor current      Print the selected editor profile.\n";
	std::cout << "  editor select <id>  Select an editor profile globally.\n";
	std::cout << "  localization report [--locale <id>] [--catalog-root <path>]\n";
	std::cout << "                      Print localization targets, pseudo-localization, RTL smoke, locale formatting, and catalog status.\n";
	std::cout << "  localization targets\n";
	std::cout << "                      List the documented localization target set.\n";
	std::cout << "  diagnostics bundle [--output <folder>]\n";
	std::cout << "                      Export redacted version, platform, command, module, and localization diagnostics.\n";
	std::cout << "  ai status           Print AI-free mode, opt-in settings, and connector stubs.\n";
	std::cout << "  ai connectors       Print provider-neutral AI connector design stubs.\n";
	std::cout << "  ai tools            Print safe AI-callable VibeStudio tools.\n";
	std::cout << "  ai explain-log --log <path>|--text <text>\n";
	std::cout << "                      Explain compiler log text without writing files.\n";
	std::cout << "  ai propose-command --prompt <text>\n";
	std::cout << "                      Propose a reviewable compiler command from natural language.\n";
	std::cout << "  ai propose-manifest <project-root> [--name <display-name>]\n";
	std::cout << "                      Draft a project manifest preview without writing files.\n";
	std::cout << "  ai package-deps <package>\n";
	std::cout << "                      Suggest missing package dependencies from package metadata.\n";
	std::cout << "  ai cli-command --prompt <text>\n";
	std::cout << "                      Generate a reviewable CLI command proposal.\n";
	std::cout << "  ai fix-plan --log <path> [--command <line>]\n";
	std::cout << "                      Generate a supervised compiler fix-and-retry plan.\n";
	std::cout << "  ai asset-request --provider <id> --kind <kind> --prompt <text>\n";
	std::cout << "                      Stage an asset generation request before import.\n";
	std::cout << "  ai compare --provider-a <id> --provider-b <id> --prompt <text>\n";
	std::cout << "                      Prepare provider output comparison metadata.\n";
	std::cout << "\nExamples:\n";
	std::cout << "  PowerShell: vibestudio --cli package validate \"C:\\Games\\Quake\\id1\\pak0.pak\" --json\n";
	std::cout << "  POSIX:      vibestudio --cli compiler plan ericw-qbsp --input './maps/start.map' --dry-run\n";
}

void printStudioReport()
{
	std::cout << "VibeStudio studio modules\n";
	for (const StudioModule& module : plannedModules()) {
		std::cout << "- " << text(module.name) << " [" << text(module.maturity) << "]\n";
		std::cout << "  Category: " << text(module.category) << "\n";
		std::cout << "  Engines: " << text(module.engines.join(", ")) << "\n";
		std::cout << "  " << text(module.description) << "\n";
	}
}

void printCompilerReport()
{
	std::cout << "VibeStudio compiler integrations\n";
	for (const CompilerIntegration& compiler : compilerIntegrations()) {
		std::cout << "- " << text(compiler.displayName) << "\n";
		std::cout << "  Engines: " << text(compiler.engines) << "\n";
		std::cout << "  Role: " << text(compiler.role) << "\n";
		std::cout << "  Source: " << text(compiler.sourcePath) << "\n";
		std::cout << "  Upstream: " << text(compiler.upstreamUrl) << "\n";
		std::cout << "  Revision: " << text(compiler.pinnedRevision) << "\n";
		std::cout << "  License: " << text(compiler.license) << "\n";
	}
}

void printPlatformReport()
{
	std::cout << "VibeStudio platform report\n";
	std::cout << "Version: " << text(versionString()) << "\n";
	std::cout << "GitHub repo: " << VIBESTUDIO_GITHUB_REPO << "\n";
	std::cout << "Update channel: " << VIBESTUDIO_UPDATE_CHANNEL << "\n";
	std::cout << "Qt runtime: " << qVersion() << "\n";
	std::cout << "Kernel: " << text(QSysInfo::kernelType()) << " " << text(QSysInfo::kernelVersion()) << "\n";
	std::cout << "CPU architecture: " << text(QSysInfo::currentCpuArchitecture()) << "\n";
	std::cout << "Product: " << text(QSysInfo::prettyProductName()) << "\n";
}

void printProjectHealth(const ProjectHealthSummary& health)
{
	std::cout << "Project health\n";
	std::cout << "State: " << text(operationStateId(health.overallState())) << "\n";
	std::cout << "Ready: " << health.readyCount << "\n";
	std::cout << "Warnings: " << health.warningCount << "\n";
	std::cout << "Failures: " << health.failedCount << "\n";
	for (const ProjectHealthCheck& check : health.checks) {
		std::cout << "- " << text(check.title) << "\n";
		std::cout << "  State: " << text(operationStateId(check.state)) << "\n";
		std::cout << "  Detail: " << text(check.detail) << "\n";
	}
}

void printProjectInfo(const ProjectManifest& manifest)
{
	std::cout << "Project manifest\n";
	std::cout << text(projectManifestToText(manifest)) << "\n";
	printProjectHealth(buildProjectHealthSummary(manifest));
}

void printOperationStates()
{
	std::cout << "Operation states\n";
	for (const QString& id : operationStateIds()) {
		const OperationState state = operationStateFromId(id);
		std::cout << "- " << text(id) << "\n";
		std::cout << "  Label: " << text(operationStateDisplayName(state)) << "\n";
		std::cout << "  Terminal: " << (operationStateIsTerminal(state) ? "yes" : "no") << "\n";
		std::cout << "  Cancellable: " << (operationStateAllowsCancellation(state) ? "yes" : "no") << "\n";
	}
}

void printUiPrimitives()
{
	std::cout << "UI primitives\n";
	for (const UiPrimitiveDescriptor& primitive : uiPrimitiveDescriptors()) {
		std::cout << "- " << text(primitive.id) << "\n";
		std::cout << "  Title: " << text(primitive.title) << "\n";
		std::cout << "  Description: " << text(primitive.description) << "\n";
		std::cout << "  Use cases: " << text(primitive.useCases.join(", ")) << "\n";
	}
}

void printUiSemantics()
{
	std::cout << text(statusChipSummaryText()) << "\n\n";
	std::cout << text(shortcutRegistrySummaryText()) << "\n\n";
	std::cout << text(commandPaletteSummaryText()) << "\n";
}

void printPackageFormats()
{
	std::cout << "Package archive interfaces\n";
	for (const PackageArchiveFormatDescriptor& descriptor : packageArchiveFormatDescriptors()) {
		std::cout << "- " << text(descriptor.id) << "\n";
		std::cout << "  Label: " << text(descriptor.displayName) << "\n";
		std::cout << "  Extensions: " << text(descriptor.extensions.isEmpty() ? QStringLiteral("(folder)") : descriptor.extensions.join(", ")) << "\n";
		std::cout << "  Capabilities: " << text(descriptor.capabilities.join(", ")) << "\n";
		std::cout << "  " << text(descriptor.description) << "\n";
	}
}

void printPackagePathCheck(const QString& path)
{
	const PackageVirtualPath normalized = normalizePackageVirtualPath(path);
	std::cout << "Package path check\n";
	std::cout << "Original: " << text(path) << "\n";
	std::cout << "Normalized: " << text(normalized.normalizedPath.isEmpty() ? QStringLiteral("(empty)") : normalized.normalizedPath) << "\n";
	std::cout << "Safe: " << (normalized.isSafe() ? "yes" : "no") << "\n";
	std::cout << "Issue: " << text(packagePathIssueId(normalized.issue)) << " (" << text(packagePathIssueDisplayName(normalized.issue)) << ")\n";
	if (normalized.isSafe()) {
		std::cout << "File name: " << text(packageVirtualPathFileName(normalized.normalizedPath)) << "\n";
		std::cout << "Parent: " << text(packageVirtualPathParent(normalized.normalizedPath).isEmpty() ? QStringLiteral("/") : packageVirtualPathParent(normalized.normalizedPath)) << "\n";
		std::cout << "Nested archive candidate: " << (packageEntryLooksNestedArchive(normalized.normalizedPath) ? "yes" : "no") << "\n";
	}
}

QString sizeText(quint64 bytes)
{
	if (bytes >= 1024ull * 1024ull * 1024ull) {
		return QStringLiteral("%1 GiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
	}
	if (bytes >= 1024ull * 1024ull) {
		return QStringLiteral("%1 MiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
	}
	if (bytes >= 1024ull) {
		return QStringLiteral("%1 KiB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 2);
	}
	return QStringLiteral("%1 B").arg(bytes);
}

void printJson(const QJsonObject& object)
{
	const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
	std::cout << std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
	if (!bytes.endsWith('\n')) {
		std::cout << "\n";
	}
}

QJsonArray stringArrayJson(const QStringList& values)
{
	QJsonArray array;
	for (const QString& value : values) {
		array.append(value);
	}
	return array;
}

QJsonObject exitCodeJson(CliExitCode code)
{
	const CliExitCodeDescriptor descriptor = cliExitCodeDescriptor(code);
	QJsonObject object;
	object.insert(QStringLiteral("code"), exitCodeValue(code));
	object.insert(QStringLiteral("id"), descriptor.id);
	object.insert(QStringLiteral("label"), descriptor.label);
	object.insert(QStringLiteral("description"), descriptor.description);
	return object;
}

QJsonArray stringArrayJson(const QStringList& values);

QJsonArray exitCodesJson()
{
	QJsonArray array;
	for (const CliExitCodeDescriptor& descriptor : cliExitCodeDescriptors()) {
		array.append(exitCodeJson(descriptor.code));
	}
	return array;
}

QJsonObject cliCommandDescriptorJson(const CliCommandDescriptor& descriptor)
{
	QJsonObject object;
	object.insert(QStringLiteral("family"), descriptor.family);
	object.insert(QStringLiteral("command"), descriptor.command);
	object.insert(QStringLiteral("name"), QStringLiteral("%1 %2").arg(descriptor.family, descriptor.command));
	object.insert(QStringLiteral("summary"), descriptor.summary);
	object.insert(QStringLiteral("examples"), stringArrayJson(descriptor.examples));
	object.insert(QStringLiteral("json"), descriptor.json);
	object.insert(QStringLiteral("quiet"), descriptor.quiet);
	object.insert(QStringLiteral("dryRun"), descriptor.dryRun);
	object.insert(QStringLiteral("watch"), descriptor.watch);
	return object;
}

QJsonArray cliCommandsJson()
{
	QJsonArray array;
	for (const CliCommandDescriptor& descriptor : cliCommandDescriptors()) {
		array.append(cliCommandDescriptorJson(descriptor));
	}
	return array;
}

QJsonObject cliResultJson(const QString& commandName, CliExitCode code = CliExitCode::Success)
{
	QJsonObject object;
	object.insert(QStringLiteral("command"), commandName);
	object.insert(QStringLiteral("ok"), code == CliExitCode::Success);
	object.insert(QStringLiteral("status"), code == CliExitCode::Success ? QStringLiteral("success") : QStringLiteral("error"));
	object.insert(QStringLiteral("exitCode"), exitCodeJson(code));
	return object;
}

int printCliError(const QString& commandName, CliExitCode code, const QString& message, CliOutputFormat format)
{
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("message"), message);
		printJson(object);
	} else {
		std::cerr << text(message) << "\n";
	}
	return exitCodeValue(code);
}

QJsonObject operationStateJson(OperationState state)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), operationStateId(state));
	object.insert(QStringLiteral("label"), operationStateDisplayName(state));
	object.insert(QStringLiteral("terminal"), operationStateIsTerminal(state));
	object.insert(QStringLiteral("cancellable"), operationStateAllowsCancellation(state));
	return object;
}

QJsonArray operationStatesJson()
{
	QJsonArray array;
	for (const QString& id : operationStateIds()) {
		array.append(operationStateJson(operationStateFromId(id)));
	}
	return array;
}

QJsonObject statusChipJson(const StatusChipDescriptor& descriptor)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), descriptor.id);
	object.insert(QStringLiteral("domain"), descriptor.domain);
	object.insert(QStringLiteral("label"), descriptor.label);
	object.insert(QStringLiteral("description"), descriptor.description);
	object.insert(QStringLiteral("state"), operationStateId(descriptor.state));
	object.insert(QStringLiteral("iconName"), descriptor.iconName);
	object.insert(QStringLiteral("colorToken"), descriptor.colorToken);
	object.insert(QStringLiteral("nonColorCue"), descriptor.nonColorCue);
	object.insert(QStringLiteral("nextAction"), descriptor.nextAction);
	return object;
}

QJsonArray statusChipsJson()
{
	QJsonArray array;
	for (const StatusChipDescriptor& descriptor : statusChipDescriptors()) {
		array.append(statusChipJson(descriptor));
	}
	return array;
}

QJsonObject shortcutJson(const ShortcutDescriptor& descriptor)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), descriptor.id);
	object.insert(QStringLiteral("commandId"), descriptor.commandId);
	object.insert(QStringLiteral("label"), descriptor.label);
	object.insert(QStringLiteral("context"), descriptor.context);
	object.insert(QStringLiteral("defaultSequence"), descriptor.defaultSequence);
	object.insert(QStringLiteral("alternateSequences"), stringArrayJson(descriptor.alternateSequences));
	object.insert(QStringLiteral("description"), descriptor.description);
	object.insert(QStringLiteral("userRemappable"), descriptor.userRemappable);
	return object;
}

QJsonArray shortcutsJson()
{
	QJsonArray array;
	for (const ShortcutDescriptor& descriptor : shortcutDescriptors()) {
		array.append(shortcutJson(descriptor));
	}
	return array;
}

QJsonObject commandPaletteEntryJson(const CommandPaletteEntry& entry)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), entry.id);
	object.insert(QStringLiteral("commandId"), entry.commandId);
	object.insert(QStringLiteral("label"), entry.label);
	object.insert(QStringLiteral("category"), entry.category);
	object.insert(QStringLiteral("summary"), entry.summary);
	object.insert(QStringLiteral("defaultShortcut"), entry.defaultShortcut);
	object.insert(QStringLiteral("requiresProject"), entry.requiresProject);
	object.insert(QStringLiteral("destructive"), entry.destructive);
	object.insert(QStringLiteral("stagedOrDryRun"), entry.stagedOrDryRun);
	return object;
}

QJsonArray commandPaletteEntriesJson()
{
	QJsonArray array;
	for (const CommandPaletteEntry& entry : commandPaletteEntries()) {
		array.append(commandPaletteEntryJson(entry));
	}
	return array;
}

QJsonObject uiSemanticsJson()
{
	QStringList conflicts;
	const bool hasShortcutConflicts = shortcutRegistryHasConflicts(&conflicts);
	QJsonObject object;
	object.insert(QStringLiteral("statusChips"), statusChipsJson());
	object.insert(QStringLiteral("statusDomains"), stringArrayJson(statusChipDomains()));
	object.insert(QStringLiteral("shortcuts"), shortcutsJson());
	object.insert(QStringLiteral("shortcutConflictCount"), conflicts.size());
	object.insert(QStringLiteral("shortcutConflicts"), stringArrayJson(conflicts));
	object.insert(QStringLiteral("shortcutRegistryOk"), !hasShortcutConflicts);
	object.insert(QStringLiteral("commandPalette"), commandPaletteEntriesJson());
	return object;
}

QJsonObject aboutDocumentJson(const AboutDocument& document)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), document.id);
	object.insert(QStringLiteral("title"), document.title);
	object.insert(QStringLiteral("path"), document.path);
	object.insert(QStringLiteral("description"), document.description);
	return object;
}

QJsonArray aboutDocumentsJson()
{
	QJsonArray documents;
	for (const AboutDocument& document : aboutDocuments()) {
		documents.append(aboutDocumentJson(document));
	}
	return documents;
}

QJsonObject aboutJson()
{
	QJsonObject object;
	object.insert(QStringLiteral("name"), QStringLiteral("VibeStudio"));
	object.insert(QStringLiteral("version"), versionString());
	object.insert(QStringLiteral("repository"), githubRepository());
	object.insert(QStringLiteral("updateChannel"), updateChannel());
	object.insert(QStringLiteral("licenseSummary"), projectLicenseSummary());
	object.insert(QStringLiteral("documents"), aboutDocumentsJson());
	return object;
}

QJsonObject packageWarningJson(const PackageLoadWarning& warning)
{
	QJsonObject object;
	object.insert(QStringLiteral("virtualPath"), warning.virtualPath);
	object.insert(QStringLiteral("message"), warning.message);
	return object;
}

QJsonArray packageWarningsJson(const QVector<PackageLoadWarning>& warnings)
{
	QJsonArray array;
	for (const PackageLoadWarning& warning : warnings) {
		array.append(packageWarningJson(warning));
	}
	return array;
}

QJsonObject packageSummaryJson(const PackageArchiveSummary& summary)
{
	QJsonObject object;
	object.insert(QStringLiteral("sourcePath"), summary.sourcePath);
	object.insert(QStringLiteral("format"), packageArchiveFormatId(summary.format));
	object.insert(QStringLiteral("formatLabel"), packageArchiveFormatDisplayName(summary.format));
	object.insert(QStringLiteral("entryCount"), summary.entryCount);
	object.insert(QStringLiteral("fileCount"), summary.fileCount);
	object.insert(QStringLiteral("directoryCount"), summary.directoryCount);
	object.insert(QStringLiteral("nestedArchiveCount"), summary.nestedArchiveCount);
	object.insert(QStringLiteral("totalSizeBytes"), static_cast<double>(summary.totalSizeBytes));
	object.insert(QStringLiteral("warningCount"), summary.warningCount);
	return object;
}

QJsonObject packageEntryJson(const PackageEntry& entry)
{
	QJsonObject object;
	object.insert(QStringLiteral("virtualPath"), entry.virtualPath);
	object.insert(QStringLiteral("kind"), packageEntryKindId(entry.kind));
	object.insert(QStringLiteral("sizeBytes"), static_cast<double>(entry.sizeBytes));
	object.insert(QStringLiteral("compressedSizeBytes"), static_cast<double>(entry.compressedSizeBytes));
	object.insert(QStringLiteral("dataOffset"), static_cast<double>(entry.dataOffset));
	if (entry.modifiedUtc.isValid()) {
		object.insert(QStringLiteral("modifiedUtc"), entry.modifiedUtc.toUTC().toString(Qt::ISODate));
	}
	object.insert(QStringLiteral("typeHint"), entry.typeHint);
	object.insert(QStringLiteral("storageMethod"), entry.storageMethod);
	object.insert(QStringLiteral("sourceArchiveId"), entry.sourceArchiveId);
	object.insert(QStringLiteral("nestedArchiveCandidate"), entry.nestedArchiveCandidate);
	object.insert(QStringLiteral("readable"), entry.readable);
	object.insert(QStringLiteral("note"), entry.note);
	return object;
}

QJsonObject packageInfoJson(const PackageArchive& archive)
{
	QJsonObject object;
	object.insert(QStringLiteral("summary"), packageSummaryJson(archive.summary()));
	object.insert(QStringLiteral("warnings"), packageWarningsJson(archive.warnings()));
	return object;
}

QJsonObject packageTimingJson(qint64 packageOpenMs, qint64 previewMs = -1)
{
	QJsonObject object;
	const qint64 normalizedPackageOpenMs = std::max<qint64>(0, packageOpenMs);
	object.insert(QStringLiteral("packageOpenMs"), static_cast<double>(normalizedPackageOpenMs));
	if (previewMs >= 0) {
		object.insert(QStringLiteral("previewMs"), static_cast<double>(previewMs));
		object.insert(QStringLiteral("totalMs"), static_cast<double>(normalizedPackageOpenMs + previewMs));
	} else {
		object.insert(QStringLiteral("totalMs"), static_cast<double>(normalizedPackageOpenMs));
	}
	return object;
}

QJsonObject packageListJson(const PackageArchive& archive)
{
	QJsonObject object = packageInfoJson(archive);
	QJsonArray entries;
	for (const PackageEntry& entry : archive.entries()) {
		entries.append(packageEntryJson(entry));
	}
	object.insert(QStringLiteral("entries"), entries);
	return object;
}

QJsonObject packagePreviewJson(const PackagePreview& preview)
{
	QJsonObject object;
	object.insert(QStringLiteral("virtualPath"), preview.virtualPath);
	object.insert(QStringLiteral("kind"), packagePreviewKindId(preview.kind));
	object.insert(QStringLiteral("kindLabel"), packagePreviewKindDisplayName(preview.kind));
	object.insert(QStringLiteral("assetKind"), preview.assetKindId);
	object.insert(QStringLiteral("title"), preview.title);
	object.insert(QStringLiteral("summary"), preview.summary);
	object.insert(QStringLiteral("body"), preview.body);
	object.insert(QStringLiteral("details"), stringArrayJson(preview.detailLines));
	object.insert(QStringLiteral("raw"), stringArrayJson(preview.rawLines));
	object.insert(QStringLiteral("assetDetails"), stringArrayJson(preview.assetDetailLines));
	object.insert(QStringLiteral("assetRaw"), stringArrayJson(preview.assetRawLines));
	object.insert(QStringLiteral("truncated"), preview.truncated);
	object.insert(QStringLiteral("bytesRead"), static_cast<double>(preview.bytesRead));
	object.insert(QStringLiteral("totalBytes"), static_cast<double>(preview.totalBytes));
	object.insert(QStringLiteral("error"), preview.error);
	object.insert(QStringLiteral("imageFormat"), preview.imageFormat);
	object.insert(QStringLiteral("imageDepth"), preview.imageDepth);
	object.insert(QStringLiteral("imageColorCount"), preview.imageColorCount);
	object.insert(QStringLiteral("imagePaletteAware"), preview.imagePaletteAware);
	object.insert(QStringLiteral("imagePalette"), stringArrayJson(preview.imagePaletteLines));
	if (preview.imageSize.isValid()) {
		QJsonObject size;
		size.insert(QStringLiteral("width"), preview.imageSize.width());
		size.insert(QStringLiteral("height"), preview.imageSize.height());
		object.insert(QStringLiteral("imageSize"), size);
	}
	object.insert(QStringLiteral("modelFormat"), preview.modelFormat);
	object.insert(QStringLiteral("modelViewport"), stringArrayJson(preview.modelViewportLines));
	object.insert(QStringLiteral("modelMaterials"), stringArrayJson(preview.modelMaterialLines));
	object.insert(QStringLiteral("modelAnimations"), stringArrayJson(preview.modelAnimationLines));
	object.insert(QStringLiteral("audioFormat"), preview.audioFormat);
	object.insert(QStringLiteral("audioWaveform"), stringArrayJson(preview.audioWaveformLines));
	object.insert(QStringLiteral("textLanguageId"), preview.textLanguageId);
	object.insert(QStringLiteral("textLanguageName"), preview.textLanguageName);
	object.insert(QStringLiteral("textHighlights"), stringArrayJson(preview.textHighlightLines));
	object.insert(QStringLiteral("textDiagnostics"), stringArrayJson(preview.textDiagnosticLines));
	object.insert(QStringLiteral("textSaveState"), preview.textSaveState);
	return object;
}

QJsonObject packageExtractionEntryJson(const PackageExtractionEntryResult& result)
{
	QJsonObject object;
	object.insert(QStringLiteral("virtualPath"), result.virtualPath);
	object.insert(QStringLiteral("outputPath"), result.outputPath);
	object.insert(QStringLiteral("kind"), packageEntryKindId(result.kind));
	object.insert(QStringLiteral("bytes"), static_cast<double>(result.bytes));
	object.insert(QStringLiteral("dryRun"), result.dryRun);
	object.insert(QStringLiteral("written"), result.written);
	object.insert(QStringLiteral("skipped"), result.skipped);
	object.insert(QStringLiteral("message"), result.message);
	object.insert(QStringLiteral("error"), result.error);
	return object;
}

QJsonObject packageExtractionReportJson(const PackageExtractionReport& report)
{
	QJsonObject object;
	object.insert(QStringLiteral("sourcePath"), report.sourcePath);
	object.insert(QStringLiteral("targetDirectory"), report.targetDirectory);
	object.insert(QStringLiteral("extractAll"), report.extractAll);
	object.insert(QStringLiteral("dryRun"), report.dryRun);
	object.insert(QStringLiteral("overwriteExisting"), report.overwriteExisting);
	object.insert(QStringLiteral("cancelled"), report.cancelled);
	object.insert(QStringLiteral("succeeded"), report.succeeded());
	object.insert(QStringLiteral("requestedCount"), report.requestedCount);
	object.insert(QStringLiteral("processedCount"), report.processedCount);
	object.insert(QStringLiteral("writtenCount"), report.writtenCount);
	object.insert(QStringLiteral("directoryCount"), report.directoryCount);
	object.insert(QStringLiteral("skippedCount"), report.skippedCount);
	object.insert(QStringLiteral("errorCount"), report.errorCount);
	object.insert(QStringLiteral("totalBytes"), static_cast<double>(report.totalBytes));
	object.insert(QStringLiteral("warnings"), stringArrayJson(report.warnings));
	QJsonArray entries;
	for (const PackageExtractionEntryResult& result : report.entries) {
		entries.append(packageExtractionEntryJson(result));
	}
	object.insert(QStringLiteral("entries"), entries);
	return object;
}

QJsonObject assetImageConversionEntryJson(const AssetImageConversionEntryResult& result)
{
	QJsonObject object;
	object.insert(QStringLiteral("virtualPath"), result.virtualPath);
	object.insert(QStringLiteral("outputPath"), result.outputPath);
	object.insert(QStringLiteral("outputFormat"), result.outputFormat);
	if (result.beforeSize.isValid()) {
		QJsonObject size;
		size.insert(QStringLiteral("width"), result.beforeSize.width());
		size.insert(QStringLiteral("height"), result.beforeSize.height());
		object.insert(QStringLiteral("beforeSize"), size);
	}
	if (result.afterSize.isValid()) {
		QJsonObject size;
		size.insert(QStringLiteral("width"), result.afterSize.width());
		size.insert(QStringLiteral("height"), result.afterSize.height());
		object.insert(QStringLiteral("afterSize"), size);
	}
	object.insert(QStringLiteral("inputBytes"), static_cast<double>(result.inputBytes));
	object.insert(QStringLiteral("outputBytes"), static_cast<double>(result.outputBytes));
	object.insert(QStringLiteral("dryRun"), result.dryRun);
	object.insert(QStringLiteral("written"), result.written);
	object.insert(QStringLiteral("message"), result.message);
	object.insert(QStringLiteral("error"), result.error);
	object.insert(QStringLiteral("preview"), stringArrayJson(result.previewLines));
	return object;
}

QJsonObject assetImageConversionReportJson(const AssetImageConversionReport& report)
{
	QJsonObject object;
	object.insert(QStringLiteral("sourcePath"), report.sourcePath);
	object.insert(QStringLiteral("outputDirectory"), report.outputDirectory);
	object.insert(QStringLiteral("requestedCount"), report.requestedCount);
	object.insert(QStringLiteral("processedCount"), report.processedCount);
	object.insert(QStringLiteral("writtenCount"), report.writtenCount);
	object.insert(QStringLiteral("errorCount"), report.errorCount);
	object.insert(QStringLiteral("totalInputBytes"), static_cast<double>(report.totalInputBytes));
	object.insert(QStringLiteral("totalOutputBytes"), static_cast<double>(report.totalOutputBytes));
	object.insert(QStringLiteral("dryRun"), report.dryRun);
	object.insert(QStringLiteral("succeeded"), report.succeeded());
	object.insert(QStringLiteral("warnings"), stringArrayJson(report.warnings));
	QJsonArray entries;
	for (const AssetImageConversionEntryResult& result : report.entries) {
		entries.append(assetImageConversionEntryJson(result));
	}
	object.insert(QStringLiteral("entries"), entries);
	return object;
}

QJsonObject assetAudioExportReportJson(const AssetAudioExportReport& report)
{
	QJsonObject object;
	object.insert(QStringLiteral("sourcePath"), report.sourcePath);
	object.insert(QStringLiteral("virtualPath"), report.virtualPath);
	object.insert(QStringLiteral("outputPath"), report.outputPath);
	object.insert(QStringLiteral("bytes"), static_cast<double>(report.bytes));
	object.insert(QStringLiteral("dryRun"), report.dryRun);
	object.insert(QStringLiteral("written"), report.written);
	object.insert(QStringLiteral("message"), report.message);
	object.insert(QStringLiteral("error"), report.error);
	object.insert(QStringLiteral("succeeded"), report.succeeded());
	return object;
}

QJsonObject assetTextMatchJson(const AssetTextMatch& match)
{
	QJsonObject object;
	object.insert(QStringLiteral("filePath"), match.filePath);
	object.insert(QStringLiteral("line"), match.line);
	object.insert(QStringLiteral("column"), match.column);
	object.insert(QStringLiteral("lineText"), match.lineText);
	return object;
}

QJsonObject assetTextSearchReportJson(const AssetTextSearchReport& report)
{
	QJsonObject object;
	object.insert(QStringLiteral("rootPath"), report.rootPath);
	object.insert(QStringLiteral("findText"), report.findText);
	object.insert(QStringLiteral("replaceText"), report.replaceText);
	object.insert(QStringLiteral("replace"), report.replace);
	object.insert(QStringLiteral("dryRun"), report.dryRun);
	object.insert(QStringLiteral("filesScanned"), report.filesScanned);
	object.insert(QStringLiteral("filesWithMatches"), report.filesWithMatches);
	object.insert(QStringLiteral("matchCount"), report.matchCount);
	object.insert(QStringLiteral("replacementCount"), report.replacementCount);
	object.insert(QStringLiteral("saveState"), report.saveState);
	object.insert(QStringLiteral("warnings"), stringArrayJson(report.warnings));
	object.insert(QStringLiteral("succeeded"), report.succeeded());
	QJsonArray matches;
	for (const AssetTextMatch& match : report.matches) {
		matches.append(assetTextMatchJson(match));
	}
	object.insert(QStringLiteral("matches"), matches);
	return object;
}

QJsonObject projectManifestJson(const ProjectManifest& manifest)
{
	QJsonObject object;
	object.insert(QStringLiteral("schemaVersion"), manifest.schemaVersion);
	object.insert(QStringLiteral("projectId"), manifest.projectId);
	object.insert(QStringLiteral("displayName"), manifest.displayName);
	object.insert(QStringLiteral("rootPath"), manifest.rootPath);
	object.insert(QStringLiteral("manifestPath"), projectManifestPath(manifest.rootPath));
	object.insert(QStringLiteral("sourceFolders"), stringArrayJson(manifest.sourceFolders));
	object.insert(QStringLiteral("packageFolders"), stringArrayJson(manifest.packageFolders));
	object.insert(QStringLiteral("outputFolder"), manifest.outputFolder);
	object.insert(QStringLiteral("tempFolder"), manifest.tempFolder);
	object.insert(QStringLiteral("selectedInstallationId"), manifest.selectedInstallationId);
	object.insert(QStringLiteral("compilerSearchPaths"), stringArrayJson(manifest.compilerSearchPaths));
	QJsonArray compilerOverrides;
	for (const CompilerToolPathOverride& override : manifest.compilerToolOverrides) {
		QJsonObject overrideObject;
		overrideObject.insert(QStringLiteral("toolId"), override.toolId);
		overrideObject.insert(QStringLiteral("executablePath"), override.executablePath);
		compilerOverrides.append(overrideObject);
	}
	object.insert(QStringLiteral("compilerToolOverrides"), compilerOverrides);
	object.insert(QStringLiteral("registeredOutputPaths"), stringArrayJson(manifest.registeredOutputPaths));
	QJsonObject overrides;
	overrides.insert(QStringLiteral("selectedInstallationId"), manifest.settingsOverrides.selectedInstallationId);
	overrides.insert(QStringLiteral("editorProfileId"), manifest.settingsOverrides.editorProfileId);
	overrides.insert(QStringLiteral("paletteId"), manifest.settingsOverrides.paletteId);
	overrides.insert(QStringLiteral("compilerProfileId"), manifest.settingsOverrides.compilerProfileId);
	overrides.insert(QStringLiteral("aiFreeModeSet"), manifest.settingsOverrides.aiFreeModeSet);
	if (manifest.settingsOverrides.aiFreeModeSet) {
		overrides.insert(QStringLiteral("aiFreeMode"), manifest.settingsOverrides.aiFreeMode);
	}
	object.insert(QStringLiteral("settingsOverrides"), overrides);
	if (manifest.createdUtc.isValid()) {
		object.insert(QStringLiteral("createdUtc"), manifest.createdUtc.toUTC().toString(Qt::ISODate));
	}
	if (manifest.updatedUtc.isValid()) {
		object.insert(QStringLiteral("updatedUtc"), manifest.updatedUtc.toUTC().toString(Qt::ISODate));
	}
	return object;
}

QJsonObject projectHealthCheckJson(const ProjectHealthCheck& check)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), check.id);
	object.insert(QStringLiteral("title"), check.title);
	object.insert(QStringLiteral("detail"), check.detail);
	object.insert(QStringLiteral("state"), operationStateJson(check.state));
	return object;
}

QJsonObject projectHealthJson(const ProjectHealthSummary& health)
{
	QJsonObject object;
	object.insert(QStringLiteral("title"), health.title);
	object.insert(QStringLiteral("detail"), health.detail);
	object.insert(QStringLiteral("state"), operationStateJson(health.overallState()));
	object.insert(QStringLiteral("readyCount"), health.readyCount);
	object.insert(QStringLiteral("warningCount"), health.warningCount);
	object.insert(QStringLiteral("failedCount"), health.failedCount);
	QJsonArray checks;
	for (const ProjectHealthCheck& check : health.checks) {
		checks.append(projectHealthCheckJson(check));
	}
	object.insert(QStringLiteral("checks"), checks);
	return object;
}

QJsonObject gameInstallationValidationJson(const GameInstallationValidation& validation)
{
	QJsonObject object;
	object.insert(QStringLiteral("usable"), validation.isUsable());
	object.insert(QStringLiteral("rootExists"), validation.rootExists);
	object.insert(QStringLiteral("rootIsDirectory"), validation.rootIsDirectory);
	object.insert(QStringLiteral("executableExists"), validation.executableExists);
	object.insert(QStringLiteral("executableIsFile"), validation.executableIsFile);
	object.insert(QStringLiteral("warnings"), stringArrayJson(validation.warnings));
	object.insert(QStringLiteral("errors"), stringArrayJson(validation.errors));
	return object;
}

QJsonObject gameInstallationProfileJson(const GameInstallationProfile& profile, const QString& selectedId)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), profile.id);
	object.insert(QStringLiteral("gameKey"), profile.gameKey);
	object.insert(QStringLiteral("engineFamily"), gameEngineFamilyId(profile.engineFamily));
	object.insert(QStringLiteral("engineFamilyLabel"), gameEngineFamilyDisplayName(profile.engineFamily));
	object.insert(QStringLiteral("displayName"), profile.displayName);
	object.insert(QStringLiteral("rootPath"), profile.rootPath);
	object.insert(QStringLiteral("executablePath"), profile.executablePath);
	object.insert(QStringLiteral("basePackagePaths"), stringArrayJson(profile.basePackagePaths));
	object.insert(QStringLiteral("modPackagePaths"), stringArrayJson(profile.modPackagePaths));
	object.insert(QStringLiteral("paletteId"), profile.paletteId);
	object.insert(QStringLiteral("compilerProfileId"), profile.compilerProfileId);
	object.insert(QStringLiteral("readOnly"), profile.readOnly);
	object.insert(QStringLiteral("active"), profile.active);
	object.insert(QStringLiteral("hidden"), profile.hidden);
	object.insert(QStringLiteral("manual"), profile.manual);
	object.insert(QStringLiteral("selected"), sameGameInstallationId(profile.id, selectedId));
	if (profile.createdUtc.isValid()) {
		object.insert(QStringLiteral("createdUtc"), profile.createdUtc.toUTC().toString(Qt::ISODate));
	}
	if (profile.updatedUtc.isValid()) {
		object.insert(QStringLiteral("updatedUtc"), profile.updatedUtc.toUTC().toString(Qt::ISODate));
	}
	object.insert(QStringLiteral("validation"), gameInstallationValidationJson(validateGameInstallationProfile(profile)));
	return object;
}

QJsonObject gameInstallationDetectionCandidateJson(const GameInstallationDetectionCandidate& candidate)
{
	QJsonObject object;
	object.insert(QStringLiteral("sourceId"), candidate.sourceId);
	object.insert(QStringLiteral("sourceName"), candidate.sourceName);
	object.insert(QStringLiteral("confidencePercent"), candidate.confidencePercent);
	object.insert(QStringLiteral("matchedPaths"), stringArrayJson(candidate.matchedPaths));
	object.insert(QStringLiteral("warnings"), stringArrayJson(candidate.warnings));
	object.insert(QStringLiteral("profile"), gameInstallationProfileJson(candidate.profile, QString()));
	return object;
}

QJsonObject gameInstallationDetectionJson(const QVector<GameInstallationDetectionCandidate>& candidates)
{
	QJsonObject object;
	object.insert(QStringLiteral("candidateCount"), candidates.size());
	QJsonArray array;
	for (const GameInstallationDetectionCandidate& candidate : candidates) {
		array.append(gameInstallationDetectionCandidateJson(candidate));
	}
	object.insert(QStringLiteral("candidates"), array);
	return object;
}

QJsonObject gameInstallationsJson(const StudioSettings& settings)
{
	QJsonObject object;
	const QString selectedId = settings.selectedGameInstallationId();
	object.insert(QStringLiteral("selectedInstallationId"), selectedId);
	QJsonArray profiles;
	for (const GameInstallationProfile& profile : settings.gameInstallations()) {
		profiles.append(gameInstallationProfileJson(profile, selectedId));
	}
	object.insert(QStringLiteral("profiles"), profiles);
	return object;
}

QJsonObject compilerToolDiscoveryJson(const CompilerToolDiscovery& discovery)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), discovery.descriptor.id);
	object.insert(QStringLiteral("integrationId"), discovery.descriptor.integrationId);
	object.insert(QStringLiteral("displayName"), discovery.descriptor.displayName);
	object.insert(QStringLiteral("engineFamily"), discovery.descriptor.engineFamily);
	object.insert(QStringLiteral("role"), discovery.descriptor.role);
	object.insert(QStringLiteral("declaredSourcePath"), discovery.descriptor.sourcePath);
	object.insert(QStringLiteral("executableNames"), stringArrayJson(discovery.descriptor.executableNames));
	object.insert(QStringLiteral("candidateRelativePaths"), stringArrayJson(discovery.descriptor.candidateRelativePaths));
	object.insert(QStringLiteral("versionProbeArguments"), stringArrayJson(discovery.descriptor.versionProbeArguments));
	object.insert(QStringLiteral("sourceAvailable"), discovery.sourceAvailable);
	object.insert(QStringLiteral("executableAvailable"), discovery.executableAvailable);
	object.insert(QStringLiteral("executablePathOverridden"), discovery.executablePathOverridden);
	object.insert(QStringLiteral("versionProbeAttempted"), discovery.versionProbeAttempted);
	object.insert(QStringLiteral("versionAvailable"), discovery.versionAvailable);
	object.insert(QStringLiteral("sourcePath"), discovery.sourcePath);
	object.insert(QStringLiteral("executablePath"), discovery.executablePath);
	object.insert(QStringLiteral("versionText"), discovery.versionText);
	object.insert(QStringLiteral("versionProbeCommandLine"), discovery.versionProbeCommandLine);
	object.insert(QStringLiteral("capabilityFlags"), stringArrayJson(discovery.capabilityFlags));
	object.insert(QStringLiteral("warnings"), stringArrayJson(discovery.warnings));
	object.insert(QStringLiteral("state"), operationStateJson(discovery.state()));
	return object;
}

QJsonObject compilerRegistryJson(const CompilerRegistrySummary& summary)
{
	QJsonObject object;
	object.insert(QStringLiteral("state"), operationStateJson(summary.overallState()));
	object.insert(QStringLiteral("sourceAvailableCount"), summary.sourceAvailableCount);
	object.insert(QStringLiteral("executableAvailableCount"), summary.executableAvailableCount);
	object.insert(QStringLiteral("warningCount"), summary.warningCount);
	QJsonArray tools;
	for (const CompilerToolDiscovery& discovery : summary.tools) {
		tools.append(compilerToolDiscoveryJson(discovery));
	}
	object.insert(QStringLiteral("tools"), tools);
	return object;
}

QJsonObject compilerProfileJson(const CompilerProfileDescriptor& profile)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), profile.id);
	object.insert(QStringLiteral("toolId"), profile.toolId);
	object.insert(QStringLiteral("displayName"), profile.displayName);
	object.insert(QStringLiteral("engineFamily"), profile.engineFamily);
	object.insert(QStringLiteral("stageId"), profile.stageId);
	object.insert(QStringLiteral("inputDescription"), profile.inputDescription);
	object.insert(QStringLiteral("inputExtensions"), stringArrayJson(profile.inputExtensions));
	object.insert(QStringLiteral("defaultOutputExtension"), profile.defaultOutputExtension);
	object.insert(QStringLiteral("description"), profile.description);
	object.insert(QStringLiteral("defaultArguments"), stringArrayJson(profile.defaultArguments));
	object.insert(QStringLiteral("inputRequired"), profile.inputRequired);
	object.insert(QStringLiteral("outputPathArgumentSupported"), profile.outputPathArgumentSupported);
	return object;
}

QJsonArray compilerProfilesJson()
{
	QJsonArray profiles;
	for (const CompilerProfileDescriptor& profile : compilerProfileDescriptors()) {
		profiles.append(compilerProfileJson(profile));
	}
	return profiles;
}

QJsonObject editorProfileBindingJson(const EditorProfileBinding& binding)
{
	QJsonObject object;
	object.insert(QStringLiteral("actionId"), binding.actionId);
	object.insert(QStringLiteral("displayName"), binding.displayName);
	object.insert(QStringLiteral("shortcut"), binding.shortcut);
	object.insert(QStringLiteral("mouseGesture"), binding.mouseGesture);
	object.insert(QStringLiteral("context"), binding.context);
	object.insert(QStringLiteral("commandId"), binding.commandId);
	object.insert(QStringLiteral("surfaceId"), binding.surfaceId);
	object.insert(QStringLiteral("implemented"), binding.implemented);
	return object;
}

QJsonObject editorProfileJson(const EditorProfileDescriptor& profile, const QString& selectedId = QString())
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), profile.id);
	object.insert(QStringLiteral("displayName"), profile.displayName);
	object.insert(QStringLiteral("shortName"), profile.shortName);
	object.insert(QStringLiteral("lineage"), profile.lineage);
	object.insert(QStringLiteral("description"), profile.description);
	object.insert(QStringLiteral("layoutPresetId"), profile.layoutPresetId);
	object.insert(QStringLiteral("cameraPresetId"), profile.cameraPresetId);
	object.insert(QStringLiteral("selectionPresetId"), profile.selectionPresetId);
	object.insert(QStringLiteral("gridPresetId"), profile.gridPresetId);
	object.insert(QStringLiteral("terminologyPresetId"), profile.terminologyPresetId);
	object.insert(QStringLiteral("supportedEngineFamilies"), stringArrayJson(profile.supportedEngineFamilies));
	object.insert(QStringLiteral("defaultPanels"), stringArrayJson(profile.defaultPanels));
	object.insert(QStringLiteral("workflowNotes"), stringArrayJson(profile.workflowNotes));
	object.insert(QStringLiteral("keybindingNotes"), stringArrayJson(profile.keybindingNotes));
	object.insert(QStringLiteral("mouseBindingNotes"), stringArrayJson(profile.mouseBindingNotes));
	object.insert(QStringLiteral("placeholder"), profile.placeholder);
	object.insert(QStringLiteral("selected"), !selectedId.isEmpty() && profile.id == selectedId);
	QJsonArray bindings;
	for (const EditorProfileBinding& binding : profile.bindings) {
		bindings.append(editorProfileBindingJson(binding));
	}
	object.insert(QStringLiteral("bindings"), bindings);
	return object;
}

QJsonArray editorProfilesJson(const QString& selectedId = QString())
{
	QJsonArray profiles;
	for (const EditorProfileDescriptor& profile : editorProfileDescriptors()) {
		profiles.append(editorProfileJson(profile, selectedId));
	}
	return profiles;
}

QJsonObject localizationTargetJson(const LocalizationTarget& target)
{
	QJsonObject object;
	object.insert(QStringLiteral("localeName"), target.localeName);
	object.insert(QStringLiteral("englishName"), target.englishName);
	object.insert(QStringLiteral("nativeName"), target.nativeName);
	object.insert(QStringLiteral("rightToLeft"), target.rightToLeft);
	return object;
}

QJsonArray localizationTargetsJson()
{
	QJsonArray array;
	for (const LocalizationTarget& target : localizationTargets()) {
		array.append(localizationTargetJson(target));
	}
	return array;
}

QJsonObject localeFormattingSampleJson(const LocaleFormattingSample& sample)
{
	QJsonObject object;
	object.insert(QStringLiteral("localeName"), sample.localeName);
	object.insert(QStringLiteral("decimalNumber"), sample.decimalNumber);
	object.insert(QStringLiteral("integerNumber"), sample.integerNumber);
	object.insert(QStringLiteral("date"), sample.date);
	object.insert(QStringLiteral("time"), sample.time);
	object.insert(QStringLiteral("dateTime"), sample.dateTime);
	object.insert(QStringLiteral("size"), sample.size);
	object.insert(QStringLiteral("duration"), sample.duration);
	object.insert(QStringLiteral("sortedLabels"), stringArrayJson(sample.sortedLabels));
	return object;
}

QJsonObject pluralizationSmokeSampleJson(const PluralizationSmokeSample& sample)
{
	QJsonObject object;
	object.insert(QStringLiteral("localeName"), sample.localeName);
	object.insert(QStringLiteral("count"), sample.count);
	object.insert(QStringLiteral("localizedNumber"), sample.localizedNumber);
	object.insert(QStringLiteral("text"), sample.text);
	object.insert(QStringLiteral("singular"), sample.singular);
	object.insert(QStringLiteral("localizedNumberVisible"), sample.localizedNumberVisible);
	return object;
}

QJsonObject translationExpansionLayoutCheckJson(const TranslationExpansionLayoutCheck& check)
{
	QJsonObject object;
	object.insert(QStringLiteral("surfaceId"), check.surfaceId);
	object.insert(QStringLiteral("label"), check.label);
	object.insert(QStringLiteral("sourceText"), check.sourceText);
	object.insert(QStringLiteral("expandedText"), check.expandedText);
	object.insert(QStringLiteral("sourceLength"), check.sourceLength);
	object.insert(QStringLiteral("expandedLength"), check.expandedLength);
	object.insert(QStringLiteral("maxRecommendedCharacters"), check.maxRecommendedCharacters);
	object.insert(QStringLiteral("expansionRatio"), check.expansionRatio);
	object.insert(QStringLiteral("passed"), check.passed);
	object.insert(QStringLiteral("recommendation"), check.recommendation);
	return object;
}

QJsonObject translationCatalogStatusJson(const TranslationCatalogStatus& status)
{
	QJsonObject object;
	object.insert(QStringLiteral("localeName"), status.localeName);
	object.insert(QStringLiteral("fileName"), status.fileName);
	object.insert(QStringLiteral("path"), status.path);
	object.insert(QStringLiteral("present"), status.present);
	object.insert(QStringLiteral("stale"), status.stale);
	object.insert(QStringLiteral("messageCount"), status.messageCount);
	object.insert(QStringLiteral("translatedCount"), status.translatedCount);
	object.insert(QStringLiteral("unfinishedCount"), status.unfinishedCount);
	object.insert(QStringLiteral("obsoleteCount"), status.obsoleteCount);
	object.insert(QStringLiteral("vanishedCount"), status.vanishedCount);
	object.insert(QStringLiteral("status"), status.status);
	object.insert(QStringLiteral("issues"), stringArrayJson(status.issues));
	return object;
}

QJsonObject localizationSmokeReportJson(const LocalizationSmokeReport& report)
{
	QJsonObject object;
	object.insert(QStringLiteral("localeName"), report.localeName);
	object.insert(QStringLiteral("targetCount"), report.targets.size());
	object.insert(QStringLiteral("targets"), localizationTargetsJson());
	object.insert(QStringLiteral("pseudoSample"), report.pseudoSample);
	object.insert(QStringLiteral("expansionSample"), report.expansionSample);
	object.insert(QStringLiteral("expansionSourceLength"), report.expansionSourceLength);
	object.insert(QStringLiteral("expansionSampleLength"), report.expansionSampleLength);
	object.insert(QStringLiteral("expansionRatio"), report.expansionRatio);
	object.insert(QStringLiteral("expansionSmokeOk"), report.expansionSmokeOk);
	object.insert(QStringLiteral("expansionLayoutSmokeOk"), report.expansionLayoutSmokeOk);
	object.insert(QStringLiteral("pluralizationSmokeOk"), report.pluralizationSmokeOk);
	object.insert(QStringLiteral("rightToLeftLocales"), stringArrayJson(report.rightToLeftLocales));
	object.insert(QStringLiteral("formatting"), localeFormattingSampleJson(report.formatting));
	QJsonArray pluralization;
	for (const PluralizationSmokeSample& sample : report.pluralization) {
		pluralization.append(pluralizationSmokeSampleJson(sample));
	}
	object.insert(QStringLiteral("pluralization"), pluralization);
	QJsonArray layoutChecks;
	for (const TranslationExpansionLayoutCheck& check : report.layoutChecks) {
		layoutChecks.append(translationExpansionLayoutCheckJson(check));
	}
	object.insert(QStringLiteral("layoutChecks"), layoutChecks);
	QJsonArray catalogs;
	for (const TranslationCatalogStatus& status : report.catalogs) {
		catalogs.append(translationCatalogStatusJson(status));
	}
	object.insert(QStringLiteral("catalogs"), catalogs);
	object.insert(QStringLiteral("catalogCount"), report.catalogCount);
	object.insert(QStringLiteral("staleCatalogCount"), report.staleCatalogCount);
	object.insert(QStringLiteral("untranslatedMessageCount"), report.untranslatedMessageCount);
	object.insert(QStringLiteral("obsoleteMessageCount"), report.obsoleteMessageCount);
	object.insert(QStringLiteral("warnings"), stringArrayJson(report.warnings));
	object.insert(QStringLiteral("ok"), report.ok);
	return object;
}

QJsonObject studioModuleJson(const StudioModule& module)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), module.id);
	object.insert(QStringLiteral("name"), module.name);
	object.insert(QStringLiteral("category"), module.category);
	object.insert(QStringLiteral("maturity"), module.maturity);
	object.insert(QStringLiteral("description"), module.description);
	object.insert(QStringLiteral("engines"), stringArrayJson(module.engines));
	return object;
}

QJsonArray studioModulesJson()
{
	QJsonArray array;
	for (const StudioModule& module : plannedModules()) {
		array.append(studioModuleJson(module));
	}
	return array;
}

QJsonObject diagnosticBundleJson()
{
	QJsonObject object;
	object.insert(QStringLiteral("schemaVersion"), 1);
	object.insert(QStringLiteral("generatedUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
	object.insert(QStringLiteral("version"), versionString());
	object.insert(QStringLiteral("qtVersion"), QString::fromUtf8(qVersion()));
	object.insert(QStringLiteral("buildAbi"), QSysInfo::buildAbi());
	object.insert(QStringLiteral("kernelType"), QSysInfo::kernelType());
	object.insert(QStringLiteral("kernelVersion"), QSysInfo::kernelVersion());
	object.insert(QStringLiteral("productType"), QSysInfo::productType());
	object.insert(QStringLiteral("productVersion"), QSysInfo::productVersion());
	object.insert(QStringLiteral("commandCount"), cliCommandDescriptors().size());
	object.insert(QStringLiteral("commands"), cliCommandsJson());
	object.insert(QStringLiteral("modules"), studioModulesJson());
	object.insert(QStringLiteral("operationStates"), operationStatesJson());
	object.insert(QStringLiteral("uiSemantics"), uiSemanticsJson());
	object.insert(QStringLiteral("localization"), localizationSmokeReportJson(buildLocalizationSmokeReport(QStringLiteral("en"), QStringLiteral("i18n"))));
	object.insert(QStringLiteral("redaction"), QStringLiteral("No secrets, API keys, environment values, home-directory contents, or project file payloads are included."));
	return object;
}

QJsonObject aiCapabilityJson(const AiCapabilityDescriptor& capability)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), capability.id);
	object.insert(QStringLiteral("displayName"), capability.displayName);
	object.insert(QStringLiteral("description"), capability.description);
	return object;
}

QJsonArray aiCapabilitiesJson()
{
	QJsonArray capabilities;
	for (const AiCapabilityDescriptor& capability : aiCapabilityDescriptors()) {
		capabilities.append(aiCapabilityJson(capability));
	}
	return capabilities;
}

QJsonObject aiConnectorJson(const AiConnectorDescriptor& connector)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), connector.id);
	object.insert(QStringLiteral("displayName"), connector.displayName);
	object.insert(QStringLiteral("providerFamily"), connector.providerFamily);
	object.insert(QStringLiteral("endpointKind"), connector.endpointKind);
	object.insert(QStringLiteral("authRequirement"), connector.authRequirement);
	object.insert(QStringLiteral("privacyNote"), connector.privacyNote);
	object.insert(QStringLiteral("capabilities"), stringArrayJson(connector.capabilities));
	object.insert(QStringLiteral("configurationNotes"), stringArrayJson(connector.configurationNotes));
	object.insert(QStringLiteral("cloudBased"), connector.cloudBased);
	object.insert(QStringLiteral("implemented"), connector.implemented);
	return object;
}

QJsonArray aiConnectorsJson()
{
	QJsonArray connectors;
	for (const AiConnectorDescriptor& connector : aiConnectorDescriptors()) {
		connectors.append(aiConnectorJson(connector));
	}
	return connectors;
}

QJsonObject aiModelJson(const AiModelDescriptor& model)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), model.id);
	object.insert(QStringLiteral("connectorId"), model.connectorId);
	object.insert(QStringLiteral("displayName"), model.displayName);
	object.insert(QStringLiteral("description"), model.description);
	object.insert(QStringLiteral("capabilities"), stringArrayJson(model.capabilities));
	object.insert(QStringLiteral("defaultForConnector"), model.defaultForConnector);
	return object;
}

QJsonArray aiModelsJson()
{
	QJsonArray models;
	for (const AiModelDescriptor& model : aiModelDescriptors()) {
		models.append(aiModelJson(model));
	}
	return models;
}

QJsonObject aiCredentialStatusJson(const AiCredentialStatus& status)
{
	QJsonObject object;
	object.insert(QStringLiteral("connectorId"), status.connectorId);
	object.insert(QStringLiteral("configured"), status.configured);
	object.insert(QStringLiteral("source"), status.source);
	object.insert(QStringLiteral("lookupKey"), status.lookupKey);
	object.insert(QStringLiteral("redactedValue"), status.redactedValue);
	object.insert(QStringLiteral("warnings"), stringArrayJson(status.warnings));
	return object;
}

QJsonArray aiCredentialStatusesJson(const AiAutomationPreferences& preferences)
{
	QJsonArray statuses;
	for (const AiCredentialStatus& status : aiCredentialStatuses(preferences)) {
		statuses.append(aiCredentialStatusJson(status));
	}
	return statuses;
}

QJsonArray aiToolsJson()
{
	QJsonArray tools;
	for (const AiToolDescriptor& tool : aiToolDescriptors()) {
		tools.append(aiToolDescriptorJson(tool));
	}
	return tools;
}

QJsonObject aiPreferencesJson(const AiAutomationPreferences& preferences)
{
	const AiAutomationPreferences normalized = normalizedAiAutomationPreferences(preferences);
	QJsonObject object;
	object.insert(QStringLiteral("aiFreeMode"), normalized.aiFreeMode);
	object.insert(QStringLiteral("cloudConnectorsEnabled"), normalized.cloudConnectorsEnabled);
	object.insert(QStringLiteral("agenticWorkflowsEnabled"), normalized.agenticWorkflowsEnabled);
	object.insert(QStringLiteral("preferredReasoningConnectorId"), normalized.preferredReasoningConnectorId);
	object.insert(QStringLiteral("preferredCodingConnectorId"), normalized.preferredCodingConnectorId);
	object.insert(QStringLiteral("preferredVisionConnectorId"), normalized.preferredVisionConnectorId);
	object.insert(QStringLiteral("preferredImageConnectorId"), normalized.preferredImageConnectorId);
	object.insert(QStringLiteral("preferredAudioConnectorId"), normalized.preferredAudioConnectorId);
	object.insert(QStringLiteral("preferredVoiceConnectorId"), normalized.preferredVoiceConnectorId);
	object.insert(QStringLiteral("preferredThreeDConnectorId"), normalized.preferredThreeDConnectorId);
	object.insert(QStringLiteral("preferredEmbeddingsConnectorId"), normalized.preferredEmbeddingsConnectorId);
	object.insert(QStringLiteral("preferredLocalConnectorId"), normalized.preferredLocalConnectorId);
	object.insert(QStringLiteral("preferredTextModelId"), normalized.preferredTextModelId);
	object.insert(QStringLiteral("preferredCodingModelId"), normalized.preferredCodingModelId);
	object.insert(QStringLiteral("preferredVisionModelId"), normalized.preferredVisionModelId);
	object.insert(QStringLiteral("preferredImageModelId"), normalized.preferredImageModelId);
	object.insert(QStringLiteral("preferredAudioModelId"), normalized.preferredAudioModelId);
	object.insert(QStringLiteral("preferredVoiceModelId"), normalized.preferredVoiceModelId);
	object.insert(QStringLiteral("preferredThreeDModelId"), normalized.preferredThreeDModelId);
	object.insert(QStringLiteral("preferredEmbeddingsModelId"), normalized.preferredEmbeddingsModelId);
	object.insert(QStringLiteral("openAiCredentialEnvironmentVariable"), normalized.openAiCredentialEnvironmentVariable);
	object.insert(QStringLiteral("elevenLabsCredentialEnvironmentVariable"), normalized.elevenLabsCredentialEnvironmentVariable);
	object.insert(QStringLiteral("meshyCredentialEnvironmentVariable"), normalized.meshyCredentialEnvironmentVariable);
	object.insert(QStringLiteral("customHttpCredentialEnvironmentVariable"), normalized.customHttpCredentialEnvironmentVariable);
	return object;
}

QJsonObject aiWorkflowResultJson(const AiWorkflowResult& result)
{
	QJsonObject object;
	object.insert(QStringLiteral("title"), result.title);
	object.insert(QStringLiteral("summary"), result.summary);
	object.insert(QStringLiteral("reviewableText"), result.reviewableText);
	object.insert(QStringLiteral("commandLine"), result.commandLine);
	object.insert(QStringLiteral("nextActions"), stringArrayJson(result.nextActions));
	object.insert(QStringLiteral("diagnostics"), stringArrayJson(result.diagnostics));
	object.insert(QStringLiteral("manifest"), aiWorkflowManifestJson(result.manifest));
	return object;
}

QJsonObject taskStateJson(const QString& taskId, OperationState state, const QString& summary, qint64 durationMs, bool cancellable)
{
	QJsonObject object;
	object.insert(QStringLiteral("taskId"), taskId);
	object.insert(QStringLiteral("state"), operationStateJson(state));
	object.insert(QStringLiteral("summary"), summary);
	object.insert(QStringLiteral("durationMs"), QString::number(durationMs));
	object.insert(QStringLiteral("cancellable"), cancellable);
	object.insert(QStringLiteral("updatedUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
	return object;
}

QJsonObject compilerCommandPlanJson(const CompilerCommandPlan& plan)
{
	QJsonObject object;
	object.insert(QStringLiteral("profileFound"), plan.profileFound);
	object.insert(QStringLiteral("toolFound"), plan.toolFound);
	object.insert(QStringLiteral("executableAvailable"), plan.executableAvailable);
	object.insert(QStringLiteral("runnable"), plan.isRunnable());
	object.insert(QStringLiteral("state"), operationStateJson(plan.state()));
	if (plan.profileFound) {
		object.insert(QStringLiteral("profile"), compilerProfileJson(plan.profile));
	}
	if (plan.toolFound) {
		object.insert(QStringLiteral("tool"), compilerToolDiscoveryJson(plan.tool));
	}
	object.insert(QStringLiteral("program"), plan.program);
	object.insert(QStringLiteral("arguments"), stringArrayJson(plan.arguments));
	object.insert(QStringLiteral("workingDirectory"), plan.workingDirectory);
	object.insert(QStringLiteral("inputPath"), plan.inputPath);
	object.insert(QStringLiteral("expectedOutputPath"), plan.expectedOutputPath);
	object.insert(QStringLiteral("commandLine"), plan.commandLine);
	object.insert(QStringLiteral("knownIssueWarnings"), stringArrayJson(plan.knownIssueWarnings));
	object.insert(QStringLiteral("preflightWarnings"), stringArrayJson(plan.preflightWarnings));
	object.insert(QStringLiteral("warnings"), stringArrayJson(plan.warnings));
	object.insert(QStringLiteral("errors"), stringArrayJson(plan.errors));
	return object;
}

QStringList optionPathList(const QStringList& args, const QString& option)
{
	const QString value = optionValue(args, option);
	if (value.trimmed().isEmpty()) {
		return {};
	}
	return value.split(';', Qt::SkipEmptyParts);
}

QJsonObject compilerDiagnosticJson(const CompilerDiagnostic& diagnostic)
{
	QJsonObject object;
	object.insert(QStringLiteral("level"), diagnostic.level);
	object.insert(QStringLiteral("message"), diagnostic.message);
	object.insert(QStringLiteral("filePath"), diagnostic.filePath);
	object.insert(QStringLiteral("line"), diagnostic.line);
	object.insert(QStringLiteral("column"), diagnostic.column);
	object.insert(QStringLiteral("rawLine"), diagnostic.rawLine);
	return object;
}

QJsonArray compilerDiagnosticsJson(const QVector<CompilerDiagnostic>& diagnostics)
{
	QJsonArray array;
	for (const CompilerDiagnostic& diagnostic : diagnostics) {
		array.append(compilerDiagnosticJson(diagnostic));
	}
	return array;
}

QJsonObject compilerRunResultJson(const CompilerRunResult& result)
{
	QJsonObject object;
	object.insert(QStringLiteral("state"), operationStateJson(result.state));
	object.insert(QStringLiteral("started"), result.started);
	object.insert(QStringLiteral("timedOut"), result.timedOut);
	object.insert(QStringLiteral("cancelled"), result.cancelled);
	object.insert(QStringLiteral("exitCode"), result.exitCode);
	object.insert(QStringLiteral("durationMs"), QString::number(result.durationMs));
	object.insert(QStringLiteral("stdout"), result.stdoutText);
	object.insert(QStringLiteral("stderr"), result.stderrText);
	object.insert(QStringLiteral("diagnostics"), compilerDiagnosticsJson(result.diagnostics));
	object.insert(QStringLiteral("registeredOutputPaths"), stringArrayJson(result.registeredOutputPaths));
	object.insert(QStringLiteral("manifestPath"), result.manifestPath);
	object.insert(QStringLiteral("error"), result.error);
	object.insert(QStringLiteral("manifest"), compilerCommandManifestJson(result.manifest));
	return object;
}

QJsonObject levelMapVec3Json(const LevelMapVec3& value)
{
	QJsonObject object;
	object.insert(QStringLiteral("valid"), value.valid);
	object.insert(QStringLiteral("x"), value.x);
	object.insert(QStringLiteral("y"), value.y);
	object.insert(QStringLiteral("z"), value.z);
	return object;
}

QJsonObject levelMapIssueJson(const LevelMapIssue& issue)
{
	QJsonObject object;
	object.insert(QStringLiteral("severity"), levelMapIssueSeverityId(issue.severity));
	object.insert(QStringLiteral("code"), issue.code);
	object.insert(QStringLiteral("message"), issue.message);
	object.insert(QStringLiteral("objectId"), issue.objectId);
	object.insert(QStringLiteral("line"), issue.line);
	return object;
}

QJsonArray levelMapIssuesJson(const QVector<LevelMapIssue>& issues)
{
	QJsonArray array;
	for (const LevelMapIssue& issue : issues) {
		array.append(levelMapIssueJson(issue));
	}
	return array;
}

QJsonObject levelMapPropertyJson(const LevelMapProperty& property)
{
	QJsonObject object;
	object.insert(QStringLiteral("key"), property.key);
	object.insert(QStringLiteral("value"), property.value);
	object.insert(QStringLiteral("line"), property.line);
	return object;
}

QJsonArray levelMapPropertiesJson(const QVector<LevelMapProperty>& properties)
{
	QJsonArray array;
	for (const LevelMapProperty& property : properties) {
		array.append(levelMapPropertyJson(property));
	}
	return array;
}

QJsonObject levelMapEntityJson(const LevelMapEntity& entity)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), entity.id);
	object.insert(QStringLiteral("className"), entity.className);
	object.insert(QStringLiteral("origin"), levelMapVec3Json(entity.origin));
	object.insert(QStringLiteral("startLine"), entity.startLine);
	object.insert(QStringLiteral("endLine"), entity.endLine);
	object.insert(QStringLiteral("selected"), entity.selected);
	object.insert(QStringLiteral("properties"), levelMapPropertiesJson(entity.properties));
	return object;
}

QJsonArray levelMapEntitiesJson(const QVector<LevelMapEntity>& entities)
{
	QJsonArray array;
	for (const LevelMapEntity& entity : entities) {
		array.append(levelMapEntityJson(entity));
	}
	return array;
}

QJsonObject levelMapBrushJson(const LevelMapBrush& brush)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), brush.id);
	object.insert(QStringLiteral("entityId"), brush.entityId);
	object.insert(QStringLiteral("faceCount"), brush.faceCount);
	object.insert(QStringLiteral("textureNames"), stringArrayJson(brush.textureNames));
	object.insert(QStringLiteral("mins"), levelMapVec3Json(brush.mins));
	object.insert(QStringLiteral("maxs"), levelMapVec3Json(brush.maxs));
	object.insert(QStringLiteral("selected"), brush.selected);
	return object;
}

QJsonArray levelMapBrushesJson(const QVector<LevelMapBrush>& brushes)
{
	QJsonArray array;
	for (const LevelMapBrush& brush : brushes) {
		array.append(levelMapBrushJson(brush));
	}
	return array;
}

QJsonObject levelMapDoomVertexJson(const LevelMapDoomVertex& vertex)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), vertex.id);
	object.insert(QStringLiteral("x"), vertex.x);
	object.insert(QStringLiteral("y"), vertex.y);
	object.insert(QStringLiteral("selected"), vertex.selected);
	return object;
}

QJsonArray levelMapDoomVerticesJson(const QVector<LevelMapDoomVertex>& vertices)
{
	QJsonArray array;
	for (const LevelMapDoomVertex& vertex : vertices) {
		array.append(levelMapDoomVertexJson(vertex));
	}
	return array;
}

QJsonObject levelMapDoomLinedefJson(const LevelMapDoomLinedef& linedef)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), linedef.id);
	object.insert(QStringLiteral("startVertex"), linedef.startVertex);
	object.insert(QStringLiteral("endVertex"), linedef.endVertex);
	object.insert(QStringLiteral("flags"), linedef.flags);
	object.insert(QStringLiteral("special"), linedef.special);
	object.insert(QStringLiteral("tag"), linedef.tag);
	object.insert(QStringLiteral("frontSidedef"), linedef.frontSidedef);
	object.insert(QStringLiteral("backSidedef"), linedef.backSidedef);
	object.insert(QStringLiteral("selected"), linedef.selected);
	return object;
}

QJsonArray levelMapDoomLinedefsJson(const QVector<LevelMapDoomLinedef>& linedefs)
{
	QJsonArray array;
	for (const LevelMapDoomLinedef& linedef : linedefs) {
		array.append(levelMapDoomLinedefJson(linedef));
	}
	return array;
}

QJsonObject levelMapDoomThingJson(const LevelMapDoomThing& thing)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), thing.id);
	object.insert(QStringLiteral("x"), thing.x);
	object.insert(QStringLiteral("y"), thing.y);
	object.insert(QStringLiteral("angle"), thing.angle);
	object.insert(QStringLiteral("type"), thing.type);
	object.insert(QStringLiteral("flags"), thing.flags);
	object.insert(QStringLiteral("selected"), thing.selected);
	return object;
}

QJsonArray levelMapDoomThingsJson(const QVector<LevelMapDoomThing>& things)
{
	QJsonArray array;
	for (const LevelMapDoomThing& thing : things) {
		array.append(levelMapDoomThingJson(thing));
	}
	return array;
}

QJsonObject levelMapStatisticsJson(const LevelMapStatistics& stats)
{
	QJsonObject object;
	object.insert(QStringLiteral("entityCount"), stats.entityCount);
	object.insert(QStringLiteral("brushCount"), stats.brushCount);
	object.insert(QStringLiteral("brushFaceCount"), stats.brushFaceCount);
	object.insert(QStringLiteral("doomThingCount"), stats.doomThingCount);
	object.insert(QStringLiteral("doomVertexCount"), stats.doomVertexCount);
	object.insert(QStringLiteral("doomLinedefCount"), stats.doomLinedefCount);
	object.insert(QStringLiteral("doomSidedefCount"), stats.doomSidedefCount);
	object.insert(QStringLiteral("doomSectorCount"), stats.doomSectorCount);
	object.insert(QStringLiteral("textureReferenceCount"), stats.textureReferenceCount);
	object.insert(QStringLiteral("uniqueTextureCount"), stats.uniqueTextureCount);
	object.insert(QStringLiteral("issueCount"), stats.issueCount);
	object.insert(QStringLiteral("warningCount"), stats.warningCount);
	object.insert(QStringLiteral("errorCount"), stats.errorCount);
	object.insert(QStringLiteral("mins"), levelMapVec3Json(stats.mins));
	object.insert(QStringLiteral("maxs"), levelMapVec3Json(stats.maxs));
	return object;
}

QJsonObject levelMapDocumentJson(const LevelMapDocument& document)
{
	QJsonObject object;
	object.insert(QStringLiteral("sourcePath"), document.sourcePath);
	object.insert(QStringLiteral("outputPath"), document.outputPath);
	object.insert(QStringLiteral("mapName"), document.mapName);
	object.insert(QStringLiteral("engineFamily"), document.engineFamily);
	object.insert(QStringLiteral("format"), levelMapFormatId(document.format));
	object.insert(QStringLiteral("formatLabel"), levelMapFormatDisplayName(document.format));
	object.insert(QStringLiteral("editState"), document.editState);
	object.insert(QStringLiteral("selectionKind"), levelMapSelectionKindId(document.selectionKind));
	object.insert(QStringLiteral("selectedObjectId"), document.selectedObjectId);
	object.insert(QStringLiteral("statistics"), levelMapStatisticsJson(levelMapStatistics(document)));
	object.insert(QStringLiteral("statisticsLines"), stringArrayJson(levelMapStatisticsLines(document)));
	object.insert(QStringLiteral("entityLines"), stringArrayJson(levelMapEntityLines(document)));
	object.insert(QStringLiteral("textureLines"), stringArrayJson(levelMapTextureLines(document)));
	object.insert(QStringLiteral("validationLines"), stringArrayJson(levelMapValidationLines(document)));
	object.insert(QStringLiteral("viewLines"), stringArrayJson(levelMapViewLines(document)));
	object.insert(QStringLiteral("selectionLines"), stringArrayJson(levelMapSelectionLines(document)));
	object.insert(QStringLiteral("propertyLines"), stringArrayJson(levelMapPropertyLines(document)));
	object.insert(QStringLiteral("undoLines"), stringArrayJson(levelMapUndoLines(document)));
	object.insert(QStringLiteral("textureReferences"), stringArrayJson(document.textureReferences));
	object.insert(QStringLiteral("issues"), levelMapIssuesJson(document.issues));
	object.insert(QStringLiteral("entities"), levelMapEntitiesJson(document.entities));
	object.insert(QStringLiteral("brushes"), levelMapBrushesJson(document.brushes));
	object.insert(QStringLiteral("doomVertices"), levelMapDoomVerticesJson(document.doomVertices));
	object.insert(QStringLiteral("doomLinedefs"), levelMapDoomLinedefsJson(document.doomLinedefs));
	object.insert(QStringLiteral("doomThings"), levelMapDoomThingsJson(document.doomThings));
	return object;
}

QJsonObject levelMapSaveReportJson(const LevelMapSaveReport& report)
{
	QJsonObject object;
	object.insert(QStringLiteral("sourcePath"), report.sourcePath);
	object.insert(QStringLiteral("outputPath"), report.outputPath);
	object.insert(QStringLiteral("mapName"), report.mapName);
	object.insert(QStringLiteral("format"), levelMapFormatId(report.format));
	object.insert(QStringLiteral("dryRun"), report.dryRun);
	object.insert(QStringLiteral("written"), report.written);
	object.insert(QStringLiteral("editState"), report.editState);
	object.insert(QStringLiteral("summaryLines"), stringArrayJson(report.summaryLines));
	object.insert(QStringLiteral("warnings"), stringArrayJson(report.warnings));
	object.insert(QStringLiteral("errors"), stringArrayJson(report.errors));
	object.insert(QStringLiteral("succeeded"), report.succeeded());
	return object;
}

QJsonObject advancedStudioIssueJson(const AdvancedStudioIssue& issue)
{
	QJsonObject object;
	object.insert(QStringLiteral("severity"), issue.severity);
	object.insert(QStringLiteral("code"), issue.code);
	object.insert(QStringLiteral("message"), issue.message);
	object.insert(QStringLiteral("objectId"), issue.objectId);
	object.insert(QStringLiteral("line"), issue.line);
	return object;
}

QJsonArray advancedStudioIssuesJson(const QVector<AdvancedStudioIssue>& issues)
{
	QJsonArray array;
	for (const AdvancedStudioIssue& issue : issues) {
		array.append(advancedStudioIssueJson(issue));
	}
	return array;
}

QJsonObject shaderStageJson(const ShaderStage& stage)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), stage.id);
	object.insert(QStringLiteral("shaderName"), stage.shaderName);
	object.insert(QStringLiteral("startLine"), stage.startLine);
	object.insert(QStringLiteral("endLine"), stage.endLine);
	object.insert(QStringLiteral("mapDirective"), stage.mapDirective);
	object.insert(QStringLiteral("blendFunc"), stage.blendFunc);
	object.insert(QStringLiteral("rgbGen"), stage.rgbGen);
	object.insert(QStringLiteral("alphaGen"), stage.alphaGen);
	object.insert(QStringLiteral("tcMod"), stage.tcMod);
	object.insert(QStringLiteral("directives"), stringArrayJson(stage.directives));
	object.insert(QStringLiteral("textureReferences"), stringArrayJson(stage.textureReferences));
	object.insert(QStringLiteral("rawText"), stage.rawText);
	object.insert(QStringLiteral("selected"), stage.selected);
	return object;
}

QJsonArray shaderStagesJson(const QVector<ShaderStage>& stages)
{
	QJsonArray array;
	for (const ShaderStage& stage : stages) {
		array.append(shaderStageJson(stage));
	}
	return array;
}

QJsonObject shaderDefinitionJson(const ShaderDefinition& shader)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), shader.id);
	object.insert(QStringLiteral("name"), shader.name);
	object.insert(QStringLiteral("startLine"), shader.startLine);
	object.insert(QStringLiteral("endLine"), shader.endLine);
	object.insert(QStringLiteral("directives"), stringArrayJson(shader.directives));
	object.insert(QStringLiteral("textureReferences"), stringArrayJson(shader.textureReferences));
	object.insert(QStringLiteral("rawText"), shader.rawText);
	object.insert(QStringLiteral("selected"), shader.selected);
	object.insert(QStringLiteral("stages"), shaderStagesJson(shader.stages));
	return object;
}

QJsonArray shaderDefinitionsJson(const QVector<ShaderDefinition>& shaders)
{
	QJsonArray array;
	for (const ShaderDefinition& shader : shaders) {
		array.append(shaderDefinitionJson(shader));
	}
	return array;
}

QJsonObject shaderReferenceValidationJson(const ShaderReferenceValidation& validation)
{
	QJsonObject object;
	object.insert(QStringLiteral("textureReference"), validation.textureReference);
	object.insert(QStringLiteral("candidatePaths"), stringArrayJson(validation.candidatePaths));
	object.insert(QStringLiteral("found"), validation.found);
	object.insert(QStringLiteral("foundInPackage"), validation.foundInPackage);
	return object;
}

QJsonArray shaderReferenceValidationsJson(const QVector<ShaderReferenceValidation>& validation)
{
	QJsonArray array;
	for (const ShaderReferenceValidation& item : validation) {
		array.append(shaderReferenceValidationJson(item));
	}
	return array;
}

QJsonObject advancedShaderDocumentJson(const ShaderDocument& document, const QVector<ShaderReferenceValidation>& validation = {}, const QStringList& validationWarnings = {})
{
	QJsonObject object;
	object.insert(QStringLiteral("sourcePath"), document.sourcePath);
	object.insert(QStringLiteral("editState"), document.editState);
	object.insert(QStringLiteral("shaderCount"), document.shaders.size());
	object.insert(QStringLiteral("shaders"), shaderDefinitionsJson(document.shaders));
	object.insert(QStringLiteral("issues"), advancedStudioIssuesJson(document.issues));
	object.insert(QStringLiteral("graphLines"), stringArrayJson(shaderGraphLines(document)));
	object.insert(QStringLiteral("previewLines"), stringArrayJson(shaderStagePreviewLines(document)));
	object.insert(QStringLiteral("referenceValidation"), shaderReferenceValidationsJson(validation));
	object.insert(QStringLiteral("referenceValidationLines"), stringArrayJson(shaderReferenceValidationLines(validation, validationWarnings)));
	object.insert(QStringLiteral("referenceValidationWarnings"), stringArrayJson(validationWarnings));
	return object;
}

QJsonObject shaderSaveReportJson(const ShaderSaveReport& report)
{
	QJsonObject object;
	object.insert(QStringLiteral("sourcePath"), report.sourcePath);
	object.insert(QStringLiteral("outputPath"), report.outputPath);
	object.insert(QStringLiteral("dryRun"), report.dryRun);
	object.insert(QStringLiteral("written"), report.written);
	object.insert(QStringLiteral("editState"), report.editState);
	object.insert(QStringLiteral("summaryLines"), stringArrayJson(report.summaryLines));
	object.insert(QStringLiteral("warnings"), stringArrayJson(report.warnings));
	object.insert(QStringLiteral("errors"), stringArrayJson(report.errors));
	object.insert(QStringLiteral("succeeded"), report.succeeded());
	return object;
}

QJsonObject spriteFramePlanJson(const SpriteFramePlan& frame)
{
	QJsonObject object;
	object.insert(QStringLiteral("index"), frame.index);
	object.insert(QStringLiteral("frameId"), frame.frameId);
	object.insert(QStringLiteral("rotationId"), frame.rotationId);
	object.insert(QStringLiteral("lumpName"), frame.lumpName);
	object.insert(QStringLiteral("sourcePath"), frame.sourcePath);
	object.insert(QStringLiteral("virtualPath"), frame.virtualPath);
	object.insert(QStringLiteral("paletteAction"), frame.paletteAction);
	return object;
}

QJsonObject spriteWorkflowPlanJson(const SpriteWorkflowPlan& plan)
{
	QJsonObject object;
	object.insert(QStringLiteral("engineFamily"), plan.engineFamily);
	object.insert(QStringLiteral("spriteName"), plan.spriteName);
	object.insert(QStringLiteral("paletteId"), plan.paletteId);
	object.insert(QStringLiteral("outputPackageRoot"), plan.outputPackageRoot);
	object.insert(QStringLiteral("state"), operationStateJson(plan.state));
	object.insert(QStringLiteral("palettePreviewLines"), stringArrayJson(plan.palettePreviewLines));
	object.insert(QStringLiteral("sequenceLines"), stringArrayJson(plan.sequenceLines));
	object.insert(QStringLiteral("stagingLines"), stringArrayJson(plan.stagingLines));
	object.insert(QStringLiteral("warnings"), stringArrayJson(plan.warnings));
	QJsonArray frames;
	for (const SpriteFramePlan& frame : plan.frames) {
		frames.append(spriteFramePlanJson(frame));
	}
	object.insert(QStringLiteral("frames"), frames);
	return object;
}

QJsonObject codeSourceFileJson(const CodeSourceFile& file)
{
	QJsonObject object;
	object.insert(QStringLiteral("filePath"), file.filePath);
	object.insert(QStringLiteral("relativePath"), file.relativePath);
	object.insert(QStringLiteral("languageId"), file.languageId);
	object.insert(QStringLiteral("bytes"), QString::number(file.bytes));
	object.insert(QStringLiteral("lineCount"), file.lineCount);
	return object;
}

QJsonObject codeSymbolJson(const CodeSymbol& symbol)
{
	QJsonObject object;
	object.insert(QStringLiteral("name"), symbol.name);
	object.insert(QStringLiteral("kind"), symbol.kind);
	object.insert(QStringLiteral("filePath"), symbol.filePath);
	object.insert(QStringLiteral("relativePath"), symbol.relativePath);
	object.insert(QStringLiteral("line"), symbol.line);
	object.insert(QStringLiteral("column"), symbol.column);
	return object;
}

QJsonObject languageServiceHookJson(const LanguageServiceHook& hook)
{
	QJsonObject object;
	object.insert(QStringLiteral("languageId"), hook.languageId);
	object.insert(QStringLiteral("displayName"), hook.displayName);
	object.insert(QStringLiteral("extensions"), stringArrayJson(hook.extensions));
	object.insert(QStringLiteral("capabilities"), stringArrayJson(hook.capabilities));
	object.insert(QStringLiteral("serverCommand"), hook.serverCommand);
	object.insert(QStringLiteral("available"), hook.available);
	object.insert(QStringLiteral("status"), hook.status);
	return object;
}

QJsonObject codeDiagnosticJson(const CodeDiagnostic& diagnostic)
{
	QJsonObject object;
	object.insert(QStringLiteral("severity"), diagnostic.severity);
	object.insert(QStringLiteral("message"), diagnostic.message);
	object.insert(QStringLiteral("filePath"), diagnostic.filePath);
	object.insert(QStringLiteral("relativePath"), diagnostic.relativePath);
	object.insert(QStringLiteral("line"), diagnostic.line);
	object.insert(QStringLiteral("column"), diagnostic.column);
	return object;
}

QJsonObject codeWorkspaceIndexJson(const CodeWorkspaceIndex& index)
{
	QJsonObject object;
	object.insert(QStringLiteral("rootPath"), index.rootPath);
	object.insert(QStringLiteral("state"), operationStateJson(index.state));
	object.insert(QStringLiteral("treeLines"), stringArrayJson(index.treeLines));
	object.insert(QStringLiteral("buildTaskLines"), stringArrayJson(index.buildTaskLines));
	object.insert(QStringLiteral("launchProfileLines"), stringArrayJson(index.launchProfileLines));
	object.insert(QStringLiteral("warnings"), stringArrayJson(index.warnings));
	QJsonArray files;
	for (const CodeSourceFile& file : index.files) {
		files.append(codeSourceFileJson(file));
	}
	object.insert(QStringLiteral("files"), files);
	QJsonArray symbols;
	for (const CodeSymbol& symbol : index.symbols) {
		symbols.append(codeSymbolJson(symbol));
	}
	object.insert(QStringLiteral("symbols"), symbols);
	QJsonArray hooks;
	for (const LanguageServiceHook& hook : index.languageHooks) {
		hooks.append(languageServiceHookJson(hook));
	}
	object.insert(QStringLiteral("languageHooks"), hooks);
	QJsonArray diagnostics;
	for (const CodeDiagnostic& diagnostic : index.diagnostics) {
		diagnostics.append(codeDiagnosticJson(diagnostic));
	}
	object.insert(QStringLiteral("diagnostics"), diagnostics);
	return object;
}

QJsonObject extensionGeneratedFileJson(const ExtensionGeneratedFile& file)
{
	QJsonObject object;
	object.insert(QStringLiteral("virtualPath"), file.virtualPath);
	object.insert(QStringLiteral("sourceDescription"), file.sourceDescription);
	object.insert(QStringLiteral("summary"), file.summary);
	object.insert(QStringLiteral("staged"), file.staged);
	return object;
}

QJsonArray extensionGeneratedFilesJson(const QVector<ExtensionGeneratedFile>& files)
{
	QJsonArray array;
	for (const ExtensionGeneratedFile& file : files) {
		array.append(extensionGeneratedFileJson(file));
	}
	return array;
}

QJsonObject extensionCommandDescriptorJson(const ExtensionCommandDescriptor& command)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), command.id);
	object.insert(QStringLiteral("displayName"), command.displayName);
	object.insert(QStringLiteral("description"), command.description);
	object.insert(QStringLiteral("program"), command.program);
	object.insert(QStringLiteral("arguments"), stringArrayJson(command.arguments));
	object.insert(QStringLiteral("workingDirectory"), command.workingDirectory);
	object.insert(QStringLiteral("capabilities"), stringArrayJson(command.capabilities));
	object.insert(QStringLiteral("generatedFiles"), extensionGeneratedFilesJson(command.generatedFiles));
	object.insert(QStringLiteral("requiresApproval"), command.requiresApproval);
	return object;
}

QJsonObject extensionManifestJson(const ExtensionManifest& manifest)
{
	QJsonObject object;
	object.insert(QStringLiteral("schemaVersion"), manifest.schemaVersion);
	object.insert(QStringLiteral("manifestPath"), manifest.manifestPath);
	object.insert(QStringLiteral("rootPath"), manifest.rootPath);
	object.insert(QStringLiteral("id"), manifest.id);
	object.insert(QStringLiteral("displayName"), manifest.displayName);
	object.insert(QStringLiteral("version"), manifest.version);
	object.insert(QStringLiteral("description"), manifest.description);
	object.insert(QStringLiteral("trustLevel"), manifest.trustLevel);
	object.insert(QStringLiteral("sandboxModel"), manifest.sandboxModel);
	object.insert(QStringLiteral("capabilities"), stringArrayJson(manifest.capabilities));
	object.insert(QStringLiteral("warnings"), stringArrayJson(manifest.warnings));
	QJsonArray commands;
	for (const ExtensionCommandDescriptor& command : manifest.commands) {
		commands.append(extensionCommandDescriptorJson(command));
	}
	object.insert(QStringLiteral("commands"), commands);
	return object;
}

QJsonObject extensionDiscoveryJson(const ExtensionDiscoveryResult& result)
{
	QJsonObject object;
	object.insert(QStringLiteral("searchRoots"), stringArrayJson(result.searchRoots));
	object.insert(QStringLiteral("state"), operationStateJson(result.state));
	object.insert(QStringLiteral("warnings"), stringArrayJson(result.warnings));
	object.insert(QStringLiteral("trustModel"), stringArrayJson(extensionTrustModelLines()));
	QJsonArray manifests;
	for (const ExtensionManifest& manifest : result.manifests) {
		manifests.append(extensionManifestJson(manifest));
	}
	object.insert(QStringLiteral("manifests"), manifests);
	return object;
}

QJsonObject extensionCommandPlanJson(const ExtensionCommandPlan& plan)
{
	QJsonObject object;
	object.insert(QStringLiteral("manifest"), extensionManifestJson(plan.manifest));
	object.insert(QStringLiteral("command"), extensionCommandDescriptorJson(plan.command));
	object.insert(QStringLiteral("program"), plan.program);
	object.insert(QStringLiteral("arguments"), stringArrayJson(plan.arguments));
	object.insert(QStringLiteral("workingDirectory"), plan.workingDirectory);
	object.insert(QStringLiteral("dryRun"), plan.dryRun);
	object.insert(QStringLiteral("executionAllowed"), plan.executionAllowed);
	object.insert(QStringLiteral("state"), operationStateJson(plan.state));
	object.insert(QStringLiteral("warnings"), stringArrayJson(plan.warnings));
	object.insert(QStringLiteral("stagingLines"), stringArrayJson(plan.stagingLines));
	return object;
}

QJsonObject extensionCommandResultJson(const ExtensionCommandResult& result)
{
	QJsonObject object;
	object.insert(QStringLiteral("plan"), extensionCommandPlanJson(result.plan));
	object.insert(QStringLiteral("started"), result.started);
	object.insert(QStringLiteral("dryRun"), result.dryRun);
	object.insert(QStringLiteral("exitCode"), result.exitCode);
	object.insert(QStringLiteral("stdout"), result.stdoutText);
	object.insert(QStringLiteral("stderr"), result.stderrText);
	object.insert(QStringLiteral("error"), result.error);
	object.insert(QStringLiteral("state"), operationStateJson(result.state));
	if (result.finishedUtc.isValid()) {
		object.insert(QStringLiteral("finishedUtc"), result.finishedUtc.toUTC().toString(Qt::ISODate));
	}
	return object;
}

QJsonArray compilerToolPathOverridesJson(const QVector<CompilerToolPathOverride>& overrides)
{
	QJsonArray array;
	for (const CompilerToolPathOverride& override : overrides) {
		QJsonObject object;
		object.insert(QStringLiteral("toolId"), override.toolId);
		object.insert(QStringLiteral("executablePath"), override.executablePath);
		array.append(object);
	}
	return array;
}

CompilerRegistryOptions compilerRegistryOptionsFromArgs(const QStringList& args)
{
	StudioSettings settings;
	CompilerRegistryOptions options;
	options.workspaceRootPath = optionValue(args, QStringLiteral("--workspace-root"));
	options.extraSearchPaths = optionPathList(args, QStringLiteral("--compiler-search-paths"));
	options.executableOverrides = settings.compilerToolPathOverrides();
	if (!options.workspaceRootPath.trimmed().isEmpty()) {
		ProjectManifest manifest;
		if (loadProjectManifest(options.workspaceRootPath, &manifest)) {
			options.extraSearchPaths = effectiveProjectCompilerSearchPaths(manifest, options.extraSearchPaths);
			options.executableOverrides = effectiveProjectCompilerToolOverrides(manifest, options.executableOverrides);
		}
	}
	return options;
}

void applyCompilerRequestContext(CompilerCommandRequest* request, const QStringList& args)
{
	if (!request) {
		return;
	}
	CompilerRegistryOptions options = compilerRegistryOptionsFromArgs(args);
	if (!request->workspaceRootPath.trimmed().isEmpty()) {
		options.workspaceRootPath = request->workspaceRootPath;
		ProjectManifest manifest;
		if (loadProjectManifest(options.workspaceRootPath, &manifest)) {
			options.extraSearchPaths = effectiveProjectCompilerSearchPaths(manifest, options.extraSearchPaths);
			options.executableOverrides = effectiveProjectCompilerToolOverrides(manifest, options.executableOverrides);
		}
	}
	request->extraSearchPaths = options.extraSearchPaths;
	request->executableOverrides = options.executableOverrides;
}

bool loadPackageForCliQuiet(const QString& path, PackageArchive* archive, QString* error, qint64* durationMs = nullptr)
{
	QString localError;
	QElapsedTimer timer;
	timer.start();
	if (!archive || !archive->load(path, &localError)) {
		if (durationMs) {
			*durationMs = timer.elapsed();
		}
		if (error) {
			*error = localError.isEmpty() ? QStringLiteral("unknown error") : localError;
		}
		return false;
	}
	if (durationMs) {
		*durationMs = timer.elapsed();
	}
	return true;
}

void printPackageWarnings(const PackageArchive& archive)
{
	const QVector<PackageLoadWarning> warnings = archive.warnings();
	if (warnings.isEmpty()) {
		return;
	}
	std::cout << "Warnings\n";
	for (const PackageLoadWarning& warning : warnings) {
		std::cout << "- " << text(warning.virtualPath.isEmpty() ? QStringLiteral("(package)") : warning.virtualPath) << ": " << text(warning.message) << "\n";
	}
}

void printPackageInfo(const PackageArchive& archive)
{
	const PackageArchiveSummary summary = archive.summary();
	std::cout << "Package info\n";
	std::cout << "Path: " << text(nativePath(summary.sourcePath)) << "\n";
	std::cout << "Format: " << text(packageArchiveFormatId(summary.format)) << " (" << text(packageArchiveFormatDisplayName(summary.format)) << ")\n";
	std::cout << "Entries: " << summary.entryCount << "\n";
	std::cout << "Files: " << summary.fileCount << "\n";
	std::cout << "Directories: " << summary.directoryCount << "\n";
	std::cout << "Nested archive candidates: " << summary.nestedArchiveCount << "\n";
	std::cout << "Total file bytes: " << text(sizeText(summary.totalSizeBytes)) << "\n";
	std::cout << "Warnings: " << summary.warningCount << "\n";
	printPackageWarnings(archive);
}

void printPackageList(const PackageArchive& archive)
{
	printPackageInfo(archive);
	std::cout << "Entries\n";
	for (const PackageEntry& entry : archive.entries()) {
		std::cout << "- " << text(entry.virtualPath) << "\n";
		std::cout << "  Kind: " << text(packageEntryKindId(entry.kind)) << "\n";
		std::cout << "  Size: " << text(sizeText(entry.sizeBytes)) << "\n";
		std::cout << "  Type: " << text(entry.typeHint) << "\n";
		std::cout << "  Storage: " << text(entry.storageMethod.isEmpty() ? QStringLiteral("unknown") : entry.storageMethod) << "\n";
		std::cout << "  Readable: " << (entry.readable ? "yes" : "no") << "\n";
		if (entry.nestedArchiveCandidate) {
			std::cout << "  Nested archive candidate: yes\n";
		}
		if (!entry.note.isEmpty()) {
			std::cout << "  Note: " << text(entry.note) << "\n";
		}
	}
}

bool printPackagePreview(const PackageArchive& archive, const QString& virtualPath, const PackagePreview* prebuiltPreview = nullptr)
{
	const PackagePreview builtPreview = prebuiltPreview ? PackagePreview() : buildPackageEntryPreview(archive, virtualPath);
	const PackagePreview& preview = prebuiltPreview ? *prebuiltPreview : builtPreview;
	std::cout << "Package preview\n";
	std::cout << "Entry: " << text(preview.virtualPath.isEmpty() ? virtualPath : preview.virtualPath) << "\n";
	std::cout << "Kind: " << text(packagePreviewKindId(preview.kind)) << " (" << text(packagePreviewKindDisplayName(preview.kind)) << ")\n";
	if (!preview.summary.isEmpty()) {
		std::cout << "Summary: " << text(preview.summary) << "\n";
	}
	if (preview.totalBytes > 0 || preview.bytesRead > 0) {
		std::cout << "Bytes sampled: " << text(sizeText(static_cast<quint64>(std::max<qint64>(0, preview.bytesRead)))) << " of " << text(sizeText(static_cast<quint64>(std::max<qint64>(0, preview.totalBytes)))) << "\n";
	}
	std::cout << "Truncated: " << (preview.truncated ? "yes" : "no") << "\n";
	if (!preview.error.isEmpty()) {
		std::cout << "Error: " << text(preview.error) << "\n";
	}
	if (!preview.imageFormat.isEmpty() || preview.imageSize.isValid()) {
		std::cout << "Image format: " << text(preview.imageFormat.isEmpty() ? QStringLiteral("unknown") : preview.imageFormat) << "\n";
		std::cout << "Image size: " << text(preview.imageSize.isValid() ? QStringLiteral("%1 x %2").arg(preview.imageSize.width()).arg(preview.imageSize.height()) : QStringLiteral("unknown")) << "\n";
		std::cout << "Image depth: " << preview.imageDepth << "\n";
		std::cout << "Palette-aware: " << (preview.imagePaletteAware ? "yes" : "no") << "\n";
	}
	if (!preview.modelFormat.isEmpty() || !preview.modelViewportLines.isEmpty()) {
		std::cout << "Model format: " << text(preview.modelFormat.isEmpty() ? QStringLiteral("unknown") : preview.modelFormat) << "\n";
		if (!preview.modelViewportLines.isEmpty()) {
			std::cout << "Model viewport\n";
			for (const QString& line : preview.modelViewportLines) {
				std::cout << "- " << text(line) << "\n";
			}
		}
	}
	if (!preview.audioFormat.isEmpty() || !preview.audioWaveformLines.isEmpty()) {
		std::cout << "Audio format: " << text(preview.audioFormat.isEmpty() ? QStringLiteral("unknown") : preview.audioFormat) << "\n";
		if (!preview.audioWaveformLines.isEmpty()) {
			std::cout << "Waveform\n";
			for (const QString& line : preview.audioWaveformLines) {
				std::cout << "- " << text(line) << "\n";
			}
		}
	}
	if (!preview.textLanguageName.isEmpty()) {
		std::cout << "Text language: " << text(preview.textLanguageName) << "\n";
		std::cout << "Text save state: " << text(preview.textSaveState.isEmpty() ? QStringLiteral("clean") : preview.textSaveState) << "\n";
	}
	if (!preview.detailLines.isEmpty()) {
		std::cout << "Details\n";
		for (const QString& line : preview.detailLines) {
			std::cout << "- " << text(line) << "\n";
		}
	}
	if (!preview.body.isEmpty()) {
		std::cout << "Body\n";
		std::cout << text(preview.body) << "\n";
	}
	return preview.kind != PackagePreviewKind::Unavailable;
}

void printPackageValidation(const PackageArchive& archive)
{
	const PackageArchiveSummary summary = archive.summary();
	std::cout << "Package validation\n";
	std::cout << "Path: " << text(nativePath(summary.sourcePath)) << "\n";
	std::cout << "Format: " << text(packageArchiveFormatId(summary.format)) << " (" << text(packageArchiveFormatDisplayName(summary.format)) << ")\n";
	std::cout << "Entries: " << summary.entryCount << "\n";
	std::cout << "Warnings: " << summary.warningCount << "\n";
	std::cout << "Validation: " << (summary.warningCount == 0 ? "usable" : "warnings") << "\n";
	printPackageWarnings(archive);
}

void printPackageExtractionReport(const PackageExtractionReport& report)
{
	std::cout << text(packageExtractionReportText(report)) << "\n";
}

QStringList packageExtractionEntriesFromArgs(const QStringList& args)
{
	QStringList entries = optionValues(args, QStringLiteral("--extract-entry"));
	entries += optionValues(args, QStringLiteral("--entry"));
	const QString packedEntries = optionValue(args, QStringLiteral("--entries"));
	if (!packedEntries.trimmed().isEmpty()) {
		entries += packedEntries.split(';', Qt::SkipEmptyParts);
	}

	QStringList normalized;
	for (const QString& entry : entries) {
		const QString trimmed = entry.trimmed();
		if (!trimmed.isEmpty() && !normalized.contains(trimmed)) {
			normalized.push_back(trimmed);
		}
	}
	return normalized;
}

PackageArchiveFormat packageWriteFormatFromArgs(const QStringList& args, const QString& outputPath)
{
	const QString formatId = optionValue(args, QStringLiteral("--format"));
	if (!formatId.trimmed().isEmpty()) {
		return packageArchiveFormatFromId(formatId);
	}
	return packageArchiveFormatFromFileName(outputPath);
}

QJsonObject packageStagingJson(const PackageStagingModel& staging)
{
	return QJsonDocument::fromJson(staging.manifestJson()).object();
}

QJsonObject packageWriteReportJson(const PackageWriteReport& report)
{
	QJsonObject object;
	object.insert(QStringLiteral("sourcePath"), report.sourcePath);
	object.insert(QStringLiteral("outputPath"), report.outputPath);
	object.insert(QStringLiteral("manifestPath"), report.manifestPath);
	object.insert(QStringLiteral("format"), packageArchiveFormatId(report.format));
	object.insert(QStringLiteral("entryCount"), report.entryCount);
	object.insert(QStringLiteral("bytesWritten"), static_cast<double>(report.bytesWritten));
	object.insert(QStringLiteral("sha256"), report.sha256);
	object.insert(QStringLiteral("deterministic"), report.deterministic);
	object.insert(QStringLiteral("dryRun"), report.dryRun);
	object.insert(QStringLiteral("wroteManifest"), report.wroteManifest);
	object.insert(QStringLiteral("warnings"), stringArrayJson(report.warnings));
	object.insert(QStringLiteral("blockedMessages"), stringArrayJson(report.blockedMessages));
	object.insert(QStringLiteral("succeeded"), report.succeeded());
	return object;
}

void printPackageStagingSummary(const PackageStagingModel& staging)
{
	const PackageStagingSummary summary = staging.summary();
	std::cout << "Package staging\n";
	std::cout << "Source: " << text(nativePath(summary.sourcePath)) << "\n";
	std::cout << "Format: " << text(packageArchiveFormatId(summary.sourceFormat)) << "\n";
	std::cout << "Base files: " << summary.baseFileCount << "\n";
	std::cout << "Staged files: " << summary.stagedFileCount << "\n";
	std::cout << "Operations: " << summary.operationCount << " (add " << summary.addedCount << ", replace " << summary.replacedCount << ", rename " << summary.renamedCount << ", delete " << summary.deletedCount << ")\n";
	std::cout << "Bytes before/after: " << summary.beforeBytes << " -> " << summary.afterBytes << "\n";
	std::cout << "Save state: " << (summary.canSave ? "ready" : "blocked") << "\n";
	for (const PackageStageConflict& conflict : staging.conflicts()) {
		std::cout << "- " << (conflict.blocking ? "BLOCKED" : "notice") << " " << text(conflict.virtualPath) << ": " << text(conflict.message) << "\n";
	}
}

bool loadPackageStaging(const QString& packagePath, PackageStagingModel* staging, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!staging) {
		if (error) {
			*error = QStringLiteral("Missing staging model.");
		}
		return false;
	}
	PackageArchive archive;
	if (!loadPackageForCliQuiet(packagePath, &archive, error)) {
		return false;
	}
	return staging->loadBaseArchive(archive, error);
}

bool applyPackageStageArgs(PackageStagingModel* staging, const QStringList& args, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!staging) {
		if (error) {
			*error = QStringLiteral("Missing staging model.");
		}
		return false;
	}

	const PackageStageConflictResolution resolution = packageStageConflictResolutionFromId(optionValue(args, QStringLiteral("--resolve")));

	const QStringList addFiles = optionValues(args, QStringLiteral("--add-file")) + optionValues(args, QStringLiteral("--import-file"));
	const QStringList addTargets = optionValues(args, QStringLiteral("--as"));
	for (int index = 0; index < addFiles.size(); ++index) {
		const QString target = addTargets.value(index, QFileInfo(addFiles[index]).fileName());
		if (!staging->addFile(addFiles[index], target, error, resolution)) {
			return false;
		}
	}

	const QStringList replaceFiles = optionValues(args, QStringLiteral("--replace-file"));
	QStringList replaceTargets = optionValues(args, QStringLiteral("--replace-entry"));
	if (replaceTargets.isEmpty() && !replaceFiles.isEmpty()) {
		replaceTargets = optionValues(args, QStringLiteral("--entry"));
	}
	if (replaceFiles.size() != replaceTargets.size()) {
		if (error) {
			*error = QStringLiteral("Each --replace-file requires a matching --replace-entry.");
		}
		return false;
	}
	for (int index = 0; index < replaceFiles.size(); ++index) {
		if (!staging->replaceFile(replaceTargets[index], replaceFiles[index], error)) {
			return false;
		}
	}

	const QStringList renameSources = optionValues(args, QStringLiteral("--rename"));
	const QStringList renameTargets = optionValues(args, QStringLiteral("--to"));
	if (renameSources.size() != renameTargets.size()) {
		if (error) {
			*error = QStringLiteral("Each --rename requires a matching --to target.");
		}
		return false;
	}
	for (int index = 0; index < renameSources.size(); ++index) {
		if (!staging->renameEntry(renameSources[index], renameTargets[index], error, resolution)) {
			return false;
		}
	}

	const QStringList deleteTargets = optionValues(args, QStringLiteral("--delete")) + optionValues(args, QStringLiteral("--remove-entry"));
	for (const QString& target : deleteTargets) {
		if (!staging->deleteEntry(target, error, resolution)) {
			return false;
		}
	}

	return true;
}

void printInstallationValidation(const GameInstallationProfile& profile)
{
	const GameInstallationValidation validation = validateGameInstallationProfile(profile);
	std::cout << "Validation: " << (validation.isUsable() ? "usable" : "blocked") << "\n";
	if (!validation.errors.isEmpty()) {
		std::cout << "Errors\n";
		for (const QString& error : validation.errors) {
			std::cout << "- " << text(error) << "\n";
		}
	}
	if (!validation.warnings.isEmpty()) {
		std::cout << "Warnings\n";
		for (const QString& warning : validation.warnings) {
			std::cout << "- " << text(warning) << "\n";
		}
	}
}

void printGameInstallationProfile(const GameInstallationProfile& profile, const QString& selectedId)
{
	std::cout << "- " << text(profile.id) << "\n";
	std::cout << "  Name: " << text(profile.displayName) << "\n";
	std::cout << "  Game: " << text(profile.gameKey) << "\n";
	std::cout << "  Engine: " << text(gameEngineFamilyId(profile.engineFamily)) << " (" << text(gameEngineFamilyDisplayName(profile.engineFamily)) << ")\n";
	std::cout << "  Root: " << text(nativePath(profile.rootPath)) << "\n";
	std::cout << "  Executable: " << text(profile.executablePath.isEmpty() ? QStringLiteral("(not set)") : nativePath(profile.executablePath)) << "\n";
	std::cout << "  Base package paths: " << text(profile.basePackagePaths.isEmpty() ? QStringLiteral("(none)") : profile.basePackagePaths.join(QStringLiteral("; "))) << "\n";
	std::cout << "  Mod package paths: " << text(profile.modPackagePaths.isEmpty() ? QStringLiteral("(none)") : profile.modPackagePaths.join(QStringLiteral("; "))) << "\n";
	std::cout << "  Palette: " << text(profile.paletteId.isEmpty() ? QStringLiteral("(generic)") : profile.paletteId) << "\n";
	std::cout << "  Compiler profile: " << text(profile.compilerProfileId.isEmpty() ? QStringLiteral("(generic)") : profile.compilerProfileId) << "\n";
	std::cout << "  Read-only: " << (profile.readOnly ? "yes" : "no") << "\n";
	std::cout << "  Hidden: " << (profile.hidden ? "yes" : "no") << "\n";
	std::cout << "  Manual: " << (profile.manual ? "yes" : "no") << "\n";
	std::cout << "  Selected: " << (sameGameInstallationId(profile.id, selectedId) ? "yes" : "no") << "\n";
}

void printGameInstallations(const StudioSettings& settings)
{
	const QVector<GameInstallationProfile> profiles = settings.gameInstallations();
	if (profiles.isEmpty()) {
		std::cout << "Game installations: none\n";
		return;
	}

	std::cout << "Game installations\n";
	std::cout << "Selected: " << text(settings.selectedGameInstallationId().isEmpty() ? QStringLiteral("(none)") : settings.selectedGameInstallationId()) << "\n";
	for (const GameInstallationProfile& profile : profiles) {
		printGameInstallationProfile(profile, settings.selectedGameInstallationId());
		printInstallationValidation(profile);
	}
}

void printInstallationDetectionCandidates(const QVector<GameInstallationDetectionCandidate>& candidates)
{
	if (candidates.isEmpty()) {
		std::cout << "Detected game installations: none\n";
		std::cout << "No profiles were saved. Add an installation manually or pass additional --root paths.\n";
		return;
	}

	std::cout << "Detected game installations\n";
	std::cout << "Candidates: " << candidates.size() << "\n";
	for (const GameInstallationDetectionCandidate& candidate : candidates) {
		std::cout << "- " << text(candidate.profile.displayName) << "\n";
		std::cout << "  Source: " << text(candidate.sourceName) << " [" << text(candidate.sourceId) << "]\n";
		std::cout << "  Confidence: " << candidate.confidencePercent << "%\n";
		printGameInstallationProfile(candidate.profile, QString());
		if (!candidate.matchedPaths.isEmpty()) {
			std::cout << "  Matched paths: " << text(candidate.matchedPaths.join(QStringLiteral("; "))) << "\n";
		}
		if (!candidate.warnings.isEmpty()) {
			std::cout << "  Candidate warnings\n";
			for (const QString& warning : candidate.warnings) {
				std::cout << "  - " << text(warning) << "\n";
			}
		}
	}
	std::cout << "No profiles were saved. Re-run --add-installation with the chosen root to confirm.\n";
}

const GameInstallationProfile* findInstallationById(const QVector<GameInstallationProfile>& profiles, const QString& id)
{
	for (const GameInstallationProfile& profile : profiles) {
		if (sameGameInstallationId(profile.id, id)) {
			return &profile;
		}
	}
	return nullptr;
}

void printRecentProjects(const StudioSettings& settings)
{
	const QVector<RecentProject> projects = settings.recentProjects();
	if (projects.isEmpty()) {
		std::cout << "Recent projects: none\n";
		return;
	}

	std::cout << "Recent projects\n";
	for (const RecentProject& project : projects) {
		std::cout << "- " << text(project.displayName) << "\n";
		std::cout << "  Path: " << text(nativePath(project.path)) << "\n";
		std::cout << "  State: " << (project.exists ? "ready" : "missing") << "\n";
		std::cout << "  Last opened UTC: " << text(project.lastOpenedUtc.toUTC().toString(Qt::ISODate)) << "\n";
	}
}

void printPreferences(const AccessibilityPreferences& preferences, const QString& editorProfileId = defaultEditorProfileId())
{
	std::cout << "Accessibility and language preferences\n";
	std::cout << "Locale: " << text(preferences.localeName) << "\n";
	std::cout << "Theme: " << text(themeId(preferences.theme)) << " (" << text(themeDisplayName(preferences.theme)) << ")\n";
	std::cout << "Text scale: " << preferences.textScalePercent << "%\n";
	std::cout << "Density: " << text(densityId(preferences.density)) << " (" << text(densityDisplayName(preferences.density)) << ")\n";
	std::cout << "Editor profile: " << text(editorProfileDisplayNameForId(editorProfileId)) << " [" << text(editorProfileForId(editorProfileId) ? normalizedEditorProfileId(editorProfileId) : defaultEditorProfileId()) << "]\n";
	std::cout << "Reduced motion: " << (preferences.reducedMotion ? "enabled" : "disabled") << "\n";
	std::cout << "Text to speech: " << (preferences.textToSpeechEnabled ? "enabled" : "disabled") << "\n";
}

void printSetupSummary(const SetupSummary& summary)
{
	std::cout << "First-run setup\n";
	std::cout << "Status: " << text(summary.status) << "\n";
	std::cout << "Current step: " << text(summary.currentStepName) << " [" << text(summary.currentStepId) << "]\n";
	std::cout << "Description: " << text(summary.currentStepDescription) << "\n";
	std::cout << "Next action: " << text(summary.nextAction) << "\n";

	if (!summary.completedItems.isEmpty()) {
		std::cout << "Completed items\n";
		for (const QString& item : summary.completedItems) {
			std::cout << "- " << text(item) << "\n";
		}
	}
	if (!summary.pendingItems.isEmpty()) {
		std::cout << "Pending items\n";
		for (const QString& item : summary.pendingItems) {
			std::cout << "- " << text(item) << "\n";
		}
	}
	if (!summary.warnings.isEmpty()) {
		std::cout << "Warnings\n";
		for (const QString& item : summary.warnings) {
			std::cout << "- " << text(item) << "\n";
		}
	}
}

void printSettingsReport()
{
	const StudioSettings settings;
	std::cout << "VibeStudio settings\n";
	std::cout << "Storage: " << text(nativePath(settings.storageLocation())) << "\n";
	std::cout << "Status: " << text(settingsStatusText(settings.status())) << "\n";
	std::cout << "Schema: " << settings.schemaVersion() << "\n";
	std::cout << "Selected mode index: " << settings.selectedMode() << "\n";
	printRecentProjects(settings);
	printGameInstallations(settings);
	printPreferences(settings.accessibilityPreferences(), settings.selectedEditorProfileId());
	std::cout << "AI automation\n";
	std::cout << text(aiAutomationPreferencesText(settings.aiAutomationPreferences())) << "\n";
	printSetupSummary(settings.setupSummary());
}

void printExitCodes(CliOutputFormat format, const QString& commandName)
{
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("exitCodes"), exitCodesJson());
		printJson(object);
		return;
	}

	std::cout << "VibeStudio CLI exit codes\n";
	for (const CliExitCodeDescriptor& descriptor : cliExitCodeDescriptors()) {
		std::cout << "- " << exitCodeValue(descriptor.code) << " " << text(descriptor.id) << "\n";
		std::cout << "  Label: " << text(descriptor.label) << "\n";
		std::cout << "  " << text(descriptor.description) << "\n";
	}
}

int runCliCommandsCommand(const QString& commandName, CliOutputFormat format)
{
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("commands"), cliCommandsJson());
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	std::cout << "VibeStudio CLI commands\n";
	for (const CliCommandDescriptor& descriptor : cliCommandDescriptors()) {
		std::cout << "- " << text(descriptor.family) << " " << text(descriptor.command) << "\n";
		std::cout << "  " << text(descriptor.summary) << "\n";
		if (!descriptor.examples.isEmpty()) {
			std::cout << "  Example: " << text(descriptor.examples.first()) << "\n";
		}
	}
	return exitCodeValue(CliExitCode::Success);
}

int runUiSemanticsCommand(const QString& commandName, CliOutputFormat format)
{
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("uiSemantics"), uiSemanticsJson());
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	printUiSemantics();
	return exitCodeValue(CliExitCode::Success);
}

struct CreditsValidationCheck {
	QString id;
	QString path;
	QString requiredText;
	QString observedText;
	bool passed = false;
	QString message;
};

struct CreditTokenRequirement {
	QString id;
	QString requiredText;
};

QString fileText(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return {};
	}
	return QString::fromUtf8(file.readAll());
}

bool containsToken(const QString& haystack, const QString& needle)
{
	return haystack.contains(needle, Qt::CaseInsensitive);
}

QString markdownSectionText(const QString& markdown, const QString& heading)
{
	const qsizetype headingIndex = markdown.indexOf(heading, 0, Qt::CaseInsensitive);
	if (headingIndex < 0) {
		return {};
	}
	const qsizetype searchStart = headingIndex + heading.size();
	const qsizetype nextHeadingIndex = markdown.indexOf(QRegularExpression(QStringLiteral("\\n##\\s+")), searchStart);
	if (nextHeadingIndex < 0) {
		return markdown.mid(headingIndex);
	}
	return markdown.mid(headingIndex, nextHeadingIndex - headingIndex);
}

QString normalizedRepositoryUrl(QString url)
{
	url = url.trimmed();
	while (url.endsWith(QLatin1Char('/'))) {
		url.chop(1);
	}
	if (url.endsWith(QStringLiteral(".git"), Qt::CaseInsensitive)) {
		url.chop(4);
	}
	return url.toLower();
}

QString gitmodulesUrlForSourcePath(const QString& gitmodules, const QString& sourcePath)
{
	bool matchingSection = false;
	const QStringList lines = gitmodules.split(QLatin1Char('\n'));
	for (const QString& line : lines) {
		const QString trimmed = line.trimmed();
		if (trimmed.startsWith(QStringLiteral("[submodule"))) {
			matchingSection = false;
			continue;
		}
		if (trimmed.startsWith(QStringLiteral("path"))) {
			const qsizetype equalsIndex = trimmed.indexOf(QLatin1Char('='));
			matchingSection = equalsIndex >= 0 && trimmed.mid(equalsIndex + 1).trimmed() == sourcePath;
			continue;
		}
		if (matchingSection && trimmed.startsWith(QStringLiteral("url"))) {
			const qsizetype equalsIndex = trimmed.indexOf(QLatin1Char('='));
			if (equalsIndex >= 0) {
				return trimmed.mid(equalsIndex + 1).trimmed();
			}
		}
	}
	return {};
}

QString gitHeadRevision(const QString& sourcePath)
{
	QProcess process;
	process.setProgram(QStringLiteral("git"));
	process.setArguments({QStringLiteral("-C"), sourcePath, QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
	process.setProcessChannelMode(QProcess::MergedChannels);
	process.start();
	if (!process.waitForStarted(3000)) {
		return {};
	}
	if (!process.waitForFinished(5000)) {
		process.kill();
		process.waitForFinished(1000);
		return {};
	}
	if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
		return {};
	}
	return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

QVector<CreditTokenRequirement> requiredCreditTokens()
{
	return {
		{QStringLiteral("pakfu"), QStringLiteral("PakFu")},
		{QStringLiteral("ericw-tools"), QStringLiteral("ericw-tools")},
		{QStringLiteral("netradiant-custom"), QStringLiteral("NetRadiant Custom")},
		{QStringLiteral("zdbsp"), QStringLiteral("ZDBSP")},
		{QStringLiteral("zokumbsp"), QStringLiteral("ZokumBSP")},
		{QStringLiteral("gtkradiant"), QStringLiteral("GtkRadiant")},
		{QStringLiteral("trenchbroom"), QStringLiteral("TrenchBroom")},
		{QStringLiteral("quark"), QStringLiteral("QuArK")},
		{QStringLiteral("openai"), QStringLiteral("OpenAI")},
		{QStringLiteral("claude"), QStringLiteral("Claude")},
		{QStringLiteral("gemini"), QStringLiteral("Gemini")},
		{QStringLiteral("elevenlabs"), QStringLiteral("ElevenLabs")},
		{QStringLiteral("meshy"), QStringLiteral("Meshy")},
	};
}

void appendCreditsTextCheck(QVector<CreditsValidationCheck>& checks, const QString& id, const QString& path, const QString& requiredText, const QString& haystack, const QString& message)
{
	checks.append({id, path, requiredText, {}, containsToken(haystack, requiredText), message});
}

QJsonObject creditsValidationCheckJson(const CreditsValidationCheck& check)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), check.id);
	object.insert(QStringLiteral("path"), check.path);
	object.insert(QStringLiteral("requiredText"), check.requiredText);
	if (!check.observedText.isEmpty()) {
		object.insert(QStringLiteral("observedText"), check.observedText);
	}
	object.insert(QStringLiteral("passed"), check.passed);
	object.insert(QStringLiteral("message"), check.message);
	return object;
}

int runCreditsValidateCommand(const QString& commandName, CliOutputFormat format)
{
	const QString readmePath = QDir::current().absoluteFilePath(QStringLiteral("README.md"));
	const QString creditsPath = QDir::current().absoluteFilePath(QStringLiteral("docs/CREDITS.md"));
	const QString gitmodulesPath = QDir::current().absoluteFilePath(QStringLiteral(".gitmodules"));
	const QString readme = fileText(readmePath);
	const QString credits = fileText(creditsPath);
	const QString readmeCredits = markdownSectionText(readme, QStringLiteral("## Credits"));
	const QString gitmodules = fileText(gitmodulesPath);
	QVector<CreditsValidationCheck> checks = {
		{QStringLiteral("readme-exists"), readmePath, QStringLiteral("README.md"), {}, !readme.isEmpty(), readme.isEmpty() ? QStringLiteral("README.md could not be read.") : QStringLiteral("README.md is readable.")},
		{QStringLiteral("credits-exists"), creditsPath, QStringLiteral("docs/CREDITS.md"), {}, !credits.isEmpty(), credits.isEmpty() ? QStringLiteral("docs/CREDITS.md could not be read.") : QStringLiteral("docs/CREDITS.md is readable.")},
		{QStringLiteral("gitmodules-exists"), gitmodulesPath, QStringLiteral(".gitmodules"), {}, !gitmodules.isEmpty(), gitmodules.isEmpty() ? QStringLiteral(".gitmodules could not be read.") : QStringLiteral(".gitmodules is readable.")},
		{QStringLiteral("readme-credits-section"), readmePath, QStringLiteral("## Credits"), {}, !readmeCredits.isEmpty(), QStringLiteral("README Credits section should stay visible and auditable.")},
	};
	for (const CreditTokenRequirement& token : requiredCreditTokens()) {
		appendCreditsTextCheck(checks, QStringLiteral("readme-credit-token-%1").arg(token.id), readmePath, token.requiredText, readmeCredits, QStringLiteral("README Credits section should include %1 attribution.").arg(token.requiredText));
		appendCreditsTextCheck(checks, QStringLiteral("docs-credit-token-%1").arg(token.id), creditsPath, token.requiredText, credits, QStringLiteral("docs/CREDITS.md should include %1 attribution.").arg(token.requiredText));
	}
	for (const CompilerIntegration& compiler : compilerIntegrations()) {
		appendCreditsTextCheck(checks, QStringLiteral("compiler-readme-revision-%1").arg(compiler.id), readmePath, compiler.pinnedRevision, readme, QStringLiteral("README should include the pinned %1 revision.").arg(compiler.displayName));
		appendCreditsTextCheck(checks, QStringLiteral("compiler-docs-revision-%1").arg(compiler.id), creditsPath, compiler.pinnedRevision, credits, QStringLiteral("docs/CREDITS.md should include the pinned %1 revision.").arg(compiler.displayName));
		appendCreditsTextCheck(checks, QStringLiteral("compiler-gitmodules-path-%1").arg(compiler.id), gitmodulesPath, compiler.sourcePath, gitmodules, QStringLiteral(".gitmodules should include the %1 submodule path.").arg(compiler.displayName));

		const QString normalizedGitmoduleUrl = normalizedRepositoryUrl(gitmodulesUrlForSourcePath(gitmodules, compiler.sourcePath));
		checks.append({
			QStringLiteral("compiler-gitmodules-url-%1").arg(compiler.id),
			gitmodulesPath,
			normalizedRepositoryUrl(compiler.upstreamUrl),
			normalizedGitmoduleUrl,
			normalizedGitmoduleUrl == normalizedRepositoryUrl(compiler.upstreamUrl),
			QStringLiteral(".gitmodules should point %1 at the structured upstream URL.").arg(compiler.displayName),
		});

		const QString absoluteSourcePath = QDir::current().absoluteFilePath(compiler.sourcePath);
		const QString headRevision = gitHeadRevision(absoluteSourcePath);
		checks.append({
			QStringLiteral("compiler-submodule-revision-%1").arg(compiler.id),
			absoluteSourcePath,
			compiler.pinnedRevision,
			headRevision,
			headRevision.compare(compiler.pinnedRevision, Qt::CaseInsensitive) == 0,
			headRevision.isEmpty()
				? QStringLiteral("Unable to read checked-out submodule revision for %1.").arg(compiler.displayName)
				: QStringLiteral("Checked-out %1 revision should match the structured manifest pin.").arg(compiler.displayName),
		});
	}
	bool passed = true;
	int failedCount = 0;
	for (const CreditsValidationCheck& check : checks) {
		passed = passed && check.passed;
		if (!check.passed) {
			++failedCount;
		}
	}
	const CliExitCode code = passed ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		QJsonArray array;
		for (const CreditsValidationCheck& check : checks) {
			array.append(creditsValidationCheckJson(check));
		}
		object.insert(QStringLiteral("checks"), array);
		object.insert(QStringLiteral("passed"), passed);
		object.insert(QStringLiteral("checkCount"), checks.size());
		object.insert(QStringLiteral("failedCount"), failedCount);
		printJson(object);
		return exitCodeValue(code);
	}

	std::cout << "Credits validation\n";
	for (const CreditsValidationCheck& check : checks) {
		std::cout << "- " << (check.passed ? "ok" : "missing") << " " << text(check.id) << "\n";
		std::cout << "  " << text(check.message) << "\n";
		if (!check.observedText.isEmpty()) {
			std::cout << "  Observed: " << text(check.observedText) << "\n";
		}
	}
	std::cout << "Passed: " << (passed ? "yes" : "no") << " (" << (checks.size() - failedCount) << "/" << checks.size() << ")\n";
	return exitCodeValue(code);
}

int runAboutCommand(const QString& commandName, CliOutputFormat format)
{
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("about"), aboutJson());
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	std::cout << text(aboutSurfaceText()) << "\n";
	return exitCodeValue(CliExitCode::Success);
}

int runEditorProfilesCommand(const QString& commandName, CliOutputFormat format)
{
	const StudioSettings settings;
	const QString selectedId = settings.selectedEditorProfileId();
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("selectedEditorProfileId"), selectedId);
		object.insert(QStringLiteral("profiles"), editorProfilesJson(selectedId));
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	std::cout << "Editor profiles\n";
	std::cout << "Selected: " << text(editorProfileDisplayNameForId(selectedId)) << " [" << text(selectedId) << "]\n";
	for (const EditorProfileDescriptor& profile : editorProfileDescriptors()) {
		std::cout << "- " << text(profile.displayName) << " [" << text(profile.id) << "]" << (profile.id == selectedId ? " selected" : "") << "\n";
		std::cout << "  Lineage: " << text(profile.lineage) << "\n";
		std::cout << "  Layout: " << text(profile.layoutPresetId) << "\n";
		std::cout << "  Camera: " << text(profile.cameraPresetId) << "\n";
		std::cout << "  Selection: " << text(profile.selectionPresetId) << "\n";
		std::cout << "  Grid: " << text(profile.gridPresetId) << "\n";
		std::cout << "  Terminology: " << text(profile.terminologyPresetId) << "\n";
		std::cout << "  Engines: " << text(profile.supportedEngineFamilies.join(", ")) << "\n";
		std::cout << "  Panels: " << text(profile.defaultPanels.join(", ")) << "\n";
		std::cout << "  Routed commands: " << profile.bindings.size() << "\n";
		std::cout << "  " << text(profile.description) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runEditorCurrentCommand(const QString& commandName, CliOutputFormat format)
{
	const StudioSettings settings;
	EditorProfileDescriptor profile;
	editorProfileForId(settings.selectedEditorProfileId(), &profile);
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("selectedEditorProfileId"), profile.id);
		object.insert(QStringLiteral("profile"), editorProfileJson(profile, profile.id));
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	std::cout << "Selected editor profile\n";
	std::cout << text(editorProfileSummaryText(profile)) << "\n";
	return exitCodeValue(CliExitCode::Success);
}

int runEditorSelectCommand(const QString& commandName, const QString& profileId, CliOutputFormat format)
{
	EditorProfileDescriptor profile;
	if (!editorProfileForId(profileId, &profile)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown editor profile '%1'. Expected one of: %2").arg(profileId, editorProfileIds().join(QStringLiteral(", "))), format);
	}

	StudioSettings settings;
	settings.setSelectedEditorProfileId(profile.id);
	settings.sync();
	if (settings.status() != QSettings::NoError) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Failed to save editor profile: %1").arg(settingsStatusText(settings.status())), format);
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("selectedEditorProfileId"), settings.selectedEditorProfileId());
		object.insert(QStringLiteral("profile"), editorProfileJson(profile, settings.selectedEditorProfileId()));
		printJson(object);
	} else {
		std::cout << "Selected editor profile: " << text(profile.displayName) << " [" << text(profile.id) << "]\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runLocalizationReportCommand(const QString& commandName, const QStringList& args, CliOutputFormat format, bool targetsOnly)
{
	const QString localeName = hasOption(args, QStringLiteral("--locale")) ? optionValue(args, QStringLiteral("--locale")) : QStringLiteral("en");
	const QString catalogRoot = hasOption(args, QStringLiteral("--catalog-root")) ? optionValue(args, QStringLiteral("--catalog-root")) : QStringLiteral("i18n");
	const LocalizationSmokeReport report = buildLocalizationSmokeReport(localeName, catalogRoot);
	const CliExitCode code = report.ok ? CliExitCode::Success : CliExitCode::ValidationFailed;

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		if (targetsOnly) {
			object.insert(QStringLiteral("targets"), localizationTargetsJson());
			object.insert(QStringLiteral("targetCount"), localizationTargets().size());
		} else {
			object.insert(QStringLiteral("localization"), localizationSmokeReportJson(report));
		}
		printJson(object);
		return exitCodeValue(code);
	}

	if (targetsOnly) {
		std::cout << "Localization targets\n";
		for (const LocalizationTarget& target : localizationTargets()) {
			std::cout << "- " << text(target.localeName) << ": " << text(target.englishName) << " / " << text(target.nativeName) << (target.rightToLeft ? " [RTL]" : "") << "\n";
		}
	} else {
		std::cout << text(localizationSmokeReportText(report)) << "\n";
	}
	return exitCodeValue(code);
}

int runDiagnosticsBundleCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QJsonObject bundle = diagnosticBundleJson();
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	QString writtenPath;
	if (!outputPath.trimmed().isEmpty()) {
		QDir outputDir(outputPath);
		if (!outputDir.exists() && !QDir().mkpath(outputDir.path())) {
			return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to create diagnostic bundle folder: %1").arg(nativePath(outputPath)), format);
		}
		writtenPath = outputDir.filePath(QStringLiteral("vibestudio-diagnostics.json"));
		QFile file(writtenPath);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
			return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to write diagnostic bundle: %1").arg(file.errorString()), format);
		}
		file.write(QJsonDocument(bundle).toJson(QJsonDocument::Indented));
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("bundle"), bundle);
		object.insert(QStringLiteral("writtenPath"), writtenPath);
		printJson(object);
	} else {
		std::cout << "Diagnostic bundle\n";
		std::cout << "Version: " << text(versionString()) << "\n";
		std::cout << "Commands: " << cliCommandDescriptors().size() << "\n";
		std::cout << "Modules: " << plannedModules().size() << "\n";
		std::cout << "Redaction: " << text(bundle.value(QStringLiteral("redaction")).toString()) << "\n";
		if (!writtenPath.isEmpty()) {
			std::cout << "Written: " << text(nativePath(QFileInfo(writtenPath).absoluteFilePath())) << "\n";
		}
	}
	return exitCodeValue(CliExitCode::Success);
}

int runAiStatusCommand(const QString& commandName, CliOutputFormat format)
{
	const StudioSettings settings;
	const AiAutomationPreferences preferences = settings.aiAutomationPreferences();
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("preferences"), aiPreferencesJson(preferences));
		object.insert(QStringLiteral("capabilities"), aiCapabilitiesJson());
		object.insert(QStringLiteral("connectors"), aiConnectorsJson());
		object.insert(QStringLiteral("models"), aiModelsJson());
		object.insert(QStringLiteral("credentials"), aiCredentialStatusesJson(preferences));
		object.insert(QStringLiteral("tools"), aiToolsJson());
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	std::cout << "AI automation\n";
	std::cout << text(aiAutomationPreferencesText(preferences)) << "\n";
	std::cout << "Connector stubs\n";
	for (const AiConnectorDescriptor& connector : aiConnectorDescriptors()) {
		std::cout << "- " << text(connector.displayName) << " [" << text(connector.id) << "]\n";
		std::cout << "  Provider: " << text(connector.providerFamily) << "\n";
		std::cout << "  Endpoint: " << text(connector.endpointKind) << "\n";
		std::cout << "  Location: " << (connector.cloudBased ? "cloud" : "local/offline") << "\n";
		std::cout << "  Implemented: " << (connector.implemented ? "yes" : "design stub") << "\n";
		std::cout << "  Capabilities: " << text(connector.capabilities.join(", ")) << "\n";
	}
	std::cout << "Credential status\n";
	for (const AiCredentialStatus& status : aiCredentialStatuses(preferences)) {
		std::cout << "- " << text(status.connectorId) << ": " << (status.configured ? "configured" : "missing") << " " << text(status.redactedValue) << "\n";
	}
	std::cout << "Models\n";
	for (const AiModelDescriptor& model : aiModelDescriptors()) {
		std::cout << "- " << text(model.displayName) << " [" << text(model.id) << "]\n";
		std::cout << "  Connector: " << text(model.connectorId) << "\n";
		std::cout << "  Capabilities: " << text(model.capabilities.join(", ")) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runAiConnectorsCommand(const QString& commandName, CliOutputFormat format)
{
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("capabilities"), aiCapabilitiesJson());
		object.insert(QStringLiteral("connectors"), aiConnectorsJson());
		object.insert(QStringLiteral("models"), aiModelsJson());
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	std::cout << "AI connector design stubs\n";
	for (const AiConnectorDescriptor& connector : aiConnectorDescriptors()) {
		std::cout << text(aiConnectorSummaryText(connector)) << "\n\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runAiToolsCommand(const QString& commandName, CliOutputFormat format)
{
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("tools"), aiToolsJson());
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	std::cout << "AI-callable VibeStudio tools\n";
	for (const AiToolDescriptor& tool : aiToolDescriptors()) {
		std::cout << "- " << text(tool.displayName) << " [" << text(tool.id) << "]\n";
		std::cout << "  " << text(tool.description) << "\n";
		std::cout << "  Capabilities: " << text(tool.capabilities.join(", ")) << "\n";
		std::cout << "  Approval: " << (tool.requiresApproval ? "required" : "not required") << "\n";
		std::cout << "  Writes: " << (tool.writesFiles ? "staged only" : "none") << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

QString aiLogTextFromArgs(const QStringList& args)
{
	if (hasOption(args, QStringLiteral("--text"))) {
		return optionValue(args, QStringLiteral("--text"));
	}
	const QString logPath = optionValue(args, QStringLiteral("--log"));
	if (logPath.trimmed().isEmpty()) {
		return {};
	}
	return fileText(logPath);
}

int finishAiWorkflowCommand(const QString& commandName, const AiWorkflowResult& result, const QStringList& args, CliOutputFormat format)
{
	AiWorkflowResult output = result;
	const QString manifestPath = optionValue(args, QStringLiteral("--manifest"));
	if (!manifestPath.trimmed().isEmpty()) {
		QString error;
		if (!saveAiWorkflowManifest(output.manifest, manifestPath, &error)) {
			return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Failed to save AI workflow manifest: %1").arg(error), format);
		}
		output.manifest.stagedOutputs.push_back({QStringLiteral("manifest-file"), QStringLiteral("json"), QStringLiteral("Saved workflow manifest"), QFileInfo(manifestPath).absoluteFilePath(), QStringLiteral("AI workflow manifest written for review."), {QFileInfo(manifestPath).absoluteFilePath()}, true});
	}

	const CliExitCode code = output.manifest.state == OperationState::Failed ? CliExitCode::Failure : CliExitCode::Success;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("workflow"), aiWorkflowResultJson(output));
		object.insert(QStringLiteral("taskState"), taskStateJson(output.manifest.manifestId, output.manifest.state, output.summary, 0, output.manifest.cancellable));
		printJson(object);
		return exitCodeValue(code);
	}

	std::cout << text(output.title) << "\n";
	std::cout << text(output.summary) << "\n";
	if (!output.reviewableText.trimmed().isEmpty()) {
		std::cout << text(output.reviewableText) << "\n";
	}
	std::cout << text(aiWorkflowManifestText(output.manifest)) << "\n";
	if (!manifestPath.trimmed().isEmpty()) {
		std::cout << "Manifest saved: " << text(nativePath(QFileInfo(manifestPath).absoluteFilePath())) << "\n";
	}
	return exitCodeValue(code);
}

int runAiExplainLogCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString logText = aiLogTextFromArgs(args);
	if (logText.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --log <path> or --text <compiler-output>.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		explainCompilerLogAiExperiment(logText, settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

int runAiProposeCommandCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString prompt = optionValue(args, QStringLiteral("--prompt"));
	if (prompt.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --prompt <text>.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		proposeCompilerCommandAiExperiment(prompt, optionValue(args, QStringLiteral("--workspace-root")), settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

int runAiProposeManifestCommand(const QString& commandName, const QString& projectRoot, const QStringList& args, CliOutputFormat format)
{
	const QString root = projectRoot.trimmed().isEmpty() ? optionValue(args, QStringLiteral("--project-root")) : projectRoot;
	if (root.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a project root path.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		draftProjectManifestAiExperiment(root, optionValue(args, QStringLiteral("--name")), settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

int runAiPackageDepsCommand(const QString& commandName, const QString& packagePath, const QStringList& args, CliOutputFormat format)
{
	const QString path = packagePath.trimmed().isEmpty() ? optionValue(args, QStringLiteral("--package")) : packagePath;
	if (path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a package path.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		suggestPackageDependenciesAiExperiment(path, settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

int runAiCliCommandCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString prompt = optionValue(args, QStringLiteral("--prompt"));
	if (prompt.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --prompt <text>.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		generateCliCommandAiExperiment(prompt, settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

int runAiFixPlanCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString logText = aiLogTextFromArgs(args);
	if (logText.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --log <path> or --text <compiler-output>.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		fixAndRetryPlanAiExperiment(logText, optionValue(args, QStringLiteral("--command")), settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

int runAiAssetRequestCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString prompt = optionValue(args, QStringLiteral("--prompt"));
	if (prompt.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --prompt <text>.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		stageAssetGenerationRequestAiExperiment(optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--kind")), prompt, settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

int runAiCompareCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString prompt = optionValue(args, QStringLiteral("--prompt"));
	if (prompt.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --prompt <text>.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		compareProviderOutputsAiExperiment(prompt, optionValue(args, QStringLiteral("--provider-a")), optionValue(args, QStringLiteral("--provider-b")), settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--model-a")), optionValue(args, QStringLiteral("--model-b"))),
		args,
		format);
}

int runAiShaderScaffoldCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString prompt = optionValue(args, QStringLiteral("--prompt"));
	if (prompt.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --prompt <text>.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		promptToShaderScaffoldAiExperiment(prompt, settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

int runAiEntitySnippetCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString prompt = optionValue(args, QStringLiteral("--prompt"));
	if (prompt.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --prompt <text>.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		promptToEntityDefinitionAiExperiment(prompt, settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

int runAiPackagePlanCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString prompt = optionValue(args, QStringLiteral("--prompt"));
	if (prompt.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --prompt <text>.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		promptToPackageValidationPlanAiExperiment(prompt, optionValue(args, QStringLiteral("--package")), settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

int runAiBatchRecipeCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString prompt = optionValue(args, QStringLiteral("--prompt"));
	if (prompt.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --prompt <text>.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		promptToBatchConversionRecipeAiExperiment(prompt, settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

AiWorkflowResult aiReviewWorkflowFromArgs(const QStringList& args, const AiAutomationPreferences& preferences)
{
	const QString prompt = optionValue(args, QStringLiteral("--prompt"));
	const QString kind = normalizedOptionId(optionValue(args, QStringLiteral("--kind")));
	if (kind == QStringLiteral("entity") || kind == QStringLiteral("entity-snippet") || kind == QStringLiteral("entity-definition")) {
		return promptToEntityDefinitionAiExperiment(prompt, preferences, optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model")));
	}
	if (kind == QStringLiteral("package") || kind == QStringLiteral("package-plan") || kind == QStringLiteral("package-validation")) {
		return promptToPackageValidationPlanAiExperiment(prompt, optionValue(args, QStringLiteral("--package")), preferences, optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model")));
	}
	if (kind == QStringLiteral("batch") || kind == QStringLiteral("batch-recipe") || kind == QStringLiteral("conversion")) {
		return promptToBatchConversionRecipeAiExperiment(prompt, preferences, optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model")));
	}
	if (kind == QStringLiteral("cli") || kind == QStringLiteral("command")) {
		return generateCliCommandAiExperiment(prompt, preferences, optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model")));
	}
	return promptToShaderScaffoldAiExperiment(prompt, preferences, optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model")));
}

int runAiReviewCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString prompt = optionValue(args, QStringLiteral("--prompt"));
	if (prompt.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --prompt <text>.").arg(commandName), format);
	}
	const StudioSettings settings;
	AiWorkflowResult result = aiReviewWorkflowFromArgs(args, settings.aiAutomationPreferences());
	result.title = QStringLiteral("%1 %2").arg(result.title, QStringLiteral("Review"));
	result.reviewableText = aiProposalReviewSurfaceText(result);
	result.summary = QStringLiteral("%1 %2").arg(result.summary, QStringLiteral("Review surface rendered."));
	return finishAiWorkflowCommand(commandName, result, args, format);
}

int runProjectInitCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	if (path.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a project folder path.").arg(commandName), format);
	}

	const QString selectedInstallationId = hasOption(args, QStringLiteral("--installation")) ? optionValue(args, QStringLiteral("--installation")) : optionValue(args, QStringLiteral("--project-installation"));
	const QString editorProfileId = hasOption(args, QStringLiteral("--editor-profile")) ? optionValue(args, QStringLiteral("--editor-profile")) : optionValue(args, QStringLiteral("--project-editor-profile"));
	const QString paletteId = hasOption(args, QStringLiteral("--palette")) ? optionValue(args, QStringLiteral("--palette")) : optionValue(args, QStringLiteral("--project-palette"));
	const QString compilerProfileId = hasOption(args, QStringLiteral("--compiler-profile")) ? optionValue(args, QStringLiteral("--compiler-profile")) : optionValue(args, QStringLiteral("--project-compiler-profile"));
	const QString projectCompilerSearchPaths = optionValue(args, QStringLiteral("--project-compiler-search-paths"));
	const QString projectCompilerToolId = optionValue(args, QStringLiteral("--project-compiler-tool"));
	const QString projectCompilerExecutable = optionValue(args, QStringLiteral("--project-compiler-executable"));
	const bool aiFreeOverrideRequested = hasOption(args, QStringLiteral("--ai-free")) || hasOption(args, QStringLiteral("--project-ai-free"));
	bool aiFreeMode = false;
	if (aiFreeOverrideRequested) {
		const QString value = hasOption(args, QStringLiteral("--ai-free")) ? optionValue(args, QStringLiteral("--ai-free")) : optionValue(args, QStringLiteral("--project-ai-free"));
		if (!boolOptionValue(value, &aiFreeMode)) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Project AI-free override requires on or off."), format);
		}
	}
	if (!editorProfileId.trimmed().isEmpty()) {
		EditorProfileDescriptor profile;
		if (!editorProfileForId(editorProfileId, &profile)) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown editor profile '%1'. Expected one of: %2").arg(editorProfileId, editorProfileIds().join(QStringLiteral(", "))), format);
		}
	}

	ProjectManifest manifest;
	QString error;
	if (!loadProjectManifest(path, &manifest, &error)) {
		manifest = defaultProjectManifest(path);
	}
	if (!selectedInstallationId.trimmed().isEmpty()) {
		manifest.selectedInstallationId = selectedInstallationId.trimmed();
		manifest.settingsOverrides.selectedInstallationId = selectedInstallationId.trimmed();
	}
	if (!editorProfileId.trimmed().isEmpty()) {
		manifest.settingsOverrides.editorProfileId = normalizedEditorProfileId(editorProfileId);
	}
	if (!paletteId.trimmed().isEmpty()) {
		manifest.settingsOverrides.paletteId = paletteId.trimmed();
	}
	if (!compilerProfileId.trimmed().isEmpty()) {
		manifest.settingsOverrides.compilerProfileId = compilerProfileId.trimmed();
	}
	if (!projectCompilerSearchPaths.trimmed().isEmpty()) {
		manifest.compilerSearchPaths = optionPathList(args, QStringLiteral("--project-compiler-search-paths"));
	}
	if (!projectCompilerToolId.trimmed().isEmpty() || !projectCompilerExecutable.trimmed().isEmpty()) {
		if (projectCompilerToolId.trimmed().isEmpty() || projectCompilerExecutable.trimmed().isEmpty()) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Project compiler executable override requires --project-compiler-tool and --project-compiler-executable."), format);
		}
		CompilerToolDescriptor descriptor;
		if (!compilerToolDescriptorForId(projectCompilerToolId, &descriptor)) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown compiler tool id: %1").arg(projectCompilerToolId), format);
		}
		const QString executablePath = QFileInfo(projectCompilerExecutable).isAbsolute() ? QFileInfo(projectCompilerExecutable).absoluteFilePath() : projectCompilerExecutable;
		bool replaced = false;
		for (CompilerToolPathOverride& override : manifest.compilerToolOverrides) {
			if (QString::compare(override.toolId, descriptor.id, Qt::CaseInsensitive) == 0) {
				override.executablePath = executablePath;
				replaced = true;
				break;
			}
		}
		if (!replaced) {
			manifest.compilerToolOverrides.push_back({descriptor.id, executablePath});
		}
	}
	if (aiFreeOverrideRequested) {
		manifest.settingsOverrides.aiFreeModeSet = true;
		manifest.settingsOverrides.aiFreeMode = aiFreeMode;
	}
	if (!saveProjectManifest(manifest, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Failed to save project manifest: %1").arg(error), format);
	}
	if (!loadProjectManifest(path, &manifest, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Failed to reload project manifest: %1").arg(error), format);
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("saved"), true);
		object.insert(QStringLiteral("manifest"), projectManifestJson(manifest));
		object.insert(QStringLiteral("health"), projectHealthJson(buildProjectHealthSummary(manifest)));
		printJson(object);
	} else {
		std::cout << "Project manifest saved\n";
		std::cout << "Path: " << text(nativePath(projectManifestPath(path))) << "\n";
		printProjectInfo(manifest);
	}
	return exitCodeValue(CliExitCode::Success);
}

int runProjectInfoCommand(const QString& commandName, const QString& path, CliOutputFormat format)
{
	if (path.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a project folder path.").arg(commandName), format);
	}

	ProjectManifest manifest;
	QString error;
	if (!loadProjectManifest(path, &manifest, &error)) {
		return printCliError(commandName, CliExitCode::NotFound, QStringLiteral("Unable to load project manifest: %1. Manifest path: %2").arg(error, nativePath(projectManifestPath(path))), format);
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("manifest"), projectManifestJson(manifest));
		object.insert(QStringLiteral("health"), projectHealthJson(buildProjectHealthSummary(manifest)));
		printJson(object);
	} else {
		printProjectInfo(manifest);
	}
	return exitCodeValue(CliExitCode::Success);
}

int runProjectValidateCommand(const QString& commandName, const QString& path, CliOutputFormat format)
{
	if (path.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a project folder path.").arg(commandName), format);
	}

	ProjectManifest manifest;
	QString error;
	if (!loadProjectManifest(path, &manifest, &error)) {
		return printCliError(commandName, CliExitCode::NotFound, QStringLiteral("Unable to load project manifest: %1. Manifest path: %2").arg(error, nativePath(projectManifestPath(path))), format);
	}

	const ProjectHealthSummary health = buildProjectHealthSummary(manifest);
	const CliExitCode code = health.failedCount > 0 ? CliExitCode::ValidationFailed : CliExitCode::Success;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("manifest"), projectManifestJson(manifest));
		object.insert(QStringLiteral("health"), projectHealthJson(health));
		printJson(object);
	} else {
		printProjectHealth(health);
	}
	return exitCodeValue(code);
}

int runPackageInfoCommand(const QString& commandName, const QString& path, CliOutputFormat format)
{
	if (path.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a folder, PAK, WAD, ZIP, or PK3 path.").arg(commandName), format);
	}

	PackageArchive archive;
	QString error;
	qint64 packageOpenMs = 0;
	if (!loadPackageForCliQuiet(path, &archive, &error, &packageOpenMs)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to open package: %1").arg(error), format);
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("package"), packageInfoJson(archive));
		object.insert(QStringLiteral("timing"), packageTimingJson(packageOpenMs));
		printJson(object);
	} else {
		printPackageInfo(archive);
		if (verboseOutput()) {
			std::cerr << "vibestudio: package opened in " << packageOpenMs << " ms\n";
		}
	}
	return exitCodeValue(CliExitCode::Success);
}

int runPackageListCommand(const QString& commandName, const QString& path, CliOutputFormat format)
{
	if (path.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a folder, PAK, WAD, ZIP, or PK3 path.").arg(commandName), format);
	}

	PackageArchive archive;
	QString error;
	qint64 packageOpenMs = 0;
	if (!loadPackageForCliQuiet(path, &archive, &error, &packageOpenMs)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to open package: %1").arg(error), format);
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("package"), packageListJson(archive));
		object.insert(QStringLiteral("timing"), packageTimingJson(packageOpenMs));
		printJson(object);
	} else {
		printPackageList(archive);
		if (verboseOutput()) {
			std::cerr << "vibestudio: package listed after open in " << packageOpenMs << " ms\n";
		}
	}
	return exitCodeValue(CliExitCode::Success);
}

int runPackagePreviewCommand(const QString& commandName, const QString& packagePath, const QString& entryPath, CliOutputFormat format)
{
	if (packagePath.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a folder, PAK, WAD, ZIP, or PK3 path.").arg(commandName), format);
	}
	if (entryPath.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a virtual package entry path.").arg(commandName), format);
	}

	PackageArchive archive;
	QString error;
	qint64 packageOpenMs = 0;
	if (!loadPackageForCliQuiet(packagePath, &archive, &error, &packageOpenMs)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to open package: %1").arg(error), format);
	}

	QElapsedTimer previewTimer;
	previewTimer.start();
	const PackagePreview preview = buildPackageEntryPreview(archive, entryPath);
	const qint64 previewMs = previewTimer.elapsed();
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, preview.kind == PackagePreviewKind::Unavailable ? CliExitCode::NotFound : CliExitCode::Success);
		object.insert(QStringLiteral("packagePath"), packagePath);
		object.insert(QStringLiteral("preview"), packagePreviewJson(preview));
		object.insert(QStringLiteral("timing"), packageTimingJson(packageOpenMs, previewMs));
		printJson(object);
		return preview.kind == PackagePreviewKind::Unavailable ? exitCodeValue(CliExitCode::NotFound) : exitCodeValue(CliExitCode::Success);
	}

	const bool previewPrinted = printPackagePreview(archive, entryPath, &preview);
	if (verboseOutput()) {
		std::cerr << "vibestudio: package opened in " << packageOpenMs << " ms; preview built in " << previewMs << " ms\n";
	}
	return previewPrinted ? exitCodeValue(CliExitCode::Success) : exitCodeValue(CliExitCode::Failure);
}

int runPackageExtractCommand(const QString& commandName, const QString& packagePath, const QString& outputPath, const QStringList& entries, bool extractAll, bool dryRun, bool overwrite, CliOutputFormat format)
{
	if (packagePath.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a folder, PAK, WAD, ZIP, or PK3 path.").arg(commandName), format);
	}
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <folder>.").arg(commandName), format);
	}

	PackageArchive archive;
	QString error;
	if (!loadPackageForCliQuiet(packagePath, &archive, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to open package: %1").arg(error), format);
	}

	PackageExtractionRequest request;
	request.targetDirectory = outputPath;
	request.virtualPaths = entries;
	request.extractAll = extractAll || entries.isEmpty();
	request.dryRun = dryRun;
	request.overwriteExisting = overwrite;
	const PackageExtractionReport report = extractPackageEntries(archive, request);
	const CliExitCode code = report.succeeded() ? CliExitCode::Success : CliExitCode::Failure;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("extraction"), packageExtractionReportJson(report));
		printJson(object);
	} else {
		printPackageExtractionReport(report);
	}
	return exitCodeValue(code);
}

int runPackageValidateCommand(const QString& commandName, const QString& packagePath, CliOutputFormat format)
{
	if (packagePath.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a folder, PAK, WAD, ZIP, or PK3 path.").arg(commandName), format);
	}
	if (!QFileInfo::exists(packagePath)) {
		return printCliError(commandName, CliExitCode::NotFound, QStringLiteral("Package path not found: %1").arg(nativePath(packagePath)), format);
	}

	PackageArchive archive;
	QString error;
	if (!loadPackageForCliQuiet(packagePath, &archive, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to open package: %1").arg(error), format);
	}

	const CliExitCode code = archive.warnings().isEmpty() ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		QJsonObject validation;
		validation.insert(QStringLiteral("usable"), archive.warnings().isEmpty());
		validation.insert(QStringLiteral("warningCount"), archive.warnings().size());
		validation.insert(QStringLiteral("warnings"), packageWarningsJson(archive.warnings()));
		object.insert(QStringLiteral("package"), packageInfoJson(archive));
		object.insert(QStringLiteral("validation"), validation);
		printJson(object);
	} else {
		printPackageValidation(archive);
	}
	return exitCodeValue(code);
}

int runPackageStageCommand(const QString& commandName, const QString& packagePath, const QStringList& args, CliOutputFormat format)
{
	if (packagePath.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a folder, PAK, WAD, ZIP, or PK3 path.").arg(commandName), format);
	}

	PackageStagingModel staging;
	QString error;
	if (!loadPackageStaging(packagePath, &staging, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to stage package: %1").arg(error), format);
	}
	if (!applyPackageStageArgs(&staging, args, &error)) {
		return printCliError(commandName, CliExitCode::Usage, error, format);
	}

	const CliExitCode code = staging.summary().canSave ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("staging"), packageStagingJson(staging));
		printJson(object);
	} else {
		printPackageStagingSummary(staging);
	}
	return exitCodeValue(code);
}

int runPackageManifestCommand(const QString& commandName, const QString& packagePath, const QString& outputPath, const QStringList& args, CliOutputFormat format)
{
	if (packagePath.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a folder, PAK, WAD, ZIP, or PK3 path.").arg(commandName), format);
	}
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <manifest.json>.").arg(commandName), format);
	}

	PackageStagingModel staging;
	QString error;
	if (!loadPackageStaging(packagePath, &staging, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to stage package: %1").arg(error), format);
	}
	if (!applyPackageStageArgs(&staging, args, &error)) {
		return printCliError(commandName, CliExitCode::Usage, error, format);
	}
	if (!staging.exportManifest(outputPath, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to export package manifest: %1").arg(error), format);
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("manifestPath"), QFileInfo(outputPath).absoluteFilePath());
		object.insert(QStringLiteral("staging"), packageStagingJson(staging));
		printJson(object);
	} else {
		std::cout << "Package staging manifest exported: " << text(nativePath(QFileInfo(outputPath).absoluteFilePath())) << "\n";
		printPackageStagingSummary(staging);
	}
	return exitCodeValue(CliExitCode::Success);
}

int runPackageSaveAsCommand(const QString& commandName, const QString& packagePath, const QString& outputPath, const QStringList& args, CliOutputFormat format)
{
	if (packagePath.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a source package path.").arg(commandName), format);
	}
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a save-as output path.").arg(commandName), format);
	}

	PackageStagingModel staging;
	QString error;
	if (!loadPackageStaging(packagePath, &staging, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to stage package: %1").arg(error), format);
	}
	if (!applyPackageStageArgs(&staging, args, &error)) {
		return printCliError(commandName, CliExitCode::Usage, error, format);
	}

	PackageWriteRequest request;
	request.destinationPath = outputPath;
	request.format = packageWriteFormatFromArgs(args, outputPath);
	request.allowOverwrite = hasOption(args, QStringLiteral("--overwrite"));
	request.writeManifest = hasOption(args, QStringLiteral("--manifest")) || hasOption(args, QStringLiteral("--write-manifest"));
	request.dryRun = hasOption(args, QStringLiteral("--dry-run"));
	request.manifestPath = optionValue(args, QStringLiteral("--manifest"));
	const PackageWriteReport report = staging.writeArchive(request);
	const CliExitCode code = report.succeeded() ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("write"), packageWriteReportJson(report));
		object.insert(QStringLiteral("staging"), packageStagingJson(staging));
		printJson(object);
	} else {
		std::cout << text(packageWriteReportText(report)) << "\n";
	}
	return exitCodeValue(code);
}

QStringList semicolonOrCommaList(const QString& value)
{
	QStringList result;
	for (const QString& chunk : value.split(QRegularExpression(QStringLiteral("[;,]")), Qt::SkipEmptyParts)) {
		const QString trimmed = chunk.trimmed();
		if (!trimmed.isEmpty()) {
			result.push_back(trimmed);
		}
	}
	return result;
}

QStringList assetEntryArgs(const QStringList& args)
{
	QStringList entries = optionValues(args, QStringLiteral("--entry"));
	entries += optionValues(args, QStringLiteral("--asset-entry"));
	entries += semicolonOrCommaList(optionValue(args, QStringLiteral("--entries")));

	QStringList normalized;
	for (const QString& entry : entries) {
		const QString trimmed = entry.trimmed();
		if (!trimmed.isEmpty() && !normalized.contains(trimmed)) {
			normalized.push_back(trimmed);
		}
	}
	return normalized;
}

QStringList assetTextExtensionsFromArgs(const QStringList& args)
{
	QStringList extensions;
	for (QString extension : semicolonOrCommaList(optionValue(args, QStringLiteral("--extensions")))) {
		extension = extension.trimmed().toLower();
		if (extension.startsWith(QLatin1Char('.'))) {
			extension.remove(0, 1);
		}
		if (!extension.isEmpty() && !extensions.contains(extension)) {
			extensions.push_back(extension);
		}
	}
	return extensions;
}

QRect cropRectFromOption(const QString& value, bool* ok)
{
	if (ok) {
		*ok = true;
	}
	if (value.trimmed().isEmpty()) {
		return {};
	}
	const QStringList parts = value.split(',', Qt::SkipEmptyParts);
	if (parts.size() != 4) {
		if (ok) {
			*ok = false;
		}
		return {};
	}
	bool parsed = true;
	const int x = parts.value(0).trimmed().toInt(&parsed);
	if (!parsed) {
		if (ok) {
			*ok = false;
		}
		return {};
	}
	const int y = parts.value(1).trimmed().toInt(&parsed);
	if (!parsed) {
		if (ok) {
			*ok = false;
		}
		return {};
	}
	const int width = parts.value(2).trimmed().toInt(&parsed);
	if (!parsed) {
		if (ok) {
			*ok = false;
		}
		return {};
	}
	const int height = parts.value(3).trimmed().toInt(&parsed);
	if (!parsed || width <= 0 || height <= 0) {
		if (ok) {
			*ok = false;
		}
		return {};
	}
	return QRect(x, y, width, height);
}

QSize resizeSizeFromOption(QString value, bool* ok)
{
	if (ok) {
		*ok = true;
	}
	value = value.trimmed().toLower();
	if (value.isEmpty()) {
		return {};
	}
	value.replace(QLatin1Char('x'), QLatin1Char(','));
	const QStringList parts = value.split(',', Qt::SkipEmptyParts);
	if (parts.size() != 2) {
		if (ok) {
			*ok = false;
		}
		return {};
	}
	bool widthOk = false;
	bool heightOk = false;
	const int width = parts.value(0).trimmed().toInt(&widthOk);
	const int height = parts.value(1).trimmed().toInt(&heightOk);
	if (!widthOk || !heightOk || width <= 0 || height <= 0) {
		if (ok) {
			*ok = false;
		}
		return {};
	}
	return QSize(width, height);
}

int runAssetInspectCommand(const QString& commandName, const QString& packagePath, const QString& entryPath, CliOutputFormat format)
{
	return runPackagePreviewCommand(commandName, packagePath, entryPath, format);
}

int runAssetConvertCommand(const QString& commandName, const QString& packagePath, const QString& outputPath, const QStringList& args, CliOutputFormat format)
{
	if (packagePath.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a folder, PAK, WAD, ZIP, or PK3 path.").arg(commandName), format);
	}
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <folder>.").arg(commandName), format);
	}

	bool cropOk = true;
	bool resizeOk = true;
	const QRect cropRect = cropRectFromOption(optionValue(args, QStringLiteral("--crop")), &cropOk);
	const QSize resizeSize = resizeSizeFromOption(optionValue(args, QStringLiteral("--resize")), &resizeOk);
	if (!cropOk) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--crop must be x,y,w,h with positive width and height."), format);
	}
	if (!resizeOk) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--resize must be WxH with positive dimensions."), format);
	}
	const QString paletteMode = optionValue(args, QStringLiteral("--palette")).trimmed().toLower();
	if (!paletteMode.isEmpty() && paletteMode != QStringLiteral("grayscale") && paletteMode != QStringLiteral("indexed")) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--palette must be grayscale or indexed."), format);
	}

	PackageArchive archive;
	QString error;
	if (!loadPackageForCliQuiet(packagePath, &archive, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to open package: %1").arg(error), format);
	}

	AssetImageConversionRequest request;
	request.outputDirectory = outputPath;
	request.virtualPaths = assetEntryArgs(args);
	request.outputFormat = optionValue(args, QStringLiteral("--format")).trimmed().isEmpty() ? QStringLiteral("png") : optionValue(args, QStringLiteral("--format")).trimmed();
	request.cropRect = cropRect;
	request.resizeSize = resizeSize;
	request.paletteMode = paletteMode;
	request.dryRun = hasOption(args, QStringLiteral("--dry-run"));
	request.overwriteExisting = hasOption(args, QStringLiteral("--overwrite"));
	const AssetImageConversionReport report = convertPackageImages(archive, request);
	const CliExitCode code = report.succeeded() ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("imageConversion"), assetImageConversionReportJson(report));
		printJson(object);
	} else {
		std::cout << text(assetImageConversionReportText(report)) << "\n";
	}
	return exitCodeValue(code);
}

int runAssetAudioWavCommand(const QString& commandName, const QString& packagePath, const QString& entryPath, const QString& outputPath, const QStringList& args, CliOutputFormat format)
{
	if (packagePath.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a folder, PAK, WAD, ZIP, or PK3 path.").arg(commandName), format);
	}
	if (entryPath.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a virtual package audio entry path.").arg(commandName), format);
	}
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <file.wav>.").arg(commandName), format);
	}

	PackageArchive archive;
	QString error;
	if (!loadPackageForCliQuiet(packagePath, &archive, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to open package: %1").arg(error), format);
	}
	const AssetAudioExportReport report = exportPackageAudioToWav(archive, entryPath, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	const CliExitCode code = report.succeeded() ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("audioExport"), assetAudioExportReportJson(report));
		printJson(object);
	} else {
		std::cout << text(assetAudioExportReportText(report)) << "\n";
	}
	return exitCodeValue(code);
}

int runAssetTextCommand(const QString& commandName, const QString& projectRoot, const QString& positionalFind, const QString& positionalReplace, bool replace, const QStringList& args, CliOutputFormat format)
{
	if (projectRoot.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a project root path.").arg(commandName), format);
	}
	const QString findText = hasOption(args, QStringLiteral("--find")) ? optionValue(args, QStringLiteral("--find")) : positionalFind;
	const QString replaceText = hasOption(args, QStringLiteral("--replace")) ? optionValue(args, QStringLiteral("--replace")) : positionalReplace;
	if (findText.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --find <text>.").arg(commandName), format);
	}
	if (replace && replaceText.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --replace <text>.").arg(commandName), format);
	}

	AssetTextSearchRequest request;
	request.rootPath = projectRoot;
	request.findText = findText;
	request.replaceText = replaceText;
	request.extensions = assetTextExtensionsFromArgs(args);
	request.replace = replace;
	request.dryRun = replace ? !hasOption(args, QStringLiteral("--write")) || hasOption(args, QStringLiteral("--dry-run")) : true;
	request.caseSensitive = hasOption(args, QStringLiteral("--case-sensitive"));
	const AssetTextSearchReport report = findReplaceProjectText(request);
	const CliExitCode code = report.succeeded() ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("textSearch"), assetTextSearchReportJson(report));
		printJson(object);
	} else {
		std::cout << text(assetTextSearchReportText(report)) << "\n";
	}
	return exitCodeValue(code);
}

LevelMapLoadRequest levelMapLoadRequestFromArgs(const QString& path, const QStringList& args)
{
	LevelMapLoadRequest request;
	request.path = hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : path;
	request.mapName = hasOption(args, QStringLiteral("--map-name")) ? optionValue(args, QStringLiteral("--map-name")) : optionValue(args, QStringLiteral("--map"));
	request.engineHint = hasOption(args, QStringLiteral("--engine-hint")) ? optionValue(args, QStringLiteral("--engine-hint")) : optionValue(args, QStringLiteral("--engine"));
	return request;
}

CliExitCode levelMapLoadFailureCode(const LevelMapLoadRequest& request)
{
	return QFileInfo::exists(request.path) ? CliExitCode::Failure : CliExitCode::NotFound;
}

bool parseLevelMapSelector(const QString& selector, QString* objectKind, int* objectId)
{
	const QStringList parts = selector.trimmed().split(':', Qt::SkipEmptyParts);
	if (parts.size() != 2) {
		return false;
	}
	bool ok = false;
	const int parsedId = parts.value(1).toInt(&ok);
	if (!ok || parsedId < 0) {
		return false;
	}
	if (objectKind) {
		*objectKind = normalizedOptionId(parts.value(0));
	}
	if (objectId) {
		*objectId = parsedId;
	}
	return true;
}

bool parseLevelMapEntityId(const QString& value, int* entityId)
{
	QString kind;
	int id = -1;
	if (parseLevelMapSelector(value, &kind, &id)) {
		if (kind != QStringLiteral("entity")) {
			return false;
		}
		if (entityId) {
			*entityId = id;
		}
		return true;
	}
	bool ok = false;
	id = value.trimmed().toInt(&ok);
	if (!ok || id < 0) {
		return false;
	}
	if (entityId) {
		*entityId = id;
	}
	return true;
}

bool parseLevelMapDelta(const QString& value, double* dx, double* dy, double* dz)
{
	const QStringList parts = value.split(QRegularExpression(QStringLiteral(R"([,\s]+)")), Qt::SkipEmptyParts);
	if (parts.size() < 2 || parts.size() > 3) {
		return false;
	}
	bool okX = false;
	bool okY = false;
	bool okZ = true;
	const double parsedX = parts.value(0).toDouble(&okX);
	const double parsedY = parts.value(1).toDouble(&okY);
	const double parsedZ = parts.size() >= 3 ? parts.value(2).toDouble(&okZ) : 0.0;
	if (!okX || !okY || !okZ) {
		return false;
	}
	if (dx) {
		*dx = parsedX;
	}
	if (dy) {
		*dy = parsedY;
	}
	if (dz) {
		*dz = parsedZ;
	}
	return true;
}

QStringList levelMapSetArguments(const QStringList& args)
{
	QStringList sets = optionValues(args, QStringLiteral("--set"));
	sets += optionValues(args, QStringLiteral("--property"));
	return sets;
}

bool splitLevelMapPropertyAssignment(const QString& assignment, QString* key, QString* value)
{
	const int equals = assignment.indexOf('=');
	if (equals <= 0) {
		return false;
	}
	const QString parsedKey = assignment.left(equals).trimmed();
	const QString parsedValue = assignment.mid(equals + 1);
	if (parsedKey.isEmpty()) {
		return false;
	}
	if (key) {
		*key = parsedKey;
	}
	if (value) {
		*value = parsedValue;
	}
	return true;
}

int runMapInspectCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom WAD or Quake-family .map path.").arg(commandName), format);
	}
	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	const QString selector = optionValue(args, QStringLiteral("--select"));
	if (!selector.trimmed().isEmpty() && !selectLevelMapObject(&document, selector, &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to select map object: %1").arg(error), format);
	}
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("map"), levelMapDocumentJson(document));
		printJson(object);
	} else {
		std::cout << text(levelMapReportText(document)) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runMapEditCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom WAD or Quake-family .map path.").arg(commandName), format);
	}
	const QString entityValue = hasOption(args, QStringLiteral("--entity")) ? optionValue(args, QStringLiteral("--entity")) : optionValue(args, QStringLiteral("--select"));
	int entityId = -1;
	if (!parseLevelMapEntityId(entityValue, &entityId)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --entity <id> or --select entity:<id>.").arg(commandName), format);
	}
	const QStringList assignments = levelMapSetArguments(args);
	if (assignments.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires at least one --set key=value assignment.").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	if (!selectLevelMapObject(&document, QStringLiteral("entity:%1").arg(entityId), &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to select entity: %1").arg(error), format);
	}
	for (const QString& assignment : assignments) {
		QString key;
		QString value;
		if (!splitLevelMapPropertyAssignment(assignment, &key, &value)) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Map property assignment must be formatted as key=value: %1").arg(assignment), format);
		}
		if (!setLevelMapEntityProperty(&document, entityId, key, value, &error)) {
			return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to edit entity property: %1").arg(error), format);
		}
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	const CliExitCode code = report.succeeded() ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("map"), levelMapDocumentJson(document));
		object.insert(QStringLiteral("save"), levelMapSaveReportJson(report));
		printJson(object);
	} else {
		std::cout << text(levelMapSaveReportText(report)) << "\n";
		std::cout << text(levelMapReportText(document)) << "\n";
	}
	return exitCodeValue(code);
}

int runMapMoveCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom WAD or Quake-family .map path.").arg(commandName), format);
	}
	const QString selector = hasOption(args, QStringLiteral("--object")) ? optionValue(args, QStringLiteral("--object")) : optionValue(args, QStringLiteral("--select"));
	QString objectKind;
	int objectId = -1;
	if (!parseLevelMapSelector(selector, &objectKind, &objectId)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --object kind:id.").arg(commandName), format);
	}
	double dx = 0.0;
	double dy = 0.0;
	double dz = 0.0;
	if (!parseLevelMapDelta(optionValue(args, QStringLiteral("--delta")), &dx, &dy, &dz)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --delta x,y,z.").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	if (!moveLevelMapObject(&document, objectKind, objectId, dx, dy, dz, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to move map object: %1").arg(error), format);
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	const CliExitCode code = report.succeeded() ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("map"), levelMapDocumentJson(document));
		object.insert(QStringLiteral("save"), levelMapSaveReportJson(report));
		printJson(object);
	} else {
		std::cout << text(levelMapSaveReportText(report)) << "\n";
		std::cout << text(levelMapReportText(document)) << "\n";
	}
	return exitCodeValue(code);
}

int runMapCompilePlanCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom WAD or Quake-family .map path.").arg(commandName), format);
	}
	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	const QString profileId = hasOption(args, QStringLiteral("--compiler-profile")) ? optionValue(args, QStringLiteral("--compiler-profile")) : optionValue(args, QStringLiteral("--profile"));
	CompilerCommandRequest compilerRequest = compilerRequestForLevelMap(document, profileId, optionValue(args, QStringLiteral("--output")));
	compilerRequest.workingDirectory = optionValue(args, QStringLiteral("--working-directory")).trimmed().isEmpty() ? compilerRequest.workingDirectory : optionValue(args, QStringLiteral("--working-directory"));
	compilerRequest.workspaceRootPath = optionValue(args, QStringLiteral("--workspace-root"));
	const QString extraArgs = optionValue(args, QStringLiteral("--extra-args"));
	if (!extraArgs.trimmed().isEmpty()) {
		compilerRequest.extraArguments = QProcess::splitCommand(extraArgs);
	}
	applyCompilerRequestContext(&compilerRequest, args);
	const CompilerCommandPlan plan = buildCompilerCommandPlan(compilerRequest);
	const CliExitCode code = plan.errors.isEmpty() ? CliExitCode::Success : (!plan.profileFound ? CliExitCode::Usage : CliExitCode::NotFound);
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("map"), levelMapDocumentJson(document));
		object.insert(QStringLiteral("plan"), compilerCommandPlanJson(plan));
		printJson(object);
	} else {
		std::cout << text(levelMapReportText(document)) << "\n";
		std::cout << text(compilerCommandPlanText(plan)) << "\n";
	}
	return exitCodeValue(code);
}

QStringList shaderPackagePathsFromArgs(const QStringList& args)
{
	QStringList paths = optionValues(args, QStringLiteral("--package"));
	paths += optionValues(args, QStringLiteral("--mounted-package"));
	QStringList normalized;
	for (const QString& path : paths) {
		const QString trimmed = path.trimmed();
		if (!trimmed.isEmpty() && !normalized.contains(trimmed)) {
			normalized.push_back(trimmed);
		}
	}
	return normalized;
}

int runShaderInspectCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString shaderPath = path.trimmed().isEmpty() ? optionValue(args, QStringLiteral("--input")) : path;
	if (shaderPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a shader script path.").arg(commandName), format);
	}
	ShaderDocument document;
	QString error;
	if (!loadShaderScript(shaderPath, &document, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to load shader script: %1").arg(error), format);
	}
	QStringList validationWarnings;
	const QVector<ShaderReferenceValidation> validation = validateShaderReferences(document, shaderPackagePathsFromArgs(args), &validationWarnings);
	const bool missingReference = std::any_of(validation.cbegin(), validation.cend(), [](const ShaderReferenceValidation& item) {
		return !item.found;
	});
	const CliExitCode code = document.shaders.isEmpty() ? CliExitCode::ValidationFailed : (missingReference && !validation.isEmpty() ? CliExitCode::ValidationFailed : CliExitCode::Success);
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("shader"), advancedShaderDocumentJson(document, validation, validationWarnings));
		printJson(object);
	} else {
		std::cout << text(shaderDocumentReportText(document, validation, validationWarnings)) << "\n";
	}
	return exitCodeValue(code);
}

int runShaderSetStageCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString shaderPath = path.trimmed().isEmpty() ? optionValue(args, QStringLiteral("--input")) : path;
	if (shaderPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a shader script path.").arg(commandName), format);
	}
	const QString shaderName = optionValue(args, QStringLiteral("--shader"));
	const QString directive = optionValue(args, QStringLiteral("--directive"));
	const QString value = optionValue(args, QStringLiteral("--value"));
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	bool stageOk = false;
	const int stageIndex = optionValue(args, QStringLiteral("--stage")).toInt(&stageOk);
	if (shaderName.trimmed().isEmpty() || !stageOk || directive.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --shader <name> --stage <index> --directive <name> --value <text>.").arg(commandName), format);
	}
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	ShaderDocument document;
	QString error;
	if (!loadShaderScript(shaderPath, &document, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to load shader script: %1").arg(error), format);
	}
	if (!setShaderStageDirective(&document, shaderName, stageIndex, directive, value, &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to edit shader stage: %1").arg(error), format);
	}
	const ShaderSaveReport report = saveShaderScriptAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	QStringList validationWarnings;
	const QVector<ShaderReferenceValidation> validation = validateShaderReferences(document, shaderPackagePathsFromArgs(args), &validationWarnings);
	const CliExitCode code = report.succeeded() ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("shader"), advancedShaderDocumentJson(document, validation, validationWarnings));
		object.insert(QStringLiteral("save"), shaderSaveReportJson(report));
		printJson(object);
	} else {
		std::cout << text(shaderSaveReportText(report)) << "\n";
		std::cout << text(shaderDocumentReportText(document, validation, validationWarnings)) << "\n";
	}
	return exitCodeValue(code);
}

int runSpritePlanCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	SpriteWorkflowRequest request;
	request.engineFamily = optionValue(args, QStringLiteral("--engine")).trimmed().isEmpty() ? QStringLiteral("doom") : optionValue(args, QStringLiteral("--engine"));
	request.spriteName = hasOption(args, QStringLiteral("--sprite-name")) ? optionValue(args, QStringLiteral("--sprite-name")) : optionValue(args, QStringLiteral("--name"));
	if (request.spriteName.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --name <sprite-name>.").arg(commandName), format);
	}
	bool framesOk = true;
	bool rotationsOk = true;
	if (hasOption(args, QStringLiteral("--frames"))) {
		request.frameCount = optionValue(args, QStringLiteral("--frames")).toInt(&framesOk);
	}
	if (hasOption(args, QStringLiteral("--rotations"))) {
		request.rotations = optionValue(args, QStringLiteral("--rotations")).toInt(&rotationsOk);
	}
	if (!framesOk || request.frameCount <= 0) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--frames must be a positive integer."), format);
	}
	if (!rotationsOk || request.rotations < 0) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--rotations must be 0 through 8 for Doom workflows."), format);
	}
	request.paletteId = optionValue(args, QStringLiteral("--palette")).trimmed().isEmpty() ? QStringLiteral("generic") : optionValue(args, QStringLiteral("--palette"));
	request.outputPackageRoot = optionValue(args, QStringLiteral("--package-root")).trimmed().isEmpty() ? QStringLiteral("sprites") : optionValue(args, QStringLiteral("--package-root"));
	request.sourceFramePaths = optionValues(args, QStringLiteral("--source-frame"));
	request.stageForPackage = !hasOption(args, QStringLiteral("--no-stage"));
	const SpriteWorkflowPlan plan = buildSpriteWorkflowPlan(request);
	const CliExitCode code = plan.state == OperationState::Failed ? CliExitCode::Failure : CliExitCode::Success;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("sprite"), spriteWorkflowPlanJson(plan));
		printJson(object);
	} else {
		std::cout << text(spriteWorkflowPlanText(plan)) << "\n";
	}
	return exitCodeValue(code);
}

int runCodeIndexCommand(const QString& commandName, const QString& rootPath, const QStringList& args, CliOutputFormat format)
{
	const QString root = rootPath.trimmed().isEmpty() ? optionValue(args, QStringLiteral("--workspace-root")) : rootPath;
	if (root.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a project source root path.").arg(commandName), format);
	}
	CodeWorkspaceIndexRequest request;
	request.rootPath = root;
	request.extensions = assetTextExtensionsFromArgs(args);
	request.symbolQuery = optionValue(args, QStringLiteral("--find"));
	if (hasOption(args, QStringLiteral("--symbol"))) {
		request.symbolQuery = optionValue(args, QStringLiteral("--symbol"));
	}
	const CodeWorkspaceIndex index = indexCodeWorkspace(request);
	const CliExitCode code = index.state == OperationState::Failed ? CliExitCode::Failure : CliExitCode::Success;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("code"), codeWorkspaceIndexJson(index));
		printJson(object);
	} else {
		std::cout << text(codeWorkspaceIndexText(index)) << "\n";
	}
	return exitCodeValue(code);
}

QStringList extensionRootsFromArgs(const QString& positionalRoot, const QStringList& args)
{
	QStringList roots;
	if (!positionalRoot.trimmed().isEmpty()) {
		roots << positionalRoot;
	}
	roots += optionValues(args, QStringLiteral("--extension-root"));
	roots += optionValues(args, QStringLiteral("--root"));
	if (roots.isEmpty()) {
		roots << QDir::currentPath();
	}
	return roots;
}

int runExtensionDiscoverCommand(const QString& commandName, const QString& rootPath, const QStringList& args, CliOutputFormat format)
{
	const ExtensionDiscoveryResult discovery = discoverExtensions(extensionRootsFromArgs(rootPath, args));
	const CliExitCode code = discovery.state == OperationState::Failed ? CliExitCode::Failure : CliExitCode::Success;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("extensions"), extensionDiscoveryJson(discovery));
		printJson(object);
	} else {
		std::cout << text(extensionDiscoveryText(discovery)) << "\n";
	}
	return exitCodeValue(code);
}

int runExtensionInspectCommand(const QString& commandName, const QString& manifestPath, CliOutputFormat format)
{
	if (manifestPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires an extension manifest path.").arg(commandName), format);
	}
	ExtensionManifest manifest;
	QString error;
	if (!loadExtensionManifest(manifestPath, &manifest, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to load extension manifest: %1").arg(error), format);
	}
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("extension"), extensionManifestJson(manifest));
		object.insert(QStringLiteral("trustModel"), stringArrayJson(extensionTrustModelLines()));
		printJson(object);
	} else {
		std::cout << text(extensionManifestText(manifest)) << "\n";
		std::cout << text(extensionTrustModelLines().join('\n')) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runExtensionRunCommand(const QString& commandName, const QString& manifestPath, const QString& commandId, const QStringList& args, CliOutputFormat format)
{
	if (manifestPath.trimmed().isEmpty() || commandId.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a manifest path and command id.").arg(commandName), format);
	}
	ExtensionManifest manifest;
	QString error;
	if (!loadExtensionManifest(manifestPath, &manifest, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to load extension manifest: %1").arg(error), format);
	}
	const bool allowExecution = hasOption(args, QStringLiteral("--execute")) || hasOption(args, QStringLiteral("--allow-execution"));
	const bool dryRun = hasOption(args, QStringLiteral("--dry-run")) || !allowExecution;
	QStringList extraArguments;
	if (hasOption(args, QStringLiteral("--extra-args"))) {
		extraArguments = QProcess::splitCommand(optionValue(args, QStringLiteral("--extra-args")));
	}
	bool timeoutOk = false;
	const int requestedTimeout = optionValue(args, QStringLiteral("--timeout-ms")).toInt(&timeoutOk);
	const ExtensionCommandPlan plan = buildExtensionCommandPlan(manifest, commandId, extraArguments, dryRun, allowExecution);
	const ExtensionCommandResult result = runExtensionCommand(plan, timeoutOk && requestedTimeout > 0 ? requestedTimeout : 30000);
	const CliExitCode code = result.state == OperationState::Failed ? CliExitCode::Failure : CliExitCode::Success;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("extensionCommand"), extensionCommandResultJson(result));
		printJson(object);
	} else {
		std::cout << text(extensionCommandResultText(result)) << "\n";
	}
	return exitCodeValue(code);
}

int runInstallListCommand(const QString& commandName, CliOutputFormat format)
{
	const StudioSettings settings;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("installations"), gameInstallationsJson(settings));
		printJson(object);
	} else {
		printGameInstallations(settings);
	}
	return exitCodeValue(CliExitCode::Success);
}

int runInstallDetectCommand(const QString& commandName, const QStringList& roots, CliOutputFormat format)
{
	const QVector<GameInstallationDetectionCandidate> candidates = detectGameInstallations(roots, roots);
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("detection"), gameInstallationDetectionJson(candidates));
		printJson(object);
	} else {
		printInstallationDetectionCandidates(candidates);
	}
	return exitCodeValue(CliExitCode::Success);
}

int runCompilerListCommand(const QString& commandName, CliOutputFormat format)
{
	const CompilerRegistrySummary summary = discoverCompilerTools(compilerRegistryOptionsFromArgs(QCoreApplication::arguments()));
	const QVector<CompilerToolPathOverride> overrides = StudioSettings().compilerToolPathOverrides();
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("compilerRegistry"), compilerRegistryJson(summary));
		object.insert(QStringLiteral("userExecutableOverrides"), compilerToolPathOverridesJson(overrides));
		printJson(object);
	} else {
		std::cout << text(compilerRegistrySummaryText(summary)) << "\n";
		if (!overrides.isEmpty()) {
			std::cout << "User executable overrides\n";
			for (const CompilerToolPathOverride& override : overrides) {
				std::cout << "- " << text(override.toolId) << ": " << text(nativePath(override.executablePath)) << "\n";
			}
		}
	}
	return exitCodeValue(CliExitCode::Success);
}

int runCompilerProfilesCommand(const QString& commandName, CliOutputFormat format)
{
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("profiles"), compilerProfilesJson());
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	std::cout << "Compiler wrapper profiles\n";
	for (const CompilerProfileDescriptor& profile : compilerProfileDescriptors()) {
		std::cout << "- " << text(profile.id) << "\n";
		std::cout << "  Tool: " << text(profile.toolId) << "\n";
		std::cout << "  Stage: " << text(profile.stageId) << "\n";
		std::cout << "  Engine: " << text(profile.engineFamily) << "\n";
		std::cout << "  Input: " << text(profile.inputDescription) << " (" << text(profile.inputExtensions.isEmpty() ? QStringLiteral("none") : profile.inputExtensions.join(", ")) << ")\n";
		std::cout << "  Input required: " << (profile.inputRequired ? "yes" : "no") << "\n";
		std::cout << "  Output extension: " << text(profile.defaultOutputExtension.isEmpty() ? QStringLiteral("(none)") : profile.defaultOutputExtension) << "\n";
		std::cout << "  " << text(profile.description) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runCompilerSetPathCommand(const QString& commandName, const QString& toolId, const QStringList& args, CliOutputFormat format)
{
	if (toolId.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a compiler tool id.").arg(commandName), format);
	}
	CompilerToolDescriptor descriptor;
	if (!compilerToolDescriptorForId(toolId, &descriptor)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown compiler tool id: %1").arg(toolId), format);
	}
	const QString executablePath = optionValue(args, QStringLiteral("--executable"));
	if (executablePath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --executable <path>.").arg(commandName), format);
	}
	if (!QFileInfo(executablePath).isFile()) {
		return printCliError(commandName, CliExitCode::NotFound, QStringLiteral("Compiler executable not found: %1").arg(nativePath(executablePath)), format);
	}

	StudioSettings settings;
	settings.upsertCompilerToolPathOverride({descriptor.id, QFileInfo(executablePath).absoluteFilePath()});
	settings.sync();
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("userExecutableOverrides"), compilerToolPathOverridesJson(settings.compilerToolPathOverrides()));
		printJson(object);
	} else {
		std::cout << "Compiler executable override saved\n";
		std::cout << text(descriptor.id) << ": " << text(nativePath(QFileInfo(executablePath).absoluteFilePath())) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runCompilerClearPathCommand(const QString& commandName, const QString& toolId, CliOutputFormat format)
{
	if (toolId.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a compiler tool id.").arg(commandName), format);
	}
	StudioSettings settings;
	settings.removeCompilerToolPathOverride(toolId);
	settings.sync();
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("userExecutableOverrides"), compilerToolPathOverridesJson(settings.compilerToolPathOverrides()));
		printJson(object);
	} else {
		std::cout << "Compiler executable override removed: " << text(toolId) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

CliExitCode compilerPlanExitCode(const CompilerCommandPlan& plan)
{
	if (plan.errors.isEmpty()) {
		return CliExitCode::Success;
	}
	if (!plan.profileFound) {
		return CliExitCode::Usage;
	}
	return CliExitCode::NotFound;
}

int runCompilerPlanCommand(const QString& commandName, const QString& profileId, const QString& positionalInputPath, const QStringList& args, CliOutputFormat format, bool manifestMode)
{
	CompilerCommandRequest request;
	request.profileId = profileId;
	request.inputPath = hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : positionalInputPath;
	request.outputPath = optionValue(args, QStringLiteral("--output"));
	request.workingDirectory = optionValue(args, QStringLiteral("--working-directory"));
	request.workspaceRootPath = optionValue(args, QStringLiteral("--workspace-root"));
	request.extraSearchPaths = optionPathList(args, QStringLiteral("--compiler-search-paths"));
	const QString extraArgs = optionValue(args, QStringLiteral("--extra-args"));
	if (!extraArgs.trimmed().isEmpty()) {
		request.extraArguments = QProcess::splitCommand(extraArgs);
	}
	applyCompilerRequestContext(&request, args);

	const CompilerCommandPlan plan = buildCompilerCommandPlan(request);
	const CompilerCommandManifest manifest = compilerCommandManifestFromPlan(plan);
	const QString manifestPath = optionValue(args, QStringLiteral("--manifest"));
	if (!manifestPath.trimmed().isEmpty()) {
		QString error;
		if (!saveCompilerCommandManifest(manifest, manifestPath, &error)) {
			return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Failed to save compiler manifest: %1").arg(error), format);
		}
	}
	const CliExitCode code = compilerPlanExitCode(plan);
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		if (manifestMode) {
			object.insert(QStringLiteral("manifest"), compilerCommandManifestJson(manifest));
		} else {
			object.insert(QStringLiteral("plan"), compilerCommandPlanJson(plan));
			object.insert(QStringLiteral("manifest"), compilerCommandManifestJson(manifest));
		}
		if (!manifestPath.trimmed().isEmpty()) {
			object.insert(QStringLiteral("manifestPath"), QDir::cleanPath(QFileInfo(manifestPath).absoluteFilePath()));
		}
		printJson(object);
	} else {
		std::cout << text(manifestMode ? compilerCommandManifestText(manifest) : compilerCommandPlanText(plan)) << "\n";
		if (!manifestPath.trimmed().isEmpty()) {
			std::cout << "Manifest saved: " << text(nativePath(QFileInfo(manifestPath).absoluteFilePath())) << "\n";
		}
	}
	return exitCodeValue(code);
}

CliExitCode compilerRunExitCode(const CompilerRunResult& result)
{
	if (result.state == OperationState::Completed || result.state == OperationState::Warning) {
		return CliExitCode::Success;
	}
	if (!result.plan.profileFound) {
		return CliExitCode::Usage;
	}
	if (!result.plan.isRunnable() && !result.plan.errors.isEmpty()) {
		return CliExitCode::NotFound;
	}
	return CliExitCode::Failure;
}

int runCompilerRunCommand(const QString& commandName, const QString& profileId, const QString& positionalInputPath, const QStringList& args, CliOutputFormat format)
{
	CompilerRunRequest runRequest;
	runRequest.command.profileId = profileId;
	runRequest.command.inputPath = hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : positionalInputPath;
	runRequest.command.outputPath = optionValue(args, QStringLiteral("--output"));
	runRequest.command.workingDirectory = optionValue(args, QStringLiteral("--working-directory"));
	runRequest.command.workspaceRootPath = optionValue(args, QStringLiteral("--workspace-root"));
	runRequest.command.extraSearchPaths = optionPathList(args, QStringLiteral("--compiler-search-paths"));
	const QString extraArgs = optionValue(args, QStringLiteral("--extra-args"));
	if (!extraArgs.trimmed().isEmpty()) {
		runRequest.command.extraArguments = QProcess::splitCommand(extraArgs);
	}
	applyCompilerRequestContext(&runRequest.command, args);
	runRequest.manifestPath = optionValue(args, QStringLiteral("--manifest"));
	runRequest.dryRun = hasOption(args, QStringLiteral("--dry-run"));
	runRequest.registerOutputs = hasOption(args, QStringLiteral("--register-output")) || hasOption(args, QStringLiteral("--register-outputs"));
	bool timeoutOk = false;
	const int timeoutMs = optionValue(args, QStringLiteral("--timeout-ms")).toInt(&timeoutOk);
	if (timeoutOk && timeoutMs > 0) {
		runRequest.timeoutMs = timeoutMs;
	}

	CompilerRunCallbacks callbacks;
	if (hasOption(args, QStringLiteral("--watch")) && format == CliOutputFormat::Text && !quietOutput()) {
		callbacks.logEntry = [](const CompilerTaskLogEntry& entry) {
			std::cout << "[" << text(entry.timestampUtc.toUTC().toString(Qt::ISODate)) << "] "
				<< text(entry.level) << " " << text(entry.message) << "\n";
		};
	}
	CompilerRunResult result = runCompilerCommand(runRequest, callbacks);
	if (runRequest.registerOutputs && !runRequest.command.workspaceRootPath.trimmed().isEmpty() && !result.registeredOutputPaths.isEmpty()) {
		ProjectManifest manifest;
		QString error;
		if (loadProjectManifest(runRequest.command.workspaceRootPath, &manifest, &error)) {
			const int registered = registerProjectOutputPaths(&manifest, result.registeredOutputPaths);
			if (registered > 0 && saveProjectManifest(manifest, &error)) {
				result.manifest.registeredOutputPaths = result.registeredOutputPaths;
			}
		}
	}

	const CliExitCode code = compilerRunExitCode(result);
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("run"), compilerRunResultJson(result));
		if (hasOption(args, QStringLiteral("--task-state")) || hasOption(args, QStringLiteral("--watch"))) {
			object.insert(QStringLiteral("taskState"), taskStateJson(result.manifest.manifestId, result.state, result.error.isEmpty() ? compilerRunResultText(result).split('\n').value(0) : result.error, result.durationMs, false));
		}
		printJson(object);
	} else {
		std::cout << text(compilerRunResultText(result)) << "\n";
		if (!runRequest.manifestPath.trimmed().isEmpty()) {
			std::cout << "Manifest saved: " << text(nativePath(QFileInfo(runRequest.manifestPath).absoluteFilePath())) << "\n";
		}
	}
	return exitCodeValue(code);
}

int runCompilerRerunCommand(const QString& commandName, const QString& manifestInputPath, const QStringList& args, CliOutputFormat format)
{
	if (manifestInputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a manifest path.").arg(commandName), format);
	}
	CompilerCommandManifest manifest;
	QString error;
	if (!loadCompilerCommandManifest(manifestInputPath, &manifest, &error)) {
		return printCliError(commandName, CliExitCode::NotFound, QStringLiteral("Unable to load compiler manifest: %1").arg(error), format);
	}
	const QString outputManifestPath = optionValue(args, QStringLiteral("--manifest"));
	CompilerRunCallbacks callbacks;
	if (hasOption(args, QStringLiteral("--watch")) && format == CliOutputFormat::Text && !quietOutput()) {
		callbacks.logEntry = [](const CompilerTaskLogEntry& entry) {
			std::cout << "[" << text(entry.timestampUtc.toUTC().toString(Qt::ISODate)) << "] "
				<< text(entry.level) << " " << text(entry.message) << "\n";
		};
	}
	CompilerRunResult result = rerunCompilerCommandManifest(manifest, callbacks, outputManifestPath);
	const CliExitCode code = compilerRunExitCode(result);
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("run"), compilerRunResultJson(result));
		if (hasOption(args, QStringLiteral("--task-state")) || hasOption(args, QStringLiteral("--watch"))) {
			object.insert(QStringLiteral("taskState"), taskStateJson(result.manifest.manifestId, result.state, result.error.isEmpty() ? compilerRunResultText(result).split('\n').value(0) : result.error, result.durationMs, false));
		}
		printJson(object);
	} else {
		std::cout << text(compilerRunResultText(result)) << "\n";
		if (!outputManifestPath.trimmed().isEmpty()) {
			std::cout << "Manifest saved: " << text(nativePath(QFileInfo(outputManifestPath).absoluteFilePath())) << "\n";
		}
	}
	return exitCodeValue(code);
}

int runCompilerCopyCommandCommand(const QString& commandName, const QString& target, const QStringList& args, CliOutputFormat format)
{
	if (target.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a manifest path or compiler profile id.").arg(commandName), format);
	}

	QString commandLine;
	CompilerCommandManifest manifest;
	QString error;
	if (loadCompilerCommandManifest(target, &manifest, &error)) {
		commandLine = manifest.commandLine;
	} else {
		CompilerCommandRequest request;
		request.profileId = target;
		request.inputPath = optionValue(args, QStringLiteral("--input"));
		request.outputPath = optionValue(args, QStringLiteral("--output"));
		request.workingDirectory = optionValue(args, QStringLiteral("--working-directory"));
		request.workspaceRootPath = optionValue(args, QStringLiteral("--workspace-root"));
		const QString extraArgs = optionValue(args, QStringLiteral("--extra-args"));
		if (!extraArgs.trimmed().isEmpty()) {
			request.extraArguments = QProcess::splitCommand(extraArgs);
		}
		applyCompilerRequestContext(&request, args);
		commandLine = buildCompilerCommandPlan(request).commandLine;
	}
	if (commandLine.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("No compiler command line could be produced."), format);
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("commandLine"), commandLine);
		printJson(object);
	} else {
		std::cout << text(commandLine) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runSubcommand(const QStringList& args)
{
	const QStringList tokens = commandTokens(args);
	if (tokens.isEmpty() || tokens.first().startsWith(QStringLiteral("--"))) {
		return -1;
	}

	const CliOutputFormat format = outputFormat(args);
	const QString family = normalizedOptionId(tokens.value(0));
	const QString action = normalizedOptionId(tokens.value(1));
	const QString commandName = action.isEmpty() ? family : QStringLiteral("%1 %2").arg(family, action);

	if (family == QStringLiteral("exit-codes")) {
		printExitCodes(format, QStringLiteral("cli exit-codes"));
		return exitCodeValue(CliExitCode::Success);
	}
	if (family == QStringLiteral("about") || family == QStringLiteral("license")) {
		return runAboutCommand(QStringLiteral("about"), format);
	}
	if (family == QStringLiteral("credits")) {
		if (action == QStringLiteral("validate") || action == QStringLiteral("check")) {
			return runCreditsValidateCommand(QStringLiteral("credits validate"), format);
		}
		return runAboutCommand(QStringLiteral("credits"), format);
	}
	if (family == QStringLiteral("cli") && (action == QStringLiteral("exit-codes") || action == QStringLiteral("codes"))) {
		printExitCodes(format, QStringLiteral("cli exit-codes"));
		return exitCodeValue(CliExitCode::Success);
	}
	if (family == QStringLiteral("cli") && (action == QStringLiteral("commands") || action == QStringLiteral("help"))) {
		return runCliCommandsCommand(QStringLiteral("cli commands"), format);
	}
	if ((family == QStringLiteral("ui") || family == QStringLiteral("shell")) && (action == QStringLiteral("semantics") || action == QStringLiteral("status") || action == QStringLiteral("shortcuts") || action == QStringLiteral("commands"))) {
		return runUiSemanticsCommand(QStringLiteral("ui semantics"), format);
	}

	if (action.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Missing CLI subcommand. Run --cli --help."), format);
	}

	if (family == QStringLiteral("project")) {
		if (action == QStringLiteral("init") || action == QStringLiteral("create")) {
			return runProjectInitCommand(QStringLiteral("project init"), tokens.value(2), args, format);
		}
		if (action == QStringLiteral("info")) {
			return runProjectInfoCommand(QStringLiteral("project info"), tokens.value(2), format);
		}
		if (action == QStringLiteral("validate")) {
			return runProjectValidateCommand(QStringLiteral("project validate"), tokens.value(2), format);
		}
	}

	if (family == QStringLiteral("install") || family == QStringLiteral("installation") || family == QStringLiteral("installations")) {
		if (action == QStringLiteral("list") || action == QStringLiteral("ls")) {
			return runInstallListCommand(QStringLiteral("install list"), format);
		}
		if (action == QStringLiteral("detect") || action == QStringLiteral("discover")) {
			QStringList roots = optionValues(args, QStringLiteral("--root"));
			roots += optionValues(args, QStringLiteral("--detect-install-root"));
			return runInstallDetectCommand(QStringLiteral("install detect"), roots, format);
		}
	}

	if (family == QStringLiteral("package")) {
		if (action == QStringLiteral("info")) {
			return runPackageInfoCommand(QStringLiteral("package info"), tokens.value(2), format);
		}
		if (action == QStringLiteral("list") || action == QStringLiteral("ls")) {
			return runPackageListCommand(QStringLiteral("package list"), tokens.value(2), format);
		}
		if (action == QStringLiteral("preview")) {
			return runPackagePreviewCommand(QStringLiteral("package preview"), tokens.value(2), tokens.value(3), format);
		}
		if (action == QStringLiteral("extract")) {
			const QString outputPath = hasOption(args, QStringLiteral("--output")) ? optionValue(args, QStringLiteral("--output")) : tokens.value(3);
			return runPackageExtractCommand(
				QStringLiteral("package extract"),
				tokens.value(2),
				outputPath,
				packageExtractionEntriesFromArgs(args),
				hasOption(args, QStringLiteral("--extract-all")),
				hasOption(args, QStringLiteral("--dry-run")),
				hasOption(args, QStringLiteral("--overwrite")),
				format);
		}
		if (action == QStringLiteral("validate") || action == QStringLiteral("check")) {
			return runPackageValidateCommand(QStringLiteral("package validate"), tokens.value(2), format);
		}
		if (action == QStringLiteral("stage") || action == QStringLiteral("staging")) {
			return runPackageStageCommand(QStringLiteral("package stage"), tokens.value(2), args, format);
		}
		if (action == QStringLiteral("manifest")) {
			const QString outputPath = hasOption(args, QStringLiteral("--output")) ? optionValue(args, QStringLiteral("--output")) : tokens.value(3);
			return runPackageManifestCommand(QStringLiteral("package manifest"), tokens.value(2), outputPath, args, format);
		}
		if (action == QStringLiteral("save-as") || action == QStringLiteral("write") || action == QStringLiteral("rebuild")) {
			const QString outputPath = hasOption(args, QStringLiteral("--output")) ? optionValue(args, QStringLiteral("--output")) : tokens.value(3);
			return runPackageSaveAsCommand(QStringLiteral("package save-as"), tokens.value(2), outputPath, args, format);
		}
	}

	if (family == QStringLiteral("asset") || family == QStringLiteral("assets")) {
		if (action == QStringLiteral("inspect") || action == QStringLiteral("preview")) {
			return runAssetInspectCommand(QStringLiteral("asset inspect"), tokens.value(2), tokens.value(3), format);
		}
		if (action == QStringLiteral("convert") || action == QStringLiteral("image-convert")) {
			const QString outputPath = hasOption(args, QStringLiteral("--output")) ? optionValue(args, QStringLiteral("--output")) : tokens.value(3);
			return runAssetConvertCommand(QStringLiteral("asset convert"), tokens.value(2), outputPath, args, format);
		}
		if (action == QStringLiteral("audio-wav") || action == QStringLiteral("wav") || action == QStringLiteral("export-wav")) {
			const QString outputPath = hasOption(args, QStringLiteral("--output")) ? optionValue(args, QStringLiteral("--output")) : tokens.value(4);
			return runAssetAudioWavCommand(QStringLiteral("asset audio-wav"), tokens.value(2), tokens.value(3), outputPath, args, format);
		}
		if (action == QStringLiteral("find") || action == QStringLiteral("search")) {
			return runAssetTextCommand(QStringLiteral("asset find"), tokens.value(2), tokens.value(3), QString(), false, args, format);
		}
		if (action == QStringLiteral("replace") || action == QStringLiteral("replace-text")) {
			return runAssetTextCommand(QStringLiteral("asset replace"), tokens.value(2), tokens.value(3), tokens.value(4), true, args, format);
		}
	}

	if (family == QStringLiteral("map") || family == QStringLiteral("maps") || family == QStringLiteral("level")) {
		const QString mapPath = hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : tokens.value(2);
		if (action == QStringLiteral("inspect") || action == QStringLiteral("info") || action == QStringLiteral("preview")) {
			return runMapInspectCommand(QStringLiteral("map inspect"), mapPath, args, format);
		}
		if (action == QStringLiteral("edit") || action == QStringLiteral("set-property")) {
			return runMapEditCommand(QStringLiteral("map edit"), mapPath, args, format);
		}
		if (action == QStringLiteral("move") || action == QStringLiteral("translate")) {
			return runMapMoveCommand(QStringLiteral("map move"), mapPath, args, format);
		}
		if (action == QStringLiteral("compile-plan") || action == QStringLiteral("plan") || action == QStringLiteral("compiler-plan")) {
			return runMapCompilePlanCommand(QStringLiteral("map compile-plan"), mapPath, args, format);
		}
	}

	if (family == QStringLiteral("shader") || family == QStringLiteral("shaders")) {
		const QString shaderPath = hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : tokens.value(2);
		if (action == QStringLiteral("inspect") || action == QStringLiteral("info") || action == QStringLiteral("validate")) {
			return runShaderInspectCommand(QStringLiteral("shader inspect"), shaderPath, args, format);
		}
		if (action == QStringLiteral("set-stage") || action == QStringLiteral("edit-stage") || action == QStringLiteral("edit")) {
			return runShaderSetStageCommand(QStringLiteral("shader set-stage"), shaderPath, args, format);
		}
	}

	if (family == QStringLiteral("sprite") || family == QStringLiteral("sprites")) {
		if (action == QStringLiteral("plan") || action == QStringLiteral("create") || action == QStringLiteral("sequence")) {
			return runSpritePlanCommand(QStringLiteral("sprite plan"), args, format);
		}
	}

	if (family == QStringLiteral("code") || family == QStringLiteral("ide") || family == QStringLiteral("source")) {
		if (action == QStringLiteral("index") || action == QStringLiteral("tree") || action == QStringLiteral("symbols")) {
			return runCodeIndexCommand(QStringLiteral("code index"), tokens.value(2), args, format);
		}
	}

	if (family == QStringLiteral("localization") || family == QStringLiteral("locale") || family == QStringLiteral("translation") || family == QStringLiteral("translations")) {
		if (action == QStringLiteral("targets") || action == QStringLiteral("list")) {
			return runLocalizationReportCommand(QStringLiteral("localization targets"), args, format, true);
		}
		if (action == QStringLiteral("report") || action == QStringLiteral("smoke") || action == QStringLiteral("audit")) {
			return runLocalizationReportCommand(QStringLiteral("localization report"), args, format, false);
		}
	}

	if (family == QStringLiteral("diagnostics") || family == QStringLiteral("diagnostic") || family == QStringLiteral("support")) {
		if (action == QStringLiteral("bundle") || action == QStringLiteral("collect") || action == QStringLiteral("report")) {
			return runDiagnosticsBundleCommand(QStringLiteral("diagnostics bundle"), args, format);
		}
	}

	if (family == QStringLiteral("extension") || family == QStringLiteral("extensions") || family == QStringLiteral("plugin") || family == QStringLiteral("plugins")) {
		if (action == QStringLiteral("discover") || action == QStringLiteral("list") || action == QStringLiteral("ls")) {
			return runExtensionDiscoverCommand(QStringLiteral("extension discover"), tokens.value(2), args, format);
		}
		if (action == QStringLiteral("inspect") || action == QStringLiteral("info")) {
			return runExtensionInspectCommand(QStringLiteral("extension inspect"), tokens.value(2), format);
		}
		if (action == QStringLiteral("run") || action == QStringLiteral("plan")) {
			return runExtensionRunCommand(QStringLiteral("extension run"), tokens.value(2), tokens.value(3), args, format);
		}
	}

	if (family == QStringLiteral("compiler")) {
		if (action == QStringLiteral("list") || action == QStringLiteral("registry")) {
			return runCompilerListCommand(QStringLiteral("compiler list"), format);
		}
		if (action == QStringLiteral("profiles") || action == QStringLiteral("profile-list") || action == QStringLiteral("list-profiles")) {
			return runCompilerProfilesCommand(QStringLiteral("compiler profiles"), format);
		}
		if (action == QStringLiteral("set-path") || action == QStringLiteral("override-path")) {
			return runCompilerSetPathCommand(QStringLiteral("compiler set-path"), tokens.value(2), args, format);
		}
		if (action == QStringLiteral("clear-path") || action == QStringLiteral("remove-path")) {
			return runCompilerClearPathCommand(QStringLiteral("compiler clear-path"), tokens.value(2), format);
		}
		if (action == QStringLiteral("plan")) {
			return runCompilerPlanCommand(QStringLiteral("compiler plan"), tokens.value(2), tokens.value(3), args, format, false);
		}
		if (action == QStringLiteral("manifest")) {
			return runCompilerPlanCommand(QStringLiteral("compiler manifest"), tokens.value(2), tokens.value(3), args, format, true);
		}
		if (action == QStringLiteral("run")) {
			return runCompilerRunCommand(QStringLiteral("compiler run"), tokens.value(2), tokens.value(3), args, format);
		}
		if (action == QStringLiteral("rerun")) {
			return runCompilerRerunCommand(QStringLiteral("compiler rerun"), tokens.value(2), args, format);
		}
		if (action == QStringLiteral("copy-command") || action == QStringLiteral("command-line")) {
			return runCompilerCopyCommandCommand(QStringLiteral("compiler copy-command"), tokens.value(2), args, format);
		}
	}

	if (family == QStringLiteral("editor") || family == QStringLiteral("editors") || family == QStringLiteral("profile") || family == QStringLiteral("profiles")) {
		if (action == QStringLiteral("profiles") || action == QStringLiteral("list") || action == QStringLiteral("ls")) {
			return runEditorProfilesCommand(QStringLiteral("editor profiles"), format);
		}
		if (action == QStringLiteral("current") || action == QStringLiteral("selected")) {
			return runEditorCurrentCommand(QStringLiteral("editor current"), format);
		}
		if (action == QStringLiteral("select") || action == QStringLiteral("set")) {
			return runEditorSelectCommand(QStringLiteral("editor select"), tokens.value(2), format);
		}
	}

	if (family == QStringLiteral("ai") || family == QStringLiteral("automation")) {
		if (action == QStringLiteral("status") || action == QStringLiteral("settings")) {
			return runAiStatusCommand(QStringLiteral("ai status"), format);
		}
		if (action == QStringLiteral("connectors") || action == QStringLiteral("connector-list") || action == QStringLiteral("providers")) {
			return runAiConnectorsCommand(QStringLiteral("ai connectors"), format);
		}
		if (action == QStringLiteral("tools") || action == QStringLiteral("tool-list")) {
			return runAiToolsCommand(QStringLiteral("ai tools"), format);
		}
		if (action == QStringLiteral("explain-log") || action == QStringLiteral("explain")) {
			return runAiExplainLogCommand(QStringLiteral("ai explain-log"), args, format);
		}
		if (action == QStringLiteral("propose-command") || action == QStringLiteral("command")) {
			return runAiProposeCommandCommand(QStringLiteral("ai propose-command"), args, format);
		}
		if (action == QStringLiteral("propose-manifest") || action == QStringLiteral("project-draft") || action == QStringLiteral("manifest")) {
			return runAiProposeManifestCommand(QStringLiteral("ai propose-manifest"), tokens.value(2), args, format);
		}
		if (action == QStringLiteral("package-deps") || action == QStringLiteral("dependencies")) {
			return runAiPackageDepsCommand(QStringLiteral("ai package-deps"), tokens.value(2), args, format);
		}
		if (action == QStringLiteral("cli-command") || action == QStringLiteral("generate-command")) {
			return runAiCliCommandCommand(QStringLiteral("ai cli-command"), args, format);
		}
		if (action == QStringLiteral("fix-plan") || action == QStringLiteral("retry-plan")) {
			return runAiFixPlanCommand(QStringLiteral("ai fix-plan"), args, format);
		}
		if (action == QStringLiteral("asset-request") || action == QStringLiteral("asset")) {
			return runAiAssetRequestCommand(QStringLiteral("ai asset-request"), args, format);
		}
		if (action == QStringLiteral("compare") || action == QStringLiteral("compare-providers")) {
			return runAiCompareCommand(QStringLiteral("ai compare"), args, format);
		}
		if (action == QStringLiteral("shader-scaffold") || action == QStringLiteral("shader") || action == QStringLiteral("prompt-to-shader")) {
			return runAiShaderScaffoldCommand(QStringLiteral("ai shader-scaffold"), args, format);
		}
		if (action == QStringLiteral("entity-snippet") || action == QStringLiteral("entity") || action == QStringLiteral("prompt-to-entity")) {
			return runAiEntitySnippetCommand(QStringLiteral("ai entity-snippet"), args, format);
		}
		if (action == QStringLiteral("package-plan") || action == QStringLiteral("package-validation") || action == QStringLiteral("prompt-to-package-validation")) {
			return runAiPackagePlanCommand(QStringLiteral("ai package-plan"), args, format);
		}
		if (action == QStringLiteral("batch-recipe") || action == QStringLiteral("batch-conversion") || action == QStringLiteral("prompt-to-batch-conversion")) {
			return runAiBatchRecipeCommand(QStringLiteral("ai batch-recipe"), args, format);
		}
		if (action == QStringLiteral("review") || action == QStringLiteral("proposal-review")) {
			return runAiReviewCommand(QStringLiteral("ai review"), args, format);
		}
	}

	return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown VibeStudio CLI subcommand. Run --cli --help."), format);
}

} // namespace

int runImpl(const QStringList& args)
{
	QStringList requestedOptions = args.mid(1);
	requestedOptions.removeAll(QStringLiteral("--cli"));
	requestedOptions.removeAll(QStringLiteral("--json"));
	requestedOptions.removeAll(QStringLiteral("--quiet"));
	requestedOptions.removeAll(QStringLiteral("--verbose"));

	if (hasOption(args, "--version")) {
		std::cout << "VibeStudio " << text(versionString()) << "\n";
		return 0;
	}
	if (hasOption(args, "--about") || hasOption(args, "--credits")) {
		return runAboutCommand(hasOption(args, "--credits") ? QStringLiteral("--credits") : QStringLiteral("--about"), outputFormat(args));
	}
	if (hasOption(args, "--help") || requestedOptions.isEmpty()) {
		printHelp();
		return 0;
	}
	if (hasOption(args, "--exit-codes")) {
		printExitCodes(outputFormat(args), QStringLiteral("cli exit-codes"));
		return 0;
	}
	const int subcommandResult = runSubcommand(args);
	if (subcommandResult >= 0) {
		return subcommandResult;
	}
	if (hasOption(args, "--studio-report")) {
		printStudioReport();
		return 0;
	}
	if (hasOption(args, "--compiler-report")) {
		printCompilerReport();
		return 0;
	}
	if (hasOption(args, "--compiler-registry")) {
		return runCompilerListCommand(QStringLiteral("--compiler-registry"), outputFormat(args));
	}
	if (hasOption(args, "--platform-report")) {
		printPlatformReport();
		return 0;
	}
	if (hasOption(args, "--project-init")) {
		return runProjectInitCommand(QStringLiteral("--project-init"), optionValue(args, QStringLiteral("--project-init")), args, outputFormat(args));
	}
	if (hasOption(args, "--project-info")) {
		return runProjectInfoCommand(QStringLiteral("--project-info"), optionValue(args, QStringLiteral("--project-info")), outputFormat(args));
	}
	if (hasOption(args, "--project-validate")) {
		return runProjectValidateCommand(QStringLiteral("--project-validate"), optionValue(args, QStringLiteral("--project-validate")), outputFormat(args));
	}
	if (hasOption(args, "--operation-states")) {
		printOperationStates();
		return 0;
	}
	if (hasOption(args, "--ui-primitives")) {
		printUiPrimitives();
		return 0;
	}
	if (hasOption(args, "--ui-semantics")) {
		return runUiSemanticsCommand(QStringLiteral("--ui-semantics"), outputFormat(args));
	}
	if (hasOption(args, "--package-formats")) {
		printPackageFormats();
		return 0;
	}
	if (hasOption(args, "--check-package-path")) {
		const QString path = optionValue(args, "--check-package-path");
		if (path.isEmpty()) {
			std::cerr << "--check-package-path requires a virtual package path.\n";
			return 2;
		}
		printPackagePathCheck(path);
		return 0;
	}
	if (hasOption(args, "--info")) {
		return runPackageInfoCommand(QStringLiteral("--info"), optionValue(args, QStringLiteral("--info")), outputFormat(args));
	}
	if (hasOption(args, "--list")) {
		return runPackageListCommand(QStringLiteral("--list"), optionValue(args, QStringLiteral("--list")), outputFormat(args));
	}
	if (hasOption(args, "--preview-package") || hasOption(args, "--preview-entry")) {
		return runPackagePreviewCommand(QStringLiteral("--preview-package"), optionValue(args, QStringLiteral("--preview-package")), optionValue(args, QStringLiteral("--preview-entry")), outputFormat(args));
	}
	if (hasOption(args, "--extract")) {
		const QStringList entries = packageExtractionEntriesFromArgs(args);
		return runPackageExtractCommand(
			QStringLiteral("--extract"),
			optionValue(args, QStringLiteral("--extract")),
			optionValue(args, QStringLiteral("--output")),
			entries,
			hasOption(args, QStringLiteral("--extract-all")) || entries.isEmpty(),
			hasOption(args, QStringLiteral("--dry-run")),
			hasOption(args, QStringLiteral("--overwrite")),
			outputFormat(args));
	}
	if (hasOption(args, "--validate-package")) {
		return runPackageValidateCommand(QStringLiteral("--validate-package"), optionValue(args, QStringLiteral("--validate-package")), outputFormat(args));
	}
	if (hasOption(args, "--setup-start") || hasOption(args, "--setup-step") || hasOption(args, "--setup-next") || hasOption(args, "--setup-skip") || hasOption(args, "--setup-complete") || hasOption(args, "--setup-reset")) {
		StudioSettings settings;
		if (hasOption(args, "--setup-reset")) {
			settings.resetSetup();
		}
		if (hasOption(args, "--setup-start")) {
			settings.startOrResumeSetup(settings.setupProgress().currentStep);
		}
		if (hasOption(args, "--setup-step")) {
			const QString stepId = optionValue(args, "--setup-step");
			if (stepId.isEmpty() || setupStepId(setupStepFromId(stepId)) != normalizedOptionId(stepId)) {
				QStringList ids;
				for (SetupStep step : setupSteps()) {
					ids.push_back(setupStepId(step));
				}
				std::cerr << "--setup-step requires one of: " << text(ids.join(", ")) << "\n";
				return 2;
			}
			settings.startOrResumeSetup(setupStepFromId(stepId));
		}
		if (hasOption(args, "--setup-next")) {
			settings.advanceSetup();
		}
		if (hasOption(args, "--setup-skip")) {
			settings.skipSetup();
		}
		if (hasOption(args, "--setup-complete")) {
			settings.completeSetup();
		}
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save setup progress: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		printSetupSummary(settings.setupSummary());
		return 0;
	}
	if (hasOption(args, "--set-locale") || hasOption(args, "--set-theme") || hasOption(args, "--set-text-scale") || hasOption(args, "--set-density") || hasOption(args, "--set-editor-profile") || hasOption(args, "--set-reduced-motion") || hasOption(args, "--set-tts")) {
		StudioSettings settings;
		AccessibilityPreferences preferences = settings.accessibilityPreferences();
		QString editorProfileId = settings.selectedEditorProfileId();

		if (hasOption(args, "--set-locale")) {
			const QString value = optionValue(args, "--set-locale");
			if (!localeOptionIsSupported(value)) {
				std::cerr << "--set-locale requires one of: " << text(supportedLocaleNames().join(", ")) << "\n";
				return 2;
			}
			preferences.localeName = normalizedLocaleName(value);
		}
		if (hasOption(args, "--set-theme")) {
			const QString value = normalizedOptionId(optionValue(args, "--set-theme"));
			if (!themeIds().contains(value)) {
				std::cerr << "--set-theme requires one of: " << text(themeIds().join(", ")) << "\n";
				return 2;
			}
			preferences.theme = themeFromId(value);
		}
		if (hasOption(args, "--set-text-scale")) {
			bool ok = false;
			const int value = optionValue(args, "--set-text-scale").toInt(&ok);
			if (!ok || value < StudioSettings::kMinimumTextScalePercent || value > StudioSettings::kMaximumTextScalePercent) {
				std::cerr << "--set-text-scale requires a value from 100 to 200.\n";
				return 2;
			}
			preferences.textScalePercent = value;
		}
		if (hasOption(args, "--set-density")) {
			const QString value = normalizedOptionId(optionValue(args, "--set-density"));
			if (!densityIds().contains(value)) {
				std::cerr << "--set-density requires one of: " << text(densityIds().join(", ")) << "\n";
				return 2;
			}
			preferences.density = densityFromId(value);
		}
		if (hasOption(args, "--set-editor-profile")) {
			EditorProfileDescriptor profile;
			if (!editorProfileForId(optionValue(args, "--set-editor-profile"), &profile)) {
				std::cerr << "--set-editor-profile requires one of: " << text(editorProfileIds().join(", ")) << "\n";
				return 2;
			}
			editorProfileId = profile.id;
		}
		if (hasOption(args, "--set-reduced-motion")) {
			bool value = false;
			if (!boolOptionValue(optionValue(args, "--set-reduced-motion"), &value)) {
				std::cerr << "--set-reduced-motion requires on or off.\n";
				return 2;
			}
			preferences.reducedMotion = value;
		}
		if (hasOption(args, "--set-tts")) {
			bool value = false;
			if (!boolOptionValue(optionValue(args, "--set-tts"), &value)) {
				std::cerr << "--set-tts requires on or off.\n";
				return 2;
			}
			preferences.textToSpeechEnabled = value;
		}

		settings.setAccessibilityPreferences(preferences);
		settings.setSelectedEditorProfileId(editorProfileId);
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save preferences: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		std::cout << "Preferences saved.\n";
		printPreferences(settings.accessibilityPreferences(), settings.selectedEditorProfileId());
		return 0;
	}
	if (hasOption(args, "--settings-report")) {
		printSettingsReport();
		return 0;
	}
	if (hasOption(args, "--setup-report")) {
		const StudioSettings settings;
		printSetupSummary(settings.setupSummary());
		return 0;
	}
	if (hasOption(args, "--preferences-report")) {
		const StudioSettings settings;
		printPreferences(settings.accessibilityPreferences(), settings.selectedEditorProfileId());
		return 0;
	}
	if (hasOption(args, "--localization-report")) {
		return runLocalizationReportCommand(QStringLiteral("--localization-report"), args, outputFormat(args), false);
	}
	if (hasOption(args, "--editor-profiles")) {
		return runEditorProfilesCommand(QStringLiteral("--editor-profiles"), outputFormat(args));
	}
	if (hasOption(args, "--ai-status")) {
		return runAiStatusCommand(QStringLiteral("--ai-status"), outputFormat(args));
	}
	if (hasOption(args, "--set-ai-free") || hasOption(args, "--set-ai-cloud") || hasOption(args, "--set-ai-agentic") || hasOption(args, "--set-ai-reasoning") || hasOption(args, "--set-ai-text-model")) {
		StudioSettings settings;
		AiAutomationPreferences preferences = settings.aiAutomationPreferences();

		if (hasOption(args, "--set-ai-free")) {
			bool value = true;
			if (!boolOptionValue(optionValue(args, "--set-ai-free"), &value)) {
				std::cerr << "--set-ai-free requires on or off.\n";
				return 2;
			}
			preferences.aiFreeMode = value;
		}
		if (hasOption(args, "--set-ai-cloud")) {
			bool value = false;
			if (!boolOptionValue(optionValue(args, "--set-ai-cloud"), &value)) {
				std::cerr << "--set-ai-cloud requires on or off.\n";
				return 2;
			}
			preferences.cloudConnectorsEnabled = value;
			if (value) {
				preferences.aiFreeMode = false;
			}
		}
		if (hasOption(args, "--set-ai-agentic")) {
			bool value = false;
			if (!boolOptionValue(optionValue(args, "--set-ai-agentic"), &value)) {
				std::cerr << "--set-ai-agentic requires on or off.\n";
				return 2;
			}
			preferences.agenticWorkflowsEnabled = value;
			if (value) {
				preferences.aiFreeMode = false;
				preferences.cloudConnectorsEnabled = true;
			}
		}
		if (hasOption(args, "--set-ai-reasoning")) {
			AiConnectorDescriptor connector;
			const QString connectorId = optionValue(args, "--set-ai-reasoning");
			if (!connectorId.isEmpty() && (!aiConnectorForId(connectorId, &connector) || !connector.capabilities.contains(QStringLiteral("reasoning")))) {
				std::cerr << "--set-ai-reasoning requires a connector with reasoning capability. Known: " << text(aiConnectorIds().join(", ")) << "\n";
				return 2;
			}
			preferences.preferredReasoningConnectorId = connectorId;
		}
		if (hasOption(args, "--set-ai-text-model")) {
			AiModelDescriptor model;
			const QString modelId = optionValue(args, "--set-ai-text-model");
			if (!modelId.isEmpty() && (!aiModelForId(modelId, &model) || !model.capabilities.contains(QStringLiteral("reasoning")))) {
				std::cerr << "--set-ai-text-model requires a reasoning-capable model. Known: " << text(aiModelIds().join(", ")) << "\n";
				return 2;
			}
			preferences.preferredTextModelId = modelId;
		}

		settings.setAiAutomationPreferences(preferences);
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save AI preferences: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		std::cout << "AI preferences saved.\n";
		std::cout << text(aiAutomationPreferencesText(settings.aiAutomationPreferences())) << "\n";
		return 0;
	}
	if (hasOption(args, "--installations-report")) {
		return runInstallListCommand(QStringLiteral("--installations-report"), outputFormat(args));
	}
	if (hasOption(args, "--detect-installations")) {
		return runInstallDetectCommand(QStringLiteral("--detect-installations"), optionValues(args, QStringLiteral("--detect-install-root")), outputFormat(args));
	}
	if (hasOption(args, "--add-installation")) {
		const QString rootPath = optionValue(args, "--add-installation");
		if (rootPath.isEmpty()) {
			std::cerr << "--add-installation requires an installation root path.\n";
			return 2;
		}

		GameInstallationProfile profile;
		profile.rootPath = rootPath;
		profile.gameKey = optionValue(args, "--install-game");
		profile.displayName = optionValue(args, "--install-name");
		profile.engineFamily = hasOption(args, "--install-engine") ? gameEngineFamilyFromId(optionValue(args, "--install-engine")) : GameEngineFamily::Unknown;
		profile.executablePath = optionValue(args, "--install-executable");
		profile.basePackagePaths = optionPathList(args, "--install-base-packages");
		profile.modPackagePaths = optionPathList(args, "--install-mod-packages");
		profile.paletteId = optionValue(args, "--install-palette");
		profile.compilerProfileId = optionValue(args, "--install-compiler-profile");
		if (hasOption(args, "--install-hidden")) {
			bool hidden = false;
			if (!boolOptionValue(optionValue(args, "--install-hidden"), &hidden)) {
				std::cerr << "--install-hidden requires on or off.\n";
				return 2;
			}
			profile.hidden = hidden;
		}
		if (hasOption(args, "--install-read-only")) {
			bool readOnly = true;
			if (!boolOptionValue(optionValue(args, "--install-read-only"), &readOnly)) {
				std::cerr << "--install-read-only requires on or off.\n";
				return 2;
			}
			profile.readOnly = readOnly;
		}
		profile = normalizedGameInstallationProfile(profile);

		StudioSettings settings;
		settings.upsertGameInstallation(profile);
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save installation profile: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		std::cout << "Saved game installation: " << text(profile.id) << "\n";
		printGameInstallationProfile(profile, settings.selectedGameInstallationId());
		printInstallationValidation(profile);
		return 0;
	}
	if (hasOption(args, "--select-installation")) {
		const QString id = optionValue(args, "--select-installation");
		if (id.isEmpty()) {
			std::cerr << "--select-installation requires an installation profile id.\n";
			return 2;
		}
		StudioSettings settings;
		settings.setSelectedGameInstallation(id);
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save selected installation: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		if (!sameGameInstallationId(settings.selectedGameInstallationId(), id)) {
			std::cerr << "Installation profile not found: " << text(id) << "\n";
			return 1;
		}
		std::cout << "Selected game installation: " << text(settings.selectedGameInstallationId()) << "\n";
		return 0;
	}
	if (hasOption(args, "--validate-installation")) {
		const QString id = optionValue(args, "--validate-installation");
		if (id.isEmpty()) {
			std::cerr << "--validate-installation requires an installation profile id.\n";
			return 2;
		}
		const StudioSettings settings;
		const QVector<GameInstallationProfile> profiles = settings.gameInstallations();
		const GameInstallationProfile* profile = findInstallationById(profiles, id);
		if (!profile) {
			std::cerr << "Installation profile not found: " << text(id) << "\n";
			return 1;
		}
		std::cout << "Game installation validation\n";
		printGameInstallationProfile(*profile, settings.selectedGameInstallationId());
		printInstallationValidation(*profile);
		return validateGameInstallationProfile(*profile).isUsable() ? 0 : 1;
	}
	if (hasOption(args, "--remove-installation")) {
		const QString id = optionValue(args, "--remove-installation");
		if (id.isEmpty()) {
			std::cerr << "--remove-installation requires an installation profile id.\n";
			return 2;
		}
		StudioSettings settings;
		const QVector<GameInstallationProfile> profiles = settings.gameInstallations();
		if (!findInstallationById(profiles, id)) {
			std::cerr << "Installation profile not found: " << text(id) << "\n";
			return 1;
		}
		settings.removeGameInstallation(id);
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save installation profiles: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		std::cout << "Removed game installation: " << text(id) << "\n";
		return 0;
	}
	if (hasOption(args, "--recent-projects")) {
		const StudioSettings settings;
		printRecentProjects(settings);
		return 0;
	}
	if (hasOption(args, "--add-recent-project")) {
		const QString path = optionValue(args, "--add-recent-project");
		if (path.isEmpty()) {
			std::cerr << "--add-recent-project requires a path.\n";
			return 2;
		}

		StudioSettings settings;
		settings.recordRecentProject(path);
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save settings: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		std::cout << "Remembered recent project: " << text(nativePath(normalizedProjectPath(path))) << "\n";
		return 0;
	}
	if (hasOption(args, "--remove-recent-project")) {
		const QString path = optionValue(args, "--remove-recent-project");
		if (path.isEmpty()) {
			std::cerr << "--remove-recent-project requires a path.\n";
			return 2;
		}

		StudioSettings settings;
		settings.removeRecentProject(path);
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save settings: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		std::cout << "Forgot recent project: " << text(nativePath(normalizedProjectPath(path))) << "\n";
		return 0;
	}
	if (hasOption(args, "--clear-recent-projects")) {
		StudioSettings settings;
		settings.clearRecentProjects();
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save settings: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		std::cout << "Recent projects cleared.\n";
		return 0;
	}

	std::cerr << "Unknown VibeStudio CLI option. Run --cli --help.\n";
	return 2;
}

int run(const QStringList& args)
{
	currentCliArgs() = args;
	QElapsedTimer timer;
	timer.start();

	NullOutputBuffer nullBuffer;
	std::streambuf* originalOutput = nullptr;
	if (quietOutput()) {
		originalOutput = std::cout.rdbuf(&nullBuffer);
	}

	const int result = runImpl(args);

	if (originalOutput) {
		std::cout.rdbuf(originalOutput);
	}
	if (verboseOutput()) {
		std::cerr << "vibestudio: command completed with exit code " << result << " in " << timer.elapsed() << " ms\n";
	}
	currentCliArgs().clear();
	return result;
}

} // namespace vibestudio::cli
