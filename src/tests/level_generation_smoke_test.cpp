#include "core/level_generation.h"

#include "core/level_document.h"
#include "core/level_map.h"

#include "tests/fake_ai_provider.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>

using namespace vibestudio;

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << "\n";
		++failures;
	}
}

bool waitUntil(const std::function<bool()>& done, int timeoutMsecs = 5000)
{
	QElapsedTimer clock;
	clock.start();
	while (!done() && clock.elapsed() < timeoutMsecs) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
	}
	return done();
}

LevelGenerationSpec specFor(const QString& prompt, const QString& game = QString())
{
	LevelGenerationSpec spec;
	spec.prompt = prompt;
	spec.game = game;
	return spec;
}

int placements(const LevelSemanticPlan& plan, const QString& kind)
{
	int total = 0;
	for (const LevelPlanPlacement& placement : plan.placements) {
		total += placement.kind == kind ? placement.count : 0;
	}
	return total;
}

bool connected(const LevelSemanticPlan& plan)
{
	if (plan.rooms.isEmpty()) {
		return false;
	}
	QSet<QString> reached {plan.rooms.first().id};
	for (bool grew = true; grew;) {
		grew = false;
		for (const LevelPlanConnection& connection : plan.connections) {
			if (reached.contains(connection.from) != reached.contains(connection.to)) {
				reached.insert(connection.from);
				reached.insert(connection.to);
				grew = true;
			}
		}
	}
	return reached.size() == plan.rooms.size();
}

bool everyObjectClosed(const QJsonObject& schema)
{
	if (schema.value(QStringLiteral("type")).toString() == QStringLiteral("object")) {
		const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
		if (schema.value(QStringLiteral("additionalProperties")) != QJsonValue(false) || schema.value(QStringLiteral("required")).toArray().size() != properties.size()) {
			return false;
		}
		for (auto it = properties.constBegin(); it != properties.constEnd(); ++it) {
			if (!everyObjectClosed(it.value().toObject())) {
				return false;
			}
		}
	}
	if (schema.value(QStringLiteral("items")).isObject()) {
		return everyObjectClosed(schema.value(QStringLiteral("items")).toObject());
	}
	return true;
}

void checkSpecs()
{
	LevelGenerationSpec spec = normalizedLevelGenerationSpec(specFor(QStringLiteral("Quake 2 deathmatch, 8 rooms, lava pits, seed 42")));
	expect(spec.game == QStringLiteral("quake2") && spec.mode == QStringLiteral("deathmatch") && spec.rooms == 8 && spec.liquid == QStringLiteral("lava") && spec.seed == 42
			&& spec.players == 8 && spec.monsters == QStringLiteral("none"),
		"A Quake II deathmatch brief is read: game, mode, rooms, liquid, seed, players, no monsters.");
	spec = normalizedLevelGenerationSpec(specFor(QStringLiteral("a small castle crypt for doom with no monsters")));
	expect(spec.game == QStringLiteral("doom") && spec.theme == QStringLiteral("medieval") && spec.size == QStringLiteral("small") && spec.rooms == 4
			&& spec.monsters == QStringLiteral("none") && spec.mode == QStringLiteral("single-player"),
		"A Doom castle brief is read, with its size and peace.");
	spec = normalizedLevelGenerationSpec(specFor(QStringLiteral("quake 3 duel arena with towers")));
	expect(spec.game == QStringLiteral("quake3") && spec.mode == QStringLiteral("duel") && spec.players == 2 && spec.verticality == QStringLiteral("high"),
		"A Quake III duel brief is read, with its verticality.");
	spec = normalizedLevelGenerationSpec(specFor(QStringLiteral("hellish lava temple called \"The Ashen Nave\"")));
	expect(spec.game == QStringLiteral("quake") && spec.mode == QStringLiteral("single-player") && spec.theme == QStringLiteral("hell")
			&& spec.title == QStringLiteral("The Ashen Nave"),
		"Quake single player is the default, and a quoted name becomes the title.");
	const LevelGenerationSpec first = normalizedLevelGenerationSpec(specFor(QStringLiteral("rusted tech base")));
	const LevelGenerationSpec second = normalizedLevelGenerationSpec(specFor(QStringLiteral("  Rusted   tech base ")));
	expect(first.seed == second.seed && first.seed >= 0 && first.title == second.title && !first.title.isEmpty(), "The same words give the same seed and title.");
	LevelGenerationSpec quake3Single = specFor(QStringLiteral("campaign"), QStringLiteral("q3"));
	quake3Single.mode = QStringLiteral("single-player");
	expect(normalizedLevelGenerationSpec(quake3Single).mode == QStringLiteral("deathmatch"), "Quake III has no single player: it becomes deathmatch.");
	LevelGenerationSpec many = specFor(QStringLiteral("100 rooms"));
	expect(normalizedLevelGenerationSpec(many).rooms == 16, "Room counts are clamped.");
}

void checkPlans()
{
	const LevelSemanticPlan sp = rulesLevelPlan(specFor(QStringLiteral("medieval castle, 7 rooms, seed 9")));
	expect(sp.planner == QStringLiteral("rules/v1") && sp.rooms.size() == 7 && sp.rooms.first().role == QStringLiteral("start") && connected(sp)
			&& placements(sp, QStringLiteral("player-start")) == 1 && placements(sp, QStringLiteral("exit")) == 1 && placements(sp, QStringLiteral("monster")) > 0
			&& sp.repairs.isEmpty(),
		"A single-player rules plan has a start, an exit, monsters, every room linked, and needs no repair.");
	const QJsonObject json = levelSemanticPlanJson(sp);
	expect(json == levelSemanticPlanJson(rulesLevelPlan(specFor(QStringLiteral("medieval castle, 7 rooms, seed 9")))), "The rules planner is deterministic.");
	expect(json != levelSemanticPlanJson(rulesLevelPlan(specFor(QStringLiteral("medieval castle, 7 rooms, seed 10")))), "Another seed makes another plan.");
	int pits = 0;
	int otherFeatures = 0;
	for (const LevelPlanRoom& room : rulesLevelPlan(specFor(QStringLiteral("lava pits everywhere, 9 rooms, seed 4"))).rooms) {
		pits += room.shape == QStringLiteral("pit") && room.liquid == QStringLiteral("lava") ? 1 : 0;
		otherFeatures += room.shape == QStringLiteral("platform") ? 1 : 0;
	}
	expect(pits >= 1 && otherFeatures == 0, "A brief asking for lava pits gets lava pits, not other features.");
	const LevelSemanticPlan dm = rulesLevelPlan(specFor(QStringLiteral("deathmatch for 10 players"), QStringLiteral("quake3")));
	expect(placements(dm, QStringLiteral("deathmatch-start")) >= 10 && placements(dm, QStringLiteral("monster")) == 0 && placements(dm, QStringLiteral("weapon")) > 0
			&& connected(dm),
		"A deathmatch plan has a start per player, weapons, and no monsters.");

	const QJsonObject schema = levelSemanticPlanSchema(QStringLiteral("quake"));
	const QJsonArray items = schema.value(QStringLiteral("properties")).toObject().value(QStringLiteral("placements")).toObject().value(QStringLiteral("items")).toObject()
								 .value(QStringLiteral("properties")).toObject().value(QStringLiteral("item")).toObject().value(QStringLiteral("enum")).toArray();
	expect(everyObjectClosed(schema) && items.contains(QStringLiteral("rocketlauncher")) && items.contains(QStringLiteral("shambler")) && items.contains(QString())
			&& !items.contains(QStringLiteral("railgun")),
		"The plan schema is strict (closed objects, all required) with the game's own items.");
	expect(aiJsonSchemaProblems(json, schema).isEmpty(), "A rules plan matches the schema a model is held to.");
	expect(levelGenerationItemIds(QStringLiteral("doom"), QStringLiteral("monster")).contains(QStringLiteral("cacodemon"))
			&& levelGenerationItemIds(QStringLiteral("quake3"), QStringLiteral("monster")).isEmpty(),
		"Doom has cacodemons; Quake III has no monsters.");

	LevelSemanticPlan broken;
	broken.rooms = {
		{QStringLiteral("a"), QString(), QStringLiteral("start"), QStringLiteral("small"), QStringLiteral("normal"), 0, QStringLiteral("box"), QStringLiteral("none"), QStringLiteral("normal")},
		{QStringLiteral("a"), QString(), QStringLiteral("ballroom"), QStringLiteral("huge"), QStringLiteral("normal"), 7, QStringLiteral("pit"), QStringLiteral("none"), QStringLiteral("normal")},
		{QStringLiteral("c"), QString(), QStringLiteral("hall"), QStringLiteral("medium"), QStringLiteral("normal"), 1, QStringLiteral("box"), QStringLiteral("none"), QStringLiteral("normal")},
	};
	broken.connections = {{QStringLiteral("a"), QStringLiteral("nowhere"), QStringLiteral("corridor")}, {QStringLiteral("a"), QStringLiteral("a"), QStringLiteral("corridor")}};
	broken.placements = {
		{QStringLiteral("c"), QStringLiteral("weapon"), QStringLiteral("railgun"), 1},
		{QStringLiteral("zzz"), QStringLiteral("ammo"), QString(), 1},
		{QStringLiteral("c"), QStringLiteral("monster"), QStringLiteral("Fiend"), 20},
	};
	const QStringList repairs = repairLevelSemanticPlan(&broken, specFor(QStringLiteral("quake campaign")));
	const QString joined = repairs.join(QLatin1Char('\n'));
	expect(broken.rooms[1].id != QStringLiteral("a") && joined.contains(QStringLiteral("renamed")) && broken.rooms[1].role == QStringLiteral("hall")
			&& broken.rooms[1].size == QStringLiteral("medium") && broken.rooms[1].level == 3 && broken.rooms[1].liquid != QStringLiteral("none"),
		"Duplicate ids are renamed; unknown roles and sizes replaced; levels clamped; a pit gets a liquid.");
	expect(connected(broken) && joined.contains(QStringLiteral("Linked")), "Disconnected rooms are linked so the start reaches them.");
	expect(joined.contains(QStringLiteral("railgun")) && joined.contains(QStringLiteral("zzz")) && placements(broken, QStringLiteral("exit")) == 1,
		"Unknown items and rooms are reported, and an exit is added.");
	bool fiend = false;
	for (const LevelPlanPlacement& placement : broken.placements) {
		fiend = fiend || (placement.item == QStringLiteral("fiend") && placement.count == 8);
	}
	expect(fiend, "Item names are normalized and counts clamped.");

	LevelSemanticPlan fromAnswer;
	QStringList problems;
	const QString answer = QStringLiteral("Here you go:\n```json\n%1\n```").arg(QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
	expect(levelSemanticPlanFromAnswer(answer, specFor(QStringLiteral("medieval castle, 7 rooms, seed 9")), &fromAnswer, &problems) && problems.isEmpty()
			&& fromAnswer.rooms.size() == 7,
		"A plan is read out of a fenced answer.");
	QJsonObject wrong = json;
	QJsonArray wrongPlacements = wrong.value(QStringLiteral("placements")).toArray();
	wrongPlacements.append(QJsonObject {{QStringLiteral("room"), QStringLiteral("r2")}, {QStringLiteral("kind"), QStringLiteral("weapon")}, {QStringLiteral("item"), QStringLiteral("bfg9000")},
		{QStringLiteral("count"), 1}});
	wrong.insert(QStringLiteral("placements"), wrongPlacements);
	wrong.remove(QStringLiteral("summary"));
	expect(levelSemanticPlanFromAnswer(QString::fromUtf8(QJsonDocument(wrong).toJson()), specFor(QStringLiteral("quake")), &fromAnswer, &problems)
			&& problems.join(QLatin1Char('\n')).contains(QStringLiteral("bfg9000")) && problems.join(QLatin1Char('\n')).contains(QStringLiteral("summary is missing")),
		"A plan's problems name the bad item and the missing field.");
	expect(!levelSemanticPlanFromAnswer(QStringLiteral("I cannot help with that."), specFor(QStringLiteral("quake")), &fromAnswer, &problems) && !problems.isEmpty(),
		"An answer without a plan says so.");

	const AiChatRequest request = levelPlanAiRequest(specFor(QStringLiteral("doom techbase")), QStringLiteral("openai"), QStringLiteral("gpt-test"), QString());
	expect(!request.responseSchema.isEmpty() && request.responseSchemaName == QStringLiteral("level_plan") && request.system.contains(QStringLiteral("cacodemon"))
			&& request.messages.size() == 1 && request.messages.first().text.contains(QStringLiteral("Game: doom")),
		"The plan request carries the schema, the game's items, and the brief.");
	const AiChatRequest revision = levelPlanAiRequest(specFor(QStringLiteral("doom techbase")), QStringLiteral("openai"), QStringLiteral("gpt-test"), QString(),
		QStringLiteral("{\"bad\":true}"), {QStringLiteral("$.rooms is missing.")});
	expect(revision.messages.size() == 3 && revision.messages[1].role == QStringLiteral("assistant") && revision.messages[2].text.contains(QStringLiteral("$.rooms is missing")),
		"A revision request shows the model its answer and the problems.");
}

LevelMapDocument readBack(const LevelGenerationResult& result, QString* error)
{
	LevelMapDocument document;
	LevelMapLoadRequest request;
	request.path = result.suggestedFileName;
	request.mapName = result.mapName;
	request.engineHint = result.spec.game == QStringLiteral("doom") ? QStringLiteral("doom") : result.spec.game == QStringLiteral("quake3") ? QStringLiteral("idTech3")
																																			   : QStringLiteral("idTech2");
	loadLevelMapBytes(request, result.mapBytes, &document, error);
	return document;
}

bool borderClosed(const LevelLayout& layout)
{
	for (int x = 0; x < layout.width; ++x) {
		if (layout.cell(x, 0).open || layout.cell(x, layout.height - 1).open) {
			return false;
		}
	}
	for (int y = 0; y < layout.height; ++y) {
		if (layout.cell(0, y).open || layout.cell(layout.width - 1, y).open) {
			return false;
		}
	}
	return true;
}

void checkGeneration()
{
	// Quake single player.
	LevelGenerationSpec quakeSpec = specFor(QStringLiteral("gothic castle with lava, 8 rooms, high verticality, seed 1234"), QStringLiteral("quake"));
	quakeSpec.wad = QStringLiteral("gfx/medieval.wad");
	const LevelGenerationResult quake = generateLevel(quakeSpec);
	if (!quake.ok) {
		std::cerr << "quake: " << quake.error.toStdString() << "\n";
	}
	const QString quakeText = QString::fromUtf8(quake.mapBytes);
	expect(quake.ok && quake.format == QStringLiteral("quake-map") && quake.suggestedFileName.endsWith(QStringLiteral(".map")) && quake.statistics.brushes > 20
			&& quake.statistics.rooms >= 6,
		"A Quake level is generated as .map text with brushes and rooms.");
	expect(quakeText.contains(QStringLiteral("\"classname\" \"worldspawn\"")) && quakeText.contains(QStringLiteral("\"wad\" \"gfx/medieval.wad\""))
			&& quakeText.contains(QStringLiteral("info_player_start")) && quakeText.contains(QStringLiteral("trigger_changelevel"))
			&& quakeText.contains(QStringLiteral("\"classname\" \"light\"")) && quakeText.contains(QStringLiteral("monster_")),
		"The Quake map has worldspawn with its WAD, a start, an exit trigger, lights, and monsters.");
	expect(quake.statistics.reachableRooms == quake.statistics.rooms, "Every Quake room is reachable from the start on foot.");
	expect(borderClosed(quake.layout), "No open cell touches the edge: the level is sealed.");
	QString error;
	LevelMapDocument document = readBack(quake, &error);
	LevelMapStatistics read = levelMapStatistics(document);
	expect(error.isEmpty() && read.errorCount == 0 && read.degenerateBrushCount == 0 && read.brushCount >= quake.statistics.brushes
			&& read.entityCount == quake.statistics.entities,
		"The editor's parser reads every Quake brush and entity back, with no errors.");
	expect(generateLevel(quakeSpec).mapBytes == quake.mapBytes, "The same spec makes the same bytes.");
	LevelGenerationSpec reseeded = quakeSpec;
	reseeded.seed = 99;
	expect(generateLevel(reseeded).mapBytes != quake.mapBytes, "Another seed makes another level.");

	// Quake II deathmatch.
	const LevelGenerationResult quake2 = generateLevel(specFor(QStringLiteral("quake 2 deathmatch, 6 rooms, slime, seed 7")));
	if (!quake2.ok) {
		std::cerr << "quake2: " << quake2.error.toStdString() << "\n";
	}
	const QString quake2Text = QString::fromUtf8(quake2.mapBytes);
	expect(quake2.ok && quake2Text.startsWith(QString::fromLatin1(kQuake2MapTargetHeader).trimmed()) && quake2Text.count(QStringLiteral("info_player_deathmatch")) >= 8
			&& !quake2Text.contains(QStringLiteral("monster_")) && quake2Text.contains(QStringLiteral(" 0 0 0\n")),
		"A Quake II deathmatch keeps its target header, has a start per player and no monsters, and writes face flags.");
	document = readBack(quake2, &error);
	read = levelMapStatistics(document);
	expect(error.isEmpty() && read.errorCount == 0 && read.degenerateBrushCount == 0, "The Quake II map reads back cleanly.");

	// Quake III duel.
	const LevelGenerationResult quake3 = generateLevel(specFor(QStringLiteral("quake 3 duel, tech base, seed 5")));
	if (!quake3.ok) {
		std::cerr << "quake3: " << quake3.error.toStdString() << "\n";
	}
	const QString quake3Text = QString::fromUtf8(quake3.mapBytes);
	expect(quake3.ok && quake3Text.contains(QStringLiteral("Q3Radiant")) && quake3Text.contains(QStringLiteral("info_player_intermission"))
			&& quake3Text.contains(QStringLiteral("common/caulk")) && quake3Text.contains(QStringLiteral("base_wall/concrete")) && !quake3Text.contains(QStringLiteral("textures/")),
		"A Quake III duel has an intermission camera, caulked hidden faces, and shader names without textures/.");
	document = readBack(quake3, &error);
	read = levelMapStatistics(document);
	expect(error.isEmpty() && read.errorCount == 0 && document.format == LevelMapFormat::Quake3Map, "The Quake III map reads back as Quake III.");

	// Doom.
	const LevelGenerationResult doom = generateLevel(specFor(QStringLiteral("doom tech base with nukage, 6 rooms, seed 77")));
	if (!doom.ok) {
		std::cerr << "doom: " << doom.error.toStdString() << "\n";
	}
	expect(doom.ok && doom.format == QStringLiteral("doom-wad") && doom.mapBytes.startsWith("PWAD") && doom.statistics.sectors > 5 && doom.statistics.linedefs > 20
			&& doom.mapName == QStringLiteral("MAP01"),
		"A Doom level is a PWAD with sectors and linedefs in MAP01.");
	document = readBack(doom, &error);
	read = levelMapStatistics(document);
	if (!error.isEmpty() || read.errorCount > 0) {
		std::cerr << "doom read back: " << error.toStdString() << " errors=" << read.errorCount << "\n";
		for (const QString& line : levelMapValidationLines(document).mid(0, 8)) {
			std::cerr << "  " << line.toStdString() << "\n";
		}
	}
	expect(error.isEmpty() && read.doomSectorCount == doom.statistics.sectors && read.doomLinedefCount == doom.statistics.linedefs && read.doomThingCount == doom.statistics.entities
			&& read.errorCount == 0,
		"The editor reads the Doom map back with the same sectors, lines, and things, and no errors.");
	bool exitLine = false;
	for (const LevelMapDoomLinedef& line : document.doomLinedefs) {
		exitLine = exitLine || line.special == 52;
	}
	expect(exitLine, "The Doom exit pad is ringed by W1 exit lines.");

	// A supplied plan, as a model would give it.
	LevelSemanticPlan plan;
	plan.planner = QStringLiteral("ai:test/model");
	plan.title = QStringLiteral("Model Made");
	plan.rooms = {
		{QStringLiteral("gate"), QStringLiteral("Gate"), QStringLiteral("start"), QStringLiteral("small"), QStringLiteral("normal"), 0, QStringLiteral("box"), QStringLiteral("none"), QStringLiteral("normal")},
		{QStringLiteral("pool"), QStringLiteral("Pool"), QStringLiteral("arena"), QStringLiteral("large"), QStringLiteral("tall"), 1, QStringLiteral("pit"), QStringLiteral("lava"), QStringLiteral("bright")},
		{QStringLiteral("dais"), QStringLiteral("Dais"), QStringLiteral("hub"), QStringLiteral("large"), QStringLiteral("normal"), 1, QStringLiteral("platform"), QStringLiteral("none"), QStringLiteral("dim")},
		{QStringLiteral("hall"), QStringLiteral("Hall"), QStringLiteral("hall"), QStringLiteral("medium"), QStringLiteral("normal"), 0, QStringLiteral("pillars"), QStringLiteral("none"), QStringLiteral("normal")},
		{QStringLiteral("out"), QStringLiteral("Out"), QStringLiteral("exit"), QStringLiteral("small"), QStringLiteral("low"), 0, QStringLiteral("box"), QStringLiteral("none"), QStringLiteral("normal")},
	};
	plan.connections = {
		{QStringLiteral("gate"), QStringLiteral("pool"), QStringLiteral("stairs")},
		{QStringLiteral("pool"), QStringLiteral("dais"), QStringLiteral("open")},
		{QStringLiteral("dais"), QStringLiteral("hall"), QStringLiteral("stairs")},
		{QStringLiteral("hall"), QStringLiteral("out"), QStringLiteral("corridor")},
		{QStringLiteral("gate"), QStringLiteral("hall"), QStringLiteral("corridor")},
	};
	plan.placements = {
		{QStringLiteral("gate"), QStringLiteral("player-start"), QString(), 1},
		{QStringLiteral("pool"), QStringLiteral("monster"), QStringLiteral("ogre"), 2},
		{QStringLiteral("dais"), QStringLiteral("weapon"), QStringLiteral("rocketlauncher"), 1},
		{QStringLiteral("out"), QStringLiteral("exit"), QString(), 1},
	};
	const LevelGenerationResult planned = generateLevel(specFor(QStringLiteral("seed 3"), QStringLiteral("quake")), &plan);
	if (!planned.ok) {
		std::cerr << "planned: " << planned.error.toStdString() << "\n";
	}
	const QString plannedText = QString::fromUtf8(planned.mapBytes);
	expect(planned.ok && planned.plan.planner == QStringLiteral("ai:test/model") && planned.layout.rooms.size() == 5 && plannedText.contains(QStringLiteral("*lava1"))
			&& plannedText.contains(QStringLiteral("weapon_rocketlauncher")) && plannedText.count(QStringLiteral("monster_ogre")) == 2,
		"A supplied plan is built as given: its lava pit, its rocket launcher, its two ogres.");
	bool raised = false;
	for (const LevelCell& cell : planned.layout.cells) {
		raised = raised || (cell.open && cell.owner == QStringLiteral("dais") && cell.floor > 64);
	}
	expect(raised, "The platform room has a stepped dais above its floor.");
	expect(planned.statistics.reachableRooms == 5, "Every room of the supplied plan is reachable, up the stairs.");

	// Textures from the project.
	LevelGenerationSpec textured = specFor(QStringLiteral("seed 4"), QStringLiteral("quake"));
	textured.availableTextures = {QStringLiteral("brick_wall_1"), QStringLiteral("stone_floor_2"), QStringLiteral("ceiling_metal"), QStringLiteral("*lava_hot"), QStringLiteral("sky3")};
	textured.textures.insert(QStringLiteral("trim"), QStringLiteral("my_trim"));
	const QString texturedText = QString::fromUtf8(generateLevel(textured).mapBytes);
	expect(texturedText.contains(QStringLiteral(" brick_wall_1 ")) && texturedText.contains(QStringLiteral(" stone_floor_2 ")) && texturedText.contains(QStringLiteral(" ceiling_metal "))
			&& !texturedText.contains(QStringLiteral(" sky3 ")),
		"A project's own textures are matched to roles, and skies are never used for walls.");

	// The preview and the report.
	const QImage preview = renderLevelLayoutPreview(quake, QSize(320, 240));
	int lit = 0;
	for (int y = 0; y < preview.height(); y += 4) {
		for (int x = 0; x < preview.width(); x += 4) {
			lit += preview.pixel(x, y) != QColor(24, 26, 31).rgb() ? 1 : 0;
		}
	}
	expect(preview.size() == QSize(320, 240) && lit > 200, "The preview draws the layout.");
	const QJsonObject report = levelGenerationReportJson(quake);
	expect(report.value(QStringLiteral("ok")).toBool() && report.value(QStringLiteral("plan")).toObject().value(QStringLiteral("rooms")).toArray().size() == quake.plan.rooms.size()
			&& report.value(QStringLiteral("statistics")).toObject().value(QStringLiteral("brushes")).toInt() == quake.statistics.brushes
			&& report.value(QStringLiteral("spec")).toObject().value(QStringLiteral("seed")).toInteger() == 1234,
		"The report carries the spec, plan, and statistics.");
	expect(levelGenerationSummaryLines(quake).size() == 3, "The summary is three lines.");
}

void checkAiPlanning()
{
	// A model's plan through the real transport, from a fake provider.
	FakeAiProvider provider;
	const LevelSemanticPlan rules = rulesLevelPlan(specFor(QStringLiteral("deathmatch, 5 rooms, seed 21"), QStringLiteral("quake")));
	QJsonObject answer = levelSemanticPlanJson(rules);
	answer.insert(QStringLiteral("title"), QStringLiteral("Fake Model Arena"));
	provider.answer(200, FakeAiProvider::openAiAnswer(QString::fromUtf8(QJsonDocument(answer).toJson(QJsonDocument::Compact))));
	const LevelGenerationSpec spec = specFor(QStringLiteral("deathmatch, 5 rooms, seed 21"), QStringLiteral("quake"));
	AiChatClient client;
	std::optional<AiChatResponse> response;
	const AiChatRequest request = levelPlanAiRequest(spec, QStringLiteral("local-offline"), QStringLiteral("fake"), provider.baseUrl(QStringLiteral("/v1")));
	expect(client.send(request, QString(), [&response](const AiChatResponse& value) { response = value; }), "The plan request is sent.");
	expect(waitUntil([&response]() { return response.has_value(); }), "The fake model answers.");
	LevelSemanticPlan plan;
	QStringList problems;
	expect(response && response->ok && levelSemanticPlanFromAnswer(response->text, spec, &plan, &problems) && problems.isEmpty(), "Its answer is a valid plan.");
	plan.planner = QStringLiteral("ai:local-offline/fake");
	const LevelGenerationResult result = generateLevel(spec, &plan);
	expect(result.ok && result.plan.title == QStringLiteral("Fake Model Arena") && QString::fromUtf8(result.mapBytes).contains(QStringLiteral("Fake Model Arena")),
		"The model's plan becomes the level, title and all.");
	const QJsonObject sent = QJsonDocument::fromJson(provider.exchanges().value(0).body).object();
	expect(sent.value(QStringLiteral("response_format")).toObject().value(QStringLiteral("type")).toString() == QStringLiteral("json_schema"),
		"The request asked the runtime for structured output.");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	checkSpecs();
	checkPlans();
	checkGeneration();
	checkAiPlanning();
	if (failures > 0) {
		std::cerr << failures << " level generation check(s) failed.\n";
		return EXIT_FAILURE;
	}
	std::cout << "Level generation checks passed.\n";
	return EXIT_SUCCESS;
}
