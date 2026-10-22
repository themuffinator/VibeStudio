#include "qmg/generator.hpp"
#include "qmg/layout_fingerprint.hpp"
#include "qmg/map_builder.hpp"
#include "qmg/navigation.hpp"
#include "qmg/upper_links.hpp"

#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace qmg;
namespace {

void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

bool overlap(const GridBounds &a, const GridBounds &b) {
    return a.min.x < b.max.x && b.min.x < a.max.x && a.min.y < b.max.y && b.min.y < a.max.y && a.min.z < b.max.z &&
           b.min.z < a.max.z;
}

void add_box(Layout &layout, GridBounds bounds, BrushPurpose purpose) {
    layout.solids.push_back({next_solid_id(layout),
                             bounds,
                             purpose == BrushPurpose::navigation_stair ? MaterialRole::stair : MaterialRole::platform,
                             purpose,
                             BrushOwnership::structural,
                             BrushContents::solid,
                             {}});
}

Layout fixture(int height = 128, bool y_axis = false, bool stacked = true) {
    Layout level;
    level.spec.game = Game::quake2;
    level.spec.mode = GameMode::deathmatch;
    level.spec.room_count = 5;
    level.spec.layout_intent = LayoutIntent::wrapped_center;
    level.planned_verticality = .75;
    level.candidate_seed = 607;
    const auto end = 64 + height / 16 * 36;
    for (int i = 0; i < 5; ++i) {
        const int x = i * 816;
        level.rooms.push_back({RoomId{static_cast<unsigned>(i + 1)},
                               "Fixture",
                               {{x, 0, 0}, {x + 768, 1024, 448}},
                               {"compact_mp", "compact_family_pinwheel"}});
        if (i)
            level.connections.push_back({next_connection_id(level),
                                         RoomId{static_cast<unsigned>(i)},
                                         RoomId{static_cast<unsigned>(i + 1)},
                                         160,
                                         192,
                                         {{{x - 48, stacked ? end : 800, 0}, {x, (stacked ? end : 800) + 160, 192}}}});
        if (i > 1)
            continue;
        add_box(level, {{x + 64, end, height - 24}, {x + 704, end + 160, height}}, BrushPurpose::raised_platform);
        for (int side : {64, 576})
            for (int step = 0; step < height / 16; ++step)
                add_box(level, {{x + side, 64 + 36 * step, 0}, {x + side + 128, 100 + 36 * step, 16 * (step + 1)}},
                        BrushPurpose::navigation_stair);
    }
    if (y_axis) {
        const auto turn = [](GridBounds &box) {
            std::swap(box.min.x, box.min.y);
            std::swap(box.max.x, box.max.y);
        };
        for (auto &room : level.rooms)
            turn(room.bounds);
        for (auto &connection : level.connections)
            for (auto &volume : connection.passage_volumes)
                turn(volume);
        for (auto &solid : level.solids)
            turn(std::get<GridBounds>(solid.geometry));
    }
    return level;
}

// Reconstruct the actual emitted world brushes, including the implicit shell.
// This exercises MapBuilder's union and differs from source passage-floor nodes.
std::vector<GridBounds> world_boxes(const Layout &layout) {
    MapBuildOptions options;
    options.dialect = MapDialect::quake2_valve220;
    const auto built = MapBuilder{}.build(layout, options);
    std::vector<GridBounds> result;
    for (const auto &entity : built.document.entities())
        for (const auto &primitive : entity.primitives) {
            const auto *brush = std::get_if<MapBrush>(&primitive);
            require(brush != nullptr, "box fixture unexpectedly emitted a patch");
            ConvexPolyhedron polyhedron;
            for (const auto &face : brush->faces)
                // MAP uses cross(p2-p0,p1-p0); Layout uses the opposite order.
                polyhedron.planes.push_back({snap_grid_vec3(face.plane_points[0]), snap_grid_vec3(face.plane_points[2]),
                                             snap_grid_vec3(face.plane_points[1])});
            result.push_back(checked_brush_bounds(polyhedron));
        }
    return result;
}

// Exact rectangle-union floor coverage over an entire axial hull sweep.
bool floor_union(const std::vector<GridBounds> &boxes, const GridBounds &body) {
    std::set<int> xs{body.min.x, body.max.x}, ys{body.min.y, body.max.y};
    std::vector<GridBounds> floors;
    for (const auto &box : boxes) {
        if (overlap(box, body))
            return false;
        if (box.max.z != body.min.z || box.min.x >= body.max.x || box.max.x <= body.min.x || box.min.y >= body.max.y ||
            box.max.y <= body.min.y)
            continue;
        floors.push_back(box);
        xs.insert(std::clamp(box.min.x, body.min.x, body.max.x));
        xs.insert(std::clamp(box.max.x, body.min.x, body.max.x));
        ys.insert(std::clamp(box.min.y, body.min.y, body.max.y));
        ys.insert(std::clamp(box.max.y, body.min.y, body.max.y));
    }
    const std::vector<int> x(xs.begin(), xs.end()), y(ys.begin(), ys.end());
    for (std::size_t i = 1; i < x.size(); ++i)
        for (std::size_t j = 1; j < y.size(); ++j)
            if (std::none_of(floors.begin(), floors.end(), [&](const auto &box) {
                    return box.min.x <= x[i - 1] && box.max.x >= x[i] && box.min.y <= y[j - 1] && box.max.y >= y[j];
                }))
                return false;
    return true;
}

GridBounds sweep(GridVec3 from, GridVec3 to) {
    require(from.z == to.z && (from.x == to.x || from.y == to.y), "expected a flat axial certificate");
    return {{std::min(from.x, to.x) - 16, std::min(from.y, to.y) - 16, from.z},
            {std::max(from.x, to.x) + 16, std::max(from.y, to.y) + 16, from.z + 56}};
}

bool upper_reaches(const Layout &level, const UpperLinkSelection &link) {
    const auto nav = NavigationAnalyzer{}.analyze(level);
    std::set<NavigationNodeId> reached;
    std::vector<NavigationNodeId> pending;
    for (const auto &node : nav.graph.nodes())
        if (node.source.kind == NavigationSourceKind::solid && node.source.id == link.from_deck.value &&
            node.area.floor_z == link.floor_z) {
            reached.insert(node.id);
            pending.push_back(node.id);
        }
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        const auto *node = nav.graph.find_node(id);
        if (node->source.kind == NavigationSourceKind::solid && node->source.id == link.to_deck.value)
            return true;
        for (const auto &edge : nav.graph.edges())
            if (edge.from == id &&
                (edge.kind == NavigationTransitionKind::walk || edge.kind == NavigationTransitionKind::step) &&
                nav.graph.find_node(edge.to)->area.floor_z == link.floor_z && reached.insert(edge.to).second)
                pending.push_back(edge.to);
    }
    return false;
}

void physical_certificate(const Layout &before, const Layout &after, const UpperLinkSelection &link) {
    require(after.rooms.size() == before.rooms.size() && after.connections.size() == before.connections.size() + 1 &&
                after.solids.size() == before.solids.size() + 3,
            "upper link replaced or unexpectedly expanded geometry");
    for (std::size_t i = 0; i < before.rooms.size(); ++i)
        require(after.rooms[i].bounds == before.rooms[i].bounds, "room envelope moved");
    for (std::size_t i = 0; i < before.connections.size(); ++i)
        require(after.connections[i].id == before.connections[i].id &&
                    after.connections[i].passage_volumes == before.connections[i].passage_volumes,
                "old lower connection changed");
    require(std::equal(before.solids.begin(), before.solids.end(), after.solids.begin()), "old motif solids changed");
    const auto world = world_boxes(after);
    require(floor_union(world, sweep(link.from_feet, link.to_feet)),
            "emitted upper route lacks full continuous hull support");
    require(upper_reaches(after, link), "new portal does not connect actual original upper decks");
    auto closed = after;
    auto closure = link.passage;
    closure.max.z = closure.min.z + 56;
    add_box(closed, closure, BrushPurpose::structural);
    // Deleting only the upper free-volume declaration is not a closure when an
    // overlapping lower opening still has64 units above a128-height deck.
    require(!upper_reaches(closed, link), "upper transfer survives physical closure of its upper opening");
    for (const auto &connection : before.connections) {
        const auto &volume = connection.passage_volumes.front();
        auto from = volume.center(), to = from;
        from.z = to.z = volume.min.z;
        if (volume.max.x - volume.min.x == 48) {
            from.x = volume.min.x - 16;
            to.x = volume.max.x + 16;
        } else {
            from.y = volume.min.y - 16;
            to.y = volume.max.y + 16;
        }
        require(floor_union(world, sweep(from, to)), "old lower portal standing path was lost");
    }
    for (const auto &solid : before.solids)
        if (solid.purpose == BrushPurpose::navigation_stair) {
            const auto box = checked_brush_bounds(solid.geometry);
            const GridBounds standing{{box.min.x, box.min.y, box.max.z}, {box.max.x, box.max.y, box.max.z + 56}};
            require(floor_union(world, standing), "old full-width tread support or standing clearance was lost");
        }
}

void axes_heights_stacked_and_offset() {
    for (const int height : {128, 192})
        for (const bool y : {false, true})
            for (const bool stacked : {false, true}) {
                auto level = fixture(height, y, stacked);
                const auto before = level;
                const auto result = add_compact_upper_link(level);
                require(result.applicable && result.considered_strips == 1 && result.feasible_strips == 1 &&
                            result.selected,
                        "exact160-wide compatible pair was not selected");
                require(result.selected->floor_z == height && result.selected->added_solids.size() == 3,
                        "wrong upper floor or missing explicit support");
                physical_certificate(before, level, *result.selected);
            }
}

void missing_portal_slab_is_not_a_navigation_certificate() {
    for (const int height : {128, 192}) {
        auto level = fixture(height);
        const auto result = add_compact_upper_link(level);
        require(result.selected.has_value(), "missing positive control");
        const auto cap = result.selected->added_solids.back();
        std::erase_if(level.solids, [&](const auto &solid) { return solid.id == cap; });
        require(upper_reaches(level, *result.selected), "negative control no longer exhibits passage-floor assumption");
        require(!floor_union(world_boxes(level), sweep(result.selected->from_feet, result.selected->to_feet)),
                "missing upper slab was hidden by an implicit floor across a lower volume");
    }
}

void blocked_stair_or_deck_is_rejected() {
    auto level = fixture();
    // This otherwise valid tread occupies the prospective left branch and has
    // its top below the new upper floor: the slab would obstruct standing there.
    add_box(level, {{704, 368, 0}, {740, 496, 80}}, BrushPurpose::navigation_stair);
    const auto hash = canonical_layout_sha256(level);
    const auto result = add_compact_upper_link(level);
    require(result.considered_strips == 1 && !result.selected && canonical_layout_sha256(level) == hash,
            "stair interference was accepted or mutated no-feasible geometry");
    level = fixture();
    add_box(level, {{720, 352, 128}, {736, 512, 240}}, BrushPurpose::structural);
    require(!add_compact_upper_link(level).selected, "standing route obstruction was accepted");
}

void ordinary_access_is_required() {
    auto level = fixture();
    std::erase_if(level.solids, [](const auto &solid) {
        return solid.purpose == BrushPurpose::navigation_stair && checked_brush_bounds(solid.geometry).min.x < 768;
    });
    const auto hash = canonical_layout_sha256(level);
    require(!add_compact_upper_link(level).selected && canonical_layout_sha256(level) == hash,
            "isolated upper balcony counted as an ordinary access");
}

void step_raise_before_advance_is_protected() {
    auto level = fixture();
    std::get<GridBounds>(level.solids.front().geometry).max.x = 576;
    std::erase_if(level.solids, [](const auto &solid) {
        return solid.purpose == BrushPurpose::navigation_stair && checked_brush_bounds(solid.geometry).min.x == 576;
    });
    const GridBounds previous{{600, 352, 0}, {728, 388, 48}};
    const GridBounds next{{600, 316, 0}, {728, 352, 64}};
    add_box(level, previous, BrushPurpose::navigation_stair);
    add_box(level, next, BrushPurpose::navigation_stair);
    const auto world = world_boxes(level);
    for (const auto &tread : {previous, next}) {
        const GridBounds body{{tread.min.x, tread.min.y, tread.max.z}, {tread.max.x, tread.max.y, tread.max.z + 56}};
        require(floor_union(world, body), "step-sweep negative control already has blocked standing treads");
    }
    const auto before = canonical_layout_sha256(level);
    const auto result = add_compact_upper_link(level);
    require(result.considered_strips == 1 && !result.selected && canonical_layout_sha256(level) == before,
            "standing-only check admitted a slab across the48-to64 raise-before-advance sweep");
}

void isolated_endpoint_fragment_is_rejected() {
    auto level = fixture();
    std::erase_if(level.solids, [](const auto &solid) {
        return solid.purpose == BrushPurpose::navigation_stair && checked_brush_bounds(solid.geometry).min.x == 576;
    });
    // The left deck fragment still has stairs. The proposed right-edge endpoint
    // is separated from them by a wall above the deck and has no local return.
    add_box(level, {{512, 352, 128}, {528, 512, 256}}, BrushPurpose::structural);
    const auto result = add_compact_upper_link(level);
    require(result.considered_strips == 1 && !result.selected,
            "an accessible fragment elsewhere on the same deck certified the isolated endpoint");
}

void emitted_shell_clearance_and_bounded_prefix() {
    for (const int gap : {16, 32}) {
        auto level = fixture(128, false, false);
        auto &lower = level.connections.front().passage_volumes.front();
        lower.min.y = 512 + gap;
        lower.max.y = lower.min.y + 160;
        const auto before = level;
        const auto result = add_compact_upper_link(level);
        require(result.feasible_strips == 1 && result.lowering_attempts == 1,
                "shell control lost its one source-clear strip");
        if (gap == 16)
            require(result.lowering_rejections == 1 && !result.selected && !result.lowering_cap_reached &&
                        canonical_layout_sha256(level) == canonical_layout_sha256(before),
                    "sub32-unit wall strip was accepted or mutated the rejected construction");
        else {
            require(result.lowering_rejections == 0 && result.selected, "exact32-unit shell clearance was rejected");
            physical_certificate(before, level, *result.selected);
        }
    }
    auto repeated = fixture(128, false, false);
    repeated.connections.front().passage_volumes.front().min.y = 528;
    repeated.connections.front().passage_volumes.front().max.y = 688;
    std::vector<GridBounds> decks;
    for (const auto &solid : repeated.solids)
        if (solid.purpose == BrushPurpose::raised_platform)
            decks.push_back(*get_box(solid.geometry));
    // Redundant but valid all-solid authored supports generate81 source-clear
    // pairs with the same bad shell strip. They must not expand lowering work.
    for (int duplicate = 0; duplicate < 8; ++duplicate)
        for (const auto &box : decks)
            add_box(repeated, box, BrushPurpose::raised_platform);
    const auto before = canonical_layout_sha256(repeated);
    const auto result = add_compact_upper_link(repeated);
    require(result.feasible_strips == 81 && result.lowering_attempts == 64 && result.lowering_rejections == 64 &&
                result.lowering_cap_reached && !result.selected && canonical_layout_sha256(repeated) == before,
            "bounded lowering prefix expanded, mutated geometry or hid exhaustion");
}

template <typename Mutate> void rejects_unchanged(Mutate mutate) {
    auto level = fixture();
    mutate(level);
    const auto before = level;
    bool rejected = false;
    try {
        (void)add_compact_upper_link(level);
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected && level.solids == before.solids && level.placements == before.placements &&
                level.connections.size() == before.connections.size() && level.rooms.size() == before.rooms.size(),
            "invalid public input was accepted or partially mutated");
}

void public_guards_and_scope() {
    rejects_unchanged([](auto &l) { l.spec.layout_intent = static_cast<LayoutIntent>(255); });
    rejects_unchanged([](auto &l) { l.spec.room_count = 6; });
    rejects_unchanged([](auto &l) { l.rooms[0].tags.clear(); });
    rejects_unchanged([](auto &l) { l.rooms[0].bounds.min.z = -16; });
    rejects_unchanged([](auto &l) { l.connections[0].passage_volumes[0].min.z = -16; });
    rejects_unchanged([](auto &l) { l.solids[0].contents = BrushContents::water; });
    rejects_unchanged([](auto &l) { l.solids[0].material = MaterialRole::trigger; });
    rejects_unchanged([](auto &l) { l.solids[0].ownership = BrushOwnership::placement; });
    rejects_unchanged([](auto &l) { l.solids[0].owner = PlacementId{1}; });
    rejects_unchanged([](auto &l) { l.solids[0].geometry = GridBounds{{64, 352, 24}, {704, 512, 48}}; });
    rejects_unchanged([](auto &l) { std::get<GridBounds>(l.solids[0].geometry).min.z = 96; });
    rejects_unchanged([](auto &l) { l.solids[1].id = l.solids[0].id; });
    rejects_unchanged([](auto &l) { l.solids[0].geometry = ConvexPolyhedron{}; });
    rejects_unchanged([](auto &l) { l.solids[0].geometry = GridBounds{{700, 352, 104}, {900, 512, 128}}; });
    rejects_unchanged([](auto &l) { l.placements.push_back({}); });
    rejects_unchanged([](auto &l) {
        while (l.solids.size() <= 512)
            l.solids.push_back(l.solids.front());
    });
    for (const auto game : {Game::quake1, Game::quake2, Game::quake3}) {
        auto level = fixture();
        level.spec.game = game;
        level.spec.layout_intent = LayoutIntent::unspecified;
        const auto hash = canonical_layout_sha256(level);
        require(!add_compact_upper_link(level).applicable && canonical_layout_sha256(level) == hash,
                "unrequested/legacy construction changed");
    }
    auto low = fixture();
    low.planned_verticality = .29;
    const auto hash = canonical_layout_sha256(low);
    require(!add_compact_upper_link(low).applicable && canonical_layout_sha256(low) == hash,
            "low verticality silently gained upper circulation");
}

void seed_and_iteration_order() {
    auto before = fixture();
    // Wider first pair offers multiple feasible strips without changing access.
    for (auto &solid : before.solids)
        if (solid.purpose == BrushPurpose::raised_platform)
            std::get<GridBounds>(solid.geometry).max.y += 128;
    std::set<GridBounds> choices;
    for (std::uint64_t seed : {17U, 42U, 73U, 99U, 503U, 607U}) {
        auto a = before, b = before;
        a.candidate_seed = b.candidate_seed = seed;
        std::reverse(b.rooms.begin(), b.rooms.end());
        std::reverse(b.solids.begin(), b.solids.end());
        std::reverse(b.connections.begin(), b.connections.end());
        const auto one = add_compact_upper_link(a), two = add_compact_upper_link(b);
        require(one == two && one.selected && one.feasible_strips > 1,
                "selection depends on iteration order or failed deterministic repeat");
        choices.insert(one.selected->passage);
    }
    require(choices.size() > 1, "seeded selection has no geometric effect on eligible strips");
}

void translated_grid_phase_and_narrow_span() {
    for (const int shift : {-8, 8})
        for (const bool y_axis : {false, true})
            for (const int floor : {0, 64}) {
                auto level = fixture(128, y_axis);
                const auto translate = [&](GridBounds &box) {
                    box.min.x += shift;
                    box.max.x += shift;
                    box.min.y += shift;
                    box.max.y += shift;
                    box.min.z += floor;
                    box.max.z += floor;
                };
                for (auto &room : level.rooms)
                    translate(room.bounds);
                for (auto &connection : level.connections)
                    for (auto &volume : connection.passage_volumes)
                        translate(volume);
                for (auto &solid : level.solids)
                    translate(std::get<GridBounds>(solid.geometry));
                const auto before = level;
                const auto result = add_compact_upper_link(level);
                require(result.considered_strips == 1 && result.feasible_strips == 1 && result.selected &&
                            result.selected->floor_z == floor + 128,
                        "global zero-phase snapping discarded an exact160-wide translated deck interval");
                physical_certificate(before, level, *result.selected);
            }
    auto narrow = fixture();
    for (auto &solid : narrow.solids)
        if (solid.purpose == BrushPurpose::raised_platform && checked_brush_bounds(solid.geometry).min.x > 768)
            --std::get<GridBounds>(solid.geometry).max.y;
    const auto before = canonical_layout_sha256(narrow);
    const auto result = add_compact_upper_link(narrow);
    require(result.considered_strips == 0 && !result.selected && canonical_layout_sha256(narrow) == before,
            "boundary anchoring widened a159-unit overlap to fit the160-unit portal");
}

void generated_fixed_sweep() {
    std::size_t selected = 0, absent = 0, attempts = 0, rejections = 0, capped = 0;
    std::set<std::pair<RoomId, RoomId>> pairs;
    for (int rooms : {5, 6, 7, 8})
        for (int verticality : {30, 75, 90})
            for (std::uint64_t seed : {17U, 73U, 503U, 607U}) {
                PromptSpec request;
                request.source_prompt = "industrial connected loops";
                request.game = Game::quake2;
                request.mode = rooms % 2 ? GameMode::duel : GameMode::deathmatch;
                request.target_players = rooms % 2 ? 2 : 6;
                request.room_count = rooms;
                request.seed = seed;
                request.prefer_loops = true;
                request.verticality = verticality / 100.0;
                auto plan = make_rule_semantic_plan(request);
                plan.symmetry = Symmetry::none;
                plan.verticality = verticality;
                // Odd-seed unspecified requests retain exactly the existing
                // pinwheel/motif RNG, without invoking the new explicit hook.
                auto level = LayoutGenerator{}.generate_candidates(request, plan, 1).front();
                level.placements.clear();
                level.spec.layout_intent = LayoutIntent::wrapped_center;
                const auto before = level;
                auto repeat = before;
                const auto result = add_compact_upper_link(level), again = add_compact_upper_link(repeat);
                attempts += result.lowering_attempts;
                rejections += result.lowering_rejections;
                capped += result.lowering_cap_reached;
                if (rooms == 5 && verticality == 30 && seed == 73)
                    require(result.lowering_rejections > 0,
                            "retained generated shell-clearance counterexample disappeared");
                require(result == again && canonical_layout_sha256(level) == canonical_layout_sha256(repeat),
                        "fixed generated search is not deterministic");
                if (!result.selected) {
                    ++absent;
                    require(canonical_layout_sha256(level) == canonical_layout_sha256(before),
                            "no-feasible observation changed the original candidate");
                    continue;
                }
                ++selected;
                pairs.emplace(result.selected->from_room, result.selected->to_room);
                try {
                    physical_certificate(before, level, *result.selected);
                } catch (const std::exception &error) {
                    throw std::runtime_error("generated rooms=" + std::to_string(rooms) +
                                             " verticality=" + std::to_string(verticality) +
                                             " seed=" + std::to_string(seed) + ": " + error.what());
                }
            }
    require(selected >= 16 && pairs.size() > 3, "bounded generated sweep lacks meaningful feasible variety");
    std::cout << "Fixed48 candidate sweep: " << selected << " upper links, " << absent
              << " no-compatible-pair observations; " << attempts << " lowering attempts, " << rejections
              << " shell rejections, " << capped << " capped searches\n";
}

} // namespace

int main() {
    try {
        axes_heights_stacked_and_offset();
        missing_portal_slab_is_not_a_navigation_certificate();
        blocked_stair_or_deck_is_rejected();
        ordinary_access_is_required();
        step_raise_before_advance_is_protected();
        isolated_endpoint_fragment_is_rejected();
        emitted_shell_clearance_and_bounded_prefix();
        public_guards_and_scope();
        seed_and_iteration_order();
        translated_grid_phase_and_narrow_span();
        generated_fixed_sweep();
        std::cout << "11 upper-link geometry, emitted-shell, ordinary-route and scope control groups passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
