#include "core/language_signature.h"
#include "core/language_completion.h"
#include "core/text_document.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {
namespace {
bool integer(const QJsonValue& value, int* target)
{
	const double number = value.toDouble(-1);
	if (!value.isDouble() || !std::isfinite(number) || number < 0 || number >= std::numeric_limits<int>::max() || std::floor(number) != number) { return false; }
	*target = int(number); return true;
}
bool boundary(const QString& text, int at)
{
	return at >= 0 && at <= text.size() && (at == 0 || at == text.size() || !(text[at - 1].isHighSurrogate() && text[at].isLowSurrogate()));
}
bool validText(const QString& text)
{
	if (!text.isValidUtf16()) { return false; }
	for (const auto ch : text) { if (ch.unicode() < 0x20 && ch != QLatin1Char('\n') && ch != QLatin1Char('\r') && ch != QLatin1Char('\t')) { return false; } }
	return true;
}
LanguageHoverPart documentation(const QJsonValue& value, qsizetype* remaining, LanguageSignatureHelp* report)
{
	LanguageHoverPart part;
	if (value.isUndefined()) { return part; }
	if (value.isString()) { part.kind = QStringLiteral("plaintext"); part.text = value.toString(); }
	else if (value.isObject()) {
		const auto object = value.toObject(); part.kind = object.value(QStringLiteral("kind")).toString(); part.text = object.value(QStringLiteral("value")).toString();
		if ((part.kind != QStringLiteral("plaintext") && part.kind != QStringLiteral("markdown")) || !object.value(QStringLiteral("value")).isString()) { ++report->skipped; return {}; }
	} else { ++report->skipped; return {}; }
	if (!validText(part.text)) { ++report->skipped; return {}; }
	part.text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")); part.text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
	const qsizetype limit = std::min<qsizetype>(8192, *remaining);
	if (part.text.size() > limit) {
		report->limited = true; qsizetype size = limit;
		if (size > 0 && part.text[size - 1].isHighSurrogate()) { --size; } part.text.truncate(size);
	}
	*remaining -= part.text.size(); return part;
}
QJsonObject markup(const LanguageHoverPart& part) { return {{QStringLiteral("kind"), part.kind}, {QStringLiteral("value"), part.text}}; }
}

LanguageSignatureHelp parseLanguageSignatureHelp(const QJsonValue& response, const QString& source, int line, int character)
{
	LanguageSignatureHelp result; result.requestOffset = languageSourceOffset(source, line, character);
	if (result.requestOffset < 0 || source.size() > textDocumentByteLimit || !source.isValidUtf16()) {
		result.error = QCoreApplication::translate("LanguageSignature", "Parameter hints require a valid source position."); return result;
	}
	const auto sourceBytes = source.toUtf8();
	if (sourceBytes.size() > textDocumentByteLimit) {
		result.error = QCoreApplication::translate("LanguageSignature", "The source exceeds the parameter hints size limit."); return result;
	}
	result.sourceSha256 = QCryptographicHash::hash(sourceBytes, QCryptographicHash::Sha256);
	if (response.isNull()) { return result; }
	const auto object = response.toObject();
	if (!response.isObject() || !object.value(QStringLiteral("signatures")).isArray()) {
		result.error = QCoreApplication::translate("LanguageSignature", "The language server returned invalid parameter hints."); return result;
	}
	const auto signatures = object.value(QStringLiteral("signatures")).toArray();
	int active = 0; integer(object.value(QStringLiteral("activeSignature")), &active);
	if (active >= signatures.size()) { active = 0; }
	qsizetype remaining = 65536;
	for (int index = 0; index < signatures.size(); ++index) {
		if (index >= 32) { result.limited = true; break; }
		const auto entry = signatures[index].toObject(); LanguageSignatureInformation signature;
		signature.label = entry.value(QStringLiteral("label")).toString();
		if (signature.label.isEmpty() || signature.label.size() > 8192 || !validText(signature.label)) { ++result.skipped; continue; }
		qsizetype budget = remaining - signature.label.size();
		if (budget < 0) { result.limited = true; break; }
		signature.documentation = documentation(entry.value(QStringLiteral("documentation")), &budget, &result);
		const auto params = entry.value(QStringLiteral("parameters"));
		if ((!params.isUndefined() && !params.isArray()) || params.toArray().size() > 128) { ++result.skipped; continue; }
		bool valid = true; int previousEnd = 0;
		for (const auto& parameterValue : params.toArray()) {
			const auto parameterObject = parameterValue.toObject(); const auto label = parameterObject.value(QStringLiteral("label"));
			LanguageSignatureParameter parameter; int end = 0;
			if (label.isString()) {
				parameter.label = label.toString(); parameter.offset = signature.label.indexOf(parameter.label, previousEnd);
				if (parameter.offset < 0) { parameter.offset = signature.label.indexOf(parameter.label); }
				end = parameter.offset + parameter.label.size();
			} else if (label.isArray() && label.toArray().size() == 2 && integer(label.toArray()[0], &parameter.offset) && integer(label.toArray()[1], &end)) {
				if (end >= parameter.offset && end <= signature.label.size()) { parameter.label = signature.label.mid(parameter.offset, end - parameter.offset); }
			} else { valid = false; break; }
			if (!boundary(signature.label, parameter.offset) || !boundary(signature.label, end) || end < parameter.offset) { valid = false; break; }
			parameter.length = end - parameter.offset; previousEnd = end;
			budget -= parameter.label.size(); if (budget < 0) { result.limited = true; valid = false; break; }
			parameter.documentation = documentation(parameterObject.value(QStringLiteral("documentation")), &budget, &result);
			signature.parameters << std::move(parameter);
		}
		if (!valid) { ++result.skipped; continue; }
		if (!signature.parameters.isEmpty()) {
			int parameter = 0;
			integer(entry.contains(QStringLiteral("activeParameter")) ? entry.value(QStringLiteral("activeParameter")) : object.value(QStringLiteral("activeParameter")), &parameter);
			signature.activeParameter = parameter < signature.parameters.size() ? parameter : 0;
		}
		if (index == active) { result.activeSignature = result.signatures.size(); }
		remaining = budget; result.signatures << std::move(signature);
	}
	if (result.activeSignature < 0 && !result.signatures.isEmpty()) { result.activeSignature = 0; }
	return result;
}

QJsonObject languageSignatureContext(const LanguageSignatureHelp& report)
{
	QJsonArray signatures;
	for (const auto& signature : report.signatures) {
		QJsonObject entry {{QStringLiteral("label"), signature.label}}; QJsonArray parameters;
		if (!signature.documentation.text.isEmpty()) { entry.insert(QStringLiteral("documentation"), markup(signature.documentation)); }
		for (const auto& parameter : signature.parameters) {
			QJsonObject value {{QStringLiteral("label"), QJsonArray {parameter.offset, parameter.offset + parameter.length}}};
			if (!parameter.documentation.text.isEmpty()) { value.insert(QStringLiteral("documentation"), markup(parameter.documentation)); } parameters << value;
		}
		entry.insert(QStringLiteral("parameters"), parameters);
		if (signature.activeParameter >= 0) { entry.insert(QStringLiteral("activeParameter"), signature.activeParameter); } signatures << entry;
	}
	QJsonObject result {{QStringLiteral("signatures"), signatures}};
	if (report.activeSignature >= 0 && report.activeSignature < report.signatures.size()) {
		result.insert(QStringLiteral("activeSignature"), report.activeSignature);
		const int parameter = report.signatures[report.activeSignature].activeParameter;
		if (parameter >= 0) { result.insert(QStringLiteral("activeParameter"), parameter); }
	}
	return result;
}

} // namespace vibestudio
