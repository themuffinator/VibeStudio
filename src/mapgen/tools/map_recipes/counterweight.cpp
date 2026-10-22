// Original traversal experiment: faceted rooms, ordinary circulation, and
// optional shortcuts with native Quake II activation and landing semantics.
#include "recipe_support.hpp"
using namespace qmg;

int main(int argc, char **argv) try {
    if (argc != 2)
        throw std::runtime_error("usage: qmg_counterweight_recipe <output directory>");
    recipes::Recipe r("qmg_counterweight", "Counterweight",
                      "Asymmetric Quake II deathmatch, industrial counterweight hall, faceted and L shaped spaces, "
                      "ordinary stairs with optional lift and committed one way teleporter shortcuts",
                      20260914, 5);
    r.floor_levels = {0, 128, 256};
    r.require_ordinary_routes = true;
    r.room("Counterweight Hall", {{-448, -320, 0}, {352, 448, 544}});
    r.room("Kiln", {{-1024, -352, 0}, {-528, 272, 416}});
    r.room("Spool", {{-832, 528, 128}, {-160, 1056, 512}});
    r.room("Switchroom", {{432, 528, 128}, {944, 944, 512}});
    r.room("Service Bay", {{432, -416, 0}, {1008, 144, 352}});

    r.connection(1, 2, 160, {{{-528, -128, 0}, {-448, 32, 192}}});
    r.connection(1, 2, 144, {{{-528, 96, 128}, {-448, 240, 320}}});
    r.connection(1, 3, 144, {{{-352, 448, 128}, {-208, 528, 320}}});
    r.connection(2, 3, 144, {{{-752, 272, 128}, {-608, 528, 320}}});
    r.connection(1, 5, 160, {{{352, -192, 0}, {432, -32, 192}}});
    r.connection(5, 4, 144, {{{640, 144, 128}, {784, 528, 320}}});
    r.connection(1, 4, 144, {{{352, 192, 256}, {576, 336, 448}}, {{432, 336, 256}, {576, 528, 448}}});
    r.connection(3, 4, 144, {{{-160, 800, 256}, {432, 944, 448}}});

    // Unequal chamfers change peek angles and the usable footprint.
    r.prism({{-448, -320}, {-320, -320}, {-448, -192}}, 0, 544);
    r.prism({{256, -320}, {352, -320}, {352, -224}}, 0, 544);
    r.prism({{352, 352}, {352, 448}, {256, 448}}, 0, 544);
    r.prism({{-448, 352}, {-352, 448}, {-448, 448}}, 0, 544);
    r.deck(-448, -224, -272, 304, 128);
    r.deck(-448, 304, 64, 448, 128);
    r.stairs(-96, 16, 0, 1, 8);
    r.deck(192, 64, 352, 448, 256);
    r.deck(-64, 320, 192, 448, 256);
    r.prism({{48, -192}, {80, -224}, {144, -224}, {176, -192}, {176, -128}, {144, -96}, {80, -96}, {48, -128}}, 0, 112);
    r.solid({{-240, -80, 0}, {-176, 32, 112}}); // screen the lower spawn from the high gallery

    r.deck(-672, -352, -528, 272, 128);
    r.deck(-1024, 64, -672, 272, 128);
    r.stairs(-1008, -224, 0, 1, 8);
    r.prism({{-768, -192}, {-640, -192}, {-640, -64}}, 0, 96);

    // The filled corners make actual L shaped rooms, not identical boxes
    // with different names. Upper strips overlook selected lower approaches.
    r.solid({{-832, 800, 128}, {-592, 1056, 512}});
    r.deck(-512, 528, -160, 704, 256);
    r.deck(-304, 704, -160, 1056, 256);
    r.stairs(-800, 560, 128, 0, 8);
    r.solid({{-432, 848, 128}, {-368, 944, 224}});

    r.deck(432, 832, 944, 944, 256);
    r.deck(432, 528, 592, 832, 256);
    r.stairs(784, 544, 128, 1, 8);
    r.solid({{656, 800, 128}, {752, 848, 216}});

    r.solid({{432, 16, 0}, {592, 144, 352}});
    r.deck(736, -416, 1008, 144, 128);
    r.deck(592, -16, 736, 144, 128);
    r.stairs(448, -384, 0, 0, 8);

    r.spawn({-304, -128, 24}, 0);
    r.spawn({272, 336, 280}, 270);
    r.spawn({-800, -272, 24}, 90);
    r.spawn({-944, 208, 152}, 0);
    r.spawn({-752, 736, 152}, 270);
    r.spawn({-224, 576, 280}, 90);
    r.spawn({656, 896, 280}, 180);
    r.spawn({944, -272, 24}, 90);

    r.point(PlacementRole::weapon, {-160, -192, 24}, 4);
    r.point(PlacementRole::weapon, {-608, -224, 24}, 0, "weapon_supershotgun");
    r.point(PlacementRole::weapon, {-736, 176, 152}, 2);
    r.point(PlacementRole::weapon, {-464, 752, 152}, 6);
    r.point(PlacementRole::weapon, {496, 752, 280}, 3);
    r.point(PlacementRole::weapon, {624, -224, 24}, 5);
    r.point(PlacementRole::weapon, {-320, 336, 152}, 0);
    r.point(PlacementRole::armor, {-240, 944, 280});
    r.point(PlacementRole::armor, {96, 384, 280});
    r.point(PlacementRole::health, {-864, -32, 24}, 0, "item_health_mega");
    r.point(PlacementRole::health, {752, 736, 152}, 0, "item_quad");
    for (auto p : {GridVec3{-240, 64, 24},
                   {-160, 368, 152},
                   {-736, -304, 24},
                   {-592, 48, 152},
                   {-560, 928, 152},
                   {-224, 736, 280},
                   {656, 624, 152},
                   {864, 64, 152}})
        r.point(PlacementRole::health, p);
    r.point(PlacementRole::health, {-160, -256, 24}, 0, "ammo_rockets");
    r.point(PlacementRole::health, {-608, -160, 24}, 0, "ammo_shells");
    r.point(PlacementRole::health, {-736, 240, 152}, 0, "ammo_bullets");
    r.point(PlacementRole::health, {-400, 752, 152}, 0, "ammo_slugs");
    r.point(PlacementRole::health, {496, 688, 280}, 0, "ammo_grenades");
    r.point(PlacementRole::health, {688, -224, 24}, 0, "ammo_cells");

    // Stock func_plat is authored at the top and starts at the lower station.
    // No external trigger: ordinary automatic touch activation stays available.
    LiftAssemblySpec lift;
    lift.key = {1};
    lift.platform_bounds = {{192, -64, 240}, {320, 64, 256}};
    lift.travel_delta = {0, 0, -240};
    lift.speed_units_per_second = 200;
    TeleporterAssemblySpec tele;
    tele.key = {2};
    tele.trigger_bounds = {{512, -128, 0}, {576, -64, 48}};
    tele.destination_origin = {-832, 192, 152};
    tele.destination_angle_degrees = 0;
    r.assemble({lift, tele});

    for (auto p : {GridVec3{-336, 32, 88},
                   {-176, -208, 112},
                   {64, 32, 160},
                   {256, -32, 120},
                   {-352, 144, 240},
                   {-192, 368, 240},
                   {16, 384, 384},
                   {272, 256, 384}})
        r.light(p, 350);
    for (auto p : {GridVec3{-816, -256, 104},
                   {-816, -32, 112},
                   {-592, -128, 96},
                   {-928, 160, 264},
                   {-720, 192, 264},
                   {-592, -192, 264}})
        r.light(p, 350, {1000, 780, 520});
    for (auto p : {GridVec3{-704, 720, 224},
                   {-496, 832, 240},
                   {-384, 976, 240},
                   {-416, 608, 376},
                   {-224, 896, 376},
                   {-608, 624, 352}})
        r.light(p, 350);
    for (auto p : {GridVec3{688, 608, 232}, {816, 752, 256}, {496, 704, 376}, {688, 880, 384}})
        r.light(p, 350, {620, 840, 1000});
    for (auto p : {GridVec3{544, -176, 120},
                   {704, -288, 216},
                   {896, -224, 88},
                   {864, -256, 256},
                   {816, 48, 264},
                   {640, 64, 264}})
        r.light(p, 360);
    for (const auto &room : r.level().rooms) {
        auto p = room.bounds.center();
        p.z = room.bounds.min.z + 80;
        r.light(p, 300);
    }
    for (const auto &c : r.level().connections)
        for (const auto &b : c.passage_volumes) {
            auto p = b.center();
            p.z = b.max.z - 24;
            r.light(p, 360);
        }
    r.finish(std::filesystem::absolute(argv[1]));
    if (r.result.quality->score.warnings != 0)
        throw std::runtime_error("Counterweight advisory findings require review");
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
