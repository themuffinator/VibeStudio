#include "core/language_hover.h"
#include "core/language_completion.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <limits>

namespace vibestudio {
namespace {
bool integer(const QJsonValue& value, int* target)
{
	if (!value.isDouble()) { return false; }
	const double number = value.toDouble();
	if (!std::isfinite(number) || number < 0 || number >= std::numeric_limits<int>::max() || std::floor(number) != number) { return false; }
	*target = int(number); return true;
}
bool validText(const QString& text)
{
	if (!text.isValidUtf16()) { return false; }
	for (const auto ch : text) { if (ch.unicode() < 0x20 && ch != QLatin1Char('\n') && ch != QLatin1Char('\r') && ch != QLatin1Char('\t')) { return false; } }
	return true;
}
}

LanguageHover parseLanguageHover(const QJsonValue& response, const QString& source, int line, int character)
{
	LanguageHover result; result.requestOffset = languageSourceOffset(source, line, character);
	if (result.requestOffset < 0 || !source.isValidUtf16()) { result.error = QCoreApplication::translate("LanguageHover", "Quick Info requires a valid source position."); return result; }
	result.sourceSha256 = QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256);
	if (response.isNull()) { return result; }
	if (!response.isObject() || !response.toObject().contains(QStringLiteral("contents"))) {
		result.error = QCoreApplication::translate("LanguageHover", "The language server returned invalid Quick Info."); return result;
	}
	const auto object = response.toObject(); const auto contents = object.value(QStringLiteral("contents"));
	const auto entries = contents.isArray() ? contents.toArray() : QJsonArray {contents};
	qsizetype remaining = 65536;
	for (qsizetype index = 0; index < entries.size(); ++index) {
		if (index >= 32) { result.limited = true; result.skipped += int(entries.size() - index); break; }
		const auto entry = entries[index]; LanguageHoverPart part;
		if (entry.isString()) { part.kind = QStringLiteral("markdown"); part.text = entry.toString(); }
		else if (entry.isObject()) {
			const auto value = entry.toObject();
			if (!value.value(QStringLiteral("value")).isString()) { ++result.skipped; continue; }
			part.text = value.value(QStringLiteral("value")).toString();
			if (!contents.isArray() && value.value(QStringLiteral("kind")).isString()) {
				part.kind = value.value(QStringLiteral("kind")).toString();
				if (part.kind != QStringLiteral("plaintext") && part.kind != QStringLiteral("markdown")) { ++result.skipped; continue; }
			} else if (value.value(QStringLiteral("language")).isString() && !value.contains(QStringLiteral("kind"))) {
				part.kind = QStringLiteral("code"); part.language = value.value(QStringLiteral("language")).toString();
				if (part.language.isEmpty() || part.language.size() > 128 || !validText(part.language) || part.language.contains(QLatin1Char('\n')) || part.language.contains(QLatin1Char('\r'))) { ++result.skipped; continue; }
			} else { ++result.skipped; continue; }
		} else { ++result.skipped; continue; }
		if (!validText(part.text)) { ++result.skipped; continue; }
		part.text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")); part.text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
		if (part.text.trimmed().isEmpty()) { continue; }
		if (part.text.size() > remaining) {
			result.limited = true;
			qsizetype length = remaining;
			if (length > 0 && part.text[length - 1].isHighSurrogate()) { --length; }
			part.text.truncate(length);
		}
		if (part.text.isEmpty()) { ++result.skipped; continue; }
		remaining -= part.text.size(); result.contents << std::move(part);
	}
	if (object.contains(QStringLiteral("range"))) {
		const auto range = object.value(QStringLiteral("range")).toObject();
		const auto start = range.value(QStringLiteral("start")).toObject(), end = range.value(QStringLiteral("end")).toObject();
		int firstLine = 0, firstColumn = 0, lastLine = 0, lastColumn = 0;
		if (integer(start.value(QStringLiteral("line")), &firstLine) && integer(start.value(QStringLiteral("character")), &firstColumn)
			&& integer(end.value(QStringLiteral("line")), &lastLine) && integer(end.value(QStringLiteral("character")), &lastColumn)) {
			const int first = languageSourceOffset(source, firstLine, firstColumn), last = languageSourceOffset(source, lastLine, lastColumn);
			if (first >= 0 && last > first && first <= result.requestOffset && last >= result.requestOffset) { result.hasRange = true; result.offset = first; result.length = last - first; }
		}
		if (!result.hasRange) { ++result.skipped; }
	}
	return result;
}

} // namespace vibestudio
