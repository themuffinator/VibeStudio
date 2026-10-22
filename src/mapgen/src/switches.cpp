#include "qmg/switches.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace qmg {
namespace {

// The generator's switch registry (D7). Keep it sorted by name; each entry's id is its index. Adding, flipping
// and retiring a switch follow docs/CHANGE_PROTOCOL.md, "Switches".
constexpr std::array<StageSwitchDefinition, 7> kRegistry{{
    {StageSwitch::r1s_monster_hull_support, "r1s.monster_hull_support", true, SwitchSubLane::sp,
     "R1-s entity safety (5): every monster of a Quake 1 or Quake 2 single-player map is tested with its class's real "
     "spawn box (data/profiles monster_hulls, from the id source); a monster whose box intersects a solid brush or "
     "hangs over a support edge moves to the nearest clear, fully supported station of its room (or, with none, is "
     "left out), so no monster spawns embedded in a platform or stuck in a wall",
     "r1s-monster-hull-support"},
    {StageSwitch::r1s_mp_player_start, "r1s.mp_player_start", true, SwitchSubLane::mp,
     "R1-s entity safety (2): every Quake 1 and Quake 2 multiplayer map (deathmatch, duel, CTF) gets exactly one "
     "info_player_start at a supported, clear station, so `map <name>` with deathmatch 0 does not abort; "
     "deathmatch spawn selection never reads it",
     "r1s-mp-player-start"},
    {StageSwitch::r1s_q1_intermission, "r1s.q1_intermission", true, SwitchSubLane::both,
     "R1-s entity safety (1): every Quake 1 map gets an info_intermission camera, so FindIntermission never "
     "reaches objerror at the end of a level or match",
     "r1s-q1-intermission"},
    {StageSwitch::r1s_q3_ctf_team_begin, "r1s.q3_ctf_team_begin", false, SwitchSubLane::mp,
     "R1-s entity safety (4): every Quake 3 CTF map gets ceil(target_players / 2) team_CTF_redplayer and as many "
     "team_CTF_blueplayer team-begin spots, in the team's own base room, on supported floor, at least 64 units from "
     "each other and from every other entity, facing into the base, so a team's players do not telefrag on its one "
     "begin spot when a match starts; the team_CTF_*spawn respawn spots are kept as they are",
     "r1s-q3-ctf-team-begin"},
    {StageSwitch::r1s_sp_coop_starts, "r1s.sp_coop_starts", true, SwitchSubLane::sp,
     "R1-s entity safety (3): every Quake 1 and Quake 2 single-player map gets three info_player_coop beside its "
     "info_player_start, on supported floor, clear of each other and of every other entity, facing the start's way, "
     "so co-op players 2 to 4 do not telefrag on the single start",
     "r1s-sp-coop-starts"},
    {StageSwitch::r1s_sp_pickup_hull_support, "r1s.sp_pickup_hull_support", false, SwitchSubLane::sp,
     "R1-s entity safety (6): every pickup (weapon, health, armor) of a Quake 1 or Quake 2 single-player map is tested "
     "with the box the engine drops it with (data/profiles pickup_hulls, from the id source); a pickup whose box "
     "meets or touches a solid brush, hangs over a support edge or does not rest on the support under it moves to the "
     "nearest clear, fully supported station of its room (or, with none, is left out), so droptofloor never removes "
     "it at spawn",
     "r1s-sp-pickup-hull-support"},
    {StageSwitch::selftest_noop, "selftest.noop", false, SwitchSubLane::none,
     "D7 mechanism self-test: changes no generator behaviour; exercises the CLI, the stage query, the report "
     "\"switches\" object and the layout fingerprint",
     "d7-switches"},
}};

constexpr bool lower_letter(const char character) noexcept { return character >= 'a' && character <= 'z'; }

constexpr bool name_character(const char character) noexcept {
    return lower_letter(character) || (character >= '0' && character <= '9') || character == '_';
}

constexpr bool valid_name(const std::string_view name) noexcept {
    if (name.empty() || name.size() > kMaximumSwitchNameBytes)
        return false;
    std::size_t segments = 0;
    bool segment_start = true;
    for (const char character : name) {
        if (character == '.') {
            if (segment_start)
                return false;
            segment_start = true;
            continue;
        }
        if (segment_start) {
            if (!lower_letter(character))
                return false;
            ++segments;
            segment_start = false;
            continue;
        }
        if (!name_character(character))
            return false;
    }
    return !segment_start && segments >= 2;
}

constexpr bool valid_registry(const std::span<const StageSwitchDefinition> registry) noexcept {
    if (registry.size() > kMaximumStageSwitches)
        return false;
    for (std::size_t index = 0; index < registry.size(); ++index) {
        if (!valid_name(registry[index].name))
            return false;
        if (index > 0 && !(registry[index - 1].name < registry[index].name))
            return false;
    }
    return true;
}

constexpr bool registry_ids_match_indices() noexcept {
    for (std::size_t index = 0; index < kRegistry.size(); ++index) {
        if (static_cast<std::size_t>(kRegistry[index].id) != index)
            return false;
    }
    return true;
}

static_assert(valid_registry(kRegistry), "the stage switch registry must be valid, sorted by name and unique");
static_assert(registry_ids_match_indices(), "each StageSwitch enumerator must equal its registry index");
static_assert(valid_name("r1s.q1_intermission") && valid_name("a.b") && !valid_name("r1s") && !valid_name("R1s.x") &&
              !valid_name("r1s..x") && !valid_name(".x") && !valid_name("x.") && !valid_name("r1s.x-y") &&
              !valid_name("1a.b") && !valid_name("a._b"));

[[nodiscard]] std::uint64_t default_mask(const std::span<const StageSwitchDefinition> registry) noexcept {
    std::uint64_t mask = 0;
    for (std::size_t index = 0; index < registry.size(); ++index) {
        if (registry[index].default_on)
            mask |= std::uint64_t{1} << index;
    }
    return mask;
}

[[nodiscard]] std::string known_names(const std::span<const StageSwitchDefinition> registry) {
    if (registry.empty())
        return "none";
    std::string names;
    for (const StageSwitchDefinition &definition : registry) {
        if (!names.empty())
            names += ", ";
        names += definition.name;
    }
    return names;
}

} // namespace

bool is_valid_switch_name(const std::string_view name) noexcept { return valid_name(name); }

bool is_valid_switch_registry(const std::span<const StageSwitchDefinition> registry) noexcept {
    return valid_registry(registry);
}

std::span<const StageSwitchDefinition> stage_switch_registry() noexcept { return kRegistry; }

std::string to_string(const SwitchSubLane lane) {
    switch (lane) {
    case SwitchSubLane::none:
        return "none";
    case SwitchSubLane::mp:
        return "mp";
    case SwitchSubLane::sp:
        return "sp";
    case SwitchSubLane::both:
        return "both";
    }
    throw std::invalid_argument("unknown switch sub-lane");
}

StageSwitches::StageSwitches() noexcept
    : registry_(kRegistry), values_(default_mask(kRegistry)), defaults_(default_mask(kRegistry)) {}

StageSwitches::StageSwitches(const std::span<const StageSwitchDefinition> registry) : registry_(registry) {
    if (!valid_registry(registry))
        throw std::invalid_argument("stage switch registry must hold at most 64 valid dotted lowercase names, "
                                    "sorted and unique");
    defaults_ = default_mask(registry);
    values_ = defaults_;
}

bool StageSwitches::on(const StageSwitch id) const {
    if (registry_.data() != kRegistry.data() || registry_.size() != kRegistry.size())
        throw std::logic_error("typed stage switch queries need the generator's switch registry");
    const auto index = static_cast<std::size_t>(id);
    if (index >= registry_.size())
        throw std::logic_error("stage switch enumerator is outside the registry");
    return bit(index);
}

bool StageSwitches::on(const std::string_view name) const { return bit(index_of(name)); }

void StageSwitches::set(const std::string_view name, const bool value) {
    const std::uint64_t mask = std::uint64_t{1} << index_of(name);
    values_ = value ? (values_ | mask) : (values_ & ~mask);
}

bool StageSwitches::all_default() const noexcept { return values_ == defaults_; }

std::vector<StageSwitches::Entry> StageSwitches::non_default() const {
    std::vector<Entry> entries;
    for (std::size_t index = 0; index < registry_.size(); ++index) {
        const bool value = bit(index);
        if (value != registry_[index].default_on)
            entries.push_back({registry_[index].name, value});
    }
    return entries;
}

bool operator==(const StageSwitches &left, const StageSwitches &right) noexcept {
    return left.registry_.data() == right.registry_.data() && left.registry_.size() == right.registry_.size() &&
           left.values_ == right.values_;
}

std::size_t StageSwitches::index_of(const std::string_view name) const {
    for (std::size_t index = 0; index < registry_.size(); ++index) {
        if (registry_[index].name == name)
            return index;
    }
    throw std::invalid_argument("unknown switch '" + std::string(name) +
                                "'; known switches: " + known_names(registry_));
}

bool StageSwitches::bit(const std::size_t index) const noexcept { return ((values_ >> index) & 1U) != 0U; }

StageSwitches parse_stage_switch_assignments(const std::span<const std::string> assignments) {
    return parse_stage_switch_assignments(assignments, stage_switch_registry());
}

StageSwitches parse_stage_switch_assignments(const std::span<const std::string> assignments,
                                             const std::span<const StageSwitchDefinition> registry) {
    StageSwitches switches{registry};
    std::uint64_t seen = 0;
    for (const std::string &assignment : assignments) {
        const std::size_t separator = assignment.find('=');
        if (separator == std::string::npos || separator == 0)
            throw std::invalid_argument("--switch expects <name>=on|off, got '" + assignment + "'");
        const std::string_view name = std::string_view(assignment).substr(0, separator);
        const std::string_view value = std::string_view(assignment).substr(separator + 1);
        if (value != "on" && value != "off")
            throw std::invalid_argument("--switch expects <name>=on|off, got '" + assignment + "'");
        if (!valid_name(name))
            throw std::invalid_argument("--switch: unknown switch '" + std::string(name) +
                                        "' (names are dotted lowercase, for example r1s.q1_intermission); "
                                        "known switches: " +
                                        known_names(registry));
        std::size_t index = registry.size();
        for (std::size_t candidate = 0; candidate < registry.size(); ++candidate) {
            if (registry[candidate].name == name) {
                index = candidate;
                break;
            }
        }
        if (index == registry.size())
            throw std::invalid_argument("--switch: unknown switch '" + std::string(name) +
                                        "'; known switches: " + known_names(registry));
        const std::uint64_t mask = std::uint64_t{1} << index;
        if ((seen & mask) != 0U)
            throw std::invalid_argument("--switch " + std::string(name) + " is given more than once");
        seen |= mask;
        switches.set(name, value == "on");
    }
    return switches;
}

} // namespace qmg
