#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qmg {

// D7 landing pattern (docs/CHANGE_PROTOCOL.md, "Switches"). Every new generator behaviour lands behind a named
// boolean stage switch that is off by default. With every switch at its default, map and report bytes are
// unchanged; a non-default switch is recorded in the report's top-level "switches" object and in the layout
// fingerprint. The CLI sets switches with `qmg generate ... --switch <name>=on|off` (repeatable).

// The output sub-lane a switch may change (docs/CHANGE_PROTOCOL.md, "Output sub-lanes").
enum class SwitchSubLane : std::uint8_t {
    none, // changes no generator output (mechanism self-tests)
    mp,
    sp,
    both,
};

// One enumerator per registered switch. The enumerator's value is its index in stage_switch_registry(), which is
// sorted by name; src/switches.cpp checks both at compile time.
enum class StageSwitch : std::uint8_t {
    r1s_monster_hull_support,
    r1s_mp_player_start,
    r1s_q1_intermission,
    r1s_q3_ctf_team_begin,
    r1s_sp_coop_starts,
    r1s_sp_pickup_hull_support,
    selftest_noop,
};

struct StageSwitchDefinition {
    StageSwitch id{};
    // Dotted lowercase, <workstream>.<cause>, for example r1s.q1_intermission. See is_valid_switch_name().
    std::string_view name;
    bool default_on{false};
    SwitchSubLane sub_lane{SwitchSubLane::none};
    // The single cause the switch introduces.
    std::string_view cause;
    // The change id (out/phase2/changes/<id>, later the commit) that introduced the switch.
    std::string_view introduced_by;
};

// A switch set stores one bit per switch, so a registry holds at most this many live switches. Retired switches
// leave the registry (docs/CHANGE_PROTOCOL.md, "Retiring a switch").
inline constexpr std::size_t kMaximumStageSwitches = 64;
inline constexpr std::size_t kMaximumSwitchNameBytes = 64;

// Two or more segments joined by '.'; each segment is [a-z][a-z0-9_]*; at most 64 bytes in all.
[[nodiscard]] bool is_valid_switch_name(std::string_view name) noexcept;

// Names valid, strictly increasing (sorted and unique) and at most kMaximumStageSwitches entries.
[[nodiscard]] bool is_valid_switch_registry(std::span<const StageSwitchDefinition> registry) noexcept;

// The generator's registry, sorted by name.
[[nodiscard]] std::span<const StageSwitchDefinition> stage_switch_registry() noexcept;

[[nodiscard]] std::string to_string(SwitchSubLane lane);

class StageSwitches {
  public:
    struct Entry {
        std::string_view name;
        bool on{};
        friend bool operator==(const Entry &, const Entry &) = default;
    };

    // Every switch of the generator's registry at its default.
    StageSwitches() noexcept;
    // Every switch of `registry` at its default. Throws std::invalid_argument for an invalid registry. For
    // tests of the mechanism itself; generation uses the generator's registry.
    explicit StageSwitches(std::span<const StageSwitchDefinition> registry);

    // Typed query for stage code. Throws std::logic_error on a set built from another registry.
    [[nodiscard]] bool on(StageSwitch id) const;
    // Throws std::invalid_argument for a name the registry does not hold.
    [[nodiscard]] bool on(std::string_view name) const;
    // Throws std::invalid_argument for a name the registry does not hold.
    void set(std::string_view name, bool value);

    [[nodiscard]] bool all_default() const noexcept;
    // The switches away from their default, in registry (name) order.
    [[nodiscard]] std::vector<Entry> non_default() const;
    [[nodiscard]] std::span<const StageSwitchDefinition> registry() const noexcept { return registry_; }

    friend bool operator==(const StageSwitches &left, const StageSwitches &right) noexcept;

  private:
    [[nodiscard]] std::size_t index_of(std::string_view name) const;
    [[nodiscard]] bool bit(std::size_t index) const noexcept;

    std::span<const StageSwitchDefinition> registry_;
    std::uint64_t values_{};
    std::uint64_t defaults_{};
};

// Applies repeated `--switch` values of the form <name>=on|off, in order, to a set at its defaults. Throws
// std::invalid_argument for a malformed value, an unknown name, or a name given more than once (even with the
// same value). Setting a switch to its default is accepted and leaves the set at its default.
[[nodiscard]] StageSwitches parse_stage_switch_assignments(std::span<const std::string> assignments);
[[nodiscard]] StageSwitches parse_stage_switch_assignments(std::span<const std::string> assignments,
                                                           std::span<const StageSwitchDefinition> registry);

} // namespace qmg
