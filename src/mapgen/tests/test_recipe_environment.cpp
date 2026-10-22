#include "../tools/map_recipes/recipe_support.hpp"
#include <iostream>

using namespace qmg;

namespace {
void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
template <typename F> void rejects(F action, const char *message) {
    bool rejected = false;
    try { action(); } catch (const std::exception &) { rejected = true; }
    require(rejected, message);
}
recipes::Recipe fixture() {
    recipes::Recipe recipe("qmg_environment_test", "Environment test", "Quake II deathmatch", 19, 2);
    recipe.room("Court", {{0, 0, 0}, {512, 512, 384}});
    recipe.room("Indoor", {{560, 0, 0}, {1072, 512, 384}});
    return recipe;
}
MapBuildResult build(recipes::Recipe &recipe) {
    MapBuildOptions options;
    options.dialect = MapDialect::quake2_valve220;
    return MapBuilder{}.build(recipe.level(), options);
}
} // namespace

int main() try {
    auto recipe = fixture();
    auto built = build(recipe);
    const auto before = MapDocumentWriter{}.write(built.document);
    recipe.apply_environment(built.document, built.manifest);
    require(MapDocumentWriter{}.write(built.document) == before, "Empty environment changed legacy source");

    recipe.sky_rooms.insert(RoomId{1});
    recipe.worldspawn_properties["sky"] = "unit1_";
    recipe.worldspawn_properties["_sunlight"] = "180";
    recipe.apply_environment(built.document, built.manifest);
    std::set<MapNodeId> court, indoor;
    for (const auto &entry : built.manifest) {
        if (entry.source.kind != MapBuildSourceKind::room) continue;
        auto &nodes = entry.source.id == 1 ? court : indoor;
        nodes.insert(entry.node_ids.begin(), entry.node_ids.end());
    }
    unsigned sky_count = 0, indoor_ceiling_count = 0;
    std::set<MapNodeId> ids;
    for (const auto &entity : built.document.entities()) {
        require(ids.insert(entity.id).second, "Duplicate entity ID");
        for (const auto &property : entity.properties)
            require(ids.insert(property.id).second, "Environment property ID collided");
        for (const auto &primitive : entity.primitives) {
            const auto &brush = std::get<MapBrush>(primitive);
            require(ids.insert(brush.id).second, "Duplicate brush ID");
            for (const auto &face : brush.faces) {
                require(ids.insert(face.id).second, "Duplicate face ID");
                if (face.material == "e1u1/sky1") {
                    ++sky_count;
                    require(court.contains(brush.id) && !indoor.contains(brush.id), "Sky leaked into adjacent room");
                    require(face.quake2_metadata && face.quake2_metadata->contents == 1 &&
                            face.quake2_metadata->surface_flags == 4 && face.quake2_metadata->value == 0,
                            "Wrong sky physics or surface flags");
                    for (const auto &point : face.plane_points)
                        require(point.z == 384, "Sky assigned to another plane");
                }
                if (indoor.contains(brush.id) && face.material == "e1u1/ceil1_4")
                    ++indoor_ceiling_count;
            }
        }
    }
    require(sky_count > 0 && indoor_ceiling_count > 0, "Sky/indoor ceiling distinction missing");
    const auto &world = built.document.entities().front();
    require(world.first_value("sky") == "unit1_" && world.first_value("_sunlight") == "180", "Missing world properties");
    auto wrong = fixture();
    auto wrong_built = build(wrong);
    wrong.sky_rooms.insert(RoomId{99});
    rejects([&] { wrong.apply_environment(wrong_built.document, wrong_built.manifest); }, "Unknown sky room accepted");
    wrong.sky_rooms = {RoomId{1}};
    rejects([&] { wrong.apply_environment(wrong_built.document, {}); }, "Missing ceiling provenance accepted");
    wrong.sky_rooms.clear();
    wrong.worldspawn_properties["classname"] = "other";
    rejects([&] { wrong.apply_environment(wrong_built.document, wrong_built.manifest); }, "Worldspawn replacement accepted");
    std::cout << "Environment fixture: legacy source, isolated sky, metadata, IDs and invalid bindings passed\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
