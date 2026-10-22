#pragma once

#include "qmg/layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace qmg {

// VibeStudio: Quake-MapGen scored candidates through a ggml graph (GgmlRanker). The same linear
// model now runs by default as plain float arithmetic with ggml's accumulation order, so builds
// need no ggml or llama.cpp. The report keeps its schema v4 keys (ggml_backend, ggml_version,
// ggml_commit, llama_revision) and records these values in them.
inline constexpr std::string_view kLayoutRankerBackend = "vibestudio/scalar-linear-layout-ranker";
inline constexpr std::string_view kLayoutRankerNotLinked = "not-linked";
// VibeStudio: builds with -Dggml=enabled can still rank through the upstream ggml graph, which
// gives the same scores and records ggml's backend, version and commit, as Quake-MapGen did.
inline constexpr std::string_view kGgmlLayoutRankerBackend = "ggml/cpu-linear-layout-ranker";

enum class RankerBackend {
    scalar,
    ggml,
};

// Whether this build links ggml, so RankerBackend::ggml can run.
[[nodiscard]] bool ggml_ranker_available() noexcept;

class LayoutRanker {
  public:
    static constexpr std::size_t feature_count = 8;
    using FeatureValues = std::array<float, feature_count>;

    struct Ranking {
        std::size_t best_index{};
        std::vector<float> scores;
        std::string backend;
        std::string ggml_version;
        std::string ggml_commit;

        // Ordered inputs to the linear model (formerly a ggml tensor). Feature values are the
        // unweighted normalized/clipped values, not raw geometric measurements.
        std::array<std::string, feature_count> feature_names;
        FeatureValues feature_weights{};
        std::vector<FeatureValues> candidate_features;
        // Individual float products are explanatory. Their scalar sum can
        // differ by accumulation rounding from the authoritative ggml scores.
        std::vector<FeatureValues> candidate_contributions;

        std::string selection_policy;
        std::string tie_break_policy;
        // Exact authoritative-score ties only. Legacy pools retain first-max.
        std::vector<std::size_t> tied_best_indices;
        // Scoped pools use the smallest unsigned mixed-seed key. Duplicate
        // seeds fall back to canonical layout SHA-256 with rank_score zeroed.
        // Hash entries stay empty unless that fallback is needed. Identical
        // neutral canonical layouts may retain first index as equivalent inputs.
        std::vector<std::uint64_t> candidate_tie_keys;
        std::vector<std::string> candidate_tie_hashes;
    };

    // RankerBackend::ggml throws std::invalid_argument in a build without ggml.
    [[nodiscard]] Ranking rank(const PromptSpec &spec, const std::vector<Layout> &candidates,
                               RankerBackend backend = RankerBackend::scalar) const;
};

} // namespace qmg
