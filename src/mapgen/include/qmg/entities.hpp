#pragma once

#include "qmg/layout.hpp"

#include <cstddef>
#include <optional>
#include <string>

namespace qmg {

class EntityAllocator {
  public:
    void populate(Layout &layout) const;
    // populate() without the map-command start of change r1s-mp-player-start, for the compact Quake 2 motif
    // builder: it binds the allocator's placements to supported stations, refines the deathmatch spawns and adds
    // its own lights first, then calls add_multiplayer_player_start() last, so that no earlier placement moves
    // or is renumbered because of the start.
    void populate_before_station_binding(Layout &layout) const;
};

// R1-s cause r1s-mp-player-start (switch r1s.mp_player_start). When the switch is on and the layout is a Quake 1
// or Quake 2 multiplayer layout (deathmatch, duel or capture the flag), appends exactly one PlacementRole::
// player_start at a supported, clear station; otherwise does nothing. Throws std::runtime_error when no room has
// such a station, so a layout never silently lacks the start. Returns whether a start was added.
bool add_multiplayer_player_start(Layout &layout);

// R1-s cause r1s-sp-coop-starts (switch r1s.sp_coop_starts). The number of info_player_coop a single-player map gets:
// with the info_player_start they give the four players a Quake 2 co-op server holds (server/sv_init.c:334-337) a
// spot each.
inline constexpr std::size_t kSinglePlayerCoopStarts = 3;

// When the switch is on and the layout is a Quake 1 or Quake 2 single-player layout, appends exactly
// kSinglePlayerCoopStarts PlacementRole::coop_start placements beside its info_player_start: on supported floor in the
// start room (the rooms joined to it only when it has too few stations), clear of every solid, of each other and of
// every other entity, facing the start's way. Otherwise does nothing. Throws std::logic_error when the layout does
// not hold exactly one player_start or already holds a coop_start, and std::runtime_error when too few clear stations
// exist, so a map never silently lacks its co-op starts. Returns whether starts were added.
bool add_single_player_coop_starts(Layout &layout);

// R1-s cause r1s-q3-ctf-team-begin (switch r1s.q3_ctf_team_begin). The number of team-begin spots
// (PlacementRole::team_initial_spawn: team_CTF_redplayer / team_CTF_blueplayer) each team of a Quake 3 CTF map needs,
// ceil(max(target_players, 2) / 2): one per player of a full team, so no two players of a team that begins together
// share a spot (code/game/g_team.c:983-1023, SelectRandomTeamSpawnPoint; src/entities.cpp has the full reading).
[[nodiscard]] int quake3_ctf_team_begin_spots_per_team(const PromptSpec &spec) noexcept;

// When the switch is on and the layout is a Quake 3 capture-the-flag layout, appends PlacementRole::
// team_initial_spawn placements for each team, red first, until the team holds quake3_ctf_team_begin_spots_per_team()
// of them: in the room that holds the team's flag (its base; only the rooms of the team's half joined to it when the
// base has too few stations), on supported floor, clear of every solid, of each other and of every other entity,
// facing into the base. The team's existing begin spots and every other placement are kept as they are. Otherwise does
// nothing. Throws std::logic_error when a team has no flag, and std::runtime_error when too few clear stations exist,
// so a map never silently lacks its team-begin spots. Returns whether spots were added.
bool add_quake3_ctf_team_begin_spots(Layout &layout);

// R1-s cause r1s-monster-hull-support (switch r1s.monster_hull_support). What support_monster_hulls() did to a layout.
struct MonsterHullSupport {
    // False when the switch is off or the layout is not a Quake 1 or Quake 2 single-player layout (nothing was tested).
    bool applied{false};
    std::size_t kept{0};
    std::size_t moved{0};
    std::size_t left_out{0};
};

// When the switch is on and the layout is a Quake 1 or Quake 2 single-player layout, tests every monster placement, in
// order, with its class's spawn box (data/profiles monster_hulls, from the id source): a monster whose box at its slot
// meets a solid brush, hangs over a support edge, touches a wall or does not rest on the support under it moves,
// keeping its class, facing and id, to the nearest station of its room (16-unit lattice through the slot) where the box
// stands fully on one solid top (or the floor), its fall from spawn height meets no solid, and it keeps 16 units clear
// of every other entity's box; with no such station it is left out. Every other placement is kept as it is. Otherwise
// does nothing. src/entities.cpp holds the id-source reading.
MonsterHullSupport support_monster_hulls(Layout &layout);

// Why a monster placement's spawn box is unsafe under the rule of support_monster_hulls(), or nothing when it is safe
// (or the placement is not a monster). Reads the layout only; does not consult the switch.
[[nodiscard]] std::optional<std::string> monster_hull_problem(const Layout &layout, const Placement &monster);

// A monster placement's spawn box at its origin (origin + the class's mins and maxs). Throws std::invalid_argument for
// another role and std::logic_error when the class has no spawn box in the game profile.
[[nodiscard]] GridBounds monster_spawn_box(const Layout &layout, const Placement &monster);

// R1-s cause r1s-sp-pickup-hull-support (switch r1s.sp_pickup_hull_support). The placement roles the cause treats as
// pickups: the items droptofloor plants (weapon, health, armor).
[[nodiscard]] bool is_pickup_role(PlacementRole role) noexcept;

// What support_pickup_hulls() did to a layout.
struct PickupHullSupport {
    // False when the switch is off or the layout is not a Quake 1 or Quake 2 single-player layout (nothing was tested).
    bool applied{false};
    std::size_t kept{0};
    std::size_t moved{0};
    std::size_t left_out{0};
};

// When the switch is on and the layout is a Quake 1 or Quake 2 single-player layout, tests every pickup placement, in
// order, with the box the engine drops it with (data/profiles pickup_hulls, from the id source): a pickup whose box,
// from its spawn height (Quake 1 raises it 6 units first) down to resting on the support under it, meets or touches a
// solid brush, hangs over a support edge, comes within 8 units of a wall, or whose origin is not kPickupSpawnLift (24)
// above that support, moves, keeping its role, class and id, to the nearest station of its room (16-unit lattice
// through the slot) where the box stands fully on one solid top (or the floor), its fall meets no solid, and it keeps
// 16 units clear of every other entity's box; with no such station it is left out. Every other placement is kept as it
// is. Otherwise does nothing. src/entities.cpp holds the id-source reading.
PickupHullSupport support_pickup_hulls(Layout &layout);

// Why a pickup placement's box is unsafe under the rule of support_pickup_hulls(), or nothing when it is safe (or the
// placement is not a pickup of a Quake 1 or Quake 2 layout). Reads the layout only; does not consult the switch.
[[nodiscard]] std::optional<std::string> pickup_hull_problem(const Layout &layout, const Placement &pickup);

// A pickup placement's drop box at its origin (origin + the class's mins and maxs, before the engine's spawn raise).
// Throws std::invalid_argument for a role that is not a pickup and std::logic_error when the class has no drop box in
// the game profile.
[[nodiscard]] GridBounds pickup_drop_box(const Layout &layout, const Placement &pickup);

// True for the map-command info_player_start of a Quake 1 or Quake 2 multiplayer layout: a PlacementRole::
// player_start whose layout mode is not single-player. Deathmatch, duel and CTF spawn selection never read it
// (the proof is in src/entities.cpp), so analyses of match play leave it out.
[[nodiscard]] bool is_multiplayer_map_start(const Layout &layout, const Placement &placement) noexcept;

} // namespace qmg
