#include "core/level_mapgen.h"

#include "core/level_document.h"
#include "core/level_map.h"

#include "qmg/prompt.hpp"
#include "qmg/semantic_plan.hpp"

#include "tests/fake_ai_provider.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace vibestudio;

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
	if (!condition) {
		++failures;
		std::cerr << "FAIL: " << message << "\n";
	}
}

LevelGenerationSpec promptSpec(const QString& prompt)
{
	LevelGenerationSpec spec;
	spec.generator = levelMapgenGeneratorId();
	spec.prompt = prompt;
	return spec;
}

QString sha256(const QByteArray& bytes)
{
	return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QJsonObject reportOf(const LevelGenerationResult& result)
{
	return QJsonDocument::fromJson(result.generatorReport).object();
}

void checkSpecs()
{
	expect(levelMapgenGameIds().size() == 3 && levelMapgenModeIds().contains(QStringLiteral("ctf")), "Quake-MapGen offers three games and capture the flag.");
	expect(levelMapgenFormatIds(QStringLiteral("quake3")) == QStringList({QStringLiteral("q3-legacy"), QStringLiteral("q3-brush-primitives")})
			&& levelMapgenFormatIds(QStringLiteral("quake")) == QStringList({QStringLiteral("q1-valve220")}),
		"Each game lists its own map formats, its default first.");
	expect(levelMapgenSwitchNames().contains(QStringLiteral("r1s.q1_intermission")), "The stage switches are listed by name.");
	expect(levelMapgenVersion().contains(QStringLiteral("vibestudio")), "The version says it is VibeStudio's build of the generator.");

	LevelGenerationSpec doom = promptSpec(QStringLiteral("a techbase"));
	doom.game = QStringLiteral("doom");
	expect(!levelMapgenSpecProblem(doom).isEmpty(), "Doom is left to the studio generator.");
	LevelGenerationSpec format = promptSpec(QStringLiteral("Quake 1 deathmatch"));
	format.game = QStringLiteral("quake");
	format.format = QStringLiteral("q3-legacy");
	expect(!levelMapgenSpecProblem(format).isEmpty(), "A Quake III format is refused for Quake.");
	LevelGenerationSpec switches = promptSpec(QStringLiteral("Quake 1 single-player"));
	switches.switches = {QStringLiteral("no.such_switch=on")};
	expect(levelMapgenSpecProblem(switches).contains(QStringLiteral("r1s.q1_intermission")), "An unknown switch is refused with the known ones listed.");
	switches.switches = {QStringLiteral("r1s.q1_intermission=on")};
	expect(levelMapgenSpecProblem(switches).isEmpty(), "A known switch is accepted.");
	const LevelGenerationResult refused = generateMapgenLevel(doom);
	expect(!refused.ok && !refused.error.isEmpty() && refused.mapBytes.isEmpty(), "Generation refuses what the spec check refuses.");
}

void checkParity()
{
	// Byte-for-byte the maps Quake-MapGen 0.4.0's own qmg (ggml ranker, built 2026-10-16 from the
	// imported revision) wrote for these prompts. A deliberate generator change updates these hashes.
	struct Case {
		const char* prompt;
		const char* format;
		const char* sha256;
	};
	const Case cases[] = {
		{"Quake 1 single-player, 6 rooms, industrial loop, seed 42", "", "8a5ec441715b2bfbb02390e8da650783858a9d43afe439603da352d54667f242"},
		{"Quake 2 deathmatch, 8 players, 8-room industrial loop with strong height variation, seed 42", "",
			"a3e6938bd722c6b531b448911000e3eec9001f0b5ef136367676d1abeeaf4b5e"},
		{"Quake 3 capture the flag, 8 rooms, gothic castle with high verticality, seed 7", "", "7c83885a09d5e43a5aaa385290f3171ead69367894f64c29e3e65c1bff14dc5b"},
		{"Quake 3 capture the flag, 8 rooms, space, seed 11", "q3-brush-primitives", "1eea36f898e4380f771afd8e03ab5ce615da9e48070f7a1cefc2c779ec89e8c1"},
	};
	for (const Case& item : cases) {
		LevelGenerationSpec spec = promptSpec(QString::fromUtf8(item.prompt));
		spec.format = QString::fromUtf8(item.format);
		const LevelGenerationResult result = generateMapgenLevel(spec);
		if (!result.ok) {
			std::cerr << "  " << item.prompt << ": " << result.error.toStdString() << "\n";
		}
		expect(result.ok && sha256(result.mapBytes) == QString::fromLatin1(item.sha256), "The map matches upstream Quake-MapGen byte for byte.");
		// A build with ggml can rank through the upstream graph: the same map, ggml's provenance.
		if (levelMapgenRankerIds().contains(QStringLiteral("ggml"))) {
			spec.ranker = QStringLiteral("ggml");
			const LevelGenerationResult ggml = generateMapgenLevel(spec);
			expect(ggml.ok && ggml.mapBytes == result.mapBytes
					&& reportOf(ggml).value(QStringLiteral("ggml_backend")).toString() == QStringLiteral("ggml/cpu-linear-layout-ranker"),
				"Ranking through ggml gives the same map and records ggml.");
		}
	}
	LevelGenerationSpec ranked = promptSpec(QStringLiteral("Quake 1 deathmatch"));
	ranked.ranker = QStringLiteral("ggml");
	expect(levelMapgenSpecProblem(ranked).isEmpty() == levelMapgenRankerIds().contains(QStringLiteral("ggml")), "The ggml ranker is offered only where ggml is linked.");
}

void checkGeneration()
{
	LevelGenerationSpec spec = promptSpec(QStringLiteral("Quake 2 deathmatch, industrial loop, seed 42"));
	spec.rooms = 6;
	spec.players = 6;
	const LevelGenerationResult result = generateMapgenLevel(spec);
	expect(result.ok, "A Quake II deathmatch level is built.");
	expect(result.generator == QStringLiteral("quake-mapgen") && result.format == QStringLiteral("quake2-map") && result.dialect == QStringLiteral("q2-valve220"),
		"It is a Quake II map written in Valve 220.");
	expect(result.spec.game == QStringLiteral("quake2") && result.spec.mode == QStringLiteral("deathmatch") && result.spec.rooms == 6 && result.spec.seed == 42,
		"The result's spec is what Quake-MapGen built.");
	expect(result.suggestedFileName == QStringLiteral("qmg_quake2_deathmatch_42.map"), "The file name names the game, mode, and seed.");
	expect(result.statistics.rooms == 6 && result.statistics.brushes > 50 && result.statistics.entities > 6 && result.statistics.reachableRooms == 6,
		"The statistics count rooms, brushes, and entities.");
	expect(result.layout.rooms.size() == 6 && result.layout.width > 8 && result.layout.height > 8 && result.statistics.openCells > 40,
		"The preview layout holds the rooms.");
	expect(result.plan.rooms.size() == 6 && result.plan.planner == QStringLiteral("quake-mapgen:rules/v1") && !result.plan.summary.isEmpty(),
		"The plan lists the rooms and names the rules planner.");
	expect(!result.analysis.isEmpty() && result.analysis.first().startsWith(QStringLiteral("Analysis")), "The analysis opens with the quality verdict.");
	const QJsonObject report = reportOf(result);
	expect(report.value(QStringLiteral("schema_version")).toInt() == 4 && report.value(QStringLiteral("ggml_backend")).toString() == QStringLiteral("vibestudio/scalar-linear-layout-ranker"),
		"The report is schema v4 from the scalar ranker.");
	expect(report.value(QStringLiteral("game")).toString() == QStringLiteral("quake2") && report.value(QStringLiteral("request_seed")).toString() == QStringLiteral("42"),
		"The report records the request.");

	const LevelGenerationResult again = generateMapgenLevel(spec);
	expect(again.ok && again.mapBytes == result.mapBytes && again.generatorReport == result.generatorReport, "The same spec gives the same bytes.");

	LevelMapDocument document;
	QString error;
	expect(levelGenerationDocument(result, &document, &error) && document.entities.size() > 6, "The map opens as an editor document.");
	const QImage preview = renderLevelLayoutPreview(result, QSize(320, 240));
	int lit = 0;
	for (int y = 0; y < preview.height(); y += 4) {
		for (int x = 0; x < preview.width(); x += 4) {
			lit += preview.pixel(x, y) != QColor(24, 26, 31).rgb() ? 1 : 0;
		}
	}
	expect(lit > 200, "The preview draws the layout.");
	expect(levelGenerationSummaryLines(result).size() >= 3, "The summary has its lines.");

	LevelGenerationSpec ctf = promptSpec(QString());
	ctf.game = QStringLiteral("quake3");
	ctf.mode = QStringLiteral("ctf");
	ctf.rooms = 6;
	ctf.seed = 5;
	ctf.format = QStringLiteral("q3-brush-primitives");
	ctf.verticality = QStringLiteral("high");
	ctf.intensity = QStringLiteral("intense");
	ctf.title = QStringLiteral("Twin Forts");
	const LevelGenerationResult flags = generateMapgenLevel(ctf);
	expect(flags.ok && flags.spec.mode == QStringLiteral("ctf") && flags.dialect == QStringLiteral("q3-brush-primitives"), "Options alone build a Quake III CTF map.");
	const QString text = QString::fromUtf8(flags.mapBytes);
	expect(text.contains(QStringLiteral("team_CTF_redflag")) && text.contains(QStringLiteral("team_CTF_blueflag")) && text.contains(QStringLiteral("brushDef")),
		"It has both flags and brush primitives.");
	expect(text.contains(QStringLiteral("\"message\" \"Twin Forts\"")) && flags.spec.title == QStringLiteral("Twin Forts"), "A chosen title becomes worldspawn's message.");
	const QJsonObject plan = reportOf(flags).value(QStringLiteral("semantic_plan")).toObject();
	expect(plan.value(QStringLiteral("verticality")).toInt() >= 70 && plan.value(QStringLiteral("intensity")).toInt() >= 65, "Verticality and intensity reach the plan.");
	LevelMapDocument q3;
	expect(levelGenerationDocument(flags, &q3, &error) && q3.format == LevelMapFormat::Quake3Map, "The Quake III map opens in the editor.");

	// r1s.q1_intermission is on by default: every Quake map gets an intermission camera.
	LevelGenerationSpec switched = promptSpec(QStringLiteral("Quake 1 single-player, 5 rooms, seed 3"));
	const LevelGenerationResult camera = generateMapgenLevel(switched);
	switched.switches = {QStringLiteral("r1s.q1_intermission=off")};
	const LevelGenerationResult intermission = generateMapgenLevel(switched);
	expect(camera.ok && QString::fromUtf8(camera.mapBytes).contains(QStringLiteral("info_intermission")) && intermission.ok
			&& !QString::fromUtf8(intermission.mapBytes).contains(QStringLiteral("info_intermission"))
			&& reportOf(intermission).value(QStringLiteral("switches")).toObject().value(QStringLiteral("r1s.q1_intermission")).toString() == QStringLiteral("off"),
		"A stage switch reaches the generator and its report.");
}

AiTextConnection fakeConnection(const FakeAiProvider& provider)
{
	AiTextConnection connection;
	connection.connectorId = QStringLiteral("local-offline");
	connection.displayName = QStringLiteral("Fake");
	connection.model = QStringLiteral("fake-planner");
	connection.endpoint = provider.baseUrl(QStringLiteral("/v1"));
	connection.local = true;
	return connection;
}

void checkAiPlanning()
{
	FakeAiProvider provider;
	expect(provider.listening(), "The fake provider listens.");
	const QString prompt = QStringLiteral("Quake 1 deathmatch, 5 rooms, gothic, seed 21");
	const LevelGenerationSpec spec = promptSpec(prompt);

	AiChatRequest request;
	QString error;
	expect(levelMapgenPlanAiRequest(spec, fakeConnection(provider), &request, &error), "The first plan request can be previewed.");
	const QJsonObject roles = request.responseSchema.value(QStringLiteral("properties")).toObject().value(QStringLiteral("room_roles")).toObject();
	expect(request.system.contains(QStringLiteral("semantic planner")) && roles.value(QStringLiteral("minItems")).toInt() == 5 && roles.value(QStringLiteral("maxItems")).toInt() == 5,
		"It carries the planner's instructions and a schema for five rooms.");

	// A valid plan, as the rules would make it, from the model.
	const qmg::PromptSpec parsed = qmg::PromptParser {}.parse(prompt.toStdString());
	qmg::SemanticPlan valid = qmg::make_rule_semantic_plan(parsed);
	valid.lighting_style = qmg::LightingStyle::high_contrast;
	provider.answer(200, FakeAiProvider::openAiAnswer(QString::fromStdString(qmg::render_semantic_plan(valid))));
	LevelMapgenModel model;
	model.connection = fakeConnection(provider);
	const LevelGenerationResult planned = generateMapgenLevel(spec, &model);
	expect(planned.ok && model.requests == 1 && model.inputTokens == 42 && model.outputTokens == 7, "The model plans in one request and its usage is counted.");
	const QJsonObject planner = reportOf(planned).value(QStringLiteral("planner")).toObject();
	expect(planner.value(QStringLiteral("requested_backend")).toString() == QStringLiteral("connector")
			&& planner.value(QStringLiteral("used_backend")).toString() == QStringLiteral("vibestudio/local-offline")
			&& planner.value(QStringLiteral("model")).toObject().value(QStringLiteral("provenance")).toString() == QStringLiteral("named_service")
			&& planner.value(QStringLiteral("model")).toObject().value(QStringLiteral("model_sha256")).toString().isEmpty(),
		"The report names the connector and claims no weight digests.");
	expect(reportOf(planned).value(QStringLiteral("semantic_plan")).toObject().value(QStringLiteral("lighting_style")).toString() == QStringLiteral("high_contrast")
			&& planned.plan.planner == QStringLiteral("quake-mapgen:vibestudio/local-offline"),
		"The model's plan is the one built.");
	const QJsonObject sent = QJsonDocument::fromJson(provider.exchanges().value(0).body).object();
	expect(sent.value(QStringLiteral("response_format")).toObject().value(QStringLiteral("type")).toString() == QStringLiteral("json_schema"),
		"The request asked for structured output.");

	// Answers that never hold a plan: the rules plan instead, and say so.
	provider.clear();
	provider.answer(200, FakeAiProvider::openAiAnswer(QStringLiteral("I would rather describe the castle in prose.")));
	LevelMapgenModel stubborn;
	stubborn.connection = fakeConnection(provider);
	stubborn.maxRevisions = 1;
	const LevelGenerationResult fallback = generateMapgenLevel(spec, &stubborn);
	expect(fallback.ok && stubborn.requests == 2 && !fallback.warnings.isEmpty() && fallback.plan.planner == QStringLiteral("quake-mapgen:rules/v1"),
		"After the corrections run out, the rules plan the level.");
	expect(!reportOf(fallback).value(QStringLiteral("planner")).toObject().value(QStringLiteral("fallback")).toString().isEmpty(), "The report says why the rules planned.");

	// A provider failure stops generation instead.
	provider.answer(500, QByteArrayLiteral("{\"error\":{\"message\":\"overloaded\"}}"));
	LevelMapgenModel failing;
	failing.connection = fakeConnection(provider);
	const LevelGenerationResult failed = generateMapgenLevel(spec, &failing);
	expect(!failed.ok && failed.mapBytes.isEmpty() && failing.failure != AiChatFailure::None, "A provider failure builds nothing.");

	LevelMapgenModel cancelled;
	cancelled.connection = fakeConnection(provider);
	cancelled.cancelled->store(true);
	const LevelGenerationResult stopped = generateMapgenLevel(spec, &cancelled);
	expect(!stopped.ok && cancelled.requests == 0 && stopped.error.contains(QStringLiteral("Cancelled")), "A cancelled model sends nothing and builds nothing.");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	checkSpecs();
	checkParity();
	checkGeneration();
	checkAiPlanning();
	if (failures > 0) {
		std::cerr << failures << " Quake-MapGen check(s) failed.\n";
		return EXIT_FAILURE;
	}
	std::cout << "Quake-MapGen checks passed.\n";
	return EXIT_SUCCESS;
}
