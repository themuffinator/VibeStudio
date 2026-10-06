#include "core/language_server.h"
#include "tests/language_server_fixture.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QEventLoop>
#include <filesystem>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* text) { if (!condition) { std::cerr << text << '\n'; } return condition; }
bool wait(const std::function<bool()>& ready, int ms = 6000)
{
	QElapsedTimer elapsed; elapsed.start();
	while (!ready() && elapsed.elapsed() < ms) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(2); }
	return ready();
}
QJsonObject cli(const QString& binary, const QStringList& arguments, int exitCode, bool& ok)
{
	QProcess process;
	process.start(binary, QStringList {QStringLiteral("--cli"), QStringLiteral("code"), QStringLiteral("language-server")} + arguments + QStringList {QStringLiteral("--json")});
	if (!process.waitForFinished(15000)) { process.kill(); process.waitForFinished(1000); }
	ok &= expect(process.exitStatus() == QProcess::NormalExit && process.exitCode() == exitCode, "CLI exit status reflects language service results");
	const auto document = QJsonDocument::fromJson(process.readAllStandardOutput());
	ok &= expect(document.isObject(), "CLI returns parseable JSON");
	return document.object().value(QStringLiteral("languageServer")).toObject();
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	if (app.arguments().value(1) == QStringLiteral("--fixture")) { return runLanguageServerFixture(app.arguments().value(2)); }
	bool ok = true;
	LanguageServerFramer framer; QVector<QJsonObject> messages; QString error;
	const QJsonObject original {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), QStringLiteral("café 雪")}};
	const auto encoded = LanguageServerFramer::encode(original);
	for (char byte : encoded) { ok &= expect(framer.append(QByteArray(1, byte), &messages, &error), "fragmented framing must decode UTF-8"); }
	ok &= expect(messages.size() == 1 && messages.first() == original, "byte lengths must not count Unicode characters");
	messages.clear(); ok &= expect(framer.append(encoded + encoded, &messages, &error) && messages.size() == 2, "coalesced messages must decode");
	for (const QByteArray& invalid : {QByteArray("Content-Length: -1\r\n\r\n"), QByteArray("Content-Length: 999999999\r\n\r\n"),
		QByteArray("Content-Length: 2\r\nContent-Length: 2\r\n\r\n{}"), QByteArray("Content-Length: 2\r\n\r\n{}"), QByteArray(8193, 'a')}) {
		framer.clear(); messages.clear(); ok &= expect(!framer.append(invalid, &messages, &error) && !error.isEmpty(), "malformed and oversized frames must fail");
	}
	QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	const QString path = temp.filePath(QStringLiteral("café file.cpp"));
	const QString originalText = QStringLiteral("bad 🌍\n  definition\n");
	QFile source(path); if (!source.open(QIODevice::WriteOnly)) { return 1; } source.write(originalText.toUtf8()); source.close();
	const QString linked = temp.filePath(QStringLiteral("linked.cpp"));
	const auto fsPath = [](const QString& value) {
#ifdef Q_OS_WIN
		return std::filesystem::path(value.toStdWString());
#else
		return std::filesystem::path(value.toUtf8().constData());
#endif
	};
	std::error_code linkError;
	std::filesystem::create_symlink(fsPath(path), fsPath(linked), linkError);
	if (linkError) { std::cerr << "Optional symlink fixture unavailable: " << linkError.message() << '\n'; }
	ok &= expect(languageServerPath(languageServerUri(path)) == path && languageServerPath(QStringLiteral("https://example.com/file.cpp")).isEmpty(), "only local file URIs may name source targets");
	for (const QString& mode : {QStringLiteral("full"), QStringLiteral("incremental"), QStringLiteral("unversioned"), QStringLiteral("oversized-location")}) {
		LanguageServerClient client;
		ok &= expect(client.start({app.applicationFilePath(), {QStringLiteral("--fixture"), mode}, temp.path(), 2000}) && wait([&]() { return client.ready() || !client.error().isEmpty(); }) && client.ready(), "initialize the fixture server");
		ok &= expect(client.synchronize({{path, QStringLiteral("cpp"), originalText}}).isEmpty(), "open a live Unicode document");
		ok &= expect(wait([&]() { return client.diagnostics(path).received; }) && client.diagnostics(path).items.size() == 1, "publish source diagnostics");
		ok &= expect(client.diagnostics(path).versioned == (mode != QStringLiteral("unversioned")), "unversioned reports must retain their uncertainty");
		bool resolved = false;
		client.definition(path, 0, 1, [&](const auto& locations, const QString& error) {
			resolved = error.isEmpty() && (mode == QStringLiteral("oversized-location") ? locations.isEmpty() : locations.size() == 1 && locations.first().line == 1 && locations.first().character == 2);
		});
		ok &= expect(wait([&]() { return resolved; }), "resolve LocationLink definitions");
		bool completed = false;
		ok &= expect(client.supportsCompletion() && client.completionTriggers().contains(QStringLiteral(".")), "negotiate completion capabilities and trigger characters");
		client.completion(path, 1, 4, 1, {}, [&](const LanguageCompletions& result) {
			QString preview, failure; int caret = 0;
			completed = result.error.isEmpty() && result.version == client.documentVersion(path) && result.items.size() == 1
				&& previewLanguageCompletion(originalText, result.items[0], &preview, &caret, &failure)
				&& preview == QStringLiteral("// imported\nbad 🌍\n  targetMember\n");
		});
		ok &= expect(wait([&]() { return completed; }), "completion and related edits use the current synchronized Unicode document");
		bool referenced = false;
		ok &= expect(client.supportsReferences(), "negotiate semantic references");
		client.references(path, 1, 4, true, [&](const LanguageReferences& result) {
			referenced = result.error.isEmpty() && result.version == client.documentVersion(path) && result.items.size() == 1 && result.items.first().character == 2;
		});
		ok &= expect(wait([&]() { return referenced; }), "reference replies preserve versions and deduplicate locations");
		bool inspected = false;
		ok &= expect(client.supportsHover(), "negotiate Quick Info capability");
		client.hover(path, 1, 4, [&](const LanguageHover& result) {
			inspected = result.error.isEmpty() && result.version == client.documentVersion(path) && result.contents.size() == 2
				&& result.hasRange && result.contents.first().text == QStringLiteral("int definition;") && result.sourceSha256.size() == 32;
		});
		ok &= expect(wait([&]() { return inspected; }), "hover content retains synchronized source provenance and validated ranges");
		const int version = client.documentVersion(path);
		ok &= expect(client.synchronize({{path, QStringLiteral("cpp"), QStringLiteral("good\n  definition\n")}}).isEmpty()
			&& client.documentVersion(path) > version && !client.diagnostics(path).received, "changes retire old diagnostics immediately");
		ok &= expect(wait([&]() { return client.diagnostics(path).received; }) && client.diagnostics(path).items.isEmpty(), "stale-version diagnostics must be discarded before the fresh empty report");
		ok &= expect(wait([&]() { return client.logLines().join(QLatin1Char('\n')).contains(QStringLiteral("correctly refused")); }), "server-initiated edits must be rejected");
		ok &= expect(!client.synchronize({{QDir(temp.path()).absoluteFilePath(QStringLiteral("../outside.cpp")), QStringLiteral("cpp"), originalText}}).isEmpty(), "synchronization cannot share outside-project documents");
		if (!linkError) { ok &= expect(!client.synchronize({{linked, QStringLiteral("cpp"), originalText}}).isEmpty(), "linked project sources cannot bypass synchronization scope"); }
		ok &= expect(client.documentVersion(path) > 0 && client.synchronizedDocuments() == 1, "rejected synchronization retains the previous complete snapshot");
		ok &= expect(client.synchronize({}).isEmpty() && client.documentVersion(path) == 0, "closed documents retire all versions and diagnostics");
		client.stop(); ok &= expect(wait([&]() { return client.state() == QStringLiteral("stopped"); }), "graceful shutdown must finish");
	}
	for (const QString& mode : {QStringLiteral("hang"), QStringLiteral("utf8"), QStringLiteral("malformed"), QStringLiteral("crash")}) {
		LanguageServerClient client; client.start({app.applicationFilePath(), {QStringLiteral("--fixture"), mode}, temp.path(), 300});
		ok &= expect(wait([&]() { return client.state() == QStringLiteral("failed"); }) && !client.error().isEmpty(), "startup errors, incompatible positions and protocol errors must fail explicitly");
	}
	LanguageServerClient slow; slow.start({app.applicationFilePath(), {QStringLiteral("--fixture"), QStringLiteral("slow")}, temp.path(), 2000});
	ok &= expect(wait([&]() { return slow.ready(); }), "start the cancellation fixture");
	slow.synchronize({{path, QStringLiteral("cpp"), originalText}});
	bool delivered = false;
	const int pending = slow.definition(path, 0, 0, [&](const auto&, const auto&) { delivered = true; });
	slow.cancelRequest(pending);
	wait([&]() { return delivered; }, 500);
	ok &= expect(!delivered && slow.pendingRequests() == 0, "cancelled definition replies cannot publish later");
	const int completion = slow.completion(path, 0, 1, 1, {}, [&](const auto&) { delivered = true; });
	ok &= expect(completion >= 0, "start a delayed completion request");
	slow.synchronize({{path, QStringLiteral("cpp"), QStringLiteral("changed\n")}});
	wait([&]() { return delivered; }, 500);
	ok &= expect(!delivered && slow.pendingRequests() == 0, "document changes cancel completion and discard late replies");
	const int referenceRequest = slow.references(path, 0, 1, true, [&](const auto&) { delivered = true; });
	slow.cancelRequest(referenceRequest); wait([&]() { return delivered; }, 500);
	ok &= expect(referenceRequest >= 0 && !delivered && slow.pendingRequests() == 0, "cancelled reference replies cannot publish later");
	const int hoverRequest = slow.hover(path, 0, 1, [&](const auto&) { delivered = true; });
	slow.synchronize({{path, QStringLiteral("cpp"), originalText}}); wait([&]() { return delivered; }, 500);
	ok &= expect(hoverRequest >= 0 && !delivered && slow.pendingRequests() == 0, "source changes retire pending hover replies");
	slow.stop(); wait([&]() { return slow.state() == QStringLiteral("stopped"); });
	if (app.arguments().size() > 1) {
		const QString binary = app.arguments()[1];
		const auto argumentsFor = [&](const QString& mode) {
			return QStringList {path, QStringLiteral("--server"), app.applicationFilePath(), QStringLiteral("--server-args"),
				QString::fromUtf8(QJsonDocument(QJsonArray {QStringLiteral("--fixture"), mode}).toJson(QJsonDocument::Compact)),
				QStringLiteral("--timeout-ms"), QStringLiteral("1500")};
		};
		const auto errors = cli(binary, argumentsFor(QStringLiteral("incremental")) + QStringList {QStringLiteral("--line"), QStringLiteral("1"), QStringLiteral("--column"), QStringLiteral("2")}, 4, ok);
		ok &= expect(errors.value(QStringLiteral("diagnosticsReceived")).toBool() && errors.value(QStringLiteral("versioned")).toBool()
			&& errors.value(QStringLiteral("diagnostics")).toArray().size() == 1 && errors.value(QStringLiteral("definitions")).toArray().size() == 1, "CLI combines diagnostic errors and requested definitions with version provenance");
		const auto targets = errors.value(QStringLiteral("definitions")).toArray();
		const auto target = targets.isEmpty() ? QJsonObject() : targets.first().toObject();
		ok &= expect(target.value(QStringLiteral("line")).toInt() == 2 && target.value(QStringLiteral("column")).toInt() == 3, "CLI converts LSP locations to one-based UTF-16 positions");
		if (!source.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return 1; } source.write("good\n  definition\n"); source.close();
		const auto clean = cli(binary, argumentsFor(QStringLiteral("incremental")), 0, ok);
		ok &= expect(clean.value(QStringLiteral("diagnosticsReceived")).toBool() && clean.value(QStringLiteral("diagnostics")).toArray().isEmpty(), "CLI reports explicit empty diagnostics as a completed check");
		const QStringList completionAt {QStringLiteral("--completion"), QStringLiteral("--line"), QStringLiteral("2"), QStringLiteral("--column"), QStringLiteral("5")};
		const auto completionResult = cli(binary, argumentsFor(QStringLiteral("full")) + completionAt, 0, ok);
		const auto completionList = completionResult.value(QStringLiteral("completions")).toObject().value(QStringLiteral("items")).toArray();
		ok &= expect(completionResult.value(QStringLiteral("completionReceived")).toBool() && completionList.size() == 1
			&& completionList.first().toObject().value(QStringLiteral("edits")).toArray().size() == 2, "CLI returns validated edit proposals without applying them");
		ok &= expect(source.open(QIODevice::ReadOnly) && source.readAll() == QByteArray("good\n  definition\n"), "CLI completion leaves source bytes unchanged"); source.close();
		const auto incomplete = cli(binary, argumentsFor(QStringLiteral("completion-incomplete")) + completionAt, 4, ok);
		ok &= expect(incomplete.value(QStringLiteral("completions")).toObject().value(QStringLiteral("incomplete")).toBool(), "CLI partial completion results retain non-success provenance");
		const QStringList referenceAt {QStringLiteral("--references"), QStringLiteral("--line"), QStringLiteral("2"), QStringLiteral("--column"), QStringLiteral("5")};
		const auto references = cli(binary, argumentsFor(QStringLiteral("full")) + referenceAt, 0, ok).value(QStringLiteral("references")).toObject();
		const auto referenceItems = references.value(QStringLiteral("items")).toArray();
		ok &= expect(references.value(QStringLiteral("complete")).toBool() && referenceItems.size() == 1
			&& referenceItems.first().toObject().value(QStringLiteral("line")).toInt() == 2
			&& referenceItems.first().toObject().value(QStringLiteral("textSha256")).toString().size() == 64, "CLI references expose validated one-based ranges and source hashes");
		const auto withoutDeclaration = cli(binary, argumentsFor(QStringLiteral("full")) + referenceAt + QStringList {QStringLiteral("--exclude-declaration")}, 0, ok).value(QStringLiteral("references")).toObject();
		ok &= expect(!withoutDeclaration.value(QStringLiteral("includeDeclaration")).toBool() && withoutDeclaration.value(QStringLiteral("items")).toArray().isEmpty(), "CLI declaration exclusion reaches the language server");
		const auto invalidReferences = cli(binary, argumentsFor(QStringLiteral("references-bad")) + referenceAt, 4, ok).value(QStringLiteral("references")).toObject();
		ok &= expect(!invalidReferences.value(QStringLiteral("complete")).toBool(true) && invalidReferences.value(QStringLiteral("skipped")).toInt() == 2, "malformed reference locations are explicit partial CLI results");
		cli(binary, argumentsFor(QStringLiteral("references-malformed")) + referenceAt, 4, ok);
		cli(binary, argumentsFor(QStringLiteral("references-many")) + referenceAt, 4, ok);
		cli(binary, argumentsFor(QStringLiteral("no-references")) + referenceAt, 4, ok);
		const auto referenceTimeout = cli(binary, argumentsFor(QStringLiteral("references-hang")) + referenceAt, 4, ok);
		ok &= expect(!referenceTimeout.value(QStringLiteral("referencesReceived")).toBool() && !referenceTimeout.value(QStringLiteral("error")).toString().isEmpty(), "a missing reference reply times out even after diagnostics arrive");
		const QStringList hoverAt {QStringLiteral("--hover"), QStringLiteral("--line"), QStringLiteral("2"), QStringLiteral("--column"), QStringLiteral("5")};
		const auto hover = cli(binary, argumentsFor(QStringLiteral("full")) + hoverAt, 0, ok);
		const auto hoverInfo = hover.value(QStringLiteral("hover")).toObject();
		ok &= expect(hover.value(QStringLiteral("hoverReceived")).toBool() && hoverInfo.value(QStringLiteral("contents")).toArray().size() == 2
			&& hoverInfo.value(QStringLiteral("sourceSha256")).toString().size() == 64 && hoverInfo.value(QStringLiteral("range")).isObject(), "CLI Quick Info exposes content and checked snapshot ranges");
		const auto emptyHover = cli(binary, argumentsFor(QStringLiteral("hover-empty")) + hoverAt, 0, ok);
		ok &= expect(emptyHover.value(QStringLiteral("hoverReceived")).toBool() && emptyHover.value(QStringLiteral("hover")).toObject().value(QStringLiteral("contents")).toArray().isEmpty(), "empty hover is a completed no-information reply");
		cli(binary, argumentsFor(QStringLiteral("hover-large")) + hoverAt, 4, ok);
		cli(binary, argumentsFor(QStringLiteral("hover-bad-range")) + hoverAt, 4, ok);
		cli(binary, argumentsFor(QStringLiteral("hover-malformed")) + hoverAt, 4, ok);
		cli(binary, argumentsFor(QStringLiteral("no-hover")) + hoverAt, 4, ok);
		cli(binary, argumentsFor(QStringLiteral("hover-hang")) + hoverAt, 4, ok);
		ok &= expect(source.open(QIODevice::ReadOnly) && source.readAll() == QByteArray("good\n  definition\n"), "CLI hover never changes saved source bytes"); source.close();
		const auto silent = cli(binary, argumentsFor(QStringLiteral("quiet")), 4, ok);
		ok &= expect(!silent.value(QStringLiteral("diagnosticsReceived")).toBool() && !silent.value(QStringLiteral("error")).toString().isEmpty(), "a silent server cannot appear to pass validation");
		const auto unversioned = cli(binary, argumentsFor(QStringLiteral("unversioned")), 0, ok);
		ok &= expect(unversioned.value(QStringLiteral("diagnosticsReceived")).toBool() && !unversioned.value(QStringLiteral("versioned")).toBool(), "CLI retains unversioned report provenance");
		cli(binary, {path, QStringLiteral("--server"), app.applicationFilePath(), QStringLiteral("--server-args"), QStringLiteral("[3]")}, 2, ok);
		cli(binary, argumentsFor(QStringLiteral("full")) + QStringList {QStringLiteral("--column"), QStringLiteral("1")}, 2, ok);
		cli(binary, argumentsFor(QStringLiteral("full")) + QStringList {QStringLiteral("--line"), QStringLiteral("999")}, 2, ok);
		cli(binary, argumentsFor(QStringLiteral("full")) + QStringList {QStringLiteral("--completion")}, 2, ok);
		cli(binary, argumentsFor(QStringLiteral("full")) + QStringList {QStringLiteral("--references")}, 2, ok);
		cli(binary, argumentsFor(QStringLiteral("full")) + referenceAt + QStringList {QStringLiteral("--completion")}, 2, ok);
		cli(binary, argumentsFor(QStringLiteral("full")) + QStringList {QStringLiteral("--exclude-declaration")}, 2, ok);
		cli(binary, argumentsFor(QStringLiteral("full")) + QStringList {QStringLiteral("--hover")}, 2, ok);
		cli(binary, argumentsFor(QStringLiteral("full")) + hoverAt + QStringList {QStringLiteral("--completion")}, 2, ok);
		cli(binary, argumentsFor(QStringLiteral("full")) + hoverAt + QStringList {QStringLiteral("--references")}, 2, ok);
	}
	return ok ? 0 : 1;
}
