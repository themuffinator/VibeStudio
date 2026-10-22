// Original drop-and-return experiment. Three local floors in the Sluice,
// staggered surrounding encounters, ordinary circulation and optional descents.
#include "recipe_support.hpp"
using namespace qmg;

int main(int argc, char **argv) try {
    if (argc != 2)
        throw std::runtime_error("usage: qmg_undercut_recipe <output directory>");
    recipes::Recipe r("qmg_undercut", "Undercut",
                      "Asymmetric Quake II maintenance undercroft, deliberate drops, three local combat floors, "
                      "offset encounters and ordinary return loops",
                      20260916, 5);
    r.floor_levels = {0, 128, 256};
    r.require_ordinary_routes = true;
    r.require_no_drop_routes = true;
    r.room("Sluice", {{-384, -192, 0}, {320, 448, 448}});
    r.room("Cistern", {{448, 64, 0}, {1024, 640, 384}});
    r.room("Crown", {{-512, 560, 128}, {320, 1008, 512}});
    r.room("Tailrace", {{-1008, -640, 0}, {-512, 128, 352}});
    r.room("Return", {{-64, -832, 0}, {640, -320, 384}});
    r.connection(1, 2, 160, {{{320, 288, 128}, {448, 448, 320}}});
    r.connection(1, 3, 160, {{{96, 448, 256}, {256, 560, 448}}});
    r.connection(1, 4, 160, {{{-512, -144, 0}, {-384, 16, 192}}});
    r.connection(1, 5, 160, {{{96, -320, 0}, {256, -192, 192}}});
    r.connection(2, 3, 160, {{{448, 640, 128}, {608, 880, 320}}, {{320, 720, 128}, {448, 880, 320}}});
    r.connection(3, 4, 160, {{{-672, 128, 128}, {-512, 720, 320}}, {{-512, 560, 128}, {-352, 720, 320}}});
    r.connection(4, 5, 160, {{{-512, -608, 128}, {-64, -448, 320}}});
    r.connection(2, 5, 160, {{{640, -480, 128}, {800, -320, 320}}, {{640, -336, 128}, {800, 64, 320}}});

    // The three-storey Sluice interleaves two ordinary climbs and two separate
    // descents; neither drop supplies mandatory circulation.
    r.deck(-384, 224, 320, 448, 128);
    r.stairs(-336, -64, 0, 1, 8);
    r.deck(160, 64, 320, 448, 256);
    r.deck(48, 288, 160, 448, 256);
    r.stairs(-240, 288, 128, 0, 8);
    r.solid({{160, 288, 256}, {224, 352, 352}});
    r.solid({{32, 32, 0}, {96, 96, 112}});
    r.prism({{-128, 0}, {-96, -32}, {-32, -32}, {0, 0}, {0, 64}, {-32, 96}, {-96, 96}, {-128, 64}}, 0, 96);
    r.prism({{-384, -192}, {-288, -192}, {-384, -96}}, 0, 448);
    r.drop_specs.push_back({"Upper ledge to middle return", {176, 248, 256}, {112, 248, 256}, {112, 248, 128}});
    r.drop_specs.push_back({"Middle rim to Mega pocket", {-64, 240, 128}, {-64, 176, 128}, {-64, 176, 0}});

    r.deck(448, 448, 1024, 640, 128);
    r.deck(448, 64, 800, 224, 128);
    r.deck(448, 224, 608, 448, 128);
    r.stairs(848, 160, 0, 1, 8);
    r.solid({{672, 272, 0}, {736, 336, 160}});
    r.prism({{928, 640}, {1024, 544}, {1024, 640}}, 0, 384);

    r.deck(96, 560, 320, 720, 256);
    r.deck(160, 720, 320, 1008, 256);
    r.stairs(-192, 576, 128, 0, 8);
    r.solid({{-64, 800, 128}, {0, 928, 384}});
    r.solid({{160, 736, 256}, {224, 800, 352}});
    r.prism({{-512, 912}, {-416, 1008}, {-512, 1008}}, 128, 512);

    // Filled northwest corner makes Tailrace an actual L-shaped encounter.
    r.deck(-672, -640, -512, 128, 128);
    r.deck(-1008, -640, -672, -480, 128);
    r.stairs(-960, -400, 0, 0, 8);
    r.solid({{-1008, -96, 0}, {-864, 128, 352}});
    r.solid({{-896, -160, 0}, {-832, -96, 112}});

    r.deck(-64, -480, 640, -320, 128);
    r.deck(480, -832, 640, -480, 128);
    r.deck(-64, -640, 96, -480, 128);
    r.stairs(144, -768, 0, 1, 8);
    r.solid({{320, -672, 0}, {384, -576, 112}});
    r.prism({{544, -832}, {640, -832}, {640, -736}}, 0, 384);

    r.spawn({-240, -144, 24}, 0);
    r.spawn({80, 400, 280}, 270);
    r.spawn({800, 384, 24}, 180);
    r.spawn({512, 560, 152}, 0);
    r.spawn({-400, 864, 152}, 270);
    r.spawn({240, 896, 280}, 270);
    r.spawn({-592, -512, 152}, 90);
    r.spawn({400, -704, 24}, 90);

    r.point(PlacementRole::weapon, {96, -80, 24}, 4);
    r.point(PlacementRole::weapon, {768, 160, 24}, 0, "weapon_supershotgun");
    r.point(PlacementRole::weapon, {736, 544, 152}, 5);
    r.point(PlacementRole::weapon, {-352, 736, 152}, 6);
    r.point(PlacementRole::weapon, {224, 640, 280}, 3);
    r.point(PlacementRole::weapon, {-896, -560, 152}, 0);
    r.point(PlacementRole::weapon, {320, -400, 152}, 2);
    r.point(PlacementRole::armor, {256, 144, 280});
    r.point(PlacementRole::armor, {-768, -432, 24});
    r.point(PlacementRole::health, {-64, 176, 24}, 0, "item_health_mega");
    r.point(PlacementRole::health, {32, -736, 24}, 0, "item_quad");
    for (auto p : {GridVec3{-256, 400, 152},
                   {-128, -80, 24},
                   {896, 96, 24},
                   {512, 352, 152},
                   {-400, 944, 152},
                   {240, 800, 280},
                   {-768, 64, 24},
                   {512, -608, 152}})
        r.point(PlacementRole::health, p);
    r.point(PlacementRole::health, {96, -144, 24}, 0, "ammo_rockets");
    r.point(PlacementRole::health, {704, 160, 24}, 0, "ammo_shells");
    r.point(PlacementRole::health, {800, 544, 152}, 0, "ammo_cells");
    r.point(PlacementRole::health, {-352, 672, 152}, 0, "ammo_slugs");
    r.point(PlacementRole::health, {160, 640, 280}, 0, "ammo_grenades");
    r.point(PlacementRole::health, {384, -400, 152}, 0, "ammo_bullets");

    for (auto p : {GridVec3{-240, -144, 128},
                   {-32, 128, 120},
                   {-64, 176, 120},
                   {-192, 192, 240},
                   {-192, 368, 224},
                   {240, 176, 384},
                   {256, 400, 400}})
        r.light(p, 360, {1000, 840, 640});
    for (auto p : {GridVec3{736, 352, 192},
                   {704, 144, 88},
                   {928, 320, 88},
                   {512, 352, 264},
                   {736, 544, 264},
                   {928, 512, 264},
                   {912, 128, 160}})
        r.light(p, 360, {720, 900, 1000});
    for (auto p : {GridVec3{-96, 784, 240},
                   {-352, 736, 264},
                   {-352, 944, 264},
                   {-48, 656, 400},
                   {224, 640, 400},
                   {240, 864, 400}})
        r.light(p, 370);
    for (auto p : {GridVec3{-784, -256, 240},
                   {-768, 32, 192},
                   {-592, -128, 256},
                   {-592, -512, 264},
                   {-896, -560, 264},
                   {-768, -432, 136}})
        r.light(p, 360);
    for (auto p : {GridVec3{288, -576, 160},
                   {32, -736, 160},
                   {400, -736, 192},
                   {32, -432, 264},
                   {320, -400, 264},
                   {544, -608, 264}})
        r.light(p, 360);
    for (const auto &connection : r.level().connections)
        for (const auto &b : connection.passage_volumes) {
            auto p = b.center();
            p.z = b.max.z - 24;
            r.light(p, 360);
        }
    r.finish(std::filesystem::absolute(argv[1]));
    if (r.result.quality->score.warnings)
        throw std::runtime_error("Undercut advisory findings require review");
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
