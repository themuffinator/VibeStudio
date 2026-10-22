#include "qmg/leak.hpp"

#include "qmg/layout_fingerprint.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <system_error>
#include <tuple>
#include <utility>

namespace qmg {
namespace {

constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;
constexpr long double geometry_epsilon = 1.0e-9L;

void hash_byte(std::uint64_t &hash, const std::uint8_t value) noexcept {
    hash ^= value;
    hash *= fnv_prime;
}

void hash_u64(std::uint64_t &hash, std::uint64_t value) noexcept {
    for (unsigned int index = 0; index < 8U; ++index) {
        hash_byte(hash, static_cast<std::uint8_t>(value & 0xffU));
        value >>= 8U;
    }
}

void hash_i64(std::uint64_t &hash, const std::int64_t value) noexcept {
    hash_u64(hash, static_cast<std::uint64_t>(value));
}

[[nodiscard]] std::uint64_t witness_fingerprint(const CompilerPointfileDialect dialect,
                                                const std::vector<LeakPoint> &points) noexcept {
    std::uint64_t hash = fnv_offset;
    hash_byte(hash, static_cast<std::uint8_t>(dialect));
    hash_u64(hash, static_cast<std::uint64_t>(points.size()));
    for (const LeakPoint point : points) {
        hash_i64(hash, point.x);
        hash_i64(hash, point.y);
        hash_i64(hash, point.z);
    }
    return hash;
}

[[nodiscard]] PointfileIngestResult pointfile_failure(const PointfileIngestError error, const std::size_t line,
                                                      std::string message) {
    return {error, line, std::move(message), std::nullopt};
}

[[nodiscard]] bool pointfile_limits_valid(const PointfileLimits &limits) noexcept {
    if (limits.max_bytes == 0 || limits.max_points < 2 || limits.max_line_bytes == 0 ||
        !std::isfinite(limits.max_absolute_coordinate) || !(limits.max_absolute_coordinate > 0.0)) {
        return false;
    }
    if (static_cast<std::uintmax_t>(limits.max_bytes) >
        static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        return false;
    }
    const long double scaled = static_cast<long double>(limits.max_absolute_coordinate) * leak_subunits_per_world_unit;
    return scaled <= static_cast<long double>(std::numeric_limits<std::int64_t>::max());
}

[[nodiscard]] bool pointfile_dialect_valid(const CompilerPointfileDialect dialect) noexcept {
    switch (dialect) {
    case CompilerPointfileDialect::quake1:
    case CompilerPointfileDialect::quake2:
    case CompilerPointfileDialect::quake3:
        return true;
    }
    return false;
}

[[nodiscard]] bool pointfile_dialect_matches_game(const CompilerPointfileDialect dialect, const Game game) noexcept {
    return (dialect == CompilerPointfileDialect::quake1 && game == Game::quake1) ||
           (dialect == CompilerPointfileDialect::quake2 && game == Game::quake2) ||
           (dialect == CompilerPointfileDialect::quake3 && game == Game::quake3);
}

[[nodiscard]] bool horizontal_space(const char value) noexcept { return value == ' ' || value == '\t'; }

[[nodiscard]] PointfileIngestError parse_coordinate(const std::string_view token, const PointfileLimits &limits,
                                                    std::int64_t &result) noexcept {
    double value{};
    const char *const begin = token.data();
    const char *const end = token.data() + token.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, value, std::chars_format::general);
    if (parsed.ec == std::errc::result_out_of_range)
        return PointfileIngestError::coordinate_out_of_range;
    if (parsed.ec != std::errc{} || parsed.ptr != end)
        return PointfileIngestError::malformed_line;
    if (!std::isfinite(value))
        return PointfileIngestError::non_finite_coordinate;
    if (std::abs(value) > limits.max_absolute_coordinate)
        return PointfileIngestError::coordinate_out_of_range;
    const long double scaled = static_cast<long double>(value) * leak_subunits_per_world_unit;
    if (scaled < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
        scaled > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return PointfileIngestError::coordinate_out_of_range;
    }
    result = static_cast<std::int64_t>(std::round(scaled));
    if (result == 0)
        result = 0;
    return PointfileIngestError::none;
}

struct InteriorVolume {
    BoundaryOwner owner;
};

[[nodiscard]] std::uint32_t owner_value(const BoundaryOwner &owner) noexcept {
    if (owner.kind == BoundaryOwnerKind::room && owner.room)
        return owner.room->value;
    if (owner.kind == BoundaryOwnerKind::connection && owner.connection)
        return owner.connection->value;
    return 0;
}

[[nodiscard]] auto volume_key(const InteriorVolume &volume) noexcept {
    const GridBounds &bounds = volume.owner.source_bounds;
    return std::tuple{static_cast<std::uint8_t>(volume.owner.kind),
                      owner_value(volume.owner),
                      bounds.min.x,
                      bounds.min.y,
                      bounds.min.z,
                      bounds.max.x,
                      bounds.max.y,
                      bounds.max.z};
}

struct PreparedLayout {
    ExteriorGridError error{ExteriorGridError::none};
    std::string message;
    std::vector<InteriorVolume> volumes;
    std::uint64_t fingerprint{};
    std::string layout_sha256;
};

[[nodiscard]] bool diagnosis_limits_valid(const LeakDiagnosisLimits &limits) noexcept {
    return limits.max_interior_volumes > 0 && limits.max_witness_points >= 2 && limits.max_segment_volume_tests > 0 &&
           limits.max_axis_planes >= 3 && limits.max_cells > 0 && limits.max_fill_operations > 0 &&
           limits.exterior_margin > 0 && limits.seal_thickness > 0 && limits.seal_half_extent > 0;
}

[[nodiscard]] PreparedLayout prepare_layout(const Layout &layout, const LeakDiagnosisLimits &limits) {
    if (!diagnosis_limits_valid(limits))
        return {ExteriorGridError::invalid_limits, "leak diagnosis limits are invalid", {}, 0, {}};
    if (layout.rooms.empty())
        return {ExteriorGridError::invalid_layout, "layout has no rooms", {}, 0, {}};
    if (!std::isfinite(layout.rank_score))
        return {ExteriorGridError::invalid_layout, "layout rank score is non-finite", {}, 0, {}};

    std::set<RoomId> room_ids;
    std::set<ConnectionId> connection_ids;
    std::vector<InteriorVolume> volumes;
    std::vector<std::array<std::int64_t, 5>> connection_records;
    volumes.reserve(std::min(limits.max_interior_volumes, layout.rooms.size()));
    connection_records.reserve(std::min(limits.max_interior_volumes, layout.connections.size()));
    for (const Room &room : layout.rooms) {
        if (!room.id || !room.bounds.valid())
            return {ExteriorGridError::invalid_layout, "layout contains an invalid room", {}, 0, {}};
        if (!room_ids.insert(room.id).second)
            return {ExteriorGridError::invalid_layout, "layout contains duplicate room IDs", {}, 0, {}};
        if (volumes.size() == limits.max_interior_volumes)
            return {ExteriorGridError::capacity_exceeded, "interior volume cap was exceeded", {}, 0, {}};
        volumes.push_back({BoundaryOwner{BoundaryOwnerKind::room, room.id, std::nullopt, room.bounds}});
    }
    for (const Connection &connection : layout.connections) {
        if (!connection.id || !connection_ids.insert(connection.id).second)
            return {
                ExteriorGridError::invalid_layout, "layout contains an invalid or duplicate connection ID", {}, 0, {}};
        if (!room_ids.contains(connection.from_room) || !room_ids.contains(connection.to_room) ||
            connection.from_room == connection.to_room) {
            return {ExteriorGridError::invalid_layout, "connection references invalid rooms", {}, 0, {}};
        }
        if (connection.width <= 0 || connection.height <= 0 || connection.passage_volumes.empty())
            return {ExteriorGridError::invalid_layout, "connection has no valid passage dimensions", {}, 0, {}};
        const std::uint32_t first_room = std::min(connection.from_room.value, connection.to_room.value);
        const std::uint32_t second_room = std::max(connection.from_room.value, connection.to_room.value);
        connection_records.push_back(
            {connection.id.value, first_room, second_room, connection.width, connection.height});
        for (const GridBounds &passage : connection.passage_volumes) {
            if (!passage.valid())
                return {ExteriorGridError::invalid_layout, "connection contains an invalid passage volume", {}, 0, {}};
            if (volumes.size() == limits.max_interior_volumes)
                return {ExteriorGridError::capacity_exceeded, "interior volume cap was exceeded", {}, 0, {}};
            volumes.push_back({BoundaryOwner{BoundaryOwnerKind::connection, std::nullopt, connection.id, passage}});
        }
    }

    std::sort(volumes.begin(), volumes.end(), [](const InteriorVolume &left, const InteriorVolume &right) {
        return volume_key(left) < volume_key(right);
    });
    volumes.erase(std::unique(volumes.begin(), volumes.end(),
                              [](const InteriorVolume &left, const InteriorVolume &right) {
                                  return volume_key(left) == volume_key(right);
                              }),
                  volumes.end());
    std::sort(connection_records.begin(), connection_records.end());

    std::uint64_t fingerprint = fnv_offset;
    hash_u64(fingerprint, static_cast<std::uint64_t>(volumes.size()));
    for (const InteriorVolume &volume : volumes) {
        hash_byte(fingerprint, static_cast<std::uint8_t>(volume.owner.kind));
        hash_u64(fingerprint, owner_value(volume.owner));
        const GridBounds &bounds = volume.owner.source_bounds;
        hash_i64(fingerprint, bounds.min.x);
        hash_i64(fingerprint, bounds.min.y);
        hash_i64(fingerprint, bounds.min.z);
        hash_i64(fingerprint, bounds.max.x);
        hash_i64(fingerprint, bounds.max.y);
        hash_i64(fingerprint, bounds.max.z);
    }
    hash_u64(fingerprint, static_cast<std::uint64_t>(connection_records.size()));
    for (const auto &record : connection_records) {
        for (const std::int64_t value : record)
            hash_i64(fingerprint, value);
    }
    return {ExteriorGridError::none, {}, std::move(volumes), fingerprint, canonical_layout_sha256(layout)};
}

[[nodiscard]] bool checked_product(const std::size_t left, const std::size_t right, const std::size_t cap,
                                   std::size_t &result) noexcept {
    if (left != 0 && right > cap / left)
        return false;
    result = left * right;
    return result <= cap;
}

void sort_unique(std::vector<std::int64_t> &values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

[[nodiscard]] std::size_t exact_plane_index(const std::vector<std::int64_t> &planes,
                                            const std::int64_t value) noexcept {
    const auto found = std::lower_bound(planes.begin(), planes.end(), value);
    if (found == planes.end() || *found != value)
        return planes.size();
    return static_cast<std::size_t>(found - planes.begin());
}

[[nodiscard]] ExteriorGridResult build_grid_from_prepared(const PreparedLayout &prepared,
                                                          const LeakDiagnosisLimits &limits) {
    ExteriorCellGrid grid;
    grid.x_planes.reserve(prepared.volumes.size() * 2U + 2U);
    grid.y_planes.reserve(prepared.volumes.size() * 2U + 2U);
    grid.z_planes.reserve(prepared.volumes.size() * 2U + 2U);
    for (const InteriorVolume &volume : prepared.volumes) {
        const GridBounds &bounds = volume.owner.source_bounds;
        grid.x_planes.push_back(bounds.min.x);
        grid.x_planes.push_back(bounds.max.x);
        grid.y_planes.push_back(bounds.min.y);
        grid.y_planes.push_back(bounds.max.y);
        grid.z_planes.push_back(bounds.min.z);
        grid.z_planes.push_back(bounds.max.z);
    }
    const auto add_exterior_planes = [&limits](std::vector<std::int64_t> &planes) {
        const auto [minimum, maximum] = std::minmax_element(planes.begin(), planes.end());
        planes.push_back(*minimum - static_cast<std::int64_t>(limits.exterior_margin));
        planes.push_back(*maximum + static_cast<std::int64_t>(limits.exterior_margin));
        sort_unique(planes);
    };
    add_exterior_planes(grid.x_planes);
    add_exterior_planes(grid.y_planes);
    add_exterior_planes(grid.z_planes);
    if (grid.x_planes.size() > limits.max_axis_planes || grid.y_planes.size() > limits.max_axis_planes ||
        grid.z_planes.size() > limits.max_axis_planes) {
        return {ExteriorGridError::capacity_exceeded, "exterior grid axis-plane cap was exceeded", std::nullopt,
                prepared.fingerprint, prepared.layout_sha256};
    }

    const std::size_t x_cells = grid.x_cells();
    const std::size_t y_cells = grid.y_cells();
    const std::size_t z_cells = grid.z_cells();
    std::size_t xy_cells{};
    std::size_t cell_count{};
    if (!checked_product(x_cells, y_cells, limits.max_cells, xy_cells) ||
        !checked_product(xy_cells, z_cells, limits.max_cells, cell_count)) {
        return {ExteriorGridError::capacity_exceeded, "exterior grid cell cap was exceeded", std::nullopt,
                prepared.fingerprint, prepared.layout_sha256};
    }
    grid.cells.assign(cell_count, ExteriorCellState::enclosed_void);

    std::size_t fill_operations{};
    for (const InteriorVolume &volume : prepared.volumes) {
        const GridBounds &bounds = volume.owner.source_bounds;
        const std::size_t min_x = exact_plane_index(grid.x_planes, bounds.min.x);
        const std::size_t max_x = exact_plane_index(grid.x_planes, bounds.max.x);
        const std::size_t min_y = exact_plane_index(grid.y_planes, bounds.min.y);
        const std::size_t max_y = exact_plane_index(grid.y_planes, bounds.max.y);
        const std::size_t min_z = exact_plane_index(grid.z_planes, bounds.min.z);
        const std::size_t max_z = exact_plane_index(grid.z_planes, bounds.max.z);
        std::size_t xy_fill{};
        std::size_t volume_fill{};
        if (!checked_product(max_x - min_x, max_y - min_y, limits.max_fill_operations, xy_fill) ||
            !checked_product(xy_fill, max_z - min_z, limits.max_fill_operations, volume_fill) ||
            volume_fill > limits.max_fill_operations - fill_operations) {
            return {ExteriorGridError::capacity_exceeded, "exterior grid fill-operation cap was exceeded", std::nullopt,
                    prepared.fingerprint, prepared.layout_sha256};
        }
        fill_operations += volume_fill;
        for (std::size_t x = min_x; x < max_x; ++x) {
            for (std::size_t y = min_y; y < max_y; ++y) {
                for (std::size_t z = min_z; z < max_z; ++z)
                    grid.cells[grid.linear_index(x, y, z)] = ExteriorCellState::interior;
            }
        }
    }

    std::vector<std::size_t> queue;
    queue.reserve(cell_count);
    for (std::size_t x = 0; x < x_cells; ++x) {
        for (std::size_t y = 0; y < y_cells; ++y) {
            for (std::size_t z = 0; z < z_cells; ++z) {
                if (x != 0 && y != 0 && z != 0 && x + 1 != x_cells && y + 1 != y_cells && z + 1 != z_cells)
                    continue;
                const std::size_t index = grid.linear_index(x, y, z);
                if (grid.cells[index] == ExteriorCellState::enclosed_void) {
                    grid.cells[index] = ExteriorCellState::exterior;
                    queue.push_back(index);
                }
            }
        }
    }

    for (std::size_t head = 0; head < queue.size(); ++head) {
        const std::size_t index = queue[head];
        const std::size_t z = index % z_cells;
        const std::size_t xy = index / z_cells;
        const std::size_t y = xy % y_cells;
        const std::size_t x = xy / y_cells;
        const auto visit = [&grid, &queue](const std::size_t neighbor) {
            if (grid.cells[neighbor] == ExteriorCellState::enclosed_void) {
                grid.cells[neighbor] = ExteriorCellState::exterior;
                queue.push_back(neighbor);
            }
        };
        if (x > 0)
            visit(grid.linear_index(x - 1, y, z));
        if (x + 1 < x_cells)
            visit(grid.linear_index(x + 1, y, z));
        if (y > 0)
            visit(grid.linear_index(x, y - 1, z));
        if (y + 1 < y_cells)
            visit(grid.linear_index(x, y + 1, z));
        if (z > 0)
            visit(grid.linear_index(x, y, z - 1));
        if (z + 1 < z_cells)
            visit(grid.linear_index(x, y, z + 1));
    }
    return {ExteriorGridError::none, {}, std::move(grid), prepared.fingerprint, prepared.layout_sha256};
}

struct WorldPoint {
    long double x{};
    long double y{};
    long double z{};
};

[[nodiscard]] WorldPoint world_point(const LeakPoint point) noexcept {
    constexpr long double scale = static_cast<long double>(leak_subunits_per_world_unit);
    return {static_cast<long double>(point.x) / scale, static_cast<long double>(point.y) / scale,
            static_cast<long double>(point.z) / scale};
}

[[nodiscard]] WorldPoint interpolate(const WorldPoint &from, const WorldPoint &to,
                                     const long double fraction) noexcept {
    return {from.x + (to.x - from.x) * fraction, from.y + (to.y - from.y) * fraction,
            from.z + (to.z - from.z) * fraction};
}

[[nodiscard]] long double component(const WorldPoint &point, const BoundaryAxis axis) noexcept {
    if (axis == BoundaryAxis::x)
        return point.x;
    if (axis == BoundaryAxis::y)
        return point.y;
    return point.z;
}

[[nodiscard]] GridCoord minimum_component(const GridBounds &bounds, const BoundaryAxis axis) noexcept {
    if (axis == BoundaryAxis::x)
        return bounds.min.x;
    if (axis == BoundaryAxis::y)
        return bounds.min.y;
    return bounds.min.z;
}

[[nodiscard]] GridCoord maximum_component(const GridBounds &bounds, const BoundaryAxis axis) noexcept {
    if (axis == BoundaryAxis::x)
        return bounds.max.x;
    if (axis == BoundaryAxis::y)
        return bounds.max.y;
    return bounds.max.z;
}

[[nodiscard]] bool point_inside_volume(const WorldPoint &point, const GridBounds &bounds) noexcept {
    return point.x > static_cast<long double>(bounds.min.x) + geometry_epsilon &&
           point.x < static_cast<long double>(bounds.max.x) - geometry_epsilon &&
           point.y > static_cast<long double>(bounds.min.y) + geometry_epsilon &&
           point.y < static_cast<long double>(bounds.max.y) - geometry_epsilon &&
           point.z > static_cast<long double>(bounds.min.z) + geometry_epsilon &&
           point.z < static_cast<long double>(bounds.max.z) - geometry_epsilon;
}

[[nodiscard]] bool point_inside_union(const WorldPoint &point, const std::vector<InteriorVolume> &volumes) noexcept {
    return std::any_of(volumes.begin(), volumes.end(), [&point](const InteriorVolume &volume) {
        return point_inside_volume(point, volume.owner.source_bounds);
    });
}

[[nodiscard]] bool almost_equal(const long double left, const long double right) noexcept {
    return std::abs(left - right) <= geometry_epsilon;
}

[[nodiscard]] std::optional<std::size_t> open_interval(const std::vector<std::int64_t> &planes,
                                                       const long double coordinate) noexcept {
    if (planes.size() < 2 || coordinate <= static_cast<long double>(planes.front()) ||
        coordinate >= static_cast<long double>(planes.back())) {
        return std::nullopt;
    }
    const auto upper = std::upper_bound(
        planes.begin(), planes.end(), coordinate,
        [](const long double value, const std::int64_t plane) { return value < static_cast<long double>(plane); });
    if (upper == planes.begin() || upper == planes.end())
        return std::nullopt;
    const std::size_t index = static_cast<std::size_t>(upper - planes.begin() - 1);
    if (almost_equal(coordinate, static_cast<long double>(planes[index])) ||
        almost_equal(coordinate, static_cast<long double>(planes[index + 1]))) {
        return std::nullopt;
    }
    return index;
}

[[nodiscard]] std::optional<ExteriorCellProof> locate_world_cell(const ExteriorCellGrid &grid,
                                                                 const WorldPoint &point) noexcept {
    const std::optional<std::size_t> x = open_interval(grid.x_planes, point.x);
    const std::optional<std::size_t> y = open_interval(grid.y_planes, point.y);
    const std::optional<std::size_t> z = open_interval(grid.z_planes, point.z);
    if (!x || !y || !z)
        return std::nullopt;
    const std::size_t index = grid.linear_index(*x, *y, *z);
    if (index >= grid.cells.size())
        return std::nullopt;
    return ExteriorCellProof{index, *x, *y, *z, grid.cells[index]};
}

struct IntervalChoices {
    std::array<std::size_t, 2> values{};
    std::size_t count{};
};

[[nodiscard]] IntervalChoices touching_intervals(const std::vector<std::int64_t> &planes,
                                                 const long double coordinate) noexcept {
    IntervalChoices choices;
    if (planes.size() < 2 || coordinate < planes.front() || coordinate > planes.back())
        return choices;
    const auto found = std::lower_bound(
        planes.begin(), planes.end(), coordinate,
        [](const std::int64_t plane, const long double value) { return static_cast<long double>(plane) < value; });
    if (found != planes.end() && almost_equal(static_cast<long double>(*found), coordinate)) {
        const std::size_t plane = static_cast<std::size_t>(found - planes.begin());
        if (plane > 0)
            choices.values[choices.count++] = plane - 1;
        if (plane + 1 < planes.size())
            choices.values[choices.count++] = plane;
        return choices;
    }
    const std::optional<std::size_t> interval = open_interval(planes, coordinate);
    if (interval)
        choices.values[choices.count++] = *interval;
    return choices;
}

[[nodiscard]] bool world_point_touches_state(const ExteriorCellGrid &grid, const WorldPoint &point,
                                             const ExteriorCellState state) noexcept {
    const IntervalChoices x_choices = touching_intervals(grid.x_planes, point.x);
    const IntervalChoices y_choices = touching_intervals(grid.y_planes, point.y);
    const IntervalChoices z_choices = touching_intervals(grid.z_planes, point.z);
    for (std::size_t x_index = 0; x_index < x_choices.count; ++x_index) {
        for (std::size_t y_index = 0; y_index < y_choices.count; ++y_index) {
            for (std::size_t z_index = 0; z_index < z_choices.count; ++z_index) {
                const std::size_t index =
                    grid.linear_index(x_choices.values[x_index], y_choices.values[y_index], z_choices.values[z_index]);
                if (index < grid.cells.size() && grid.cells[index] == state)
                    return true;
            }
        }
    }
    return false;
}

[[nodiscard]] std::optional<LeakPoint> quantize_world_point(const WorldPoint &point) noexcept {
    constexpr long double scale = static_cast<long double>(leak_subunits_per_world_unit);
    const std::array<long double, 3> scaled{point.x * scale, point.y * scale, point.z * scale};
    for (const long double value : scaled) {
        if (!std::isfinite(value) || value < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
            value > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
    }
    return LeakPoint{static_cast<std::int64_t>(std::round(scaled[0])), static_cast<std::int64_t>(std::round(scaled[1])),
                     static_cast<std::int64_t>(std::round(scaled[2]))};
}

[[nodiscard]] bool tangentially_inside(const WorldPoint &point, const GridBounds &bounds,
                                       const BoundaryAxis normal_axis) noexcept {
    for (const BoundaryAxis axis : {BoundaryAxis::x, BoundaryAxis::y, BoundaryAxis::z}) {
        if (axis == normal_axis)
            continue;
        const long double value = component(point, axis);
        if (!(value > static_cast<long double>(minimum_component(bounds, axis)) + geometry_epsilon &&
              value < static_cast<long double>(maximum_component(bounds, axis)) - geometry_epsilon)) {
            return false;
        }
    }
    return true;
}

struct BoundaryCandidate {
    BoundaryOwner owner;
    BoundaryAxis axis{BoundaryAxis::x};
    BoundaryFace face{BoundaryFace::minimum};
    std::optional<ExteriorCellProof> proof;
};

[[nodiscard]] bool same_candidate(const BoundaryCandidate &left, const BoundaryCandidate &right) noexcept {
    return left.owner == right.owner && left.axis == right.axis && left.face == right.face;
}

[[nodiscard]] std::optional<ExteriorCellProof> adjacent_face_cell(const ExteriorCellGrid &grid,
                                                                  const BoundaryOwner &owner, const BoundaryAxis axis,
                                                                  const BoundaryFace face,
                                                                  const WorldPoint &crossing) noexcept {
    const GridBounds &bounds = owner.source_bounds;
    const std::int64_t face_coordinate =
        face == BoundaryFace::minimum ? minimum_component(bounds, axis) : maximum_component(bounds, axis);
    const std::vector<std::int64_t> *normal_planes = &grid.x_planes;
    if (axis == BoundaryAxis::y)
        normal_planes = &grid.y_planes;
    else if (axis == BoundaryAxis::z)
        normal_planes = &grid.z_planes;
    const std::size_t plane = exact_plane_index(*normal_planes, face_coordinate);
    if (plane == normal_planes->size())
        return std::nullopt;
    std::optional<std::size_t> x = open_interval(grid.x_planes, crossing.x);
    std::optional<std::size_t> y = open_interval(grid.y_planes, crossing.y);
    std::optional<std::size_t> z = open_interval(grid.z_planes, crossing.z);
    const auto outside_index = [face, plane]() -> std::optional<std::size_t> {
        if (face == BoundaryFace::minimum) {
            if (plane == 0)
                return std::nullopt;
            return plane - 1;
        }
        return plane;
    }();
    if (!outside_index)
        return std::nullopt;
    if (axis == BoundaryAxis::x)
        x = outside_index;
    else if (axis == BoundaryAxis::y)
        y = outside_index;
    else
        z = outside_index;
    if (!x || !y || !z || *x >= grid.x_cells() || *y >= grid.y_cells() || *z >= grid.z_cells())
        return std::nullopt;
    const std::size_t index = grid.linear_index(*x, *y, *z);
    return ExteriorCellProof{index, *x, *y, *z, grid.cells[index]};
}

[[nodiscard]] std::vector<BoundaryCandidate>
boundary_candidates(const std::vector<InteriorVolume> &volumes, const ExteriorCellGrid &grid,
                    const WorldPoint &crossing, const WorldPoint &inside_sample, const WorldPoint &outside_sample) {
    std::vector<BoundaryCandidate> candidates;
    for (const InteriorVolume &volume : volumes) {
        const GridBounds &bounds = volume.owner.source_bounds;
        if (!point_inside_volume(inside_sample, bounds) || point_inside_volume(outside_sample, bounds))
            continue;
        for (const BoundaryAxis axis : {BoundaryAxis::x, BoundaryAxis::y, BoundaryAxis::z}) {
            if (!tangentially_inside(crossing, bounds, axis))
                continue;
            const long double crossing_component = component(crossing, axis);
            const long double inside_component = component(inside_sample, axis);
            const long double outside_component = component(outside_sample, axis);
            for (const BoundaryFace face : {BoundaryFace::minimum, BoundaryFace::maximum}) {
                const GridCoord face_value =
                    face == BoundaryFace::minimum ? minimum_component(bounds, axis) : maximum_component(bounds, axis);
                if (!almost_equal(crossing_component, static_cast<long double>(face_value)))
                    continue;
                const bool crosses_outward =
                    face == BoundaryFace::minimum
                        ? inside_component > static_cast<long double>(face_value) + geometry_epsilon &&
                              outside_component < static_cast<long double>(face_value) - geometry_epsilon
                        : inside_component < static_cast<long double>(face_value) - geometry_epsilon &&
                              outside_component > static_cast<long double>(face_value) + geometry_epsilon;
                if (!crosses_outward)
                    continue;
                BoundaryCandidate candidate{volume.owner, axis, face,
                                            adjacent_face_cell(grid, volume.owner, axis, face, crossing)};
                if (std::none_of(candidates.begin(), candidates.end(), [&candidate](const BoundaryCandidate &existing) {
                        return same_candidate(existing, candidate);
                    })) {
                    candidates.push_back(std::move(candidate));
                }
            }
        }
    }
    return candidates;
}

[[nodiscard]] GridCoord checked_component(const std::int64_t value) {
    if (value < std::numeric_limits<GridCoord>::min() || value > std::numeric_limits<GridCoord>::max())
        throw std::overflow_error("local seal coordinate exceeds the layout grid range");
    return static_cast<GridCoord>(value);
}

[[nodiscard]] std::int64_t floor_subunits(const std::int64_t value) noexcept {
    const std::int64_t quotient = value / leak_subunits_per_world_unit;
    const std::int64_t remainder = value % leak_subunits_per_world_unit;
    return remainder < 0 ? quotient - 1 : quotient;
}

[[nodiscard]] std::int64_t ceil_subunits(const std::int64_t value) noexcept {
    const std::int64_t quotient = value / leak_subunits_per_world_unit;
    const std::int64_t remainder = value % leak_subunits_per_world_unit;
    return remainder > 0 ? quotient + 1 : quotient;
}

[[nodiscard]] std::optional<GridBounds> local_seal_bounds(const LeakBoundaryCrossing &crossing,
                                                          const LeakDiagnosisLimits &limits) noexcept {
    try {
        const GridBounds &source = crossing.owner.source_bounds;
        GridBounds seal = source;
        const std::array<std::int64_t, 3> coordinates{crossing.point.x, crossing.point.y, crossing.point.z};
        for (std::size_t index = 0; index < 3; ++index) {
            const BoundaryAxis axis = static_cast<BoundaryAxis>(index);
            GridCoord *minimum = index == 0 ? &seal.min.x : (index == 1 ? &seal.min.y : &seal.min.z);
            GridCoord *maximum = index == 0 ? &seal.max.x : (index == 1 ? &seal.max.y : &seal.max.z);
            const GridCoord source_minimum = minimum_component(source, axis);
            const GridCoord source_maximum = maximum_component(source, axis);
            if (axis == crossing.axis) {
                if (crossing.face == BoundaryFace::minimum) {
                    *minimum = checked_component(static_cast<std::int64_t>(source_minimum) - limits.seal_thickness);
                    *maximum = source_minimum;
                } else {
                    *minimum = source_maximum;
                    *maximum = checked_component(static_cast<std::int64_t>(source_maximum) + limits.seal_thickness);
                }
                continue;
            }
            const std::int64_t rounded_minimum = floor_subunits(coordinates[index]);
            const std::int64_t rounded_maximum = ceil_subunits(coordinates[index]);
            if (rounded_minimum <
                    std::numeric_limits<std::int64_t>::min() + static_cast<std::int64_t>(limits.seal_half_extent) ||
                rounded_maximum >
                    std::numeric_limits<std::int64_t>::max() - static_cast<std::int64_t>(limits.seal_half_extent)) {
                return std::nullopt;
            }
            const std::int64_t proposed_minimum = rounded_minimum - static_cast<std::int64_t>(limits.seal_half_extent);
            const std::int64_t proposed_maximum = rounded_maximum + static_cast<std::int64_t>(limits.seal_half_extent);
            *minimum = checked_component(std::max<std::int64_t>(source_minimum, proposed_minimum));
            *maximum = checked_component(std::min<std::int64_t>(source_maximum, proposed_maximum));
        }
        if (!seal.valid())
            return std::nullopt;
        return seal;
    } catch (const std::overflow_error &) {
        return std::nullopt;
    }
}

[[nodiscard]] MaterialRole local_seal_material(const BoundaryAxis axis, const BoundaryFace face) noexcept {
    if (axis != BoundaryAxis::z)
        return MaterialRole::wall;
    return face == BoundaryFace::minimum ? MaterialRole::floor : MaterialRole::ceiling;
}

[[nodiscard]] LeakDiagnosisStatus diagnosis_status(const ExteriorGridError error) noexcept {
    if (error == ExteriorGridError::invalid_limits)
        return LeakDiagnosisStatus::invalid_limits;
    if (error == ExteriorGridError::capacity_exceeded)
        return LeakDiagnosisStatus::capacity_exceeded;
    if (error == ExteriorGridError::coordinate_overflow)
        return LeakDiagnosisStatus::coordinate_overflow;
    return LeakDiagnosisStatus::invalid_layout;
}

} // namespace

Vec3 leak_point_to_world(const LeakPoint point) noexcept {
    const double scale = static_cast<double>(leak_subunits_per_world_unit);
    return {static_cast<double>(point.x) / scale, static_cast<double>(point.y) / scale,
            static_cast<double>(point.z) / scale};
}

PointfileIngestResult parse_compiler_pointfile(const std::string_view contents, const CompilerPointfileDialect dialect,
                                               const PointfileLimits &limits) {
    if (!pointfile_dialect_valid(dialect))
        return pointfile_failure(PointfileIngestError::unsupported_dialect, 0, "pointfile dialect is unsupported");
    if (!pointfile_limits_valid(limits))
        return pointfile_failure(PointfileIngestError::invalid_limits, 0, "pointfile limits are invalid");
    if (contents.empty())
        return pointfile_failure(PointfileIngestError::empty_input, 0, "pointfile is empty");
    if (contents.size() > limits.max_bytes)
        return pointfile_failure(PointfileIngestError::input_too_large, 0, "pointfile byte cap was exceeded");
    if (contents.find('\0') != std::string_view::npos)
        return pointfile_failure(PointfileIngestError::malformed_line, 0, "pointfile contains a NUL byte");

    std::vector<LeakPoint> points;
    points.reserve(std::min<std::size_t>(limits.max_points, 1024));
    std::size_t source_points{};
    std::size_t offset{};
    std::size_t line_number{1};
    while (offset < contents.size()) {
        std::size_t line_end = contents.find('\n', offset);
        if (line_end == std::string_view::npos)
            line_end = contents.size();
        std::string_view line = contents.substr(offset, line_end - offset);
        if (line.size() > limits.max_line_bytes)
            return pointfile_failure(PointfileIngestError::line_too_long, line_number,
                                     "pointfile line byte cap was exceeded");
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line.find('\r') != std::string_view::npos)
            return pointfile_failure(PointfileIngestError::malformed_line, line_number,
                                     "pointfile contains an embedded carriage return");

        std::array<std::string_view, 3> tokens;
        std::size_t token_count{};
        std::size_t position{};
        while (position < line.size()) {
            while (position < line.size() && horizontal_space(line[position]))
                ++position;
            if (position == line.size())
                break;
            const std::size_t token_start = position;
            while (position < line.size() && !horizontal_space(line[position]))
                ++position;
            if (token_count == tokens.size())
                return pointfile_failure(PointfileIngestError::malformed_line, line_number,
                                         "pointfile line contains more than three fields");
            tokens[token_count++] = line.substr(token_start, position - token_start);
        }
        if (token_count != 0) {
            if (token_count != tokens.size())
                return pointfile_failure(PointfileIngestError::malformed_line, line_number,
                                         "pointfile line must contain exactly three coordinates");
            if (source_points == limits.max_points)
                return pointfile_failure(PointfileIngestError::too_many_points, line_number,
                                         "pointfile point cap was exceeded");
            LeakPoint point;
            std::array<std::int64_t *, 3> coordinates{&point.x, &point.y, &point.z};
            for (std::size_t index = 0; index < tokens.size(); ++index) {
                const PointfileIngestError coordinate_error =
                    parse_coordinate(tokens[index], limits, *coordinates[index]);
                if (coordinate_error != PointfileIngestError::none) {
                    const char *message = coordinate_error == PointfileIngestError::non_finite_coordinate
                                              ? "pointfile coordinate is non-finite"
                                          : coordinate_error == PointfileIngestError::coordinate_out_of_range
                                              ? "pointfile coordinate is outside the configured range"
                                              : "pointfile coordinate is malformed";
                    return pointfile_failure(coordinate_error, line_number, message);
                }
            }
            ++source_points;
            if (points.empty() || points.back() != point)
                points.push_back(point);
        }
        if (line_end == contents.size())
            break;
        offset = line_end + 1;
        ++line_number;
    }
    if (source_points == 0)
        return pointfile_failure(PointfileIngestError::empty_input, 0, "pointfile contains no points");
    if (points.size() < 2)
        return pointfile_failure(PointfileIngestError::too_few_distinct_points, 0,
                                 "pointfile contains fewer than two distinct consecutive points");

    std::vector<LeakPoint> reversed(points.rbegin(), points.rend());
    if (std::lexicographical_compare(reversed.begin(), reversed.end(), points.begin(), points.end()))
        points = std::move(reversed);
    LeakWitness witness{dialect, std::move(points), source_points, 0};
    witness.fingerprint = witness_fingerprint(witness.dialect, witness.points);
    return {PointfileIngestError::none, 0, {}, std::move(witness)};
}

PointfileIngestResult load_compiler_pointfile(const std::filesystem::path &path, const CompilerPointfileDialect dialect,
                                              const PointfileLimits &limits) {
    if (!pointfile_dialect_valid(dialect))
        return pointfile_failure(PointfileIngestError::unsupported_dialect, 0, "pointfile dialect is unsupported");
    if (!pointfile_limits_valid(limits))
        return pointfile_failure(PointfileIngestError::invalid_limits, 0, "pointfile limits are invalid");
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        return pointfile_failure(PointfileIngestError::io_error, 0, "could not open pointfile");
    const std::streampos end = input.tellg();
    if (end < 0)
        return pointfile_failure(PointfileIngestError::io_error, 0, "could not determine pointfile size");
    const auto unsigned_size = static_cast<std::uintmax_t>(end);
    if (unsigned_size > limits.max_bytes || unsigned_size > std::numeric_limits<std::size_t>::max())
        return pointfile_failure(PointfileIngestError::input_too_large, 0, "pointfile byte cap was exceeded");
    std::string contents(static_cast<std::size_t>(unsigned_size), '\0');
    input.seekg(0, std::ios::beg);
    if (!contents.empty())
        input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!input || static_cast<std::size_t>(input.gcount()) != contents.size())
        return pointfile_failure(PointfileIngestError::io_error, 0, "could not read complete pointfile");
    if (input.peek() != std::char_traits<char>::eof())
        return pointfile_failure(PointfileIngestError::input_too_large, 0, "pointfile changed while being read");
    return parse_compiler_pointfile(contents, dialect, limits);
}

std::size_t ExteriorCellGrid::x_cells() const noexcept { return x_planes.empty() ? 0 : x_planes.size() - 1; }

std::size_t ExteriorCellGrid::y_cells() const noexcept { return y_planes.empty() ? 0 : y_planes.size() - 1; }

std::size_t ExteriorCellGrid::z_cells() const noexcept { return z_planes.empty() ? 0 : z_planes.size() - 1; }

std::size_t ExteriorCellGrid::linear_index(const std::size_t x, const std::size_t y,
                                           const std::size_t z) const noexcept {
    return (x * y_cells() + y) * z_cells() + z;
}

ExteriorGridResult build_layout_exterior_grid(const Layout &layout, const LeakDiagnosisLimits &limits) {
    const PreparedLayout prepared = prepare_layout(layout, limits);
    if (prepared.error != ExteriorGridError::none)
        return {prepared.error, prepared.message, std::nullopt, prepared.fingerprint, prepared.layout_sha256};
    return build_grid_from_prepared(prepared, limits);
}

std::optional<ExteriorCellProof> locate_exterior_cell(const ExteriorCellGrid &grid, const LeakPoint point) noexcept {
    return locate_world_cell(grid, world_point(point));
}

bool verify_exposed_boundary_proof(const ExteriorCellGrid &grid, const LeakBoundaryCrossing &crossing,
                                   const ExteriorCellProof &proof) noexcept {
    if (proof.state != ExteriorCellState::exterior || proof.x >= grid.x_cells() || proof.y >= grid.y_cells() ||
        proof.z >= grid.z_cells()) {
        return false;
    }
    const std::size_t expected_index = grid.linear_index(proof.x, proof.y, proof.z);
    if (proof.linear_index != expected_index || expected_index >= grid.cells.size() ||
        grid.cells[expected_index] != ExteriorCellState::exterior) {
        return false;
    }
    const WorldPoint point = world_point(crossing.point);
    const GridBounds &bounds = crossing.owner.source_bounds;
    const auto tangential_interval_contains = [&point](const std::vector<std::int64_t> &planes, const std::size_t index,
                                                       const BoundaryAxis axis) {
        const long double value = component(point, axis);
        return index + 1 < planes.size() && value > static_cast<long double>(planes[index]) &&
               value < static_cast<long double>(planes[index + 1]);
    };
    if (crossing.axis != BoundaryAxis::x && !tangential_interval_contains(grid.x_planes, proof.x, BoundaryAxis::x))
        return false;
    if (crossing.axis != BoundaryAxis::y && !tangential_interval_contains(grid.y_planes, proof.y, BoundaryAxis::y))
        return false;
    if (crossing.axis != BoundaryAxis::z && !tangential_interval_contains(grid.z_planes, proof.z, BoundaryAxis::z))
        return false;

    const std::int64_t face_coordinate = crossing.face == BoundaryFace::minimum
                                             ? minimum_component(bounds, crossing.axis)
                                             : maximum_component(bounds, crossing.axis);
    if (!almost_equal(component(point, crossing.axis), static_cast<long double>(face_coordinate)))
        return false;
    std::size_t inside_x = proof.x;
    std::size_t inside_y = proof.y;
    std::size_t inside_z = proof.z;
    if (crossing.axis == BoundaryAxis::x) {
        if (crossing.face == BoundaryFace::minimum) {
            if (grid.x_planes[proof.x + 1] != face_coordinate || proof.x + 1 >= grid.x_cells())
                return false;
            inside_x = proof.x + 1;
        } else {
            if (grid.x_planes[proof.x] != face_coordinate || proof.x == 0)
                return false;
            inside_x = proof.x - 1;
        }
    } else if (crossing.axis == BoundaryAxis::y) {
        if (crossing.face == BoundaryFace::minimum) {
            if (grid.y_planes[proof.y + 1] != face_coordinate || proof.y + 1 >= grid.y_cells())
                return false;
            inside_y = proof.y + 1;
        } else {
            if (grid.y_planes[proof.y] != face_coordinate || proof.y == 0)
                return false;
            inside_y = proof.y - 1;
        }
    } else if (crossing.face == BoundaryFace::minimum) {
        if (grid.z_planes[proof.z + 1] != face_coordinate || proof.z + 1 >= grid.z_cells())
            return false;
        inside_z = proof.z + 1;
    } else {
        if (grid.z_planes[proof.z] != face_coordinate || proof.z == 0)
            return false;
        inside_z = proof.z - 1;
    }
    return grid.cells[grid.linear_index(inside_x, inside_y, inside_z)] == ExteriorCellState::interior;
}

LeakDiagnosisResult diagnose_layout_leak(const Layout &layout, const LeakWitness &witness,
                                         const LeakDiagnosisLimits &limits) {
    LeakDiagnosisResult result;
    if (!diagnosis_limits_valid(limits)) {
        result.status = LeakDiagnosisStatus::invalid_limits;
        result.message = "leak diagnosis limits are invalid";
        return result;
    }
    if (witness.points.size() > limits.max_witness_points) {
        result.status = LeakDiagnosisStatus::capacity_exceeded;
        result.message = "witness point cap was exceeded";
        return result;
    }
    if (!pointfile_dialect_valid(witness.dialect) ||
        !pointfile_dialect_matches_game(witness.dialect, layout.spec.game) || witness.points.size() < 2 ||
        witness.source_point_count < witness.points.size() || witness.fingerprint == 0 ||
        witness.fingerprint != witness_fingerprint(witness.dialect, witness.points) ||
        std::adjacent_find(witness.points.begin(), witness.points.end()) != witness.points.end()) {
        result.status = LeakDiagnosisStatus::invalid_witness;
        result.message = "leak witness is not a valid normalized point sequence";
        return result;
    }
    result.witness = witness;
    const PreparedLayout prepared = prepare_layout(layout, limits);
    if (prepared.error != ExteriorGridError::none) {
        result.status = diagnosis_status(prepared.error);
        result.message = prepared.message;
        return result;
    }
    result.layout_sha256 = prepared.layout_sha256;
    std::size_t segment_volume_tests{};
    if (!checked_product(witness.points.size() - 1, prepared.volumes.size(), limits.max_segment_volume_tests,
                         segment_volume_tests)) {
        result.status = LeakDiagnosisStatus::capacity_exceeded;
        result.message = "witness segment-volume test cap was exceeded";
        return result;
    }
    ExteriorGridResult grid_result = build_grid_from_prepared(prepared, limits);
    if (!grid_result.ok()) {
        result.status = diagnosis_status(grid_result.error);
        result.message = grid_result.message;
        return result;
    }
    ExteriorCellGrid &grid = *grid_result.grid;

    enum class FirstEvent : std::uint8_t { none, unique, ambiguous };
    FirstEvent first_event{FirstEvent::none};
    std::optional<LeakBoundaryCrossing> first_crossing;
    std::optional<ExteriorCellProof> first_proof;

    const auto record_transition = [&](const WorldPoint &crossing_point, const WorldPoint &before_sample,
                                       const WorldPoint &after_sample, const bool inside_before,
                                       const bool inside_after, const std::size_t segment, const long double fraction) {
        const WorldPoint &inside_sample = inside_before ? before_sample : after_sample;
        const WorldPoint &outside_sample = inside_before ? after_sample : before_sample;
        std::vector<BoundaryCandidate> candidates =
            boundary_candidates(prepared.volumes, grid, crossing_point, inside_sample, outside_sample);
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                        [](const BoundaryCandidate &candidate) {
                                            return !candidate.proof ||
                                                   candidate.proof->state != ExteriorCellState::exterior;
                                        }),
                         candidates.end());
        const long double outside_delta_x = outside_sample.x - crossing_point.x;
        const long double outside_delta_y = outside_sample.y - crossing_point.y;
        const long double outside_delta_z = outside_sample.z - crossing_point.z;
        const long double largest_delta =
            std::max({std::abs(outside_delta_x), std::abs(outside_delta_y), std::abs(outside_delta_z)});
        const long double probe_fraction =
            largest_delta > geometry_epsilon
                ? std::min(0.5L, static_cast<long double>(limits.exterior_margin) * 0.5L / largest_delta)
                : 0.0L;
        const WorldPoint outside_probe{crossing_point.x + outside_delta_x * probe_fraction,
                                       crossing_point.y + outside_delta_y * probe_fraction,
                                       crossing_point.z + outside_delta_z * probe_fraction};
        const bool outside_is_exterior = world_point_touches_state(grid, outside_probe, ExteriorCellState::exterior);
        if (candidates.empty()) {
            if (!outside_is_exterior)
                return;
            ++result.exposed_crossing_count;
            ++result.ambiguous_crossing_count;
            if (first_event == FirstEvent::none)
                first_event = FirstEvent::ambiguous;
            return;
        }
        ++result.exposed_crossing_count;
        if (candidates.size() != 1) {
            ++result.ambiguous_crossing_count;
            if (first_event == FirstEvent::none)
                first_event = FirstEvent::ambiguous;
            return;
        }
        if (first_event != FirstEvent::none)
            return;
        const BoundaryCandidate &candidate = candidates.front();
        std::optional<LeakPoint> quantized = quantize_world_point(crossing_point);
        if (!quantized) {
            first_event = FirstEvent::ambiguous;
            ++result.ambiguous_crossing_count;
            return;
        }
        const GridCoord face_coordinate = candidate.face == BoundaryFace::minimum
                                              ? minimum_component(candidate.owner.source_bounds, candidate.axis)
                                              : maximum_component(candidate.owner.source_bounds, candidate.axis);
        const std::int64_t exact_face = static_cast<std::int64_t>(face_coordinate) * leak_subunits_per_world_unit;
        if (candidate.axis == BoundaryAxis::x)
            quantized->x = exact_face;
        else if (candidate.axis == BoundaryAxis::y)
            quantized->y = exact_face;
        else
            quantized->z = exact_face;
        const long double clamped_fraction = std::clamp(fraction, 0.0L, 1.0L);
        const auto fraction_millionths = static_cast<std::uint32_t>(std::round(clamped_fraction * 1000000.0L));
        LeakBoundaryCrossing crossing{candidate.owner, candidate.axis,      candidate.face, *quantized,
                                      segment,         fraction_millionths, inside_after};
        if (!verify_exposed_boundary_proof(grid, crossing, *candidate.proof)) {
            first_event = FirstEvent::ambiguous;
            ++result.ambiguous_crossing_count;
            return;
        }
        first_event = FirstEvent::unique;
        first_crossing = std::move(crossing);
        first_proof = candidate.proof;
    };

    std::optional<WorldPoint> previous_end_sample;
    std::optional<bool> previous_end_inside;
    for (std::size_t segment = 0; segment + 1 < witness.points.size(); ++segment) {
        const WorldPoint from = world_point(witness.points[segment]);
        const WorldPoint to = world_point(witness.points[segment + 1]);
        std::vector<long double> fractions{0.0L, 1.0L};
        fractions.reserve(prepared.volumes.size() * 6U + 2U);
        for (const InteriorVolume &volume : prepared.volumes) {
            const GridBounds &bounds = volume.owner.source_bounds;
            for (const BoundaryAxis axis : {BoundaryAxis::x, BoundaryAxis::y, BoundaryAxis::z}) {
                const long double start = component(from, axis);
                const long double delta = component(to, axis) - start;
                if (std::abs(delta) <= geometry_epsilon)
                    continue;
                for (const GridCoord plane : {minimum_component(bounds, axis), maximum_component(bounds, axis)}) {
                    const long double fraction = (static_cast<long double>(plane) - start) / delta;
                    if (fraction > geometry_epsilon && fraction < 1.0L - geometry_epsilon)
                        fractions.push_back(fraction);
                }
            }
        }
        std::sort(fractions.begin(), fractions.end());
        fractions.erase(std::unique(fractions.begin(), fractions.end(),
                                    [](const long double left, const long double right) {
                                        return std::abs(left - right) <= 1.0e-12L;
                                    }),
                        fractions.end());
        std::vector<WorldPoint> samples;
        std::vector<bool> inside;
        samples.reserve(fractions.size() - 1);
        inside.reserve(fractions.size() - 1);
        for (std::size_t interval = 0; interval + 1 < fractions.size(); ++interval) {
            const long double midpoint = (fractions[interval] + fractions[interval + 1]) / 2.0L;
            samples.push_back(interpolate(from, to, midpoint));
            inside.push_back(point_inside_union(samples.back(), prepared.volumes));
        }
        if (previous_end_sample && previous_end_inside && *previous_end_inside != inside.front()) {
            record_transition(from, *previous_end_sample, samples.front(), *previous_end_inside, inside.front(),
                              segment - 1, 1.0L);
        }
        for (std::size_t boundary = 1; boundary + 1 <= fractions.size(); ++boundary) {
            if (boundary >= inside.size())
                break;
            if (inside[boundary - 1] == inside[boundary])
                continue;
            record_transition(interpolate(from, to, fractions[boundary]), samples[boundary - 1], samples[boundary],
                              inside[boundary - 1], inside[boundary], segment, fractions[boundary]);
        }
        previous_end_sample = samples.back();
        previous_end_inside = inside.back();
    }

    result.exterior_grid = std::move(grid_result.grid);
    if (first_event == FirstEvent::none) {
        result.status = LeakDiagnosisStatus::no_exposed_crossing;
        result.message = "witness does not cross a uniquely attributable exterior boundary";
        return result;
    }
    if (first_event == FirstEvent::ambiguous || !first_crossing || !first_proof) {
        result.status = LeakDiagnosisStatus::ambiguous_crossing;
        result.message = "the first exposed crossing is ambiguous";
        return result;
    }
    const std::optional<GridBounds> seal_bounds = local_seal_bounds(*first_crossing, limits);
    if (!seal_bounds) {
        result.status = LeakDiagnosisStatus::coordinate_overflow;
        result.message = "local seal proposal exceeds the layout grid range";
        return result;
    }

    LocalSealProposal proposal;
    proposal.owner = first_crossing->owner;
    proposal.axis = first_crossing->axis;
    proposal.face = first_crossing->face;
    proposal.bounds = *seal_bounds;
    proposal.material = local_seal_material(first_crossing->axis, first_crossing->face);
    proposal.applicability = {1,
                              prepared.fingerprint,
                              witness.fingerprint,
                              first_crossing->owner,
                              first_crossing->axis,
                              first_crossing->face,
                              *seal_bounds,
                              first_crossing->point,
                              first_crossing->witness_segment,
                              first_crossing->segment_fraction_millionths,
                              *first_proof,
                              {}};
    proposal.applicability.layout_sha256 = prepared.layout_sha256;
    proposal.regression = {layout.rank_score, result.exposed_crossing_count,
                           result.exposed_crossing_count == 0 ? 0 : result.exposed_crossing_count - 1, true, true};
    result.status = LeakDiagnosisStatus::diagnosed;
    result.message = "first exposed local boundary crossing was diagnosed";
    result.crossing = std::move(first_crossing);
    result.exterior_proof = std::move(first_proof);
    result.proposal = std::move(proposal);
    return result;
}

bool local_seal_proposal_preconditions_hold(const Layout &layout, const LocalSealProposal &proposal,
                                            const LeakDiagnosisLimits &limits) {
    if (proposal.applicability.schema_version != 1 || proposal.owner != proposal.applicability.expected_owner ||
        proposal.axis != proposal.applicability.expected_axis ||
        proposal.face != proposal.applicability.expected_face ||
        proposal.bounds != proposal.applicability.expected_seal_bounds ||
        proposal.material != local_seal_material(proposal.axis, proposal.face) ||
        proposal.purpose != BrushPurpose::structural || proposal.ownership != BrushOwnership::structural ||
        !proposal.bounds.valid() || proposal.applicability.witness_fingerprint == 0 ||
        proposal.applicability.layout_sha256.size() != 64 || !proposal.regression.compiler_recheck_required ||
        !proposal.regression.reject_new_validation_errors || !std::isfinite(proposal.regression.baseline_rank_score) ||
        proposal.regression.baseline_rank_score != layout.rank_score ||
        proposal.regression.baseline_exposed_crossings == 0 ||
        proposal.regression.required_max_exposed_crossings + 1 != proposal.regression.baseline_exposed_crossings) {
        return false;
    }
    const PreparedLayout prepared = prepare_layout(layout, limits);
    if (prepared.error != ExteriorGridError::none ||
        prepared.fingerprint != proposal.applicability.layout_fingerprint ||
        prepared.layout_sha256 != proposal.applicability.layout_sha256)
        return false;
    const bool owner_exists =
        std::any_of(prepared.volumes.begin(), prepared.volumes.end(),
                    [&proposal](const InteriorVolume &volume) { return volume.owner == proposal.owner; });
    if (!owner_exists)
        return false;
    const ExteriorGridResult grid_result = build_grid_from_prepared(prepared, limits);
    if (!grid_result.ok())
        return false;
    const LeakBoundaryCrossing expected_crossing{proposal.owner,
                                                 proposal.axis,
                                                 proposal.face,
                                                 proposal.applicability.expected_crossing_point,
                                                 proposal.applicability.witness_segment,
                                                 proposal.applicability.segment_fraction_millionths,
                                                 false};
    if (!verify_exposed_boundary_proof(*grid_result.grid, expected_crossing, proposal.applicability.exterior_proof)) {
        return false;
    }
    const GridBounds &source = proposal.owner.source_bounds;
    for (const BoundaryAxis axis : {BoundaryAxis::x, BoundaryAxis::y, BoundaryAxis::z}) {
        const GridCoord proposed_minimum = minimum_component(proposal.bounds, axis);
        const GridCoord proposed_maximum = maximum_component(proposal.bounds, axis);
        const GridCoord source_minimum = minimum_component(source, axis);
        const GridCoord source_maximum = maximum_component(source, axis);
        if (axis == proposal.axis) {
            if (proposal.face == BoundaryFace::minimum) {
                if (proposed_maximum != source_minimum || proposed_minimum >= proposed_maximum ||
                    static_cast<std::int64_t>(proposed_maximum) - proposed_minimum > limits.seal_thickness)
                    return false;
            } else if (proposed_minimum != source_maximum || proposed_minimum >= proposed_maximum ||
                       static_cast<std::int64_t>(proposed_maximum) - proposed_minimum > limits.seal_thickness) {
                return false;
            }
        } else if (proposed_minimum < source_minimum || proposed_maximum > source_maximum ||
                   proposed_minimum >= proposed_maximum) {
            return false;
        }
    }
    return true;
}

} // namespace qmg
