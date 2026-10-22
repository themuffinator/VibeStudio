#pragma once

#include "qmg/layout.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qmg {

inline constexpr std::int64_t leak_subunits_per_world_unit = 1024;

enum class CompilerPointfileDialect : std::uint8_t {
    quake1,
    quake2,
    quake3,
};

struct LeakPoint {
    std::int64_t x{};
    std::int64_t y{};
    std::int64_t z{};

    auto operator<=>(const LeakPoint &) const = default;
};

[[nodiscard]] Vec3 leak_point_to_world(LeakPoint point) noexcept;

enum class PointfileIngestError : std::uint8_t {
    none,
    unsupported_dialect,
    invalid_limits,
    io_error,
    empty_input,
    input_too_large,
    line_too_long,
    malformed_line,
    non_finite_coordinate,
    coordinate_out_of_range,
    too_many_points,
    too_few_distinct_points,
};

struct PointfileLimits {
    std::size_t max_bytes{4U * 1024U * 1024U};
    std::size_t max_points{65536};
    std::size_t max_line_bytes{256};
    double max_absolute_coordinate{1048576.0};
};

struct LeakWitness {
    CompilerPointfileDialect dialect{CompilerPointfileDialect::quake1};
    std::vector<LeakPoint> points;
    std::size_t source_point_count{};
    std::uint64_t fingerprint{};

    bool operator==(const LeakWitness &) const = default;
};

struct PointfileIngestResult {
    PointfileIngestError error{PointfileIngestError::none};
    std::size_t line{};
    std::string message;
    std::optional<LeakWitness> witness;

    [[nodiscard]] bool ok() const noexcept { return error == PointfileIngestError::none && witness.has_value(); }
};

[[nodiscard]] PointfileIngestResult parse_compiler_pointfile(std::string_view contents,
                                                             CompilerPointfileDialect dialect,
                                                             const PointfileLimits &limits = {});
[[nodiscard]] PointfileIngestResult load_compiler_pointfile(const std::filesystem::path &path,
                                                            CompilerPointfileDialect dialect,
                                                            const PointfileLimits &limits = {});

enum class ExteriorCellState : std::uint8_t {
    interior,
    enclosed_void,
    exterior,
};

struct ExteriorCellGrid {
    // Coordinate planes are integer world coordinates. Cells are the open
    // intervals between adjacent planes, indexed with Z varying fastest.
    std::vector<std::int64_t> x_planes;
    std::vector<std::int64_t> y_planes;
    std::vector<std::int64_t> z_planes;
    std::vector<ExteriorCellState> cells;

    [[nodiscard]] std::size_t x_cells() const noexcept;
    [[nodiscard]] std::size_t y_cells() const noexcept;
    [[nodiscard]] std::size_t z_cells() const noexcept;
    [[nodiscard]] std::size_t linear_index(std::size_t x, std::size_t y, std::size_t z) const noexcept;

    bool operator==(const ExteriorCellGrid &) const = default;
};

enum class ExteriorGridError : std::uint8_t {
    none,
    invalid_limits,
    invalid_layout,
    coordinate_overflow,
    capacity_exceeded,
};

struct LeakDiagnosisLimits {
    std::size_t max_interior_volumes{4096};
    std::size_t max_witness_points{65536};
    std::size_t max_segment_volume_tests{16U * 1024U * 1024U};
    std::size_t max_axis_planes{512};
    std::size_t max_cells{2U * 1024U * 1024U};
    std::size_t max_fill_operations{32U * 1024U * 1024U};
    GridCoord exterior_margin{1};
    GridCoord seal_thickness{16};
    GridCoord seal_half_extent{64};
};

struct ExteriorGridResult {
    ExteriorGridError error{ExteriorGridError::none};
    std::string message;
    std::optional<ExteriorCellGrid> grid;
    std::uint64_t layout_fingerprint{};
    std::string layout_sha256;

    [[nodiscard]] bool ok() const noexcept { return error == ExteriorGridError::none && grid.has_value(); }
};

[[nodiscard]] ExteriorGridResult build_layout_exterior_grid(const Layout &layout,
                                                            const LeakDiagnosisLimits &limits = {});

struct ExteriorCellProof {
    std::size_t linear_index{};
    std::size_t x{};
    std::size_t y{};
    std::size_t z{};
    ExteriorCellState state{ExteriorCellState::enclosed_void};

    auto operator<=>(const ExteriorCellProof &) const = default;
};

[[nodiscard]] std::optional<ExteriorCellProof> locate_exterior_cell(const ExteriorCellGrid &grid,
                                                                    LeakPoint point) noexcept;

enum class BoundaryAxis : std::uint8_t {
    x,
    y,
    z,
};

enum class BoundaryFace : std::uint8_t {
    minimum,
    maximum,
};

enum class BoundaryOwnerKind : std::uint8_t {
    room,
    connection,
};

struct BoundaryOwner {
    BoundaryOwnerKind kind{BoundaryOwnerKind::room};
    std::optional<RoomId> room;
    std::optional<ConnectionId> connection;
    GridBounds source_bounds;

    bool operator==(const BoundaryOwner &) const = default;
};

struct LeakBoundaryCrossing {
    BoundaryOwner owner;
    BoundaryAxis axis{BoundaryAxis::x};
    BoundaryFace face{BoundaryFace::minimum};
    LeakPoint point;
    std::size_t witness_segment{};
    std::uint32_t segment_fraction_millionths{};
    bool entering_interior{};

    bool operator==(const LeakBoundaryCrossing &) const = default;
};

[[nodiscard]] bool verify_exposed_boundary_proof(const ExteriorCellGrid &grid, const LeakBoundaryCrossing &crossing,
                                                 const ExteriorCellProof &proof) noexcept;

struct SealApplicabilityMetadata {
    std::uint32_t schema_version{1};
    std::uint64_t layout_fingerprint{};
    std::uint64_t witness_fingerprint{};
    BoundaryOwner expected_owner;
    BoundaryAxis expected_axis{BoundaryAxis::x};
    BoundaryFace expected_face{BoundaryFace::minimum};
    GridBounds expected_seal_bounds;
    LeakPoint expected_crossing_point;
    std::size_t witness_segment{};
    std::uint32_t segment_fraction_millionths{};
    ExteriorCellProof exterior_proof;
    std::string layout_sha256;

    bool operator==(const SealApplicabilityMetadata &) const = default;
};

struct SealRegressionMetadata {
    double baseline_rank_score{};
    std::size_t baseline_exposed_crossings{};
    std::size_t required_max_exposed_crossings{};
    bool compiler_recheck_required{true};
    bool reject_new_validation_errors{true};

    bool operator==(const SealRegressionMetadata &) const = default;
};

struct LocalSealProposal {
    BoundaryOwner owner;
    BoundaryAxis axis{BoundaryAxis::x};
    BoundaryFace face{BoundaryFace::minimum};
    GridBounds bounds;
    MaterialRole material{MaterialRole::wall};
    BrushPurpose purpose{BrushPurpose::structural};
    BrushOwnership ownership{BrushOwnership::structural};
    SealApplicabilityMetadata applicability;
    SealRegressionMetadata regression;

    bool operator==(const LocalSealProposal &) const = default;
};

enum class LeakDiagnosisStatus : std::uint8_t {
    diagnosed,
    no_exposed_crossing,
    ambiguous_crossing,
    invalid_witness,
    invalid_limits,
    invalid_layout,
    coordinate_overflow,
    capacity_exceeded,
};

struct LeakDiagnosisResult {
    LeakDiagnosisStatus status{LeakDiagnosisStatus::invalid_witness};
    std::string message;
    std::optional<ExteriorCellGrid> exterior_grid;
    std::optional<LeakBoundaryCrossing> crossing;
    std::optional<ExteriorCellProof> exterior_proof;
    std::optional<LocalSealProposal> proposal;
    // Retained so a downstream acceptance policy can independently rerun the
    // bounded diagnosis instead of trusting a mutable result payload.
    std::optional<LeakWitness> witness;
    std::size_t exposed_crossing_count{};
    std::size_t ambiguous_crossing_count{};
    std::string layout_sha256;

    [[nodiscard]] bool ok() const noexcept {
        return status == LeakDiagnosisStatus::diagnosed && crossing.has_value() && exterior_proof.has_value() &&
               proposal.has_value();
    }

    bool operator==(const LeakDiagnosisResult &) const = default;
};

[[nodiscard]] LeakDiagnosisResult diagnose_layout_leak(const Layout &layout, const LeakWitness &witness,
                                                       const LeakDiagnosisLimits &limits = {});
// This proves only that proposal metadata still matches the exact Layout and
// local exterior-cell proof. It does not prove that a lowered/compiled map is
// sealed; acceptance requires a separate compiler recheck.
[[nodiscard]] bool local_seal_proposal_preconditions_hold(const Layout &layout, const LocalSealProposal &proposal,
                                                          const LeakDiagnosisLimits &limits = {});

} // namespace qmg
