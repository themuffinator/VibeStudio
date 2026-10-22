// Original teleporter-arrival experiment. This is an explicit recipe, not a
// default-generator capability; no archive geometry is incorporated.
#include "recipe_support.hpp"

using namespace qmg;

namespace {

void write_points(std::ostream &out, const std::vector<GridVec3> &points) {
    out << '[';
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (i)
            out << ',';
        recipes::vector_json(out, points[i]);
    }
    out << ']';
}

struct StudyPath {
    const char *id;
    const char *kind;
    std::vector<GridVec3> points;
    unsigned exit_connection{};
};

} // namespace

int main(int argc, char **argv) try {
    if (argc != 2)
        throw std::runtime_error("usage: qmg_relay_recipe <output directory>");
    const auto output = std::filesystem::absolute(argv[1]);
    recipes::Recipe r("qmg_relay", "Relay",
                      "Compact asymmetric Quake II switching works, an offset perimeter arrival pocket, "
                      "two walking departures, a resource after a corner, selective upper connections "
                      "and one optional one way teleporter with an ordinary bypass",
                      20260924, 6);
    r.floor_levels = {0, 128};
    r.require_ordinary_routes = r.require_no_drop_routes = true;

    // Unequal adjoining bands wrap around the small crossover. The narrow
    // 48-unit seams are occupied by offset passages, not exterior corridors.
    r.room("Arrival Gallery", {{-864, 288, 0}, {288, 800, 384}});
    r.room("Relay Atrium", {{336, -256, 0}, {864, 800, 384}});
    r.room("Crossover", {{-288, -256, 0}, {288, 240, 352}});
    r.room("Elbow", {{-864, -256, 0}, {-336, 240, 320}});
    r.room("Source Bay", {{-864, -800, 0}, {-336, -304, 320}});
    r.room("Switch Hall", {{-288, -800, 0}, {864, -304, 352}});

    r.connection(1, 4, 160, {{{-784, 240, 0}, {-624, 288, 192}}});
    r.connection(1, 3, 160, {{{96, 240, 0}, {256, 288, 192}}});
    r.connection(1, 2, 160, {{{288, 544, 0}, {336, 704, 192}}});
    r.connection(4, 3, 160, {{{-336, -144, 0}, {-288, 16, 192}}});
    r.connection(4, 5, 160, {{{-800, -304, 0}, {-640, -256, 192}}});
    r.connection(5, 6, 160, {{{-336, -736, 0}, {-288, -576, 192}}});
    r.connection(3, 6, 160, {{{-64, -304, 0}, {96, -256, 192}}});
    r.connection(6, 2, 160, {{{624, -304, 0}, {784, -256, 192}}});
    r.connection(3, 2, 160, {{{288, -160, 0}, {336, 0, 192}}});
    r.connection(3, 2, 160, {{{288, 64, 128}, {336, 224, 320}}});

    // A ceiling-height partition and the rear perimeter wall make a recessed
    // gallery with two open ends. The floor stays flat throughout. No resource
    // or spawn occupies the arrival pocket; the chaingun is beyond its corner.
    const GridBounds partition{{-672, 512, 0}, {-224, 544, 384}};
    r.solid(partition);
    const auto partition_id = r.level().solids.back().id;

    // Wall-attached returns shape the remaining ground rooms. These are not
    // jump props: the lower loop bends around them on full supported floor.
    r.solid({{-864, -32, 0}, {-688, 64, 320}});
    r.solid({{-544, -800, 0}, {-336, -672, 320}});
    r.solid({{96, -800, 0}, {256, -544, 352}});
    r.solid({{448, -416, 0}, {608, -304, 352}});

    // The upper crossing joins two different rooms. Both have their own
    // ordinary stairs; 104 units remain below each 24-thick deck.
    r.deck(-288, 64, 288, 224, 128);
    r.stairs(-256, -224, 0, 1, 8);
    r.deck(336, -256, 496, 800, 128);
    r.deck(496, 128, 864, 288, 128);
    r.stairs(656, -160, 0, 1, 8);
    r.solid({{704, 432, 0}, {864, 496, 384}});

    r.spawn({-800, 176, 24}, 0);
    r.spawn({-400, -384, 24}, 180);
    r.spawn({-160, -736, 24}, 90);
    r.spawn({736, -432, 24}, 270);
    r.spawn({144, -176, 24}, 180);
    r.spawn({-64, 144, 152}, 0);
    r.spawn({784, 208, 152}, 180);
    r.spawn({784, 688, 24}, 270);

    const GridVec3 resource_origin{-64, 400, 24};
    const auto resource_id = r.point(PlacementRole::weapon, resource_origin, 2, "weapon_chaingun");
    r.point(PlacementRole::weapon, {-576, -176, 24}, 0, "weapon_supershotgun");
    r.point(PlacementRole::weapon, {-464, -544, 24}, 4, "weapon_rocketlauncher");
    r.point(PlacementRole::weapon, {-160, -464, 24}, 3, "weapon_grenadelauncher");
    r.point(PlacementRole::weapon, {688, -704, 24}, 6, "weapon_railgun");
    r.point(PlacementRole::weapon, {432, 32, 152}, 5, "weapon_hyperblaster");
    r.point(PlacementRole::weapon, {-176, 144, 152}, 0, "weapon_shotgun");
    r.point(PlacementRole::armor, {-480, 160, 24});
    r.point(PlacementRole::armor, {416, 672, 152});
    r.point(PlacementRole::health, {352, -672, 24}, 0, "item_health_mega");
    r.point(PlacementRole::health, {560, 208, 152}, 0, "item_quad");
    for (const auto point : {GridVec3{-768, 384, 24}, {160, 448, 24}, {-448, -64, 24}, {-800, -432, 24},
                             {-112, -624, 24}, {784, -560, 24}, {560, 608, 24}, {96, 144, 152}})
        r.point(PlacementRole::health, point);
    r.point(PlacementRole::health, {-128, 400, 24}, 0, "ammo_bullets");
    r.point(PlacementRole::health, {-512, -176, 24}, 0, "ammo_shells");
    r.point(PlacementRole::health, {-464, -480, 24}, 0, "ammo_rockets");
    r.point(PlacementRole::health, {-96, -464, 24}, 0, "ammo_grenades");
    r.point(PlacementRole::health, {752, -704, 24}, 0, "ammo_slugs");
    r.point(PlacementRole::health, {432, -32, 152}, 0, "ammo_cells");

    const GridVec3 source_origin{-704, -624, 24};
    const GridVec3 destination_origin{-480, 672, 24};
    constexpr int arrival_yaw = 0;
    TeleporterAssemblySpec teleporter;
    teleporter.key = {1};
    teleporter.trigger_bounds = {{-736, -656, 0}, {-672, -592, 48}};
    teleporter.destination_origin = destination_origin;
    teleporter.destination_angle_degrees = arrival_yaw;
    r.assemble({teleporter});

    // Distinct warm source and cool arrival lighting identifies the study's
    // two stations. This is an intended cue, not tested player comprehension.
    for (const auto point : {GridVec3{-768, 656, 184}, {-480, 688, 184}, {-128, 656, 184},
                             {-688, 384, 192}, {-256, 384, 192}, {128, 416, 192}})
        r.light(point, 370, {700, 880, 1000});
    for (const auto point : {GridVec3{-768, -672, 192}, {-688, -432, 192}, {-416, -560, 184},
                             {-752, -160, 192}, {-560, 32, 192}, {-752, 176, 192}})
        r.light(point, 350, {1000, 820, 640});
    for (const auto point : {GridVec3{-160, -688, 192}, {-96, -432, 192}, {352, -656, 208},
                             {704, -656, 192}, {752, -416, 192}, {368, -464, 208}})
        r.light(point, 360);
    for (const auto point : {GridVec3{-64, -144, 192}, {-64, 144, 272}, {96, 144, 80},
                             {416, -144, 80}, {416, 176, 80}, {416, 624, 80},
                             {416, -112, 272}, {416, 400, 272}, {416, 688, 272},
                             {720, 208, 272}, {672, -128, 208}, {656, 624, 192}})
        r.light(point, 350);
    for (const auto &connection : r.level().connections) {
        auto point = connection.passage_volumes.front().center();
        point.z = connection.passage_volumes.front().min.z + 144;
        r.light(point, 320);
    }
    r.finish(output);

    // Feet-based flat routes start off the native discs. Native arrival and
    // disc departure are separate contracts: do not sweep floor zero through
    // an eight-unit entity disc and call that a native movement trajectory.
    const std::vector<StudyPath> paths{
        {"west_departure", "departure",
         {{-608, 672, 0}, {-768, 672, 0}, {-768, 384, 0}, {-704, 384, 0}, {-704, 176, 0}}, 1},
        {"east_departure", "departure",
         {{-352, 672, 0}, {-160, 672, 0}, {-160, 400, 0}, {-64, 400, 0}, {176, 400, 0}, {176, 176, 0}}, 2},
        {"source_to_chaingun_ordinary", "ordinary_bypass",
         {{-704, -720, 0}, {-816, -720, 0}, {-816, -384, 0}, {-704, -384, 0}, {-704, -160, 0},
          {-576, -160, 0}, {-576, 144, 0}, {-704, 144, 0}, {-704, 384, 0}, {-64, 384, 0}, {-64, 400, 0}}},
        {"arrival_to_chaingun", "resource_approach",
         {{-352, 672, 0}, {-160, 672, 0}, {-160, 400, 0}, {-64, 400, 0}}}};
    std::ofstream features(output / "features.json", std::ios::binary);
    features << "{\"schema_version\":1,\"map\":\"qmg_relay\",\"layout_sha256\":"
             << std::quoted(r.result.player_navigation->layout_sha256)
             << ",\"source_approach\":[-704,-720,0],\"teleporter\":{\"source_origin\":";
    recipes::vector_json(features, source_origin);
    features << ",\"destination_origin\":";
    recipes::vector_json(features, destination_origin);
    features << ",\"arrival_yaw\":" << arrival_yaw << "},\"partition\":{\"solid_id\":" << partition_id.value
             << ",\"bounds\":";
    recipes::bounds_json(features, partition);
    features << "},\"resource\":{\"placement_id\":" << resource_id.value
             << ",\"classname\":\"weapon_chaingun\",\"origin\":";
    recipes::vector_json(features, resource_origin);
    features << "},\"paths\":[";
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (i)
            features << ',';
        const auto &path = paths[i];
        features << "{\"id\":" << std::quoted(path.id) << ",\"kind\":" << std::quoted(path.kind) << ",\"points\":";
        write_points(features, path.points);
        if (path.exit_connection)
            features << ",\"exit_connection\":" << path.exit_connection;
        features << '}';
    }
    features << "],\"observer_stations\":["
                "{\"id\":\"east_off_disc\",\"feet\":[-352,672,0]},"
                "{\"id\":\"west_off_disc\",\"feet\":[-608,672,0]},"
                "{\"id\":\"resource\",\"feet\":[-64,400,0]},"
                "{\"id\":\"west_exit\",\"feet\":[-704,176,0]},"
                "{\"id\":\"center_exit\",\"feet\":[176,176,0]}],"
                "\"limits\":[\"Original fixed recipe; default generator and ranker unchanged.\","
                "\"Static paths do not execute teleport activation, disc collision or occupied arrivals.\"]}";
    if (!features)
        throw std::runtime_error("Relay features write failed");
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
