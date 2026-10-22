"""Measured static features and explicitly unverified navigation-sidecar observations."""

from __future__ import annotations

import math
from collections import Counter, deque

from .binary import Aas, Bsp, FormatError, TRAVEL_NAMES, add_z, vec

MAX_ANCHORS = 1024
MAX_SPAWNS = 128
FLOOR_SAMPLES = 32
RAY_LENGTH = 2048
BASE_WEAPONS = {"weapon_shotgun", "weapon_supershotgun", "weapon_machinegun", "weapon_chaingun",
                "weapon_grenadelauncher", "weapon_rocketlauncher", "weapon_hyperblaster", "weapon_railgun", "weapon_bfg"}
BASE_AMMO = {"ammo_shells", "ammo_bullets", "ammo_grenades", "ammo_rockets", "ammo_cells", "ammo_slugs"}
BASE_ITEMS = {"item_armor_jacket", "item_armor_combat", "item_armor_body", "item_armor_shard", "item_power_screen",
              "item_power_shield", "item_health", "item_health_small", "item_health_large", "item_health_mega",
              "item_quad", "item_invulnerability", "item_silencer", "item_breather", "item_enviro", "item_adrenaline",
              "item_ancient_head", "item_bandolier", "item_pack", "item_flag_team1", "item_flag_team2"}


def quantile(values, fraction):
    values = sorted(values)
    if not values:
        return None
    index = (len(values) - 1) * fraction
    lower = math.floor(index)
    return values[lower] + (values[math.ceil(index)] - values[lower]) * (index - lower)


def describe(values):
    return {"count": len(values), **{key: quantile(values, fraction) for key, fraction in
                                   (("min", 0), ("p10", .1), ("p50", .5), ("p90", .9), ("max", 1))}}


def role(classname):
    if classname == "info_player_deathmatch":
        return "spawn"
    if classname.startswith("weapon_"):
        return "weapon"
    if classname.startswith("ammo_"):
        return "ammo"
    if classname in ("item_flag_team1", "item_flag_team2"):
        return "objective"
    if classname in ("item_quad", "item_invulnerability", "item_health_mega", "item_armor_body",
                     "item_power_shield", "item_power_screen"):
        return "major_item"
    if classname.startswith("item_armor_"):
        return "armor"
    if classname.startswith("item_health"):
        return "health"
    return None


def anchors(entities):
    result, excluded = [], Counter()
    for index, entity in enumerate(entities):
        classname = entity.get("classname", "")
        try:
            spawnflags = int(entity.get("spawnflags", "0"))
        except ValueError as error:
            raise FormatError(f"invalid spawnflags in entity {index}") from error
        if spawnflags & 2048:
            excluded[classname] += 1
            continue
        category = role(classname)
        if category:
            # The engine defaults omitted origins to zero; preserve that behavior and flag it.
            result.append({"entity": index, "class": classname, "role": category,
                           "origin": vec(entity.get("origin", "0 0 0")),
                           "origin_defaulted": "origin" not in entity})
    if len(result) > MAX_ANCHORS or sum(a["role"] == "spawn" for a in result) > MAX_SPAWNS:
        raise FormatError("gameplay anchor/spawn count exceeds analysis cap")
    return result, dict(sorted(excluded.items()))


def strongly_connected(graph):
    reverse = [set() for _ in graph]
    for source, targets in enumerate(graph):
        for target in targets:
            reverse[target].add(source)
    active = {i for i in range(1, len(graph)) if graph[i] or reverse[i]}
    seen, order = set(), []
    for root in sorted(active):
        if root in seen:
            continue
        stack = [(root, False)]
        while stack:
            node, finish = stack.pop()
            if finish:
                order.append(node)
            elif node not in seen:
                seen.add(node)
                stack.append((node, True))
                stack.extend((n, False) for n in sorted(graph[node], reverse=True) if n not in seen)
    membership = {}
    sizes = []
    for root in reversed(order):
        if root in membership:
            continue
        label = len(sizes)
        stack = [root]
        count = 0
        while stack:
            node = stack.pop()
            if node in membership:
                continue
            membership[node] = label
            count += 1
            stack.extend(reverse[node] - membership.keys())
        sizes.append(count)
    return active, membership, sizes


def navigation(aas: Aas, points):
    graph = aas.graph()
    active, membership, sizes = strongly_connected(graph)
    bound = {p["entity"]: aas.area_at(p["origin"]) for p in points}
    spawns = [p for p in points if p["role"] == "spawn"]
    destinations = [p for p in points if p["role"] in ("weapon", "major_item", "objective")]
    tested, reached, unbound_pairs = 0, 0, 0
    by_spawn = []
    for spawn in spawns:
        area = bound[spawn["entity"]]
        seen = {area} if area else set()
        queue = deque(seen)
        while queue:
            current = queue.popleft()
            for target in graph[current]:
                if target not in seen:
                    seen.add(target)
                    queue.append(target)
        missing = []
        for destination in destinations:
            target = bound[destination["entity"]]
            if not area or not target:
                unbound_pairs += 1
                continue
            tested += 1
            if target in seen:
                reached += 1
            else:
                missing.append(destination["entity"])
        by_spawn.append({"entity": spawn["entity"], "area": area, "unreached_bound_entities": missing})
    travel = Counter(TRAVEL_NAMES.get(r[9] & 0xFFFFFF, f"unknown_{r[9] & 0xFFFFFF}")
                     for r in aas.reachability)
    return {
        "status": "unverified_sidecar", "sha256": aas.sha256, "version": aas.version,
        "stored_bsp_checksum": aas.bsp_checksum, "bsp_association_verified": False,
        "limitations": ["Compiler settings, source BSP association and physics profile are unknown.",
                        "Area subdivisions are not rooms; connectivity is not playtest evidence.",
                        "No nearest-area snapping: unbound origins remain unknown.",
                        "Routes omit special jumps and team-restricted edges; travel time is not estimated."],
        "areas": len(aas.areas) - 1, "reachabilities": len(aas.reachability),
        "travel_types": dict(sorted(travel.items())), "active_graph_areas": len(active),
        "strong_components": len(sizes),
        "largest_strong_component_fraction": max(sizes, default=0) / len(active) if active else None,
        "bound_anchors": sum(bool(v) for v in bound.values()), "anchors": len(points),
        "tested_spawn_item_pairs": tested, "unbound_spawn_item_pairs": unbound_pairs,
        "reachable_bound_pair_fraction": reached / tested if tested else None,
        "spawn_bindings": by_spawn,
    }


def analyze(bsp: Bsp, entity_data=None):
    source_entities = bsp.entities if entity_data is None else entity_data
    points, excluded = anchors(source_entities)
    unsupported = Counter(e.get("classname", "") for e in source_entities
                          if not int(e.get("spawnflags", "0")) & 2048
                          and e.get("classname", "").startswith(("weapon_", "ammo_", "item_"))
                          and e["classname"] not in BASE_WEAPONS | BASE_AMMO | BASE_ITEMS)
    classes = Counter(p["class"] for p in points)
    spawns = [p for p in points if p["role"] == "spawn"]
    weapons = [p for p in points if p["role"] == "weapon"]
    major = [p for p in points if p["role"] == "major_item"]
    combat = [p for p in points if p["role"] in ("spawn", "weapon", "major_item", "armor", "objective")]
    all_polygons = list(bsp.polygons())
    visible = [p for p in all_polygons if not p["flags"] & 128 and p["area"] > 0]
    # Upward rendered surfaces include inaccessible trims; this is not navigable floor area.
    floors = [p for p in visible if p["normal"][2] >= .7 and not p["flags"] & (4 | 8 | 16 | 32)]
    area = sum(p["area"] for p in visible)
    floor_area = sum(p["area"] for p in floors)
    palette = Counter()
    for polygon in visible:
        palette[polygon["texture"]] += polygon["area"]
    entropy = -sum(v / area * math.log2(v / area) for v in palette.values()) if area else None
    heights = [p["origin"][2] for p in combat]
    height_span = quantile(heights, .9) - quantile(heights, .1) if heights else None
    nearest_weapon = [min(math.dist(s["origin"], w["origin"]) for w in weapons) for s in spawns] if weapons else []
    spawn_pairs = [(a, b) for i, a in enumerate(spawns) for b in spawns[i + 1:]]
    los_pairs = []
    invalid_eye_entities = set()
    eyes = {p["entity"]: add_z(p["origin"], 22) for p in spawns}
    for entity, eye in eyes.items():
        if bsp.trace(eye, eye) < 1:
            invalid_eye_entities.add(entity)
    tested_pairs = 0
    for a, b in spawn_pairs:
        if a["entity"] in invalid_eye_entities or b["entity"] in invalid_eye_entities:
            continue
        tested_pairs += 1
        if bsp.trace(eyes[a["entity"]], eyes[b["entity"]]) >= 1:
            los_pairs.append([a["entity"], b["entity"]])
    # Systematic area-weighted samples keep tiny compiler face subdivisions from dominating.
    samples, distances = [], []
    if floor_area:
        cursor, cumulative = 0, floors[0]["area"]
        used = set()
        for i in range(FLOOR_SAMPLES):
            target = (i + .5) * floor_area / FLOOR_SAMPLES
            while cumulative < target and cursor + 1 < len(floors):
                cursor += 1
                cumulative += floors[cursor]["area"]
            polygon = floors[cursor]
            if polygon["id"] in used:
                continue
            used.add(polygon["id"])
            eye = add_z(polygon["center"], 46)
            if bsp.trace(eye, eye) < 1:
                continue
            rays = []
            for direction in range(8):
                angle = direction * math.pi / 4
                end = (eye[0] + math.cos(angle) * RAY_LENGTH, eye[1] + math.sin(angle) * RAY_LENGTH, eye[2])
                length = RAY_LENGTH * bsp.trace(eye, end)
                rays.append(length)
                distances.append(length)
            samples.append({"face": polygon["id"], "eye": eye, "ray_lengths": rays})
    diagonal = math.dist(bsp.models[0][:3], bsp.models[0][3:6])
    major_separation = [math.dist(a["origin"], b["origin"]) for i, a in enumerate(major) for b in major[i + 1:]]
    heights_min = min(heights, default=0)
    flat = {
        "spawn_count": len(spawns), "weapon_count": len(weapons),
        "weapon_types": len({w["class"] for w in weapons}), "major_item_count": len(major),
        "combat_height_p90_p10": height_span,
        "combat_height_bands_128": len({math.floor((h - heights_min) / 128) for h in heights}),
        "spawn_nearest_weapon_euclidean_p50": quantile(nearest_weapon, .5),
        "spawn_nearest_weapon_euclidean_p90": quantile(nearest_weapon, .9),
        "spawn_pair_world_los_fraction": len(los_pairs) / tested_pairs if tested_pairs else None,
        "major_item_min_euclidean_separation": min(major_separation) if major_separation else None,
        "world_diagonal": diagonal, "upward_surface_area": floor_area,
        "texture_area_entropy_bits": entropy, "texture_top_area_fraction": max(palette.values()) / area if area else None,
        "sky_surface_area_fraction": sum(p["area"] for p in visible if p["flags"] & 4) / area if area else None,
        "sampled_world_sightline_p50": quantile(distances, .5),
        "sampled_world_sightline_p90": quantile(distances, .9),
    }
    movers = Counter(e.get("classname", "") for e in source_entities
                     if e.get("classname", "").startswith(("func_door", "func_plat", "func_train", "func_rotating")))
    links = []
    for i, entity in enumerate(source_entities):
        if entity.get("classname") in ("misc_teleporter", "trigger_teleport"):
            target = entity.get("target")
            links.append({"entity": i, "target": target,
                          "destinations": [j for j, e in enumerate(source_entities) if target and e.get("targetname") == target]})
    return {
        "metrics": flat, "anchors": points, "classes": dict(sorted(classes.items())),
        "unsupported_pickup_classes": dict(sorted(unsupported.items())),
        "excluded_not_deathmatch": excluded, "entity_count": len(source_entities),
        "spawn_los": {"tested_pairs": tested_pairs, "visible_pairs": los_pairs,
                      "invalid_eye_entities": sorted(invalid_eye_entities)},
        "sightline_samples": samples,
        "surface_palette": [{"texture": name, "area": value, "fraction": value / area}
                            for name, value in palette.most_common(20)],
        "world_bounds": [bsp.models[0][:3], bsp.models[0][3:6]],
        "upward_faces": len(floors), "world_faces": len(all_polygons),
        "movers": dict(sorted(movers.items())), "teleporter_links": links,
        "compiled_visibility_bytes": len(bsp.lumps[3]), "compiled_lighting_bytes": len(bsp.lumps[7]),
        "availability": {
            "geometry_entities": "measured", "world_point_los": "measured_static_proxy",
            "navigation": "not_evaluated", "theme": "metadata_and_material_distribution_only",
            "lighting_readability": "not_evaluated", "runtime_balance": "not_evaluated",
        },
        "limitations": [
            "Stock Q2 deathmatch spawnflag filtering only; required mods and dmflags need review.",
            "Entity origins are authored positions, before runtime item floor drops or spawn adjustment.",
            "Point LOS uses world brushes; windows, lava and slime block, movers/entities are omitted.",
            "LOS is omnidirectional and cannot prove spawn safety, cover quality or player-hull clearance.",
            "Upward surfaces can include inaccessible trim; bounds and height bands are not rooms or routes.",
            "32 area-weighted face samples, 8 horizontal rays each, capped at 2048 units; no PVS-as-LOS inference.",
            "Euclidean distances are not navigation distances or travel times.",
            "Material names/area cannot establish visual coherence; lighting bytes cannot establish readability.",
        ],
    }, floors
