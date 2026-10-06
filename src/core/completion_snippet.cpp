#include "core/completion_snippet.h"
#include "core/language_completion.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

namespace vibestudio {
namespace {
constexpr int textLimit = 65536;
struct Node {
	QString text, variable;
	int number = -1;
	bool hasDefault = false;
	QVector<Node> children;
	QStringList choices;
};
bool nameStart(QChar c) { return c == QLatin1Char('_') || (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) || (c >= QLatin1Char('a') && c <= QLatin1Char('z')); }
bool digit(QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('9'); }
bool boundary(const QString& text, int at) { return at >= 0 && at <= text.size() && (at == 0 || at == text.size() || !(text[at - 1].isHighSurrogate() && text[at].isLowSurrogate())); }
struct Parser {
	const QString& body;
	int at = 0, count = 0, maximum = 0;
	bool valid = true, transform = false;
	QVector<Node> sequence(int depth, bool nested = false)
	{
		QVector<Node> nodes; QString literal;
		const auto flush = [&]() { if (!literal.isEmpty()) { Node node; node.text = literal; nodes << node; literal.clear(); ++count; } };
		if (depth > 16) { valid = false; return {}; }
		while (at < body.size() && valid) {
			const QChar ch = body[at++];
			if (ch == QLatin1Char('}') && nested) { flush(); return nodes; }
			if (ch == QLatin1Char('\\') && at < body.size() && QStringLiteral("$}\\").contains(body[at])) { literal += body[at++]; continue; }
			if (ch != QLatin1Char('$') || at == body.size() || (body[at] != QLatin1Char('{') && !digit(body[at]) && !nameStart(body[at]))) { literal += ch; continue; }
			flush(); if (++count > 256) { valid = false; break; }
			Node node; const bool braced = body[at] == QLatin1Char('{'); if (braced) { ++at; }
			if (at >= body.size()) { valid = false; break; }
			if (digit(body[at])) {
				qint64 number = 0;
				while (at < body.size() && digit(body[at])) { number = number * 10 + body[at++].digitValue(); if (number > 1000000) { valid = false; break; } }
				node.number = int(number); maximum = std::max(maximum, node.number);
			} else if (nameStart(body[at])) {
				while (at < body.size() && (nameStart(body[at]) || digit(body[at]))) { node.variable += body[at++]; }
			} else { valid = false; break; }
			if (braced && valid) {
				if (at >= body.size()) { valid = false; break; }
				const auto next = body[at++];
				if (next == QLatin1Char(':')) { node.hasDefault = true; node.children = sequence(depth + 1, true); }
				else if (next == QLatin1Char('|') && node.number >= 0) {
					node.hasDefault = true; QString choice; bool closed = false;
					while (at < body.size()) {
						const auto c = body[at++];
						if (c == QLatin1Char('\\') && at < body.size() && QStringLiteral("$}\\,|").contains(body[at])) { choice += body[at++]; }
						else if (c == QLatin1Char(',')) { node.choices << choice; choice.clear(); }
						else if (c == QLatin1Char('|') && at < body.size() && body[at] == QLatin1Char('}')) { ++at; node.choices << choice; closed = true; break; }
						else { choice += c; }
						if (node.choices.size() > 32) { break; }
					}
					if (!closed || node.choices.size() > 32) { valid = false; }
				} else if (next != QLatin1Char('}')) { transform = next == QLatin1Char('/'); valid = false; }
			}
			nodes << node;
		}
		flush(); if (nested) { valid = false; } return nodes;
	}
};
QVector<Node> variablesResolved(const QVector<Node>& nodes, const QHash<QString, QString>& variables, int* nextUnknown, QHash<QString, int>* unknown)
{
	QVector<Node> result;
	for (auto node : nodes) {
		if (!node.variable.isEmpty()) {
			const auto found = variables.constFind(node.variable);
			if (found != variables.cend() && !found->isEmpty()) { node.text = *found; node.variable.clear(); node.children.clear(); }
			else if (node.hasDefault) { result += variablesResolved(node.children, variables, nextUnknown, unknown); continue; }
			else if (found != variables.cend()) { continue; }
			else {
				if (!unknown->contains(node.variable)) { unknown->insert(node.variable, ++*nextUnknown); }
				node.number = unknown->value(node.variable); node.hasDefault = true;
				Node value; value.text = node.variable; node.children = {value}; node.variable.clear();
			}
		}
		node.children = variablesResolved(node.children, variables, nextUnknown, unknown); result << node;
	}
	return result;
}
void definitions(const QVector<Node>& nodes, QHash<int, const Node*>* found)
{
	for (const auto& node : nodes) {
		if (node.number >= 0 && (!found->contains(node.number) || (!found->value(node.number)->hasDefault && node.hasDefault))) { found->insert(node.number, &node); }
		definitions(node.children, found);
	}
}
struct Renderer {
	CompletionSnippet result;
	QHash<int, const Node*> definitions;
	QSet<int> expanding;
	bool valid = true;
	void render(const QVector<Node>& nodes, int parent = -1)
	{
		for (const auto& node : nodes) {
			if (!valid) { return; }
			if (node.number < 0) { result.text += node.text; }
			else {
				if (expanding.contains(node.number) || result.stops.size() >= 128) { valid = false; return; }
				const auto* value = definitions.value(node.number, &node); const int index = result.stops.size();
				result.stops << CompletionSnippetStop {node.number, int(result.text.size()), 0, parent, value->choices};
				expanding.insert(node.number);
				if (!value->choices.isEmpty()) { result.text += value->choices.front(); } else { render(value->children, index); }
				expanding.remove(node.number); result.stops[index].length = result.text.size() - result.stops[index].offset;
			}
			if (result.text.size() > textLimit) { valid = false; return; }
		}
	}
};
}

CompletionSnippet expandCompletionSnippet(const QString& body, const QHash<QString, QString>& variables)
{
	CompletionSnippet result;
	if (body.size() > textLimit || !body.isValidUtf16() || body.contains(QChar(0))) {
		result.error = QCoreApplication::translate("CompletionSnippet", "The completion snippet is invalid or exceeds its size limit."); return result;
	}
	Parser parser {body}; const auto parsed = parser.sequence(0);
	if (!parser.valid || parser.count > 256) {
		result.error = parser.transform ? QCoreApplication::translate("CompletionSnippet", "Completion snippet transforms are not supported.")
			: QCoreApplication::translate("CompletionSnippet", "The completion snippet syntax or nesting is invalid."); return result;
	}
	QHash<QString, int> unknown; auto resolved = variablesResolved(parsed, variables, &parser.maximum, &unknown);
	Renderer renderer; definitions(resolved, &renderer.definitions); renderer.render(resolved);
	if (!renderer.valid || !renderer.result.text.isValidUtf16() || renderer.result.text.contains(QChar(0)) || !validCompletionSnippetStops(renderer.result.text, renderer.result.stops)) {
		result.error = QCoreApplication::translate("CompletionSnippet", "The completion snippet has cyclic placeholders, invalid ranges or excessive expansion."); return result;
	}
	result = renderer.result; result.finalOffset = result.text.size();
	for (const auto& stop : result.stops) { if (stop.number == 0) { result.finalOffset = stop.offset; break; } }
	return result;
}

bool validCompletionSnippetStops(const QString& text, const QVector<CompletionSnippetStop>& stops)
{
	if (stops.size() > 128) { return false; }
	for (int i = 0; i < stops.size(); ++i) {
		const auto& stop = stops[i];
		if (stop.number < 0 || stop.length < 0 || qint64(stop.offset) + stop.length > text.size() || !boundary(text, stop.offset)
			|| !boundary(text, stop.offset + stop.length) || stop.parent < -1 || stop.parent >= i || stop.choices.size() > 32) { return false; }
		if (stop.parent >= 0) { const auto& parent = stops[stop.parent]; if (stop.offset < parent.offset || stop.offset + stop.length > parent.offset + parent.length) { return false; } }
		for (int j = 0; j < i; ++j) {
			const auto& other = stops[j];
			if (stop.number == other.number && text.mid(stop.offset, stop.length) != text.mid(other.offset, other.length)) { return false; }
			bool ancestor = false; for (int p = stop.parent; p >= 0; p = stops[p].parent) { if (p == j) { ancestor = true; break; } }
			if (stop.offset < other.offset + other.length && other.offset < stop.offset + stop.length && !ancestor) { return false; }
		}
	}
	return true;
}

QHash<QString, QString> completionSnippetVariables(const QString& source, int caret, const QString& filePath)
{
	if (!boundary(source, caret)) { return {}; }
	const auto prefix = QStringView(source).left(caret); const int line = prefix.count(QLatin1Char('\n'));
	const int start = prefix.lastIndexOf(QLatin1Char('\n')) + 1, newline = source.indexOf(QLatin1Char('\n'), caret);
	int wordEnd = caret;
	while (wordEnd < source.size()) {
		char32_t character = source[wordEnd].unicode(); const int units = source[wordEnd].isHighSurrogate() && wordEnd + 1 < source.size() && source[wordEnd + 1].isLowSurrogate() ? 2 : 1;
		if (units == 2) { character = QChar::surrogateToUcs4(source[wordEnd], source[wordEnd + 1]); }
		const auto category = QChar::category(character);
		if (!QChar::isLetterOrNumber(character) && character != '_' && category != QChar::Mark_NonSpacing && category != QChar::Mark_SpacingCombining && category != QChar::Mark_Enclosing) { break; }
		wordEnd += units;
	}
	const int wordStart = languageWordStart(source, caret); const QFileInfo file(filePath);
	return {{QStringLiteral("TM_SELECTED_TEXT"), {}}, {QStringLiteral("TM_CURRENT_LINE"), source.mid(start, (newline < 0 ? source.size() : newline) - start)},
		{QStringLiteral("TM_CURRENT_WORD"), source.mid(wordStart, wordEnd - wordStart)}, {QStringLiteral("TM_LINE_INDEX"), QString::number(line)},
		{QStringLiteral("TM_LINE_NUMBER"), QString::number(line + 1)}, {QStringLiteral("TM_FILENAME"), filePath.isEmpty() ? QString() : file.fileName()},
		{QStringLiteral("TM_FILENAME_BASE"), filePath.isEmpty() ? QString() : file.completeBaseName()},
		{QStringLiteral("TM_DIRECTORY"), filePath.isEmpty() ? QString() : file.path()}, {QStringLiteral("TM_FILEPATH"), filePath}};
}

} // namespace vibestudio
