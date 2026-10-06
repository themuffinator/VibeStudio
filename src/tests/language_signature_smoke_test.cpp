#include "tests/language_server_fixture.h"
#include "core/text_document.h"
#include <QProcess>
#include <QTemporaryDir>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* why) { if (!condition) { std::cerr << why << '\n'; } return condition; }
bool wait(const std::function<bool()>& ready, int ms = 5000)
{
	QElapsedTimer clock; clock.start(); while (!ready() && clock.elapsed() < ms) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(2); } return ready();
}
QJsonObject signature(const QString& label, QJsonArray parameters = {}) { return {{QStringLiteral("label"), label}, {QStringLiteral("parameters"), parameters}}; }
QJsonObject parameter(const QJsonValue& label) { return {{QStringLiteral("label"), label}}; }
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); if (app.arguments().value(1) == QStringLiteral("--fixture")) { return runLanguageServerFixture(app.arguments().value(2)); }
	bool ok = true; const QString source = QStringLiteral("sum(1, ");
	const auto first = signature(QStringLiteral("f(int, int)"), {parameter(QStringLiteral("int")), parameter(QStringLiteral("int"))});
	auto second = signature(QStringLiteral("g(🌍, right)"), {parameter(QJsonArray {2, 4}), parameter(QStringLiteral("right"))}); second.insert(QStringLiteral("activeParameter"), 1);
	QJsonObject wire {{QStringLiteral("signatures"), QJsonArray {first, second}}, {QStringLiteral("activeSignature"), 1}, {QStringLiteral("activeParameter"), 0}};
	auto report = parseLanguageSignatureHelp(wire, source, 0, source.size());
	ok &= expect(report.error.isEmpty() && report.signatures.size() == 2 && report.activeSignature == 1 && report.signatures[1].activeParameter == 1
		&& report.signatures[1].parameters[0].label == QStringLiteral("🌍") && report.signatures[0].parameters[1].offset == 7, "overloads, UTF-16 ranges, repeated labels and per-signature active parameters");
	const auto context = languageSignatureContext(report);
	ok &= expect(context.value(QStringLiteral("activeSignature")).toInt(-1) == 1 && context.value(QStringLiteral("activeParameter")).toInt(-1) == 1
		&& context.value(QStringLiteral("signatures")).toArray()[1].toObject().value(QStringLiteral("parameters")).toArray()[0].toObject().value(QStringLiteral("label")).toArray() == QJsonArray({2, 4}), "retrigger context preserves normalized ranges and selected overload");
	wire.insert(QStringLiteral("activeSignature"), 99); wire.insert(QStringLiteral("activeParameter"), 99);
	report = parseLanguageSignatureHelp(wire, source, 0, source.size());
	ok &= expect(report.activeSignature == 0 && report.signatures[0].activeParameter == 0, "out-of-range active indices use protocol defaults");
	for (const auto& label : QJsonArray {QJsonArray {3, 4}, QJsonArray {-1, 4}, QJsonArray {2, 99}, QJsonArray {4, 2}, QJsonArray {2.5, 4}, QJsonValue(QStringLiteral("missing"))}) {
		wire = {{QStringLiteral("signatures"), QJsonArray {signature(QStringLiteral("g(🌍)"), {parameter(label)})}}};
		report = parseLanguageSignatureHelp(wire, source, 0, source.size());
		ok &= expect(report.signatures.isEmpty() && report.skipped == 1, "invalid or split parameter ranges omit the complete signature");
	}
	ok &= expect(!parseLanguageSignatureHelp(QJsonArray {}, source, 0, 1).error.isEmpty()
		&& !parseLanguageSignatureHelp(QJsonValue(QJsonValue::Null), QStringLiteral("🌍"), 0, 1).error.isEmpty()
		&& parseLanguageSignatureHelp(QJsonValue(QJsonValue::Null), source, 0, 1).signatures.isEmpty(), "malformed help and source positions differ from an empty result");
	ok &= expect(!parseLanguageSignatureHelp(QJsonValue(QJsonValue::Null), QString(textDocumentByteLimit / 2, QChar(0x754c)), 0, 0).error.isEmpty(), "source bounds count UTF-8 bytes as well as UTF-16 code units");
	auto documented = first; documented.insert(QStringLiteral("documentation"), QString(8191, QLatin1Char('x')) + QStringLiteral("🌍"));
	report = parseLanguageSignatureHelp(QJsonObject {{QStringLiteral("signatures"), QJsonArray {documented}}}, source, 0, 1);
	ok &= expect(report.limited && report.signatures[0].documentation.text.size() == 8191 && report.signatures[0].documentation.text.isValidUtf16(), "documentation bounds preserve Unicode");
	QJsonArray many; for (int i = 0; i < 40; ++i) { many << first; }
	report = parseLanguageSignatureHelp(QJsonObject {{QStringLiteral("signatures"), many}}, source, 0, 1);
	ok &= expect(report.limited && report.signatures.size() == 32, "overload count is bounded");
	QJsonArray parameters; for (int i = 0; i < 129; ++i) { parameters << parameter(QStringLiteral("int")); }
	report = parseLanguageSignatureHelp(QJsonObject {{QStringLiteral("signatures"), QJsonArray {signature(QStringLiteral("f(int)"), parameters)}}}, source, 0, 1);
	ok &= expect(report.skipped == 1 && report.signatures.isEmpty(), "excessive parameters are not partially reindexed");
	QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	const QString path = temp.filePath(QStringLiteral("source.cpp")); { QFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(source.toUtf8()) < 0) { return 1; } }
	LanguageServerClient client; client.start({app.applicationFilePath(), {QStringLiteral("--fixture"), QStringLiteral("signature-slow")}, temp.path(), 1200});
	ok &= expect(wait([&]() { return client.ready(); }) && client.supportsSignatureHelp() && client.signatureTriggers().contains(QStringLiteral("("))
		&& !client.signatureTriggers().contains(QStringLiteral(")")) && client.signatureTriggers(true).contains(QStringLiteral(")")), "provider triggers and retriggers negotiate independently");
	client.synchronize({{path, QStringLiteral("cpp"), source}}); bool received = false;
	client.signatureHelp(path, 0, source.size(), 1, {}, false, {}, [&](const auto& value) { report = value; received = true; });
	ok &= expect(wait([&]() { return received; }) && report.signatures.size() == 2 && report.signatures[0].activeParameter == 1, "client delivers active-argument hints at the synchronized caret");
	report.activeSignature = 1; received = false;
	client.signatureHelp(path, 0, source.size(), 2, QStringLiteral(","), true, report, [&](const auto& value) { report = value; received = true; });
	ok &= expect(wait([&]() { return received; }) && report.activeSignature == 1, "retrigger sends the user's selected overload back to the provider");
	ok &= expect(client.signatureHelp(path, 0, 1, 2, QStringLiteral("?"), false, {}, {}) < 0, "unadvertised trigger characters cannot start requests");
	received = false; const int request = client.signatureHelp(path, 0, 1, 1, {}, false, {}, [&](const auto&) { received = true; }); client.cancelRequest(request); wait([] { return false; }, 450);
	ok &= expect(!received && client.pendingRequests() == 0, "cancelled replies cannot revive parameter hints");
	client.signatureHelp(path, 0, 1, 1, {}, false, {}, [&](const auto&) { received = true; }); client.synchronize({{path, QStringLiteral("cpp"), source + QLatin1Char('2')}}); wait([] { return false; }, 450);
	ok &= expect(!received, "changed source versions retire pending hints"); client.stop(); wait([&]() { return client.state() == QStringLiteral("stopped"); });
	if (app.arguments().size() > 1) {
		const auto run = [&](const QString& mode, const QStringList& extra, int expected) {
			QProcess process; process.setWorkingDirectory(temp.path());
			process.start(app.arguments()[1], QStringList {QStringLiteral("--cli"), QStringLiteral("code"), QStringLiteral("language-server"), path,
				QStringLiteral("--server"), app.applicationFilePath(), QStringLiteral("--server-args"), QString::fromUtf8(QJsonDocument(QJsonArray {QStringLiteral("--fixture"), mode}).toJson(QJsonDocument::Compact)),
				QStringLiteral("--root"), temp.path(), QStringLiteral("--signature-help"), QStringLiteral("--line"), QStringLiteral("1"), QStringLiteral("--column"), QStringLiteral("8"),
				QStringLiteral("--timeout-ms"), QStringLiteral("1000"), QStringLiteral("--json")} + extra);
			const bool done = process.waitForFinished(10000); const auto bytes = process.readAllStandardOutput();
			ok &= expect(done && process.exitCode() == expected, qPrintable(QStringLiteral("CLI %1 expected %2, got %3: %4").arg(mode).arg(expected).arg(process.exitCode()).arg(QString::fromUtf8(bytes))));
			return QJsonDocument::fromJson(bytes).object().value(QStringLiteral("languageServer")).toObject();
		};
		const auto result = run(QStringLiteral("signature"), {}, 0);
		ok &= expect(result.value(QStringLiteral("signatureHelpReceived")).toBool() && result.value(QStringLiteral("signatureHelp")).toObject().value(QStringLiteral("signatures")).toArray().size() == 2, "CLI publishes complete structured signature results");
		run(QStringLiteral("signature-empty"), {}, 0); run(QStringLiteral("signature-malformed"), {}, 4); run(QStringLiteral("signature-bad-range"), {}, 4);
		run(QStringLiteral("signature-large"), {}, 4); run(QStringLiteral("signature-hang"), {}, 4); run(QStringLiteral("no-signature"), {}, 4);
		run(QStringLiteral("signature"), {QStringLiteral("--hover")}, 2); run(QStringLiteral("signature"), {QStringLiteral("--write")}, 2);
		QFile file(path); ok &= expect(file.open(QIODevice::ReadOnly) && file.readAll() == source.toUtf8(), "signature queries and failures never change saved bytes");
	}
	return ok ? 0 : 1;
}
