#include "core/ai_transport.h"

#include "tests/fake_ai_provider.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>

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

QByteArray headerValue(const AiHttpRequest& request, const QByteArray& name)
{
	for (const auto& header : request.headers) {
		if (header.first.toLower() == name.toLower()) {
			return header.second;
		}
	}
	return {};
}

bool hasHeader(const AiHttpRequest& request, const QByteArray& name)
{
	for (const auto& header : request.headers) {
		if (header.first.toLower() == name.toLower()) {
			return true;
		}
	}
	return false;
}

QJsonObject bodyOf(const QByteArray& body)
{
	return QJsonDocument::fromJson(body).object();
}

AiChatRequest question(const QString& connectorId, const QString& model, const QString& endpoint = QString())
{
	AiChatRequest request;
	request.connectorId = connectorId;
	request.model = model;
	request.endpoint = endpoint;
	request.system = QStringLiteral("You help with Quake maps.");
	request.messages = {{QStringLiteral("user"), QStringLiteral("Why does qbsp leak?")}};
	request.maxOutputTokens = 1000;
	return request;
}

void checkEndpoints()
{
	expect(aiDefaultEndpoint(QStringLiteral("openai")) == QStringLiteral("https://api.openai.com/v1"), "OpenAI should default to its v1 API.");
	expect(aiDefaultEndpoint(QStringLiteral("CLAUDE")) == QStringLiteral("https://api.anthropic.com"), "Claude should default to Anthropic's API, whatever the id's case.");
	expect(aiDefaultEndpoint(QStringLiteral("local-offline")) == QStringLiteral("http://localhost:11434/v1"), "A local runtime should default to Ollama's OpenAI-compatible address.");
	expect(aiDefaultEndpoint(QStringLiteral("custom-http")).isEmpty(), "A custom endpoint has no default.");
	expect(aiSuggestedModel(QStringLiteral("claude")) == QStringLiteral("claude-opus-5-5") && aiSuggestedModel(QStringLiteral("openai")).isEmpty(),
		"Only Claude should suggest a model.");
	expect(aiConnectorHasChatTransport(QStringLiteral("gemini")) && !aiConnectorHasChatTransport(QStringLiteral("meshy")), "Gemini answers text; Meshy does not.");
	expect(aiEndpointIsLocal(QStringLiteral("http://localhost:11434/v1")) && aiEndpointIsLocal(QStringLiteral("http://127.0.0.1:8080"))
			&& aiEndpointIsLocal(QStringLiteral("http://[::1]:1234/v1")) && aiEndpointIsLocal(QStringLiteral("http://llm.localhost/v1")),
		"localhost, loopback addresses, and .localhost names are on this machine.");
	expect(!aiEndpointIsLocal(QStringLiteral("https://api.anthropic.com")) && !aiEndpointIsLocal(QStringLiteral("http://192.168.1.20:11434/v1"))
			&& !aiEndpointIsLocal(QString()),
		"A provider, a LAN machine, or nothing at all is not this machine.");
}

void checkRequests()
{
	AiHttpRequest http;
	QString error;

	// OpenAI itself.
	expect(buildAiHttpRequest(question(QStringLiteral("openai"), QStringLiteral("gpt-test")), QStringLiteral("sk-test-openai-1234567890"), &http, &error),
		"An OpenAI request should build.");
	QJsonObject body = bodyOf(http.body);
	const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
	expect(http.url.toString() == QStringLiteral("https://api.openai.com/v1/chat/completions"), "OpenAI requests go to chat/completions.");
	expect(headerValue(http, "authorization") == "Bearer sk-test-openai-1234567890", "OpenAI takes a bearer key.");
	expect(messages.size() == 2 && messages.at(0).toObject().value(QStringLiteral("role")).toString() == QStringLiteral("system")
			&& messages.at(1).toObject().value(QStringLiteral("content")).toString() == QStringLiteral("Why does qbsp leak?"),
		"The system prompt leads the OpenAI messages, then the question.");
	expect(body.value(QStringLiteral("max_completion_tokens")).toInt() == 1000 && !body.contains(QStringLiteral("max_tokens")),
		"OpenAI's own endpoint is given max_completion_tokens.");

	// A local runtime: no key, and the compatible max_tokens.
	expect(buildAiHttpRequest(question(QStringLiteral("local-offline"), QStringLiteral("llama-test")), QString(), &http, &error), "A local request should build.");
	body = bodyOf(http.body);
	expect(http.url.toString() == QStringLiteral("http://localhost:11434/v1/chat/completions") && !hasHeader(http, "authorization"),
		"A local runtime is asked at Ollama's address without a key.");
	expect(body.value(QStringLiteral("max_tokens")).toInt() == 1000 && body.value(QStringLiteral("model")).toString() == QStringLiteral("llama-test"),
		"Compatible runtimes are given max_tokens and the model.");
	expect(buildAiHttpRequest(question(QStringLiteral("custom-http"), QStringLiteral("m"), QStringLiteral("https://llm.example.test/v1/chat/completions/")), QString(), &http)
			&& http.url.toString() == QStringLiteral("https://llm.example.test/v1/chat/completions"),
		"An endpoint that already names chat/completions is used as it is.");

	// Claude.
	expect(buildAiHttpRequest(question(QStringLiteral("claude"), QStringLiteral("claude-opus-5-5")), QStringLiteral("sk-ant-test-1234567890"), &http, &error),
		"A Claude request should build.");
	body = bodyOf(http.body);
	expect(http.url.toString() == QStringLiteral("https://api.anthropic.com/v1/messages") && headerValue(http, "x-api-key") == "sk-ant-test-1234567890"
			&& headerValue(http, "anthropic-version") == "2023-06-01",
		"Claude requests go to v1/messages with x-api-key and anthropic-version.");
	expect(body.value(QStringLiteral("system")).toString() == QStringLiteral("You help with Quake maps.")
			&& body.value(QStringLiteral("messages")).toArray().size() == 1 && body.value(QStringLiteral("max_tokens")).toInt() == 1000,
		"Claude takes the system prompt at the top level and max_tokens.");
	expect(body.value(QStringLiteral("fallbacks")).toString() == QStringLiteral("default") && headerValue(http, "anthropic-beta") == "server-side-fallback-2026-07-01",
		"Claude Opus 5.5 on Anthropic's API opts into the server-side refusal fallback.");
	buildAiHttpRequest(question(QStringLiteral("claude"), QStringLiteral("claude-haiku-4-5")), QStringLiteral("k"), &http);
	expect(!bodyOf(http.body).contains(QStringLiteral("fallbacks")) && !hasHeader(http, "anthropic-beta"), "A model without the fallback is not given it.");
	buildAiHttpRequest(question(QStringLiteral("claude"), QStringLiteral("claude-opus-5-5"), QStringLiteral("https://proxy.example.test")), QStringLiteral("k"), &http);
	expect(!bodyOf(http.body).contains(QStringLiteral("fallbacks")) && http.url.toString() == QStringLiteral("https://proxy.example.test/v1/messages"),
		"A proxy in front of Claude is asked plainly.");

	// Gemini.
	AiChatRequest gemini = question(QStringLiteral("gemini"), QStringLiteral("models/gemini-test"));
	gemini.messages.push_back({QStringLiteral("assistant"), QStringLiteral("A leak is a hole to the void.")});
	gemini.messages.push_back({QStringLiteral("user"), QStringLiteral("How do I find it?")});
	expect(buildAiHttpRequest(gemini, QStringLiteral("AIzaTestKey"), &http, &error), "A Gemini request should build.");
	body = bodyOf(http.body);
	const QJsonArray contents = body.value(QStringLiteral("contents")).toArray();
	expect(http.url.toString() == QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/gemini-test:generateContent")
			&& headerValue(http, "x-goog-api-key") == "AIzaTestKey",
		"Gemini requests name the model in the path, without a models/ prefix, and carry x-goog-api-key.");
	expect(contents.size() == 3 && contents.at(1).toObject().value(QStringLiteral("role")).toString() == QStringLiteral("model")
			&& body.value(QStringLiteral("systemInstruction")).toObject().value(QStringLiteral("parts")).toArray().size() == 1
			&& body.value(QStringLiteral("generationConfig")).toObject().value(QStringLiteral("maxOutputTokens")).toInt() == 1000,
		"Gemini takes the assistant's turns as the model's, the system prompt as systemInstruction, and maxOutputTokens.");

	// What cannot be asked.
	expect(!buildAiHttpRequest(question(QStringLiteral("openai"), QStringLiteral(" ")), QString(), &http, &error) && error.contains(QStringLiteral("model")),
		"A request without a model should say so.");
	expect(!buildAiHttpRequest(question(QStringLiteral("custom-http"), QStringLiteral("m")), QString(), &http, &error) && error.contains(QStringLiteral("endpoint")),
		"A custom connector without an endpoint should say so.");
	expect(!buildAiHttpRequest(question(QStringLiteral("local-offline"), QStringLiteral("m"), QStringLiteral("ftp://127.0.0.1/v1")), QString(), &http, &error),
		"Only http and https endpoints are asked.");
	expect(!buildAiHttpRequest(question(QStringLiteral("meshy"), QStringLiteral("m")), QString(), &http, &error), "Meshy does not answer text.");
	AiChatRequest empty = question(QStringLiteral("openai"), QStringLiteral("m"));
	empty.messages.clear();
	expect(!buildAiHttpRequest(empty, QString(), &http, &error), "An empty conversation is not sent.");
}

void checkAnswers()
{
	AiChatResponse response = parseAiChatResponse(QStringLiteral("openai"), 200, FakeAiProvider::openAiAnswer(QStringLiteral("Seal the map."), QStringLiteral("gpt-test")));
	expect(response.ok && response.text == QStringLiteral("Seal the map.") && response.inputTokens == 42 && response.outputTokens == 7
			&& response.model == QStringLiteral("gpt-test") && response.finishReason == QStringLiteral("stop") && !response.truncated,
		"An OpenAI answer should give its text, usage, model, and finish reason.");
	response = parseAiChatResponse(QStringLiteral("local-offline"), 200,
		R"({"choices":[{"message":{"content":"Part of it"},"finish_reason":"length"}]})");
	expect(response.ok && response.truncated, "An answer cut at the output limit is kept and marked.");
	response = parseAiChatResponse(QStringLiteral("openai"), 200, R"({"choices":[{"message":{"content":""},"finish_reason":"content_filter"}]})");
	expect(!response.ok && response.failure == AiChatFailure::Refused, "OpenAI's content filter is a refusal.");

	response = parseAiChatResponse(QStringLiteral("claude"), 200,
		R"({"model":"claude-opus-5-5","content":[{"type":"thinking","thinking":"private working"},{"type":"text","text":"Check "},{"type":"text","text":"the pointfile."}],"stop_reason":"end_turn","usage":{"input_tokens":120,"output_tokens":9}})");
	expect(response.ok && response.text == QStringLiteral("Check the pointfile.") && response.inputTokens == 120 && response.outputTokens == 9,
		"A Claude answer is its text blocks in order, without thinking.");
	response = parseAiChatResponse(QStringLiteral("claude"), 200,
		R"({"content":[],"stop_reason":"refusal","stop_details":{"type":"refusal","category":"cyber","explanation":null}})");
	expect(!response.ok && response.failure == AiChatFailure::Refused && response.errorMessage.contains(QStringLiteral("cyber")),
		"A Claude refusal is reported with its category.");
	response = parseAiChatResponse(QStringLiteral("claude"), 200, R"({"content":[{"type":"text","text":"Half"}],"stop_reason":"max_tokens"})");
	expect(response.ok && response.truncated, "Claude's max_tokens stop marks the answer cut.");

	response = parseAiChatResponse(QStringLiteral("gemini"), 200,
		R"({"candidates":[{"content":{"role":"model","parts":[{"text":"working","thought":true},{"text":"Use -leaktest."}]},"finishReason":"STOP"}],"usageMetadata":{"promptTokenCount":30,"candidatesTokenCount":4},"modelVersion":"gemini-test"})");
	expect(response.ok && response.text == QStringLiteral("Use -leaktest.") && response.inputTokens == 30 && response.model == QStringLiteral("gemini-test"),
		"A Gemini answer is its parts that are not thoughts.");
	response = parseAiChatResponse(QStringLiteral("gemini"), 200, R"({"candidates":[{"content":{"parts":[]},"finishReason":"SAFETY"}]})");
	expect(!response.ok && response.failure == AiChatFailure::Refused, "Gemini's safety stop is a refusal.");
	response = parseAiChatResponse(QStringLiteral("gemini"), 200, R"({"promptFeedback":{"blockReason":"OTHER"}})");
	expect(!response.ok && response.failure == AiChatFailure::Refused && response.errorMessage.contains(QStringLiteral("OTHER")),
		"A blocked Gemini prompt says why.");

	response = parseAiChatResponse(QStringLiteral("claude"), 401,
		R"({"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key sk-ant-api03-abcdefghijklmnop"}})");
	expect(!response.ok && response.failure == AiChatFailure::Credential && response.errorMessage.contains(QStringLiteral("invalid x-api-key"))
			&& !response.errorMessage.contains(QStringLiteral("abcdefghijklmnop")),
		"A refused credential is a credential failure, with any key in the message hidden.");
	expect(parseAiChatResponse(QStringLiteral("openai"), 429, R"({"error":{"message":"Rate limit reached"}})").failure == AiChatFailure::RateLimited
			&& parseAiChatResponse(QStringLiteral("claude"), 529, R"({"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}})").failure == AiChatFailure::RateLimited,
		"429 and Anthropic's 529 mean try again shortly.");
	response = parseAiChatResponse(QStringLiteral("gemini"), 502, "<html>Bad gateway</html>");
	expect(response.failure == AiChatFailure::Provider && response.errorMessage.contains(QStringLiteral("Bad gateway")), "A non-JSON error keeps the server's words.");
	expect(parseAiChatResponse(QStringLiteral("openai"), 200, "not json").failure == AiChatFailure::BadResponse, "A 200 that is not JSON cannot be read.");
	expect(parseAiChatResponse(QStringLiteral("openai"), 200, R"({"choices":[{"message":{"content":"  "}}]})").failure == AiChatFailure::BadResponse,
		"An answer without text cannot be used.");
	expect(aiChatFailureId(AiChatFailure::RateLimited) == QStringLiteral("rate-limited") && !aiChatFailureText(AiChatFailure::Network).isEmpty(),
		"Failures have ids and words.");
}

void checkRedaction()
{
	const QString leaked = QStringLiteral("key=sk-proj-ABCDEFGHIJKLMNOP1234 google=AIzaSyA1234567890abcdefghijk custom=supersecretvalue");
	const QString redacted = redactAiText(leaked, {QStringLiteral("supersecretvalue"), QStringLiteral("abc")});
	expect(!redacted.contains(QStringLiteral("ABCDEFGHIJKLMNOP")) && !redacted.contains(QStringLiteral("SyA1234567890")) && !redacted.contains(QStringLiteral("supersecretvalue")),
		"Provider-shaped keys and named secrets are hidden.");
	expect(redactAiText(QStringLiteral("abc cab"), {QStringLiteral("abc")}) == QStringLiteral("abc cab"), "A short secret does not blank out ordinary text.");

	const QString log = QStringLiteral("C:\\Users\\Mapper\\Mods\\Foundry\\maps\\foundry.map and c:/users/mapper/Mods/Foundry/progs.src, saved in C:\\Users\\Mapper\\AppData\\x.log");
	const QString paths = redactAiContextPaths(log, QStringLiteral("C:/Users/Mapper/Mods/Foundry/"), QStringLiteral("C:\\Users\\Mapper"));
	expect(paths == QStringLiteral("<project>\\maps\\foundry.map and <project>/progs.src, saved in ~\\AppData\\x.log"),
		"The project and home folders are named without the machine, in either slash style and case.");
	expect(redactAiContextPaths(QStringLiteral("/home/ann/q/maps/a.map"), QStringLiteral("/home/ann/q"), QStringLiteral("/home/ann")) == QStringLiteral("<project>/maps/a.map"),
		"Unix paths are redacted too.");

	AiHttpRequest http;
	buildAiHttpRequest(question(QStringLiteral("gemini"), QStringLiteral("g")), QStringLiteral("AIzaSecretKeyValue123456"), &http);
	const QString described = describeAiHttpRequest(http);
	expect(described.startsWith(QStringLiteral("POST https://generativelanguage.googleapis.com/")) && described.contains(QStringLiteral("x-goog-api-key: ***"))
			&& !described.contains(QStringLiteral("SecretKeyValue")) && described.contains(QStringLiteral("generationConfig.maxOutputTokens: 1000"))
			&& described.contains(QStringLiteral("=== system ===\nYou help with Quake maps.")) && described.contains(QStringLiteral("=== user ===\nWhy does qbsp leak?")),
		"The readable preview shows the address, headers without the key, the settings, and each message's text as written.");
	const QString raw = describeAiHttpRequest(http, AiRequestView::Raw);
	expect(raw.contains(QStringLiteral("\"maxOutputTokens\": 1000")) && !raw.contains(QStringLiteral("SecretKeyValue")), "The raw preview shows the JSON body as it goes.");
	buildAiHttpRequest(question(QStringLiteral("claude"), QStringLiteral("claude-opus-5-5")), QStringLiteral("k"), &http);
	const QString claude = describeAiHttpRequest(http);
	expect(claude.contains(QStringLiteral("=== system ===")) && claude.indexOf(QStringLiteral("=== system ===")) < claude.indexOf(QStringLiteral("=== user ===")),
		"The system text leads the messages whatever the provider calls it.");
}

void checkContext()
{
	QStringList lines;
	for (int index = 1; index <= 500; ++index) {
		lines << QStringLiteral("line %1").arg(index);
	}
	const AiContextItem item = makeAiContextItem(QStringLiteral("build-log"), QStringLiteral("Build log"), lines.join(QLatin1Char('\n')), 100);
	expect(item.text.split(QLatin1Char('\n')).size() == 100 && item.text.endsWith(QStringLiteral("line 100")) && item.omittedNote.contains(QStringLiteral("500")),
		"A long context item keeps its first lines and says how many were left out.");
	const AiContextItem small = makeAiContextItem(QStringLiteral("code"), QStringLiteral("sentry.qc"), QStringLiteral("void() main = {};\n\n"));
	expect(small.omittedNote.isEmpty() && small.text == QStringLiteral("void() main = {};"), "A short item is sent whole.");
	const AiContextItem bytes = makeAiContextItem(QStringLiteral("big"), QStringLiteral("Big"), QString(3000, QLatin1Char('x')) + QStringLiteral("\nnext"), 400, 1024);
	expect(bytes.text.isEmpty() && !bytes.omittedNote.isEmpty(), "A line longer than the byte limit is left out, and said to be.");
	const QString prompt = aiContextPromptText({small, makeAiContextItem(QStringLiteral("md"), QStringLiteral("Notes"), QStringLiteral("```\ncode\n```"))});
	expect(prompt.startsWith(QStringLiteral("## sentry.qc\n```\nvoid() main = {};\n```")) && prompt.contains(QStringLiteral("````\n```\ncode\n```\n````")),
		"Each item is a fenced section, the fence outgrowing any fence inside.");
	expect(studioAssistantSystemPrompt().contains(QStringLiteral("idTech")), "The assistant knows where it works.");
}

QJsonObject roomPlanSchema()
{
	const QJsonObject room {
		{QStringLiteral("type"), QStringLiteral("object")},
		{QStringLiteral("properties"), QJsonObject {
			{QStringLiteral("id"), QJsonObject {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 8}}},
			{QStringLiteral("size"), QJsonObject {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray {QStringLiteral("small"), QStringLiteral("large")}}}},
			{QStringLiteral("level"), QJsonObject {{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 3}}},
		}},
		{QStringLiteral("required"), QJsonArray {QStringLiteral("id"), QStringLiteral("size"), QStringLiteral("level")}},
		{QStringLiteral("additionalProperties"), false},
	};
	return QJsonObject {
		{QStringLiteral("type"), QStringLiteral("object")},
		{QStringLiteral("properties"), QJsonObject {
			{QStringLiteral("title"), QJsonObject {{QStringLiteral("type"), QStringLiteral("string")}}},
			{QStringLiteral("rooms"), QJsonObject {{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("minItems"), 1}, {QStringLiteral("items"), room}}},
		}},
		{QStringLiteral("required"), QJsonArray {QStringLiteral("title"), QStringLiteral("rooms")}},
		{QStringLiteral("additionalProperties"), false},
	};
}

void checkStructured()
{
	AiHttpRequest http;
	AiChatRequest request = question(QStringLiteral("openai"), QStringLiteral("gpt-test"));
	request.responseSchema = roomPlanSchema();
	request.responseSchemaName = QStringLiteral("level plan!");
	expect(buildAiHttpRequest(request, QStringLiteral("k"), &http), "A structured OpenAI request should build.");
	QJsonObject format = bodyOf(http.body).value(QStringLiteral("response_format")).toObject();
	QJsonObject schema = format.value(QStringLiteral("json_schema")).toObject().value(QStringLiteral("schema")).toObject();
	const QJsonObject roomSchema = schema.value(QStringLiteral("properties")).toObject().value(QStringLiteral("rooms")).toObject().value(QStringLiteral("items")).toObject();
	expect(format.value(QStringLiteral("type")).toString() == QStringLiteral("json_schema")
			&& format.value(QStringLiteral("json_schema")).toObject().value(QStringLiteral("strict")).toBool()
			&& format.value(QStringLiteral("json_schema")).toObject().value(QStringLiteral("name")).toString() == QStringLiteral("level_plan_"),
		"OpenAI is asked for a strict json_schema with a name it accepts.");
	expect(!schema.value(QStringLiteral("properties")).toObject().value(QStringLiteral("rooms")).toObject().contains(QStringLiteral("minItems"))
			&& !roomSchema.value(QStringLiteral("properties")).toObject().value(QStringLiteral("level")).toObject().contains(QStringLiteral("minimum"))
			&& roomSchema.value(QStringLiteral("additionalProperties")) == QJsonValue(false)
			&& roomSchema.value(QStringLiteral("properties")).toObject().value(QStringLiteral("size")).toObject().value(QStringLiteral("enum")).toArray().size() == 2,
		"The strict schema keeps types, enums, and closed objects, and leaves bounds to the local check.");
	const QString described = describeAiHttpRequest(http);
	expect(described.contains(QStringLiteral("response_format.json_schema: {")) && described.contains(QStringLiteral("response_format.type: json_schema")),
		"The readable preview shows the structured-output setting as compact JSON.");

	request = question(QStringLiteral("claude"), QStringLiteral("claude-opus-5-5"));
	request.responseSchema = roomPlanSchema();
	buildAiHttpRequest(request, QStringLiteral("k"), &http);
	format = bodyOf(http.body).value(QStringLiteral("output_config")).toObject().value(QStringLiteral("format")).toObject();
	expect(format.value(QStringLiteral("type")).toString() == QStringLiteral("json_schema")
			&& format.value(QStringLiteral("schema")).toObject().value(QStringLiteral("required")).toArray().size() == 2
			&& bodyOf(http.body).value(QStringLiteral("fallbacks")).toString() == QStringLiteral("default"),
		"Claude is asked through output_config.format, alongside its refusal fallback.");

	request = question(QStringLiteral("gemini"), QStringLiteral("gemini-test"));
	request.responseSchema = roomPlanSchema();
	buildAiHttpRequest(request, QStringLiteral("k"), &http);
	const QJsonObject generation = bodyOf(http.body).value(QStringLiteral("generationConfig")).toObject();
	schema = generation.value(QStringLiteral("responseSchema")).toObject();
	const QJsonObject geminiRoom = schema.value(QStringLiteral("properties")).toObject().value(QStringLiteral("rooms")).toObject();
	expect(generation.value(QStringLiteral("responseMimeType")).toString() == QStringLiteral("application/json") && schema.value(QStringLiteral("type")).toString() == QStringLiteral("OBJECT")
			&& !schema.contains(QStringLiteral("additionalProperties")) && geminiRoom.value(QStringLiteral("minItems")).toInt() == 1
			&& geminiRoom.value(QStringLiteral("items")).toObject().value(QStringLiteral("properties")).toObject().value(QStringLiteral("level")).toObject().value(QStringLiteral("type")).toString() == QStringLiteral("INTEGER")
			&& generation.value(QStringLiteral("maxOutputTokens")).toInt() == 1000,
		"Gemini is asked for JSON with its OpenAPI-style schema: upper-case types, no additionalProperties, bounds kept.");
	expect(!bodyOf(http.body).contains(QStringLiteral("response_format")), "Gemini is not sent OpenAI's field.");

	QJsonObject object;
	QString error;
	expect(extractAiJsonObject(QStringLiteral(" {\"title\":\"Keep\"} "), &object) && object.value(QStringLiteral("title")).toString() == QStringLiteral("Keep"),
		"A bare JSON answer is read whole.");
	expect(extractAiJsonObject(QStringLiteral("Here is the plan:\n```json\n{\"title\":\"Fenced\"}\n```\nEnjoy."), &object)
			&& object.value(QStringLiteral("title")).toString() == QStringLiteral("Fenced"),
		"A fenced JSON block is found inside prose.");
	expect(extractAiJsonObject(QStringLiteral("Plan {draft} follows: {\"title\":\"Brace } in {string\",\"rooms\":[]} done"), &object)
			&& object.value(QStringLiteral("title")).toString() == QStringLiteral("Brace } in {string"),
		"The first balanced object is found, braces inside strings skipped.");
	expect(!extractAiJsonObject(QStringLiteral("No JSON here at all."), &object, &error) && !error.isEmpty(), "Prose without JSON says so.");
	expect(!extractAiJsonObject(QStringLiteral("[1, 2]"), &object, &error), "A JSON array is not an object.");

	const QJsonObject good = QJsonDocument::fromJson(R"({"title":"T","rooms":[{"id":"r1","size":"small","level":2}]})").object();
	expect(aiJsonSchemaProblems(good, roomPlanSchema()).isEmpty(), "A plan that fits the schema has no problems.");
	const QJsonObject bad = QJsonDocument::fromJson(R"({"rooms":[{"id":"far-too-long","size":"huge","level":7,"extra":1},{"id":"r2","size":"small","level":1.5}]})").object();
	const QStringList problems = aiJsonSchemaProblems(bad, roomPlanSchema());
	const QString joined = problems.join(QLatin1Char('\n'));
	expect(joined.contains(QStringLiteral("$.title is missing")) && joined.contains(QStringLiteral("$.rooms[0].id is longer than 8"))
			&& joined.contains(QStringLiteral("$.rooms[0].size is \"huge\"")) && joined.contains(QStringLiteral("$.rooms[0].level is above 3"))
			&& joined.contains(QStringLiteral("$.rooms[0].extra is not expected")) && joined.contains(QStringLiteral("$.rooms[1].level should be integer")),
		"Each problem names its place: missing, too long, not allowed, out of bounds, unexpected, and of the wrong type.");
	expect(!aiJsonSchemaProblems(QJsonDocument::fromJson(R"({"title":"T","rooms":[]})").object(), roomPlanSchema()).isEmpty(), "minItems is checked locally.");
}

void checkConnections()
{
	AiAutomationPreferences preferences;
	expect(resolveAiTextConnection(preferences).block == AiTextConnectionBlock::AiFreeMode, "AI-free mode stops every text connection.");
	preferences.aiFreeMode = false;
	expect(resolveAiTextConnection(preferences).block == AiTextConnectionBlock::NoConnector, "Without a chosen connector nothing is asked.");
	expect(resolveAiTextConnection(preferences, QStringLiteral("elevenlabs")).block == AiTextConnectionBlock::NoTextTransport, "ElevenLabs does not answer text.");

	preferences.preferredLocalConnectorId = QStringLiteral("local-offline");
	AiTextConnection connection = resolveAiTextConnection(preferences);
	expect(connection.connectorId == QStringLiteral("local-offline") && connection.local && connection.block == AiTextConnectionBlock::NoModel
			&& aiTextConnectionBlockText(connection).contains(QStringLiteral("model")),
		"The local connector is on this machine and waits for a model.");
	preferences.connectorModels.insert(QStringLiteral("local-offline"), QStringLiteral("llama-test"));
	connection = resolveAiTextConnection(preferences);
	expect(connection.ready() && connection.credentialVariable.isEmpty() && connection.model == QStringLiteral("llama-test"),
		"A local runtime with a model is ready, without a key and without cloud opt-in.");

	preferences.preferredReasoningConnectorId = QStringLiteral("claude");
	connection = resolveAiTextConnection(preferences);
	expect(connection.connectorId == QStringLiteral("local-offline") && connection.ready(),
		"With cloud connectors off, the local connector answers instead of a cloud reasoning connector.");
	AiAutomationPreferences cloudOnly = preferences;
	cloudOnly.preferredLocalConnectorId.clear();
	connection = resolveAiTextConnection(cloudOnly);
	expect(connection.connectorId == QStringLiteral("claude") && connection.block == AiTextConnectionBlock::CloudNotAllowed
			&& aiTextConnectionBlockText(connection).contains(QStringLiteral("api.anthropic.com")),
		"Without a local connector, a cloud reasoning connector waits for cloud opt-in.");
	preferences.cloudConnectorsEnabled = true;
	qputenv("ANTHROPIC_API_KEY", QByteArray());
	connection = resolveAiTextConnection(preferences);
	expect(connection.block == AiTextConnectionBlock::NoCredential && connection.credentialVariable == QStringLiteral("ANTHROPIC_API_KEY")
			&& connection.model == QStringLiteral("claude-opus-5-5"),
		"Claude suggests its model and names the missing key.");
	qputenv("ANTHROPIC_API_KEY", "sk-ant-test-value-1234567890");
	connection = resolveAiTextConnection(preferences);
	expect(connection.ready() && aiTextConnectionApiKey(connection) == QStringLiteral("sk-ant-test-value-1234567890") && !connection.local,
		"With cloud opt-in and a key, Claude is ready.");
	qunsetenv("ANTHROPIC_API_KEY");

	preferences.connectorEndpoints.insert(QStringLiteral("custom-http"), QStringLiteral("http://192.168.0.5:8000/v1"));
	preferences.connectorModels.insert(QStringLiteral("custom-http"), QStringLiteral("team-model"));
	preferences.cloudConnectorsEnabled = false;
	expect(resolveAiTextConnection(preferences, QStringLiteral("custom-http")).block == AiTextConnectionBlock::CloudNotAllowed,
		"An endpoint off this machine needs cloud opt-in, whichever connector reaches it.");
	preferences.connectorEndpoints.insert(QStringLiteral("custom-http"), QStringLiteral("http://127.0.0.1:8000/v1"));
	expect(resolveAiTextConnection(preferences, QStringLiteral("custom-http")).ready(), "A custom endpoint on this machine needs no key unless one is named.");
	const AiAutomationPreferences normalized = normalizedAiAutomationPreferences(preferences);
	expect(normalized.connectorModels.value(QStringLiteral("custom-http")) == QStringLiteral("team-model"), "Per-connector models survive normalization.");
}

void checkClient()
{
	FakeAiProvider provider;
	expect(provider.listening(), "The fake provider should listen on 127.0.0.1.");

	// A local runtime answers.
	AiChatClient client;
	std::optional<AiChatResponse> answer;
	provider.answer(200, FakeAiProvider::openAiAnswer(QStringLiteral("Seal the leak near the door.")));
	QString error;
	const bool started = client.send(question(QStringLiteral("local-offline"), QStringLiteral("llama-test"), provider.baseUrl(QStringLiteral("/v1"))), QString(),
		[&answer](const AiChatResponse& response) { answer = response; }, &error);
	expect(started && client.busy(), "A request should start.");
	AiChatClient::Callback ignore = [](const AiChatResponse&) {};
	expect(!client.send(question(QStringLiteral("local-offline"), QStringLiteral("m"), provider.baseUrl()), QString(), ignore, &error) && !error.isEmpty(),
		"A second request waits for the first.");
	expect(waitUntil([&answer]() { return answer.has_value(); }), "The local runtime should answer.");
	expect(answer && answer->ok && answer->text == QStringLiteral("Seal the leak near the door.") && answer->httpStatus == 200 && answer->elapsedMsecs >= 0,
		"The answer should come back whole.");
	expect(!client.busy(), "The client is free once the answer is in.");
	expect(provider.exchanges().size() == 1 && provider.exchanges().first().path == "/v1/chat/completions"
			&& !provider.exchanges().first().headers.contains("authorization")
			&& bodyOf(provider.exchanges().first().body).value(QStringLiteral("model")).toString() == QStringLiteral("llama-test"),
		"The runtime should be sent the question, the model, and no key.");

	// A key of no known shape is still kept out of a report that repeats it.
	provider.clear();
	answer.reset();
	provider.answer(401, R"({"error":{"message":"The key team-secret-value-0123456789 is not valid here."}})");
	client.send(question(QStringLiteral("custom-http"), QStringLiteral("team-model"), provider.baseUrl(QStringLiteral("/v1"))), QStringLiteral("team-secret-value-0123456789"),
		[&answer](const AiChatResponse& response) { answer = response; });
	expect(waitUntil([&answer]() { return answer.has_value(); }), "The team endpoint should answer the bad key.");
	expect(answer && answer->failure == AiChatFailure::Credential && answer->errorMessage.contains(QStringLiteral("is not valid here"))
			&& !answer->errorMessage.contains(QStringLiteral("team-secret-value")),
		"The key sent is hidden in the report even when it has no provider's shape.");
	expect(!provider.exchanges().isEmpty() && provider.exchanges().last().headers.value("authorization") == "Bearer team-secret-value-0123456789",
		"A custom endpoint takes its key as a bearer token.");

	// A refused key, with the key kept out of the report.
	provider.clear();
	answer.reset();
	provider.answer(401, R"({"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key sk-ant-test-key-abcdefghij"}})");
	client.send(question(QStringLiteral("claude"), QStringLiteral("claude-opus-5-5"), provider.baseUrl()), QStringLiteral("sk-ant-test-key-abcdefghij"),
		[&answer](const AiChatResponse& response) { answer = response; });
	expect(waitUntil([&answer]() { return answer.has_value(); }), "The provider should answer the bad key.");
	expect(answer && answer->failure == AiChatFailure::Credential && answer->httpStatus == 401 && !answer->errorMessage.contains(QStringLiteral("abcdefghij")),
		"A refused key is a credential failure that never repeats the key.");
	expect(!provider.exchanges().isEmpty() && provider.exchanges().first().path == "/v1/messages"
			&& provider.exchanges().first().headers.value("x-api-key") == "sk-ant-test-key-abcdefghij"
			&& provider.exchanges().first().headers.value("anthropic-version") == "2023-06-01",
		"Claude's request should reach v1/messages with its headers.");

	// Cancel.
	answer.reset();
	provider.hang();
	const qsizetype asked = provider.exchanges().size();
	client.send(question(QStringLiteral("local-offline"), QStringLiteral("m"), provider.baseUrl()), QString(), [&answer](const AiChatResponse& response) { answer = response; });
	expect(waitUntil([&provider, asked]() { return provider.exchanges().size() > asked; }, 3000), "The provider should receive the request to cancel.");
	client.cancel();
	expect(waitUntil([&answer]() { return answer.has_value(); }, 3000) && answer->failure == AiChatFailure::Cancelled && !client.busy(),
		"Cancel ends the request and says so.");

	// Timeout.
	answer.reset();
	client.setTimeoutMsecs(1000);
	QElapsedTimer clock;
	clock.start();
	client.send(question(QStringLiteral("local-offline"), QStringLiteral("m"), provider.baseUrl()), QString(), [&answer](const AiChatResponse& response) { answer = response; });
	expect(waitUntil([&answer]() { return answer.has_value(); }, 5000) && answer->failure == AiChatFailure::Timeout && clock.elapsed() >= 900,
		"A provider that never answers times out.");

	// Nothing listening.
	quint16 closedPort = 0;
	{
		QTcpServer probe;
		probe.listen(QHostAddress::LocalHost, 0);
		closedPort = probe.serverPort();
	}
	answer.reset();
	// Windows retries a refused loopback connection for about two seconds.
	client.setTimeoutMsecs(15000);
	client.send(question(QStringLiteral("local-offline"), QStringLiteral("m"), QStringLiteral("http://127.0.0.1:%1/v1").arg(closedPort)), QString(),
		[&answer](const AiChatResponse& response) { answer = response; });
	expect(waitUntil([&answer]() { return answer.has_value(); }, 12000) && answer->failure == AiChatFailure::Network && !answer->errorMessage.isEmpty(),
		"An endpoint with nothing listening is a network failure with the reason.");

	// A client that goes away mid-request never calls back.
	bool calledBack = false;
	{
		AiChatClient doomed;
		doomed.send(question(QStringLiteral("local-offline"), QStringLiteral("m"), provider.baseUrl()), QString(), [&calledBack](const AiChatResponse&) { calledBack = true; });
		waitUntil([]() { return false; }, 200);
	}
	waitUntil([]() { return false; }, 200);
	expect(!calledBack, "A destroyed client never calls back.");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	checkEndpoints();
	checkRequests();
	checkAnswers();
	checkRedaction();
	checkContext();
	checkStructured();
	checkConnections();
	checkClient();
	if (failures > 0) {
		std::cerr << failures << " AI transport check(s) failed.\n";
		return EXIT_FAILURE;
	}
	std::cout << "AI transport checks passed.\n";
	return EXIT_SUCCESS;
}
