#include "core/language_server.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {
namespace {
bool positionNumber(const QJsonValue& value, int* number)
{
	const double candidate = value.toDouble(-1);
	if (!value.isDouble() || candidate < 0 || candidate >= std::numeric_limits<int>::max() || std::floor(candidate) != candidate) { return false; }
	*number = int(candidate); return true;
}
QString bounded(const QString& value, int limit)
{
	QString result = value.left(limit); if (!result.isEmpty() && result.back().isHighSurrogate()) { result.chop(1); } return result;
}
}

// Original implementation of LSP 3.17 push/pull diagnostic facts, including
// full/unchanged reports and refresh. Microsoft CC-BY-4.0, reviewed 2026-10-04.
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_diagnostic
LanguageDiagnostics parseLanguageDiagnostics(const QJsonValue& items, const LanguageDocument& document, int version, bool versioned, const QString& origin)
{
	LanguageDiagnostics result; result.filePath = document.filePath; result.version = version; result.versioned = versioned; result.origin = origin; result.received = true;
	if (!items.isArray()) { result.error = QCoreApplication::translate("LanguageServer", "The language server returned an invalid diagnostic list."); return result; }
	const auto entries = items.toArray(); result.limited = entries.size() > 2000; qint64 bytes = 0;
	QVector<int> lines {0}; for (int i = 0; i < document.text.size(); ++i) { if (document.text[i] == QLatin1Char('\n')) { lines << i + 1; } }
	const auto offset = [&](int line, int column) {
		if (line < 0 || line >= lines.size() || column < 0) { return -1; }
		const int end = line + 1 < lines.size() ? lines[line + 1] - 1 : document.text.size();
		if (column > end - lines[line]) { return -1; }
		const int at = lines[line] + column;
		return at > 0 && at < document.text.size() && document.text[at - 1].isHighSurrogate() && document.text[at].isLowSurrogate() ? -1 : at;
	};
	for (qsizetype i = 0; i < std::min<qsizetype>(entries.size(), 2000); ++i) {
		const auto object = entries[i].toObject(); const auto range = object.value(QStringLiteral("range")).toObject();
		const auto start = range.value(QStringLiteral("start")).toObject(), end = range.value(QStringLiteral("end")).toObject();
		LanguageDiagnostic diagnostic; auto& at = diagnostic.location; at.filePath = document.filePath;
		const auto message = object.value(QStringLiteral("message"));
		int severity = 3;
		bool valid = positionNumber(start.value(QStringLiteral("line")), &at.line) && positionNumber(start.value(QStringLiteral("character")), &at.character)
			&& positionNumber(end.value(QStringLiteral("line")), &at.endLine) && positionNumber(end.value(QStringLiteral("character")), &at.endCharacter)
			&& message.isString() && message.toString().isValidUtf16() && !message.toString().contains(QChar(0));
		if (object.contains(QStringLiteral("severity"))) { valid = valid && positionNumber(object.value(QStringLiteral("severity")), &severity) && severity >= 1 && severity <= 4; }
		const int first = valid ? offset(at.line, at.character) : -1;
		const int last = valid ? offset(at.endLine, at.endCharacter) : -1;
		if (!valid || first < 0 || last < first) { ++result.skipped; result.limited = true; continue; }
		diagnostic.severity = severity; diagnostic.message = bounded(message.toString(), 8192);
		diagnostic.code = bounded(object.value(QStringLiteral("code")).toVariant().toString(), 128);
		diagnostic.source = bounded(object.value(QStringLiteral("source")).toString(), 128);
		const auto wireBytes = QJsonDocument(object).toJson(QJsonDocument::Compact).size();
		if (wireBytes <= 65536 && bytes + wireBytes <= 2 * 1024 * 1024) { diagnostic.wire = object; bytes += wireBytes; }
		else { result.limited = true; }
		if (message.toString().size() > 8192) { result.limited = true; }
		result.items << diagnostic;
	}
	return result;
}

void LanguageServerClient::queueDiagnostics(const QString& key, bool resetRetries)
{
	if (!ready() || !m_pullDiagnostics || !m_documents.contains(key)) { return; }
	const int pending = m_documents.value(key).diagnosticRequest;
	if (pending >= 0) { cancelRequest(pending); }
	if (!ready() || !m_documents.contains(key)) { return; }
	auto& document = m_documents[key]; document.diagnosticRequest = -1; document.diagnosticQueued = true;
	if (resetRetries) { document.diagnosticRetries = 0; document.diagnosticRetryAfter = 0; }
	document.diagnostics = {}; document.diagnostics.filePath = document.source.filePath; document.diagnostics.version = document.version;
	document.diagnostics.versioned = true; document.diagnostics.pending = true; document.diagnostics.origin = QStringLiteral("pull");
	const auto state = document.diagnostics;
	if (diagnosticsChanged) { diagnosticsChanged(state); }
	QTimer::singleShot(0, this, [this]() { dispatchDiagnostics(); });
}

void LanguageServerClient::dispatchDiagnostics()
{
	if (!ready() || !m_pullDiagnostics) { return; }
	int active = 0; for (const auto& document : m_documents) { if (document.diagnosticRequest >= 0) { ++active; } }
	for (const auto& key : m_documents.keys()) {
		if (active >= 4 || m_pending.size() >= 28 || !ready()) { return; }
		const auto document = m_documents.value(key);
		if (!document.diagnosticQueued || document.diagnosticRequest >= 0 || m_clock.elapsed() < document.diagnosticRetryAfter) { continue; }
		QJsonObject params {{QStringLiteral("textDocument"), QJsonObject {{QStringLiteral("uri"), languageServerUri(document.source.filePath)}}}};
		if (!m_diagnosticIdentifier.isEmpty()) { params.insert(QStringLiteral("identifier"), m_diagnosticIdentifier); }
		if (!document.diagnosticResultId.isEmpty()) { params.insert(QStringLiteral("previousResultId"), document.diagnosticResultId); }
		const int id = request(QStringLiteral("textDocument/diagnostic"), params, [this, key, document](const QJsonValue& value, const QString& error) {
			auto current = m_documents.find(key);
			if (!ready() || current == m_documents.end() || current->version != document.version) { return; }
			current->diagnosticRequest = -1; current->diagnosticQueued = false;
			const auto object = value.toObject(); const auto kind = object.value(QStringLiteral("kind")).toString();
			const auto idValue = object.value(QStringLiteral("resultId")); const auto resultId = idValue.toString();
			QString why = error;
			if (why.isEmpty() && (!value.isObject() || (kind != QStringLiteral("full") && kind != QStringLiteral("unchanged"))
				|| (!idValue.isUndefined() && !idValue.isString()) || resultId.size() > 1024 || !resultId.isValidUtf16() || resultId.contains(QChar(0)))) {
				why = QCoreApplication::translate("LanguageServer", "The language server returned an invalid diagnostic report.");
			}
			QJsonValue items = object.value(QStringLiteral("items"));
			if (why.isEmpty() && kind == QStringLiteral("unchanged")) {
				if (resultId.isEmpty() || document.diagnosticResultId.isEmpty() || !document.diagnosticCache.received || document.diagnosticCache.limited || !document.diagnosticCache.error.isEmpty()) {
					why = QCoreApplication::translate("LanguageServer", "The server reused diagnostics without a valid previous result.");
				} else {
					QJsonArray retained; for (const auto& diagnostic : document.diagnosticCache.items) { retained << diagnostic.wire; } items = retained;
				}
			}
			auto report = parseLanguageDiagnostics(items, document.source, document.version, true, QStringLiteral("pull"));
			if (!why.isEmpty()) { report.error = why; report.items.clear(); report.limited = false; report.skipped = 0; }
			current->diagnostics = report;
			if (report.error.isEmpty() && !report.limited) { current->diagnosticCache = report; current->diagnosticResultId = resultId; }
			else { current->diagnosticCache = {}; current->diagnosticResultId.clear(); }
			if (diagnosticsChanged) { diagnosticsChanged(report); }
			QTimer::singleShot(0, this, [this]() { dispatchDiagnostics(); });
		});
		if (!ready() || !m_documents.contains(key)) { return; }
		if (id >= 0) { m_documents[key].diagnosticRequest = id; m_documents[key].diagnosticQueued = false; ++active; }
	}
}

bool LanguageServerClient::retryDiagnostics(int id, const QJsonObject& error)
{
	if (error.value(QStringLiteral("code")).toInt() != -32802 || !error.value(QStringLiteral("data")).toObject().value(QStringLiteral("retriggerRequest")).toBool(true)) { return false; }
	for (auto it = m_documents.begin(); it != m_documents.end(); ++it) {
		if (it->diagnosticRequest != id || it->diagnosticRetries >= 2) { continue; }
		it->diagnosticRequest = -1; it->diagnosticQueued = true; ++it->diagnosticRetries;
		it->diagnosticRetryAfter = m_clock.elapsed() + 250 * it->diagnosticRetries; return true;
	}
	return false;
}

} // namespace vibestudio
