#!/usr/bin/env python3
"""Compile data/profiles/*/generator-current.json (qmg.game_profile v1) into a C++ header.

    python generate_profile_tables.py <mapgen dir> <header path>

A port of Quake-MapGen's cmake/GenerateGameProfileTables.cmake that writes the same header, byte for
byte apart from its first comment line. The header holds the tables src/map_builder.cpp and
src/entities.cpp read (materials per role, liquid materials, classnames per placement role, team
classnames, weapon and monster variant lists, light presets, monster spawn boxes, pickup drop boxes)
and kProfileSetSha256: sha256 over "<relative path>\\n<sha256 of the LF-normalised file>\\n" for every
data/profiles JSON in sorted path order. Module profiles are hashed but not compiled. The output is
rewritten only when its text changes.
"""
from __future__ import annotations

import hashlib
import json
import re
import sys
from pathlib import Path

GAMES = ("quake1", "quake2", "quake3")
LITERAL = re.compile(r"^[A-Za-z0-9_*+/. -]+$")
IDENTIFIER = re.compile(r"^[a-z][a-z0-9_]*$")
INTEGER = re.compile(r"^-?[0-9]+$")


class ProfileError(Exception):
    pass


def fail(message: str) -> None:
    raise ProfileError(f"generate_profile_tables: {message}")


def text_of(value: object) -> str:
    # string(JSON GET) yields strings as-is and numbers as their JSON text.
    if isinstance(value, bool) or value is None:
        fail(f"expected a string or number, found {json.dumps(value)}")
    if isinstance(value, (int, float)):
        return json.dumps(value)
    return str(value)


def get(document: dict, *path: object) -> object:
    node: object = document
    for key in path:
        try:
            node = node[key]  # type: ignore[index]
        except (KeyError, IndexError, TypeError):
            fail(f"member {'.'.join(str(part) for part in path)} not found")
    return node


def members(document: dict, *path: object) -> list[str]:
    node = get(document, *path)
    if not isinstance(node, dict):
        fail(f"{'.'.join(str(part) for part in path)} is not an object")
    # CMake's string(JSON MEMBER) lists members in sorted order (jsoncpp keeps objects in a std::map).
    return sorted(node.keys())


def check_literal(value: str, context: str) -> None:
    if not LITERAL.match(value):
        fail(f"{context}: value '{value}' is not a safe C++ literal")


def check_identifier(value: str, context: str) -> None:
    if not IDENTIFIER.match(value):
        fail(f"{context}: '{value}' is not a C++ enumerator name")


def string_return(document: dict, context: str, *path: object) -> str:
    value = get(document, *path, "value")
    if value is None:
        return "{}"
    if isinstance(value, str):
        check_literal(value, context)
        return f'"{value}"'
    fail(f"{context}: expected a string or null value")
    return ""


def color_expression(document: dict, *path: object) -> str:
    parts = []
    for index in range(3):
        component = text_of(get(document, *path, "value", index))
        if not re.match(r"^[0-9]+$", component) or int(component) > 1000:
            fail(f"colour component '{component}' is not 0..1000")
        parts.append(component)
    return "RgbColor{" + ", ".join(parts) + "}"


def hull(document: dict, context: str, table: str, classname: str) -> str:
    components = []
    for end in ("mins", "maxs"):
        vector = []
        for axis in range(3):
            component = text_of(get(document, table, classname, "value", end, axis))
            if not INTEGER.match(component):
                fail(f"{context} {table}.{classname}.{end}: '{component}' is not an integer")
            vector.append(component)
        components.append("{" + ", ".join(vector) + "}")
    return ", ".join(components)


def generate(root: Path) -> str:
    profile_files = sorted(path.relative_to(root).as_posix() for path in (root / "data/profiles").rglob("*.json"))
    if not profile_files:
        fail(f"no profiles under {root}/data/profiles")

    manifest = ""
    generator_files = []
    texts: dict[str, dict] = {}
    for relative in profile_files:
        raw = (root / relative).read_bytes().replace(b"\r\n", b"\n")
        manifest += f"{relative}\n{hashlib.sha256(raw).hexdigest()}\n"
        if relative == "data/profiles/schema.json":
            continue
        document = json.loads(raw.decode("utf-8"))
        if text_of(get(document, "schema")) != "qmg.game_profile" or text_of(get(document, "schema_version")) != "1":
            fail(f"{relative}: not a qmg.game_profile v1 file")
        if text_of(get(document, "profile_kind")) == "generator":
            generator_files.append(relative)
            texts[relative] = document
    set_hash = hashlib.sha256(manifest.encode("utf-8")).hexdigest()

    found: dict[str, str] = {}
    tables: dict[str, dict[str, str]] = {name: {} for name in ("solid", "liquid", "role", "team", "variant", "light", "hull", "pickup")}
    arrays = ""
    for relative in generator_files:
        document = texts[relative]
        game = text_of(get(document, "game"))
        if game not in GAMES:
            fail(f"{relative}: unknown game '{game}'")
        if game in found:
            fail(f"{relative}: a second generator file for {game} ({found[game]})")
        found[game] = relative

        cases = ""
        for role in members(document, "solid_materials"):
            check_identifier(role, f"{relative} solid_materials")
            expression = string_return(document, f"{relative} solid_materials.{role}", "solid_materials", role)
            cases += f"        case MaterialRole::{role}:\n            return {expression};\n"
        tables["solid"][game] = cases

        cases = ""
        for content in members(document, "liquid_materials"):
            check_identifier(content, f"{relative} liquid_materials")
            expression = string_return(document, f"{relative} liquid_materials.{content}", "liquid_materials", content)
            cases += f"        case BrushContents::{content}:\n            return {expression};\n"
        tables["liquid"][game] = cases

        plain_roles = members(document, "classnames")
        team_roles = members(document, "team_classnames")
        variant_roles = members(document, "variants")
        cases = ""
        for role in plain_roles:
            check_identifier(role, f"{relative} classnames")
            expression = string_return(document, f"{relative} classnames.{role}", "classnames", role)
            cases += f"        case PlacementRole::{role}:\n            return {expression};\n"
        for role in team_roles + variant_roles:
            check_identifier(role, f"{relative} roles")
            cases += f"        case PlacementRole::{role}:\n            return {{}};\n"
        tables["role"][game] = cases

        cases = ""
        for role in team_roles:
            cases += f"        case PlacementRole::{role}:\n            switch (team) {{\n"
            for team in members(document, "team_classnames", role):
                check_identifier(team, f"{relative} team_classnames.{role}")
                expression = string_return(document, f"{relative} team_classnames.{role}.{team}", "team_classnames", role, team)
                cases += f"            case Team::{team}:\n                return {expression};\n"
            cases += "            }\n            return {};\n"
        for role in plain_roles + variant_roles:
            cases += f"        case PlacementRole::{role}:\n            return {{}};\n"
        tables["team"][game] = cases

        cases = ""
        for role in variant_roles:
            check_identifier(role, f"{relative} variants")
            listed = get(document, "variants", role, "value")
            if not isinstance(listed, list):
                fail(f"{relative} variants.{role}: value is not a list")
            values = []
            for value in listed:
                value = text_of(value)
                check_literal(value, f"{relative} variants.{role}")
                values.append(f'"{value}"')
            arrays += f"inline constexpr std::array<std::string_view, {len(values)}> {game}_{role}_variants{{{', '.join(values)}}};\n"
            entry = get(document, "variants", role)
            if isinstance(entry, dict) and "unsupported_reason" in entry:
                reason = text_of(entry["unsupported_reason"])
                check_literal(reason, f"{relative} variants.{role}.unsupported_reason")
                reason_expression = f'"{reason}"'
            else:
                reason_expression = "{}"
                if not values:
                    fail(f"{relative} variants.{role}: an empty list needs an unsupported_reason")
            cases += f"        case PlacementRole::{role}:\n            return VariantTable{{detail::{game}_{role}_variants, {reason_expression}}};\n"
        for role in plain_roles + team_roles:
            cases += f"        case PlacementRole::{role}:\n            return std::nullopt;\n"
        tables["variant"][game] = cases

        cases = ""
        for style in members(document, "lighting"):
            check_identifier(style, f"{relative} lighting")
            brightness = text_of(get(document, "lighting", style, "brightness", "value"))
            if not re.match(r"^[0-9]+$", brightness):
                fail(f"{relative} lighting.{style}: brightness '{brightness}' is not a positive integer")
            preset = get(document, "lighting", style)
            if isinstance(preset, dict) and "color" in preset:
                odd = color_expression(document, "lighting", style, "color")
                even = odd
            else:
                odd = color_expression(document, "lighting", style, "odd_room_color")
                even = color_expression(document, "lighting", style, "even_room_color")
            cases += f"        case LightingStyle::{style}:\n            return LightPreset{{{brightness}, {odd}, {even}}};\n"
        tables["light"][game] = cases

        monsters = get(document, "variants", "monster", "value")
        hull_classes = members(document, "monster_hulls") if "monster_hulls" in document else []
        if len(hull_classes) != len(monsters):
            fail(f"{relative}: monster_hulls has {len(hull_classes)} entries for {len(monsters)} monster variants")
        cases = ""
        for value in monsters:
            classname = text_of(value)
            if classname not in hull_classes:
                fail(f"{relative}: monster variant {classname} has no monster_hulls entry")
            cases += f'        if (classname == "{classname}")\n            return MonsterHull{{{hull(document, relative, "monster_hulls", classname)}}};\n'
        tables["hull"][game] = cases

        pickup_classes = members(document, "pickup_hulls") if "pickup_hulls" in document else []
        expected: list[str] = []
        if game in ("quake1", "quake2"):
            for value in get(document, "variants", "weapon", "value"):
                expected.append(text_of(value))
            for role in ("health", "armor"):
                expected.append(text_of(get(document, "classnames", role, "value")))
            expected = list(dict.fromkeys(expected))
        if len(pickup_classes) != len(expected):
            fail(f"{relative}: pickup_hulls has {len(pickup_classes)} entries for {len(expected)} pickup classnames")
        cases = ""
        for classname in expected:
            if classname not in pickup_classes:
                fail(f"{relative}: pickup classname {classname} has no pickup_hulls entry")
            check_literal(classname, f"{relative} pickup_hulls")
            cases += f'        if (classname == "{classname}")\n            return PickupHull{{{hull(document, relative, "pickup_hulls", classname)}}};\n'
        tables["pickup"][game] = cases

    for game in GAMES:
        if game not in found:
            fail(f"no generator-current profile for {game}")

    def function(signature: str, selector: str, table: str, fallback: str) -> str:
        body = f"{signature} {{\n    switch (game) {{\n"
        for game in GAMES:
            body += f"    case Game::{game}:\n        switch ({selector}) {{\n{tables[table][game]}        }}\n        break;\n"
        return body + f"    }}\n    return {fallback};\n}}\n"

    def classname_function(signature: str, table: str) -> str:
        body = f"{signature} {{\n    switch (game) {{\n"
        for game in GAMES:
            body += f"    case Game::{game}:\n{tables[table][game]}        break;\n"
        return body + "    }\n    return std::nullopt;\n}\n"

    solid = function("[[nodiscard]] constexpr std::string_view solid_material(const Game game, const MaterialRole role) noexcept", "role", "solid", "{}")
    liquid = function("[[nodiscard]] constexpr std::string_view liquid_material(const Game game, const BrushContents contents) noexcept", "contents", "liquid", "{}")
    role = function("[[nodiscard]] constexpr std::string_view role_classname(const Game game, const PlacementRole role) noexcept", "role", "role", "{}")
    team = function("[[nodiscard]] constexpr std::string_view team_classname(const Game game, const PlacementRole role, const Team team) noexcept", "role", "team", "{}")
    variant = function("[[nodiscard]] constexpr std::optional<VariantTable> variant_table(const Game game, const PlacementRole role) noexcept", "role", "variant", "std::nullopt")
    light = function("[[nodiscard]] constexpr std::optional<LightPreset> light_preset(const Game game, const LightingStyle style) noexcept", "style", "light", "std::nullopt")
    monster = classname_function("[[nodiscard]] constexpr std::optional<MonsterHull> monster_hull(const Game game, const std::string_view classname) noexcept", "hull")
    pickup = classname_function("[[nodiscard]] constexpr std::optional<PickupHull> pickup_hull(const Game game, const std::string_view classname) noexcept", "pickup")

    return f"""// Generated by src/mapgen/generate_profile_tables.py from data/profiles (qmg.game_profile v1).
// Do not edit: change the generator-current JSON and rebuild. tools/profiles/check_profiles.py
// (CTest profile_schema) checks this header against the profiles.
#pragma once

#include "qmg/layout.hpp"
#include "qmg/semantic_plan.hpp"
#include "qmg/types.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace qmg::game_profile {{

// sha256 over every data/profiles JSON (see the generator script); report schema v5 will carry it.
inline constexpr std::string_view kProfileSetSha256 = "{set_hash}";

struct VariantTable {{
    std::span<const std::string_view> classnames;
    // Non-empty when the game has no mapping for the role.
    std::string_view unsupported_reason;
}};

struct LightPreset {{
    std::int32_t brightness{{}};
    RgbColor odd_room_color{{}};
    RgbColor even_room_color{{}};
}};

// A monster class's spawn box relative to its origin, as the id source sets it (change r1s-monster-hull-support).
struct MonsterHull {{
    GridVec3 mins;
    GridVec3 maxs;
}};

// The box the engine drops a pickup with, relative to its origin, as the id source sets it (change
// r1s-sp-pickup-hull-support). A Quake 1 item's origin may be the box's minimum corner.
struct PickupHull {{
    GridVec3 mins;
    GridVec3 maxs;
}};

namespace detail {{
{arrays}}} // namespace detail

// An empty view means the (game, role) pair has no material, which the map builder rejects.
{solid}
// An empty view means the contents are not a liquid, which the map builder rejects.
{liquid}
// Classname for a role that needs neither a team nor a variant; empty for team and variant roles.
{role}
// Classname for a team role; empty for every other role.
{team}
// Variant list for the weapon and monster roles; nullopt for every other role.
{variant}
// Room light brightness and colours; odd and even room ids differ only for alternating styles.
{light}
// Spawn box of a monster variant's classname; nullopt for any other classname.
{monster}
// Drop box of a Quake 1 or Quake 2 pickup classname; nullopt for any other classname or game.
{pickup}
}} // namespace qmg::game_profile
"""


def main(arguments: list[str]) -> int:
    if len(arguments) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    root, output = Path(arguments[0]), Path(arguments[1])
    try:
        header = generate(root)
    except ProfileError as error:
        print(error, file=sys.stderr)
        return 1
    encoded = header.encode("utf-8")
    if not output.is_file() or output.read_bytes() != encoded:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
