#include "core/language_formatting.h"
#include "tests/language_server_fixture.h"

#include <QCryptographicHash>
#include <QProcess>
#include <QTemporaryDir>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* why) { if (!condition) { std::cerr << why << '\n'; } return condition; }
QJsonObject pos(int line, int column) { return {{QStringLiteral("line"), line}, {QStringLiteral("character"), column}}; }
QJsonObject edit(int line, int first, int endLine, int last, const QString& text)
{
	return {{QStringLiteral("range"), QJsonObject {{QStringLiteral("start"), pos(line, first)}, {QStringLiteral("end"), pos(endLine, last)}}}, {QStringLiteral("newText"), text}};
}
bool wait(const std::function<bool()>& ready, int ms = 4000)
{
	QElapsedTimer clock; clock.start(); while (!ready() && clock.elapsed() < ms) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(2); } return ready();
}
bool write(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	if (app.arguments().value(1) == QStringLiteral("--fixture")) { return runLanguageServerFixture(app.arguments().value(2)); }
	bool ok = true; QString text, error;
	const QString source = QString::fromUtf8("// 🌍\nint   value;\nint   other;\n");
	const QJsonArray edits {edit(2, 3, 2, 6, QStringLiteral(" ")), edit(1, 3, 1, 6, QStringLiteral(" "))};
	const auto report = parseLanguageFormatting(edits, source);
	ok &= expect(report.error.isEmpty() && report.edits.size() == 2 && report.edits[0].offset < report.edits[1].offset
		&& previewLanguageFormatting(source, report, &text, &error) && text == QString::fromUtf8("// 🌍\nint value;\nint other;\n"), "unsorted formatting edits preserve Unicode and compose against one source snapshot");
	ok &= expect(!previewLanguageFormatting(source + QLatin1Char('x'), report, &text, &error) && text.isEmpty(), "a changed source rejects the complete edit set");
	const auto multiple = parseLanguageFormatting(QJsonArray {edit(0, 0, 0, 0, QStringLiteral("a")), edit(0, 0, 0, 0, QStringLiteral("b")), edit(0, 0, 0, 1, QStringLiteral("c"))}, QStringLiteral("x!"));
	ok &= expect(multiple.edits.size() == 1 && previewLanguageFormatting(QStringLiteral("x!"), multiple, &text) && text == QStringLiteral("abc!"), "same-position insertions retain array order before a replacement");
	const auto newline = parseLanguageFormatting(QJsonArray {edit(0, 1, 0, 1, QStringLiteral("\r\n"))}, QStringLiteral("ab"));
	ok &= expect(previewLanguageFormatting(QStringLiteral("ab"), newline, &text) && text == QStringLiteral("a\nb"), "inserted line separators normalize to the editor representation");
	const auto reject = [&](const QJsonValue& value, const QString& why) { const auto invalid = parseLanguageFormatting(value, source); return expect(!invalid.error.isEmpty() && invalid.edits.isEmpty(), why.toUtf8().constData()); };
	ok &= reject(QJsonObject {}, QStringLiteral("malformed reply must fail"));
	ok &= reject(QJsonArray {edit(1, 3, 1, 6, QStringLiteral(" ")), edit(1, 4, 1, 7, {})}, QStringLiteral("overlapping edits cannot partially apply"));
	ok &= reject(QJsonArray {edit(0, 4, 0, 5, {})}, QStringLiteral("ranges cannot split an astral character"));
	ok &= reject(QJsonArray {edit(999999, 0, 999999, 1, {})}, QStringLiteral("ranges must exist in the source"));
	ok &= reject(QJsonArray {edit(1, 0, 0, 0, {})}, QStringLiteral("reversed ranges must fail"));
	ok &= reject(QJsonArray {edit(1, 0, 1, 0, QString(QChar(0)))}, QStringLiteral("NUL insertion must fail"));
	ok &= reject(QJsonArray {edit(1, 0, 1, 0, QString(QChar(0xd800)))}, QStringLiteral("invalid Unicode insertion must fail"));
	auto annotation = edit(1, 0, 1, 0, QStringLiteral("x")); annotation.insert(QStringLiteral("annotationId"), QStringLiteral("review"));
	ok &= reject(QJsonArray {annotation}, QStringLiteral("unreviewed annotated edits are unsupported"));
	QJsonArray tooMany; for (int i = 0; i <= 10000; ++i) { tooMany << edit(0, 0, 0, 0, QStringLiteral("x")); }
	ok &= reject(tooMany, QStringLiteral("excessive edit counts reject the whole reply"));
	ok &= reject(QJsonArray {edit(0, 0, 0, 0, QString(textDocumentByteLimit + 1, QLatin1Char('x')))}, QStringLiteral("inserted text is bounded"));
	ok &= expect(parseLanguageFormatting(QJsonValue(QJsonValue::Null), source).error.isEmpty()
		&& parseLanguageFormatting(QJsonArray {edit(1, 0, 1, 3, QStringLiteral("int"))}, source).edits.isEmpty(), "null and exact no-op replies produce no changes");
	ok &= expect(parseLanguageFormatting(QJsonArray {edit(0, 0, 0, 1, QStringLiteral("ab")), edit(0, 1, 0, 2, {})}, QStringLiteral("ab")).edits.isEmpty(), "combined no-op replies cannot alter encoding or line-ending metadata on save");
	ok &= expect(!validLanguageFormattingOptions(source, {0, true}) && !validLanguageFormattingOptions(source, {4, true, 4, 1})
		&& validLanguageFormattingOptions(source, {2, true, 7, 5}), "formatting options enforce valid tab width and Unicode selection boundaries");
	previewLanguageFormatting(source, report, &text);
	ok &= expect(languageFormattedOffset(report, text, source.size()) == text.size(), "caret after edits tracks cumulative displacement");
	const auto emoji = parseLanguageFormatting(QJsonArray {edit(0, 0, 0, 2, QString::fromUtf8("🌍"))}, QStringLiteral("ab"));
	previewLanguageFormatting(QStringLiteral("ab"), emoji, &text);
	ok &= expect(languageFormattedOffset(emoji, text, 1) == 0, "caret mapping cannot split replacement surrogate pairs");

	QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	const QString path = temp.filePath(QStringLiteral("source.cpp")); const QByteArray bytes = "int   value;\r\nint   other;\n";
	ok &= expect(write(path, bytes), "write source fixture");
	auto file = readTextFile(path); auto savedEdits = parseLanguageFormatting(QJsonArray {edit(0, 3, 0, 6, QStringLiteral(" "))}, file.text);
	ok &= expect(saveTextFileEdits(file, savedEdits.edits).succeeded && read(path) == bytes, "default exact-range save is a no-write preview");
	ok &= expect(saveTextFileEdits(file, savedEdits.edits, false).succeeded && read(path) == QByteArray("int value;\r\nint   other;\n"), "exact-range writes preserve mixed line endings");
	ok &= expect(!saveTextFileEdits(file, savedEdits.edits, false).succeeded, "stale byte snapshot cannot overwrite a later save");
	write(path, bytes);
	LanguageServerClient client; client.start({app.applicationFilePath(), {QStringLiteral("--fixture"), QStringLiteral("slow")}, temp.path(), 2000});
	ok &= expect(wait([&]() { return client.ready(); }) && client.supportsFormatting() && client.supportsFormatting(true), "server advertises document and selection formatting");
	client.synchronize({{path, QStringLiteral("cpp"), file.text}});
	bool received = false;
	client.formatting(path, {2, true, 0, 12}, [&](const auto& result) { received = result.error.isEmpty() && result.edits.size() == 1 && result.version == client.documentVersion(path) && result.sourceSha256 == QCryptographicHash::hash(file.text.toUtf8(), QCryptographicHash::Sha256); });
	ok &= expect(wait([&]() { return received; }), "range request carries formatting options, range and synchronized version");
	received = false; const int pending = client.formatting(path, {}, [&](const auto&) { received = true; }); client.cancelRequest(pending);
	wait([&]() { return false; }, 450); ok &= expect(!received && client.pendingRequests() == 0, "cancelled formatting never publishes a late reply");
	client.formatting(path, {}, [&](const auto&) { received = true; }); client.synchronize({{path, QStringLiteral("cpp"), file.text + QLatin1Char(' ')}});
	wait([&]() { return false; }, 450); ok &= expect(!received && client.pendingRequests() == 0, "source changes retire formatting callbacks");
	client.stop(); wait([&]() { return client.state() == QStringLiteral("stopped"); });

	if (app.arguments().size() > 1) {
		const QString binary = app.arguments()[1];
		const auto run = [&](const QString& mode, const QStringList& options, int exit) {
			QProcess process; process.start(binary, QStringList {QStringLiteral("--cli"), QStringLiteral("code"), QStringLiteral("language-server"), path,
				QStringLiteral("--server"), app.applicationFilePath(), QStringLiteral("--server-args"), QString::fromUtf8(QJsonDocument(QJsonArray {QStringLiteral("--fixture"), mode}).toJson(QJsonDocument::Compact)),
				QStringLiteral("--timeout-ms"), QStringLiteral("1200"), QStringLiteral("--json")} + options);
			if (!process.waitForFinished(7000)) { process.kill(); process.waitForFinished(); }
			ok &= expect(process.exitStatus() == QProcess::NormalExit && process.exitCode() == exit, qPrintable(QStringLiteral("formatting CLI exit status: %1").arg(mode)));
			const auto output = QJsonDocument::fromJson(process.readAllStandardOutput()); ok &= expect(output.isObject(), "formatting CLI emits valid JSON");
			return output.object().value(QStringLiteral("languageServer")).toObject();
		};
		const QStringList document {QStringLiteral("--format-document")};
		const QStringList range {QStringLiteral("--format-range"), QStringLiteral("--line"), QStringLiteral("1"), QStringLiteral("--column"), QStringLiteral("1"), QStringLiteral("--end-line"), QStringLiteral("1"), QStringLiteral("--end-column"), QStringLiteral("13")};
		const auto preview = run(QStringLiteral("quiet"), document, 0);
		const auto formatting = preview.value(QStringLiteral("formatting")).toObject();
		ok &= expect(preview.value(QStringLiteral("formattingReceived")).toBool() && !preview.value(QStringLiteral("diagnosticsReceived")).toBool()
			&& formatting.value(QStringLiteral("dryRun")).toBool() && formatting.value(QStringLiteral("text")).toString() == QStringLiteral("int value;\nint other;\n") && read(path) == bytes, "formatter-only servers can preview without diagnostic publication or file changes");
		const auto selected = run(QStringLiteral("full"), range + QStringList {QStringLiteral("--tab-size"), QStringLiteral("2"), QStringLiteral("--insert-spaces")}, 0).value(QStringLiteral("formatting")).toObject();
		ok &= expect(selected.value(QStringLiteral("edits")).toArray().size() == 1 && selected.value(QStringLiteral("tabSize")).toInt() == 2 && selected.value(QStringLiteral("insertSpaces")).toBool(), "range CLI returns exact proposed edits and options");
		const QStringList writeOptions {QStringLiteral("--write"), QStringLiteral("--expected-sha256"), formatting.value(QStringLiteral("sourceFileSha256")).toString()};
		for (const auto& mode : {QStringLiteral("format-overlap"), QStringLiteral("format-large"), QStringLiteral("format-malformed"), QStringLiteral("format-error"), QStringLiteral("format-hang"), QStringLiteral("no-format")}) {
			run(mode, document + writeOptions, 4); ok &= expect(read(path) == bytes, "failed formatting leaves saved bytes intact");
		}
		run(QStringLiteral("no-range-format"), range, 4);
		ok &= expect(run(QStringLiteral("format-empty"), document, 0).value(QStringLiteral("formatting")).toObject().value(QStringLiteral("edits")).toArray().isEmpty(), "valid empty formatting is successful");
		const auto written = run(QStringLiteral("full"), range + writeOptions, 0).value(QStringLiteral("formatting")).toObject();
		ok &= expect(written.value(QStringLiteral("written")).toBool() && read(path) == QByteArray("int value;\r\nint   other;\n"), "explicit range write preserves untouched mixed separators");
		run(QStringLiteral("full"), document + writeOptions, 4);
		write(path, bytes); run(QStringLiteral("format-write-race"), document + writeOptions, 4);
		ok &= expect(read(path) == QByteArray("external edit\n"), "edits arriving after an external write cannot replace it");
		write(path, bytes);
		for (const QStringList& bad : {document + QStringList {QStringLiteral("--write")}, document + range, document + QStringList {QStringLiteral("--tab-size"), QStringLiteral("0")}, document + writeOptions + QStringList {QStringLiteral("--dry-run")}, QStringList {QStringLiteral("--format-range")}}) { run(QStringLiteral("full"), bad, 2); }
	}
	return ok ? 0 : 1;
}
