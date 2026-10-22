// The generative commands through the real executable: map generate, map
// plan, map ai-edit with a saved proposal, asset audio-generate, and ai
// status, all with no AI, and the refusals AI-free settings bring.

#include "core/package_archive.h"
#include "core/project_manifest.h"
#include "core/studio_settings.h"

#include "qmg/prompt.hpp"
#include "qmg/semantic_plan.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;

namespace {

int failures = 0;

void expect(bool condition, const char* message, const QString& detail = QString())
{
	if (!condition) {
		std::cerr << "FAIL: " << message << (detail.isEmpty() ? "" : ": ") << detail.left(800).toStdString() << "\n";
		++failures;
	}
}

struct Run {
	int exitCode = -1;
	QJsonObject json;
	QString output;
};

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid() || argc < 2 || argc > 3) {
		std::cerr << "usage: ai_generation_cli_smoke_test <vibestudio executable> [<fake_ai_harness executable>]\n";
		return EXIT_FAILURE;
	}
	const QString cli = QString::fromLocal8Bit(argv[1]);
	const QString harness = argc == 3 ? QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath() : QString();
	const QString settings = temp.filePath(QStringLiteral("cli.ini"));
	const auto run = [&](const QStringList& arguments) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(cli, QStringList {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--settings-file"), settings} + arguments);
		Run result;
		const bool ended = process.waitForStarted() && process.waitForFinished(60000);
		if (!ended) {
			process.kill();
			process.waitForFinished();
			result.output = QStringLiteral("timed out");
			return result;
		}
		const QByteArray out = process.readAllStandardOutput();
		result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
		result.json = QJsonDocument::fromJson(out).object();
		result.output = QString::fromUtf8(out + process.readAllStandardError());
		return result;
	};

	// A level, by the rules.
	const QString map = temp.filePath(QStringLiteral("maps/keep.map"));
	Run generated = run({QStringLiteral("map"), QStringLiteral("generate"), QStringLiteral("--prompt"), QStringLiteral("gothic keep, 4 rooms, seed 9"),
		QStringLiteral("--game"), QStringLiteral("quake"), QStringLiteral("--output"), map});
	expect(generated.exitCode == 0 && QFileInfo::exists(map) && generated.json.value(QStringLiteral("written")).toArray().size() == 1,
		"map generate writes a level", generated.output);
	Run plan = run({QStringLiteral("map"), QStringLiteral("plan"), QStringLiteral("--prompt"), QStringLiteral("deathmatch, 5 rooms"), QStringLiteral("--output"),
		temp.filePath(QStringLiteral("plan.json"))});
	expect(plan.exitCode == 0 && QFileInfo::exists(temp.filePath(QStringLiteral("plan.json"))) && plan.json.contains(QStringLiteral("plan")),
		"map plan writes the plan", plan.output);

	// A level by Quake-MapGen: a Quake III CTF map in brush primitives, its report, and its plan.
	const QString ctf = temp.filePath(QStringLiteral("maps/forts.map"));
	const QString ctfReport = temp.filePath(QStringLiteral("maps/forts.json"));
	Run mapgen = run({QStringLiteral("map"), QStringLiteral("generate"), QStringLiteral("--generator"), QStringLiteral("quake-mapgen"), QStringLiteral("--prompt"),
		QStringLiteral("Quake 3 capture the flag, 6 rooms, seed 7"), QStringLiteral("--format"), QStringLiteral("q3-brush-primitives"), QStringLiteral("--output"), ctf,
		QStringLiteral("--report"), ctfReport});
	const QJsonObject qmgReport = mapgen.json.value(QStringLiteral("quakeMapgen")).toObject();
	expect(mapgen.exitCode == 0 && QFileInfo::exists(ctf) && QFileInfo::exists(ctfReport) && mapgen.json.value(QStringLiteral("generator")).toString() == QStringLiteral("quake-mapgen")
			&& mapgen.json.value(QStringLiteral("dialect")).toString() == QStringLiteral("q3-brush-primitives") && qmgReport.value(QStringLiteral("schema_version")).toInt() == 4
			&& qmgReport.value(QStringLiteral("mode")).toString() == QStringLiteral("capture_the_flag"),
		"map generate --generator quake-mapgen writes a CTF map and its report", mapgen.output);
	Run mapgenPlan = run({QStringLiteral("map"), QStringLiteral("plan"), QStringLiteral("--generator"), QStringLiteral("quake-mapgen"), QStringLiteral("--prompt"),
		QStringLiteral("Quake 1 single-player castle, 6 rooms")});
	expect(mapgenPlan.exitCode == 0 && mapgenPlan.json.value(QStringLiteral("plan")).toObject().value(QStringLiteral("room_roles")).toArray().size() == 6,
		"map plan --generator quake-mapgen prints the semantic plan", mapgenPlan.output);
	Run studioOnly = run({QStringLiteral("map"), QStringLiteral("generate"), QStringLiteral("--generator"), QStringLiteral("quake-mapgen"), QStringLiteral("--prompt"),
		QStringLiteral("Quake 2 deathmatch"), QStringLiteral("--theme"), QStringLiteral("hell"), QStringLiteral("--dry-run")});
	expect(studioOnly.exitCode == 2 && studioOnly.output.contains(QStringLiteral("--theme")), "the studio generator's options are refused for Quake-MapGen", studioOnly.output);
	Run wrongFormat = run({QStringLiteral("map"), QStringLiteral("generate"), QStringLiteral("--prompt"), QStringLiteral("Quake 2 deathmatch"), QStringLiteral("--format"),
		QStringLiteral("q2-valve220"), QStringLiteral("--dry-run")});
	expect(wrongFormat.exitCode == 2 && wrongFormat.output.contains(QStringLiteral("quake-mapgen")), "Quake-MapGen's options need --generator quake-mapgen", wrongFormat.output);
	Run mapgenModel = run({QStringLiteral("map"), QStringLiteral("generate"), QStringLiteral("--generator"), QStringLiteral("quake-mapgen"), QStringLiteral("--prompt"),
		QStringLiteral("Quake 2 deathmatch"), QStringLiteral("--planner"), QStringLiteral("ai"), QStringLiteral("--output"), temp.filePath(QStringLiteral("maps/ai.map"))});
	expect(mapgenModel.exitCode == 5 && mapgenModel.output.contains(QStringLiteral("AI-free")), "Quake-MapGen asks no model in AI-free mode", mapgenModel.output);

	// Edits from a saved proposal: one sound action, one the map refuses.
	const auto action = [](const QString& kind, const QStringList& targets) {
		return QJsonObject {{QStringLiteral("kind"), kind}, {QStringLiteral("reason"), QStringLiteral("Test.")}, {QStringLiteral("targets"), QJsonArray::fromStringList(targets)},
			{QStringLiteral("classname"), QString()}, {QStringLiteral("origin"), QJsonArray()}, {QStringLiteral("keys"), QJsonArray()}, {QStringLiteral("key"), QString()},
			{QStringLiteral("value"), QString()}, {QStringLiteral("mins"), QJsonArray()}, {QStringLiteral("maxs"), QJsonArray()}, {QStringLiteral("texture"), QString()},
			{QStringLiteral("delta"), QJsonArray()}};
	};
	QJsonObject wad = action(QStringLiteral("set-key"), {QStringLiteral("entity:0")});
	wad.insert(QStringLiteral("key"), QStringLiteral("wad"));
	wad.insert(QStringLiteral("value"), QStringLiteral("gfx/base.wad"));
	const QJsonObject proposal {{QStringLiteral("summary"), QStringLiteral("Test.")},
		{QStringLiteral("actions"), QJsonArray {wad, action(QStringLiteral("delete"), {QStringLiteral("entity:0")})}}};
	QFile proposalFile(temp.filePath(QStringLiteral("edits.json")));
	expect(proposalFile.open(QIODevice::WriteOnly) && proposalFile.write(QJsonDocument(proposal).toJson()) > 0, "the proposal is written");
	proposalFile.close();
	const QString edited = temp.filePath(QStringLiteral("maps/keep-edited.map"));
	Run edit = run({QStringLiteral("map"), QStringLiteral("ai-edit"), map, QStringLiteral("--proposal"), proposalFile.fileName(), QStringLiteral("--output"), edited});
	const QJsonArray editActions = edit.json.value(QStringLiteral("proposal")).toObject().value(QStringLiteral("actions")).toArray();
	expect(edit.exitCode == 0 && edit.json.value(QStringLiteral("applied")).toInt() == 1 && editActions.size() == 2
			&& !editActions.at(1).toObject().value(QStringLiteral("enabled")).toBool() && QFileInfo::exists(edited),
		"map ai-edit applies the sound action of a saved proposal and blocks the other", edit.output);
	QFile editedFile(edited);
	expect(editedFile.open(QIODevice::ReadOnly) && editedFile.readAll().contains("\"wad\" \"gfx/base.wad\""), "the edited map carries the new key");
	editedFile.close();
	Run review = run({QStringLiteral("map"), QStringLiteral("ai-edit"), map, QStringLiteral("--proposal"), proposalFile.fileName()});
	expect(review.exitCode == 0 && !review.json.contains(QStringLiteral("output")), "map ai-edit without --output only reviews", review.output);
	Run refused = run({QStringLiteral("map"), QStringLiteral("ai-edit"), map, QStringLiteral("--prompt"), QStringLiteral("add a light")});
	expect(refused.exitCode == 5 && refused.output.contains(QStringLiteral("AI-free")), "map ai-edit asks no model in AI-free mode", refused.output);

	// Sounds, by the synthesizer: a Quake WAV and a Doom lump.
	const QString project = temp.filePath(QStringLiteral("mod"));
	Run quakeSound = run({QStringLiteral("asset"), QStringLiteral("audio-generate"), QStringLiteral("--prompt"), QStringLiteral("heavy metal door slam"),
		QStringLiteral("--game"), QStringLiteral("quake"), QStringLiteral("--output"), project});
	expect(quakeSound.exitCode == 0 && QFileInfo::exists(QDir(project).filePath(QStringLiteral("sound/vibestudio/door_slam.wav")))
			&& quakeSound.json.value(QStringLiteral("sounds")).toArray().first().toObject().value(QStringLiteral("format")).toString() == QStringLiteral("wav-8"),
		"asset audio-generate writes a Quake WAV", quakeSound.output);
	Run doomSound = run({QStringLiteral("asset"), QStringLiteral("audio-generate"), QStringLiteral("--prompt"), QStringLiteral("rocket explosion"),
		QStringLiteral("--game"), QStringLiteral("doom"), QStringLiteral("--variants"), QStringLiteral("2"), QStringLiteral("--output"), project});
	PackageArchive archive;
	QString error;
	QStringList lumps;
	if (archive.load(QDir(project).filePath(QStringLiteral("wads/vibestudio_sounds.wad")), &error)) {
		for (const PackageEntry& entry : archive.entries()) {
			lumps << entry.virtualPath;
		}
	}
	expect(doomSound.exitCode == 0 && lumps.size() == 2 && lumps.first().startsWith(QStringLiteral("DS")), "asset audio-generate adds Doom lumps to one PWAD",
		doomSound.output + error);
	Run again = run({QStringLiteral("asset"), QStringLiteral("audio-generate"), QStringLiteral("--prompt"), QStringLiteral("heavy metal door slam"),
		QStringLiteral("--game"), QStringLiteral("quake"), QStringLiteral("--output"), project});
	expect(again.exitCode == 4, "an existing sound is not replaced without --overwrite", again.output);
	Run model = run({QStringLiteral("asset"), QStringLiteral("audio-generate"), QStringLiteral("--prompt"), QStringLiteral("hum"), QStringLiteral("--source"),
		QStringLiteral("ai"), QStringLiteral("--output"), project});
	expect(model.exitCode == 5, "asset audio-generate --source ai asks no model in AI-free mode", model.output);
	Run usage = run({QStringLiteral("asset"), QStringLiteral("audio-generate"), QStringLiteral("--prompt"), QStringLiteral("x"), QStringLiteral("--loop"),
		QStringLiteral("sometimes"), QStringLiteral("--output"), project});
	expect(usage.exitCode == 2, "a bad --loop value is a usage error", usage.output);

	// Where requests would go, and what stops them.
	Run status = run({QStringLiteral("ai"), QStringLiteral("status")});
	expect(status.exitCode == 0 && status.json.value(QStringLiteral("imageConnection")).toObject().value(QStringLiteral("state")).toString() == QStringLiteral("ai-free-mode")
			&& status.json.value(QStringLiteral("soundConnection")).toObject().value(QStringLiteral("state")).toString() == QStringLiteral("ai-free-mode"),
		"ai status reports the image and sound connections", status.output);

	// A project that turns AI off stops requests even with AI allowed.
	Run allow = run({QStringLiteral("--set-ai-free"), QStringLiteral("off"), QStringLiteral("--set-ai-local"), QStringLiteral("local-offline"),
		QStringLiteral("--set-ai-model"), QStringLiteral("local-offline=llama3.2")});
	expect(allow.exitCode == 0, "AI is allowed in the settings", allow.output);
	ProjectManifest manifest = defaultProjectManifest(project);
	manifest.settingsOverrides.aiFreeModeSet = true;
	manifest.settingsOverrides.aiFreeMode = true;
	expect(saveProjectManifest(manifest, &error), "the AI-free manifest is saved", error);
	Run ready = run({QStringLiteral("ai"), QStringLiteral("status")});
	expect(ready.json.value(QStringLiteral("textConnection")).toObject().value(QStringLiteral("state")).toString() == QStringLiteral("ready"),
		"with AI allowed and no project open, the local model is ready", ready.output);
	if (!harness.isEmpty()) {
		// Quake-MapGen planned through Claude Code and the user's own login; the fake tool stands in.
		const std::string prompt = "Quake 2 deathmatch, 4 rooms, seed 3";
		qputenv("FAKE_HARNESS_ANSWER", QByteArray::fromStdString(qmg::render_semantic_plan(qmg::make_rule_semantic_plan(qmg::PromptParser {}.parse(prompt)))));
		const QStringList harnessLevel {QStringLiteral("map"), QStringLiteral("generate"), QStringLiteral("--generator"), QStringLiteral("quake-mapgen"), QStringLiteral("--prompt"),
			QString::fromStdString(prompt), QStringLiteral("--planner"), QStringLiteral("ai"), QStringLiteral("--provider"), QStringLiteral("claude-code"), QStringLiteral("--endpoint"),
			harness, QStringLiteral("--output"), temp.filePath(QStringLiteral("maps/claude.map"))};
		Run cloudOff = run(harnessLevel);
		expect(cloudOff.exitCode == 5 && cloudOff.output.contains(QStringLiteral("cloud")), "a login harness needs cloud connectors allowed", cloudOff.output);
		Run cloud = run({QStringLiteral("--set-ai-cloud"), QStringLiteral("on")});
		expect(cloud.exitCode == 0, "cloud connectors are allowed", cloud.output);
		Run unconfirmed = run(harnessLevel);
		expect(unconfirmed.exitCode == 5 && unconfirmed.output.contains(QStringLiteral("Anthropic")) && unconfirmed.output.contains(QStringLiteral("--yes")),
			"a harness request names where it goes and waits for --yes", unconfirmed.output);
		Run planned = run(harnessLevel + QStringList {QStringLiteral("--yes")});
		const QJsonObject planner = planned.json.value(QStringLiteral("quakeMapgen")).toObject().value(QStringLiteral("planner")).toObject();
		expect(planned.exitCode == 0 && QFileInfo::exists(temp.filePath(QStringLiteral("maps/claude.map")))
				&& planned.json.value(QStringLiteral("ai")).toObject().value(QStringLiteral("connector")).toString() == QStringLiteral("claude-code")
				&& planner.value(QStringLiteral("requested_backend")).toString() == QStringLiteral("connector")
				&& planner.value(QStringLiteral("used_backend")).toString() == QStringLiteral("vibestudio/claude-code")
				&& planner.value(QStringLiteral("model")).toObject().value(QStringLiteral("model_id")).toString() == QStringLiteral("claude-opus-5-5"),
			"map generate --planner ai --provider claude-code plans the level through the harness with claude-opus-5-5", planned.output);
		qputenv("FAKE_HARNESS_ANSWER", "OK");
		Run test = run({QStringLiteral("ai"), QStringLiteral("test-connection"), QStringLiteral("--provider"), QStringLiteral("codex"), QStringLiteral("--endpoint"), harness});
		expect(test.exitCode == 0 && test.json.value(QStringLiteral("login")).toObject().value(QStringLiteral("signedIn")).toBool()
				&& test.json.value(QStringLiteral("connection")).toObject().value(QStringLiteral("transport")).toString() == QStringLiteral("harness"),
			"ai test-connection checks the Codex login and asks it", test.output);
		Run status = run({QStringLiteral("ai"), QStringLiteral("status")});
		expect(status.json.value(QStringLiteral("runtimes")).toObject().value(QStringLiteral("harnesses")).toArray().size() == 3, "ai status lists the harnesses", status.output);
		run({QStringLiteral("--set-ai-cloud"), QStringLiteral("off")});
	}
	{
		// The project the GUI last opened, as the CLI reads it from the settings.
		StudioSettings studio(settings);
		studio.setCurrentProjectPath(project);
		studio.sync();
	}
	Run projectStatus = run({QStringLiteral("ai"), QStringLiteral("status")});
	expect(projectStatus.json.value(QStringLiteral("textConnection")).toObject().value(QStringLiteral("state")).toString() == QStringLiteral("project-ai-free")
			&& projectStatus.json.value(QStringLiteral("preferences")).toObject().value(QStringLiteral("projectAiFree")).toBool(),
		"with the AI-free project open, text requests are stopped", projectStatus.output);
	Run projectSound = run({QStringLiteral("asset"), QStringLiteral("audio-generate"), QStringLiteral("--prompt"), QStringLiteral("hum"), QStringLiteral("--source"),
		QStringLiteral("ai"), QStringLiteral("--output"), project});
	expect(projectSound.exitCode == 5 && projectSound.output.contains(QStringLiteral("--project-ai-free off")), "and so are sound requests", projectSound.output);

	if (failures > 0) {
		std::cerr << failures << " AI generation CLI check(s) failed.\n";
		return EXIT_FAILURE;
	}
	std::cout << "AI generation CLI checks passed.\n";
	return EXIT_SUCCESS;
}
