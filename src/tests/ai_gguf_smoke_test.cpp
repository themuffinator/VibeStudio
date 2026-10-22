// The Local GGUF connector with a real model: free text, schema-constrained answers, the
// AiChatClient path with cancellation, and Quake-MapGen planning under its own grammar with GGUF
// provenance. Needs a build with -Dggml=enabled and VIBESTUDIO_TEST_GGUF naming a small chat model
// (SmolLM2-135M-Instruct is enough); otherwise it reports itself skipped.

#include "core/ai_gguf_runtime.h"
#include "core/ai_json_grammar.h"
#include "core/ai_transport.h"
#include "core/level_mapgen.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>

using namespace vibestudio;

namespace {

int failures = 0;

void expect(bool condition, const char* message, const QString& detail = QString())
{
	if (!condition) {
		++failures;
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty()) {
			std::cerr << " (" << detail.toStdString() << ")";
		}
		std::cerr << "\n";
	}
}

bool waitUntil(const std::function<bool()>& done, int msecs)
{
	QElapsedTimer clock;
	clock.start();
	while (!done() && clock.elapsed() < msecs) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
	}
	return done();
}

QJsonObject choiceSchema()
{
	return QJsonObject {
		{QStringLiteral("type"), QStringLiteral("object")},
		{QStringLiteral("additionalProperties"), false},
		{QStringLiteral("required"), QJsonArray {QStringLiteral("game"), QStringLiteral("rooms")}},
		{QStringLiteral("properties"), QJsonObject {
			{QStringLiteral("game"), QJsonObject {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray {QStringLiteral("quake"), QStringLiteral("doom")}}}},
			{QStringLiteral("rooms"), QJsonObject {{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 2}, {QStringLiteral("maximum"), 9}}},
		}},
	};
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	const QString model = qEnvironmentVariable("VIBESTUDIO_TEST_GGUF");
	if (!aiGgufRuntimeAvailable() || !aiGgufModelProblem(model).isEmpty()) {
		std::cout << "Skipped: needs -Dggml=enabled and VIBESTUDIO_TEST_GGUF naming a .gguf chat model.\n";
		return 77;
	}

	AiGgufRequest free;
	free.modelPath = model;
	free.system = QStringLiteral("You answer briefly.");
	free.messages = {{QStringLiteral("user"), QStringLiteral("Name one colour.")}};
	free.maxOutputTokens = 24;
	const AiGgufResult text = runAiGguf(free);
	expect(text.ok && !text.text.trimmed().isEmpty() && text.promptTokens > 0 && text.outputTokens > 0 && text.modelSha256.size() == 64
			&& text.sampling == QStringLiteral("penalties+greedy"),
		"A GGUF model answers in free text and is named by its SHA-256.", text.error);
	const AiGgufResult again = runAiGguf(free);
	expect(again.ok && again.text == text.text, "Greedy decoding gives the same answer twice.");

	// The window is the model's, whatever the prompt: a planner's revision prompt is longer than
	// its first, and must see the same inference settings.
	AiGgufRequest longer = free;
	longer.messages = {{QStringLiteral("user"), QStringLiteral("Name one colour. ").repeated(40)}};
	const AiGgufResult wider = runAiGguf(longer);
	expect(wider.ok && wider.promptTokens > text.promptTokens && wider.contextTokens == text.contextTokens && text.contextTokens >= text.promptTokens + free.maxOutputTokens,
		"Every request to a model reports the same context window.", QStringLiteral("%1 and %2").arg(text.contextTokens).arg(wider.contextTokens));
	AiGgufRequest cut = free;
	cut.messages = {{QStringLiteral("user"), QStringLiteral("Count from one to fifty in words.")}};
	cut.maxOutputTokens = 3;
	const AiGgufResult cutShort = runAiGguf(cut);
	expect(cutShort.ok && cutShort.truncated && cutShort.outputTokens == 3, "An answer stopped at its limit says so, with the limit's token count.");

	AiGgufRequest constrained = free;
	constrained.messages = {{QStringLiteral("user"), QStringLiteral("Pick a game and a room count, as JSON.")}};
	constrained.grammar = aiJsonSchemaGrammar(choiceSchema());
	constrained.maxOutputTokens = 64;
	const AiGgufResult json = runAiGguf(constrained);
	const QJsonObject object = QJsonDocument::fromJson(json.text.toUtf8()).object();
	expect(json.ok && json.sampling == QStringLiteral("gbnf+greedy") && aiJsonSchemaProblems(object, choiceSchema()).isEmpty(),
		"Under a schema's grammar the model can only write a valid object.", json.error + json.text);

	// Through AiChatClient, as every pipeline sends: structured output, then a cancel.
	AiChatRequest request;
	request.connectorId = QStringLiteral("local-gguf");
	request.model = model;
	request.messages = constrained.messages;
	request.responseSchema = choiceSchema();
	request.maxOutputTokens = 64;
	AiChatClient client;
	std::optional<AiChatResponse> answer;
	QString error;
	expect(client.send(request, QString(), [&answer](const AiChatResponse& response) { answer = response; }, &error), "The GGUF request starts.", error);
	expect(waitUntil([&answer] { return answer.has_value(); }, 120000) && answer->ok && answer->modelSha256 == text.modelSha256
			&& aiJsonSchemaProblems(QJsonDocument::fromJson(answer->text.toUtf8()).object(), choiceSchema()).isEmpty(),
		"AiChatClient runs the model on a worker and answers to the schema.", answer ? answer->errorMessage : QString());
	answer.reset();
	request.responseSchema = QJsonObject();
	request.maxOutputTokens = 2000;
	request.messages = {{QStringLiteral("user"), QStringLiteral("Write a very long story about a castle.")}};
	expect(client.send(request, QString(), [&answer](const AiChatResponse& response) { answer = response; }, &error), "A long request starts.", error);
	QTimer::singleShot(200, [&client] { client.cancel(); });
	expect(waitUntil([&answer] { return answer.has_value(); }, 120000) && answer->failure == AiChatFailure::Cancelled, "Cancelling stops the model.");

	// A run waiting for the model behind another can be cancelled without waiting its turn.
	answer.reset();
	AiChatClient queued;
	std::optional<AiChatResponse> queuedAnswer;
	expect(client.send(request, QString(), [&answer](const AiChatResponse& response) { answer = response; }, &error)
			&& queued.send(request, QString(), [&queuedAnswer](const AiChatResponse& response) { queuedAnswer = response; }, &error),
		"Two GGUF requests start.", error);
	QTimer::singleShot(300, [&queued] { queued.cancel(); });
	QElapsedTimer waited;
	waited.start();
	expect(waitUntil([&queuedAnswer] { return queuedAnswer.has_value(); }, 120000) && queuedAnswer->failure == AiChatFailure::Cancelled && waited.elapsed() < 5000 && !answer,
		"The queued one ends at once while the first carries on.", QString::number(waited.elapsed()));
	client.cancel();
	waitUntil([&answer] { return answer.has_value(); }, 120000);

	// Quake-MapGen plans under its own grammar, with GGUF provenance in its report.
	LevelMapgenModel planner;
	planner.connection.connectorId = QStringLiteral("local-gguf");
	planner.connection.displayName = QStringLiteral("Local GGUF");
	planner.connection.model = model;
	planner.connection.local = true;
	planner.maxOutputTokens = 512;
	LevelGenerationSpec spec;
	spec.generator = levelMapgenGeneratorId();
	spec.prompt = QStringLiteral("Quake 2 deathmatch, 4 rooms, seed 3");
	const LevelGenerationResult level = generateMapgenLevel(spec, &planner);
	const QJsonObject planned = QJsonDocument::fromJson(level.generatorReport).object().value(QStringLiteral("planner")).toObject();
	const QJsonObject identity = planned.value(QStringLiteral("model")).toObject();
	expect(level.ok && planner.requests >= 1, "Quake-MapGen builds a level the GGUF model planned.", level.error);
	if (planned.value(QStringLiteral("fallback")).toString().isEmpty()) {
		expect(planned.value(QStringLiteral("requested_backend")).toString() == QStringLiteral("gguf") && identity.value(QStringLiteral("backend")).toString() == QStringLiteral("llama.cpp/gguf")
				&& identity.value(QStringLiteral("model_sha256")).toString() == text.modelSha256 && !identity.contains(QStringLiteral("provenance")),
			"Its report names the GGUF file by SHA-256, as upstream's own backend did.", QString::fromUtf8(QJsonDocument(planned).toJson(QJsonDocument::Compact)));
	} else {
		// A small model may not manage a valid plan; the planner must still have read its answers.
		const QString fallback = planned.value(QStringLiteral("fallback")).toString();
		expect(!fallback.contains(QStringLiteral("inference configuration")) && !fallback.contains(QStringLiteral("token accounting"))
				&& !fallback.contains(QStringLiteral("provenance")),
			"The model's plans are rejected only for what they say, not for how they were made.", fallback);
		std::cout << "Note: the small model's plans failed validation; the rules planned: " << fallback.toStdString() << "\n";
	}
	if (failures > 0) {
		std::cerr << failures << " GGUF check(s) failed.\n";
		return EXIT_FAILURE;
	}
	std::cout << "GGUF checks passed.\n";
	return EXIT_SUCCESS;
}
