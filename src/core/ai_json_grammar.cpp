#include "core/ai_json_grammar.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {

namespace {

// Integer ranges no wider than this are written out value by value.
constexpr qint64 kEnumeratedIntegerSpan = 256;
// llama.cpp expands x{m,n} into a rule per repetition, lifts counts above 2000, and refuses a
// grammar with too many rules; past these bounds the grammar leaves the length open instead, and
// the answer's own validation keeps the schema's limit.
constexpr int kBoundedStringLength = 2000;
constexpr int kBoundedArrayItems = 256;

class GrammarBuilder {
public:
	QString build(const QJsonObject& schema)
	{
		const QString root = rule(schema);
		QStringList lines;
		lines << QStringLiteral("root ::= ws %1 ws").arg(root);
		lines += m_rules;
		lines << QStringLiteral("ws ::= [ \\t\\n\\r]{0,16}");
		if (m_used.contains(QStringLiteral("string"))) {
			lines << QStringLiteral("string ::= \"\\\"\" char* \"\\\"\"");
		}
		if (m_used.contains(QStringLiteral("string")) || m_used.contains(QStringLiteral("char"))) {
			lines << QStringLiteral("char ::= [^\"\\\\\\x7F\\x00-\\x1F] | \"\\\\\" ([\"\\\\/bfnrt] | \"u\" [0-9a-fA-F]{4})");
		}
		if (m_used.contains(QStringLiteral("integer")) || m_used.contains(QStringLiteral("number"))) {
			lines << QStringLiteral("integer ::= \"-\"? ([0-9] | [1-9] [0-9]{1,15})");
		}
		if (m_used.contains(QStringLiteral("number"))) {
			lines << QStringLiteral("number ::= integer (\".\" [0-9]{1,16})? ([eE] [-+]? [0-9]{1,4})?");
		}
		if (m_used.contains(QStringLiteral("boolean"))) {
			lines << QStringLiteral("boolean ::= \"true\" | \"false\"");
		}
		if (m_used.contains(QStringLiteral("value"))) {
			// Any JSON value, for a schema the subset does not describe.
			lines << QStringLiteral("value ::= object | array | string | number | boolean | \"null\"");
			lines << QStringLiteral("object ::= \"{\" ws (string ws \":\" ws value (ws \",\" ws string ws \":\" ws value)*)? ws \"}\"");
			lines << QStringLiteral("array ::= \"[\" ws (value (ws \",\" ws value)*)? ws \"]\"");
			if (!m_used.contains(QStringLiteral("number"))) {
				lines << QStringLiteral("number ::= integer (\".\" [0-9]{1,16})? ([eE] [-+]? [0-9]{1,4})?");
			}
			if (!m_used.contains(QStringLiteral("integer")) && !m_used.contains(QStringLiteral("number"))) {
				lines << QStringLiteral("integer ::= \"-\"? ([0-9] | [1-9] [0-9]{1,15})");
			}
			if (!m_used.contains(QStringLiteral("boolean"))) {
				lines << QStringLiteral("boolean ::= \"true\" | \"false\"");
			}
			if (!m_used.contains(QStringLiteral("string"))) {
				lines << QStringLiteral("string ::= \"\\\"\" char* \"\\\"\"");
				if (!m_used.contains(QStringLiteral("char"))) {
					lines << QStringLiteral("char ::= [^\"\\\\\\x7F\\x00-\\x1F] | \"\\\\\" ([\"\\\\/bfnrt] | \"u\" [0-9a-fA-F]{4})");
				}
			}
		}
		return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
	}

private:
	// A GBNF literal for the given text.
	static QString literal(const QString& text)
	{
		QString escaped;
		for (const QChar character : text) {
			const ushort code = character.unicode();
			if (character == QLatin1Char('"') || character == QLatin1Char('\\')) {
				escaped += QLatin1Char('\\');
				escaped += character;
			} else if (character == QLatin1Char('\n')) {
				escaped += QStringLiteral("\\n");
			} else if (character == QLatin1Char('\r')) {
				escaped += QStringLiteral("\\r");
			} else if (character == QLatin1Char('\t')) {
				escaped += QStringLiteral("\\t");
			} else if (code < 0x20) {
				escaped += QStringLiteral("\\x%1").arg(code, 2, 16, QLatin1Char('0'));
			} else {
				escaped += character;
			}
		}
		return QStringLiteral("\"%1\"").arg(escaped);
	}

	// The JSON text of a value, as a GBNF literal.
	static QString jsonLiteral(const QJsonValue& value)
	{
		QString json;
		if (value.isString()) {
			json = QString::fromUtf8(QJsonDocument(QJsonArray {value}).toJson(QJsonDocument::Compact));
			json = json.mid(1, json.size() - 2);
		} else if (value.isBool()) {
			json = value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
		} else if (value.isNull()) {
			json = QStringLiteral("null");
		} else if (value.isDouble()) {
			const double number = value.toDouble();
			json = std::floor(number) == number && std::abs(number) < 1e15 ? QString::number(qint64(number)) : QString::number(number, 'g', 17);
		} else {
			json = QString::fromUtf8(QJsonDocument(QJsonArray {value}).toJson(QJsonDocument::Compact));
			json = json.mid(1, json.size() - 2);
		}
		return literal(json);
	}

	QString use(const QString& name)
	{
		m_used.insert(name);
		return name;
	}

	QString define(const QString& body)
	{
		const QString name = QStringLiteral("r%1").arg(++m_count);
		m_rules << QStringLiteral("%1 ::= %2").arg(name, body);
		return name;
	}

	QString alternatives(const QStringList& bodies)
	{
		if (bodies.isEmpty()) {
			return use(QStringLiteral("value"));
		}
		return bodies.size() == 1 ? bodies.first() : define(bodies.join(QStringLiteral(" | ")));
	}

	QString typed(const QString& type, const QJsonObject& schema)
	{
		if (type == QStringLiteral("object")) {
			return objectRule(schema);
		}
		if (type == QStringLiteral("array")) {
			return arrayRule(schema);
		}
		if (type == QStringLiteral("string")) {
			if (schema.contains(QStringLiteral("maxLength")) && schema.value(QStringLiteral("maxLength")).toInt(-1) >= 0
				&& schema.value(QStringLiteral("maxLength")).toInt() <= kBoundedStringLength) {
				use(QStringLiteral("char"));
				return define(QStringLiteral("\"\\\"\" char{0,%1} \"\\\"\"").arg(schema.value(QStringLiteral("maxLength")).toInt()));
			}
			return use(QStringLiteral("string"));
		}
		if (type == QStringLiteral("integer")) {
			const double lowBound = schema.value(QStringLiteral("minimum")).toDouble(std::nan(""));
			const double highBound = schema.value(QStringLiteral("maximum")).toDouble(std::nan(""));
			// Only bounds a 64-bit integer holds, so the conversion below is defined.
			constexpr double kLargest = 9.0e15;
			if (std::isfinite(lowBound) && std::isfinite(highBound) && std::abs(lowBound) < kLargest && std::abs(highBound) < kLargest) {
				const qint64 low = qint64(std::ceil(lowBound));
				const qint64 high = qint64(std::floor(highBound));
				if (low <= high && high - low <= kEnumeratedIntegerSpan) {
					QStringList values;
					for (qint64 value = low; value <= high; ++value) {
						values << literal(QString::number(value));
					}
					return define(values.join(QStringLiteral(" | ")));
				}
			}
			return use(QStringLiteral("integer"));
		}
		if (type == QStringLiteral("number")) {
			return use(QStringLiteral("number"));
		}
		if (type == QStringLiteral("boolean")) {
			return use(QStringLiteral("boolean"));
		}
		if (type == QStringLiteral("null")) {
			return literal(QStringLiteral("null"));
		}
		return use(QStringLiteral("value"));
	}

	QString objectRule(const QJsonObject& schema)
	{
		const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
		if (properties.isEmpty()) {
			use(QStringLiteral("value"));
			return use(QStringLiteral("object"));
		}
		// Every property is written: the strict schemas the studio sends require them all.
		QStringList order;
		for (const QJsonValue& name : schema.value(QStringLiteral("required")).toArray()) {
			if (properties.contains(name.toString()) && !order.contains(name.toString())) {
				order << name.toString();
			}
		}
		for (const QString& name : properties.keys()) {
			if (!order.contains(name)) {
				order << name;
			}
		}
		QStringList members;
		for (const QString& name : order) {
			members << QStringLiteral("%1 ws \":\" ws %2").arg(jsonLiteral(QJsonValue(name)), rule(properties.value(name).toObject()));
		}
		return define(QStringLiteral("\"{\" ws %1 ws \"}\"").arg(members.join(QStringLiteral(" ws \",\" ws "))));
	}

	QString arrayRule(const QJsonObject& schema)
	{
		const QString item = schema.value(QStringLiteral("items")).isObject() ? rule(schema.value(QStringLiteral("items")).toObject()) : use(QStringLiteral("value"));
		const int minimum = std::clamp(schema.value(QStringLiteral("minItems")).toInt(0), 0, kBoundedArrayItems);
		const int maxItems = schema.value(QStringLiteral("maxItems")).toInt(-1);
		const bool bounded = maxItems >= 0 && maxItems <= kBoundedArrayItems;
		const int maximum = bounded ? std::max(minimum, maxItems) : -1;
		if (bounded && maximum == 0) {
			return define(QStringLiteral("\"[\" ws \"]\""));
		}
		// Joined, not passed through arg(): an item that is a literal may hold "%1".
		const QString more = QStringLiteral("(ws \",\" ws ") + item + QStringLiteral(")");
		const QString counts = bounded ? QStringLiteral("{%1,%2}").arg(std::max(0, minimum - 1)).arg(maximum - 1) : QStringLiteral("{%1,}").arg(std::max(0, minimum - 1));
		const QString items = item + QLatin1Char(' ') + more + counts;
		return define(minimum == 0 ? QStringLiteral("\"[\" ws (") + items + QStringLiteral(")? ws \"]\"") : QStringLiteral("\"[\" ws ") + items + QStringLiteral(" ws \"]\""));
	}

	QString rule(const QJsonObject& schema)
	{
		if (schema.contains(QStringLiteral("const"))) {
			return jsonLiteral(schema.value(QStringLiteral("const")));
		}
		if (schema.contains(QStringLiteral("enum"))) {
			QStringList values;
			for (const QJsonValue& value : schema.value(QStringLiteral("enum")).toArray()) {
				values << jsonLiteral(value);
			}
			return alternatives(values);
		}
		for (const QString& key : {QStringLiteral("anyOf"), QStringLiteral("oneOf")}) {
			if (schema.value(key).isArray()) {
				QStringList choices;
				for (const QJsonValue& choice : schema.value(key).toArray()) {
					choices << rule(choice.toObject());
				}
				return alternatives(choices);
			}
		}
		const QJsonValue type = schema.value(QStringLiteral("type"));
		if (type.isArray()) {
			QStringList choices;
			for (const QJsonValue& entry : type.toArray()) {
				choices << typed(entry.toString(), schema);
			}
			return alternatives(choices);
		}
		if (type.isString()) {
			return typed(type.toString(), schema);
		}
		if (schema.contains(QStringLiteral("properties"))) {
			return objectRule(schema);
		}
		if (schema.contains(QStringLiteral("items"))) {
			return arrayRule(schema);
		}
		return use(QStringLiteral("value"));
	}

	QStringList m_rules;
	QSet<QString> m_used;
	int m_count = 0;
};

} // namespace

QString aiJsonSchemaGrammar(const QJsonObject& schema, QString* error)
{
	const bool objectSchema = schema.value(QStringLiteral("type")).toString() == QStringLiteral("object") || schema.contains(QStringLiteral("properties"));
	if (!objectSchema) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioAiGguf", "Only a JSON object schema can be turned into a grammar.");
		}
		return {};
	}
	return GrammarBuilder().build(schema);
}

} // namespace vibestudio
