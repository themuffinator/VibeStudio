#pragma once

// Provider-neutral sound-effect generation over HTTP: a description in, a
// short sound out, for the Sound Generator and `audio generate`.
//
// One wire format so far, ElevenLabs' Sound Effects API:
// `POST {base}/v1/sound-generation?output_format=...` with the description,
// an optional length (0.5 to 30 seconds), how closely to follow it, and
// whether the sound should loop; the answer is the encoded audio itself.
// https://elevenlabs.io/docs/api-reference/text-to-sound-effects/convert
// The custom connector sends the same request to an address of its own.
//
// As with text (core/ai_transport.h) and images, building and parsing are
// plain functions, testable without a network, and the client sends one
// request at a time. Nothing here decides whether a request may be sent:
// AI-free mode, cloud opt-in, and consent are the caller's to check first.

#include "core/ai_transport.h"

#include <QByteArray>
#include <QString>

#include <functional>
#include <memory>

class QNetworkAccessManager;

namespace vibestudio {

enum class AiSoundApi {
	None,
	ElevenLabsSoundEffects,
};

// A sound request in provider-neutral terms.
struct AiSoundRequest {
	QString connectorId;
	// The provider's own model name; empty leaves the provider's default.
	QString model;
	// Base URL; empty uses the connector's default sound endpoint.
	QString endpoint;
	QString prompt;
	// Seconds wanted, 0.5 to 30; 0 lets the model choose.
	double durationSeconds = 0.0;
	// How closely to follow the description, 0 to 1; negative leaves the
	// provider's default.
	double promptInfluence = -1.0;
	bool loop = false;
	// The provider's name for the encoding. MP3 is offered on every plan and
	// decodes wherever VibeStudio runs.
	QString outputFormat = QStringLiteral("mp3_44100_128");
};

struct AiSoundResponse {
	bool ok = false;
	// Encoded as the provider sent it.
	QByteArray audio;
	QString mimeType;
	int httpStatus = 0;
	AiChatFailure failure = AiChatFailure::None;
	// The provider's own words about a failure, secrets removed.
	QString errorMessage;
	// What the request cost, where the provider says (ElevenLabs'
	// character-cost header), and its id for support.
	QString cost;
	QString requestId;
	qint64 elapsedMsecs = 0;
};

[[nodiscard]] AiSoundApi aiSoundApiFor(const QString& connectorId);
[[nodiscard]] QString aiSoundApiId(AiSoundApi api);
[[nodiscard]] bool aiConnectorHasSoundTransport(const QString& connectorId);
[[nodiscard]] QString aiDefaultSoundEndpoint(const QString& connectorId);
// A model to start from, or empty where the provider's default serves.
[[nodiscard]] QString aiSuggestedSoundModel(const QString& connectorId);
[[nodiscard]] QString aiEffectiveSoundEndpoint(const QString& connectorId, const QString& endpoint);
bool buildAiSoundHttpRequest(const AiSoundRequest& request, const QString& apiKey, AiHttpRequest* out, QString* error = nullptr);
[[nodiscard]] AiSoundResponse parseAiSoundResponse(int httpStatus, const QByteArray& body, const QByteArray& contentType = QByteArray());
// WAV, MP3, Ogg, or FLAC from the first bytes; empty when unknown.
[[nodiscard]] QString aiSoundMimeType(const QByteArray& bytes);

enum class AiSoundConnectionBlock {
	None,
	AiFreeMode,
	// The open project's manifest turns AI off.
	ProjectAiFree,
	NoConnector,
	NoSoundTransport,
	NoEndpoint,
	CloudNotAllowed,
	NoCredential,
};

// Where the studio's sound requests go, set up how, and what stops them.
struct AiSoundConnection {
	QString connectorId;
	QString displayName;
	QString model;
	QString endpoint;
	bool local = false;
	QString credentialVariable;
	bool credentialFound = false;
	AiSoundConnectionBlock block = AiSoundConnectionBlock::None;

	[[nodiscard]] bool ready() const
	{
		return block == AiSoundConnectionBlock::None;
	}
};

// The audio connector, or the one an override names (as the CLI's
// --provider does), under the same AI-free, cloud opt-in, and credential
// rules as text and images.
[[nodiscard]] AiSoundConnection resolveAiSoundConnection(const AiAutomationPreferences& preferences, const QString& connectorOverride = QString());
[[nodiscard]] QString aiSoundConnectionBlockId(AiSoundConnectionBlock block);
// What stops the connection, and where to fix it.
[[nodiscard]] QString aiSoundConnectionBlockText(const AiSoundConnection& connection);
[[nodiscard]] QString aiSoundConnectionApiKey(const AiSoundConnection& connection);

// Sends sound requests over HTTP, one at a time.
class AiSoundClient {
public:
	using Callback = std::function<void(const AiSoundResponse&)>;

	explicit AiSoundClient(QNetworkAccessManager* network = nullptr);
	~AiSoundClient();
	AiSoundClient(const AiSoundClient&) = delete;
	AiSoundClient& operator=(const AiSoundClient&) = delete;

	// Starts the request; `done` runs once, from the event loop. Returns
	// false, and says why, when nothing started.
	bool send(const AiSoundRequest& request, const QString& apiKey, Callback done, QString* error = nullptr);
	// Stops the request in flight; `done` then runs with Cancelled.
	void cancel();
	[[nodiscard]] bool busy() const;
	void setTimeoutMsecs(int msecs);
	[[nodiscard]] int timeoutMsecs() const;

private:
	struct State;
	std::unique_ptr<State> m_state;
};

} // namespace vibestudio
