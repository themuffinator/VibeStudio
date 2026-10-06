#pragma once

#include "tests/language_server_fixture.h"
#include <QCryptographicHash>

// Isolated protocol peer for diagnostic scheduling and successful-save ordering.
inline int runDiagnosticServerFixture(const QString& mode)
{
#ifdef Q_OS_WIN
	_setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
#endif
	const auto send = [](const QJsonObject& message) { const auto bytes = vibestudio::LanguageServerFramer::encode(message); std::cout.write(bytes.data(), bytes.size()); std::cout.flush(); };
	const auto log = [&](const QString& message) { send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), QStringLiteral("window/logMessage")}, {QStringLiteral("params"), QJsonObject {{QStringLiteral("message"), message}}}}); };
	const bool pull = mode.startsWith(QStringLiteral("pull"));
	QHash<QString, QString> documents; QHash<QString, int> versions, requests; bool refreshed = false;
	for (;;) {
		std::string line; qint64 length = -1;
		while (std::getline(std::cin, line)) { const auto header = QByteArray::fromStdString(line).trimmed(); if (header.isEmpty()) { break; } if (header.startsWith("Content-Length:")) { length = header.mid(15).trimmed().toLongLong(); } }
		if (!std::cin || length < 0 || length > 16 * 1024 * 1024) { return 0; }
		QByteArray body(length, '\0'); std::cin.read(body.data(), length); const auto message = QJsonDocument::fromJson(body).object();
		const auto id = message.value(QStringLiteral("id")); const auto method = message.value(QStringLiteral("method")).toString(); const auto params = message.value(QStringLiteral("params")).toObject();
		const auto result = [&](const QJsonValue& value) { send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("result"), value}}); };
		const QString uri = params.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString();
		if (method == QStringLiteral("initialize")) {
			const auto caps = params.value(QStringLiteral("capabilities")).toObject(); const auto text = caps.value(QStringLiteral("textDocument")).toObject();
			if (!text.value(QStringLiteral("synchronization")).toObject().value(QStringLiteral("didSave")).toBool()
				|| !text.value(QStringLiteral("diagnostic")).isObject() || text.value(QStringLiteral("diagnostic")).toObject().value(QStringLiteral("relatedDocumentSupport")).toBool()
				|| !caps.value(QStringLiteral("workspace")).toObject().value(QStringLiteral("diagnostics")).toObject().value(QStringLiteral("refreshSupport")).toBool()) { return 71; }
			QJsonObject capability {{QStringLiteral("textDocumentSync"), QJsonObject {{QStringLiteral("openClose"), true}, {QStringLiteral("change"), 2},
				{QStringLiteral("save"), mode == QStringLiteral("save-disabled") ? QJsonValue(false) : mode == QStringLiteral("save-no-text") ? QJsonValue(true) : QJsonValue(QJsonObject {{QStringLiteral("includeText"), true}})}}}};
			if (pull) { capability.insert(QStringLiteral("diagnosticProvider"), QJsonObject {{QStringLiteral("identifier"), QStringLiteral("fixture")}, {QStringLiteral("interFileDependencies"), mode == QStringLiteral("pull-interdependent")}, {QStringLiteral("workspaceDiagnostics"), false}}); }
			result(QJsonObject {{QStringLiteral("capabilities"), capability}, {QStringLiteral("serverInfo"), QJsonObject {{QStringLiteral("name"), QStringLiteral("Diagnostic fixture")}}}});
		} else if (method == QStringLiteral("textDocument/didOpen") || method == QStringLiteral("textDocument/didChange")) {
			const auto document = params.value(QStringLiteral("textDocument")).toObject(); QString text;
			if (method.endsWith(QStringLiteral("didOpen"))) { text = document.value(QStringLiteral("text")).toString(); log(QStringLiteral("opened:") + uri); }
			else {
				const auto changes = params.value(QStringLiteral("contentChanges")).toArray(); if (changes.size() != 1 || !documents.contains(uri)) { return 72; }
				text = changes.first().toObject().value(QStringLiteral("text")).toString();
				log(QStringLiteral("changed:") + uri);
			}
			documents.insert(uri, text); versions.insert(uri, document.value(QStringLiteral("version")).toInt());
		} else if (method == QStringLiteral("textDocument/didClose")) { documents.remove(uri); versions.remove(uri); log(QStringLiteral("closed:") + uri); }
		else if (method == QStringLiteral("textDocument/didSave")) {
			if (!documents.contains(uri) || mode == QStringLiteral("save-disabled")) { return 73; }
			const bool include = mode != QStringLiteral("save-no-text");
			if (params.contains(QStringLiteral("text")) != include || (include && params.value(QStringLiteral("text")).toString() != documents.value(uri))) { return 74; }
			const auto saved = vibestudio::readTextFile(vibestudio::languageServerPath(uri));
			if (!saved.editable() || saved.text != documents.value(uri)) { return 75; }
			log(QStringLiteral("saved:%1 version=%2 text=%3").arg(uri).arg(versions.value(uri)).arg(include ? QStringLiteral("included") : QStringLiteral("omitted")));
			if (!pull) { send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), QStringLiteral("textDocument/publishDiagnostics")}, {QStringLiteral("params"), QJsonObject {
				{QStringLiteral("uri"), uri}, {QStringLiteral("version"), versions.value(uri)}, {QStringLiteral("diagnostics"), QJsonArray {}}}}}); }
		} else if (method == QStringLiteral("textDocument/diagnostic")) {
			if (!pull || !documents.contains(uri) || params.value(QStringLiteral("identifier")).toString() != QStringLiteral("fixture")) { return 76; }
			const int count = ++requests[uri]; log(QStringLiteral("pull:%1 count=%2 previous=%3").arg(uri).arg(count).arg(params.contains(QStringLiteral("previousResultId"))));
			if (mode == QStringLiteral("pull-hang")) { continue; }
			if (mode == QStringLiteral("pull-slow")) { QThread::msleep(250); }
			if (mode == QStringLiteral("pull-error") || mode == QStringLiteral("pull-retry-forever") || mode == QStringLiteral("pull-no-retry") || (mode == QStringLiteral("pull-retry") && count == 1)) {
				send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("error"), QJsonObject {{QStringLiteral("code"), mode == QStringLiteral("pull-error") ? -32603 : -32802},
					{QStringLiteral("message"), QStringLiteral("Fixture diagnostic unavailable")}, {QStringLiteral("data"), QJsonObject {{QStringLiteral("retriggerRequest"), mode != QStringLiteral("pull-no-retry")}}}}}}); continue;
			}
			if (mode == QStringLiteral("pull-malformed")) { result(QJsonValue(QJsonValue::Null)); continue; }
			if (mode == QStringLiteral("pull-unknown-cache")) { result(QJsonObject {{QStringLiteral("kind"), QStringLiteral("unchanged")}, {QStringLiteral("resultId"), QStringLiteral("missing")}}); continue; }
			QString dependency = documents.value(uri); if (mode == QStringLiteral("pull-interdependent")) { const auto paths = documents.keys(); for (const auto& path : paths) { dependency += documents.value(path); } }
			const QString resultId = QString::fromLatin1(QCryptographicHash::hash(dependency.toUtf8(), QCryptographicHash::Sha256).toHex());
			QJsonArray items; const auto source = documents.value(uri); const int bad = source.indexOf(QStringLiteral("bad"));
			if (bad >= 0 || (mode == QStringLiteral("pull-interdependent") && dependency.contains(QStringLiteral("bad")))) {
				const int at = std::max(0, bad); const auto prefix = QStringView(source).left(at); const int line = prefix.count(QLatin1Char('\n')), column = at - prefix.lastIndexOf(QLatin1Char('\n')) - 1;
				items << QJsonObject {{QStringLiteral("range"), QJsonObject {{QStringLiteral("start"), QJsonObject {{QStringLiteral("line"), line}, {QStringLiteral("character"), column}}},
					{QStringLiteral("end"), QJsonObject {{QStringLiteral("line"), line}, {QStringLiteral("character"), column + (bad >= 0 ? 3 : 0)}}}}},
					{QStringLiteral("severity"), 1}, {QStringLiteral("message"), QStringLiteral("Fixture diagnostic café 雪")}, {QStringLiteral("data"), QJsonObject {{QStringLiteral("token"), QStringLiteral("pull-context")}}}};
			}
			if (mode == QStringLiteral("pull-invalid")) { items << QJsonObject {{QStringLiteral("message"), QStringLiteral("bad range")}, {QStringLiteral("range"), QJsonObject {}}}; }
			const bool unchanged = params.value(QStringLiteral("previousResultId")).toString() == resultId || (mode == QStringLiteral("pull-unchanged") && params.contains(QStringLiteral("previousResultId")));
			QJsonObject report {{QStringLiteral("kind"), unchanged ? QStringLiteral("unchanged") : QStringLiteral("full")}, {QStringLiteral("resultId"), resultId}};
			if (!unchanged) { report.insert(QStringLiteral("items"), items); } result(report);
			if (mode == QStringLiteral("pull-refresh") && !refreshed) { refreshed = true; send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), QStringLiteral("workspace/diagnostic/refresh")}, {QStringLiteral("id"), QStringLiteral("refresh-check")}}); }
		} else if (method == QStringLiteral("shutdown")) { result(QJsonValue(QJsonValue::Null)); }
		else if (method == QStringLiteral("exit")) { return 0; }
		else if (method.isEmpty() && id == QJsonValue(QStringLiteral("refresh-check"))) { if (message.contains(QStringLiteral("error"))) { return 77; } log(QStringLiteral("refresh acknowledged")); }
	}
}
