#include "qmg/generator.hpp"
// VibeStudio: Quake-MapGen's GgmlRanker (qmg/ggml_ranker.hpp) is LayoutRanker here.
#include "qmg/layout_ranker.hpp"
#include "qmg/layout_fingerprint.hpp"
#include "qmg/pipeline.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <locale>
#include <stdexcept>

// Original observation adapter: retain all six source geometries without
// replacing the ordinary pipeline's selection or its post-selection repairs.
namespace {
void point(qmg::GridVec3 p) { std::cout << '[' << p.x << ',' << p.y << ',' << p.z << ']'; }
void bounds(const qmg::GridBounds &b) {
    std::cout << '['; point(b.min); std::cout << ','; point(b.max); std::cout << ']';
}
template <typename T> void numbers(const T &values) {
    std::cout << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << values[i];
    }
    std::cout << ']';
}
}

int main(int argc, char **argv) try {
    if (argc != 2) throw std::runtime_error("Expected one prompt file");
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read prompt");
    const std::string prompt{std::istreambuf_iterator<char>(input), {}};
    if (prompt.empty() || prompt.size() > 65536) throw std::runtime_error("Prompt size outside bounds");
    const auto spec = qmg::PromptParser{}.parse(prompt);
    const auto planning = qmg::SemanticPlanner{}.plan(spec);
    auto candidates = qmg::LayoutGenerator{}.generate_candidates(spec, planning.plan, 6);
    const auto ranked = qmg::LayoutRanker{}.rank(spec, candidates);
    const auto pipeline = qmg::GenerationPipeline{}.run(prompt);
    if (candidates.at(ranked.best_index).candidate_seed != pipeline.layout.candidate_seed)
        throw std::runtime_error("Ordinary pipeline selected a different seed");
    std::cout.imbue(std::locale::classic());
    std::cout << std::setprecision(17) << "{\"schema_version\":1,\"candidate_count\":6,\"selected_index\":"
              << ranked.best_index << ",\"pipeline_selected_seed\":" << pipeline.layout.candidate_seed
              << ",\"pipeline_layout_sha256\":\"" << qmg::canonical_layout_sha256(pipeline.layout)
              << "\",\"repair_passes\":" << pipeline.repairs.size() << ",\"planned_verticality\":"
              << pipeline.layout.planned_verticality << ",\"feature_weights\":";
    numbers(ranked.feature_weights);
    std::cout << ",\"selection_policy\":" << std::quoted(ranked.selection_policy)
              << ",\"tie_break_policy\":" << std::quoted(ranked.tie_break_policy) << ",\"candidates\":[";
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (i) std::cout << ',';
        auto &layout = candidates[i];
        layout.rank_score = ranked.scores[i];
        std::cout << "{\"index\":" << i << ",\"seed\":" << layout.candidate_seed
                  << ",\"selected\":" << (i == ranked.best_index ? "true" : "false")
                  << ",\"layout_sha256\":\"" << qmg::canonical_layout_sha256(layout)
                  << "\",\"rank_score\":" << layout.rank_score << ",\"features\":";
        numbers(ranked.candidate_features[i]);
        std::cout << ",\"rooms\":[";
        for (std::size_t j = 0; j < layout.rooms.size(); ++j) {
            if (j) std::cout << ',';
            const auto &room = layout.rooms[j];
            std::cout << "{\"id\":" << room.id.value << ",\"bounds\":"; bounds(room.bounds);
            std::cout << ",\"tags\":[";
            for (std::size_t k = 0; k < room.tags.size(); ++k) {
                if (k) std::cout << ',';
                std::cout << std::quoted(room.tags[k]);
            }
            std::cout << "]}";
        }
        std::cout << "],\"connections\":[";
        for (std::size_t j = 0; j < layout.connections.size(); ++j) {
            if (j) std::cout << ',';
            const auto &c = layout.connections[j];
            std::cout << "{\"id\":" << c.id.value << ",\"from\":" << c.from_room.value
                      << ",\"to\":" << c.to_room.value << ",\"volumes\":[";
            for (std::size_t k = 0; k < c.passage_volumes.size(); ++k) {
                if (k) std::cout << ',';
                bounds(c.passage_volumes[k]);
            }
            std::cout << "]}";
        }
        std::cout << "],\"solids\":[";
        for (std::size_t j = 0; j < layout.solids.size(); ++j) {
            if (j) std::cout << ',';
            const auto &s = layout.solids[j];
            std::cout << "{\"id\":" << s.id.value << ",\"bounds\":"; bounds(qmg::checked_brush_bounds(s.geometry));
            std::cout << ",\"purpose_enum\":" << static_cast<int>(s.purpose) << '}';
        }
        std::cout << "]}";
    }
    std::cout << "]}\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
