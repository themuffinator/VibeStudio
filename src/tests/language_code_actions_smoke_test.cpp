#include "core/language_workspace_edit.h"
#include "tests/language_server_fixture.h"
#include <QProcess>
#include <QTemporaryDir>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* why) { if (!condition) { std::cerr << why << '\n'; } return condition; }
bool write(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
bool wait(const std::function<bool()>& ready, int ms = 4000)
{
	QElapsedTimer clock; clock.start(); while (!ready() && clock.elapsed() < ms) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(2); } return ready();
}
QJsonObject literal(const QString& title = QStringLiteral("Fix")) { return {{QStringLiteral("title"), title}, {QStringLiteral("edit"), QJsonObject {}}}; }
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); if (app.arguments().value(1) == QStringLiteral("--fixture")) { return runLanguageServerFixture(app.arguments().value(2)); }
	bool ok = true; QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	const QString path = temp.filePath(QStringLiteral("source.cpp")), other = temp.filePath(QStringLiteral("other.cpp"));
	const QByteArray original = "int value;\r\nvalue++;\n"; write(path, original); write(other, "value++;\r\n");
	const QString unicode = QString::fromUtf8("a😀b");
	ok &= expect(validLanguageCodeActionRange(unicode, 1, 2) && validLanguageCodeActionRange(unicode, 4, 0)
		&& !validLanguageCodeActionRange(unicode, 2, 0) && !validLanguageCodeActionRange(unicode, 1, 1) && !validLanguageCodeActionRange(unicode, 3, 2), "caret and selection ranges preserve UTF-16 boundaries");
	ok &= expect(parseLanguageCodeActions(QJsonValue(QJsonValue::Null), false).items.isEmpty() && !parseLanguageCodeActions(QJsonObject {}, true).error.isEmpty(), "null and malformed action responses are distinct");
	auto invalid = literal(); invalid.insert(QStringLiteral("title"), 42);
	auto command = literal(); command.insert(QStringLiteral("command"), QJsonObject {{QStringLiteral("command"), QStringLiteral("danger")}});
	auto lazy = literal(QStringLiteral("Lazy")); lazy.remove(QStringLiteral("edit")); lazy.insert(QStringLiteral("data"), QJsonObject {{QStringLiteral("opaque"), 27}});
	auto parsed = parseLanguageCodeActions(QJsonArray {invalid, literal(), command, lazy}, true);
	ok &= expect(parsed.skipped == 1 && parsed.items.size() == 3 && parsed.items[0].available() && !parsed.items[1].available() && parsed.items[2].needsResolve, "invalid entries are counted, edit-plus-command is unavailable, and lazy actions retain data");
	ok &= expect(!parseLanguageCodeActions(QJsonArray {lazy}, false).items[0].available(), "missing edits need negotiated resolve support");
	auto resolved = lazy; resolved.insert(QStringLiteral("edit"), QJsonObject {});
	ok &= expect(resolveLanguageCodeAction(parsed.items[2], resolved).available(), "resolve may add an edit while preserving opaque data");
	resolved.insert(QStringLiteral("title"), QStringLiteral("Changed"));
	ok &= expect(!resolveLanguageCodeAction(parsed.items[2], resolved).available(), "resolve cannot change the selected action's identity");
	QJsonArray excess; for (int i = 0; i < 201; ++i) { excess << literal(); }
	ok &= expect(parseLanguageCodeActions(excess, false).limited && parseLanguageCodeActions(excess, false).items.size() == 200, "bounded lists report truncation");
	auto oversized = literal(); oversized.insert(QStringLiteral("data"), QString(2 * 1024 * 1024, QLatin1Char('x')));
	ok &= expect(parseLanguageCodeActions(QJsonArray {oversized}, true).skipped == 1, "oversized action payloads cannot reach resolve or the editor");
	LanguageServerClient client;
	const auto connect = [&](const QString& mode, const QString& text, bool publication = true) {
		client.stop(); wait([&]() { return client.state() == QStringLiteral("stopped"); });
		client.start({app.applicationFilePath(), {QStringLiteral("--fixture"), mode}, temp.path(), 1500});
		if (!wait([&]() { return client.ready(); })) { return false; }
		client.synchronize({{path, QStringLiteral("cpp"), text}}); return !publication || mode == QStringLiteral("quiet") || wait([&]() { return client.diagnostics(path).received; });
	};
	ok &= expect(connect(QStringLiteral("actions-context"), QStringLiteral("bad value;\n"), false) && client.supportsCodeActions() && client.supportsCodeActionResolve(), "literal and resolve capabilities negotiate");
	LanguageCodeActions actions; bool received = false;
	client.codeActions(path, 0, 3, [&](const auto& result) { actions = result; received = true; });
	ok &= expect(wait([&]() { return received; }) && actions.error.isEmpty() && actions.items.size() == 5 && actions.length == 3 && actions.sourceSha256.size() == 32, "immediate requests wait for current diagnostic data and preserve the UTF-16 selection");
	if (actions.items.size() < 2) { return 1; }
	ok &= expect(actions.items[0].preferred && actions.items[1].needsResolve && !actions.items[2].available() && !actions.items[3].available() && !actions.items[4].available(), "preferred, lazy, disabled and command actions remain distinct");
	LanguageCodeAction action; received = false;
	client.resolveCodeAction(path, actions.version, actions.items[1], [&](const auto& result, const QString& why) { action = result; received = why.isEmpty(); });
	ok &= expect(wait([&]() { return received; }) && action.available() && !action.needsResolve, "opaque lazy action resolves to text edits");
	// The fixture echoes context to make provenance observable without exposing it in product UI.
	ok &= expect(connect(QStringLiteral("unversioned"), QStringLiteral("bad value;\n")), "unversioned diagnostics fixture"); received = false;
	client.codeActions(path, 0, 0, [&](const auto& result) { actions = result; received = true; }); wait([&]() { return received; });
	ok &= expect(received && !actions.items.isEmpty() && actions.items[0].wire.value(QStringLiteral("data")).toObject().value(QStringLiteral("context")).toObject().value(QStringLiteral("diagnostics")).toArray().isEmpty(), "unversioned diagnostics are not presented as current quick-fix context");
	ok &= expect(connect(QStringLiteral("actions-slow"), QStringLiteral("int value;\n")), "delayed actions fixture"); received = false;
	const int request = client.codeActions(path, 4, 5, [&](const auto&) { received = true; }); client.cancelRequest(request); wait([] { return false; }, 450);
	ok &= expect(!received, "cancellation retires delayed action replies");
	client.codeActions(path, 4, 5, [&](const auto&) { received = true; }); client.synchronize({{path, QStringLiteral("cpp"), QStringLiteral("int changed;\n")}}); wait([] { return false; }, 450);
	ok &= expect(!received, "source changes retire stale action replies");
	ok &= expect(connect(QStringLiteral("actions-resolve-slow"), QStringLiteral("int value;\n")), "delayed resolve fixture"); received = false;
	client.codeActions(path, 4, 0, [&](const auto& result) { actions = result; received = true; }); wait([&]() { return received; });
	if (actions.items.size() < 2) { return 1; } received = false;
	const int resolveId = client.resolveCodeAction(path, actions.version, actions.items[1], [&](const auto&, const QString&) { received = true; }); client.cancelRequest(resolveId); wait([] { return false; }, 450);
	ok &= expect(!received, "lazy resolve is cancellable");
	client.resolveCodeAction(path, actions.version, actions.items[1], [&](const auto&, const QString&) { received = true; }); client.synchronize({{path, QStringLiteral("cpp"), QStringLiteral("int edited;\n")}}); wait([] { return false; }, 450);
	ok &= expect(!received && client.resolveCodeAction(path, actions.version, actions.items[1], {}) < 0, "stale resolve cannot prepare edits");
	ok &= expect(connect(QStringLiteral("actions-boolean"), QStringLiteral("int value;\n")) && client.supportsCodeActions() && !client.supportsCodeActionResolve(), "boolean capability supports immediate edits only");
	client.stop(); wait([&]() { return client.state() == QStringLiteral("stopped"); });
	if (app.arguments().size() > 1) {
		const auto run = [&](const QString& mode, const QStringList& extra, int expected) {
			QProcess process; process.setWorkingDirectory(temp.path());
			process.start(app.arguments()[1], QStringList {QStringLiteral("--cli"), QStringLiteral("code"), QStringLiteral("language-server"), path, QStringLiteral("--server"), app.applicationFilePath(), QStringLiteral("--server-args"),
				QString::fromUtf8(QJsonDocument(QJsonArray {QStringLiteral("--fixture"), mode}).toJson(QJsonDocument::Compact)), QStringLiteral("--root"), temp.path(), QStringLiteral("--line"), QStringLiteral("1"), QStringLiteral("--column"), QStringLiteral("5"), QStringLiteral("--code-actions"), QStringLiteral("--timeout-ms"), QStringLiteral("1200"), QStringLiteral("--json")} + extra);
			const bool finished = process.waitForFinished(10000); const auto output = process.readAllStandardOutput();
			ok &= expect(finished && process.exitCode() == expected, qPrintable(QStringLiteral("CLI %1 expected %2 got %3: %4").arg(mode).arg(expected).arg(process.exitCode()).arg(QString::fromUtf8(output))));
			const auto json = QJsonDocument::fromJson(output); ok &= expect(json.isObject(), "code actions CLI emits JSON"); return json.object().value(QStringLiteral("languageServer")).toObject();
		};
		const QStringList first {QStringLiteral("--action-index"), QStringLiteral("1")}, second {QStringLiteral("--action-index"), QStringLiteral("2")};
		const auto list = run(QStringLiteral("quiet"), {}, 0).value(QStringLiteral("codeActions")).toObject();
		ok &= expect(list.value(QStringLiteral("items")).toArray().size() == 5 && read(path) == original, "CLI lists actions without requiring diagnostics or changing files");
		const auto preview = run(QStringLiteral("quiet"), first, 0).value(QStringLiteral("codeActions")).toObject().value(QStringLiteral("preview")).toObject();
		const QString hash = preview.value(QStringLiteral("planSha256")).toString();
		ok &= expect(hash.size() == 64 && preview.value(QStringLiteral("changes")).toArray().size() == 2 && read(path) == original, "CLI selected action exposes a complete multi-file preview and hash");
		const auto deferred = run(QStringLiteral("quiet"), second, 0).value(QStringLiteral("codeActions")).toObject().value(QStringLiteral("preview")).toObject();
		ok &= expect(deferred.value(QStringLiteral("planSha256")).toString() != hash && deferred.value(QStringLiteral("replacementCount")).toInt() == 3, "resolved preview binds the selected action title even when edits match");
		run(QStringLiteral("quiet"), {QStringLiteral("--write")}, 2); run(QStringLiteral("quiet"), first + QStringList {QStringLiteral("--write")}, 2);
		run(QStringLiteral("quiet"), first + QStringList {QStringLiteral("--end-line"), QStringLiteral("1")}, 2);
		run(QStringLiteral("quiet"), first + QStringList {QStringLiteral("--rename"), QStringLiteral("x")}, 2);
		const QStringList apply = first + QStringList {QStringLiteral("--write"), QStringLiteral("--expected-plan-sha256"), hash};
		run(QStringLiteral("quiet"), apply + QStringList {QStringLiteral("--dry-run")}, 2);
		run(QStringLiteral("quiet"), second + QStringList {QStringLiteral("--write"), QStringLiteral("--expected-plan-sha256"), hash}, 4);
		run(QStringLiteral("quiet"), {QStringLiteral("--action-index"), QStringLiteral("5")}, 4);
		run(QStringLiteral("actions-resource"), apply, 4); run(QStringLiteral("actions-resolve-identity"), second, 4); run(QStringLiteral("actions-resolve-command"), second, 4);
		run(QStringLiteral("actions-skipped"), {}, 4); run(QStringLiteral("actions-malformed"), {}, 4); run(QStringLiteral("no-actions"), {}, 4); run(QStringLiteral("actions-hang"), {}, 4);
		ok &= expect(read(path) == original && read(other) == QByteArray("value++;\r\n"), "unavailable, incomplete, stale-plan and unsupported actions never write a partial batch");
		const auto written = run(QStringLiteral("quiet"), apply, 0).value(QStringLiteral("codeActions")).toObject().value(QStringLiteral("preview")).toObject();
		ok &= expect(written.value(QStringLiteral("writtenFiles")).toArray().size() == 2 && read(path) == QByteArray("int fixedValue;\r\nfixedValue++;\n"), "reviewed action saves all targets and preserves mixed newlines");
	}
	return ok ? 0 : 1;
}
