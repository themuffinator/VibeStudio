// A stand-in for Claude Code and Codex in tests: it answers the way `claude -p --output-format json`
// and `codex exec --json -o <file>` do, and records how it was run. No network, no login.
//
//  - FAKE_HARNESS_RECORD: a file to write {arguments, input, folder, files, anthropicKey} to.
//  - FAKE_HARNESS_ANSWER: the answer text (a JSON object for structured requests).
//  - FAKE_HARNESS_MODE: ok (default), login (signed out), limit (usage limit), sleep (never answers),
//    spawn (starts a child that keeps working, as node does with Codex's own binary, and never
//    answers), heartbeat (the child: appends to FAKE_HARNESS_HEARTBEAT every 100 ms).
//  - FAKE_HARNESS_SIGNED_IN: 0 makes the status commands say signed out.

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QThread>

#include <cstdio>
#include <iostream>

namespace {

QByteArray readInput()
{
	QFile input;
	if (!input.open(stdin, QIODevice::ReadOnly)) {
		return {};
	}
	return input.readAll();
}

void write(const QByteArray& bytes)
{
	std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
	std::fflush(stdout);
}

QString valueAfter(const QStringList& arguments, const QString& flag)
{
	const qsizetype index = arguments.indexOf(flag);
	return index >= 0 && index + 1 < arguments.size() ? arguments.at(index + 1) : QString();
}

void record(const QStringList& arguments, const QByteArray& input)
{
	const QString path = qEnvironmentVariable("FAKE_HARNESS_RECORD");
	if (path.isEmpty()) {
		return;
	}
	QJsonObject files;
	for (const QString& name : QDir::current().entryList(QDir::Files)) {
		QFile file(name);
		if (file.open(QIODevice::ReadOnly)) {
			files.insert(name, QString::fromUtf8(file.readAll()));
		}
	}
	const QJsonObject object {
		{QStringLiteral("arguments"), QJsonArray::fromStringList(arguments)},
		{QStringLiteral("input"), QString::fromUtf8(input)},
		{QStringLiteral("folder"), QDir::currentPath()},
		{QStringLiteral("files"), files},
		{QStringLiteral("anthropicKey"), qEnvironmentVariableIsSet("ANTHROPIC_API_KEY")},
		{QStringLiteral("autoUpdater"), qEnvironmentVariable("DISABLE_AUTOUPDATER")},
	};
	QFile out(path);
	if (out.open(QIODevice::WriteOnly)) {
		out.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
	}
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	const QStringList arguments = app.arguments().mid(1);
	const QString mode = qEnvironmentVariable("FAKE_HARNESS_MODE", QStringLiteral("ok"));
	const bool signedIn = qEnvironmentVariable("FAKE_HARNESS_SIGNED_IN", QStringLiteral("1")) != QStringLiteral("0");
	const QString answer = qEnvironmentVariable("FAKE_HARNESS_ANSWER", QStringLiteral("OK"));

	// The status commands.
	if (arguments.value(0) == QStringLiteral("auth") && arguments.value(1) == QStringLiteral("status")) {
		write(QJsonDocument(QJsonObject {{QStringLiteral("loggedIn"), signedIn}, {QStringLiteral("authMethod"), signedIn ? QStringLiteral("claude.ai") : QStringLiteral("none")}})
				  .toJson());
		return signedIn ? 0 : 1;
	}
	if (arguments.value(0) == QStringLiteral("login") && arguments.value(1) == QStringLiteral("status")) {
		write(signedIn ? QByteArrayLiteral("Logged in using ChatGPT\n") : QByteArrayLiteral("Not logged in\n"));
		return signedIn ? 0 : 1;
	}

	if (mode == QStringLiteral("heartbeat")) {
		for (int beat = 0; beat < 600; ++beat) {
			QFile heart(qEnvironmentVariable("FAKE_HARNESS_HEARTBEAT"));
			if (heart.open(QIODevice::Append)) {
				heart.write(".");
			}
			heart.close();
			QThread::msleep(100);
		}
		return 0;
	}

	const QByteArray input = readInput();
	record(arguments, input);
	if (mode == QStringLiteral("sleep")) {
		QThread::sleep(60);
		return 0;
	}
	if (mode == QStringLiteral("spawn")) {
		QProcess child;
		QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
		environment.insert(QStringLiteral("FAKE_HARNESS_MODE"), QStringLiteral("heartbeat"));
		child.setProcessEnvironment(environment);
		child.start(QCoreApplication::applicationFilePath(), {});
		child.waitForStarted();
		QThread::sleep(60);
		return 0;
	}

	if (arguments.value(0) == QStringLiteral("exec")) {
		// Codex: events as JSON lines, the last message in the -o file.
		write(QByteArrayLiteral("{\"type\":\"thread.started\",\"thread_id\":\"fake\"}\n{\"type\":\"turn.started\"}\n"));
		if (mode == QStringLiteral("limit") || mode == QStringLiteral("login")) {
			const QByteArray message = mode == QStringLiteral("limit") ? QByteArrayLiteral("You've hit your usage limit.") : QByteArrayLiteral("Not logged in. Run codex login.");
			write("{\"type\":\"error\",\"message\":\"" + message + "\"}\n{\"type\":\"turn.failed\",\"error\":{\"message\":\"" + message + "\"}}\n");
			return 1;
		}
		const QJsonObject item {{QStringLiteral("type"), QStringLiteral("item.completed")},
			{QStringLiteral("item"), QJsonObject {{QStringLiteral("id"), QStringLiteral("item_0")}, {QStringLiteral("type"), QStringLiteral("agent_message")}, {QStringLiteral("text"), answer}}}};
		write(QJsonDocument(item).toJson(QJsonDocument::Compact) + "\n");
		write(QByteArrayLiteral("{\"type\":\"turn.completed\",\"usage\":{\"input_tokens\":120,\"cached_input_tokens\":0,\"output_tokens\":30}}\n"));
		QFile last(valueAfter(arguments, QStringLiteral("-o")));
		if (last.open(QIODevice::WriteOnly)) {
			last.write(answer.toUtf8());
		}
		return 0;
	}

	// Claude Code: one JSON result object.
	QJsonObject result {{QStringLiteral("type"), QStringLiteral("result")}, {QStringLiteral("session_id"), QStringLiteral("fake")}, {QStringLiteral("num_turns"), 1}};
	if (mode == QStringLiteral("login")) {
		result.insert(QStringLiteral("subtype"), QStringLiteral("success"));
		result.insert(QStringLiteral("is_error"), true);
		result.insert(QStringLiteral("result"), QStringLiteral("Not logged in · Please run /login"));
		write(QJsonDocument(result).toJson(QJsonDocument::Compact));
		return 1;
	}
	if (mode == QStringLiteral("limit")) {
		result.insert(QStringLiteral("subtype"), QStringLiteral("error_during_execution"));
		result.insert(QStringLiteral("is_error"), true);
		result.insert(QStringLiteral("errors"), QJsonArray {QStringLiteral("Claude usage limit reached.")});
		write(QJsonDocument(result).toJson(QJsonDocument::Compact));
		return 1;
	}
	result.insert(QStringLiteral("subtype"), QStringLiteral("success"));
	result.insert(QStringLiteral("is_error"), false);
	if (arguments.contains(QStringLiteral("--json-schema"))) {
		result.insert(QStringLiteral("result"), QString());
		result.insert(QStringLiteral("structured_output"), QJsonDocument::fromJson(answer.toUtf8()).object());
	} else {
		result.insert(QStringLiteral("result"), answer);
	}
	result.insert(QStringLiteral("usage"), QJsonObject {{QStringLiteral("input_tokens"), 40}, {QStringLiteral("cache_read_input_tokens"), 2}, {QStringLiteral("output_tokens"), 9}});
	// A small helper model is listed too, ahead of the answering one alphabetically.
	const QString model = valueAfter(arguments, QStringLiteral("--model"));
	result.insert(QStringLiteral("modelUsage"), QJsonObject {
		{model.isEmpty() ? QStringLiteral("claude-default") : model, QJsonObject {{QStringLiteral("inputTokens"), 40}, {QStringLiteral("outputTokens"), 9}}},
		{QStringLiteral("a-helper-model"), QJsonObject {{QStringLiteral("inputTokens"), 5}, {QStringLiteral("outputTokens"), 1}}},
	});
	write(QJsonDocument(result).toJson(QJsonDocument::Compact));
	return 0;
}
