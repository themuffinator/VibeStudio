#include "cli/cli.h"

#include "app/ui_primitives.h"
#include "core/ai_connectors.h"
#include "core/ai_workflows.h"
#include "core/compiler_profiles.h"
#include "core/compiler_registry.h"
#include "core/compiler_runner.h"
#include "core/editor_profiles.h"
#include "core/operation_state.h"
#include "core/package_archive.h"
#include "core/package_preview.h"
#include "core/project_manifest.h"
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
		{QStringLiteral("project"), QStringLiteral("init"), QStringLiteral("Create or refresh a project manifest."), {QStringLiteral("vibestudio --cli project init C:\\Games\\Quake\\mymod"), QStringLiteral("vibestudio --cli project init ./mymod --project-ai-free on")}},
		{QStringLiteral("project"), QStringLiteral("info"), QStringLiteral("Print project manifest and health summary."), {QStringLiteral("vibestudio --cli project info ./mymod --json")}},
		{QStringLiteral("project"), QStringLiteral("validate"), QStringLiteral("Validate project health and return validation-failed for blocking issues."), {QStringLiteral("vibestudio --cli project validate ./mymod")}},
		{QStringLiteral("package"), QStringLiteral("info"), QStringLiteral("Print package/archive summary."), {QStringLiteral("vibestudio --cli package info ./id1/pak0.pak")}},
		{QStringLiteral("package"), QStringLiteral("list"), QStringLiteral("List package entries."), {QStringLiteral("vibestudio --cli package list ./baseq3/pak0.pk3 --json")}},
		{QStringLiteral("package"), QStringLiteral("preview"), QStringLiteral("Preview a package entry."), {QStringLiteral("vibestudio --cli package preview ./pak0.pak maps/start.bsp")}},
		{QStringLiteral("package"), QStringLiteral("extract"), QStringLiteral("Extract staged package entries with dry-run support."), {QStringLiteral("vibestudio --cli package extract ./pak0.pak --output ./out --entry maps/start.bsp --dry-run")}, true, true, true},
		{QStringLiteral("package"), QStringLiteral("validate"), QStringLiteral("Validate package loading."), {QStringLiteral("vibestudio --cli package validate ./pak0.pak")}},
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
		{QStringLiteral("credits"), QStringLiteral("validate"), QStringLiteral("Validate README and docs credits coverage for imported lineage."), {QStringLiteral("vibestudio --cli credits validate --json")}},
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
		if (token == QStringLiteral("--cli") || token == QStringLiteral("--json") || token == QStringLiteral("--quiet") || token == QStringLiteral("--verbose") || token == QStringLiteral("--dry-run") || token == QStringLiteral("--watch") || token == QStringLiteral("--task-state")) {
			continue;
		}
		if (token == QStringLiteral("--installation") || token == QStringLiteral("--project-installation") || token == QStringLiteral("--set-editor-profile") || token == QStringLiteral("--set-ai-free") || token == QStringLiteral("--set-ai-cloud") || token == QStringLiteral("--set-ai-agentic") || token == QStringLiteral("--set-ai-reasoning") || token == QStringLiteral("--set-ai-text-model") || token == QStringLiteral("--provider") || token == QStringLiteral("--provider-a") || token == QStringLiteral("--provider-b") || token == QStringLiteral("--model") || token == QStringLiteral("--model-a") || token == QStringLiteral("--model-b") || token == QStringLiteral("--prompt") || token == QStringLiteral("--text") || token == QStringLiteral("--log") || token == QStringLiteral("--command") || token == QStringLiteral("--kind") || token == QStringLiteral("--name") || token == QStringLiteral("--manifest") || token == QStringLiteral("--workspace-root")) {
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
	std::cout << "  --dry-run           Report staged package writes or compiler command plans without touching the file system.\n";
	std::cout << "  --watch             Stream compiler task log entries while a long-running command is active.\n";
	std::cout << "  --task-state        Include machine-readable task state objects in JSON output where supported.\n";
	std::cout << "  --overwrite         Allow package extraction to replace existing output files.\n";
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
	std::cout << "  --set-locale <code> Set the preferred UI locale. Supported: " << text(supportedLocaleNames().join(", ")) << "\n";
	std::cout << "  --set-theme <id>    Set theme: " << text(themeIds().join(", ")) << "\n";
	std::cout << "  --set-text-scale <percent>\n";
	std::cout << "                      Set text scale from 100 to 200.\n";
	std::cout << "  --set-density <id>  Set density: " << text(densityIds().join(", ")) << "\n";
	std::cout << "  --editor-profiles   Print placeholder editor interaction profiles.\n";
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
	std::cout << "  credits validate    Validate README and docs credits coverage.\n";
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
	std::cout << "  editor profiles     Print placeholder editor interaction profiles.\n";
	std::cout << "  editor current      Print the selected editor profile.\n";
	std::cout << "  editor select <id>  Select an editor profile globally.\n";
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
	std::cout << "VibeStudio planned modules\n";
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
	object.insert(QStringLiteral("title"), preview.title);
	object.insert(QStringLiteral("summary"), preview.summary);
	object.insert(QStringLiteral("body"), preview.body);
	object.insert(QStringLiteral("details"), stringArrayJson(preview.detailLines));
	object.insert(QStringLiteral("raw"), stringArrayJson(preview.rawLines));
	object.insert(QStringLiteral("truncated"), preview.truncated);
	object.insert(QStringLiteral("bytesRead"), static_cast<double>(preview.bytesRead));
	object.insert(QStringLiteral("totalBytes"), static_cast<double>(preview.totalBytes));
	object.insert(QStringLiteral("error"), preview.error);
	object.insert(QStringLiteral("imageFormat"), preview.imageFormat);
	if (preview.imageSize.isValid()) {
		QJsonObject size;
		size.insert(QStringLiteral("width"), preview.imageSize.width());
		size.insert(QStringLiteral("height"), preview.imageSize.height());
		object.insert(QStringLiteral("imageSize"), size);
	}
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

bool loadPackageForCliQuiet(const QString& path, PackageArchive* archive, QString* error)
{
	QString localError;
	if (!archive || !archive->load(path, &localError)) {
		if (error) {
			*error = localError.isEmpty() ? QStringLiteral("unknown error") : localError;
		}
		return false;
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

bool printPackagePreview(const PackageArchive& archive, const QString& virtualPath)
{
	const PackagePreview preview = buildPackageEntryPreview(archive, virtualPath);
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

struct CreditsValidationCheck {
	QString id;
	QString path;
	QString requiredText;
	bool passed = false;
	QString message;
};

QString fileText(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return {};
	}
	return QString::fromUtf8(file.readAll());
}

QJsonObject creditsValidationCheckJson(const CreditsValidationCheck& check)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), check.id);
	object.insert(QStringLiteral("path"), check.path);
	object.insert(QStringLiteral("requiredText"), check.requiredText);
	object.insert(QStringLiteral("passed"), check.passed);
	object.insert(QStringLiteral("message"), check.message);
	return object;
}

int runCreditsValidateCommand(const QString& commandName, CliOutputFormat format)
{
	const QString readmePath = QDir::current().absoluteFilePath(QStringLiteral("README.md"));
	const QString creditsPath = QDir::current().absoluteFilePath(QStringLiteral("docs/CREDITS.md"));
	const QString readme = fileText(readmePath);
	const QString credits = fileText(creditsPath);
	QVector<CreditsValidationCheck> checks = {
		{QStringLiteral("readme-exists"), readmePath, QStringLiteral("README.md"), !readme.isEmpty(), readme.isEmpty() ? QStringLiteral("README.md could not be read.") : QStringLiteral("README.md is readable.")},
		{QStringLiteral("credits-exists"), creditsPath, QStringLiteral("docs/CREDITS.md"), !credits.isEmpty(), credits.isEmpty() ? QStringLiteral("docs/CREDITS.md could not be read.") : QStringLiteral("docs/CREDITS.md is readable.")},
		{QStringLiteral("readme-credits-section"), readmePath, QStringLiteral("Credits"), readme.contains(QStringLiteral("Credits"), Qt::CaseInsensitive), QStringLiteral("README Credits section should stay visible.")},
		{QStringLiteral("docs-pakfu"), creditsPath, QStringLiteral("PakFu"), credits.contains(QStringLiteral("PakFu"), Qt::CaseInsensitive), QStringLiteral("PakFu lineage must be credited.")},
		{QStringLiteral("docs-ericw-tools"), creditsPath, QStringLiteral("ericw-tools"), credits.contains(QStringLiteral("ericw-tools"), Qt::CaseInsensitive), QStringLiteral("ericw-tools compiler integration must be credited.")},
		{QStringLiteral("docs-netradiant"), creditsPath, QStringLiteral("NetRadiant"), credits.contains(QStringLiteral("NetRadiant"), Qt::CaseInsensitive), QStringLiteral("NetRadiant/q3map2 lineage must be credited.")},
		{QStringLiteral("docs-zdbsp"), creditsPath, QStringLiteral("ZDBSP"), credits.contains(QStringLiteral("ZDBSP"), Qt::CaseInsensitive), QStringLiteral("ZDBSP compiler integration must be credited.")},
		{QStringLiteral("docs-zokumbsp"), creditsPath, QStringLiteral("ZokumBSP"), credits.contains(QStringLiteral("ZokumBSP"), Qt::CaseInsensitive), QStringLiteral("ZokumBSP compiler integration must be credited.")},
	};
	bool passed = true;
	for (const CreditsValidationCheck& check : checks) {
		passed = passed && check.passed;
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
		printJson(object);
		return exitCodeValue(code);
	}

	std::cout << "Credits validation\n";
	for (const CreditsValidationCheck& check : checks) {
		std::cout << "- " << (check.passed ? "ok" : "missing") << " " << text(check.id) << "\n";
		std::cout << "  " << text(check.message) << "\n";
	}
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
	if (!loadPackageForCliQuiet(path, &archive, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to open package: %1").arg(error), format);
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("package"), packageInfoJson(archive));
		printJson(object);
	} else {
		printPackageInfo(archive);
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
	if (!loadPackageForCliQuiet(path, &archive, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to open package: %1").arg(error), format);
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("package"), packageListJson(archive));
		printJson(object);
	} else {
		printPackageList(archive);
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
	if (!loadPackageForCliQuiet(packagePath, &archive, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to open package: %1").arg(error), format);
	}

	const PackagePreview preview = buildPackageEntryPreview(archive, entryPath);
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, preview.kind == PackagePreviewKind::Unavailable ? CliExitCode::NotFound : CliExitCode::Success);
		object.insert(QStringLiteral("packagePath"), packagePath);
		object.insert(QStringLiteral("preview"), packagePreviewJson(preview));
		printJson(object);
		return preview.kind == PackagePreviewKind::Unavailable ? exitCodeValue(CliExitCode::NotFound) : exitCodeValue(CliExitCode::Success);
	}

	return printPackagePreview(archive, entryPath) ? exitCodeValue(CliExitCode::Success) : exitCodeValue(CliExitCode::Failure);
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
