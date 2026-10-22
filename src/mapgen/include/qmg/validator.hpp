#pragma once

#include "qmg/layout.hpp"
#include <tuple>

namespace qmg {

// Distinct routes between the same rooms are useful on stacked floors.
// Direction, order and repeated volume declarations do not make a new route.
// This key describes declared geometry, not its geometric union.
using ConnectionRouteKey = std::tuple<RoomId, RoomId, std::vector<GridBounds>>;
[[nodiscard]] ConnectionRouteKey connection_route_key(const Connection &connection);
[[nodiscard]] bool connection_has_valid_passage(const Room &from, const Room &to, const Connection &connection);

class LayoutValidator {
  public:
    [[nodiscard]] ValidationResult validate(const Layout &layout) const;
};

} // namespace qmg
