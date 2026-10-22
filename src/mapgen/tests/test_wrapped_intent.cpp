#include "qmg/generator.hpp"
#include "qmg/layout_ranker.hpp"
#include "qmg/layout_fingerprint.hpp"
#include "qmg/mp_motifs.hpp"
#include "qmg/navigation.hpp"
#include "qmg/prompt.hpp"
#include "qmg/report.hpp"
#include "qmg/semantic_planner.hpp"
#include "qmg/validator.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using namespace qmg;

namespace {
void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Action> void rejects(Action action, std::string_view message_part) {
    try {
        action();
    } catch (const std::invalid_argument &error) {
        require(std::string_view(error.what()).find(message_part) != std::string::npos,
                "rejection did not explain the invalid intent: " + std::string(error.what()));
        return;
    }
    throw std::runtime_error("invalid construction intent was accepted");
}

bool tagged(const Room &room, std::string_view tag) {
    return std::find(room.tags.begin(), room.tags.end(), tag) != room.tags.end();
}

void parsing() {
    for (const std::string phrase :
         {"wrapped layout around a playable center", "wraparound layout", "wrap-around layout",
          "layout wrapped around a playable centre", "layout wrapping around a playable center", "WRAPPED\t\nLAYOUT"}) {
        const auto request = PromptParser{}.parse("Quake 2 duel with 6 rooms, " + phrase);
        require(request.layout_intent == LayoutIntent::wrapped_center, "positive phrase lost typed intent");
    }
    for (const std::string phrase : {"unwrapped layout", "wrapped layouting", "wrapped paper", "compact loops"})
        require(PromptParser{}.parse("Quake 2 deathmatch, " + phrase).layout_intent == LayoutIntent::unspecified,
                "substring or unrelated wording enabled wrapped intent");
    for (const std::string phrase :
         {"no wrapped layout", "do not use a wrapped layout", "not a wraparound layout", "without the wrapped layout",
          "avoid any wrap-around layout", "don't create a wrapped layout"})
        rejects([&] { (void)PromptParser{}.parse("Quake 2 deathmatch, " + phrase); }, "negative wrapped layout");
    rejects([] { (void)PromptParser{}.parse("Quake 2 DM, wrapped layout; no wrapped layout"); }, "conflicting");
    rejects([] { (void)PromptParser{}.parse("Quake 2 DM, wrapped layout with strict symmetry"); }, "strict symmetry");

    for (const std::string context : {"Quake 1 DM", "Quake 3 DM", "Quake 2 SP", "Quake 2 CTF",
                                      "Quake 2 DM with 4 rooms", "Quake 2 DM with 9 rooms"})
        rejects([&] { (void)PromptParser{}.parse(context + ", wrapped layout"); }, "5 to 8 rooms");
    const auto overridden =
        PromptParser{}.parse("Quake 1 SP with 4 rooms, wrapped layout", {Game::quake2, GameMode::duel, 503, 8, 2});
    require(overridden.layout_intent == LayoutIntent::wrapped_center && overridden.room_count == 8,
            "scope validation ran before final overrides");
    rejects([] { (void)PromptParser{}.parse("Quake 2 DM, wrapped layout", {Game::quake3}); }, "5 to 8 rooms");
    rejects([] { (void)PromptParser{}.parse("Quake 2 DM, wrapped layout", {{}, {}, {}, 4}); }, "5 to 8 rooms");
}

class CapturingModel final : public TextModel {
  public:
    explicit CapturingModel(SemanticPlan plan) : plan_(std::move(plan)) {}
    TextGenerationResult generate(const TextGenerationRequest &request) override {
        requests.push_back(request);
        TextGenerationResult result;
        result.text = render_semantic_plan(plan_);
        result.prompt_tokens = 73;
        result.output_tokens = 41;
        result.identity = {"scripted/gguf", "test-v1", "fixture.gguf", std::string(64, 'a'), std::string(64, 'b')};
        result.inference = {"scripted", 1, 4096, "deterministic-script"};
        return result;
    }
    std::vector<TextGenerationRequest> requests;

  private:
    SemanticPlan plan_;
};

void semantic_and_fingerprint_contract() {
    auto request = PromptParser{}.parse("Quake 2 DM with 6 rooms, wrapped layout, seed 503");
    const auto plan = make_rule_semantic_plan(request);
    require(plan.schema_version == 1 && plan.symmetry == Symmetry::none && plan.route_style == RouteStyle::loop,
            "wrapped request lost its asymmetric loop contract");
    const auto json = render_semantic_plan(plan);
    const auto parsed = parse_semantic_plan(json);
    require(parsed.ok() && render_semantic_plan(*parsed.plan) == json, "semantic v1 no longer round-trips");
    require(json.find("layout_intent") == std::string::npos, "construction leaked into semantic-plan schema");
    const auto grammar = build_semantic_plan_grammar(request);
    require(grammar.find("symmetry ::= \"\\\"none\\\"\"\n") != std::string::npos,
            "wrapped grammar permits a false symmetry claim");
    for (const auto symmetry : {Symmetry::soft, Symmetry::strict}) {
        auto incompatible = plan;
        incompatible.symmetry = symmetry;
        const auto issues = validate_semantic_plan(incompatible, request);
        require(std::any_of(issues.begin(), issues.end(),
                            [](const auto &issue) { return issue.code == "wrapped_symmetry"; }),
                "wrapped semantic plan accepted contradictory symmetry");
        rejects([&] { (void)LayoutGenerator{}.generate_candidates(request, incompatible, 1); }, "valid semantic plan");
        Layout direct;
        direct.spec = request;
        rejects([&] { build_compact_mp_layout(direct, incompatible); }, "asymmetric semantic planning");
    }
    CapturingModel model(plan);
    (void)SemanticPlanner{}.plan(request, {&model});
    require(model.requests.size() == 1 &&
                model.requests.front().user_prompt.find("layout_intent=wrapped_center") != std::string::npos,
            "model planner did not receive authoritative typed intent");

    request.layout_intent = LayoutIntent::unspecified;
    const auto unrequested_plan = make_rule_semantic_plan(request);
    require(unrequested_plan.symmetry == Symmetry::soft, "unrequested semantic behavior changed");
    CapturingModel old_model(unrequested_plan);
    (void)SemanticPlanner{}.plan(request, {&old_model});
    require(old_model.requests.front().user_prompt.find("layout_intent=") == std::string::npos,
            "unrequested planner prompt gained an intent field");
    require(build_semantic_plan_grammar(request).find("symmetry ::= \"\\\"none\\\"\" | \"\\\"soft\\\"\"\n") !=
                std::string::npos,
            "unrequested semantic grammar changed");

    // This legacy empty-layout value was reconstructed from the original v1
    // byte schema before adding the new field; no generated geometry is pinned.
    require(canonical_layout_sha256(Layout{}) == "05dae3fe748158df28b9ed6f6d51fdbdd5a2d11562547bf95f306ef1c614dfe5",
            "unspecified intent changed the historical canonical byte stream");
    Layout layout;
    layout.spec = request;
    const auto legacy_hash = canonical_layout_sha256(layout);
    layout.spec.layout_intent = LayoutIntent::wrapped_center;
    require(canonical_layout_sha256(layout) != legacy_hash, "typed intent is absent from the canonical fingerprint");

    for (const auto invalid : {LayoutIntent::wrapped_center, static_cast<LayoutIntent>(99)}) {
        auto bad = request;
        bad.layout_intent = invalid;
        if (invalid == LayoutIntent::wrapped_center)
            bad.room_count = 9;
        const std::string explanation =
            invalid == LayoutIntent::wrapped_center ? "5 to 8 rooms" : "invalid LayoutIntent";
        rejects([&] { (void)make_rule_semantic_plan(bad); }, explanation);
        rejects([&] { (void)build_semantic_plan_grammar(bad); }, explanation);
        rejects([&] { (void)validate_semantic_plan(plan, bad); }, explanation);
        rejects([&] { (void)LayoutGenerator{}.generate_candidates(bad, plan, 1); }, explanation);
        Layout direct;
        direct.spec = bad;
        rejects([&] { build_compact_mp_layout(direct, plan); }, explanation);
        rejects([&] { (void)canonical_layout_sha256(direct); }, explanation);
    }
    rejects([] { (void)to_string(static_cast<LayoutIntent>(99)); }, "invalid LayoutIntent");
}

bool gameplay_anchor(const Placement &placement) {
    return placement.role == PlacementRole::deathmatch_spawn || placement.role == PlacementRole::weapon ||
           placement.role == PlacementRole::health || placement.role == PlacementRole::armor;
}

void ordinary_routes(const Layout &layout) {
    const auto nav = NavigationAnalyzer{}.analyze(layout);
    require(nav.ok(), "wrapped candidate failed source navigation");
    for (const auto &spawn : layout.placements) {
        if (spawn.role != PlacementRole::deathmatch_spawn)
            continue;
        const auto *binding = nav.find_binding(spawn.id);
        require(binding && binding->node, "spawn lacks source navigation binding");
        std::set<NavigationNodeId> seen{*binding->node};
        std::vector<NavigationNodeId> pending{*binding->node};
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            for (const auto &edge : nav.graph.edges())
                if (edge.from == current &&
                    (edge.kind == NavigationTransitionKind::walk || edge.kind == NavigationTransitionKind::step ||
                     edge.kind == NavigationTransitionKind::door) &&
                    seen.insert(edge.to).second)
                    pending.push_back(edge.to);
        }
        for (const auto &target : layout.placements)
            if (gameplay_anchor(target)) {
                const auto *finish = nav.find_binding(target.id);
                require(finish && finish->node && seen.contains(*finish->node),
                        "wrapped candidate lost ordinary anchor reachability");
            }
    }
}

bool overlap(const GridBounds &a, const GridBounds &b) {
    return a.min.x < b.max.x && a.max.x > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y && a.min.z < b.max.z &&
           a.max.z > b.min.z;
}

void wrapped_geometry(const Layout &layout) {
    require(LayoutValidator{}.validate(layout).ok(), "wrapped candidate failed structural validation");
    const auto floor_z = layout.rooms.front().bounds.min.z;
    std::size_t ground_connections = 0, upper_connections = 0;
    GridBounds envelope = layout.rooms.front().bounds;
    for (const auto &room : layout.rooms) {
        require(tagged(room, "compact_family_pinwheel"), "explicit candidate reverted to the row family");
        envelope.min.x = std::min(envelope.min.x, room.bounds.min.x);
        envelope.min.y = std::min(envelope.min.y, room.bounds.min.y);
        envelope.max.x = std::max(envelope.max.x, room.bounds.max.x);
        envelope.max.y = std::max(envelope.max.y, room.bounds.max.y);
        require(room.bounds.max.x - room.bounds.min.x >= 640 && room.bounds.max.y - room.bounds.min.y >= 640,
                "wrapped room cannot hold the supported motif rotations");
    }
    std::vector<RoomId> interior;
    for (const auto &room : layout.rooms)
        if (room.bounds.min.x > envelope.min.x && room.bounds.max.x < envelope.max.x &&
            room.bounds.min.y > envelope.min.y && room.bounds.max.y < envelope.max.y)
            interior.push_back(room.id);
    require(interior.size() == 1, "wrapped layout lacks one playable interior room");
    std::map<RoomId, std::set<RoomId>> rim;
    int spokes = 0;
    for (const auto &connection : layout.connections) {
        require(connection.passage_volumes.size() == 1, "wrapped face join has unexpected geometry");
        const auto b = connection.passage_volumes.front();
        require((b.max.x - b.min.x == 48 && b.max.y - b.min.y == 160) ||
                    (b.max.y - b.min.y == 48 && b.max.x - b.min.x == 160),
                "wrapped join is not a short face passage");
        require(b.max.z - b.min.z == 192, "wrapped passage lost standing height");
        if (b.min.z == floor_z) {
            ++ground_connections;
            if (connection.from_room == interior.front() || connection.to_room == interior.front())
                ++spokes;
            else {
                rim[connection.from_room].insert(connection.to_room);
                rim[connection.to_room].insert(connection.from_room);
            }
        } else {
            ++upper_connections;
            require(b.min.z == floor_z + 128 || b.min.z == floor_z + 192,
                    "upper passage does not meet an existing deck height");
            const GridBounds support{{b.min.x, b.min.y, b.min.z - 24}, {b.max.x, b.max.y, b.min.z}};
            require(std::count_if(layout.solids.begin(), layout.solids.end(), [&](const auto &solid) {
                        return solid.purpose == BrushPurpose::raised_platform &&
                               checked_brush_bounds(solid.geometry) == support;
                    }) == 1,
                    "upper passage lacks its explicit full-width portal slab");
        }
    }
    require(ground_connections == layout.rooms.size() + 3 && upper_connections <= 1,
            "wrapped candidate lost its ground cycle or added multiple upper passages");
    require(spokes == 4 && rim.size() + 1 == layout.rooms.size(), "wrapped center lacks four ordinary sides");
    std::set<RoomId> reached;
    std::vector<RoomId> pending{rim.begin()->first};
    while (!pending.empty()) {
        const auto room = pending.back();
        pending.pop_back();
        if (!reached.insert(room).second)
            continue;
        require(rim.at(room).size() == 2, "outer bypass does not form a cycle");
        for (const auto neighbor : rim.at(room))
            pending.push_back(neighbor);
    }
    require(reached.size() == rim.size(), "outer bypass splits into disconnected cycles");
    for (const auto &placement : layout.placements) {
        if (!gameplay_anchor(placement))
            continue;
        const GridBounds hull{{placement.origin.x - 16, placement.origin.y - 16, placement.origin.z - 24},
                              {placement.origin.x + 16, placement.origin.y + 16, placement.origin.z + 32}};
        bool supported = false;
        for (const auto &room : layout.rooms)
            supported |= hull.min.z == room.bounds.min.z && hull.min.x >= room.bounds.min.x &&
                         hull.max.x <= room.bounds.max.x && hull.min.y >= room.bounds.min.y &&
                         hull.max.y <= room.bounds.max.y;
        for (const auto &solid : layout.solids) {
            const auto b = checked_brush_bounds(solid.geometry);
            require(!overlap(hull, b), "gameplay anchor intersects a solid with the full standing hull");
            supported |= b.max.z == hull.min.z && b.min.x <= hull.min.x && b.max.x >= hull.max.x &&
                         b.min.y <= hull.min.y && b.max.y >= hull.max.y;
        }
        require(supported, "gameplay anchor lacks a complete supported footprint");
    }
    ordinary_routes(layout);
    // Each spoke can close while the actual supported outer route still joins
    // every spawn and item; this is source hull evidence, not a native trial.
    for (const auto &connection : layout.connections) {
        if (connection.passage_volumes.front().min.z != floor_z)
            continue;
        if (connection.from_room != interior.front() && connection.to_room != interior.front())
            continue;
        auto closed = layout;
        std::erase_if(closed.connections, [&](const auto &other) { return other.id == connection.id; });
        ordinary_routes(closed);
    }
}

void candidate_contract() {
    for (const int rooms : {5, 6, 7, 8}) {
        for (const int verticality : {15, 90}) {
            auto request =
                PromptParser{}.parse("wrapped layout around a playable center",
                                     {Game::quake2, rooms % 2 ? GameMode::duel : GameMode::deathmatch, 503, rooms, 6});
            request.verticality = verticality / 100.0;
            const auto plan = make_rule_semantic_plan(request);
            const auto candidates = LayoutGenerator{}.generate_candidates(request, plan, 6);
            const auto repeat = LayoutGenerator{}.generate_candidates(request, plan, 6);
            std::set<std::string> room_geometry;
            require(candidates.size() == 6, "explicit intent changed candidate budget");
            for (std::size_t index = 0; index < candidates.size(); ++index) {
                const auto &candidate = candidates[index];
                wrapped_geometry(candidate);
                require(canonical_layout_sha256(candidate) == canonical_layout_sha256(repeat[index]),
                        "explicit candidate is not seeded deterministically");
                std::ostringstream coordinates;
                for (const auto &room : candidate.rooms)
                    coordinates << room.bounds.min.x << ',' << room.bounds.min.y << ',' << room.bounds.max.x << ','
                                << room.bounds.max.y << ';';
                room_geometry.insert(coordinates.str());
                if (verticality == 15)
                    require(
                        std::none_of(candidate.solids.begin(), candidate.solids.end(),
                                     [](const auto &solid) { return solid.purpose == BrushPurpose::raised_platform; }),
                        "explicit wrapping ignored low verticality");
                else {
                    int platforms = 0;
                    for (const auto &solid : candidate.solids)
                        if (solid.purpose == BrushPurpose::raised_platform) {
                            ++platforms;
                            const auto bounds = checked_brush_bounds(solid.geometry);
                            require((bounds.max.z == 128 || bounds.max.z == 192) && bounds.max.z - bounds.min.z == 24,
                                    "wrapped verticality lost its useful raised deck geometry");
                        }
                    require(platforms >= 2, "high verticality produced no useful raised routes");
                }
            }
            require(room_geometry.size() == 6, "constrained candidates repeat the same room geometry");
            const auto ranking = LayoutRanker{}.rank(request, candidates);
            require(ranking.selection_policy == "compact_q2_mp_adequacy_v1",
                    "intent changed the existing ranking policy");
            auto mixed = candidates;
            mixed.front().spec.layout_intent = LayoutIntent::unspecified;
            require(LayoutRanker{}.rank(request, mixed).selection_policy == "legacy_weighted_v1",
                    "ranker treated conflicting typed request intents as one coherent pool");

            // The exact source text remains present: builders must consume the
            // typed intent rather than searching it again deep in construction.
            request.layout_intent = LayoutIntent::unspecified;
            const auto defaults = LayoutGenerator{}.generate_candidates(request, make_rule_semantic_plan(request), 6);
            require(std::count_if(defaults.begin(), defaults.end(),
                                  [](const auto &candidate) {
                                      return tagged(candidate.rooms.front(), "compact_family_rows");
                                  }) == 3,
                    "unspecified request no longer retains the mixed-family default");
        }
    }
}

void pipeline_metadata() {
    const auto result = GenerationPipeline{}.run("Quake 2 DM with 6 rooms, wrapped layout, seed 503");
    require(result.validation.ok() && result.map_build.has_value(),
            "explicit intent did not complete the ordinary pipeline");
    const auto report = render_report(result);
    require(report.find("\"schema_version\": 4") != std::string::npos &&
                report.find("\"layout_intent\": \"wrapped_center\"") != std::string::npos,
            "explicit request is absent from report v4");
    const auto unspecified = GenerationPipeline{}.run("Quake 2 DM with 6 rooms, seed 503");
    require(render_report(unspecified).find("\"layout_intent\"") == std::string::npos,
            "unrequested report gained a new serialized field");
}
} // namespace

int main() try {
    parsing();
    semantic_and_fingerprint_contract();
    candidate_contract();
    pipeline_metadata();
    std::cout << "Typed intent parsing, rejection, semantic v1, legacy hashing, 48 constrained candidates, ordinary "
                 "routes and pipeline metadata passed.\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
