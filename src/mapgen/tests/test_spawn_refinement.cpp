#include "qmg/spawn_refinement.hpp"

#include "qmg/layout_fingerprint.hpp"
#include "qmg/visibility.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace qmg;

void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

Placement point(std::uint32_t id, PlacementRole role, GridVec3 origin) {
    Placement p;
    p.id = PlacementId{id};
    p.role = role;
    p.origin = origin;
    return p;
}

LayoutSolid box(std::uint32_t id, GridBounds bounds, BrushPurpose purpose = BrushPurpose::structural) {
    LayoutSolid s;
    s.id = SolidId{id};
    s.geometry = bounds;
    s.purpose = purpose;
    return s;
}

Layout seam() {
    Layout level;
    level.spec.game = Game::quake2;
    level.spec.mode = GameMode::deathmatch;
    level.spec.room_count = 2;
    level.spec.target_players = 2;
    level.rooms = {{RoomId{1}, "west", {{0, 0, 0}, {512, 512, 320}}, {"compact_mp"}},
                   {RoomId{2}, "east", {{560, 0, 0}, {1072, 512, 320}}, {"compact_mp"}}};
    level.connections.push_back({ConnectionId{1}, RoomId{1}, RoomId{2}, 160, 192, {{{512, 176, 0}, {560, 336, 192}}}});
    level.placements = {point(1, PlacementRole::deathmatch_spawn, {448, 448, 24}),
                        point(2, PlacementRole::deathmatch_spawn, {624, 448, 24})};
    return level;
}

Layout barrier() {
    auto level = seam();
    level.rooms[0].bounds = {{0, 0, 0}, {1024, 1024, 320}};
    level.rooms[1].bounds = {{1072, 0, 0}, {1584, 1024, 320}};
    level.connections[0].passage_volumes = {{{1024, 176, 0}, {1072, 336, 192}}};
    level.solids.push_back(box(1, {{480, 0, 0}, {544, 700, 128}}));
    level.placements = {point(1, PlacementRole::deathmatch_spawn, {400, 600, 24}),
                        point(2, PlacementRole::deathmatch_spawn, {624, 600, 24}),
                        point(3, PlacementRole::deathmatch_spawn, {100, 100, 24})};
    return level;
}

void unchanged_except_spawn_origins(const Layout &before, Layout after) {
    require(after.placements.size() == before.placements.size(), "placement count changed");
    for (std::size_t i = 0; i < before.placements.size(); ++i)
        if (before.placements[i].role == PlacementRole::deathmatch_spawn)
            after.placements[i].origin = before.placements[i].origin;
    require(canonical_layout_sha256(before) == canonical_layout_sha256(after), "non-spawn-origin data changed");
}

template <typename Edit>
void rejects_without_mutation(Edit edit, const std::vector<SpawnRefinementChoices> &choices = {}) {
    auto level = seam();
    edit(level);
    const auto before = canonical_layout_sha256(level);
    bool rejected = false;
    try {
        (void)refine_compact_mp_spawns(level, choices);
    } catch (const std::invalid_argument &) {
        rejected = true;
    } catch (const VisibilityError &) {
        // The shared source-geometry validator has its own typed input error.
        rejected = true;
    }
    require(rejected, "invalid helper construction/identity was accepted");
    require(canonical_layout_sha256(level) == before, "rejection partially mutated caller");
}

void shell_occlusion_and_local_separation() {
    auto level = seam();
    const auto before = level;
    const auto ray = VisibilityAnalyzer{}.classify_segment(level, {448, 448, 46}, {624, 448, 46});
    require(ray.kind == SegmentVisibilityKind::outside_free_space && ray.precision == VisibilityPrecision::exact,
            "fixture must cross implicit shell with valid endpoints");
    const auto result = refine_compact_mp_spawns(level, {{PlacementId{1}, {{384, 448, 24}, {320, 448, 24}}}});
    require(result.before == SpawnRefinementObjective{0, 1, 34560} && result.after == SpawnRefinementObjective{},
            "close occluded pair was not reduced independently of visibility");
    require(level.placements[0].origin == GridVec3{320, 448, 24} && result.accepted_moves == 1,
            "bounded candidate objective selected wrong station");
    unchanged_except_spawn_origins(before, level);
}

void exact_explicit_occlusion_retained() {
    auto level = barrier();
    const auto result = refine_compact_mp_spawns(level, {{PlacementId{1}, {{300, 600, 24}}}});
    require(result.before.visible_pairs == 1 && result.after.visible_pairs == 1 && result.after.close_pairs == 0,
            "explicitly blocked close pair was not improved");
    require(VisibilityAnalyzer{}.classify_segment(level, {300, 600, 46}, {624, 600, 46}).kind ==
                SegmentVisibilityKind::occluded,
            "explicitly occluded pair was exposed");
}

void cannot_exchange_blocked_pair_for_new_exposure() {
    auto level = barrier();
    const auto before = canonical_layout_sha256(level);
    const auto result = refine_compact_mp_spawns(level, {{PlacementId{1}, {{700, 100, 24}}}});
    require(result.before.visible_pairs == 1 && result.before.close_pairs == 1,
            "exposure-exchange fixture is ineffective");
    require(result.accepted_moves == 0 && canonical_layout_sha256(level) == before,
            "a hidden pair was traded for a newly visible pair at equal total exposure");
}

void zero_problem_is_noop() {
    auto level = seam();
    level.placements[0].origin.x = 128;
    const auto before = canonical_layout_sha256(level);
    const auto result = refine_compact_mp_spawns(level, {{PlacementId{1}, {{64, 448, 24}, {192, 448, 24}}}});
    require(result.before == SpawnRefinementObjective{} && result.tested_candidates == 0 &&
                canonical_layout_sha256(level) == before,
            "adequate occluded pair was optimized beyond the cap");
}

void outside_endpoint_and_changed_floor_are_not_candidates() {
    auto level = seam();
    const auto before = canonical_layout_sha256(level);
    const auto result =
        refine_compact_mp_spawns(level, {{PlacementId{1}, {{-64, 448, 24}, {320, 448, 152}, {768, 448, 24}}}});
    require(result.accepted_moves == 0 && result.tested_candidates == 0 && canonical_layout_sha256(level) == before,
            "outside endpoint, changed floor or room was admitted");
}

void hull_overlap_is_not_clearance() {
    auto level = seam();
    level.solids.push_back(box(1, {{332, 432, 0}, {356, 464, 112}}));
    const auto result = refine_compact_mp_spawns(level, {{PlacementId{1}, {{320, 448, 24}}}});
    require(result.accepted_moves == 0 && result.tested_candidates == 0,
            "clear point with overlapping32wide hull was accepted");
}

void entire_upper_footprint_requires_support() {
    auto level = seam();
    level.solids.push_back(box(1, {{256, 256, 104}, {480, 480, 128}}, BrushPurpose::raised_platform));
    level.solids.push_back(box(2, {{592, 256, 104}, {800, 480, 128}}, BrushPurpose::raised_platform));
    level.placements[0].origin.z = level.placements[1].origin.z = 152;
    const auto result = refine_compact_mp_spawns(level, {{PlacementId{1}, {{264, 448, 152}}}});
    require(result.accepted_moves == 0 && result.tested_candidates == 0,
            "supported center with8units of footprint overhang was accepted");
}

void other_anchor_spacing_is_enforced() {
    auto level = seam();
    level.placements.push_back(point(3, PlacementRole::health, {320, 448, 24}));
    const auto result = refine_compact_mp_spawns(level, {{PlacementId{1}, {{320, 448, 24}}}});
    require(result.accepted_moves == 0 && result.tested_candidates == 0, "spawn placed on a point anchor");
}

void anchor_boundary_and_nonblocking_light() {
    auto level = seam();
    level.placements.push_back(point(3, PlacementRole::health, {320, 384, 24}));
    level.placements.push_back(point(4, PlacementRole::light, {320, 448, 24}));
    const auto result = refine_compact_mp_spawns(level, {{PlacementId{1}, {{320, 448, 24}}}});
    require(result.accepted_moves == 1 && result.after.close_pairs == 0,
            "exact64unit anchor boundary or light was treated as blocked");
}

void closest24_and_two_passes_are_hard_bounds() {
    auto level = seam();
    std::vector<GridVec3> candidates;
    for (int x = 449; x <= 480; ++x)
        candidates.push_back({x, 448, 24}); // all worsen the existing close pair
    candidates.push_back({320, 448, 24});   // useful but outside the declared local24
    const auto result = refine_compact_mp_spawns(level, {{PlacementId{1}, candidates}});
    require(result.tested_candidates == 24 && result.passes == 1 && result.accepted_moves == 0,
            "local search silently expanded or retried after no improvement");
}

void deterministic_equal_objective_and_distance_tie() {
    auto a = seam();
    auto b = a;
    const auto one = refine_compact_mp_spawns(a, {{PlacementId{1}, {{384, 320, 24}, {320, 384, 24}}}});
    const auto two = refine_compact_mp_spawns(b, {{PlacementId{1}, {{320, 384, 24}, {384, 320, 24}, {320, 384, 24}}}});
    require(one.after == two.after && canonical_layout_sha256(a) == canonical_layout_sha256(b) &&
                a.placements[0].origin == GridVec3{320, 384, 24},
            "candidate order/duplication changedXYZ tie");
}

void scope_and_input_guards() {
    rejects_without_mutation([](Layout &l) { l.spec.room_count = 9; });
    rejects_without_mutation([](Layout &l) { l.rooms[1].tags.clear(); });
    rejects_without_mutation([](Layout &l) { l.spec.game = Game::quake3; });
    rejects_without_mutation([](Layout &l) { l.placements[0].origin = {-64, 448, 24}; });
    rejects_without_mutation([](Layout &) {}, {{PlacementId{99}, {{320, 448, 24}}}});
    rejects_without_mutation([](Layout &) {}, {{PlacementId{1}, {}}, {PlacementId{1}, {}}});
}

void unsupported_geometry_and_mechanisms_rejected() {
    rejects_without_mutation([](Layout &l) {
        auto s = box(1, {{32, 32, 0}, {96, 96, 64}});
        s.contents = BrushContents::water;
        l.solids.push_back(s);
    });
    rejects_without_mutation(
        [](Layout &l) { l.solids.push_back(box(1, {{32, 32, 0}, {96, 96, 64}}, BrushPurpose::door)); });
    rejects_without_mutation([](Layout &l) {
        auto s = box(1, {{32, 32, 0}, {96, 96, 64}});
        s.geometry = ConvexPolyhedron{};
        l.solids.push_back(s);
    });
    rejects_without_mutation(
        [](Layout &l) { l.placements.push_back(point(3, PlacementRole::teleporter_destination, {320, 320, 24})); });
    for (const auto material : {MaterialRole::trigger, MaterialRole::liquid, static_cast<MaterialRole>(255)})
        rejects_without_mutation([&](Layout &l) {
            auto s = box(1, {{32, 32, 0}, {96, 96, 64}});
            s.material = material;
            l.solids.push_back(s);
        });
    rejects_without_mutation([](Layout &l) {
        auto s = box(1, {{32, 32, 0}, {96, 96, 64}});
        s.ownership = static_cast<BrushOwnership>(255);
        l.solids.push_back(s);
    });
}

void lower_free_volume_cannot_erase_assumed_shell_floor() {
    rejects_without_mutation([](Layout &l) {
        l.rooms[1].bounds = {{0, 0, -128}, {1072, 512, 16}};
        l.placements[1].origin = {624, 448, -104};
    });
    rejects_without_mutation([](Layout &l) { l.connections[0].passage_volumes[0].min.z = -16; });
    rejects_without_mutation([](Layout &l) {
        // This chain member crosses below the room floor under spawn 1's
        // complete footprint; the free union would remove its assumed floor.
        l.connections[0].passage_volumes.push_back({{400, 176, -16}, {560, 496, 16}});
    });
    rejects_without_mutation([](Layout &l) {
        for (auto &room : l.rooms) {
            room.bounds.min.z += 64;
            room.bounds.max.z += 64;
        }
        for (auto &spawn : l.placements)
            spawn.origin.z += 64;
        // Positive world Z can still be below a nonzero common room floor.
        l.connections[0].passage_volumes[0].min.z = 32;
    });
}

void elevated_passage_preserves_in_room_ground_support() {
    for (const int floor : {0, 64}) {
        auto level = seam();
        for (auto &room : level.rooms) {
            room.bounds.min.z += floor;
            room.bounds.max.z += floor;
        }
        for (auto &spawn : level.placements)
            spawn.origin.z += floor;
        level.connections[0].passage_volumes = {{{512, 176, floor + 128}, {560, 336, floor + 320}}};
        const auto before = level;
        const auto result =
            refine_compact_mp_spawns(level, {{PlacementId{1}, {{384, 448, floor + 24}, {320, 448, floor + 24}}}});
        require(result.before == SpawnRefinementObjective{0, 1, 34560} &&
                    result.after == SpawnRefinementObjective{} && result.accepted_moves == 1 &&
                    level.placements[0].origin == GridVec3{320, 448, floor + 24},
                "elevated passage changed supported room-floor refinement");
        unchanged_except_spawn_origins(before, level);
    }
}

void overlapping_passages_do_not_change_room_floor_support_contract() {
    auto level = seam();
    // The lower passage removes the upper passage's implicit bottom where
    // they overlap. That is an upper-route problem, not a room-floor hole:
    // neither passage extends below the common room plane at Z=0.
    level.connections.push_back(
        {ConnectionId{2}, RoomId{1}, RoomId{2}, 160, 192, {{{512, 176, 128}, {560, 336, 320}}}});
    const auto before = level;
    const auto result = refine_compact_mp_spawns(level, {{PlacementId{1}, {{320, 448, 24}}}});
    require(result.before == SpawnRefinementObjective{0, 1, 34560} &&
                result.after == SpawnRefinementObjective{} && result.accepted_moves == 1 &&
                level.placements[0].origin == GridVec3{320, 448, 24},
            "layered passages prevented a safe in-room ground-station move");
    unchanged_except_spawn_origins(before, level);
}

void elevated_passage_does_not_supply_an_in_room_raised_floor() {
    auto level = seam();
    level.connections[0].passage_volumes = {{{512, 176, 128}, {560, 336, 320}}};
    level.solids.push_back(box(1, {{256, 256, 104}, {480, 480, 128}}, BrushPurpose::raised_platform));
    level.solids.push_back(box(2, {{592, 256, 104}, {800, 480, 128}}, BrushPurpose::raised_platform));
    level.placements[0].origin.z = level.placements[1].origin.z = 152;
    const auto before = level;
    const auto result = refine_compact_mp_spawns(level, {{PlacementId{1}, {{320, 448, 152}, {264, 448, 152}}}});
    require(result.before == SpawnRefinementObjective{0, 1, 34560} &&
                result.after == SpawnRefinementObjective{} && result.accepted_moves == 1 &&
                result.tested_candidates == 1 && level.placements[0].origin == GridVec3{320, 448, 152},
            "elevated passage admitted a partial footprint or rejected an explicit supported cap");
    unchanged_except_spawn_origins(before, level);
    rejects_without_mutation([](Layout &l) {
        l.connections[0].passage_volumes = {{{512, 176, 128}, {560, 336, 320}}};
        l.placements[0].origin.z = l.placements[1].origin.z = 152;
        // No explicit cap supports either in-room raised spawn.
    });
}

void unsupported_explicit_intent_is_rejected_without_mutation() {
    for (const auto intent : {LayoutIntent::wrapped_center, static_cast<LayoutIntent>(255)}) {
        auto level = seam();
        const auto before = canonical_layout_sha256(level);
        level.spec.layout_intent = intent;
        bool rejected = false;
        try {
            (void)refine_compact_mp_spawns(level, {{PlacementId{1}, {{320, 448, 24}}}});
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        require(rejected && level.spec.layout_intent == intent, "invalid explicit intent was accepted or changed");
        level.spec.layout_intent = LayoutIntent::unspecified;
        require(canonical_layout_sha256(level) == before, "intent rejection changed remaining layout data");
    }
}

} // namespace

int main() {
    const char *active = "setup";
    try {
        const auto run = [&](const char *name, auto function) {
            active = name;
            function();
            std::cout << name << " passed\n";
        };
        run("shell_occlusion_and_local_separation", shell_occlusion_and_local_separation);
        run("exact_explicit_occlusion_retained", exact_explicit_occlusion_retained);
        run("cannot_exchange_blocked_pair_for_new_exposure", cannot_exchange_blocked_pair_for_new_exposure);
        run("zero_problem_is_noop", zero_problem_is_noop);
        run("outside_endpoint_and_changed_floor_are_not_candidates",
            outside_endpoint_and_changed_floor_are_not_candidates);
        run("hull_overlap_is_not_clearance", hull_overlap_is_not_clearance);
        run("entire_upper_footprint_requires_support", entire_upper_footprint_requires_support);
        run("other_anchor_spacing_is_enforced", other_anchor_spacing_is_enforced);
        run("anchor_boundary_and_nonblocking_light", anchor_boundary_and_nonblocking_light);
        run("closest24_and_two_passes_are_hard_bounds", closest24_and_two_passes_are_hard_bounds);
        run("deterministic_equal_objective_and_distance_tie", deterministic_equal_objective_and_distance_tie);
        run("scope_and_input_guards", scope_and_input_guards);
        run("unsupported_geometry_and_mechanisms_rejected", unsupported_geometry_and_mechanisms_rejected);
        run("lower_free_volume_cannot_erase_assumed_shell_floor", lower_free_volume_cannot_erase_assumed_shell_floor);
        run("elevated_passage_preserves_in_room_ground_support", elevated_passage_preserves_in_room_ground_support);
        run("overlapping_passages_do_not_change_room_floor_support_contract",
            overlapping_passages_do_not_change_room_floor_support_contract);
        run("elevated_passage_does_not_supply_an_in_room_raised_floor",
            elevated_passage_does_not_supply_an_in_room_raised_floor);
        run("unsupported_explicit_intent_is_rejected_without_mutation",
            unsupported_explicit_intent_is_rejected_without_mutation);
        std::cout << "18 spawn-refinement control groups passed\n";
    } catch (const std::exception &error) {
        std::cerr << active << ": " << error.what() << '\n';
        return 1;
    }
}
