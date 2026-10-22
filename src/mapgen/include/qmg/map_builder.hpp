#pragma once

#include "qmg/construction.hpp"
#include "qmg/map_document.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace qmg {

enum class MapBuildSourceKind : std::uint8_t {
    room,
    connection,
    solid,
    placement,
};

struct MapBuildSourceRef {
    MapBuildSourceKind kind{MapBuildSourceKind::room};
    std::uint32_t id{};

    auto operator<=>(const MapBuildSourceRef &) const = default;
};

struct MapBuildManifestEntry {
    MapBuildSourceRef source;
    std::vector<MapNodeId> node_ids;
};

struct MapBuildStats {
    std::size_t input_free_volumes{};
    std::size_t unique_free_volumes{};
    std::size_t evaluated_face_cells{};
    std::size_t shell_brushes{};
    std::size_t structural_brushes{};
    std::size_t detail_brushes{};
    std::size_t placement_brushes{};
    std::size_t entity_count{};
    std::size_t brush_count{};
    std::size_t patch_count{};
    std::size_t face_count{};
};

struct MapBuildOptions {
    MapDialect dialect{MapDialect::quake1_valve220};
    GridCoord shell_thickness{32};
    GridCoord max_coordinate{32768};
    std::size_t max_face_cells{1U << 20U};
    std::size_t max_shell_brushes{32768};
    std::size_t max_total_brushes{65536};
};

class MapBuildError final : public std::runtime_error {
  public:
    MapBuildError(std::string code, std::string message);

    [[nodiscard]] const std::string &code() const noexcept { return code_; }

  private:
    std::string code_;
};

struct MapBuildResult {
    MapDocument document;
    std::vector<MapBuildManifestEntry> manifest;
    MapBuildStats stats;
};

class MapBuilder {
  public:
    [[nodiscard]] MapBuildResult build(const Layout &layout, const MapBuildOptions &options) const;
    [[nodiscard]] MapBuildResult build(const Layout &layout, const MapBuildOptions &options,
                                       const ConstructionResult &construction) const;
};

} // namespace qmg
