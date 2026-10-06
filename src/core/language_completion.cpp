#include "core/language_completion.h"
#include "core/text_document.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {
namespace {
constexpr int itemLimit = 500;
constexpr int editLimit = 32;
constexpr int itemTextLimit = 65536;

QByteArray hash(const QString& text) { return QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256); }
bool boundary(const QString& text, int offset)
{
	return offset >= 0 && offset <= text.size()
		&& !(offset > 0 && offset < text.size() && text[offset - 1].isHighSurrogate() && text[offset].isLowSurrogate());
}
bool integer(const QJsonValue& value, int* number)
{
	const double n = value.toDouble(-1);
	if (!value.isDouble() || n < 0 || n >= std::numeric_limits<int>::max() || n != std::floor(n)) { return false; }
	*number = int(n); return true;
}
int offset(const QString& source, const QVector<int>& starts, int line, int character)
{
	if (line < 0 || line >= starts.size() || character < 0) { return -1; }
	const int begin = starts[line], end = line + 1 < starts.size() ? starts[line + 1] - 1 : int(source.size());
	return character <= end - begin && boundary(source, begin + character) ? begin + character : -1;
}
bool range(const QString& source, const QVector<int>& starts, const QJsonValue& value, LanguageCompletionEdit* edit, bool singleLine)
{
	const auto object = value.toObject();
	const auto start = object.value(QStringLiteral("start")).toObject(), end = object.value(QStringLiteral("end")).toObject();
	int line, character, endLine, endCharacter;
	if (!integer(start.value(QStringLiteral("line")), &line) || !integer(start.value(QStringLiteral("character")), &character)
		|| !integer(end.value(QStringLiteral("line")), &endLine) || !integer(end.value(QStringLiteral("character")), &endCharacter)
		|| (singleLine && line != endLine)) { return false; }
	edit->offset = offset(source, starts, line, character);
	const int finish = offset(source, starts, endLine, endCharacter);
	if (edit->offset < 0 || finish < edit->offset) { return false; }
	edit->length = finish - edit->offset; return true;
}
bool insertion(const QJsonValue& value, QString* text)
{
	if (!value.isString()) { return false; }
	*text = value.toString();
	if (text->size() > itemTextLimit || !text->isValidUtf16() || text->contains(QChar(0))) { return false; }
	text->replace(QStringLiteral("\r\n"), QStringLiteral("\n")); text->replace(QLatin1Char('\r'), QLatin1Char('\n')); text->replace(QChar(0x2029), QLatin1Char('\n'));
	return true;
}
bool validateEdits(const QString& source, const QVector<LanguageCompletionEdit>& edits)
{
	if (edits.isEmpty() || edits.size() > editLimit + 1) { return false; }
	qint64 textSize = source.size(), inserted = 0;
	int priorStart = -1;
	for (const auto& edit : edits) {
		if (edit.offset < 0 || edit.length < 0 || qint64(edit.offset) + edit.length > source.size()
			|| !boundary(source, edit.offset) || !boundary(source, edit.offset + edit.length)
			|| (priorStart >= 0 && (edit.offset >= priorStart || edit.offset + edit.length > priorStart))
			|| !edit.text.isValidUtf16() || edit.text.contains(QChar(0))) { return false; }
		priorStart = edit.offset; inserted += edit.text.size(); textSize += edit.text.size() - edit.length;
	}
	return inserted <= itemTextLimit && textSize <= textDocumentByteLimit;
}
}

int languageSourceOffset(const QString& source, int line, int character)
{
	if (line < 0 || character < 0 || line > source.size() || character > source.size()) { return -1; }
	qsizetype begin = 0;
	for (int i = 0; i < line; ++i) { const auto end = source.indexOf(QLatin1Char('\n'), begin); if (end < 0) { return -1; } begin = end + 1; }
	const auto end = source.indexOf(QLatin1Char('\n'), begin);
	const auto length = (end < 0 ? source.size() : end) - begin;
	return character <= length && boundary(source, int(begin) + character) ? int(begin) + character : -1;
}

int languageWordStart(const QString& source, int caret)
{
	if (!boundary(source, caret)) { return caret; }
	int start = caret;
	while (start > 0) {
		int previous = start - 1;
		char32_t character = source[previous].unicode();
		if (source[previous].isLowSurrogate() && previous > 0 && source[previous - 1].isHighSurrogate()) { character = QChar::surrogateToUcs4(source[previous - 1], source[previous]); --previous; }
		const auto category = QChar::category(character);
		if (!QChar::isLetterOrNumber(character) && character != '_' && category != QChar::Mark_NonSpacing && category != QChar::Mark_SpacingCombining && category != QChar::Mark_Enclosing) { break; }
		start = previous;
	}
	return start;
}

LanguageCompletions parseLanguageCompletions(const QJsonValue& response, const QString& source, int line, int character, bool resolveSupported, const QString& filePath)
{
	LanguageCompletions result;
	const int cursor = languageSourceOffset(source, line, character);
	if (cursor < 0 || source.size() > textDocumentByteLimit || !source.isValidUtf16()) {
		result.error = QCoreApplication::translate("LanguageCompletion", "The completion position or source text is invalid."); return result;
	}
	if (response.isNull()) { return result; }
	const QJsonObject list = response.toObject();
	if (!response.isArray() && (!response.isObject() || !list.value(QStringLiteral("items")).isArray())) {
		result.error = QCoreApplication::translate("LanguageCompletion", "The language server returned an invalid completion list."); return result;
	}
	const auto entries = response.isArray() ? response.toArray() : list.value(QStringLiteral("items")).toArray();
	const auto defaults = list.value(QStringLiteral("itemDefaults")).toObject();
	result.incomplete = list.value(QStringLiteral("isIncomplete")).toBool();
	result.limited = entries.size() > itemLimit;
	const QByteArray sourceHash = hash(source);
	QVector<int> starts {0};
	for (qsizetype at = source.indexOf(QLatin1Char('\n')); at >= 0; at = source.indexOf(QLatin1Char('\n'), at + 1)) { starts << int(at) + 1; }
	const int wordStart = languageWordStart(source, cursor);
	const auto snippetVariables = completionSnippetVariables(source, cursor, filePath);
	qint64 totalText = 0, totalWire = 0;
	for (qsizetype index = 0; index < std::min<qsizetype>(itemLimit, entries.size()); ++index) {
		auto object = entries[index].toObject();
		for (const auto& name : {QStringLiteral("insertTextFormat"), QStringLiteral("insertTextMode"), QStringLiteral("data")}) {
			if (!object.contains(name) && defaults.contains(name)) { object.insert(name, defaults.value(name)); }
		}
		LanguageCompletionItem item;
		item.label = object.value(QStringLiteral("label")).toString();
		const auto format = object.value(QStringLiteral("insertTextFormat")).isUndefined() ? defaults.value(QStringLiteral("insertTextFormat")) : object.value(QStringLiteral("insertTextFormat"));
		const auto mode = object.value(QStringLiteral("insertTextMode")).isUndefined() ? defaults.value(QStringLiteral("insertTextMode")) : object.value(QStringLiteral("insertTextMode"));
		if (item.label.isEmpty() || item.label.size() > 1024 || !item.label.isValidUtf16() || item.label.contains(QChar(0))
			|| (!format.isUndefined() && format.toInt(-1) != 1 && format.toInt(-1) != 2) || (!mode.isUndefined() && mode.toInt(-1) != 1) || object.contains(QStringLiteral("command"))) { ++result.skipped; continue; }
		LanguageCompletionEdit primary {wordStart, cursor - wordStart, {}};
		QJsonValue editValue = object.value(QStringLiteral("textEdit"));
		const bool defaultRange = editValue.isUndefined() && defaults.contains(QStringLiteral("editRange"));
		if (defaultRange) {
			const auto shared = defaults.value(QStringLiteral("editRange")).toObject();
			QJsonObject edit = shared.contains(QStringLiteral("insert")) ? shared : QJsonObject {{QStringLiteral("range"), shared}};
			edit.insert(QStringLiteral("newText"), object.value(QStringLiteral("textEditText")).isUndefined() ? QJsonValue(item.label) : object.value(QStringLiteral("textEditText")));
			editValue = edit;
			object.insert(QStringLiteral("textEdit"), edit);
		}
		bool valid = true;
		if (!editValue.isUndefined()) {
			const auto edit = editValue.toObject();
			if (edit.contains(QStringLiteral("range"))) { valid = range(source, starts, edit.value(QStringLiteral("range")), &primary, true); }
			else {
				LanguageCompletionEdit insert;
				valid = range(source, starts, edit.value(QStringLiteral("replace")), &primary, true) && range(source, starts, edit.value(QStringLiteral("insert")), &insert, true)
					&& insert.offset == primary.offset && insert.length <= primary.length && insert.offset <= cursor && insert.offset + insert.length >= cursor;
			}
			valid = valid && primary.offset <= cursor && primary.offset + primary.length >= cursor && insertion(edit.value(QStringLiteral("newText")), &primary.text);
			item.filterable = object.contains(QStringLiteral("filterText"));
		} else { valid = insertion(object.value(QStringLiteral("insertText")).isUndefined() ? QJsonValue(item.label) : object.value(QStringLiteral("insertText")), &primary.text); }
		item.snippet = format.toInt(1) == 2; item.sourceFilePath = filePath;
		int finalOffset = primary.text.size();
		if (valid && item.snippet) {
			const auto expanded = expandCompletionSnippet(primary.text, snippetVariables);
			valid = expanded.error.isEmpty(); primary.text = expanded.text; item.tabStops = expanded.stops; finalOffset = expanded.finalOffset;
		}
		int snippetStart = primary.offset; item.edits << primary;
		const auto additional = object.value(QStringLiteral("additionalTextEdits"));
		if ((!additional.isUndefined() && !additional.isArray()) || additional.toArray().size() > editLimit) { valid = false; }
		item.caret = primary.offset + finalOffset;
		if (valid) {
			for (const auto& entry : additional.toArray()) {
				LanguageCompletionEdit edit;
				if (!range(source, starts, entry.toObject().value(QStringLiteral("range")), &edit, false) || !insertion(entry.toObject().value(QStringLiteral("newText")), &edit.text)) { valid = false; break; }
				if (edit.offset < primary.offset) { item.caret += edit.text.size() - edit.length; snippetStart += edit.text.size() - edit.length; }
				item.edits << edit;
			}
		}
		std::sort(item.edits.begin(), item.edits.end(), [](const auto& a, const auto& b) { return a.offset > b.offset; });
		if (!valid || !validateEdits(source, item.edits)) { ++result.skipped; continue; }
		for (auto& stop : item.tabStops) { stop.offset += snippetStart; }
		item.detail = object.value(QStringLiteral("detail")).toString().left(2048);
		const auto labelDetails = object.value(QStringLiteral("labelDetails")).toObject();
		const QString extra = labelDetails.value(QStringLiteral("detail")).toString().left(1024) + QLatin1Char(' ') + labelDetails.value(QStringLiteral("description")).toString().left(1024);
		if (!extra.trimmed().isEmpty()) { item.detail = extra.trimmed() + QLatin1Char('\n') + item.detail; }
		const auto documentation = object.value(QStringLiteral("documentation"));
		item.documentation = (documentation.isString() ? documentation.toString() : documentation.toObject().value(QStringLiteral("value")).toString()).left(8192);
		item.filterText = object.value(QStringLiteral("filterText")).toString(item.label).left(1024);
		item.sortText = object.value(QStringLiteral("sortText")).toString(item.label).left(1024);
		item.kind = object.value(QStringLiteral("kind")).toInt(1);
		item.deprecated = object.value(QStringLiteral("deprecated")).toBool() || object.value(QStringLiteral("tags")).toArray().contains(1);
		item.sourceSha256 = sourceHash; item.requestOffset = cursor;
		const auto wireBytes = QJsonDocument(object).toJson(QJsonDocument::Compact).size();
		if (wireBytes > 256 * 1024) { ++result.skipped; continue; }
		totalWire += wireBytes;
		if (totalWire > 8ll * 1024 * 1024) { result.limited = true; break; }
		item.wire = object; item.needsResolve = resolveSupported;
		for (const auto& edit : item.edits) { totalText += edit.text.size(); }
		totalText += item.label.size() + item.detail.size() + item.documentation.size() + item.filterText.size() + item.sortText.size();
		if (totalText > 2 * 1024 * 1024) { result.limited = true; break; }
		result.items << item;
	}
	std::stable_sort(result.items.begin(), result.items.end(), [](const auto& a, const auto& b) { return a.sortText < b.sortText; });
	return result;
}

// LSP 3.17 completionItem/resolve facts, Microsoft CC-BY-4.0 specification,
// reviewed 2026-10-04. Only the advertised lazy properties may change.
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#completionItem_resolve
LanguageCompletionItem resolveLanguageCompletion(const LanguageCompletionItem& original, const QJsonValue& response, const QString& source, QString* error)
{
	const auto fail = [&](const QString& why) { if (error) { *error = why; } return LanguageCompletionItem {}; };
	if (original.wire.isEmpty() || original.sourceSha256 != hash(source) || !boundary(source, original.requestOffset)) {
		return fail(QCoreApplication::translate("LanguageCompletion", "The suggestion no longer matches the source. Request completions again."));
	}
	if (!response.isObject()) { return fail(QCoreApplication::translate("LanguageCompletion", "The provider returned an invalid resolved suggestion.")); }
	const auto object = response.toObject();
	const auto identity = [](QJsonObject value) {
		value.remove(QStringLiteral("detail")); value.remove(QStringLiteral("documentation")); value.remove(QStringLiteral("additionalTextEdits")); return value;
	};
	if (identity(object) != identity(original.wire)) {
		return fail(QCoreApplication::translate("LanguageCompletion", "The resolved suggestion changed its identity or primary edit. Request completions again."));
	}
	const auto prefix = QStringView(source).left(original.requestOffset);
	const auto parsed = parseLanguageCompletions(QJsonArray {object}, source, prefix.count(QLatin1Char('\n')), original.requestOffset - prefix.lastIndexOf(QLatin1Char('\n')) - 1, false, original.sourceFilePath);
	if (!parsed.error.isEmpty() || parsed.items.size() != 1 || parsed.skipped || parsed.limited) {
		return fail(QCoreApplication::translate("LanguageCompletion", "The resolved suggestion contains unsupported, overlapping or excessive edits."));
	}
	if (error) { error->clear(); } return parsed.items.front();
}

bool previewLanguageCompletion(const QString& source, const LanguageCompletionItem& item, QString* text, int* caret, QString* error)
{
	if (item.needsResolve) { if (error) { *error = QCoreApplication::translate("LanguageCompletion", "Resolve this suggestion before applying its edits."); } return false; }
	if (source.size() > textDocumentByteLimit || item.sourceSha256 != hash(source) || !validateEdits(source, item.edits)) {
		if (error) { *error = QCoreApplication::translate("LanguageCompletion", "The completion no longer matches this document, or its edits are invalid."); } return false;
	}
	QString result = source;
	for (const auto& edit : item.edits) { result.replace(edit.offset, edit.length, edit.text); }
	if (result.toUtf8().size() > textDocumentByteLimit || !boundary(result, item.caret) || !validCompletionSnippetStops(result, item.tabStops)) {
		if (error) { *error = QCoreApplication::translate("LanguageCompletion", "The completion exceeds the document limit or has an invalid caret location."); } return false;
	}
	if (text) { *text = result; } if (caret) { *caret = item.caret; } if (error) { error->clear(); } return true;
}

} // namespace vibestudio
