#include "qmg/generator.hpp"
#include "qmg/layout_ranker.hpp"
#include "qmg/layout_fingerprint.hpp"
#include "qmg/prompt.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace qmg;

void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

void near(float actual, float expected, const std::string &message, float tolerance = 0.000002F) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}

// An analytical room-record fixture. It tests what the ranker reads; it does
// not itself certify compiled space, ordinary movement or a valid map build.
Layout partition(const std::vector<int> &widths) {
    Layout layout;
    layout.spec.game = Game::quake2;
    layout.spec.mode = GameMode::deathmatch;
    layout.spec.target_players = 4;
    layout.spec.room_count = static_cast<int>(widths.size());
    layout.spec.prefer_loops = true;
    layout.spec.verticality = 0.5;
    int x = 0;
    for (int width : widths) {
        const auto id = next_room_id(layout);
        layout.rooms.push_back({id, "partition", {{x, 0, 0}, {x + width, 1024, 256}}, {}});
        x += width;
        if (id.value > 1)
            layout.connections.push_back({next_connection_id(layout), RoomId{id.value - 1}, id});
    }
    for (int station = 0; station < 4; ++station) {
        Placement spawn;
        spawn.id = next_placement_id(layout);
        spawn.role = PlacementRole::deathmatch_spawn;
        spawn.origin = {64 + station * 512, 128, 24};
        layout.placements.push_back(spawn);
        Placement weapon;
        weapon.id = next_placement_id(layout);
        weapon.role = PlacementRole::weapon;
        weapon.origin = {64 + station * 512, 896, 24};
        layout.placements.push_back(weapon);
    }
    return layout;
}

void diagnostic_contract(const LayoutRanker::Ranking &ranking, std::size_t candidates) {
    const std::array<std::string, 8> names{"connection_count_ratio",      "cycle_rank_ratio",
                                           "raised_platform_count_ratio", "room_volume_cv_scaled",
                                           "branching_room_count_ratio",  "spawn_count_ratio",
                                           "weapon_count_ratio",          "connected_room_center_distance_score"};
    require(ranking.feature_names == names, "ordered diagnostic feature identities changed");
    require(ranking.scores.size() == candidates && ranking.candidate_features.size() == candidates &&
                ranking.candidate_contributions.size() == candidates && ranking.best_index < candidates,
            "candidate diagnostics do not align with authoritative scores");
    require(ranking.backend == qmg::kLayoutRankerBackend, "instrumentation changed the backend");
    for (std::size_t candidate = 0; candidate < candidates; ++candidate) {
        double sum = 0;
        for (std::size_t feature = 0; feature < LayoutRanker::feature_count; ++feature) {
            const float value = ranking.candidate_features[candidate][feature];
            require(std::isfinite(value) && value >= 0 && value <= 1, "invalid normalized feature");
            const float product = value * ranking.feature_weights[feature];
            require(ranking.candidate_contributions[candidate][feature] == product,
                    "contribution differs from the exposed tensor input product");
            sum += product;
        }
        // GGML's accumulation order remains authoritative; do not replace its
        // output with a scalar sum or require identical last-bit rounding here.
        near(ranking.scores[candidate], static_cast<float>(sum), "contributions cannot explain ggml score");
    }
}

void partition_sensitivity() {
    const auto equal = partition({512, 512, 512, 512});
    const auto unequal = partition({256, 256, 512, 1024});
    const auto ranking = LayoutRanker{}.rank(equal.spec, {equal, unequal});
    diagnostic_contract(ranking, 2);
    const LayoutRanker::FeatureValues equal_expected{1, 0, 0, 0, 0, 1, 1, 0.4F};
    const LayoutRanker::FeatureValues unequal_expected{1, 0, 0, 1, 0, 1, 1, (1408.0F / 3.0F) / 1280.0F};
    for (std::size_t feature = 0; feature < LayoutRanker::feature_count; ++feature) {
        near(ranking.candidate_features[0][feature], equal_expected[feature], "equal partition feature mismatch");
        near(ranking.candidate_features[1][feature], unequal_expected[feature], "unequal partition feature mismatch");
    }
    near(ranking.scores[1] - ranking.scores[0], 0.34F, "legacy partition score difference changed");
    require(ranking.best_index == 1, "diagnostics changed the legacy partition preference");

    // The distance feature reads centers, not the authored passage's dimensions.
    auto changed_passages = equal;
    for (auto &connection : changed_passages.connections)
        connection.passage_volumes = {{{-4096, -2048, 0}, {4096, 2048, 256}}};
    const auto passage_ranking = LayoutRanker{}.rank(equal.spec, {equal, changed_passages});
    require(passage_ranking.candidate_features[0] == passage_ranking.candidate_features[1] &&
                passage_ranking.scores[0] == passage_ranking.scores[1] && passage_ranking.best_index == 0,
            "diagnostics changed the inherited center-distance or first-tie policy");
}

void request_weights() {
    const auto layout = partition({512, 512, 512, 512});
    auto request = layout.spec;
    request.mode = GameMode::single_player;
    request.prefer_loops = false;
    request.verticality = 0.0;
    const auto single = LayoutRanker{}.rank(request, {layout});
    diagnostic_contract(single, 1);
    const LayoutRanker::FeatureValues expected{1.4F, 0.25F, 0.4F, 0.9F, 0.8F, 0.4F, 0.7F, 0.3F};
    require(single.feature_weights == expected, "diagnostics changed request-conditioned legacy weights");
    const auto multi = LayoutRanker{}.rank(layout.spec, {layout});
    require(multi.candidate_features == single.candidate_features,
            "request weights unexpectedly changed the candidate feature inputs");
    require(multi.feature_weights != single.feature_weights, "request-dependent weights were not exposed");
}

Layout marked(Layout layout, std::uint64_t seed) {
    layout.candidate_seed = seed;
    for (auto &room : layout.rooms)
        room.tags.push_back("compact_mp");
    return layout;
}

std::string neutral_hash(Layout layout) {
    layout.rank_score = 0;
    return canonical_layout_sha256(layout);
}

void scoped_abstention_and_seed_keys() {
    const auto equal = marked(partition({512, 512, 512, 512}), 0);
    const auto unequal = marked(partition({256, 256, 512, 1024}), 1);
    const auto ranking = LayoutRanker{}.rank(equal.spec, {equal, unequal});
    diagnostic_contract(ranking, 2);
    require(ranking.selection_policy == "compact_q2_mp_adequacy_v1" &&
                ranking.tie_break_policy == "minimum_splitmix64_seed_then_neutral_sha256_v1",
            "scoped policy was not reported");
    require(ranking.feature_weights[3] == 0 && ranking.feature_weights[7] == 0,
            "unsupported scoped proxies still receive weight");
    require(ranking.candidate_features[0] != ranking.candidate_features[1], "diagnostics erased retired inputs");
    require(ranking.scores[0] == ranking.scores[1] && ranking.tied_best_indices == std::vector<std::size_t>{0, 1},
            "scoped abstention did not retain exact ggml-score equality");
    require(ranking.candidate_tie_keys == std::vector<std::uint64_t>{0xE220A8397B1DCDAFULL, 0x910A2DEC89025CC1ULL} &&
                ranking.best_index == 1,
            "stable SplitMix64 output keys or unsigned ordering changed");
    require(std::all_of(ranking.candidate_tie_hashes.begin(), ranking.candidate_tie_hashes.end(),
                        [](const auto &hash) { return hash.empty(); }),
            "unique seeds should not need the canonical fallback");
    const auto reversed = LayoutRanker{}.rank(equal.spec, {unequal, equal});
    require(reversed.best_index == 0, "seed tie policy depends on input order");

    // A deliberately tiny but nonzero score difference must outrank the seed
    // key. The large player denominator is an analytical API boundary fixture.
    auto higher = equal, lower = equal;
    higher.spec.target_players = lower.spec.target_players = 1'000'000;
    lower.candidate_seed = 1;
    lower.placements.erase(std::find_if(lower.placements.begin(), lower.placements.end(),
                                        [](const auto &p) { return p.role == PlacementRole::deathmatch_spawn; }));
    const auto close = LayoutRanker{}.rank(higher.spec, {higher, lower});
    require(close.scores[0] > close.scores[1] && close.scores[0] - close.scores[1] < 0.00001F &&
                close.best_index == 0 && close.tied_best_indices == std::vector<std::size_t>{0},
            "an exact-score policy became a near-tie window");
}

void duplicate_seed_fallback() {
    auto first = marked(partition({512, 512, 512, 512}), 42);
    auto second = marked(partition({256, 256, 512, 1024}), 42);
    first.rank_score = 99;
    second.rank_score = -17;
    const auto first_hash = neutral_hash(first), second_hash = neutral_hash(second);
    require(first_hash != second_hash, "different partition fixtures lost canonical identity");
    const auto ranking = LayoutRanker{}.rank(first.spec, {first, second});
    require(ranking.candidate_tie_hashes == std::vector<std::string>{first_hash, second_hash} &&
                ranking.best_index == (first_hash < second_hash ? 0 : 1),
            "duplicate-seed exact ties did not use rank-neutral canonical identity");
    const auto reversed = LayoutRanker{}.rank(first.spec, {second, first});
    require(ranking.candidate_tie_hashes[ranking.best_index] == reversed.candidate_tie_hashes[reversed.best_index],
            "duplicate-seed selection depends on input order");
    require(first.rank_score == 99 && second.rank_score == -17, "canonical fallback mutated input scores");
    auto equivalent = first;
    equivalent.rank_score = -99;
    const auto identical = LayoutRanker{}.rank(first.spec, {first, equivalent});
    require(identical.best_index == 0 && identical.candidate_tie_hashes[0] == identical.candidate_tie_hashes[1],
            "identical neutral inputs should retain the first equivalent index");
}

void negative_scope_controls() {
    const auto base = marked(partition({512, 512, 512, 512}), 0);
    const auto legacy = [](const PromptSpec &request, const std::vector<Layout> &pool) {
        const auto result = LayoutRanker{}.rank(request, pool);
        require(result.selection_policy == "legacy_weighted_v1" && result.tie_break_policy == "first_maximum_index" &&
                    result.feature_weights[3] != 0 && result.feature_weights[7] != 0 &&
                    result.candidate_tie_keys.empty() && result.candidate_tie_hashes.empty(),
                "unscoped pool entered the new policy");
    };
    auto changed = base;
    changed.rooms.front().tags.clear();
    legacy(base.spec, {base, changed});
    legacy(base.spec, {changed});
    changed = base;
    changed.spec.room_count = 3;
    legacy(base.spec, {base, changed});
    changed = base;
    changed.rooms.pop_back();
    legacy(base.spec, {base, changed});
    changed = base;
    ++changed.spec.seed;
    legacy(base.spec, {base, changed});
    changed = base;
    changed.spec.source_prompt = "different request provenance";
    legacy(base.spec, {base, changed});
    for (auto [game, mode] :
         {std::pair{Game::quake1, GameMode::deathmatch}, std::pair{Game::quake3, GameMode::deathmatch},
          std::pair{Game::quake2, GameMode::single_player}, std::pair{Game::quake2, GameMode::capture_the_flag}}) {
        changed = base;
        changed.spec.game = game;
        changed.spec.mode = mode;
        legacy(changed.spec, {changed, changed});
    }
    for (int rooms : {1, 9, 32}) {
        changed = marked(partition(std::vector<int>(static_cast<std::size_t>(rooms), 512)), 0);
        legacy(changed.spec, {changed, changed});
    }
}

void generated_pool_permutations() {
    const auto spec = PromptParser{}.parse("Quake 2 deathmatch industrial arena for 6 players with 6 rooms, vertical "
                                           "combat, tight connected loops, seed 73");
    const auto candidates = LayoutGenerator{}.generate_candidates(spec, make_rule_semantic_plan(spec), 6);
    const auto ranking = LayoutRanker{}.rank(spec, candidates);
    diagnostic_contract(ranking, 6);
    require(ranking.tied_best_indices.size() == 6, "fixed compact pool lost the observed exact adequacy tie");
    const auto selected_seed = candidates[ranking.best_index].candidate_seed;
    std::array<std::size_t, 6> order{0, 1, 2, 3, 4, 5};
    do {
        std::vector<Layout> permuted;
        for (auto index : order)
            permuted.push_back(candidates[index]);
        const auto before = neutral_hash(permuted.front());
        const auto result = LayoutRanker{}.rank(spec, permuted);
        require(permuted[result.best_index].candidate_seed == selected_seed,
                "fixed-pool selected identity changed under permutation");
        require(neutral_hash(permuted.front()) == before, "policy diagnostics mutated candidate geometry");
    } while (std::next_permutation(order.begin(), order.end()));
    for (int rooms : {2, 3, 5, 7, 8}) {
        const auto control_spec =
            PromptParser{}.parse("industrial connected loops", {Game::quake2, GameMode::duel, 211, rooms, 2});
        const auto pool = LayoutGenerator{}.generate_candidates(control_spec, make_rule_semantic_plan(control_spec), 2);
        const auto result = LayoutRanker{}.rank(control_spec, pool);
        require(result.selection_policy == "compact_q2_mp_adequacy_v1", "positive bounded duel scope was excluded");
    }
}

void frozen_splice_control() {
    const auto spec = PromptParser{}.parse("Quake 2 deathmatch industrial arena for 6 players with 6 rooms, vertical "
                                           "combat, tight connected loops, seed 73");
    auto candidates = LayoutGenerator{}.generate_candidates(spec, make_rule_semantic_plan(spec), 6);
    // Remove only the scope marker to exercise the unchanged legacy ranker on
    // the frozen geometry. Tags are not part of its eight feature inputs.
    for (auto &candidate : candidates)
        for (auto &room : candidate.rooms)
            std::erase(room.tags, "compact_mp");
    std::vector<std::string> before;
    for (const auto &candidate : candidates)
        before.push_back(canonical_layout_sha256(candidate));
    const auto ranking = LayoutRanker{}.rank(spec, candidates);
    diagnostic_contract(ranking, 6);
    // Recorded before instrumentation in out/splice/candidate-trace.json.
    // The independent fixed-pool experiment additionally checks exact host bits.
    const std::array<float, 6> legacy_scores{7.56669092F, 7.32515001F, 7.60177946F,
                                             7.34761286F, 7.5985136F,  7.28396511F};
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        near(ranking.scores[i], legacy_scores[i], "frozen legacy score changed");
        require(canonical_layout_sha256(candidates[i]) == before[i], "ranker diagnostics mutated a candidate");
    }
    require(ranking.best_index == 2 && candidates[ranking.best_index].candidate_seed == 4354685564936845427ULL,
            "instrumentation changed the frozen selector result");
    std::array<std::size_t, 6> order{0, 1, 2, 3, 4, 5};
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return ranking.scores[a] > ranking.scores[b]; });
    require(order == std::array<std::size_t, 6>{2, 4, 0, 3, 1, 5}, "legacy full candidate ordering changed");
}

void bounded_inputs() {
    const auto layout = partition({512, 512, 512, 512});
    for (std::size_t count : {std::size_t{0}, std::size_t{65}}) {
        bool rejected = false;
        try {
            static_cast<void>(LayoutRanker{}.rank(layout.spec, std::vector<Layout>(count, layout)));
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        require(rejected, "legacy candidate bound changed");
    }
    const auto maximal = LayoutRanker{}.rank(layout.spec, std::vector<Layout>(64, layout));
    diagnostic_contract(maximal, 64);
    require(maximal.best_index == 0, "equal-score candidate order changed at the supported bound");
}
} // namespace

int main() try {
    partition_sensitivity();
    request_weights();
    scoped_abstention_and_seed_keys();
    duplicate_seed_fallback();
    negative_scope_controls();
    generated_pool_permutations();
    frozen_splice_control();
    bounded_inputs();
    std::cout << "Ranker diagnostics preserve legacy scores/order and expose actual feature products, request weights, "
                 "partition sensitivity and bounded nonmutating inputs.\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
