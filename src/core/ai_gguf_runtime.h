#pragma once

// GGUF language models run inside VibeStudio through llama.cpp and ggml: the Local GGUF connector.
// Present only in builds configured with -Dggml=enabled (scripts/build_ggml.py builds the pinned
// llama.cpp); elsewhere aiGgufRuntimeAvailable() is false and a request says how to get it.
//
// Runs on the CPU, deterministically: greedy decoding, constrained by a GBNF grammar when the
// request has one (a structured request's schema becomes one through core/ai_json_grammar.h), and
// a repetition penalty otherwise. The model stays loaded between requests until another is asked
// for. Derived from Quake-MapGen's src/llama_text_model.cpp (MIT; see docs/CREDITS.md).
//
// runAiGguf blocks; AiChatClient calls it on a worker thread.

#include "core/ai_transport.h"

#include <QString>
#include <QVector>

#include <atomic>

namespace vibestudio {

[[nodiscard]] bool aiGgufRuntimeAvailable();
// "llama.cpp c1d0e7a, CPU", or empty without the runtime.
[[nodiscard]] QString aiGgufRuntimeVersion();
// Why a model path cannot be used (missing, not a .gguf file); empty when it can.
[[nodiscard]] QString aiGgufModelProblem(const QString& path);
// What to do to get the runtime in a build without it.
[[nodiscard]] QString aiGgufRuntimeMissingText();

struct AiGgufRequest {
	QString modelPath;
	QString system;
	QVector<AiChatMessage> messages;
	// GBNF, root rule "root"; empty writes free text.
	QString grammar;
	int maxOutputTokens = 1024;
	// 0 takes the machine's cores, up to 8.
	int threads = 0;
	// The context window: what the prompt and the answer may hold together. 0 takes the model's
	// own, up to 32768. The window is the same for every request to a model, so a planner that
	// checks its inference settings across revisions sees them unchanged.
	int contextTokens = 0;
};

struct AiGgufResult {
	bool ok = false;
	bool cancelled = false;
	QString error;
	QString text;
	int promptTokens = 0;
	int outputTokens = 0;
	// Stopped at maxOutputTokens with the answer unfinished.
	bool truncated = false;
	// The model as loaded: file name, SHA-256 of the file, and the settings used.
	QString modelName;
	QString modelSha256;
	int threads = 0;
	int contextTokens = 0;
	QString sampling;
};

[[nodiscard]] AiGgufResult runAiGguf(const AiGgufRequest& request, const std::atomic_bool* cancelled = nullptr);

} // namespace vibestudio
