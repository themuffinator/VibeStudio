#pragma once

#include "qmg/layout.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace qmg {

struct ConstructionKey {
    std::uint64_t value{};

    [[nodiscard]] explicit constexpr operator bool() const noexcept { return value != 0; }
    auto operator<=>(const ConstructionKey &) const = default;
};

enum class HorizontalDirection : std::uint8_t {
    positive_x,
    negative_x,
    positive_y,
    negative_y,
};

enum class ArchAxis : std::uint8_t {
    along_x,
    along_y,
};

enum class CurveQuadrant : std::uint8_t {
    positive_x_positive_y,
    negative_x_positive_y,
    negative_x_negative_y,
    positive_x_negative_y,
};

struct StairRunSpec {
    ConstructionKey key;
    // The lower tread begins at start. Longitudinal extent follows ascent;
    // transverse width always extends in the positive perpendicular axis.
    GridVec3 start;
    HorizontalDirection ascent{HorizontalDirection::positive_x};
    GridCoord width{128};
    GridCoord tread_depth{32};
    GridCoord riser_height{16};
    std::uint32_t step_count{4};
    BrushOwnership ownership{BrushOwnership::detail};
};

struct RampSpec {
    ConstructionKey key;
    // start is the low end at the common bottom plane. The ramp has a
    // nonzero low-end thickness, then rises along direction.
    GridVec3 start;
    HorizontalDirection direction{HorizontalDirection::positive_x};
    GridCoord width{128};
    GridCoord run{128};
    GridCoord low_height{8};
    GridCoord rise{64};
    BrushOwnership ownership{BrushOwnership::detail};
};

struct ArchSpec {
    ConstructionKey key;
    // For along_x, origin.x is the extrusion minimum, origin.y is the
    // opening center, and origin.z is the spring line. along_y swaps x/y.
    GridVec3 origin;
    ArchAxis axis{ArchAxis::along_x};
    GridCoord depth{32};
    GridCoord inner_radius{96};
    GridCoord ring_thickness{24};
    std::uint32_t segment_count{8};
    BrushOwnership ownership{BrushOwnership::detail};
};

struct CurvedWallSpec {
    ConstructionKey key;
    // center.z is the bottom plane. The selected quadrant is swept from its
    // signed x-axis endpoint toward its signed y-axis endpoint.
    GridVec3 center;
    CurveQuadrant quadrant{CurveQuadrant::positive_x_positive_y};
    GridCoord inner_radius{128};
    GridCoord thickness{24};
    GridCoord height{192};
    std::uint32_t segment_count{4};
    BrushOwnership ownership{BrushOwnership::detail};
    // A patch veneer is an optional, format-neutral extra. With a veneer,
    // collision brushes use the hidden material role to avoid coplanar draw.
    bool emit_patch_veneer{};
};

struct LiquidSpec {
    ConstructionKey key;
    GridBounds bounds;
    BrushContents contents{BrushContents::water};
    // Liquids participate in the sealed world/contents volume and therefore
    // must remain structural across every supported lowerer.
    BrushOwnership ownership{BrushOwnership::structural};
};

struct DoorAssemblySpec {
    ConstructionKey key;
    GridBounds leaf_bounds;
    GridVec3 travel_delta;
    GridCoord speed_units_per_second{100};
    std::uint32_t wait_milliseconds{3000};
    std::optional<GridBounds> trigger_bounds;
};

struct LiftAssemblySpec {
    ConstructionKey key;
    GridBounds platform_bounds;
    // Either endpoint may be supplied. Q2 lowering authors the native brush
    // at the higher station because func_plat always subtracts its height.
    GridVec3 travel_delta{0, 0, 128};
    GridCoord speed_units_per_second{100};
    std::uint32_t wait_milliseconds{3000};
    std::optional<GridBounds> trigger_bounds;
};

struct TeleporterAssemblySpec {
    ConstructionKey key;
    // Q1/Q3 use this brush trigger. In Q2 its center locates the stock disc;
    // construction replaces the typed volume with the fixed native touch
    // bounds (center + [-8,-8,8]..[8,8,24]). Put the center at floor + 24.
    GridBounds trigger_bounds;
    // Q2 places the solid destination disc here and arrives ten units above
    // this origin. Use floor + 24, with clearance for the raised arrival hull.
    GridVec3 destination_origin;
    std::optional<std::int16_t> destination_angle_degrees;
};

using ConstructionRequest = std::variant<StairRunSpec, RampSpec, ArchSpec, CurvedWallSpec, LiquidSpec, DoorAssemblySpec,
                                         LiftAssemblySpec, TeleporterAssemblySpec>;

struct LayoutCurveControlPoint {
    GridVec3 position;
    // Fixed-point texture coordinates in thousandths; this avoids native map
    // projection syntax while retaining deterministic patch parameterization.
    std::int32_t u_milli{};
    std::int32_t v_milli{};

    auto operator<=>(const LayoutCurveControlPoint &) const = default;
};

struct LayoutCurve {
    // Curves share the SolidId namespace so manifests can identify one stable
    // semantic source even though Layout itself remains brush-only.
    SolidId source_id;
    MaterialRole material{MaterialRole::curve};
    BrushOwnership ownership{BrushOwnership::detail};
    std::uint32_t width{};
    std::uint32_t height{};
    // Width-major: control_points[column * height + row].
    std::vector<LayoutCurveControlPoint> control_points;
    // These solids are complete collision/backing geometry and are applied to
    // Layout; a Q3 lowerer may additionally emit the patch veneer.
    std::vector<LayoutSolid> collision_backing_solids;

    [[nodiscard]] const LayoutCurveControlPoint &control(std::size_t column, std::size_t row) const;
    bool operator==(const LayoutCurve &) const = default;
};

struct MoverDirective {
    PlacementId mover;
    GridVec3 travel_delta;
    GridCoord speed_units_per_second{};
    std::uint32_t wait_milliseconds{};

    auto operator<=>(const MoverDirective &) const = default;
};

struct ConstructionLimits {
    GridCoord max_coordinate{32768};
    std::size_t max_requests{1024};
    std::size_t max_generated_solids{8192};
    std::size_t max_segments_per_feature{128};
    std::size_t max_patch_control_points{4096};
};

class ConstructionError final : public std::runtime_error {
  public:
    ConstructionError(std::string code, std::string message);

    [[nodiscard]] const std::string &code() const noexcept { return code_; }

  private:
    std::string code_;
};

struct ConstructionResult {
    // Non-curve solids. Curve collision/backing solids live with their curve
    // and are included by all_solids() and apply_to().
    std::vector<LayoutSolid> solids;
    std::vector<Placement> placements;
    std::vector<LayoutCurve> curves;
    std::vector<MoverDirective> movers;
    SolidId expected_first_solid_id;
    PlacementId expected_first_placement_id;

    [[nodiscard]] std::vector<LayoutSolid> all_solids() const;
    // Checked, deterministic merge. The target may have been reordered, but
    // its identifier maxima must still match the build base. Curves remain in
    // this result because Layout deliberately has no syntax-specific surface.
    void apply_to(Layout &layout) const;

    bool operator==(const ConstructionResult &) const = default;
};

class ConstructionBuilder {
  public:
    [[nodiscard]] ConstructionResult build(const Layout &base, std::vector<ConstructionRequest> requests,
                                           const ConstructionLimits &limits = {}) const;
};

} // namespace qmg
