#include "tests/language_server_fixture.h"
#include <QProcess>
#include <QTemporaryDir>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* why) { if (!condition) { std::cerr << why << '\n'; } return condition; }
bool wait(const std::function<bool()>& ready, int ms = 4000)
{
	QElapsedTimer clock; clock.start(); while (!ready() && clock.elapsed() < ms) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(2); } return ready();
}
QJsonObject pos(int line, int character) { return {{QStringLiteral("line"), line}, {QStringLiteral("character"), character}}; }
QJsonObject range(int line, int first, int last) { return {{QStringLiteral("start"), pos(line, first)}, {QStringLiteral("end"), pos(line, last)}}; }
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); if (app.arguments().value(1) == QStringLiteral("--fixture")) { return runLanguageServerFixture(app.arguments().value(2)); }
	bool ok = true; QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	const QString source = QStringLiteral("// 🌍\nobject.taSuffix\n"), path = temp.filePath(QStringLiteral("source.cpp"));
	{ QFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(source.toUtf8()) < 0) { return 1; } }
	QJsonObject base {{QStringLiteral("label"), QStringLiteral("target")}, {QStringLiteral("textEditText"), QStringLiteral("targetMember")}, {QStringLiteral("tags"), QJsonArray {1, 91}}};
	const QJsonObject defaults {{QStringLiteral("editRange"), range(1, 7, 15)}, {QStringLiteral("insertTextFormat"), 1}, {QStringLiteral("insertTextMode"), 1}, {QStringLiteral("data"), QJsonObject {{QStringLiteral("opaque"), 19}}}};
	auto list = parseLanguageCompletions(QJsonObject {{QStringLiteral("items"), QJsonArray {base}}, {QStringLiteral("itemDefaults"), defaults}}, source, 1, 9, true);
	ok &= expect(list.items.size() == 1 && list.items[0].needsResolve && list.items[0].wire.value(QStringLiteral("data")).toObject().value(QStringLiteral("opaque")).toInt() == 19
		&& list.items[0].wire.value(QStringLiteral("tags")).toArray().size() == 2 && list.items[0].wire.contains(QStringLiteral("textEdit")), "effective defaults and opaque protocol fields survive deferred completion");
	if (list.items.isEmpty()) { return 1; }
	const auto original = list.items[0]; QString error, preview; int caret = 0;
	ok &= expect(!previewLanguageCompletion(source, original, &preview, &caret, &error), "an unresolved suggestion cannot apply an incomplete edit set");
	auto wire = original.wire; wire.insert(QStringLiteral("detail"), QStringLiteral("resolved detail")); wire.insert(QStringLiteral("documentation"), QStringLiteral("resolved docs"));
	wire.insert(QStringLiteral("additionalTextEdits"), QJsonArray {QJsonObject {{QStringLiteral("range"), range(0, 0, 0)}, {QStringLiteral("newText"), QStringLiteral("// import\n")}}});
	const auto resolved = resolveLanguageCompletion(original, wire, source, &error);
	ok &= expect(error.isEmpty() && !resolved.needsResolve && resolved.detail == QStringLiteral("resolved detail") && resolved.edits.size() == 2
		&& previewLanguageCompletion(source, resolved, &preview, &caret, &error) && preview == QStringLiteral("// import\n// 🌍\nobject.targetMember\n") && caret == preview.size() - 1, "resolution combines metadata and import edits into one source-checked preview");
	const auto reject = [&](const QJsonValue& response, const QString& text = QString()) { QString why; resolveLanguageCompletion(original, response, text.isNull() ? source : text, &why); return !why.isEmpty(); };
	auto changed = wire; changed.insert(QStringLiteral("label"), QStringLiteral("other")); ok &= expect(reject(changed), "resolved identity cannot change");
	changed = wire; changed.insert(QStringLiteral("insertText"), QStringLiteral("changed")); ok &= expect(reject(changed), "resolve cannot introduce new primary insertion behavior");
	changed = wire; changed.insert(QStringLiteral("command"), QJsonObject {}); ok &= expect(reject(changed), "resolved commands are refused");
	changed = wire; changed.insert(QStringLiteral("additionalTextEdits"), QJsonArray {wire.value(QStringLiteral("textEdit"))}); ok &= expect(reject(changed), "overlapping related edits reject the whole suggestion");
	changed = wire; changed.insert(QStringLiteral("documentation"), QString(300000, QLatin1Char('x'))); ok &= expect(reject(changed), "oversized resolved payloads are rejected");
	ok &= expect(reject(QJsonValue(QJsonValue::Null)) && reject(wire, source + QLatin1Char('x')), "malformed resolve and changed source snapshots fail");
	LanguageServerClient client; client.start({app.applicationFilePath(), {QStringLiteral("--fixture"), QStringLiteral("resolve-completion-slow")}, temp.path(), 1200});
	ok &= expect(wait([&]() { return client.ready(); }) && client.supportsCompletionResolve(), "completion resolve capability negotiates");
	client.synchronize({{path, QStringLiteral("cpp"), source}}); bool received = false;
	client.completion(path, 1, 9, 1, {}, [&](const auto& result) { list = result; received = true; });
	ok &= expect(wait([&]() { return received; }) && list.items.size() == 1 && list.items[0].needsResolve, "client retains default data for deferred resolve");
	if (list.items.isEmpty()) { return 1; } received = false;
	client.resolveCompletion(path, list.version, list.items[0], [&](const auto& item, const QString& why) { error = why; received = true; ok &= expect(item.edits.size() == 2 && item.documentation.contains(QStringLiteral("Resolved")), "resolved provider edit and metadata"); });
	ok &= expect(wait([&]() { return received; }) && error.isEmpty(), "server receives effective opaque defaults and resolves correctly");
	received = false; const int id = client.resolveCompletion(path, list.version, list.items[0], [&](const auto&, const QString&) { received = true; }); client.cancelRequest(id); wait([] { return false; }, 450);
	ok &= expect(!received, "cancel retires a delayed completion resolve");
	client.resolveCompletion(path, list.version, list.items[0], [&](const auto&, const QString&) { received = true; }); client.synchronize({{path, QStringLiteral("cpp"), source + QLatin1Char('x')}}); wait([] { return false; }, 450);
	ok &= expect(!received && client.resolveCompletion(path, list.version, list.items[0], {}) < 0, "source changes retire resolve and reject stale requests");
	client.stop(); wait([&]() { return client.state() == QStringLiteral("stopped"); });
	if (app.arguments().size() > 1) {
		const auto run = [&](const QString& mode, const QStringList& extra, int expected) {
			QProcess process; process.setWorkingDirectory(temp.path());
			process.start(app.arguments()[1], QStringList {QStringLiteral("--cli"), QStringLiteral("code"), QStringLiteral("language-server"), path, QStringLiteral("--server"), app.applicationFilePath(),
				QStringLiteral("--server-args"), QString::fromUtf8(QJsonDocument(QJsonArray {QStringLiteral("--fixture"), mode}).toJson(QJsonDocument::Compact)), QStringLiteral("--root"), temp.path(),
				QStringLiteral("--completion"), QStringLiteral("--line"), QStringLiteral("2"), QStringLiteral("--column"), QStringLiteral("10"), QStringLiteral("--timeout-ms"), QStringLiteral("1000"), QStringLiteral("--json")} + extra);
			const bool done = process.waitForFinished(10000); const auto output = process.readAllStandardOutput();
			ok &= expect(done && process.exitCode() == expected, qPrintable(QStringLiteral("CLI %1 expected %2 got %3: %4").arg(mode).arg(expected).arg(process.exitCode()).arg(QString::fromUtf8(output))));
			return QJsonDocument::fromJson(output).object().value(QStringLiteral("languageServer")).toObject().value(QStringLiteral("completions")).toObject();
		};
		const auto listed = run(QStringLiteral("resolve-completion"), {}, 0);
		ok &= expect(listed.value(QStringLiteral("items")).toArray().first().toObject().value(QStringLiteral("needsResolve")).toBool(), "CLI listing marks deferred suggestions");
		const QStringList selected {QStringLiteral("--resolve-completion"), QStringLiteral("1")};
		const auto result = run(QStringLiteral("resolve-completion"), selected, 0); const auto item = result.value(QStringLiteral("items")).toArray().first().toObject();
		ok &= expect(result.value(QStringLiteral("resolveReceived")).toBool() && result.value(QStringLiteral("resolvedIndex")).toInt() == 1 && !item.value(QStringLiteral("needsResolve")).toBool(true)
			&& item.value(QStringLiteral("edits")).toArray().size() == 2 && item.value(QStringLiteral("documentation")).toString().contains(QStringLiteral("Resolved")), "CLI resolution exposes the complete edit set and metadata without writing");
		run(QStringLiteral("resolve-completion-identity"), selected, 4); run(QStringLiteral("resolve-completion-command"), selected, 4); run(QStringLiteral("resolve-completion-overlap"), selected, 4);
		run(QStringLiteral("resolve-completion-malformed"), selected, 4); run(QStringLiteral("resolve-completion-hang"), selected, 4); run(QStringLiteral("incremental"), selected, 4);
		run(QStringLiteral("resolve-completion"), {QStringLiteral("--resolve-completion"), QStringLiteral("2")}, 4);
		run(QStringLiteral("resolve-completion"), {QStringLiteral("--resolve-completion"), QStringLiteral("0")}, 2);
		QFile file(path); ok &= expect(file.open(QIODevice::ReadOnly) && file.readAll() == source.toUtf8(), "every CLI completion operation preserves source bytes");
	}
	return ok ? 0 : 1;
}
