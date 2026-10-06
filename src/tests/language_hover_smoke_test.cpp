#include "core/language_hover.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }
QJsonObject position(int line, int column) { return {{QStringLiteral("line"), line}, {QStringLiteral("character"), column}}; }
QJsonObject range(int first, int last) { return {{QStringLiteral("start"), position(1, first)}, {QStringLiteral("end"), position(1, last)}}; }
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); bool ok = true;
	const QString source = QStringLiteral("// 🌍\nint target;\n");
	QJsonObject response {{QStringLiteral("contents"), QJsonObject {{QStringLiteral("kind"), QStringLiteral("markdown")}, {QStringLiteral("value"), QStringLiteral("**target**\r\nDocumentation")}}}, {QStringLiteral("range"), range(4, 10)}};
	auto info = parseLanguageHover(response, source, 1, 6);
	ok &= expect(info.error.isEmpty() && info.hasRange && info.offset == 10 && info.length == 6 && info.requestOffset == 12 && info.contents.size() == 1
		&& info.contents.first().text == QStringLiteral("**target**\nDocumentation") && info.sourceSha256 == QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256), "hover Markdown, normalized text and source range/hash provenance");
	response.insert(QStringLiteral("contents"), QJsonArray {QStringLiteral("Legacy **documentation**"), QJsonObject {{QStringLiteral("language"), QStringLiteral("cpp")}, {QStringLiteral("value"), QStringLiteral("int target;")}}});
	info = parseLanguageHover(response, source, 1, 6);
	ok &= expect(info.contents.size() == 2 && info.contents[0].kind == QStringLiteral("markdown") && info.contents[1].kind == QStringLiteral("code") && info.contents[1].language == QStringLiteral("cpp"), "legacy mixed Markdown and code retain distinct display semantics");
	response.insert(QStringLiteral("contents"), QJsonObject {{QStringLiteral("kind"), QStringLiteral("plaintext")}, {QStringLiteral("value"), QStringLiteral("<b>literal</b>")}});
	info = parseLanguageHover(response, source, 1, 6);
	ok &= expect(info.contents.first().kind == QStringLiteral("plaintext") && info.contents.first().text == QStringLiteral("<b>literal</b>"), "plain text remains literal");
	response.insert(QStringLiteral("range"), range(4, 999)); info = parseLanguageHover(response, source, 1, 6);
	ok &= expect(!info.hasRange && info.skipped == 1 && !info.contents.isEmpty(), "invalid ranges cannot become source highlights and remain explicitly partial");
	response.insert(QStringLiteral("range"), QJsonObject {{QStringLiteral("start"), position(0, 4)}, {QStringLiteral("end"), position(1, 10)}});
	ok &= expect(!parseLanguageHover(response, source, 1, 6).hasRange, "range boundaries cannot split surrogate pairs");
	response.remove(QStringLiteral("range"));
	response.insert(QStringLiteral("contents"), QJsonArray {42, QJsonObject {{QStringLiteral("kind"), QStringLiteral("html")}, {QStringLiteral("value"), QStringLiteral("bad")}}, QString(QChar(0)), QString(QChar(0xd800)), QStringLiteral("valid")});
	info = parseLanguageHover(response, source, 1, 6);
	ok &= expect(info.skipped == 4 && info.contents.size() == 1 && !info.limited, "malformed, unsupported and invalid Unicode content is omitted explicitly");
	response.insert(QStringLiteral("contents"), QString(65535, QLatin1Char('x')) + QStringLiteral("🌍rest")); info = parseLanguageHover(response, source, 1, 6);
	ok &= expect(info.limited && info.contents.first().text.size() == 65535 && info.contents.first().text.isValidUtf16(), "bounded documentation never truncates halfway through a Unicode character");
	QJsonArray parts; for (int i = 0; i < 40; ++i) { parts << QStringLiteral("part"); } response.insert(QStringLiteral("contents"), parts);
	info = parseLanguageHover(response, source, 1, 6);
	ok &= expect(info.limited && info.contents.size() == 32 && info.skipped == 8, "part count bound is explicit");
	const auto empty = parseLanguageHover(QJsonValue(QJsonValue::Null), source, 1, 6);
	ok &= expect(empty.error.isEmpty() && empty.contents.isEmpty() && !empty.sourceSha256.isEmpty(), "null is a valid no-information reply with source provenance");
	ok &= expect(!parseLanguageHover(QJsonObject {}, source, 1, 6).error.isEmpty() && !parseLanguageHover(QJsonArray {}, source, 1, 6).error.isEmpty(), "invalid envelopes fail visibly");
	ok &= expect(!parseLanguageHover(response, source, 0, 4).error.isEmpty() && !parseLanguageHover(response, source, 99, 0).error.isEmpty(), "request positions are validated before parsing");
	return ok ? 0 : 1;
}
