#include "core/ai_image_transport.h"

#include "core/ai_connectors.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace vibestudio {

namespace {

QString trimmedBase(const QString& endpoint)
{
	QString base = endpoint.trimmed();
	while (base.endsWith(QLatin1Char('/'))) {
		base.chop(1);
	}
	return base;
}

bool openAiHost(const QString& endpoint)
{
	return QUrl(endpoint).host() == QStringLiteral("api.openai.com");
}

bool gptImageModel(const QString& model)
{
	return model.trimmed().startsWith(QStringLiteral("gpt-image"));
}

QString jsonErrorMessage(const QJsonObject& root)
{
	const QJsonValue error = root.value(QStringLiteral("error"));
	if (error.isObject()) {
		const QString message = error.toObject().value(QStringLiteral("message")).toString();
		return message.isEmpty() ? error.toObject().value(QStringLiteral("type")).toString() : message;
	}
	if (error.isString()) {
		// The web UI puts its exception's name here and the words in detail.
		const QString detail = root.value(QStringLiteral("detail")).toString();
		return detail.isEmpty() ? error.toString() : QStringLiteral("%1: %2").arg(error.toString(), detail);
	}
	// FastAPI's validation errors: {"detail": [{"loc": [...], "msg": "..."}]}.
	const QJsonValue detail = root.value(QStringLiteral("detail"));
	if (detail.isArray()) {
		QStringList messages;
		for (const QJsonValue& entry : detail.toArray()) {
			messages << entry.toObject().value(QStringLiteral("msg")).toString();
		}
		messages.removeAll(QString());
		return messages.join(QStringLiteral("; "));
	}
	if (detail.isString()) {
		return detail.toString();
	}
	return root.value(QStringLiteral("message")).toString();
}

AiChatFailure failureForStatus(int status)
{
	if (status == 401 || status == 403) {
		return AiChatFailure::Credential;
	}
	if (status == 429 || status == 529 || status == 503) {
		return AiChatFailure::RateLimited;
	}
	return AiChatFailure::Provider;
}

// The aspect ratio Gemini offers nearest the shape wanted.
QString geminiAspectRatio(QSize wanted)
{
	static const QVector<QPair<QString, double>> ratios = {
		{QStringLiteral("1:1"), 1.0},
		{QStringLiteral("4:3"), 4.0 / 3.0},
		{QStringLiteral("3:4"), 3.0 / 4.0},
		{QStringLiteral("3:2"), 3.0 / 2.0},
		{QStringLiteral("2:3"), 2.0 / 3.0},
		{QStringLiteral("16:9"), 16.0 / 9.0},
		{QStringLiteral("9:16"), 9.0 / 16.0},
		{QStringLiteral("21:9"), 21.0 / 9.0},
	};
	const double aspect = wanted.height() > 0 ? double(wanted.width()) / double(wanted.height()) : 1.0;
	QString best = ratios.first().first;
	double bestDistance = 1e9;
	for (const auto& ratio : ratios) {
		const double distance = std::abs(std::log(aspect / ratio.second));
		if (distance < bestDistance) {
			bestDistance = distance;
			best = ratio.first;
		}
	}
	return best;
}

// One multipart/form-data body, built by hand so a reviewer can be shown it
// and a test can read it back.
class MultipartBody {
public:
	explicit MultipartBody(const QByteArray& seed)
	{
		m_boundary = QByteArrayLiteral("VibeStudioBoundary") + QCryptographicHash::hash(seed, QCryptographicHash::Sha1).toHex().left(24);
	}

	void addText(const QByteArray& name, const QString& value)
	{
		m_parts.append({name, QByteArray(), QByteArray(), value.toUtf8()});
	}

	void addFile(const QByteArray& name, const QByteArray& fileName, const QByteArray& contentType, const QByteArray& bytes)
	{
		m_parts.append({name, fileName, contentType, bytes});
	}

	[[nodiscard]] QByteArray contentType() const
	{
		return QByteArrayLiteral("multipart/form-data; boundary=") + m_boundary;
	}

	[[nodiscard]] QByteArray body()
	{
		// A boundary must not occur in the data; lengthen it until it does not.
		while (std::any_of(m_parts.cbegin(), m_parts.cend(), [this](const Part& part) { return part.bytes.contains(m_boundary); })) {
			m_boundary += QByteArrayLiteral("x");
		}
		QByteArray out;
		for (const Part& part : m_parts) {
			out += QByteArrayLiteral("--") + m_boundary + QByteArrayLiteral("\r\n");
			out += QByteArrayLiteral("Content-Disposition: form-data; name=\"") + part.name + '"';
			if (!part.fileName.isEmpty()) {
				out += QByteArrayLiteral("; filename=\"") + part.fileName + '"';
			}
			out += QByteArrayLiteral("\r\n");
			if (!part.contentType.isEmpty()) {
				out += QByteArrayLiteral("Content-Type: ") + part.contentType + QByteArrayLiteral("\r\n");
			}
			out += QByteArrayLiteral("\r\n") + part.bytes + QByteArrayLiteral("\r\n");
		}
		out += QByteArrayLiteral("--") + m_boundary + QByteArrayLiteral("--\r\n");
		return out;
	}

private:
	struct Part {
		QByteArray name;
		QByteArray fileName;
		QByteArray contentType;
		QByteArray bytes;
	};
	QByteArray m_boundary;
	QVector<Part> m_parts;
};

void parseOpenAiImages(const QJsonObject& root, AiImageResponse* response)
{
	const QString format = root.value(QStringLiteral("output_format")).toString();
	for (const QJsonValue& value : root.value(QStringLiteral("data")).toArray()) {
		const QJsonObject entry = value.toObject();
		AiGeneratedImage image;
		image.revisedPrompt = entry.value(QStringLiteral("revised_prompt")).toString();
		const QString encoded = entry.value(QStringLiteral("b64_json")).toString();
		if (!encoded.isEmpty()) {
			image.bytes = QByteArray::fromBase64(encoded.toLatin1());
			image.mimeType = aiImageMimeType(image.bytes);
			if (image.mimeType.isEmpty() && !format.isEmpty()) {
				image.mimeType = QStringLiteral("image/%1").arg(format == QStringLiteral("jpg") ? QStringLiteral("jpeg") : format);
			}
			response->images.push_back(image);
		} else if (const QString url = entry.value(QStringLiteral("url")).toString(); !url.isEmpty()) {
			response->pendingUrls << url;
		}
	}
	const QJsonObject usage = root.value(QStringLiteral("usage")).toObject();
	response->inputTokens = usage.value(QStringLiteral("input_tokens")).toInt(-1);
	response->outputTokens = usage.value(QStringLiteral("output_tokens")).toInt(-1);
	response->model = root.value(QStringLiteral("model")).toString();
}

void parseGeminiImages(const QJsonObject& root, AiImageResponse* response)
{
	response->model = root.value(QStringLiteral("modelVersion")).toString();
	const QJsonObject usage = root.value(QStringLiteral("usageMetadata")).toObject();
	response->inputTokens = usage.value(QStringLiteral("promptTokenCount")).toInt(-1);
	response->outputTokens = usage.value(QStringLiteral("candidatesTokenCount")).toInt(-1);
	const QJsonArray candidates = root.value(QStringLiteral("candidates")).toArray();
	if (candidates.isEmpty()) {
		const QString blocked = root.value(QStringLiteral("promptFeedback")).toObject().value(QStringLiteral("blockReason")).toString();
		response->failure = blocked.isEmpty() ? AiChatFailure::BadResponse : AiChatFailure::Refused;
		response->errorMessage = blocked.isEmpty() ? QCoreApplication::translate("VibeStudioAiImageTransport", "The provider's answer had no candidates in it.")
												   : QCoreApplication::translate("VibeStudioAiImageTransport", "The provider blocked the prompt (%1).").arg(blocked);
		return;
	}
	const QJsonObject candidate = candidates.first().toObject();
	QStringList words;
	QVector<AiGeneratedImage> images;
	for (const QJsonValue& value : candidate.value(QStringLiteral("content")).toObject().value(QStringLiteral("parts")).toArray()) {
		const QJsonObject part = value.toObject();
		// Thought parts are the model's working, interim images included.
		if (part.value(QStringLiteral("thought")).toBool()) {
			continue;
		}
		const QJsonObject inlineData = part.contains(QStringLiteral("inlineData")) ? part.value(QStringLiteral("inlineData")).toObject()
																				   : part.value(QStringLiteral("inline_data")).toObject();
		if (!inlineData.isEmpty()) {
			AiGeneratedImage image;
			image.bytes = QByteArray::fromBase64(inlineData.value(QStringLiteral("data")).toString().toLatin1());
			image.mimeType = inlineData.contains(QStringLiteral("mimeType")) ? inlineData.value(QStringLiteral("mimeType")).toString()
																			   : inlineData.value(QStringLiteral("mime_type")).toString();
			if (!image.bytes.isEmpty()) {
				images.push_back(image);
			}
		} else if (part.contains(QStringLiteral("text"))) {
			words << part.value(QStringLiteral("text")).toString();
		}
	}
	// The last picture is the answer; earlier ones in the same turn are drafts.
	if (!images.isEmpty()) {
		response->images.push_back(images.last());
	}
	response->text = words.join(QString()).trimmed();
	const QString finish = candidate.value(QStringLiteral("finishReason")).toString();
	static const QStringList refusals = {
		QStringLiteral("SAFETY"),
		QStringLiteral("RECITATION"),
		QStringLiteral("BLOCKLIST"),
		QStringLiteral("PROHIBITED_CONTENT"),
		QStringLiteral("SPII"),
		QStringLiteral("IMAGE_SAFETY"),
		QStringLiteral("IMAGE_PROHIBITED_CONTENT"),
	};
	if (refusals.contains(finish) && response->images.isEmpty()) {
		response->failure = AiChatFailure::Refused;
		response->errorMessage = QCoreApplication::translate("VibeStudioAiImageTransport", "The provider stopped the picture (%1).").arg(finish);
	}
}

void parseStableDiffusionImages(const QJsonObject& root, AiImageResponse* response)
{
	// "info" is itself JSON, as a string: the seeds used are in it.
	const QJsonObject info = QJsonDocument::fromJson(root.value(QStringLiteral("info")).toString().toUtf8()).object();
	const QJsonArray seeds = info.value(QStringLiteral("all_seeds")).toArray();
	const QJsonArray images = root.value(QStringLiteral("images")).toArray();
	for (qsizetype index = 0; index < images.size(); ++index) {
		AiGeneratedImage image;
		QString encoded = images.at(index).toString();
		// Some forks prefix a data URL.
		if (encoded.startsWith(QStringLiteral("data:"))) {
			encoded = encoded.mid(encoded.indexOf(QLatin1Char(',')) + 1);
		}
		image.bytes = QByteArray::fromBase64(encoded.toLatin1());
		image.mimeType = aiImageMimeType(image.bytes);
		image.seed = index < seeds.size() ? static_cast<qint64>(seeds.at(index).toDouble(-1)) : static_cast<qint64>(info.value(QStringLiteral("seed")).toDouble(-1));
		if (!image.bytes.isEmpty()) {
			response->images.push_back(image);
		}
	}
	response->model = info.value(QStringLiteral("sd_model_name")).toString();
}

} // namespace

AiImageApi aiImageApiFor(const QString& connectorId, const QString& endpoint)
{
	const QString id = normalizedAiId(connectorId);
	if (id == QStringLiteral("openai")) {
		return AiImageApi::OpenAiImages;
	}
	if (id == QStringLiteral("gemini")) {
		return AiImageApi::GeminiImages;
	}
	const QString path = QUrl(trimmedBase(endpoint)).path();
	if (id == QStringLiteral("local-offline")) {
		if (path.contains(QStringLiteral("/sdapi"))) {
			return AiImageApi::StableDiffusionWebUi;
		}
		return path.endsWith(QStringLiteral("/v1")) || path.contains(QStringLiteral("/v1/")) ? AiImageApi::OpenAiImages : AiImageApi::StableDiffusionWebUi;
	}
	if (id == QStringLiteral("custom-http")) {
		return path.contains(QStringLiteral("/sdapi")) ? AiImageApi::StableDiffusionWebUi : AiImageApi::OpenAiImages;
	}
	return AiImageApi::None;
}

QString aiImageApiId(AiImageApi api)
{
	switch (api) {
	case AiImageApi::None:
		return QStringLiteral("none");
	case AiImageApi::OpenAiImages:
		return QStringLiteral("openai-images");
	case AiImageApi::GeminiImages:
		return QStringLiteral("gemini-images");
	case AiImageApi::StableDiffusionWebUi:
		return QStringLiteral("sd-webui");
	}
	return QStringLiteral("none");
}

bool aiConnectorHasImageTransport(const QString& connectorId)
{
	const QString id = normalizedAiId(connectorId);
	return id == QStringLiteral("openai") || id == QStringLiteral("gemini") || id == QStringLiteral("local-offline") || id == QStringLiteral("custom-http");
}

QString aiDefaultImageEndpoint(const QString& connectorId)
{
	const QString id = normalizedAiId(connectorId);
	if (id == QStringLiteral("openai")) {
		return QStringLiteral("https://api.openai.com/v1");
	}
	if (id == QStringLiteral("gemini")) {
		return QStringLiteral("https://generativelanguage.googleapis.com");
	}
	if (id == QStringLiteral("local-offline")) {
		// The Stable Diffusion web UI's own address (AUTOMATIC1111, Forge).
		return QStringLiteral("http://127.0.0.1:7860");
	}
	return {};
}

QString aiSuggestedImageModel(const QString& connectorId)
{
	// OpenAI's image model line has kept one name per generation; Gemini's
	// image models are previews that change too fast to suggest one.
	if (normalizedAiId(connectorId) == QStringLiteral("openai")) {
		return QStringLiteral("gpt-image-1.5");
	}
	return {};
}

QString aiEffectiveImageEndpoint(const QString& connectorId, const QString& endpoint)
{
	const QString configured = endpoint.trimmed();
	return configured.isEmpty() ? aiDefaultImageEndpoint(connectorId) : configured;
}

QSize aiImageRequestSize(AiImageApi api, const QString& endpoint, const QString& model, QSize wanted)
{
	if (!wanted.isValid() || wanted.isEmpty()) {
		wanted = QSize(1024, 1024);
	}
	const double aspect = double(wanted.width()) / double(wanted.height());
	switch (api) {
	case AiImageApi::OpenAiImages:
		if (openAiHost(endpoint)) {
			// OpenAI's own sizes: three shapes for its GPT image models,
			// squares for dall-e-2, and wide or tall shapes for dall-e-3.
			if (model.startsWith(QStringLiteral("dall-e-2"))) {
				const int side = std::max(wanted.width(), wanted.height());
				return side <= 256 ? QSize(256, 256) : side <= 512 ? QSize(512, 512) : QSize(1024, 1024);
			}
			if (model.startsWith(QStringLiteral("dall-e-3"))) {
				return aspect > 1.25 ? QSize(1792, 1024) : aspect < 0.8 ? QSize(1024, 1792) : QSize(1024, 1024);
			}
			return aspect > 1.25 ? QSize(1536, 1024) : aspect < 0.8 ? QSize(1024, 1536) : QSize(1024, 1024);
		}
		return wanted;
	case AiImageApi::GeminiImages:
		return wanted;
	case AiImageApi::StableDiffusionWebUi: {
		// Multiples of 64 between 256 and 2048 suit every checkpoint family.
		const auto snap = [](int side) { return std::clamp((side + 32) / 64 * 64, 256, 2048); };
		return QSize(snap(wanted.width()), snap(wanted.height()));
	}
	case AiImageApi::None:
		break;
	}
	return wanted;
}

QString aiImagePromptText(const AiImageRequest& request, AiImageApi api)
{
	QString prompt = request.prompt.trimmed();
	if (api != AiImageApi::StableDiffusionWebUi) {
		if (!request.negativePrompt.trimmed().isEmpty()) {
			prompt += QStringLiteral("\nAvoid: %1").arg(request.negativePrompt.trimmed());
		}
	}
	return prompt;
}

bool buildAiImageHttpRequest(const AiImageRequest& request, const QString& apiKey, AiHttpRequest* out, QString* error)
{
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!out) {
		return fail(QCoreApplication::translate("VibeStudioAiImageTransport", "No request to fill."));
	}
	const QString base = trimmedBase(aiEffectiveImageEndpoint(request.connectorId, request.endpoint));
	const AiImageApi api = aiImageApiFor(request.connectorId, base);
	if (api == AiImageApi::None) {
		return fail(QCoreApplication::translate("VibeStudioAiImageTransport", "The %1 connector does not make images.").arg(request.connectorId));
	}
	if (base.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioAiImageTransport", "No image endpoint is set for this connector."));
	}
	const QUrl baseUrl(base);
	if (!baseUrl.isValid() || (baseUrl.scheme() != QStringLiteral("http") && baseUrl.scheme() != QStringLiteral("https"))) {
		return fail(QCoreApplication::translate("VibeStudioAiImageTransport", "The endpoint must be an http:// or https:// address."));
	}
	const QString model = request.model.trimmed();
	if (model.isEmpty() && api != AiImageApi::StableDiffusionWebUi) {
		return fail(QCoreApplication::translate("VibeStudioAiImageTransport", "No image model is set for this connector."));
	}
	if (request.prompt.trimmed().isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioAiImageTransport", "There is no prompt to draw from."));
	}
	const int count = std::clamp(request.count, 1, 8);
	const QSize size = aiImageRequestSize(api, base, model, request.size);
	const bool edit = !request.sourceImage.isEmpty();
	const QString prompt = aiImagePromptText(request, api);

	AiHttpRequest http;
	if (api == AiImageApi::OpenAiImages) {
		const bool official = openAiHost(base);
		const bool gptImage = gptImageModel(model);
		const QString sizeText = QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
		if (!apiKey.isEmpty()) {
			http.headers.append({QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + apiKey.toUtf8()});
		}
		if (edit) {
			http.url = QUrl(base + QStringLiteral("/images/edits"));
			MultipartBody form(prompt.toUtf8() + request.sourceImage.left(4096));
			form.addText("model", model);
			form.addText("prompt", prompt);
			form.addText("n", QString::number(count));
			form.addText("size", sizeText);
			if (!request.quality.isEmpty()) {
				form.addText("quality", request.quality);
			}
			if (official && gptImage) {
				form.addText("output_format", QStringLiteral("png"));
				if (request.transparentBackground) {
					form.addText("background", QStringLiteral("transparent"));
				}
			} else {
				form.addText("response_format", QStringLiteral("b64_json"));
			}
			form.addFile("image", "source.png", "image/png", request.sourceImage);
			if (!request.maskImage.isEmpty()) {
				form.addFile("mask", "mask.png", "image/png", request.maskImage);
			}
			http.body = form.body();
			http.headers.append({QByteArrayLiteral("Content-Type"), form.contentType()});
		} else {
			http.url = QUrl(base + QStringLiteral("/images/generations"));
			QJsonObject body {
				{QStringLiteral("model"), model},
				{QStringLiteral("prompt"), prompt},
				{QStringLiteral("n"), count},
				{QStringLiteral("size"), sizeText},
			};
			if (!request.quality.isEmpty()) {
				body.insert(QStringLiteral("quality"), request.quality);
			}
			if (official && gptImage) {
				// GPT image models always answer in base64.
				body.insert(QStringLiteral("output_format"), QStringLiteral("png"));
				if (request.transparentBackground) {
					body.insert(QStringLiteral("background"), QStringLiteral("transparent"));
				}
			} else {
				body.insert(QStringLiteral("response_format"), QStringLiteral("b64_json"));
			}
			http.headers.append({QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")});
			http.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
		}
	} else if (api == AiImageApi::GeminiImages) {
		const QString geminiModel = model.startsWith(QStringLiteral("models/")) ? model.mid(7) : model;
		http.url = QUrl(QStringLiteral("%1/v1beta/models/%2:generateContent").arg(base, QString::fromUtf8(QUrl::toPercentEncoding(geminiModel))));
		if (!apiKey.isEmpty()) {
			http.headers.append({QByteArrayLiteral("x-goog-api-key"), apiKey.toUtf8()});
		}
		QJsonArray parts {QJsonObject {{QStringLiteral("text"), prompt}}};
		if (edit) {
			parts.append(QJsonObject {{QStringLiteral("inlineData"), QJsonObject {
				{QStringLiteral("mimeType"), QStringLiteral("image/png")},
				{QStringLiteral("data"), QString::fromLatin1(request.sourceImage.toBase64())},
			}}});
		}
		const QJsonObject body {
			{QStringLiteral("contents"), QJsonArray {QJsonObject {
				{QStringLiteral("role"), QStringLiteral("user")},
				{QStringLiteral("parts"), parts},
			}}},
			{QStringLiteral("generationConfig"), QJsonObject {
				{QStringLiteral("responseModalities"), QJsonArray {QStringLiteral("TEXT"), QStringLiteral("IMAGE")}},
				{QStringLiteral("imageConfig"), QJsonObject {{QStringLiteral("aspectRatio"), geminiAspectRatio(size)}}},
			}},
		};
		http.headers.append({QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")});
		http.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
	} else {
		// A base that already names the API's root, or the web UI's own address.
		QString root = base;
		if (root.endsWith(QStringLiteral("/sdapi/v1"))) {
			root.chop(9);
		}
		http.url = QUrl(root + (edit ? QStringLiteral("/sdapi/v1/img2img") : QStringLiteral("/sdapi/v1/txt2img")));
		if (!apiKey.isEmpty()) {
			http.headers.append({QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + apiKey.toUtf8()});
		}
		QJsonObject body {
			{QStringLiteral("prompt"), prompt},
			{QStringLiteral("negative_prompt"), request.negativePrompt.trimmed()},
			{QStringLiteral("width"), size.width()},
			{QStringLiteral("height"), size.height()},
			{QStringLiteral("batch_size"), count},
			{QStringLiteral("n_iter"), 1},
			{QStringLiteral("seed"), static_cast<double>(request.seed)},
			{QStringLiteral("tiling"), request.tileable},
		};
		if (request.steps > 0) {
			body.insert(QStringLiteral("steps"), request.steps);
		}
		if (!model.isEmpty()) {
			body.insert(QStringLiteral("override_settings"), QJsonObject {{QStringLiteral("sd_model_checkpoint"), model}});
		}
		if (edit) {
			body.insert(QStringLiteral("init_images"), QJsonArray {QString::fromLatin1(request.sourceImage.toBase64())});
			body.insert(QStringLiteral("denoising_strength"), std::clamp(request.sourceStrength, 0.0, 1.0));
			if (!request.maskImage.isEmpty()) {
				body.insert(QStringLiteral("mask"), QString::fromLatin1(request.maskImage.toBase64()));
			}
		}
		http.headers.append({QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")});
		http.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
	}
	if (!http.url.isValid()) {
		return fail(QCoreApplication::translate("VibeStudioAiImageTransport", "The endpoint does not make a valid address."));
	}
	*out = http;
	return true;
}

AiImageResponse parseAiImageResponse(AiImageApi api, int httpStatus, const QByteArray& body)
{
	AiImageResponse response;
	response.httpStatus = httpStatus;
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
	const QJsonObject root = document.isObject() ? document.object() : QJsonObject();
	if (httpStatus < 200 || httpStatus >= 300) {
		response.failure = failureForStatus(httpStatus);
		QString message = root.isEmpty() ? QString::fromUtf8(body.left(400)).trimmed() : jsonErrorMessage(root);
		if (message.isEmpty()) {
			message = QCoreApplication::translate("VibeStudioAiImageTransport", "The provider answered HTTP %1.").arg(httpStatus);
		}
		// OpenAI's moderation refusal arrives as a 400 with this code.
		if (root.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString() == QStringLiteral("moderation_blocked")) {
			response.failure = AiChatFailure::Refused;
		}
		response.errorMessage = redactAiText(message);
		return response;
	}
	if (root.isEmpty()) {
		response.failure = AiChatFailure::BadResponse;
		response.errorMessage = QCoreApplication::translate("VibeStudioAiImageTransport", "The provider's answer was not JSON: %1").arg(parseError.errorString());
		return response;
	}
	switch (api) {
	case AiImageApi::OpenAiImages:
		parseOpenAiImages(root, &response);
		break;
	case AiImageApi::GeminiImages:
		parseGeminiImages(root, &response);
		break;
	case AiImageApi::StableDiffusionWebUi:
		parseStableDiffusionImages(root, &response);
		break;
	case AiImageApi::None:
		response.failure = AiChatFailure::NotConfigured;
		response.errorMessage = QCoreApplication::translate("VibeStudioAiImageTransport", "This connector does not make images.");
		return response;
	}
	if (response.failure == AiChatFailure::None && response.images.isEmpty() && response.pendingUrls.isEmpty()) {
		response.failure = AiChatFailure::BadResponse;
		response.errorMessage = response.text.isEmpty() ? QCoreApplication::translate("VibeStudioAiImageTransport", "The provider's answer had no picture in it.")
														: QCoreApplication::translate("VibeStudioAiImageTransport", "The provider answered without a picture: %1").arg(response.text.left(300));
	}
	response.ok = response.failure == AiChatFailure::None;
	return response;
}

QString aiImageMimeType(const QByteArray& bytes)
{
	if (bytes.startsWith("\x89PNG\r\n\x1a\n")) {
		return QStringLiteral("image/png");
	}
	if (bytes.startsWith("\xFF\xD8\xFF")) {
		return QStringLiteral("image/jpeg");
	}
	if (bytes.size() >= 12 && bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WEBP") {
		return QStringLiteral("image/webp");
	}
	if (bytes.startsWith("GIF87a") || bytes.startsWith("GIF89a")) {
		return QStringLiteral("image/gif");
	}
	return {};
}

AiImageConnection resolveAiImageConnection(const AiAutomationPreferences& preferences, const QString& connectorOverride)
{
	const AiAutomationPreferences normalized = normalizedAiAutomationPreferences(preferences);
	AiImageConnection connection;
	connection.connectorId = normalizedAiId(connectorOverride);
	if (connection.connectorId.isEmpty()) {
		// The image connector, unless it would leave the machine while cloud
		// connectors are off and the local connector makes images: then that.
		const QString image = normalized.preferredImageConnectorId;
		const QString local = aiConnectorHasImageTransport(normalized.preferredLocalConnectorId) ? normalized.preferredLocalConnectorId : QString();
		const bool imageAllowed = !image.isEmpty()
			&& (normalized.cloudConnectorsEnabled || aiEndpointIsLocal(aiEffectiveImageEndpoint(image, normalized.connectorImageEndpoints.value(image))));
		connection.connectorId = imageAllowed || local.isEmpty() ? image : local;
	}
	AiConnectorDescriptor descriptor;
	const bool known = !connection.connectorId.isEmpty() && aiConnectorForId(connection.connectorId, &descriptor);
	connection.displayName = known ? descriptor.displayName : connection.connectorId;
	if (normalized.aiFreeMode) {
		connection.block = AiImageConnectionBlock::AiFreeMode;
		return connection;
	}
	if (normalized.projectAiFree) {
		connection.block = AiImageConnectionBlock::ProjectAiFree;
		return connection;
	}
	if (!known) {
		connection.block = AiImageConnectionBlock::NoConnector;
		return connection;
	}
	if (!aiConnectorHasImageTransport(connection.connectorId)) {
		connection.block = AiImageConnectionBlock::NoImageTransport;
		return connection;
	}
	connection.endpoint = aiEffectiveImageEndpoint(connection.connectorId, normalized.connectorImageEndpoints.value(connection.connectorId));
	connection.model = normalized.connectorImageModels.value(connection.connectorId, aiSuggestedImageModel(connection.connectorId));
	connection.api = aiImageApiFor(connection.connectorId, connection.endpoint);
	if (connection.endpoint.isEmpty()) {
		connection.block = AiImageConnectionBlock::NoEndpoint;
		return connection;
	}
	connection.local = aiEndpointIsLocal(connection.endpoint);
	if (!connection.local && !normalized.cloudConnectorsEnabled) {
		connection.block = AiImageConnectionBlock::CloudNotAllowed;
		return connection;
	}
	// The web UI draws with whatever checkpoint it has loaded.
	if (connection.model.isEmpty() && connection.api != AiImageApi::StableDiffusionWebUi) {
		connection.block = AiImageConnectionBlock::NoModel;
		return connection;
	}
	// As for text: a server on this machine needs no key, a cloud provider
	// does, and a custom endpoint only when the user named a variable for one.
	connection.credentialVariable = aiCredentialEnvironmentVariableForConnector(connection.connectorId, normalized);
	if (!descriptor.cloudBased || connection.local) {
		connection.credentialVariable.clear();
	}
	if (!connection.credentialVariable.isEmpty()) {
		connection.credentialFound = !qEnvironmentVariable(connection.credentialVariable.toUtf8().constData()).trimmed().isEmpty();
		if (!connection.credentialFound) {
			connection.block = AiImageConnectionBlock::NoCredential;
		}
	}
	return connection;
}

QString aiImageConnectionBlockId(AiImageConnectionBlock block)
{
	switch (block) {
	case AiImageConnectionBlock::None:
		return QStringLiteral("ready");
	case AiImageConnectionBlock::AiFreeMode:
		return QStringLiteral("ai-free-mode");
	case AiImageConnectionBlock::ProjectAiFree:
		return QStringLiteral("project-ai-free");
	case AiImageConnectionBlock::NoConnector:
		return QStringLiteral("no-connector");
	case AiImageConnectionBlock::NoImageTransport:
		return QStringLiteral("no-image-transport");
	case AiImageConnectionBlock::NoEndpoint:
		return QStringLiteral("no-endpoint");
	case AiImageConnectionBlock::CloudNotAllowed:
		return QStringLiteral("cloud-not-allowed");
	case AiImageConnectionBlock::NoModel:
		return QStringLiteral("no-model");
	case AiImageConnectionBlock::NoCredential:
		return QStringLiteral("no-credential");
	}
	return QStringLiteral("no-connector");
}

QString aiImageConnectionBlockText(const AiImageConnection& connection)
{
	switch (connection.block) {
	case AiImageConnectionBlock::None: {
		const QString model = connection.model.isEmpty() ? QCoreApplication::translate("VibeStudioAiImageTransport", "the loaded checkpoint") : connection.model;
		return connection.local
			? QCoreApplication::translate("VibeStudioAiImageTransport", "Ready: %1 draws with %2, on this machine.").arg(connection.displayName, model)
			: QCoreApplication::translate("VibeStudioAiImageTransport", "Ready: %1 draws with %2, at %3.").arg(connection.displayName, model, QUrl(connection.endpoint).host());
	}
	case AiImageConnectionBlock::AiFreeMode:
		return QCoreApplication::translate("VibeStudioAiImageTransport", "AI-free mode is on. Turn it off in Settings > AI and Automation to generate images.");
	case AiImageConnectionBlock::ProjectAiFree:
		return aiProjectAiFreeText();
	case AiImageConnectionBlock::NoConnector:
		return QCoreApplication::translate("VibeStudioAiImageTransport", "No image connector is chosen. Pick an Image or Local connector in Settings > AI and Automation.");
	case AiImageConnectionBlock::NoImageTransport:
		return QCoreApplication::translate("VibeStudioAiImageTransport", "%1 does not make images. Pick OpenAI, Gemini, a local Stable Diffusion web UI, or a custom endpoint.").arg(connection.displayName);
	case AiImageConnectionBlock::NoEndpoint:
		return QCoreApplication::translate("VibeStudioAiImageTransport", "%1 has no image endpoint. Set one in Settings > AI and Automation.").arg(connection.displayName);
	case AiImageConnectionBlock::CloudNotAllowed:
		return QCoreApplication::translate("VibeStudioAiImageTransport", "%1 sends to %2, off this machine, and cloud connectors are not allowed. Allow them in Settings > AI and Automation, or use a local Stable Diffusion web UI.")
			.arg(connection.displayName, QUrl(connection.endpoint).host());
	case AiImageConnectionBlock::NoModel:
		return QCoreApplication::translate("VibeStudioAiImageTransport", "No image model is set for %1. Name one in Settings > AI and Automation.").arg(connection.displayName);
	case AiImageConnectionBlock::NoCredential:
		return QCoreApplication::translate("VibeStudioAiImageTransport", "%1 needs an API key in the %2 environment variable, which is not set.").arg(connection.displayName, connection.credentialVariable);
	}
	return {};
}

QString aiImageConnectionApiKey(const AiImageConnection& connection)
{
	return connection.credentialVariable.isEmpty() ? QString() : qEnvironmentVariable(connection.credentialVariable.toUtf8().constData()).trimmed();
}

// ---------------------------------------------------------------------------
// AiImageClient

struct AiImageClient::State {
	QNetworkAccessManager* network = nullptr;
	std::unique_ptr<QNetworkAccessManager> ownedNetwork;
	std::unique_ptr<QTimer> deadline;
	QPointer<QNetworkReply> reply;
	QElapsedTimer clock;
	AiImageRequest request;
	AiImageApi api = AiImageApi::None;
	QStringList secrets;
	QString apiKey;
	AiImageResponse gathered;
	QStringList pendingUrls;
	Callback done;
	Progress progress;
	int wanted = 1;
	int timeoutMsecs = 5 * 60 * 1000;
	bool cancelled = false;
	bool timedOut = false;
};

AiImageClient::AiImageClient(QNetworkAccessManager* network)
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

AiImageClient::~AiImageClient()
{
	if (QNetworkReply* reply = m_state->reply) {
		QObject::disconnect(reply, nullptr, nullptr, nullptr);
		reply->abort();
		reply->deleteLater();
	}
}

bool AiImageClient::send(const AiImageRequest& request, const QString& apiKey, Callback done, QString* error, Progress progress)
{
	if (busy()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioAiImageTransport", "A request is already running.");
		}
		return false;
	}
	AiHttpRequest http;
	AiImageRequest first = request;
	const AiImageApi api = aiImageApiFor(request.connectorId, aiEffectiveImageEndpoint(request.connectorId, request.endpoint));
	// Gemini draws one picture per call; ask it once per picture.
	if (api == AiImageApi::GeminiImages) {
		first.count = 1;
	}
	if (!buildAiImageHttpRequest(first, apiKey, &http, error)) {
		return false;
	}
	m_state->request = first;
	m_state->api = api;
	m_state->apiKey = apiKey;
	m_state->secrets = apiKey.isEmpty() ? QStringList() : QStringList {apiKey};
	m_state->gathered = AiImageResponse();
	m_state->pendingUrls.clear();
	m_state->done = std::move(done);
	m_state->progress = std::move(progress);
	m_state->wanted = std::clamp(request.count, 1, 8);
	m_state->cancelled = false;
	m_state->timedOut = false;
	m_state->clock.start();
	m_state->deadline->start(m_state->timeoutMsecs);

	// One call: the next picture request, or a link to fetch.
	auto start = std::make_shared<std::function<void(const AiHttpRequest&, bool)>>();
	*start = [this, start](const AiHttpRequest& next, bool download) {
		QNetworkRequest networkRequest(next.url);
		for (const auto& header : next.headers) {
			networkRequest.setRawHeader(header.first, header.second);
		}
		networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
		QNetworkReply* reply = download ? m_state->network->get(networkRequest) : m_state->network->post(networkRequest, next.body);
		m_state->reply = reply;
		QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply, download, start]() {
			State& state = *m_state;
			const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			const QByteArray body = reply->readAll();
			reply->deleteLater();
			state.reply = nullptr;
			const auto finish = [this](AiImageResponse response) {
				State& finished = *m_state;
				finished.deadline->stop();
				response.errorMessage = redactAiText(response.errorMessage, finished.secrets);
				response.elapsedMsecs = finished.clock.elapsed();
				Callback callback = std::move(finished.done);
				finished.done = nullptr;
				finished.progress = nullptr;
				finished.secrets.clear();
				finished.apiKey.clear();
				if (callback) {
					callback(response);
				}
			};
			AiImageResponse failed = state.gathered;
			failed.ok = false;
			if (state.cancelled || state.timedOut) {
				failed.failure = state.cancelled ? AiChatFailure::Cancelled : AiChatFailure::Timeout;
				failed.errorMessage = state.cancelled ? aiChatFailureText(AiChatFailure::Cancelled)
													  : QCoreApplication::translate("VibeStudioAiImageTransport", "No picture after %1 seconds.").arg(state.timeoutMsecs / 1000);
				finish(failed);
				return;
			}
			if (status <= 0) {
				failed.failure = AiChatFailure::Network;
				failed.errorMessage = reply->errorString();
				finish(failed);
				return;
			}
			if (download) {
				if (status < 200 || status >= 300 || body.isEmpty()) {
					failed.failure = AiChatFailure::Provider;
					failed.errorMessage = QCoreApplication::translate("VibeStudioAiImageTransport", "The picture's link answered HTTP %1.").arg(status);
					finish(failed);
					return;
				}
				AiGeneratedImage image;
				image.bytes = body;
				image.mimeType = aiImageMimeType(body);
				image.sourceUrl = reply->url().toString(QUrl::RemoveUserInfo | QUrl::RemoveQuery);
				state.gathered.images.push_back(image);
			} else {
				const AiImageResponse parsed = parseAiImageResponse(state.api, status, body);
				state.gathered.httpStatus = parsed.httpStatus;
				state.gathered.model = parsed.model.isEmpty() ? state.gathered.model : parsed.model;
				if (!parsed.text.isEmpty()) {
					state.gathered.text += (state.gathered.text.isEmpty() ? QString() : QStringLiteral("\n")) + parsed.text;
				}
				if (parsed.inputTokens >= 0) {
					state.gathered.inputTokens = std::max(0, state.gathered.inputTokens) + parsed.inputTokens;
				}
				if (parsed.outputTokens >= 0) {
					state.gathered.outputTokens = std::max(0, state.gathered.outputTokens) + parsed.outputTokens;
				}
				if (!parsed.ok) {
					failed = state.gathered;
					failed.ok = false;
					failed.failure = parsed.failure;
					failed.errorMessage = parsed.errorMessage;
					finish(failed);
					return;
				}
				state.gathered.images += parsed.images;
				state.pendingUrls += parsed.pendingUrls;
			}
			if (state.progress) {
				state.progress(static_cast<int>(state.gathered.images.size()), state.wanted);
			}
			// Links to fetch come before further pictures, so a failure stops early.
			if (!state.pendingUrls.isEmpty()) {
				AiHttpRequest next;
				next.url = QUrl(state.pendingUrls.takeFirst());
				if (next.url.scheme() != QStringLiteral("https") && next.url.scheme() != QStringLiteral("http")) {
					failed = state.gathered;
					failed.failure = AiChatFailure::BadResponse;
					failed.errorMessage = QCoreApplication::translate("VibeStudioAiImageTransport", "The provider linked the picture at an address that is not http or https.");
					finish(failed);
					return;
				}
				(*start)(next, true);
				return;
			}
			if (state.api == AiImageApi::GeminiImages && state.gathered.images.size() < state.wanted) {
				AiHttpRequest next;
				QString error;
				if (!buildAiImageHttpRequest(state.request, state.apiKey, &next, &error)) {
					failed = state.gathered;
					failed.failure = AiChatFailure::NotConfigured;
					failed.errorMessage = error;
					finish(failed);
					return;
				}
				(*start)(next, false);
				return;
			}
			AiImageResponse response = state.gathered;
			response.ok = !response.images.isEmpty();
			if (!response.ok) {
				response.failure = AiChatFailure::BadResponse;
				response.errorMessage = QCoreApplication::translate("VibeStudioAiImageTransport", "The provider's answer had no picture in it.");
			}
			finish(response);
		});
	};
	(*start)(http, false);
	return true;
}

void AiImageClient::cancel()
{
	if (QNetworkReply* reply = m_state->reply) {
		m_state->cancelled = true;
		reply->abort();
	}
}

bool AiImageClient::busy() const
{
	return !m_state->reply.isNull();
}

void AiImageClient::setTimeoutMsecs(int msecs)
{
	m_state->timeoutMsecs = std::max(1000, msecs);
}

int AiImageClient::timeoutMsecs() const
{
	return m_state->timeoutMsecs;
}

} // namespace vibestudio
