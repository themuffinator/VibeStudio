#include "tests/diagnostic_server_fixture.h"
#include <QElapsedTimer>
#include <QEventLoop>
#include <QProcess>
#include <QTemporaryDir>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* why) { if (!value) { std::cerr << why << '\n'; } return value; }
bool wait(const std::function<bool()>& ready, int ms = 7000) { QElapsedTimer elapsed; elapsed.start(); while (!ready() && elapsed.elapsed() < ms) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(2); } return ready(); }
bool write(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); if (app.arguments().value(1) == QStringLiteral("--fixture")) { return runDiagnosticServerFixture(app.arguments().value(2)); }
	QTemporaryDir temp; if (!temp.isValid()) { return 1; } bool ok = true; const QString path = temp.filePath(QStringLiteral("source.cpp"));
	const QString text = QStringLiteral("bad value\n"); const QByteArray original = "bad value\r\n"; if (!write(path, original)) { return 1; }
	const auto range = [](int first, int last) { return QJsonObject {{QStringLiteral("start"), QJsonObject {{QStringLiteral("line"), 0}, {QStringLiteral("character"), first}}}, {QStringLiteral("end"), QJsonObject {{QStringLiteral("line"), 0}, {QStringLiteral("character"), last}}}}; };
	const QJsonObject diagnostic {{QStringLiteral("message"), QStringLiteral("Unicode diagnostic")}, {QStringLiteral("range"), range(0, 2)}};
	const LanguageDocument unicode {path, QStringLiteral("cpp"), QStringLiteral("🌍 text\n")};
	auto report = parseLanguageDiagnostics(QJsonArray {diagnostic}, unicode, 2, true, QStringLiteral("pull"));
	ok &= expect(report.received && report.items.size() == 1 && !report.limited && !report.items[0].wire.isEmpty(), "parse valid UTF-16 diagnostic locations and retain provider data");
	auto bad = diagnostic; bad.insert(QStringLiteral("range"), range(1, 2));
	report = parseLanguageDiagnostics(QJsonArray {diagnostic, bad}, unicode, 2, true, QStringLiteral("pull"));
	ok &= expect(report.items.size() == 1 && report.skipped == 1 && report.limited, "reject a range that splits a Unicode character with explicit partial status");
	ok &= expect(!parseLanguageDiagnostics(QJsonObject {}, unicode, 2, true, QStringLiteral("pull")).error.isEmpty(), "a malformed list cannot be reported as clean diagnostics");
	QJsonArray excessive; for (int i = 0; i < 2001; ++i) { excessive << diagnostic; }
	report = parseLanguageDiagnostics(excessive, unicode, 2, true, QStringLiteral("pull"));
	ok &= expect(report.limited && report.items.size() == 2000, "diagnostic list retention is bounded");
	for (const auto& mode : {QStringLiteral("pull"), QStringLiteral("pull-unchanged"), QStringLiteral("pull-refresh"), QStringLiteral("pull-retry")}) {
		LanguageServerClient client; client.start({app.applicationFilePath(), {QStringLiteral("--fixture"), mode}, temp.path(), 2000});
		if (!expect(wait([&]() { return client.ready(); }), "start a pull-only server")) { return 1; }
		ok &= expect(client.supportsPullDiagnostics() && client.supportsSaveNotifications() && client.synchronize({{path, QStringLiteral("cpp"), text}}).isEmpty(), "negotiate pull diagnostics and synchronize unsaved text");
		ok &= expect(client.diagnostics(path).pending && wait([&]() { return client.diagnostics(path).received; }), "pull diagnostics expose a queued state before completing");
		report = client.diagnostics(path); const int version = client.documentVersion(path);
		ok &= expect(report.items.size() == 1 && report.versioned && report.version == version && report.origin == QStringLiteral("pull") && report.error.isEmpty(), "pull-only providers produce versioned diagnostics");
		if (mode == QStringLiteral("pull-refresh")) { ok &= expect(wait([&]() { return client.logLines().join(QLatin1Char('\n')).contains(QStringLiteral("refresh acknowledged")) && client.diagnostics(path).received && client.pendingRequests() == 0; }), "server refresh is acknowledged and checks shared documents again"); }
		client.refreshDiagnostics(path); ok &= expect(wait([&]() { return client.diagnostics(path).received; }) && client.diagnostics(path).items.size() == 1
			&& client.logLines().join(QLatin1Char('\n')).contains(QStringLiteral("previous=1")), "unchanged results reuse a validated prior cache");
		if (mode == QStringLiteral("pull-unchanged")) {
			client.synchronize({{path, QStringLiteral("cpp"), QStringLiteral("bad other\n")}});
			ok &= expect(wait([&]() { return client.diagnostics(path).received; }) && client.diagnostics(path).version > version && client.diagnostics(path).items.size() == 1, "unchanged diagnostics are revalidated against the new document version");
		} else {
			client.synchronize({{path, QStringLiteral("cpp"), QStringLiteral("good value\n")}});
			ok &= expect(wait([&]() { return client.diagnostics(path).received; }) && client.diagnostics(path).items.isEmpty(), "fresh empty reports clear prior diagnostics after an edit");
		}
		client.synchronize({}); ok &= expect(client.documentVersion(path) == 0 && !client.diagnostics(path).received, "closing retires diagnostic versions and cached results");
		client.stop(); wait([&]() { return client.state() == QStringLiteral("stopped"); });
	}
	for (const auto& mode : {QStringLiteral("pull-invalid"), QStringLiteral("pull-malformed"), QStringLiteral("pull-error"), QStringLiteral("pull-unknown-cache"), QStringLiteral("pull-hang"), QStringLiteral("pull-no-retry"), QStringLiteral("pull-retry-forever")}) {
		LanguageServerClient client; client.start({app.applicationFilePath(), {QStringLiteral("--fixture"), mode}, temp.path(), 600});
		if (!expect(wait([&]() { return client.ready(); }), "start diagnostic failure fixture")) { return 1; }
		client.synchronize({{path, QStringLiteral("cpp"), text}}); ok &= expect(wait([&]() { return client.diagnostics(path).received; }), "diagnostic failure must reach a terminal report"); report = client.diagnostics(path);
		ok &= expect(mode == QStringLiteral("pull-invalid") ? report.limited && report.skipped == 1 && report.items.size() == 1 : !report.error.isEmpty(), "failures and invalid entries cannot masquerade as a clean check");
		if (mode == QStringLiteral("pull-no-retry")) { ok &= expect(!client.logLines().join(QLatin1Char('\n')).contains(QStringLiteral("count=2")), "honor server cancellation with retrigger disabled"); }
		if (mode == QStringLiteral("pull-retry-forever")) { ok &= expect(client.logLines().join(QLatin1Char('\n')).contains(QStringLiteral("count=3")) && !client.logLines().join(QLatin1Char('\n')).contains(QStringLiteral("count=4")), "server cancellation retries are bounded"); }
		client.stop(); wait([&]() { return client.state() == QStringLiteral("stopped"); });
	}
	{
		LanguageServerClient client; client.start({app.applicationFilePath(), {QStringLiteral("--fixture"), QStringLiteral("pull-slow")}, temp.path(), 2000}); if (!wait([&]() { return client.ready(); })) { return 1; }
		client.synchronize({{path, QStringLiteral("cpp"), text}}); wait([&]() { return client.pendingRequests() > 0; });
		client.synchronize({{path, QStringLiteral("cpp"), QStringLiteral("good\n")}}); ok &= expect(wait([&]() { return client.diagnostics(path).received; }) && client.diagnostics(path).items.isEmpty(), "late diagnostic replies cannot revive an old error after editing");
		client.stop(); wait([&]() { return client.state() == QStringLiteral("stopped"); });
	}
	{
		LanguageServerClient client; client.start({app.applicationFilePath(), {QStringLiteral("--fixture"), QStringLiteral("pull-interdependent")}, temp.path(), 2000}); if (!wait([&]() { return client.ready(); })) { return 1; }
		QVector<LanguageDocument> sources; for (int i = 0; i < 40; ++i) { sources << LanguageDocument {temp.filePath(QStringLiteral("buffer%1.cpp").arg(i)), QStringLiteral("cpp"), QStringLiteral("good\n")}; }
		client.synchronize(sources); ok &= expect(wait([&]() { return std::all_of(sources.cbegin(), sources.cend(), [&](const auto& source) { return client.diagnostics(source.filePath).received; }); }), "queued pulls service more documents than the request concurrency limit");
		sources[0].text = text; client.synchronize(sources);
		ok &= expect(wait([&]() { return client.diagnostics(sources[39].filePath).received; }) && !client.diagnostics(sources[39].filePath).items.isEmpty(), "declared inter-file dependencies refresh other shared documents");
		client.stop(); wait([&]() { return client.state() == QStringLiteral("stopped"); });
	}
	for (const auto& mode : {QStringLiteral("save-push"), QStringLiteral("save-no-text"), QStringLiteral("save-disabled")}) {
		LanguageServerClient client; client.start({app.applicationFilePath(), {QStringLiteral("--fixture"), mode}, temp.path(), 2000}); if (!wait([&]() { return client.ready(); })) { return 1; }
		client.synchronize({{path, QStringLiteral("cpp"), text}});
		ok &= expect(!client.documentSaved(path, QStringLiteral("stale")), "save notifications reject text that is not the synchronized snapshot");
		const bool supported = mode != QStringLiteral("save-disabled"); ok &= expect(client.documentSaved(path, text) == supported, "save interest controls notification emission");
		if (supported) { ok &= expect(wait([&]() { return client.diagnostics(path).received; }) && client.diagnostics(path).origin == QStringLiteral("push"), "save-only push providers receive correctly ordered content and publish results"); }
		client.stop(); wait([&]() { return client.state() == QStringLiteral("stopped"); });
	}
	if (app.arguments().size() > 1) {
		for (const auto& mode : {QStringLiteral("pull"), QStringLiteral("pull-malformed"), QStringLiteral("pull-invalid")}) {
			QProcess process; process.start(app.arguments()[1], {QStringLiteral("--cli"), QStringLiteral("code"), QStringLiteral("language-server"), path, QStringLiteral("--server"), app.applicationFilePath(), QStringLiteral("--server-args"),
				QString::fromUtf8(QJsonDocument(QJsonArray {QStringLiteral("--fixture"), mode}).toJson(QJsonDocument::Compact)), QStringLiteral("--root"), temp.path(), QStringLiteral("--timeout-ms"), QStringLiteral("3000"), QStringLiteral("--json")});
			ok &= expect(process.waitForFinished(12000) && process.exitCode() == 4, "CLI diagnostic failures preserve the validation exit contract"); const auto result = QJsonDocument::fromJson(process.readAllStandardOutput()).object().value(QStringLiteral("languageServer")).toObject();
			ok &= expect(result.value(QStringLiteral("diagnosticsReceived")).toBool() && result.value(QStringLiteral("diagnosticOrigin")).toString() == QStringLiteral("pull") && read(path) == original, "CLI consumes pull-only diagnostics without changing saved bytes");
		}
		ok &= expect(write(path, "good\r\n"), "save a clean CLI fixture"); QProcess process;
		process.start(app.arguments()[1], {QStringLiteral("--cli"), QStringLiteral("code"), QStringLiteral("language-server"), path, QStringLiteral("--server"), app.applicationFilePath(), QStringLiteral("--server-args"),
			QString::fromUtf8(QJsonDocument(QJsonArray {QStringLiteral("--fixture"), QStringLiteral("pull")}).toJson(QJsonDocument::Compact)), QStringLiteral("--root"), temp.path(), QStringLiteral("--json")});
		ok &= expect(process.waitForFinished(12000) && process.exitCode() == 0 && read(path) == QByteArray("good\r\n"), "an explicit empty pull report is a successful read-only CLI check");
	}
	return ok ? 0 : 1;
}
