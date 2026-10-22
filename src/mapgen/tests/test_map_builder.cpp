#include "qmg/map_builder.hpp"

#include "qmg/map_document_writer.hpp"
#include "qmg/map_parser.hpp"
#include "qmg/quake2_mechanisms.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

template <typename Function>
void require_build_error(Function &&function, const std::string_view code, const std::string_view message) {
    try {
        function();
    } catch (const qmg::MapBuildError &error) {
        if (error.code() == code)
            return;
        throw std::runtime_error(std::string(message) + ": got " + error.code());
    }
    throw std::runtime_error(std::string(message));
}

[[nodiscard]] qmg::MapBuildOptions options_for(const qmg::Game game, const bool brush_primitives = false) {
    qmg::MapBuildOptions options;
    switch (game) {
    case qmg::Game::quake1:
        options.dialect = qmg::MapDialect::quake1_valve220;
        break;
    case qmg::Game::quake2:
        options.dialect = qmg::MapDialect::quake2_valve220;
        break;
    case qmg::Game::quake3:
        options.dialect = brush_primitives ? qmg::MapDialect::quake3_brush_primitives : qmg::MapDialect::quake3_legacy;
        break;
    }
    return options;
}

[[nodiscard]] qmg::Layout one_room_layout(const qmg::Game game,
                                          const qmg::GridBounds bounds = {{0, 0, 0}, {512, 512, 256}}) {
    qmg::Layout layout;
    layout.spec.game = game;
    layout.spec.title = "MapBuilder fixture";
    layout.spec.theme = "industrial";
    layout.candidate_seed = 0x12345678U;
    layout.rooms.push_back({qmg::RoomId{1}, "room", bounds, {}});
    return layout;
}

[[nodiscard]] std::string canonical(const qmg::MapBuildResult &result, const qmg::GridCoord limit = 32768) {
    qmg::MapWriteOptions write_options;
    write_options.validate_document = true;
    write_options.validation_options.profile = qmg::MapValidationProfile::generated_document;
    write_options.validation_options.max_coordinate = limit;
    return qmg::MapDocumentWriter{}.write(result.document, write_options);
}

void require_canonical_fixed_point(const qmg::MapBuildResult &result, const qmg::GridCoord limit = 32768) {
    const std::string first = canonical(result, limit);
    const qmg::MapDocument reparsed = qmg::MapParser{}.parse_or_throw(first, result.document.dialect());
    qmg::MapValidationOptions validation_options;
    validation_options.profile = qmg::MapValidationProfile::generated_document;
    validation_options.max_coordinate = limit;
    const qmg::MapValidationResult validation = qmg::validate_map_document(reparsed, validation_options);
    require(validation.ok(), "canonical MapBuilder output did not pass generated-document validation after parsing");
    require(qmg::MapDocumentWriter{}.write(reparsed) == first,
            "canonical MapBuilder output did not reach a parse/write fixed point");
}

[[nodiscard]] const qmg::MapEntity *find_entity(const qmg::MapDocument &document, const std::string_view classname) {
    const auto found = std::find_if(document.entities().begin(), document.entities().end(),
                                    [&](const auto &entity) { return entity.first_value("classname") == classname; });
    return found == document.entities().end() ? nullptr : &*found;
}

void require_property(const qmg::MapEntity &entity, const std::string_view key, const std::string_view expected) {
    const std::optional<std::string_view> value = entity.first_value(key);
    require(value.has_value() && *value == expected,
            std::string("native property '") + std::string(key) + "' did not equal '" + std::string(expected) + "'");
}

[[nodiscard]] const qmg::MapBuildManifestEntry *
find_manifest_entry(const qmg::MapBuildResult &result, const qmg::MapBuildSourceKind kind, const std::uint32_t id) {
    const auto found = std::find_if(result.manifest.begin(), result.manifest.end(), [&](const auto &entry) {
        return entry.source.kind == kind && entry.source.id == id;
    });
    return found == result.manifest.end() ? nullptr : &*found;
}

[[nodiscard]] const qmg::MapPatch *only_world_patch(const qmg::MapBuildResult &result) {
    const qmg::MapPatch *found = nullptr;
    for (const qmg::MapPrimitive &primitive : result.document.entities().front().primitives) {
        if (const qmg::MapPatch *patch = std::get_if<qmg::MapPatch>(&primitive)) {
            require(found == nullptr, "expected exactly one worldspawn patch");
            found = patch;
        }
    }
    require(found != nullptr, "worldspawn patch veneer is missing");
    return found;
}

[[nodiscard]] qmg::GridBounds brush_bounds(const qmg::MapBrush &brush) {
    require(!brush.faces.empty(), "cannot find bounds for an empty brush");
    qmg::GridVec3 minimum{std::numeric_limits<qmg::GridCoord>::max(), std::numeric_limits<qmg::GridCoord>::max(),
                          std::numeric_limits<qmg::GridCoord>::max()};
    qmg::GridVec3 maximum{std::numeric_limits<qmg::GridCoord>::min(), std::numeric_limits<qmg::GridCoord>::min(),
                          std::numeric_limits<qmg::GridCoord>::min()};
    for (const qmg::MapBrushFace &face : brush.faces) {
        for (const qmg::Vec3 point : face.plane_points) {
            const qmg::GridVec3 grid = qmg::snap_grid_vec3(point);
            minimum.x = std::min(minimum.x, grid.x);
            minimum.y = std::min(minimum.y, grid.y);
            minimum.z = std::min(minimum.z, grid.z);
            maximum.x = std::max(maximum.x, grid.x);
            maximum.y = std::max(maximum.y, grid.y);
            maximum.z = std::max(maximum.z, grid.z);
        }
    }
    return {minimum, maximum};
}

[[nodiscard]] bool positive_overlap(const qmg::GridCoord a0, const qmg::GridCoord a1, const qmg::GridCoord b0,
                                    const qmg::GridCoord b1) {
    return std::max(a0, b0) < std::min(a1, b1);
}

void require_east_opening(const qmg::MapBuildResult &result, const qmg::GridCoord plane,
                          const qmg::GridBounds &opening) {
    const qmg::MapEntity &world = result.document.entities().front();
    for (const qmg::MapPrimitive &primitive : world.primitives) {
        const auto *brush = std::get_if<qmg::MapBrush>(&primitive);
        if (brush == nullptr)
            continue;
        const qmg::GridBounds bounds = brush_bounds(*brush);
        if (bounds.min.x != plane || bounds.max.x <= plane)
            continue;
        const bool blocks = positive_overlap(bounds.min.y, bounds.max.y, opening.min.y, opening.max.y) &&
                            positive_overlap(bounds.min.z, bounds.max.z, opening.min.z, opening.max.z);
        require(!blocks, "union shell left a brush across an explicit passage opening");
    }
}

[[nodiscard]] qmg::LayoutSolid box_solid(const std::uint32_t id, const qmg::GridBounds bounds,
                                         const qmg::MaterialRole material = qmg::MaterialRole::wall,
                                         const qmg::BrushPurpose purpose = qmg::BrushPurpose::structural,
                                         const qmg::BrushOwnership ownership = qmg::BrushOwnership::structural,
                                         const qmg::BrushContents contents = qmg::BrushContents::solid) {
    qmg::LayoutSolid solid;
    solid.id = qmg::SolidId{id};
    solid.geometry = bounds;
    solid.material = material;
    solid.purpose = purpose;
    solid.ownership = ownership;
    solid.contents = contents;
    return solid;
}

[[nodiscard]] qmg::ConvexPolyhedron tetrahedron() {
    return {{
        {{0, 0, 0}, {0, 0, 64}, {0, 64, 0}},
        {{0, 0, 0}, {64, 0, 0}, {0, 0, 64}},
        {{0, 0, 0}, {0, 64, 0}, {64, 0, 0}},
        {{64, 0, 0}, {0, 64, 0}, {0, 0, 64}},
    }};
}

[[nodiscard]] qmg::ConvexPolyhedron ramp_prism() {
    return {{
        {{0, 0, 0}, {64, 0, 0}, {64, 0, 64}},
        {{0, 64, 0}, {64, 64, 64}, {64, 64, 0}},
        {{0, 0, 0}, {0, 64, 0}, {64, 0, 0}},
        {{64, 0, 0}, {64, 64, 0}, {64, 0, 64}},
        {{0, 0, 0}, {64, 0, 64}, {0, 64, 0}},
    }};
}

void test_single_room_and_dialect_contract() {
    const qmg::Layout layout = one_room_layout(qmg::Game::quake1);
    const qmg::MapBuildResult result = qmg::MapBuilder{}.build(layout, options_for(qmg::Game::quake1));
    require(result.stats.input_free_volumes == 1 && result.stats.unique_free_volumes == 1,
            "single room free-volume statistics are wrong");
    require(result.stats.shell_brushes == 6 && result.stats.brush_count == 6,
            "single rectangular room did not lower to six boundary slabs");
    require(result.manifest.size() == 1 && !result.manifest.front().node_ids.empty(),
            "single room manifest is missing shell provenance");
    require_canonical_fixed_point(result);

    qmg::MapBuildOptions mismatch = options_for(qmg::Game::quake2);
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(layout, mismatch)); }, "dialect_game_mismatch",
                        "builder accepted a dialect from a different title");

    qmg::MapBuildOptions capped = options_for(qmg::Game::quake1);
    capped.max_shell_brushes = 5;
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(layout, capped)); }, "shell_brush_limit",
                        "shell brush cap was not enforced");

    qmg::MapBuildOptions cell_capped = options_for(qmg::Game::quake1);
    cell_capped.max_face_cells = 5;
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(layout, cell_capped)); }, "face_cell_limit",
                        "coordinate-compressed face-cell cap was not enforced");

    qmg::Layout extra_brush = layout;
    extra_brush.solids.push_back(box_solid(1, {{32, 32, 0}, {96, 96, 32}}));
    qmg::MapBuildOptions total_capped = options_for(qmg::Game::quake1);
    total_capped.max_shell_brushes = 6;
    total_capped.max_total_brushes = 6;
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(extra_brush, total_capped)); },
                        "total_brush_limit", "total brush cap was not enforced");
}

void test_passages_and_partial_openings() {
    qmg::Layout layout = one_room_layout(qmg::Game::quake1, {{0, 0, 0}, {256, 512, 256}});
    layout.rooms.push_back({qmg::RoomId{2}, "east_low", {{512, 0, 0}, {768, 256, 256}}, {}});
    layout.rooms.push_back({qmg::RoomId{3}, "east_high", {{512, 256, 0}, {768, 512, 256}}, {}});
    const qmg::GridBounds lower_opening{{256, 32, 0}, {512, 160, 128}};
    const qmg::GridBounds upper_opening{{256, 320, 0}, {512, 448, 128}};
    layout.connections.push_back({qmg::ConnectionId{1}, qmg::RoomId{1}, qmg::RoomId{2}, 128, 128, {lower_opening}});
    layout.connections.push_back({qmg::ConnectionId{2}, qmg::RoomId{1}, qmg::RoomId{3}, 128, 128, {upper_opening}});

    const qmg::MapBuildResult result = qmg::MapBuilder{}.build(layout, options_for(qmg::Game::quake1));
    require_east_opening(result, 256, lower_opening);
    require_east_opening(result, 256, upper_opening);
    require(result.stats.input_free_volumes == 5 && result.stats.shell_brushes > 6,
            "partial/multiple passage fixture did not exercise union subdivision");
    require_canonical_fixed_point(result);
}

void test_cross_nested_duplicate_and_failures() {
    qmg::Layout tee = one_room_layout(qmg::Game::quake1, {{-320, -64, 0}, {320, 64, 160}});
    tee.rooms.push_back({qmg::RoomId{2}, "stem", {{-64, -32, 0}, {64, 320, 160}}, {}});
    const qmg::MapBuildResult tee_result = qmg::MapBuilder{}.build(tee, options_for(qmg::Game::quake1));
    require(tee_result.stats.shell_brushes >= 8, "T-junction free volumes were not boundary-subdivided");
    require_canonical_fixed_point(tee_result);

    qmg::Layout cross = one_room_layout(qmg::Game::quake1, {{-320, -64, 0}, {320, 64, 160}});
    cross.rooms.push_back({qmg::RoomId{2}, "vertical", {{-64, -320, 0}, {64, 320, 160}}, {}});
    const qmg::MapBuildResult cross_result = qmg::MapBuilder{}.build(cross, options_for(qmg::Game::quake1));
    require(cross_result.stats.shell_brushes >= 12, "crossing free volumes were not boundary-subdivided");
    require_canonical_fixed_point(cross_result);

    qmg::Layout nested = one_room_layout(qmg::Game::quake1, {{0, 0, 0}, {512, 512, 256}});
    nested.rooms.push_back({qmg::RoomId{2}, "duplicate", {{0, 0, 0}, {512, 512, 256}}, {}});
    nested.rooms.push_back({qmg::RoomId{3}, "nested", {{64, 64, 32}, {448, 448, 224}}, {}});
    const qmg::MapBuildResult nested_result = qmg::MapBuilder{}.build(nested, options_for(qmg::Game::quake1));
    require(nested_result.stats.input_free_volumes == 3 && nested_result.stats.unique_free_volumes == 2,
            "duplicate free volume was not normalized deterministically");
    require(nested_result.stats.shell_brushes == 6, "nested/duplicate volumes emitted internal shell brushes");
    const auto nested_entry =
        std::find_if(nested_result.manifest.begin(), nested_result.manifest.end(), [](const auto &entry) {
            return entry.source.kind == qmg::MapBuildSourceKind::room && entry.source.id == 3;
        });
    require(nested_entry != nested_result.manifest.end() && nested_entry->node_ids.empty(),
            "fully occluded nested room did not retain a stable empty manifest entry");

    qmg::Layout edge = one_room_layout(qmg::Game::quake1, {{0, 0, 0}, {128, 128, 128}});
    edge.rooms.push_back({qmg::RoomId{2}, "edge", {{128, 128, 0}, {256, 256, 128}}, {}});
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(edge, options_for(qmg::Game::quake1))); },
                        "edge_only_free_volume_contact", "edge-only free-space contact was accepted");

    qmg::Layout clearance = one_room_layout(qmg::Game::quake1, {{0, 0, 0}, {128, 128, 128}});
    clearance.rooms.push_back({qmg::RoomId{2}, "too_close", {{144, 0, 0}, {272, 128, 128}}, {}});
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(clearance, options_for(qmg::Game::quake1))); },
                        "shell_clearance", "shell intrusion into a nearby disconnected free volume was accepted");

    const qmg::GridCoord maximum = std::numeric_limits<qmg::GridCoord>::max();
    qmg::Layout overflow = one_room_layout(qmg::Game::quake1, {{maximum - 128, 0, 0}, {maximum, 128, 128}});
    qmg::MapBuildOptions overflow_options = options_for(qmg::Game::quake1);
    overflow_options.max_coordinate = maximum;
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(overflow, overflow_options)); },
                        "shell_overflow", "signed grid overflow during shell extrusion was accepted");
}

void require_same_manifest(const std::vector<qmg::MapBuildManifestEntry> &left,
                           const std::vector<qmg::MapBuildManifestEntry> &right) {
    require(left.size() == right.size(), "shuffled layout changed manifest cardinality");
    for (std::size_t index = 0; index < left.size(); ++index) {
        require(left[index].source == right[index].source && left[index].node_ids == right[index].node_ids,
                "shuffled layout changed stable manifest contents");
    }
}

void test_shuffle_byte_invariance() {
    qmg::Layout layout = one_room_layout(qmg::Game::quake1, {{0, 0, 0}, {256, 256, 256}});
    layout.rooms.push_back({qmg::RoomId{2}, "east", {{768, 0, 0}, {1024, 256, 256}}, {}});
    layout.connections.push_back({qmg::ConnectionId{1},
                                  qmg::RoomId{1},
                                  qmg::RoomId{2},
                                  128,
                                  128,
                                  {{{512, 64, 0}, {768, 192, 128}}, {{256, 64, 0}, {512, 192, 128}}}});
    layout.solids.push_back(box_solid(2, {{800, 32, 0}, {864, 96, 32}}, qmg::MaterialRole::platform,
                                      qmg::BrushPurpose::raised_platform, qmg::BrushOwnership::detail));
    layout.solids.push_back(box_solid(1, {{32, 32, 0}, {96, 96, 32}}, qmg::MaterialRole::platform,
                                      qmg::BrushPurpose::raised_platform, qmg::BrushOwnership::detail));
    qmg::Placement health;
    health.id = qmg::PlacementId{2};
    health.role = qmg::PlacementRole::health;
    health.origin = {832, 128, 48};
    layout.placements.push_back(health);
    qmg::Placement spawn;
    spawn.id = qmg::PlacementId{1};
    spawn.role = qmg::PlacementRole::deathmatch_spawn;
    spawn.origin = {128, 128, 32};
    layout.placements.push_back(spawn);

    qmg::Layout shuffled = layout;
    std::reverse(shuffled.rooms.begin(), shuffled.rooms.end());
    std::reverse(shuffled.connections.begin(), shuffled.connections.end());
    std::reverse(shuffled.connections.front().passage_volumes.begin(),
                 shuffled.connections.front().passage_volumes.end());
    std::reverse(shuffled.solids.begin(), shuffled.solids.end());
    std::reverse(shuffled.placements.begin(), shuffled.placements.end());

    const auto first = qmg::MapBuilder{}.build(layout, options_for(qmg::Game::quake1));
    const auto second = qmg::MapBuilder{}.build(shuffled, options_for(qmg::Game::quake1));
    require(canonical(first) == canonical(second), "collection reordering changed canonical map bytes");
    require_same_manifest(first.manifest, second.manifest);
    require(first.stats.shell_brushes == second.stats.shell_brushes &&
                first.stats.brush_count == second.stats.brush_count &&
                first.stats.face_count == second.stats.face_count,
            "collection reordering changed deterministic lowering statistics");
}

void test_convex_geometry() {
    qmg::Layout layout = one_room_layout(qmg::Game::quake1);
    layout.solids.push_back(box_solid(1, {{32, 32, 0}, {96, 96, 32}}, qmg::MaterialRole::platform));
    qmg::LayoutSolid tetra;
    tetra.id = qmg::SolidId{2};
    tetra.geometry = tetrahedron();
    require(qmg::validate_brush_geometry(tetra.geometry).ok(), "tetrahedron fixture itself is invalid");
    tetra.material = qmg::MaterialRole::arch;
    layout.solids.push_back(tetra);
    qmg::LayoutSolid ramp;
    ramp.id = qmg::SolidId{3};
    ramp.geometry = ramp_prism();
    const qmg::BrushGeometryValidation ramp_validation = qmg::validate_brush_geometry(ramp.geometry);
    require(ramp_validation.ok(),
            "ramp fixture itself is invalid with error " + std::to_string(static_cast<int>(ramp_validation.error)));
    ramp.material = qmg::MaterialRole::ramp;
    ramp.purpose = qmg::BrushPurpose::ramp;
    layout.solids.push_back(ramp);

    const qmg::MapBuildResult result = qmg::MapBuilder{}.build(layout, options_for(qmg::Game::quake1));
    require(result.stats.structural_brushes == result.stats.shell_brushes + 3,
            "AABB/tetrahedron/ramp structural counts are wrong");
    require_canonical_fixed_point(result);

    qmg::Layout invalid = one_room_layout(qmg::Game::quake1);
    qmg::LayoutSolid open;
    open.id = qmg::SolidId{1};
    qmg::ConvexPolyhedron broken = tetrahedron();
    broken.planes.pop_back();
    open.geometry = broken;
    invalid.solids.push_back(open);
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(invalid, options_for(qmg::Game::quake1))); },
                        "invalid_brush_geometry", "unbounded convex solid was accepted");
}

void add_liquids(qmg::Layout &layout) {
    layout.solids.push_back(box_solid(1, {{32, 32, 16}, {128, 128, 64}}, qmg::MaterialRole::liquid,
                                      qmg::BrushPurpose::liquid, qmg::BrushOwnership::structural,
                                      qmg::BrushContents::water));
    layout.solids.push_back(box_solid(2, {{160, 32, 16}, {256, 128, 64}}, qmg::MaterialRole::liquid,
                                      qmg::BrushPurpose::liquid, qmg::BrushOwnership::structural,
                                      qmg::BrushContents::slime));
    layout.solids.push_back(box_solid(3, {{288, 32, 16}, {384, 128, 64}}, qmg::MaterialRole::liquid,
                                      qmg::BrushPurpose::liquid, qmg::BrushOwnership::structural,
                                      qmg::BrushContents::lava));
}

void test_liquids_and_detail_ownership() {
    qmg::Layout quake1 = one_room_layout(qmg::Game::quake1);
    add_liquids(quake1);
    quake1.solids.push_back(box_solid(4, {{32, 192, 0}, {128, 256, 32}}, qmg::MaterialRole::platform,
                                      qmg::BrushPurpose::raised_platform, qmg::BrushOwnership::detail));
    const auto q1_result = qmg::MapBuilder{}.build(quake1, options_for(qmg::Game::quake1));
    const qmg::MapEntity *q1_detail = find_entity(q1_result.document, "func_detail");
    require(q1_detail != nullptr && q1_detail->primitives.size() == 1,
            "Quake 1 detail brush was not owned by func_detail");
    for (const qmg::MapEntity &entity : q1_result.document.entities()) {
        for (const qmg::MapPrimitive &primitive : entity.primitives) {
            if (const auto *brush = std::get_if<qmg::MapBrush>(&primitive)) {
                for (const qmg::MapBrushFace &face : brush->faces)
                    require(!face.quake2_metadata && !face.quake3_metadata, "Quake 1 brush emitted trailing metadata");
            }
        }
    }
    require_canonical_fixed_point(q1_result);

    qmg::Layout quake2 = one_room_layout(qmg::Game::quake2);
    add_liquids(quake2);
    quake2.solids.push_back(box_solid(4, {{32, 192, 0}, {128, 256, 32}}, qmg::MaterialRole::platform,
                                      qmg::BrushPurpose::raised_platform, qmg::BrushOwnership::detail));
    const auto q2_result = qmg::MapBuilder{}.build(quake2, options_for(qmg::Game::quake2));
    require(find_entity(q2_result.document, "func_detail") != nullptr,
            "Quake 2 detail brush was not owned by func_detail");
    std::set<std::int32_t> q2_contents;
    for (const qmg::MapEntity &entity : q2_result.document.entities()) {
        for (const qmg::MapPrimitive &primitive : entity.primitives) {
            if (const auto *brush = std::get_if<qmg::MapBrush>(&primitive)) {
                for (const qmg::MapBrushFace &face : brush->faces) {
                    if (face.quake2_metadata)
                        q2_contents.insert(face.quake2_metadata->contents);
                }
            }
        }
    }
    require(q2_contents == std::set<std::int32_t>{8, 16, 32},
            "Quake 2 liquid brushes did not preserve exact explicit contents values");
    require_canonical_fixed_point(q2_result);

    for (const bool brush_primitives : {false, true}) {
        qmg::Layout quake3 = one_room_layout(qmg::Game::quake3);
        add_liquids(quake3);
        quake3.solids.push_back(box_solid(4, {{32, 192, 0}, {128, 256, 32}}, qmg::MaterialRole::platform,
                                          qmg::BrushPurpose::raised_platform, qmg::BrushOwnership::detail));
        const auto q3_result = qmg::MapBuilder{}.build(quake3, options_for(qmg::Game::quake3, brush_primitives));
        require(find_entity(q3_result.document, "func_detail") == nullptr,
                "Quake 3 detail brush was incorrectly moved into func_detail");
        bool saw_detail = false;
        std::set<std::int32_t> liquid_contents;
        for (const qmg::MapPrimitive &primitive : q3_result.document.entities().front().primitives) {
            const auto &brush = std::get<qmg::MapBrush>(primitive);
            std::optional<bool> brush_detail;
            for (const qmg::MapBrushFace &face : brush.faces) {
                require(face.quake3_metadata.has_value(), "generated Quake 3 face omitted c/s/v metadata");
                const bool detail = (face.quake3_metadata->contents & qmg::kMapDetailContentsBit) != 0;
                if (!brush_detail)
                    brush_detail = detail;
                require(*brush_detail == detail, "Quake 3 detail bit is inconsistent within a brush");
                const std::int32_t base = face.quake3_metadata->contents & ~qmg::kMapDetailContentsBit;
                if (base != 0)
                    liquid_contents.insert(base);
                if (brush_primitives)
                    require(std::holds_alternative<qmg::Quake3BrushPrimitiveProjection>(face.projection),
                            "brush-primitives output used a legacy projection");
                else
                    require(std::holds_alternative<qmg::Quake3LegacyProjection>(face.projection),
                            "legacy output used a brush-primitive projection");
            }
            saw_detail = saw_detail || brush_detail.value_or(false);
        }
        require(saw_detail, "Quake 3 detail solid did not receive C_DETAIL on every face");
        require(liquid_contents == std::set<std::int32_t>{8, 16, 32}, "Quake 3 liquid contents values are wrong");
        require_canonical_fixed_point(q3_result);
    }
}

[[nodiscard]] qmg::Placement point_placement(const std::uint32_t id, const qmg::PlacementRole role,
                                             const qmg::GridVec3 origin, const std::uint16_t variant = 0) {
    qmg::Placement placement;
    placement.id = qmg::PlacementId{id};
    placement.role = role;
    placement.origin = origin;
    placement.variant = variant;
    return placement;
}

[[nodiscard]] qmg::Placement volume_placement(const std::uint32_t id, const qmg::PlacementRole role,
                                              const qmg::GridBounds volume) {
    qmg::Placement placement = point_placement(id, role, volume.center());
    placement.volume = volume;
    return placement;
}

void add_profile_placements(qmg::Layout &layout) {
    std::uint32_t id = 1;
    auto add = [&](qmg::Placement placement) {
        placement.id = qmg::PlacementId{id++};
        layout.placements.push_back(std::move(placement));
        return layout.placements.back().id;
    };
    qmg::Placement player = point_placement(0, qmg::PlacementRole::player_start, {-800, -800, 32});
    player.angle_degrees = 0;
    add(player);
    add(point_placement(0, qmg::PlacementRole::deathmatch_spawn, {-700, -800, 32}));
    qmg::Placement team_initial = point_placement(0, qmg::PlacementRole::team_initial_spawn, {-600, -800, 32});
    team_initial.team = qmg::Team::red;
    add(team_initial);
    qmg::Placement team_respawn = point_placement(0, qmg::PlacementRole::team_respawn, {-500, -800, 32});
    team_respawn.team = qmg::Team::blue;
    add(team_respawn);
    add(point_placement(0, qmg::PlacementRole::weapon, {-400, -800, 32}, 1));
    add(point_placement(0, qmg::PlacementRole::health, {-300, -800, 32}));
    add(point_placement(0, qmg::PlacementRole::armor, {-200, -800, 32}));
    if (layout.spec.game != qmg::Game::quake3)
        add(point_placement(0, qmg::PlacementRole::monster, {-100, -800, 32}, 2));
    qmg::Placement flag = point_placement(0, qmg::PlacementRole::flag, {0, -800, 32});
    flag.team = qmg::Team::red;
    add(flag);
    qmg::Placement light = point_placement(0, qmg::PlacementRole::light, {100, -800, 160});
    light.light_intensity = 450;
    light.light_color = qmg::RgbColor{920, 880, 760};
    add(light);

    qmg::Placement exit_destination = point_placement(0, qmg::PlacementRole::exit_destination, {200, -800, 32});
    exit_destination.destination_map = "next_map";
    const qmg::PlacementId exit_destination_id = add(exit_destination);
    qmg::Placement exit_trigger =
        volume_placement(0, qmg::PlacementRole::exit_trigger, {{-800, -600, 0}, {-704, -504, 128}});
    if (layout.spec.game == qmg::Game::quake1)
        exit_trigger.destination_map = "next_map";
    else
        exit_trigger.link = exit_destination_id;
    add(exit_trigger);

    qmg::Placement door = volume_placement(0, qmg::PlacementRole::door, {{-600, -600, 0}, {-536, -472, 160}});
    door.angle_degrees = 90;
    const qmg::PlacementId door_id = add(door);
    qmg::Placement door_trigger =
        volume_placement(0, qmg::PlacementRole::door_trigger, {{-500, -600, 0}, {-404, -504, 96}});
    door_trigger.link = door_id;
    add(door_trigger);
    add(point_placement(0, qmg::PlacementRole::door_destination, {-350, -550, 32}));

    qmg::Placement lift = volume_placement(0, qmg::PlacementRole::lift, {{-200, -600, 0}, {-72, -472, 32}});
    const qmg::PlacementId lift_id = add(lift);
    qmg::Placement lift_trigger =
        volume_placement(0, qmg::PlacementRole::lift_trigger, {{-20, -600, 0}, {76, -504, 96}});
    lift_trigger.link = lift_id;
    add(lift_trigger);
    add(point_placement(0, qmg::PlacementRole::lift_destination, {128, -550, 128}));

    const qmg::PlacementId teleporter_destination_id =
        add(point_placement(0, qmg::PlacementRole::teleporter_destination, {400, -550, 32}));
    qmg::Placement teleporter =
        volume_placement(0, qmg::PlacementRole::teleporter_trigger, {{240, -600, 0}, {336, -504, 96}});
    if (layout.spec.game == qmg::Game::quake2) {
        teleporter.origin.z = 24;
        teleporter.volume = qmg::quake2_teleporter_bounds(teleporter.origin, true);
    }
    teleporter.link = teleporter_destination_id;
    add(teleporter);
}

void require_class(const qmg::MapDocument &document, const std::string_view classname) {
    require(find_entity(document, classname) != nullptr,
            std::string("native placement mapping is missing classname ") + std::string(classname));
}

void test_placement_profiles() {
    for (const qmg::Game game : {qmg::Game::quake1, qmg::Game::quake2, qmg::Game::quake3}) {
        const std::array<bool, 2> dialects =
            game == qmg::Game::quake3 ? std::array<bool, 2>{false, true} : std::array<bool, 2>{false, false};
        for (std::size_t dialect_index = 0; dialect_index < (game == qmg::Game::quake3 ? 2U : 1U); ++dialect_index) {
            qmg::Layout layout = one_room_layout(game, {{-1024, -1024, 0}, {1024, 1024, 256}});
            add_profile_placements(layout);
            const auto result = qmg::MapBuilder{}.build(layout, options_for(game, dialects[dialect_index]));
            require_class(result.document, "info_player_deathmatch");
            require_class(result.document, "item_health");
            require_class(result.document, "light");
            require_class(result.document, "func_door");
            require_class(result.document, "func_plat");
            require_class(result.document, "trigger_multiple");
            if (game == qmg::Game::quake1) {
                require_class(result.document, "info_player_start");
                require_class(result.document, "weapon_supernailgun");
                require_class(result.document, "monster_ogre");
                require_class(result.document, "item_armor2");
                require_class(result.document, "item_flag_team1");
                require_class(result.document, "trigger_changelevel");
                require_class(result.document, "trigger_teleport");
                require_class(result.document, "info_teleport_destination");
            } else if (game == qmg::Game::quake2) {
                require_class(result.document, "info_player_start");
                require_class(result.document, "weapon_machinegun");
                require_class(result.document, "monster_gunner");
                require_class(result.document, "item_armor_combat");
                require_class(result.document, "item_flag_team1");
                require_class(result.document, "trigger_once");
                require_class(result.document, "target_changelevel");
                const qmg::MapEntity *teleporter = find_entity(result.document, "misc_teleporter");
                require(teleporter != nullptr && teleporter->primitives.empty() && teleporter->first_value("origin"),
                        "Quake 2 teleporter did not lower to the stock point entity contract");
                require_class(result.document, "misc_teleporter_dest");
            } else {
                require_class(result.document, "weapon_machinegun");
                require_class(result.document, "item_armor_body");
                require_class(result.document, "team_CTF_redplayer");
                require_class(result.document, "team_CTF_bluespawn");
                require_class(result.document, "team_CTF_redflag");
                require_class(result.document, "target_relay");
                require_class(result.document, "trigger_teleport");
                require_class(result.document, "misc_teleporter_dest");
            }
            const qmg::MapEntity *door = find_entity(result.document, "func_door");
            const qmg::MapEntity *lift = find_entity(result.document, "func_plat");
            require(door != nullptr && !door->primitives.empty() && door->first_value("targetname"),
                    "typed door did not retain brush ownership and stable targetname");
            require(lift != nullptr && !lift->primitives.empty() && lift->first_value("targetname"),
                    "typed lift did not retain brush ownership and stable targetname");
            require(result.stats.placement_brushes >= 5, "placement brush statistics omitted movers/triggers");
            require_canonical_fixed_point(result);
        }
    }

    qmg::Layout unsupported = one_room_layout(qmg::Game::quake3);
    unsupported.placements.push_back(point_placement(1, qmg::PlacementRole::monster, {32, 32, 32}));
    require_build_error(
        [&] { static_cast<void>(qmg::MapBuilder{}.build(unsupported, options_for(qmg::Game::quake3))); },
        "unsupported_placement", "builder invented a stock Quake 3 monster classname");
}

void test_placement_owned_convex_brush() {
    qmg::Layout layout = one_room_layout(qmg::Game::quake1);
    qmg::Placement door = volume_placement(1, qmg::PlacementRole::door, {{0, 0, 0}, {96, 96, 96}});
    layout.placements.push_back(door);
    qmg::LayoutSolid leaf = box_solid(1, {{16, 16, 0}, {80, 80, 96}}, qmg::MaterialRole::door, qmg::BrushPurpose::door,
                                      qmg::BrushOwnership::placement);
    leaf.owner = door.id;
    layout.solids.push_back(leaf);
    const auto result = qmg::MapBuilder{}.build(layout, options_for(qmg::Game::quake1));
    const qmg::MapEntity *entity = find_entity(result.document, "func_door");
    require(entity != nullptr && entity->primitives.size() == 1,
            "placement-owned brush was duplicated or lost during mover lowering");
    require(result.stats.placement_brushes == 1, "placement-owned brush count is wrong");
    require_canonical_fixed_point(result);
}

[[nodiscard]] qmg::CurvedWallSpec curved_wall(const std::uint64_t key, const qmg::GridVec3 center) {
    qmg::CurvedWallSpec spec;
    spec.key = {key};
    spec.center = center;
    spec.inner_radius = 128;
    spec.thickness = 24;
    spec.height = 160;
    spec.segment_count = 4;
    spec.ownership = qmg::BrushOwnership::detail;
    spec.emit_patch_veneer = true;
    return spec;
}

void test_curve_patch_lowering() {
    for (const bool brush_primitives : {false, true}) {
        qmg::Layout layout = one_room_layout(qmg::Game::quake3, {{-512, -512, -32}, {512, 512, 256}});
        const qmg::ConstructionResult construction =
            qmg::ConstructionBuilder{}.build(layout, {curved_wall(1, {10, -20, 16})});
        construction.apply_to(layout);
        const qmg::MapBuildResult result =
            qmg::MapBuilder{}.build(layout, options_for(qmg::Game::quake3, brush_primitives), construction);
        const qmg::LayoutCurve &source = construction.curves.front();
        const qmg::MapPatch *patch = only_world_patch(result);
        require(result.stats.patch_count == 1, "patch veneer was omitted from MapBuildStats");
        require(patch->material == "base_wall/comp3text" && patch->material.rfind("textures/", 0) != 0,
                "patch veneer did not use the centralized relative Quake 3 material");
        require(patch->width == source.width && patch->height == source.height &&
                    patch->control_points.size() == source.control_points.size() &&
                    patch->reserved == std::array<std::int32_t, 3>{},
                "patch header or exact width-major control count changed");
        for (std::size_t index = 0; index < source.control_points.size(); ++index) {
            const qmg::LayoutCurveControlPoint &input = source.control_points[index];
            const qmg::MapPatchControlPoint &output = patch->control_points[index];
            require(output.id != 0 && output.position.x == input.position.x && output.position.y == input.position.y &&
                        output.position.z == input.position.z &&
                        output.s == static_cast<double>(input.u_milli) / 1000.0 &&
                        output.t == static_cast<double>(input.v_milli) / 1000.0,
                    "patch control order, position, fixed-point UV, or node ID changed");
        }
        const qmg::MapBuildManifestEntry *manifest =
            find_manifest_entry(result, qmg::MapBuildSourceKind::solid, source.source_id.value);
        require(manifest != nullptr &&
                    std::binary_search(manifest->node_ids.begin(), manifest->node_ids.end(), patch->id),
                "curve manifest does not attribute the patch node to its source SolidId");
        for (const qmg::MapPatchControlPoint &control : patch->control_points) {
            require(std::binary_search(manifest->node_ids.begin(), manifest->node_ids.end(), control.id),
                    "curve manifest does not attribute every patch control node");
        }
        for (std::size_t entity_index = 1; entity_index < result.document.entities().size(); ++entity_index) {
            for (const qmg::MapPrimitive &primitive : result.document.entities()[entity_index].primitives)
                require(std::get_if<qmg::MapPatch>(&primitive) == nullptr, "detail patch escaped worldspawn ownership");
        }
        require_canonical_fixed_point(result);
    }

    qmg::Layout ordered_layout = one_room_layout(qmg::Game::quake3, {{-768, -768, -32}, {768, 768, 256}});
    qmg::ConstructionResult ordered_construction = qmg::ConstructionBuilder{}.build(
        ordered_layout, {curved_wall(20, {-256, -64, 0}), curved_wall(10, {128, -64, 0})});
    ordered_construction.apply_to(ordered_layout);
    qmg::Layout shuffled_layout = ordered_layout;
    qmg::ConstructionResult shuffled_construction = ordered_construction;
    std::reverse(shuffled_layout.solids.begin(), shuffled_layout.solids.end());
    std::reverse(shuffled_layout.placements.begin(), shuffled_layout.placements.end());
    std::reverse(shuffled_construction.solids.begin(), shuffled_construction.solids.end());
    std::reverse(shuffled_construction.placements.begin(), shuffled_construction.placements.end());
    std::reverse(shuffled_construction.curves.begin(), shuffled_construction.curves.end());
    std::reverse(shuffled_construction.movers.begin(), shuffled_construction.movers.end());
    for (qmg::LayoutCurve &curve : shuffled_construction.curves)
        std::reverse(curve.collision_backing_solids.begin(), curve.collision_backing_solids.end());
    const qmg::MapBuildOptions options = options_for(qmg::Game::quake3, true);
    const qmg::MapBuildResult ordered = qmg::MapBuilder{}.build(ordered_layout, options, ordered_construction);
    const qmg::MapBuildResult shuffled = qmg::MapBuilder{}.build(shuffled_layout, options, shuffled_construction);
    require(canonical(ordered) == canonical(shuffled), "construction collection reordering changed patch map bytes");
    require_same_manifest(ordered.manifest, shuffled.manifest);
    require(ordered.stats.patch_count == 2 && shuffled.stats.patch_count == 2,
            "construction collection reordering changed patch statistics");
}

void test_curve_patch_rejections() {
    qmg::Layout quake1 = one_room_layout(qmg::Game::quake1, {{-512, -512, -32}, {512, 512, 256}});
    const qmg::ConstructionResult quake1_construction =
        qmg::ConstructionBuilder{}.build(quake1, {curved_wall(1, {0, 0, 0})});
    quake1_construction.apply_to(quake1);
    require_build_error(
        [&] {
            static_cast<void>(qmg::MapBuilder{}.build(quake1, options_for(qmg::Game::quake1), quake1_construction));
        },
        "unsupported_curve_dialect", "Quake 1 accepted a patch veneer");

    qmg::Layout layout = one_room_layout(qmg::Game::quake3, {{-512, -512, -32}, {512, 512, 256}});
    const qmg::ConstructionResult valid = qmg::ConstructionBuilder{}.build(layout, {curved_wall(1, {0, 0, 0})});
    valid.apply_to(layout);
    const qmg::MapBuildOptions options = options_for(qmg::Game::quake3);

    qmg::Layout missing_applied_backing = layout;
    const qmg::SolidId backing_id = valid.curves.front().collision_backing_solids.front().id;
    missing_applied_backing.solids.erase(
        std::remove_if(missing_applied_backing.solids.begin(), missing_applied_backing.solids.end(),
                       [&](const qmg::LayoutSolid &solid) { return solid.id == backing_id; }),
        missing_applied_backing.solids.end());
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(missing_applied_backing, options, valid)); },
                        "construction_not_applied", "patch veneer accepted an absent collision backing solid");

    qmg::ConstructionResult no_declared_backing = valid;
    no_declared_backing.curves.front().collision_backing_solids.clear();
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(layout, options, no_declared_backing)); },
                        "missing_curve_backing", "patch veneer accepted no declared collision backing");

    qmg::ConstructionResult colliding = valid;
    colliding.curves.front().source_id = layout.solids.front().id;
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(layout, options, colliding)); },
                        "curve_id_collision", "patch source ID collided with an applied LayoutSolid ID");

    qmg::ConstructionResult invalid_dimensions = valid;
    invalid_dimensions.curves.front().width = 4;
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(layout, options, invalid_dimensions)); },
                        "invalid_curve_dimensions", "even patch dimensions were accepted");

    qmg::ConstructionResult invalid_controls = valid;
    invalid_controls.curves.front().control_points.pop_back();
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(layout, options, invalid_controls)); },
                        "invalid_curve_controls", "incomplete width-major patch controls were accepted");

    qmg::ConstructionResult invalid_ownership = valid;
    invalid_ownership.curves.front().ownership = qmg::BrushOwnership::structural;
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(layout, options, invalid_ownership)); },
                        "invalid_curve_ownership", "structural patch veneer ownership was accepted");

    qmg::ConstructionResult out_of_bounds = valid;
    out_of_bounds.curves.front().control_points.front().position.x = options.max_coordinate + 1;
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(layout, options, out_of_bounds)); },
                        "coordinate_limit", "out-of-bounds patch control was accepted");
}

[[nodiscard]] qmg::ConstructionResult mover_construction(const qmg::Layout &layout, const qmg::GridVec3 door_delta) {
    qmg::DoorAssemblySpec door;
    door.key = {1};
    door.leaf_bounds = {{-320, -128, 0}, {-224, -32, 128}};
    door.travel_delta = door_delta;
    door.speed_units_per_second = 120;
    door.wait_milliseconds = 2501;

    qmg::LiftAssemblySpec lift;
    lift.key = {2};
    lift.platform_bounds = {{128, 128, 0}, {256, 256, 24}};
    lift.travel_delta = {0, 0, -160};
    lift.speed_units_per_second = 80;
    lift.wait_milliseconds = 4000;
    return qmg::ConstructionBuilder{}.build(layout, {lift, door});
}

void test_typed_mover_lowering() {
    const std::array<qmg::Game, 3> games{qmg::Game::quake1, qmg::Game::quake2, qmg::Game::quake3};
    const std::array<qmg::GridVec3, 3> door_deltas{{{64, 0, 0}, {-64, 0, 0}, {0, -64, 0}}};
    const std::array<std::string_view, 3> expected_angles{"0", "180", "270"};
    for (std::size_t index = 0; index < games.size(); ++index) {
        qmg::Layout layout = one_room_layout(games[index], {{-768, -768, -256}, {768, 768, 384}});
        const qmg::ConstructionResult construction = mover_construction(layout, door_deltas[index]);
        construction.apply_to(layout);
        const qmg::MapBuildResult result = qmg::MapBuilder{}.build(layout, options_for(games[index]), construction);
        const qmg::MapEntity *door = find_entity(result.document, "func_door");
        const qmg::MapEntity *lift = find_entity(result.document, "func_plat");
        require(door != nullptr && lift != nullptr, "typed construction movers did not emit native entities");
        require_property(*door, "speed", "120");
        require_property(*door, "wait", "2.501");
        require_property(*door, "angle", expected_angles[index]);
        require_property(*door, "lip", "32");
        require_property(*lift, "speed", "80");
        require_property(*lift, "wait", "4");
        require_property(*lift, "height", "160");
        require(!lift->first_value("angle") && !lift->first_value("lip"),
                "lift adapter invented door-only native motion properties");
        require_canonical_fixed_point(result);
    }

    qmg::Layout directions = one_room_layout(qmg::Game::quake3, {{-1024, -1024, -512}, {1024, 1024, 512}});
    const std::array<qmg::GridVec3, 6> deltas{
        {{64, 0, 0}, {-64, 0, 0}, {0, 64, 0}, {0, -64, 0}, {0, 0, 64}, {0, 0, -64}}};
    std::vector<qmg::ConstructionRequest> requests;
    for (std::size_t index = 0; index < deltas.size(); ++index) {
        const qmg::GridCoord x = static_cast<qmg::GridCoord>(-800 + static_cast<int>(index) * 256);
        qmg::DoorAssemblySpec door;
        door.key = {static_cast<std::uint64_t>(index + 1)};
        door.leaf_bounds = {{x, -48, -48}, {static_cast<qmg::GridCoord>(x + 96), 48, 48}};
        door.travel_delta = deltas[index];
        door.speed_units_per_second = 96;
        door.wait_milliseconds = 1;
        requests.push_back(door);
    }
    const qmg::ConstructionResult direction_construction =
        qmg::ConstructionBuilder{}.build(directions, std::move(requests));
    direction_construction.apply_to(directions);
    const qmg::MapBuildResult direction_result =
        qmg::MapBuilder{}.build(directions, options_for(qmg::Game::quake3, true), direction_construction);
    std::set<std::string> angles;
    for (const qmg::MapEntity &entity : direction_result.document.entities()) {
        if (entity.first_value("classname") != "func_door")
            continue;
        require_property(entity, "lip", "32");
        require_property(entity, "wait", "0.001");
        angles.emplace(*entity.first_value("angle"));
    }
    require(angles == std::set<std::string>{"-1", "-2", "0", "180", "270", "90"},
            "door travel directions did not map to all six native angle values");
    require_canonical_fixed_point(direction_result);
}

void test_mover_rejections() {
    qmg::Layout layout = one_room_layout(qmg::Game::quake1, {{-768, -768, -256}, {768, 768, 384}});
    const qmg::ConstructionResult valid = mover_construction(layout, {64, 0, 0});
    qmg::Layout applied = layout;
    valid.apply_to(applied);
    const qmg::MapBuildOptions options = options_for(qmg::Game::quake1);

    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(layout, options, valid)); },
                        "construction_not_applied", "unapplied generated mover content was accepted");

    qmg::ConstructionResult duplicate = valid;
    duplicate.movers.push_back(duplicate.movers.front());
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(applied, options, duplicate)); },
                        "duplicate_mover_directive", "duplicate mover directives were accepted");

    qmg::ConstructionResult unresolved = valid;
    unresolved.movers.front().mover = qmg::PlacementId{9999};
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(applied, options, unresolved)); },
                        "unresolved_mover_directive", "unresolved mover directive was accepted");

    qmg::ConstructionResult inconsistent = valid;
    inconsistent.movers.front().travel_delta.x /= 2;
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(applied, options, inconsistent)); },
                        "inconsistent_mover_directive", "mover delta inconsistent with its destination was accepted");

    qmg::ConstructionResult missing = valid;
    missing.movers.clear();
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(applied, options, missing)); },
                        "missing_mover_directive", "generated mover without a directive was accepted");
}

void test_quake2_native_mechanisms() {
    for (const bool positive_delta : {false, true}) {
        qmg::Layout layout = one_room_layout(qmg::Game::quake2, {{-256, -256, -64}, {256, 256, 320}});
        qmg::LiftAssemblySpec lift;
        lift.key = {1};
        lift.platform_bounds = positive_delta ? qmg::GridBounds{{-64, -64, 0}, {64, 64, 16}}
                                              : qmg::GridBounds{{-64, -64, 160}, {64, 64, 176}};
        lift.travel_delta = {0, 0, positive_delta ? 160 : -160};
        const auto construction = qmg::ConstructionBuilder{}.build(layout, {lift});
        construction.apply_to(layout);
        const auto result = qmg::MapBuilder{}.build(layout, options_for(qmg::Game::quake2), construction);
        const auto *native = find_entity(result.document, "func_plat");
        require(native && native->primitives.size() == 1 && !native->first_value("targetname") &&
                    !native->first_value("origin"),
                "ordinary Q2 lift became a triggered or geometry-free mechanism");
        require_property(*native, "height", "160");
        require(brush_bounds(std::get<qmg::MapBrush>(native->primitives.front())) ==
                    qmg::GridBounds{{-64, -64, 160}, {64, 64, 176}},
                "Q2 func_plat brush is not at the upper station for both authored travel directions");
        require(find_entity(result.document, "info_notnull") && !find_entity(result.document, "target_position"),
                "Q2 mover marker lowered to a classname absent from the stock spawn table");
        require_canonical_fixed_point(result);
    }
    qmg::Layout layout = one_room_layout(qmg::Game::quake2);
    qmg::TeleporterAssemblySpec teleporter;
    teleporter.key = {1};
    teleporter.trigger_bounds = {{32, 32, 0}, {96, 96, 48}};
    teleporter.destination_origin = {384, 64, 24};
    teleporter.destination_angle_degrees = 270;
    const auto construction = qmg::ConstructionBuilder{}.build(layout, {teleporter});
    construction.apply_to(layout);
    const auto result = qmg::MapBuilder{}.build(layout, options_for(qmg::Game::quake2), construction);
    const auto *source = find_entity(result.document, "misc_teleporter");
    const auto *destination = find_entity(result.document, "misc_teleporter_dest");
    require(source && destination && source->primitives.empty() && destination->primitives.empty(),
            "Q2 stock discs were replaced by brush teleporters");
    require_property(*source, "origin", "64 64 24");
    require_property(*destination, "origin", "384 64 24");
    require_property(*destination, "angle", "270");
    for (auto &placement : layout.placements)
        if (placement.role == qmg::PlacementRole::teleporter_trigger)
            placement.volume = teleporter.trigger_bounds;
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(layout, options_for(qmg::Game::quake2))); },
                        "invalid_q2_teleporter_volume", "Q2 lowering accepted a fictitious large touch volume");

    qmg::Layout oversized_disc = one_room_layout(qmg::Game::quake2);
    oversized_disc.placements.push_back(point_placement(1, qmg::PlacementRole::teleporter_destination,
                                                        {std::numeric_limits<qmg::GridCoord>::max(), 64, 24}));
    require_build_error(
        [&] { static_cast<void>(qmg::MapBuilder{}.build(oversized_disc, options_for(qmg::Game::quake2))); },
        "coordinate_limit", "Q2 native disc arithmetic overflow did not use the bounded lowerer error contract");

    qmg::Layout translated_limit = one_room_layout(qmg::Game::quake2);
    qmg::LiftAssemblySpec oversized_lift;
    oversized_lift.key = {1};
    oversized_lift.platform_bounds = {{32, 32, 0}, {160, 160, 24}};
    oversized_lift.travel_delta = {0, 0, 244};
    const auto tall = qmg::ConstructionBuilder{}.build(translated_limit, {oversized_lift});
    tall.apply_to(translated_limit);
    auto constrained = options_for(qmg::Game::quake2);
    constrained.max_coordinate = 256;
    translated_limit.rooms[0].bounds.max.x = translated_limit.rooms[0].bounds.max.y = 192;
    translated_limit.rooms[0].bounds.max.z = 224;
    require_build_error([&] { static_cast<void>(qmg::MapBuilder{}.build(translated_limit, constrained, tall)); },
                        "coordinate_limit", "translated upper lift brush bypassed the native coordinate cap");
}

} // namespace

int main() {
    try {
        test_single_room_and_dialect_contract();
        test_passages_and_partial_openings();
        test_cross_nested_duplicate_and_failures();
        test_shuffle_byte_invariance();
        test_convex_geometry();
        test_liquids_and_detail_ownership();
        test_placement_profiles();
        test_placement_owned_convex_brush();
        test_curve_patch_lowering();
        test_curve_patch_rejections();
        test_typed_mover_lowering();
        test_mover_rejections();
        test_quake2_native_mechanisms();
        std::cout << "MapBuilder lowering tests passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "MapBuilder lowering test failure: " << error.what() << '\n';
        return 1;
    }
}
