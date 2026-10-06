#pragma once

// Provider-neutral image generation over HTTP: what the studio asks an image
// model for (textures, sprites, concept art), how each provider family wants
// that asked, and how the pictures come back.
//
// Three wire formats:
//  - OpenAI Images: `POST {base}/images/generations`, or `/images/edits` with
//    a source image sent as multipart/form-data. OpenAI-compatible servers
//    such as LocalAI take the same shape:
//    https://platform.openai.com/docs/api-reference/images
//  - Gemini's generateContent asked for an image (`responseModalities`), a
//    source image going as inlineData:
//    https://ai.google.dev/gemini-api/docs/image-generation
//  - The Stable Diffusion web UI API of AUTOMATIC1111, Forge, and SD.Next:
//    `POST {base}/sdapi/v1/txt2img`, or `/img2img` with a source image. Its
//    "tiling" switch makes seamless textures on the model's side:
//    https://github.com/AUTOMATIC1111/stable-diffusion-webui/wiki/API
//
// As with text (core/ai_transport.h), building and parsing are plain
// functions, testable without a network, and the client sends one request at
// a time. Nothing here decides whether a request may be sent: AI-free mode,
// cloud opt-in, and consent are the caller's to check first.

#include "core/ai_transport.h"

#include <QByteArray>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

class QNetworkAccessManager;

namespace vibestudio {

enum class AiImageApi {
	None,
	OpenAiImages,
	GeminiImages,
	StableDiffusionWebUi,
};

// An image request in provider-neutral terms.
struct AiImageRequest {
	QString connectorId;
	// The provider's own model name. The Stable Diffusion web UI takes it as
	// a checkpoint to switch to, and uses the loaded one when it is empty.
	QString model;
	// Base URL; empty uses the connector's default image endpoint.
	QString endpoint;
	QString prompt;
	// What to keep out. The web UI takes it as such; the others are told in
	// the prompt.
	QString negativePrompt;
	// The size wanted. Providers with fixed sizes get the nearest shape they
	// offer; callers resize the answer to what they need.
	QSize size = QSize(1024, 1024);
	// Pictures wanted. Gemini makes one per request, so the client asks again.
	int count = 1;
	// OpenAI: low, medium, high, or auto. Empty leaves the provider's default.
	QString quality;
	bool transparentBackground = false;
	// Ask for an image that repeats seamlessly. Only the web UI can enforce
	// it; elsewhere it is part of the prompt, and callers blend the seams.
	bool tileable = false;
	// The web UI's seed; -1 lets it choose.
	qint64 seed = -1;
	// The web UI's sampling steps; 0 leaves its default.
	int steps = 0;
	// A source picture (PNG bytes) makes this an edit or image-to-image request.
	QByteArray sourceImage;
	// OpenAI edits: where the source may change (transparent pixels), as PNG.
	QByteArray maskImage;
	// The web UI's img2img denoising strength: how far to move from the source.
	double sourceStrength = 0.6;
};

struct AiGeneratedImage {
	// Encoded as the provider sent it: PNG, JPEG, or WebP.
	QByteArray bytes;
	QString mimeType;
	// Where the picture was fetched from, when the provider answered with a
	// link instead of the bytes.
	QString sourceUrl;
	// The prompt as the provider rewrote it, where it says.
	QString revisedPrompt;
	qint64 seed = -1;
};

struct AiImageResponse {
	bool ok = false;
	QVector<AiGeneratedImage> images;
	// Words the provider sent alongside the pictures (Gemini).
	QString text;
	QString model;
	int inputTokens = -1;
	int outputTokens = -1;
	int httpStatus = 0;
	AiChatFailure failure = AiChatFailure::None;
	// The provider's own words about a failure, secrets removed.
	QString errorMessage;
	// Links still to fetch, from an answer that sent no bytes. The client
	// fetches them; a caller of parseAiImageResponse does it itself.
	QStringList pendingUrls;
	qint64 elapsedMsecs = 0;
};

// The image API a connector speaks at an endpoint. The local connector speaks
// the web UI's API, or OpenAI's at an endpoint ending in /v1; a custom
// endpoint speaks OpenAI's unless it names /sdapi.
[[nodiscard]] AiImageApi aiImageApiFor(const QString& connectorId, const QString& endpoint = QString());
[[nodiscard]] QString aiImageApiId(AiImageApi api);
[[nodiscard]] bool aiConnectorHasImageTransport(const QString& connectorId);
[[nodiscard]] QString aiDefaultImageEndpoint(const QString& connectorId);
// A model to start from, or empty where the user must name one.
[[nodiscard]] QString aiSuggestedImageModel(const QString& connectorId);
[[nodiscard]] QString aiEffectiveImageEndpoint(const QString& connectorId, const QString& endpoint);

// The size a provider is asked for, given the size wanted.
[[nodiscard]] QSize aiImageRequestSize(AiImageApi api, const QString& endpoint, const QString& model, QSize wanted);
// The prompt as a provider without a negative-prompt field is sent it.
[[nodiscard]] QString aiImagePromptText(const AiImageRequest& request, AiImageApi api);

bool buildAiImageHttpRequest(const AiImageRequest& request, const QString& apiKey, AiHttpRequest* out, QString* error = nullptr);
[[nodiscard]] AiImageResponse parseAiImageResponse(AiImageApi api, int httpStatus, const QByteArray& body);
// PNG, JPEG, WebP, or GIF from the first bytes; empty when unknown.
[[nodiscard]] QString aiImageMimeType(const QByteArray& bytes);

enum class AiImageConnectionBlock {
	None,
	AiFreeMode,
	// The open project's manifest turns AI off.
	ProjectAiFree,
	NoConnector,
	NoImageTransport,
	NoEndpoint,
	CloudNotAllowed,
	NoModel,
	NoCredential,
};

// Where the studio's image requests go, set up how, and what stops them.
struct AiImageConnection {
	QString connectorId;
	QString displayName;
	QString model;
	QString endpoint;
	AiImageApi api = AiImageApi::None;
	bool local = false;
	QString credentialVariable;
	bool credentialFound = false;
	AiImageConnectionBlock block = AiImageConnectionBlock::None;

	[[nodiscard]] bool ready() const
	{
		return block == AiImageConnectionBlock::None;
	}
};

// The image connector, else the local one; with cloud connectors off, a local
// connector that makes images wins over an image connector that would leave
// the machine. An override names the connector outright.
[[nodiscard]] AiImageConnection resolveAiImageConnection(const AiAutomationPreferences& preferences, const QString& connectorOverride = QString());
[[nodiscard]] QString aiImageConnectionBlockId(AiImageConnectionBlock block);
// What stops the connection, and where to fix it.
[[nodiscard]] QString aiImageConnectionBlockText(const AiImageConnection& connection);
[[nodiscard]] QString aiImageConnectionApiKey(const AiImageConnection& connection);

// Sends image requests over HTTP, one at a time. A request for several
// pictures from a provider that makes one per call is asked again until the
// count is met, and pictures answered as links are fetched (without the key).
class AiImageClient {
public:
	using Callback = std::function<void(const AiImageResponse&)>;
	// Pictures in so far, of the total wanted.
	using Progress = std::function<void(int done, int total)>;

	explicit AiImageClient(QNetworkAccessManager* network = nullptr);
	~AiImageClient();
	AiImageClient(const AiImageClient&) = delete;
	AiImageClient& operator=(const AiImageClient&) = delete;

	// Starts the request; `done` runs once, from the event loop. Returns
	// false, and says why, when nothing started.
	bool send(const AiImageRequest& request, const QString& apiKey, Callback done, QString* error = nullptr, Progress progress = {});
	// Stops the request in flight; `done` then runs with Cancelled and any
	// pictures already in.
	void cancel();
	[[nodiscard]] bool busy() const;
	// Longest the whole request may take, every call included.
	void setTimeoutMsecs(int msecs);
	[[nodiscard]] int timeoutMsecs() const;

private:
	struct State;
	std::unique_ptr<State> m_state;
};

} // namespace vibestudio
