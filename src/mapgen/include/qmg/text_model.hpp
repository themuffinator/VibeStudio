#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace qmg {

enum class FinishReason {
    end_of_generation,
    token_limit,
};

// VibeStudio: how a model is identified. A local GGUF is named by the SHA-256 of its weights and
// tokenizer. A model reached through a VibeStudio AI connector (a hosted provider, or a local
// OpenAI-compatible server) can only be named by connector and model: its digests stay empty, its
// token counts are whatever the provider reported (0 when it reported none), and its context limit
// is unknown (0).
enum class ModelProvenance {
    local_weights,
    named_service,
};

struct ModelIdentity {
    std::string backend;
    std::string backend_version;
    std::string model_id;
    std::string model_sha256;
    std::string tokenizer_sha256;
    ModelProvenance provenance{ModelProvenance::local_weights};
};

struct InferenceConfig {
    std::string device;
    int threads{};
    std::size_t context_limit{};
    std::string sampling;
};

struct TextGenerationRequest {
    std::string system_prompt;
    std::string user_prompt;
    std::string grammar;
    std::string grammar_root{"root"};
    // VibeStudio: the same contract as a JSON Schema, for providers that take structured output
    // instead of a grammar (build_semantic_plan_json_schema).
    std::string json_schema;
    std::uint64_t seed{};
    std::size_t max_output_tokens{512};
    std::size_t max_output_bytes{65536};
};

struct TextGenerationResult {
    std::string text;
    std::size_t prompt_tokens{};
    std::size_t output_tokens{};
    FinishReason finish_reason{FinishReason::end_of_generation};
    ModelIdentity identity;
    InferenceConfig inference;
};

// VibeStudio: thrown by a TextModel that could not answer at all (no connection, refused, cancelled),
// as opposed to answering with an invalid plan. Planning stops; a rules fallback never hides it.
class TextModelUnavailable : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class TextModel {
  public:
    virtual ~TextModel() = default;
    [[nodiscard]] virtual TextGenerationResult generate(const TextGenerationRequest &request) = 0;
    // VibeStudio: what generate() will report in identity.provenance; the planner records it as the
    // requested backend before the first call.
    [[nodiscard]] virtual ModelProvenance provenance() const { return ModelProvenance::local_weights; }
};

} // namespace qmg
