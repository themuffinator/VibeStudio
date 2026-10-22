#include "qmg/generator.hpp"
#include "qmg/layout_fingerprint.hpp"
#include "qmg/map_builder.hpp"
#include "qmg/mp_motifs.hpp"
#include "qmg/navigation.hpp"
#include "qmg/prompt.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace qmg;
namespace {

void require(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

bool overlap(const GridBounds &a, const GridBounds &b) {
    return a.min.x < b.max.x && b.min.x < a.max.x && a.min.y < b.max.y && b.min.y < a.max.y &&
           a.min.z < b.max.z && b.min.z < a.max.z;
}

// Independent local-point transform for the declared quarter-turn convention.
GridVec3 point(GridBounds room, int rotation, int u, int v, int z) {
    const std::array<GridVec3, 4> points{{{room.min.x + u, room.min.y + v, z},
                                       {room.max.x - v, room.min.y + u, z},
                                       {room.max.x - u, room.max.y - v, z},
                                       {room.min.x + v, room.max.y - u, z}}};
    return points.at(static_cast<std::size_t>(rotation));
}

GridBounds sweep(GridVec3 a, GridVec3 b) {
    require(a.z == b.z && (a.x == b.x || a.y == b.y), "Expected a flat axial segment");
    return {{std::min(a.x, b.x) - 16, std::min(a.y, b.y) - 16, a.z},
            {std::max(a.x, b.x) + 16, std::max(a.y, b.y) + 16, a.z + 56}};
}

void add_box(Layout &level, GridBounds box, BrushPurpose purpose) {
    level.solids.push_back({next_solid_id(level), box,
                            purpose == BrushPurpose::navigation_stair ? MaterialRole::stair : MaterialRole::platform,
                            purpose, BrushOwnership::structural, BrushContents::solid, {}});
}

void add_point(Layout &level, GridVec3 feet, PlacementRole role) {
    Placement p;
    p.id = next_placement_id(level);
    p.role = role;
    p.origin = {feet.x, feet.y, feet.z + 24};
    level.placements.push_back(p);
}

Layout fixture(GridBounds bounds, int rotation, int height) {
    Layout level;
    level.spec.game = Game::quake2;
    level.spec.mode = GameMode::deathmatch;
    level.spec.theme = "industrial";
    level.rooms.push_back({RoomId{1}, "Opposed bridge fixture", bounds, {"opposed_stair_bridge"}});
    const auto geometry = make_opposed_bridge_geometry(bounds, rotation, height);
    for (const auto &box : geometry.decks)
        add_box(level, box, BrushPurpose::raised_platform);
    for (const auto &run : geometry.stairs)
        for (const auto &box : run)
            add_box(level, box, BrushPurpose::navigation_stair);
    const int w = rotation % 2 ? bounds.max.y - bounds.min.y : bounds.max.x - bounds.min.x;
    const int d = rotation % 2 ? bounds.max.x - bounds.min.x : bounds.max.y - bounds.min.y;
    const int end = 64 + height / 16 * 36;
    add_point(level, point(bounds, rotation, w / 2, 32, 0), PlacementRole::deathmatch_spawn);
    add_point(level, point(bounds, rotation, w / 2, d - 32, 0), PlacementRole::deathmatch_spawn);
    add_point(level, point(bounds, rotation, 128, end + 80, height), PlacementRole::weapon);
    add_point(level, point(bounds, rotation, w / 2, end + 80, height), PlacementRole::armor);
    add_point(level, point(bounds, rotation, w - 128, d - end - 80, height), PlacementRole::weapon);
    return level;
}

std::vector<GridBounds> emitted_world(const Layout &level) {
    MapBuildOptions options;
    options.dialect = MapDialect::quake2_valve220;
    const auto built = MapBuilder{}.build(level, options);
    std::vector<GridBounds> result;
    for (const auto &entity : built.document.entities())
        for (const auto &primitive : entity.primitives) {
            const auto *brush = std::get_if<MapBrush>(&primitive);
            require(brush != nullptr, "Unexpected non-brush primitive");
            ConvexPolyhedron polyhedron;
            for (const auto &face : brush->faces)
                polyhedron.planes.push_back({snap_grid_vec3(face.plane_points[0]), snap_grid_vec3(face.plane_points[2]),
                                             snap_grid_vec3(face.plane_points[1])});
            result.push_back(checked_brush_bounds(polyhedron));
        }
    return result;
}

// Exact rectangular partition of the whole swept footprint, including butt joins.
bool supported(const std::vector<GridBounds> &world, GridBounds body) {
    std::set<int> xs{body.min.x, body.max.x}, ys{body.min.y, body.max.y};
    std::vector<GridBounds> floors;
    for (const auto &box : world) {
        if (overlap(box, body))
            return false;
        if (box.max.z != body.min.z || box.min.x >= body.max.x || box.max.x <= body.min.x ||
            box.min.y >= body.max.y || box.max.y <= body.min.y)
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
                    return box.min.x <= x[i - 1] && box.max.x >= x[i] &&
                           box.min.y <= y[j - 1] && box.max.y >= y[j];
                }))
                return false;
    return true;
}

bool all_ordinary_anchors(const Layout &level) {
    const auto nav = NavigationAnalyzer{}.analyze(level);
    for (const auto &spawn : level.placements) {
        if (spawn.role != PlacementRole::deathmatch_spawn)
            continue;
        const auto *start = nav.find_binding(spawn.id);
        if (!start || !start->node)
            return false;
        std::set<NavigationNodeId> seen{*start->node};
        std::vector<NavigationNodeId> pending{*start->node};
        while (!pending.empty()) {
            const auto id = pending.back();
            pending.pop_back();
            for (const auto &edge : nav.graph.edges())
                if (edge.from == id && (edge.kind == NavigationTransitionKind::walk ||
                                        edge.kind == NavigationTransitionKind::step ||
                                        edge.kind == NavigationTransitionKind::door) && seen.insert(edge.to).second)
                    pending.push_back(edge.to);
        }
        for (const auto &target : level.placements) {
            if (target.role == PlacementRole::light)
                continue;
            const auto *finish = nav.find_binding(target.id);
            if (!finish || !finish->node || !seen.contains(*finish->node))
                return false;
        }
    }
    return true;
}

std::vector<GridVec3> upper_route(GridBounds room, int rotation, int w, int d, int h) {
    const int end = 64 + h / 16 * 36;
    std::vector<GridVec3> result;
    for (const auto [u, v] : {std::pair{128, end - 18}, {128, end + 80}, {w / 2, end + 80},
                              {w / 2, d - end - 80}, {w - 128, d - end - 80}, {w - 128, d - end + 18}})
        result.push_back(point(room, rotation, u, v, h));
    return result;
}

void physical_cases() {
    int cases = 0;
    for (const int h : {128, 192})
        for (const int rotation : {0, 1, 2, 3})
            for (const int depth : {h == 128 ? 640 : 768, h == 128 ? 864 : 1152, 960, 1536}) {
                // Nonzero8-unit translation exercises the actual rotation frame.
                const int width = 640;
                GridBounds room{{-1016, 24, 0}, {-1016 + (rotation % 2 ? depth : width),
                                                24 + (rotation % 2 ? width : depth), h + 56}};
                const auto geometry = make_opposed_bridge_geometry(room, rotation, h);
                require(geometry == make_opposed_bridge_geometry(room, rotation, h), "Pure construction is nondeterministic");
                const auto level = fixture(room, rotation, h);
                const auto world = emitted_world(level);
                const auto route = upper_route(room, rotation, width, depth, h);
                for (std::size_t i = 1; i < route.size(); ++i)
                    require(supported(world, sweep(route[i - 1], route[i])), "Upper elbow/landing loses full support or headroom");
                require(supported(world, sweep(point(room, rotation, width / 2, 32, 0),
                                                 point(room, rotation, width / 2, depth - 32, 0))),
                        "Full continuous lower aisle is blocked or unsupported");
                for (const auto &deck : geometry.decks)
                    require(deck.valid() && deck.max.z == h && deck.min.z == h - 24,
                            "Deck thickness/elevation changed");
                for (const auto &run : geometry.stairs) {
                    require(run.size() == static_cast<std::size_t>(h / 16), "Wrong tread count");
                    for (std::size_t i = 0; i < run.size(); ++i) {
                        const auto box = run[i];
                        require(box.min.z == 0 && box.max.z == static_cast<int>(i + 1) * 16,
                                "Treads lack common base or16-unit ascending order");
                        const GridBounds standing{{box.min.x, box.min.y, box.max.z},
                                                   {box.max.x, box.max.y, box.max.z + 56}};
                        require(supported(world, standing), "Full tread footprint is blocked/unsupported");
                        if (i + 1 < run.size()) {
                            auto raised = standing;
                            raised.max.z += 16;
                            require(std::none_of(world.begin(), world.end(), [&](const auto &b) { return overlap(b, raised); }),
                                    "Raised-before-advance standing sweep is blocked");
                        }
                    }
                }
                const auto delta = [](GridBounds low, GridBounds high) {
                    const auto a = low.center(), b = high.center();
                    return std::pair{b.x - a.x, b.y - a.y};
                };
                const auto a = delta(geometry.stairs[0].front(), geometry.stairs[0].back());
                const auto b = delta(geometry.stairs[1].front(), geometry.stairs[1].back());
                require(a.first == -b.first && a.second == -b.second && a != std::pair{0, 0},
                        "Stairs do not ascend in opposite directions");
                require(all_ordinary_anchors(level), "Original ordinary fixture route fails");
                for (const auto &run : geometry.stairs) {
                    auto removed = level;
                    std::erase_if(removed.solids, [&](const auto &solid) {
                        return solid.purpose == BrushPurpose::navigation_stair &&
                               std::find(run.begin(), run.end(), checked_brush_bounds(solid.geometry)) != run.end();
                    });
                    require(all_ordinary_anchors(removed), "A single removed stair access isolates an upper anchor");
                }
                ++cases;
            }
    std::cout << cases << " emitted geometry cases: both heights, all rotations, minimum/aligned/long depths.\n";
}

void meaningful_negative_controls() {
    const GridBounds room{{0, 0, 0}, {640, 640, 384}};
    const auto original = fixture(room, 0, 128);
    const auto route = upper_route(room, 0, 640, 640, 128);
    auto changed = original;
    changed.solids.erase(changed.solids.begin() + 1);
    require(!supported(emitted_world(changed), sweep(route[2], route[3])), "Missing middle slab falsely certified");
    require(all_ordinary_anchors(changed) == false, "Missing central anchor support was overlooked");

    changed = original;
    auto &middle = std::get<GridBounds>(changed.solids[1].geometry);
    middle.min.y = 440; // Leave only72 at the west seam; declared center at432 loses its footprint.
    require(!supported(emitted_world(changed), sweep(route[1], route[2])), "Pinched joining face falsely certified");

    changed = original;
    add_box(changed, {{304, 240, 0}, {336, 272, 80}}, BrushPurpose::structural);
    require(!supported(emitted_world(changed), sweep({320, 32, 0}, {320, 608, 0})), "Lower barrier falsely certified");

    changed = original;
    std::erase_if(changed.solids, [](const auto &s) { return s.purpose == BrushPurpose::navigation_stair; });
    require(!all_ordinary_anchors(changed), "Removing both stairs did not invalidate ordinary upper access");
}

void invalid_input_controls() {
    const GridBounds valid{{0, 0, 0}, {640, 768, 248}};
    auto rejects = [&](GridBounds room, int rotation, int height) {
        bool rejected = false;
        try {
            (void)make_opposed_bridge_geometry(room, rotation, height);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        require(rejected, "Invalid public geometry input was accepted or silently clamped");
    };
    for (int rotation : {-1, 4})
        rejects(valid, rotation, 128);
    for (int height : {0, 64, 160, 256})
        rejects(valid, 0, height);
    for (const GridBounds room : {GridBounds{{0, 0, 1}, {640, 768, 248}},
                                  {{0, 0, 0}, {639, 768, 248}}, {{0, 0, 0}, {640, 639, 248}},
                                  {{0, 0, 0}, {640, 768, 183}}, {{640, 0, 0}, {0, 768, 248}},
                                  {{std::numeric_limits<int>::min(), 0, 0}, {640, 768, 248}}})
        rejects(room, 0, 128);
    rejects({{0, 0, 0}, {640, 767, 248}}, 0, 192);
    rejects({{0, 0, 0}, {640, 768, 247}}, 0, 192);
    (void)make_opposed_bridge_geometry(valid, 0, 192); // Exact minimum ceiling is legal.
}

bool has_tag(const Room &room, const std::string &tag) {
    return std::find(room.tags.begin(), room.tags.end(), tag) != room.tags.end();
}

void actual_default_integration() {
    for (const int count : {2, 3, 6, 8, 32}) {
        auto spec = PromptParser{}.parse("industrial connected loops", {Game::quake2, GameMode::deathmatch, 701, count, 6});
        spec.verticality = .95;
        auto plan = make_rule_semantic_plan(spec);
        plan.verticality = 95;
        const auto level = LayoutGenerator{}.generate_candidates(spec, plan, 1).front();
        require(canonical_layout_sha256(level) == canonical_layout_sha256(LayoutGenerator{}.generate_candidates(spec, plan, 1).front()),
                "Default integration changed seeded determinism");
        int opposed = 0;
        for (const auto &room : level.rooms) {
            if (!has_tag(room, "opposed_stair_bridge"))
                continue;
            ++opposed;
            require(!has_tag(room, "offset_barrier"), "New elevated room also received a ground barrier");
            int rotation = -1;
            for (int r = 0; r < 4; ++r)
                if (has_tag(room, "motif_rotation_" + std::to_string(r)))
                    rotation = r;
            std::vector<GridBounds> decks, stairs;
            for (const auto &solid : level.solids) {
                const auto box = checked_brush_bounds(solid.geometry);
                if (!room.bounds.contains(box.center()))
                    continue;
                if (solid.purpose == BrushPurpose::raised_platform)
                    decks.push_back(box);
                if (solid.purpose == BrushPurpose::navigation_stair)
                    stairs.push_back(box);
            }
            require(decks.size() >= 3 && !stairs.empty(), "Tag lacks actual bridge geometry");
            const auto expected = make_opposed_bridge_geometry(room.bounds, rotation, decks.front().max.z);
            require(std::equal(expected.decks.begin(), expected.decks.end(), decks.begin()), "Generated bridge differs from pure geometry");
            auto expected_stairs = expected.stairs[0];
            expected_stairs.insert(expected_stairs.end(), expected.stairs[1].begin(), expected.stairs[1].end());
            require(stairs == expected_stairs, "Generated stair positions/directions differ");
        }
        const int elevated = std::clamp(static_cast<int>(std::ceil(count * (.3 + .4 * .95))), 2, count);
        require(opposed == elevated / 3, "Every-third eligible room policy changed");
        require(all_ordinary_anchors(level), "Actual default anchor routes failed");
        spec.verticality = .15;
        plan = make_rule_semantic_plan(spec);
        plan.verticality = 15;
        const auto flat = LayoutGenerator{}.generate_candidates(spec, plan, 1).front();
        require(std::none_of(flat.solids.begin(), flat.solids.end(), [](const auto &s) {
                    return s.purpose == BrushPurpose::raised_platform || s.purpose == BrushPurpose::navigation_stair;
                }), "Low verticality gained an upper motif");
    }
}

} // namespace

int main() try {
    physical_cases();
    meaningful_negative_controls();
    invalid_input_controls();
    actual_default_integration();
    std::cout << "Opposed bridge emitted support, ordinary independent accesses, negative controls and default scope passed.\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
