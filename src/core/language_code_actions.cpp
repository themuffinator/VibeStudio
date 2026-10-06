#include "core/language_code_actions.h"
#include "core/text_document.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>

namespace vibestudio {
namespace {
bool validText(const QString& text, int limit)
{
	return !text.trimmed().isEmpty() && text.size() <= limit && text.isValidUtf16() && !text.contains(QChar(0));
}
}

bool validLanguageCodeActionRange(const QString& source, int offset, int length)
{
	const auto boundary = [&](qint64 at) { return at >= 0 && at <= source.size()
		&& !(at > 0 && at < source.size() && source[at - 1].isHighSurrogate() && source[at].isLowSurrogate()); };
	return source.size() <= textDocumentByteLimit && length >= 0 && boundary(offset) && boundary(qint64(offset) + length);
}

// Original code-action literal/resolve handling based on Microsoft's LSP 3.17
// specification (CC-BY-4.0), reviewed 2026-10-04. No upstream code copied.
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_codeAction
LanguageCodeActions parseLanguageCodeActions(const QJsonValue& reply, bool resolveSupported)
{
	LanguageCodeActions result;
	if (reply.isNull()) { return result; }
	if (!reply.isArray()) { result.error = QCoreApplication::translate("LanguageCodeActions", "The language server returned an invalid code action list."); return result; }
	qint64 bytes = 0;
	for (const auto& value : reply.toArray()) {
		if (result.items.size() >= 200) { result.limited = true; break; }
		const auto object = value.toObject(); LanguageCodeAction action;
		action.title = object.value(QStringLiteral("title")).toString(); action.kind = object.value(QStringLiteral("kind")).toString();
		const auto size = QJsonDocument(object).toJson(QJsonDocument::Compact).size();
		bytes += size;
		if (bytes > 8ll * 1024 * 1024) { result.limited = true; break; }
		if (!validText(action.title, 512) || action.kind.size() > 128 || !action.kind.isValidUtf16() || action.kind.contains(QChar(0)) || size > 2ll * 1024 * 1024
			|| (object.contains(QStringLiteral("kind")) && !object.value(QStringLiteral("kind")).isString())
			|| (object.contains(QStringLiteral("isPreferred")) && !object.value(QStringLiteral("isPreferred")).isBool())) { ++result.skipped; continue; }
		action.wire = object; action.preferred = object.value(QStringLiteral("isPreferred")).toBool();
		if (object.contains(QStringLiteral("disabled"))) {
			action.disabledReason = object.value(QStringLiteral("disabled")).toObject().value(QStringLiteral("reason")).toString();
			if (!validText(action.disabledReason, 1024)) { action.disabledReason = QCoreApplication::translate("LanguageCodeActions", "The provider marked this action unavailable."); }
		}
		if (object.contains(QStringLiteral("command"))) {
			action.disabledReason = QCoreApplication::translate("LanguageCodeActions", "This action requires a server command, which this client does not execute.");
		} else if (object.contains(QStringLiteral("edit")) && !object.value(QStringLiteral("edit")).isObject()) {
			action.disabledReason = QCoreApplication::translate("LanguageCodeActions", "The action contains an invalid workspace edit.");
		} else if (!object.contains(QStringLiteral("edit"))) {
			action.needsResolve = resolveSupported;
			if (!resolveSupported && action.disabledReason.isEmpty()) { action.disabledReason = QCoreApplication::translate("LanguageCodeActions", "The action has no text edits and the provider cannot resolve it."); }
		}
		result.items << action;
	}
	return result;
}

LanguageCodeAction resolveLanguageCodeAction(const LanguageCodeAction& original, const QJsonValue& reply)
{
	const auto parsed = parseLanguageCodeActions(QJsonArray {reply}, false);
	LanguageCodeAction invalid = original; invalid.needsResolve = false;
	invalid.disabledReason = QCoreApplication::translate("LanguageCodeActions", "The resolved action changed its identity or contains invalid edits. Request code actions again.");
	if (!reply.isObject() || parsed.items.size() != 1 || parsed.skipped || parsed.limited || !parsed.error.isEmpty()) { return invalid; }
	const auto resolved = reply.toObject();
	for (auto it = original.wire.begin(); it != original.wire.end(); ++it) {
		if (it.key() != QStringLiteral("edit") && resolved.value(it.key()) != it.value()) { return invalid; }
	}
	return parsed.items.front();
}

} // namespace vibestudio
