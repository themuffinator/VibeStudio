#include "core/language_rename.h"
#include "tests/language_server_fixture.h"

#include <QProcess>
#include <QTemporaryDir>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* why) { if (!condition) { std::cerr << why << '\n'; } return condition; }
bool write(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
QJsonObject pos(int line, int character) { return {{QStringLiteral("line"), line}, {QStringLiteral("character"), character}}; }
QJsonObject range(int line, int first, int last) { return {{QStringLiteral("start"), pos(line, first)}, {QStringLiteral("end"), pos(line, last)}}; }
QJsonObject edit(int line, int first, int last, const QString& text) { return {{QStringLiteral("range"), range(line, first, last)}, {QStringLiteral("newText"), text}}; }
QJsonObject target(const QString& path, const QJsonArray& edits, QJsonValue version = QJsonValue(QJsonValue::Null))
{
	return {{QStringLiteral("textDocument"), QJsonObject {{QStringLiteral("uri"), languageServerUri(path)}, {QStringLiteral("version"), version}}}, {QStringLiteral("edits"), edits}};
}
QJsonObject workspace(const QJsonArray& changes) { return {{QStringLiteral("documentChanges"), changes}}; }
bool wait(const std::function<bool()>& ready, int ms = 4000)
{
	QElapsedTimer clock; clock.start(); while (!ready() && clock.elapsed() < ms) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(2); } return ready();
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); if (app.arguments().value(1) == QStringLiteral("--fixture")) { return runLanguageServerFixture(app.arguments().value(2)); }
	bool ok = true; QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	const QString path = temp.filePath(QStringLiteral("source.cpp")), other = temp.filePath(QStringLiteral("other.cpp"));
	const QByteArray original = "int value;\r\nvalue++;\n";
	write(path, original); write(other, "value++;\r\n");
	ok &= expect(validLanguageRenameName(QString::fromUtf8("値")) && !validLanguageRenameName(QStringLiteral("x\ny")) && !validLanguageRenameName(QStringLiteral(" ")) && !validLanguageRenameName(QString(QChar(0xd800))), "name validation allows Unicode but rejects empty, controls and invalid Unicode");
	const auto preparation = parseLanguageRenamePreparation(QJsonObject {{QStringLiteral("range"), range(0, 4, 9)}, {QStringLiteral("placeholder"), QStringLiteral("value")}}, QStringLiteral("int value;"), 5);
	ok &= expect(preparation.error.isEmpty() && preparation.offset == 4 && preparation.length == 5, "prepareRename accepts a verified range and placeholder");
	ok &= expect(!parseLanguageRenamePreparation(range(0, 4, 9), QStringLiteral("int value;"), 1).error.isEmpty()
		&& !parseLanguageRenamePreparation(QJsonValue(QJsonValue::Null), QStringLiteral("int value;"), 5).error.isEmpty(), "unrenamable positions and unrelated ranges are rejected");
	LanguageRenameRequest request; request.rootPath = temp.path(); request.symbol = QStringLiteral("value"); request.newName = QStringLiteral("renamed"); request.provider = QStringLiteral("fixture");
	const QJsonArray first {edit(1, 0, 5, request.newName), edit(0, 4, 9, request.newName)}, second {edit(0, 0, 5, request.newName)};
	request.rename.workspaceEdit = workspace({target(path, first), target(other, second)});
	const auto preview = prepareLanguageRename(request); const auto fingerprint = languageRenamePlanHash(preview);
	ok &= expect(preview.canApply() && preview.changes.size() == 2 && preview.replacementCount == 3 && fingerprint.size() == 32 && read(path) == original, "multi-file rename previews exact edits without writing");
	write(other, "changed\n"); auto failed = applyProjectTextReplacements(preview);
	ok &= expect(!failed.succeeded() && failed.writtenFiles.isEmpty() && read(path) == original, "one stale target preflights the whole batch before any write");
	write(other, "value++;\r\n"); auto applied = applyProjectTextReplacements(preview);
	ok &= expect(applied.succeeded() && applied.writtenFiles.size() == 2 && applied.replacementsApplied == 3 && read(path) == QByteArray("int renamed;\r\nrenamed++;\n"), "rename uses exact-range encoding and preserves mixed source separators");
	write(path, original); write(other, "value++;\r\n");
	request.buffers << AssetTextBuffer {path, QStringLiteral("tab"), 7, QStringLiteral("int value;\nvalue++;\n// unsaved\n"), QStringLiteral("UTF-8"), {}};
	request.versions.insert(path, 9); request.rename.workspaceEdit = workspace({target(path, first, 9), target(other, second)});
	const auto live = prepareLanguageRename(request);
	ok &= expect(live.canApply() && live.buffersScanned == 1 && live.matches.back().textSha256.size() == 32 && !applyProjectTextReplacements(live).succeeded(), "open snapshots stay editor-owned and cannot be saved by a CLI writer");
	const auto deferred = applyProjectTextReplacements(live, {}, {}, true);
	ok &= expect(deferred.bufferEditsPending && deferred.writtenFiles.size() == 1 && read(path) == original, "mixed batch defers open edits to editor host"); write(other, "value++;\r\n");
	const auto reject = [&](const QJsonValue& changes, const char* why) { auto copy = request; copy.rename.workspaceEdit = changes; const auto result = prepareLanguageRename(copy); return expect(!result.succeeded() && !result.canApply() && result.changes.isEmpty() && result.matches.isEmpty(), why); };
	ok &= reject(workspace({target(path, first, 8), target(other, second)}), "stale wire version rejects all edits");
	ok &= reject(workspace({target(other, second, 2)}), "unknown disk wire version is rejected");
	ok &= reject(workspace({target(path, first, 9), target(path, first, 9)}), "duplicate targets cannot overwrite each other's edits");
	ok &= reject(workspace({target(path, first), QJsonObject {{QStringLiteral("kind"), QStringLiteral("delete")}, {QStringLiteral("uri"), languageServerUri(other)}}}), "resource operations cannot partially apply text edits");
	ok &= reject(workspace({target(path, QJsonArray {edit(0, 4, 9, QStringLiteral("a")), edit(0, 5, 9, QStringLiteral("b"))})}), "overlapping text edits reject the complete plan");
	ok &= reject(workspace({target(temp.filePath(QStringLiteral("../outside.cpp")), second)}), "outside-project targets are rejected");
	ok &= reject(workspace({target(path, first, 1.5)}), "fractional document versions are rejected");
	ok &= reject(QJsonArray {}, "malformed workspace edit rejected");
	auto unsynced = request; unsynced.versions.clear(); unsynced.rename.workspaceEdit = workspace({target(path, first)});
	ok &= expect(!prepareLanguageRename(unsynced).succeeded(), "unversioned edits cannot replace an unsynchronized open buffer");
	auto unavailable = request; unavailable.buffers[0].error = QStringLiteral("read only");
	ok &= expect(!prepareLanguageRename(unavailable).succeeded(), "unavailable live sources never fall back to disk");
	auto cancelled = request; cancelled.isCancelled = [] { return true; };
	ok &= expect(prepareLanguageRename(cancelled).cancelled && prepareLanguageRename(cancelled).changes.isEmpty(), "cancellation produces no applicable partial preview");
	auto mapRequest = request; mapRequest.buffers.clear(); mapRequest.versions.clear();
	mapRequest.rename.workspaceEdit = QJsonObject {{QStringLiteral("changes"), QJsonObject {{languageServerUri(path), first}, {languageServerUri(other), second}}}};
	ok &= expect(languageRenamePlanHash(prepareLanguageRename(mapRequest)) == fingerprint, "changes and documentChanges produce the same deterministic reviewed plan");
	auto preferred = workspace({target(path, first, 9)}); preferred.insert(QStringLiteral("changes"), QJsonObject {{QStringLiteral("file:///outside.cpp"), second}});
	auto precedence = request; precedence.rename.workspaceEdit = preferred;
	ok &= expect(prepareLanguageRename(precedence).changes.size() == 1, "documentChanges takes protocol precedence");
	auto noOp = request; noOp.rename.workspaceEdit = QJsonValue(QJsonValue::Null);
	ok &= expect(prepareLanguageRename(noOp).succeeded() && !prepareLanguageRename(noOp).canApply(), "null rename is a successful unchanged result");
	LanguageServerClient client; client.start({app.applicationFilePath(), {QStringLiteral("--fixture"), QStringLiteral("rename-slow")}, temp.path(), 2000});
	ok &= expect(wait([&]() { return client.ready(); }) && client.supportsRename() && client.supportsPrepareRename(), "rename capability negotiation");
	client.synchronize({{path, QStringLiteral("cpp"), QStringLiteral("int value;\n")}}); bool received = false;
	client.prepareRename(path, 0, 5, [&](const auto& result) { received = result.error.isEmpty() && result.placeholder == QStringLiteral("value") && result.version == client.documentVersion(path); });
	ok &= expect(wait([&]() { return received; }), "prepare request carries current document and UTF-16 position");
	received = false; const int id = client.rename(path, 0, 5, QStringLiteral("renamed"), [&](const auto&) { received = true; }); client.cancelRequest(id);
	wait([&]() { return false; }, 450); ok &= expect(!received, "cancel discards late rename responses");
	client.rename(path, 0, 5, QStringLiteral("renamed"), [&](const auto&) { received = true; }); client.synchronize({{path, QStringLiteral("cpp"), QStringLiteral("int edited;\n")}});
	wait([&]() { return false; }, 450); ok &= expect(!received, "source synchronization retires stale rename requests"); client.stop(); wait([&]() { return client.state() == QStringLiteral("stopped"); });
	if (app.arguments().size() > 1) {
		const auto run = [&](const QString& mode, const QStringList& extra, int expected) {
			QProcess process; process.setWorkingDirectory(temp.path());
			process.start(app.arguments()[1], QStringList {QStringLiteral("--cli"), QStringLiteral("code"), QStringLiteral("language-server"), path, QStringLiteral("--server"), app.applicationFilePath(), QStringLiteral("--server-args"),
				QString::fromUtf8(QJsonDocument(QJsonArray {QStringLiteral("--fixture"), mode}).toJson(QJsonDocument::Compact)), QStringLiteral("--root"), temp.path(), QStringLiteral("--line"), QStringLiteral("1"), QStringLiteral("--column"), QStringLiteral("5"), QStringLiteral("--rename"), QStringLiteral("renamed"), QStringLiteral("--timeout-ms"), QStringLiteral("1500"), QStringLiteral("--json")} + extra);
			ok &= expect(process.waitForFinished(15000) && process.exitCode() == expected, qPrintable(QStringLiteral("CLI mode %1 expected exit %2, got %3: %4").arg(mode).arg(expected).arg(process.exitCode()).arg(QString::fromUtf8(process.readAllStandardError()))));
			const auto json = QJsonDocument::fromJson(process.readAllStandardOutput()); ok &= expect(json.isObject(), "rename CLI JSON"); return json.object().value(QStringLiteral("languageServer")).toObject();
		};
		const auto cli = run(QStringLiteral("quiet"), {}, 0).value(QStringLiteral("rename")).toObject();
		const QString hash = cli.value(QStringLiteral("planSha256")).toString();
		ok &= expect(hash.size() == 64 && cli.value(QStringLiteral("changes")).toArray().size() == 2 && read(path) == original, "CLI preview exposes full edit plan without diagnostics or writes");
		const QStringList apply {QStringLiteral("--write"), QStringLiteral("--expected-plan-sha256"), hash};
		run(QStringLiteral("quiet"), {QStringLiteral("--write")}, 2); run(QStringLiteral("quiet"), apply + QStringList {QStringLiteral("--dry-run")}, 2);
		run(QStringLiteral("quiet"), {QStringLiteral("--write"), QStringLiteral("--expected-plan-sha256"), QString(64, QLatin1Char('0'))}, 4);
		ok &= expect(read(path) == original && read(other) == QByteArray("value++;\r\n"), "unreviewed CLI writes leave all files untouched");
		run(QStringLiteral("rename-resource"), apply, 4); run(QStringLiteral("no-rename"), {}, 4); run(QStringLiteral("rename-hang"), {}, 4);
		const auto written = run(QStringLiteral("quiet"), apply, 0).value(QStringLiteral("rename")).toObject();
		ok &= expect(written.value(QStringLiteral("writtenFiles")).toArray().size() == 2 && read(path) == QByteArray("int renamed;\r\nrenamed++;\n"), "reviewed CLI plan writes every target and preserves separators");
		write(path, original); write(other, "value++;\r\n"); run(QStringLiteral("rename-no-prepare"), {}, 0);
	}
	return ok ? 0 : 1;
}
