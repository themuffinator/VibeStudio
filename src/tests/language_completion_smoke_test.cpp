#include "core/language_completion.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool ok, const char* message) { if (!ok) { std::cerr << message << '\n'; } return ok; }
QJsonObject position(int line, int character) { return {{QStringLiteral("line"), line}, {QStringLiteral("character"), character}}; }
QJsonObject range(int line, int start, int end) { return {{QStringLiteral("start"), position(line, start)}, {QStringLiteral("end"), position(line, end)}}; }
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); bool ok = true;
	const QString source = QStringLiteral("// 🌍\nobject.taSuffix\n");
	QJsonObject primary {{QStringLiteral("insert"), range(1, 7, 9)}, {QStringLiteral("replace"), range(1, 7, 15)}, {QStringLiteral("newText"), QStringLiteral("target()")}};
	QJsonObject item {{QStringLiteral("label"), QStringLiteral("target")}, {QStringLiteral("textEdit"), primary}, {QStringLiteral("detail"), QStringLiteral("int target()")},
		{QStringLiteral("additionalTextEdits"), QJsonArray {QJsonObject {{QStringLiteral("range"), range(0, 0, 0)}, {QStringLiteral("newText"), QStringLiteral("// import\r\n")}}}}};
	auto result = parseLanguageCompletions(QJsonArray {item}, source, 1, 9);
	ok &= expect(result.error.isEmpty() && result.items.size() == 1 && result.items[0].edits.size() == 2, "parse a replace range plus a related import edit");
	if (result.items.isEmpty()) { return 1; }
	QString edited, error; int caret = 0;
	ok &= expect(previewLanguageCompletion(source, result.items[0], &edited, &caret, &error)
		&& edited == QStringLiteral("// import\n// 🌍\nobject.target()\n") && caret == edited.indexOf(QStringLiteral("target()")) + 8,
		"related edits preserve Unicode and place the caret after the primary insertion");
	ok &= expect(!previewLanguageCompletion(source + QLatin1Char('x'), result.items[0], &edited, &caret, &error), "changed source hashes reject the complete edit set");
	QJsonObject overlap = item;
	overlap.insert(QStringLiteral("additionalTextEdits"), QJsonArray {QJsonObject {{QStringLiteral("range"), range(1, 7, 7)}, {QStringLiteral("newText"), QStringLiteral("bad")}}});
	ok &= expect(parseLanguageCompletions(QJsonArray {overlap}, source, 1, 9).skipped == 1, "overlapping import/main edits cannot apply partially");
	QJsonObject surrogate = item;
	surrogate.insert(QStringLiteral("additionalTextEdits"), QJsonArray {QJsonObject {{QStringLiteral("range"), range(0, 4, 4)}, {QStringLiteral("newText"), QStringLiteral("bad")}}});
	ok &= expect(parseLanguageCompletions(QJsonArray {surrogate}, source, 1, 9).skipped == 1, "edits cannot split a supplementary Unicode character");
	QJsonObject snippet = item; snippet.insert(QStringLiteral("insertTextFormat"), 2);
	snippet.insert(QStringLiteral("textEdit"), QJsonObject {{QStringLiteral("range"), range(1, 7, 15)}, {QStringLiteral("newText"), QStringLiteral("${TM_FILENAME/(.*)/$1/}")}});
	QJsonObject command = item; command.insert(QStringLiteral("command"), QJsonObject {{QStringLiteral("command"), QStringLiteral("unsafe.run")}});
	QJsonObject oversized = item; oversized.insert(QStringLiteral("label"), QString(1025, QLatin1Char('a')));
	QJsonObject positionOverflow = item; positionOverflow.insert(QStringLiteral("textEdit"), QJsonObject {{QStringLiteral("range"), range(2147483647, 0, 0)}, {QStringLiteral("newText"), QStringLiteral("x")}});
	ok &= expect(parseLanguageCompletions(QJsonArray {snippet, command, oversized, positionOverflow}, source, 1, 9).skipped == 4, "unsupported snippet transforms, executable commands and invalid protocol bounds are explicit omissions");
	QJsonObject defaultItem {{QStringLiteral("label"), QStringLiteral("target")}, {QStringLiteral("textEditText"), QStringLiteral("target()")}};
	QJsonObject list {{QStringLiteral("isIncomplete"), true}, {QStringLiteral("itemDefaults"), QJsonObject {{QStringLiteral("editRange"), range(1, 7, 15)}, {QStringLiteral("insertTextFormat"), 1}}},
		{QStringLiteral("items"), QJsonArray {defaultItem}}};
	result = parseLanguageCompletions(list, source, 1, 9);
	ok &= expect(result.incomplete && result.items.size() == 1 && result.items[0].edits[0].text == QStringLiteral("target()"), "completion list defaults preserve incomplete state and edit text");
	result = parseLanguageCompletions(QJsonArray {QJsonObject {{QStringLiteral("label"), QStringLiteral("target")}, {QStringLiteral("insertText"), QStringLiteral("target()")} }}, source, 1, 9);
	ok &= expect(result.items.size() == 1 && previewLanguageCompletion(source, result.items[0], &edited, &caret, &error)
		&& edited.endsWith(QStringLiteral("object.target()Suffix\n")), "plain insertText replaces the typed prefix without deleting the following suffix");
	QJsonArray many; for (int i = 0; i < 501; ++i) { many << QJsonObject {{QStringLiteral("label"), QStringLiteral("name%1").arg(i)}}; }
	result = parseLanguageCompletions(many, source, 1, 9);
	ok &= expect(result.limited && result.items.size() == 500, "large completion lists remain bounded and explicitly limited");
	ok &= expect(!parseLanguageCompletions(QJsonObject {}, source, 1, 9).error.isEmpty()
		&& !parseLanguageCompletions(QJsonArray {}, source, 0, 4).error.isEmpty(), "malformed lists and caret offsets inside surrogate pairs fail explicitly");
	const QString identifier = QStringLiteral("object.\U00010400e\u0301");
	result = parseLanguageCompletions(QJsonArray {QJsonObject {{QStringLiteral("label"), QStringLiteral("replacement")}}}, identifier, 0, int(identifier.size()));
	ok &= expect(languageWordStart(identifier, int(identifier.size())) == 7 && result.items.size() == 1
		&& previewLanguageCompletion(identifier, result.items[0], &edited, &caret, &error) && edited == QStringLiteral("object.replacement"),
		"default replacements preserve supplementary letters and combining marks as a complete prefix");
	return ok ? 0 : 1;
}
