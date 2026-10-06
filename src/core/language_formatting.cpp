#include "core/language_formatting.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {
namespace {
constexpr qsizetype editLimit = 10000;
QByteArray hash(const QString& text) { return QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256); }
bool boundary(const QString& source, qsizetype offset)
{
	return offset >= 0 && offset <= source.size()
		&& !(offset > 0 && offset < source.size() && source[offset - 1].isHighSurrogate() && source[offset].isLowSurrogate());
}
QString invalid() { return QCoreApplication::translate("LanguageFormatting", "The formatting reply contains invalid, overlapping or excessive edits. No edits were applied."); }
bool integer(const QJsonValue& value, int* target)
{
	const double n = value.toDouble(-1);
	if (!value.isDouble() || !std::isfinite(n) || n < 0 || n >= std::numeric_limits<int>::max() || std::floor(n) != n) { return false; }
	*target = int(n); return true;
}
}

bool validLanguageFormattingOptions(const QString& source, const LanguageFormattingOptions& options)
{
	return options.tabSize >= 1 && options.tabSize <= 16 && source.size() <= textDocumentByteLimit
		&& source.isValidUtf16() && !source.contains(QChar(0))
		&& ((options.rangeOffset == -1 && options.rangeLength == 0)
			|| (options.rangeLength > 0 && boundary(source, options.rangeOffset)
				&& boundary(source, qint64(options.rangeOffset) + options.rangeLength)));
}

bool previewLanguageFormatting(const QString& source, const LanguageFormatting& report, QString* text, QString* error)
{
	if (text) { text->clear(); }
	const auto fail = [&](const QString& reason) { if (error) { *error = reason; } return false; };
	if (!report.error.isEmpty()) { return fail(report.error); }
	if (!validLanguageFormattingOptions(source, {}) || report.sourceSha256 != hash(source)) {
		return fail(QCoreApplication::translate("LanguageFormatting", "The formatting no longer matches this document. Request formatting again."));
	}
	if (report.edits.size() > editLimit) { return fail(invalid()); }
	qsizetype end = 0, previousStart = -1; qint64 inserted = 0, size = source.size();
	for (const auto& edit : report.edits) {
		if (edit.length < 0 || edit.length > source.size() || edit.offset < end || edit.offset <= previousStart
			|| !boundary(source, edit.offset) || edit.offset > source.size() - edit.length || !boundary(source, edit.offset + edit.length)
			|| !edit.replacement.isValidUtf16() || edit.replacement.contains(QChar(0)) || edit.replacement.contains(QLatin1Char('\r')) || edit.replacement.contains(QChar(0x2029))) { return fail(invalid()); }
		previousStart = edit.offset; end = edit.offset + edit.length;
		inserted += edit.replacement.size(); size += edit.replacement.size() - edit.length;
		if (inserted > textDocumentByteLimit) { return fail(invalid()); }
	}
	if (size > textDocumentByteLimit) { return fail(invalid()); }
	QString result; result.reserve(size); end = 0;
	for (const auto& edit : report.edits) {
		result.append(QStringView(source).mid(end, edit.offset - end)); result += edit.replacement; end = edit.offset + edit.length;
	}
	result.append(QStringView(source).mid(end));
	if (result.toUtf8().size() > textDocumentByteLimit) { return fail(invalid()); }
	if (text) { *text = std::move(result); } if (error) { error->clear(); } return true;
}

LanguageFormatting parseLanguageFormatting(const QJsonValue& response, const QString& source)
{
	LanguageFormatting report; report.sourceSha256 = hash(source);
	const auto fail = [&]() { report.edits.clear(); report.error = invalid(); return report; };
	if (!validLanguageFormattingOptions(source, {}) || source.toUtf8().size() > textDocumentByteLimit) { return fail(); }
	if (response.isNull()) { return report; }
	if (!response.isArray() || response.toArray().size() > editLimit) { return fail(); }
	QVector<int> starts {0};
	for (qsizetype at = source.indexOf(QLatin1Char('\n')); at >= 0; at = source.indexOf(QLatin1Char('\n'), at + 1)) { starts << int(at) + 1; }
	const auto offset = [&](const QJsonValue& value) -> int {
		const auto object = value.toObject(); int line = 0, character = 0;
		if (!integer(object.value(QStringLiteral("line")), &line) || !integer(object.value(QStringLiteral("character")), &character) || line >= starts.size()) { return -1; }
		const int end = line + 1 < starts.size() ? starts[line + 1] - 1 : int(source.size());
		return character <= end - starts[line] && boundary(source, starts[line] + character) ? starts[line] + character : -1;
	};
	QVector<TextFileEdit> edits; qint64 inserted = 0;
	for (const auto& value : response.toArray()) {
		const auto object = value.toObject(), range = object.value(QStringLiteral("range")).toObject();
		const int first = offset(range.value(QStringLiteral("start"))), last = offset(range.value(QStringLiteral("end")));
		const auto replacement = object.value(QStringLiteral("newText"));
		if (first < 0 || last < first || !replacement.isString() || object.contains(QStringLiteral("annotationId"))) { return fail(); }
		QString text = replacement.toString(); inserted += text.size();
		if (inserted > textDocumentByteLimit || !text.isValidUtf16() || text.contains(QChar(0))) { return fail(); }
		text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")); text.replace(QLatin1Char('\r'), QLatin1Char('\n')); text.replace(QChar(0x2029), QLatin1Char('\n'));
		edits << TextFileEdit {first, last - first, std::move(text)};
	}
	std::stable_sort(edits.begin(), edits.end(), [](const auto& a, const auto& b) { return a.offset < b.offset; });
	for (const auto& edit : edits) {
		if (!report.edits.isEmpty() && report.edits.back().offset == edit.offset) {
			if (report.edits.back().length != 0) { return fail(); }
			report.edits.back().length = edit.length; report.edits.back().replacement += edit.replacement;
		} else { report.edits << edit; }
	}
	QString formatted;
	if (!previewLanguageFormatting(source, report, &formatted, &report.error)) { report.edits.clear(); return report; }
	if (formatted == source) { report.edits.clear(); return report; }
	report.edits.removeIf([&](const auto& edit) { return QStringView(source).mid(edit.offset, edit.length) == edit.replacement; });
	return report;
}

int languageFormattedOffset(const LanguageFormatting& report, const QString& formatted, int offset)
{
	qint64 shift = 0, mapped = offset;
	for (const auto& edit : report.edits) {
		if (offset < edit.offset) { break; }
		if (offset < edit.offset + edit.length) { mapped = edit.offset + shift + std::min<qsizetype>(offset - edit.offset, edit.replacement.size()); shift = 0; break; }
		shift += edit.replacement.size() - edit.length;
	}
	mapped += shift;
	int result = int(std::clamp<qint64>(mapped, 0, formatted.size()));
	if (!boundary(formatted, result)) { --result; } return result;
}

} // namespace vibestudio
