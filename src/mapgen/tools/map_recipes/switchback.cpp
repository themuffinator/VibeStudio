// Original experimental layout: staggered rooms, overlapping loops and local
// multi-level routes. The reference archive contributes observations, not geometry.
#include "recipe_support.hpp"
using namespace qmg;

int main(int argc, char **argv) try {
    if (argc != 2)
        throw std::runtime_error("usage: switchback_recipe <output directory>");
    recipes::Recipe r("qmg_switchback", "Switchback",
                      "Compact asymmetric industrial Quake II deathmatch with overlapping loops, three useful combat "
                      "floors and balanced lighting",
                      20260912);
    r.room("Turbine", {{-384, -320, 0}, {384, 448, 640}});
    r.room("Pumpwell", {{-1120, -96, 0}, {-464, 544, 448}});
    r.room("Loading", {{-768, -896, 0}, {128, -400, 352}});
    r.room("Relay", {{464, -128, 192}, {1040, 384, 576}});
    r.room("Accumulator", {{-176, 528, 192}, {528, 976, 576}});
    // Short, staggered thresholds. Two room pairs have independent links at
    // different elevations; the outer detours prevent a central-room monopoly.
    r.connection(1, 2, 160, {{{-464, -64, 0}, {-384, 96, 160}}});
    r.connection(1, 2, 192, {{{-464, 160, 192}, {-384, 352, 384}}});
    r.connection(1, 3, 192, {{{-192, -400, 0}, {0, -320, 192}}});
    r.connection(2, 3, 192, {{{-688, -400, 0}, {-496, -96, 192}}});
    r.connection(1, 4, 160, {{{384, 64, 192}, {464, 224, 384}}});
    r.connection(1, 4, 160, {{{384, -128, 384}, {464, 32, 576}}});
    r.connection(1, 5, 192, {{{-128, 448, 192}, {64, 528, 384}}});
    r.connection(4, 5, 192, {{{688, 384, 192}, {880, 720, 384}}, {{528, 528, 192}, {688, 720, 384}}});
    r.connection(2, 5, 192, {{{-784, 544, 192}, {-592, 720, 384}}, {{-592, 576, 192}, {-176, 720, 384}}});
    r.connection(3, 4, 160, {{{128, -624, 192}, {688, -464, 384}}, {{496, -464, 192}, {688, -128, 384}}});

    // Turbine has three combat levels. The floor remains open below partial
    // galleries; the second stair is transverse, with a separate mid-level lane.
    r.deck(-384, 112, -224, 448, 192);
    r.deck(-224, 224, 384, 448, 192);
    r.deck(224, -80, 384, 224, 192);
    r.stairs(-368, -320, 0, 1);
    r.stairs(-208, -80, 0, 0);
    r.deck(-144, -320, 208, -160, 384);
    r.deck(208, -320, 384, 448, 384);
    r.stairs(-224, 320, 192, 0);
    // Pumpwell has a lower recovery loop and a middle return path to two rooms.
    r.deck(-1120, 336, -464, 544, 192);
    r.deck(-656, 160, -464, 336, 192);
    r.stairs(-1104, -96, 0, 1);
    // Loading's small mezzanine creates the east bypass and exposes Quad.
    r.deck(-32, -896, 128, -400, 192);
    r.stairs(-464, -560, 0, 0);
    // Relay's upper armor route returns to Turbine independently of its lower door.
    r.deck(912, -128, 1040, 384, 384);
    r.deck(464, -128, 912, 32, 384);
    r.stairs(480, 256, 192, 0);
    // Each space has different cover and a clear landing at every stair.
    r.solid({{80, 64, 0}, {176, 192, 528}});
    r.solid({{-160, 48, 0}, {-96, 224, 96}});
    r.solid({{-864, 176, 0}, {-800, 304, 128}});
    r.solid({{-448, -624, 0}, {-320, -576, 128}});
    r.solid({{624, 80, 192}, {688, 144, 544}});
    r.solid({{112, 640, 192}, {176, 832, 384}});
    // Screen the long lower doorway axis without mirroring cover between rooms.
    r.solid({{-544, 0, 0}, {-512, 112, 128}});
    r.solid({{-720, 336, 192}, {-688, 464, 320}});
    r.solid({{-688, 64, 0}, {-576, 96, 128}});

    r.spawn({-128, -224, 24}, 0);
    r.spawn({-288, 192, 216}, 90);
    r.spawn({304, 176, 408}, 270);
    r.spawn({-640, 464, 24}, 270);
    r.spawn({-992, 512, 216}, 0);
    r.spawn({-688, -480, 24}, 270);
    r.spawn({832, 160, 216}, 180);
    r.spawn({-48, 864, 216}, 270);
    r.point(PlacementRole::weapon, {96, -112, 24}, 4);
    r.point(PlacementRole::weapon, {-576, -736, 24}, 0, "weapon_supershotgun");
    r.point(PlacementRole::weapon, {-944, 224, 24}, 2);
    r.point(PlacementRole::weapon, {832, 32, 216}, 6);
    r.point(PlacementRole::weapon, {-64, -240, 408}, 3);
    r.point(PlacementRole::weapon, {304, 816, 216}, 5);
    r.point(PlacementRole::weapon, {-784, 480, 216}, 0);
    r.point(PlacementRole::armor, {0, 264, 216});
    r.point(PlacementRole::armor, {704, -48, 408});
    r.point(PlacementRole::health, {48, -784, 216}, 0, "item_quad");
    r.point(PlacementRole::health, {-832, 64, 24}, 0, "item_health_mega");
    for (auto p : {GridVec3{-208, -144, 24},
                   {288, -240, 24},
                   {-592, 304, 24},
                   {-928, 96, 24},
                   {-672, -800, 24},
                   {-240, -656, 24},
                   {928, 128, 216},
                   {400, 608, 216}})
        r.point(PlacementRole::health, p);
    r.point(PlacementRole::health, {160, -112, 24}, 0, "ammo_rockets");
    r.point(PlacementRole::health, {-512, -736, 24}, 0, "ammo_shells");
    r.point(PlacementRole::health, {-944, 288, 24}, 0, "ammo_bullets");
    r.point(PlacementRole::health, {832, -32, 216}, 0, "ammo_slugs");
    r.point(PlacementRole::health, {0, -240, 408}, 0, "ammo_grenades");
    r.point(PlacementRole::health, {368, 816, 216}, 0, "ammo_cells");
    // Light the actual routes and alcoves at their own elevations.
    for (auto p : {GridVec3{0, -112, 136}, {-192, 32, 128}, {288, 128, 128}, {-48, 352, 128}})
        r.light(p, 320);
    for (auto p : {GridVec3{-288, 240, 320}, {64, 272, 320}, {288, 96, 320}})
        r.light(p, 330);
    for (auto p : {GridVec3{-64, -240, 528}, {288, 16, 528}, {256, 368, 528}})
        r.light(p, 330);
    for (auto p : {GridVec3{-944, 112, 128}, {-592, 128, 128}, {-944, 448, 128}, {-592, 448, 128}})
        r.light(p, 350, {620, 820, 1000});
    for (auto p : {GridVec3{-944, 432, 328}, {-544, 256, 328}})
        r.light(p, 340, {620, 820, 1000});
    for (auto p : {GridVec3{-592, -736, 144}, {-240, -704, 144}, {-592, -448, 144}, {64, -736, 112}})
        r.light(p, 340, {1000, 780, 540});
    r.light({48, -784, 304}, 250, {1000, 780, 540});
    r.light({48, -480, 304}, 280, {1000, 780, 540});
    for (auto p : {GridVec3{560, 64, 304}, {832, 192, 304}, {960, -48, 304}})
        r.light(p, 340, {840, 1000, 840});
    for (auto p : {GridVec3{608, -48, 512}, {960, 256, 512}})
        r.light(p, 340, {840, 1000, 840});
    r.light({-48, 720, 336}, 400);
    r.light({336, 768, 336}, 400);
    // Corridor fixtures are explicit native point entities in sealed rooms at
    // their thresholds, preserving the current validator's placement contract.
    for (auto p : {GridVec3{-656, -64, 128},
                   {-656, -432, 128},
                   {144, 608, 320},
                   {704, 352, 320},
                   {-720, 512, 320},
                   {-144, 656, 320},
                   {96, -544, 288},
                   {560, -96, 320}})
        r.light(p, 450);
    // Compress XY by 8%, keeping the floor heights and 16-unit rises intact.
    // A 36-unit tread becomes 33 or 34 units, retaining positive hull support.
    const auto compact = [](GridVec3 &p) {
        p.x = checked_grid_coord(std::llround(double(p.x) * 23 / 25));
        p.y = checked_grid_coord(std::llround(double(p.y) * 23 / 25));
    };
    for (auto &room : r.level().rooms) {
        compact(room.bounds.min);
        compact(room.bounds.max);
    }
    for (auto &c : r.level().connections) {
        c.width = 2 * (c.width * 23 / 50);
        for (auto &b : c.passage_volumes) {
            compact(b.min);
            compact(b.max);
        }
    }
    for (auto &solid : r.level().solids) {
        auto &b = std::get<GridBounds>(solid.geometry);
        compact(b.min);
        compact(b.max);
    }
    for (auto &p : r.level().placements)
        compact(p.origin);
    r.finish(std::filesystem::absolute(argv[1]));
    if (r.result.quality->score.warnings != 0)
        throw std::runtime_error("Switchback regression: new advisory findings require review");
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
