#pragma once

#include "qmg/map_document.hpp"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace qmg {

struct MapParseLimits {
    std::size_t max_source_bytes{16U * 1024U * 1024U};
    std::size_t max_token_bytes{4096};
    std::size_t max_entities{65536};
    std::size_t max_properties_per_entity{4096};
    std::size_t max_primitives_per_entity{65536};
    std::size_t max_faces_per_brush{512};
    std::size_t max_patch_dimension{31};
    std::size_t max_total_nodes{1U * 1024U * 1024U};
    double max_absolute_number{1.0e12};
    bool preserve_pristine_source{true};
};

struct MapParseIssue {
    std::string code;
    std::size_t offset{};
    std::size_t line{1};
    std::size_t column{1};
    std::string message;
};

struct MapParseResult {
    std::optional<MapDocument> document;
    std::vector<MapParseIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return document.has_value() && issues.empty(); }
};

class MapParseError final : public std::runtime_error {
  public:
    explicit MapParseError(MapParseIssue issue);

    [[nodiscard]] const MapParseIssue &issue() const noexcept { return issue_; }

  private:
    MapParseIssue issue_;
};

class MapParser {
  public:
    explicit MapParser(MapParseLimits limits = {});

    [[nodiscard]] MapParseResult parse(std::string_view source, MapDialect dialect) const;
    [[nodiscard]] MapDocument parse_or_throw(std::string_view source, MapDialect dialect) const;

  private:
    MapParseLimits limits_;
};

} // namespace qmg
