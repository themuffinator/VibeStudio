#include "core/ai_audio_transport.h"

#include "core/ai_connectors.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace vibestudio {

namespace {

AiChatFailure failureForStatus(int status)
{
	if (status == 401 || status == 403) {
		return AiChatFailure::Credential;
	}
	if (status == 429 || status == 503) {
		return AiChatFailure::RateLimited;
	}
	return AiChatFailure::Provider;
}

// ElevenLabs answers {"detail": {"status": ..., "message": ...}}, or FastAPI's
// validation list {"detail": [{"loc": [...], "msg": ...}]}.
QString errorMessageFrom(const QJsonObject& root, QString* status)
{
	const QJsonValue detail = root.value(QStringLiteral("detail"));
	if (detail.isObject()) {
		*status = detail.toObject().value(QStringLiteral("status")).toString();
		const QString message = detail.toObject().value(QStringLiteral("message")).toString();
		return message.isEmpty() ? *status : message;
	}
	if (detail.isArray()) {
		QStringList messages;
		for (const QJsonValue& entry : detail.toArray()) {
			const QJsonObject item = entry.toObject();
			QStringList where;
			for (const QJsonValue& part : item.value(QStringLiteral("loc")).toArray()) {
				where << (part.isString() ? part.toString() : QString::number(part.toInt()));
			}
			const QString message = item.value(QStringLiteral("msg")).toString();
			messages << (where.isEmpty() ? message : QStringLiteral("%1: %2").arg(where.join(QLatin1Char('.')), message));
		}
		messages.removeAll(QString());
		return messages.join(QStringLiteral("; "));
	}
	if (detail.isString()) {
		return detail.toString();
	}
	const QJsonValue error = root.value(QStringLiteral("error"));
	if (error.isObject()) {
		return error.toObject().value(QStringLiteral("message")).toString();
	}
	return error.isString() ? error.toString() : root.value(QStringLiteral("message")).toString();
}

} // namespace

AiSoundApi aiSoundApiFor(const QString& connectorId)
{
	const QString id = normalizedAiId(connectorId);
	return id == QStringLiteral("elevenlabs") || id == QStringLiteral("custom-http") ? AiSoundApi::ElevenLabsSoundEffects : AiSoundApi::None;
}

QString aiSoundApiId(AiSoundApi api)
{
	return api == AiSoundApi::ElevenLabsSoundEffects ? QStringLiteral("elevenlabs-sound-effects") : QStringLiteral("none");
}

bool aiConnectorHasSoundTransport(const QString& connectorId)
{
	return aiSoundApiFor(connectorId) != AiSoundApi::None;
}

QString aiDefaultSoundEndpoint(const QString& connectorId)
{
	return normalizedAiId(connectorId) == QStringLiteral("elevenlabs") ? QStringLiteral("https://api.elevenlabs.io") : QString();
}

QString aiSuggestedSoundModel(const QString& connectorId)
{
	return normalizedAiId(connectorId) == QStringLiteral("elevenlabs") ? QStringLiteral("eleven_text_to_sound_v2") : QString();
}

QString aiEffectiveSoundEndpoint(const QString& connectorId, const QString& endpoint)
{
	const QString configured = endpoint.trimmed();
	return configured.isEmpty() ? aiDefaultSoundEndpoint(connectorId) : configured;
}

bool buildAiSoundHttpRequest(const AiSoundRequest& request, const QString& apiKey, AiHttpRequest* out, QString* error)
{
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!out) {
		return false;
	}
	if (aiSoundApiFor(request.connectorId) == AiSoundApi::None) {
		return fail(QCoreApplication::translate("VibeStudioAiAudioTransport", "This connector does not make sounds."));
	}
	if (request.prompt.trimmed().isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioAiAudioTransport", "Describe the sound to make."));
	}
	QString base = aiEffectiveSoundEndpoint(request.connectorId, request.endpoint);
	while (base.endsWith(QLatin1Char('/'))) {
		base.chop(1);
	}
	if (base.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioAiAudioTransport", "No sound endpoint is set for this connector."));
	}
	// The API's path is versioned; a base already ending in /v1 keeps it.
	QUrl url(base.endsWith(QStringLiteral("/v1")) ? base + QStringLiteral("/sound-generation") : base + QStringLiteral("/v1/sound-generation"));
	if (!url.isValid() || (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http"))) {
		return fail(QCoreApplication::translate("VibeStudioAiAudioTransport", "The endpoint does not make a valid address."));
	}
	if (!request.outputFormat.isEmpty()) {
		QUrlQuery query(url);
		query.addQueryItem(QStringLiteral("output_format"), request.outputFormat);
		url.setQuery(query);
	}
	QJsonObject body {{QStringLiteral("text"), request.prompt.trimmed()}};
	if (!request.model.trimmed().isEmpty()) {
		body.insert(QStringLiteral("model_id"), request.model.trimmed());
	}
	if (request.durationSeconds > 0.0) {
		body.insert(QStringLiteral("duration_seconds"), std::clamp(request.durationSeconds, 0.5, 30.0));
	}
	if (request.promptInfluence >= 0.0) {
		body.insert(QStringLiteral("prompt_influence"), std::clamp(request.promptInfluence, 0.0, 1.0));
	}
	if (request.loop) {
		body.insert(QStringLiteral("loop"), true);
	}
	AiHttpRequest http;
	http.url = url;
	http.headers.append({QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")});
	http.headers.append({QByteArrayLiteral("Accept"), QByteArrayLiteral("audio/*")});
	if (!apiKey.isEmpty()) {
		// ElevenLabs' own header; a custom endpoint takes the usual bearer token.
		if (normalizedAiId(request.connectorId) == QStringLiteral("elevenlabs")) {
			http.headers.append({QByteArrayLiteral("xi-api-key"), apiKey.toUtf8()});
		} else {
			http.headers.append({QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + apiKey.toUtf8()});
		}
	}
	http.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
	*out = http;
	return true;
}

QString aiSoundMimeType(const QByteArray& bytes)
{
	if (bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WAVE") {
		return QStringLiteral("audio/wav");
	}
	if (bytes.startsWith("ID3") || (bytes.size() > 1 && quint8(bytes[0]) == 0xFF && (quint8(bytes[1]) & 0xE0) == 0xE0)) {
		return QStringLiteral("audio/mpeg");
	}
	if (bytes.startsWith("OggS")) {
		return QStringLiteral("audio/ogg");
	}
	if (bytes.startsWith("fLaC")) {
		return QStringLiteral("audio/flac");
	}
	return {};
}

AiSoundResponse parseAiSoundResponse(int httpStatus, const QByteArray& body, const QByteArray& contentType)
{
	AiSoundResponse response;
	response.httpStatus = httpStatus;
	const QJsonDocument document = QJsonDocument::fromJson(body);
	const QJsonObject root = document.isObject() ? document.object() : QJsonObject();
	if (httpStatus < 200 || httpStatus >= 300) {
		response.failure = failureForStatus(httpStatus);
		QString status;
		QString message = root.isEmpty() ? QString::fromUtf8(body.left(400)).trimmed() : errorMessageFrom(root, &status);
		if (message.isEmpty()) {
			message = QCoreApplication::translate("VibeStudioAiAudioTransport", "The provider answered HTTP %1.").arg(httpStatus);
		}
		if (status.contains(QStringLiteral("moderation")) || status.contains(QStringLiteral("blocked"))) {
			response.failure = AiChatFailure::Refused;
		} else if (status.contains(QStringLiteral("quota")) || status.contains(QStringLiteral("rate"))) {
			response.failure = AiChatFailure::RateLimited;
		}
		response.errorMessage = redactAiText(message);
		return response;
	}
	if (body.isEmpty() || !root.isEmpty()) {
		response.failure = AiChatFailure::BadResponse;
		response.errorMessage = root.isEmpty() ? QCoreApplication::translate("VibeStudioAiAudioTransport", "The provider's answer had no sound in it.")
											   : QCoreApplication::translate("VibeStudioAiAudioTransport", "The provider answered with JSON instead of a sound: %1")
													 .arg(redactAiText(QString::fromUtf8(body.left(300))));
		return response;
	}
	response.audio = body;
	response.mimeType = aiSoundMimeType(body);
	if (response.mimeType.isEmpty()) {
		response.mimeType = QString::fromLatin1(contentType.split(';').value(0).trimmed());
	}
	response.ok = true;
	return response;
}

AiSoundConnection resolveAiSoundConnection(const AiAutomationPreferences& preferences, const QString& connectorOverride)
{
	const AiAutomationPreferences normalized = normalizedAiAutomationPreferences(preferences);
	AiSoundConnection connection;
	connection.connectorId = normalizedAiId(connectorOverride);
	if (connection.connectorId.isEmpty()) {
		connection.connectorId = normalized.preferredAudioConnectorId;
	}
	AiConnectorDescriptor descriptor;
	const bool known = !connection.connectorId.isEmpty() && aiConnectorForId(connection.connectorId, &descriptor);
	connection.displayName = known ? descriptor.displayName : connection.connectorId;
	if (normalized.aiFreeMode) {
		connection.block = AiSoundConnectionBlock::AiFreeMode;
		return connection;
	}
	if (normalized.projectAiFree) {
		connection.block = AiSoundConnectionBlock::ProjectAiFree;
		return connection;
	}
	if (!known) {
		connection.block = AiSoundConnectionBlock::NoConnector;
		return connection;
	}
	if (!aiConnectorHasSoundTransport(connection.connectorId)) {
		connection.block = AiSoundConnectionBlock::NoSoundTransport;
		return connection;
	}
	connection.endpoint = aiEffectiveSoundEndpoint(connection.connectorId, normalized.connectorAudioEndpoints.value(connection.connectorId));
	connection.model = normalized.connectorAudioModels.value(connection.connectorId, aiSuggestedSoundModel(connection.connectorId));
	if (connection.endpoint.isEmpty()) {
		connection.block = AiSoundConnectionBlock::NoEndpoint;
		return connection;
	}
	connection.local = aiEndpointIsLocal(connection.endpoint);
	if (!connection.local && !normalized.cloudConnectorsEnabled) {
		connection.block = AiSoundConnectionBlock::CloudNotAllowed;
		return connection;
	}
	// As for text and images: a server on this machine needs no key, a cloud
	// provider does, and a custom endpoint only when the user named a variable.
	connection.credentialVariable = aiCredentialEnvironmentVariableForConnector(connection.connectorId, normalized);
	if (!descriptor.cloudBased || connection.local) {
		connection.credentialVariable.clear();
	}
	if (!connection.credentialVariable.isEmpty()) {
		connection.credentialFound = !qEnvironmentVariable(connection.credentialVariable.toUtf8().constData()).trimmed().isEmpty();
		if (!connection.credentialFound) {
			connection.block = AiSoundConnectionBlock::NoCredential;
		}
	}
	return connection;
}

QString aiSoundConnectionBlockId(AiSoundConnectionBlock block)
{
	switch (block) {
	case AiSoundConnectionBlock::None:
		return QStringLiteral("ready");
	case AiSoundConnectionBlock::AiFreeMode:
		return QStringLiteral("ai-free-mode");
	case AiSoundConnectionBlock::ProjectAiFree:
		return QStringLiteral("project-ai-free");
	case AiSoundConnectionBlock::NoConnector:
		return QStringLiteral("no-connector");
	case AiSoundConnectionBlock::NoSoundTransport:
		return QStringLiteral("no-sound-transport");
	case AiSoundConnectionBlock::NoEndpoint:
		return QStringLiteral("no-endpoint");
	case AiSoundConnectionBlock::CloudNotAllowed:
		return QStringLiteral("cloud-not-allowed");
	case AiSoundConnectionBlock::NoCredential:
		return QStringLiteral("no-credential");
	}
	return QStringLiteral("no-connector");
}

QString aiSoundConnectionBlockText(const AiSoundConnection& connection)
{
	switch (connection.block) {
	case AiSoundConnectionBlock::None: {
		const QString model = connection.model.isEmpty() ? QCoreApplication::translate("VibeStudioAiAudioTransport", "its default model") : connection.model;
		return connection.local
			? QCoreApplication::translate("VibeStudioAiAudioTransport", "Ready: %1 makes sounds with %2, on this machine.").arg(connection.displayName, model)
			: QCoreApplication::translate("VibeStudioAiAudioTransport", "Ready: %1 makes sounds with %2, at %3.").arg(connection.displayName, model, QUrl(connection.endpoint).host());
	}
	case AiSoundConnectionBlock::AiFreeMode:
		return QCoreApplication::translate("VibeStudioAiAudioTransport", "AI-free mode is on. Turn it off in Settings > AI and Automation to generate sounds with a model; the synthesizer works without it.");
	case AiSoundConnectionBlock::ProjectAiFree:
		return aiProjectAiFreeText();
	case AiSoundConnectionBlock::NoConnector:
		return QCoreApplication::translate("VibeStudioAiAudioTransport", "No sound connector is chosen. Pick ElevenLabs or a custom endpoint as the Audio connector in Settings > AI and Automation.");
	case AiSoundConnectionBlock::NoSoundTransport:
		return QCoreApplication::translate("VibeStudioAiAudioTransport", "%1 does not make sounds. Pick ElevenLabs or a custom endpoint.").arg(connection.displayName);
	case AiSoundConnectionBlock::NoEndpoint:
		return QCoreApplication::translate("VibeStudioAiAudioTransport", "%1 has no sound endpoint. Set one in Settings > AI and Automation.").arg(connection.displayName);
	case AiSoundConnectionBlock::CloudNotAllowed:
		return QCoreApplication::translate("VibeStudioAiAudioTransport", "%1 sends to %2, off this machine, and cloud connectors are not allowed. Allow them in Settings > AI and Automation, or use the synthesizer.")
			.arg(connection.displayName, QUrl(connection.endpoint).host());
	case AiSoundConnectionBlock::NoCredential:
		return QCoreApplication::translate("VibeStudioAiAudioTransport", "%1 needs an API key in the %2 environment variable, which is not set.").arg(connection.displayName, connection.credentialVariable);
	}
	return {};
}

QString aiSoundConnectionApiKey(const AiSoundConnection& connection)
{
	return connection.credentialVariable.isEmpty() ? QString() : qEnvironmentVariable(connection.credentialVariable.toUtf8().constData()).trimmed();
}

// ---------------------------------------------------------------------------
// AiSoundClient

struct AiSoundClient::State {
	QNetworkAccessManager* network = nullptr;
	std::unique_ptr<QNetworkAccessManager> ownedNetwork;
	std::unique_ptr<QTimer> deadline;
	QPointer<QNetworkReply> reply;
	QElapsedTimer clock;
	QStringList secrets;
	Callback done;
	int timeoutMsecs = 3 * 60 * 1000;
	bool cancelled = false;
	bool timedOut = false;
};

AiSoundClient::AiSoundClient(QNetworkAccessManager* network)
	: m_state(std::make_unique<State>())
{
	if (network) {
		m_state->network = network;
	} else {
		m_state->ownedNetwork = std::make_unique<QNetworkAccessManager>();
		m_state->network = m_state->ownedNetwork.get();
	}
	m_state->deadline = std::make_unique<QTimer>();
	m_state->deadline->setSingleShot(true);
	QObject::connect(m_state->deadline.get(), &QTimer::timeout, m_state->deadline.get(), [this]() {
		if (QNetworkReply* reply = m_state->reply) {
			m_state->timedOut = true;
			reply->abort();
		}
	});
}

AiSoundClient::~AiSoundClient()
{
	if (QNetworkReply* reply = m_state->reply) {
		QObject::disconnect(reply, nullptr, nullptr, nullptr);
		reply->abort();
		reply->deleteLater();
	}
}

bool AiSoundClient::send(const AiSoundRequest& request, const QString& apiKey, Callback done, QString* error)
{
	if (busy()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioAiAudioTransport", "A request is already running.");
		}
		return false;
	}
	AiHttpRequest http;
	if (!buildAiSoundHttpRequest(request, apiKey, &http, error)) {
		return false;
	}
	m_state->secrets = apiKey.isEmpty() ? QStringList() : QStringList {apiKey};
	m_state->done = std::move(done);
	m_state->cancelled = false;
	m_state->timedOut = false;
	m_state->clock.start();
	m_state->deadline->start(m_state->timeoutMsecs);
	QNetworkRequest networkRequest(http.url);
	for (const auto& header : http.headers) {
		networkRequest.setRawHeader(header.first, header.second);
	}
	networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	QNetworkReply* reply = m_state->network->post(networkRequest, http.body);
	m_state->reply = reply;
	QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply]() {
		State& state = *m_state;
		state.deadline->stop();
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QByteArray body = reply->readAll();
		AiSoundResponse response;
		if (state.cancelled || state.timedOut) {
			response.failure = state.cancelled ? AiChatFailure::Cancelled : AiChatFailure::Timeout;
			response.errorMessage = state.cancelled ? aiChatFailureText(AiChatFailure::Cancelled)
												  : QCoreApplication::translate("VibeStudioAiAudioTransport", "No sound after %1 seconds.").arg(state.timeoutMsecs / 1000);
		} else if (status <= 0) {
			response.failure = AiChatFailure::Network;
			response.errorMessage = reply->errorString();
		} else {
			response = parseAiSoundResponse(status, body, reply->header(QNetworkRequest::ContentTypeHeader).toByteArray());
			response.cost = QString::fromLatin1(reply->rawHeader("character-cost"));
			response.requestId = QString::fromLatin1(reply->rawHeader("request-id"));
		}
		reply->deleteLater();
		state.reply = nullptr;
		response.errorMessage = redactAiText(response.errorMessage, state.secrets);
		response.elapsedMsecs = state.clock.elapsed();
		state.secrets.clear();
		Callback callback = std::move(state.done);
		state.done = nullptr;
		if (callback) {
			callback(response);
		}
	});
	return true;
}

void AiSoundClient::cancel()
{
	if (QNetworkReply* reply = m_state->reply) {
		m_state->cancelled = true;
		reply->abort();
	}
}

bool AiSoundClient::busy() const
{
	return !m_state->reply.isNull();
}

void AiSoundClient::setTimeoutMsecs(int msecs)
{
	m_state->timeoutMsecs = std::max(1000, msecs);
}

int AiSoundClient::timeoutMsecs() const
{
	return m_state->timeoutMsecs;
}

} // namespace vibestudio
