#include "core/ai_harness.h"
#include "core/process_tree.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

namespace vibestudio {

namespace {

const QString kRunFolder = QStringLiteral("{dir}");

QString home()
{
	return QDir::homePath();
}

bool usableProgram(const QString& path)
{
	const QFileInfo info(path);
	return info.isFile() && (info.isExecutable() || info.suffix().compare(QStringLiteral("cmd"), Qt::CaseInsensitive) == 0
		|| info.suffix().compare(QStringLiteral("bat"), Qt::CaseInsensitive) == 0);
}

// A configured program: a path that exists, or a name found on PATH.
QString configuredProgram(const QString& configured)
{
	const QString value = configured.trimmed();
	if (value.isEmpty()) {
		return {};
	}
	if (usableProgram(value)) {
		return QFileInfo(value).absoluteFilePath();
	}
	if (!value.contains(QLatin1Char('/')) && !value.contains(QLatin1Char('\\'))) {
		return QStandardPaths::findExecutable(value);
	}
	return {};
}

QString toolName(AiHarnessKind kind)
{
	return kind == AiHarnessKind::ClaudeCode ? QStringLiteral("claude") : kind == AiHarnessKind::Codex ? QStringLiteral("codex") : QString();
}

QString displayName(const QString& connectorId)
{
	return aiHarnessToolName(connectorId);
}

// What a tool is not given, so its own login answers and its requests go where the studio says
// they go, and what is set for every run of it (no updates, surveys, or telemetry mid-request).
QStringList removedEnvironment(AiHarnessKind kind)
{
	if (kind == AiHarnessKind::ClaudeCode) {
		return {QStringLiteral("ANTHROPIC_API_KEY"), QStringLiteral("ANTHROPIC_AUTH_TOKEN"), QStringLiteral("ANTHROPIC_BASE_URL"),
			QStringLiteral("CLAUDE_CODE_USE_BEDROCK"), QStringLiteral("CLAUDE_CODE_USE_VERTEX"), QStringLiteral("CLAUDE_CODE_USE_FOUNDRY")};
	}
	if (kind == AiHarnessKind::Codex) {
		return {QStringLiteral("CODEX_API_KEY"), QStringLiteral("OPENAI_API_KEY"), QStringLiteral("OPENAI_BASE_URL")};
	}
	return {};
}

QList<QPair<QString, QString>> setEnvironment(AiHarnessKind kind)
{
	if (kind == AiHarnessKind::ClaudeCode) {
		return {{QStringLiteral("DISABLE_AUTOUPDATER"), QStringLiteral("1")}, {QStringLiteral("DISABLE_TELEMETRY"), QStringLiteral("1")},
			{QStringLiteral("DISABLE_ERROR_REPORTING"), QStringLiteral("1")}, {QStringLiteral("CLAUDE_CODE_DISABLE_FEEDBACK_SURVEY"), QStringLiteral("1")},
			{QStringLiteral("CLAUDE_CODE_DISABLE_TERMINAL_TITLE"), QStringLiteral("1")}, {QStringLiteral("CLAUDE_CODE_DISABLE_AUTO_MEMORY"), QStringLiteral("1")}};
	}
	return {};
}

} // namespace

QString aiHarnessToolName(const QString& connectorId)
{
	switch (aiHarnessKind(connectorId)) {
	case AiHarnessKind::ClaudeCode:
		return QStringLiteral("Claude Code");
	case AiHarnessKind::Codex:
		return QStringLiteral("Codex");
	case AiHarnessKind::CustomCommand:
	case AiHarnessKind::None:
		break;
	}
	return QCoreApplication::translate("VibeStudioAiHarness", "The command");
}

QString aiHarnessProvider(const QString& connectorId)
{
	switch (aiHarnessKind(connectorId)) {
	case AiHarnessKind::ClaudeCode:
		return QStringLiteral("Anthropic");
	case AiHarnessKind::Codex:
		return QStringLiteral("OpenAI");
	case AiHarnessKind::CustomCommand:
	case AiHarnessKind::None:
		break;
	}
	return {};
}

namespace {

// The text a single-prompt tool is given: one user message as it is, a longer conversation as a
// labelled transcript ending on the question.
QString conversationText(const AiChatRequest& request)
{
	if (request.messages.size() == 1) {
		return request.messages.first().text;
	}
	QStringList parts;
	parts << QStringLiteral("The conversation so far, oldest first:");
	for (const AiChatMessage& message : request.messages) {
		parts << QStringLiteral("[%1]\n%2").arg(message.role, message.text);
	}
	parts << QStringLiteral("Answer the last user message.");
	return parts.join(QStringLiteral("\n\n"));
}

// Instructions for a tool with no system prompt of its own, ahead of the question.
QString withInstructions(const AiChatRequest& request)
{
	const QString question = conversationText(request);
	return request.system.isEmpty() ? question : QStringLiteral("<instructions>\n%1\n</instructions>\n\n%2").arg(request.system, question);
}

QByteArray compactJson(const QJsonObject& object)
{
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

// npm installs a tool on Windows as a .cmd shim that starts node with the package's script. Run
// node with that script directly, so arguments never pass through cmd.exe and its quoting.
bool launchThroughShim(const QString& program, QString* launcher, QStringList* prefix, QString* error)
{
	const QFileInfo info(program);
	const QString suffix = info.suffix().toLower();
	if (suffix != QStringLiteral("cmd") && suffix != QStringLiteral("bat")) {
		*launcher = program;
		prefix->clear();
		return true;
	}
	QFile shim(program);
	if (!shim.open(QIODevice::ReadOnly)) {
		*error = QCoreApplication::translate("VibeStudioAiHarness", "Could not read %1.").arg(QDir::toNativeSeparators(program));
		return false;
	}
	const QString text = QString::fromLocal8Bit(shim.readAll());
	static const QRegularExpression script(QStringLiteral("\"%~?dp0%?\\\\([^\"]+\\.(?:js|cjs|mjs))\""), QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch match = script.match(text);
	if (!match.hasMatch()) {
		*error = QCoreApplication::translate("VibeStudioAiHarness",
			"%1 is a batch file VibeStudio cannot run safely. Point the connector at the program itself (an .exe), or at node and its script.")
					 .arg(QDir::toNativeSeparators(program));
		return false;
	}
	const QString scriptPath = info.absoluteDir().filePath(match.captured(1));
	const QString localNode = info.absoluteDir().filePath(QStringLiteral("node.exe"));
	const QString node = QFileInfo(localNode).isFile() ? localNode : QStandardPaths::findExecutable(QStringLiteral("node"));
	if (node.isEmpty() || !QFileInfo(scriptPath).isFile()) {
		*error = QCoreApplication::translate("VibeStudioAiHarness", "%1 needs Node.js, which was not found.").arg(QDir::toNativeSeparators(program));
		return false;
	}
	*launcher = node;
	*prefix = {QDir::toNativeSeparators(scriptPath)};
	return true;
}

// The first non-empty JSON object in the output: the whole of it, or its last line that is one.
QJsonObject outputObject(const QByteArray& output)
{
	const QJsonDocument whole = QJsonDocument::fromJson(output.trimmed());
	if (whole.isObject()) {
		return whole.object();
	}
	const QList<QByteArray> lines = output.split('\n');
	for (auto line = lines.crbegin(); line != lines.crend(); ++line) {
		const QJsonDocument document = QJsonDocument::fromJson(line->trimmed());
		if (document.isObject()) {
			return document.object();
		}
	}
	return {};
}

// A failure, classified by what the tool said.
AiChatResponse failed(const QString& connectorId, const QString& message)
{
	AiChatResponse response;
	const QString lower = message.toLower();
	// An older tool refusing an option VibeStudio passes: an update fixes it.
	if (aiHarnessKind(connectorId) != AiHarnessKind::CustomCommand && (lower.contains(QStringLiteral("unexpected argument")) || lower.contains(QStringLiteral("unknown option"))
		|| lower.contains(QStringLiteral("unrecognized option")) || lower.contains(QStringLiteral("unknown argument")))) {
		response.failure = AiChatFailure::NotConfigured;
		response.errorMessage = QCoreApplication::translate("VibeStudioAiHarness", "%1 is older than VibeStudio needs (%2). Update it and try again: %3")
									.arg(displayName(connectorId), message.trimmed(), aiHarnessUpdateHint(connectorId));
		return response;
	}
	if (lower.contains(QStringLiteral("/login")) || lower.contains(QStringLiteral("not logged in")) || lower.contains(QStringLiteral("log in"))
		|| lower.contains(QStringLiteral("login")) || lower.contains(QStringLiteral("authenticat")) || lower.contains(QStringLiteral("invalid api key"))
		|| lower.contains(QStringLiteral("unauthorized"))) {
		response.failure = AiChatFailure::Credential;
		response.errorMessage = QCoreApplication::translate("VibeStudioAiHarness", "%1 is not signed in (%2). %3")
									.arg(displayName(connectorId), message.trimmed(), aiHarnessSetupHint(connectorId));
		return response;
	}
	if (lower.contains(QStringLiteral("usage limit")) || lower.contains(QStringLiteral("rate limit")) || lower.contains(QStringLiteral("limit reached"))
		|| lower.contains(QStringLiteral("overloaded")) || lower.contains(QStringLiteral("too many requests"))) {
		response.failure = AiChatFailure::RateLimited;
	} else {
		response.failure = AiChatFailure::Provider;
	}
	response.errorMessage = message.trimmed().isEmpty() ? aiChatFailureText(response.failure) : message.trimmed();
	return response;
}

QString lastLines(const QByteArray& text, int count)
{
	const QStringList lines = QString::fromUtf8(text).trimmed().split(QLatin1Char('\n'));
	return lines.mid(std::max<qsizetype>(0, lines.size() - count)).join(QLatin1Char('\n')).trimmed();
}

AiChatResponse parseClaudeCode(int exitCode, const QByteArray& standardOutput, const QByteArray& standardError)
{
	const QString id = QStringLiteral("claude-code");
	const QJsonObject result = outputObject(standardOutput);
	if (result.isEmpty()) {
		const QString detail = lastLines(standardError.isEmpty() ? standardOutput : standardError, 6);
		AiChatResponse response = failed(id, detail.isEmpty() ? QCoreApplication::translate("VibeStudioAiHarness", "Claude Code exited with code %1 and no answer.").arg(exitCode) : detail);
		if (response.failure == AiChatFailure::Provider && exitCode == 0) {
			response.failure = AiChatFailure::BadResponse;
		}
		return response;
	}
	const QString subtype = result.value(QStringLiteral("subtype")).toString();
	const bool success = subtype == QStringLiteral("success") && !result.value(QStringLiteral("is_error")).toBool();
	AiChatResponse response;
	if (!success) {
		QStringList messages;
		for (const QJsonValue& error : result.value(QStringLiteral("errors")).toArray()) {
			messages << error.toString();
		}
		if (messages.isEmpty() && result.value(QStringLiteral("result")).isString()) {
			messages << result.value(QStringLiteral("result")).toString();
		}
		if (messages.isEmpty()) {
			messages << subtype;
		}
		response = failed(id, messages.join(QStringLiteral(" ")));
		if (subtype == QStringLiteral("error_max_structured_output_retries")) {
			response.failure = AiChatFailure::BadResponse;
		}
	} else {
		const QJsonValue structured = result.value(QStringLiteral("structured_output"));
		response.text = structured.isObject() ? QString::fromUtf8(compactJson(structured.toObject())) : result.value(QStringLiteral("result")).toString();
		response.finishReason = subtype;
		// Success with nothing in it is an answer no feature can use.
		response.ok = !response.text.trimmed().isEmpty();
		if (!response.ok) {
			response.failure = AiChatFailure::BadResponse;
			response.errorMessage = QCoreApplication::translate("VibeStudioAiHarness", "Claude Code finished without an answer.");
		}
	}
	const QJsonObject usage = result.value(QStringLiteral("usage")).toObject();
	if (!usage.isEmpty()) {
		response.inputTokens = usage.value(QStringLiteral("input_tokens")).toInt() + usage.value(QStringLiteral("cache_creation_input_tokens")).toInt()
			+ usage.value(QStringLiteral("cache_read_input_tokens")).toInt();
		response.outputTokens = usage.value(QStringLiteral("output_tokens")).toInt();
	}
	// The model that wrote the most: Claude Code may also ask a small helper model.
	const QJsonObject models = result.value(QStringLiteral("modelUsage")).toObject();
	double most = -1.0;
	for (auto model = models.constBegin(); model != models.constEnd(); ++model) {
		const double written = model.value().toObject().value(QStringLiteral("outputTokens")).toDouble();
		if (written > most) {
			most = written;
			response.model = model.key();
		}
	}
	return response;
}

AiChatResponse parseCodex(int exitCode, const QByteArray& standardOutput, const QByteArray& standardError, const QByteArray& answer)
{
	const QString id = QStringLiteral("codex");
	QString lastText;
	QString error;
	bool completed = false;
	int inputTokens = -1;
	int outputTokens = -1;
	for (const QByteArray& line : standardOutput.split('\n')) {
		const QJsonObject event = QJsonDocument::fromJson(line.trimmed()).object();
		const QString type = event.value(QStringLiteral("type")).toString();
		if (type == QStringLiteral("error")) {
			// Also sent for a stream error Codex retries and recovers from.
			error = event.value(QStringLiteral("message")).toString();
		} else if (type == QStringLiteral("turn.failed")) {
			completed = false;
			error = event.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString(error);
		} else if (type == QStringLiteral("turn.completed")) {
			completed = true;
			const QJsonObject usage = event.value(QStringLiteral("usage")).toObject();
			inputTokens = std::max(0, inputTokens) + usage.value(QStringLiteral("input_tokens")).toInt();
			outputTokens = std::max(0, outputTokens) + usage.value(QStringLiteral("output_tokens")).toInt();
		} else if (type == QStringLiteral("item.completed")) {
			const QJsonObject item = event.value(QStringLiteral("item")).toObject();
			if (item.value(QStringLiteral("type")).toString() == QStringLiteral("agent_message")) {
				lastText = item.value(QStringLiteral("text")).toString();
			}
		}
	}
	const QString text = answer.trimmed().isEmpty() ? lastText : QString::fromUtf8(answer).trimmed();
	AiChatResponse response;
	if ((!completed && !error.isEmpty()) || text.isEmpty()) {
		const QString detail = !error.isEmpty() ? error : lastLines(standardError, 6);
		response = failed(id, detail.isEmpty() ? QCoreApplication::translate("VibeStudioAiHarness", "Codex exited with code %1 and no answer.").arg(exitCode) : detail);
	} else {
		response.ok = true;
		response.text = text;
		response.finishReason = QStringLiteral("turn.completed");
	}
	response.inputTokens = inputTokens;
	response.outputTokens = outputTokens;
	return response;
}

AiChatResponse parseCustom(int exitCode, const QByteArray& standardOutput, const QByteArray& standardError, const QByteArray& answer)
{
	const QString text = QString::fromUtf8(answer.isEmpty() ? standardOutput : answer).trimmed();
	if (exitCode != 0 || text.isEmpty()) {
		const QString detail = lastLines(standardError.isEmpty() ? standardOutput : standardError, 6);
		return failed(QStringLiteral("custom-command"),
			detail.isEmpty() ? QCoreApplication::translate("VibeStudioAiHarness", "The command exited with code %1 and no answer.").arg(exitCode) : detail);
	}
	AiChatResponse response;
	response.ok = true;
	response.text = text;
	response.finishReason = QStringLiteral("exit");
	return response;
}

} // namespace

AiHarnessKind aiHarnessKind(const QString& connectorId)
{
	const QString id = normalizedAiId(connectorId);
	if (id == QStringLiteral("claude-code")) {
		return AiHarnessKind::ClaudeCode;
	}
	if (id == QStringLiteral("codex")) {
		return AiHarnessKind::Codex;
	}
	if (id == QStringLiteral("custom-command")) {
		return AiHarnessKind::CustomCommand;
	}
	return AiHarnessKind::None;
}

bool aiConnectorIsHarness(const QString& connectorId)
{
	return aiHarnessKind(connectorId) != AiHarnessKind::None;
}

QStringList aiHarnessSearchLocations(const QString& connectorId)
{
	const AiHarnessKind kind = aiHarnessKind(connectorId);
	const QString name = toolName(kind);
	if (name.isEmpty()) {
		return {};
	}
	QStringList locations {QStringLiteral("PATH")};
	const auto native = [](const QString& path) { return QDir::toNativeSeparators(path); };
#ifdef Q_OS_WIN
	locations << native(QDir(home()).filePath(QStringLiteral(".local/bin/%1.exe").arg(name)));
	if (kind == AiHarnessKind::ClaudeCode) {
		locations << native(QDir(home()).filePath(QStringLiteral(".claude/local/claude.exe")));
	}
	const QString appData = qEnvironmentVariable("APPDATA");
	if (!appData.isEmpty()) {
		locations << native(QDir(appData).filePath(QStringLiteral("npm/%1.cmd").arg(name)));
	}
#else
	locations << native(QDir(home()).filePath(QStringLiteral(".local/bin/%1").arg(name)));
	if (kind == AiHarnessKind::ClaudeCode) {
		locations << native(QDir(home()).filePath(QStringLiteral(".claude/local/claude")));
	}
	locations << QStringLiteral("/opt/homebrew/bin/%1").arg(name) << QStringLiteral("/usr/local/bin/%1").arg(name);
#endif
	return locations;
}

QString aiHarnessProgram(const QString& connectorId, const QString& configured)
{
	const AiHarnessKind kind = aiHarnessKind(connectorId);
	if (kind == AiHarnessKind::None) {
		return {};
	}
	if (kind == AiHarnessKind::CustomCommand) {
		const QStringList words = QProcess::splitCommand(configured);
		return words.isEmpty() ? QString() : configuredProgram(words.first());
	}
	if (!configured.trimmed().isEmpty()) {
		return configuredProgram(configured);
	}
	for (const QString& location : aiHarnessSearchLocations(connectorId)) {
		const QString found = location == QStringLiteral("PATH") ? QStandardPaths::findExecutable(toolName(kind)) : (usableProgram(location) ? location : QString());
		if (!found.isEmpty()) {
			return QFileInfo(found).absoluteFilePath();
		}
	}
	return {};
}

QString aiHarnessUpdateHint(const QString& connectorId)
{
	switch (aiHarnessKind(connectorId)) {
	case AiHarnessKind::ClaudeCode:
		return QStringLiteral("claude update");
	case AiHarnessKind::Codex:
		return QStringLiteral("npm install -g @openai/codex@latest");
	case AiHarnessKind::CustomCommand:
	case AiHarnessKind::None:
		break;
	}
	return QCoreApplication::translate("VibeStudioAiHarness", "check the command's options");
}

QString aiHarnessSetupHint(const QString& connectorId)
{
	switch (aiHarnessKind(connectorId)) {
	case AiHarnessKind::ClaudeCode:
		return QCoreApplication::translate("VibeStudioAiHarness", "Install Claude Code (https://code.claude.com/docs/en/setup), then sign in with: claude auth login");
	case AiHarnessKind::Codex:
		return QCoreApplication::translate("VibeStudioAiHarness", "Install Codex (npm install -g @openai/codex), then sign in with: codex login");
	case AiHarnessKind::CustomCommand:
		return QCoreApplication::translate("VibeStudioAiHarness", "Set the command line in Settings > AI and Automation, for example: gemini -m {model} (the prompt arrives on standard input)");
	case AiHarnessKind::None:
		break;
	}
	return {};
}

bool buildAiHarnessInvocation(const AiChatRequest& request, const QString& program, AiHarnessInvocation* out, QString* error)
{
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	const AiHarnessKind kind = aiHarnessKind(request.connectorId);
	if (kind == AiHarnessKind::None || !out) {
		return fail(QCoreApplication::translate("VibeStudioAiHarness", "%1 is not a command-line connector.").arg(request.connectorId));
	}
	if (request.messages.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioAiTransport", "There is nothing to ask."));
	}
	AiHarnessInvocation invocation;
	invocation.connectorId = normalizedAiId(request.connectorId);
	const QString model = request.model.trimmed();
	QString launchProgram = program;
	QStringList arguments;
	if (kind == AiHarnessKind::ClaudeCode) {
		// Print mode, safe mode (no CLAUDE.md, hooks, plugins, MCP or memory), no tools, no saved
		// session; the answer is the JSON result object on standard output.
		arguments << QStringLiteral("-p") << QStringLiteral("--safe-mode") << QStringLiteral("--output-format") << QStringLiteral("json")
				  << QStringLiteral("--no-session-persistence") << QStringLiteral("--tools") << QString() << QStringLiteral("--strict-mcp-config")
				  << QStringLiteral("--max-turns") << QStringLiteral("3");
		if (!model.isEmpty()) {
			arguments << QStringLiteral("--model") << model;
		}
		if (!request.system.isEmpty()) {
			invocation.files.append({QStringLiteral("system.txt"), request.system.toUtf8()});
			arguments << QStringLiteral("--system-prompt-file") << kRunFolder + QStringLiteral("/system.txt");
		}
		if (!request.responseSchema.isEmpty()) {
			arguments << QStringLiteral("--json-schema") << QString::fromUtf8(compactJson(aiProviderResponseSchema(QStringLiteral("claude"), request.responseSchema)));
		}
		invocation.input = conversationText(request).toUtf8();
	} else if (kind == AiHarnessKind::Codex) {
		// No tools and none of the user's config.toml (its MCP servers, hooks, and plugins), a
		// read-only sandbox in the empty run folder for anything left, no saved session, events
		// as JSON lines, the last message written to a file. Feature switches an older or newer
		// Codex does not know are ignored.
		arguments << QStringLiteral("exec") << QStringLiteral("--skip-git-repo-check") << QStringLiteral("--sandbox") << QStringLiteral("read-only")
				  << QStringLiteral("--ephemeral") << QStringLiteral("--ignore-rules") << QStringLiteral("--ignore-user-config");
		for (const char* feature : {"shell_tool", "unified_exec", "apps", "plugins", "multi_agent", "browser_use", "computer_use", "image_generation", "hooks"}) {
			arguments << QStringLiteral("-c") << QStringLiteral("features.%1=false").arg(QLatin1String(feature));
		}
		arguments << QStringLiteral("--color") << QStringLiteral("never") << QStringLiteral("--json") << QStringLiteral("-C") << kRunFolder << QStringLiteral("-o")
				  << kRunFolder + QStringLiteral("/answer.txt");
		if (!model.isEmpty()) {
			arguments << QStringLiteral("-m") << model;
		}
		if (!request.responseSchema.isEmpty()) {
			invocation.files.append({QStringLiteral("schema.json"), compactJson(aiProviderResponseSchema(QStringLiteral("openai"), request.responseSchema))});
			arguments << QStringLiteral("--output-schema") << kRunFolder + QStringLiteral("/schema.json");
		}
		arguments << QStringLiteral("-");
		invocation.input = withInstructions(request).toUtf8();
		invocation.answerFile = QStringLiteral("answer.txt");
	} else {
		QStringList words = QProcess::splitCommand(program);
		if (words.isEmpty()) {
			return fail(QCoreApplication::translate("VibeStudioAiHarness", "No command is set for the custom command connector."));
		}
		launchProgram = configuredProgram(words.takeFirst());
		if (launchProgram.isEmpty()) {
			return fail(QCoreApplication::translate("VibeStudioAiHarness", "The custom command's program was not found."));
		}
		bool promptFile = false;
		bool systemFile = false;
		for (QString word : words) {
			// With no model set, "{model}" and the option before it ("-m {model}") are left out, so
			// the program uses its own default.
			if (model.isEmpty() && word.contains(QStringLiteral("{model}"))) {
				if (word == QStringLiteral("{model}") && !arguments.isEmpty() && arguments.constLast().startsWith(QLatin1Char('-'))) {
					arguments.removeLast();
				}
				continue;
			}
			promptFile = promptFile || word.contains(QStringLiteral("{prompt_file}"));
			systemFile = systemFile || word.contains(QStringLiteral("{system_file}"));
			if (word.contains(QStringLiteral("{schema_file}"))) {
				invocation.files.append({QStringLiteral("schema.json"), compactJson(request.responseSchema)});
			}
			if (word.contains(QStringLiteral("{answer_file}"))) {
				invocation.answerFile = QStringLiteral("answer.txt");
			}
			word.replace(QStringLiteral("{model}"), model);
			word.replace(QStringLiteral("{prompt_file}"), kRunFolder + QStringLiteral("/prompt.txt"));
			word.replace(QStringLiteral("{system_file}"), kRunFolder + QStringLiteral("/system.txt"));
			word.replace(QStringLiteral("{schema_file}"), kRunFolder + QStringLiteral("/schema.json"));
			word.replace(QStringLiteral("{answer_file}"), kRunFolder + QStringLiteral("/answer.txt"));
			arguments << word;
		}
		if (systemFile) {
			invocation.files.append({QStringLiteral("system.txt"), request.system.toUtf8()});
		}
		const QByteArray prompt = (systemFile ? conversationText(request) : withInstructions(request)).toUtf8();
		if (promptFile) {
			invocation.files.append({QStringLiteral("prompt.txt"), prompt});
		} else {
			invocation.input = prompt;
		}
	}
	if (launchProgram.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioAiHarness", "%1 was not found. %2").arg(displayName(request.connectorId), aiHarnessSetupHint(request.connectorId)));
	}
	invocation.removedEnvironment = removedEnvironment(kind);
	invocation.setEnvironment = setEnvironment(kind);
	QStringList prefix;
	QString shimError;
	if (!launchThroughShim(launchProgram, &invocation.program, &prefix, &shimError)) {
		return fail(shimError);
	}
	invocation.arguments = prefix + arguments;
	*out = invocation;
	return true;
}

AiHttpRequest aiHarnessRequestRecord(const AiHarnessInvocation& invocation)
{
	AiHttpRequest record;
	record.command = QStringList {invocation.program} + invocation.arguments;
	record.body = invocation.input;
	record.files = invocation.files;
	record.removedEnvironment = invocation.removedEnvironment;
	return record;
}

AiChatResponse parseAiHarnessOutput(const QString& connectorId, int exitCode, const QByteArray& standardOutput, const QByteArray& standardError, const QByteArray& answer)
{
	switch (aiHarnessKind(connectorId)) {
	case AiHarnessKind::ClaudeCode:
		return parseClaudeCode(exitCode, standardOutput, standardError);
	case AiHarnessKind::Codex:
		return parseCodex(exitCode, standardOutput, standardError, answer);
	case AiHarnessKind::CustomCommand:
		return parseCustom(exitCode, standardOutput, standardError, answer);
	case AiHarnessKind::None:
		break;
	}
	return failed(connectorId, QCoreApplication::translate("VibeStudioAiHarness", "%1 is not a command-line connector.").arg(connectorId));
}

AiHarnessLoginStatus aiHarnessLoginStatus(const QString& connectorId, const QString& program, int timeoutMsecs)
{
	AiHarnessLoginStatus status;
	const AiHarnessKind kind = aiHarnessKind(connectorId);
	if ((kind != AiHarnessKind::ClaudeCode && kind != AiHarnessKind::Codex) || program.isEmpty()) {
		return status;
	}
	QString launcher;
	QStringList prefix;
	QString error;
	if (!launchThroughShim(program, &launcher, &prefix, &error)) {
		status.detail = error;
		return status;
	}
	// Asked as a request would run, so the answer is about the login a request would use.
	QProcess process;
	QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	for (const QString& name : removedEnvironment(kind)) {
		environment.remove(name);
	}
	for (const auto& variable : setEnvironment(kind)) {
		environment.insert(variable.first, variable.second);
	}
	process.setProcessEnvironment(environment);
	process.setWorkingDirectory(QDir::tempPath());
	const QStringList arguments = kind == AiHarnessKind::ClaudeCode ? QStringList {QStringLiteral("auth"), QStringLiteral("status"), QStringLiteral("--json")}
																	: QStringList {QStringLiteral("login"), QStringLiteral("status")};
	ProcessTree tree(&process);
	process.start(launcher, prefix + arguments);
	tree.adopt();
	if (!process.waitForStarted(timeoutMsecs) || !process.waitForFinished(timeoutMsecs)) {
		tree.kill();
		process.waitForFinished(2000);
		status.detail = QCoreApplication::translate("VibeStudioAiHarness", "%1 did not say whether it is signed in.").arg(displayName(connectorId));
		return status;
	}
	const QByteArray output = process.readAllStandardOutput() + process.readAllStandardError();
	if (kind == AiHarnessKind::ClaudeCode) {
		const QJsonObject result = outputObject(output);
		if (result.contains(QStringLiteral("loggedIn"))) {
			status.known = true;
			status.signedIn = result.value(QStringLiteral("loggedIn")).toBool();
			status.method = result.value(QStringLiteral("authMethod")).toString();
		}
	} else {
		const QString text = QString::fromUtf8(output).trimmed();
		static const QRegularExpression loggedIn(QStringLiteral("Logged in(?: using (.+))?"), QRegularExpression::CaseInsensitiveOption);
		const QRegularExpressionMatch match = loggedIn.match(text);
		status.known = !text.isEmpty();
		status.signedIn = match.hasMatch() && !text.contains(QStringLiteral("Not logged in"), Qt::CaseInsensitive);
		status.method = status.signedIn ? match.captured(1).trimmed() : QString();
	}
	status.detail = lastLines(output, 3);
	return status;
}

QString aiHarnessLoginStatusText(const QString& connectorId, const AiHarnessLoginStatus& status)
{
	if (!status.known) {
		return status.detail.isEmpty() ? QCoreApplication::translate("VibeStudioAiHarness", "%1 did not say whether it is signed in.").arg(displayName(connectorId)) : status.detail;
	}
	if (!status.signedIn) {
		return QCoreApplication::translate("VibeStudioAiHarness", "%1 is not signed in. %2").arg(displayName(connectorId), aiHarnessSetupHint(connectorId));
	}
	if (status.method == QStringLiteral("api_key") || status.method == QStringLiteral("api_key_helper")) {
		return QCoreApplication::translate("VibeStudioAiHarness", "%1 is signed in with an API key (%2), so requests are billed to it, not to a subscription.")
			.arg(displayName(connectorId), status.method);
	}
	return status.method.isEmpty() ? QCoreApplication::translate("VibeStudioAiHarness", "%1 is signed in.").arg(displayName(connectorId))
								   : QCoreApplication::translate("VibeStudioAiHarness", "%1 is signed in (%2).").arg(displayName(connectorId), status.method);
}

} // namespace vibestudio
