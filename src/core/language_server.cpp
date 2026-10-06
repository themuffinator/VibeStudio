#include "core/language_server.h"
#include "core/text_document.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {
namespace {
constexpr qsizetype frameLimit = 16 * 1024 * 1024;

QString pathKey(const QString& path)
{
	QString key = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
	key = key.toCaseFolded();
#endif
	return key;
}
} // namespace

bool languageProjectSourcePath(const QString& root, const QString& path)
{
	if (path.isEmpty() || path.size() > 32768 || !QDir::isAbsolutePath(path) || path.contains(QChar(0))) { return false; }
	const auto inside = [&](const QString& candidate) {
		const QString relative = QDir(root).relativeFilePath(candidate);
		return !QDir::isAbsolutePath(relative) && relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../"));
	};
	QString current = QDir::cleanPath(path);
	if (!inside(current) || pathKey(current) == pathKey(root)) { return false; }
	// A deleted open file may still synchronize, but no source or ancestor may
	// redirect a project buffer through a symbolic link or Windows junction.
	while (pathKey(current) != pathKey(root)) {
		const QFileInfo info(current);
		if (info.isSymLink() || info.isJunction() || (info.exists() && !inside(info.canonicalFilePath()))) { return false; }
		const QString parent = info.absolutePath();
		if (parent == current) { return false; }
		current = parent;
	}
	return true;
}
namespace {
bool integer(const QJsonValue& value, int* result)
{
	if (!value.isDouble()) { return false; }
	const double number = value.toDouble();
	// Reserve room for the one-based editor and CLI location conversion.
	if (number < 0 || number >= std::numeric_limits<int>::max() || std::floor(number) != number) { return false; }
	*result = int(number);
	return true;
}
bool location(const QString& uri, const QJsonObject& range, LanguageLocation* result)
{
	result->filePath = languageServerPath(uri);
	const auto start = range.value(QStringLiteral("start")).toObject();
	const auto end = range.value(QStringLiteral("end")).toObject();
	return !result->filePath.isEmpty() && integer(start.value(QStringLiteral("line")), &result->line)
		&& integer(start.value(QStringLiteral("character")), &result->character) && integer(end.value(QStringLiteral("line")), &result->endLine)
		&& integer(end.value(QStringLiteral("character")), &result->endCharacter)
		&& (result->endLine > result->line || (result->endLine == result->line && result->endCharacter >= result->character));
}
QJsonObject position(int line, int character) { return {{QStringLiteral("line"), line}, {QStringLiteral("character"), character}}; }
}

QString languageServerUri(const QString& path)
{
	return QUrl::fromLocalFile(QDir::cleanPath(QFileInfo(path).absoluteFilePath())).toString(QUrl::FullyEncoded);
}
QString languageServerPath(const QString& uri)
{
	const QUrl url(uri, QUrl::StrictMode);
	if (!url.isValid() || !url.isLocalFile() || url.hasQuery() || url.hasFragment() || !url.userInfo().isEmpty()) { return {}; }
	const QString path = url.toLocalFile();
	return !QDir::isAbsolutePath(path) || path.contains(QChar(0)) ? QString() : QDir::cleanPath(path);
}
QString languageServerStateLabel(const QString& state)
{
	if (state == QStringLiteral("starting")) { return QCoreApplication::translate("LanguageServer", "Starting language server…"); }
	if (state == QStringLiteral("initializing")) { return QCoreApplication::translate("LanguageServer", "Initializing language server…"); }
	if (state == QStringLiteral("ready")) { return QCoreApplication::translate("LanguageServer", "Connected"); }
	if (state == QStringLiteral("stopping")) { return QCoreApplication::translate("LanguageServer", "Disconnecting language server…"); }
	if (state == QStringLiteral("failed")) { return QCoreApplication::translate("LanguageServer", "Language server failed"); }
	return QCoreApplication::translate("LanguageServer", "Disconnected");
}

void LanguageServerFramer::clear() { m_buffer.clear(); m_length = -1; }
QByteArray LanguageServerFramer::encode(const QJsonObject& value)
{
	const QByteArray body = QJsonDocument(value).toJson(QJsonDocument::Compact);
	return "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}
bool LanguageServerFramer::append(const QByteArray& bytes, QVector<QJsonObject>* messages, QString* error)
{
	const auto fail = [&](const QString& why) { if (error) { *error = why; } clear(); return false; };
	if (!messages || bytes.size() > frameLimit + 8192 || m_buffer.size() + bytes.size() > frameLimit + 73728) {
		return fail(QCoreApplication::translate("LanguageServer", "Language server message exceeds the receive limit."));
	}
	m_buffer += bytes;
	for (;;) {
		if (m_length < 0) {
			const auto end = m_buffer.indexOf("\r\n\r\n");
			if (end < 0) { return m_buffer.size() <= 8192 || fail(QCoreApplication::translate("LanguageServer", "Language server header is too large.")); }
			if (end > 8192) { return fail(QCoreApplication::translate("LanguageServer", "Language server header is too large.")); }
			bool found = false;
			for (const auto& line : m_buffer.left(end).split('\n')) {
				const auto colon = line.indexOf(':');
				if (colon < 1) { return fail(QCoreApplication::translate("LanguageServer", "Malformed language server header.")); }
				const auto name = line.left(colon).trimmed().toLower();
				const auto value = line.mid(colon + 1).trimmed();
				if (name == "content-length") {
					bool valid = false;
					const qint64 length = value.toLongLong(&valid);
					if (found || !valid || value.isEmpty() || std::any_of(value.cbegin(), value.cend(), [](char ch) { return ch < '0' || ch > '9'; }) || length < 2 || length > frameLimit) {
						return fail(QCoreApplication::translate("LanguageServer", "Invalid language server Content-Length."));
					}
					found = true; m_length = qsizetype(length);
				} else if (name == "content-type" && value.toLower().contains("charset=") && !value.toLower().endsWith("charset=utf-8") && !value.toLower().endsWith("charset=utf8")) {
					return fail(QCoreApplication::translate("LanguageServer", "Language server messages must use UTF-8."));
				}
			}
			if (!found) { return fail(QCoreApplication::translate("LanguageServer", "Language server header has no Content-Length.")); }
			m_buffer.remove(0, end + 4);
		}
		if (m_buffer.size() < m_length) { return true; }
		const auto body = m_buffer.left(m_length);
		QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
		const QString decoded = decoder(body);
		QJsonParseError parseError;
		const auto document = QJsonDocument::fromJson(body, &parseError);
		if (decoder.hasError() || !document.isObject() || parseError.error != QJsonParseError::NoError
			|| document.object().value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0")) {
			return fail(QCoreApplication::translate("LanguageServer", "Language server sent invalid JSON-RPC."));
		}
		if (messages->size() >= 256) { return fail(QCoreApplication::translate("LanguageServer", "Language server message rate exceeded the processing limit.")); }
		messages->push_back(document.object());
		m_buffer.remove(0, m_length); m_length = -1;
		if (m_buffer.isEmpty()) { return true; }
	}
}

LanguageServerClient::LanguageServerClient(QObject* parent) : QObject(parent), m_process(new QProcess(this)), m_timer(new QTimer(this))
{
	m_clock.start();
	m_timer->setInterval(100);
	connect(m_timer, &QTimer::timeout, this, [this]() { checkTimeouts(); });
	connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() { readOutput(); });
	connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
		const auto bytes = m_process->readAllStandardError();
		log(QString::fromUtf8(bytes.left(16384)));
	});
	connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (m_state != QStringLiteral("stopping") && m_state != QStringLiteral("failed") && error != QProcess::UnknownError) { fail(m_process->errorString()); }
	});
	connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
		m_timer->stop();
		if (m_state == QStringLiteral("stopping")) { m_pending.clear(); clearDocuments(); setState(QStringLiteral("stopped")); }
		else if (m_state != QStringLiteral("failed")) { fail(QCoreApplication::translate("LanguageServer", "Language server exited unexpectedly (%1).").arg(code)); }
	});
	connect(m_process, &QProcess::started, this, [this]() {
		if (m_state != QStringLiteral("starting")) { m_process->kill(); return; }
		setState(QStringLiteral("initializing"));
		const QJsonObject capabilities {
			{QStringLiteral("general"), QJsonObject {{QStringLiteral("positionEncodings"), QJsonArray {QStringLiteral("utf-16")}}}},
			{QStringLiteral("workspace"), QJsonObject {{QStringLiteral("workspaceFolders"), true}, {QStringLiteral("configuration"), true}, {QStringLiteral("applyEdit"), false},
				{QStringLiteral("diagnostics"), QJsonObject {{QStringLiteral("refreshSupport"), true}}},
				{QStringLiteral("workspaceEdit"), QJsonObject {{QStringLiteral("documentChanges"), true}, {QStringLiteral("resourceOperations"), QJsonArray {}}, {QStringLiteral("failureHandling"), QStringLiteral("abort")}}}}},
			{QStringLiteral("textDocument"), QJsonObject {
				{QStringLiteral("codeAction"), QJsonObject {{QStringLiteral("dynamicRegistration"), false}, {QStringLiteral("isPreferredSupport"), true}, {QStringLiteral("disabledSupport"), true},
					{QStringLiteral("dataSupport"), true}, {QStringLiteral("honorsChangeAnnotations"), false}, {QStringLiteral("resolveSupport"), QJsonObject {{QStringLiteral("properties"), QJsonArray {QStringLiteral("edit")}}}},
					{QStringLiteral("codeActionLiteralSupport"), QJsonObject {{QStringLiteral("codeActionKind"), QJsonObject {{QStringLiteral("valueSet"), QJsonArray {
						QStringLiteral(""), QStringLiteral("quickfix"), QStringLiteral("refactor"), QStringLiteral("refactor.extract"), QStringLiteral("refactor.inline"), QStringLiteral("refactor.rewrite"),
						QStringLiteral("source"), QStringLiteral("source.organizeImports"), QStringLiteral("source.fixAll")}}}}}}}},
				{QStringLiteral("rename"), QJsonObject {{QStringLiteral("dynamicRegistration"), false}, {QStringLiteral("prepareSupport"), true}, {QStringLiteral("honorsChangeAnnotations"), false}}},
				{QStringLiteral("formatting"), QJsonObject {{QStringLiteral("dynamicRegistration"), false}}},
				{QStringLiteral("rangeFormatting"), QJsonObject {{QStringLiteral("dynamicRegistration"), false}}},
				{QStringLiteral("synchronization"), QJsonObject {{QStringLiteral("dynamicRegistration"), false}, {QStringLiteral("didSave"), true}}},
				{QStringLiteral("diagnostic"), QJsonObject {{QStringLiteral("dynamicRegistration"), false}, {QStringLiteral("relatedDocumentSupport"), false}, {QStringLiteral("dataSupport"), true}}},
				{QStringLiteral("definition"), QJsonObject {{QStringLiteral("linkSupport"), true}}},
				{QStringLiteral("references"), QJsonObject {{QStringLiteral("dynamicRegistration"), false}}},
				{QStringLiteral("hover"), QJsonObject {{QStringLiteral("dynamicRegistration"), false}, {QStringLiteral("contentFormat"), QJsonArray {QStringLiteral("markdown"), QStringLiteral("plaintext")}}}},
				{QStringLiteral("signatureHelp"), QJsonObject {{QStringLiteral("dynamicRegistration"), false}, {QStringLiteral("contextSupport"), true},
					{QStringLiteral("signatureInformation"), QJsonObject {{QStringLiteral("documentationFormat"), QJsonArray {QStringLiteral("markdown"), QStringLiteral("plaintext")}},
						{QStringLiteral("activeParameterSupport"), true}, {QStringLiteral("parameterInformation"), QJsonObject {{QStringLiteral("labelOffsetSupport"), true}}}}}}},
				{QStringLiteral("completion"), QJsonObject {{QStringLiteral("dynamicRegistration"), false}, {QStringLiteral("contextSupport"), true},
					{QStringLiteral("insertTextMode"), 1}, {QStringLiteral("completionItem"), QJsonObject {{QStringLiteral("snippetSupport"), true},
						{QStringLiteral("commitCharactersSupport"), false}, {QStringLiteral("insertReplaceSupport"), true}, {QStringLiteral("deprecatedSupport"), true},
						{QStringLiteral("labelDetailsSupport"), true}, {QStringLiteral("documentationFormat"), QJsonArray {QStringLiteral("plaintext")}},
						{QStringLiteral("resolveSupport"), QJsonObject {{QStringLiteral("properties"), QJsonArray {QStringLiteral("detail"), QStringLiteral("documentation"), QStringLiteral("additionalTextEdits")}}}},
						{QStringLiteral("insertTextModeSupport"), QJsonObject {{QStringLiteral("valueSet"), QJsonArray {1}}}}}},
					{QStringLiteral("completionList"), QJsonObject {{QStringLiteral("itemDefaults"), QJsonArray {QStringLiteral("editRange"), QStringLiteral("insertTextFormat"), QStringLiteral("insertTextMode"), QStringLiteral("data")}}}}}},
				{QStringLiteral("publishDiagnostics"), QJsonObject {{QStringLiteral("versionSupport"), true}, {QStringLiteral("dataSupport"), true}}}}}
		};
		request(QStringLiteral("initialize"), {{QStringLiteral("processId"), QCoreApplication::applicationPid()},
			{QStringLiteral("clientInfo"), QJsonObject {{QStringLiteral("name"), QStringLiteral("VibeStudio")}}},
			{QStringLiteral("rootUri"), languageServerUri(m_config.rootPath)}, {QStringLiteral("capabilities"), capabilities},
			{QStringLiteral("workspaceFolders"), QJsonArray {QJsonObject {{QStringLiteral("uri"), languageServerUri(m_config.rootPath)}, {QStringLiteral("name"), QFileInfo(m_config.rootPath).fileName()}}}}});
	});
}
LanguageServerClient::~LanguageServerClient()
{
	changed = {}; diagnosticsChanged = {};
	disconnect(m_process, nullptr, this, nullptr);
	if (m_process->state() != QProcess::NotRunning) { m_process->kill(); m_process->waitForFinished(1000); }
}
void LanguageServerClient::setState(const QString& state) { m_state = state; if (changed) { changed(); } }
void LanguageServerClient::log(const QString& text)
{
	for (const auto& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) { m_log << line.left(2000); }
	while (m_log.size() > 128) { m_log.removeFirst(); }
}
void LanguageServerClient::clearDocuments()
{
	const auto old = m_documents;
	m_documents.clear();
	if (diagnosticsChanged) { for (const auto& document : old) { diagnosticsChanged({document.source.filePath}); } }
}
void LanguageServerClient::fail(const QString& error)
{
	if (m_state == QStringLiteral("failed")) { return; }
	m_error = error; log(error);
	m_pending.clear(); clearDocuments(); m_timer->stop();
	setState(QStringLiteral("failed"));
	if (m_process->state() != QProcess::NotRunning) { m_process->kill(); }
}
bool LanguageServerClient::start(const LanguageServerConfig& config)
{
	if (m_process->state() != QProcess::NotRunning) { return false; }
	m_error.clear(); m_log.clear(); m_framer.clear(); m_pending.clear(); clearDocuments();
	m_definition = false; m_completion = false; m_completionResolve = false; m_references = false; m_hover = false; m_completionTriggers.clear(); m_syncKind = 0; m_serverName.clear();
	m_signatureHelp = false; m_signatureTriggers.clear(); m_signatureRetriggers.clear();
	m_formatting = false; m_rangeFormatting = false;
	m_rename = false; m_prepareRename = false;
	m_codeActions = false; m_codeActionResolve = false;
	m_pullDiagnostics = false; m_diagnosticDependencies = false; m_diagnosticIdentifier.clear(); m_didSave = false; m_saveText = false;
	m_config = config;
	const QFileInfo program(config.program), root(config.rootPath);
	if (!QDir::isAbsolutePath(config.program) || !program.isFile() || !program.isExecutable() || !QDir::isAbsolutePath(config.rootPath)
		|| !root.isDir() || root.isSymLink() || root.isJunction() || root.canonicalFilePath().isEmpty()
		|| config.rootPath.isEmpty() || config.arguments.size() > 128 || config.timeoutMs < 100 || config.timeoutMs > 120000) {
		m_state = QStringLiteral("stopped"); fail(QCoreApplication::translate("LanguageServer", "Choose an absolute language server executable and an existing project directory.")); return false;
	}
	for (const auto& argument : config.arguments) {
		if (argument.size() > 8192 || argument.contains(QChar(0))) { m_state = QStringLiteral("stopped"); fail(QCoreApplication::translate("LanguageServer", "A language server argument is invalid or too long.")); return false; }
	}
	m_config.rootPath = root.canonicalFilePath();
	m_process->setProgram(program.absoluteFilePath()); m_process->setArguments(config.arguments);
	m_process->setWorkingDirectory(m_config.rootPath);
	m_deadline = m_clock.elapsed() + config.timeoutMs;
	setState(QStringLiteral("starting")); m_timer->start(); m_process->start(); return true;
}
bool LanguageServerClient::send(const QJsonObject& value)
{
	const auto bytes = LanguageServerFramer::encode(value);
	if (bytes.size() > frameLimit || m_process->bytesToWrite() + bytes.size() > 64ll * 1024 * 1024) { fail(QCoreApplication::translate("LanguageServer", "Language server send queue exceeded its size limit.")); return false; }
	if (m_process->write(bytes) != bytes.size()) { fail(QCoreApplication::translate("LanguageServer", "Unable to send a language server message.")); return false; }
	return true;
}
void LanguageServerClient::notify(const QString& method, const QJsonObject& params)
{
	send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), method}, {QStringLiteral("params"), params}});
}
int LanguageServerClient::request(const QString& method, const QJsonObject& params, JsonReply reply)
{
	if (m_pending.size() >= 32) { return -1; }
	const int id = ++m_nextId;
	m_pending.insert(id, {method, m_clock.elapsed() + m_config.timeoutMs, std::move(reply)});
	if (!send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("method"), method}, {QStringLiteral("params"), params}})) { return -1; }
	return id;
}
void LanguageServerClient::stop()
{
	if (m_state == QStringLiteral("stopped") || m_state == QStringLiteral("stopping")) { return; }
	const bool initialized = ready();
	m_pending.clear(); clearDocuments();
	setState(QStringLiteral("stopping")); m_deadline = m_clock.elapsed() + 1500; m_timer->start();
	if (m_process->state() == QProcess::NotRunning) { m_timer->stop(); setState(QStringLiteral("stopped")); }
	else if (initialized) { request(QStringLiteral("shutdown"), {}); }
	else { m_process->kill(); }
}
void LanguageServerClient::readOutput()
{
	if (m_state == QStringLiteral("failed")) { m_process->readAllStandardOutput(); return; }
	m_process->setReadChannel(QProcess::StandardOutput);
	QVector<QJsonObject> messages;
	QString error;
	if (!m_framer.append(m_process->read(65536), &messages, &error)) { fail(error); return; }
	for (const auto& value : messages) { receive(value); if (m_state == QStringLiteral("failed")) { return; } }
	if (m_process->bytesAvailable() > 0) { QTimer::singleShot(0, this, [this]() { readOutput(); }); }
}
void LanguageServerClient::receive(const QJsonObject& value)
{
	const QString method = value.value(QStringLiteral("method")).toString();
	const auto params = value.value(QStringLiteral("params")).toObject();
	if (!method.isEmpty() && value.contains(QStringLiteral("id"))) {
		QJsonObject response {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), value.value(QStringLiteral("id"))}};
		if (method == QStringLiteral("workspace/configuration")) {
			QJsonArray settings; for (const auto& item : params.value(QStringLiteral("items")).toArray()) { Q_UNUSED(item); settings << QJsonValue(QJsonValue::Null); }
			response.insert(QStringLiteral("result"), settings);
		} else if (method == QStringLiteral("workspace/workspaceFolders")) {
			response.insert(QStringLiteral("result"), QJsonArray {QJsonObject {{QStringLiteral("uri"), languageServerUri(m_config.rootPath)}, {QStringLiteral("name"), QFileInfo(m_config.rootPath).fileName()}}});
		} else if (method == QStringLiteral("workspace/applyEdit")) {
			response.insert(QStringLiteral("result"), QJsonObject {{QStringLiteral("applied"), false}, {QStringLiteral("failureReason"), QCoreApplication::translate("LanguageServer", "Server-initiated edits are not supported.")}});
		} else if (method == QStringLiteral("workspace/diagnostic/refresh") && ready() && m_pullDiagnostics) {
			refreshDiagnostics(); response.insert(QStringLiteral("result"), QJsonValue(QJsonValue::Null));
		} else if (method == QStringLiteral("window/showMessageRequest")) { response.insert(QStringLiteral("result"), QJsonValue(QJsonValue::Null)); }
		else { response.insert(QStringLiteral("error"), QJsonObject {{QStringLiteral("code"), -32601}, {QStringLiteral("message"), QCoreApplication::translate("LanguageServer", "This client does not support the requested server operation.")}}); }
		send(response); return;
	}
	if (method == QStringLiteral("window/logMessage") || method == QStringLiteral("window/showMessage")) { log(params.value(QStringLiteral("message")).toString().left(8192)); return; }
	if (method == QStringLiteral("textDocument/publishDiagnostics") && ready()) {
		const auto key = pathKey(languageServerPath(params.value(QStringLiteral("uri")).toString()));
		auto found = m_documents.find(key);
		if (found == m_documents.end() || m_pullDiagnostics) { return; }
		int version = -1; const bool versioned = integer(params.value(QStringLiteral("version")), &version);
		if (versioned && version != found->version) { return; }
		const auto diagnostics = parseLanguageDiagnostics(params.value(QStringLiteral("diagnostics")), found->source, version, versioned, QStringLiteral("push"));
		found->diagnostics = diagnostics;
		if (diagnosticsChanged) { diagnosticsChanged(diagnostics); } return;
	}
	if (!method.isEmpty()) { return; }
	int id = -1;
	if (!integer(value.value(QStringLiteral("id")), &id) || !m_pending.contains(id)) { return; }
	const auto pending = m_pending.take(id);
	const auto error = value.value(QStringLiteral("error")).toObject();
	if (!error.isEmpty()) {
		if (pending.method == QStringLiteral("textDocument/diagnostic") && retryDiagnostics(id, error)) { return; }
		const QString why = error.value(QStringLiteral("message")).toString().left(8192);
		if (pending.method == QStringLiteral("initialize")) { fail(why); }
		else if (pending.reply) { pending.reply({}, why.isEmpty() ? QCoreApplication::translate("LanguageServer", "Language server request failed.") : why); }
		return;
	}
	const auto result = value.value(QStringLiteral("result"));
	if (pending.method == QStringLiteral("initialize")) {
		const auto caps = result.toObject().value(QStringLiteral("capabilities")).toObject();
		const auto sync = caps.value(QStringLiteral("textDocumentSync"));
		m_syncKind = sync.isObject() ? sync.toObject().value(QStringLiteral("change")).toInt() : sync.toInt();
		const auto save = sync.toObject().value(QStringLiteral("save"));
		m_didSave = save.toBool() || save.isObject(); m_saveText = save.toObject().value(QStringLiteral("includeText")).toBool();
		const auto diagnostic = caps.value(QStringLiteral("diagnosticProvider")); m_pullDiagnostics = diagnostic.isObject();
		m_diagnosticDependencies = diagnostic.toObject().value(QStringLiteral("interFileDependencies")).toBool();
		m_diagnosticIdentifier = diagnostic.toObject().value(QStringLiteral("identifier")).toString();
		if (m_diagnosticIdentifier.size() > 1024 || !m_diagnosticIdentifier.isValidUtf16() || m_diagnosticIdentifier.contains(QChar(0))) {
			fail(QCoreApplication::translate("LanguageServer", "The diagnostic provider identifier is invalid or too long.")); return;
		}
		if (caps.value(QStringLiteral("positionEncoding")).toString(QStringLiteral("utf-16")) != QStringLiteral("utf-16")
			|| (m_syncKind != 1 && m_syncKind != 2) || (sync.isObject() && !sync.toObject().value(QStringLiteral("openClose")).toBool())) {
			fail(QCoreApplication::translate("LanguageServer", "This server must support UTF-16 positions and open/change/close synchronization.")); return;
		}
		m_definition = caps.value(QStringLiteral("definitionProvider")).toBool() || caps.value(QStringLiteral("definitionProvider")).isObject();
		m_references = caps.value(QStringLiteral("referencesProvider")).toBool() || caps.value(QStringLiteral("referencesProvider")).isObject();
		m_hover = caps.value(QStringLiteral("hoverProvider")).toBool() || caps.value(QStringLiteral("hoverProvider")).isObject();
		m_signatureHelp = caps.value(QStringLiteral("signatureHelpProvider")).isObject();
		const auto signatureOptions = caps.value(QStringLiteral("signatureHelpProvider")).toObject();
		for (const auto& field : {QStringLiteral("triggerCharacters"), QStringLiteral("retriggerCharacters")}) {
			auto& target = field == QStringLiteral("triggerCharacters") ? m_signatureTriggers : m_signatureRetriggers;
			for (const auto& entry : signatureOptions.value(field).toArray()) {
				const auto trigger = entry.toString();
				if (!trigger.isEmpty() && trigger.size() <= 8 && trigger.isValidUtf16() && !target.contains(trigger) && target.size() < 32) { target << trigger; }
			}
		}
		for (const auto& trigger : m_signatureTriggers) { if (!m_signatureRetriggers.contains(trigger)) { m_signatureRetriggers << trigger; } }
		m_formatting = caps.value(QStringLiteral("documentFormattingProvider")).toBool() || caps.value(QStringLiteral("documentFormattingProvider")).isObject();
		m_rename = caps.value(QStringLiteral("renameProvider")).toBool() || caps.value(QStringLiteral("renameProvider")).isObject();
		m_prepareRename = caps.value(QStringLiteral("renameProvider")).toObject().value(QStringLiteral("prepareProvider")).toBool();
		m_codeActions = caps.value(QStringLiteral("codeActionProvider")).toBool() || caps.value(QStringLiteral("codeActionProvider")).isObject();
		m_codeActionResolve = caps.value(QStringLiteral("codeActionProvider")).toObject().value(QStringLiteral("resolveProvider")).toBool();
		m_rangeFormatting = caps.value(QStringLiteral("documentRangeFormattingProvider")).toBool() || caps.value(QStringLiteral("documentRangeFormattingProvider")).isObject();
		m_completion = caps.value(QStringLiteral("completionProvider")).isObject();
		m_completionResolve = caps.value(QStringLiteral("completionProvider")).toObject().value(QStringLiteral("resolveProvider")).toBool();
		for (const auto& entry : caps.value(QStringLiteral("completionProvider")).toObject().value(QStringLiteral("triggerCharacters")).toArray()) {
			const QString trigger = entry.toString();
			if (m_completionTriggers.size() < 32 && trigger.size() == 1 && !trigger.at(0).isSurrogate() && !trigger.at(0).isNull()) { m_completionTriggers << trigger; }
		}
		m_serverName = result.toObject().value(QStringLiteral("serverInfo")).toObject().value(QStringLiteral("name")).toString().left(200);
		notify(QStringLiteral("initialized"));
		if (m_state != QStringLiteral("failed")) { setState(QStringLiteral("ready")); }
	} else if (pending.method == QStringLiteral("shutdown")) {
		notify(QStringLiteral("exit")); m_process->closeWriteChannel();
	} else if (pending.reply) { pending.reply(result, {}); }
}
void LanguageServerClient::checkTimeouts()
{
	const qint64 now = m_clock.elapsed();
	if (m_state == QStringLiteral("stopping") && now >= m_deadline) { m_process->kill(); return; }
	if (m_state == QStringLiteral("starting") && now >= m_deadline) { fail(QCoreApplication::translate("LanguageServer", "Language server startup timed out.")); return; }
	const auto ids = m_pending.keys();
	for (const int id : ids) {
		if (!m_pending.contains(id) || m_pending.value(id).deadline > now) { continue; }
		const auto pending = m_pending.take(id);
		if (pending.method == QStringLiteral("initialize")) { fail(QCoreApplication::translate("LanguageServer", "Language server initialization timed out.")); return; }
		notify(QStringLiteral("$/cancelRequest"), {{QStringLiteral("id"), id}});
		if (pending.reply) { pending.reply({}, QCoreApplication::translate("LanguageServer", "Language server request timed out.")); }
	}
	dispatchDiagnostics();
}
void LanguageServerClient::cancelRequest(int id)
{
	if (!m_pending.contains(id) || !m_pending.value(id).reply) { return; }
	m_pending.remove(id); notify(QStringLiteral("$/cancelRequest"), {{QStringLiteral("id"), id}});
	for (auto it = m_documents.begin(); it != m_documents.end(); ++it) {
		if (it->diagnosticRequest == id) { it->diagnosticRequest = -1; it->diagnosticQueued = true; }
	}
}
void LanguageServerClient::cancelDocumentRequests()
{
	for (const int id : m_pending.keys()) { cancelRequest(id); }
}
int LanguageServerClient::documentVersion(const QString& path) const { return m_documents.value(pathKey(path)).version; }
bool LanguageServerClient::matchesDocument(const QString& path, const QString& text) const
{
	const auto found = m_documents.constFind(pathKey(path));
	return found != m_documents.cend() && found->source.text == text;
}
LanguageDiagnostics LanguageServerClient::diagnostics(const QString& path) const { return m_documents.value(pathKey(path)).diagnostics; }
void LanguageServerClient::refreshDiagnostics(const QString& path)
{
	if (!ready() || !m_pullDiagnostics) { return; }
	if (path.isEmpty()) { for (const auto& key : m_documents.keys()) { queueDiagnostics(key); } }
	else { queueDiagnostics(pathKey(path)); }
}
bool LanguageServerClient::documentSaved(const QString& path, const QString& savedText)
{
	if (!ready() || !matchesDocument(path, savedText)) { return false; }
	const auto document = m_documents.value(pathKey(path));
	QJsonObject params {{QStringLiteral("textDocument"), QJsonObject {{QStringLiteral("uri"), languageServerUri(document.source.filePath)}}}};
	if (m_saveText) { params.insert(QStringLiteral("text"), savedText); }
	if (m_didSave) { notify(QStringLiteral("textDocument/didSave"), params); }
	if (m_diagnosticDependencies) { refreshDiagnostics(); } else { refreshDiagnostics(path); }
	return ready() && m_didSave;
}
QString LanguageServerClient::synchronize(const QVector<LanguageDocument>& documents)
{
	if (!ready()) { return QCoreApplication::translate("LanguageServer", "Connect a language server before synchronizing documents."); }
	if (documents.size() > 128) { return QCoreApplication::translate("LanguageServer", "Too many open documents for this language server connection."); }
	QHash<QString, LanguageDocument> wanted; qint64 total = 0;
	for (const auto& document : documents) {
		const qint64 size = document.text.toUtf8().size(); total += size;
		if (!languageProjectSourcePath(m_config.rootPath, document.filePath)
			|| !document.text.isValidUtf16() || document.text.contains(QChar(0)) || document.languageId.isEmpty() || document.languageId.size() > 64
			|| size > textDocumentByteLimit || total > 64ll * 1024 * 1024 || wanted.contains(pathKey(document.filePath))) {
			return QCoreApplication::translate("LanguageServer", "A document is outside the project, duplicated, invalid or too large for language service synchronization.");
		}
		wanted.insert(pathKey(document.filePath), document);
	}
	bool changedDocuments = false;
	for (const auto& key : m_documents.keys()) {
		if (wanted.contains(key)) { continue; }
		changedDocuments = true;
		cancelDocumentRequests();
		if (!ready()) { return m_error; }
		const auto removed = m_documents.take(key);
		notify(QStringLiteral("textDocument/didClose"), {{QStringLiteral("textDocument"), QJsonObject {{QStringLiteral("uri"), languageServerUri(removed.source.filePath)}}}});
		if (!ready()) { return m_error; }
		if (diagnosticsChanged) { diagnosticsChanged({removed.source.filePath}); }
	}
	for (auto it = wanted.cbegin(); it != wanted.cend(); ++it) {
		auto old = m_documents.find(it.key());
		if (old != m_documents.end() && old->source.text == it->text && old->source.languageId == it->languageId) { continue; }
		changedDocuments = true;
		cancelDocumentRequests();
		if (!ready()) { return m_error; }
		const bool opening = old == m_documents.end() || old->source.languageId != it->languageId;
		if (opening && old != m_documents.end()) {
			notify(QStringLiteral("textDocument/didClose"), {{QStringLiteral("textDocument"), QJsonObject {{QStringLiteral("uri"), languageServerUri(old->source.filePath)}}}});
			if (!ready()) { return m_error; }
		}
		const int version = ++m_nextVersion;
		const QString uri = languageServerUri(it->filePath);
		QJsonObject identifier {{QStringLiteral("uri"), uri}, {QStringLiteral("version"), version}};
		if (opening) {
			identifier.insert(QStringLiteral("languageId"), it->languageId); identifier.insert(QStringLiteral("text"), it->text);
			notify(QStringLiteral("textDocument/didOpen"), {{QStringLiteral("textDocument"), identifier}});
		} else {
			QJsonObject change {{QStringLiteral("text"), it->text}};
			if (m_syncKind == 2) {
				const auto& prior = old->source.text;
				const auto lastBreak = prior.lastIndexOf(QLatin1Char('\n'));
				change.insert(QStringLiteral("range"), QJsonObject {{QStringLiteral("start"), position(0, 0)},
					{QStringLiteral("end"), position(int(prior.count(QLatin1Char('\n'))), int(prior.size() - lastBreak - 1))}});
			}
			notify(QStringLiteral("textDocument/didChange"), {{QStringLiteral("textDocument"), identifier}, {QStringLiteral("contentChanges"), QJsonArray {change}}});
		}
		if (!ready()) { return m_error; }
		Document updated; updated.source = *it; updated.version = version;
		if (!opening) { updated.diagnosticCache = old->diagnosticCache; updated.diagnosticResultId = old->diagnosticResultId; }
		m_documents.insert(it.key(), updated);
		if (diagnosticsChanged) { diagnosticsChanged({it->filePath}); }
		queueDiagnostics(it.key());
	}
	if (changedDocuments && m_diagnosticDependencies) { refreshDiagnostics(); }
	return {};
}
int LanguageServerClient::definition(const QString& path, int line, int character, DefinitionReply reply)
{
	if (!ready() || !m_definition || !m_documents.contains(pathKey(path)) || line < 0 || character < 0) { return -1; }
	return request(QStringLiteral("textDocument/definition"), {{QStringLiteral("textDocument"), QJsonObject {{QStringLiteral("uri"), languageServerUri(m_documents.value(pathKey(path)).source.filePath)}}},
		{QStringLiteral("position"), position(line, character)}}, [reply = std::move(reply)](const QJsonValue& result, const QString& error) {
			QVector<LanguageLocation> locations;
			const auto entries = result.isArray() ? result.toArray() : result.isObject() ? QJsonArray {result} : QJsonArray {};
			for (const auto& entry : entries) {
				if (locations.size() >= 128) { break; }
				const auto object = entry.toObject(); LanguageLocation target;
				const bool link = object.contains(QStringLiteral("targetUri"));
				if (location(object.value(link ? QStringLiteral("targetUri") : QStringLiteral("uri")).toString(),
					object.value(link ? QStringLiteral("targetSelectionRange") : QStringLiteral("range")).toObject(), &target)) { locations << target; }
			}
			if (reply) { reply(locations, error); }
		});
}

int LanguageServerClient::signatureHelp(const QString& path, int line, int character, int triggerKind, const QString& triggerCharacter,
	bool retrigger, const LanguageSignatureHelp& active, SignatureHelpReply reply)
{
	const auto found = m_documents.constFind(pathKey(path));
	if (!ready() || !m_signatureHelp || found == m_documents.cend() || languageSourceOffset(found->source.text, line, character) < 0
		|| triggerKind < 1 || triggerKind > 3 || (triggerKind == 2 && !signatureTriggers(retrigger).contains(triggerCharacter))) { return -1; }
	const auto document = *found;
	QJsonObject context {{QStringLiteral("triggerKind"), triggerKind}, {QStringLiteral("isRetrigger"), retrigger}};
	if (triggerKind == 2) { context.insert(QStringLiteral("triggerCharacter"), triggerCharacter); }
	if (retrigger && !active.signatures.isEmpty() && active.error.isEmpty() && pathKey(active.filePath) == pathKey(path)) {
		const auto help = languageSignatureContext(active);
		if (QJsonDocument(help).toJson(QJsonDocument::Compact).size() > 512 * 1024) { return -1; }
		context.insert(QStringLiteral("activeSignatureHelp"), help);
	}
	return request(QStringLiteral("textDocument/signatureHelp"), {{QStringLiteral("textDocument"), QJsonObject {{QStringLiteral("uri"), languageServerUri(path)}}},
		{QStringLiteral("position"), position(line, character)}, {QStringLiteral("context"), context}},
		[this, document, line, character, reply = std::move(reply)](const QJsonValue& value, const QString& error) {
			if (documentVersion(document.source.filePath) != document.version) { return; }
			auto result = error.isEmpty() ? parseLanguageSignatureHelp(value, document.source.text, line, character) : LanguageSignatureHelp {};
			result.filePath = document.source.filePath; result.version = document.version;
			if (!error.isEmpty()) { result.error = error; } if (reply) { reply(result); }
		});
}

int LanguageServerClient::completion(const QString& path, int line, int character, int triggerKind, const QString& triggerCharacter, CompletionReply reply)
{
	const auto found = m_documents.constFind(pathKey(path));
	if (!ready() || !m_completion || found == m_documents.cend() || languageSourceOffset(found->source.text, line, character) < 0
		|| triggerKind < 1 || triggerKind > 3 || (triggerKind == 2 && !m_completionTriggers.contains(triggerCharacter))) { return -1; }
	const auto document = *found;
	QJsonObject context {{QStringLiteral("triggerKind"), triggerKind}};
	if (triggerKind == 2) { context.insert(QStringLiteral("triggerCharacter"), triggerCharacter); }
	return request(QStringLiteral("textDocument/completion"), {{QStringLiteral("textDocument"), QJsonObject {{QStringLiteral("uri"), languageServerUri(document.source.filePath)}}},
		{QStringLiteral("position"), position(line, character)}, {QStringLiteral("context"), context}},
		[this, document, line, character, reply = std::move(reply)](const QJsonValue& value, const QString& error) {
			if (documentVersion(document.source.filePath) != document.version) { return; }
			auto result = error.isEmpty() ? parseLanguageCompletions(value, document.source.text, line, character, m_completionResolve, document.source.filePath) : LanguageCompletions {};
			result.filePath = document.source.filePath; result.version = document.version;
			if (!error.isEmpty()) { result.error = error; }
			if (reply) { reply(result); }
		});
}

int LanguageServerClient::resolveCompletion(const QString& path, int version, const LanguageCompletionItem& item, CompletionResolveReply reply)
{
	const auto found = m_documents.constFind(pathKey(path));
	if (!ready() || !m_completionResolve || found == m_documents.cend() || version <= 0 || found->version != version
		|| !item.needsResolve || item.wire.isEmpty() || item.sourceSha256 != assetTextSnapshotHash(found->source.text)) { return -1; }
	const auto document = *found;
	return request(QStringLiteral("completionItem/resolve"), item.wire, [this, document, item, reply = std::move(reply)](const QJsonValue& value, const QString& error) {
		if (documentVersion(document.source.filePath) != document.version) { return; }
		QString why = error; LanguageCompletionItem resolved;
		if (why.isEmpty()) { resolved = resolveLanguageCompletion(item, value, document.source.text, &why); }
		if (reply) { reply(resolved, why); }
	});
}

int LanguageServerClient::codeActions(const QString& path, int offset, int length, CodeActionsReply reply)
{
	const auto found = m_documents.constFind(pathKey(path));
	if (!ready() || !m_codeActions || m_pending.size() >= 32 || found == m_documents.cend() || !validLanguageCodeActionRange(found->source.text, offset, length)) { return -1; }
	const auto document = *found; const int id = ++m_nextId;
	m_pending.insert(id, {QStringLiteral("textDocument/codeAction"), m_clock.elapsed() + m_config.timeoutMs,
		[this, document, offset, length, reply = std::move(reply)](const QJsonValue& value, const QString& error) {
			if (documentVersion(document.source.filePath) != document.version) { return; }
			auto result = error.isEmpty() ? parseLanguageCodeActions(value, m_codeActionResolve) : LanguageCodeActions {};
			result.filePath = document.source.filePath; result.version = document.version; result.offset = offset; result.length = length;
			result.sourceSha256 = assetTextSnapshotHash(document.source.text); if (!error.isEmpty()) { result.error = error; } if (reply) { reply(result); }
		}});
	const auto dispatch = [this, id, document, offset, length]() {
		const auto at = [&](int n) { const auto prefix = QStringView(document.source.text).left(n); return position(prefix.count(QLatin1Char('\n')), n - prefix.lastIndexOf(QLatin1Char('\n')) - 1); };
		QJsonArray context; qint64 bytes = 0;
		const auto current = diagnostics(document.source.filePath);
		if (current.versioned && current.version == document.version) {
			for (const auto& diagnostic : current.items) {
				const auto& where = diagnostic.location;
				const int first = languageSourceOffset(document.source.text, where.line, where.character), last = languageSourceOffset(document.source.text, where.endLine, where.endCharacter);
				if (first < 0 || last < first || diagnostic.wire.isEmpty() || (length ? (first >= offset + length || last <= offset) : (first > offset || last < offset))) { continue; }
				bytes += QJsonDocument(diagnostic.wire).toJson(QJsonDocument::Compact).size(); if (context.size() >= 32 || bytes > 1024 * 1024) { break; } context << diagnostic.wire;
			}
		}
		const QJsonObject params {{QStringLiteral("textDocument"), QJsonObject {{QStringLiteral("uri"), languageServerUri(document.source.filePath)}}},
			{QStringLiteral("range"), QJsonObject {{QStringLiteral("start"), at(offset)}, {QStringLiteral("end"), at(offset + length)}}},
			{QStringLiteral("context"), QJsonObject {{QStringLiteral("diagnostics"), context}, {QStringLiteral("triggerKind"), 1}}}};
		send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("method"), QStringLiteral("textDocument/codeAction")}, {QStringLiteral("params"), params}});
	};
	if (document.diagnostics.received) { dispatch(); }
	else {
		// Quick fixes may depend on diagnostic data published after didOpen/change.
		// Keep one cancellable request identity and one overall timeout, while
		// allowing diagnostic-free providers to answer after a bounded grace period.
		auto* grace = new QTimer(this); grace->setInterval(25);
		const qint64 deadline = m_clock.elapsed() + std::min(2000, m_config.timeoutMs / 3);
		connect(grace, &QTimer::timeout, this, [this, grace, id, document, deadline, dispatch]() {
			if (!m_pending.contains(id) || !ready() || documentVersion(document.source.filePath) != document.version) { grace->stop(); grace->deleteLater(); return; }
			if (diagnostics(document.source.filePath).received || m_clock.elapsed() >= deadline) { grace->stop(); grace->deleteLater(); dispatch(); }
		});
		grace->start();
	}
	return id;
}

int LanguageServerClient::resolveCodeAction(const QString& path, int version, const LanguageCodeAction& action, CodeActionResolveReply reply)
{
	if (!ready() || !m_codeActionResolve || !action.available() || !action.needsResolve || version <= 0 || documentVersion(path) != version) { return -1; }
	return request(QStringLiteral("codeAction/resolve"), action.wire, [this, path, version, action, reply = std::move(reply)](const QJsonValue& value, const QString& error) {
		if (documentVersion(path) != version) { return; }
		if (reply) { reply(error.isEmpty() ? resolveLanguageCodeAction(action, value) : action, error); }
	});
}

int LanguageServerClient::prepareRename(const QString& path, int line, int character, PrepareRenameReply reply)
{
	const auto found = m_documents.constFind(pathKey(path));
	if (!ready() || !m_rename || !m_prepareRename || found == m_documents.cend() || languageSourceOffset(found->source.text, line, character) < 0) { return -1; }
	const auto document = *found;
	return request(QStringLiteral("textDocument/prepareRename"), {{QStringLiteral("textDocument"), QJsonObject {{QStringLiteral("uri"), languageServerUri(document.source.filePath)}}},
		{QStringLiteral("position"), position(line, character)}}, [this, document, line, character, reply = std::move(reply)](const QJsonValue& value, const QString& error) {
			if (documentVersion(document.source.filePath) != document.version) { return; }
			auto result = error.isEmpty() ? parseLanguageRenamePreparation(value, document.source.text, languageSourceOffset(document.source.text, line, character)) : LanguageRenamePreparation {};
			result.version = document.version; if (!error.isEmpty()) { result.error = error; } if (reply) { reply(result); }
		});
}

int LanguageServerClient::rename(const QString& path, int line, int character, const QString& newName, RenameReply reply)
{
	const auto found = m_documents.constFind(pathKey(path));
	if (!ready() || !m_rename || found == m_documents.cend() || languageSourceOffset(found->source.text, line, character) < 0 || !validLanguageRenameName(newName)) { return -1; }
	const auto document = *found;
	return request(QStringLiteral("textDocument/rename"), {{QStringLiteral("textDocument"), QJsonObject {{QStringLiteral("uri"), languageServerUri(document.source.filePath)}}},
		{QStringLiteral("position"), position(line, character)}, {QStringLiteral("newName"), newName}},
		[this, document, reply = std::move(reply)](const QJsonValue& value, const QString& error) {
			if (documentVersion(document.source.filePath) != document.version) { return; }
			if (reply) { reply({error, value, document.version}); }
		});
}

int LanguageServerClient::formatting(const QString& path, const LanguageFormattingOptions& options, FormattingReply reply)
{
	const auto found = m_documents.constFind(pathKey(path));
	if (!ready() || !supportsFormatting(options.rangeOffset >= 0) || found == m_documents.cend() || !validLanguageFormattingOptions(found->source.text, options)) { return -1; }
	const QString sourcePath = found->source.filePath, source = found->source.text; const int version = found->version;
	QJsonObject params {{QStringLiteral("textDocument"), QJsonObject {{QStringLiteral("uri"), languageServerUri(sourcePath)}}},
		{QStringLiteral("options"), QJsonObject {{QStringLiteral("tabSize"), options.tabSize}, {QStringLiteral("insertSpaces"), options.insertSpaces}}}};
	if (options.rangeOffset >= 0) {
		const auto at = [&](int offset) { const auto prefix = QStringView(source).left(offset); return position(int(prefix.count(QLatin1Char('\n'))), offset - int(prefix.lastIndexOf(QLatin1Char('\n'))) - 1); };
		params.insert(QStringLiteral("range"), QJsonObject {{QStringLiteral("start"), at(options.rangeOffset)}, {QStringLiteral("end"), at(options.rangeOffset + options.rangeLength)}});
	}
	return request(options.rangeOffset < 0 ? QStringLiteral("textDocument/formatting") : QStringLiteral("textDocument/rangeFormatting"), params,
		[this, sourcePath, source, version, reply = std::move(reply)](const QJsonValue& value, const QString& error) {
			if (documentVersion(sourcePath) != version) { return; }
			auto result = error.isEmpty() ? parseLanguageFormatting(value, source) : LanguageFormatting {};
			result.filePath = sourcePath; result.version = version; if (!error.isEmpty()) { result.error = error; }
			if (reply) { reply(result); }
		});
}

int LanguageServerClient::hover(const QString& path, int line, int character, HoverReply reply)
{
	const auto found = m_documents.constFind(pathKey(path));
	if (!ready() || !m_hover || found == m_documents.cend() || languageSourceOffset(found->source.text, line, character) < 0) { return -1; }
	const QString sourcePath = found->source.filePath, source = found->source.text;
	const int version = found->version;
	return request(QStringLiteral("textDocument/hover"), {{QStringLiteral("textDocument"), QJsonObject {{QStringLiteral("uri"), languageServerUri(sourcePath)}}},
		{QStringLiteral("position"), position(line, character)}},
		[this, sourcePath, source, version, line, character, reply = std::move(reply)](const QJsonValue& value, const QString& error) {
			if (documentVersion(sourcePath) != version) { return; }
			auto result = error.isEmpty() ? parseLanguageHover(value, source, line, character) : LanguageHover {};
			result.filePath = sourcePath; result.version = version;
			if (!error.isEmpty()) { result.error = error; }
			if (reply) { reply(result); }
		});
}

int LanguageServerClient::references(const QString& path, int line, int character, bool includeDeclaration, ReferencesReply reply)
{
	const auto found = m_documents.constFind(pathKey(path));
	if (!ready() || !m_references || found == m_documents.cend() || languageSourceOffset(found->source.text, line, character) < 0) { return -1; }
	const QString sourcePath = found->source.filePath;
	const int version = found->version;
	// Original LSP 3.17 references interface implementation (Microsoft,
	// CC-BY-4.0; reviewed 2026-10-04), with no copied source or prose.
	// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_references
	return request(QStringLiteral("textDocument/references"), {{QStringLiteral("textDocument"), QJsonObject {{QStringLiteral("uri"), languageServerUri(sourcePath)}}},
		{QStringLiteral("position"), position(line, character)}, {QStringLiteral("context"), QJsonObject {{QStringLiteral("includeDeclaration"), includeDeclaration}}}},
		[this, sourcePath, version, includeDeclaration, reply = std::move(reply)](const QJsonValue& value, const QString& error) {
			if (documentVersion(sourcePath) != version) { return; }
			LanguageReferences result; result.filePath = sourcePath; result.version = version; result.includeDeclaration = includeDeclaration; result.error = error;
			if (error.isEmpty() && !value.isNull() && !value.isArray()) { result.error = QCoreApplication::translate("LanguageServer", "The language server returned an invalid reference list."); }
			if (result.error.isEmpty()) {
				const auto entries = value.toArray(); result.limited = entries.size() > languageReferenceLimit;
				QSet<QString> seen;
				for (qsizetype i = 0; i < std::min<qsizetype>(entries.size(), languageReferenceLimit); ++i) {
					const auto entry = entries[i].toObject(); LanguageLocation at;
					if (!location(entry.value(QStringLiteral("uri")).toString(), entry.value(QStringLiteral("range")).toObject(), &at)) { ++result.skipped; continue; }
					const QString identity = pathKey(at.filePath) + QChar(0) + QStringLiteral("%1:%2:%3:%4").arg(at.line).arg(at.character).arg(at.endLine).arg(at.endCharacter);
					if (!seen.contains(identity)) { seen.insert(identity); result.items << at; }
				}
			}
			if (reply) { reply(result); }
		});
}

} // namespace vibestudio
