#pragma once

#include "qmg/switches.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace qmg {

enum class Game {
    quake1,
    quake2,
    quake3,
};

enum class GameMode {
    single_player,
    deathmatch,
    duel,
    capture_the_flag,
};

enum class LayoutIntent {
    unspecified,
    wrapped_center,
};

enum class Severity {
    warning,
    error,
};

struct Vec3 {
    double x{};
    double y{};
    double z{};
};

struct Bounds {
    Vec3 min;
    Vec3 max;

    [[nodiscard]] Vec3 center() const;
    [[nodiscard]] bool valid() const;
    [[nodiscard]] bool contains(const Vec3 &point, double margin = 0.0) const;
};

struct PromptSpec {
    std::string source_prompt;
    std::string title{"Untitled generated level"};
    std::string theme{"industrial"};
    Game game{Game::quake1};
    GameMode mode{GameMode::single_player};
    std::uint64_t seed{1};
    int room_count{6};
    int target_players{4};
    double verticality{0.5};
    double intensity{0.5};
    bool prefer_loops{false};
    LayoutIntent layout_intent{LayoutIntent::unspecified};
    // D7 stage switches. Set from PipelineOptions, never from the prompt text. Stages query
    // spec.switches.on(StageSwitch::...); at their defaults they change no map or report byte.
    StageSwitches switches;
};

struct Issue {
    Severity severity{Severity::error};
    std::string code;
    std::string message;
};

struct ValidationResult {
    std::vector<Issue> issues;

    [[nodiscard]] bool ok() const;
    [[nodiscard]] std::size_t error_count() const;
};

struct RepairPass {
    int iteration{};
    std::vector<Issue> before;
    bool changed{};
};

[[nodiscard]] std::string to_string(Game game);
[[nodiscard]] std::string to_string(GameMode mode);
[[nodiscard]] std::string to_string(LayoutIntent intent);
// Explicit construction requests must never fall back to an unrelated layout.
void require_supported_layout_intent(const PromptSpec &spec);
[[nodiscard]] std::string to_string(Severity severity);
[[nodiscard]] bool is_valid_utf8(std::string_view value);

} // namespace qmg
