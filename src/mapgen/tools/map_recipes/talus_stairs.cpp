// Controlled ascent variant. Compile the unchanged Talus recipe body with a
// narrowly scoped Recipe namespace adapter; do not duplicate the map recipe or
// rewrite its generated MAP text. Only the first, explicitly checked ramp call
// is intercepted. The macro below is undefined immediately after the include.
#include "qmg/map_parser.hpp"
#include "recipe_support.hpp"

#include <fstream>
#include <iterator>
#include <limits>

namespace qmg::talus_stair_variant {

struct Recipe final : qmg::recipes::Recipe {
    using qmg::recipes::Recipe::Recipe;

    void ramp(const GridVec3 start, const HorizontalDirection direction, const int width, const int run = 480,
              const int rise = 120) {
        ++ramp_calls_;
        if (ramp_calls_ == 1) {
            if (start != GridVec3{-736, -160, 0} || direction != HorizontalDirection::positive_y || width != 160 ||
                run != 480 || rise != 120)
                throw std::runtime_error("Talus Bench ramp changed; controlled stairs comparison must be reviewed");
            ++intercepted_;
            return;
        }
        if (ramp_calls_ != 2 || start != GridVec3{-624, 576, 128} || direction != HorizontalDirection::positive_x ||
            width != 160 || run != 480 || rise != 120)
            throw std::runtime_error("Talus nonintervention ramp changed; controlled comparison must be reviewed");
        qmg::recipes::Recipe::ramp(start, direction, width, run, rise);
    }

    void finish(const std::filesystem::path &output) {
        if (intercepted_ != 1 || ramp_calls_ != 2 || ramp_specs.size() != 1 || construction ||
            map_name != "qmg_talus" || level().spec.title != "Talus" || stair_runs.size() != 4 ||
            level().solids.size() != 57)
            throw std::runtime_error("Talus source drift invalidated the single-intervention comparison");

        // Same 160x480 footprint and floor-0/floor-128 landings. Keep the ramp's
        // material role, solid contents and structural ownership. The eight
        // 60-unit treads change the support profile only; no lights/items move.
        stair_runs.emplace_back();
        for (int step = 0; step < 8; ++step) {
            solid({{-736, -160 + step * 60, 0}, {-576, -160 + (step + 1) * 60, (step + 1) * 16}}, MaterialRole::ramp,
                  BrushPurpose::navigation_stair);
            stair_runs.back().push_back(level().solids.back().id);
        }
        map_name = "qmg_talus_stairs";
        level().spec.title = "Talus Bench Stairs";
        result.planner.used_backend = "qmg_talus_stairs/controlled-bench-ascent-v1";
        qmg::recipes::Recipe::finish(output);
        preserve_ascent_material(output / (map_name + ".map"));
    }

  private:
    static void preserve_ascent_material(const std::filesystem::path &path) {
        // The shared palette normally maps horizontal intermediate floors to
        // stair/grate textures. Undo that only on this intervention's eight
        // exact boxes, through the native parser/document API, so material and
        // bounce changes do not become a second experimental variable.
        if (std::filesystem::file_size(path) > 2U * 1024U * 1024U)
            throw std::runtime_error("Variant MAP exceeds controlled parse cap");
        std::ifstream input(path, std::ios::binary);
        const std::string source{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
        if (!input.eof() && input.fail())
            throw std::runtime_error("Could not read variant MAP for material normalization");
        MapParseLimits limits;
        limits.max_source_bytes = 2U * 1024U * 1024U;
        limits.max_absolute_number = 32768;
        auto document = MapParser{limits}.parse_or_throw(source, MapDialect::quake2_valve220);
        auto &entities = document.edit_entities();
        if (entities.empty() || entities.front().last_value("classname") != "worldspawn")
            throw std::runtime_error("Variant MAP has no worldspawn");
        unsigned found_mask = 0, changed_faces = 0;
        for (auto &primitive : entities.front().primitives) {
            auto *brush = std::get_if<MapBrush>(&primitive);
            if (!brush)
                throw std::runtime_error("Unexpected patch in controlled brush variant");
            Vec3 low{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                     std::numeric_limits<double>::max()};
            Vec3 high{-low.x, -low.y, -low.z};
            for (const auto &face : brush->faces)
                for (const auto &point : face.plane_points) {
                    low.x = std::min(low.x, point.x);
                    low.y = std::min(low.y, point.y);
                    low.z = std::min(low.z, point.z);
                    high.x = std::max(high.x, point.x);
                    high.y = std::max(high.y, point.y);
                    high.z = std::max(high.z, point.z);
                }
            for (unsigned step = 0; step < 8; ++step) {
                if (low.x != -736 || high.x != -576 || low.y != -160 + static_cast<int>(step) * 60 ||
                    high.y != -100 + static_cast<int>(step) * 60 || low.z != 0 || high.z != (step + 1) * 16)
                    continue;
                if (brush->faces.size() != 6 || (found_mask & (1U << step)))
                    throw std::runtime_error("Replacement tread identity is ambiguous");
                found_mask |= 1U << step;
                unsigned face_mask = 0;
                for (auto &face : brush->faces) {
                    const auto &p = face.plane_points;
                    // Native MAP planes use (p2-p0) x (p1-p0), matching
                    // MapDocument's outward-plane validation convention.
                    const Vec3 a{p[2].x - p[0].x, p[2].y - p[0].y, p[2].z - p[0].z};
                    const Vec3 b{p[1].x - p[0].x, p[1].y - p[0].y, p[1].z - p[0].z};
                    const std::array<double, 3> normal{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                                                       a.x * b.y - a.y * b.x};
                    if (std::count_if(normal.begin(), normal.end(), [](double value) { return value != 0; }) != 1)
                        throw std::runtime_error("Replacement tread is not an exact axial box");
                    const std::array<double, 3> point{p[0].x, p[0].y, p[0].z}, minimum{low.x, low.y, low.z},
                        maximum{high.x, high.y, high.z};
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        if (normal[axis] == 0)
                            continue;
                        const unsigned bit = 1U << (axis * 2 + (normal[axis] > 0 ? 1U : 0U));
                        if ((face_mask & bit) || point[axis] != (normal[axis] > 0 ? maximum[axis] : minimum[axis]))
                            throw std::runtime_error("Replacement tread plane differs from its exact box bounds");
                        face_mask |= bit;
                    }
                    if (face.material == "e2u1/mine04_1")
                        continue;
                    const bool top = std::all_of(face.plane_points.begin(), face.plane_points.end(),
                                                 [&](const Vec3 &point) { return point.z == high.z; });
                    const std::string_view expected = step == 7 ? "e1u1/grate1_5" : "e1u1/stairs1_1";
                    if (!top || face.material != expected)
                        throw std::runtime_error("Unexpected intervention material; comparison must be reviewed");
                    face.material = "e2u1/mine04_1";
                    ++changed_faces;
                }
                if (face_mask != 63)
                    throw std::runtime_error("Replacement tread lacks an exact bounding face");
            }
        }
        if (found_mask != 255 || changed_faces != 8)
            throw std::runtime_error("Did not normalize exactly the eight replacement tread materials");
        MapWriteOptions options;
        options.validate_document = true;
        options.validation_options.profile = MapValidationProfile::generated_document;
        const std::string normalized = MapDocumentWriter{}.write(document, options);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << normalized;
        if (!output)
            throw std::runtime_error("Could not write validated variant material normalization");
    }

    unsigned ramp_calls_{};
    unsigned intercepted_{};
};

} // namespace qmg::talus_stair_variant

#define recipes talus_stair_variant
#include "talus.cpp"
#undef recipes
