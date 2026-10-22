#pragma once

#include "qmg/layout.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace qmg {

struct NavigationNodeId {
    std::uint32_t value{};

    [[nodiscard]] explicit constexpr operator bool() const noexcept { return value != 0; }
    auto operator<=>(const NavigationNodeId &) const = default;
};

struct NavigationEdgeId {
    std::uint32_t value{};

    [[nodiscard]] explicit constexpr operator bool() const noexcept { return value != 0; }
    auto operator<=>(const NavigationEdgeId &) const = default;
};

enum class NavigationAgentKind : std::uint8_t {
    player,
    monster_small,
    monster_large,
};

struct NavigationAgentProfile {
    Game game{Game::quake1};
    NavigationAgentKind kind{NavigationAgentKind::player};
    GridVec3 hull_mins{-16, -16, -24};
    GridVec3 hull_maxs{16, 16, 32};
    GridCoord max_step_up{18};
    GridCoord max_jump_up{40};
    GridCoord max_jump_gap{64};
    GridCoord max_drop{128};

    bool operator==(const NavigationAgentProfile &) const = default;
};

// Constants come from the local official id source trees. Q1 and Q2 standing
// players use [-16,-16,-24]..[16,16,32], Q3 uses
// [-15,-15,-24]..[15,15,32], and all three step 18 units. Q1 BSP hull 2 and
// representative large Q2 ground monsters use
// [-32,-32,-24]..[32,32,64]. A 40-unit planned jump rise is deliberately
// below the ~45-unit apex implied by jump velocity 270 and gravity 800.
// Stock ground-monster profiles conservatively permit 18-unit steps but no
// planned jump or cliff-drop transitions.
[[nodiscard]] NavigationAgentProfile standard_navigation_profile(Game game, NavigationAgentKind kind);

enum class NavigationTransitionKind : std::uint8_t {
    walk,
    step,
    jump,
    drop,
    door,
    lift,
    teleporter,
};

enum class NavigationSourceKind : std::uint8_t {
    room,
    connection,
    solid,
    placement,
};

struct NavigationSourceRef {
    NavigationSourceKind kind{NavigationSourceKind::room};
    std::uint32_t id{};
    // Stable coordinate-order index for repeated source geometry, such as a
    // connection's passage chain or a lift's lower/upper endpoint.
    std::uint32_t subindex{};

    auto operator<=>(const NavigationSourceRef &) const = default;
};

struct NavigationSlope {
    // Exact axial surface: low_z + rise * (axis - start) / (end - start).
    // end may precede start. support_offset is the selected hull's uphill
    // horizontal extent: standing feet touch the highest plane under the hull.
    bool along_x{true};
    GridCoord start{};
    GridCoord end{};
    GridCoord low_z{};
    GridCoord rise{};
    GridCoord support_offset{};

    auto operator<=>(const NavigationSlope &) const = default;
};

struct NavigationArea {
    // Bounds for legal agent origins after hull expansion. The vertical
    // coordinate of an origin is navigation_floor_z(area,x,y) - hull_mins.z.
    // On a slope floor_z is the minimum supported feet height; ceiling_z is
    // a certified clearance envelope bound, not a flat ceiling surface.
    GridCoord min_x{};
    GridCoord min_y{};
    GridCoord max_x{};
    GridCoord max_y{};
    GridCoord floor_z{};
    GridCoord ceiling_z{};
    std::optional<NavigationSlope> slope{};

    [[nodiscard]] bool valid() const noexcept { return min_x < max_x && min_y < max_y && floor_z < ceiling_z; }
    [[nodiscard]] bool contains_xy(GridVec3 point) const noexcept {
        return point.x >= min_x && point.x <= max_x && point.y >= min_y && point.y <= max_y;
    }
    auto operator<=>(const NavigationArea &) const = default;
};

// Hull-aware feet height, rounded upward to the integer grid. Slopes retain an
// exact rational height function; rounding does not construct physical steps.
// XY must lie inside the area. Unsupported/invalid slope metadata throws.
[[nodiscard]] GridCoord navigation_floor_z(const NavigationArea &area, GridCoord x, GridCoord y);

struct NavigationNode {
    NavigationNodeId id;
    NavigationSourceRef source;
    std::uint32_t fragment_index{};
    NavigationArea area;
    GridVec3 origin;

    bool operator==(const NavigationNode &) const = default;
};

using NavigationCost = std::uint64_t;
inline constexpr NavigationCost kUnreachableNavigationCost = std::numeric_limits<NavigationCost>::max();

struct NavigationEdge {
    NavigationEdgeId id;
    NavigationNodeId from;
    NavigationNodeId to;
    NavigationTransitionKind kind{NavigationTransitionKind::walk};
    NavigationCost cost{};
    // Natural transitions identify the stable free-space/support boundary
    // that permits them. Typed transitions identify their PlacementId.
    NavigationSourceRef source;

    bool operator==(const NavigationEdge &) const = default;
};

enum class NavigationFindingSeverity : std::uint8_t {
    warning,
    error,
};

enum class NavigationFindingKind : std::uint8_t {
    insufficient_clearance,
    invalid_source_geometry,
    missing_passage_geometry,
    unbound_placement,
    unresolved_transition,
    missing_spawn,
    unreachable_spawn,
    unreachable_objective,
    unreachable_monster,
};

struct NavigationFinding {
    NavigationFindingSeverity severity{NavigationFindingSeverity::error};
    NavigationFindingKind kind{NavigationFindingKind::insufficient_clearance};
    std::optional<NavigationSourceRef> source;
    std::optional<NavigationSourceRef> related_source;
    std::string message;

    bool operator==(const NavigationFinding &) const = default;
};

struct NavigationPlacementBinding {
    PlacementId placement;
    std::optional<NavigationNodeId> node;

    bool operator==(const NavigationPlacementBinding &) const = default;
};

struct NavigationStats {
    std::size_t source_volumes{};
    std::size_t rejected_source_volumes{};
    std::size_t evaluated_grid_cells{};
    std::size_t evaluated_node_pairs{};
    std::size_t node_count{};
    std::size_t edge_count{};
    std::size_t placement_bindings{};

    bool operator==(const NavigationStats &) const = default;
};

struct NavigationOptions {
    NavigationAgentKind agent_kind{NavigationAgentKind::player};
    std::optional<NavigationAgentProfile> profile_override;
    GridCoord max_coordinate{32768};
    GridCoord placement_snap_height{96};
    GridCoord max_mover_side_distance{64};
    NavigationCost cost_per_unit{1000};
    NavigationCost step_penalty{10000};
    NavigationCost jump_penalty{50000};
    NavigationCost drop_penalty{20000};
    NavigationCost door_penalty{100000};
    NavigationCost lift_penalty{150000};
    NavigationCost teleporter_penalty{1000};
    std::size_t max_rooms{256};
    std::size_t max_connections{512};
    std::size_t max_solids{8192};
    std::size_t max_placements{2048};
    std::size_t max_passage_volumes{4096};
    std::size_t max_convex_planes{64};
    std::size_t max_source_volumes{8192};
    std::size_t max_grid_cells{1U << 20U};
    std::size_t max_nodes{32768};
    std::size_t max_edges{131072};
    std::size_t max_pair_checks{1U << 22U};
    std::size_t max_path_expansions{131072};
};

class NavigationError final : public std::runtime_error {
  public:
    NavigationError(std::string code, std::string message);

    [[nodiscard]] const std::string &code() const noexcept { return code_; }

  private:
    std::string code_;
};

class NavigationGraph {
  public:
    [[nodiscard]] const std::vector<NavigationNode> &nodes() const noexcept { return nodes_; }
    [[nodiscard]] const std::vector<NavigationEdge> &edges() const noexcept { return edges_; }
    [[nodiscard]] const NavigationNode *find_node(NavigationNodeId id) const noexcept;
    [[nodiscard]] const NavigationEdge *find_edge(NavigationEdgeId id) const noexcept;

  private:
    friend class NavigationAnalyzer;
    std::vector<NavigationNode> nodes_;
    std::vector<NavigationEdge> edges_;
};

struct NavigationPath {
    NavigationCost cost{kUnreachableNavigationCost};
    std::vector<NavigationNodeId> nodes;
    std::vector<NavigationEdgeId> edges;

    [[nodiscard]] bool reachable() const noexcept { return cost != kUnreachableNavigationCost; }
};

[[nodiscard]] NavigationPath shortest_navigation_path(const NavigationGraph &graph, NavigationNodeId from,
                                                      NavigationNodeId to, std::size_t max_expansions);
// A placement-oriented cost estimate. XY positions must be inside their bound
// areas; Z is projected onto the areas' standing floors. Same-area travel uses
// direct Manhattan distance. Other paths include the approach from/to the stable
// node anchors, in addition to the graph cost and transition penalties. This is
// neither a shortest continuous-space path nor measured engine traversal time.
// cost_per_unit must match the policy used to build the graph.
[[nodiscard]] NavigationPath navigation_path_between_positions(const NavigationGraph &graph, NavigationNodeId from,
                                                               GridVec3 from_position, NavigationNodeId to,
                                                               GridVec3 to_position, NavigationCost cost_per_unit,
                                                               std::size_t max_expansions);
[[nodiscard]] std::vector<NavigationNodeId>
reachable_navigation_nodes(const NavigationGraph &graph, NavigationNodeId from, std::size_t max_expansions);

struct NavigationAnalysis {
    NavigationAgentProfile profile;
    NavigationGraph graph;
    std::vector<NavigationPlacementBinding> placement_bindings;
    std::vector<NavigationFinding> findings;
    NavigationStats stats;
    std::string layout_sha256;

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] const NavigationPlacementBinding *find_binding(PlacementId placement) const noexcept;
};

class NavigationAnalyzer {
  public:
    [[nodiscard]] NavigationAnalysis analyze(const Layout &layout, const NavigationOptions &options = {}) const;
};

} // namespace qmg
