"""Texture admission checks: (game, texture name, role) -> accept or reject, with reasons, from registry metadata.

usage:
  python -B tools/asset_registry/admission.py [--registry q2=PATH] [--registry q3=PATH] [--registry q1=PATH]
         check --game q2 --name e1u1/ceil1_4 --role ceiling
  python -B tools/asset_registry/admission.py --registry q2=... --registry q3=... profiles [--repo DIR]

The library entry point is admit(index, game, name, role). Verdicts use registry metadata only (WAL flags, contents
and value; Q3 shader surfaceparms, q3map_surfacelight, skyparms and stages; Q1 WAD lump names). Pixel content
(for example luminance) is not inspected here; that belongs to the material tests in plan section 9.6.

Roles:
  plain     wall, floor, ceiling, trim   an opaque, static, non-emissive, structural solid surface
  hidden    hidden                       a structural face nobody sees (Q3 caulk is accepted)
  special   light_emitter, sky, water, slime, lava, fog (q3), window (q2), clip, trigger, hint, skip (q1, q2),
            origin
Every rejection carries a reason code from REASON_CODES.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]

PLAIN_ROLES = ("wall", "floor", "ceiling", "trim")
SOLID_ROLES = PLAIN_ROLES + ("hidden",)
SPECIAL_ROLES = ("light_emitter", "sky", "water", "slime", "lava", "fog", "window", "clip", "trigger", "hint", "skip",
                 "origin")
ROLES = SOLID_ROLES + SPECIAL_ROLES
GAME_ALIASES = {"q1": "q1", "quake1": "q1", "q2": "q2", "quake2": "q2", "q3": "q3", "quake3": "q3"}
UNSUPPORTED = {"q1": {"light_emitter", "fog", "window"}, "q2": {"fog"}, "q3": {"window", "skip"}}

REASON_CODES = {
    "name_missing": "no texture name was given",
    "unknown_role": "the role is not one of the admission roles",
    "unsupported_role": "the game has no texture metadata that can fill this role",
    "no_q1_wad": "no Quake texture WAD has been supplied (decision D9 pending), so no Quake texture name can be verified",
    "registry_unavailable": "no registry was loaded for this game",
    "absent": "the name is not in the registry",
    "no_wal": "only replacement images exist; Quake II 3.19 loads textures/<name>.wal and nothing else",
    "header_invalid": "the texture header is malformed",
    "emitter": "the texture emits light",
    "sky": "the texture is a sky surface",
    "liquid": "the texture has liquid contents or a liquid surface",
    "warp": "the texture is a turbulent (warped) surface",
    "translucent": "the texture is translucent",
    "flowing": "the texture scrolls (SURF_FLOWING)",
    "tool_surface": "the texture is a compiler or tool surface (nodraw, clip, hint, skip, origin, trigger, portal)",
    "non_structural": "the contents or surfaceparms do not make a structural solid",
    "nonsolid": "the shader clears the solid flag (surfaceparm nonsolid)",
    "slick": "the surface changes movement physics (slick)",
    "animated": "the texture animates (WAL animation chain, Q1 '+' name, animMap or a moving base stage)",
    "deform": "the shader deforms its vertexes",
    "transparent_stage": "the shader is alpha-tested or alpha-blended on its base stage",
    "layer_image": "an image that shipped shaders use only as an animated or additive layer, never as a static base",
    "missing_stage_image": "the shader references an image that is not in the game data",
    "fog": "the shader is a fog volume",
    "role_requirement": "the texture lacks the property the role needs",
}

ANIMATED_TCMODS = {"scroll", "rotate", "turb", "stretch"}
OPAQUE_BLENDS = (None, ["GL_ONE", "GL_ZERO"])
# A texture stage that modulates (multiplies) what is below it is the "lightmap first, texture second" form of a
# static opaque base (for example textures/base_floor/clangfloor_noisy), so it counts as a plain use of the image.
MODULATE_BLENDS = (["GL_DST_COLOR", "GL_ZERO"], ["GL_ZERO", "GL_SRC_COLOR"])
Q3_TOOL_PARMS = ("playerclip", "monsterclip", "botclip", "origin", "areaportal", "clusterportal", "donotenter", "hint")
# surfaceparms that set brush contents (q3map/shaders.c:69-90); a trigger brush must carry none of them.
Q3_CONTENT_PARMS = {"water", "slime", "lava", "playerclip", "monsterclip", "nodrop", "origin", "areaportal",
                    "clusterportal", "donotenter", "botclip", "fog"}
Q1_TOOL_NAMES = {"clip", "hint", "hintskip", "origin", "trigger", "skip"}

# Where each rule comes from (id source unless named otherwise).
RULE_CITATIONS = {
    "q1_names": ["q1:WinQuake/gl_model.c:802 (sky prefix)", "q1:WinQuake/gl_model.c:811 ('*' turbulent)",
                 "q1:WinQuake/gl_model.c:402 ('+' animation)",
                 "ericw-upstream:common/bspfile.cc:353-399 ('*lava', '*slime', other '*' water; clip, hint, origin; "
                 "upstream source, not the pinned alpha11 build)"],
    "q2_flags": ["q2:qcommon/qfiles.h:333-376", "q3:code/bspc/q2files.h:381-382"],
    "q2_wal_only": ["q2:ref_gl/gl_model.c:470"],
    "q3_lookup": ["q3:code/renderer/tr_shader.c:2465,2485-2491", "q3:code/renderer/tr_image.c:1827-1837"],
    "q3_surfaceparms": ["q3:q3map/shaders.c:69-112"],
}


class Index:
    """Lookup tables built from loaded registries."""

    def __init__(self, registries: dict[str, dict] | None = None):
        self.registries: dict[str, dict] = {}
        self.q1: dict[str, dict] = {}
        self.q2: dict[str, dict] = {}
        self.q3_shaders: dict[str, dict] = {}
        self.q3_images: dict[str, list[dict]] = {}
        self.q3_refs: dict[str, list[dict]] = {}
        self.q3_editor_refs: dict[str, list[str]] = {}
        for game, registry in (registries or {}).items():
            self.add(game, registry)

    def add(self, game: str, registry: dict) -> None:
        game = GAME_ALIASES[game]
        self.registries[game] = registry
        if game == "q1":
            self.q1 = {entry["key"]: entry for entry in registry["textures"]}
        elif game == "q2":
            self.q2 = {entry["key"]: entry for entry in registry["textures"]}
        else:
            self.q3_shaders = {}
            for shader in registry["shaders"]:
                if shader.get("definition_index", 0) == 0:
                    self.q3_shaders[shader["key"]] = shader
            self.q3_images = {}
            for image in registry["images"]:
                self.q3_images.setdefault(image["key"], []).append(image)
            self.q3_refs, self.q3_editor_refs = {}, {}
            for key in sorted(self.q3_shaders):
                shader = self.q3_shaders[key]
                for index, stage in enumerate(shader["stages"]):
                    for stage_map in stage["maps"]:
                        if stage_map.startswith(("$", "*")):
                            continue
                        self.q3_refs.setdefault(image_key(stage_map), []).append(
                            {"shader": key, "stage": index, "plain": plain_stage(stage), "source": shader["source"]})
                if shader.get("qer_editorimage"):
                    self.q3_editor_refs.setdefault(image_key(shader["qer_editorimage"]), []).append(key)

    def q3_image(self, path: str) -> dict | None:
        """Resolve an image reference the way the 1.32 renderer does (.tga falls back to .jpg)."""
        key = image_key(path)
        candidates = self.q3_images.get(key, [])
        ext = Path(path).suffix.lower()
        by_format = {image["format"]: image for image in candidates}
        if ext in ("", ".tga"):
            return by_format.get("tga") or by_format.get("jpeg")
        if ext in (".jpg", ".jpeg"):
            return by_format.get("jpeg")
        return candidates[0] if candidates else None


def image_key(path: str) -> str:
    path = path.replace("\\", "/").lower()
    suffix = Path(path).suffix
    return path[: -len(suffix)] if suffix else path


def plain_stage(stage: dict) -> bool:
    return (stage.get("map_kind") in ("map", "clampmap") and not (set(stage["tcmods"]) & ANIMATED_TCMODS)
            and stage.get("rgbgen") != "wave" and (stage.get("blendfunc") in OPAQUE_BLENDS + MODULATE_BLENDS)
            and stage.get("tcgen") in (None, "base", "texture") and not stage.get("alphafunc"))


def load_index(paths: dict[str, Path]) -> Index:
    index = Index()
    for game, path in sorted(paths.items()):
        index.add(game, json.loads(Path(path).read_text(encoding="utf-8")))
    return index


# --------------------------------------------------------------------------- verdicts

def verdict(game: str, name: str, role: str, reasons: list[tuple[str, str]], facts: dict, notes: list[str]) -> dict:
    return {"game": game, "name": name, "role": role, "verdict": "reject" if reasons else "accept",
            "reasons": [{"code": code, "detail": detail} for code, detail in reasons], "facts": facts, "notes": notes}


def admit(index: Index, game: str, name: str | None, role: str) -> dict:
    game_key = GAME_ALIASES.get(str(game).lower(), str(game))
    name = (name or "").strip()
    if role not in ROLES:
        return verdict(game_key, name, role, [("unknown_role", f"role '{role}' is not one of {', '.join(ROLES)}")], {}, [])
    if not name:
        return verdict(game_key, name, role, [("name_missing", "empty texture name")], {}, [])
    if game_key not in ("q1", "q2", "q3"):
        return verdict(game_key, name, role, [("unknown_role", f"unknown game '{game}'")], {}, [])
    if role in UNSUPPORTED[game_key]:
        return verdict(game_key, name, role, [("unsupported_role", f"{game_key} cannot express role '{role}' in texture metadata")], {}, [])
    if game_key == "q1":
        return admit_q1(index, name, role)
    if game_key == "q2":
        return admit_q2(index, name, role)
    return admit_q3(index, name, role)


def admit_q1(index: Index, name: str, role: str) -> dict:
    if "q1" not in index.registries:
        return verdict("q1", name, role, [("no_q1_wad", "no --q1-wad registry was supplied; decision D9 (Q1 WAD) is pending")], {}, [])
    entry = index.q1.get(name.lower())
    if entry is None:
        return verdict("q1", name, role, [("absent", f"'{name}' is not a miptex lump in {index.registries['q1']['source']['wad_name']}")], {}, [])
    lower = entry["name"].lower()
    facts = {"width": entry["width"], "height": entry["height"], "wad": index.registries["q1"]["source"]["wad_name"]}
    reasons: list[tuple[str, str]] = []
    if not entry.get("header_valid", True):
        reasons.append(("header_invalid", "miptex offsets do not match the lump size"))
    liquid = None
    if lower.startswith("*"):
        liquid = "lava" if lower[1:].startswith("lava") else "slime" if lower[1:].startswith("slime") else "water"
    facts["name_class"] = ("liquid:" + liquid if liquid else "sky" if lower.startswith("sky") else
                           "animated" if lower.startswith("+") else "tool" if lower in Q1_TOOL_NAMES else "plain")
    if role in SOLID_ROLES:
        if liquid:
            reasons.append(("liquid", f"'*' names compile as {liquid} contents and draw turbulent"))
        if lower.startswith("sky"):
            reasons.append(("sky", "'sky' names compile as sky"))
        if lower.startswith("+") and role != "hidden":
            reasons.append(("animated", "'+' names are animation frames"))
        if lower in Q1_TOOL_NAMES:
            reasons.append(("tool_surface", f"'{lower}' is a compiler tool name"))
    elif role in ("water", "slime", "lava"):
        if liquid != role:
            reasons.append(("role_requirement", f"a {role} texture needs a '*{role}' name (Q1 liquids are named by prefix)"
                            if role != "water" else "a water texture needs a '*' name that is not '*lava' or '*slime'"))
    elif role == "sky":
        if not lower.startswith("sky"):
            reasons.append(("role_requirement", "a sky texture needs a 'sky' name prefix"))
    elif role in ("clip", "trigger", "hint", "skip", "origin"):
        if lower != role and not (role == "hint" and lower == "hintskip"):
            reasons.append(("role_requirement", f"the {role} role needs the tool name '{role}'"))
    return verdict("q1", entry["name"], role, reasons, facts, [])


def admit_q2(index: Index, name: str, role: str) -> dict:
    if "q2" not in index.registries:
        return verdict("q2", name, role, [("registry_unavailable", "no Quake II registry was loaded")], {}, [])
    entry = index.q2.get(name.lower())
    if entry is None:
        return verdict("q2", name, role, [("absent", f"no textures/{name}.wal and no replacement image in the registry")], {}, [])
    wal = entry.get("wal")
    if wal is None:
        formats = sorted({image["format"] for image in entry["images"]})
        return verdict("q2", entry["name"], role, [("no_wal", f"only {', '.join(formats)} replacement images exist")],
                       {"images": [image["source"] for image in entry["images"]]}, [])
    surf, contents = set(wal["surf"]), set(wal["contents_flags"])
    facts = {"flags": wal["flags"], "surf": wal["surf"], "contents": wal["contents"], "contents_flags": wal["contents_flags"],
             "value": wal["value"], "width": wal["width"], "height": wal["height"], "animname": wal["animname"]}
    notes = []
    if wal.get("internal_name_mismatch"):
        notes.append(f"WAL internal name '{wal['internal_name']}' differs from its path; engines and compilers use the path")
    reasons: list[tuple[str, str]] = []
    if not wal["header_valid"]:
        reasons.append(("header_invalid", wal.get("header_error", "malformed WAL header")))
    liquids = contents & {"CONTENTS_WATER", "CONTENTS_SLIME", "CONTENTS_LAVA"}
    if role in SOLID_ROLES:
        if "SURF_LIGHT" in surf:
            reasons.append(("emitter", f"SURF_LIGHT with value {wal['value']}: an emitter, not a plain {role}"))
        if "SURF_SKY" in surf:
            reasons.append(("sky", "SURF_SKY"))
        if liquids:
            reasons.append(("liquid", f"{', '.join(sorted(liquids))} on a solid role"))
        if "CONTENTS_MIST" in contents:
            reasons.append(("liquid", "CONTENTS_MIST on a solid role"))
        if "SURF_WARP" in surf:
            reasons.append(("warp", "SURF_WARP"))
        if surf & {"SURF_TRANS33", "SURF_TRANS66"} or contents & {"CONTENTS_WINDOW", "CONTENTS_TRANSLUCENT"}:
            reasons.append(("translucent", ", ".join(sorted((surf & {"SURF_TRANS33", "SURF_TRANS66"}) |
                                                            (contents & {"CONTENTS_WINDOW", "CONTENTS_TRANSLUCENT"})))))
        if "SURF_FLOWING" in surf:
            reasons.append(("flowing", "SURF_FLOWING"))
        tools = (surf & {"SURF_NODRAW", "SURF_HINT", "SURF_SKIP"}) | (contents & {
            "CONTENTS_PLAYERCLIP", "CONTENTS_MONSTERCLIP", "CONTENTS_ORIGIN", "CONTENTS_AREAPORTAL"})
        if tools:
            reasons.append(("tool_surface", ", ".join(sorted(tools))))
        odd = contents - {"CONTENTS_SOLID", "CONTENTS_WATER", "CONTENTS_SLIME", "CONTENTS_LAVA", "CONTENTS_MIST",
                          "CONTENTS_WINDOW", "CONTENTS_TRANSLUCENT", "CONTENTS_PLAYERCLIP", "CONTENTS_MONSTERCLIP",
                          "CONTENTS_ORIGIN", "CONTENTS_AREAPORTAL"}
        if odd or wal.get("contents_unknown_bits"):
            reasons.append(("non_structural", ", ".join(sorted(odd) + wal.get("contents_unknown_bits", []))))
        if "SURF_SLICK" in surf and role == "floor":
            reasons.append(("slick", "SURF_SLICK on a floor"))
        if wal["animname"] and role != "hidden":
            reasons.append(("animated", f"animation chain to {wal['animname']}"))
    elif role == "light_emitter":
        if "SURF_LIGHT" not in surf or wal["value"] <= 0:
            reasons.append(("role_requirement", f"needs SURF_LIGHT with a positive value (flags {wal['flags']}, value {wal['value']})"))
        for flag, code in (("SURF_SKY", "sky"), ("SURF_WARP", "warp")):
            if flag in surf:
                reasons.append((code, flag))
        if liquids:
            reasons.append(("liquid", ", ".join(sorted(liquids))))
        if "SURF_NODRAW" in surf:
            reasons.append(("tool_surface", "SURF_NODRAW"))
    elif role == "sky":
        if "SURF_SKY" not in surf:
            reasons.append(("role_requirement", "needs SURF_SKY"))
    elif role in ("water", "slime", "lava"):
        wanted = "CONTENTS_" + role.upper()
        if wanted not in contents:
            reasons.append(("role_requirement", f"needs {wanted} (contents {wal['contents']})"))
        if "SURF_WARP" not in surf:
            notes.append("no SURF_WARP: the liquid surface will not warp")
    elif role == "window":
        if not (contents & {"CONTENTS_WINDOW"} or surf & {"SURF_TRANS33", "SURF_TRANS66"}):
            reasons.append(("role_requirement", "needs CONTENTS_WINDOW or SURF_TRANS33/66"))
        if liquids:
            reasons.append(("liquid", ", ".join(sorted(liquids))))
    elif role == "clip":
        if not contents & {"CONTENTS_PLAYERCLIP", "CONTENTS_MONSTERCLIP"}:
            reasons.append(("role_requirement", "needs CONTENTS_PLAYERCLIP or CONTENTS_MONSTERCLIP"))
    elif role == "trigger":
        if "SURF_NODRAW" not in surf:
            reasons.append(("role_requirement", "a trigger brush texture needs SURF_NODRAW"))
        if liquids or contents & {"CONTENTS_PLAYERCLIP", "CONTENTS_MONSTERCLIP"}:
            reasons.append(("non_structural", ", ".join(sorted(liquids | (contents & {"CONTENTS_PLAYERCLIP", "CONTENTS_MONSTERCLIP"})))))
    elif role == "hint":
        if "SURF_HINT" not in surf:
            reasons.append(("role_requirement", "needs SURF_HINT"))
    elif role == "skip":
        if "SURF_SKIP" not in surf:
            reasons.append(("role_requirement", "needs SURF_SKIP"))
    elif role == "origin":
        if "CONTENTS_ORIGIN" not in contents:
            reasons.append(("role_requirement", "needs CONTENTS_ORIGIN"))
    return verdict("q2", entry["name"], role, reasons, facts, notes)


def q3_key(name: str) -> str:
    key = name.replace("\\", "/").lower()
    return key if key.startswith("textures/") else "textures/" + key


def admit_q3(index: Index, name: str, role: str) -> dict:
    if "q3" not in index.registries:
        return verdict("q3", name, role, [("registry_unavailable", "no Quake III registry was loaded")], {}, [])
    key = q3_key(name)
    shader = index.q3_shaders.get(key)
    if shader is None:
        image = index.q3_image(key + ".tga")
        if image is None:
            return verdict("q3", name, role, [("absent", f"no shader '{key}' in scripts/*.shader and no {key}.tga or .jpg")], {}, [])
        return admit_q3_implicit(index, name, role, key, image)
    return admit_q3_shader(index, name, role, shader)


def admit_q3_implicit(index: Index, name: str, role: str, key: str, image: dict) -> dict:
    refs = index.q3_refs.get(key, [])
    facts = {"resolution": "implicit shader (no script)", "image": image["source"], "width": image.get("width"),
             "height": image.get("height"), "stage_references": [f"{ref['shader']} stage {ref['stage'] + 1}" +
                                                                ("" if ref["plain"] else " (animated or blended)") for ref in refs]}
    notes = []
    if image.get("tga_alpha_bits"):
        notes.append("the TGA has an alpha channel, which the implicit shader ignores")
    if index.q3_editor_refs.get(key):
        notes.append(f"editor image of {', '.join(index.q3_editor_refs[key])}; the shader is the intended surface")
    reasons: list[tuple[str, str]] = []
    if not image.get("header_valid", True):
        reasons.append(("header_invalid", image.get("header_error", "malformed image header")))
    if role in SOLID_ROLES:
        if refs and not any(ref["plain"] for ref in refs):
            reasons.append(("layer_image", "used in shipped shaders only as a scrolled, waved or additive layer: " +
                            "; ".join(f"{ref['shader']} ({ref['source']}) stage {ref['stage'] + 1}" for ref in refs)))
    elif role in SPECIAL_ROLES:
        reasons.append(("role_requirement", f"an implicit shader has default surface properties; role '{role}' needs a shader"))
    return verdict("q3", name, role, reasons, facts, notes)


def admit_q3_shader(index: Index, name: str, role: str, shader: dict) -> dict:
    parms = set(shader["surfaceparms"])
    light = shader.get("q3map_surfacelight") or 0
    stages = shader["stages"]
    missing = []
    for stage in stages:
        for stage_map in stage["maps"]:
            if not stage_map.startswith(("$", "*")) and index.q3_image(stage_map) is None:
                missing.append(stage_map)
    facts = {"resolution": "shader", "shader": shader["source"], "surfaceparms": shader["surfaceparms"],
             "q3map_surfacelight": shader.get("q3map_surfacelight"), "skyparms": shader.get("skyparms"),
             "stages": len(stages), "definitions": shader.get("definitions", 1)}
    notes = []
    if shader.get("definitions", 1) > 1:
        notes.append(f"{shader['definitions']} definitions; the first in file order is used")
    reasons: list[tuple[str, str]] = []
    if missing:
        reasons.append(("missing_stage_image", ", ".join(sorted(set(missing)))))
    base = stages[0] if stages else None
    if role in SOLID_ROLES:
        if light > 0:
            reasons.append(("emitter", f"q3map_surfacelight {light}"))
        if "sky" in parms or shader.get("skyparms"):
            reasons.append(("sky", "surfaceparm sky" if "sky" in parms else "skyparms"))
        liquids = parms & {"water", "slime", "lava"}
        if liquids:
            reasons.append(("liquid", "surfaceparm " + ", ".join(sorted(liquids))))
        if "fog" in parms or shader.get("fogparms"):
            reasons.append(("fog", "surfaceparm fog / fogparms"))
        if "nonsolid" in parms:
            reasons.append(("nonsolid", "surfaceparm nonsolid"))
        if "trans" in parms:
            reasons.append(("translucent", "surfaceparm trans"))
        tools = parms & set(Q3_TOOL_PARMS)
        if tools:
            reasons.append(("tool_surface", "surfaceparm " + ", ".join(sorted(tools))))
        if "nodraw" in parms and role != "hidden":
            reasons.append(("tool_surface", "surfaceparm nodraw"))
        if "detail" in parms:
            reasons.append(("non_structural", "surfaceparm detail"))
        if "slick" in parms and role == "floor":
            reasons.append(("slick", "surfaceparm slick on a floor"))
        if shader.get("deformvertexes") and role != "hidden":
            reasons.append(("deform", "deformVertexes " + ", ".join(shader["deformvertexes"])))
        if role != "hidden" and not ("nodraw" in parms):
            if base is None:
                reasons.append(("role_requirement", "a visible surface shader needs at least one stage"))
            else:
                moving = sorted(set(base["tcmods"]) & ANIMATED_TCMODS)
                if base.get("map_kind") in ("animmap", "videomap") or moving or base.get("rgbgen") == "wave":
                    reasons.append(("animated", f"base stage {base.get('map_kind')} " +
                                    " ".join(["tcMod " + m for m in moving] + (["rgbGen wave"] if base.get("rgbgen") == "wave" else []))))
                if base.get("alphafunc") or base.get("blendfunc") not in OPAQUE_BLENDS:
                    reasons.append(("transparent_stage", f"base stage blendFunc {base.get('blendfunc')} alphaFunc {base.get('alphafunc')}"))
    elif role == "light_emitter":
        if light <= 0:
            reasons.append(("role_requirement", "needs q3map_surfacelight > 0"))
        if "sky" in parms or shader.get("skyparms"):
            reasons.append(("sky", "a sky shader is not a light fixture"))
        if parms & {"water", "slime", "lava", "fog"}:
            reasons.append(("liquid", "surfaceparm " + ", ".join(sorted(parms & {"water", "slime", "lava", "fog"}))))
        if "nodraw" in parms:
            reasons.append(("tool_surface", "surfaceparm nodraw"))
    elif role == "sky":
        if "sky" not in parms:
            reasons.append(("role_requirement", "needs surfaceparm sky"))
        if not shader.get("skyparms"):
            reasons.append(("role_requirement", "needs skyparms"))
    elif role in ("water", "slime", "lava"):
        if role not in parms:
            reasons.append(("role_requirement", f"needs surfaceparm {role}"))
    elif role == "fog":
        if "fog" not in parms or not shader.get("fogparms"):
            reasons.append(("role_requirement", "needs surfaceparm fog and fogparms"))
    elif role == "clip":
        if not parms & {"playerclip", "monsterclip"}:
            reasons.append(("role_requirement", "needs surfaceparm playerclip"))
    elif role == "trigger":
        if "nodraw" not in parms:
            reasons.append(("role_requirement", "a trigger brush shader needs surfaceparm nodraw"))
        if parms & Q3_CONTENT_PARMS:
            reasons.append(("non_structural", "content-setting surfaceparm " + ", ".join(sorted(parms & Q3_CONTENT_PARMS))))
    elif role == "hint":
        if "hint" not in parms:
            reasons.append(("role_requirement", "needs surfaceparm hint"))
    elif role == "origin":
        if "origin" not in parms:
            reasons.append(("role_requirement", "needs surfaceparm origin"))
    return verdict("q3", name, role, reasons, facts, notes)


# --------------------------------------------------------------------------- generator profile report

PROFILE_ROLE_MAP = {"floor": "floor", "platform": "floor", "stair": "floor", "ramp": "floor", "ceiling": "ceiling",
                    "wall": "wall", "arch": "wall", "curve": "wall", "door": "wall", "lift": "wall", "hidden": "hidden",
                    "trigger": "trigger", "water": "water", "slime": "slime", "lava": "lava"}


def profile_report(index: Index, repo: Path) -> list[dict]:
    rows = []
    for game in ("q1", "q2", "q3"):
        path = repo / "data" / "profiles" / game / "generator-current.json"
        if not path.is_file():
            continue
        profile = json.loads(path.read_text(encoding="utf-8"))
        for table in ("solid_materials", "liquid_materials"):
            for generator_role in sorted(profile.get(table, {})):
                value = profile[table][generator_role].get("value")
                role = PROFILE_ROLE_MAP.get(generator_role)
                if value is None or role is None:
                    continue
                result = admit(index, game, value, role)
                rows.append({"game": game, "generator_role": generator_role, "admission_role": role, "name": value,
                             "verdict": result["verdict"], "reasons": [r["code"] for r in result["reasons"]]})
    return rows


def parse_registry_args(values: list[str]) -> dict[str, Path]:
    paths = {}
    for value in values:
        game, _, path = value.partition("=")
        if game not in ("q1", "q2", "q3") or not path:
            raise SystemExit(f"--registry expects q1=PATH, q2=PATH or q3=PATH, got '{value}'")
        paths[game] = Path(path)
    return paths


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--registry", action="append", default=[], help="game=PATH of a registry.json (repeatable)")
    sub = parser.add_subparsers(dest="command", required=True)
    check = sub.add_parser("check", help="admit one texture for one role")
    check.add_argument("--game", required=True)
    check.add_argument("--name", required=True)
    check.add_argument("--role", required=True)
    profiles = sub.add_parser("profiles", help="verdicts for every generator-current material")
    profiles.add_argument("--repo", type=Path, default=REPO)
    args = parser.parse_args(argv)
    index = load_index(parse_registry_args(args.registry))
    if args.command == "check":
        result = admit(index, args.game, args.name, args.role)
        print(json.dumps(result, indent=1, sort_keys=True))
        return 0 if result["verdict"] == "accept" else 1
    print(json.dumps(profile_report(index, args.repo), indent=1, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
