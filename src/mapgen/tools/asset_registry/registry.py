"""Build a qmg.asset_registry v1 JSON file from local game data. Stores names, hashes and header metadata only.

usage:
  python -B tools/asset_registry/registry.py --game q2 --root E:/_SOURCE/_ASSETS/Q2        [--out FILE | --stdout]
  python -B tools/asset_registry/registry.py --game q3 --root E:/_SOURCE/_ASSETS/Q3A       [--out FILE | --stdout]
  python -B tools/asset_registry/registry.py --game q1 --q1-wad PATH/TO/textures.wad       [--out FILE | --stdout]

Default output: out/assets/<game>/registry.json under the repository. Decision D9 (tracking retail texture
metadata) is pending, so --out must stay under out/assets/; --stdout writes nowhere. The tool never copies
asset bytes: each entry holds its name, relative source path, sha256, byte size, image dimensions and the
header fields listed below.

  q2  textures/**/*.wal  -> width, height, internal name, animname, flags (SURF_*), contents (CONTENTS_*), value
      textures/** images -> format, width, height (replacement images; Quake II 3.19 loads only .wal)
      env/* sky faces    -> <name><rt|bk|lf|ft|up|dn>.pcx/.tga sets
  q3  scripts/*.shader   -> shader name, file:line, surfaceparms, q3map_surfacelight, skyparms, stage summary
      every image in tree -> format, width, height (TGA pixel depth and alpha bits)
  q1  a user-supplied WAD2 -> miptex lump names, sha256 of each lump, width, height

Extracted trees only: a .pak (Q2) or .pk3 (Q3) archive in the root is refused, so a registry is never silently
partial. Output is deterministic: sorted walks, relative paths, sorted JSON keys, LF line endings.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
SCHEMA = "qmg.asset_registry"
SCHEMA_VERSION = 1
GAMES = {"q1": "quake1", "q2": "quake2", "q3": "quake3"}
LICENCE_STATUS = "retail game data, not verified or cleared for redistribution; registry holds metadata only"
IMAGE_EXTS = {".tga": "tga", ".jpg": "jpeg", ".jpeg": "jpeg", ".pcx": "pcx", ".png": "png", ".bmp": "bmp"}
SKY_SUFFIXES = ("rt", "bk", "lf", "ft", "up", "dn")

# Quake II surface and contents bits. Citations are id source (Quake-2-master and the Q3 bspc copy of the Q2
# tool header) unless named otherwise.
Q2_SURF = [
    (0x1, "SURF_LIGHT", "q2:qcommon/qfiles.h:367"),
    (0x2, "SURF_SLICK", "q2:qcommon/qfiles.h:369"),
    (0x4, "SURF_SKY", "q2:qcommon/qfiles.h:371"),
    (0x8, "SURF_WARP", "q2:qcommon/qfiles.h:372"),
    (0x10, "SURF_TRANS33", "q2:qcommon/qfiles.h:373"),
    (0x20, "SURF_TRANS66", "q2:qcommon/qfiles.h:374"),
    (0x40, "SURF_FLOWING", "q2:qcommon/qfiles.h:375"),
    (0x80, "SURF_NODRAW", "q2:qcommon/qfiles.h:376"),
    (0x100, "SURF_HINT", "q3:code/bspc/q2files.h:381"),
    (0x200, "SURF_SKIP", "q3:code/bspc/q2files.h:382"),
]
Q2_CONTENTS = [
    (0x1, "CONTENTS_SOLID", "q2:qcommon/qfiles.h:333"),
    (0x2, "CONTENTS_WINDOW", "q2:qcommon/qfiles.h:334"),
    (0x4, "CONTENTS_AUX", "q2:qcommon/qfiles.h:335"),
    (0x8, "CONTENTS_LAVA", "q2:qcommon/qfiles.h:336"),
    (0x10, "CONTENTS_SLIME", "q2:qcommon/qfiles.h:337"),
    (0x20, "CONTENTS_WATER", "q2:qcommon/qfiles.h:338"),
    (0x40, "CONTENTS_MIST", "q2:qcommon/qfiles.h:339"),
    (0x8000, "CONTENTS_AREAPORTAL", "q2:qcommon/qfiles.h:344"),
    (0x10000, "CONTENTS_PLAYERCLIP", "q2:qcommon/qfiles.h:346"),
    (0x20000, "CONTENTS_MONSTERCLIP", "q2:qcommon/qfiles.h:347"),
    (0x40000, "CONTENTS_CURRENT_0", "q2:qcommon/qfiles.h:350"),
    (0x80000, "CONTENTS_CURRENT_90", "q2:qcommon/qfiles.h:351"),
    (0x100000, "CONTENTS_CURRENT_180", "q2:qcommon/qfiles.h:352"),
    (0x200000, "CONTENTS_CURRENT_270", "q2:qcommon/qfiles.h:353"),
    (0x400000, "CONTENTS_CURRENT_UP", "q2:qcommon/qfiles.h:354"),
    (0x800000, "CONTENTS_CURRENT_DOWN", "q2:qcommon/qfiles.h:355"),
    (0x1000000, "CONTENTS_ORIGIN", "q2:qcommon/qfiles.h:357"),
    (0x2000000, "CONTENTS_MONSTER", "q2:qcommon/qfiles.h:359"),
    (0x4000000, "CONTENTS_DEADMONSTER", "q2:qcommon/qfiles.h:360"),
    (0x8000000, "CONTENTS_DETAIL", "q2:qcommon/qfiles.h:361"),
    (0x10000000, "CONTENTS_TRANSLUCENT", "q2:qcommon/qfiles.h:362"),
    (0x20000000, "CONTENTS_LADDER", "q2:qcommon/qfiles.h:363"),
]
CITATIONS = {
    "q2_wal_header": "q2:qcommon/qfiles.h:196-207 (miptex_t: name[32], width, height, offsets[4], animname[32], flags, contents, value)",
    "q2_texture_path": "q2:ref_gl/gl_model.c:470 (textures/%s.wal; 3.19 loads no other texture format)",
    "q2_sky_path": "q2:ref_gl/gl_warp.c:625,642-644 (env/<sky><rt|bk|lf|ft|up|dn>.pcx|.tga)",
    "q3_shader_lookup": "q3:code/renderer/tr_shader.c:2465 FindShaderInShaderText, then 2485-2491 an implicit shader from <name>.tga",
    "q3_tga_jpg_fallback": "q3:code/renderer/tr_image.c:1827-1837 (a .tga that is absent is loaded as .jpg)",
    "q3_skyparms": "q3:code/renderer/tr_shader.c:1212-1235 (skyParms <farbox> <cloudheight> <nearbox>; env/<farbox>_<rt|bk|lf|ft|up|dn>.tga)",
    "q3_surfaceparms": "q3:q3map/shaders.c:69-112 (infoParms)",
    "q3_shader_text_order": "q3:code/renderer/tr_shader.c:2857-2891 (every scripts/*.shader file is read; first definition found wins)",
    "q1_wad2": "q1:WinQuake/wad.h:32-37,47-63 (wadinfo_t, lumpinfo_t, TYP_MIPTEX 68)",
    "q1_miptex": "q1:WinQuake/bspfile.h:104-109 (miptex_t: name[16], width, height, offsets[4])",
    "roots": {"q1": "E:/_SOURCE/_CODE/Quake-master", "q2": "E:/_SOURCE/_CODE/Quake-2-master",
              "q3": "E:/_SOURCE/_CODE/Quake-III-Arena-master"},
}


class RegistryError(Exception):
    pass


# --------------------------------------------------------------------------- hashing and walking

def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tool_sha256() -> str:
    return sha256_file(Path(__file__).resolve())


def walk(root: Path, subdir: str) -> list[str]:
    """Sorted posix paths relative to root for every regular file under root/subdir."""
    base = root / subdir
    found: list[str] = []
    if not base.is_dir():
        return found
    for dirpath, dirnames, filenames in os.walk(base):
        dirnames.sort()
        for filename in sorted(filenames):
            full = Path(dirpath) / filename
            if full.is_file():
                found.append(full.relative_to(root).as_posix())
    return sorted(found)


def refuse_archives(root: Path, suffix: str) -> None:
    archives = sorted(p.name for p in root.iterdir() if p.is_file() and p.suffix.lower() == suffix)
    if archives:
        raise RegistryError(f"{root} holds {suffix} archives ({', '.join(archives)}); this tool reads extracted trees only "
                            "and would otherwise write a partial registry")


def decode_bits(value: int, table) -> tuple[list[str], list[str]]:
    names, unknown = [], []
    remaining = value & 0xFFFFFFFF
    for bit, name, _ in table:
        if remaining & bit:
            names.append(name)
            remaining &= ~bit
    bit = 1
    while remaining:
        if remaining & bit:
            unknown.append(f"0x{bit:x}")
            remaining &= ~bit
        bit <<= 1
    return names, unknown


# --------------------------------------------------------------------------- binary headers (no pixels kept)

def c_string(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("latin-1")


def parse_wal_header(head: bytes, size: int) -> dict:
    if len(head) < 100:
        return {"header_valid": False, "header_error": "shorter than the 100-byte miptex_t header"}
    width, height = struct.unpack_from("<II", head, 32)
    offsets = list(struct.unpack_from("<4I", head, 40))
    flags, contents, value = struct.unpack_from("<iii", head, 88)
    info = {
        "internal_name": c_string(head[0:32]),
        "width": width,
        "height": height,
        "animname": c_string(head[56:88]),
        "flags": flags & 0xFFFFFFFF,
        "contents": contents & 0xFFFFFFFF,
        "value": value,
    }
    surf, surf_unknown = decode_bits(flags, Q2_SURF)
    cont, cont_unknown = decode_bits(contents, Q2_CONTENTS)
    info["surf"] = surf
    info["contents_flags"] = cont
    if surf_unknown:
        info["surf_unknown_bits"] = surf_unknown
    if cont_unknown:
        info["contents_unknown_bits"] = cont_unknown
    expected = [100]
    for level in range(1, 4):
        expected.append(expected[-1] + (width >> (level - 1)) * (height >> (level - 1)))
    end = expected[3] + (width >> 3) * (height >> 3)
    valid = width > 0 and height > 0 and offsets == expected and end <= size
    info["header_valid"] = valid
    if not valid:
        info["header_error"] = f"mip offsets {offsets} do not match {expected} for {width}x{height} in {size} bytes"
    return info


def parse_tga_header(head: bytes) -> dict:
    if len(head) < 18:
        return {"header_valid": False, "header_error": "shorter than the 18-byte TGA header"}
    image_type = head[2]
    width, height = struct.unpack_from("<HH", head, 12)
    depth, descriptor = head[16], head[17]
    valid = image_type in (1, 2, 3, 9, 10, 11) and width > 0 and height > 0
    info = {"width": width, "height": height, "tga_image_type": image_type, "tga_pixel_depth": depth,
            "tga_alpha_bits": descriptor & 0x0F, "header_valid": valid}
    if not valid:
        info["header_error"] = f"TGA image type {image_type} or size {width}x{height} is not a usable image"
    return info


JPEG_SOF = {0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF}


def parse_jpeg_header(path: Path) -> dict:
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:2] != b"\xff\xd8":
        return {"header_valid": False, "header_error": "missing JPEG SOI marker"}
    pos = 2
    while pos + 4 <= len(data):
        if data[pos] != 0xFF:
            return {"header_valid": False, "header_error": f"JPEG marker expected at byte {pos}"}
        marker = data[pos + 1]
        if marker == 0xFF:
            pos += 1
            continue
        if marker in (0x01,) or 0xD0 <= marker <= 0xD8:
            pos += 2
            continue
        if marker == 0xD9:
            break
        length = struct.unpack_from(">H", data, pos + 2)[0]
        if marker in JPEG_SOF:
            if pos + 10 > len(data):
                break
            height, width = struct.unpack_from(">HH", data, pos + 5)
            return {"width": width, "height": height, "jpeg_components": data[pos + 9],
                    "jpeg_progressive": marker == 0xC2, "header_valid": width > 0 and height > 0}
        pos += 2 + length
    return {"header_valid": False, "header_error": "no JPEG SOF marker found"}


def parse_pcx_header(head: bytes) -> dict:
    if len(head) < 128 or head[0] != 0x0A:
        return {"header_valid": False, "header_error": "not a PCX header"}
    xmin, ymin, xmax, ymax = struct.unpack_from("<HHHH", head, 4)
    width, height = xmax - xmin + 1, ymax - ymin + 1
    return {"width": width, "height": height, "pcx_bits_per_pixel": head[3], "pcx_planes": head[65],
            "header_valid": width > 0 and height > 0}


def parse_png_header(head: bytes) -> dict:
    if len(head) < 24 or head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
        return {"header_valid": False, "header_error": "not a PNG header"}
    width, height = struct.unpack_from(">II", head, 16)
    return {"width": width, "height": height, "header_valid": width > 0 and height > 0}


def parse_bmp_header(head: bytes) -> dict:
    if len(head) < 26 or head[:2] != b"BM":
        return {"header_valid": False, "header_error": "not a BMP header"}
    width, height = struct.unpack_from("<ii", head, 18)
    return {"width": width, "height": abs(height), "header_valid": width > 0 and height != 0}


def image_record(root: Path, rel: str) -> dict:
    path = root / rel
    ext = Path(rel).suffix.lower()
    fmt = IMAGE_EXTS[ext]
    with open(path, "rb") as handle:
        head = handle.read(128)
    if fmt == "tga":
        info = parse_tga_header(head)
    elif fmt == "jpeg":
        info = parse_jpeg_header(path)
    elif fmt == "pcx":
        info = parse_pcx_header(head)
    elif fmt == "png":
        info = parse_png_header(head)
    else:
        info = parse_bmp_header(head)
    record = {"source": rel, "format": fmt, "sha256": sha256_file(path), "bytes": path.stat().st_size}
    record.update(info)
    return record


def strip_ext(rel: str) -> str:
    return rel[: -len(Path(rel).suffix)] if Path(rel).suffix else rel


# --------------------------------------------------------------------------- Quake II

def build_q2(root: Path) -> dict:
    refuse_archives(root, ".pak")
    origin = f"local install tree '{root.name}' (Quake II game data; extraction not verified against a retail pak)"
    names: dict[str, dict] = {}
    other_files: list[str] = []
    hashed = []
    for rel in walk(root, "textures"):
        ext = Path(rel).suffix.lower()
        name = strip_ext(rel)[len("textures/"):]
        key = name.lower()
        if ext == ".wal":
            path = root / rel
            with open(path, "rb") as handle:
                head = handle.read(100)
            wal = {"source": rel, "sha256": sha256_file(path), "bytes": path.stat().st_size}
            wal.update(parse_wal_header(head, wal["bytes"]))
            if wal.get("internal_name") is not None and wal["internal_name"].lower() != key:
                wal["internal_name_mismatch"] = True
            entry = names.setdefault(key, {"key": key, "images": []})
            entry["name"] = name
            entry["wal"] = wal
            hashed.append((rel, wal["sha256"]))
        elif ext in IMAGE_EXTS:
            image = image_record(root, rel)
            entry = names.setdefault(key, {"key": key, "images": []})
            entry.setdefault("name", name)
            entry["images"].append(image)
            hashed.append((rel, image["sha256"]))
        else:
            other_files.append(rel)
    textures = []
    for key in sorted(names):
        entry = names[key]
        entry.setdefault("wal", None)
        entry["images"].sort(key=lambda item: item["source"])
        entry["source"] = entry["wal"]["source"] if entry["wal"] else entry["images"][0]["source"]
        entry["origin"] = origin
        entry["licence_status"] = LICENCE_STATUS
        textures.append(entry)

    skies: dict[str, dict] = {}
    for rel in walk(root, "env"):
        stem = Path(rel).stem
        ext = Path(rel).suffix.lower()
        if ext not in (".pcx", ".tga") or len(stem) < 3 or stem[-2:].lower() not in SKY_SUFFIXES:
            other_files.append(rel)
            continue
        base, face = stem[:-2], stem[-2:].lower()
        image = image_record(root, rel)
        hashed.append((rel, image["sha256"]))
        sky = skies.setdefault(base.lower(), {"name": base, "key": base.lower(), "faces": {}})
        sky["faces"].setdefault(face, {})[ext[1:]] = image
    sky_list = []
    for key in sorted(skies):
        sky = skies[key]
        sky["complete_pcx"] = all("pcx" in sky["faces"].get(face, {}) for face in SKY_SUFFIXES)
        sky["complete_tga"] = all("tga" in sky["faces"].get(face, {}) for face in SKY_SUFFIXES)
        first_face = sorted(sky["faces"])[0]
        sky["source"] = sorted(sky["faces"][first_face].values(), key=lambda item: item["source"])[0]["source"]
        sky["origin"] = origin
        sky["licence_status"] = LICENCE_STATUS
        sky_list.append(sky)

    with_wal = sum(1 for entry in textures if entry["wal"])
    counts = {
        "texture_names": len(textures),
        "with_wal": with_wal,
        "image_only": len(textures) - with_wal,
        "wal_header_invalid": sum(1 for entry in textures if entry["wal"] and not entry["wal"]["header_valid"]),
        "wal_internal_name_mismatch": sum(1 for entry in textures if entry["wal"] and entry["wal"].get("internal_name_mismatch")),
        "images": sum(len(entry["images"]) for entry in textures),
        "skies": len(sky_list),
        "other_files": len(other_files),
    }
    return {
        "game": GAMES["q2"],
        "source": {"kind": "extracted_tree", "layout": "baseq2", "files_hashed": len(hashed),
                   "content_sha256": content_digest(hashed)},
        "bit_tables": {"surf": [[f"0x{bit:x}", name, cite] for bit, name, cite in Q2_SURF],
                       "contents": [[f"0x{bit:x}", name, cite] for bit, name, cite in Q2_CONTENTS]},
        "counts": counts,
        "textures": textures,
        "skies": sky_list,
        "other_files": other_files,
    }


def content_digest(hashed: list[tuple[str, str]]) -> str:
    lines = "".join(f"{sha} {rel}\n" for rel, sha in sorted(hashed))
    return sha256_bytes(lines.encode("utf-8"))


# --------------------------------------------------------------------------- Quake III shaders

def tokenize_shader(text: str):
    """Yield (token, line) pairs and ('\\n', line) markers. Handles // and /* */ comments and quoted strings;
    braces are always separate tokens."""
    i, line, n = 0, 1, len(text)
    while i < n:
        ch = text[i]
        if ch == "\n":
            yield "\n", line
            line += 1
            i += 1
        elif ch in " \t\r\f\v":
            i += 1
        elif text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            for c in text[i:end]:
                if c == "\n":
                    yield "\n", line
                    line += 1
            i = end
        elif ch in "{}":
            yield ch, line
            i += 1
        elif ch == '"':
            end = text.find('"', i + 1)
            end = n if end < 0 else end
            yield text[i + 1:end], line
            i = end + 1
        else:
            start = i
            while i < n and text[i] not in " \t\r\n\f\v{}\"" and not text.startswith("//", i) and not text.startswith("/*", i):
                i += 1
            yield text[start:i], line


def number(token: str):
    try:
        value = float(token)
    except ValueError:
        return None
    return int(value) if value.is_integer() else value


BLEND_SHORTHAND = {"add": ["GL_ONE", "GL_ONE"], "filter": ["GL_DST_COLOR", "GL_ZERO"],
                   "blend": ["GL_SRC_ALPHA", "GL_ONE_MINUS_SRC_ALPHA"]}


def summarize_stage(line: int, directives: list[list]) -> dict:
    stage = {"line": line, "maps": [], "map_kind": None, "blendfunc": None, "tcgen": None, "tcmods": [],
             "rgbgen": None, "alphagen": None, "alphafunc": None, "depthwrite": False, "detail": False}
    for keyword, args, _ in directives:
        if keyword in ("map", "clampmap", "videomap") and args:
            stage["map_kind"] = keyword
            stage["maps"] = [args[0]]
        elif keyword == "animmap" and len(args) >= 2:
            stage["map_kind"] = "animmap"
            stage["animmap_frequency"] = number(args[0])
            stage["maps"] = list(args[1:])
        elif keyword == "blendfunc" and args:
            stage["blendfunc"] = BLEND_SHORTHAND.get(args[0].lower(), [a.upper() for a in args[:2]])
        elif keyword == "tcgen" and args:
            stage["tcgen"] = args[0].lower()
        elif keyword == "tcmod" and args:
            stage["tcmods"].append(args[0].lower())
        elif keyword == "rgbgen" and args:
            stage["rgbgen"] = args[0].lower()
        elif keyword == "alphagen" and args:
            stage["alphagen"] = args[0].lower()
        elif keyword == "alphafunc" and args:
            stage["alphafunc"] = args[0].upper()
        elif keyword == "depthwrite":
            stage["depthwrite"] = True
        elif keyword == "detail":
            stage["detail"] = True
    stage["directives"] = [[keyword] + args for keyword, args, _ in directives]
    return stage


def parse_shader_file(text: str, file_label: str) -> tuple[list[dict], list[dict]]:
    tokens = list(tokenize_shader(text))
    shaders: list[dict] = []
    errors: list[dict] = []
    i, n = 0, len(tokens)

    def skip_newlines(index: int) -> int:
        while index < n and tokens[index][0] == "\n":
            index += 1
        return index

    while True:
        i = skip_newlines(i)
        if i >= n:
            break
        name, name_line = tokens[i]
        if name in "{}":
            errors.append({"file": file_label, "line": name_line, "error": f"unexpected '{name}' where a shader name belongs"})
            i += 1
            # skip a stray block
            if name == "{":
                depth = 1
                while i < n and depth:
                    depth += {"{": 1, "}": -1}.get(tokens[i][0], 0)
                    i += 1
            continue
        i = skip_newlines(i + 1)
        if i >= n or tokens[i][0] != "{":
            errors.append({"file": file_label, "line": name_line, "error": f"shader '{name}' has no body"})
            continue
        body_start_line = tokens[i][1]
        i += 1
        directives: list[list] = []
        stages: list[dict] = []
        current: list = []
        stage_directives: list[list] | None = None
        stage_line = 0
        closed = False
        malformed = False

        def flush_line():
            nonlocal current
            if current:
                keyword = current[0][0].lower()
                args = [tok for tok, _ in current[1:]]
                target = stage_directives if stage_directives is not None else directives
                target.append([keyword, args, current[0][1]])
            current = []

        while i < n:
            tok, tok_line = tokens[i]
            i += 1
            if tok == "\n":
                flush_line()
            elif tok == "{":
                flush_line()
                if stage_directives is not None:
                    errors.append({"file": file_label, "line": tok_line, "error": f"nested stage in shader '{name}'"})
                    malformed = True
                    break
                stage_directives, stage_line = [], tok_line
            elif tok == "}":
                flush_line()
                if stage_directives is not None:
                    stages.append(summarize_stage(stage_line, stage_directives))
                    stage_directives = None
                else:
                    closed = True
                    break
            else:
                current.append((tok, tok_line))
        if not closed:
            if not malformed:
                errors.append({"file": file_label, "line": name_line, "error": f"shader '{name}' is not closed"})
            # resynchronise: skip to the end of the enclosing block
            depth = 1 if stage_directives is None else 2
            while i < n and depth:
                depth += {"{": 1, "}": -1}.get(tokens[i][0], 0)
                i += 1
            continue
        record = {"name": name, "key": name.replace("\\", "/").lower(), "file": file_label, "line": name_line,
                  "body_line": body_start_line, "surfaceparms": [], "q3map_surfacelight": None, "skyparms": None,
                  "q3map_lightimage": None, "qer_editorimage": None, "deformvertexes": [], "fogparms": False}
        for keyword, args, _ in directives:
            if keyword == "surfaceparm" and args:
                parm = args[0].lower()
                if parm not in record["surfaceparms"]:
                    record["surfaceparms"].append(parm)
            elif keyword == "q3map_surfacelight" and args:
                record["q3map_surfacelight"] = number(args[0])
            elif keyword == "skyparms":
                record["skyparms"] = {"farbox": args[0] if len(args) > 0 else None,
                                      "cloudheight": args[1] if len(args) > 1 else None,
                                      "nearbox": args[2] if len(args) > 2 else None}
            elif keyword == "q3map_lightimage" and args:
                record["q3map_lightimage"] = args[0]
            elif keyword == "qer_editorimage" and args:
                record["qer_editorimage"] = args[0]
            elif keyword == "deformvertexes" and args:
                record["deformvertexes"].append(args[0].lower())
            elif keyword == "fogparms":
                record["fogparms"] = True
        record["directives"] = [[keyword] + args for keyword, args, _ in directives]
        record["stages"] = stages
        shaders.append(record)
    return shaders, errors


def build_q3(root: Path) -> dict:
    refuse_archives(root, ".pk3")
    origin = f"local install tree '{root.name}' (Quake III Arena game data; extraction not verified against a retail pk3)"
    shaders: list[dict] = []
    parse_errors: list[dict] = []
    hashed = []
    shader_files = [rel for rel in walk(root, "scripts") if rel.lower().endswith(".shader")]
    for rel in shader_files:
        raw = (root / rel).read_bytes()
        file_sha = sha256_bytes(raw)
        hashed.append((rel, file_sha))
        found, errors = parse_shader_file(raw.decode("latin-1"), rel)
        for record in found:
            record["origin"] = origin
            record["licence_status"] = LICENCE_STATUS
            record["source"] = f"{rel}:{record['line']}"
        shaders.extend(found)
        parse_errors.extend(errors)
    # Duplicate definitions: keep every one, mark the first in sorted file order (and file position) as active.
    by_key: dict[str, list[dict]] = {}
    for record in shaders:
        by_key.setdefault(record["key"], []).append(record)
    for key, records in by_key.items():
        for index, record in enumerate(records):
            record["definition_index"] = index
            record["definitions"] = len(records)
    shaders.sort(key=lambda record: (record["key"], record["definition_index"]))

    # Every image in the tree: shader stages also reference models/, sprites/, gfx/ and other directories.
    images = []
    for rel in walk(root, ""):
        if Path(rel).suffix.lower() not in IMAGE_EXTS:
            continue
        image = image_record(root, rel)
        image["key"] = strip_ext(rel).lower()
        image["origin"] = origin
        image["licence_status"] = LICENCE_STATUS
        images.append(image)
        hashed.append((rel, image["sha256"]))
    images.sort(key=lambda image: (image["key"], image["source"]))
    counts = {
        "shader_files": len(shader_files),
        "shader_definitions": len(shaders),
        "shader_names": len(by_key),
        "duplicate_names": sum(1 for records in by_key.values() if len(records) > 1),
        "parse_errors": len(parse_errors),
        "images": len(images),
        "image_header_invalid": sum(1 for image in images if not image.get("header_valid")),
        "with_surfacelight": sum(1 for record in shaders if (record["q3map_surfacelight"] or 0) > 0),
        "with_skyparms": sum(1 for record in shaders if record["skyparms"]),
    }
    return {
        "game": GAMES["q3"],
        "source": {"kind": "extracted_tree", "layout": "baseq3", "files_hashed": len(hashed),
                   "content_sha256": content_digest(hashed), "shader_files": shader_files},
        "counts": counts,
        "shaders": shaders,
        "images": images,
        "parse_errors": parse_errors,
    }


# --------------------------------------------------------------------------- Quake (WAD2)

TYP_MIPTEX = 68


def build_q1(wad: Path) -> dict:
    data = wad.read_bytes()
    if len(data) < 12 or data[:4] not in (b"WAD2", b"2DAW"):
        raise RegistryError(f"{wad} is not a WAD2 file")
    numlumps, infotableofs = struct.unpack_from("<ii", data, 4)
    if numlumps < 0 or infotableofs < 12 or infotableofs + numlumps * 32 > len(data):
        raise RegistryError(f"{wad} has a lump directory outside the file")
    origin = f"user-supplied WAD '{wad.name}' (origin, licence and author to be recorded per decision D9)"
    textures, other_lumps, errors = [], [], []
    for index in range(numlumps):
        filepos, disksize, size, lump_type, compression = struct.unpack_from("<iiibb", data, infotableofs + index * 32)
        name = c_string(data[infotableofs + index * 32 + 16: infotableofs + index * 32 + 32])
        if lump_type != TYP_MIPTEX:
            other_lumps.append({"name": name, "type": lump_type})
            continue
        if compression != 0 or filepos < 0 or filepos + disksize > len(data) or disksize < 40:
            errors.append({"lump": name, "error": "compressed or out-of-range miptex lump"})
            continue
        lump = data[filepos:filepos + disksize]
        width, height = struct.unpack_from("<II", lump, 16)
        offsets = list(struct.unpack_from("<4I", lump, 24))
        expected = [40]
        for level in range(1, 4):
            expected.append(expected[-1] + (width >> (level - 1)) * (height >> (level - 1)))
        textures.append({"name": name, "key": name.lower(), "internal_name": c_string(lump[:16]),
                         "source": f"{wad.name}#{name}", "sha256": sha256_bytes(lump), "bytes": disksize,
                         "width": width, "height": height, "header_valid": offsets == expected,
                         "origin": origin, "licence_status": LICENCE_STATUS})
    textures.sort(key=lambda entry: (entry["key"], entry["name"]))
    return {
        "game": GAMES["q1"],
        "source": {"kind": "wad2", "wad_name": wad.name, "wad_sha256": sha256_bytes(data), "wad_bytes": len(data),
                   "lumps": numlumps},
        "counts": {"miptex": len(textures), "other_lumps": len(other_lumps), "errors": len(errors)},
        "textures": textures,
        "other_lumps": other_lumps,
        "parse_errors": errors,
    }


# --------------------------------------------------------------------------- output

def build(game: str, root: Path | None, wad: Path | None) -> dict:
    if game == "q1":
        if wad is None:
            raise RegistryError("--game q1 needs --q1-wad (no Q1 texture WAD has been supplied; decision D9 is pending)")
        body = build_q1(wad)
    else:
        if root is None or not root.is_dir():
            raise RegistryError(f"--root must name the extracted {game} game directory (got {root})")
        body = build_q2(root) if game == "q2" else build_q3(root)
    registry = {
        "schema": SCHEMA,
        "schema_version": SCHEMA_VERSION,
        "policy": {
            "asset_bytes": "none: names, relative paths, sha256, sizes, dimensions and header metadata only",
            "d9": "pending: written under out/assets only and not tracked; its sha256 is recorded in tools/asset_registry/README.md",
        },
        "tool": {"path": "tools/asset_registry/registry.py", "sha256": tool_sha256()},
        "citations": CITATIONS,
    }
    registry.update(body)
    return registry


def serialize(registry: dict) -> bytes:
    return (json.dumps(registry, indent=1, sort_keys=True, ensure_ascii=True) + "\n").encode("ascii")


def default_out(game: str) -> Path:
    return REPO / "out" / "assets" / game / "registry.json"


def check_out_path(path: Path) -> Path:
    resolved = path.resolve()
    allowed = (REPO / "out" / "assets").resolve()
    if allowed != resolved and allowed not in resolved.parents:
        raise RegistryError(f"refusing to write {resolved}: decision D9 is pending, so a registry may be written only "
                            f"under {allowed} (or use --stdout)")
    return resolved


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game", choices=sorted(GAMES), required=True)
    parser.add_argument("--root", type=Path, help="extracted game directory (q2: baseq2 layout, q3: baseq3 layout)")
    parser.add_argument("--q1-wad", type=Path, help="user-supplied Quake texture WAD2 (q1 only)")
    target = parser.add_mutually_exclusive_group()
    target.add_argument("--out", type=Path, help="output file under out/assets/ (default out/assets/<game>/registry.json)")
    target.add_argument("--stdout", action="store_true", help="write the registry to standard output only")
    args = parser.parse_args(argv)
    try:
        registry = build(args.game, args.root, args.q1_wad)
        payload = serialize(registry)
        if args.stdout:
            sys.stdout.buffer.write(payload)
            sys.stdout.buffer.flush()
            return 0
        out = check_out_path(args.out if args.out else default_out(args.game))
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(payload)
    except RegistryError as error:
        print(f"asset_registry: error: {error}", file=sys.stderr)
        return 2
    print(f"{out.relative_to(REPO.resolve()).as_posix() if REPO.resolve() in out.parents else out} "
          f"sha256={sha256_bytes(payload)} bytes={len(payload)} counts={json.dumps(registry['counts'], sort_keys=True)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
