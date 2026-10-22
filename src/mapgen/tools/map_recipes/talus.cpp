// Original quarry experiment: continuous ramps, distributed route loops,
// selective galleries and a split-height encounter. No archive geometry.
#include "recipe_support.hpp"
using namespace qmg;

int main(int argc, char **argv) try {
    if (argc != 2)
        throw std::runtime_error("usage: qmg_talus_recipe <output directory>");
    recipes::Recipe r("qmg_talus", "Talus",
                      "Asymmetric Quake II quarry deathmatch, continuous shallow ramps, selective galleries, "
                      "split level encounters and distributed recovery loops",
                      20260915, 6);
    r.floor_levels = {0, 128, 256};
    r.require_ordinary_routes = true;
    r.require_no_drop_routes = true;
    r.material_overrides = {{"e1u1/flat1_1", "e2u1/rocks19_1"},   {"e1u1/flat1_2", "e2u1/mine05_1"},
                            {"e1u1/btactmach0", "e2u1/mine10_1"}, {"e1u1/floor3_1", "e2u1/mine04_1"},
                            {"e1u1/floor3_2", "e2u1/flr1_1"},     {"e1u1/ceil1_4", "e2u1/mine03_1"}};
    r.room("Bench", {{-768, -256, 0}, {-96, 448, 448}});
    r.room("Crusher", {{96, -256, 0}, {816, 256, 384}});
    r.room("Headworks", {{-704, 528, 128}, {16, 1120, 576}});
    r.room("Relay", {{288, 432, 128}, {960, 1008, 544}});
    r.room("Cableworks", {{-512, -864, 0}, {64, -336, 352}});
    r.room("Apron", {{144, -896, 0}, {848, -336, 384}});
    r.connection(1, 2, 160, {{{-96, -128, 0}, {96, 32, 192}}});
    r.connection(1, 3, 160, {{{-480, 448, 128}, {-320, 528, 320}}});
    r.connection(2, 4, 160, {{{560, 256, 128}, {720, 432, 320}}});
    r.connection(3, 4, 160, {{{16, 736, 256}, {288, 896, 448}}});
    r.connection(1, 5, 160, {{{-400, -336, 0}, {-240, -256, 192}}});
    r.connection(2, 6, 160, {{{448, -336, 0}, {608, -256, 192}}});
    r.connection(5, 6, 160, {{{64, -656, 128}, {144, -496, 320}}});
    r.connection(3, 4, 144, {{{16, 560, 128}, {288, 704, 320}}});
    r.connection(1, 4, 160, {{{-96, 288, 128}, {288, 448, 320}}, {{128, 448, 128}, {288, 592, 320}}});

    // The quarry ramp exposes a continuous uphill approach to the north rim.
    r.ramp({-736, -160, 0}, HorizontalDirection::positive_y, 160);
    r.deck(-768, 320, -96, 448, 128);
    r.deck(-256, 96, -96, 320, 128);
    r.prism({{-192, -256}, {-96, -256}, {-96, -160}}, 0, 448);
    r.prism({{-160, 448}, {-96, 384}, {-96, 448}}, 0, 448);
    r.prism({{-480, -32}, {-448, -64}, {-384, -64}, {-352, -32}, {-352, 64}, {-384, 96}, {-448, 96}, {-480, 64}}, 0,
            112);
    r.solid({{-384, 208, 0}, {-320, 272, 96}});

    r.deck(96, 96, 816, 256, 128);
    r.deck(656, -256, 816, 96, 128);
    r.stairs(144, -192, 0, 1, 8);
    r.solid({{352, -96, 0}, {416, 16, 192}});

    // This second ramp ascends across the room, giving upper and lower
    // approaches different directions instead of repeating the Bench section.
    r.ramp({-624, 576, 128}, HorizontalDirection::positive_x, 160);
    r.deck(-144, 528, 16, 1120, 256);
    r.deck(-448, 960, -144, 1120, 256);
    r.solid({{-480, 896, 128}, {-384, 976, 224}});
    r.prism({{-704, 1024}, {-608, 1120}, {-704, 1120}}, 128, 576);

    r.deck(288, 832, 960, 1008, 256);
    r.deck(288, 704, 448, 832, 256);
    r.stairs(768, 544, 128, 1, 8);
    r.solid({{512, 704, 128}, {576, 800, 240}});
    r.prism({{864, 1008}, {960, 912}, {960, 1008}}, 128, 544);

    r.deck(-96, -864, 64, -336, 128);
    r.deck(-512, -864, -96, -704, 128);
    r.stairs(-384, -544, 0, 0, 8);
    r.solid({{-432, -640, 0}, {-368, -576, 96}});
    r.deck(144, -896, 304, -336, 128);
    r.deck(304, -896, 848, -736, 128);
    r.deck(608, -736, 848, -336, 128);
    r.stairs(320, -528, 0, 0, 8);
    r.solid({{448, -736, 0}, {544, -640, 112}});
    r.prism({{720, -896}, {848, -896}, {848, -768}}, 0, 384);

    r.spawn({-208, 128, 24}, 270);
    r.spawn({-224, 384, 152}, 180);
    r.spawn({736, -144, 24}, 180);
    r.spawn({-624, 864, 152}, 0);
    r.spawn({-64, 1040, 280}, 180);
    r.spawn({512, 944, 280}, 0);
    r.spawn({-432, -400, 24}, 270);
    r.spawn({224, -480, 24}, 270);

    r.point(PlacementRole::weapon, {-288, -96, 24}, 4);
    r.point(PlacementRole::weapon, {512, -160, 24}, 0, "weapon_supershotgun");
    r.point(PlacementRole::weapon, {-256, 864, 152}, 6);
    r.point(PlacementRole::weapon, {816, 928, 280}, 3);
    r.point(PlacementRole::weapon, {-320, -768, 152}, 0);
    r.point(PlacementRole::weapon, {736, -448, 152}, 5);
    r.point(PlacementRole::weapon, {544, 192, 152}, 2);
    r.point(PlacementRole::armor, {-304, 1040, 280});
    r.point(PlacementRole::armor, {224, -800, 152});
    r.point(PlacementRole::health, {-528, 240, 24}, 0, "item_health_mega");
    r.point(PlacementRole::health, {672, 896, 152}, 0, "item_quad");
    for (auto p : {GridVec3{-192, 208, 152},
                   {-400, 384, 152},
                   {480, 64, 24},
                   {736, 160, 152},
                   {-560, 1024, 152},
                   {368, 624, 152},
                   {-32, -736, 152},
                   {512, -816, 152}})
        r.point(PlacementRole::health, p);
    r.point(PlacementRole::health, {-288, -160, 24}, 0, "ammo_rockets");
    r.point(PlacementRole::health, {576, -160, 24}, 0, "ammo_shells");
    r.point(PlacementRole::health, {608, 192, 152}, 0, "ammo_bullets");
    r.point(PlacementRole::health, {-256, 800, 152}, 0, "ammo_slugs");
    r.point(PlacementRole::health, {752, 928, 280}, 0, "ammo_grenades");
    r.point(PlacementRole::health, {736, -512, 152}, 0, "ammo_cells");

    for (auto p : {GridVec3{-480, -160, 136},
                   {-304, 176, 120},
                   {-656, 64, 240},
                   {-592, 384, 248},
                   {-192, 368, 256},
                   {-704, 272, 264}})
        r.light(p, 370, {1000, 860, 640});
    for (auto p :
         {GridVec3{256, -48, 232}, {512, -128, 128}, {736, -128, 88}, {480, 192, 256}, {736, 144, 264}, {456, 32, 80}})
        r.light(p, 350);
    for (auto p : {GridVec3{-624, 832, 256},
                   {-432, 800, 240},
                   {-560, 1024, 256},
                   {-384, 656, 392},
                   {-64, 720, 416},
                   {-192, 1040, 416}})
        r.light(p, 370, {760, 900, 1000});
    for (auto p :
         {GridVec3{448, 560, 240}, {704, 704, 256}, {672, 912, 216}, {368, 800, 400}, {608, 928, 416}, {832, 752, 392}})
        r.light(p, 350);
    for (auto p : {GridVec3{-432, -512, 136},
                   {-256, -384, 128},
                   {-208, -608, 264},
                   {-288, -800, 264},
                   {-16, -448, 264},
                   {-16, -736, 264}})
        r.light(p, 340);
    for (auto p : {GridVec3{240, -560, 88},
                   {432, -432, 232},
                   {512, -656, 168},
                   {736, -608, 264},
                   {240, -800, 264},
                   {544, -816, 264}})
        r.light(p, 360);
    r.light({624, 720, 256}, 350);
    r.light({-224, -600, 192}, 350);
    for (const auto &connection : r.level().connections)
        for (const auto &b : connection.passage_volumes) {
            auto p = b.center();
            p.z = b.max.z - 24;
            r.light(p, 360);
        }
    r.finish(std::filesystem::absolute(argv[1]));
    if (r.result.quality->score.warnings)
        throw std::runtime_error("Talus advisory findings require review");
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
