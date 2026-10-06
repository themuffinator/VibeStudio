#include "core/level_ai_edit.h"

#include "core/level_generation.h"
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

// A generated level, as the editor holds it: no file, ids from the parser.
LevelMapDocument generatedMap(const QString& game)
{
	LevelGenerationSpec spec;
	spec.prompt = QStringLiteral("small base, 4 rooms, seed 77");
	spec.game = game;
	const LevelGenerationResult result = generateLevel(spec);
	LevelMapDocument document;
	QString error;
	expect(result.ok && levelGenerationDocument(result, &document, &error), "A generated level opens as a document.");
	return document;
}

int entityWith(const LevelMapDocument& document, const QString& className)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.className == className) {
			return entity.id;
		}
	}
	return -1;
}

const LevelMapEntity* entityById(const LevelMapDocument& document, int id)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.id == id) {
			return &entity;
		}
	}
	return nullptr;
}

QString keyOf(const LevelMapDocument& document, int id, const QString& key)
{
	const LevelMapEntity* entity = entityById(document, id);
	if (!entity) {
		return {};
	}
	for (const LevelMapProperty& property : entity->properties) {
		if (property.key == key) {
			return property.value;
		}
	}
	return {};
}

QJsonObject action(const QString& kind, const QStringList& targets = {})
{
	return QJsonObject {
		{QStringLiteral("kind"), kind},
		{QStringLiteral("reason"), QStringLiteral("Because the test asks.")},
		{QStringLiteral("targets"), QJsonArray::fromStringList(targets)},
		{QStringLiteral("classname"), QString()},
		{QStringLiteral("origin"), QJsonArray()},
		{QStringLiteral("keys"), QJsonArray()},
		{QStringLiteral("key"), QString()},
		{QStringLiteral("value"), QString()},
		{QStringLiteral("mins"), QJsonArray()},
		{QStringLiteral("maxs"), QJsonArray()},
		{QStringLiteral("texture"), QString()},
		{QStringLiteral("delta"), QJsonArray()},
	};
}

QJsonObject with(QJsonObject object, const QString& key, const QJsonValue& value)
{
	object.insert(key, value);
	return object;
}

bool closedEverywhere(const QJsonObject& schema)
{
	if (schema.value(QStringLiteral("type")).toString() == QStringLiteral("object")) {
		const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
		const QJsonArray required = schema.value(QStringLiteral("required")).toArray();
		if (schema.value(QStringLiteral("additionalProperties")).toBool(true) || required.size() != properties.size()) {
			return false;
		}
		for (auto it = properties.constBegin(); it != properties.constEnd(); ++it) {
			if (!required.contains(it.key()) || !closedEverywhere(it.value().toObject())) {
				return false;
			}
		}
	}
	if (schema.value(QStringLiteral("type")).toString() == QStringLiteral("array")) {
		return closedEverywhere(schema.value(QStringLiteral("items")).toObject());
	}
	return true;
}

void checkSchema()
{
	const QJsonObject schema = levelAiEditSchema();
	expect(closedEverywhere(schema), "Every object in the proposal schema is closed, with every property required.");
	const QJsonObject actionSchema = schema.value(QStringLiteral("properties")).toObject().value(QStringLiteral("actions")).toObject().value(QStringLiteral("items")).toObject();
	QStringList kinds;
	for (const QJsonValue& kind : actionSchema.value(QStringLiteral("properties")).toObject().value(QStringLiteral("kind")).toObject().value(QStringLiteral("enum")).toArray()) {
		kinds << kind.toString();
	}
	expect(kinds == levelAiEditActionKinds() && kinds.size() == 7, "The schema's action kinds are the seven the editor applies.");
}

void checkContext()
{
	LevelMapDocument document = generatedMap(QStringLiteral("quake"));
	const QString home = QStringLiteral("C:/Users/mapper");
	expect(setLevelMapEntityProperty(&document, 0, QStringLiteral("_source"), home + QStringLiteral("/maps/secret.map")), "A key with a home path is set.");
	const QString context = levelAiEditMapContext(document, QString(), home);
	expect(context.contains(QStringLiteral("entity:0 worldspawn")), "The summary names the worldspawn by its selector.");
	expect(context.contains(QStringLiteral("info_player_start at (")), "Point entities carry their origins.");
	expect(context.contains(QStringLiteral("brush:")) && context.contains(QStringLiteral(" of entity:0 from (")), "Brushes carry their owner and bounds.");
	expect(context.contains(QStringLiteral("Textures in use (faces): ")), "The summary lists the textures in use.");
	expect(context.contains(QStringLiteral("~/maps/secret.map")) && !context.contains(home), "Home paths are shortened.");
	expect(context.contains(QStringLiteral("entity:0 worldspawn (owns ")), "Brush entities say how many brushes they own.");
	clearLevelMapSelection(&document);
	expect(levelAiEditMapContext(document).contains(QStringLiteral("Selected: nothing")), "The summary says when nothing is selected.");
	expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, 1}, {LevelMapSelectionKind::QuakeBrush, document.brushes.first().id}})
			&& levelAiEditMapContext(document).contains(QStringLiteral("Selected: entity:1, brush:%1").arg(document.brushes.first().id)),
		"The summary names the selection, so a request can say \"the selected\".");
	const QString capped = levelAiEditMapContext(document, QString(), QString(), 2);
	expect(capped.contains(QStringLiteral("more entities not listed")) && capped.contains(QStringLiteral("more brushes not listed")), "What is left out is counted.");
	const int farBrush = document.brushes.last().id;
	expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, farBrush}})
			&& levelAiEditMapContext(document, QString(), QString(), 2).contains(QStringLiteral("brush:%1 of entity:").arg(farBrush)),
		"A selected object is described even past the cap.");

	const AiChatRequest request = levelAiEditRequest(document, QStringLiteral("Add a light."), QStringLiteral("openai"), QStringLiteral("gpt-test"), QString(), QString(), home);
	expect(request.responseSchemaName == QStringLiteral("map_edit_proposal") && !request.responseSchema.isEmpty(), "The request asks for a proposal by schema.");
	expect(request.messages.size() == 1 && request.messages.first().text.contains(QStringLiteral("## Request\nAdd a light.")), "The instruction follows the map summary.");
	expect(request.system.contains(QStringLiteral("never write map text")), "The standing instructions keep the model to actions.");
}

void checkValidationAndApply()
{
	LevelMapDocument document = generatedMap(QStringLiteral("quake"));
	const int start = entityWith(document, QStringLiteral("info_player_start"));
	const int light = entityWith(document, QStringLiteral("light"));
	expect(start > 0 && light > 0 && document.brushes.size() > 4, "The generated map has a start, lights, and brushes.");
	const int brush = document.brushes.at(1).id;
	// A worldspawn brush: deleting a brush entity's only brush takes the entity too.
	int lastBrush = -1;
	for (const LevelMapBrush& each : document.brushes) {
		lastBrush = each.entityId == 0 ? each.id : lastBrush;
	}
	const LevelMapVec3 startOrigin = entityById(document, start)->origin;
	const int entities = document.entities.size();
	const int brushes = document.brushes.size();
	const QString startSelector = QStringLiteral("entity:%1").arg(start);
	const QString lightSelector = QStringLiteral("entity:%1").arg(light);

	QJsonArray actions;
	// 0: valid add-entity.
	actions.append(with(with(with(action(QStringLiteral("add-entity")), QStringLiteral("classname"), QStringLiteral("item_health")), QStringLiteral("origin"), QJsonArray {16, 32, 24}),
		QStringLiteral("keys"), QJsonArray {QJsonObject {{QStringLiteral("key"), QStringLiteral("spawnflags")}, {QStringLiteral("value"), QStringLiteral("1")}}}));
	// 1: valid set-key.
	actions.append(with(with(action(QStringLiteral("set-key"), {lightSelector}), QStringLiteral("key"), QStringLiteral("light")), QStringLiteral("value"), QStringLiteral("400")));
	// 2: valid add-box.
	actions.append(with(with(with(action(QStringLiteral("add-box")), QStringLiteral("mins"), QJsonArray {-32, -32, 0}), QStringLiteral("maxs"), QJsonArray {32, 32, 16}),
		QStringLiteral("texture"), QStringLiteral("crate_top")));
	// 3: valid set-texture.
	actions.append(with(action(QStringLiteral("set-texture"), {QStringLiteral("brush:%1").arg(brush)}), QStringLiteral("texture"), QStringLiteral("metal5_2")));
	// 4: valid move.
	actions.append(with(action(QStringLiteral("move"), {startSelector}), QStringLiteral("delta"), QJsonArray {32, 0, 0}));
	// 5: valid delete, listed before others but run last.
	actions.append(action(QStringLiteral("delete"), {QStringLiteral("brush:%1").arg(lastBrush)}));
	// 6-10: invalid ones.
	actions.append(action(QStringLiteral("delete"), {QStringLiteral("entity:0")}));
	actions.append(action(QStringLiteral("move"), {QStringLiteral("brush:999999")}));
	actions.append(action(QStringLiteral("explode"), {startSelector}));
	actions.append(with(with(action(QStringLiteral("set-key"), {QStringLiteral("brush:%1").arg(brush)}), QStringLiteral("key"), QStringLiteral("light")), QStringLiteral("value"), QStringLiteral("1")));
	actions.append(with(with(with(action(QStringLiteral("add-box")), QStringLiteral("mins"), QJsonArray {0, 0, 0}), QStringLiteral("maxs"), QJsonArray {0, 64, 64}),
		QStringLiteral("texture"), QStringLiteral("crate_top")));
	// 11: set-key with a quote in the value.
	actions.append(with(with(action(QStringLiteral("set-key"), {lightSelector}), QStringLiteral("key"), QStringLiteral("message")), QStringLiteral("value"), QStringLiteral("say \"hi\"")));
	// 12: set-texture on a point entity.
	actions.append(with(action(QStringLiteral("set-texture"), {startSelector}), QStringLiteral("texture"), QStringLiteral("metal5_2")));
	const QJsonObject answer {{QStringLiteral("summary"), QStringLiteral("Test edits.")}, {QStringLiteral("actions"), actions}};

	LevelAiEditProposal proposal;
	QString error;
	expect(levelAiEditProposalFromJson(answer, &proposal, &error) && proposal.actions.size() == 13, "The proposal reads.");
	expect(proposal.problems.size() == 1 && proposal.problems.first().contains(QStringLiteral("$.actions[8].kind is \"explode\"")),
		"The schema check names the one unknown kind and passes the rest.");
	validateLevelAiEditProposal(document, &proposal);
	for (int index = 0; index < 6; ++index) {
		if (!proposal.actions.at(index).valid() || !proposal.actions.at(index).enabled) {
			std::cerr << "  action " << index << ": " << proposal.actions.at(index).problems.join(QStringLiteral("; ")).toStdString() << "\n";
		}
		expect(proposal.actions.at(index).valid() && proposal.actions.at(index).enabled, "Each sound action passes and starts checked.");
	}
	for (int index = 6; index < 13; ++index) {
		expect(!proposal.actions.at(index).valid() && !proposal.actions.at(index).enabled, "Each unsound action fails and starts unchecked.");
	}
	expect(proposal.actions.at(6).problems.join(QString()).contains(QStringLiteral("worldspawn")), "Deleting the worldspawn is refused by name.");
	expect(proposal.actions.at(7).problems.join(QString()).contains(QStringLiteral("brush:999999")), "A missing target is named.");
	expect(proposal.actions.at(0).description == QStringLiteral("Add item_health at (16 32 24) with spawnflags=1."), "Actions are described in words.");
	expect(proposal.actions.at(4).description == QStringLiteral("Move %1 by (32 0 0).").arg(startSelector), "A move is described with its offset.");

	const QJsonObject json = levelAiEditProposalJson(proposal);
	LevelAiEditProposal reread;
	expect(levelAiEditProposalFromJson(json, &reread) && reread.actions.size() == 13 && reread.actions.at(2).maxs.z == 16.0,
		"A saved proposal reads back.");

	// The reviewer unchecks the add-box; the rest of the sound ones run.
	proposal.actions[2].enabled = false;
	const int undoBefore = document.undoStack.size();
	const LevelAiEditApplyReport report = applyLevelAiEditProposal(&document, proposal);
	expect(report.applied == 5 && report.skipped == 8 && report.errors.isEmpty(), "The checked, sound actions run and the rest are skipped.");
	expect(report.appliedActions == QVector<int>({0, 1, 3, 4, 5}) && report.failedActions.isEmpty(), "The report says which actions ran, deletes last.");
	if (!report.errors.isEmpty()) {
		std::cerr << "  " << report.errors.join(QStringLiteral("\n  ")).toStdString() << "\n";
	}
	expect(document.undoStack.size() == undoBefore + 5, "Each action is its own undo step.");
	expect(report.lines.last().contains(QStringLiteral("Delete brush:%1").arg(lastBrush)), "Deletes run last.");
	expect(entityWith(document, QStringLiteral("item_health")) > 0 && document.entities.size() == entities + 1, "The entity was added.");
	expect(keyOf(document, light, QStringLiteral("light")) == QStringLiteral("400"), "The key was set.");
	const LevelMapEntity* moved = entityById(document, start);
	expect(moved && moved->origin.x == startOrigin.x + 32.0 && moved->origin.y == startOrigin.y, "The start moved.");
	expect(document.brushes.size() == brushes - 1, "One brush was deleted and none added.");
	bool retextured = false;
	for (const LevelMapBrush& each : document.brushes) {
		if (each.id == brush) {
			retextured = std::all_of(each.faces.cbegin(), each.faces.cend(), [](const LevelMapBrushFace& face) { return face.textureName == QStringLiteral("metal5_2"); });
		}
	}
	expect(retextured, "The brush was retextured on every face.");

	const LevelMapSerialized saved = serializeLevelMap(document);
	expect(saved.succeeded(), "The edited map writes.");
	LevelMapDocument reloaded;
	LevelMapLoadRequest load;
	load.path = QStringLiteral("edited.map");
	load.mapName = QStringLiteral("edited");
	const bool loaded = loadLevelMapBytes(load, saved.bytes, &reloaded, &error);
	expect(loaded && reloaded.entities.size() == entities + 1 && reloaded.brushes.size() == brushes - 1, "The edited map reads back.");
	if (!loaded || reloaded.entities.size() != entities + 1 || reloaded.brushes.size() != brushes - 1) {
		std::cerr << "  " << error.toStdString() << " entities " << reloaded.entities.size() << "/" << entities + 1 << " brushes " << reloaded.brushes.size() << "/"
				  << brushes - 1 << "\n";
	}

	for (int step = 0; step < 5; ++step) {
		expect(undoLevelMapEdit(&document, &error), "Each step undoes.");
	}
	expect(document.entities.size() == entities && document.brushes.size() == brushes && keyOf(document, light, QStringLiteral("light")) != QStringLiteral("400"),
		"Undoing every step restores the map.");
}

void checkStaleTargets()
{
	// An entity deleted with its brushes leaves a later action on them nothing to do.
	LevelMapDocument document = generatedMap(QStringLiteral("quake"));
	int brushEntity = -1;
	int ownBrush = -1;
	for (const LevelMapBrush& brush : document.brushes) {
		if (brush.entityId > 0) {
			brushEntity = brush.entityId;
			ownBrush = brush.id;
			break;
		}
	}
	expect(brushEntity > 0, "The generated map has a brush entity (its exit trigger).");
	QJsonArray actions;
	actions.append(action(QStringLiteral("delete"), {QStringLiteral("entity:%1").arg(brushEntity)}));
	actions.append(action(QStringLiteral("delete"), {QStringLiteral("brush:%1").arg(ownBrush)}));
	LevelAiEditProposal proposal;
	expect(levelAiEditProposalFromJson(QJsonObject {{QStringLiteral("summary"), QString()}, {QStringLiteral("actions"), actions}}, &proposal), "The proposal reads.");
	validateLevelAiEditProposal(document, &proposal);
	const LevelAiEditApplyReport report = applyLevelAiEditProposal(&document, proposal);
	expect(report.applied == 1 && report.skipped == 1 && report.errors.size() == 1 && report.errors.first().contains(QStringLiteral("no longer in the map")),
		"A target an earlier action removed is reported, not guessed at.");
	expect(report.failedActions == QVector<int>({1}), "The report names the action that failed.");
}

void checkDoomRefused()
{
	LevelMapDocument document = generatedMap(QStringLiteral("doom"));
	QJsonArray actions;
	actions.append(with(with(action(QStringLiteral("add-entity")), QStringLiteral("classname"), QStringLiteral("light")), QStringLiteral("origin"), QJsonArray {0, 0, 0}));
	LevelAiEditProposal proposal;
	expect(levelAiEditProposalFromJson(QJsonObject {{QStringLiteral("summary"), QString()}, {QStringLiteral("actions"), actions}}, &proposal), "The proposal reads.");
	validateLevelAiEditProposal(document, &proposal);
	expect(!proposal.actions.first().enabled && proposal.actions.first().problems.join(QString()).contains(QStringLiteral("Quake-family")), "Doom maps are refused.");
}

void checkModelRoundTrip()
{
	FakeAiProvider provider;
	LevelMapDocument document = generatedMap(QStringLiteral("quake"));
	const int light = entityWith(document, QStringLiteral("light"));
	QJsonArray actions;
	actions.append(with(with(action(QStringLiteral("set-key"), {QStringLiteral("entity:%1").arg(light)}), QStringLiteral("key"), QStringLiteral("light")), QStringLiteral("value"), QStringLiteral("500")));
	const QJsonObject answer {{QStringLiteral("summary"), QStringLiteral("Brighter.")}, {QStringLiteral("actions"), actions}};
	// Prose around a fenced block, as a model without structured output answers.
	const QString text = QStringLiteral("Here it is:\n```json\n%1\n```\nDone.").arg(QString::fromUtf8(QJsonDocument(answer).toJson(QJsonDocument::Indented)));
	provider.answer(200, FakeAiProvider::openAiAnswer(text));
	AiChatClient client;
	std::optional<AiChatResponse> response;
	const AiChatRequest request = levelAiEditRequest(document, QStringLiteral("Make the first light brighter."), QStringLiteral("local-offline"), QStringLiteral("fake"),
		provider.baseUrl(QStringLiteral("/v1")));
	expect(client.send(request, QString(), [&response](const AiChatResponse& value) { response = value; }), "The edit request is sent.");
	expect(waitUntil([&response]() { return response.has_value(); }), "The fake model answers.");
	LevelAiEditProposal proposal;
	QString error;
	expect(response && response->ok && levelAiEditProposalFromAnswer(response->text, &proposal, &error), "Its answer is a proposal.");
	validateLevelAiEditProposal(document, &proposal);
	expect(proposal.summary == QStringLiteral("Brighter.") && proposal.actions.size() == 1 && proposal.actions.first().enabled, "The proposal is sound.");
	const QJsonObject sent = QJsonDocument::fromJson(provider.exchanges().value(0).body).object();
	const QJsonObject format = sent.value(QStringLiteral("response_format")).toObject();
	expect(format.value(QStringLiteral("type")).toString() == QStringLiteral("json_schema")
			&& format.value(QStringLiteral("json_schema")).toObject().value(QStringLiteral("name")).toString() == QStringLiteral("map_edit_proposal"),
		"The request asked for the proposal schema.");
	expect(QString::fromUtf8(provider.exchanges().value(0).body).contains(QStringLiteral("entity:0 worldspawn")), "The map summary went with it.");
	expect(!levelAiEditProposalFromAnswer(QStringLiteral("I cannot help with that."), &proposal, &error) && !error.isEmpty(), "Prose alone is no proposal.");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	checkSchema();
	checkContext();
	checkValidationAndApply();
	checkStaleTargets();
	checkDoomRefused();
	checkModelRoundTrip();
	if (failures > 0) {
		std::cerr << failures << " map edit check(s) failed.\n";
		return EXIT_FAILURE;
	}
	std::cout << "Map edit checks passed.\n";
	return EXIT_SUCCESS;
}
