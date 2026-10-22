#pragma once

#include "qmg/types.hpp"

#include <optional>
#include <string_view>

namespace qmg {

struct PromptOverrides {
    std::optional<Game> game;
    std::optional<GameMode> mode;
    std::optional<std::uint64_t> seed;
    std::optional<int> room_count;
    std::optional<int> target_players;
    // VibeStudio: the Generate Level dialog's and CLI's choices, 0..1, in place of the words the
    // prompt would otherwise be read for.
    std::optional<double> verticality;
    std::optional<double> intensity;
};

class PromptParser {
  public:
    [[nodiscard]] PromptSpec parse(std::string_view prompt, const PromptOverrides &overrides = {}) const;
};

[[nodiscard]] std::optional<Game> parse_game(std::string_view value);
[[nodiscard]] std::optional<GameMode> parse_mode(std::string_view value);

} // namespace qmg
