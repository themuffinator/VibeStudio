#!/usr/bin/env python3
"""Build (once) and check the Phase-2 fixed sets for the benchmark (stdlib only).

The fixed sets of plan section 8.3 are frozen once. This script is the
recorded procedure that produced them. It never rewrites a frozen file:

  python -B tools/benchmark/freeze_sets.py            # --check (default): rebuild in memory, compare
  python -B tools/benchmark/freeze_sets.py --write    # write files that do not exist yet; refuse to
                                                      # replace any existing file whose bytes differ

Outputs (all under tools/benchmark/):
  rc1-v1.json             BENCH-v1: the 138 draft-v0 cases verbatim plus new cases
  contra-v1.json          CONTRA-v1: the 34 contradictory ids (an id list over BENCH-v1)
  parse-matrix-v1.json    PARSE-MATRIX-v1: a data table for a later C++ normalization test
  portfolios/*.json       MP-PORTFOLIO, SPAN-PORTFOLIO, LATTICE-PORTFOLIO, SPLICE-PORTFOLIO
  sealed.json             the seal list (D26)
  heroes.json             HEROES: schema and an empty list (no seeds chosen)
  manifests.json          sha256 registry of every frozen set

The portfolio manifests are reconstructed from the local, git-ignored records
under out/mp-portfolio, out/span-portfolio, out/lattice-portfolio and
out/splice-portfolio. When those records are absent (for example in a clean
clone), --check skips the portfolio rebuild and says so.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
BENCH = REPO / "tools" / "benchmark"
DRAFT = BENCH / "draft-v0.json"
DRAFT_SHA256 = "056889a0bdf7f74fad66c5afc493d3da9217becd834b0af6774954c54edea3d7"
FROZEN_ON = "2026-10-15"
CHANGE_ID = "x1-bench"
DRAFT_KEYS = ["id", "target", "game", "mode", "category", "prompt", "seed", "expected_requests", "notes"]
NEW_SEED_BASE = 2000

# --------------------------------------------------------------------------- paths, tiers, modules

# The 17 game x mode paths (plan section 7), with the request vocabulary of draft-v0
# ("mode <token>") and the GameMode token proposed by D4 for the primary mode.
PATHS = {
    "q1-sp": ("q1", "sp", "single_player", "single_player"),
    "q1-coop": ("q1", "coop", "coop", "cooperative"),
    "q1-dm": ("q1", "dm", "deathmatch", "deathmatch"),
    "q1-duel": ("q1", "duel", "duel", "duel"),
    "q1-tdm": ("q1", "tdm", "team_deathmatch", "team_deathmatch"),
    "q1-ctf-threewave": ("q1", "ctf-threewave", "threewave_ctf", "capture_the_flag"),
    "q2-sp": ("q2", "sp", "single_player", "single_player"),
    "q2-coop": ("q2", "coop", "coop", "cooperative"),
    "q2-dm": ("q2", "dm", "deathmatch", "deathmatch"),
    "q2-duel": ("q2", "duel", "duel", "duel"),
    "q2-tdm": ("q2", "tdm", "team_deathmatch", "team_deathmatch"),
    "q2-ctf": ("q2", "ctf", "capture_the_flag", "capture_the_flag"),
    "q3-botarena": ("q3", "botarena", "bot_arena", "bot_arena"),
    "q3-ffa": ("q3", "ffa", "deathmatch", "deathmatch"),
    "q3-tourney": ("q3", "tourney", "duel", "duel"),
    "q3-tdm": ("q3", "tdm", "team_deathmatch", "team_deathmatch"),
    "q3-ctf": ("q3", "ctf", "capture_the_flag", "capture_the_flag"),
}
GAME_NAME = {"q1": "quake1", "q2": "quake2", "q3": "quake3"}

# Plan section 12 recommendations, as tiers. "conditional" = keep if the user supplies the
# input; "deferred" = decided at an E1 checkpoint (default defer); "dropped-claim" = not
# claimed separately (Q1 TDM) or likely dropped (Q1 Threewave CTF). Unclaimed paths' cases
# stay in BENCH-v1 as honesty probes (D11(6)).
CLAIMED = {
    "q2-dm": "committed", "q2-duel": "committed", "q3-ffa": "committed", "q3-tourney": "committed",
    "q3-tdm": "committed", "q3-ctf": "committed",
    "q2-ctf": "conditional", "q2-tdm": "conditional", "q3-botarena": "conditional",
    "q1-dm": "conditional", "q1-duel": "conditional",
    "q1-sp": "deferred", "q1-coop": "deferred", "q2-sp": "deferred", "q2-coop": "deferred",
    "q1-tdm": "dropped-claim", "q1-ctf-threewave": "dropped-claim",
}

# Expected game-module tokens (rank 1 / D4). The Q1 token does not choose between the 1996
# progs and the 2021 re-release QuakeC (D13 is pending); Q2 tokens follow the recommended
# D14 (a); Q3 follows the recommended D15 (1.32b).
MODULES = {
    "q1-id1": "Quake id1 progs (1996 or 2021 re-release; D13 pending)",
    "q1-threewave-5.0": "Threewave CTF 5.0 (quake-rerelease-qc-main/quakec_ctf)",
    "q2-baseq2-3.19": "Quake II 3.19 baseq2 game module",
    "q2-ctf-3.19": "Quake II 3.19 ctf game module (Threewave CTF 1.09b)",
    "q3-baseq3-1.32b": "Quake III Arena 1.32b baseq3 qagame",
}


def module_for(path: str) -> str:
    if path == "q1-ctf-threewave":
        return "q1-threewave-5.0"
    if path == "q2-ctf":
        return "q2-ctf-3.19"
    return {"q1": "q1-id1", "q2": "q2-baseq2-3.19", "q3": "q3-baseq3-1.32b"}[PATHS[path][0]]


# --------------------------------------------------------------------------- conflicts

CONFLICT_CLASSES = {
    "verticality_contradiction": "Two contradictory verticality requests (for example flat vs extreme verticality).",
    "size_room_count_contradiction": "A size word contradicts an explicit room count (for example tiny vs 32 rooms).",
    "mode_player_count_contradiction": "A requested player count lies outside the named mode's player range.",
    "capacity_contradiction": "A player count does not fit the requested room count or size.",
    "mode_mode_contradiction": "Two incompatible game modes are named.",
    "mode_content_contradiction": "A mode is named together with a request that removes its required content.",
    "game_content_unsupported": "Content is requested that the stock game does not have.",
}

RESOLUTION_RULES = {
    "mode_precedence": "The primary mode is the case's path mode (the draft target). The other named mode is "
                       "reported unmet with a typed finding. The parser rule that produces this is rank 1's R1-c.",
    "first_term": "Of two contradictory qualifiers of the same field, the first in prompt order is kept; the "
                  "other is reported unmet with a typed finding.",
    "explicit_count": "An explicit room count beats a size word; the size word is reported unmet.",
    "mode_player_range": "The player count is clamped into the mode's player range (duel and tourney 2; co-op at "
                         "least 2; team modes at least 4; CTF at least 2), with a typed finding.",
    "capacity": "An explicit room count is kept; the player count is clamped to what that room count can hold, "
                "with a typed finding. A count that fits is kept and only a warning finding is raised.",
    "mode_content": "The mode's required entities are placed; a request that removes them is reported unmet.",
    "game_content": "Content the game does not have is reported unsupported; the map is generated without it.",
    "server_setting": "A request that is a server setting, not map content (bot presence), is reported as such.",
}

# id -> list of (class, [span texts]); spans are located in the prompt by first occurrence.
CONTRA = {
    "q1-sp-c1": [("verticality_contradiction", ["flat", "extreme verticality"])],
    "q1-sp-c2": [("size_room_count_contradiction", ["tiny", "32 rooms"])],
    "q1-coop-c1": [("mode_player_count_contradiction", ["coop", "exactly 1 player"])],
    "q1-coop-c2": [("capacity_contradiction", ["16 players", "2 rooms"])],
    "q1-dm-c1": [("verticality_contradiction", ["flat", "extreme verticality"])],
    "q1-dm-c2": [("capacity_contradiction", ["32 players", "2 rooms"])],
    "q1-duel-c1": [("mode_player_count_contradiction", ["duel", "16 players"])],
    "q1-duel-c2": [("size_room_count_contradiction", ["tiny", "32 rooms"])],
    "q1-tdm-c1": [("mode_player_count_contradiction", ["team deathmatch", "2 players"])],
    "q1-tdm-c2": [("mode_mode_contradiction", ["team deathmatch", "single-player campaign"])],
    "q1-ctf-threewave-c1": [("mode_mode_contradiction", ["capture the flag", "duel"]),
                            ("mode_content_contradiction", ["capture the flag", "no flags"])],
    "q1-ctf-threewave-c2": [("mode_mode_contradiction", ["single-player", "CTF"])],
    "q2-sp-c1": [("mode_mode_contradiction", ["single player", "deathmatch"]),
                 ("mode_player_count_contradiction", ["single player", "8 players"])],
    "q2-sp-c2": [("verticality_contradiction", ["flat", "extreme verticality"])],
    "q2-coop-c1": [("mode_player_count_contradiction", ["coop", "1 player"])],
    "q2-coop-c2": [("mode_mode_contradiction", ["co-op", "deathmatch"])],
    "q2-dm-c1": [("capacity_contradiction", ["32 players", "tiny 2-room"])],
    "q2-dm-c2": [("verticality_contradiction", ["flat", "extreme verticality"])],
    "q2-duel-c1": [("mode_player_count_contradiction", ["duel", "16 players"])],
    "q2-duel-c2": [("mode_mode_contradiction", ["1v1", "single player"])],
    "q2-tdm-c1": [("mode_player_count_contradiction", ["team deathmatch", "2 players"])],
    "q2-tdm-c2": [("mode_player_count_contradiction", ["team deathmatch", "three teams of one player"]),
                  ("capacity_contradiction", ["three teams of one player", "32-room"])],
    "q2-ctf-c1": [("mode_player_count_contradiction", ["CTF", "one player"]),
                  ("mode_content_contradiction", ["CTF", "no flags"])],
    "q2-ctf-c2": [("capacity_contradiction", ["tiny 2-room", "32 players"])],
    "q3-botarena-c1": [("mode_content_contradiction", ["bot arena", "no bots"])],
    "q3-botarena-c2": [("game_content_unsupported", ["Quake 3", "monsters"])],
    "q3-ffa-c1": [("capacity_contradiction", ["32 players", "2-room"])],
    "q3-ffa-c2": [("verticality_contradiction", ["flat", "extreme verticality"])],
    "q3-tourney-c1": [("mode_player_count_contradiction", ["tourney", "16 players"])],
    "q3-tourney-c2": [("mode_mode_contradiction", ["tourney", "campaign"]),
                      ("game_content_unsupported", ["Quake 3", "monsters"])],
    "q3-tdm-c1": [("mode_player_count_contradiction", ["team deathmatch", "2 players"])],
    "q3-tdm-c2": [("mode_mode_contradiction", ["TDM", "single-player campaign"]),
                  ("game_content_unsupported", ["Quake 3", "monsters"])],
    "q3-ctf-c1": [("mode_player_count_contradiction", ["CTF", "one player"]),
                  ("mode_content_contradiction", ["CTF", "no flags"])],
    "q3-ctf-c2": [("mode_mode_contradiction", ["capture the flag", "duel"]),
                  ("capacity_contradiction", ["2 players", "32 rooms"])],
}

# id -> (rules, unmet expected requests that need a typed finding)
RESOLUTIONS = {
    "q1-sp-c1": (["first_term"], ["extreme verticality"]),
    "q1-sp-c2": (["explicit_count"], ["tiny"]),
    "q1-coop-c1": (["mode_player_range"], ["1 players"]),
    "q1-coop-c2": (["capacity"], ["16 players"]),
    "q1-dm-c1": (["first_term"], ["extreme verticality"]),
    "q1-dm-c2": (["capacity"], ["32 players"]),
    "q1-duel-c1": (["mode_player_range"], ["16 players"]),
    "q1-duel-c2": (["explicit_count"], ["tiny"]),
    "q1-tdm-c1": (["mode_player_range"], ["2 players"]),
    "q1-tdm-c2": (["mode_precedence"], ["mode single_player", "monsters"]),
    "q1-ctf-threewave-c1": (["mode_precedence", "mode_content"], ["mode duel", "no flags"]),
    "q1-ctf-threewave-c2": (["mode_precedence"], ["mode single_player", "monsters"]),
    "q2-sp-c1": (["mode_precedence", "mode_player_range"], ["mode deathmatch", "8 players"]),
    "q2-sp-c2": (["first_term"], ["extreme verticality"]),
    "q2-coop-c1": (["mode_player_range"], ["1 players"]),
    "q2-coop-c2": (["mode_precedence"], ["mode deathmatch"]),
    "q2-dm-c1": (["capacity"], ["32 players"]),
    "q2-dm-c2": (["first_term"], ["extreme verticality"]),
    "q2-duel-c1": (["mode_player_range"], ["16 players"]),
    "q2-duel-c2": (["mode_precedence"], ["mode single_player", "monsters"]),
    "q2-tdm-c1": (["mode_player_range"], ["2 players"]),
    "q2-tdm-c2": (["mode_player_range", "capacity"], ["3 players"]),
    "q2-ctf-c1": (["mode_player_range", "mode_content"], ["1 players", "no flags"]),
    "q2-ctf-c2": (["capacity"], ["32 players"]),
    "q3-botarena-c1": (["server_setting", "mode_content"], ["no bots"]),
    "q3-botarena-c2": (["mode_precedence", "game_content"], ["mode single_player", "monsters"]),
    "q3-ffa-c1": (["capacity"], ["32 players"]),
    "q3-ffa-c2": (["first_term"], ["extreme verticality"]),
    "q3-tourney-c1": (["mode_player_range"], ["16 players"]),
    "q3-tourney-c2": (["mode_precedence", "game_content"], ["mode single_player", "monsters"]),
    "q3-tdm-c1": (["mode_player_range"], ["2 players"]),
    "q3-tdm-c2": (["mode_precedence", "game_content"], ["mode single_player", "monsters"]),
    "q3-ctf-c1": (["mode_player_range", "mode_content"], ["1 players", "no flags"]),
    "q3-ctf-c2": (["mode_precedence", "capacity"], ["mode duel"]),
}


def utf8_span(prompt: str, text: str) -> dict:
    index = prompt.find(text)
    if index < 0:
        raise SystemExit(f"span {text!r} not in prompt {prompt!r}")
    start = len(prompt[:index].encode("utf-8"))
    end = start + len(text.encode("utf-8"))
    return {"start": start, "end": end, "text": text}


# --------------------------------------------------------------------------- new cases

MODE_PHRASES = {
    "q1-sp": ["Quake 1 single-player level", "Quake single player map", "Q1 single-player episode level"],
    "q1-coop": ["Quake 1 co-op map", "Quake 1 cooperative level", "Quake co-op level"],
    "q1-dm": ["Quake 1 deathmatch map", "Quake deathmatch arena", "Q1 deathmatch map"],
    "q1-duel": ["Quake 1 duel map", "Quake 1 duel arena", "Quake one-on-one duel map"],
    "q1-tdm": ["Quake 1 team deathmatch map", "Quake 1 team deathmatch arena", "Quake TDM map"],
    "q1-ctf-threewave": ["Quake 1 Threewave CTF map", "Quake 1 capture the flag map for Threewave CTF",
                         "Threewave CTF map for Quake 1"],
    "q2-sp": ["Quake 2 single-player level", "Quake 2 single player unit map", "Q2 single-player base level"],
    "q2-coop": ["Quake 2 co-op map", "Quake 2 cooperative level", "Q2 co-op level"],
    "q2-dm": ["Quake 2 deathmatch map", "Quake 2 deathmatch arena", "Q2 deathmatch map"],
    "q2-duel": ["Quake 2 duel map", "Quake 2 duel arena", "Q2 one-on-one duel map"],
    "q2-tdm": ["Quake 2 team deathmatch map", "Quake 2 team deathmatch arena", "Q2 TDM map"],
    "q2-ctf": ["Quake 2 capture the flag map", "Quake 2 CTF map", "Q2 capture the flag level"],
    "q3-botarena": ["Quake 3 bot arena map", "Quake 3 bot arena level", "Q3 bot arena"],
    "q3-ffa": ["Quake 3 free for all map", "Quake 3 FFA arena", "Q3 free for all arena"],
    "q3-tourney": ["Quake 3 tourney map", "Quake 3 tournament map", "Q3 tourney arena"],
    "q3-tdm": ["Quake 3 team deathmatch map", "Quake 3 TDM map", "Q3 team deathmatch arena"],
    "q3-ctf": ["Quake 3 capture the flag map", "Quake 3 CTF map", "Q3 capture the flag arena"],
}

MECHANISMS = {
    "lift": ["with a lift up to a raised walkway", "where a lift connects the lower hall to an upper balcony",
             "with an elevator platform between two floors", "built around a central lift shaft",
             "with a lift rising to a sniper ledge", "where a moving platform lifts players to the upper deck",
             "with two lifts serving a raised gallery", "with a lift beside the main stairwell"],
    "door": ["with sliding doors between the main rooms", "where a heavy door guards the armory",
             "with doors that open into each hall", "with a door separating the hub from the side rooms",
             "with a pair of doors at the entrance hall", "where blast doors split the corridors",
             "with a door leading down to the lower vault", "with doors on every side passage"],
    "teleporter": ["with a teleporter linking the two far ends", "where a teleporter returns players from the pit",
                   "with a teleporter pair across the map", "with a one-way teleporter to the upper floor",
                   "featuring a teleporter in the central room", "with a teleporter hidden in a side alcove",
                   "with teleporters connecting three areas", "with a teleporter exit overlooking the main hall"],
    "water": ["with a flooded water channel", "with a deep water pool in the middle",
              "with water corridors under the main floor", "with a submerged water passage",
              "around a water reservoir", "with water pouring into a lower basin",
              "with a shallow water moat", "with an underwater route between rooms"],
    "slime": ["with slime pits", "with a slime-filled trench", "with toxic slime channels", "around a slime vat",
              "with a slime moat", "with slime pools on the lower level", "with a slime sewer",
              "with a slime pit trap"],
    "lava": ["with lava pits", "with a lava river under a bridge", "around a lava lake", "with lava channels",
             "with a lava moat around the center", "with lava pools in the lower cavern",
             "with a lava trench beside the main route", "with a lava fall in the central hall"],
    "ramp": ["with ramps between the floor levels", "with a long ramp up to the upper ring",
             "with sloped ramps instead of stairs", "with a spiral of ramps",
             "with a wide ramp into the central pit", "with a ramp leading to the balcony",
             "with ramps on both sides of the hall", "with a ramp down to the basement"],
    "drop": ["with a drop down from the upper ledge to the main floor", "with one-way drops between levels",
             "with a drop-down shortcut into the pit", "where players drop from the catwalk to the lower hall",
             "with a ledge to drop off into the courtyard", "with a drop through a hole in the floor",
             "with drop-down routes from the balcony", "with a high drop into the basement"],
    "jump pad": ["with a jump pad up to the rail ledge", "with jump pads launching players to the upper deck",
                 "with a jump pad in the courtyard", "with a pair of jump pads",
                 "with a jump pad over the central pit", "with a jump pad to the armor platform",
                 "where a jump pad lifts players to the top floor", "with jump pads on both sides"],
    "launch pad": ["with a launch pad that throws players across the map",
                   "with a launch pad flinging players over the gap",
                   "with a long-distance launch pad between towers", "with launch pads crossing the central void",
                   "with a launch pad from the base to the middle",
                   "where a launch pad sends players across the courtyard",
                   "with a launch pad to the far platform", "with launch pads over the open atrium"],
}
GAME_MECHANISMS = {
    "q1": ["lift", "door", "teleporter", "water", "slime", "lava", "ramp", "drop"],
    "q2": ["lift", "door", "teleporter", "water", "slime", "lava", "ramp", "drop"],
    "q3": ["jump pad", "launch pad", "teleporter", "lift", "water", "lava", "ramp", "drop"],
}
GAME_PATHS = {g: [p for p in PATHS if PATHS[p][0] == g] for g in ("q1", "q2", "q3")}

# (path, prompt, extra expected requests, notes)
PARSE_CASES = [
    ("q2-dm", "Quake 2 deathmatch map for eight players", ["8 players"], "Number word: eight."),
    ("q3-ffa", "a Quake 3 free for all map for 16 people", ["16 players"], "Number with 'people'."),
    ("q2-dm", "Quake 2 deathmatch map for 2-8 players", ["2-8 players"], "Range with a hyphen."),
    ("q3-ffa", "Quake 3 FFA map for 2 to 8 players", ["2-8 players"], "Range with 'to'."),
    ("q1-dm", "Quake 1 deathmatch map for 2–8 players", ["2-8 players"],
     "Range with an en dash (U+2013)."),
    ("q2-duel", "Quake 2 duel map for two players", ["2 players"], "Number word: two."),
    ("q2-dm", "a q2dm1 style map for Quake 2 deathmatch", ["style reference q2dm1"],
     "Map-name reference is a style hint; it never switches game or mode."),
    ("q2-dm", "q3dm17 remake for Quake 2", ["style reference q3dm17"],
     "Map-name reference to a Quake 3 map must not switch the game. No mode is named; the path default is "
     "deathmatch, so no mode request is listed."),
    ("q1-dm", "Quake 1 deathmatch map with no loops, just a straight run between rooms", ["no loop route"],
     "Negation: no loops."),
    ("q2-duel", "Quake 2 duel map that is not vertical at all, everything on one floor", ["flat"],
     "Negation: not vertical."),
    ("q3-ffa", "Quake 3 FFA map in an old temple with no water", ["no water", "theme temple"],
     "Negation: no water; 'water' must not select the waterworks theme."),
    ("q2-dm", "Quake 2 deathmatch map in a gothic cathedral with a database archive in the crypt",
     ["theme gothic"], "False positive: 'database' must not select the techbase theme."),
    ("q3-ctf", "Quake 3 CTF map with two bases inside a medieval castle", ["theme medieval", "flags", "team spawns"],
     "False positive: 'two bases' must not select the techbase theme or a 2-player count."),
    ("q1-dm", "Quake 1 deathmatch map in a medieval keep with plenty of open space", ["theme medieval"],
     "False positive: 'open space' must not select the space theme."),
]

ITEM_CASES = [
    ("q1-dm", "Quake 1 deathmatch map with a super shotgun near every spawn", ["super shotgun"]),
    ("q1-duel", "Quake 1 duel map with a rocket launcher and a lightning gun on opposite sides",
     ["rocket launcher", "lightning gun"]),
    ("q1-dm", "Quake 1 deathmatch map with quad damage on a high ledge and a pentagram of protection in a side room",
     ["quad damage", "pentagram of protection"]),
    ("q1-tdm", "Quake 1 team deathmatch map with red armor and yellow armor for each team",
     ["red armor", "yellow armor"]),
    ("q1-sp", "Quake 1 single-player level with a nailgun, a super nailgun and a ring of shadows",
     ["nailgun", "super nailgun", "ring of shadows"]),
    ("q1-coop", "Quake 1 co-op map with a grenade launcher, green armor and a biosuit",
     ["grenade launcher", "green armor", "biosuit"]),
    ("q1-duel", "Quake 1 duel map with a mega health and the thunderbolt", ["mega health", "thunderbolt"]),
    ("q1-ctf-threewave", "Quake 1 Threewave CTF map with a grenade launcher in each base",
     ["grenade launcher per base", "flags", "team spawns"]),
    ("q2-dm", "Quake 2 deathmatch map with a railgun, a chaingun and a hyperblaster",
     ["railgun", "chaingun", "hyperblaster"]),
    ("q2-duel", "Quake 2 duel map with a super shotgun, a machinegun and combat armor",
     ["super shotgun", "machinegun", "combat armor"]),
    ("q2-dm", "Quake 2 deathmatch map with a BFG10K in a hard-to-reach alcove", ["bfg10k"]),
    ("q2-tdm", "Quake 2 team deathmatch map with quad damage and invulnerability",
     ["quad damage", "invulnerability"]),
    ("q2-ctf", "Quake 2 CTF map with a power shield and jacket armor near each flag",
     ["power shield", "jacket armor", "flags", "team spawns"]),
    ("q2-sp", "Quake 2 single-player level with a bandolier, an ammo pack and adrenaline",
     ["bandolier", "ammo pack", "adrenaline", "exit"]),
    ("q2-coop", "Quake 2 co-op map with a rebreather, a silencer and body armor",
     ["rebreather", "silencer", "body armor", "coop spawns"]),
    ("q2-duel", "Quake 2 duel map with a mega health and a rocket launcher", ["mega health", "rocket launcher"]),
    ("q3-ffa", "Quake 3 FFA map with a plasma gun, a lightning gun and a railgun",
     ["plasma gun", "lightning gun", "railgun"]),
    ("q3-tourney", "Quake 3 tourney map with red armor, yellow armor and a mega health",
     ["red armor", "yellow armor", "mega health"]),
    ("q3-ffa", "Quake 3 free for all map with a BFG and quad damage", ["bfg", "quad damage"]),
    ("q3-tdm", "Quake 3 team deathmatch map with regeneration and haste powerups", ["regeneration", "haste"]),
    ("q3-ctf", "Quake 3 CTF map with a battle suit and invisibility in the middle",
     ["battle suit", "invisibility", "flags", "team spawns"]),
    ("q3-botarena", "Quake 3 bot arena map with a shotgun, a grenade launcher and a medkit",
     ["shotgun", "grenade launcher", "medkit"]),
    ("q3-ffa", "Quake 3 FFA map with flight and a personal teleporter", ["flight", "personal teleporter"]),
    ("q3-tourney", "Quake 3 tourney map with a rocket launcher and a machinegun", ["rocket launcher", "machinegun"]),
]

THREEWAVE_CASES = [
    ("q1-ctf-threewave", "Quake 1 Threewave CTF map with mirrored bases",
     ["flags", "team spawns", "mirrored symmetry"]),
    ("q1-ctf-threewave", "Threewave capture the flag map for Quake 1, 4v4", ["4v4 players", "flags", "team spawns"]),
    ("q1-ctf-threewave", "a Quake 1 Threewave CTF level with a wide midfield and a flag room at each end",
     ["flags", "team spawns"]),
    ("q1-ctf-threewave", "Quake 1 CTF for Threewave 5.0 with runes spread around the midfield",
     ["flags", "team spawns", "runes"]),
]

SIZE_CASES = [
    ("q1-dm", "a small Quake 1 deathmatch map", ["size small"]),
    ("q1-sp", "a large Quake 1 single-player level", ["size large", "exit"]),
    ("q2-dm", "a compact Quake 2 deathmatch map", ["size small"]),
    ("q2-ctf", "a huge Quake 2 CTF map", ["size large", "flags", "team spawns"]),
    ("q3-tourney", "a small Quake 3 tourney map", ["size small"]),
    ("q3-ffa", "a large Quake 3 FFA map", ["size large"]),
]

MOOD_CASES = [
    ("q1-sp", "a grim, oppressive Quake 1 single-player level", ["mood grim", "mood oppressive", "exit"]),
    ("q1-dm", "an eerie Quake 1 deathmatch map", ["mood eerie"]),
    ("q2-sp", "a tense Quake 2 single-player level", ["mood tense", "exit"]),
    ("q2-dm", "a calm but eerie Quake 2 deathmatch map", ["mood calm", "mood eerie"]),
    ("q3-ffa", "a frantic Quake 3 FFA arena", ["mood frantic"]),
    ("q3-ctf", "a grim Quake 3 CTF map", ["mood grim", "flags", "team spawns"]),
]

FORBID_CASES = [
    ("q1-dm", "Quake 1 deathmatch map with no quad damage", ["no quad damage"]),
    ("q1-duel", "Quake 1 duel map without any powerups", ["no powerups"]),
    ("q1-tdm", "Quake 1 team deathmatch map with no rocket launcher", ["no rocket launcher"]),
    ("q2-dm", "Quake 2 deathmatch map with no BFG10K", ["no bfg10k"]),
    ("q2-duel", "Quake 2 duel map with no powerups", ["no powerups"]),
    ("q2-tdm", "Quake 2 team deathmatch map with no railgun", ["no railgun"]),
    ("q3-ffa", "Quake 3 FFA map with no BFG", ["no bfg"]),
    ("q3-tourney", "Quake 3 tourney map without powerups", ["no powerups"]),
    ("q3-ctf", "Quake 3 CTF map with no railgun", ["no railgun", "flags", "team spawns"]),
]

LANDMARK_CASES = [
    ("q1-sp", "Quake 1 single-player level with a giant statue landmark in the main hall", ["landmark statue", "exit"]),
    ("q1-dm", "Quake 1 deathmatch map built around a central tower landmark", ["landmark tower"]),
    ("q2-dm", "Quake 2 deathmatch map with a central furnace landmark", ["landmark furnace"]),
    ("q2-sp", "Quake 2 single-player level with a huge reactor core as its landmark", ["landmark reactor", "exit"]),
    ("q3-ffa", "Quake 3 FFA map with a pillar cluster landmark in the middle", ["landmark pillar cluster"]),
    ("q3-ctf", "Quake 3 CTF map with a stone monolith landmark at midfield",
     ["landmark monolith", "flags", "team spawns"]),
]

SP_CASES = [
    ("q1-sp", "Quake 1 single-player level with a silver key door and a gold key door before the exit",
     ["silver key", "gold key", "exit"]),
    ("q1-sp", "Quake 1 single-player map with three secret areas and a slipgate exit", ["secret area", "exit"]),
    ("q1-sp", "Quake 1 single-player level where the gold key opens the way to the exit, with one secret area",
     ["gold key", "secret area", "exit"]),
    ("q1-sp", "Quake 1 single-player level with a silver key, a secret area behind a shootable wall, and an exit",
     ["silver key", "secret area", "exit"]),
    ("q1-coop", "Quake 1 co-op level with a gold key door and two secret areas",
     ["gold key", "secret area", "coop spawns"]),
    ("q1-coop", "Quake 1 cooperative map with a silver key, a secret area and an exit",
     ["silver key", "secret area", "exit", "coop spawns"]),
    ("q2-sp", "Quake 2 single-player level with a blue keycard door, a secret area and an exit",
     ["blue keycard", "secret area", "exit"]),
    ("q2-sp", "Quake 2 single player unit map with a red keycard and a computer objective to complete before the exit",
     ["red keycard", "objective", "exit"]),
    ("q2-sp", "Quake 2 single-player level with two mission objectives and a secret area",
     ["objective", "secret area", "exit"]),
    ("q2-sp", "Quake 2 single-player base with a blue keycard, a red keycard and an exit",
     ["blue keycard", "red keycard", "exit"]),
    ("q2-coop", "Quake 2 co-op level with a blue keycard and a secret area",
     ["blue keycard", "secret area", "coop spawns"]),
    ("q2-coop", "Quake 2 cooperative map with an objective and an exit", ["objective", "exit", "coop spawns"]),
]


def base_requests(path: str, include_mode: bool = True) -> list:
    game, _, mode_request, _ = PATHS[path]
    out = [f"game {GAME_NAME[game]}"]
    if include_mode:
        out.append(f"mode {mode_request}")
    return out


def new_cases() -> list:
    rows = []  # (id, path, category, prompt, requests, notes, extra fields)
    counters = {}

    def add(path, category, prompt, extra, notes="", fields=None, include_mode=True, tag=None):
        key = (path, tag or category)
        counters[key] = counters.get(key, 0) + 1
        cid = f"{path}-x-{tag or category}-{counters[key]}"
        requests = base_requests(path, include_mode)
        requests += [r for r in extra if r not in requests]
        rows.append((cid, path, category, prompt, requests, notes, fields or {}))

    for path, prompt, extra, notes in PARSE_CASES:
        add(path, "parse", prompt, extra, notes, include_mode="No mode is named" not in notes)
    for path, prompt, extra in ITEM_CASES:
        add(path, "item", prompt, extra)
    for game in ("q1", "q2", "q3"):
        paths = GAME_PATHS[game]
        for m_index, mechanism in enumerate(GAME_MECHANISMS[game]):
            for k, phrase in enumerate(MECHANISMS[mechanism]):
                path = paths[(k + m_index) % len(paths)]
                variants = MODE_PHRASES[path]
                prompt = f"{variants[(k + m_index) % len(variants)]} {phrase}"
                add(path, "mechanism", prompt, [mechanism], fields={"mechanism": mechanism},
                    tag="mech-" + mechanism.replace(" ", ""))
    for path, prompt, extra in THREEWAVE_CASES:
        add(path, "threewave", prompt, extra, "Q1 CTF prompt that names Threewave.")
    for path, prompt, extra in SIZE_CASES:
        add(path, "size", prompt, extra, "Size bands are D3 (pending); provisional bands small 4-6, "
            "medium 7-10, large 11-16 rooms.")
    for path, prompt, extra in MOOD_CASES:
        add(path, "mood", prompt, extra, "Mood word list is D3 (pending).")
    for path, prompt, extra in FORBID_CASES:
        add(path, "forbidden", prompt, extra, "Forbidden-item request.")
    for path, prompt, extra in LANDMARK_CASES:
        add(path, "landmark", prompt, extra, "Landmark request (LandmarkSpec, D8, Phase 4).")
    for path, prompt, extra in SP_CASES:
        add(path, "sp-progression", prompt, extra, "SP keys, secrets or objectives.")

    cases = []
    for index, (cid, path, category, prompt, requests, notes, fields) in enumerate(rows):
        game, mode = PATHS[path][0], PATHS[path][1]
        case = {"id": cid, "target": path, "game": game, "mode": mode, "category": category, "prompt": prompt,
                "seed": NEW_SEED_BASE + index, "expected_requests": requests, "notes": notes}
        case.update(case_extras(path, cid, prompt, None))
        case.update(fields)
        case["added_in"] = "BENCH-v1"
        cases.append(case)
    return cases


def case_extras(path: str, cid: str, prompt: str, category) -> dict:
    conflicts = []
    resolution = None
    if cid in CONTRA:
        for cls, texts in CONTRA[cid]:
            assert cls in CONFLICT_CLASSES, cls
            conflicts.append({"class": cls, "span": [utf8_span(prompt, t) for t in texts]})
        rules, unmet = RESOLUTIONS[cid]
        for rule in rules:
            assert rule in RESOLUTION_RULES, rule
        game, _, _, gamemode = PATHS[path]
        resolution = {"action": "generate",
                      "primary": {"game": GAME_NAME[game], "mode": gamemode, "module": module_for(path)},
                      "rules": rules, "unmet": unmet}
    return {"path": path, "claimed": CLAIMED[path], "module": module_for(path),
            "expected_conflicts": conflicts, "expected_resolution": resolution}


# --------------------------------------------------------------------------- BENCH-v1

def canonical_sha256(value) -> str:
    text = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def dump(value) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def build_bench() -> dict:
    raw = DRAFT.read_bytes()
    if hashlib.sha256(raw).hexdigest() != DRAFT_SHA256:
        raise SystemExit("draft-v0.json does not match its recorded sha256 056889a0...; refusing to freeze")
    draft = json.loads(raw.decode("utf-8"))
    cases = []
    for case in draft["cases"]:
        if list(case.keys()) != DRAFT_KEYS:
            raise SystemExit(f"unexpected draft keys in {case['id']}")
        path = "q2-dm" if case["target"] == "q2-showcase" else case["target"]
        out = dict(case)
        out.update(case_extras(path, case["id"], case["prompt"], case["category"]))
        if case["category"] == "contradictory" and not out["expected_conflicts"]:
            raise SystemExit(f"no conflicts declared for {case['id']}")
        if out["expected_resolution"]:
            stray = [r for r in out["expected_resolution"]["unmet"] if r not in case["expected_requests"]]
            if stray:
                raise SystemExit(f"{case['id']}: unmet requests {stray} are not expected requests")
        if {k: out[k] for k in DRAFT_KEYS} != case:
            raise SystemExit(f"{case['id']}: draft fields changed")
        cases.append(out)
    added = new_cases()
    seen_ids = {c["id"] for c in cases}
    seen_seeds = {c["seed"] for c in cases}
    seen_prompts = {c["prompt"] for c in cases}
    for case in added:
        if case["id"] in seen_ids or case["seed"] in seen_seeds or case["prompt"] in seen_prompts:
            raise SystemExit(f"duplicate id/seed/prompt for new case {case['id']}")
        seen_ids.add(case["id"])
        seen_seeds.add(case["seed"])
        seen_prompts.add(case["prompt"])
    cases += added
    paths = {p: {"game": v[0], "mode": v[1], "mode_request": v[2], "primary_gamemode": v[3],
                 "module": module_for(p), "claimed": CLAIMED[p]} for p, v in PATHS.items()}
    manifest = {
        "schema": "qmg.benchmark.manifest",
        "schema_version": 2,
        "set": "BENCH-v1",
        "description": "Frozen Phase-2 benchmark (plan section 8.3). The 138 draft-v0 cases verbatim (same ids, "
                       "prompts, seeds, expected_requests, targets, categories and notes), each with path, claimed, "
                       "module, expected_conflicts and expected_resolution added, followed by new cases with new "
                       "ids and seeds 2000+index. Cases are never edited; cases may be added only under new ids in "
                       "a new manifest version. q2-showcase-hero is sealed by tools/benchmark/sealed.json (D26).",
        "created": FROZEN_ON,
        "frozen": True,
        "change_id": CHANGE_ID,
        "draft_manifest": {"path": "tools/benchmark/draft-v0.json", "sha256": DRAFT_SHA256,
                           "cases": len(draft["cases"])},
        "seal_list": "tools/benchmark/sealed.json",
        "cases_sha256": canonical_sha256(cases),
        "cases_sha256_rule": "sha256 of json.dumps(cases, sort_keys=True, separators=(',', ':'), "
                             "ensure_ascii=False) encoded as UTF-8. The sha256 of the file bytes is recorded in "
                             "tools/benchmark/manifests.json, because a file cannot carry its own byte hash.",
        "case_count": len(cases),
        "draft_case_count": len(draft["cases"]),
        "new_case_count": len(added),
        "new_seed_rule": f"seed = {NEW_SEED_BASE} + index of the new case in manifest order",
        "target_mode_vocabulary": draft["target_mode_vocabulary"],
        "game_vocabulary": draft["game_vocabulary"],
        "targets": draft["targets"],
        "paths": paths,
        "claim_tiers": {
            "committed": "Kept (plan section 12).",
            "conditional": "Kept if the user supplies the named input (plan section 12).",
            "deferred": "Decided at the E1 checkpoint; default defer (plan section 12).",
            "dropped-claim": "Not claimed separately or likely dropped (plan section 12). Cases stay as honesty "
                             "probes (D11(6)).",
        },
        "modules": MODULES,
        "conflict_classes": CONFLICT_CLASSES,
        "resolution_rules": RESOLUTION_RULES,
        "span_rule": "Spans are UTF-8 byte offsets into the prompt (start inclusive, end exclusive) plus the text.",
        "cases": cases,
    }
    return manifest


def build_contra(bench: dict, bench_sha: str) -> dict:
    ids = [c["id"] for c in bench["cases"] if c["category"] == "contradictory"]
    if len(ids) != 34:
        raise SystemExit(f"expected 34 contradictory cases, found {len(ids)}")
    return {"schema": "qmg.benchmark.id_set", "schema_version": 1, "set": "CONTRA-v1", "created": FROZEN_ON,
            "frozen": True, "change_id": CHANGE_ID,
            "description": "The 34 contradictory BENCH-v1 cases (2 per target). Gate G10 (conflict surfaced) is "
                           "measured on this set. Conflict regressions are added only under new ids in a new set "
                           "version.",
            "base_manifest": {"path": "tools/benchmark/rc1-v1.json", "sha256": bench_sha},
            "ids": ids}


# --------------------------------------------------------------------------- PARSE-MATRIX-v1

def build_parse_matrix() -> dict:
    games = [("quake1", "Quake 1"), ("quake2", "Quake 2"), ("quake3", "Quake 3")]
    rows = []

    def row(group, text, expect, depends=None, note=""):
        rows.append({"id": f"pm-{len(rows) + 1:03d}", "group": group, "input": text, "expect": expect,
                     "decision_dependency": depends or [], "note": note})

    mode_phrases = [
        ("single player", "single_player"), ("single-player", "single_player"), ("campaign", "single_player"),
        ("co-op", "cooperative"), ("coop", "cooperative"), ("cooperative", "cooperative"),
        ("deathmatch", "deathmatch"), ("DM", "deathmatch"), ("free for all", "deathmatch"), ("FFA", "deathmatch"),
        ("LAN party", "deathmatch"),
        ("duel", "duel"), ("1v1", "duel"), ("one-on-one", "duel"), ("tourney", "duel"), ("tournament", "duel"),
        ("team deathmatch", "team_deathmatch"), ("TDM", "team_deathmatch"),
        ("capture the flag", "capture_the_flag"), ("CTF", "capture_the_flag"), ("Threewave CTF", "capture_the_flag"),
        ("bot arena", "bot_arena"),
    ]
    for game, label in games:
        for phrase, mode in mode_phrases:
            text = f"a {label} {phrase} map"
            expect = {"game": game, "mode": mode}
            depends = []
            note = ""
            if mode in ("single_player", "cooperative") and game == "quake3":
                if mode == "cooperative":
                    expect = {"game": game, "mode": None, "finding": "unsupported_mode"}
                    note = "Quake 3 has no co-op mode."
                else:
                    expect = {"game": game, "mode": None}
                    depends = ["D15"]
                    note = "Quake 3 single player is the bot-arena ladder or a tourney map; D15 decides."
            elif mode == "bot_arena" and game != "quake3":
                expect = {"game": game, "mode": None, "finding": "unsupported_mode"}
                note = "Bot arena exists only in Quake 3."
            elif mode == "bot_arena":
                depends = ["D15"]
            if mode == "capture_the_flag":
                if game == "quake1":
                    expect["module"] = "q1-threewave-5.0"
                    note = "id1 has no CTF; Quake 1 CTF means Threewave."
                elif game == "quake2":
                    expect["module"] = "q2-ctf-3.19"
                elif phrase == "Threewave CTF":
                    expect["module"] = "q3-baseq3-1.32b"
                    expect["finding"] = "unsupported_module"
                    note = "Threewave is not part of stock Quake 3; CTF is served by baseq3."
            if game == "quake3" and phrase in ("tourney", "tournament"):
                expect["entity_gametype_token"] = "tournament"
                expect["arena_type_token"] = "tourney"
            if game == "quake1" and mode == "team_deathmatch":
                note = "Q1 TDM is dropped as a separate claim (section 12) but must still parse."
            row("mode_phrase", text, expect, depends, note)
    numbers = [
        ("a Quake 2 deathmatch map for eight players", 8, 8), ("a Quake 3 FFA map for 16 people", 16, 16),
        ("a Quake 2 deathmatch map for 2-8 players", 2, 8), ("a Quake 2 deathmatch map for 2 to 8 players", 2, 8),
        ("a Quake 2 deathmatch map for 2–8 players", 2, 8),
        ("a Quake 1 duel map for two players", 2, 2), ("a Quake 3 team deathmatch map for four to six players", 4, 6),
        ("a Quake 2 CTF map for 4v4", 8, 8), ("a Quake 1 deathmatch map for twelve players", 12, 12),
        ("a Quake 3 tourney map for 2 players", 2, 2),
    ]
    for text, low, high in numbers:
        row("number", text, {"player_min": low, "player_max": high})
    negations = [
        ("a Quake 1 deathmatch map with no loops", {"route_style_not": "loop"}),
        ("a Quake 2 duel map that is not vertical", {"verticality": "flat"}),
        ("a Quake 3 FFA map with no water", {"liquids_forbidden": ["water"], "theme_not": "waterworks"}),
        ("a Quake 2 single-player level without lava", {"liquids_forbidden": ["lava"], "theme_not": "lava fortress"}),
        ("a Quake 1 single-player map with no monsters", {"monsters": "none"}),
        ("a Quake 2 deathmatch map with no BFG", {"forbidden_items": ["weapon_bfg"]}),
        ("a Quake 3 tourney map without powerups", {"forbidden_item_classes": ["powerup"]}),
    ]
    for text, expect in negations:
        row("negation", text, expect)
    false_positives = [
        ("a Quake 2 deathmatch map in a gothic cathedral with a database archive",
         {"theme": "gothic", "theme_not": "techbase"}),
        ("a Quake 3 CTF map with two bases in a medieval castle",
         {"theme": "medieval", "theme_not": "techbase", "player_count_not": 2, "mode": "capture_the_flag"}),
        ("a Quake 1 deathmatch map in a medieval keep with open space", {"theme": "medieval", "theme_not": "space"}),
        ("a Quake 2 single-player level in a gothic basement", {"theme": "gothic", "theme_not": "techbase"}),
    ]
    for text, expect in false_positives:
        row("false_positive", text, expect)
    references = [
        ("a q2dm1 style map", {"style_reference": "q2dm1", "game_from_style_reference": False,
                               "mode_from_style_reference": False}),
        ("q3dm17 remake for Quake 2", {"game": "quake2", "style_reference": "q3dm17",
                                       "game_from_style_reference": False}),
        ("like q3dm6 but for Quake 1 deathmatch", {"game": "quake1", "mode": "deathmatch",
                                                    "style_reference": "q3dm6"}),
        ("a dm3-inspired Quake 2 duel map", {"game": "quake2", "mode": "duel", "style_reference": "dm3"}),
        ("a q2ctf1 style capture the flag map for Quake 2", {"game": "quake2", "mode": "capture_the_flag",
                                                             "style_reference": "q2ctf1"}),
    ]
    for text, expect in references:
        row("map_reference", text, expect, note="Map-name references are style hints; they never switch "
                                                "game or mode (rank 1, R1-e).")
    return {
        "schema": "qmg.benchmark.parse_matrix", "schema_version": 1, "set": "PARSE-MATRIX-v1",
        "created": FROZEN_ON, "frozen": True, "change_id": CHANGE_ID,
        "description": "Unit-test table for the prompt normalizer (rank 1 deliverable "
                       "tests/test_prompt_normalization.cpp). Each row gives an input and the normalized fields "
                       "it must produce. Keys absent from 'expect' are not checked by that row. Rows with a "
                       "decision_dependency are reported, not failed, until that decision is recorded.",
        "expect_keys": {
            "game": "PromptSpec game token (quake1, quake2, quake3).",
            "mode": "Primary GameMode token (D4 vocabulary); null means no mode may be inferred from the phrase.",
            "module": "GameModule token (D4).",
            "finding": "A typed prompt finding code that must be raised.",
            "player_min": "Lower bound of the player range.", "player_max": "Upper bound of the player range.",
            "route_style_not": "The plan's route style must not be this value.",
            "verticality": "Verticality class the normalizer must set.",
            "liquids_forbidden": "Liquids the brief must mark forbidden.",
            "theme": "Theme the brief must select.", "theme_not": "Theme the brief must not select.",
            "monsters": "'none' means no monsters may be requested.",
            "forbidden_items": "Classnames the brief must mark forbidden.",
            "forbidden_item_classes": "Item classes the brief must mark forbidden.",
            "player_count_not": "A player count the phrase must not produce.",
            "style_reference": "Map-name style hint to record.",
            "game_from_style_reference": "Whether the game may be taken from the style reference (always false).",
            "mode_from_style_reference": "Whether the mode may be taken from the style reference (always false).",
            "entity_gametype_token": "Q3 entity gametype key token (g_spawn.c).",
            "arena_type_token": "Q3 arena/UI type token (ui_startserver.c).",
        },
        "rows": rows,
    }


# --------------------------------------------------------------------------- portfolios

def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def portfolio_entry(pid, group, prompt_path: Path, map_name, outputs_dir: Path, rooms, seed, mode, extra=None):
    data = prompt_path.read_bytes()
    prompt = data.decode("utf-8")
    path = "q2-duel" if mode == "duel" else "q2-dm"
    mode_code = "duel" if mode == "duel" else "dm"
    recorded = {}
    for suffix in (".map", ".qmg.json"):
        target = outputs_dir / (map_name + suffix)
        if target.is_file():
            recorded[suffix.lstrip(".").replace(".", "_") + "_sha256"] = sha256_bytes(target.read_bytes())
    report_path = outputs_dir / (map_name + ".qmg.json")
    if report_path.is_file():
        report = json.loads(report_path.read_text(encoding="utf-8"))
        recorded["layout_sha256"] = report.get("layout_sha256")
        recorded["candidate_seed"] = report.get("candidate_seed")
        recorded["report_prompt_matches_file"] = report.get("prompt") == prompt
    entry = {
        "id": pid, "target": path, "game": "q2", "mode": mode_code, "category": "portfolio", "group": group,
        "prompt": prompt, "prompt_sha256": sha256_bytes(data), "prompt_source": str(prompt_path.relative_to(REPO))
        .replace("\\", "/"), "requested_seed": seed, "seed": None, "rooms": rooms,
        "map_name": map_name,
        "qmg_args": ["generate", "--prompt-file", "{prompt_file}", "--output", map_name + ".map"],
        "expected_requests": ["game quake2", "mode " + ("duel" if mode == "duel" else "deathmatch"),
                              f"{rooms} rooms", "theme industrial"],
        "notes": "", "path": path, "claimed": CLAIMED[path], "module": module_for(path),
        "expected_conflicts": [], "expected_resolution": None,
        "recorded_outputs": dict(recorded, directory=str(outputs_dir.relative_to(REPO)).replace("\\", "/")),
    }
    if extra:
        entry.update(extra)
    return entry


def portfolio_manifest(name, set_name, description, source, entries, missing, invocation_evidence):
    return {
        "schema": "qmg.benchmark.manifest", "schema_version": 2, "set": set_name, "created": FROZEN_ON,
        "frozen": True, "change_id": CHANGE_ID, "description": description,
        "reconstructed_from": source,
        "invocation": "qmg generate --prompt-file <exact prompt bytes> --output <map_name>.map, all other options "
                      "at their defaults (6 candidates, rules planner, no --seed: the request seed is in the "
                      "prompt text). run.py writes the prompt bytes verbatim to <case>/prompt.txt.",
        "invocation_evidence": invocation_evidence,
        "reconstruction_gaps": missing,
        "cases_sha256": canonical_sha256(entries),
        "case_count": len(entries),
        "cases": entries,
    }


def build_portfolios() -> dict:
    out = REPO / "out"
    needed = [out / "mp-portfolio" / "cases.json", out / "span-portfolio" / "protocol.json",
              out / "lattice-portfolio" / "cases.json", out / "splice-portfolio" / "cases.json"]
    if not all(p.is_file() for p in needed):
        return {}
    result = {}
    # MP-PORTFOLIO (Weft nine + two verticality controls)
    mp = out / "mp-portfolio"
    entries = []
    for c in json.loads((mp / "cases.json").read_text(encoding="utf-8-sig")):
        entries.append(portfolio_entry(c["id"], "primary", REPO / c["prompt_file"], "qmg_" + c["id"],
                                       mp / "generated" / c["id"], c["rooms"], c["seed"], c["mode"]))
    for c in json.loads((mp / "semantic-controls.json").read_text(encoding="utf-8-sig")):
        entries.append(portfolio_entry(c["id"], "verticality_control", REPO / c["prompt_file"], "qmg_" + c["id"],
                                       mp / "generated" / c["id"], 6, 42, "deathmatch",
                                       {"control_expectation": c.get("expected")}))
    result["mp-portfolio.json"] = portfolio_manifest(
        "mp-portfolio", "MP-PORTFOLIO",
        "Weft nine: Q2 compact requests with 4/6/8 rooms x request seeds 17/42/99, plus the rooms6-seed42 "
        "high and low verticality controls (11 entries). Q2 DM regression set (plan section 8.3).",
        ["out/mp-portfolio/cases.json", "out/mp-portfolio/semantic-controls.json", "out/mp-portfolio/prompts/",
         "out/mp-portfolio/final-cli-parity/parity.json"], entries, [],
        "out/mp-portfolio/final-cli-parity/parity.json records the CLI command "
        "'qmg.exe generate --prompt-file out/mp-portfolio/prompts/rooms6-seed42.txt --output .../qmg_weft.map' and "
        "byte parity with the exporter; tools/mp_portfolio/README.md states the exporter uses the default "
        "pipeline options.")
    # SPAN-PORTFOLIO (protocol, pinned by prompt sha256)
    sp = out / "span-portfolio"
    protocol_raw = (sp / "protocol.json").read_bytes()
    protocol = json.loads(protocol_raw.decode("utf-8-sig"))
    entries = []
    missing = []
    for c in protocol["cases"]:
        prompt_path = REPO / c["prompt_file"]
        entry = portfolio_entry(c["id"], c["group"], prompt_path, c["map_name"], sp / "generated" / c["id"],
                                c["rooms"], c["requested_seed"], "duel" if c["rooms"] == 4 else "deathmatch",
                                {"protocol_hero": bool(c.get("hero"))})
        if entry["prompt_sha256"] != c["prompt_sha256"]:
            missing.append(f"{c['id']}: prompt file sha256 differs from protocol.json")
        entries.append(entry)
    if protocol.get("candidate_count") != 6:
        missing.append(f"protocol candidate_count is {protocol.get('candidate_count')}, not the default 6")
    result["span-portfolio.json"] = portfolio_manifest(
        "span-portfolio", "SPAN-PORTFOLIO",
        "The out/span-portfolio/protocol.json cases (9 primary, 2 holdouts, 3 flat controls), pinned by prompt "
        "sha256. The protocol hero (rooms6-seed701, 'qmg_span') is a Span study example, not a D26 hero.",
        ["out/span-portfolio/protocol.json (sha256 " + sha256_bytes(protocol_raw) + ")",
         "out/span-portfolio/generated/record.json"], entries, missing,
        "protocol.json candidate_count 6; out/span-portfolio/generated/<id>/export.log records 'default "
        "candidate selection'.")
    # LATTICE fifteen (9 primary + 6 holdouts)
    la = out / "lattice-portfolio"
    entries = []
    for group, file in (("primary", "cases.json"), ("holdout", "holdouts.json")):
        for c in json.loads((la / file).read_text(encoding="utf-8-sig")):
            mode = c.get("mode") or ("duel" if c["rooms"] == 4 else "deathmatch")
            entries.append(portfolio_entry(c["id"], group, REPO / c["prompt_file"], "qmg_" + c["id"],
                                           la / "generated" / c["id"], c["rooms"], c["seed"], mode))
    result["lattice-portfolio.json"] = portfolio_manifest(
        "lattice-portfolio", "LATTICE-PORTFOLIO",
        "Lattice fifteen: 9 primary requests (4/6/8 rooms x seeds 17/42/99) and 6 declared holdouts (6/8 rooms x "
        "seeds 7/101/307). The separate Lattice example (rooms8-seed211, out/lattice) is not part of the fifteen.",
        ["out/lattice-portfolio/cases.json", "out/lattice-portfolio/holdouts.json",
         "out/lattice-portfolio/generate_final.py"], entries, [],
        "out/lattice-portfolio/generate_final.py runs qmg_mp_portfolio_export.exe <prompt_file> <dir> qmg_<id> "
        "(the ordinary six-candidate default pipeline).")
    # SPLICE nine
    sl = out / "splice-portfolio"
    entries = []
    for c in json.loads((sl / "cases.json").read_text(encoding="utf-8-sig")):
        entries.append(portfolio_entry(c["id"], "primary", REPO / c["prompt_file"], "qmg_" + c["id"],
                                       sl / "generated" / c["id"], c["rooms"], c["seed"], c["mode"]))
    result["splice-portfolio.json"] = portfolio_manifest(
        "splice-portfolio", "SPLICE-PORTFOLIO",
        "Splice nine: 4/6/8 rooms x seeds 17/42/99 through the ordinary default pipeline. The separate Splice "
        "example (rooms6-seed73) is not part of the nine.",
        ["out/splice-portfolio/cases.json", "out/splice-portfolio/README.md"], entries,
        ["No generation script is retained under out/splice-portfolio; the invocation is taken from the README "
         "('run through the ordinary default pipeline') and the exporter contract in tools/mp_portfolio/README.md."],
        "out/splice-portfolio/README.md: 'run through the ordinary default pipeline'.")
    return result


# --------------------------------------------------------------------------- seal, heroes, registry

def build_sealed(bench: dict) -> dict:
    hero = next(c for c in bench["cases"] if c["id"] == "q2-showcase-hero")
    return {
        "schema": "qmg.benchmark.sealed", "schema_version": 1, "created": FROZEN_ON, "change_id": CHANGE_ID,
        "description": "Sealed cases. run.py refuses to generate a sealed case, or any case whose prompt has the "
                       "same normalized sha256, unless --hero-run is given. Only the prompt is checked in routine "
                       "work, through the 'qmg brief' dry run, which generates nothing.",
        "prompt_normalization": "sha256 of the UTF-8 prompt after str.strip(), lower-casing and collapsing runs "
                                "of whitespace to one space",
        "sealed": [{
            "id": hero["id"], "manifest": "tools/benchmark/rc1-v1.json", "seed": hero["seed"],
            "prompt_sha256": sha256_bytes(hero["prompt"].encode("utf-8")),
            "normalized_prompt_sha256": sha256_bytes(" ".join(hero["prompt"].strip().lower().split())
                                                     .encode("utf-8")),
            "reason": "D26 hero seal",
            "baseline": "Its draft-v0 output (out/benchmark-draft/q2-showcase-hero) is baseline-0 evidence and "
                        "may be re-evaluated read-only; it is not generated again outside the hero run.",
        }],
    }


def build_heroes() -> dict:
    return {
        "schema": "qmg.benchmark.heroes", "schema_version": 1, "created": FROZEN_ON, "change_id": CHANGE_ID,
        "description": "HEROES (plan section 8.3). One entry per path, committed before that hero is generated. "
                       "A hero's primary seed appears in no other fixed set. No hero seed is chosen yet (D26 is "
                       "pending): the list is empty.",
        "entry_schema": {
            "path": "one of the 17 paths",
            "name": "hero name",
            "prompt": "the exact prompt text",
            "prompt_sha256": "sha256 of the UTF-8 prompt",
            "seed": "primary seed (integer); must not appear in any other fixed set",
            "control_seeds": "optional same-prompt controls (for example 4242 for the Q2 DM hero under D26 (b))",
            "expected_requests": "list, in the run.py request vocabulary",
            "declared_out_of_scope": "requests declared out of scope BEFORE generation, each with a reason; "
                                     "waivers on a hero are 0 unless declared here",
            "reference_set": "stock maps used for comparison (ids only)",
            "evidence_plan": {"native_routes": "predeclared routes for native movement trials",
                              "bot_runs": {"k": "number of runs", "frame_budget": "frames per run"},
                              "screenshot_views": "named camera views (D19)"},
            "committed": "date the entry was committed, before generation",
            "approved_by": "the user's approval record",
        },
        "heroes": [],
    }


def write_or_check(files: dict, write: bool) -> int:
    status = 0
    for rel, data in files.items():
        target = BENCH / rel
        if target.exists():
            same = target.read_bytes() == data
            print(f"{'same' if same else 'DIFFERS'}  {rel}")
            if not same:
                status = 1
        elif write:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            print(f"wrote  {rel}")
        else:
            print(f"absent {rel}")
            status = 1
    return status


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--write", action="store_true", help="write files that do not exist yet")
    args = parser.parse_args(argv)
    bench = build_bench()
    bench_bytes = dump(bench)
    bench_sha = sha256_bytes(bench_bytes)
    files = {"rc1-v1.json": bench_bytes,
             "contra-v1.json": dump(build_contra(bench, bench_sha)),
             "parse-matrix-v1.json": dump(build_parse_matrix()),
             "sealed.json": dump(build_sealed(bench)),
             "heroes.json": dump(build_heroes())}
    portfolios = build_portfolios()
    if not portfolios:
        print("portfolio sources under out/ are absent: portfolio manifests not rebuilt")
    for name, manifest in portfolios.items():
        files["portfolios/" + name] = dump(manifest)
    registry = {"schema": "qmg.benchmark.manifest_registry", "schema_version": 1, "created": FROZEN_ON,
                "change_id": CHANGE_ID,
                "description": "sha256 of every frozen fixed-set file. run.py refuses a registered manifest whose "
                               "bytes differ from this registry. A new set version gets a new file and a new row.",
                "manifests": []}
    sets = {"rc1-v1.json": "BENCH-v1", "contra-v1.json": "CONTRA-v1", "parse-matrix-v1.json": "PARSE-MATRIX-v1",
            "sealed.json": "SEAL-LIST", "heroes.json": "HEROES",
            "portfolios/mp-portfolio.json": "MP-PORTFOLIO", "portfolios/span-portfolio.json": "SPAN-PORTFOLIO",
            "portfolios/lattice-portfolio.json": "LATTICE-PORTFOLIO",
            "portfolios/splice-portfolio.json": "SPLICE-PORTFOLIO"}
    for rel, set_name in sets.items():
        data = files.get(rel)
        if data is None and (BENCH / rel).exists():
            data = (BENCH / rel).read_bytes()
        if data is None:
            continue
        row = {"set": set_name, "path": "tools/benchmark/" + rel, "sha256": sha256_bytes(data),
               "frozen": set_name not in ("SEAL-LIST", "HEROES")}
        parsed = json.loads(data.decode("utf-8"))
        if "cases_sha256" in parsed:
            row["cases_sha256"] = parsed["cases_sha256"]
            row["cases"] = parsed.get("case_count")
        registry["manifests"].append(row)
    registry["draft"] = {"set": "draft-v0", "path": "tools/benchmark/draft-v0.json", "sha256": DRAFT_SHA256,
                         "frozen": True}
    files["manifests.json"] = dump(registry)
    return write_or_check(files, args.write)


if __name__ == "__main__":
    sys.exit(main())
