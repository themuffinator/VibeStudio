#pragma once

#include "core/language_server.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include "core/text_document.h"
#include <QThread>
#include <iostream>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

// Controlled stdio server used only by the core/CLI/UI regression suites.
inline int runLanguageServerFixture(const QString& mode)
{
#ifdef Q_OS_WIN
	_setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
#endif
	const auto send = [](const QJsonObject& object) {
		const auto bytes = vibestudio::LanguageServerFramer::encode(object);
		// Split both a header and a UTF-8 payload across writes.
		std::cout.write(bytes.data(), 7); std::cout.flush();
		std::cout.write(bytes.data() + 7, bytes.size() - 7); std::cout.flush();
	};
	QHash<QString, QString> documents;
	QHash<QString, int> versions;
	for (;;) {
		std::string line; qint64 length = -1;
		while (std::getline(std::cin, line)) {
			const auto header = QByteArray::fromStdString(line).trimmed();
			if (header.isEmpty()) { break; }
			if (header.startsWith("Content-Length:")) { length = header.mid(15).trimmed().toLongLong(); }
		}
		if (!std::cin || length < 0 || length > 16 * 1024 * 1024) { return 0; }
		QByteArray body(length, '\0'); std::cin.read(body.data(), length);
		const auto object = QJsonDocument::fromJson(body).object();
		const auto id = object.value(QStringLiteral("id"));
		const auto method = object.value(QStringLiteral("method")).toString();
		const auto params = object.value(QStringLiteral("params")).toObject();
		const auto result = [&](const QJsonValue& value) { send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("result"), value}}); };
		if (method == QStringLiteral("initialize")) {
			if (mode.contains(QStringLiteral("snippet")) && !params.value(QStringLiteral("capabilities")).toObject().value(QStringLiteral("textDocument")).toObject()
				.value(QStringLiteral("completion")).toObject().value(QStringLiteral("completionItem")).toObject().value(QStringLiteral("snippetSupport")).toBool()) { return 61; }
			if (mode == QStringLiteral("hang")) { continue; }
			if (mode == QStringLiteral("crash")) { return 7; }
			if (mode == QStringLiteral("malformed")) { std::cout << "Content-Length: 999999999\r\n\r\n" << std::flush; continue; }
			result(QJsonObject {{QStringLiteral("capabilities"), QJsonObject {{QStringLiteral("positionEncoding"), mode == QStringLiteral("utf8") ? QStringLiteral("utf-8") : QStringLiteral("utf-16")},
				{QStringLiteral("textDocumentSync"), QJsonObject {{QStringLiteral("openClose"), true}, {QStringLiteral("change"), mode == QStringLiteral("full") ? 1 : 2}}}, {QStringLiteral("definitionProvider"), true},
				{QStringLiteral("referencesProvider"), mode != QStringLiteral("no-references")},
				{QStringLiteral("hoverProvider"), mode != QStringLiteral("no-hover")},
				{QStringLiteral("signatureHelpProvider"), mode.startsWith(QStringLiteral("signature")) ? QJsonValue(QJsonObject {
					{QStringLiteral("triggerCharacters"), QJsonArray {QStringLiteral("("), QStringLiteral(",")}},
					{QStringLiteral("retriggerCharacters"), QJsonArray {QStringLiteral(")")}}}) : QJsonValue(false)},
				{QStringLiteral("codeActionProvider"), mode == QStringLiteral("no-actions") ? QJsonValue(false) : mode == QStringLiteral("actions-boolean") ? QJsonValue(true) : QJsonValue(QJsonObject {{QStringLiteral("resolveProvider"), mode != QStringLiteral("actions-no-resolve")}})},
				{QStringLiteral("renameProvider"), mode == QStringLiteral("no-rename") ? QJsonValue(false) : mode == QStringLiteral("rename-no-prepare") ? QJsonValue(true) : QJsonValue(QJsonObject {{QStringLiteral("prepareProvider"), true}})},
				{QStringLiteral("documentFormattingProvider"), mode != QStringLiteral("no-format")},
				{QStringLiteral("documentRangeFormattingProvider"), mode != QStringLiteral("no-range-format")},
				{QStringLiteral("completionProvider"), QJsonObject {{QStringLiteral("resolveProvider"), mode.startsWith(QStringLiteral("resolve-completion"))}, {QStringLiteral("triggerCharacters"), QJsonArray {QStringLiteral("."), QStringLiteral(">")}}}}}},
				{QStringLiteral("serverInfo"), QJsonObject {{QStringLiteral("name"), QStringLiteral("VibeStudio test server")}}}});
		} else if (method == QStringLiteral("textDocument/didOpen") || method == QStringLiteral("textDocument/didChange")) {
			const auto document = params.value(QStringLiteral("textDocument")).toObject();
			const QString uri = document.value(QStringLiteral("uri")).toString();
			QString text;
			if (method.endsWith(QStringLiteral("didOpen"))) { text = document.value(QStringLiteral("text")).toString(); }
			else {
				const auto change = params.value(QStringLiteral("contentChanges")).toArray().first().toObject();
				if (mode != QStringLiteral("full")) {
					const auto end = change.value(QStringLiteral("range")).toObject().value(QStringLiteral("end")).toObject();
					const auto prior = documents.value(uri);
					if (end.value(QStringLiteral("line")).toInt(-1) != prior.count(QLatin1Char('\n'))
						|| end.value(QStringLiteral("character")).toInt(-1) != prior.size() - prior.lastIndexOf(QLatin1Char('\n')) - 1) { return 37; }
				}
				text = change.value(QStringLiteral("text")).toString();
			}
			documents.insert(uri, text);
			versions.insert(uri, document.value(QStringLiteral("version")).toInt());
			if (mode == QStringLiteral("quiet")) { continue; }
			const QJsonObject range {{QStringLiteral("start"), QJsonObject {{QStringLiteral("line"), 0}, {QStringLiteral("character"), 0}}},
				{QStringLiteral("end"), QJsonObject {{QStringLiteral("line"), 0}, {QStringLiteral("character"), 1}}}};
			QJsonObject diagnostic {{QStringLiteral("range"), range}, {QStringLiteral("severity"), 1}, {QStringLiteral("message"), QStringLiteral("fixture: café 雪 ") + text.left(100)}, {QStringLiteral("source"), QStringLiteral("fixture")}};
			diagnostic.insert(QStringLiteral("data"), QJsonObject {{QStringLiteral("token"), QStringLiteral("diagnostic-data")}});
			QJsonObject publication {{QStringLiteral("uri"), uri}, {QStringLiteral("version"), document.value(QStringLiteral("version")).toInt() - 1}, {QStringLiteral("diagnostics"), QJsonArray {diagnostic}}};
			send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), QStringLiteral("textDocument/publishDiagnostics")}, {QStringLiteral("params"), publication}});
			if (mode == QStringLiteral("unversioned")) { publication.remove(QStringLiteral("version")); }
			else { publication.insert(QStringLiteral("version"), document.value(QStringLiteral("version"))); }
			if (!text.contains(QStringLiteral("bad"))) { publication.insert(QStringLiteral("diagnostics"), QJsonArray {}); }
			send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), QStringLiteral("textDocument/publishDiagnostics")}, {QStringLiteral("params"), publication}});
			send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), QStringLiteral("workspace/applyEdit")}, {QStringLiteral("id"), QStringLiteral("edit-check")}, {QStringLiteral("params"), QJsonObject {}}});
		} else if (method == QStringLiteral("textDocument/didClose")) {
			documents.remove(params.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString());
		} else if (method == QStringLiteral("textDocument/codeAction") || method == QStringLiteral("codeAction/resolve")) {
			const bool resolve = method == QStringLiteral("codeAction/resolve");
			if (mode == QStringLiteral("actions-slow") || (resolve && mode == QStringLiteral("actions-resolve-slow"))) { QThread::msleep(350); }
			if (mode == QStringLiteral("actions-hang") || (resolve && mode == QStringLiteral("actions-resolve-hang"))) { continue; }
			if (mode == QStringLiteral("actions-empty")) { result(QJsonValue(QJsonValue::Null)); continue; }
			if (mode == QStringLiteral("actions-malformed")) { result(QJsonObject {}); continue; }
			if (mode == QStringLiteral("actions-skipped")) { result(QJsonArray {QJsonObject {{QStringLiteral("title"), 4}}}); continue; }
			const QString uri = (resolve ? params.value(QStringLiteral("data")) : params.value(QStringLiteral("textDocument"))).toObject().value(QStringLiteral("uri")).toString();
			if (!documents.contains(uri)) { return 49; }
			QJsonArray changes; QStringList uris = documents.keys();
			const QString other = QDir(QFileInfo(vibestudio::languageServerPath(uri)).absolutePath()).filePath(QStringLiteral("other.cpp")), otherUri = vibestudio::languageServerUri(other);
			if (QFileInfo::exists(other) && !uris.contains(otherUri)) { uris << otherUri; }
			for (const auto& target : uris) {
				const QString source = documents.contains(target) ? documents.value(target) : vibestudio::readTextFile(vibestudio::languageServerPath(target)).text;
				const auto position = [&](int at) { const auto prefix = QStringView(source).left(at); return QJsonObject {{QStringLiteral("line"), prefix.count(QLatin1Char('\n'))}, {QStringLiteral("character"), at - prefix.lastIndexOf(QLatin1Char('\n')) - 1}}; };
				QJsonArray edits;
				for (int at = source.indexOf(QStringLiteral("value")); at >= 0; at = source.indexOf(QStringLiteral("value"), at + 5)) {
					edits << QJsonObject {{QStringLiteral("range"), QJsonObject {{QStringLiteral("start"), position(at)}, {QStringLiteral("end"), position(at + 5)}}}, {QStringLiteral("newText"), QStringLiteral("fixedValue")}};
				}
				if (!edits.isEmpty()) { changes << QJsonObject {{QStringLiteral("textDocument"), QJsonObject {{QStringLiteral("uri"), target}, {QStringLiteral("version"), versions.contains(target) ? QJsonValue(versions.value(target)) : QJsonValue(QJsonValue::Null)}}}, {QStringLiteral("edits"), edits}}; }
			}
			if (mode == QStringLiteral("actions-resource")) { changes << QJsonObject {{QStringLiteral("kind"), QStringLiteral("delete")}, {QStringLiteral("uri"), uri}}; }
			const QJsonObject edit {{QStringLiteral("documentChanges"), changes}};
			if (resolve) {
				if (params.value(QStringLiteral("data")).toObject().value(QStringLiteral("token")).toString() != QStringLiteral("opaque-data")) { return 50; }
				auto action = params; action.insert(QStringLiteral("edit"), edit);
				if (mode == QStringLiteral("actions-resolve-identity")) { action.insert(QStringLiteral("title"), QStringLiteral("Different action")); }
				if (mode == QStringLiteral("actions-resolve-command")) { action.insert(QStringLiteral("command"), QJsonObject {{QStringLiteral("command"), QStringLiteral("fixture.write")}}); }
				result(action); continue;
			}
			const auto context = params.value(QStringLiteral("context")).toObject();
			if (mode == QStringLiteral("actions-context") && (context.value(QStringLiteral("diagnostics")).toArray().size() != 1
				|| context.value(QStringLiteral("diagnostics")).toArray().first().toObject().value(QStringLiteral("data")).toObject().value(QStringLiteral("token")).toString() != QStringLiteral("diagnostic-data"))) { return 51; }
			const QJsonObject data {{QStringLiteral("uri"), uri}, {QStringLiteral("token"), QStringLiteral("opaque-data")}, {QStringLiteral("selection"), params.value(QStringLiteral("range"))}, {QStringLiteral("context"), context}};
			result(QJsonArray {
				QJsonObject {{QStringLiteral("title"), QStringLiteral("Fix value usage")}, {QStringLiteral("kind"), QStringLiteral("quickfix")}, {QStringLiteral("isPreferred"), true}, {QStringLiteral("edit"), edit}, {QStringLiteral("data"), data}},
				QJsonObject {{QStringLiteral("title"), QStringLiteral("Resolve value refactoring")}, {QStringLiteral("kind"), QStringLiteral("refactor.rewrite")}, {QStringLiteral("data"), data}},
				QJsonObject {{QStringLiteral("title"), QStringLiteral("Unavailable fix")}, {QStringLiteral("disabled"), QJsonObject {{QStringLiteral("reason"), QStringLiteral("Selection is not supported")}}}},
				QJsonObject {{QStringLiteral("title"), QStringLiteral("Command action")}, {QStringLiteral("command"), QStringLiteral("fixture.write")}},
				QJsonObject {{QStringLiteral("title"), QStringLiteral("Edit then command")}, {QStringLiteral("edit"), edit}, {QStringLiteral("command"), QJsonObject {{QStringLiteral("command"), QStringLiteral("fixture.write")}}}}});
		} else if (method == QStringLiteral("textDocument/prepareRename") || method == QStringLiteral("textDocument/rename")) {
			if (mode == QStringLiteral("slow") || mode == QStringLiteral("rename-slow")) { QThread::msleep(350); }
			if (mode == QStringLiteral("rename-hang")) { continue; }
			if (mode == QStringLiteral("rename-empty")) { result(QJsonValue(QJsonValue::Null)); continue; }
			const QString uri = params.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString(), source = documents.value(uri);
			const auto position = params.value(QStringLiteral("position")).toObject();
			const int at = vibestudio::languageSourceOffset(source, position.value(QStringLiteral("line")).toInt(-1), position.value(QStringLiteral("character")).toInt(-1));
			if (at < 0) { return 46; }
			int first = at, end = at;
			const auto nameChar = [](QChar ch) { return ch.isLetterOrNumber() || ch == QLatin1Char('_'); };
			while (first > 0 && nameChar(source[first - 1])) { --first; } while (end < source.size() && nameChar(source[end])) { ++end; }
			const QString name = source.mid(first, end - first);
			const auto range = [](const QString& text, int first, int end) {
				const auto pos = [&](int offset) { const auto prefix = QStringView(text).left(offset); return QJsonObject {{QStringLiteral("line"), prefix.count(QLatin1Char('\n'))}, {QStringLiteral("character"), offset - prefix.lastIndexOf(QLatin1Char('\n')) - 1}}; };
				return QJsonObject {{QStringLiteral("start"), pos(first)}, {QStringLiteral("end"), pos(end)}};
			};
			if (method.endsWith(QStringLiteral("prepareRename"))) { result(name.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(QJsonObject {{QStringLiteral("range"), range(source, first, end)}, {QStringLiteral("placeholder"), name}})); continue; }
			const auto edits = [&](const QString& text) {
				QJsonArray result; if (name.isEmpty()) { return result; }
				for (int offset = text.indexOf(name); offset >= 0; offset = text.indexOf(name, offset + name.size())) {
					result << QJsonObject {{QStringLiteral("range"), range(text, offset, offset + name.size())}, {QStringLiteral("newText"), params.value(QStringLiteral("newName"))}};
				}
				return result;
			};
			QJsonArray changes; QStringList uris = documents.keys();
			const QString extra = QDir(QFileInfo(vibestudio::languageServerPath(uri)).absolutePath()).filePath(QStringLiteral("other.cpp"));
			const QString extraUri = vibestudio::languageServerUri(extra); if (QFileInfo::exists(extra) && !uris.contains(extraUri)) { uris << extraUri; }
			for (const auto& target : uris) {
				const auto sourceEdits = edits(documents.contains(target) ? documents.value(target) : vibestudio::readTextFile(vibestudio::languageServerPath(target)).text);
				if (sourceEdits.isEmpty()) { continue; }
				changes << QJsonObject {{QStringLiteral("textDocument"), QJsonObject {{QStringLiteral("uri"), target}, {QStringLiteral("version"), versions.contains(target) ? QJsonValue(versions.value(target)) : QJsonValue(QJsonValue::Null)}}}, {QStringLiteral("edits"), sourceEdits}};
			}
			if (mode == QStringLiteral("rename-resource")) { changes << QJsonObject {{QStringLiteral("kind"), QStringLiteral("delete")}, {QStringLiteral("uri"), uri}}; }
			result(QJsonObject {{QStringLiteral("documentChanges"), changes}});
		} else if (method == QStringLiteral("textDocument/formatting") || method == QStringLiteral("textDocument/rangeFormatting")) {
			if (mode == QStringLiteral("slow")) { QThread::msleep(350); }
			if (mode == QStringLiteral("format-hang")) { continue; }
			if (mode == QStringLiteral("format-malformed")) { result(QJsonObject {}); continue; }
			if (mode == QStringLiteral("format-empty")) { result(QJsonValue(QJsonValue::Null)); continue; }
			if (mode == QStringLiteral("format-error")) { send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("error"), QJsonObject {{QStringLiteral("code"), -32603}, {QStringLiteral("message"), QStringLiteral("Fixture formatting failure")}}}}); continue; }
			const auto options = params.value(QStringLiteral("options")).toObject();
			if (options.value(QStringLiteral("tabSize")).toInt() < 1 || options.value(QStringLiteral("tabSize")).toInt() > 16 || !options.value(QStringLiteral("insertSpaces")).isBool()) { return 42; }
			const QString uri = params.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString(), source = documents.value(uri);
			int first = 0, end = int(source.size());
			if (method.endsWith(QStringLiteral("rangeFormatting"))) {
				const auto range = params.value(QStringLiteral("range")).toObject(); const auto start = range.value(QStringLiteral("start")).toObject(), finish = range.value(QStringLiteral("end")).toObject();
				first = vibestudio::languageSourceOffset(source, start.value(QStringLiteral("line")).toInt(-1), start.value(QStringLiteral("character")).toInt(-1));
				end = vibestudio::languageSourceOffset(source, finish.value(QStringLiteral("line")).toInt(-1), finish.value(QStringLiteral("character")).toInt(-1));
				if (first < 0 || end <= first) { return 43; }
			}
			const auto pos = [&](int offset) { const QString prefix = source.left(offset); return QJsonObject {{QStringLiteral("line"), prefix.count(QLatin1Char('\n'))}, {QStringLiteral("character"), offset - prefix.lastIndexOf(QLatin1Char('\n')) - 1}}; };
			QJsonArray edits;
			for (int at = int(source.indexOf(QStringLiteral("   "), first)); at >= 0 && at + 3 <= end; at = int(source.indexOf(QStringLiteral("   "), at + 3))) {
				edits << QJsonObject {{QStringLiteral("range"), QJsonObject {{QStringLiteral("start"), pos(at)}, {QStringLiteral("end"), pos(at + 3)}}}, {QStringLiteral("newText"), QStringLiteral(" ")}};
			}
			if (!edits.isEmpty() && mode == QStringLiteral("format-overlap")) { edits << edits.first(); }
			if (!edits.isEmpty() && mode == QStringLiteral("format-large")) { while (edits.size() <= 10000) { edits << edits.first(); } }
			if (mode == QStringLiteral("format-write-race")) { QFile changed(vibestudio::languageServerPath(uri)); if (changed.open(QIODevice::WriteOnly)) { changed.write("external edit\n"); } }
			result(edits);
		} else if (method == QStringLiteral("textDocument/signatureHelp")) {
			if (mode == QStringLiteral("signature-slow")) { QThread::msleep(350); }
			if (mode == QStringLiteral("signature-hang")) { continue; }
			if (mode == QStringLiteral("signature-malformed")) { result(QJsonObject {}); continue; }
			const auto uri = params.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString();
			const auto pos = params.value(QStringLiteral("position")).toObject();
			const auto source = documents.value(uri); const int offset = vibestudio::languageSourceOffset(source, pos.value(QStringLiteral("line")).toInt(), pos.value(QStringLiteral("character")).toInt());
			if (offset < 0) { return 46; }
			const auto prefix = source.left(offset); const int opening = prefix.lastIndexOf(QLatin1Char('('));
			if (mode == QStringLiteral("signature-empty") || opening < 0 || prefix.lastIndexOf(QLatin1Char(')')) > opening) { result(QJsonValue(QJsonValue::Null)); continue; }
			const auto context = params.value(QStringLiteral("context")).toObject();
			const int kind = context.value(QStringLiteral("triggerKind")).toInt();
			if (kind < 1 || kind > 3 || !context.value(QStringLiteral("isRetrigger")).isBool()
				|| (kind == 2 && !QStringLiteral("(,)").contains(context.value(QStringLiteral("triggerCharacter")).toString()))) { return 47; }
			const int parameter = prefix.mid(opening).count(QLatin1Char(',')) > 0 ? 1 : 0;
			QJsonArray signatures;
			for (const auto& type : {QStringLiteral("int"), QStringLiteral("double")}) {
				const QString label = QStringLiteral("sum(%1 left, %1 right)").arg(type), first = type + QStringLiteral(" left"), second = type + QStringLiteral(" right");
				const int start = label.indexOf(second);
				QJsonArray parameters {QJsonObject {{QStringLiteral("label"), first}, {QStringLiteral("documentation"), QStringLiteral("The first value.")}},
					QJsonObject {{QStringLiteral("label"), QJsonArray {start, start + second.size()}}, {QStringLiteral("documentation"), QJsonObject {
						{QStringLiteral("kind"), QStringLiteral("markdown")}, {QStringLiteral("value"), QStringLiteral("The **second** value. ![blocked](file:///not-a-documentation-resource)")}}}}};
				if (mode == QStringLiteral("signature-bad-range")) { parameters = QJsonArray {QJsonObject {{QStringLiteral("label"), QJsonArray {0, 99999}}}}; }
				signatures << QJsonObject {{QStringLiteral("label"), label}, {QStringLiteral("parameters"), parameters}, {QStringLiteral("activeParameter"), parameter},
					{QStringLiteral("documentation"), mode == QStringLiteral("signature-large") ? QString(9000, QLatin1Char('x')) : QStringLiteral("Sum two values. <b>Literal text</b>.")}};
			}
			const auto previous = context.value(QStringLiteral("activeSignatureHelp")).toObject();
			result(QJsonObject {{QStringLiteral("signatures"), signatures}, {QStringLiteral("activeSignature"), previous.value(QStringLiteral("activeSignature")).toInt(0)}, {QStringLiteral("activeParameter"), 99}});
		} else if (method == QStringLiteral("textDocument/hover")) {
			if (mode == QStringLiteral("slow")) { QThread::msleep(350); }
			if (mode == QStringLiteral("hover-hang")) { continue; }
			if (mode == QStringLiteral("hover-malformed")) { result(QJsonObject {}); continue; }
			if (mode == QStringLiteral("hover-empty")) { result(QJsonValue(QJsonValue::Null)); continue; }
			const auto uri = params.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString();
			const auto at = params.value(QStringLiteral("position")).toObject();
			const int row = at.value(QStringLiteral("line")).toInt(), column = at.value(QStringLiteral("character")).toInt();
			const auto text = documents.value(uri);
			const int offset = vibestudio::languageSourceOffset(text, row, column);
			if (offset < 0) { return 41; }
			const int start = vibestudio::languageWordStart(text, offset); int end = offset;
			while (end < text.size() && (text[end].isLetterOrNumber() || text[end] == QLatin1Char('_'))) { ++end; }
			const auto word = text.mid(start, end - start);
			const auto pos = [](int row, int column) { return QJsonObject {{QStringLiteral("line"), row}, {QStringLiteral("character"), column}}; };
			QJsonObject reply {{QStringLiteral("contents"), QJsonArray {QJsonObject {{QStringLiteral("language"), QStringLiteral("cpp")}, {QStringLiteral("value"), QStringLiteral("int %1;").arg(word)}}, QStringLiteral("**Documentation** for %1. café 雪").arg(word)}},
				{QStringLiteral("range"), QJsonObject {{QStringLiteral("start"), pos(row, column - offset + start)}, {QStringLiteral("end"), pos(row, mode == QStringLiteral("hover-bad-range") ? 999999 : column - offset + end)}}}};
			if (mode == QStringLiteral("hover-large")) { reply.insert(QStringLiteral("contents"), QString(70000, QLatin1Char('x'))); }
			if (mode == QStringLiteral("hover-plain")) { reply.insert(QStringLiteral("contents"), QJsonObject {{QStringLiteral("kind"), QStringLiteral("plaintext")}, {QStringLiteral("value"), QStringLiteral("int target; <b>literal</b>")}}); }
			result(reply);
		} else if (method == QStringLiteral("textDocument/references")) {
			if (mode == QStringLiteral("slow")) { QThread::msleep(350); }
			if (mode == QStringLiteral("references-hang")) { continue; }
			if (mode == QStringLiteral("references-malformed")) { result(QJsonObject {}); continue; }
			const QString uri = params.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString();
			const auto at = params.value(QStringLiteral("position")).toObject();
			const auto text = documents.value(uri); const int offset = vibestudio::languageSourceOffset(text, at.value(QStringLiteral("line")).toInt(), at.value(QStringLiteral("character")).toInt());
			if (offset < 0 || !params.value(QStringLiteral("context")).toObject().value(QStringLiteral("includeDeclaration")).isBool()) { return 40; }
			const bool include = params.value(QStringLiteral("context")).toObject().value(QStringLiteral("includeDeclaration")).toBool();
			const int start = vibestudio::languageWordStart(text, offset); int end = offset;
			while (end < text.size() && (text[end].isLetterOrNumber() || text[end] == QLatin1Char('_'))) { ++end; }
			const auto word = text.mid(start, end - start); QJsonArray matches;
			auto sources = documents;
			const QString other = QFileInfo(vibestudio::languageServerPath(uri)).dir().filePath(QStringLiteral("references-other.cpp"));
			const QString otherUri = vibestudio::languageServerUri(other);
			if (!sources.contains(otherUri) && QFileInfo::exists(other)) { sources.insert(otherUri, vibestudio::readTextFile(other).text); }
			const auto pos = [](int line, int column) { return QJsonObject {{QStringLiteral("line"), line}, {QStringLiteral("character"), column}}; };
			for (auto source = sources.cbegin(); !word.isEmpty() && source != sources.cend(); ++source) {
				const auto lines = source.value().split(QLatin1Char('\n')); bool declaration = true;
				for (int line = 0; line < lines.size(); ++line) {
					// Deliberate fixture semantics: comments and a shadowed binding are excluded.
					if (lines[line].trimmed().startsWith(QStringLiteral("//")) || lines[line].contains(QStringLiteral("shadow"))) { continue; }
					for (qsizetype column = lines[line].indexOf(word); column >= 0; column = lines[line].indexOf(word, column + word.size())) {
						if (source.key() == uri && declaration) { declaration = false; if (!include) { continue; } }
						matches << QJsonObject {{QStringLiteral("uri"), source.key()}, {QStringLiteral("range"), QJsonObject {{QStringLiteral("start"), pos(line, int(column))}, {QStringLiteral("end"), pos(line, int(column + word.size()))}}}};
					}
				}
			}
			if (!matches.isEmpty()) { matches << matches.first(); }
			if (mode == QStringLiteral("references-many") && !matches.isEmpty()) { while (matches.size() <= vibestudio::languageReferenceLimit) { matches << matches.first(); } }
			if (mode == QStringLiteral("references-bad")) {
				matches << QJsonObject {{QStringLiteral("uri"), QStringLiteral("https://example.com/source.cpp")}, {QStringLiteral("range"), QJsonObject {{QStringLiteral("start"), pos(0, 0)}, {QStringLiteral("end"), pos(0, 1)}}}};
				matches << QJsonObject {{QStringLiteral("uri"), uri}, {QStringLiteral("range"), QJsonObject {{QStringLiteral("start"), pos(2147483647, 0)}, {QStringLiteral("end"), pos(2147483647, 1)}}}};
			}
			result(matches);
		} else if (method == QStringLiteral("textDocument/completion")) {
			if (mode == QStringLiteral("slow")) { QThread::msleep(350); }
			const auto uri = params.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString();
			const auto at = params.value(QStringLiteral("position")).toObject();
			const int row = at.value(QStringLiteral("line")).toInt(), column = at.value(QStringLiteral("character")).toInt();
			const auto lines = documents.value(uri).split(QLatin1Char('\n'));
			if (row < 0 || row >= lines.size() || column < 0 || column > lines[row].size()) { return 39; }
			int begin = column, end = column;
			while (begin > 0 && lines[row][begin - 1].isLetterOrNumber()) { --begin; }
			while (end < lines[row].size() && lines[row][end].isLetterOrNumber()) { ++end; }
			const auto pos = [](int line, int character) { return QJsonObject {{QStringLiteral("line"), line}, {QStringLiteral("character"), character}}; };
			const QJsonObject range {{QStringLiteral("start"), pos(row, begin)}, {QStringLiteral("end"), pos(row, end)}};
			QJsonObject item {{QStringLiteral("label"), QStringLiteral("targetMember")}, {QStringLiteral("detail"), QStringLiteral("int Object::targetMember")},
				{QStringLiteral("filterText"), QStringLiteral("targetMember")}, {QStringLiteral("sortText"), QStringLiteral("000")},
				{QStringLiteral("documentation"), QStringLiteral("Member documentation <b>shown as text</b>.")},
				{QStringLiteral("textEdit"), QJsonObject {{QStringLiteral("range"), range}, {QStringLiteral("newText"), QStringLiteral("targetMember")}}}};
			if (row > 0) { item.insert(QStringLiteral("additionalTextEdits"), QJsonArray {QJsonObject {{QStringLiteral("range"), QJsonObject {{QStringLiteral("start"), pos(0, 0)}, {QStringLiteral("end"), pos(0, 0)}}}, {QStringLiteral("newText"), QStringLiteral("// imported\n")}}}); }
			QJsonObject defaults;
			if (mode.contains(QStringLiteral("snippet"))) {
				defaults.insert(QStringLiteral("insertTextFormat"), 2);
				item.insert(QStringLiteral("textEdit"), QJsonObject {{QStringLiteral("range"), range},
					{QStringLiteral("newText"), mode == QStringLiteral("snippet-transform") ? QStringLiteral("${TM_FILENAME/(.*)/$1/}") : QStringLiteral("target(${1:value}, $1, ${2|left,right|})$0")}});
			}
			if (mode.startsWith(QStringLiteral("resolve-completion"))) {
				defaults.insert(QStringLiteral("data"), QJsonObject {{QStringLiteral("uri"), uri}, {QStringLiteral("row"), row}, {QStringLiteral("opaque"), QStringLiteral("kept")}});
				item.remove(QStringLiteral("detail")); item.remove(QStringLiteral("documentation")); item.remove(QStringLiteral("additionalTextEdits"));
			}
			result(QJsonObject {{QStringLiteral("isIncomplete"), mode == QStringLiteral("completion-incomplete")}, {QStringLiteral("itemDefaults"), defaults}, {QStringLiteral("items"), QJsonArray {item}}});
		} else if (method == QStringLiteral("completionItem/resolve")) {
			if (mode == QStringLiteral("resolve-completion-slow")) { QThread::msleep(350); }
			if (mode == QStringLiteral("resolve-completion-hang")) { continue; }
			if (mode == QStringLiteral("resolve-completion-malformed")) { result(QJsonValue(QJsonValue::Null)); continue; }
			const auto data = params.value(QStringLiteral("data")).toObject();
			if (data.value(QStringLiteral("opaque")).toString() != QStringLiteral("kept") || !documents.contains(data.value(QStringLiteral("uri")).toString())) { return 52; }
			auto item = params; item.insert(QStringLiteral("detail"), QStringLiteral("int Object::targetMember (resolved)"));
			item.insert(QStringLiteral("documentation"), QStringLiteral("Resolved documentation <b>stays literal</b>."));
			const QJsonObject start {{QStringLiteral("line"), 0}, {QStringLiteral("character"), 0}};
			if (data.value(QStringLiteral("row")).toInt() > 0) {
				item.insert(QStringLiteral("additionalTextEdits"), QJsonArray {QJsonObject {{QStringLiteral("range"), QJsonObject {{QStringLiteral("start"), start}, {QStringLiteral("end"), start}}}, {QStringLiteral("newText"), QStringLiteral("// resolved import\n")}}});
			}
			if (mode == QStringLiteral("resolve-completion-identity")) { item.insert(QStringLiteral("label"), QStringLiteral("changed")); }
			if (mode == QStringLiteral("resolve-completion-command")) { item.insert(QStringLiteral("command"), QJsonObject {{QStringLiteral("command"), QStringLiteral("fixture.write")}}); }
			if (mode == QStringLiteral("resolve-completion-overlap")) { item.insert(QStringLiteral("additionalTextEdits"), QJsonArray {params.value(QStringLiteral("textEdit"))}); }
			result(item);
		} else if (method == QStringLiteral("textDocument/definition")) {
			if (mode == QStringLiteral("slow")) { QThread::msleep(350); }
			const auto uri = params.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri"));
			const int line = mode == QStringLiteral("oversized-location") ? 2147483647 : 1;
			const QJsonObject range {{QStringLiteral("start"), QJsonObject {{QStringLiteral("line"), line}, {QStringLiteral("character"), 2}}},
				{QStringLiteral("end"), QJsonObject {{QStringLiteral("line"), line}, {QStringLiteral("character"), 4}}}};
			result(QJsonArray {QJsonObject {{QStringLiteral("targetUri"), uri}, {QStringLiteral("targetRange"), range}, {QStringLiteral("targetSelectionRange"), range}}});
		} else if (method == QStringLiteral("shutdown")) { result(QJsonValue(QJsonValue::Null)); }
		else if (method == QStringLiteral("exit")) { return 0; }
		else if (id.toString() == QStringLiteral("edit-check")) {
			if (object.value(QStringLiteral("result")).toObject().value(QStringLiteral("applied")).toBool(true)) { return 38; }
			send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), QStringLiteral("window/logMessage")},
				{QStringLiteral("params"), QJsonObject {{QStringLiteral("type"), 3}, {QStringLiteral("message"), QStringLiteral("Server edit correctly refused.")}}}});
		}
	}
}
