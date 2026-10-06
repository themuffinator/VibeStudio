#include "core/completion_snippet.h"
#include "tests/language_server_fixture.h"
#include <QProcess>
#include <QTemporaryDir>
#include <random>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* why) { if (!value) { std::cerr << why << '\n'; } return value; }
QJsonObject position(int line, int character) { return {{QStringLiteral("line"), line}, {QStringLiteral("character"), character}}; }
QJsonObject range(int line, int start, int end) { return {{QStringLiteral("start"), position(line, start)}, {QStringLiteral("end"), position(line, end)}}; }
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); if (app.arguments().value(1) == QStringLiteral("--fixture")) { return runLanguageServerFixture(app.arguments().value(2)); }
	bool ok = true;
	auto snippet = expandCompletionSnippet(QStringLiteral("call(${1:🌍}, $1, ${2|left,right|})$0"));
	ok &= expect(snippet.error.isEmpty() && snippet.text == QStringLiteral("call(🌍, 🌍, left)") && snippet.stops.size() == 4
		&& snippet.stops[0].length == 2 && snippet.stops[2].choices.size() == 2 && snippet.finalOffset == snippet.text.size(), "Unicode fields, linked defaults, choices and final cursor expand together");
	snippet = expandCompletionSnippet(QStringLiteral("${1:pair(${2:name})} = $1"));
	ok &= expect(snippet.error.isEmpty() && snippet.text == QStringLiteral("pair(name) = pair(name)") && snippet.stops.size() == 4
		&& snippet.stops[1].parent == 0 && snippet.stops[3].parent == 2, "nested fields are retained inside every linked outer placeholder");
	snippet = expandCompletionSnippet(QStringLiteral("$1:${2:two}:${1:one}:$0!"));
	ok &= expect(snippet.error.isEmpty() && snippet.text == QStringLiteral("one:two:one:!") && snippet.finalOffset == snippet.text.size() - 1, "forward mirrors resolve defaults and explicit final stop precedes trailing text");
	snippet = expandCompletionSnippet(QStringLiteral("\\$1 \\} \\\\ \\q ${1|a\\,b,c\\|d|}"));
	ok &= expect(snippet.error.isEmpty() && snippet.text == QStringLiteral("$1 } \\ \\q a,b") && snippet.stops[0].choices[1] == QStringLiteral("c|d"), "snippet and choice escaping preserves literal punctuation");
	const auto variables = completionSnippetVariables(QStringLiteral("first\nobject.word\n"), 11, QStringLiteral("/project/code/source.cpp"));
	ok &= expect(completionSnippetVariables(QStringLiteral("🌍value"), 0, {}).value(QStringLiteral("TM_CURRENT_WORD")).isEmpty()
		&& completionSnippetVariables(QStringLiteral("\U00010400name"), 0, {}).value(QStringLiteral("TM_CURRENT_WORD")) == QStringLiteral("\U00010400name"), "document word variables distinguish astral letters from symbols");
	snippet = expandCompletionSnippet(QStringLiteral("$TM_FILENAME ${TM_SELECTED_TEXT:${1:fallback}} $TM_LINE_NUMBER $UNKNOWN $UNKNOWN"), variables);
	ok &= expect(snippet.error.isEmpty() && snippet.text == QStringLiteral("source.cpp fallback 2 UNKNOWN UNKNOWN") && snippet.stops.size() == 3
		&& snippet.stops[1].number == snippet.stops[2].number && variables.value(QStringLiteral("TM_CURRENT_WORD")) == QStringLiteral("object"), "bounded document variables, defaults and unknown linked variables");
	for (const auto& bad : {QStringLiteral("${1:missing"), QStringLiteral("${1|a,b}"), QStringLiteral("${1:$1}"), QStringLiteral("${1:$2}${2:$1}"),
		QStringLiteral("${1000001:x}"), QStringLiteral("${TM_FILENAME/(.*)/$1/}"), QStringLiteral("${1:x}").repeated(129), QString(65537, QLatin1Char('x')),
		QStringLiteral("${1:").repeated(17) + QStringLiteral("x") + QStringLiteral("}").repeated(17)}) {
		ok &= expect(!expandCompletionSnippet(bad).error.isEmpty(), "malformed, cyclic, transformed and excessive snippets cannot become partial insertions");
	}
	const QString source = QStringLiteral("// source\nobject.taSuffix\n");
	QJsonObject item {{QStringLiteral("label"), QStringLiteral("target")}, {QStringLiteral("textEditText"), QStringLiteral("target(${1:value}, $1)$0")},
		{QStringLiteral("additionalTextEdits"), QJsonArray {QJsonObject {{QStringLiteral("range"), range(0, 0, 0)}, {QStringLiteral("newText"), QStringLiteral("// import\n")}}}}};
	const QJsonObject defaults {{QStringLiteral("insertTextFormat"), 2}, {QStringLiteral("editRange"), range(1, 7, 15)}};
	auto parsed = parseLanguageCompletions(QJsonObject {{QStringLiteral("itemDefaults"), defaults}, {QStringLiteral("items"), QJsonArray {item}}}, source, 1, 9, true, QStringLiteral("source.cpp"));
	if (!expect(parsed.items.size() == 1, "completion defaults produce a snippet item")) { return 1; }
	QString preview, error; int caret = 0;
	ok &= expect(!previewLanguageCompletion(source, parsed.items[0], &preview, &caret, &error), "deferred snippet edits wait for resolution");
	const auto resolved = resolveLanguageCompletion(parsed.items[0], parsed.items[0].wire, source, &error);
	ok &= expect(error.isEmpty() && previewLanguageCompletion(source, resolved, &preview, &caret, &error)
		&& preview == QStringLiteral("// import\n// source\nobject.target(value, value)\n") && resolved.snippet
		&& resolved.tabStops[0].offset == preview.indexOf(QStringLiteral("value")) && caret == preview.indexOf(QStringLiteral(")")) + 1, "resolved snippets keep ranges aligned after related imports");
	auto corrupt = resolved; corrupt.tabStops[0].offset = 100000;
	ok &= expect(!previewLanguageCompletion(source, corrupt, nullptr, nullptr, &error), "corrupt field metadata rejects the entire preview");
	std::mt19937 random(41); const QString alphabet = QStringLiteral("abc0123${}:|,/\\_");
	for (int i = 0; i < 2000; ++i) {
		QString body; for (int j = 0, size = random() % 150; j < size; ++j) { body += alphabet[random() % alphabet.size()]; }
		const auto value = expandCompletionSnippet(body);
		ok &= expect(!value.error.isEmpty() || (value.text.size() <= 65536 && validCompletionSnippetStops(value.text, value.stops)), "bounded malformed-input exploration preserves valid expansion ranges");
	}
	if (app.arguments().size() > 1) {
		QTemporaryDir temp; if (!temp.isValid()) { return 1; } const QString path = temp.filePath(QStringLiteral("source.cpp"));
		const QByteArray original = "// source\r\nobject.ta\r\n"; { QFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(original) != original.size()) { return 1; } }
		for (const auto& mode : {QStringLiteral("snippet-completion"), QStringLiteral("resolve-completion-snippet"), QStringLiteral("snippet-transform")}) {
			QProcess process; process.setWorkingDirectory(temp.path());
			QStringList args {QStringLiteral("--cli"), QStringLiteral("code"), QStringLiteral("language-server"), path, QStringLiteral("--server"), app.applicationFilePath(),
				QStringLiteral("--server-args"), QString::fromUtf8(QJsonDocument(QJsonArray {QStringLiteral("--fixture"), mode}).toJson(QJsonDocument::Compact)),
				QStringLiteral("--root"), temp.path(), QStringLiteral("--completion"), QStringLiteral("--line"), QStringLiteral("2"), QStringLiteral("--column"), QStringLiteral("10"), QStringLiteral("--json")};
			if (mode.startsWith(QStringLiteral("resolve"))) { args << QStringLiteral("--resolve-completion") << QStringLiteral("1"); }
			process.start(app.arguments()[1], args); const bool done = process.waitForFinished(15000);
			const auto result = QJsonDocument::fromJson(process.readAllStandardOutput()).object().value(QStringLiteral("languageServer")).toObject().value(QStringLiteral("completions")).toObject();
			const bool transform = mode == QStringLiteral("snippet-transform");
			ok &= expect(done && process.exitCode() == (transform ? 4 : 0), "CLI completion snippets retain capability and diagnostic exit contracts");
			if (!transform) { const auto items = result.value(QStringLiteral("items")).toArray(); const auto proposal = items.isEmpty() ? QJsonObject() : items.first().toObject(); ok &= expect(proposal.value(QStringLiteral("snippet")).toBool() && proposal.value(QStringLiteral("tabStops")).toArray().size() == 4, "CLI returns expanded text with navigable absolute ranges"); }
		}
		QFile file(path); ok &= expect(file.open(QIODevice::ReadOnly) && file.readAll() == original, "CLI snippet inspection preserves original source bytes");
	}
	return ok ? 0 : 1;
}
