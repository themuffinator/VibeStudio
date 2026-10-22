#include "qmg/generator.hpp"
#include "qmg/layout_fingerprint.hpp"
#include "qmg/navigation.hpp"
#include "qmg/prompt.hpp"
#include "qmg/validator.hpp"
#include "qmg/visibility.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
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
bool tag(const Room &r, const std::string &name) {
    return std::find(r.tags.begin(), r.tags.end(), name) != r.tags.end();
}
bool overlap(const GridBounds &a, const GridBounds &b) {
    return a.min.x < b.max.x && a.max.x > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y && a.min.z < b.max.z &&
           a.max.z > b.min.z;
}
bool anchor(const Placement &p) {
    return p.role == PlacementRole::deathmatch_spawn || p.role == PlacementRole::weapon ||
           p.role == PlacementRole::armor || p.role == PlacementRole::health;
}

std::set<NavigationNodeId> ordinary(const NavigationAnalysis &nav, NavigationNodeId start) {
    std::set<NavigationNodeId> seen{start};
    std::vector<NavigationNodeId> pending{start};
    while (!pending.empty()) {
        auto id = pending.back();
        pending.pop_back();
        for (const auto &edge : nav.graph.edges())
            if (edge.from == id &&
                (edge.kind == NavigationTransitionKind::walk || edge.kind == NavigationTransitionKind::step ||
                 edge.kind == NavigationTransitionKind::door) &&
                seen.insert(edge.to).second)
                pending.push_back(edge.to);
    }
    return seen;
}

void verify_navigation(const Layout &level) {
    const auto nav = NavigationAnalyzer{}.analyze(level);
    require(nav.ok(), "native-sized source navigation failed");
    for (const auto &p : level.placements)
        if (anchor(p)) {
            const auto *binding = nav.find_binding(p.id);
            require(binding && binding->node.has_value(), "gameplay point lacks support binding");
            if (p.role != PlacementRole::deathmatch_spawn)
                continue;
            const auto reached = ordinary(nav, *binding->node);
            for (const auto &target : level.placements)
                if (anchor(target)) {
                    const auto *finish = nav.find_binding(target.id);
                    require(finish && finish->node && reached.contains(*finish->node),
                            "required ordinary gameplay route failed");
                }
        }
}

void verify_geometry(const Layout &level) {
    const auto validation = LayoutValidator{}.validate(level);
    if (!validation.ok()) {
        for (const auto &issue : validation.issues)
            std::cerr << issue.code << ":" << issue.message << "\n";
        throw std::runtime_error("compact layout structural validation failed");
    }
    const auto min_x = std::min_element(level.rooms.begin(), level.rooms.end(), [](auto &a, auto &b) {
                           return a.bounds.min.x < b.bounds.min.x;
                       })->bounds.min.x;
    const auto max_x = std::max_element(level.rooms.begin(), level.rooms.end(), [](auto &a, auto &b) {
                           return a.bounds.max.x < b.bounds.max.x;
                       })->bounds.max.x;
    const auto min_y = std::min_element(level.rooms.begin(), level.rooms.end(), [](auto &a, auto &b) {
                           return a.bounds.min.y < b.bounds.min.y;
                       })->bounds.min.y;
    const auto max_y = std::max_element(level.rooms.begin(), level.rooms.end(), [](auto &a, auto &b) {
                           return a.bounds.max.y < b.bounds.max.y;
                       })->bounds.max.y;
    long long room_area = 0;
    for (const auto &r : level.rooms) {
        require(r.bounds.max.x - r.bounds.min.x >= 640 && r.bounds.max.y - r.bounds.min.y >= 640,
                "room footprint cannot hold every motif rotation");
        room_area += static_cast<long long>(r.bounds.max.x - r.bounds.min.x) * (r.bounds.max.y - r.bounds.min.y);
    }
    require(room_area * 100 > static_cast<long long>(max_x - min_x) * (max_y - min_y) * 85,
            "gross room packing below construction contract");
    for (const auto &c : level.connections) {
        require(c.passage_volumes.size() == 1, "compact join should be one face seam");
        const auto b = c.passage_volumes.front();
        require((b.max.x - b.min.x == 48 && b.max.y - b.min.y == 160) ||
                    (b.max.y - b.min.y == 48 && b.max.x - b.min.x == 160),
                "passage is not the short face seam");
    }
    require(level.connections.size() >= (level.rooms.size() == 2 ? 2 : level.rooms.size() + 1),
            "ordinary room graph lacks multiple independent cycles");
    for (const auto &p : level.placements)
        if (anchor(p)) {
            const GridBounds hull{{p.origin.x - 16, p.origin.y - 16, p.origin.z - 24},
                                  {p.origin.x + 16, p.origin.y + 16, p.origin.z + 32}};
            bool support = p.origin.z == 24;
            for (const auto &s : level.solids) {
                const auto b = checked_brush_bounds(s.geometry);
                require(!overlap(hull, b), "allocated standing hull intersects actual solid");
                support |= b.max.z == hull.min.z && b.min.x <= hull.min.x && b.max.x >= hull.max.x &&
                           b.min.y <= hull.min.y && b.max.y >= hull.max.y;
            }
            require(support, "upper item lacks complete footprint support");
        }
}

bool has_full_slicing_gap(const Layout &level) {
    for (bool x_axis : {false, true}) {
        std::vector<std::pair<int, int>> projected;
        for (const auto &room : level.rooms)
            projected.push_back(x_axis ? std::pair{room.bounds.min.x, room.bounds.max.x}
                                       : std::pair{room.bounds.min.y, room.bounds.max.y});
        std::sort(projected.begin(), projected.end());
        int covered = projected.front().second;
        for (const auto &[low, high] : projected) {
            if (low > covered)
                return true;
            covered = std::max(covered, high);
        }
    }
    return false;
}

void verify_pinwheel(const Layout &level) {
    require(!has_full_slicing_gap(level), "pinwheel has reverted to a slicing row/column partition");
    require(level.connections.size() == level.rooms.size() + 3, "pinwheel lost its center/rim topology");
    // Independently recover the unique interior room from actual envelope faces.
    int min_x = 32768, min_y = 32768, max_x = -32768, max_y = -32768;
    for (const auto &room : level.rooms) {
        min_x = std::min(min_x, room.bounds.min.x);
        min_y = std::min(min_y, room.bounds.min.y);
        max_x = std::max(max_x, room.bounds.max.x);
        max_y = std::max(max_y, room.bounds.max.y);
    }
    std::vector<RoomId> inside;
    for (const auto &room : level.rooms)
        if (room.bounds.min.x > min_x && room.bounds.min.y > min_y && room.bounds.max.x < max_x &&
            room.bounds.max.y < max_y)
            inside.push_back(room.id);
    require(inside.size() == 1, "pinwheel requires exactly one playable interior room");
    const auto center = inside.front();
    std::map<RoomId, std::set<RoomId>> rim;
    int spokes = 0;
    for (const auto &connection : level.connections) {
        if (connection.from_room == center || connection.to_room == center) {
            ++spokes;
        } else {
            rim[connection.from_room].insert(connection.to_room);
            rim[connection.to_room].insert(connection.from_room);
        }
    }
    require(spokes == 4 && rim.size() == level.rooms.size() - 1, "center/rim access lost a side");
    std::set<RoomId> reached;
    std::vector<RoomId> pending{rim.begin()->first};
    while (!pending.empty()) {
        const auto next = pending.back();
        pending.pop_back();
        if (!reached.insert(next).second)
            continue;
        require(rim.at(next).size() == 2, "outer bypass is not a distinct ordinary cycle");
        for (auto neighbor : rim.at(next))
            pending.push_back(neighbor);
    }
    require(reached.size() == rim.size(), "outer bypass is split into disconnected cycles");
    // Closing each actual face passage still leaves all required ordinary routes.
    for (const auto &connection : level.connections) {
        auto closed = level;
        std::erase_if(closed.connections, [&](const auto &other) { return connection.id == other.id; });
        verify_navigation(closed);
    }
}

void two_accesses(const Layout &level) {
    for (const auto &room : level.rooms)
        if (tag(room, "raised_crosswalk") || tag(room, "horseshoe_gallery") || tag(room, "opposed_stair_bridge")) {
            // Infer independent stair runs from physical span/axis, not a tag count.
            std::map<std::tuple<int, int, int>, std::vector<SolidId>> groups;
            for (const auto &s : level.solids)
                if (s.purpose == BrushPurpose::navigation_stair) {
                    const auto b = checked_brush_bounds(s.geometry);
                    if (!room.bounds.contains(b.center()))
                        continue;
                    const bool x = b.max.x - b.min.x == 36;
                    require((x && b.max.y - b.min.y == 128) ||
                                (!x && b.max.y - b.min.y == 36 && b.max.x - b.min.x == 128),
                            "tread physical footprint changed");
                    groups[{x ? 0 : 1, x ? b.min.y : b.min.x, x ? b.max.y : b.max.x}].push_back(s.id);
                }
            require(groups.size() == 2, "elevated motif needs two physically separate stair runs");
            for (const auto &[key, ids] : groups) {
                require(ids.size() == 8 || ids.size() == 12, "access needs full128or192rise");
                auto changed = level;
                std::erase_if(changed.solids,
                              [&](const auto &s) { return std::find(ids.begin(), ids.end(), s.id) != ids.end(); });
                verify_navigation(changed);
            }
            if (tag(room, "raised_crosswalk")) {
                const auto found = std::find_if(level.solids.begin(), level.solids.end(), [&](const auto &s) {
                    return s.purpose == BrushPurpose::raised_platform &&
                           room.bounds.contains(checked_brush_bounds(s.geometry).center());
                });
                require(found != level.solids.end(), "crosswalk needs physical deck");
                const auto b = checked_brush_bounds(found->geometry);
                const bool wide_x = b.max.x - b.min.x > b.max.y - b.min.y;
                const auto c = b.center();
                GridBounds lower =
                    wide_x ? GridBounds{{c.x - 16, room.bounds.min.y + 32, 0}, {c.x + 16, room.bounds.max.y - 32, 56}}
                           : GridBounds{{room.bounds.min.x + 32, c.y - 16, 0}, {room.bounds.max.x - 32, c.y + 16, 56}};
                for (const auto &s : level.solids)
                    require(!overlap(lower, checked_brush_bounds(s.geometry)),
                            "crosswalk blocks full continuous lower route");
                require(b.min.z >= 104 && b.max.z - b.min.z == 24, "crosswalk headroom/deck contract changed");
            }
        }
}

Layout generate(int rooms, unsigned seed, int verticality = 90, Game game = Game::quake2,
                GameMode mode = GameMode::deathmatch) {
    auto spec = PromptParser{}.parse("industrial connected loops", {game, mode, seed, rooms, 6});
    spec.verticality = verticality / 100.0;
    auto plan = make_rule_semantic_plan(spec);
    plan.verticality = verticality;
    return LayoutGenerator{}.generate_candidates(spec, plan, 1).front();
}
} // namespace

int main() try {
    std::set<std::string> fingerprints, rotation_tags;
    std::set<int> elevations;
    for (int rooms : {4, 6, 8})
        for (unsigned seed : {17U, 42U, 99U}) {
            auto level = generate(rooms, seed);
            verify_geometry(level);
            verify_navigation(level);
            two_accesses(level);
            require(canonical_layout_sha256(level) == canonical_layout_sha256(generate(rooms, seed)),
                    "same seed is not deterministic");
            fingerprints.insert(canonical_layout_sha256(level));
            for (const auto &room : level.rooms)
                for (const auto &t : room.tags)
                    if (t.starts_with("motif_rotation_"))
                        rotation_tags.insert(t);
            for (const auto &s : level.solids)
                if (s.purpose == BrushPurpose::raised_platform)
                    elevations.insert(checked_brush_bounds(s.geometry).max.z);
            require(std::any_of(level.rooms.begin(), level.rooms.end(),
                                [](const auto &r) { return tag(r, "raised_crosswalk"); }) &&
                        std::any_of(level.rooms.begin(), level.rooms.end(),
                                    [](const auto &r) { return tag(r, "horseshoe_gallery"); }) &&
                        std::any_of(level.rooms.begin(), level.rooms.end(),
                                    [](const auto &r) { return tag(r, "opposed_stair_bridge"); }),
                    "portfolio lacks all three reusable motifs");
        }
    require(fingerprints.size() == 9 && rotation_tags.size() >= 3 && elevations == std::set<int>{128, 192},
            "fixed portfolio lacks seeded geometry variation");
    for (int rooms : {5, 6, 7, 8})
        for (int verticality : {15, 90}) {
            auto spec =
                PromptParser{}.parse("industrial connected loops", {Game::quake2, GameMode::deathmatch, 73, rooms, 6});
            spec.verticality = verticality / 100.0;
            auto plan = make_rule_semantic_plan(spec);
            plan.verticality = verticality;
            const auto candidates = LayoutGenerator{}.generate_candidates(spec, plan, 6);
            int rows_count = 0, pinwheel_count = 0;
            std::set<std::size_t> row_edge_counts, pinwheel_edge_counts;
            for (const auto &level : candidates) {
                verify_geometry(level);
                verify_navigation(level);
                if (tag(level.rooms.front(), "compact_family_pinwheel")) {
                    ++pinwheel_count;
                    verify_pinwheel(level);
                    two_accesses(level);
                    pinwheel_edge_counts.insert(level.connections.size());
                } else {
                    ++rows_count;
                    require(tag(level.rooms.front(), "compact_family_rows") && has_full_slicing_gap(level),
                            "row family changed its physical partition contract");
                    row_edge_counts.insert(level.connections.size());
                }
                if (verticality < 30)
                    require(
                        std::none_of(level.solids.begin(), level.solids.end(),
                                     [](const auto &solid) { return solid.purpose == BrushPurpose::raised_platform; }),
                        "low verticality received an upper motif in a candidate family");
            }
            require(rows_count == 3 && pinwheel_count == 3, "six-candidate batch does not offer both families equally");
            if (rooms == 6 || rooms == 8)
                require(row_edge_counts != pinwheel_edge_counts,
                        "family change lacks an actual room-graph invariant difference");
        }
    // Exact predeclared portfolio prompts also exercise positive duel behavior.
    for (int rooms : {4, 6, 8})
        for (unsigned seed : {17U, 42U, 99U}) {
            const auto mode = rooms == 4 ? "duel" : "deathmatch";
            const int players = rooms == 4 ? 2 : 6;
            const auto prompt = "Quake 2 " + std::string(mode) + " industrial arena for " + std::to_string(players) +
                                " players with " + std::to_string(rooms) +
                                " rooms, vertical combat, tight connected loops, seed " + std::to_string(seed);
            const auto spec = PromptParser{}.parse(prompt);
            const auto plan = make_rule_semantic_plan(spec);
            const auto level = LayoutGenerator{}.generate_candidates(spec, plan, 1).front();
            require(level.spec.mode == (rooms == 4 ? GameMode::duel : GameMode::deathmatch),
                    "portfolio prompt lost mode");
            verify_geometry(level);
            verify_navigation(level);
            const auto visibility = VisibilityAnalyzer{}.analyze(level);
            for (const auto &spawn : visibility.spawn_safety)
                require(spawn.visible_hostile_origins == 0, "fixed source fixture retains direct spawn eye exposure");
        }
    for (int rooms : {2, 3, 31, 32})
        for (int verticality : {15, 90}) {
            auto level = generate(rooms, 73, verticality);
            require(static_cast<int>(level.rooms.size()) == rooms, "room count contract changed");
            verify_geometry(level);
            verify_navigation(level);
            if (verticality < 30)
                require(std::none_of(level.solids.begin(), level.solids.end(),
                                     [](auto &s) { return s.purpose == BrushPurpose::raised_platform; }),
                        "low verticality received upper motif");
        }
    for (auto [game, mode] :
         {std::pair{Game::quake1, GameMode::deathmatch}, std::pair{Game::quake3, GameMode::deathmatch},
          std::pair{Game::quake2, GameMode::single_player}, std::pair{Game::quake2, GameMode::capture_the_flag}}) {
        auto level = generate(6, 17, 90, game, mode);
        require(std::none_of(level.rooms.begin(), level.rooms.end(), [](auto &r) { return tag(r, "compact_mp"); }),
                "unrelated mode entered new generator");
    }
    std::cout << "Seeded compact MP geometry, ordinary paths, independent access, bounds, variety and legacy mode "
                 "scope passed.\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
