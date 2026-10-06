#include "core/ai_image_transport.h"

#include "tests/fake_ai_provider.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>

using namespace vibestudio;

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << "\n";
		++failures;
	}
}

bool waitUntil(const std::function<bool()>& done, int timeoutMsecs = 5000)
{
	QElapsedTimer clock;
	clock.start();
	while (!done() && clock.elapsed() < timeoutMsecs) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
	}
	return done();
}

QByteArray headerValue(const AiHttpRequest& request, const QByteArray& name)
{
	for (const auto& header : request.headers) {
		if (header.first.toLower() == name.toLower()) {
			return header.second;
		}
	}
	return {};
}

QJsonObject bodyOf(const QByteArray& body)
{
	return QJsonDocument::fromJson(body).object();
}

QByteArray pngBytes(const QColor& color, QSize size = QSize(8, 8))
{
	QImage image(size, QImage::Format_ARGB32);
	image.fill(color);
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	return bytes;
}

AiImageRequest texture(const QString& connectorId, const QString& model, const QString& endpoint = QString())
{
	AiImageRequest request;
	request.connectorId = connectorId;
	request.model = model;
	request.endpoint = endpoint;
	request.prompt = QStringLiteral("Rusted riveted metal plate, seamless");
	request.negativePrompt = QStringLiteral("text, logos");
	request.size = QSize(1024, 1024);
	return request;
}

void checkApis()
{
	expect(aiImageApiFor(QStringLiteral("openai")) == AiImageApi::OpenAiImages && aiImageApiFor(QStringLiteral("Gemini")) == AiImageApi::GeminiImages,
		"OpenAI speaks its Images API and Gemini its generateContent.");
	expect(aiImageApiFor(QStringLiteral("local-offline"), QStringLiteral("http://127.0.0.1:7860")) == AiImageApi::StableDiffusionWebUi
			&& aiImageApiFor(QStringLiteral("local-offline"), QStringLiteral("http://localhost:8080/v1")) == AiImageApi::OpenAiImages,
		"The local connector speaks the web UI's API, or OpenAI's at an address ending in /v1.");
	expect(aiImageApiFor(QStringLiteral("custom-http"), QStringLiteral("https://images.example.test/v1")) == AiImageApi::OpenAiImages
			&& aiImageApiFor(QStringLiteral("custom-http"), QStringLiteral("https://sd.example.test/sdapi/v1")) == AiImageApi::StableDiffusionWebUi,
		"A custom endpoint speaks OpenAI's API unless it names /sdapi.");
	expect(!aiConnectorHasImageTransport(QStringLiteral("claude")) && aiImageApiFor(QStringLiteral("claude")) == AiImageApi::None,
		"Claude does not make images.");
	expect(aiDefaultImageEndpoint(QStringLiteral("local-offline")) == QStringLiteral("http://127.0.0.1:7860")
			&& aiDefaultImageEndpoint(QStringLiteral("openai")) == QStringLiteral("https://api.openai.com/v1") && aiDefaultImageEndpoint(QStringLiteral("custom-http")).isEmpty(),
		"Image endpoints default to the web UI's address and OpenAI's API; a custom one has none.");
	expect(aiSuggestedImageModel(QStringLiteral("openai")) == QStringLiteral("gpt-image-1.5") && aiSuggestedImageModel(QStringLiteral("gemini")).isEmpty(),
		"OpenAI suggests its image model; Gemini's must be named.");
	expect(aiImageApiId(AiImageApi::StableDiffusionWebUi) == QStringLiteral("sd-webui"), "Image APIs have ids.");
	expect(aiImageMimeType(pngBytes(Qt::red)) == QStringLiteral("image/png") && aiImageMimeType(QByteArray("\xFF\xD8\xFF\xE0", 4)) == QStringLiteral("image/jpeg")
			&& aiImageMimeType(QByteArray("RIFF\0\0\0\0WEBPVP8 ", 16)) == QStringLiteral("image/webp") && aiImageMimeType("nothing").isEmpty(),
		"Pictures are typed by their first bytes.");
}

void checkSizes()
{
	const QString openAi = QStringLiteral("https://api.openai.com/v1");
	expect(aiImageRequestSize(AiImageApi::OpenAiImages, openAi, QStringLiteral("gpt-image-1.5"), QSize(64, 64)) == QSize(1024, 1024)
			&& aiImageRequestSize(AiImageApi::OpenAiImages, openAi, QStringLiteral("gpt-image-1.5"), QSize(256, 128)) == QSize(1536, 1024)
			&& aiImageRequestSize(AiImageApi::OpenAiImages, openAi, QStringLiteral("gpt-image-1.5"), QSize(64, 128)) == QSize(1024, 1536),
		"OpenAI's GPT image models get the nearest of their three shapes.");
	expect(aiImageRequestSize(AiImageApi::OpenAiImages, openAi, QStringLiteral("dall-e-2"), QSize(200, 200)) == QSize(256, 256),
		"dall-e-2 gets the nearest square.");
	expect(aiImageRequestSize(AiImageApi::OpenAiImages, QStringLiteral("http://127.0.0.1:8080/v1"), QStringLiteral("sd"), QSize(300, 700)) == QSize(300, 700),
		"A compatible server is asked for the size wanted.");
	expect(aiImageRequestSize(AiImageApi::StableDiffusionWebUi, QString(), QString(), QSize(300, 700)) == QSize(320, 704)
			&& aiImageRequestSize(AiImageApi::StableDiffusionWebUi, QString(), QString(), QSize(64, 64)) == QSize(256, 256),
		"The web UI is asked for multiples of 64 within its range.");
}

void checkRequests()
{
	AiHttpRequest http;
	QString error;

	// OpenAI itself.
	expect(buildAiImageHttpRequest(texture(QStringLiteral("openai"), QStringLiteral("gpt-image-1.5")), QStringLiteral("sk-test-images-1234567890"), &http, &error),
		"An OpenAI image request should build.");
	QJsonObject body = bodyOf(http.body);
	expect(http.url.toString() == QStringLiteral("https://api.openai.com/v1/images/generations") && headerValue(http, "authorization") == "Bearer sk-test-images-1234567890",
		"OpenAI image requests go to images/generations with a bearer key.");
	expect(body.value(QStringLiteral("model")).toString() == QStringLiteral("gpt-image-1.5") && body.value(QStringLiteral("size")).toString() == QStringLiteral("1024x1024")
			&& body.value(QStringLiteral("output_format")).toString() == QStringLiteral("png") && !body.contains(QStringLiteral("response_format"))
			&& body.value(QStringLiteral("prompt")).toString().contains(QStringLiteral("Avoid: text, logos")),
		"GPT image models are asked for PNG, without response_format, the negative prompt folded in.");
	AiImageRequest transparent = texture(QStringLiteral("openai"), QStringLiteral("gpt-image-1.5"));
	transparent.transparentBackground = true;
	transparent.quality = QStringLiteral("high");
	buildAiImageHttpRequest(transparent, QStringLiteral("k"), &http);
	expect(bodyOf(http.body).value(QStringLiteral("background")).toString() == QStringLiteral("transparent") && bodyOf(http.body).value(QStringLiteral("quality")).toString() == QStringLiteral("high"),
		"A sprite can ask for a transparent background and a quality.");

	// An OpenAI-compatible server asks for base64 explicitly.
	buildAiImageHttpRequest(texture(QStringLiteral("custom-http"), QStringLiteral("flux"), QStringLiteral("http://127.0.0.1:8080/v1/")), QString(), &http);
	body = bodyOf(http.body);
	expect(http.url.toString() == QStringLiteral("http://127.0.0.1:8080/v1/images/generations") && body.value(QStringLiteral("response_format")).toString() == QStringLiteral("b64_json")
			&& !body.contains(QStringLiteral("output_format")),
		"A compatible server is asked for b64_json.");

	// An edit is a form with the source picture in it.
	AiImageRequest edit = texture(QStringLiteral("openai"), QStringLiteral("gpt-image-1.5"));
	edit.sourceImage = pngBytes(Qt::darkGray, QSize(16, 16));
	edit.maskImage = pngBytes(Qt::transparent, QSize(16, 16));
	expect(buildAiImageHttpRequest(edit, QStringLiteral("sk-test-images-1234567890"), &http, &error), "An OpenAI edit should build.");
	const QByteArray contentType = headerValue(http, "content-type");
	expect(http.url.path() == QStringLiteral("/v1/images/edits") && contentType.startsWith("multipart/form-data; boundary=")
			&& http.body.contains("name=\"image\"; filename=\"source.png\"") && http.body.contains(edit.sourceImage)
			&& http.body.contains("name=\"mask\"; filename=\"mask.png\"") && http.body.contains("name=\"model\"\r\n\r\ngpt-image-1.5\r\n"),
		"An OpenAI edit sends the model, prompt, source, and mask as form parts.");
	const QString described = describeAiHttpRequest(http);
	expect(described.contains(QStringLiteral("image: source.png (image/png,")) && described.contains(QStringLiteral("model: gpt-image-1.5"))
			&& described.contains(QStringLiteral("Authorization: ***")) && !described.contains(QStringLiteral("sk-test-images")) && !described.contains(QStringLiteral("IHDR")),
		"The preview of a form shows each part, files by size, and never the key or the picture's bytes.");

	// Gemini.
	AiImageRequest gemini = texture(QStringLiteral("gemini"), QStringLiteral("models/gemini-image-test"));
	gemini.size = QSize(1920, 1080);
	expect(buildAiImageHttpRequest(gemini, QStringLiteral("AIzaTestKey"), &http, &error), "A Gemini image request should build.");
	body = bodyOf(http.body);
	const QJsonObject generation = body.value(QStringLiteral("generationConfig")).toObject();
	expect(http.url.toString() == QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/gemini-image-test:generateContent")
			&& headerValue(http, "x-goog-api-key") == "AIzaTestKey",
		"Gemini image requests name the model in the path.");
	expect(generation.value(QStringLiteral("responseModalities")).toArray().contains(QStringLiteral("IMAGE"))
			&& generation.value(QStringLiteral("imageConfig")).toObject().value(QStringLiteral("aspectRatio")).toString() == QStringLiteral("16:9"),
		"Gemini is asked for an image at the nearest aspect ratio it offers.");
	gemini.sourceImage = QByteArray(4096, 'x');
	gemini.size = QSize(512, 512);
	buildAiImageHttpRequest(gemini, QStringLiteral("AIzaTestKey"), &http);
	const QJsonArray parts = bodyOf(http.body).value(QStringLiteral("contents")).toArray().first().toObject().value(QStringLiteral("parts")).toArray();
	expect(parts.size() == 2 && parts.at(1).toObject().value(QStringLiteral("inlineData")).toObject().value(QStringLiteral("mimeType")).toString() == QStringLiteral("image/png"),
		"A Gemini edit sends the source as inlineData after the prompt.");
	const QString geminiPreview = describeAiHttpRequest(http);
	expect(geminiPreview.contains(QStringLiteral("[image/png <")) && geminiPreview.contains(QStringLiteral("bytes of base64 data>")) && !geminiPreview.contains(QStringLiteral("eHh4eHh4")),
		"The readable preview names an attached picture by type and size.");
	expect(describeAiHttpRequest(http, AiRequestView::Raw).contains(QStringLiteral("bytes of base64 data")), "The raw preview elides picture data too.");

	// The Stable Diffusion web UI.
	AiImageRequest local = texture(QStringLiteral("local-offline"), QString());
	local.tileable = true;
	local.count = 3;
	local.seed = 42;
	local.steps = 25;
	local.size = QSize(512, 512);
	expect(buildAiImageHttpRequest(local, QString(), &http, &error), "A web UI request needs no model.");
	body = bodyOf(http.body);
	expect(http.url.toString() == QStringLiteral("http://127.0.0.1:7860/sdapi/v1/txt2img") && headerValue(http, "authorization").isEmpty(),
		"The web UI is asked at txt2img without a key.");
	expect(body.value(QStringLiteral("tiling")).toBool() && body.value(QStringLiteral("negative_prompt")).toString() == QStringLiteral("text, logos")
			&& !body.value(QStringLiteral("prompt")).toString().contains(QStringLiteral("Avoid:")) && body.value(QStringLiteral("batch_size")).toInt() == 3
			&& body.value(QStringLiteral("seed")).toInteger() == 42 && body.value(QStringLiteral("steps")).toInt() == 25 && body.value(QStringLiteral("width")).toInt() == 512
			&& !body.contains(QStringLiteral("override_settings")),
		"The web UI gets tiling, its own negative prompt, a batch, the seed, and the steps.");
	local.model = QStringLiteral("dreamshaper_8");
	local.endpoint = QStringLiteral("http://127.0.0.1:7861/sdapi/v1/");
	local.sourceImage = pngBytes(Qt::gray);
	local.sourceStrength = 0.35;
	buildAiImageHttpRequest(local, QString(), &http);
	body = bodyOf(http.body);
	expect(http.url.toString() == QStringLiteral("http://127.0.0.1:7861/sdapi/v1/img2img") && body.value(QStringLiteral("init_images")).toArray().size() == 1
			&& qAbs(body.value(QStringLiteral("denoising_strength")).toDouble() - 0.35) < 1e-9
			&& body.value(QStringLiteral("override_settings")).toObject().value(QStringLiteral("sd_model_checkpoint")).toString() == QStringLiteral("dreamshaper_8"),
		"A source picture makes it img2img, at the given strength, on the named checkpoint.");

	// What cannot be asked.
	expect(!buildAiImageHttpRequest(texture(QStringLiteral("openai"), QString()), QString(), &http, &error) && error.contains(QStringLiteral("model")),
		"OpenAI needs an image model.");
	AiImageRequest silent = texture(QStringLiteral("local-offline"), QString());
	silent.prompt = QStringLiteral("  ");
	expect(!buildAiImageHttpRequest(silent, QString(), &http, &error), "An empty prompt is not sent.");
	expect(!buildAiImageHttpRequest(texture(QStringLiteral("claude"), QStringLiteral("m")), QString(), &http, &error), "Claude does not make images.");
	expect(!buildAiImageHttpRequest(texture(QStringLiteral("custom-http"), QStringLiteral("m")), QString(), &http, &error) && error.contains(QStringLiteral("endpoint")),
		"A custom connector without an endpoint says so.");
	expect(!buildAiImageHttpRequest(texture(QStringLiteral("local-offline"), QString(), QStringLiteral("ftp://127.0.0.1")), QString(), &http, &error),
		"Only http and https endpoints are asked.");
}

void checkAnswers()
{
	const QByteArray png = pngBytes(Qt::blue);
	const QString encoded = QString::fromLatin1(png.toBase64());

	AiImageResponse response = parseAiImageResponse(AiImageApi::OpenAiImages, 200,
		QStringLiteral(R"({"created":1,"data":[{"b64_json":"%1","revised_prompt":"rusty plate"}],"usage":{"input_tokens":30,"output_tokens":4160}})").arg(encoded).toUtf8());
	expect(response.ok && response.images.size() == 1 && response.images.first().bytes == png && response.images.first().mimeType == QStringLiteral("image/png")
			&& response.images.first().revisedPrompt == QStringLiteral("rusty plate") && response.inputTokens == 30 && response.outputTokens == 4160,
		"An OpenAI answer gives its pictures, the rewritten prompt, and usage.");
	response = parseAiImageResponse(AiImageApi::OpenAiImages, 200, R"({"data":[{"url":"https://files.example.test/a.png"}]})");
	expect(response.ok && response.images.isEmpty() && response.pendingUrls == QStringList {QStringLiteral("https://files.example.test/a.png")},
		"A linked picture is left for the client to fetch.");
	response = parseAiImageResponse(AiImageApi::OpenAiImages, 400,
		R"({"error":{"message":"Your request was rejected by the safety system.","type":"image_generation_user_error","code":"moderation_blocked"}})");
	expect(!response.ok && response.failure == AiChatFailure::Refused && response.errorMessage.contains(QStringLiteral("safety system")),
		"OpenAI's moderation block is a refusal.");
	expect(parseAiImageResponse(AiImageApi::OpenAiImages, 401, R"({"error":{"message":"Incorrect API key provided: sk-proj-abcdefghijklmnop"}})").failure == AiChatFailure::Credential,
		"A refused key is a credential failure.");
	expect(!parseAiImageResponse(AiImageApi::OpenAiImages, 401, R"({"error":{"message":"Incorrect API key provided: sk-proj-abcdefghijklmnop"}})").errorMessage.contains(QStringLiteral("abcdefghijklmnop")),
		"A key in the provider's words is hidden.");
	expect(parseAiImageResponse(AiImageApi::OpenAiImages, 200, R"({"data":[]})").failure == AiChatFailure::BadResponse, "An answer with no picture cannot be used.");

	response = parseAiImageResponse(AiImageApi::GeminiImages, 200,
		QStringLiteral(R"({"candidates":[{"content":{"parts":[{"text":"thinking","thought":true},{"inlineData":{"mimeType":"image/png","data":"AAAA"},"thought":true},{"text":"Here is your texture."},{"inlineData":{"mimeType":"image/png","data":"%1"}}]},"finishReason":"STOP"}],"usageMetadata":{"promptTokenCount":12,"candidatesTokenCount":1290},"modelVersion":"gemini-image-test"})")
			.arg(encoded)
			.toUtf8());
	expect(response.ok && response.images.size() == 1 && response.images.first().bytes == png && response.text == QStringLiteral("Here is your texture.")
			&& response.model == QStringLiteral("gemini-image-test") && response.outputTokens == 1290,
		"A Gemini answer gives its last picture that is not a thought, and its words.");
	response = parseAiImageResponse(AiImageApi::GeminiImages, 200, R"({"candidates":[{"content":{"parts":[]},"finishReason":"IMAGE_SAFETY"}]})");
	expect(!response.ok && response.failure == AiChatFailure::Refused, "Gemini's image safety stop is a refusal.");
	response = parseAiImageResponse(AiImageApi::GeminiImages, 200, R"({"candidates":[{"content":{"parts":[{"text":"I can only describe it."}]},"finishReason":"STOP"}]})");
	expect(!response.ok && response.failure == AiChatFailure::BadResponse && response.errorMessage.contains(QStringLiteral("only describe")),
		"A Gemini answer of words alone says what it said.");

	response = parseAiImageResponse(AiImageApi::StableDiffusionWebUi, 200,
		QStringLiteral(R"({"images":["%1","data:image/png;base64,%1"],"parameters":{},"info":"{\"seed\": 7, \"all_seeds\": [7, 8], \"sd_model_name\": \"dreamshaper_8\"}"})")
			.arg(encoded)
			.toUtf8());
	expect(response.ok && response.images.size() == 2 && response.images.at(1).bytes == png && response.images.at(0).seed == 7 && response.images.at(1).seed == 8
			&& response.model == QStringLiteral("dreamshaper_8"),
		"A web UI answer gives each picture with its seed, data-URL prefixes removed.");
	response = parseAiImageResponse(AiImageApi::StableDiffusionWebUi, 422, R"({"detail":[{"loc":["body","width"],"msg":"value is not a valid integer"}]})");
	expect(!response.ok && response.failure == AiChatFailure::Provider && response.errorMessage.contains(QStringLiteral("not a valid integer")),
		"The web UI's validation errors are read.");
	response = parseAiImageResponse(AiImageApi::StableDiffusionWebUi, 500, R"({"error":"OutOfMemoryError","detail":"CUDA out of memory"})");
	expect(response.errorMessage.contains(QStringLiteral("OutOfMemoryError")) && response.errorMessage.contains(QStringLiteral("CUDA")), "The web UI's exceptions are read.");
}

void checkConnections()
{
	AiAutomationPreferences preferences;
	expect(resolveAiImageConnection(preferences).block == AiImageConnectionBlock::AiFreeMode, "AI-free mode stops every image connection.");
	preferences.aiFreeMode = false;
	expect(resolveAiImageConnection(preferences).block == AiImageConnectionBlock::NoConnector, "Without a chosen connector nothing is drawn.");
	expect(resolveAiImageConnection(preferences, QStringLiteral("claude")).block == AiImageConnectionBlock::NoImageTransport
			&& aiImageConnectionBlockText(resolveAiImageConnection(preferences, QStringLiteral("claude"))).contains(QStringLiteral("does not make images")),
		"Claude makes no images, and the block says so.");

	preferences.preferredImageConnectorId = QStringLiteral("openai");
	AiImageConnection connection = resolveAiImageConnection(preferences);
	expect(connection.connectorId == QStringLiteral("openai") && connection.block == AiImageConnectionBlock::CloudNotAllowed && connection.model == QStringLiteral("gpt-image-1.5"),
		"A cloud image connector waits for cloud opt-in, with its suggested model.");
	preferences.preferredLocalConnectorId = QStringLiteral("local-offline");
	connection = resolveAiImageConnection(preferences);
	expect(connection.connectorId == QStringLiteral("local-offline") && connection.ready() && connection.local && connection.api == AiImageApi::StableDiffusionWebUi
			&& connection.credentialVariable.isEmpty(),
		"With cloud off, the local web UI draws instead, with no model or key needed.");
	expect(aiImageConnectionBlockText(connection).contains(QStringLiteral("loaded checkpoint")), "The ready text says the web UI uses its loaded checkpoint.");

	preferences.cloudConnectorsEnabled = true;
	qputenv("OPENAI_API_KEY", QByteArray());
	connection = resolveAiImageConnection(preferences);
	expect(connection.connectorId == QStringLiteral("openai") && connection.block == AiImageConnectionBlock::NoCredential && connection.credentialVariable == QStringLiteral("OPENAI_API_KEY"),
		"With cloud on, the image connector answers, and names its missing key.");
	qputenv("OPENAI_API_KEY", "sk-test-image-key-1234567890");
	connection = resolveAiImageConnection(preferences);
	expect(connection.ready() && aiImageConnectionApiKey(connection) == QStringLiteral("sk-test-image-key-1234567890"), "With a key, OpenAI is ready to draw.");
	qunsetenv("OPENAI_API_KEY");

	preferences.connectorImageModels.insert(QStringLiteral("openai"), QStringLiteral("gpt-image-test"));
	preferences.connectorImageEndpoints.insert(QStringLiteral("local-offline"), QStringLiteral("http://127.0.0.1:8080/v1"));
	expect(resolveAiImageConnection(preferences).model == QStringLiteral("gpt-image-test"), "The image model set for a connector is used.");
	connection = resolveAiImageConnection(preferences, QStringLiteral("local-offline"));
	expect(connection.api == AiImageApi::OpenAiImages && connection.block == AiImageConnectionBlock::NoModel,
		"A local OpenAI-compatible image server needs a model named.");
	expect(resolveAiImageConnection(preferences, QStringLiteral("gemini")).block == AiImageConnectionBlock::NoModel, "Gemini needs its image model named.");
	const AiAutomationPreferences normalized = normalizedAiAutomationPreferences(preferences);
	expect(normalized.connectorImageModels.value(QStringLiteral("openai")) == QStringLiteral("gpt-image-test")
			&& normalized.connectorImageEndpoints.value(QStringLiteral("local-offline")) == QStringLiteral("http://127.0.0.1:8080/v1"),
		"Per-connector image settings survive normalization.");
}

void checkClient()
{
	FakeAiProvider provider;
	expect(provider.listening(), "The fake provider should listen on 127.0.0.1.");
	const QByteArray png = pngBytes(Qt::green);
	const QString encoded = QString::fromLatin1(png.toBase64());

	// The local web UI draws a batch.
	AiImageClient client;
	std::optional<AiImageResponse> answer;
	provider.answer(200, QStringLiteral(R"({"images":["%1","%1"],"info":"{\"all_seeds\":[1,2]}"})").arg(encoded).toUtf8());
	AiImageRequest local = texture(QStringLiteral("local-offline"), QString(), provider.baseUrl());
	local.count = 2;
	QString error;
	expect(client.send(local, QString(), [&answer](const AiImageResponse& response) { answer = response; }, &error) && client.busy(), "A web UI request should start.");
	expect(waitUntil([&answer]() { return answer.has_value(); }), "The web UI should answer.");
	expect(answer && answer->ok && answer->images.size() == 2 && answer->images.first().bytes == png && !client.busy(), "Both pictures come back.");
	expect(provider.exchanges().size() == 1 && provider.exchanges().first().path == "/sdapi/v1/txt2img" && !provider.exchanges().first().headers.contains("authorization"),
		"The web UI is asked once, at txt2img, without a key.");

	// Gemini draws one per call, so two pictures take two calls.
	provider.clear();
	answer.reset();
	const QByteArray geminiAnswer = QStringLiteral(R"({"candidates":[{"content":{"parts":[{"inlineData":{"mimeType":"image/png","data":"%1"}}]},"finishReason":"STOP"}]})").arg(encoded).toUtf8();
	provider.answerInTurn(200, geminiAnswer);
	provider.answerInTurn(200, geminiAnswer);
	AiImageRequest gemini = texture(QStringLiteral("gemini"), QStringLiteral("gemini-image-test"), provider.baseUrl());
	gemini.count = 2;
	QVector<int> progress;
	client.send(gemini, QStringLiteral("AIzaTestKey"), [&answer](const AiImageResponse& response) { answer = response; }, nullptr,
		[&progress](int done, int total) { progress << done * 10 + total; });
	expect(waitUntil([&answer]() { return answer.has_value(); }), "Gemini should answer twice.");
	expect(answer && answer->ok && answer->images.size() == 2 && provider.exchanges().size() == 2 && progress == QVector<int> {12, 22},
		"Two Gemini pictures take two calls, with progress after each.");

	// A failure on the second call keeps the first picture and says why.
	provider.clear();
	answer.reset();
	provider.answerInTurn(200, geminiAnswer);
	provider.answerInTurn(500, R"({"error":{"message":"Internal error encountered."}})");
	client.send(gemini, QStringLiteral("AIzaTestKey"), [&answer](const AiImageResponse& response) { answer = response; });
	expect(waitUntil([&answer]() { return answer.has_value(); }), "The failing call should end the request.");
	expect(answer && !answer->ok && answer->failure == AiChatFailure::Provider && answer->images.size() == 1 && answer->errorMessage.contains(QStringLiteral("Internal error")),
		"A failure part-way keeps the pictures already in.");

	// A picture answered as a link is fetched without the key.
	provider.clear();
	answer.reset();
	provider.answerInTurn(200, QStringLiteral(R"({"data":[{"url":"%1"}]})").arg(provider.baseUrl(QStringLiteral("/files/plate.png"))).toUtf8());
	provider.answerInTurn(200, png, QByteArrayLiteral("image/png"));
	client.send(texture(QStringLiteral("custom-http"), QStringLiteral("flux"), provider.baseUrl(QStringLiteral("/v1"))), QStringLiteral("team-secret-value-0123456789"),
		[&answer](const AiImageResponse& response) { answer = response; });
	expect(waitUntil([&answer]() { return answer.has_value(); }), "The linked picture should be fetched.");
	expect(answer && answer->ok && answer->images.size() == 1 && answer->images.first().bytes == png && answer->images.first().sourceUrl.endsWith(QStringLiteral("/files/plate.png")),
		"The linked picture comes back as bytes, with where it came from.");
	expect(provider.exchanges().size() == 2 && provider.exchanges().at(0).headers.value("authorization") == "Bearer team-secret-value-0123456789"
			&& provider.exchanges().at(1).method == "GET" && !provider.exchanges().at(1).headers.contains("authorization"),
		"The key goes to the API, never to the picture's link.");

	// Cancel.
	answer.reset();
	provider.hang();
	const qsizetype asked = provider.exchanges().size();
	client.send(local, QString(), [&answer](const AiImageResponse& response) { answer = response; });
	expect(waitUntil([&provider, asked]() { return provider.exchanges().size() > asked; }, 3000), "The provider should receive the request to cancel.");
	client.cancel();
	expect(waitUntil([&answer]() { return answer.has_value(); }, 3000) && answer->failure == AiChatFailure::Cancelled && !client.busy(), "Cancel ends the request and says so.");

	// Timeout across calls.
	answer.reset();
	client.setTimeoutMsecs(1000);
	client.send(local, QString(), [&answer](const AiImageResponse& response) { answer = response; });
	expect(waitUntil([&answer]() { return answer.has_value(); }, 5000) && answer->failure == AiChatFailure::Timeout, "A web UI that never answers times out.");

	// A client that goes away mid-request never calls back.
	bool calledBack = false;
	{
		AiImageClient doomed;
		doomed.send(local, QString(), [&calledBack](const AiImageResponse&) { calledBack = true; });
		waitUntil([]() { return false; }, 200);
	}
	waitUntil([]() { return false; }, 200);
	expect(!calledBack, "A destroyed client never calls back.");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	checkApis();
	checkSizes();
	checkRequests();
	checkAnswers();
	checkConnections();
	checkClient();
	if (failures > 0) {
		std::cerr << failures << " AI image transport check(s) failed.\n";
		return EXIT_FAILURE;
	}
	std::cout << "AI image transport checks passed.\n";
	return EXIT_SUCCESS;
}
