#include "core/ai_gguf_runtime.h"

#include "vibestudio_config.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <chrono>
#include <thread>

#if VIBESTUDIO_HAVE_GGML
#include <llama-cpp.h>
#include <llama.h>

#include <array>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#endif

namespace vibestudio {

QString aiGgufModelProblem(const QString& path)
{
	const QFileInfo info(path.trimmed());
	if (path.trimmed().isEmpty()) {
		return QCoreApplication::translate("VibeStudioAiGguf", "No GGUF model file is set. Name a .gguf file as the model in Settings > AI and Automation.");
	}
	if (!info.isFile()) {
		return QCoreApplication::translate("VibeStudioAiGguf", "The GGUF model %1 does not exist.").arg(QDir::toNativeSeparators(path));
	}
	if (info.suffix().compare(QStringLiteral("gguf"), Qt::CaseInsensitive) != 0) {
		return QCoreApplication::translate("VibeStudioAiGguf", "%1 is not a .gguf model file.").arg(QDir::toNativeSeparators(path));
	}
	return {};
}

QString aiGgufRuntimeMissingText()
{
	return QCoreApplication::translate("VibeStudioAiGguf",
		"This build of VibeStudio has no ggml runtime. Build llama.cpp with python scripts/build_ggml.py and configure with -Dggml=enabled, or run llama.cpp's server and use the Local/Offline Runtime connector.");
}

#if VIBESTUDIO_HAVE_GGML

// Derived from Quake-MapGen's src/llama_text_model.cpp (MIT): backend start-up, batches, chat
// templates, tokenizing, token pieces, and grammar-constrained greedy decoding. VibeStudio adds
// conversations, free text with a repetition penalty, cancellation, and a cached model.
namespace {

// Raised while a run is being stopped: llama.cpp then reports the aborted decode as an error.
// Runs decode one at a time (the model lock), so one flag serves.
std::atomic_bool stopping {false};

// llama.cpp reports every load and context step; only its errors are worth a line on stderr.
void quietLog(ggml_log_level level, const char* text, void*)
{
	if (level == GGML_LOG_LEVEL_ERROR && text && !stopping.load()) {
		std::fputs(text, stderr);
	}
}

class BackendRuntime {
public:
	BackendRuntime()
	{
		llama_log_set(quietLog, nullptr);
		llama_backend_init();
	}
	~BackendRuntime() { llama_backend_free(); }
	BackendRuntime(const BackendRuntime&) = delete;
	BackendRuntime& operator=(const BackendRuntime&) = delete;
};

void ensureBackend()
{
	static BackendRuntime runtime;
	static_cast<void>(runtime);
}

class Batch {
public:
	explicit Batch(int32_t capacity)
		: m_value(llama_batch_init(capacity, 0, 1))
	{
		if (!m_value.token || !m_value.pos || !m_value.n_seq_id || !m_value.seq_id || !m_value.logits) {
			llama_batch_free(m_value);
			m_value = {};
			throw std::runtime_error("llama.cpp could not allocate an inference batch");
		}
	}
	~Batch() { llama_batch_free(m_value); }
	Batch(const Batch&) = delete;
	Batch& operator=(const Batch&) = delete;
	llama_batch& get() { return m_value; }

private:
	llama_batch m_value {};
};

void setBatchItem(llama_batch& batch, int32_t index, llama_token token, llama_pos position, bool logits)
{
	batch.token[index] = token;
	batch.pos[index] = position;
	batch.n_seq_id[index] = 1;
	batch.seq_id[index][0] = 0;
	batch.logits[index] = logits ? 1 : 0;
}

std::vector<llama_token> tokenize(const llama_vocab* vocab, const std::string& prompt)
{
	if (prompt.size() > size_t(std::numeric_limits<int32_t>::max())) {
		throw std::invalid_argument("the prompt is too large for llama.cpp");
	}
	const int32_t required = llama_tokenize(vocab, prompt.data(), int32_t(prompt.size()), nullptr, 0, true, true);
	if (required == INT32_MIN || required >= 0) {
		throw std::runtime_error("llama.cpp could not size the tokenized prompt");
	}
	std::vector<llama_token> tokens(size_t(-required));
	if (llama_tokenize(vocab, prompt.data(), int32_t(prompt.size()), tokens.data(), -required, true, true) != -required) {
		throw std::runtime_error("llama.cpp could not tokenize the prompt");
	}
	return tokens;
}

std::string tokenPiece(const llama_vocab* vocab, llama_token token)
{
	std::array<char, 128> local {};
	int32_t length = llama_token_to_piece(vocab, token, local.data(), int32_t(local.size()), 0, true);
	if (length >= 0) {
		return std::string(local.data(), size_t(std::min<int32_t>(length, int32_t(local.size()))));
	}
	std::vector<char> expanded(size_t(-length));
	length = llama_token_to_piece(vocab, token, expanded.data(), int32_t(expanded.size()), 0, true);
	if (length < 0) {
		throw std::runtime_error("llama.cpp could not turn an output token into text");
	}
	return std::string(expanded.data(), size_t(length));
}

struct LoadedModel {
	QString path;
	qint64 size = -1;
	QDateTime modified;
	QString sha256;
	llama_model_ptr model;
	const llama_vocab* vocab = nullptr;
	const char* chatTemplate = nullptr;
	// The text of the model's control tokens (<|im_start|> and its kin), which message text may
	// not carry into the prompt.
	std::vector<std::string> controlTexts;
};

// Thrown when the run's stop flag is raised mid-step.
struct Cancelled {
};

bool stopRequested(const std::atomic_bool* cancelled)
{
	return cancelled && cancelled->load();
}

std::mutex& modelMutex()
{
	static std::mutex mutex;
	return mutex;
}

LoadedModel& cachedModel()
{
	static LoadedModel model;
	return model;
}

// The SHA-256 of a model file, kept by path, size, and time, so a model is read through once and
// a run waiting for the model lock is not held up by another file's hashing.
QString fileSha256(const QString& path, const std::atomic_bool* cancelled)
{
	static std::mutex mutex;
	static std::map<std::tuple<QString, qint64, qint64>, QString> known;
	const QFileInfo info(path);
	const auto key = std::make_tuple(info.absoluteFilePath(), info.size(), info.lastModified().toMSecsSinceEpoch());
	{
		const std::lock_guard<std::mutex> lock(mutex);
		if (const auto found = known.find(key); found != known.end()) {
			return found->second;
		}
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	QCryptographicHash hash(QCryptographicHash::Sha256);
	QByteArray chunk(4 << 20, Qt::Uninitialized);
	for (qint64 read = 0; (read = file.read(chunk.data(), chunk.size())) > 0;) {
		if (stopRequested(cancelled)) {
			throw Cancelled();
		}
		hash.addData(QByteArrayView(chunk.constData(), read));
	}
	const QString digest = QString::fromLatin1(hash.result().toHex());
	const std::lock_guard<std::mutex> lock(mutex);
	known[key] = digest;
	return digest;
}

std::vector<std::string> controlTexts(const llama_vocab* vocab)
{
	std::vector<std::string> texts;
	const int32_t count = llama_vocab_n_tokens(vocab);
	for (llama_token token = 0; token < count; ++token) {
		if ((llama_vocab_get_attr(vocab, token) & LLAMA_TOKEN_ATTR_CONTROL) != 0) {
			std::string text = tokenPiece(vocab, token);
			if (text.size() >= 3) {
				texts.push_back(std::move(text));
			}
		}
	}
	return texts;
}

bool keepLoading(float, void* data)
{
	return !stopRequested(static_cast<const std::atomic_bool*>(data));
}

bool abortDecoding(void* data)
{
	const bool stop = stopRequested(static_cast<const std::atomic_bool*>(data));
	if (stop) {
		stopping.store(true);
	}
	return stop;
}

// The model at `path`, loaded once and kept until another is asked for. Called with the mutex held.
LoadedModel& loadModel(const QString& path, const QString& sha256, const std::atomic_bool* cancelled)
{
	LoadedModel& cached = cachedModel();
	const QFileInfo info(path);
	if (cached.model && cached.path == info.absoluteFilePath() && cached.size == info.size() && cached.modified == info.lastModified()) {
		return cached;
	}
	cached = LoadedModel();
	ensureBackend();
	llama_model_params parameters = llama_model_default_params();
	parameters.n_gpu_layers = 0;
	// Loading a large model takes a while; a raised stop flag ends it.
	parameters.progress_callback = keepLoading;
	parameters.progress_callback_user_data = const_cast<std::atomic_bool*>(cancelled);
	const QByteArray encoded = QDir::toNativeSeparators(info.absoluteFilePath()).toUtf8();
	llama_model_ptr model(llama_model_load_from_file(encoded.constData(), parameters));
	if (!model) {
		if (stopRequested(cancelled)) {
			throw Cancelled();
		}
		throw std::runtime_error("llama.cpp could not load the GGUF model");
	}
	if (llama_model_has_encoder(model.get()) || !llama_model_has_decoder(model.get())) {
		throw std::invalid_argument("only decoder-only GGUF language models are supported");
	}
	const char* chatTemplate = llama_model_chat_template(model.get(), nullptr);
	if (!chatTemplate) {
		throw std::invalid_argument("the GGUF model declares no chat template");
	}
	cached.path = info.absoluteFilePath();
	cached.size = info.size();
	cached.modified = info.lastModified();
	cached.sha256 = sha256;
	cached.vocab = llama_model_get_vocab(model.get());
	cached.chatTemplate = chatTemplate;
	cached.model = std::move(model);
	if (!cached.vocab) {
		cached = LoadedModel();
		throw std::runtime_error("the GGUF model has no tokenizer vocabulary");
	}
	cached.controlTexts = controlTexts(cached.vocab);
	return cached;
}

// Message text with the model's control-token text taken out: the prompt is tokenized with
// special tokens recognised, for the chat template's own, so text that spelled one out could
// otherwise open a turn of its own.
std::string withoutControlTexts(const LoadedModel& model, std::string text)
{
	for (bool changed = true; changed;) {
		changed = false;
		for (const std::string& control : model.controlTexts) {
			for (size_t at = text.find(control); at != std::string::npos; at = text.find(control, at)) {
				text.erase(at, control.size());
				changed = true;
			}
		}
	}
	return text;
}

std::string chatPrompt(const LoadedModel& model, const AiGgufRequest& request)
{
	std::vector<std::string> roles;
	std::vector<std::string> texts;
	if (!request.system.isEmpty()) {
		roles.push_back("system");
		texts.push_back(withoutControlTexts(model, request.system.toStdString()));
	}
	for (const AiChatMessage& message : request.messages) {
		roles.push_back(message.role == QStringLiteral("assistant") ? "assistant" : "user");
		texts.push_back(withoutControlTexts(model, message.text.toStdString()));
	}
	std::vector<llama_chat_message> messages;
	size_t total = 0;
	for (size_t index = 0; index < roles.size(); ++index) {
		messages.push_back({roles[index].c_str(), texts[index].c_str()});
		total += texts[index].size();
	}
	std::vector<char> buffer(std::max<size_t>(1024, total * 2 + 256));
	int32_t length = llama_chat_apply_template(model.chatTemplate, messages.data(), messages.size(), true, buffer.data(), int32_t(buffer.size()));
	if (length < 0) {
		throw std::runtime_error("this llama.cpp revision does not support the model's chat template");
	}
	if (size_t(length) > buffer.size()) {
		buffer.resize(size_t(length));
		length = llama_chat_apply_template(model.chatTemplate, messages.data(), messages.size(), true, buffer.data(), int32_t(buffer.size()));
	}
	return std::string(buffer.data(), size_t(std::max<int32_t>(0, length)));
}

} // namespace

bool aiGgufRuntimeAvailable()
{
	return true;
}

QString aiGgufRuntimeVersion()
{
	return QStringLiteral("llama.cpp %1, CPU").arg(QStringLiteral(VIBESTUDIO_GGML_REVISION).left(7));
}

AiGgufResult runAiGguf(const AiGgufRequest& request, const std::atomic_bool* cancelled)
{
	AiGgufResult result;
	if (const QString problem = aiGgufModelProblem(request.modelPath); !problem.isEmpty()) {
		result.error = problem;
		return result;
	}
	try {
		// The file's hash first, outside the model lock; then the lock, waited for while the
		// stop flag stays down, so a queued run can still be cancelled.
		const QString sha256 = fileSha256(request.modelPath, cancelled);
		std::unique_lock<std::mutex> lock(modelMutex(), std::defer_lock);
		while (!lock.try_lock()) {
			if (stopRequested(cancelled)) {
				throw Cancelled();
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		stopping.store(false);
		LoadedModel& model = loadModel(request.modelPath, sha256, cancelled);
		result.modelName = QFileInfo(model.path).fileName();
		result.modelSha256 = model.sha256;
		const std::vector<llama_token> prompt = tokenize(model.vocab, chatPrompt(model, request));
		// The window is fixed per model, whatever this prompt's length; only what it needs is
		// allocated.
		const int trained = llama_model_n_ctx_train(model.model.get());
		const int ceiling = trained > 0 ? std::min(trained, 32768) : 8192;
		const int context = request.contextTokens > 0 ? std::min(std::max(request.contextTokens, 512), ceiling) : ceiling;
		if (int(prompt.size()) + 1 > context) {
			result.error = QCoreApplication::translate("VibeStudioAiGguf", "The request is %1 tokens; the model's context holds %2.").arg(prompt.size()).arg(context);
			return result;
		}
		const int maxOutput = std::clamp(request.maxOutputTokens, 1, context - int(prompt.size()));
		const int threads = request.threads > 0 ? request.threads : int(std::clamp<unsigned>(std::thread::hardware_concurrency(), 1u, 8u));
		llama_context_params parameters = llama_context_default_params();
		parameters.n_ctx = uint32_t(std::min(context, int(prompt.size()) + maxOutput + 1));
		parameters.n_batch = uint32_t(std::min<size_t>(prompt.size(), 2048));
		parameters.n_ubatch = uint32_t(std::min<size_t>(prompt.size(), 512));
		parameters.n_seq_max = 1;
		parameters.n_threads = threads;
		parameters.n_threads_batch = threads;
		parameters.no_perf = true;
		parameters.abort_callback = abortDecoding;
		parameters.abort_callback_data = const_cast<std::atomic_bool*>(cancelled);
		llama_context_ptr inference(llama_init_from_model(model.model.get(), parameters));
		if (!inference) {
			throw std::runtime_error("llama.cpp could not create an inference context");
		}
		llama_sampler_chain_params chainParameters = llama_sampler_chain_default_params();
		chainParameters.no_perf = true;
		llama_sampler_ptr sampler(llama_sampler_chain_init(chainParameters));
		if (!request.grammar.isEmpty()) {
			const QByteArray grammar = request.grammar.toUtf8();
			llama_sampler* constrained = llama_sampler_init_grammar(model.vocab, grammar.constData(), "root");
			if (!constrained) {
				throw std::invalid_argument("llama.cpp rejected the request's grammar");
			}
			llama_sampler_chain_add(sampler.get(), constrained);
			result.sampling = QStringLiteral("gbnf+greedy");
		} else {
			llama_sampler_chain_add(sampler.get(), llama_sampler_init_penalties(llama_vocab_n_tokens(model.vocab), 64, 1.1f, 0.0f, 0.0f));
			result.sampling = QStringLiteral("penalties+greedy");
		}
		llama_sampler_chain_add(sampler.get(), llama_sampler_init_greedy());

		// The prompt, in batches.
		Batch batch(int32_t(parameters.n_batch));
		llama_batch& raw = batch.get();
		for (size_t start = 0; start < prompt.size(); start += parameters.n_batch) {
			const size_t count = std::min<size_t>(parameters.n_batch, prompt.size() - start);
			raw.n_tokens = int32_t(count);
			for (size_t index = 0; index < count; ++index) {
				setBatchItem(raw, int32_t(index), prompt[start + index], llama_pos(start + index), start + index + 1 == prompt.size());
			}
			if (llama_decode(inference.get(), raw) != 0) {
				if (stopRequested(cancelled)) {
					throw Cancelled();
				}
				throw std::runtime_error("llama.cpp could not decode the prompt");
			}
		}
		result.promptTokens = int(prompt.size());
		result.threads = threads;
		result.contextTokens = context;
		std::string text;
		// Up to maxOutput tokens, and one more sample to tell a finished answer from a cut one.
		for (int index = 0;; ++index) {
			if (stopRequested(cancelled)) {
				throw Cancelled();
			}
			const llama_token token = llama_sampler_sample(sampler.get(), inference.get(), -1);
			if (token == LLAMA_TOKEN_NULL) {
				throw std::runtime_error("llama.cpp returned no output token");
			}
			if (llama_vocab_is_eog(model.vocab, token)) {
				break;
			}
			if (index == maxOutput) {
				result.truncated = true;
				break;
			}
			text += tokenPiece(model.vocab, token);
			++result.outputTokens;
			raw.n_tokens = 1;
			setBatchItem(raw, 0, token, llama_pos(prompt.size() + size_t(index)), true);
			if (llama_decode(inference.get(), raw) != 0) {
				if (stopRequested(cancelled)) {
					throw Cancelled();
				}
				throw std::runtime_error("llama.cpp could not decode the output");
			}
		}
		result.text = QString::fromUtf8(text.data(), qsizetype(text.size()));
		result.ok = true;
	} catch (const Cancelled&) {
		result.cancelled = true;
	} catch (const std::exception& error) {
		result.error = QCoreApplication::translate("VibeStudioAiGguf", "The GGUF model could not answer: %1").arg(QString::fromUtf8(error.what()));
	}
	return result;
}

#else

bool aiGgufRuntimeAvailable()
{
	return false;
}

QString aiGgufRuntimeVersion()
{
	return {};
}

AiGgufResult runAiGguf(const AiGgufRequest&, const std::atomic_bool*)
{
	AiGgufResult result;
	result.error = aiGgufRuntimeMissingText();
	return result;
}

#endif

} // namespace vibestudio
