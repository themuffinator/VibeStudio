#include "cli/level_build_workspace.h"
#include "core/compiler_registry.h"
#include "core/level_build_workspace.h"
#include "core/package_draft.h"
#include "core/studio_settings.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QSet>

namespace vibestudio::cli {
LevelBuildCliResult runLevelBuildWorkspaceCommand(const QStringList& arguments) {
	const auto fail = [](int code, const QString& message) { return LevelBuildCliResult{code, message, {}, {}}; };
	const QSet<QString> globals{"--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run"};
	const QSet<QString> options{"--package",  "--output", "--name",		  "--engine",	  "--target",		"--max-bytes",
								"--pipeline", "--tool",	  "--timeout-ms", "--stage-args", "--disable-stage"};
	QSet<QString> seen;
	QHash<QString, QStringList> values;
	QStringList positional;
	for (qsizetype at = 1; at < arguments.size(); ++at) {
		const auto word = arguments[at];
		if (!word.startsWith('-')) {
			positional << word;
			continue;
		}
		const auto equal = word.indexOf('=');
		const auto name = equal < 0 ? word : word.left(equal);
		if (seen.contains(name) && name != "--stage-args" && name != "--disable-stage" && name != "--tool") {
			return fail(2, QCoreApplication::translate("LevelBuildCli", "Repeated option: %1").arg(name));
		}
		seen.insert(name);
		if (flags.contains(name) && equal < 0) {
			continue;
		}
		if (!options.contains(name) && !globals.contains(name)) {
			return fail(2, QCoreApplication::translate("LevelBuildCli", "Unexpected option: %1").arg(word));
		}
		QString value;
		if (equal >= 0) {
			value = word.mid(equal + 1);
		} else if (at + 1 < arguments.size() && !arguments[at + 1].startsWith('-')) {
			value = arguments[++at];
		}
		if (value.trimmed().isEmpty()) {
			return fail(2, QCoreApplication::translate("LevelBuildCli", "Missing value for %1.").arg(name));
		}
		values[name] << value;
	}
	if (positional.size() != 3 || (positional[1] != "prepare" && positional[1] != "run-prepared")) {
		return fail(2, QCoreApplication::translate(
						   "LevelBuildCli",
						   "Expected build prepare <map> --package <assets> --output <new-directory>, or build run-prepared <directory>."));
	}
	const bool prepare = positional[1] == "prepare";
	const QSet<QString> allowed = prepare ? QSet<QString>{"--package", "--output", "--name", "--engine", "--target", "--max-bytes"}
										  : QSet<QString>{"--pipeline", "--tool", "--timeout-ms", "--stage-args", "--disable-stage"};
	for (const auto& name : seen) {
		if (!flags.contains(name) && !globals.contains(name) && !allowed.contains(name)) {
			return fail(2, QCoreApplication::translate("LevelBuildCli", "Option %1 does not apply to this command.").arg(name));
		}
	}
	const auto value = [&](const QString& name, const QString& fallback = QString()) { return values.value(name).value(0, fallback); };
	const bool dry = seen.contains("--dry-run");
	if (!dry && StudioSettings().isReadOnly()) {
		return fail(1, QCoreApplication::translate("LevelBuildCli", "The settings store is read-only."));
	}
	if (prepare) {
		if (!seen.contains("--package") || !seen.contains("--output")) {
			return fail(2, QCoreApplication::translate("LevelBuildCli", "Build preparation requires --package and --output."));
		}
		LevelBuildWorkspaceRequest request;
		request.directory = value("--output");
		request.target = value("--target");
		request.dryRun = dry;
		request.mapName = value("--name", QFileInfo(positional[2]).completeBaseName());
		if (seen.contains("--max-bytes")) {
			bool valid = false;
			request.maximumBytes = value("--max-bytes").toULongLong(&valid);
			if (!valid || request.maximumBytes == 0) {
				return fail(2, QCoreApplication::translate("LevelBuildCli", "--max-bytes requires a positive whole number."));
			}
		}
		LevelMapDocument map;
		QString error;
		if (!loadLevelMap(
				{positional[2], {}, value("--engine", (request.target == "quake" || request.target == "quake2") ? "idTech2" : "idTech3")},
				&map, &error)) {
			return fail(3, error);
		}
		std::shared_ptr<const PackageArchiveReader> reader;
		const auto package = value("--package");
		if (QFileInfo(package).suffix().compare("vibepackage", Qt::CaseInsensitive) == 0) {
			PackageStagingModel staging;
			if (!PackageDraft::load(package, &staging, &error)) {
				return fail(3, error);
			}
			reader = std::make_shared<PackageStagingArchive>(staging);
		} else {
			auto archive = std::make_shared<PackageArchive>();
			if (!archive->load(package, &error)) {
				return fail(3, error);
			}
			reader = std::move(archive);
		}
		const auto result = prepareLevelBuildWorkspace(map, *reader, request);
		auto payload = levelBuildWorkspaceJson(result);
		payload.insert("directory", result.directory);
		payload.insert("inputPath", result.inputPath());
		payload.insert("manifestPath", result.manifestPath());
		payload.insert("prepared", result.ready);
		payload.insert("dryRun", result.dryRun);
		return {result.ready ? 0 : 4, result.error, {{"workspace", payload}}, {levelBuildWorkspaceText(result)}};
	}
	const auto workspace = readLevelBuildWorkspace(positional[2]);
	if (!workspace.ready) {
		return fail(4, workspace.error);
	}
	BuildPipelineRequest request;
	request.pipelineId = value("--pipeline", workspace.defaultPipeline());
	request.dryRun = dry;
	request.registerOutputs = true;
	QSet<QString> seenTools;
	for (const auto& tool : values.value("--tool")) {
		const auto equal = tool.indexOf('=');
		const auto id = tool.left(equal), executable = tool.mid(equal + 1);
		const auto allowedTools =
			workspace.target == QStringLiteral("quake3") ? QStringList{"vibemap3"} : QStringList{"vibemap2-bsp", "vibemap2-vis", "vibemap2-light"};
		if (equal >= 1 && allowedTools.contains(renamedCompilerId(id))) {
			return fail(2, QCoreApplication::translate("LevelBuildCli", "--tool %1 was renamed %2 when VibeStudio moved to VibeMap2 and VibeMap3.").arg(id, renamedCompilerId(id)));
		}
		if (equal < 1 || !allowedTools.contains(id) || executable.trimmed().isEmpty() || seenTools.contains(id)) {
			return fail(2,
						QCoreApplication::translate("LevelBuildCli", "Use one --tool <compiler-id>=<executable> per compatible compiler."));
		}
		seenTools.insert(id);
		request.executableOverrides.append({id, executable});
	}
	if (seen.contains("--timeout-ms")) {
		bool valid = false;
		request.stageTimeoutMs = value("--timeout-ms").toInt(&valid);
		if (!valid || request.stageTimeoutMs < 1) {
			return fail(2, QCoreApplication::translate("LevelBuildCli", "--timeout-ms requires a positive whole number."));
		}
	}
	BuildPipelineDescriptor pipeline;
	buildPipelineForId(request.pipelineId, &pipeline);
	QSet<QString> stages;
	for (const auto& stage : pipeline.stages) {
		stages.insert(stage.id);
	}
	for (const auto& stage : values.value("--disable-stage")) {
		if (!stages.contains(stage) || request.disabledStageIds.contains(stage)) {
			return fail(2, QCoreApplication::translate("LevelBuildCli", "Unknown or repeated stage: %1").arg(stage));
		}
		request.disabledStageIds << stage;
	}
	for (const auto& argument : values.value("--stage-args")) {
		const auto equals = argument.indexOf('=');
		const auto stage = argument.left(equals);
		if (equals < 1 || !stages.contains(stage) || request.stageExtraArguments.contains(stage)) {
			return fail(2, QCoreApplication::translate("LevelBuildCli", "Use one --stage-args <stage>=<arguments> per known stage."));
		}
		request.stageExtraArguments[stage] = QProcess::splitCommand(argument.mid(equals + 1));
	}
	const auto result = runLevelBuildWorkspace(workspace, request);
	return {result.succeeded() ? 0 : 4,
			result.errors.join('\n'),
			{{"workspaceDirectory", workspace.directory}, {"pipeline", buildPipelineResultJson(result)}},
			{buildPipelineResultText(result)}};
}
} // namespace vibestudio::cli
