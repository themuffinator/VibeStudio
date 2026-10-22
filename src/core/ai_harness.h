#pragma once

// Agent command-line tools as text connectors. Claude Code (`claude -p`) and Codex (`codex exec`)
// answer with the user's own subscription login, so a Claude or ChatGPT plan works with no API key;
// a custom command runs any other program the same way. Every text pipeline that uses
// core/ai_transport (the Assistant, both level generators' planners, map AI edits, connection
// tests) can use them unchanged: AiChatClient runs the program instead of posting a request.
//
// A harness only answers. It runs in a new, empty folder with its own tools turned off where the
// tool allows it (Claude Code: no built-in tools, no MCP servers, safe mode; Codex: no shell or
// other tools, none of the user's config.toml with its MCP servers and hooks, and a read-only
// sandbox for whatever is left), so it cannot touch the project; whatever it proposes goes
// through the studio's normal review, as a provider's answer would. Credentials stay with the
// tool: VibeStudio never reads them, and passes neither tool the variables that would send its
// requests elsewhere (an API key, a base URL, a cloud platform switch), so the login is what
// answers (the Claude and OpenAI connectors are the API-key routes).
//
// Building invocations and reading output are plain functions, testable without the tools.

#include "core/ai_transport.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace vibestudio {

enum class AiHarnessKind {
	None,
	ClaudeCode,
	Codex,
	CustomCommand,
};

[[nodiscard]] AiHarnessKind aiHarnessKind(const QString& connectorId);
[[nodiscard]] bool aiConnectorIsHarness(const QString& connectorId);

// The program a harness connector runs: the configured one (a path, or a name on PATH), else the
// one found on PATH or in the tool's usual install folders. Empty when there is none. For a custom
// command, `configured` is the whole command line and the program is its first word.
[[nodiscard]] QString aiHarnessProgram(const QString& connectorId, const QString& configured = QString());
// Where aiHarnessProgram looks, for messages that say what was not found.
[[nodiscard]] QStringList aiHarnessSearchLocations(const QString& connectorId);
// The tool's own name, for sentences: Claude Code, Codex, or the command.
[[nodiscard]] QString aiHarnessToolName(const QString& connectorId);
// Who answers through the tool: Anthropic, OpenAI, or empty for a custom command.
[[nodiscard]] QString aiHarnessProvider(const QString& connectorId);
// The command to install or sign in, as the tool's own documentation gives it.
[[nodiscard]] QString aiHarnessSetupHint(const QString& connectorId);
// The command that updates the tool, for an older one that refuses an option.
[[nodiscard]] QString aiHarnessUpdateHint(const QString& connectorId);

// One run of a harness. `{dir}` in arguments and file names stands for the run's own empty folder.
struct AiHarnessInvocation {
	QString connectorId;
	// What is started: the tool itself, or node.exe with an npm shim's script.
	QString program;
	QStringList arguments;
	// Written to standard input, which is then closed.
	QByteArray input;
	// Files written into the run folder first, by name.
	QList<QPair<QString, QByteArray>> files;
	// Environment variables not passed on, and ones set for the run.
	QStringList removedEnvironment;
	QList<QPair<QString, QString>> setEnvironment;
	// The file in the run folder the answer is read from; empty reads standard output.
	QString answerFile;
};

// `program` is aiHarnessProgram's answer (or a configured command line for a custom command).
bool buildAiHarnessInvocation(const AiChatRequest& request, const QString& program, AiHarnessInvocation* out, QString* error = nullptr);
// The invocation as AiHttpRequest carries it for review and consent (command, files, input).
[[nodiscard]] AiHttpRequest aiHarnessRequestRecord(const AiHarnessInvocation& invocation);
// Reads a finished run. `answer` is the answer file's contents, when the invocation names one.
[[nodiscard]] AiChatResponse parseAiHarnessOutput(const QString& connectorId, int exitCode, const QByteArray& standardOutput, const QByteArray& standardError,
	const QByteArray& answer = QByteArray());

// Whether the tool is signed in, from its own status command (`claude auth status --json`,
// `codex login status`). Runs the program, so call it for status views and tests, not per request.
struct AiHarnessLoginStatus {
	// The tool answered the question.
	bool known = false;
	bool signedIn = false;
	// How it is signed in, as the tool says it: claude.ai, ChatGPT, an API key...
	QString method;
	QString detail;
};
[[nodiscard]] AiHarnessLoginStatus aiHarnessLoginStatus(const QString& connectorId, const QString& program, int timeoutMsecs = 20000);
[[nodiscard]] QString aiHarnessLoginStatusText(const QString& connectorId, const AiHarnessLoginStatus& status);

} // namespace vibestudio
