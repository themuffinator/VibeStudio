#pragma once

#include "qmg/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace qmg {

using MapNodeId = std::uint64_t;
inline constexpr std::int32_t kMapDetailContentsBit = 0x08000000;

enum class MapDialect {
    quake1_valve220,
    quake2_valve220,
    quake3_legacy,
    quake3_brush_primitives,
};

struct Valve220Axis {
    Vec3 axis;
    double shift{};
};

struct Valve220Projection {
    Valve220Axis s_axis;
    Valve220Axis t_axis;
    double rotation{};
    double s_scale{1.0};
    double t_scale{1.0};
};

struct Quake3LegacyProjection {
    double s_shift{};
    double t_shift{};
    double rotation{};
    double s_scale{0.5};
    double t_scale{0.5};
};

struct Quake3BrushPrimitiveProjection {
    std::array<std::array<double, 3>, 2> matrix{{{{1.0, 0.0, 0.0}}, {{0.0, 1.0, 0.0}}}};
};

using MapTextureProjection = std::variant<Valve220Projection, Quake3LegacyProjection, Quake3BrushPrimitiveProjection>;

struct Quake2FaceMetadata {
    std::int32_t contents{};
    std::int32_t surface_flags{};
    std::int32_t value{};
};

struct Quake3FaceMetadata {
    std::int32_t contents{};
    std::int32_t surface_flags{};
    std::int32_t value{};
};

struct MapEntityProperty {
    MapNodeId id{};
    std::string key;
    std::string value;
};

struct MapBrushFace {
    MapNodeId id{};
    std::array<Vec3, 3> plane_points;
    std::string material;
    MapTextureProjection projection{Valve220Projection{}};
    std::optional<Quake2FaceMetadata> quake2_metadata;
    std::optional<Quake3FaceMetadata> quake3_metadata;
};

struct MapBrush {
    MapNodeId id{};
    std::vector<MapBrushFace> faces;
};

struct MapPatchControlPoint {
    MapNodeId id{};
    Vec3 position;
    double s{};
    double t{};
};

struct MapPatch {
    MapNodeId id{};
    std::string material;
    std::uint32_t width{};
    std::uint32_t height{};
    std::array<std::int32_t, 3> reserved{};
    // Source order is width-major: control_points[column * height + row].
    std::vector<MapPatchControlPoint> control_points;

    [[nodiscard]] const MapPatchControlPoint &control(std::size_t column, std::size_t row) const;
    [[nodiscard]] MapPatchControlPoint &control(std::size_t column, std::size_t row);
};

using MapPrimitive = std::variant<MapBrush, MapPatch>;

struct MapEntity {
    MapNodeId id{};
    // Both vectors retain source order; duplicate properties are intentional.
    std::vector<MapEntityProperty> properties;
    std::vector<MapPrimitive> primitives;

    [[nodiscard]] std::optional<std::string_view> first_value(std::string_view key) const;
    [[nodiscard]] std::optional<std::string_view> last_value(std::string_view key) const;
};

class MapDocument {
  public:
    explicit MapDocument(MapDialect dialect = MapDialect::quake1_valve220);

    [[nodiscard]] MapDialect dialect() const noexcept { return dialect_; }
    void set_dialect(MapDialect dialect);

    [[nodiscard]] const std::vector<MapEntity> &entities() const noexcept { return entities_; }
    // Requesting mutable storage immediately invalidates an adopted pristine source.
    [[nodiscard]] std::vector<MapEntity> &edit_entities() noexcept;

    void assign_deterministic_node_ids();

    [[nodiscard]] bool source_is_pristine() const noexcept { return pristine_source_.has_value(); }
    [[nodiscard]] std::optional<std::string_view> pristine_source() const noexcept;
    void clear_pristine_source() noexcept;

  private:
    friend class MapParser;

    void adopt_pristine_source(std::string source);

    MapDialect dialect_;
    std::vector<MapEntity> entities_;
    std::optional<std::string> pristine_source_;
};

struct MapValidationIssue {
    std::string code;
    MapNodeId node_id{};
    std::string message;
};

enum class MapValidationProfile {
    syntax_preserving,
    generated_document,
};

struct MapValidationOptions {
    MapValidationProfile profile{MapValidationProfile::syntax_preserving};
    bool require_worldspawn{true};
    bool require_closed_convex_brushes{true};
    double plane_epsilon{1.0e-6};
    double point_epsilon{1.0e-5};
    double max_coordinate{1.0e9};
    std::size_t max_issues{256};
};

struct MapValidationResult {
    std::vector<MapValidationIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
};

// Returns equivalent point ordering whose Quake plane normal points away from
// interior_point. Throws std::invalid_argument when the inputs cannot define
// an unambiguous finite plane.
[[nodiscard]] std::array<Vec3, 3> order_face_points_outward(std::array<Vec3, 3> plane_points,
                                                            const Vec3 &interior_point, double epsilon = 1.0e-9);

[[nodiscard]] MapValidationResult validate_map_document(const MapDocument &document,
                                                        const MapValidationOptions &options = {});
[[nodiscard]] std::string to_string(MapDialect dialect);

} // namespace qmg
