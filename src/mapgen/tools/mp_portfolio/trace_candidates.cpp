#include "qmg/generator.hpp"
// VibeStudio: Quake-MapGen's GgmlRanker (qmg/ggml_ranker.hpp) is LayoutRanker here.
#include "qmg/layout_ranker.hpp"
#include "qmg/layout_fingerprint.hpp"
#include "qmg/pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <locale>
#include <stdexcept>

// Read-only observation of the same pre-repair generator/ranker calls used by
// GenerationPipeline. A second ordinary run checks selected identity; this tool
// never exports or substitutes a candidate for the pipeline's chosen map.
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Expected one prompt file; JSON is written to stdout.");
        std::ifstream input(argv[1], std::ios::binary);
        if (!input)
            throw std::runtime_error("Cannot read prompt file.");
        const std::string prompt{std::istreambuf_iterator<char>(input), {}};
        if (prompt.empty() || prompt.size() > 262144)
            throw std::runtime_error("Prompt size outside bounds.");
        const auto spec = qmg::PromptParser{}.parse(prompt);
        const auto planning = qmg::SemanticPlanner{}.plan(spec);
        auto candidates = qmg::LayoutGenerator{}.generate_candidates(spec, planning.plan, 6);
        const auto ranking = qmg::LayoutRanker{}.rank(spec, candidates);
        const auto result = qmg::GenerationPipeline{}.run(prompt);
        if (candidates.at(ranking.best_index).candidate_seed != result.layout.candidate_seed)
            throw std::runtime_error("Trace selected seed differs from ordinary pipeline.");
        if (ranking.scores.at(ranking.best_index) != result.layout.rank_score)
            throw std::runtime_error("Trace selected score differs from ordinary pipeline.");
        candidates[ranking.best_index].rank_score = ranking.scores[ranking.best_index];
        if (result.repairs.empty() && qmg::canonical_layout_sha256(candidates[ranking.best_index]) !=
                                          qmg::canonical_layout_sha256(result.layout))
            throw std::runtime_error("Unrepaired pipeline geometry differs from selected candidate.");
        std::cout.imbue(std::locale::classic());
        std::cout << std::setprecision(9) << "{\"schema_version\":2,\"candidate_count\":6,\"selected_index\":"
                  << ranking.best_index << ",\"pipeline_selected_seed\":" << result.layout.candidate_seed
                  << ",\"pipeline_layout_sha256\":\"" << qmg::canonical_layout_sha256(result.layout)
                  << "\",\"repair_passes\":" << result.repairs.size();
        if (spec.layout_intent != qmg::LayoutIntent::unspecified)
            std::cout << ",\"layout_intent\":\"" << qmg::to_string(spec.layout_intent) << '"';
        std::cout << ",\"feature_names\":[";
        for (std::size_t feature = 0; feature < ranking.feature_names.size(); ++feature) {
            if (feature)
                std::cout << ',';
            std::cout << '"' << ranking.feature_names[feature] << '"';
        }
        const auto values = [](const auto &items) {
            std::cout << '[';
            for (std::size_t j = 0; j < items.size(); ++j) {
                if (j)
                    std::cout << ',';
                if (!std::isfinite(items[j]))
                    throw std::runtime_error("Nonfinite ranking diagnostic.");
                std::cout << items[j];
            }
            std::cout << ']';
        };
        std::cout << "],\"feature_weights\":";
        values(ranking.feature_weights);
        std::cout << ",\"selection_policy\":\"" << ranking.selection_policy
                  << "\",\"tie_break_policy\":\"" << ranking.tie_break_policy
                  << "\",\"tied_best_indices\":[";
        for (std::size_t i = 0; i < ranking.tied_best_indices.size(); ++i) {
            if (i)
                std::cout << ',';
            std::cout << ranking.tied_best_indices[i];
        }
        std::cout << "],\"contribution_scope\":\"Float feature-times-weight products; their scalar sum can differ from authoritative ggml accumulation by rounding.\",\"candidates\":[";
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            auto &layout = candidates[i];
            layout.rank_score = ranking.scores[i];
            if (!std::isfinite(layout.rank_score))
                throw std::runtime_error("Nonfinite candidate rank.");
            const bool pinwheel = std::any_of(layout.rooms.begin(), layout.rooms.end(), [](const auto &room) {
                return std::find(room.tags.begin(), room.tags.end(), "compact_family_pinwheel") != room.tags.end();
            });
            if (i)
                std::cout << ',';
            auto neutral_rank_layout = layout;
            neutral_rank_layout.rank_score = 0;
            auto neutral_spawn_origins = neutral_rank_layout;
            for (auto &placement : neutral_spawn_origins.placements)
                if (placement.role == qmg::PlacementRole::deathmatch_spawn)
                    placement.origin = {};
            std::cout << "{\"index\":" << i << ",\"seed\":" << layout.candidate_seed
                      << ",\"family\":\"" << (pinwheel ? "pinwheel" : "rows")
                      << "\",\"rank_score\":" << layout.rank_score << ",\"selected\":"
                      << (i == ranking.best_index ? "true" : "false")
                      << ",\"ranked_pre_repair_layout_sha256\":\"" << qmg::canonical_layout_sha256(layout)
                      << "\",\"rank_neutral_layout_sha256\":\"" << qmg::canonical_layout_sha256(neutral_rank_layout)
                      << "\",\"spawn_origin_neutral_layout_sha256\":\"" << qmg::canonical_layout_sha256(neutral_spawn_origins)
                      << "\",\"tie_key\":";
            if (ranking.candidate_tie_keys.empty())
                std::cout << "null";
            else
                std::cout << ranking.candidate_tie_keys[i];
            std::cout << ",\"tie_hash\":\""
                      << (ranking.candidate_tie_hashes.empty() ? "" : ranking.candidate_tie_hashes[i])
                      << "\",\"features\":";
            values(ranking.candidate_features[i]);
            std::cout << ",\"contributions\":";
            values(ranking.candidate_contributions[i]);
            std::cout << ",\"rooms\":[";
            for (std::size_t j = 0; j < layout.rooms.size(); ++j) {
                if (j)
                    std::cout << ',';
                const auto &r = layout.rooms[j];
                std::cout << "{\"id\":" << r.id.value << "}";
            }
            std::cout << "],\"connections\":[";
            for (std::size_t j = 0; j < layout.connections.size(); ++j) {
                if (j)
                    std::cout << ',';
                const auto &c = layout.connections[j];
                std::cout << "{\"from\":" << c.from_room.value << ",\"to\":" << c.to_room.value << "}";
            }
            std::cout << "]}";
        }
        std::cout << "]}\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
