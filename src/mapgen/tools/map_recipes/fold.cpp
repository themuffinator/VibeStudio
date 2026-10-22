// Original compact packing experiment: five adjoining unequal spaces, an
// actual horseshoe footprint, stacked ordinary routes and an optional wash bay.
#include "recipe_support.hpp"
using namespace qmg;

int main(int argc, char **argv) try {
    if (argc != 2)
        throw std::runtime_error("usage: qmg_fold_recipe <output directory>");
    recipes::Recipe r("qmg_fold", "Fold",
                      "Compact asymmetric Quake II folded maintenance works, adjoining unequal spaces, "
                      "central atrium, true horseshoe, interleaved upper and lower routes, crate steps, "
                      "optional shallow water and a committed one way teleporter",
                      20260917, 5);
    r.floor_levels = {0, 128};
    r.require_ordinary_routes = true;
    r.require_no_drop_routes = true;
    r.material_overrides["e1u1/water1"] = "e3u2/water2";
    r.quake2_material_metadata_overrides["e3u2/water2"] = {32, 24, 0};

    // A pinwheel partition uses almost the entire 1792 x 1664 envelope. Short
    // 48-unit seams carry staggered openings, not exterior corridor sprawl.
    r.room("Fold Atrium", {{-272, -256, 0}, {336, 352, 384}});
    r.room("Horseshoe", {{-896, -256, 0}, {-320, 832, 384}});
    r.room("Lintel", {{-272, 400, 128}, {896, 832, 416}});
    r.room("Wash Bay", {{384, -832, 0}, {896, 352, 384}});
    r.room("Press", {{-896, -832, 0}, {336, -304, 352}});
    // The short opening still retains a positive hull-eroded center interval.
    r.connection(1, 2, 160, {{{-320, -128, 0}, {-272, 32, 192}}});
    r.connection(1, 2, 144, {{{-320, 192, 128}, {-272, 336, 320}}});
    r.connection(1, 3, 160, {{{64, 352, 128}, {224, 400, 320}}});
    r.connection(1, 4, 160, {{{336, -160, 0}, {384, 0, 192}}});
    r.connection(1, 5, 160, {{{-80, -304, 0}, {80, -256, 192}}});
    r.connection(2, 3, 160, {{{-320, 608, 128}, {-272, 768, 320}}});
    r.connection(2, 5, 160, {{{-800, -304, 128}, {-640, -256, 320}}});
    r.connection(3, 4, 160, {{{672, 352, 128}, {832, 400, 320}}});
    r.connection(4, 5, 160, {{{336, -704, 128}, {384, -544, 320}}});

    // The north/east balcony leaves 104 units of usable lower clearance. Its
    // stair run ends exactly at the north deck, outside that deck's footprint.
    r.deck(-272, 160, 336, 352, 128);
    r.deck(176, -96, 336, 160, 128);
    r.stairs(-240, -128, 0, 1, 8);
    r.solid({{-16, -64, 0}, {64, 16, 128}});
    r.solid({{64, 160, 128}, {128, 224, 224}});
    r.prism({{-272, -256}, {-192, -256}, {-272, -176}}, 0, 384);
    r.prism({{256, 352}, {336, 272}, {336, 352}}, 256, 384);

    // A west-attached service core removes the middle of the footprint. The
    // lower U route crosses the southern arm, eastern spine and northern arm;
    // the continuous high strips overlook selected parts of that same route.
    const GridBounds horseshoe_core{{-896, 64, 0}, {-640, 544, 384}};
    r.solid(horseshoe_core);
    r.deck(-896, -256, -320, -96, 128);
    r.deck(-464, -96, -320, 640, 128);
    r.deck(-896, 640, -320, 832, 128);
    r.stairs(-752, -80, 0, 0, 8);
    r.solid({{-576, 704, 128}, {-512, 800, 240}});

    // Lintel is a short upper crossing with two offset solid screens. It adds
    // a second return around the atrium without a redundant third floor.
    r.solid({{192, 624, 128}, {384, 832, 416}});
    r.solid({{-112, 592, 128}, {-16, 688, 240}});
    r.solid({{400, 448, 128}, {480, 528, 224}});

    // Five abutting balcony strips form one upper route; none overlaps the
    // stair's footprint. The basin below has wide dry paths on both sides.
    r.deck(384, -832, 896, -672, 128);
    r.deck(384, -672, 528, -160, 128);
    r.deck(384, -160, 752, 160, 128);
    r.deck(752, -288, 896, 352, 128);
    r.deck(608, 192, 752, 352, 128);
    r.stairs(752, -576, 0, 1, 8);
    r.solid({{640, -544, 0}, {720, -464, 128}});
    const GridBounds water_bounds{{512, -64, 0}, {752, 160, 24}};
    const std::array<GridBounds, 4> basin_curbs{{
        {{496, -80, 0}, {512, 176, 24}}, {{752, -80, 0}, {768, 176, 24}},
        {{512, -80, 0}, {752, -64, 24}}, {{512, 160, 0}, {752, 176, 24}}}};
    const std::array<GridBounds, 4> wet_steps{{
        {{568, -116, 0}, {696, -80, 16}}, {{568, -64, 0}, {696, -28, 16}},
        {{568, 124, 0}, {696, 160, 16}}, {{568, 176, 0}, {696, 212, 16}}}};
    for (const auto &b : basin_curbs)
        r.solid(b, MaterialRole::wall);
    // These belong to the optional wet basin contract. The ordinary analyzer
    // blocks water and must not advertise its submerged steps as dry routes.
    for (const auto &b : wet_steps)
        r.solid(b, MaterialRole::stair, BrushPurpose::structural);

    r.deck(-896, -448, 336, -304, 128);
    r.deck(176, -832, 336, -448, 128);
    r.stairs(-560, -736, 0, 1, 8);
    r.solid({{-224, -688, 0}, {-96, -560, 112}});
    // An explicit four-step crate stack earns a small raised pickup position;
    // this 64-unit prop is not another room floor or a required jump shortcut.
    r.stairs(-112, -816, 0, 0, 4);
    const GridBounds pickup_crate{{32, -816, 0}, {160, -688, 64}};
    r.solid(pickup_crate, MaterialRole::platform, BrushPurpose::raised_platform);

    r.spawn({-144, -192, 24}, 0);
    r.spawn({256, 272, 152}, 180);
    r.spawn({-544, -208, 24}, 90);
    r.spawn({-752, 736, 152}, 270);
    r.spawn({-192, 752, 152}, 270);
    r.spawn({624, -384, 24}, 180);
    r.spawn({816, -720, 152}, 180);
    r.spawn({-784, -688, 24}, 0);

    r.point(PlacementRole::weapon, {80, -112, 24}, 4);
    r.point(PlacementRole::weapon, {-752, -176, 152}, 0, "weapon_supershotgun");
    r.point(PlacementRole::weapon, {-384, 432, 152}, 2);
    r.point(PlacementRole::weapon, {544, 656, 152}, 6);
    r.point(PlacementRole::weapon, {-64, 512, 152}, 3);
    r.point(PlacementRole::weapon, {704, -752, 152}, 5);
    r.point(PlacementRole::weapon, {-608, -368, 152}, 0);
    r.point(PlacementRole::armor, {256, 32, 152});
    r.point(PlacementRole::armor, {-464, 592, 24});
    r.point(PlacementRole::health, {-32, 272, 24}, 0, "item_health_mega");
    r.point(PlacementRole::health, {96, -752, 88}, 0, "item_quad");
    for (auto p : {GridVec3{-80, 80, 24}, {-144, 272, 152}, {-688, 608, 24}, {-816, -64, 24},
                   {736, 736, 152}, {432, -432, 24}, {832, -224, 152}, {-320, -752, 24}})
        r.point(PlacementRole::health, p);
    r.point(PlacementRole::health, {80, -176, 24}, 0, "ammo_rockets");
    r.point(PlacementRole::health, {-688, -176, 152}, 0, "ammo_shells");
    r.point(PlacementRole::health, {-384, 496, 152}, 0, "ammo_bullets");
    r.point(PlacementRole::health, {608, 656, 152}, 0, "ammo_slugs");
    r.point(PlacementRole::health, {-128, 512, 152}, 0, "ammo_grenades");
    r.point(PlacementRole::health, {704, -688, 152}, 0, "ammo_cells");

    LiquidSpec water{{1}, water_bounds, BrushContents::water, BrushOwnership::structural};
    TeleporterAssemblySpec tele;
    tele.key = {2};
    tele.trigger_bounds = {{-832, 576, 0}, {-768, 640, 48}};
    tele.destination_origin = {816, 96, 152};
    tele.destination_angle_degrees = 270;
    r.assemble({water, tele});

    for (auto p : {GridVec3{-160, -160, 112}, {-80, 80, 88}, {-32, 272, 80}, {256, 48, 88},
                   {-144, 272, 264}, {256, 240, 264}, {256, 16, 264}, {-80, 48, 264}})
        r.light(p, 350, {1000, 860, 680});
    for (auto p : {GridVec3{-752, -192, 80}, {-544, -208, 80}, {-816, -32, 160}, {-528, 256, 192},
                   {-384, 176, 80}, {-384, 512, 80}, {-688, 592, 112}, {-816, 736, 264},
                   {-384, 720, 264}, {-384, 432, 264}, {-640, -192, 264}, {-800, 608, 208}})
        r.light(p, 350);
    for (auto p : {GridVec3{-176, 752, 288}, {-144, 480, 288}, {96, 720, 304}, {240, 496, 288},
                   {544, 688, 288}, {768, 512, 288}, {800, 768, 288}})
        r.light(p, 350, {860, 960, 1000});
    for (auto p : {GridVec3{624, -752, 80}, {624, -752, 264}, {432, -480, 80}, {624, -384, 176},
                   {816, -432, 240}, {432, -16, 80}, {816, 16, 80}, {632, 48, 80},
                   {512, 96, 264}, {816, 80, 264}, {736, 272, 264}, {624, 272, 80}})
        r.light(p, 350, {680, 880, 1000});
    for (auto p : {GridVec3{-784, -688, 144}, {-640, -368, 264}, {-592, -368, 80}, {-320, -752, 144},
                   {-128, -464, 88}, {16, -368, 264}, {96, -752, 216}, {256, -608, 264}})
        r.light(p, 350, {1000, 800, 600});
    for (const auto &connection : r.level().connections)
        for (const auto &b : connection.passage_volumes) {
            auto p = b.center();
            p.z = b.max.z - 24;
            r.light(p, 360);
        }
    const auto output = std::filesystem::absolute(argv[1]);
    r.finish(output);
    if (r.result.quality->score.warnings)
        throw std::runtime_error("Fold advisory findings require review");

    std::ofstream features(output / "features.json", std::ios::binary);
    features << "{\"schema_version\":1,\"map\":\"qmg_fold\",\"layout_sha256\":"
             << std::quoted(r.result.player_navigation->layout_sha256)
             << ",\"main_floors\":[0,128],"
                "\"play_volume_envelope\":[[-896,-832,0],[896,832,416]],"
                "\"room_seam_width\":48,\"horseshoe_service_core\":";
    recipes::bounds_json(features, horseshoe_core);
    features << ",\"horseshoe_route\":[[-752,-192,0],[-384,-192,0],[-384,592,0],[-752,592,0]],"
                "\"flat_segments\":[{\"label\":\"Atrium lower passage\",\"from\":[-80,240,0],\"to\":[144,240,0]},"
                "{\"label\":\"Atrium balcony above\",\"from\":[-80,240,128],\"to\":[144,240,128]},"
                "{\"label\":\"Horseshoe south arm\",\"from\":[-752,-192,0],\"to\":[-384,-192,0]},"
                "{\"label\":\"Horseshoe east spine\",\"from\":[-384,-192,0],\"to\":[-384,592,0]},"
                "{\"label\":\"Horseshoe north arm\",\"from\":[-384,592,0],\"to\":[-752,592,0]},"
                "{\"label\":\"Optional crate top\",\"from\":[64,-752,64],\"to\":[128,-752,64]},"
                "{\"label\":\"Dry wash bypass\",\"from\":[432,-144,0],\"to\":[432,240,0]},"
                "{\"label\":\"Optional wet inner floor only\",\"from\":[632,0,0],\"to\":[632,96,0]}],"
                "\"crate_steps\":{\"bounds\":";
    recipes::bounds_json(features, pickup_crate);
    features << ",\"step_start\":[-112,-816,0],\"direction\":\"positive_x\",\"count\":4,\"run\":36,"
                "\"rise\":16,\"width\":128,\"role\":\"Optional ordinary ascent to Quad crate\"},"
                "\"cover_crates\":[[[-16,-64,0],[64,16,128]],[[64,160,128],[128,224,224]],"
                "[[-576,704,128],[-512,800,240]],"
                "[[640,-544,0],[720,-464,128]],[[-224,-688,0],[-96,-560,112]]],"
                "\"water\":{\"bounds\":";
    recipes::bounds_json(features, water_bounds);
    features << ",\"material\":\"e3u2/water2\",\"contents\":32,\"surface_flags\":24,\"value\":0,"
                "\"entry\":[632,-144,0],\"exit\":[632,240,0],\"dry_alternative\":[[432,-144,0],[432,240,0]],"
                "\"crossing_poses\":[[632,-144,0],[632,-132,0],[632,-132,16],[632,-100,16],"
                "[632,-96,16],[632,-96,24],[632,-48,24],[632,-48,16],[632,-12,16],[632,-12,0],"
                "[632,108,0],[632,108,16],[632,140,16],[632,144,16],[632,144,24],[632,192,24],"
                "[632,192,16],[632,228,16],[632,228,0],[632,240,0]],\"curbs\":[";
    for (std::size_t i = 0; i < basin_curbs.size(); ++i) {
        if (i)
            features << ',';
        recipes::bounds_json(features, basin_curbs[i]);
    }
    features << "],\"steps\":[";
    for (std::size_t i = 0; i < wet_steps.size(); ++i) {
        if (i)
            features << ',';
        recipes::bounds_json(features, wet_steps[i]);
    }
    features << "],\"ordinary_graph_uses_water\":false,\"native_wading_verified\":false},"
                "\"teleporter\":{\"source_origin\":[-800,608,24],\"destination_origin\":[816,96,152],"
                "\"arrival_yaw\":270,\"optional\":true},\"native_movement_verified\":false}";
    if (!features)
        throw std::runtime_error("Fold feature export failed");
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
