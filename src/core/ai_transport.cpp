#include "core/ai_transport.h"

#include "core/ai_connectors.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {

namespace {

enum class WireFormat {
	None,
	OpenAiChat,
	AnthropicMessages,
	GeminiGenerate,
};

WireFormat wireFormatFor(const QString& connectorId)
{
	const QString id = normalizedAiId(connectorId);
	if (id == QStringLiteral("openai") || id == QStringLiteral("local-offline") || id == QStringLiteral("custom-http")) {
		return WireFormat::OpenAiChat;
	}
	if (id == QStringLiteral("claude")) {
		return WireFormat::AnthropicMessages;
	}
	if (id == QStringLiteral("gemini")) {
		return WireFormat::GeminiGenerate;
	}
	return WireFormat::None;
}

QString trimmedBase(const QString& endpoint)
{
	QString base = endpoint.trimmed();
	while (base.endsWith(QLatin1Char('/'))) {
		base.chop(1);
	}
	return base;
}

// Models that take Anthropic's server-side refusal fallback: when the model
// declines, the API routes the request by the refusal's category instead of
// returning the refusal. Only on the first-party API.
bool anthropicModelTakesFallback(const QString& model)
{
	static const QStringList models = {
		QStringLiteral("claude-fable-5-1"),
		QStringLiteral("claude-opus-5-5"),
		QStringLiteral("claude-opus-5"),
		QStringLiteral("claude-sonnet-5-5"),
	};
	return models.contains(model.trimmed());
}

QString jsonErrorMessage(const QJsonObject& root)
{
	const QJsonValue error = root.value(QStringLiteral("error"));
	if (error.isObject()) {
		const QString message = error.toObject().value(QStringLiteral("message")).toString();
		if (!message.isEmpty()) {
			return message;
		}
		return error.toObject().value(QStringLiteral("type")).toString();
	}
	if (error.isString()) {
		return error.toString();
	}
	return root.value(QStringLiteral("message")).toString();
}

AiChatFailure failureForStatus(int status)
{
	if (status == 401 || status == 403) {
		return AiChatFailure::Credential;
	}
	// 529 is Anthropic's "overloaded"; 503 is how Gemini and many proxies say
	// the same. Both pass with time, like a rate limit.
	if (status == 429 || status == 529 || status == 503) {
		return AiChatFailure::RateLimited;
	}
	return AiChatFailure::Provider;
}

int intOr(const QJsonValue& value, int fallback)
{
	return value.isDouble() ? value.toInt() : fallback;
}

void parseOpenAiChat(const QJsonObject& root, AiChatResponse* response)
{
	const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
	if (choices.isEmpty()) {
		response->failure = AiChatFailure::BadResponse;
		response->errorMessage = QCoreApplication::translate("VibeStudioAiTransport", "The provider's answer had no choices in it.");
		return;
	}
	const QJsonObject choice = choices.first().toObject();
	const QJsonObject message = choice.value(QStringLiteral("message")).toObject();
	response->text = message.value(QStringLiteral("content")).toString();
	response->finishReason = choice.value(QStringLiteral("finish_reason")).toString();
	response->truncated = response->finishReason == QStringLiteral("length");
	const QJsonObject usage = root.value(QStringLiteral("usage")).toObject();
	response->inputTokens = intOr(usage.value(QStringLiteral("prompt_tokens")), -1);
	response->outputTokens = intOr(usage.value(QStringLiteral("completion_tokens")), -1);
	response->model = root.value(QStringLiteral("model")).toString();
	if (response->finishReason == QStringLiteral("content_filter")) {
		response->failure = AiChatFailure::Refused;
		const QString refusal = message.value(QStringLiteral("refusal")).toString();
		response->errorMessage = refusal.isEmpty() ? QCoreApplication::translate("VibeStudioAiTransport", "The provider's content filter stopped the answer.") : refusal;
		return;
	}
	if (const QString refusal = message.value(QStringLiteral("refusal")).toString(); !refusal.isEmpty() && response->text.isEmpty()) {
		response->failure = AiChatFailure::Refused;
		response->errorMessage = refusal;
	}
}

void parseAnthropicMessages(const QJsonObject& root, AiChatResponse* response)
{
	// The answer is the text blocks in order; thinking blocks and anything
	// newer are not part of it.
	QStringList parts;
	for (const QJsonValue& value : root.value(QStringLiteral("content")).toArray()) {
		const QJsonObject block = value.toObject();
		if (block.value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
			parts << block.value(QStringLiteral("text")).toString();
		}
	}
	response->text = parts.join(QString());
	response->finishReason = root.value(QStringLiteral("stop_reason")).toString();
	response->truncated = response->finishReason == QStringLiteral("max_tokens");
	const QJsonObject usage = root.value(QStringLiteral("usage")).toObject();
	response->inputTokens = intOr(usage.value(QStringLiteral("input_tokens")), -1);
	response->outputTokens = intOr(usage.value(QStringLiteral("output_tokens")), -1);
	response->model = root.value(QStringLiteral("model")).toString();
	if (response->finishReason == QStringLiteral("refusal")) {
		response->failure = AiChatFailure::Refused;
		const QJsonObject details = root.value(QStringLiteral("stop_details")).toObject();
		QString explanation = details.value(QStringLiteral("explanation")).toString();
		const QString category = details.value(QStringLiteral("category")).toString();
		if (explanation.isEmpty()) {
			explanation = category.isEmpty() ? QCoreApplication::translate("VibeStudioAiTransport", "The model declined to answer.")
											 : QCoreApplication::translate("VibeStudioAiTransport", "The model declined to answer (%1).").arg(category);
		}
		response->errorMessage = explanation;
	}
}

void parseGeminiGenerate(const QJsonObject& root, AiChatResponse* response)
{
	const QJsonArray candidates = root.value(QStringLiteral("candidates")).toArray();
	response->model = root.value(QStringLiteral("modelVersion")).toString();
	const QJsonObject usage = root.value(QStringLiteral("usageMetadata")).toObject();
	response->inputTokens = intOr(usage.value(QStringLiteral("promptTokenCount")), -1);
	response->outputTokens = intOr(usage.value(QStringLiteral("candidatesTokenCount")), -1);
	if (candidates.isEmpty()) {
		const QString blocked = root.value(QStringLiteral("promptFeedback")).toObject().value(QStringLiteral("blockReason")).toString();
		response->failure = blocked.isEmpty() ? AiChatFailure::BadResponse : AiChatFailure::Refused;
		response->errorMessage = blocked.isEmpty() ? QCoreApplication::translate("VibeStudioAiTransport", "The provider's answer had no candidates in it.")
												   : QCoreApplication::translate("VibeStudioAiTransport", "The provider blocked the prompt (%1).").arg(blocked);
		return;
	}
	const QJsonObject candidate = candidates.first().toObject();
	QStringList parts;
	for (const QJsonValue& value : candidate.value(QStringLiteral("content")).toObject().value(QStringLiteral("parts")).toArray()) {
		const QJsonObject part = value.toObject();
		// Parts marked as thought are the model's working, not its answer.
		if (!part.value(QStringLiteral("thought")).toBool()) {
			parts << part.value(QStringLiteral("text")).toString();
		}
	}
	response->text = parts.join(QString());
	response->finishReason = candidate.value(QStringLiteral("finishReason")).toString();
	response->truncated = response->finishReason == QStringLiteral("MAX_TOKENS");
	static const QStringList refusals = {
		QStringLiteral("SAFETY"),
		QStringLiteral("RECITATION"),
		QStringLiteral("BLOCKLIST"),
		QStringLiteral("PROHIBITED_CONTENT"),
		QStringLiteral("SPII"),
	};
	if (refusals.contains(response->finishReason)) {
		response->failure = AiChatFailure::Refused;
		response->errorMessage = QCoreApplication::translate("VibeStudioAiTransport", "The provider stopped the answer (%1).").arg(response->finishReason);
	}
}

bool isAuthorizationHeader(const QByteArray& name)
{
	const QByteArray lowered = name.toLower();
	return lowered == "authorization" || lowered == "x-api-key" || lowered == "x-goog-api-key" || lowered == "api-key" || lowered == "xi-api-key";
}

// The schema keywords every structured-output provider accepts: the strict
// subset OpenAI and Claude enforce. Bounds are checked after the answer.
QJsonObject strictResponseSchema(const QJsonObject& schema)
{
	static const QStringList dropped = {
		QStringLiteral("$schema"),
		QStringLiteral("title"),
		QStringLiteral("default"),
		QStringLiteral("examples"),
		QStringLiteral("minimum"),
		QStringLiteral("maximum"),
		QStringLiteral("exclusiveMinimum"),
		QStringLiteral("exclusiveMaximum"),
		QStringLiteral("multipleOf"),
		QStringLiteral("minLength"),
		QStringLiteral("maxLength"),
		QStringLiteral("pattern"),
		QStringLiteral("minItems"),
		QStringLiteral("maxItems"),
		QStringLiteral("uniqueItems"),
	};
	QJsonObject out;
	for (auto it = schema.constBegin(); it != schema.constEnd(); ++it) {
		const QString key = it.key();
		if (dropped.contains(key)) {
			continue;
		}
		if (key == QStringLiteral("properties") && it.value().isObject()) {
			QJsonObject properties;
			const QJsonObject source = it.value().toObject();
			for (auto property = source.constBegin(); property != source.constEnd(); ++property) {
				properties.insert(property.key(), strictResponseSchema(property.value().toObject()));
			}
			out.insert(key, properties);
		} else if (key == QStringLiteral("items") && it.value().isObject()) {
			out.insert(key, strictResponseSchema(it.value().toObject()));
		} else if ((key == QStringLiteral("anyOf") || key == QStringLiteral("allOf")) && it.value().isArray()) {
			QJsonArray alternatives;
			for (const QJsonValue& alternative : it.value().toArray()) {
				alternatives.append(strictResponseSchema(alternative.toObject()));
			}
			out.insert(key, alternatives);
		} else {
			out.insert(key, it.value());
		}
	}
	return out;
}

// Gemini's responseSchema is an OpenAPI-style subset: upper-case type names,
// "nullable" instead of a null type, and no additionalProperties.
// https://ai.google.dev/api/generate-content#v1beta.GenerationConfig
QJsonObject geminiResponseSchema(const QJsonObject& schema)
{
	QJsonObject out;
	const QJsonValue type = schema.value(QStringLiteral("type"));
	QString typeName;
	bool nullable = false;
	if (type.isString()) {
		typeName = type.toString();
	} else if (type.isArray()) {
		for (const QJsonValue& entry : type.toArray()) {
			if (entry.toString() == QStringLiteral("null")) {
				nullable = true;
			} else if (typeName.isEmpty()) {
				typeName = entry.toString();
			}
		}
	}
	if (!typeName.isEmpty()) {
		out.insert(QStringLiteral("type"), typeName.toUpper());
	}
	if (nullable || schema.value(QStringLiteral("nullable")).toBool()) {
		out.insert(QStringLiteral("nullable"), true);
	}
	static const QStringList kept = {
		QStringLiteral("format"),
		QStringLiteral("description"),
		QStringLiteral("enum"),
		QStringLiteral("required"),
		QStringLiteral("minItems"),
		QStringLiteral("maxItems"),
		QStringLiteral("minimum"),
		QStringLiteral("maximum"),
	};
	for (const QString& key : kept) {
		if (schema.contains(key)) {
			out.insert(key, schema.value(key));
		}
	}
	if (schema.contains(QStringLiteral("const")) && !schema.contains(QStringLiteral("enum"))) {
		out.insert(QStringLiteral("enum"), QJsonArray {schema.value(QStringLiteral("const"))});
	}
	if (schema.value(QStringLiteral("properties")).isObject()) {
		QJsonObject properties;
		const QJsonObject source = schema.value(QStringLiteral("properties")).toObject();
		for (auto property = source.constBegin(); property != source.constEnd(); ++property) {
			properties.insert(property.key(), geminiResponseSchema(property.value().toObject()));
		}
		out.insert(QStringLiteral("properties"), properties);
	}
	if (schema.value(QStringLiteral("items")).isObject()) {
		out.insert(QStringLiteral("items"), geminiResponseSchema(schema.value(QStringLiteral("items")).toObject()));
	}
	if (schema.value(QStringLiteral("anyOf")).isArray()) {
		QJsonArray alternatives;
		for (const QJsonValue& alternative : schema.value(QStringLiteral("anyOf")).toArray()) {
			alternatives.append(geminiResponseSchema(alternative.toObject()));
		}
		out.insert(QStringLiteral("anyOf"), alternatives);
	}
	return out;
}

// OpenAI names a schema with letters, digits, underscores and dashes.
QString responseSchemaName(const QString& name)
{
	QString cleaned;
	for (const QChar character : name.trimmed()) {
		cleaned += character.isLetterOrNumber() && character.unicode() < 128 ? character : QLatin1Char('_');
	}
	cleaned = cleaned.left(64);
	return cleaned.isEmpty() ? QStringLiteral("answer") : cleaned;
}

// A long run of base64 is image or audio data: a reviewer needs its size,
// not its characters.
bool looksLikeBase64Payload(const QString& text)
{
	if (text.size() < 2048) {
		return false;
	}
	static const QRegularExpression base64(QStringLiteral("^[A-Za-z0-9+/=\\r\\n]+$"));
	return base64.match(text.left(4096)).hasMatch();
}

QJsonValue elidedPayloads(const QJsonValue& value)
{
	if (value.isString() && looksLikeBase64Payload(value.toString())) {
		// The count is worked out first: template brackets inside a translate()
		// call send lupdate's parser into a minutes-long backtrack.
		const qsizetype bytes = value.toString().size() * 3 / 4;
		const int count = static_cast<int>(std::min<qsizetype>(bytes, std::numeric_limits<int>::max()));
		return QCoreApplication::translate("VibeStudioAiTransport", "<%n bytes of base64 data>", nullptr, count);
	}
	if (value.isArray()) {
		QJsonArray array;
		for (const QJsonValue& entry : value.toArray()) {
			array.append(elidedPayloads(entry));
		}
		return array;
	}
	if (value.isObject()) {
		QJsonObject object;
		const QJsonObject source = value.toObject();
		for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
			object.insert(it.key(), elidedPayloads(it.value()));
		}
		return object;
	}
	return value;
}

// A setting's value on one line: nested objects and arrays as compact JSON.
QString settingText(const QJsonValue& value)
{
	if (value.isObject() || value.isArray()) {
		const QJsonValue elided = elidedPayloads(value);
		const QByteArray json = elided.isObject() ? QJsonDocument(elided.toObject()).toJson(QJsonDocument::Compact)
												 : QJsonDocument(elided.toArray()).toJson(QJsonDocument::Compact);
		QString text = QString::fromUtf8(json);
		if (text.size() > 240) {
			text = text.left(237) + QStringLiteral("...");
		}
		return text;
	}
	return value.toVariant().toString();
}

// The parts of a multipart/form-data body, one line each: a file part by its
// name, type and size, a text part by its value.
QStringList multipartSummary(const QByteArray& contentType, const QByteArray& body)
{
	QStringList lines;
	const qsizetype at = contentType.indexOf("boundary=");
	if (at < 0) {
		return lines;
	}
	QByteArray boundary = contentType.mid(at + 9).trimmed();
	if (boundary.startsWith('"') && boundary.endsWith('"') && boundary.size() >= 2) {
		boundary = boundary.mid(1, boundary.size() - 2);
	}
	const QByteArray delimiter = QByteArrayLiteral("--") + boundary;
	qsizetype position = body.indexOf(delimiter);
	while (position >= 0) {
		qsizetype partStart = position + delimiter.size();
		if (body.mid(partStart, 2) == "--") {
			break;
		}
		partStart = body.indexOf("\r\n", partStart);
		if (partStart < 0) {
			break;
		}
		partStart += 2;
		const qsizetype next = body.indexOf(delimiter, partStart);
		if (next < 0) {
			break;
		}
		const QByteArray part = body.mid(partStart, next - partStart);
		const qsizetype headerEnd = part.indexOf("\r\n\r\n");
		const QByteArray headers = headerEnd >= 0 ? part.left(headerEnd) : QByteArray();
		QByteArray content = headerEnd >= 0 ? part.mid(headerEnd + 4) : part;
		if (content.endsWith("\r\n")) {
			content.chop(2);
		}
		static const QRegularExpression nameField(QStringLiteral("name=\"([^\"]*)\""));
		static const QRegularExpression fileField(QStringLiteral("filename=\"([^\"]*)\""));
		static const QRegularExpression typeField(QStringLiteral("Content-Type:\\s*([^\\r\\n]+)"), QRegularExpression::CaseInsensitiveOption);
		const QString headerText = QString::fromUtf8(headers);
		const QString name = nameField.match(headerText).captured(1);
		const QRegularExpressionMatch file = fileField.match(headerText);
		if (file.hasMatch()) {
			// Counted outside translate(), whose arguments lupdate parses slowly.
			const int size = int(content.size());
			lines << QStringLiteral("%1: %2 (%3, %4)")
						 .arg(name, file.captured(1), typeField.match(headerText).captured(1).trimmed(),
							 QCoreApplication::translate("VibeStudioAiTransport", "%n byte(s)", nullptr, size));
		} else {
			lines << QStringLiteral("%1: %2").arg(name, QString::fromUtf8(content));
		}
		position = next;
	}
	return lines;
}

} // namespace

bool aiConnectorHasChatTransport(const QString& connectorId)
{
	return wireFormatFor(connectorId) != WireFormat::None;
}

QString aiDefaultEndpoint(const QString& connectorId)
{
	const QString id = normalizedAiId(connectorId);
	if (id == QStringLiteral("openai")) {
		return QStringLiteral("https://api.openai.com/v1");
	}
	if (id == QStringLiteral("claude")) {
		return QStringLiteral("https://api.anthropic.com");
	}
	if (id == QStringLiteral("gemini")) {
		return QStringLiteral("https://generativelanguage.googleapis.com");
	}
	if (id == QStringLiteral("local-offline")) {
		// Ollama's OpenAI-compatible endpoint; LM Studio and llama.cpp's server
		// listen elsewhere and are set in Settings.
		return QStringLiteral("http://localhost:11434/v1");
	}
	return {};
}

QString aiSuggestedModel(const QString& connectorId)
{
	if (normalizedAiId(connectorId) == QStringLiteral("claude")) {
		return QStringLiteral("claude-opus-5-5");
	}
	return {};
}

QString aiEffectiveEndpoint(const QString& connectorId, const QString& endpoint)
{
	const QString configured = endpoint.trimmed();
	return configured.isEmpty() ? aiDefaultEndpoint(connectorId) : configured;
}

bool aiEndpointIsLocal(const QString& endpoint)
{
	const QUrl url = QUrl::fromUserInput(endpoint.trimmed());
	const QString host = url.host().toLower();
	if (host.isEmpty()) {
		return false;
	}
	if (host == QStringLiteral("localhost") || host.endsWith(QStringLiteral(".localhost"))) {
		return true;
	}
	const QHostAddress address(host);
	return !address.isNull() && address.isLoopback();
}

bool buildAiHttpRequest(const AiChatRequest& request, const QString& apiKey, AiHttpRequest* out, QString* error)
{
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!out) {
		return fail(QCoreApplication::translate("VibeStudioAiTransport", "No request to fill."));
	}
	const WireFormat format = wireFormatFor(request.connectorId);
	if (format == WireFormat::None) {
		return fail(QCoreApplication::translate("VibeStudioAiTransport", "The %1 connector does not answer text requests.").arg(request.connectorId));
	}
	const QString model = request.model.trimmed();
	if (model.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioAiTransport", "No model is set for this connector."));
	}
	const QString base = trimmedBase(aiEffectiveEndpoint(request.connectorId, request.endpoint));
	if (base.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioAiTransport", "No endpoint is set for this connector."));
	}
	const QUrl baseUrl(base);
	if (!baseUrl.isValid() || (baseUrl.scheme() != QStringLiteral("http") && baseUrl.scheme() != QStringLiteral("https"))) {
		return fail(QCoreApplication::translate("VibeStudioAiTransport", "The endpoint must be an http:// or https:// address."));
	}
	if (request.messages.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioAiTransport", "There is nothing to ask."));
	}
	const int maxTokens = std::clamp(request.maxOutputTokens, 16, 128000);

	AiHttpRequest http;
	http.headers.append({QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")});
	QJsonObject body;
	if (format == WireFormat::OpenAiChat) {
		http.url = QUrl(base.endsWith(QStringLiteral("/chat/completions")) ? base : base + QStringLiteral("/chat/completions"));
		if (!apiKey.isEmpty()) {
			http.headers.append({QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + apiKey.toUtf8()});
		}
		QJsonArray messages;
		if (!request.system.isEmpty()) {
			messages.append(QJsonObject {{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), request.system}});
		}
		for (const AiChatMessage& message : request.messages) {
			messages.append(QJsonObject {{QStringLiteral("role"), message.role}, {QStringLiteral("content"), message.text}});
		}
		body.insert(QStringLiteral("model"), model);
		body.insert(QStringLiteral("messages"), messages);
		// OpenAI's own endpoint takes max_completion_tokens (its reasoning
		// models refuse max_tokens); compatible runtimes know max_tokens.
		const bool openAiItself = normalizedAiId(request.connectorId) == QStringLiteral("openai") && baseUrl.host() == QStringLiteral("api.openai.com");
		body.insert(openAiItself ? QStringLiteral("max_completion_tokens") : QStringLiteral("max_tokens"), maxTokens);
		body.insert(QStringLiteral("stream"), false);
		if (!request.responseSchema.isEmpty()) {
			// https://platform.openai.com/docs/guides/structured-outputs; Ollama,
			// LM Studio, llama.cpp's server and vLLM take the same field.
			body.insert(QStringLiteral("response_format"), QJsonObject {
				{QStringLiteral("type"), QStringLiteral("json_schema")},
				{QStringLiteral("json_schema"), QJsonObject {
					{QStringLiteral("name"), responseSchemaName(request.responseSchemaName)},
					{QStringLiteral("strict"), true},
					{QStringLiteral("schema"), aiProviderResponseSchema(request.connectorId, request.responseSchema)},
				}},
			});
		}
	} else if (format == WireFormat::AnthropicMessages) {
		http.url = QUrl(base.endsWith(QStringLiteral("/v1/messages")) ? base : base + QStringLiteral("/v1/messages"));
		if (!apiKey.isEmpty()) {
			http.headers.append({QByteArrayLiteral("x-api-key"), apiKey.toUtf8()});
		}
		http.headers.append({QByteArrayLiteral("anthropic-version"), QByteArrayLiteral("2023-06-01")});
		QJsonArray messages;
		for (const AiChatMessage& message : request.messages) {
			messages.append(QJsonObject {{QStringLiteral("role"), message.role}, {QStringLiteral("content"), message.text}});
		}
		body.insert(QStringLiteral("model"), model);
		body.insert(QStringLiteral("max_tokens"), maxTokens);
		if (!request.system.isEmpty()) {
			body.insert(QStringLiteral("system"), request.system);
		}
		body.insert(QStringLiteral("messages"), messages);
		if (anthropicModelTakesFallback(model) && baseUrl.host() == QStringLiteral("api.anthropic.com")) {
			http.headers.append({QByteArrayLiteral("anthropic-beta"), QByteArrayLiteral("server-side-fallback-2026-07-01")});
			body.insert(QStringLiteral("fallbacks"), QStringLiteral("default"));
		}
		if (!request.responseSchema.isEmpty()) {
			// Structured outputs: https://platform.claude.com/docs/en/build-with-claude/structured-outputs
			body.insert(QStringLiteral("output_config"), QJsonObject {
				{QStringLiteral("format"), QJsonObject {
					{QStringLiteral("type"), QStringLiteral("json_schema")},
					{QStringLiteral("schema"), aiProviderResponseSchema(request.connectorId, request.responseSchema)},
				}},
			});
		}
	} else {
		// Gemini's own listings name models "models/<id>"; either form works here.
		const QString geminiModel = model.startsWith(QStringLiteral("models/")) ? model.mid(7) : model;
		http.url = QUrl(QStringLiteral("%1/v1beta/models/%2:generateContent").arg(base, QString::fromUtf8(QUrl::toPercentEncoding(geminiModel))));
		if (!apiKey.isEmpty()) {
			http.headers.append({QByteArrayLiteral("x-goog-api-key"), apiKey.toUtf8()});
		}
		QJsonArray contents;
		for (const AiChatMessage& message : request.messages) {
			const QString role = message.role == QStringLiteral("assistant") ? QStringLiteral("model") : QStringLiteral("user");
			contents.append(QJsonObject {
				{QStringLiteral("role"), role},
				{QStringLiteral("parts"), QJsonArray {QJsonObject {{QStringLiteral("text"), message.text}}}},
			});
		}
		if (!request.system.isEmpty()) {
			body.insert(QStringLiteral("systemInstruction"), QJsonObject {{QStringLiteral("parts"), QJsonArray {QJsonObject {{QStringLiteral("text"), request.system}}}}});
		}
		body.insert(QStringLiteral("contents"), contents);
		QJsonObject generationConfig {{QStringLiteral("maxOutputTokens"), maxTokens}};
		if (!request.responseSchema.isEmpty()) {
			generationConfig.insert(QStringLiteral("responseMimeType"), QStringLiteral("application/json"));
			generationConfig.insert(QStringLiteral("responseSchema"), aiProviderResponseSchema(request.connectorId, request.responseSchema));
		}
		body.insert(QStringLiteral("generationConfig"), generationConfig);
	}
	if (!http.url.isValid()) {
		return fail(QCoreApplication::translate("VibeStudioAiTransport", "The endpoint does not make a valid address."));
	}
	http.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
	*out = http;
	return true;
}

AiChatResponse parseAiChatResponse(const QString& connectorId, int httpStatus, const QByteArray& body)
{
	AiChatResponse response;
	response.httpStatus = httpStatus;
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
	const QJsonObject root = document.isObject() ? document.object() : QJsonObject();
	if (httpStatus < 200 || httpStatus >= 300) {
		response.failure = failureForStatus(httpStatus);
		QString message = root.isEmpty() ? QString::fromUtf8(body.left(400)).trimmed() : jsonErrorMessage(root);
		if (message.isEmpty()) {
			message = QCoreApplication::translate("VibeStudioAiTransport", "The provider answered HTTP %1.").arg(httpStatus);
		}
		response.errorMessage = redactAiText(message);
		return response;
	}
	if (root.isEmpty()) {
		response.failure = AiChatFailure::BadResponse;
		response.errorMessage = QCoreApplication::translate("VibeStudioAiTransport", "The provider's answer was not JSON: %1").arg(parseError.errorString());
		return response;
	}
	switch (wireFormatFor(connectorId)) {
	case WireFormat::OpenAiChat:
		parseOpenAiChat(root, &response);
		break;
	case WireFormat::AnthropicMessages:
		parseAnthropicMessages(root, &response);
		break;
	case WireFormat::GeminiGenerate:
		parseGeminiGenerate(root, &response);
		break;
	case WireFormat::None:
		response.failure = AiChatFailure::NotConfigured;
		response.errorMessage = QCoreApplication::translate("VibeStudioAiTransport", "The %1 connector does not answer text requests.").arg(connectorId);
		return response;
	}
	if (response.failure == AiChatFailure::None && response.text.trimmed().isEmpty()) {
		response.failure = AiChatFailure::BadResponse;
		response.errorMessage = response.truncated ? QCoreApplication::translate("VibeStudioAiTransport", "The answer hit the output limit before any text came.")
												   : QCoreApplication::translate("VibeStudioAiTransport", "The provider's answer had no text in it.");
	}
	response.ok = response.failure == AiChatFailure::None;
	return response;
}

QJsonObject aiProviderResponseSchema(const QString& connectorId, const QJsonObject& schema)
{
	return wireFormatFor(connectorId) == WireFormat::GeminiGenerate ? geminiResponseSchema(schema) : strictResponseSchema(schema);
}

bool extractAiJsonObject(const QString& text, QJsonObject* out, QString* error)
{
	QString firstProblem;
	const auto tryParse = [&firstProblem, out](const QString& candidate) {
		QJsonParseError parseError;
		const QJsonDocument document = QJsonDocument::fromJson(candidate.trimmed().toUtf8(), &parseError);
		if (document.isObject()) {
			if (out) {
				*out = document.object();
			}
			return true;
		}
		if (firstProblem.isEmpty()) {
			firstProblem = document.isNull() ? parseError.errorString() : QCoreApplication::translate("VibeStudioAiTransport", "the JSON is not an object");
		}
		return false;
	};
	if (tryParse(text)) {
		return true;
	}
	// A fenced block, as models often answer even when told not to.
	static const QRegularExpression fence(QStringLiteral("```[^\\n`]*\\n(.*?)```"), QRegularExpression::DotMatchesEverythingOption);
	for (auto matches = fence.globalMatch(text); matches.hasNext();) {
		if (tryParse(matches.next().captured(1))) {
			return true;
		}
	}
	// The first balanced object, skipping braces inside strings. A bounded
	// number of starts keeps a long answer of prose cheap.
	int starts = 0;
	for (qsizetype start = text.indexOf(QLatin1Char('{')); start >= 0 && starts < 64; start = text.indexOf(QLatin1Char('{'), start + 1), ++starts) {
		int depth = 0;
		bool inString = false;
		bool escaped = false;
		for (qsizetype index = start; index < text.size(); ++index) {
			const QChar character = text.at(index);
			if (inString) {
				if (escaped) {
					escaped = false;
				} else if (character == QLatin1Char('\\')) {
					escaped = true;
				} else if (character == QLatin1Char('"')) {
					inString = false;
				}
				continue;
			}
			if (character == QLatin1Char('"')) {
				inString = true;
			} else if (character == QLatin1Char('{')) {
				++depth;
			} else if (character == QLatin1Char('}') && --depth == 0) {
				if (tryParse(text.mid(start, index - start + 1))) {
					return true;
				}
				break;
			}
		}
	}
	if (error) {
		*error = firstProblem.isEmpty() ? QCoreApplication::translate("VibeStudioAiTransport", "The answer held no JSON object.")
										: QCoreApplication::translate("VibeStudioAiTransport", "The answer held no JSON object (%1).").arg(firstProblem);
	}
	return false;
}

QStringList aiJsonSchemaProblems(const QJsonValue& value, const QJsonObject& schema, const QString& path)
{
	QStringList problems;
	if (schema.value(QStringLiteral("anyOf")).isArray()) {
		for (const QJsonValue& alternative : schema.value(QStringLiteral("anyOf")).toArray()) {
			if (aiJsonSchemaProblems(value, alternative.toObject(), path).isEmpty()) {
				return {};
			}
		}
		problems << QCoreApplication::translate("VibeStudioAiTransport", "%1 matches none of the allowed shapes.").arg(path);
		return problems;
	}
	QStringList types;
	const QJsonValue type = schema.value(QStringLiteral("type"));
	if (type.isString()) {
		types << type.toString();
	} else {
		for (const QJsonValue& entry : type.toArray()) {
			types << entry.toString();
		}
	}
	if (!types.isEmpty()) {
		bool typed = false;
		for (const QString& name : types) {
			typed = typed || (name == QStringLiteral("object") && value.isObject()) || (name == QStringLiteral("array") && value.isArray())
				|| (name == QStringLiteral("string") && value.isString()) || (name == QStringLiteral("boolean") && value.isBool())
				|| (name == QStringLiteral("null") && value.isNull()) || (name == QStringLiteral("number") && value.isDouble())
				|| (name == QStringLiteral("integer") && value.isDouble() && std::floor(value.toDouble()) == value.toDouble());
		}
		if (!typed) {
			problems << QCoreApplication::translate("VibeStudioAiTransport", "%1 should be %2.").arg(path, types.join(QStringLiteral(" or ")));
			return problems;
		}
	}
	// Values as JSON writes them, so a string reads as one.
	const auto literal = [](const QJsonValue& entry) {
		return entry.isString() ? QStringLiteral("\"%1\"").arg(entry.toString()) : settingText(entry);
	};
	if (schema.contains(QStringLiteral("const")) && value != schema.value(QStringLiteral("const"))) {
		problems << QCoreApplication::translate("VibeStudioAiTransport", "%1 should be %2.").arg(path, literal(schema.value(QStringLiteral("const"))));
	}
	if (schema.value(QStringLiteral("enum")).isArray()) {
		const QJsonArray allowed = schema.value(QStringLiteral("enum")).toArray();
		if (!allowed.contains(value)) {
			QStringList names;
			for (const QJsonValue& entry : allowed) {
				names << literal(entry);
			}
			problems << QCoreApplication::translate("VibeStudioAiTransport", "%1 is %2, not one of: %3.").arg(path, literal(value), names.join(QStringLiteral(", ")));
		}
	}
	if (value.isString()) {
		const qsizetype length = value.toString().size();
		if (schema.contains(QStringLiteral("minLength")) && length < schema.value(QStringLiteral("minLength")).toInt()) {
			problems << QCoreApplication::translate("VibeStudioAiTransport", "%1 is shorter than %2 characters.").arg(path).arg(schema.value(QStringLiteral("minLength")).toInt());
		}
		if (schema.contains(QStringLiteral("maxLength")) && length > schema.value(QStringLiteral("maxLength")).toInt()) {
			problems << QCoreApplication::translate("VibeStudioAiTransport", "%1 is longer than %2 characters.").arg(path).arg(schema.value(QStringLiteral("maxLength")).toInt());
		}
	}
	if (value.isDouble()) {
		if (schema.contains(QStringLiteral("minimum")) && value.toDouble() < schema.value(QStringLiteral("minimum")).toDouble()) {
			problems << QCoreApplication::translate("VibeStudioAiTransport", "%1 is below %2.").arg(path, settingText(schema.value(QStringLiteral("minimum"))));
		}
		if (schema.contains(QStringLiteral("maximum")) && value.toDouble() > schema.value(QStringLiteral("maximum")).toDouble()) {
			problems << QCoreApplication::translate("VibeStudioAiTransport", "%1 is above %2.").arg(path, settingText(schema.value(QStringLiteral("maximum"))));
		}
	}
	if (value.isArray()) {
		const QJsonArray array = value.toArray();
		if (schema.contains(QStringLiteral("minItems")) && array.size() < schema.value(QStringLiteral("minItems")).toInt()) {
			problems << QCoreApplication::translate("VibeStudioAiTransport", "%1 has fewer than %2 entries.").arg(path).arg(schema.value(QStringLiteral("minItems")).toInt());
		}
		if (schema.contains(QStringLiteral("maxItems")) && array.size() > schema.value(QStringLiteral("maxItems")).toInt()) {
			problems << QCoreApplication::translate("VibeStudioAiTransport", "%1 has more than %2 entries.").arg(path).arg(schema.value(QStringLiteral("maxItems")).toInt());
		}
		if (schema.value(QStringLiteral("items")).isObject()) {
			const QJsonObject items = schema.value(QStringLiteral("items")).toObject();
			for (qsizetype index = 0; index < array.size(); ++index) {
				problems << aiJsonSchemaProblems(array.at(index), items, QStringLiteral("%1[%2]").arg(path).arg(index));
			}
		}
	}
	if (value.isObject()) {
		const QJsonObject object = value.toObject();
		const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
		for (const QJsonValue& required : schema.value(QStringLiteral("required")).toArray()) {
			if (!object.contains(required.toString())) {
				problems << QCoreApplication::translate("VibeStudioAiTransport", "%1.%2 is missing.").arg(path, required.toString());
			}
		}
		const bool closed = schema.value(QStringLiteral("additionalProperties")) == QJsonValue(false);
		for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
			const QString childPath = QStringLiteral("%1.%2").arg(path, it.key());
			if (properties.contains(it.key())) {
				problems << aiJsonSchemaProblems(it.value(), properties.value(it.key()).toObject(), childPath);
			} else if (closed) {
				problems << QCoreApplication::translate("VibeStudioAiTransport", "%1 is not expected.").arg(childPath);
			}
		}
	}
	return problems;
}

QString aiChatFailureId(AiChatFailure failure)
{
	switch (failure) {
	case AiChatFailure::None:
		return QStringLiteral("none");
	case AiChatFailure::NotConfigured:
		return QStringLiteral("not-configured");
	case AiChatFailure::Credential:
		return QStringLiteral("credential");
	case AiChatFailure::RateLimited:
		return QStringLiteral("rate-limited");
	case AiChatFailure::Refused:
		return QStringLiteral("refused");
	case AiChatFailure::Network:
		return QStringLiteral("network");
	case AiChatFailure::Timeout:
		return QStringLiteral("timeout");
	case AiChatFailure::Cancelled:
		return QStringLiteral("cancelled");
	case AiChatFailure::Provider:
		return QStringLiteral("provider");
	case AiChatFailure::BadResponse:
		return QStringLiteral("bad-response");
	}
	return QStringLiteral("provider");
}

QString aiChatFailureText(AiChatFailure failure)
{
	switch (failure) {
	case AiChatFailure::None:
		return QCoreApplication::translate("VibeStudioAiTransport", "Answered.");
	case AiChatFailure::NotConfigured:
		return QCoreApplication::translate("VibeStudioAiTransport", "The connector is not set up.");
	case AiChatFailure::Credential:
		return QCoreApplication::translate("VibeStudioAiTransport", "The provider did not accept the credential.");
	case AiChatFailure::RateLimited:
		return QCoreApplication::translate("VibeStudioAiTransport", "The provider is busy or rate-limited; try again shortly.");
	case AiChatFailure::Refused:
		return QCoreApplication::translate("VibeStudioAiTransport", "The model declined to answer.");
	case AiChatFailure::Network:
		return QCoreApplication::translate("VibeStudioAiTransport", "The endpoint could not be reached.");
	case AiChatFailure::Timeout:
		return QCoreApplication::translate("VibeStudioAiTransport", "The provider took too long to answer.");
	case AiChatFailure::Cancelled:
		return QCoreApplication::translate("VibeStudioAiTransport", "Cancelled.");
	case AiChatFailure::Provider:
		return QCoreApplication::translate("VibeStudioAiTransport", "The provider reported an error.");
	case AiChatFailure::BadResponse:
		return QCoreApplication::translate("VibeStudioAiTransport", "The provider's answer could not be read.");
	}
	return {};
}

QString redactAiText(const QString& text, const QStringList& secrets)
{
	QString redacted = text;
	for (const QString& secret : secrets) {
		// Short values would blank out ordinary words.
		if (secret.size() >= 8) {
			redacted.replace(secret, QStringLiteral("***"));
		}
	}
	// Shapes of provider keys that may sit in pasted logs or config files.
	static const QRegularExpression keyShapes(QStringLiteral(
		"\\b(sk-(?:ant-|proj-)?[A-Za-z0-9_\\-]{12,}|AIza[0-9A-Za-z_\\-]{20,}|xai-[A-Za-z0-9]{20,}|gsk_[A-Za-z0-9]{20,})"));
	redacted.replace(keyShapes, QStringLiteral("***"));
	return redacted;
}

QString redactAiContextPaths(const QString& text, const QString& projectRoot, const QString& homeDirectory)
{
	QString redacted = text;
	const auto replaceFolder = [&redacted](QString folder, const QString& token) {
		folder = folder.trimmed();
		while (folder.endsWith(QLatin1Char('/')) || folder.endsWith(QLatin1Char('\\'))) {
			folder.chop(1);
		}
		if (folder.size() < 3) {
			return;
		}
		QString forward = folder;
		forward.replace(QLatin1Char('\\'), QLatin1Char('/'));
		QString backward = folder;
		backward.replace(QLatin1Char('/'), QLatin1Char('\\'));
		// Windows paths compare without case; the rest are left as they are.
		const Qt::CaseSensitivity sensitivity = folder.contains(QLatin1Char(':')) ? Qt::CaseInsensitive : Qt::CaseSensitive;
		redacted.replace(forward, token, sensitivity);
		redacted.replace(backward, token, sensitivity);
	};
	// The project first: it usually sits inside the home folder.
	replaceFolder(projectRoot, QStringLiteral("<project>"));
	replaceFolder(homeDirectory, QStringLiteral("~"));
	return redacted;
}

QString describeAiHttpRequest(const AiHttpRequest& request, AiRequestView view)
{
	QStringList lines;
	lines << QStringLiteral("POST %1").arg(request.url.toString(QUrl::RemoveUserInfo));
	QByteArray contentType;
	for (const auto& header : request.headers) {
		lines << QStringLiteral("%1: %2").arg(QString::fromUtf8(header.first),
			isAuthorizationHeader(header.first) ? QStringLiteral("***") : QString::fromUtf8(header.second));
		if (header.first.toLower() == "content-type") {
			contentType = header.second;
		}
	}
	lines << QString();
	// A form with files in it: the parts by name, the files by size.
	if (contentType.toLower().startsWith("multipart/form-data")) {
		lines << multipartSummary(contentType, request.body);
		return lines.join(QLatin1Char('\n'));
	}
	const QJsonDocument parsed = QJsonDocument::fromJson(request.body);
	// Image and audio payloads are shown by size in either view.
	const QJsonDocument document = parsed.isObject() ? QJsonDocument(elidedPayloads(parsed.object()).toObject()) : parsed;
	if (view == AiRequestView::Raw || !document.isObject()) {
		lines << (document.isNull() ? QString::fromUtf8(request.body) : QString::fromUtf8(document.toJson(QJsonDocument::Indented)).trimmed());
		return lines.join(QLatin1Char('\n'));
	}
	// The text of a message or a system instruction, in any provider's shape:
	// a string, content blocks with "text", or Gemini's parts.
	const auto textOf = [](const QJsonValue& value) {
		if (value.isString()) {
			return value.toString();
		}
		QStringList parts;
		const QJsonArray blocks = value.isArray() ? value.toArray() : value.toObject().value(QStringLiteral("parts")).toArray();
		for (const QJsonValue& block : blocks) {
			const QJsonObject part = block.toObject();
			// An attached image is named by its type and size; its data was
			// elided above.
			const QJsonObject inlineData = part.value(QStringLiteral("inlineData")).toObject();
			if (!inlineData.isEmpty()) {
				parts << QStringLiteral("[%1 %2]\n").arg(inlineData.value(QStringLiteral("mimeType")).toString(), inlineData.value(QStringLiteral("data")).toString());
			} else {
				parts << part.value(QStringLiteral("text")).toString();
			}
		}
		return parts.join(QString());
	};
	const QJsonObject body = document.object();
	QStringList settings;
	QStringList system;
	QStringList messages;
	for (auto it = body.constBegin(); it != body.constEnd(); ++it) {
		const QString key = it.key();
		const QJsonValue value = it.value();
		if (key == QStringLiteral("messages") || key == QStringLiteral("contents")) {
			for (const QJsonValue& entry : value.toArray()) {
				const QJsonObject message = entry.toObject();
				const QJsonValue content = message.contains(QStringLiteral("content")) ? message.value(QStringLiteral("content")) : message.value(QStringLiteral("parts"));
				messages << QStringLiteral("=== %1 ===\n%2").arg(message.value(QStringLiteral("role")).toString(), textOf(content));
			}
		} else if (key == QStringLiteral("system") || key == QStringLiteral("systemInstruction")) {
			system << QStringLiteral("=== system ===\n%1").arg(textOf(value));
		} else if (value.isObject()) {
			const QJsonObject group = value.toObject();
			for (auto inner = group.constBegin(); inner != group.constEnd(); ++inner) {
				settings << QStringLiteral("%1.%2: %3").arg(key, inner.key(), settingText(inner.value()));
			}
		} else {
			settings << QStringLiteral("%1: %2").arg(key, settingText(value));
		}
	}
	lines << settings;
	lines << QString();
	lines << (system + messages).join(QStringLiteral("\n\n"));
	return lines.join(QLatin1Char('\n'));
}

AiTextConnection resolveAiTextConnection(const AiAutomationPreferences& preferences, const QString& connectorOverride)
{
	const AiAutomationPreferences normalized = normalizedAiAutomationPreferences(preferences);
	AiTextConnection connection;
	connection.connectorId = normalizedAiId(connectorOverride);
	if (connection.connectorId.isEmpty()) {
		// The reasoning connector, unless it would leave the machine while
		// cloud connectors are off and a local one is set: then the local one.
		const QString reasoning = normalized.preferredReasoningConnectorId;
		const QString local = normalized.preferredLocalConnectorId;
		const bool reasoningAllowed = !reasoning.isEmpty()
			&& (normalized.cloudConnectorsEnabled || aiEndpointIsLocal(aiEffectiveEndpoint(reasoning, normalized.connectorEndpoints.value(reasoning))));
		connection.connectorId = reasoningAllowed || local.isEmpty() ? reasoning : local;
	}
	AiConnectorDescriptor descriptor;
	const bool known = !connection.connectorId.isEmpty() && aiConnectorForId(connection.connectorId, &descriptor);
	connection.displayName = known ? descriptor.displayName : connection.connectorId;
	if (normalized.aiFreeMode) {
		connection.block = AiTextConnectionBlock::AiFreeMode;
		return connection;
	}
	if (normalized.projectAiFree) {
		connection.block = AiTextConnectionBlock::ProjectAiFree;
		return connection;
	}
	if (!known) {
		connection.block = AiTextConnectionBlock::NoConnector;
		return connection;
	}
	if (!aiConnectorHasChatTransport(connection.connectorId)) {
		connection.block = AiTextConnectionBlock::NoTextTransport;
		return connection;
	}
	connection.endpoint = aiEffectiveEndpoint(connection.connectorId, normalized.connectorEndpoints.value(connection.connectorId));
	connection.model = normalized.connectorModels.value(connection.connectorId, aiSuggestedModel(connection.connectorId));
	if (connection.endpoint.isEmpty()) {
		connection.block = AiTextConnectionBlock::NoEndpoint;
		return connection;
	}
	connection.local = aiEndpointIsLocal(connection.endpoint);
	if (!connection.local && !normalized.cloudConnectorsEnabled) {
		connection.block = AiTextConnectionBlock::CloudNotAllowed;
		return connection;
	}
	if (connection.model.isEmpty()) {
		connection.block = AiTextConnectionBlock::NoModel;
		return connection;
	}
	// A local runtime needs no key; a cloud provider does, and a custom
	// endpoint only when the user named a variable for one.
	connection.credentialVariable = aiCredentialEnvironmentVariableForConnector(connection.connectorId, normalized);
	if (connection.local && descriptor.cloudBased) {
		connection.credentialVariable.clear();
	}
	if (!descriptor.cloudBased) {
		connection.credentialVariable.clear();
	}
	if (!connection.credentialVariable.isEmpty()) {
		connection.credentialFound = !qEnvironmentVariable(connection.credentialVariable.toUtf8().constData()).trimmed().isEmpty();
		if (!connection.credentialFound) {
			connection.block = AiTextConnectionBlock::NoCredential;
		}
	}
	return connection;
}

QString aiTextConnectionBlockId(AiTextConnectionBlock block)
{
	switch (block) {
	case AiTextConnectionBlock::None:
		return QStringLiteral("ready");
	case AiTextConnectionBlock::AiFreeMode:
		return QStringLiteral("ai-free-mode");
	case AiTextConnectionBlock::ProjectAiFree:
		return QStringLiteral("project-ai-free");
	case AiTextConnectionBlock::NoConnector:
		return QStringLiteral("no-connector");
	case AiTextConnectionBlock::NoTextTransport:
		return QStringLiteral("no-text-transport");
	case AiTextConnectionBlock::NoEndpoint:
		return QStringLiteral("no-endpoint");
	case AiTextConnectionBlock::CloudNotAllowed:
		return QStringLiteral("cloud-not-allowed");
	case AiTextConnectionBlock::NoModel:
		return QStringLiteral("no-model");
	case AiTextConnectionBlock::NoCredential:
		return QStringLiteral("no-credential");
	}
	return QStringLiteral("no-connector");
}

QString aiProjectAiFreeText()
{
	return QCoreApplication::translate("VibeStudioAiTransport",
		"This project turns AI off in its manifest. To allow AI here, run: vibestudio --cli project init <project folder> --project-ai-free off");
}

QString aiTextConnectionBlockText(const AiTextConnection& connection)
{
	switch (connection.block) {
	case AiTextConnectionBlock::None:
		return connection.local
			? QCoreApplication::translate("VibeStudioAiTransport", "Ready: %1, model %2, on this machine.").arg(connection.displayName, connection.model)
			: QCoreApplication::translate("VibeStudioAiTransport", "Ready: %1, model %2, at %3.").arg(connection.displayName, connection.model, QUrl(connection.endpoint).host());
	case AiTextConnectionBlock::AiFreeMode:
		return QCoreApplication::translate("VibeStudioAiTransport", "AI-free mode is on. Turn it off in Settings > AI and Automation to ask a model.");
	case AiTextConnectionBlock::ProjectAiFree:
		return aiProjectAiFreeText();
	case AiTextConnectionBlock::NoConnector:
		return QCoreApplication::translate("VibeStudioAiTransport", "No connector is chosen. Pick a Reasoning or Local connector in Settings > AI and Automation.");
	case AiTextConnectionBlock::NoTextTransport:
		return QCoreApplication::translate("VibeStudioAiTransport", "%1 does not answer text questions. Pick OpenAI, Claude, Gemini, a local runtime, or a custom endpoint.").arg(connection.displayName);
	case AiTextConnectionBlock::NoEndpoint:
		return QCoreApplication::translate("VibeStudioAiTransport", "%1 has no endpoint. Set one in Settings > AI and Automation.").arg(connection.displayName);
	case AiTextConnectionBlock::CloudNotAllowed:
		return QCoreApplication::translate("VibeStudioAiTransport", "%1 sends to %2, off this machine, and cloud connectors are not allowed. Allow them in Settings > AI and Automation, or use a local runtime.")
			.arg(connection.displayName, QUrl(connection.endpoint).host());
	case AiTextConnectionBlock::NoModel:
		return QCoreApplication::translate("VibeStudioAiTransport", "No model is set for %1. Name one in Settings > AI and Automation.").arg(connection.displayName);
	case AiTextConnectionBlock::NoCredential:
		return QCoreApplication::translate("VibeStudioAiTransport", "%1 needs an API key in the %2 environment variable, which is not set.").arg(connection.displayName, connection.credentialVariable);
	}
	return {};
}

QString aiTextConnectionApiKey(const AiTextConnection& connection)
{
	return connection.credentialVariable.isEmpty() ? QString() : qEnvironmentVariable(connection.credentialVariable.toUtf8().constData()).trimmed();
}

AiContextItem makeAiContextItem(const QString& id, const QString& label, const QString& text, int maxLines, int maxBytes)
{
	AiContextItem item;
	item.id = id;
	item.label = label;
	QStringList lines = text.split(QLatin1Char('\n'));
	while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) {
		lines.removeLast();
	}
	const int totalLines = static_cast<int>(lines.size());
	QStringList kept;
	int bytes = 0;
	for (const QString& line : lines) {
		const int lineBytes = static_cast<int>(line.toUtf8().size()) + 1;
		if (kept.size() >= maxLines || bytes + lineBytes > maxBytes) {
			break;
		}
		kept << line;
		bytes += lineBytes;
	}
	item.text = kept.join(QLatin1Char('\n'));
	if (kept.size() < totalLines) {
		item.omittedNote = QCoreApplication::translate("VibeStudioAiTransport", "The first %1 of %2 lines; the rest was not sent.").arg(kept.size()).arg(totalLines);
	}
	return item;
}

QString aiContextPromptText(const QVector<AiContextItem>& items)
{
	QStringList sections;
	for (const AiContextItem& item : items) {
		if (item.text.trimmed().isEmpty()) {
			continue;
		}
		QString section = QStringLiteral("## %1\n").arg(item.label);
		if (!item.omittedNote.isEmpty()) {
			section += QStringLiteral("(%1)\n").arg(item.omittedNote);
		}
		// A fence longer than any run of backticks inside keeps the text whole.
		QString fence = QStringLiteral("```");
		while (item.text.contains(fence)) {
			fence += QLatin1Char('`');
		}
		section += fence + QLatin1Char('\n') + item.text + QLatin1Char('\n') + fence;
		sections << section;
	}
	return sections.join(QStringLiteral("\n\n"));
}

QString studioAssistantSystemPrompt()
{
	// Kept in English: it instructs the model, and the user sees it in the
	// request preview as it is sent.
	return QStringLiteral(
		"You are the assistant inside VibeStudio, an integrated development environment for idTech1, idTech2, and idTech3 games "
		"(Doom, Quake, Quake II, Quake III Arena, and their source ports). The user makes maps, models, textures, sounds, "
		"packages, QuakeC and other game code, shaders, and compiles them with VibeStudio's VibeMap2 (Quake, Quake II) and VibeMap3 (Quake III) compilers and the ZDBSP and ZokumBSP node builders.\n"
		"Answer the user's question, using the project context they chose to include. Be concise and practical: name the files, "
		"entities, keys, compiler options, and exact steps involved. When you suggest code, map, or shader changes, give them as "
		"snippets the user can review and apply; you cannot change their files yourself, so never claim to have done so. "
		"If the context does not hold what you need to answer, say what is missing.");
}

// ---------------------------------------------------------------------------
// AiChatClient

struct AiChatClient::State {
	QNetworkAccessManager* network = nullptr;
	std::unique_ptr<QNetworkAccessManager> ownedNetwork;
	QPointer<QNetworkReply> reply;
	QElapsedTimer clock;
	QString connectorId;
	QStringList secrets;
	Callback done;
	int timeoutMsecs = 5 * 60 * 1000;
	bool cancelled = false;
	bool timedOut = false;
};

AiChatClient::AiChatClient(QNetworkAccessManager* network)
	: m_state(std::make_unique<State>())
{
	if (network) {
		m_state->network = network;
	} else {
		m_state->ownedNetwork = std::make_unique<QNetworkAccessManager>();
		m_state->network = m_state->ownedNetwork.get();
	}
}

AiChatClient::~AiChatClient()
{
	if (QNetworkReply* reply = m_state->reply) {
		// The callback belongs to whoever is being destroyed with us.
		QObject::disconnect(reply, nullptr, nullptr, nullptr);
		reply->abort();
		reply->deleteLater();
	}
}

bool AiChatClient::send(const AiChatRequest& request, const QString& apiKey, Callback done, QString* error)
{
	if (busy()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioAiTransport", "A request is already running.");
		}
		return false;
	}
	AiHttpRequest http;
	if (!buildAiHttpRequest(request, apiKey, &http, error)) {
		return false;
	}
	QNetworkRequest networkRequest(http.url);
	for (const auto& header : http.headers) {
		networkRequest.setRawHeader(header.first, header.second);
	}
	// Never follow a redirect from https to http with the key attached.
	networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

	m_state->connectorId = request.connectorId;
	m_state->secrets = apiKey.isEmpty() ? QStringList() : QStringList {apiKey};
	m_state->done = std::move(done);
	m_state->cancelled = false;
	m_state->timedOut = false;
	m_state->clock.start();
	QNetworkReply* reply = m_state->network->post(networkRequest, http.body);
	m_state->reply = reply;

	auto* timer = new QTimer(reply);
	timer->setSingleShot(true);
	QObject::connect(timer, &QTimer::timeout, reply, [this, reply]() {
		m_state->timedOut = true;
		reply->abort();
	});
	timer->start(m_state->timeoutMsecs);

	QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply]() {
		AiChatResponse response;
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (m_state->cancelled) {
			response.failure = AiChatFailure::Cancelled;
			response.errorMessage = aiChatFailureText(AiChatFailure::Cancelled);
		} else if (m_state->timedOut) {
			response.failure = AiChatFailure::Timeout;
			response.errorMessage = QCoreApplication::translate("VibeStudioAiTransport", "No answer after %1 seconds.").arg(m_state->timeoutMsecs / 1000);
		} else if (status > 0) {
			// An HTTP error still carries the provider's explanation.
			response = parseAiChatResponse(m_state->connectorId, status, reply->readAll());
		} else {
			response.failure = AiChatFailure::Network;
			response.errorMessage = reply->errorString();
		}
		response.errorMessage = redactAiText(response.errorMessage, m_state->secrets);
		response.elapsedMsecs = m_state->clock.elapsed();
		Callback done = std::move(m_state->done);
		m_state->done = nullptr;
		m_state->reply = nullptr;
		m_state->secrets.clear();
		reply->deleteLater();
		// Last, so the callback may start the next request.
		if (done) {
			done(response);
		}
	});
	return true;
}

void AiChatClient::cancel()
{
	if (QNetworkReply* reply = m_state->reply) {
		m_state->cancelled = true;
		reply->abort();
	}
}

bool AiChatClient::busy() const
{
	return !m_state->reply.isNull();
}

void AiChatClient::setTimeoutMsecs(int msecs)
{
	m_state->timeoutMsecs = std::max(1000, msecs);
}

int AiChatClient::timeoutMsecs() const
{
	return m_state->timeoutMsecs;
}

} // namespace vibestudio
