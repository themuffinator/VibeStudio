#pragma once

#include "qmg/map_document_writer.hpp"
#include "qmg/report.hpp"
#include "qmg/validator.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace qmg::recipes {

inline void vector_json(std::ostream &out, GridVec3 p) { out << '[' << p.x << ',' << p.y << ',' << p.z << ']'; }
inline void bounds_json(std::ostream &out, GridBounds b) {
    out << '[';
    vector_json(out, b.min);
    out << ',';
    vector_json(out, b.max);
    out << ']';
}

// Keep the same planner-cost basis for comparisons between original recipes.
// Costs are graph approximations; they are not measured engine travel times.
inline void write_flow(const PipelineResult &result, const std::filesystem::path &path) {
    std::ofstream out(path, std::ios::binary);
    out << "{\"cost_basis\":\"endpoint-aware Manhattan/node-anchor proxy v2\",\"layout_sha256\":"
        << std::quoted(result.player_navigation->layout_sha256) << ",\"cost_units_per_world_unit\":1000,\"timings\":[";
    bool first = true;
    for (const auto &timing : result.quality->placement_timing) {
        if (!first)
            out << ',';
        first = false;
        out << "{\"spawn\":" << timing.spawn.value << ",\"target\":" << timing.target.value
            << ",\"kind\":" << int(timing.target_kind) << ",\"reachable\":" << (timing.reachable ? "true" : "false")
            << ",\"cost\":" << timing.navigation_cost << '}';
    }
    out << "]}";
    if (!out)
        throw std::runtime_error("flow report write failed");
}

struct Recipe {
    struct DropSpec {
        std::string label;
        GridVec3 from, departure, to;
    };
    PipelineResult result;
    std::map<PlacementId, std::string> native_classes;
    std::string map_name;
    std::vector<GridCoord> floor_levels{0, 192, 384};
    std::vector<std::vector<SolidId>> stair_runs;
    std::map<SolidId, std::vector<std::array<GridCoord, 2>>> footprints;
    std::optional<ConstructionResult> construction;
    std::vector<RampSpec> ramp_specs;
    std::vector<DropSpec> drop_specs;
    std::map<std::string, std::string> material_overrides;
    std::map<std::string, Quake2FaceMetadata> quake2_material_metadata_overrides;
    std::set<RoomId> sky_rooms;
    std::map<std::string, std::string> worldspawn_properties;
    bool require_ordinary_routes{};
    bool require_no_drop_routes{};
    Layout &level() { return result.layout; }

    Recipe(std::string name, std::string title, std::string prompt, std::uint64_t seed, int rooms = 5)
        : map_name(std::move(name)) {
        auto &l = level();
        l.spec = PromptParser{}.parse(prompt, {Game::quake2, GameMode::deathmatch, seed, rooms, 6});
        l.spec.title = std::move(title);
        l.candidate_seed = seed;
        l.planned_verticality = .95;
        l.planned_intensity = .65;
        const auto plan = SemanticPlanner{}.plan(l.spec);
        result.semantic_plan = plan.plan;
        result.planner = plan.trace;
        result.planner.used_backend = map_name + "/original-recipe-v1";
        result.ggml_backend = "not used: explicit experimental recipe";
        result.ggml_version = result.ggml_commit = result.llama_revision = "not used";
        result.dialect = MapDialect::quake2_valve220;
    }
    void room(std::string name, GridBounds b) {
        level().rooms.push_back({next_room_id(level()), std::move(name), b, {"combat"}});
    }
    void connection(unsigned a, unsigned b, int width, std::initializer_list<GridBounds> volumes) {
        level().connections.push_back({next_connection_id(level()), {a}, {b}, width, 192, volumes});
    }
    void solid(GridBounds b, MaterialRole material = MaterialRole::wall,
               BrushPurpose purpose = BrushPurpose::structural) {
        level().solids.push_back(
            {next_solid_id(level()), b, material, purpose, BrushOwnership::structural, BrushContents::solid, {}});
    }
    void deck(int x0, int y0, int x1, int y1, int z) {
        solid({{x0, y0, z - 24}, {x1, y1, z}}, MaterialRole::platform, BrushPurpose::raised_platform);
    }
    // Counter-clockwise convex vertical prism; exact planes are retained while
    // the current navigation analyzer conservatively uses aggregate blockers.
    void prism(std::initializer_list<std::array<GridCoord, 2>> points, int low, int high) {
        const std::vector<std::array<GridCoord, 2>> xy(points);
        if (xy.size() < 3 || low >= high)
            throw std::invalid_argument("invalid prism");
        const auto vertex = [&](std::size_t i, int z) { return GridVec3{xy[i][0], xy[i][1], z}; };
        ConvexPolyhedron poly;
        poly.planes.push_back({vertex(2, low), vertex(1, low), vertex(0, low)});
        poly.planes.push_back({vertex(0, high), vertex(1, high), vertex(2, high)});
        for (std::size_t i = 0; i < xy.size(); ++i)
            poly.planes.push_back(
                {vertex(i, low), vertex((i + 1) % xy.size(), low), vertex((i + 1) % xy.size(), high)});
        if (!validate_brush_geometry(poly).ok())
            throw std::invalid_argument("prism must be convex and counter-clockwise");
        const auto id = next_solid_id(level());
        level().solids.push_back({id,
                                  std::move(poly),
                                  MaterialRole::wall,
                                  BrushPurpose::structural,
                                  BrushOwnership::structural,
                                  BrushContents::solid,
                                  {}});
        footprints.emplace(id, xy);
    }
    void assemble(std::vector<ConstructionRequest> requests) {
        if (construction)
            throw std::logic_error("assemble all construction requests together");
        construction = ConstructionBuilder{}.build(level(), std::move(requests));
        construction->apply_to(level());
    }
    void ramp(GridVec3 start, HorizontalDirection direction, int width, int run = 480, int rise = 120) {
        RampSpec spec;
        spec.key = {ramp_specs.size() + 1};
        spec.start = start;
        spec.direction = direction;
        spec.width = width;
        spec.run = run;
        spec.low_height = 8;
        spec.rise = rise;
        spec.ownership = BrushOwnership::structural;
        ramp_specs.push_back(spec);
    }
    static bool gameplay_anchor(const Placement &p) {
        return p.role == PlacementRole::deathmatch_spawn || p.role == PlacementRole::weapon ||
               p.role == PlacementRole::armor || p.role == PlacementRole::health;
    }
    void stairs(int x, int y, int base, int axis, int count = 12) {
        stair_runs.emplace_back();
        for (int i = 0; i < count; ++i) {
            const int sx = x + (axis == 0 ? i * 36 : 0), sy = y + (axis == 1 ? i * 36 : 0);
            solid({{sx, sy, base}, {sx + (axis == 0 ? 36 : 128), sy + (axis == 1 ? 36 : 128), base + (i + 1) * 16}},
                  MaterialRole::stair, BrushPurpose::navigation_stair);
            stair_runs.back().push_back(level().solids.back().id);
        }
    }
    PlacementId point(PlacementRole role, GridVec3 origin, int variant = 0, std::string native_class = "") {
        Placement p;
        p.id = next_placement_id(level());
        p.role = role;
        p.origin = origin;
        p.variant = static_cast<std::uint16_t>(variant);
        level().placements.push_back(p);
        if (!native_class.empty())
            native_classes.emplace(p.id, std::move(native_class));
        return p.id;
    }
    void spawn(GridVec3 origin, int angle) {
        point(PlacementRole::deathmatch_spawn, origin);
        level().placements.back().angle_degrees = static_cast<std::int16_t>(angle);
    }
    void light(GridVec3 p, int power = 340, RgbColor color = {860, 920, 1000}) {
        point(PlacementRole::light, p);
        level().placements.back().light_intensity = power;
        level().placements.back().light_color = color;
    }

    // Retain a sealed solid ceiling and change only its room-owned inner face
    // to Q2 sky. Geometry and unrelated indoor ceilings remain unchanged.
    void apply_environment(MapDocument &document, const std::vector<MapBuildManifestEntry> &manifest) {
        std::map<MapNodeId, GridCoord> sky_nodes;
        for (const auto room_id : sky_rooms) {
            const auto room = std::find_if(level().rooms.begin(), level().rooms.end(),
                                          [&](const auto &value) { return value.id == room_id; });
            if (room == level().rooms.end())
                throw std::invalid_argument("sky room does not exist");
            for (const auto &entry : manifest)
                if (entry.source.kind == MapBuildSourceKind::room && entry.source.id == room_id.value)
                    for (const auto id : entry.node_ids)
                        sky_nodes[id] = room->bounds.max.z;
        }
        std::set<MapNodeId> changed_nodes;
        MapNodeId next_id = 1;
        for (const auto &entity : document.entities()) {
            next_id = std::max(next_id, entity.id + 1);
            for (const auto &property : entity.properties)
                next_id = std::max(next_id, property.id + 1);
            for (const auto &primitive : entity.primitives)
                std::visit([&](const auto &value) {
                    next_id = std::max(next_id, value.id + 1);
                    if constexpr (std::is_same_v<std::decay_t<decltype(value)>, MapBrush>)
                        for (const auto &face : value.faces)
                            next_id = std::max(next_id, face.id + 1);
                    else
                        for (const auto &control : value.control_points)
                            next_id = std::max(next_id, control.id + 1);
                }, primitive);
        }
        for (auto &entity : document.edit_entities()) {
            if (entity.first_value("classname") != "worldspawn")
                continue;
            for (const auto &[key, value] : worldspawn_properties) {
                if (key == "classname")
                    throw std::invalid_argument("environment cannot replace worldspawn classname");
                auto property = std::find_if(entity.properties.begin(), entity.properties.end(),
                                             [&](const auto &p) { return p.key == key; });
                if (property == entity.properties.end())
                    entity.properties.push_back({next_id++, key, value});
                else
                    property->value = value;
            }
            for (auto &primitive : entity.primitives) {
                auto *brush = std::get_if<MapBrush>(&primitive);
                if (!brush || !sky_nodes.contains(brush->id))
                    continue;
                for (auto &face : brush->faces) {
                    const auto height = sky_nodes.at(brush->id);
                    if (face.material != "e1u1/ceil1_4" ||
                        !std::all_of(face.plane_points.begin(), face.plane_points.end(),
                                     [&](const auto &p) { return p.z == height; }))
                        continue;
                    face.material = "e1u1/sky1";
                    face.quake2_metadata = Quake2FaceMetadata{1, 4, 0};
                    changed_nodes.insert(brush->id);
                }
            }
        }
        for (const auto room_id : sky_rooms) {
            const auto entry = std::find_if(manifest.begin(), manifest.end(), [&](const auto &value) {
                return value.source.kind == MapBuildSourceKind::room && value.source.id == room_id.value;
            });
            if (entry == manifest.end() || !std::any_of(entry->node_ids.begin(), entry->node_ids.end(),
                [&](const auto id) { return changed_nodes.contains(id); }))
                throw std::runtime_error("selected sky room has no owned ceiling face");
        }
    }

    void finish(const std::filesystem::path &output) {
        if (!ramp_specs.empty()) {
            std::vector<ConstructionRequest> requests;
            for (const auto &ramp : ramp_specs)
                requests.push_back(ramp);
            assemble(std::move(requests));
        }
        auto &l = level();
        std::filesystem::create_directories(output);
        result.validation = LayoutValidator{}.validate(l);
        for (const auto &issue : result.validation.issues)
            std::cerr << issue.code << ": " << issue.message << '\n';
        if (!result.validation.ok()) {
            for (const auto &p : l.placements)
                for (const auto &s : l.solids)
                    if (checked_brush_bounds(s.geometry).contains(p.origin))
                        std::cerr << "Placement " << p.id.value << " in solid " << s.id.value << " at " << p.origin.x
                                  << ',' << p.origin.y << ',' << p.origin.z << '\n';
            throw std::runtime_error("layout validation failed");
        }
        MapBuildOptions build_options;
        build_options.dialect = MapDialect::quake2_valve220;
        build_options.shell_thickness = 24;
        result.map_build =
            construction ? MapBuilder{}.build(l, build_options, *construction) : MapBuilder{}.build(l, build_options);
        auto &document = result.map_build->document;
        std::map<MapNodeId, std::string> classes;
        for (const auto &entry : result.map_build->manifest)
            if (entry.source.kind == MapBuildSourceKind::placement)
                if (auto it = native_classes.find({entry.source.id}); it != native_classes.end())
                    for (const auto node : entry.node_ids)
                        classes[node] = it->second;
        for (auto &entity : document.edit_entities()) {
            if (auto it = classes.find(entity.id); it != classes.end())
                for (auto &p : entity.properties)
                    if (p.key == "classname")
                        p.value = it->second;
            for (auto &primitive : entity.primitives) {
                auto *brush = std::get_if<MapBrush>(&primitive);
                if (!brush)
                    continue;
                for (auto &face : brush->faces) {
                    const auto &p = face.plane_points;
                    const double x = (p[0].x + p[1].x + p[2].x) / 3., y = (p[0].y + p[1].y + p[2].y) / 3.;
                    const bool horizontal = p[0].z == p[1].z && p[1].z == p[2].z;
                    if (face.material == "e1u1/floor3_1") {
                        if (horizontal && p[0].z == floor_levels.back())
                            face.material = "e1u1/ggrat12_2";
                        else if (horizontal && floor_levels.size() > 1 && p[0].z == floor_levels[1])
                            face.material = "e1u1/grate1_5";
                        else if (horizontal && p[0].z > 0)
                            face.material = "e1u1/stairs1_1";
                        else if (y < -320)
                            face.material = "e1u1/floor3_2";
                    } else if (face.material == "e1u1/flat1_1") {
                        if (x > 384)
                            face.material = "e1u1/flat1_2";
                        if (y > 512)
                            face.material = "e1u1/btactmach0";
                    }
                    if (const auto replacement = material_overrides.find(face.material);
                        replacement != material_overrides.end())
                        face.material = replacement->second;
                    if (const auto metadata = quake2_material_metadata_overrides.find(face.material);
                        metadata != quake2_material_metadata_overrides.end())
                        face.quake2_metadata = metadata->second;
                }
            }
        }
        if (!sky_rooms.empty() || !worldspawn_properties.empty())
            apply_environment(document, result.map_build->manifest);
        MapWriteOptions options;
        options.validate_document = true;
        options.validation_options.profile = MapValidationProfile::generated_document;
        std::ofstream map(output / (map_name + ".map"), std::ios::binary);
        map << MapDocumentWriter{}.write(document, options);
        map.close();
        if (!map)
            throw std::runtime_error("map write failed");
        result.player_navigation = NavigationAnalyzer{}.analyze(l);
        result.visibility = VisibilityAnalyzer{}.analyze(l);
        result.lighting = LightingAnalyzer{}.analyze(l);
        result.quality =
            QualityAnalyzer{}.analyze(l, {*result.player_navigation, *result.visibility, *result.lighting});
        write_report(result, output / (map_name + ".qmg.json"));
        for (const auto &finding : result.player_navigation->findings)
            std::cerr << "Navigation: " << finding.message << '\n';
        for (const auto &sample : result.lighting->samples)
            if (!sample.readable)
                std::cerr << "Dark probe " << sample.id.value << " at " << sample.position.x << ',' << sample.position.y
                          << ',' << sample.position.z << '\n';
        if (!result.player_navigation->ok())
            throw std::runtime_error("navigation failed");
        std::size_t checked = 0, ordinary_checked = 0;
        for (const auto &spawn : l.placements) {
            if (spawn.role != PlacementRole::deathmatch_spawn)
                continue;
            const auto *start = result.player_navigation->find_binding(spawn.id);
            if (!start || !start->node)
                throw std::runtime_error("unbound spawn");
            const auto reached = reachable_navigation_nodes(result.player_navigation->graph, *start->node, 100000);
            std::set<NavigationNodeId> ordinary{*start->node};
            std::vector<NavigationNodeId> pending{*start->node};
            while (!pending.empty()) {
                const auto node = pending.back();
                pending.pop_back();
                for (const auto &edge : result.player_navigation->graph.edges())
                    if (edge.from == node && edge.kind != NavigationTransitionKind::lift &&
                        edge.kind != NavigationTransitionKind::teleporter &&
                        edge.kind != NavigationTransitionKind::jump &&
                        (!require_no_drop_routes || edge.kind != NavigationTransitionKind::drop) &&
                        ordinary.insert(edge.to).second)
                        pending.push_back(edge.to);
            }
            for (const auto &p : l.placements) {
                if (!gameplay_anchor(p))
                    continue;
                const auto *binding = result.player_navigation->find_binding(p.id);
                if (!binding || !binding->node ||
                    std::find(reached.begin(), reached.end(), *binding->node) == reached.end())
                    throw std::runtime_error("unreachable gameplay placement " + std::to_string(p.id.value));
                ++checked;
                if (ordinary.contains(*binding->node))
                    ++ordinary_checked;
                else if (require_ordinary_routes)
                    throw std::runtime_error("mechanism is required for gameplay anchor " + std::to_string(p.id.value));
            }
        }
        write_flow(result, output / "flow.json");
        std::ofstream geometry(output / "layout.json", std::ios::binary);
        geometry << "{\"name\":" << std::quoted(l.spec.title)
                 << ",\"layout_sha256\":" << std::quoted(result.player_navigation->layout_sha256) << ",\"rooms\":[";
        bool first = true;
        for (const auto &room : l.rooms) {
            if (!first)
                geometry << ',';
            first = false;
            geometry << "{\"id\":" << room.id.value << ",\"name\":" << std::quoted(room.name) << ",\"bounds\":";
            bounds_json(geometry, room.bounds);
            geometry << '}';
        }
        geometry << "],\"connections\":[";
        first = true;
        for (const auto &c : l.connections) {
            if (!first)
                geometry << ',';
            first = false;
            geometry << "{\"id\":" << c.id.value << ",\"from\":" << c.from_room.value << ",\"to\":" << c.to_room.value
                     << ",\"volumes\":[";
            bool fv = true;
            for (const auto &v : c.passage_volumes) {
                if (!fv)
                    geometry << ',';
                fv = false;
                bounds_json(geometry, v);
            }
            geometry << "]}";
        }
        geometry << "],\"solids\":[";
        first = true;
        for (const auto &s : l.solids) {
            if (!first)
                geometry << ',';
            first = false;
            geometry << "{\"id\":" << s.id.value << ",\"owner\":";
            if (s.owner)
                geometry << s.owner->value;
            else
                geometry << "null";
            geometry << ",\"purpose\":" << int(s.purpose) << ",\"bounds\":";
            bounds_json(geometry, checked_brush_bounds(s.geometry));
            if (s.contents != BrushContents::solid)
                geometry << ",\"contents\":"
                         << std::quoted(s.contents == BrushContents::water   ? "water"
                                        : s.contents == BrushContents::slime ? "slime"
                                                                             : "lava");
            if (const auto it = footprints.find(s.id); it != footprints.end()) {
                geometry << ",\"footprint\":[";
                for (std::size_t i = 0; i < it->second.size(); ++i) {
                    if (i)
                        geometry << ',';
                    geometry << '[' << it->second[i][0] << ',' << it->second[i][1] << ']';
                }
                geometry << ']';
            }
            geometry << '}';
        }
        geometry << "],\"placements\":[";
        first = true;
        for (const auto &p : l.placements) {
            if (p.role == PlacementRole::light)
                continue;
            if (!first)
                geometry << ',';
            first = false;
            geometry << "{\"id\":" << p.id.value << ",\"role\":" << int(p.role) << ",\"origin\":";
            vector_json(geometry, p.origin);
            if (p.volume) {
                geometry << ",\"volume\":";
                bounds_json(geometry, *p.volume);
            }
            geometry << '}';
        }
        geometry << "],\"map_name\":" << std::quoted(map_name) << ",\"players\":[4,6],\"levels\":[";
        for (std::size_t i = 0; i < floor_levels.size(); ++i) {
            if (i)
                geometry << ',';
            geometry << floor_levels[i];
        }
        geometry << "],\"stair_runs\":[";
        for (std::size_t i = 0; i < stair_runs.size(); ++i) {
            if (i)
                geometry << ',';
            geometry << '[';
            for (std::size_t j = 0; j < stair_runs[i].size(); ++j) {
                if (j)
                    geometry << ',';
                const auto it = std::find_if(l.solids.begin(), l.solids.end(),
                                             [&](const LayoutSolid &s) { return s.id == stair_runs[i][j]; });
                if (it == l.solids.end())
                    throw std::runtime_error("missing stair solid");
                bounds_json(geometry, checked_brush_bounds(it->geometry));
            }
            geometry << ']';
        }
        geometry << "],\"ramps\":[";
        for (std::size_t i = 0; i < ramp_specs.size(); ++i) {
            const auto &ramp = ramp_specs[i];
            const bool x =
                ramp.direction == HorizontalDirection::positive_x || ramp.direction == HorizontalDirection::negative_x;
            const int sign =
                (ramp.direction == HorizontalDirection::negative_x || ramp.direction == HorizontalDirection::negative_y)
                    ? -1
                    : 1;
            auto low = ramp.start, high = ramp.start;
            low.z += ramp.low_height;
            high.z += ramp.low_height + ramp.rise;
            if (x)
                high.x += sign * ramp.run;
            else
                high.y += sign * ramp.run;
            auto low_far = low, high_far = high;
            if (x) {
                low_far.y += ramp.width;
                high_far.y += ramp.width;
            } else {
                low_far.x += ramp.width;
                high_far.x += ramp.width;
            }
            const GridBounds ramp_bounds{{std::min(low.x, high.x), std::min(low.y, high.y), ramp.start.z},
                                         {std::max(low_far.x, high_far.x), std::max(low_far.y, high_far.y), high.z}};
            // Construction preserves sorted request keys. Its conservative
            // floating bounds can expand a sloped face by one integer unit;
            // bind by the constructed ID, retaining the exact authored surface.
            const auto constructed_id = construction->solids.at(i).id;
            const auto solid = std::find_if(l.solids.begin(), l.solids.end(), [&](const LayoutSolid &s) {
                return s.purpose == BrushPurpose::ramp && s.id == constructed_id;
            });
            if (solid == l.solids.end())
                throw std::runtime_error("ramp metadata does not match construction");
            if (i)
                geometry << ',';
            geometry << "{\"id\":" << ramp.key.value << ",\"solid_id\":" << solid->id.value << ",\"bounds\":";
            bounds_json(geometry, ramp_bounds);
            geometry << ",\"surface\":[";
            vector_json(geometry, low);
            geometry << ',';
            vector_json(geometry, low_far);
            geometry << ',';
            vector_json(geometry, high_far);
            geometry << ',';
            vector_json(geometry, high);
            geometry << ']';
            if (x) {
                low.y += ramp.width / 2;
                high.y += ramp.width / 2;
            } else {
                low.x += ramp.width / 2;
                high.x += ramp.width / 2;
            }
            geometry << ",\"from\":";
            vector_json(geometry, low);
            geometry << ",\"to\":";
            vector_json(geometry, high);
            geometry << ",\"width\":" << ramp.width << ",\"run\":" << ramp.run << ",\"rise\":" << ramp.rise
                     << ",\"base_floor\":" << ramp.start.z << '}';
        }
        geometry << ']';
        if (!drop_specs.empty()) {
            geometry << ",\"drops\":[";
            for (std::size_t i = 0; i < drop_specs.size(); ++i) {
                if (i)
                    geometry << ',';
                const auto &drop = drop_specs[i];
                geometry << "{\"id\":" << i + 1 << ",\"label\":" << std::quoted(drop.label) << ",\"from\":";
                vector_json(geometry, drop.from);
                geometry << ",\"departure\":";
                vector_json(geometry, drop.departure);
                geometry << ",\"to\":";
                vector_json(geometry, drop.to);
                geometry << '}';
            }
            geometry << ']';
        }
        geometry << ",\"mechanisms\":[";
        bool first_mechanism = true;
        for (const auto &p : l.placements) {
            if ((p.role != PlacementRole::lift && p.role != PlacementRole::teleporter_trigger) || !p.link || !p.volume)
                continue;
            const auto destination = std::find_if(l.placements.begin(), l.placements.end(),
                                                  [&](const Placement &target) { return target.id == *p.link; });
            if (destination == l.placements.end())
                throw std::runtime_error("unresolved mechanism metadata");
            if (!first_mechanism)
                geometry << ',';
            first_mechanism = false;
            const bool lift = p.role == PlacementRole::lift;
            auto from = p.origin, to = destination->origin;
            if (lift) {
                const auto half = p.volume->max.z - p.origin.z;
                from.z += half;
                to.z += half;
            } else {
                from.z -= 24;
                to.z -= 24;
            }
            geometry << "{\"kind\":" << std::quoted(lift ? "lift" : "teleporter") << ",\"id\":" << p.id.value
                     << ",\"label\":"
                     << std::quoted(lift ? "Lift: wait or take the stairs" : "One-way teleporter: committed exit")
                     << ",\"from\":";
            vector_json(geometry, from);
            geometry << ",\"to\":";
            vector_json(geometry, to);
            geometry << ",\"bounds\":";
            bounds_json(geometry, *p.volume);
            geometry << '}';
        }
        geometry << "],\"navigation_edge_counts\":{";
        const char *edge_names[] = {"walk", "step", "jump", "drop", "door", "lift", "teleporter"};
        for (int kind = 0; kind < 7; ++kind) {
            if (kind)
                geometry << ',';
            const auto count = std::count_if(result.player_navigation->graph.edges().begin(),
                                             result.player_navigation->graph.edges().end(),
                                             [&](const NavigationEdge &edge) { return int(edge.kind) == kind; });
            geometry << std::quoted(edge_names[kind]) << ':' << count;
        }
        geometry << "},\"ordinary_route_edge_kinds\":"
                 << (require_no_drop_routes ? "[\"walk\",\"step\",\"door\"],"
                                            : "[\"walk\",\"step\",\"drop\",\"door\"],")
                 << "\"ordinary_path_checks\":" << ordinary_checked << ",\"path_checks\":" << checked << '}';
        if (!geometry)
            throw std::runtime_error("geometry export failed");
        std::cout << l.spec.title << ": " << checked << " spawn-to-gameplay-anchor checks passed; "
                  << result.map_build->stats.brush_count << " brushes; "
                  << result.visibility->summary.articulation_rooms << " articulation rooms.\n";
    }
};
} // namespace qmg::recipes
