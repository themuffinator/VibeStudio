// Original design experiment: distributed encounters and an independent
// overpass/underpass. Reference maps supply observations, never geometry.
#include "recipe_support.hpp"
using namespace qmg;

int main(int argc, char **argv) try {
    if (argc != 2)
        throw std::runtime_error("usage: qmg_spillway_recipe <output directory>");
    recipes::Recipe r("qmg_spillway", "Spillway",
                      "Asymmetric Quake II deathmatch, industrial spillway, six unequal spaces, overlapping routes and "
                      "useful local height variation",
                      20260913, 6);
    r.floor_levels = {0, 144, 288};
    r.room("Intake", {{-896, -448, 0}, {-256, 208, 480}});
    r.room("Pressure", {{64, -176, 0}, {720, 480, 480}});
    r.room("Separator", {{-640, 288, 0}, {-16, 768, 512}});
    r.room("Control", {{112, 560, 144}, {720, 944, 512}});
    r.room("Workshop", {{-704, -976, 0}, {-64, -528, 320}});
    r.room("Outfall", {{800, -544, 0}, {1184, 144, 352}});

    r.connection(1, 5, 160, {{{-624, -528, 0}, {-464, -448, 192}}});
    r.connection(1, 3, 144, {{{-432, 208, 144}, {-288, 288, 336}}});
    // A raised east-west link crosses the north-south lower passage with
    // 24 units of solid separation. The crossing is not a central junction.
    r.connection(1, 2, 160, {{{-256, -32, 144}, {64, 128, 336}}});
    r.connection(5, 3, 144, {{{-224, -528, 0}, {-80, 288, 120}}});
    r.connection(2, 6, 160, {{{720, -48, 0}, {800, 112, 160}}});
    r.connection(
        5, 6, 144,
        {{{-64, -864, 0}, {432, -720, 192}}, {{288, -720, 0}, {432, -544, 192}}, {{432, -688, 0}, {944, -544, 192}}});
    r.connection(2, 4, 160, {{{384, 480, 144}, {544, 560, 336}}});
    r.connection(3, 4, 144, {{{-16, 560, 144}, {112, 704, 264}}});
    r.connection(3, 4, 144, {{{-16, 608, 288}, {112, 752, 480}}});
    r.connection(4, 6, 144, {{{720, 784, 144}, {1056, 928, 336}}, {{912, 144, 144}, {1056, 784, 336}}});

    // Intake: broad lower rocket space and a middle route on three sides.
    r.deck(-556, -448, -256, -256, 144);
    r.deck(-416, -256, -256, 208, 144);
    r.deck(-896, 64, -416, 208, 144);
    r.stairs(-880, -384, 0, 0, 9);
    // Pressure: a separate close-range encounter with an offset ascent.
    r.deck(64, -176, 240, 224, 144);
    r.deck(240, 164, 720, 480, 144);
    r.stairs(560, -160, 0, 1, 9);
    // Separator links the lower trench to a middle loop and upper armor lane.
    r.deck(-640, 288, -192, 432, 144);
    r.deck(-640, 432, -512, 768, 144);
    r.deck(-512, 628, -16, 768, 144);
    r.deck(-192, 432, -16, 628, 144);
    r.stairs(-480, 304, 0, 1, 9);
    r.deck(-172, 528, -16, 768, 288);
    r.stairs(-496, 640, 144, 0, 9);
    // Control only adds a third floor where it gives a distinct return path.
    r.deck(452, 560, 720, 944, 288);
    r.deck(112, 560, 452, 752, 288);
    r.stairs(128, 800, 144, 0, 9);
    // Outfall supplies a second way from the lower east side to middle Control.
    r.deck(800, 16, 1184, 144, 144);
    r.deck(1056, -544, 1184, 16, 144);
    r.stairs(832, -308, 0, 1, 9);

    // Unequal machinery footprints and baffles screen spawns and long axes.
    r.solid({{-656, -112, 0}, {-544, 16, 112}});
    r.solid({{-816, -192, 0}, {-752, -96, 192}});
    r.solid({{352, -64, 0}, {432, 64, 320}});
    r.solid({{272, 288, 144}, {336, 416, 240}});
    r.solid({{-272, 480, 0}, {-208, 576, 112}});
    r.solid({{256, 608, 144}, {320, 704, 240}});
    r.solid({{-432, -864, 0}, {-336, -800, 128}});
    r.solid({{992, -368, 0}, {1056, -256, 128}});

    r.spawn({-800, -32, 24}, 0);
    r.spawn({-576, 144, 168}, 0);
    r.spawn({272, 96, 24}, 270);
    r.spawn({640, 416, 168}, 180);
    r.spawn({-576, 576, 168}, 270);
    r.spawn({624, 848, 312}, 180);
    r.spawn({-576, -880, 24}, 90);
    r.spawn({1120, -448, 24}, 90);

    r.point(PlacementRole::weapon, {-592, -224, 24}, 4); // rockets, lower Intake
    r.point(PlacementRole::weapon, {480, 80, 24}, 0, "weapon_supershotgun");
    r.point(PlacementRole::weapon, {-272, 352, 24}, 6); // lower rail recovery
    r.point(PlacementRole::weapon, {-192, -832, 24}, 2);
    r.point(PlacementRole::weapon, {1008, -160, 24}, 5);
    r.point(PlacementRole::weapon, {192, 768, 168}, 3);
    r.point(PlacementRole::weapon, {-832, 128, 168}, 0);
    r.point(PlacementRole::armor, {-80, 672, 312});
    r.point(PlacementRole::armor, {624, 608, 312});
    r.point(PlacementRole::health, {-608, -624, 24}, 0, "item_health_mega");
    r.point(PlacementRole::health, {1120, 80, 168}, 0, "item_quad");
    for (auto p : {GridVec3{-704, -224, 24},
                   {-304, 144, 168},
                   {496, 384, 168},
                   {144, 16, 24},
                   {-560, 480, 24},
                   {384, 768, 168},
                   {-544, -736, 24},
                   {1120, -112, 24}})
        r.point(PlacementRole::health, p);
    r.point(PlacementRole::health, {-496, -224, 24}, 0, "ammo_rockets");
    r.point(PlacementRole::health, {480, 144, 24}, 0, "ammo_shells");
    r.point(PlacementRole::health, {-192, -896, 24}, 0, "ammo_bullets");
    r.point(PlacementRole::health, {-256, 416, 24}, 0, "ammo_slugs");
    r.point(PlacementRole::health, {192, 704, 168}, 0, "ammo_grenades");
    r.point(PlacementRole::health, {1008, -80, 24}, 0, "ammo_cells");

    // Light each floor locally. Warm Workshop and cool Outfall distinguish
    // opposite recovery routes; geometry and material changes remain primary.
    for (auto p : {GridVec3{-736, -272, 104},
                   {-704, 16, 104},
                   {-320, -64, 104},
                   {-448, -352, 264},
                   {-640, 128, 264},
                   {-320, 128, 264}})
        r.light(p, 340);
    for (auto p :
         {GridVec3{176, 64, 104}, {480, -80, 120}, {448, 304, 104}, {160, 32, 264}, {400, 336, 280}, {640, 272, 280}})
        r.light(p, 350);
    for (auto p : {GridVec3{-560, 384, 104},
                   {-240, 608, 104},
                   {-96, 608, 104},
                   {-560, 400, 256},
                   {-576, 672, 256},
                   {-112, 544, 256},
                   {-96, 656, 408}})
        r.light(p, 350);
    for (auto p : {GridVec3{384, 736, 248}, {192, 608, 248}, {640, 864, 256}, {256, 656, 416}, {608, 784, 416}})
        r.light(p, 340);
    for (auto p : {GridVec3{-576, -848, 160}, {-192, -784, 160}, {-576, -608, 160}})
        r.light(p, 360, {1000, 760, 520});
    for (auto p : {GridVec3{960, -464, 112}, {1120, -256, 104}, {896, -112, 104}, {1120, -272, 264}, {992, 80, 264}})
        r.light(p, 360, {600, 840, 1000});
    for (const auto &room : r.level().rooms) {
        auto p = room.bounds.center();
        p.z = room.bounds.min.z + 96;
        r.light(p, 300);
    }
    // Direct corridor fixtures are typed, validated and analyzed.
    for (const auto &c : r.level().connections)
        for (const auto &b : c.passage_volumes) {
            auto p = b.center();
            p.z = b.max.z - 24;
            r.light(p, 360);
        }
    r.finish(std::filesystem::absolute(argv[1]));
    if (r.result.quality->score.warnings != 0)
        throw std::runtime_error("Spillway advisory findings require review");
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
