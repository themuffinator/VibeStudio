// Harness connectors (Claude Code, Codex, a custom command) and the GGUF connector's no-runtime
// path, end to end through AiChatClient against tests/fake_ai_harness. Also the JSON Schema to
// GBNF grammar the GGUF connector decodes under.

#include "core/ai_gguf_runtime.h"
#include "core/ai_harness.h"
#include "core/ai_json_grammar.h"
#include "core/ai_transport.h"
#include "core/level_generation.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
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

bool waitUntil(const std::function<bool()>& done, int msecs = 30000)
{
	QElapsedTimer clock;
	clock.start();
	while (!done() && clock.elapsed() < msecs) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
	}
	return done();
}

AiChatRequest question(const QString& connector, const QString& program, const QString& text = QStringLiteral("Reply OK."))
{
	AiChatRequest request;
	request.connectorId = connector;
	request.endpoint = program;
	request.model = aiSuggestedModel(connector);
	request.system = QStringLiteral("You plan levels.");
	request.messages = {{QStringLiteral("user"), text}};
	return request;
}

AiChatResponse ask(const AiChatRequest& request, int timeoutMsecs = 30000, const std::function<void(AiChatClient&)>& meanwhile = {})
{
	AiChatClient client;
	client.setTimeoutMsecs(timeoutMsecs);
	std::optional<AiChatResponse> answer;
	QString error;
	if (!client.send(request, QString(), [&answer](const AiChatResponse& response) { answer = response; }, &error)) {
		AiChatResponse failed;
		failed.errorMessage = error;
		return failed;
	}
	if (meanwhile) {
		meanwhile(client);
	}
	waitUntil([&answer] { return answer.has_value(); }, timeoutMsecs + 10000);
	return answer.value_or(AiChatResponse());
}

QJsonObject readRecord(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

QJsonObject planSchema()
{
	return QJsonObject {
		{QStringLiteral("type"), QStringLiteral("object")},
		{QStringLiteral("additionalProperties"), false},
		{QStringLiteral("required"), QJsonArray {QStringLiteral("title"), QStringLiteral("rooms")}},
		{QStringLiteral("properties"), QJsonObject {
			{QStringLiteral("title"), QJsonObject {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 40}}},
			{QStringLiteral("rooms"), QJsonObject {
				{QStringLiteral("type"), QStringLiteral("array")},
				{QStringLiteral("minItems"), 2},
				{QStringLiteral("maxItems"), 4},
				{QStringLiteral("items"), QJsonObject {
					{QStringLiteral("type"), QStringLiteral("object")},
					{QStringLiteral("additionalProperties"), false},
					{QStringLiteral("required"), QJsonArray {QStringLiteral("role"), QStringLiteral("level")}},
					{QStringLiteral("properties"), QJsonObject {
						{QStringLiteral("role"), QJsonObject {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray {QStringLiteral("start"), QStringLiteral("exit")}}}},
						{QStringLiteral("level"), QJsonObject {{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 3}}},
					}},
				}},
			}},
		}},
	};
}

void checkInvocations(const QString& fake)
{
	QString error;
	AiChatRequest request = question(QStringLiteral("claude-code"), fake);
	request.responseSchema = planSchema();
	AiHarnessInvocation claude;
	expect(buildAiHarnessInvocation(request, fake, &claude, &error), "A Claude Code run builds.", error);
	const QStringList& words = claude.arguments;
	const qsizetype tools = words.indexOf(QStringLiteral("--tools"));
	expect(words.contains(QStringLiteral("-p")) && words.contains(QStringLiteral("--safe-mode")) && words.contains(QStringLiteral("--no-session-persistence"))
			&& words.contains(QStringLiteral("--strict-mcp-config")) && tools >= 0 && words.value(tools + 1).isEmpty(),
		"Claude Code runs in print and safe mode with no tools, MCP servers or saved session.");
	expect(words.contains(QStringLiteral("--json-schema")) && words.value(words.indexOf(QStringLiteral("--model")) + 1) == QStringLiteral("claude-opus-5-5"),
		"It asks for structured output from claude-opus-5-5 by default.");
	expect(claude.removedEnvironment.contains(QStringLiteral("ANTHROPIC_API_KEY")) && claude.files.value(0).first == QStringLiteral("system.txt")
			&& QString::fromUtf8(claude.input) == QStringLiteral("Reply OK."),
		"The login answers (no API key), with the system prompt in a file and the question on standard input.");
	expect(claude.removedEnvironment.contains(QStringLiteral("ANTHROPIC_BASE_URL")) && claude.removedEnvironment.contains(QStringLiteral("CLAUDE_CODE_USE_BEDROCK")),
		"Nothing sends Claude Code's requests anywhere but Anthropic.");

	AiHarnessInvocation codex;
	AiChatRequest codexRequest = question(QStringLiteral("codex"), fake);
	codexRequest.responseSchema = planSchema();
	codexRequest.messages << AiChatMessage {QStringLiteral("assistant"), QStringLiteral("Which game?")} << AiChatMessage {QStringLiteral("user"), QStringLiteral("Quake.")};
	expect(buildAiHarnessInvocation(codexRequest, fake, &codex, &error), "A Codex run builds.", error);
	expect(codex.arguments.value(0) == QStringLiteral("exec") && codex.arguments.contains(QStringLiteral("read-only")) && codex.arguments.contains(QStringLiteral("--ephemeral"))
			&& codex.arguments.contains(QStringLiteral("--output-schema")) && codex.answerFile == QStringLiteral("answer.txt") && !codex.arguments.contains(QStringLiteral("-m")),
		"Codex runs read-only and ephemeral, with its own default model and a schema file.");
	expect(codex.arguments.contains(QStringLiteral("--ignore-user-config")) && codex.arguments.contains(QStringLiteral("features.shell_tool=false"))
			&& codex.removedEnvironment.contains(QStringLiteral("CODEX_API_KEY")) && codex.removedEnvironment.contains(QStringLiteral("OPENAI_API_KEY")),
		"Codex runs without its tools, the user's config, or an API key in the environment.", codex.arguments.join(QLatin1Char(' ')));
	const QString input = QString::fromUtf8(codex.input);
	expect(input.startsWith(QStringLiteral("<instructions>\nYou plan levels.")) && input.contains(QStringLiteral("[assistant]\nWhich game?"))
			&& input.endsWith(QStringLiteral("Answer the last user message.")),
		"A conversation reaches Codex as instructions and a transcript.");

	AiHarnessInvocation custom;
	AiChatRequest customRequest = question(QStringLiteral("custom-command"), QString());
	customRequest.model = QStringLiteral("m1");
	expect(buildAiHarnessInvocation(customRequest, QStringLiteral("\"%1\" --model {model} --prompt {prompt_file} --out {answer_file}").arg(fake), &custom, &error),
		"A custom command builds from its command line.", error);
	expect(custom.arguments.contains(QStringLiteral("m1")) && custom.arguments.contains(QStringLiteral("{dir}/prompt.txt")) && custom.answerFile == QStringLiteral("answer.txt")
			&& custom.input.isEmpty(),
		"Its placeholders become the model, a prompt file and an answer file.");
	AiHarnessInvocation defaulted;
	customRequest.model.clear();
	expect(buildAiHarnessInvocation(customRequest, QStringLiteral("\"%1\" -m {model} --quiet").arg(fake), &defaulted, &error)
			&& defaulted.arguments == QStringList {QStringLiteral("--quiet")} && !defaulted.input.isEmpty(),
		"With no model set, \"-m {model}\" is left out and the prompt goes on standard input.", defaulted.arguments.join(QLatin1Char(' ')));

	const AiHttpRequest record = aiHarnessRequestRecord(claude);
	const QString shown = describeAiHttpRequest(record);
	expect(shown.startsWith(QStringLiteral("RUN ")) && shown.contains(QStringLiteral("--tools \"\"")) && shown.contains(QStringLiteral("=== system.txt ==="))
			&& shown.contains(QStringLiteral("Reply OK.")) && shown.contains(QStringLiteral("ANTHROPIC_API_KEY")),
		"The review shows the command, its files, its input, and what is not passed on.", shown);
	AiHttpRequest built;
	expect(buildAiHttpRequest(request, QString(), &built, &error) && built.command.value(0) == fake, "buildAiHttpRequest makes the same record for consent and previews.", error);

#ifdef Q_OS_WIN
	// An npm shim runs as node with the package's script, never through cmd.exe.
	QTemporaryDir shimFolder;
	QDir(shimFolder.path()).mkpath(QStringLiteral("node_modules/@openai/codex/bin"));
	QFile script(QDir(shimFolder.path()).filePath(QStringLiteral("node_modules/@openai/codex/bin/codex.js")));
	QFile node(QDir(shimFolder.path()).filePath(QStringLiteral("node.exe")));
	QFile shim(QDir(shimFolder.path()).filePath(QStringLiteral("codex.cmd")));
	expect(script.open(QIODevice::WriteOnly) && node.open(QIODevice::WriteOnly) && shim.open(QIODevice::WriteOnly), "The shim fixture is written.");
	shim.write("@ECHO off\r\nendLocal & goto #_undefined_# 2>NUL || title %COMSPEC% & \"%_prog%\"  \"%dp0%\\node_modules\\@openai\\codex\\bin\\codex.js\" %*\r\n");
	script.close();
	node.close();
	shim.close();
	AiHarnessInvocation shimmed;
	expect(buildAiHarnessInvocation(question(QStringLiteral("codex"), shim.fileName()), shim.fileName(), &shimmed, &error)
			&& QFileInfo(shimmed.program) == QFileInfo(node.fileName()) && shimmed.arguments.value(0).endsWith(QStringLiteral("codex.js")),
		"An npm .cmd shim is run as node and its script.", error);
#endif
}

void checkParsing()
{
	AiChatResponse ok = parseAiHarnessOutput(QStringLiteral("claude-code"), 0,
		QByteArrayLiteral("{\"type\":\"result\",\"subtype\":\"success\",\"is_error\":false,\"result\":\"\",\"structured_output\":{\"a\":1},"
						  "\"usage\":{\"input_tokens\":10,\"cache_read_input_tokens\":5,\"output_tokens\":3},\"modelUsage\":{\"claude-opus-5-5\":{}}}"),
		QByteArray());
	expect(ok.ok && ok.text == QStringLiteral("{\"a\":1}") && ok.inputTokens == 15 && ok.outputTokens == 3 && ok.model == QStringLiteral("claude-opus-5-5"),
		"Claude Code's structured output, usage and model are read.");
	const AiChatResponse signedOut = parseAiHarnessOutput(QStringLiteral("claude-code"), 1,
		QByteArrayLiteral("{\"type\":\"result\",\"subtype\":\"success\",\"is_error\":true,\"result\":\"Not logged in \\u00b7 Please run /login\"}"), QByteArray());
	expect(!signedOut.ok && signedOut.failure == AiChatFailure::Credential && signedOut.errorMessage.contains(QStringLiteral("claude auth login")),
		"A signed-out Claude Code says how to sign in.");
	const AiChatResponse limited = parseAiHarnessOutput(QStringLiteral("codex"), 1,
		QByteArrayLiteral("{\"type\":\"turn.started\"}\n{\"type\":\"error\",\"message\":\"You've hit your usage limit.\"}\n{\"type\":\"turn.failed\",\"error\":{\"message\":\"You've hit your usage limit.\"}}\n"),
		QByteArray());
	expect(!limited.ok && limited.failure == AiChatFailure::RateLimited, "Codex's usage limit is a rate limit.");
	const AiChatResponse codex = parseAiHarnessOutput(QStringLiteral("codex"), 0,
		QByteArrayLiteral("{\"type\":\"item.completed\",\"item\":{\"type\":\"agent_message\",\"text\":\"from events\"}}\n{\"type\":\"turn.completed\",\"usage\":{\"input_tokens\":7,\"output_tokens\":2}}\n"),
		QByteArray(), QByteArrayLiteral("from the file\n"));
	expect(codex.ok && codex.text == QStringLiteral("from the file") && codex.inputTokens == 7 && codex.outputTokens == 2, "Codex's last message and usage are read.");
	expect(!parseAiHarnessOutput(QStringLiteral("custom-command"), 2, QByteArray(), QByteArrayLiteral("boom")).ok, "A failing custom command fails.");
	const AiChatResponse recovered = parseAiHarnessOutput(QStringLiteral("codex"), 0,
		QByteArrayLiteral("{\"type\":\"error\",\"message\":\"stream error: reset; retrying 1/5\"}\n"
						  "{\"type\":\"item.completed\",\"item\":{\"type\":\"agent_message\",\"text\":\"done\"}}\n{\"type\":\"turn.completed\",\"usage\":{}}\n"),
		QByteArray());
	expect(recovered.ok && recovered.text == QStringLiteral("done"), "An error Codex recovered from does not fail a finished turn.", recovered.errorMessage);
	const AiChatResponse empty = parseAiHarnessOutput(QStringLiteral("claude-code"), 0,
		QByteArrayLiteral("{\"type\":\"result\",\"subtype\":\"success\",\"is_error\":false,\"result\":\"\"}"), QByteArray());
	expect(!empty.ok && empty.failure == AiChatFailure::BadResponse, "Claude Code finishing with no answer is a bad response.");
	const AiChatResponse old = parseAiHarnessOutput(QStringLiteral("codex"), 2, QByteArray(),
		QByteArrayLiteral("error: unexpected argument '--ignore-user-config' found\n\nUsage: codex exec [OPTIONS] [PROMPT]\n"));
	expect(!old.ok && old.failure == AiChatFailure::NotConfigured && old.errorMessage.contains(QStringLiteral("npm install -g @openai/codex@latest")),
		"An older Codex refusing an option says how to update it.", old.errorMessage);
}

void checkRuns(const QString& fake)
{
	QTemporaryDir temp;
	const QString recordPath = temp.filePath(QStringLiteral("record.json"));
	qputenv("FAKE_HARNESS_RECORD", recordPath.toUtf8());
	qputenv("ANTHROPIC_API_KEY", "sk-ant-test-should-not-pass");
	qputenv("FAKE_HARNESS_ANSWER", "{\"title\":\"Keep\",\"rooms\":[]}");
	AiChatRequest request = question(QStringLiteral("claude-code"), fake);
	request.responseSchema = planSchema();
	const AiChatResponse claude = ask(request);
	const QJsonObject record = readRecord(recordPath);
	expect(claude.ok && claude.text == QStringLiteral("{\"rooms\":[],\"title\":\"Keep\"}") && claude.inputTokens == 42 && claude.model == QStringLiteral("claude-opus-5-5"),
		"Claude Code answers through AiChatClient with its structured output, named by the model that wrote it.", claude.errorMessage + claude.text + claude.model);
	expect(!record.value(QStringLiteral("anthropicKey")).toBool() && record.value(QStringLiteral("autoUpdater")).toString() == QStringLiteral("1")
			&& record.value(QStringLiteral("files")).toObject().value(QStringLiteral("system.txt")).toString() == QStringLiteral("You plan levels."),
		"It runs without the API key, with updates off and the system prompt file in its folder.");
	const QString folder = record.value(QStringLiteral("folder")).toString();
	expect(!folder.isEmpty() && QFileInfo(folder) != QFileInfo(QDir::currentPath()) && !QFileInfo::exists(folder), "It ran in its own folder, removed afterwards.", folder);

	qputenv("FAKE_HARNESS_ANSWER", "OK");
	const AiChatResponse codex = ask(question(QStringLiteral("codex"), fake));
	expect(codex.ok && codex.text == QStringLiteral("OK") && codex.inputTokens == 120 && codex.outputTokens == 30, "Codex answers through AiChatClient.", codex.errorMessage);

	// Cancelling ends what the tool started too: node starting Codex's own binary, here a child
	// that keeps writing to a file.
	const QString heartbeat = temp.filePath(QStringLiteral("heartbeat.txt"));
	qputenv("FAKE_HARNESS_HEARTBEAT", heartbeat.toUtf8());
	qputenv("FAKE_HARNESS_MODE", "spawn");
	const AiChatResponse tree = ask(question(QStringLiteral("codex"), fake), 30000, [&heartbeat](AiChatClient& client) {
		waitUntil([&heartbeat] { return QFileInfo(heartbeat).size() > 2; }, 15000);
		client.cancel();
	});
	waitUntil([] { return false; }, 600);
	const qint64 beats = QFileInfo(heartbeat).size();
	waitUntil([] { return false; }, 900);
	expect(!tree.ok && tree.failure == AiChatFailure::Cancelled && beats > 0 && QFileInfo(heartbeat).size() == beats,
		"Cancelling a tool also ends the processes it started.", QStringLiteral("%1 then %2 beats").arg(beats).arg(QFileInfo(heartbeat).size()));
	expect(!QFileInfo::exists(readRecord(recordPath).value(QStringLiteral("folder")).toString()), "Its folder is removed once they have ended.");
	qputenv("FAKE_HARNESS_MODE", "ok");

	qputenv("FAKE_HARNESS_MODE", "limit");
	const AiChatResponse limited = ask(question(QStringLiteral("codex"), fake));
	expect(!limited.ok && limited.failure == AiChatFailure::RateLimited, "A usage limit comes back as a rate limit.", limited.errorMessage);
	qputenv("FAKE_HARNESS_MODE", "login");
	const AiChatResponse signedOut = ask(question(QStringLiteral("claude-code"), fake));
	expect(!signedOut.ok && signedOut.failure == AiChatFailure::Credential, "A signed-out tool comes back as a credential failure.", signedOut.errorMessage);

	qputenv("FAKE_HARNESS_MODE", "sleep");
	const AiChatResponse cancelled = ask(question(QStringLiteral("claude-code"), fake), 30000, [](AiChatClient& client) {
		QTimer::singleShot(300, [&client] { client.cancel(); });
	});
	expect(!cancelled.ok && cancelled.failure == AiChatFailure::Cancelled, "Cancelling stops the run.", cancelled.errorMessage);
	const AiChatResponse timedOut = ask(question(QStringLiteral("claude-code"), fake), 1000);
	expect(!timedOut.ok && timedOut.failure == AiChatFailure::Timeout, "A run that takes too long times out.", timedOut.errorMessage);
	qputenv("FAKE_HARNESS_MODE", "ok");

	const AiChatResponse missing = ask(question(QStringLiteral("claude-code"), temp.filePath(QStringLiteral("no-such-claude.exe"))));
	expect(!missing.ok, "A configured program that does not exist is not run.");

	expect(aiHarnessLoginStatus(QStringLiteral("claude-code"), fake).signedIn && aiHarnessLoginStatus(QStringLiteral("codex"), fake).method == QStringLiteral("ChatGPT"),
		"The status commands say who is signed in.");
	qputenv("FAKE_HARNESS_SIGNED_IN", "0");
	const AiHarnessLoginStatus out = aiHarnessLoginStatus(QStringLiteral("claude-code"), fake);
	expect(out.known && !out.signedIn && aiHarnessLoginStatusText(QStringLiteral("claude-code"), out).contains(QStringLiteral("claude auth login")), "And who is not.");
	qunsetenv("FAKE_HARNESS_SIGNED_IN");
	qunsetenv("ANTHROPIC_API_KEY");
}

void checkConnections(const QString& fake)
{
	AiAutomationPreferences preferences;
	preferences.aiFreeMode = false;
	preferences.preferredReasoningConnectorId = QStringLiteral("claude-code");
	preferences.connectorEndpoints.insert(QStringLiteral("claude-code"), fake);
	AiTextConnection connection = resolveAiTextConnection(preferences);
	expect(connection.block == AiTextConnectionBlock::CloudNotAllowed && aiTextConnectionBlockText(connection).contains(QStringLiteral("to Anthropic")),
		"A login harness still leaves the machine, so it needs cloud connectors allowed.", aiTextConnectionBlockText(connection));
	preferences.cloudConnectorsEnabled = true;
	connection = resolveAiTextConnection(preferences);
	expect(connection.ready() && connection.program == QFileInfo(fake).absoluteFilePath() && connection.model == QStringLiteral("claude-opus-5-5") && !connection.local,
		"Allowed, Claude Code is ready with claude-opus-5-5.", aiTextConnectionBlockText(connection));
	preferences.connectorEndpoints.insert(QStringLiteral("claude-code"), QStringLiteral("Z:/nowhere/claude.exe"));
	expect(resolveAiTextConnection(preferences).block == AiTextConnectionBlock::NoProgram, "A program that is not there blocks with where to get it.");

	preferences.preferredReasoningConnectorId = QStringLiteral("local-gguf");
	preferences.cloudConnectorsEnabled = false;
	connection = resolveAiTextConnection(preferences);
	if (aiGgufRuntimeAvailable()) {
		expect(connection.block == AiTextConnectionBlock::NoModel && connection.local, "Without a model file the GGUF connector asks for one.");
	} else {
		expect(connection.block == AiTextConnectionBlock::NoRuntime && aiTextConnectionBlockText(connection).contains(QStringLiteral("-Dggml=enabled")),
			"Without ggml in the build the GGUF connector says how to get it.");
	}
	expect(aiTextTransport(QStringLiteral("claude")) == AiTextTransport::Http && aiTextTransport(QStringLiteral("codex")) == AiTextTransport::Harness
			&& aiTextTransport(QStringLiteral("local-gguf")) == AiTextTransport::Gguf && aiConnectorHasChatTransport(QStringLiteral("custom-command")),
		"Each connector names its transport.");
}

void checkGrammar()
{
	QString error;
	const QString grammar = aiJsonSchemaGrammar(planSchema(), &error);
	expect(grammar.startsWith(QStringLiteral("root ::= ws ")) && grammar.contains(QStringLiteral("\"\\\"title\\\"\"")) && grammar.contains(QStringLiteral("char{0,40}"))
			&& grammar.contains(QStringLiteral("\"0\" | \"1\" | \"2\" | \"3\"")) && grammar.contains(QStringLiteral("\"\\\"start\\\"\" | \"\\\"exit\\\"\"")),
		"A schema becomes a grammar: keys in order, bounded strings, small integer ranges, enums.", grammar);
	expect(grammar.indexOf(QStringLiteral("\\\"title\\\"")) < grammar.indexOf(QStringLiteral("\\\"rooms\\\"")), "Properties follow the required order.");
	expect(grammar.contains(QStringLiteral("{1,3}")), "Array bounds become repetition.", grammar);
	expect(aiJsonSchemaGrammar(QJsonObject {{QStringLiteral("type"), QStringLiteral("string")}}).isEmpty(), "Only object schemas become grammars.");
	// Past llama.cpp's repetition limits the length is left open, and a literal holding "%1" is
	// written as it is.
	const QJsonObject large {
		{QStringLiteral("type"), QStringLiteral("object")},
		{QStringLiteral("required"), QJsonArray {QStringLiteral("notes"), QStringLiteral("marks")}},
		{QStringLiteral("properties"), QJsonObject {
			{QStringLiteral("notes"), QJsonObject {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 5000}}},
			{QStringLiteral("marks"), QJsonObject {{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("maxItems"), 2000},
				{QStringLiteral("items"), QJsonObject {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray {QStringLiteral("%1")}}}}}},
			{QStringLiteral("span"), QJsonObject {{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), -1e300}, {QStringLiteral("maximum"), 1e300}}},
		}},
	};
	const QString open = aiJsonSchemaGrammar(large, &error);
	expect(!open.contains(QStringLiteral("char{0,5000}")) && !open.contains(QStringLiteral("1999")) && open.contains(QStringLiteral("{0,}"))
			&& open.contains(QStringLiteral("\"\\\"%1\\\"\"")),
		"Large bounds leave the length open, and literals keep their text.", open);
	const QString plan = aiJsonSchemaGrammar(levelSemanticPlanSchema(QStringLiteral("quake")), &error);
	expect(!plan.isEmpty() && plan.count(QStringLiteral("::=")) > 5, "The level plan schema becomes a grammar.", error);

	// A GGUF request is reviewed like any other before it runs.
	AiChatRequest request;
	request.connectorId = QStringLiteral("local-gguf");
	request.model = QStringLiteral("Z:/models/missing.gguf");
	request.messages = {{QStringLiteral("user"), QStringLiteral("Plan.")}};
	AiHttpRequest http;
	expect(!buildAiHttpRequest(request, QString(), &http, &error) && error.contains(QStringLiteral("does not exist")), "A missing model file is named before anything runs.");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	if (argc != 2) {
		std::cerr << "usage: ai_harness_smoke_test <fake_ai_harness executable>\n";
		return EXIT_FAILURE;
	}
	const QString fake = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
	checkInvocations(fake);
	checkParsing();
	checkRuns(fake);
	checkConnections(fake);
	checkGrammar();
	if (failures > 0) {
		std::cerr << failures << " harness check(s) failed.\n";
		return EXIT_FAILURE;
	}
	std::cout << "Harness checks passed.\n";
	return EXIT_SUCCESS;
}
