#include "qmg/map_writer.hpp"
#include "qmg/pipeline.hpp"
#include "qmg/report.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// This adapter serializes the actual default pipeline result. It never invokes
// recipe construction, changes a placement or substitutes the selected map.
namespace {
namespace fs = std::filesystem;
using namespace qmg;

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

fs::path utf8_path(std::string_view text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string read_bounded(const fs::path &path, std::size_t maximum) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "cannot read input file");
    std::vector<char> bytes(maximum + 1);
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto count = input.gcount();
    require(!input.bad() && (input.eof() || !input.fail()), "file read failed");
    require(count >= 0 && static_cast<std::size_t>(count) <= maximum, "file exceeds byte budget");
    return {bytes.data(), static_cast<std::size_t>(count)};
}

bool is_link(const fs::path &path) {
    if (fs::is_symlink(fs::symlink_status(path)))
        return true;
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return false;
#endif
}

void check_path_chain(fs::path path) {
    for (;;) {
        require(!is_link(path), "input/output paths must not traverse filesystem links");
        if (path == path.parent_path() || path.empty())
            break;
        path = path.parent_path();
    }
}

fs::path output_directory(const fs::path &argument) {
    const fs::path root = fs::current_path().lexically_normal();
    check_path_chain(root);
    require(fs::is_regular_file(root / "CMakeLists.txt") &&
                fs::is_regular_file(root / "include/qmg/pipeline.hpp"),
            "run the exporter with the project root as its working directory");
    const auto path = fs::absolute(argument).lexically_normal();
    const auto relative = path.lexically_relative(root / "out");
    require(!relative.empty() && relative != "." && !relative.is_absolute(),
            "output must be a named directory inside this project's out directory");
    for (const auto &component : relative)
        require(component != "..", "output directory escapes the project output area");
    check_path_chain(path);
    if (fs::exists(path)) {
        require(fs::is_directory(path), "output must be a directory");
        // Existing compiler/viewer outputs may coexist. No file other than the
        // explicit three export targets below is replaced or consumed.
        std::size_t entries = 0;
        for (const auto &entry : fs::recursive_directory_iterator(path)) {
            require(++entries <= 4096, "existing output directory exceeds inspection budget");
            require(!is_link(entry.path()), "output directory contains a filesystem link");
            require(entry.is_directory() || entry.is_regular_file(), "output contains a special file");
        }
    }
    return path;
}

void json_string(std::ostream &out, std::string_view text) {
    constexpr char hex[] = "0123456789abcdef";
    out << '"';
    for (const unsigned char value : text) {
        if (value == '"' || value == '\\')
            out << '\\' << static_cast<char>(value);
        else if (value < 32)
            out << "\\u00" << hex[value >> 4] << hex[value & 15];
        else
            out << static_cast<char>(value);
    }
    out << '"';
}

void vector_json(std::ostream &out, GridVec3 point) {
    out << '[' << point.x << ',' << point.y << ',' << point.z << ']';
}
void bounds_json(std::ostream &out, GridBounds bounds) {
    out << '[';
    vector_json(out, bounds.min);
    out << ',';
    vector_json(out, bounds.max);
    out << ']';
}

bool valid_map_name(std::string_view name) {
    if (name.empty() || name.size() > 48 || name.front() < 'a' || name.front() > 'z')
        return false;
    for (const char ch : name)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-'))
            return false;
    if (name == "con" || name == "prn" || name == "aux" || name == "nul")
        return false;
    return !(name.size() == 4 && (name.starts_with("com") || name.starts_with("lpt")) &&
             name.back() >= '1' && name.back() <= '9');
}

struct Stair {
    const LayoutSolid *solid;
    GridBounds bounds;
    bool along_x;
};

// The supported generator contract is a box tread run: 36-unit depth,16-unit
// rise, common base and cross span. Ambiguous, branched or incomplete chains
// fail rather than being grouped by ID order or replaced by bounds geometry.
std::vector<std::vector<Stair>> infer_stairs(const Layout &layout, const std::set<GridCoord> &levels) {
    std::vector<Stair> stairs;
    for (const auto &solid : layout.solids) {
        require(std::holds_alternative<GridBounds>(solid.geometry),
                "portfolio diagram export supports AABB solids only; refusing convex flattening");
        if (solid.purpose != BrushPurpose::navigation_stair)
            continue;
        const auto bounds = std::get<GridBounds>(solid.geometry);
        const auto width = std::int64_t(bounds.max.x) - bounds.min.x;
        const auto depth = std::int64_t(bounds.max.y) - bounds.min.y;
        require(solid.contents == BrushContents::solid && !solid.owner,
                "stair metadata requires an unowned solid tread");
        require((width == 36 && depth > 36) || (depth == 36 && width > 36),
                "unsupported or ambiguous stair tread dimensions");
        require(std::int64_t(bounds.max.z) - bounds.min.z >= 16, "invalid stair height");
        stairs.push_back({&solid, bounds, width == 36});
    }
    require(stairs.size() <= 1024, "stair inference exceeds its bound");
    std::vector<int> previous(stairs.size(), -1), next(stairs.size(), -1), signs(stairs.size(), 0);
    for (std::size_t i = 0; i < stairs.size(); ++i) {
        const auto &a = stairs[i];
        for (std::size_t j = 0; j < stairs.size(); ++j) {
            const auto &b = stairs[j];
            if (i == j || a.along_x != b.along_x || a.bounds.min.z != b.bounds.min.z ||
                std::int64_t(b.bounds.max.z) - a.bounds.max.z != 16)
                continue;
            const auto cross_a = a.along_x ? std::pair{a.bounds.min.y, a.bounds.max.y}
                                          : std::pair{a.bounds.min.x, a.bounds.max.x};
            const auto cross_b = b.along_x ? std::pair{b.bounds.min.y, b.bounds.max.y}
                                          : std::pair{b.bounds.min.x, b.bounds.max.x};
            if (cross_a != cross_b)
                continue;
            const auto near_a = a.along_x ? a.bounds.min.x : a.bounds.min.y;
            const auto far_a = a.along_x ? a.bounds.max.x : a.bounds.max.y;
            const auto near_b = b.along_x ? b.bounds.min.x : b.bounds.min.y;
            const auto far_b = b.along_x ? b.bounds.max.x : b.bounds.max.y;
            const int sign = far_a == near_b ? 1 : near_a == far_b ? -1 : 0;
            if (!sign)
                continue;
            require(next[i] == -1 && previous[j] == -1, "ambiguous or branching stair chain");
            next[i] = static_cast<int>(j);
            previous[j] = static_cast<int>(i);
            signs[i] = sign;
        }
    }
    std::vector<std::vector<Stair>> runs;
    std::set<std::size_t> used;
    for (std::size_t i = 0; i < stairs.size(); ++i) {
        if (previous[i] != -1)
            continue;
        const auto &first = stairs[i].bounds;
        require(levels.contains(first.min.z) && std::int64_t(first.max.z) - first.min.z == 16,
                "stair run does not start with a 16-unit rise from a declared floor");
        std::vector<Stair> run;
        int direction = 0;
        for (int at = static_cast<int>(i); at != -1; at = next[at]) {
            require(used.insert(static_cast<std::size_t>(at)).second, "stair solid belongs to multiple runs");
            run.push_back(stairs[at]);
            if (next[at] != -1) {
                require(direction == 0 || direction == signs[at], "stair run changes axial direction");
                direction = signs[at];
            }
        }
        require(levels.contains(run.back().bounds.max.z), "stair run does not terminate at a floor/platform top");
        runs.push_back(std::move(run));
    }
    require(used.size() == stairs.size(), "unresolved stair component");
    std::sort(runs.begin(), runs.end(), [](const auto &a, const auto &b) {
        return a.front().solid->id < b.front().solid->id;
    });
    return runs;
}

bool gameplay_anchor(const Placement &placement) {
    return placement.role == PlacementRole::deathmatch_spawn || placement.role == PlacementRole::weapon ||
           placement.role == PlacementRole::armor || placement.role == PlacementRole::health;
}

std::string layout_json(const PipelineResult &result, std::string_view map_name, std::string_view title) {
    const auto &layout = result.layout;
    const auto &navigation = *result.player_navigation;
    for (const auto &placement : layout.placements)
        require(placement.role != PlacementRole::lift && placement.role != PlacementRole::teleporter_trigger,
                "portfolio exporter has no explicit mechanism display contract for this layout");
    std::set<GridCoord> levels;
    for (const auto &room : layout.rooms)
        levels.insert(room.bounds.min.z);
    for (const auto &solid : layout.solids)
        if (solid.purpose == BrushPurpose::raised_platform) {
            require(std::holds_alternative<GridBounds>(solid.geometry) && solid.contents == BrushContents::solid,
                    "floor-level export requires a solid AABB platform");
            levels.insert(std::get<GridBounds>(solid.geometry).max.z);
        }
    require(!levels.empty() && levels.size() <= 32, "invalid floor-level count");
    const auto stairs = infer_stairs(layout, levels);
    std::size_t pairs = 0, ordinary_pairs = 0, all_pairs = 0;
    const auto &edges = navigation.graph.edges();
    for (const auto &spawn : layout.placements) {
        if (spawn.role != PlacementRole::deathmatch_spawn)
            continue;
        const auto *start = navigation.find_binding(spawn.id);
        std::set<NavigationNodeId> ordinary;
        std::vector<NavigationNodeId> pending;
        std::vector<NavigationNodeId> all;
        if (start && start->node) {
            ordinary.insert(*start->node);
            pending.push_back(*start->node);
            all = reachable_navigation_nodes(navigation.graph, *start->node, 100000);
        }
        while (!pending.empty()) {
            const auto node = pending.back();
            pending.pop_back();
            for (const auto &edge : edges)
                if (edge.from == node && (edge.kind == NavigationTransitionKind::walk ||
                    edge.kind == NavigationTransitionKind::step || edge.kind == NavigationTransitionKind::door) &&
                    ordinary.insert(edge.to).second)
                    pending.push_back(edge.to);
        }
        for (const auto &placement : layout.placements) {
            if (!gameplay_anchor(placement))
                continue;
            ++pairs;
            const auto *binding = navigation.find_binding(placement.id);
            if (!binding || !binding->node)
                continue;
            ordinary_pairs += ordinary.contains(*binding->node) ? 1 : 0;
            all_pairs += std::find(all.begin(), all.end(), *binding->node) != all.end() ? 1 : 0;
        }
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"schema_version\":1,\"exporter\":\"default-generation-pipeline-aabb-v1\",\"name\":";
    json_string(out, title);
    out << ",\"map_name\":";
    json_string(out, map_name);
    out << ",\"layout_sha256\":";
    json_string(out, navigation.layout_sha256);
    out << ",\"players\":[" << layout.spec.target_players << "],\"candidate_seed\":" << layout.candidate_seed;
    if (layout.spec.layout_intent != qmg::LayoutIntent::unspecified) {
        out << ",\"layout_intent\":";
        json_string(out, qmg::to_string(layout.spec.layout_intent));
    }
    out << ",\"rooms\":[";
    bool comma = false;
    for (const auto &room : layout.rooms) {
        if (comma) out << ',';
        comma = true;
        out << "{\"id\":" << room.id.value << ",\"name\":";
        json_string(out, room.name);
        out << ",\"bounds\":";
        bounds_json(out, room.bounds);
        out << ",\"tags\":[";
        for (std::size_t i = 0; i < room.tags.size(); ++i) {
            if (i) out << ',';
            json_string(out, room.tags[i]);
        }
        out << "]}";
    }
    out << "],\"connections\":[";
    comma = false;
    for (const auto &connection : layout.connections) {
        if (comma) out << ',';
        comma = true;
        out << "{\"id\":" << connection.id.value << ",\"from\":" << connection.from_room.value
            << ",\"to\":" << connection.to_room.value << ",\"width\":" << connection.width
            << ",\"height\":" << connection.height << ",\"volumes\":[";
        for (std::size_t i = 0; i < connection.passage_volumes.size(); ++i) {
            if (i) out << ',';
            bounds_json(out, connection.passage_volumes[i]);
        }
        out << "]}";
    }
    out << "],\"solids\":[";
    comma = false;
    for (const auto &solid : layout.solids) {
        if (comma) out << ',';
        comma = true;
        out << "{\"id\":" << solid.id.value << ",\"purpose\":" << int(solid.purpose)
            << ",\"material\":" << int(solid.material) << ",\"ownership\":" << int(solid.ownership)
            << ",\"owner\":";
        if (solid.owner) out << solid.owner->value; else out << "null";
        out << ",\"bounds\":";
        bounds_json(out, std::get<GridBounds>(solid.geometry));
        out << ",\"contents\":";
        json_string(out, solid.contents == BrushContents::solid ? "solid" :
                         solid.contents == BrushContents::water ? "water" :
                         solid.contents == BrushContents::slime ? "slime" : "lava");
        out << '}';
    }
    out << "],\"placements\":[";
    comma = false;
    for (const auto &placement : layout.placements) {
        if (comma) out << ',';
        comma = true;
        out << "{\"id\":" << placement.id.value << ",\"role\":" << int(placement.role)
            << ",\"variant\":" << placement.variant << ",\"origin\":";
        vector_json(out, placement.origin);
        if (placement.volume) { out << ",\"volume\":"; bounds_json(out, *placement.volume); }
        if (placement.angle_degrees) out << ",\"angle_degrees\":" << *placement.angle_degrees;
        if (placement.link) out << ",\"link\":" << placement.link->value;
        if (placement.team) out << ",\"team\":" << int(*placement.team);
        if (placement.light_intensity) out << ",\"light_intensity\":" << *placement.light_intensity;
        if (placement.light_color) out << ",\"light_color_milli\":[" << placement.light_color->red_milli
            << ',' << placement.light_color->green_milli << ',' << placement.light_color->blue_milli << ']';
        if (placement.destination_map) { out << ",\"destination_map\":"; json_string(out, *placement.destination_map); }
        out << '}';
    }
    out << "],\"levels\":[";
    comma = false;
    for (const auto z : levels) { if (comma) out << ','; comma = true; out << z; }
    out << "],\"stair_runs\":[";
    for (std::size_t i = 0; i < stairs.size(); ++i) {
        if (i) out << ',';
        out << '[';
        for (std::size_t j = 0; j < stairs[i].size(); ++j) {
            if (j) out << ',';
            bounds_json(out, stairs[i][j].bounds);
        }
        out << ']';
    }
    out << "],\"stair_run_solid_ids\":[";
    for (std::size_t i = 0; i < stairs.size(); ++i) {
        if (i) out << ',';
        out << '[';
        for (std::size_t j = 0; j < stairs[i].size(); ++j) {
            if (j) out << ',';
            out << stairs[i][j].solid->id.value;
        }
        out << ']';
    }
    out << "],\"ramps\":[],\"drops\":[],\"mechanisms\":[],\"navigation_edge_counts\":{";
    constexpr const char *edge_names[] = {"walk", "step", "jump", "drop", "door", "lift", "teleporter"};
    for (int kind = 0; kind < 7; ++kind) {
        if (kind) out << ',';
        json_string(out, edge_names[kind]);
        out << ':' << std::count_if(edges.begin(), edges.end(), [kind](const auto &edge) { return int(edge.kind) == kind; });
    }
    out << "},\"ordinary_route_edge_kinds\":[\"walk\",\"step\",\"door\"],\"ordinary_path_checks\":"
        << ordinary_pairs << ",\"path_checks\":" << all_pairs << ",\"required_gameplay_pairs\":" << pairs
        << ",\"ordinary_missing_pairs\":" << pairs - ordinary_pairs
        << ",\"export_limits\":[\"Default pipeline MAP/report are unmodified. Optional title is display metadata only.\","
           "\"Stair runs are exact unambiguous36-depth/16-rise AABB chains, not native movement certificates.\","
           "\"Levels use room floors and raised-platform tops; intermediate treads are not floors.\","
           "\"Placement records retain typed roles; native classes come from the unmodified MAP/BSP.\","
           "\"Graph reachability counts are observations, not hard acceptance or multiplayer quality.\"]}\n";
    return out.str();
}

void preflight_file(const fs::path &path, std::string_view contents) {
    check_path_chain(path);
    if (!fs::exists(path))
        return;
    require(fs::is_regular_file(path) && fs::hard_link_count(path) == 1,
            "export target must be an independent regular file");
    require(fs::file_size(path) == contents.size() && read_bounded(path, contents.size()) == contents,
            "refusing to overwrite a different existing export; use a new output directory");
}

void write_new(const fs::path &path, std::string_view contents) {
    preflight_file(path, contents);
    if (fs::exists(path))
        return;
    std::ofstream output(path, std::ios::binary);
    require(bool(output), "cannot create export file");
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    require(bool(output), "export write failed");
}

int run(const std::vector<std::string> &args) {
    require(args.size() == 4 || args.size() == 5,
            "usage: qmg_mp_portfolio_export <prompt-file> <output-directory> <map-name> [display-title]");
    require(valid_map_name(args[3]), "map-name must be a nonreserved lowercase filename identifier of 1–48 characters");
    require(args.size() != 5 || (!args[4].empty() && args[4].size() <= 256), "display title must have 1–256 UTF-8 bytes");
    const auto prompt_path = fs::absolute(utf8_path(args[1])).lexically_normal();
    check_path_chain(prompt_path);
    require(fs::is_regular_file(prompt_path), "prompt must be a regular file");
    const auto prompt = read_bounded(prompt_path, 65536);
    require(!prompt.empty(), "prompt must not be empty");
    const auto output = output_directory(utf8_path(args[2]));
    const auto result = GenerationPipeline{}.run(prompt);
    require(result.layout.spec.game == Game::quake2 &&
                (result.layout.spec.mode == GameMode::deathmatch || result.layout.spec.mode == GameMode::duel),
            "portfolio exporter currently supports Quake II deathmatch/duel only");
    require(result.validation.ok() && result.map_build && result.player_navigation && result.quality,
            "default pipeline did not produce a structurally valid analyzed map");
    const auto map = MapWriter{}.render(result.map_build->document);
    const auto report = render_report(result);
    const auto layout = layout_json(result, args[3], args.size() == 5 ? args[4] : result.layout.spec.title);
    const std::array<std::pair<fs::path, std::string_view>, 3> files{{
        {output / (args[3] + ".map"), map}, {output / (args[3] + ".qmg.json"), report},
        {output / "layout.json", layout}}};
    // Validate every destination before creating any output, including aliases
    // of the prompt or pre-existing maps. Identical reruns leave files untouched.
    for (const auto &[path, contents] : files) {
        require(path != prompt_path, "prompt cannot also be an output");
        preflight_file(path, contents);
    }
    fs::create_directories(output);
    for (const auto &[path, contents] : files)
        write_new(path, contents);
    std::cout << "exported " << args[3] << " with default candidate selection; rooms=" << result.layout.rooms.size()
              << " candidate_seed=" << result.layout.candidate_seed << " planner=" << result.planner.used_backend
              << " ggml=" << result.ggml_backend << '\n';
    return 0;
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    try {
        std::vector<std::string> args;
        for (int i = 0; i < argc; ++i) {
            const auto length = std::char_traits<wchar_t>::length(argv[i]);
            require(length <= static_cast<std::size_t>(std::numeric_limits<int>::max()), "argument is too long");
            const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], static_cast<int>(length),
                                                  nullptr, 0, nullptr, nullptr);
            require(bytes > 0 || length == 0, "invalid UTF-16 argument");
            std::string text(static_cast<std::size_t>(bytes), '\0');
            if (bytes)
                require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], static_cast<int>(length),
                                              text.data(), bytes, nullptr, nullptr) == bytes,
                        "argument conversion failed");
            args.push_back(std::move(text));
        }
        return run(args);
    } catch (const std::exception &error) {
        std::cerr << "portfolio export: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(int argc, char **argv) {
    try {
        return run(std::vector<std::string>(argv, argv + argc));
    } catch (const std::exception &error) {
        std::cerr << "portfolio export: " << error.what() << '\n';
        return 1;
    }
}
#endif
