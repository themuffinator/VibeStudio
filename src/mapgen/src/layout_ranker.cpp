#include "qmg/layout_ranker.hpp"
#include "qmg/layout_fingerprint.hpp"

#if QMG_HAVE_GGML
#include <ggml-cpu.h>
#include <ggml.h>

#include <cstring>
#include <memory>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace qmg {
namespace {

constexpr std::size_t kFeatureCount = LayoutRanker::feature_count;

bool compact_mp_pool(const PromptSpec &spec, const std::vector<Layout> &candidates) {
    if (spec.game != Game::quake2 || (spec.mode != GameMode::deathmatch && spec.mode != GameMode::duel) ||
        spec.room_count < 2 || spec.room_count > 8)
        return false;
    const auto request_fields = [](const PromptSpec &value) {
        return std::tie(value.source_prompt, value.title, value.theme, value.game, value.mode, value.seed,
                        value.room_count, value.target_players, value.verticality, value.intensity, value.prefer_loops,
                        value.layout_intent);
    };
    return std::all_of(candidates.begin(), candidates.end(), [&](const Layout &candidate) {
        return request_fields(candidate.spec) == request_fields(spec) &&
               candidate.rooms.size() == static_cast<std::size_t>(spec.room_count) &&
               std::all_of(candidate.rooms.begin(), candidate.rooms.end(), [](const Room &room) {
                   return std::find(room.tags.begin(), room.tags.end(), "compact_mp") != room.tags.end();
               });
    });
}

std::uint64_t mixed_seed_key(std::uint64_t value) {
    // SplitMix64's output permutation: unsigned wraparound, addition, odd
    // multiplications and xor-shifts are all invertible on64-bit values.
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

double room_volume(const Room &room) {
    const std::int64_t width = static_cast<std::int64_t>(room.bounds.max.x) - room.bounds.min.x;
    const std::int64_t depth = static_cast<std::int64_t>(room.bounds.max.y) - room.bounds.min.y;
    const std::int64_t height = static_cast<std::int64_t>(room.bounds.max.z) - room.bounds.min.z;
    return static_cast<double>(static_cast<long double>(width) * depth * height);
}

std::array<float, kFeatureCount> features(const Layout &level) {
    const float room_count = static_cast<float>(std::max<std::size_t>(level.rooms.size(), 1));
    const float edge_count = static_cast<float>(level.connections.size());
    const float loop_score = std::clamp((edge_count - room_count + 1.0F) / 2.0F, 0.0F, 1.0F);

    std::vector<double> volumes;
    volumes.reserve(level.rooms.size());
    for (const Room &room : level.rooms)
        volumes.push_back(room_volume(room));
    const double mean = volumes.empty() ? 0.0 : std::accumulate(volumes.begin(), volumes.end(), 0.0) / volumes.size();
    double variance = 0.0;
    for (const double volume : volumes) {
        const double normalized = mean > 0.0 ? (volume - mean) / mean : 0.0;
        variance += normalized * normalized;
    }
    variance = volumes.empty() ? 0.0 : variance / volumes.size();

    std::vector<int> degrees(level.rooms.size(), 0);
    std::int64_t total_corridor_length = 0;
    for (const Connection &connection : level.connections) {
        const std::optional<std::size_t> from_index = room_index(level, connection.from_room);
        const std::optional<std::size_t> to_index = room_index(level, connection.to_room);
        if (from_index)
            ++degrees[*from_index];
        if (to_index)
            ++degrees[*to_index];
        if (from_index && to_index) {
            const GridVec3 a = level.rooms[*from_index].bounds.center();
            const GridVec3 b = level.rooms[*to_index].bounds.center();
            total_corridor_length +=
                std::abs(static_cast<std::int64_t>(a.x) - b.x) + std::abs(static_cast<std::int64_t>(a.y) - b.y);
        }
    }
    const int branches =
        static_cast<int>(std::count_if(degrees.begin(), degrees.end(), [](const int degree) { return degree >= 3; }));
    const int vertical_solids =
        static_cast<int>(std::count_if(level.solids.begin(), level.solids.end(), [](const LayoutSolid &solid) {
            return solid.purpose == BrushPurpose::raised_platform;
        }));
    const int spawn_count = static_cast<int>(
        std::count_if(level.placements.begin(), level.placements.end(), [](const Placement &placement) {
            return placement.role == PlacementRole::player_start || placement.role == PlacementRole::deathmatch_spawn ||
                   placement.role == PlacementRole::team_initial_spawn || placement.role == PlacementRole::team_respawn;
        }));
    const int weapon_count = static_cast<int>(
        std::count_if(level.placements.begin(), level.placements.end(),
                      [](const Placement &placement) { return placement.role == PlacementRole::weapon; }));

    const float average_corridor = level.connections.empty()
                                       ? 0.0F
                                       : static_cast<float>(static_cast<double>(total_corridor_length) /
                                                            static_cast<double>(level.connections.size()));
    const float spawn_balance =
        std::min(1.0F, static_cast<float>(spawn_count) / std::max(2.0F, static_cast<float>(level.spec.target_players)));
    const float weapon_balance = std::min(1.0F, static_cast<float>(weapon_count) / room_count);

    return {
        std::min(1.0F, edge_count / std::max(1.0F, room_count - 1.0F)),
        loop_score,
        std::min(1.0F, static_cast<float>(vertical_solids) / std::max(1.0F, room_count * 0.5F)),
        std::clamp(static_cast<float>(std::sqrt(variance)) * 2.0F, 0.0F, 1.0F),
        std::min(1.0F, static_cast<float>(branches) / 2.0F),
        spawn_balance,
        weapon_balance,
        1.0F - std::clamp(std::abs(average_corridor - 1280.0F) / 1280.0F, 0.0F, 1.0F),
    };
}

// The x86 ggml build compiled that tail's `sumf += x[i]*y[i]` into fused multiply-adds, so each step
// rounds once. std::fma does the same on every platform: over 256,000 random rows it matched
// ggml_mul_mat bit for bit, where a separate multiply and add differed in 30% of them.
#if QMG_HAVE_GGML
// The upstream ranking graph, unchanged: ggml_mul_mat of the candidate matrix and the weight vector
// on the CPU backend, one thread.
std::vector<float> ggml_scores(const std::vector<float> &feature_matrix, const std::array<float, kFeatureCount> &model_weights,
                               const std::size_t candidate_count) {
    std::vector<float> scores(candidate_count);
    const std::size_t context_size = feature_matrix.size() * sizeof(float) + model_weights.size() * sizeof(float) +
                                     scores.size() * sizeof(float) + 3 * ggml_tensor_overhead() +
                                     ggml_graph_overhead() + 1024;
    const ggml_init_params params{context_size, nullptr, false};
    std::unique_ptr<ggml_context, decltype(&ggml_free)> context(ggml_init(params), &ggml_free);
    if (!context)
        throw std::runtime_error("ggml failed to create a ranking context");
    ggml_tensor *candidate_tensor = ggml_new_tensor_2d(context.get(), GGML_TYPE_F32, static_cast<std::int64_t>(kFeatureCount),
                                                       static_cast<std::int64_t>(candidate_count));
    ggml_tensor *weight_tensor = ggml_new_tensor_1d(context.get(), GGML_TYPE_F32, static_cast<std::int64_t>(kFeatureCount));
    std::memcpy(candidate_tensor->data, feature_matrix.data(), feature_matrix.size() * sizeof(float));
    std::memcpy(weight_tensor->data, model_weights.data(), model_weights.size() * sizeof(float));
    ggml_tensor *output = ggml_mul_mat(context.get(), candidate_tensor, weight_tensor);
    ggml_cgraph *graph = ggml_new_graph(context.get());
    ggml_build_forward_expand(graph, output);
    ggml_cplan compute_plan = ggml_graph_plan(graph, 1, nullptr);
    std::vector<std::uint8_t> work_buffer(compute_plan.work_size);
    compute_plan.work_data = work_buffer.empty() ? nullptr : work_buffer.data();
    if (ggml_graph_compute(graph, &compute_plan) != GGML_STATUS_SUCCESS)
        throw std::runtime_error("ggml failed while scoring layout candidates");
    std::memcpy(scores.data(), output->data, scores.size() * sizeof(float));
    return scores;
}
#endif

float scalar_dot(const float *values, const std::array<float, kFeatureCount> &weights) {
    float sum = 0.0F;
    for (std::size_t feature = 0; feature < kFeatureCount; ++feature)
        sum = std::fma(values[feature], weights[feature], sum);
    return sum;
}

std::array<float, kFeatureCount> weights(const PromptSpec &spec) {
    std::array<float, kFeatureCount> result{1.4F, 0.5F, 0.5F, 0.4F, 0.35F, 0.9F, 0.8F, 0.3F};
    result[1] = spec.prefer_loops ? 1.5F : 0.25F;
    result[2] = static_cast<float>(0.4 + spec.verticality * 1.4);
    result[3] = spec.mode == GameMode::single_player ? 0.9F : 0.35F;
    result[4] = spec.mode == GameMode::single_player ? 0.8F : 0.4F;
    result[5] = spec.mode == GameMode::single_player ? 0.4F : 1.2F;
    result[6] = spec.mode == GameMode::single_player ? 0.7F : 1.1F;
    return result;
}

} // namespace

bool ggml_ranker_available() noexcept {
#if QMG_HAVE_GGML
    return true;
#else
    return false;
#endif
}

LayoutRanker::Ranking LayoutRanker::rank(const PromptSpec &spec, const std::vector<Layout> &candidates,
                                         const RankerBackend backend) const {
    if (backend == RankerBackend::ggml && !ggml_ranker_available())
        throw std::invalid_argument("this build has no ggml, so it cannot rank through ggml");
    if (candidates.empty()) {
        throw std::invalid_argument("cannot rank an empty candidate set");
    }
    if (candidates.size() > 64) {
        throw std::invalid_argument("cannot rank more than 64 candidates at once");
    }

    std::vector<float> feature_matrix(candidates.size() * kFeatureCount);
    std::vector<float> scores(candidates.size());
    for (std::size_t candidate = 0; candidate < candidates.size(); ++candidate) {
        const auto values = features(candidates[candidate]);
        std::copy(values.begin(), values.end(),
                  feature_matrix.begin() + static_cast<std::ptrdiff_t>(candidate * kFeatureCount));
    }
    auto model_weights = weights(spec);
    const bool scoped = compact_mp_pool(spec, candidates);
    if (scoped) {
        // Fixed-pool evidence found only these unsupported proxies varied.
        // Abstain from them within the tested marked2–8-room Q2 MP scope; keep
        // their input values visible and retain the other request weights.
        model_weights[3] = 0.0F;
        model_weights[7] = 0.0F;
    }

    // VibeStudio: one float dot product per candidate, in place of the ggml graph Quake-MapGen ran
    // (ggml_mul_mat of the candidate matrix and the weight vector on the CPU backend, one thread).
    // At the pinned llama.cpp revision c1d0e7a004015f23bc0233470b747b596f29b264,
    // ggml/src/ggml-cpu/vec.cpp ggml_vec_dot_f32 handles an 8-element row entirely in its scalar
    // tail: a float sum from +0, in feature order. scalar_dot keeps that order and rounding so every
    // score, tie and selection matches the ggml build bit for bit (src/mapgen/VIBESTUDIO.md).
    for (std::size_t candidate = 0; candidate < candidates.size(); ++candidate)
        scores[candidate] = scalar_dot(&feature_matrix[candidate * kFeatureCount], model_weights);
#if QMG_HAVE_GGML
    if (backend == RankerBackend::ggml)
        scores = ggml_scores(feature_matrix, model_weights, candidates.size());
#endif

    Ranking ranking;
    ranking.scores = std::move(scores);
    ranking.best_index = static_cast<std::size_t>(
        std::distance(ranking.scores.begin(), std::max_element(ranking.scores.begin(), ranking.scores.end())));
    ranking.selection_policy = scoped ? "compact_q2_mp_adequacy_v1" : "legacy_weighted_v1";
    ranking.tie_break_policy = scoped ? "minimum_splitmix64_seed_then_neutral_sha256_v1" : "first_maximum_index";
    const float best_score = ranking.scores[ranking.best_index];
    for (std::size_t candidate = 0; candidate < candidates.size(); ++candidate)
        if (ranking.scores[candidate] == best_score)
            ranking.tied_best_indices.push_back(candidate);
    if (scoped) {
        ranking.candidate_tie_hashes.resize(candidates.size());
        for (const auto &candidate : candidates)
            ranking.candidate_tie_keys.push_back(mixed_seed_key(candidate.candidate_seed));
        const auto neutral_hash = [&](std::size_t index) -> const std::string & {
            auto &hash = ranking.candidate_tie_hashes[index];
            if (hash.empty()) {
                auto neutral = candidates[index];
                neutral.rank_score = 0;
                hash = canonical_layout_sha256(neutral);
            }
            return hash;
        };
        for (std::size_t candidate : ranking.tied_best_indices) {
            const auto key = ranking.candidate_tie_keys[candidate];
            const auto best_key = ranking.candidate_tie_keys[ranking.best_index];
            if (key < best_key || (key == best_key && candidate != ranking.best_index &&
                                   neutral_hash(candidate) < neutral_hash(ranking.best_index)))
                ranking.best_index = candidate;
        }
    }
    ranking.backend = std::string(kLayoutRankerBackend);
    ranking.ggml_version = std::string(kLayoutRankerNotLinked);
    ranking.ggml_commit = std::string(kLayoutRankerNotLinked);
#if QMG_HAVE_GGML
    if (backend == RankerBackend::ggml) {
        ranking.backend = std::string(kGgmlLayoutRankerBackend);
        ranking.ggml_version = ggml_version();
        ranking.ggml_commit = ggml_commit();
    }
#endif
    ranking.feature_names = {"connection_count_ratio",      "cycle_rank_ratio",
                             "raised_platform_count_ratio", "room_volume_cv_scaled",
                             "branching_room_count_ratio",  "spawn_count_ratio",
                             "weapon_count_ratio",          "connected_room_center_distance_score"};
    ranking.feature_weights = model_weights;
    ranking.candidate_features.resize(candidates.size());
    ranking.candidate_contributions.resize(candidates.size());
    for (std::size_t candidate = 0; candidate < candidates.size(); ++candidate) {
        for (std::size_t feature = 0; feature < kFeatureCount; ++feature) {
            const float value = feature_matrix[candidate * kFeatureCount + feature];
            ranking.candidate_features[candidate][feature] = value;
            ranking.candidate_contributions[candidate][feature] = value * model_weights[feature];
        }
    }
    return ranking;
}

} // namespace qmg
