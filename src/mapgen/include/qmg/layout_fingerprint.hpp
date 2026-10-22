#pragma once

#include "qmg/layout.hpp"

#include <string>

namespace qmg {

// Canonical SHA-256 over every typed Layout field. Top-level collections,
// room tags, and passage boxes are ordered by semantic value before hashing;
// convex plane order is retained because it can affect lowered source bytes.
[[nodiscard]] std::string canonical_layout_sha256(const Layout &layout);

} // namespace qmg
