#pragma once

// Provider-neutral text generation over HTTP: what the studio asks a model,
// how each provider family wants that asked, and how its answer is read back.
//
// Three wire formats cover the connectors that answer in text:
//  - OpenAI-compatible Chat Completions (`POST {base}/chat/completions`), used
//    by OpenAI itself and by local runtimes and proxies that speak the same
//    shape (Ollama, LM Studio, llama.cpp's server, vLLM):
//    https://platform.openai.com/docs/api-reference/chat
//  - Anthropic's Messages API (`POST {base}/v1/messages`):
//    https://platform.claude.com/docs/en/api/messages
//  - Google's Gemini generateContent
//    (`POST {base}/v1beta/models/{model}:generateContent`):
//    https://ai.google.dev/api/generate-content
//
// Building and parsing are plain functions, so every shape is testable without
// a network. `AiChatClient` sends one request at a time over Qt Network; like
// the rest of src/core it has no Q_OBJECT: the reply's signals connect to
// lambdas that use the reply as their context object.
//
// Nothing here decides whether a request may be sent. AI-free mode, cloud
// opt-in, and consent are the caller's to check before `send()`.

#include "core/ai_connectors.h"

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <functional>
#include <memory>

class QNetworkAccessManager;

namespace vibestudio {

// One turn of a conversation.
struct AiChatMessage {
	QString role; // "user" or "assistant"
	QString text;
};

// A text request in provider-neutral terms.
struct AiChatRequest {
	QString connectorId;
	// The provider's own model name, as the user configured it.
	QString model;
	// Base URL; empty uses the connector's default endpoint.
	QString endpoint;
	QString system;
	QVector<AiChatMessage> messages;
	int maxOutputTokens = 4096;
	// Asks for one JSON object matching this JSON Schema instead of prose.
	// List every property of every object under "required" and set
	// "additionalProperties" to false: OpenAI and Claude hold the model to a
	// schema only in that strict form. Bounds such as minimum or maxItems are
	// checked here after the answer (aiJsonSchemaProblems), since not every
	// provider accepts them. Empty asks for prose.
	QJsonObject responseSchema;
	// What the schema describes, as OpenAI asks for one: letters, digits, _
	// and -. Empty names it "answer".
	QString responseSchemaName;
};

// The HTTP request a provider adapter makes of an AiChatRequest.
struct AiHttpRequest {
	QUrl url;
	QList<QPair<QByteArray, QByteArray>> headers;
	QByteArray body;
};

enum class AiChatFailure {
	None,
	NotConfigured,
	Credential,
	RateLimited,
	Refused,
	Network,
	Timeout,
	Cancelled,
	Provider,
	BadResponse,
};

struct AiChatResponse {
	bool ok = false;
	QString text;
	// As the provider said it: "stop", "end_turn", "MAX_TOKENS"...
	QString finishReason;
	// The answer stopped at the output limit.
	bool truncated = false;
	int inputTokens = -1;
	int outputTokens = -1;
	// The model that answered, as the provider reported it.
	QString model;
	int httpStatus = 0;
	AiChatFailure failure = AiChatFailure::None;
	// The provider's own words about a failure, secrets removed.
	QString errorMessage;
	qint64 elapsedMsecs = 0;
};

// Connectors that answer text through one of the three wire formats.
[[nodiscard]] bool aiConnectorHasChatTransport(const QString& connectorId);
[[nodiscard]] QString aiDefaultEndpoint(const QString& connectorId);
// A model to start from, or empty where the user must name one: model lists
// change faster than releases, so only a connector with a stable default
// suggests one.
[[nodiscard]] QString aiSuggestedModel(const QString& connectorId);
// The endpoint a request would use: the configured one, else the default.
[[nodiscard]] QString aiEffectiveEndpoint(const QString& connectorId, const QString& endpoint);
// localhost, 127.0.0.0/8, ::1, or a *.localhost name: nothing leaves the machine.
[[nodiscard]] bool aiEndpointIsLocal(const QString& endpoint);

bool buildAiHttpRequest(const AiChatRequest& request, const QString& apiKey, AiHttpRequest* out, QString* error = nullptr);
[[nodiscard]] AiChatResponse parseAiChatResponse(const QString& connectorId, int httpStatus, const QByteArray& body);

// The schema as a provider's structured-output field takes it. OpenAI-shaped
// endpoints and Claude get the strict subset (bounds removed); Gemini gets
// its OpenAPI-style schema (upper-case types, no additionalProperties).
[[nodiscard]] QJsonObject aiProviderResponseSchema(const QString& connectorId, const QJsonObject& schema);
// The JSON object an answer holds: the whole answer, a fenced ```json block,
// or the first balanced {...} in it. False, with the reason, when none parses.
bool extractAiJsonObject(const QString& text, QJsonObject* out, QString* error = nullptr);
// Checks a value against the JSON Schema subset the studio's structured
// requests use: type, properties, required, additionalProperties false,
// items, enum, const, minItems, maxItems, minimum, maximum, minLength and
// maxLength. Each problem names its place, such as $.rooms[2].size.
[[nodiscard]] QStringList aiJsonSchemaProblems(const QJsonValue& value, const QJsonObject& schema, const QString& path = QStringLiteral("$"));

[[nodiscard]] QString aiChatFailureId(AiChatFailure failure);
[[nodiscard]] QString aiChatFailureText(AiChatFailure failure);

// Replaces each secret, and anything shaped like a provider key, with ***.
[[nodiscard]] QString redactAiText(const QString& text, const QStringList& secrets = {});
// Replaces the project folder with <project> and the home folder with ~, in
// either slash style, so shared context does not name the user's machine.
[[nodiscard]] QString redactAiContextPaths(const QString& text, const QString& projectRoot, const QString& homeDirectory);
enum class AiRequestView {
	// The settings one per line, then each message's text as it is written:
	// what a person reviews before it is sent.
	Readable,
	// The body as it goes, pretty-printed JSON.
	Raw,
};

// The request as a reviewer should see it: method, URL, and headers with their
// credentials hidden, then the body in the chosen view.
[[nodiscard]] QString describeAiHttpRequest(const AiHttpRequest& request, AiRequestView view = AiRequestView::Readable);

enum class AiTextConnectionBlock {
	None,
	AiFreeMode,
	// The open project's manifest turns AI off.
	ProjectAiFree,
	NoConnector,
	NoTextTransport,
	NoEndpoint,
	CloudNotAllowed,
	NoModel,
	NoCredential,
};

// Where the studio's text questions go, set up how, and what stops them.
struct AiTextConnection {
	QString connectorId;
	QString displayName;
	QString model;
	// The endpoint in effect: the configured one, else the default.
	QString endpoint;
	// The endpoint is on this machine; nothing leaves it.
	bool local = false;
	QString credentialVariable;
	bool credentialFound = false;
	AiTextConnectionBlock block = AiTextConnectionBlock::None;

	[[nodiscard]] bool ready() const
	{
		return block == AiTextConnectionBlock::None;
	}
};

// The reasoning connector, else the local one; with cloud connectors off, a
// local connector wins over a reasoning connector that would leave the
// machine. An override names the connector outright, as the CLI's --provider
// does. Cloud opt-in covers any endpoint off this machine, whichever connector
// reaches it.
[[nodiscard]] AiTextConnection resolveAiTextConnection(const AiAutomationPreferences& preferences, const QString& connectorOverride = QString());
[[nodiscard]] QString aiTextConnectionBlockId(AiTextConnectionBlock block);
// What stops the connection, and where to fix it.
[[nodiscard]] QString aiTextConnectionBlockText(const AiTextConnection& connection);
// Why nothing is sent while the open project's manifest turns AI off, for
// the text, image, and sound connections alike.
[[nodiscard]] QString aiProjectAiFreeText();
// The credential from the environment, or empty where none is needed.
[[nodiscard]] QString aiTextConnectionApiKey(const AiTextConnection& connection);

// A piece of project context the user chose to send with a question.
struct AiContextItem {
	QString id;
	QString label;
	QString text;
	// Set when the text was cut to fit; says what was left out.
	QString omittedNote;
};

// Cuts text to at most maxLines lines and maxBytes UTF-8 bytes, keeping whole
// lines, and says what was left out.
AiContextItem makeAiContextItem(const QString& id, const QString& label, const QString& text, int maxLines = 400, int maxBytes = 24 * 1024);
// The context as the prompt carries it: one fenced section per item.
[[nodiscard]] QString aiContextPromptText(const QVector<AiContextItem>& items);
// The studio assistant's standing instructions.
[[nodiscard]] QString studioAssistantSystemPrompt();

// Sends chat requests over HTTP, one at a time.
class AiChatClient {
public:
	using Callback = std::function<void(const AiChatResponse&)>;

	// Uses the given network manager, or owns one.
	explicit AiChatClient(QNetworkAccessManager* network = nullptr);
	~AiChatClient();
	AiChatClient(const AiChatClient&) = delete;
	AiChatClient& operator=(const AiChatClient&) = delete;

	// Starts the request; `done` runs once, with the answer or the failure,
	// from the event loop. Returns false, and says why, when nothing started.
	bool send(const AiChatRequest& request, const QString& apiKey, Callback done, QString* error = nullptr);
	// Stops the request in flight; `done` then runs with Cancelled.
	void cancel();
	[[nodiscard]] bool busy() const;
	// Longest a request may take in all, not just between bytes.
	void setTimeoutMsecs(int msecs);
	[[nodiscard]] int timeoutMsecs() const;

private:
	struct State;
	std::unique_ptr<State> m_state;
};

} // namespace vibestudio
