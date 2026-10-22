#!/usr/bin/env python3
"""Benchmark runner v1 for Quake-MapGen (stdlib only).

For every manifest case this runs the normal prompt-driven CLI
(``qmg generate --prompt ... --seed ... --output <id>.map``, or the case's
recorded ``qmg_args``; never ``--game``/``--mode``), parses the report and the
emitted .map, compiles with full VIS and final light (plus BSPC AAS for Quake 3),
inspects the binary outputs itself, evaluates gates G1-G15 and writes
``result.json``. A summary (``summary.json`` / ``summary.md``) is rebuilt over
every case of the manifest that has a result. The summary refuses results
written by a different runner (sha256 of this file) or a different qmg.exe.

Gate policies:
  --policy v1     (default) G1-G7 hard, with the v1 G4 (anchored full-VIS marker, -fast rejected) and
                  G5 (anchored "0 empty lightmaps" line, nonempty lump, light budget); G8-G15 reported.
  --policy draft  G1-G7 exactly as draft-v0 defined them; G8-G15 still reported.

Other modes: --controls (gate controls), --import <run-dir> (re-evaluate an earlier run's compiled
outputs without recompiling), --generate-only, --repeat 2 (G8 determinism), --summary-only.

Outputs stay local under ``out/`` (git-ignored). See tools/benchmark/README.md.
"""
from __future__ import annotations

import argparse
import collections
import concurrent.futures
import copy
import datetime as _dt
import hashlib
import json
import math
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import threading
import time
import zipfile
from pathlib import Path

RUNNER_PATH = Path(__file__).resolve()
RUNNER_SHA256 = hashlib.sha256(RUNNER_PATH.read_bytes()).hexdigest()
# v1: the runner version is the sha256 of this file, so every result identifies the runner code.
RUNNER_VERSION = RUNNER_SHA256
DRAFT_RUNNER_VERSION = "0.1.0"
REPO = RUNNER_PATH.parents[2]
BENCH_DIR = REPO / "tools" / "benchmark"
DEFAULT_MANIFEST = BENCH_DIR / "rc1-v1.json"
DEFAULT_OUTPUT = REPO / "out" / "benchmark-v1" / "run"
DEFAULT_CONTROLS = REPO / "out" / "benchmark-v1" / "controls"
REGISTRY_PATH = BENCH_DIR / "manifests.json"
SEALED_PATH = BENCH_DIR / "sealed.json"
DEFAULT_QMG = REPO / "build" / "windows-clang" / "qmg.exe"
DEFAULT_ERICW = Path("E:/_SOURCE/_CODE/ericw-tools-2.0.0-alpha11")
DEFAULT_Q3TOOLS = Path("E:/_SOURCE/_CODE/netradiant-custom-449778b-m2-build/install")
DEFAULT_Q3RUNTIME = Path("C:/msys64/mingw64/bin")
DEFAULT_Q1_ASSETS = Path("E:/_SOURCE/_ASSETS/Q1")
DEFAULT_Q2_ASSETS = Path("E:/_SOURCE/_ASSETS/Q2")
DEFAULT_Q3_BASEPATH = Path("E:/_SOURCE/_ASSETS")
DEFAULT_Q3_GAME = "Q3A"
STAGE_TIMEOUT = 600
DEFAULT_LIGHT_BUDGET = 1200.0  # plan 8.3 (provisional until frozen under 8 jobs x 2 threads)
TOOL_THREADS = 2

# Pins: ericw-tools from tools/mp_portfolio/compile.ps1 and
# tools/map_recipes/build.ps1; q3map2/BSPC from the configured CMake cache
# (QMG_Q3MAP2_SHA256 / QMG_BSPC_SHA256).
TOOL_PINS = {
    "qbsp": "fad5a4dc4daf42773ad9a812349b2667221bec47a920cad10cb821ae442c8eb8",
    "vis": "01be572845e05c87b01a0b667e590ac49bdaba23369af8529fe82fcfbc4fe6cf",
    "light": "8e4fe9821f73c2d5c8a13655be20d3a71f357e76171de37c02413f4da3131a2f",
    "q3map2": "7cc3963b21fa937880935e818624eb28c5424f7ff312148b13468e7afb1a71e7",
    "mbspc": "77c9b9cac946e7d770bce9d934404b421e51ebe01963cd864c9d0e245243b003",
}

GAME_NAMES = {"q1": "quake1", "q2": "quake2", "q3": "quake3"}
# Target mode -> generator GameMode vocabulary (include/qmg/types.hpp GameMode).
TARGET_MODES = {
    "sp": "single_player", "dm": "deathmatch", "ffa": "deathmatch", "duel": "duel",
    "tourney": "duel", "ctf": "capture_the_flag",
    "coop": None, "tdm": None, "botarena": None, "ctf-threewave": None,
}
# v1 G2 mapping, extended with the D4 GameMode tokens. With schema-v4 reports (no such modes, no
# module field) it gives exactly the draft verdicts; ctf-threewave additionally needs a reported module.
TARGET_MODES_V1 = {
    "sp": "single_player", "dm": "deathmatch", "ffa": "deathmatch", "duel": "duel", "tourney": "duel",
    "ctf": "capture_the_flag", "coop": "cooperative", "tdm": "team_deathmatch", "botarena": "bot_arena",
    "ctf-threewave": "capture_the_flag",
}
MODULE_REQUIRED_MODES = {"ctf-threewave": "q1-threewave-5.0"}
GENERATOR_MODES = {"single_player", "deathmatch", "duel", "capture_the_flag"}
NOT_REPRESENTABLE_REQUEST_MODES = {"coop", "team_deathmatch", "bot_arena", "threewave_ctf"}
# src/prompt.cpp detect_theme vocabulary.
GENERATOR_THEMES = {"gothic", "medieval", "techbase", "industrial", "sewer", "waterworks",
                    "lava fortress", "toxic facility", "space", "ruins", "temple"}

INVALID_BRUSH_MARKERS = ["invalid brush", "brush with no planes", "brush with no faces",
                         "no visible sides on brush", "outside world", "textures/textures/", "max_map"]
LEAK_MARKERS = ["leaked", "leak file written", "aborting because -leaktest was used", "stopped by leak"]
CLASSIC_TOOL_FAILURE_MARKERS = ["************ error", "error occurred when trying to parse arguments",
                                "unknown option", "loadprtfile: unknown header", "no vis portals"]
BSPC_FAILURE_MARKERS = ["leaked", "no entities inside", "entity reached from outside", "**** error", "error:"]
# q3map2 shader-script parse noise from stock assets (not map diagnostics).
Q3_ASSET_NOISE = re.compile(r"unknown surfaceparm", re.I)
AAS_REACH_TYPES = ["walk", "crouch", "barrier jump", "jump", "ladder", "walk off ledge", "swim",
                   "water jump", "teleport", "elevator", "rocket jump", "bfg jump", "grapple hook",
                   "double jump", "ramp jump", "strafe jump", "jump pad", "func bob"]
AAS_METRICS = ["numareas", "numareasettings", "reachabilitysize", "numnodes", "numportals", "numclusters"]

# Mode-entity requirements (G7). Sources verified by reading the id releases:
#  Q1 coop: Quake-master/qw-qc/client.qc:632 info_player_coop
#  Q1 CTF: quake-rerelease-qc-main/quakec_ctf/client.qc:695 info_player_team1/2,
#          quakec_ctf/teamplay.qc:1198 item_flag_team1/2 (Threewave-derived CTF)
#  Q2 coop/CTF: Quake-2-master/game/g_spawn.c:156, ctf/g_spawn.c:159
#  Q3 TDM: Quake-III-Arena-master/code/game/g_client.c:1076 (team spawns only for >= GT_CTF)
TDM_NA_REASON = {
    "q1": "id1 teamplay spawns at info_player_deathmatch; no team spawn classnames outside the CTF mod",
    "q2": "baseq2 team deathmatch (dmflags skin/model teams) spawns at info_player_deathmatch; "
          "info_player_team1/2 exist only in the ctf game module",
    "q3": "GT_TEAM spawns at info_player_deathmatch; team_CTF_* spawns are selected only for "
          "g_gametype >= GT_CTF (code/game/g_client.c:1076)",
}

HARD_GATE_KEYS = ["G1_generated", "G2_intent_match", "G3_leak_free_compile", "G4_full_vis", "G5_final_light",
                  "G6_aas_valid", "G7_mode_entities"]
REPORTED_GATE_KEYS = ["G8_determinism", "G9_request_accounting", "G10_conflict_surfaced", "G11_error_findings",
                      "G12_materials", "G13_lighting_readability", "G14_engine_smoke",
                      "G15_q3_entity_reachability"]
ALL_GATE_KEYS = HARD_GATE_KEYS + REPORTED_GATE_KEYS
NOT_RUN = "not run"
NOT_CONFIGURED = "not configured"


def now_iso() -> str:
    return _dt.datetime.now().astimezone().isoformat(timespec="seconds")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def tail(text: str, limit: int = 2000) -> str:
    return text if len(text) <= limit else text[-limit:]


def norm_text(text: str) -> str:
    return " ".join(str(text).strip().lower().split())


def rel(path: Path) -> str:
    try:
        return str(Path(path).resolve().relative_to(REPO)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def ensure_inside(path: Path, root: Path) -> Path:
    resolved = path.resolve()
    root_resolved = root.resolve()
    if resolved != root_resolved and root_resolved not in resolved.parents:
        raise RuntimeError(f"refusing to touch {resolved}: outside {root_resolved}")
    probe = resolved
    while probe != probe.parent:
        if probe.exists() and probe.is_symlink():
            raise RuntimeError(f"refusing linked path component: {probe}")
        if probe == root_resolved:
            break
        probe = probe.parent
    return resolved


def write_json(path: Path, value) -> None:
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    os.replace(tmp, path)


# --------------------------------------------------------------------------- process

def run_stage(name: str, command: list, cwd: Path, log_path: Path, env=None, timeout=STAGE_TIMEOUT) -> dict:
    started = time.perf_counter()
    timed_out = False
    try:
        completed = subprocess.run(command, cwd=str(cwd), env=env, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, timeout=timeout)
        exit_code = completed.returncode
        stdout_b, stderr_b = completed.stdout, completed.stderr
    except subprocess.TimeoutExpired as expired:
        timed_out = True
        exit_code = None
        stdout_b, stderr_b = expired.stdout or b"", expired.stderr or b""
    except OSError as error:
        exit_code = None
        stdout_b, stderr_b = b"", str(error).encode()
    wall = round(time.perf_counter() - started, 3)
    stdout = stdout_b.decode("utf-8", errors="replace")
    stderr = stderr_b.decode("utf-8", errors="replace")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with open(log_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("# command: " + json.dumps([str(c) for c in command], ensure_ascii=False) + "\n")
        handle.write(f"# cwd: {cwd}\n# exit: {exit_code} timed_out: {timed_out} wall_seconds: {wall}\n")
        handle.write("# ---- stdout ----\n" + stdout + "\n# ---- stderr ----\n" + stderr + "\n")
    return {"name": name, "command": [str(c) for c in command], "exit_code": exit_code,
            "timed_out": timed_out, "wall_seconds": wall, "log": str(log_path.name),
            "_stdout": stdout, "_stderr": stderr}


def read_stage_log(path: Path, name: str):
    """Rebuild a run_stage() record from the log it wrote (used by --import and the controls)."""
    if not path.is_file():
        return None
    with open(path, "r", encoding="utf-8", errors="replace", newline="") as handle:
        text = handle.read()
    header, sep, rest = text.partition("# ---- stdout ----\n")
    if not sep:
        return None
    marker = "\n# ---- stderr ----\n"
    index = rest.rfind(marker)
    stdout = rest[:index] if index >= 0 else rest
    stderr = rest[index + len(marker):] if index >= 0 else ""
    if stderr.endswith("\n"):
        stderr = stderr[:-1]
    command, exit_code, timed_out, wall = [], None, False, None
    for line in header.splitlines():
        if line.startswith("# command: "):
            command = json.loads(line[len("# command: "):])
        match = re.match(r"# exit: (\S+) timed_out: (\S+) wall_seconds: (\S+)", line)
        if match:
            exit_code = None if match.group(1) == "None" else int(match.group(1))
            timed_out = match.group(2) == "True"
            wall = float(match.group(3))
    return {"name": name, "command": command, "exit_code": exit_code, "timed_out": timed_out,
            "wall_seconds": wall, "log": path.name, "_stdout": stdout, "_stderr": stderr}


def scan_output(text: str, game: str, stage: str) -> dict:
    lines = text.splitlines()
    lower_lines = [line.lower() for line in lines]
    warnings = [line.strip() for line, low in zip(lines, lower_lines) if "warning" in low]
    errors = [line.strip() for line, low in zip(lines, lower_lines) if re.search(r"\berror\b", low)]
    asset_noise = [w for w in warnings if Q3_ASSET_NOISE.search(w)] if game == "q3" else []
    map_warnings = [w for w in warnings if not (game == "q3" and Q3_ASSET_NOISE.search(w))]
    lower = text.lower()
    invalid = [m for m in INVALID_BRUSH_MARKERS if m in lower]
    leak_hits = []
    for low in lower_lines:
        if "leaktest\" was set" in low or low.strip() == "leaktest enabled":
            continue
        for marker in LEAK_MARKERS:
            if marker in low and marker not in leak_hits:
                leak_hits.append(marker)
    tool_failures = []
    if game in ("q1", "q2") and stage in ("vis", "light"):
        tool_failures = [m for m in CLASSIC_TOOL_FAILURE_MARKERS if m in lower]
    if stage.startswith("bspc"):
        tool_failures = [m for m in BSPC_FAILURE_MARKERS if m in lower]
    distinct = collections.Counter(map_warnings)
    return {
        "warnings_count": len(warnings),
        "warnings_first5": warnings[:5],
        "map_warnings_count": len(map_warnings),
        "map_warnings_first5": map_warnings[:5],
        "map_warnings_distinct": [{"line": k, "count": v} for k, v in distinct.most_common(10)],
        "asset_noise_warnings_count": len(asset_noise),
        "errors_count": len(errors),
        "errors_first5": errors[:5],
        "invalid_brush_markers": invalid,
        "leak_markers": leak_hits,
        "tool_failure_markers": tool_failures,
    }


# Anchored, line-based facts used by the v1 gates. Every pattern must match a whole line.
RE_FULL_VIS = re.compile(r"\s*Calculating Full Vis:\s*")
RE_FAST_OPTION = re.compile(r'\s*"fast" was set to "1".*')
RE_C_CHAINS = re.compile(r"\s*c_chains:\s*(\d+)\s*")
RE_Q3_FASTVIS = re.compile(r"\s*fastvis = true\s*", re.I)
RE_Q3_PORTALFLOW = re.compile(r"\s*--- (?:Passage)?PortalFlow \(\d+\) ---\s*")
RE_EMPTY_LIGHTMAPS = re.compile(r"\s*(\d+) empty lightmaps\s*")
RE_LIGHTING_COMPLETED = re.compile(r"\s*Lighting Completed\.\s*")
RE_Q3_TOTAL_LIGHTMAPS = re.compile(r"\s*(\d+) total lightmaps\s*")
RE_Q3_LUXELS = re.compile(r"\s*(\d+) luxels illuminated\s*")
RE_MISSING_TEXTURE = re.compile(r"\s*WARNING: Couldn't locate texture for (.+?)\s*")
RE_Q1_MISSING_TEXTURE = re.compile(r"\s*WARNING: unable to find texture (.+?)\s*")
RE_Q3_MISSING_IMAGE = re.compile(r".*couldn't find image for shader (\S+).*", re.I)


def log_facts(stage: str, game: str, text: str) -> dict:
    lines = text.splitlines()
    facts = {}
    if stage == "vis":
        facts["full_vis_line"] = any(RE_FULL_VIS.fullmatch(line) for line in lines)
        facts["fast_option_line"] = any(RE_FAST_OPTION.fullmatch(line) for line in lines)
        chains = [int(m.group(1)) for m in (RE_C_CHAINS.fullmatch(line) for line in lines) if m]
        facts["c_chains"] = chains[-1] if chains else None
    elif stage == "q3map2-vis":
        facts["fastvis_line"] = any(RE_Q3_FASTVIS.fullmatch(line) for line in lines)
        facts["portal_flow_line"] = any(RE_Q3_PORTALFLOW.fullmatch(line) for line in lines)
    elif stage == "light":
        facts["empty_lightmaps_lines"] = [int(m.group(1)) for m in
                                          (RE_EMPTY_LIGHTMAPS.fullmatch(line) for line in lines) if m]
        facts["lighting_completed_line"] = any(RE_LIGHTING_COMPLETED.fullmatch(line) for line in lines)
    elif stage == "q3map2-light":
        facts["total_lightmaps_lines"] = [int(m.group(1)) for m in
                                          (RE_Q3_TOTAL_LIGHTMAPS.fullmatch(line) for line in lines) if m]
        facts["luxels_illuminated_lines"] = [int(m.group(1)) for m in
                                             (RE_Q3_LUXELS.fullmatch(line) for line in lines) if m]
    elif stage == "qbsp":
        facts["missing_texture_lines"] = sorted({m.group(1) for m in
                                                 (RE_MISSING_TEXTURE.fullmatch(line) for line in lines) if m})
        facts["unable_to_find_texture_lines"] = sorted({m.group(1) for m in (RE_Q1_MISSING_TEXTURE.fullmatch(line)
                                                                             for line in lines) if m})
    elif stage == "q3map2-bsp":
        facts["missing_image_lines"] = sorted({m.group(1) for m in
                                               (RE_Q3_MISSING_IMAGE.fullmatch(line) for line in lines) if m})
    return facts


# --------------------------------------------------------------------------- binaries

BSP_LAYOUTS = {
    "q1": {"magic": None, "version": 29, "lumps": 15, "vis": 4, "light": 8, "vis_header": 0},
    "q2": {"magic": b"IBSP", "version": 38, "lumps": 19, "vis": 3, "light": 7, "vis_header": 4},
    "q3": {"magic": b"IBSP", "version": 46, "lumps": 17, "vis": 16, "light": 14, "vis_header": 8},
}


def inspect_bsp(path: Path, game: str) -> dict:
    info = {"path": path.name, "exists": path.is_file()}
    if not info["exists"]:
        return info
    data = path.read_bytes()
    info["bytes"] = len(data)
    layout = BSP_LAYOUTS[game]
    try:
        if layout["magic"] is None:
            version = struct.unpack_from("<i", data, 0)[0]
            table = 4
            info["magic"] = None
        else:
            info["magic"] = data[:4].decode("latin-1")
            version = struct.unpack_from("<i", data, 4)[0]
            table = 8
        info["version"] = version
        lumps = [struct.unpack_from("<ii", data, table + 8 * i) for i in range(layout["lumps"])]
    except struct.error as error:
        info["error"] = f"truncated header: {error}"
        return info
    info["header_ok"] = (version == layout["version"] and
                         (layout["magic"] is None or info["magic"] == layout["magic"].decode()))
    bounds_ok = all(0 <= off and length >= 0 and off + length <= len(data) for off, length in lumps)
    info["lump_bounds_ok"] = bounds_ok
    info["lump_lengths"] = [length for _, length in lumps]
    vis_off, vis_len = lumps[layout["vis"]]
    light_off, light_len = lumps[layout["light"]]
    info["visdata_lump"] = layout["vis"]
    info["visdata_bytes"] = vis_len
    info["lightdata_lump"] = layout["light"]
    info["lightdata_bytes"] = light_len
    vis_clusters = None
    if game == "q2" and vis_len >= 4:
        vis_clusters = struct.unpack_from("<i", data, vis_off)[0]
    if game == "q3" and vis_len >= 8:
        vis_clusters = struct.unpack_from("<i", data, vis_off)[0]
        info["visdata_bytes_per_cluster"] = struct.unpack_from("<i", data, vis_off + 4)[0]
    info["visdata_clusters"] = vis_clusters
    payload = vis_len - layout["vis_header"]
    info["visdata_nonempty"] = bool(bounds_ok and payload > 0 and (vis_clusters is None or vis_clusters > 0))
    if bounds_ok and light_len > 0:
        sample = data[light_off:light_off + light_len]
        info["lightdata_nonzero_bytes"] = len(sample) - sample.count(0)
    else:
        info["lightdata_nonzero_bytes"] = 0
    info["lightdata_nonempty"] = bool(bounds_ok and light_len > 0 and info["lightdata_nonzero_bytes"] > 0)
    return info


def inspect_lit(path: Path, bsp_info: dict) -> dict:
    info = {"path": path.name, "exists": path.is_file()}
    if not info["exists"]:
        return info
    data = path.read_bytes()
    info["bytes"] = len(data)
    info["magic_ok"] = data[:4] == b"QLIT"
    info["version"] = struct.unpack_from("<i", data, 4)[0] if len(data) >= 8 else None
    expected = 8 + 3 * int(bsp_info.get("lightdata_bytes") or 0)
    info["size_matches_bsp_lighting"] = len(data) == expected
    return info


def inspect_file_nonempty(path: Path) -> dict:
    return {"path": path.name, "exists": path.is_file(), "bytes": path.stat().st_size if path.is_file() else 0}


def inspect_aas(path: Path) -> dict:
    info = {"path": path.name, "exists": path.is_file()}
    if not info["exists"]:
        return info
    data = path.read_bytes()
    info["bytes"] = len(data)
    info["magic"] = data[:4].decode("latin-1") if len(data) >= 4 else None
    info["version"] = struct.unpack_from("<i", data, 4)[0] if len(data) >= 8 else None
    info["envelope_ok"] = len(data) >= 124 and info["magic"] == "EAAS" and info["version"] == 5
    return info


def parse_aasinfo(text: str) -> dict:
    lower = text.lower().replace("\r\n", "\n")
    metrics = {}
    for metric in AAS_METRICS:
        match = re.search(metric + r"[ \t]*=[ \t]*([0-9]+)", lower)
        metrics[metric] = int(match.group(1)) if match else None
    reach = {}
    for label in AAS_REACH_TYPES:
        match = re.search(r"\n[ \t]*([0-9]+)[ \t]+" + re.escape(label) + r"\n", lower)
        reach[label] = int(match.group(1)) if match else None
    total = sum(v for v in reach.values() if v is not None)
    rs = metrics.get("reachabilitysize")
    return {"metrics": metrics, "reachability": reach, "reachability_total": total,
            "reachability_consistent": rs is not None and all(v is not None for v in reach.values())
            and total == rs - 1}


# --------------------------------------------------------------------------- compiled lighting probe (G13)
# Port of out/benchmark-draft/probes/lighting-calibration/lm.py: floor-facing (normal z > 0.7) world
# luxels of the stored lightmaps, without overbright, gamma or rendering.

def _lumps(data, count, offset):
    return [struct.unpack_from("<ii", data, offset + 8 * i) for i in range(count)]


def _lum(r, g, b):
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def _extents(verts, vecs):
    out = []
    for k in range(2):
        vals = [v[0] * vecs[k][0] + v[1] * vecs[k][1] + v[2] * vecs[k][2] + vecs[k][3] for v in verts]
        lo = int(math.floor(min(vals) / 16.0))
        hi = int(math.ceil(max(vals) / 16.0))
        out.append(int(hi - lo + 1))
    return out


def _probe_classic(data: bytes, q2: bool):
    if q2:
        lumps = _lumps(data, 19, 8)
        P, V, TI, F, LI, E, SE, M = 1, 2, 5, 6, 7, 11, 12, 13
        tisz, msz, mff = 76, 48, 40
    else:
        lumps = _lumps(data, 15, 4)
        P, V, TI, F, LI, E, SE, M = 1, 3, 6, 7, 8, 12, 13, 14
        tisz, msz, mff = 40, 64, 56

    def arr(index, size):
        off, length = lumps[index]
        return off, length // size
    po, _ = arr(P, 20)
    vo, _ = arr(V, 12)
    to, _ = arr(TI, tisz)
    fo, fn = arr(F, 20)
    eo, _ = arr(E, 4)
    so, _ = arr(SE, 4)
    mo, _ = arr(M, msz)
    lo, ll = lumps[LI]
    bpp = 3 if q2 else 1
    firstface, numfaces = struct.unpack_from("<ii", data, mo + mff)
    floor, allv, total_size = [], [], 0
    for fi in range(fn):
        planenum, side, firstedge, numedges, texinfo = struct.unpack_from("<Hhihh", data, fo + 20 * fi)
        styles = data[fo + 20 * fi + 12:fo + 20 * fi + 16]
        lightofs = struct.unpack_from("<i", data, fo + 20 * fi + 16)[0]
        nst = sum(1 for s in styles if s != 255)
        vecs = [struct.unpack_from("<4f", data, to + tisz * texinfo),
                struct.unpack_from("<4f", data, to + tisz * texinfo + 16)]
        verts = []
        for k in range(numedges):
            se = struct.unpack_from("<i", data, so + 4 * (firstedge + k))[0]
            edge = struct.unpack_from("<HH", data, eo + 4 * abs(se))
            vi = edge[0] if se >= 0 else edge[1]
            verts.append(struct.unpack_from("<3f", data, vo + 12 * vi))
        w, h = _extents(verts, vecs)
        size = w * h
        tflags = struct.unpack_from("<i", data, to + tisz * texinfo + 32)[0]
        if q2 and (tflags & (4 | 8 | 128)):
            continue
        if lightofs < 0 or nst == 0:
            continue
        total_size += size * nst
        if not (firstface <= fi < firstface + numfaces):
            continue
        normal = struct.unpack_from("<3f", data, po + 20 * planenum)
        nz = -normal[2] if side else normal[2]
        o = lo + lightofs
        if bpp == 3:
            vals = [_lum(data[o + 3 * i], data[o + 3 * i + 1], data[o + 3 * i + 2]) for i in range(size)]
        else:
            vals = list(data[o:o + size])
        allv += vals
        if nz > 0.7:
            floor += vals
    return floor, allv, [total_size * bpp, ll]


def _probe_q3(data: bytes):
    lumps = _lumps(data, 17, 8)
    so, _ = lumps[13]
    lo, _ = lumps[14]
    mo, _ = lumps[7]
    vo, _ = lumps[10]
    firstsurf, numsurf = struct.unpack_from("<ii", data, mo + 24)
    floor, allv = [], []
    for si in range(firstsurf, firstsurf + numsurf):
        d = struct.unpack_from("<12i12f2i", data, so + 104 * si)
        stype, fv, nv, lmn = d[2], d[3], d[4], d[7]
        if stype != 1 or lmn < 0:
            continue
        normal = d[21:24]
        us, vs = [], []
        for k in range(nv):
            lu, lv = struct.unpack_from("<2f", data, vo + 44 * (fv + k) + 20)
            us.append(lu * 128)
            vs.append(lv * 128)
        x0 = max(int(math.floor(min(us))), 0)
        y0 = max(int(math.floor(min(vs))), 0)
        x1 = min(int(math.ceil(max(us))), 128)
        y1 = min(int(math.ceil(max(vs))), 128)
        base = lo + lmn * 128 * 128 * 3
        vals = []
        for yy in range(y0, y1):
            for xx in range(x0, x1):
                o = base + 3 * (yy * 128 + xx)
                vals.append(_lum(data[o], data[o + 1], data[o + 2]))
        allv += vals
        if normal[2] > 0.7:
            floor += vals
    return floor, allv, None


def _probe_stats(values):
    if not values:
        return None
    v = sorted(values)
    n = len(v)
    share = lambda pred: round(100 * sum(1 for x in v if pred(x)) / n, 1)
    return {"n": n, "mean": round(sum(v) / n, 1), "p10": round(v[n // 10], 1), "median": round(v[n // 2], 1),
            "lt8": share(lambda x: x < 8), "lt16": share(lambda x: x < 16), "lt32": share(lambda x: x < 32),
            "ge250": share(lambda x: x >= 250)}


def lighting_probe(bsp: Path, game: str) -> dict:
    if not bsp.is_file():
        return {"available": False, "reason": "no BSP"}
    try:
        data = bsp.read_bytes()
        if game == "q3":
            floor, allv, check = _probe_q3(data)
        else:
            floor, allv, check = _probe_classic(data, game == "q2")
    except (struct.error, IndexError, ZeroDivisionError, ValueError) as error:
        return {"available": False, "reason": f"probe error: {type(error).__name__}: {error}"}
    return {"available": True, "method": "lm.py port (floor normal z > 0.7, world model, stored bytes)",
            "floor": _probe_stats(floor), "all": _probe_stats(allv),
            "lightlump_size_check": check}


# --------------------------------------------------------------------------- map parsing

TOKEN = re.compile(r'"((?:[^"\\]|\\.)*)"|(\S+)')


def strip_leading_paren_groups(line: str) -> str:
    text = line.strip()
    while text.startswith("("):
        depth = 0
        for index, char in enumerate(text):
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0:
                    text = text[index + 1:].strip()
                    break
        else:
            return ""
    return text


def parse_map(path: Path) -> dict:
    entities = []
    textures = collections.Counter()
    brush_total = 0
    patch_total = 0
    depth = 0
    current = None
    in_patch = False
    patch_wants_texture = False
    brush_def = False
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            line = raw.strip()
            if not line or line.startswith("//"):
                continue
            if line == "{":
                depth += 1
                if depth == 1:
                    current = {"keys": {}, "brushes": 0, "patches": 0}
                elif depth == 2:
                    current["brushes"] += 1
                elif depth == 3 and in_patch:
                    patch_wants_texture = True
                continue
            if line == "}":
                if depth == 3 and in_patch:
                    in_patch = False
                if depth == 2:
                    brush_def = False
                if depth == 1 and current is not None:
                    entities.append(current)
                    current = None
                depth -= 1
                continue
            if depth == 1 and line.startswith('"'):
                tokens = [m.group(1) if m.group(1) is not None else m.group(2) for m in TOKEN.finditer(line)]
                if len(tokens) >= 2:
                    current["keys"].setdefault(tokens[0], tokens[1])
                continue
            if depth == 2:
                if line == "patchDef2" or line == "patchDef3":
                    in_patch = True
                    current["patches"] += 1
                    current["brushes"] -= 1
                    continue
                if line == "brushDef":
                    brush_def = True
                    continue
            if depth >= 2:
                if patch_wants_texture:
                    textures[line.split()[0]] += 1
                    patch_wants_texture = False
                    continue
                if in_patch:
                    continue
                if line.startswith("("):
                    rest = strip_leading_paren_groups(line)
                    if rest:
                        textures[rest.split()[0]] += 1
    classnames = collections.Counter()
    spawnflags = collections.Counter()
    spawnflags_by_class = collections.defaultdict(collections.Counter)
    worldspawn = {}
    entity_brushes = 0
    for entity in entities:
        cls = entity["keys"].get("classname", "<missing>")
        classnames[cls] += 1
        if "spawnflags" in entity["keys"]:
            value = entity["keys"]["spawnflags"]
            spawnflags[value] += 1
            spawnflags_by_class[cls][value] += 1
        brush_total += entity["brushes"]
        patch_total += entity["patches"]
        if cls == "worldspawn":
            worldspawn = dict(entity["keys"])
        else:
            entity_brushes += entity["brushes"]
    return {
        "entities": len(entities),
        "classnames": dict(sorted(classnames.items())),
        "spawnflags": dict(sorted(spawnflags.items())),
        "spawnflags_by_classname": {k: dict(v) for k, v in sorted(spawnflags_by_class.items())},
        "worldspawn": worldspawn,
        "textures": dict(textures.most_common()),
        "brush_count": brush_total,
        "entity_brush_count": entity_brushes,
        "patch_count": patch_total,
        "_entities": entities,
    }


# --------------------------------------------------------------------------- asset texture lookup

class AssetIndex:
    """Read-only lookups of stock texture availability (no links, no writes)."""

    def __init__(self, q1: Path, q2: Path, q3_base: Path, q3_game: str):
        self.q1, self.q2, self.q3 = q1, q2, q3_base / q3_game
        self._lock = threading.Lock()
        self._q1_names = None
        self._q3_shaders = None
        self._q3_pk3 = None
        self._q3_surfaceparms = None

    def q1_names(self) -> set:
        with self._lock:
            if self._q1_names is None:
                names = set()
                for bsp in sorted((self.q1 / "maps").glob("*.bsp")):
                    try:
                        data = bsp.read_bytes()
                        if struct.unpack_from("<i", data, 0)[0] != 29:
                            continue
                        offset, length = struct.unpack_from("<ii", data, 4 + 8 * 2)
                        count = struct.unpack_from("<i", data, offset)[0]
                        for i in range(count):
                            mip = struct.unpack_from("<i", data, offset + 4 + 4 * i)[0]
                            if mip < 0:
                                continue
                            raw = data[offset + mip: offset + mip + 16].split(b"\0")[0]
                            names.add(raw.decode("latin-1").lower())
                    except (struct.error, OSError):
                        continue
                self._q1_names = names
            return self._q1_names

    def q3_shaders(self) -> set:
        with self._lock:
            if self._q3_shaders is None:
                names = set()
                for script in sorted((self.q3 / "scripts").glob("*.shader")):
                    depth = 0
                    for line in script.read_text(encoding="latin-1", errors="replace").splitlines():
                        text = line.split("//")[0].strip()
                        if not text:
                            continue
                        if depth == 0 and text not in ("{", "}"):
                            names.add(text.split()[0].lower())
                        depth += text.count("{") - text.count("}")
                self._q3_shaders = names
                pk3 = set()
                for archive in sorted(self.q3.glob("*.pk3")):
                    try:
                        with zipfile.ZipFile(archive) as zf:
                            pk3.update(n.lower() for n in zf.namelist())
                    except (zipfile.BadZipFile, OSError):
                        continue
                self._q3_pk3 = pk3
            return self._q3_shaders

    def q3_surfaceparms(self) -> dict:
        """shader name (lower case) -> set of surfaceparm values, from scripts/*.shader (loose and pk3)."""
        with self._lock:
            if self._q3_surfaceparms is None:
                texts = []
                for script in sorted((self.q3 / "scripts").glob("*.shader")):
                    texts.append(script.read_text(encoding="latin-1", errors="replace"))
                for archive in sorted(self.q3.glob("*.pk3")):
                    try:
                        with zipfile.ZipFile(archive) as zf:
                            for name in sorted(zf.namelist()):
                                if name.lower().startswith("scripts/") and name.lower().endswith(".shader"):
                                    texts.append(zf.read(name).decode("latin-1", errors="replace"))
                    except (zipfile.BadZipFile, OSError):
                        continue
                parms = {}
                for text in texts:
                    depth = 0
                    current = None
                    for line in text.splitlines():
                        body = line.split("//")[0].strip()
                        if not body:
                            continue
                        for token_line in re.split(r"([{}])", body):
                            token_line = token_line.strip()
                            if not token_line:
                                continue
                            if token_line == "{":
                                depth += 1
                                continue
                            if token_line == "}":
                                depth = max(0, depth - 1)
                                continue
                            words = token_line.split()
                            if depth == 0:
                                current = words[0].lower()
                                parms.setdefault(current, set())
                            elif depth == 1 and current and words[0].lower() == "surfaceparm" and len(words) > 1:
                                parms[current].add(words[1].lower())
                self._q3_surfaceparms = parms
            return self._q3_surfaceparms

    def resolve(self, game: str, texture: str) -> str:
        low = texture.lower()
        if game == "q1":
            return "stock_miptex" if low in self.q1_names() else "not_in_stock_bsps"
        if game == "q2":
            base = self.q2 / "textures" / texture
            if base.with_suffix(".wal").is_file():
                return "wal"
            for ext in (".png", ".tga", ".jpg"):
                if base.with_suffix(ext).is_file():
                    return "image_only" + ext
            return "missing"
        shaders = self.q3_shaders()
        name = "textures/" + low
        if name in shaders:
            return "shader"
        for ext in (".tga", ".jpg"):
            if (self.q3 / "textures" / (texture + ext)).is_file() or (name + ext) in (self._q3_pk3 or set()):
                return "image" + ext
        return "missing"


def read_wad2_names(path: Path) -> set:
    data = path.read_bytes()
    if data[:4] not in (b"WAD2", b"WAD3"):
        raise ValueError(f"{path} is not a WAD2/WAD3 file")
    count, offset = struct.unpack_from("<ii", data, 4)
    names = set()
    for i in range(count):
        entry = data[offset + 32 * i: offset + 32 * (i + 1)]
        names.add(entry[16:32].split(b"\0")[0].decode("latin-1").lower())
    return names


# --------------------------------------------------------------------------- report parsing

def summarize_report(report: dict) -> dict:
    plan = report.get("semantic_plan") or {}
    planner = report.get("planner") or {}
    analysis = report.get("analysis") or {}
    findings = {}
    severities = collections.Counter()

    def add(analyzer: str, block):
        if not isinstance(block, dict) or not block.get("available"):
            findings[analyzer] = None
            return
        codes = collections.Counter()
        for finding in block.get("findings") or []:
            codes[finding.get("code", "<none>")] += 1
            severities[f"{analyzer}:{finding.get('severity')}"] += 1
        findings[analyzer] = dict(sorted(codes.items()))

    navigation = analysis.get("navigation") or {}
    for agent in ("player", "monster_small", "monster_large"):
        add(f"navigation.{agent}", navigation.get(agent))
    for analyzer in ("visibility", "lighting", "quality"):
        add(analyzer, analysis.get(analyzer))
    nav_graph = ((navigation.get("player") or {}).get("graph") or {})
    nodes = nav_graph.get("nodes") or []
    room_floors = sorted({n["area"]["floor_z"] for n in nodes if n.get("source", {}).get("kind") == "room"})
    all_floors = sorted({n["area"]["floor_z"] for n in nodes})
    slopes = sum(1 for n in nodes if (n.get("area") or {}).get("slope"))
    validation = report.get("validation") or {}
    quality = analysis.get("quality") or {}
    return {
        "game": report.get("game"),
        "mode": report.get("mode"),
        "layout_intent": report.get("layout_intent", "unspecified (field absent)"),
        "theme": report.get("theme"),
        "players": None,
        "players_note": "schema-v4 report has no target_players field (src/report.cpp render_report)",
        "request_seed": report.get("request_seed"),
        "candidate_seed": report.get("candidate_seed"),
        "layout_sha256": report.get("layout_sha256"),
        "rank_score": report.get("rank_score"),
        "counts": report.get("counts"),
        "map_stats": (report.get("map") or {}).get("stats"),
        "dialect": (report.get("map") or {}).get("dialect"),
        "semantic_plan": {
            "route_style": plan.get("route_style"), "symmetry": plan.get("symmetry"),
            "lighting_style": plan.get("lighting_style"), "verticality": plan.get("verticality"),
            "intensity": plan.get("intensity"), "room_roles": plan.get("room_roles"),
        },
        "planner": {
            "requested_backend": planner.get("requested_backend"), "used_backend": planner.get("used_backend"),
            "status": planner.get("status"), "attempts": planner.get("attempts"),
            "revisions": planner.get("revisions"),
            "model_id": (planner.get("model") or {}).get("model_id"),
            "model_sha256": (planner.get("model") or {}).get("model_sha256"),
        },
        "validation_ok": validation.get("ok"),
        "validation_issues": validation.get("issues") or [],
        "repair_passes": len(report.get("repair_passes") or []),
        "analysis_findings_by_code": findings,
        "analysis_findings_by_severity": dict(sorted(severities.items())),
        "quality_score": quality.get("score"),
        "visibility_summary": (analysis.get("visibility") or {}).get("summary"),
        "lighting_summary": (analysis.get("lighting") or {}).get("summary"),
        "nav_room_floor_heights": room_floors,
        "nav_all_floor_heights": all_floors,
        "nav_slope_nodes": slopes,
        "room_graph_signature": room_graph_signature(nav_graph),
    }


def report_extras(report: dict) -> dict:
    """v1 additions read from the raw report: typed findings, request trace, error findings, drop edges.

    Reads schema v4 and the additive v5 fields of D2 (findings, request_trace, unmet, module, player range)
    when present. Nothing here changes a draft-v0 verdict."""
    analysis = report.get("analysis") or {}
    navigation = analysis.get("navigation") or {}
    errors = collections.Counter()
    blocks = [(f"navigation.{a}", navigation.get(a)) for a in ("player", "monster_small", "monster_large")]
    blocks += [(a, analysis.get(a)) for a in ("visibility", "lighting", "quality")]
    for name, block in blocks:
        if isinstance(block, dict) and block.get("available"):
            for finding in block.get("findings") or []:
                if str(finding.get("severity")).lower() == "error":
                    errors[f"{name}:{finding.get('code', '<none>')}"] += 1
    graph = ((navigation.get("player") or {}).get("graph") or {})
    floor = {n.get("id"): (n.get("area") or {}).get("floor_z") for n in graph.get("nodes") or []}
    drops = 0
    for edge in graph.get("edges") or []:
        if edge.get("kind") == "drop":
            a, b = floor.get(edge.get("from")), floor.get(edge.get("to"))
            if isinstance(a, (int, float)) and isinstance(b, (int, float)) and a - b >= 64:
                drops += 1
    typed = []
    sources = [report.get("findings"), (report.get("brief") or {}).get("findings")
               if isinstance(report.get("brief"), dict) else None]
    for source in sources:
        if isinstance(source, list):
            typed += [f for f in source if isinstance(f, dict)]
    trace = [t for t in (report.get("request_trace") or []) if isinstance(t, dict)] \
        if isinstance(report.get("request_trace"), list) else []
    unmet = [u for u in (report.get("unmet") or []) if isinstance(u, dict)] \
        if isinstance(report.get("unmet"), list) else []
    players = report.get("players") if isinstance(report.get("players"), dict) else {}
    return {
        "schema_version": report.get("schema_version"),
        "generator_version": report.get("generator_version"),
        "module": report.get("module") or report.get("game_module"),
        "player_min": players.get("min", report.get("player_min")),
        "player_max": players.get("max", report.get("player_max")),
        "switches": report.get("switches"),
        "typed_findings": typed,
        "request_trace": trace,
        "unmet": unmet,
        "error_findings_by_code": dict(sorted(errors.items())),
        "error_findings_total": sum(errors.values()),
        "nav_drop_edges_64": drops,
    }


def room_graph_signature(graph: dict):
    """Isomorphism-invariant (WL, 3 rounds) hash of the room/connection/solid source graph."""
    nodes = graph.get("nodes") or []
    edges = graph.get("edges") or []
    if not nodes:
        return None
    source_of = {}
    for node in nodes:
        src = node.get("source") or {}
        source_of[node["id"]] = (src.get("kind"), src.get("id"))
    adjacency = collections.defaultdict(set)
    for edge in edges:
        a, b = source_of.get(edge.get("from")), source_of.get(edge.get("to"))
        if a and b and a != b:
            adjacency[a].add(b)
            adjacency[b].add(a)
    vertices = set(source_of.values())
    labels = {v: v[0] for v in vertices}
    for _ in range(3):
        labels = {v: hashlib.sha1((labels[v] + "|" + ",".join(sorted(labels[u] for u in adjacency[v])))
                                  .encode()).hexdigest()[:16] for v in vertices}
    multiset = ",".join(sorted(labels.values()))
    kinds = collections.Counter(v[0] for v in vertices)
    return {"sha": hashlib.sha256(multiset.encode()).hexdigest()[:16],
            "vertices": dict(sorted(kinds.items())),
            "source_edges": sum(len(s) for s in adjacency.values()) // 2}


# --------------------------------------------------------------------------- request evaluation

ITEMS = {
    # phrase: {game: [classnames]}
    "railgun": {"q2": ["weapon_railgun"], "q3": ["weapon_railgun"]},
    "rocket launcher": {"q1": ["weapon_rocketlauncher"], "q2": ["weapon_rocketlauncher"], "q3": ["weapon_rocketlauncher"]},
    "grenade launcher": {"q1": ["weapon_grenadelauncher"], "q2": ["weapon_grenadelauncher"], "q3": ["weapon_grenadelauncher"]},
    "lightning gun": {"q1": ["weapon_lightning"], "q3": ["weapon_lightning"]},
    "super nailgun": {"q1": ["weapon_supernailgun"]},
    "nailgun": {"q1": ["weapon_nailgun"]},
    "super shotgun": {"q1": ["weapon_supershotgun"], "q2": ["weapon_supershotgun"]},
    "shotgun": {"q2": ["weapon_shotgun"], "q3": ["weapon_shotgun"]},
    "hyperblaster": {"q2": ["weapon_hyperblaster"]},
    "chaingun": {"q2": ["weapon_chaingun"]},
    "plasma gun": {"q3": ["weapon_plasmagun"]},
    "quad damage": {"q1": ["item_artifact_super_damage"], "q2": ["item_quad"], "q3": ["item_quad"]},
    "mega health": {"q1": ["item_health@2"], "q2": ["item_health_mega"], "q3": ["item_health_mega"]},
    "red armor": {"q1": ["item_armorInv"], "q2": ["item_armor_body"], "q3": ["item_armor_body"]},
    "yellow armor": {"q1": ["item_armor2"], "q2": ["item_armor_combat"], "q3": ["item_armor_combat"]},
    "body armor": {"q2": ["item_armor_body"], "q3": ["item_armor_body"]},
    "combat armor": {"q2": ["item_armor_combat"]},
    "power armor": {"q2": ["item_power_screen", "item_power_shield"]},
    "power shield": {"q2": ["item_power_shield"]},
    "rebreather": {"q2": ["item_breather"]},
    "gold key": {"q1": ["item_key2"]},
    "silver key": {"q1": ["item_key1"]},
    "blue keycard": {"q2": ["key_blue_key"]},
    "red keycard": {"q2": ["key_red_key"]},
    "teleporter": {"q1": ["trigger_teleport"], "q2": ["misc_teleporter"], "q3": ["trigger_teleport", "misc_teleporter_dest"]},
    "jump pad": {"q3": ["trigger_push"]},
    "lift": {"q1": ["func_plat", "func_train"], "q2": ["func_plat", "func_train"], "q3": ["func_plat", "func_train"]},
    "secret area": {"q1": ["trigger_secret"], "q2": ["target_secret"]},
    "ogre": {"q1": ["monster_ogre"]}, "knight": {"q1": ["monster_knight", "monster_hell_knight"]},
    "grunt": {"q1": ["monster_army"]}, "enforcer": {"q1": ["monster_enforcer"], "q2": ["monster_infantry"]},
    "zombie": {"q1": ["monster_zombie"]}, "fiend": {"q1": ["monster_demon1"]},
    "shambler": {"q1": ["monster_shambler"]},
    "gunner": {"q2": ["monster_gunner"]}, "berserker": {"q2": ["monster_berserk"]},
    "parasite": {"q2": ["monster_parasite"]}, "iron maiden": {"q2": ["monster_chick"]},
    "gladiator": {"q2": ["monster_gladiator"]}, "tank": {"q2": ["monster_tank", "monster_tank_commander"]},
    "flyer": {"q2": ["monster_flyer"]},
}
ITEM_PHRASES = sorted(ITEMS, key=len, reverse=True)
# v1 additions (classnames read from Quake-master/qw-qc/items.qc, Quake-2-master/game/g_items.c and
# Quake-III-Arena-master/code/game/bg_misc.c). None of these phrases occurs in a draft-v0 request.
V1_ITEMS = dict(ITEMS)
V1_ITEMS.update({
    "pentagram of protection": {"q1": ["item_artifact_invulnerability"]},
    "ring of shadows": {"q1": ["item_artifact_invisibility"]},
    "biosuit": {"q1": ["item_artifact_envirosuit"]},
    "green armor": {"q1": ["item_armor1"], "q2": ["item_armor_jacket"]},
    "thunderbolt": {"q1": ["weapon_lightning"]},
    "machinegun": {"q2": ["weapon_machinegun"], "q3": ["weapon_machinegun"]},
    "bfg10k": {"q2": ["weapon_bfg"], "q3": ["weapon_bfg"]},
    "bfg": {"q2": ["weapon_bfg"], "q3": ["weapon_bfg"]},
    "invulnerability": {"q1": ["item_artifact_invulnerability"], "q2": ["item_invulnerability"]},
    "jacket armor": {"q2": ["item_armor_jacket"]},
    "bandolier": {"q2": ["item_bandolier"]},
    "ammo pack": {"q2": ["item_pack"]},
    "adrenaline": {"q2": ["item_adrenaline"]},
    "silencer": {"q2": ["item_silencer"]},
    "regeneration": {"q3": ["item_regen"]},
    "haste": {"q3": ["item_haste"]},
    "battle suit": {"q3": ["item_enviro"]},
    "invisibility": {"q1": ["item_artifact_invisibility"], "q3": ["item_invis"]},
    "flight": {"q3": ["item_flight"]},
    "medkit": {"q3": ["holdable_medkit"]},
    "personal teleporter": {"q3": ["holdable_teleporter"]},
})
V1_ITEM_PHRASES = sorted(V1_ITEMS, key=len, reverse=True)
POWERUPS = {
    "q1": ["item_artifact_super_damage", "item_artifact_invulnerability", "item_artifact_invisibility",
           "item_artifact_envirosuit"],
    "q2": ["item_quad", "item_invulnerability", "item_silencer", "item_breather", "item_enviro"],
    "q3": ["item_quad", "item_enviro", "item_haste", "item_invis", "item_regen", "item_flight"],
}
SIZE_BANDS_PROVISIONAL = {"small": (4, 6), "medium": (7, 10), "large": (11, 16)}
FILLER = {"and", "the", "a", "an", "one", "each", "per", "both", "of"}
SP_EXIT = {"q1": ["trigger_changelevel"], "q2": ["target_changelevel"], "q3": []}
FLAGS = {"q1": ["item_flag_team1", "item_flag_team2"], "q2": ["item_flag_team1", "item_flag_team2"],
         "q3": ["team_CTF_redflag", "team_CTF_blueflag"]}
TEAM_SPAWNS = {"q1": ["info_player_team1", "info_player_team2"], "q2": ["info_player_team1", "info_player_team2"],
               "q3": ["team_CTF_redspawn", "team_CTF_bluespawn", "team_CTF_redplayer", "team_CTF_blueplayer"]}
NUMBER_WORDS = {"one": 1, "two": 2, "three": 3, "four": 4, "five": 5}


def has_class(classnames: dict, entities: list, spec: str) -> bool:
    if "@" in spec:
        cls, flag = spec.split("@")
        for entity in entities:
            keys = entity["keys"]
            if keys.get("classname") == cls:
                try:
                    if int(keys.get("spawnflags", "0")) & int(flag):
                        return True
                except ValueError:
                    continue
        return False
    return classnames.get(spec, 0) > 0


def liquid_present(textures: dict, game: str, liquid: str) -> list:
    hits = []
    for name in textures:
        low = name.lower()
        if liquid in low:
            hits.append(name)
        elif liquid == "water" and game == "q1" and low.startswith("*") and "water" in low:
            hits.append(name)
    return sorted(set(hits))


def evaluate_request(request: str, ctx: dict, items=None, phrases=None) -> dict:
    """The draft-v0 request evaluator. With the default tables it is exactly the draft behaviour."""
    items = ITEMS if items is None else items
    phrases = ITEM_PHRASES if phrases is None else phrases
    req = request.strip()
    low = req.lower()
    game = ctx["game_code"]
    rep = ctx.get("report")
    mp = ctx.get("map")
    classnames = (mp or {}).get("classnames", {})
    entities = (mp or {}).get("_entities", [])
    textures = (mp or {}).get("textures", {})

    def out(status, evidence, rule):
        return {"request": request, "status": status, "evidence": evidence, "rule": rule}

    if low == "conflict surfaced":
        gen = ctx["generate"]
        if gen.get("exit_code") not in (0, None):
            return out("honoured", "generation refused: " + tail(gen.get("stderr_tail", ""), 300).strip(),
                       "non-zero exit with diagnostic")
        if rep is None:
            return out("unverified", "no report", "report missing")
        issues = rep.get("validation_issues") or []
        if issues:
            return out("partial", f"{len(issues)} validation issue(s); no explicit prompt-conflict diagnostic",
                       "validation issues present")
        text = (gen.get("stdout_tail", "") + gen.get("stderr_tail", "")).lower()
        if "conflict" in text or "warning" in text or "ignored" in text:
            return out("partial", "CLI output mentions a warning/conflict", "cli output keyword")
        return out("not_honoured", "exit 0, validation ok, no CLI warning: contradiction resolved silently",
                   "silent resolution")

    if rep is None and not low.startswith(("game ", "mode ")):
        return out("unverified", "no report/map produced", "no artifacts")

    match = re.fullmatch(r"game (\w+)", low)
    if match:
        if rep is None:
            return out("not_honoured", "no report produced", "report game")
        return out("honoured" if rep["game"] == match.group(1) else "not_honoured",
                   f"report game={rep['game']}", "report game")
    match = re.fullmatch(r"mode (\w+)", low)
    if match:
        wanted = match.group(1)
        if rep is None:
            return out("not_honoured", "no report produced", "report mode")
        if wanted in NOT_REPRESENTABLE_REQUEST_MODES:
            return out("not_representable", f"generator GameMode has no {wanted}; resolved {rep['mode']}",
                       "include/qmg/types.hpp GameMode")
        return out("honoured" if rep["mode"] == wanted else "not_honoured", f"report mode={rep['mode']}",
                   "report mode")
    match = re.fullmatch(r"(\d+) rooms", low)
    if match:
        rooms = (rep.get("counts") or {}).get("rooms")
        return out("honoured" if rooms == int(match.group(1)) else "not_honoured", f"report rooms={rooms}",
                   "report counts.rooms")
    if re.fullmatch(r"(\d+)(-\d+|v\d+)? players", low) or low in ("1 players",):
        dm = classnames.get("info_player_deathmatch", 0)
        team = sum(classnames.get(c, 0) for c in TEAM_SPAWNS[game])
        coop = classnames.get("info_player_coop", 0)
        return out("unverified", f"report has no players field; spawns: deathmatch={dm}, team={team}, coop={coop}",
                   "no target_players in report")
    match = re.fullmatch(r"theme (.+)", low)
    if match:
        wanted = match.group(1)
        if rep["theme"] == wanted:
            return out("honoured", f"report theme={rep['theme']}", "report theme")
        if wanted in GENERATOR_THEMES:
            return out("not_honoured", f"report theme={rep['theme']}", "report theme (generator vocabulary)")
        return out("unverified", f"report theme={rep['theme']}; requested theme outside generator vocabulary",
                   "report theme")
    if low == "exit" or low.startswith("exit "):
        wanted = SP_EXIT[game]
        present = [c for c in wanted if classnames.get(c)]
        if not wanted:
            return out("not_honoured", "no exit classname for this game", "map classnames")
        if not present:
            return out("not_honoured", f"missing {wanted}", "map classnames")
        return out("honoured" if low == "exit" else "partial", f"present {present}", "map classnames")
    if low == "coop spawns":
        n = classnames.get("info_player_coop", 0)
        return out("honoured" if n else "not_honoured", f"info_player_coop={n}", "map classnames")
    if low in ("flags", "no flags"):
        counts = {c: classnames.get(c, 0) for c in FLAGS[game]}
        present = all(counts.values())
        none = not any(counts.values())
        if low == "flags":
            return out("honoured" if present else "not_honoured", str(counts), "map classnames")
        return out("honoured" if none else "not_honoured", str(counts), "map classnames")
    if low.startswith("flags "):
        counts = {c: classnames.get(c, 0) for c in FLAGS[game]}
        return out("partial" if all(counts.values()) else "not_honoured", str(counts) + "; placement unverified",
                   "map classnames")
    if low == "team spawns":
        counts = {c: classnames.get(c, 0) for c in TEAM_SPAWNS[game]}
        return out("honoured" if all(counts.values()) else "not_honoured", str(counts), "map classnames")
    if low == "aas":
        g6 = (ctx.get("gates") or {}).get("G6_aas_valid", {})
        if g6.get("status") == "pass":
            return out("honoured", "G6 AAS valid", "bspc gate")
        if g6.get("status") == "fail":
            return out("not_honoured", g6.get("detail", ""), "bspc gate")
        return out("unverified", "AAS gate not evaluated", "bspc gate")
    if low in ("monsters", "no monsters"):
        monsters = {c: n for c, n in classnames.items() if c.startswith("monster_")}
        if low == "monsters":
            return out("honoured" if monsters else "not_honoured", f"monster classnames={monsters}", "map classnames")
        return out("honoured" if not monsters else "not_honoured", f"monster classnames={monsters}", "map classnames")
    if low == "no bots":
        return out("unverified", "bot presence is a server setting, not map content", "n/a")
    if "sky" in low:
        hits = [t for t in textures if "sky" in t.lower()]
        if not hits:
            return out("not_honoured", "no sky texture in map", "map textures")
        return out("honoured" if low in ("outdoor sky", "sky") else "partial", f"sky textures {hits}", "map textures")
    for liquid in ("lava", "slime", "water"):
        if re.search(r"\b" + liquid + r"\b", low) and not any(p in low for p in phrases):
            negative = low.startswith("no ")
            hits = liquid_present(textures, game, liquid)
            if negative:
                return out("honoured" if not hits else "not_honoured", f"{liquid} textures {hits}", "map textures")
            if not hits:
                return out("not_honoured", f"no {liquid} texture in map", "map textures")
            return out("honoured" if low == liquid else "partial", f"{liquid} textures {hits}; placement unverified",
                       "map textures")
    match = re.fullmatch(r"(\d+|one|two|three|four|five) floor levels", low)
    if match:
        want = int(match.group(1)) if match.group(1).isdigit() else NUMBER_WORDS[match.group(1)]
        floors = rep.get("nav_room_floor_heights") or []
        if len(floors) < want:
            return out("not_honoured", f"distinct room floor heights={floors}", "nav room nodes floor_z")
        return out("unverified", f"distinct room floor heights={floors} (heights, not proven levels); "
                   f"plan verticality={rep['semantic_plan']['verticality']}", "nav room nodes floor_z")
    if low in ("flat", "extreme verticality", "high verticality"):
        floors = rep.get("nav_all_floor_heights") or []
        v = rep["semantic_plan"]["verticality"]
        evidence = f"plan verticality={v}; distinct nav floor heights={floors}"
        if low == "flat":
            return out("honoured" if len(floors) <= 1 else "unverified", evidence, "nav floor_z / plan")
        if len(floors) <= 1:
            return out("not_honoured", evidence, "nav floor_z / plan")
        return out("unverified", evidence, "nav floor_z / plan")
    if low in ("high intensity", "low intensity"):
        return out("unverified", f"plan intensity={rep['semantic_plan']['intensity']} (plan only)", "semantic plan")
    if low == "loop route":
        counts = rep.get("counts") or {}
        comps = (rep.get("visibility_summary") or {}).get("connected_components")
        if counts.get("connections", 0) >= counts.get("rooms", 0) and comps == 1:
            return out("honoured", f"rooms={counts.get('rooms')} connections={counts.get('connections')} "
                       f"(connected graph with a cycle); plan route_style={rep['semantic_plan']['route_style']}",
                       "room graph cycle")
        return out("not_honoured", f"rooms={counts.get('rooms')} connections={counts.get('connections')} components={comps}",
                   "room graph cycle")
    if low == "linear route":
        counts = rep.get("counts") or {}
        if counts.get("connections", 0) >= counts.get("rooms", 0):
            return out("not_honoured", f"rooms={counts.get('rooms')} connections={counts.get('connections')} (has a cycle)",
                       "room graph cycle")
        return out("unverified", f"acyclic room graph; path shape not verified; plan route_style="
                   f"{rep['semantic_plan']['route_style']}", "room graph cycle")
    if low == "mirrored symmetry":
        sym = rep["semantic_plan"]["symmetry"]
        if sym == "none":
            return out("not_honoured", "plan symmetry=none", "semantic plan")
        return out("unverified", f"plan symmetry={sym}; geometry symmetry not checked", "semantic plan")
    if low in ("health",):
        hits = {c: n for c, n in classnames.items() if c.startswith("item_health")}
        return out("honoured" if hits else "not_honoured", str(hits), "map classnames")
    if low == "ammo" or low.startswith("ammo "):
        hits = {c: n for c, n in classnames.items() if c.startswith("ammo_") or c in
                ("item_shells", "item_spikes", "item_rockets", "item_cells")}
        if not hits:
            return out("not_honoured", "no ammo classnames", "map classnames")
        return out("honoured" if low == "ammo" else "partial", str(hits), "map classnames")

    # Item / monster phrases (possibly several, possibly negated, possibly with qualifiers).
    negative = low.startswith("no ")
    body = low[3:] if negative else low
    found = []
    remaining = body
    for phrase in phrases:
        if re.search(r"\b" + re.escape(phrase) + r"s?\b", remaining):
            found.append(phrase)
            remaining = re.sub(r"\b" + re.escape(phrase) + r"s?\b", " ", remaining)
    if found:
        checks = {}
        unknown = []
        for phrase in found:
            specs = items[phrase].get(game)
            if not specs:
                unknown.append(phrase)
                continue
            checks[phrase] = {s: has_class(classnames, entities, s) for s in specs}
        if unknown and not checks:
            return out("unverified", f"no stock classname mapping for {unknown} in {game}", "item table")
        present = {p: any(v.values()) for p, v in checks.items()}
        evidence = f"classname presence {checks}"
        if negative:
            return out("honoured" if not any(present.values()) else "not_honoured", evidence, "map classnames")
        if not all(present.values()):
            return out("not_honoured", evidence, "map classnames")
        qualifiers = [w for w in re.findall(r"[a-z0-9-]+", remaining) if w not in FILLER]
        if qualifiers or unknown:
            return out("partial", evidence + f"; qualifier {' '.join(qualifiers)!r} unverified", "map classnames")
        return out("honoured", evidence, "map classnames")
    return out("unverified", "not checkable from report/map", "no rule")


def q3_surface_hits(textures: dict, surfaceparms: dict, parm: str) -> list:
    return sorted(t for t in textures if parm in surfaceparms.get("textures/" + t.lower(), set()))


def evaluate_request_v1(request: str, ctx: dict) -> dict:
    """v1 evaluator: new BENCH-v1 vocabulary first, Q3 liquids/sky by shader surfaceparm, then the draft
    rules with the extended item table. Draft-v0 request strings give the draft verdicts, except Q3
    liquid and sky requests, which are now read from surfaceparms instead of texture-name substrings."""
    low = request.strip().lower()
    game = ctx["game_code"]
    rep = ctx.get("report")
    mp = ctx.get("map")
    extras = ctx.get("report_extras") or {}
    classnames = (mp or {}).get("classnames", {})
    textures = (mp or {}).get("textures", {})

    def out(status, evidence, rule):
        return {"request": request, "status": status, "evidence": evidence, "rule": rule}

    if rep is None and not low.startswith(("game ", "mode ")) and low != "conflict surfaced":
        return out("unverified", "no report/map produced", "no artifacts")
    if low.startswith("style reference "):
        return out("unverified", "report has no style-reference field", "v1: style reference")
    if low.startswith("size "):
        band = low[5:]
        rooms = (rep.get("counts") or {}).get("rooms")
        lo_hi = SIZE_BANDS_PROVISIONAL.get(band)
        inside = lo_hi is not None and rooms is not None and lo_hi[0] <= rooms <= lo_hi[1]
        return out("unverified", f"report has no size field; rooms={rooms}; provisional band {band}={lo_hi} "
                   f"({'inside' if inside else 'outside'}; bands are D3, pending)", "v1: size")
    if low.startswith("mood "):
        return out("unverified", f"report has no mood field; plan lighting_style="
                   f"{rep['semantic_plan']['lighting_style']}", "v1: mood")
    if low.startswith("landmark "):
        return out("unverified", "report has no landmark field (LandmarkSpec, D8, Phase 4)", "v1: landmark")
    if low == "runes":
        return out("unverified", "Threewave runes are spawned by the progs, not placed as map entities",
                   "v1: runes")
    if low == "door":
        n = classnames.get("func_door", 0)
        return out("honoured" if n else "not_honoured", f"func_door={n}", "v1: map classnames")
    if low == "ramp":
        n = rep.get("nav_slope_nodes") or 0
        return out("honoured" if n else "not_honoured", f"navigation slope nodes={n}", "v1: nav slope nodes")
    if low == "drop":
        n = extras.get("nav_drop_edges_64") or 0
        if not n:
            return out("not_honoured", "no navigation drop edge of at least 64 u", "v1: nav drop edges")
        return out("partial", f"{n} navigation drop edge(s) of at least 64 u; a designed drop route is not "
                   "verified", "v1: nav drop edges")
    if low == "launch pad":
        n = classnames.get("trigger_push", 0)
        if game != "q3":
            return out("unverified", "launch pads are a Quake 3 request in BENCH-v1", "v1: launch pad")
        if not n:
            return out("not_honoured", "trigger_push=0", "v1: map classnames")
        return out("partial", f"trigger_push={n}; launch vs jump pad not distinguished", "v1: map classnames")
    if low == "objective":
        if game != "q2":
            return out("unverified", f"no objective entity class for {game}", "v1: objective")
        hits = {c: classnames.get(c, 0) for c in ("target_goal", "target_help")}
        return out("honoured" if any(hits.values()) else "not_honoured", str(hits), "v1: map classnames")
    if low == "no loop route":
        counts = rep.get("counts") or {}
        if counts.get("connections", 0) >= counts.get("rooms", 0):
            return out("not_honoured", f"rooms={counts.get('rooms')} connections={counts.get('connections')} "
                       "(has a cycle)", "v1: room graph cycle")
        return out("honoured", f"rooms={counts.get('rooms')} connections={counts.get('connections')} (acyclic)",
                   "v1: room graph cycle")
    if low == "no powerups":
        hits = {c: classnames.get(c, 0) for c in POWERUPS[game] if classnames.get(c)}
        return out("honoured" if not hits else "not_honoured", f"powerup classnames={hits}", "v1: map classnames")
    if game == "q3" and "sky" in low and not any(p in low for p in V1_ITEM_PHRASES):
        hits = q3_surface_hits(textures, ctx["surfaceparms"](), "sky")
        if not hits:
            return out("not_honoured", "no shader with surfaceparm sky", "v1: q3 surfaceparm")
        return out("honoured" if low in ("outdoor sky", "sky") else "partial", f"sky shaders {hits}",
                   "v1: q3 surfaceparm")
    if game == "q3":
        for liquid in ("lava", "slime", "water"):
            if re.search(r"\b" + liquid + r"\b", low) and not any(p in low for p in V1_ITEM_PHRASES):
                hits = q3_surface_hits(textures, ctx["surfaceparms"](), liquid)
                if low.startswith("no "):
                    return out("honoured" if not hits else "not_honoured", f"surfaceparm {liquid} shaders {hits}",
                               "v1: q3 surfaceparm")
                if not hits:
                    return out("not_honoured", f"no shader with surfaceparm {liquid}", "v1: q3 surfaceparm")
                return out("honoured" if low == liquid else "partial",
                           f"surfaceparm {liquid} shaders {hits}; placement unverified", "v1: q3 surfaceparm")
    return evaluate_request(request, ctx, items=V1_ITEMS, phrases=V1_ITEM_PHRASES)


# --------------------------------------------------------------------------- compilation

def compile_case(case: dict, case_dir: Path, map_name: str, cfg, vis_extra=(), light_timeout=None) -> dict:
    game = case["game"]
    stem = Path(map_name).stem
    logs = case_dir / "logs"
    map_path = case_dir / map_name
    bsp_path = case_dir / f"{stem}.bsp"
    prt_path = case_dir / f"{stem}.prt"
    stages = []
    result = {"stages": stages}
    light_timeout = light_timeout or STAGE_TIMEOUT

    def record(stage):
        text = stage.pop("_stdout") + "\n" + stage.pop("_stderr")
        stage.update(scan_output(text, game, stage["name"]))
        stage["facts"] = log_facts(stage["name"], game, text)
        stages.append(stage)
        return text

    def ok(stage):
        return stage["exit_code"] == 0 and not stage["timed_out"]

    if game in ("q1", "q2"):
        ericw = cfg.ericw
        if game == "q1":
            qbsp_args = ["-leaktest", "-noallowupgrade", "-notex", "-lowpriority", "0", "-nolog", "-nocolor", "-quiet"]
            path_args = []
            # Q1 light adds -nodefaultpaths exactly as cmake/RunClassicVisLight.cmake:155-157 does.
            light_path_args = ["-nodefaultpaths"]
        else:
            path_args = ["-nodefaultpaths", "-path", str(cfg.q2_assets).replace("\\", "/")]
            qbsp_args = ["-q2bsp", "-leaktest", "-noallowupgrade"] + path_args + \
                        ["-lowpriority", "0", "-nolog", "-nocolor", "-quiet"]
            light_path_args = path_args
        stage = run_stage("qbsp", [str(ericw / "qbsp.exe")] + qbsp_args + [map_name, f"{stem}.bsp"],
                          case_dir, logs / "qbsp.log")
        record(stage)
        result["pointfiles"] = [p.name for p in (case_dir / f"{stem}.pts", case_dir / f"{stem}.lin") if p.exists()]
        result["leak"] = bool(result["pointfiles"] or stage["leak_markers"])
        result["prt"] = inspect_file_nonempty(prt_path)
        result["bsp_after_qbsp"] = inspect_bsp(bsp_path, game)
        result["qbsp_ok"] = ok(stage) and result["bsp_after_qbsp"].get("header_ok", False)
        if result["qbsp_ok"] and result["prt"]["bytes"] > 0:
            vis = run_stage("vis", [str(ericw / "vis.exe")] + list(vis_extra) +
                            ["-threads", "2", "-lowpriority", "0", "-nostate"]
                            + path_args + ["-nolog", "-nocolor", "-quiet", f"{stem}.bsp"], case_dir, logs / "vis.log")
            vis_text = record(vis)
            vis["full_vis_reported"] = "calculating full vis" in vis_text.lower()
            m = re.search(r"visdatasize:\s*([0-9]+)", vis_text.lower())
            vis["visdatasize"] = int(m.group(1)) if m else None
            result["bsp_after_vis"] = inspect_bsp(bsp_path, game)
            result["vis_ok"] = ok(vis) and not vis["tool_failure_markers"]
            light = run_stage("light", [str(ericw / "light.exe"), "-extra4", "-bounce", "1", "-soft", "-threads", "2",
                                        "-lowpriority", "0"] + light_path_args + ["-nolog", "-nocolor", "-quiet",
                                                                                  f"{stem}.bsp"],
                              case_dir, logs / "light.log", timeout=light_timeout)
            light_text = record(light)
            low = light_text.lower()
            light["lighting_completed"] = "lighting completed" in low
            m = re.search(r"([0-9]+) empty lightmaps", low)
            light["empty_lightmaps"] = int(m.group(1)) if m else None
            result["light_ok"] = ok(light) and not light["tool_failure_markers"]
            result["bsp_final"] = inspect_bsp(bsp_path, game)
            if game == "q1":
                result["lit"] = inspect_lit(case_dir / f"{stem}.lit", result["bsp_final"])
        return result

    # Quake 3
    env = q3_env(cfg)
    q3map2 = str(cfg.q3tools / "q3map2.exe")
    home = case_dir / "q3home"
    home.mkdir(exist_ok=True)
    fs = q3_fs_args(cfg, home)
    abs_map = str(map_path).replace("\\", "/")
    abs_bsp = str(bsp_path).replace("\\", "/")
    stage = run_stage("q3map2-bsp", [q3map2] + fs + ["-bsp", "-leaktest", abs_map], case_dir, logs / "q3map2-bsp.log",
                      env=env)
    record(stage)
    result["pointfiles"] = [p.name for p in (case_dir / f"{stem}.lin", case_dir / f"{stem}.pts") if p.exists()]
    result["leak"] = bool(result["pointfiles"] or stage["leak_markers"])
    result["prt"] = inspect_file_nonempty(prt_path)
    result["srf"] = inspect_file_nonempty(case_dir / f"{stem}.srf")
    result["bsp_after_qbsp"] = inspect_bsp(bsp_path, game)
    result["qbsp_ok"] = ok(stage) and result["bsp_after_qbsp"].get("header_ok", False)
    if result["qbsp_ok"] and result["prt"]["bytes"] > 0:
        vis = run_stage("q3map2-vis", [q3map2, "-vis", "-saveprt"] + list(vis_extra) + fs + [abs_bsp], case_dir,
                        logs / "q3map2-vis.log", env=env)
        vis_text = record(vis)
        m = re.search(r"visdatasize:\s*([0-9]+)", vis_text.lower())
        vis["visdatasize"] = int(m.group(1)) if m else None
        vis["average_clusters_visible_reported"] = "average clusters visible" in vis_text.lower()
        result["bsp_after_vis"] = inspect_bsp(bsp_path, game)
        result["vis_ok"] = ok(vis)
        light = run_stage("q3map2-light", [q3map2, "-light", "-samples", "2", "-filter", "-bounce", "1",
                                           "-patchshadows"] + fs + [abs_bsp], case_dir, logs / "q3map2-light.log",
                          env=env, timeout=light_timeout)
        light_text = record(light)
        low = light_text.lower()
        found = re.findall(r"([0-9]+)[ \t]+luxels illuminated", low)
        light["luxels_illuminated"] = int(found[-1]) if found else None
        found = re.findall(r"([0-9]+)[ \t]+total lightmaps", low)
        light["total_lightmaps"] = int(found[-1]) if found else None
        result["light_ok"] = ok(light)
        result["bsp_final"] = inspect_bsp(bsp_path, game)
    if result["qbsp_ok"]:
        work = case_dir / "aaswork"
        aas_dir = case_dir / "aas"
        work.mkdir(exist_ok=True)
        aas_dir.mkdir(exist_ok=True)
        for stale in (work / "bspc.log",):
            if stale.exists():
                stale.unlink()
        mbspc = str(cfg.q3tools / "mbspc.exe")
        bspc = run_stage("bspc-bsp2aas", [mbspc, "-bsp2aas", abs_bsp, "-output", str(aas_dir).replace("\\", "/"),
                                          "-threads", "1", "-noverbose"], work, logs / "bspc-bsp2aas.log", env=env)
        bspc_log = ""
        if (work / "bspc.log").exists():
            bspc_log = (work / "bspc.log").read_text(encoding="utf-8", errors="replace")
            shutil.move(str(work / "bspc.log"), str(logs / "bspc-bsp2aas.bspc.log"))
        bspc["_stdout"] += "\n" + bspc_log
        record(bspc)
        aas_path = aas_dir / f"{stem}.aas"
        result["aas"] = inspect_aas(aas_path)
        result["bspc_ok"] = ok(bspc) and not bspc["tool_failure_markers"]
        if result["aas"]["exists"]:
            info = run_aasinfo(aas_path, work, logs, cfg, env)
            text = record(info)
            result["aasinfo"] = parse_aasinfo(text)
            result["aasinfo_ok"] = ok(info) and not info["tool_failure_markers"]
    return result


def q3_env(cfg) -> dict:
    env = dict(os.environ)
    env["PATH"] = str(cfg.q3_runtime) + os.pathsep + env.get("PATH", "")
    return env


def q3_fs_args(cfg, home: Path) -> list:
    return ["-game", "quake3", "-fs_basepath", str(cfg.q3_basepath).replace("\\", "/"),
            "-fs_homepath", str(home).replace("\\", "/"), "-fs_game", cfg.q3_game, "-threads", "2"]


def run_aasinfo(aas_path: Path, work: Path, logs: Path, cfg, env, log_name="bspc-aasinfo") -> dict:
    mbspc = str(cfg.q3tools / "mbspc.exe")
    work.mkdir(parents=True, exist_ok=True)
    if (work / "bspc.log").exists():
        (work / "bspc.log").unlink()
    info = run_stage("bspc-aasinfo", [mbspc, "-aasinfo", str(aas_path).replace("\\", "/"), "-threads", "1",
                                      "-noverbose"], work, logs / f"{log_name}.log", env=env)
    info_log = ""
    if (work / "bspc.log").exists():
        info_log = (work / "bspc.log").read_text(encoding="utf-8", errors="replace")
        shutil.move(str(work / "bspc.log"), str(logs / f"{log_name}.bspc.log"))
    info["_stdout"] += "\n" + info_log
    return info


def reconstruct_compile(game: str, src_dir: Path, stem: str) -> dict | None:
    """Rebuild compile_case()'s record from an earlier run's logs and files, without recompiling.

    The BSP is inspected once, after the final stage; bsp_after_qbsp / bsp_after_vis are therefore read from
    the final BSP (its header and visdata are not changed by light), and are marked as such."""
    logs = src_dir / "logs"
    first = "qbsp" if game in ("q1", "q2") else "q3map2-bsp"
    if not (logs / f"{first}.log").is_file():
        return None
    stages = []
    result = {"stages": stages, "reconstructed_from_logs": True}

    def record(stage):
        text = stage.pop("_stdout") + "\n" + stage.pop("_stderr")
        stage.update(scan_output(text, game, stage["name"]))
        stage["facts"] = log_facts(stage["name"], game, text)
        stages.append(stage)
        return text

    def ok(stage):
        return stage["exit_code"] == 0 and not stage["timed_out"]

    bsp_path = src_dir / f"{stem}.bsp"
    final = inspect_bsp(bsp_path, game)
    final_note = dict(final, inferred_from_final_bsp=True)
    if game in ("q1", "q2"):
        stage = read_stage_log(logs / "qbsp.log", "qbsp")
        record(stage)
        result["pointfiles"] = [p.name for p in (src_dir / f"{stem}.pts", src_dir / f"{stem}.lin") if p.exists()]
        result["leak"] = bool(result["pointfiles"] or stage["leak_markers"])
        result["prt"] = inspect_file_nonempty(src_dir / f"{stem}.prt")
        result["bsp_after_qbsp"] = final_note
        result["qbsp_ok"] = ok(stage) and final.get("header_ok", False)
        vis = read_stage_log(logs / "vis.log", "vis")
        if vis is not None:
            vis_text = record(vis)
            vis["full_vis_reported"] = "calculating full vis" in vis_text.lower()
            m = re.search(r"visdatasize:\s*([0-9]+)", vis_text.lower())
            vis["visdatasize"] = int(m.group(1)) if m else None
            result["bsp_after_vis"] = final_note
            result["vis_ok"] = ok(vis) and not vis["tool_failure_markers"]
        light = read_stage_log(logs / "light.log", "light")
        if light is not None:
            light_text = record(light)
            low = light_text.lower()
            light["lighting_completed"] = "lighting completed" in low
            m = re.search(r"([0-9]+) empty lightmaps", low)
            light["empty_lightmaps"] = int(m.group(1)) if m else None
            result["light_ok"] = ok(light) and not light["tool_failure_markers"]
            result["bsp_final"] = final
            if game == "q1":
                result["lit"] = inspect_lit(src_dir / f"{stem}.lit", final)
        return result
    stage = read_stage_log(logs / "q3map2-bsp.log", "q3map2-bsp")
    record(stage)
    result["pointfiles"] = [p.name for p in (src_dir / f"{stem}.lin", src_dir / f"{stem}.pts") if p.exists()]
    result["leak"] = bool(result["pointfiles"] or stage["leak_markers"])
    result["prt"] = inspect_file_nonempty(src_dir / f"{stem}.prt")
    result["srf"] = inspect_file_nonempty(src_dir / f"{stem}.srf")
    result["bsp_after_qbsp"] = final_note
    result["qbsp_ok"] = ok(stage) and final.get("header_ok", False)
    vis = read_stage_log(logs / "q3map2-vis.log", "q3map2-vis")
    if vis is not None:
        vis_text = record(vis)
        m = re.search(r"visdatasize:\s*([0-9]+)", vis_text.lower())
        vis["visdatasize"] = int(m.group(1)) if m else None
        vis["average_clusters_visible_reported"] = "average clusters visible" in vis_text.lower()
        result["bsp_after_vis"] = final_note
        result["vis_ok"] = ok(vis)
    light = read_stage_log(logs / "q3map2-light.log", "q3map2-light")
    if light is not None:
        light_text = record(light)
        low = light_text.lower()
        found = re.findall(r"([0-9]+)[ \t]+luxels illuminated", low)
        light["luxels_illuminated"] = int(found[-1]) if found else None
        found = re.findall(r"([0-9]+)[ \t]+total lightmaps", low)
        light["total_lightmaps"] = int(found[-1]) if found else None
        result["light_ok"] = ok(light)
        result["bsp_final"] = final
    bspc = read_stage_log(logs / "bspc-bsp2aas.log", "bspc-bsp2aas")
    if bspc is not None:
        extra = logs / "bspc-bsp2aas.bspc.log"
        bspc["_stdout"] += "\n" + (extra.read_text(encoding="utf-8", errors="replace") if extra.is_file() else "")
        record(bspc)
        aas_path = src_dir / "aas" / f"{stem}.aas"
        result["aas"] = inspect_aas(aas_path)
        result["bspc_ok"] = ok(bspc) and not bspc["tool_failure_markers"]
        info = read_stage_log(logs / "bspc-aasinfo.log", "bspc-aasinfo")
        if result["aas"]["exists"] and info is not None:
            extra = logs / "bspc-aasinfo.bspc.log"
            info["_stdout"] += "\n" + (extra.read_text(encoding="utf-8", errors="replace") if extra.is_file() else "")
            text = record(info)
            result["aasinfo"] = parse_aasinfo(text)
            result["aasinfo_ok"] = ok(info) and not info["tool_failure_markers"]
    return result


# --------------------------------------------------------------------------- gates

def gate(status: str, detail: str) -> dict:
    return {"status": status, "detail": detail}


def evaluate_gates(case: dict, res: dict) -> dict:
    """The draft-v0 gates G1-G7, unchanged."""
    game = case["game"]
    target_mode = case["mode"]
    gen = res["generate"]
    comp = res.get("compile") or {}
    mp = res.get("map") or {}
    classnames = mp.get("classnames", {})
    entities = mp.get("_entities", [])
    gates = {}
    generated = gen.get("exit_code") == 0 and gen.get("map_bytes", 0) > 0 and res.get("report") is not None
    gates["G1_generated"] = gate("pass" if generated else "fail",
                                 f"exit={gen.get('exit_code')} map_bytes={gen.get('map_bytes', 0)} "
                                 f"report={'yes' if res.get('report') else 'no'}")
    intent = res["intent"]
    gates["G2_intent_match"] = gate("pass" if intent["intent_match"] else "fail", intent["reason"])
    stages = {s["name"]: s for s in comp.get("stages", [])}
    if not comp:
        detail = "not compiled (no generated map)"
        gates["G3_leak_free_compile"] = gate("fail", detail)
        gates["G4_full_vis"] = gate("fail", detail)
        gates["G5_final_light"] = gate("fail", detail)
    else:
        bsp_stage = stages.get("qbsp") or stages.get("q3map2-bsp") or {}
        problems = []
        if not comp.get("qbsp_ok"):
            problems.append(f"bsp stage exit={bsp_stage.get('exit_code')} timed_out={bsp_stage.get('timed_out')}")
        if comp.get("leak"):
            problems.append(f"leak pointfiles={comp.get('pointfiles')} markers={bsp_stage.get('leak_markers')}")
        if bsp_stage.get("invalid_brush_markers"):
            problems.append(f"invalid-brush markers={bsp_stage.get('invalid_brush_markers')}")
        gates["G3_leak_free_compile"] = gate("fail" if problems else "pass", "; ".join(problems) or
                                             f"bsp ok, no leak, no invalid-brush markers; "
                                             f"map_warnings={bsp_stage.get('map_warnings_count')}")
        vis_stage = stages.get("vis") or stages.get("q3map2-vis")
        bvis = comp.get("bsp_after_vis") or {}
        prt = comp.get("prt") or {}
        if vis_stage is None:
            gates["G4_full_vis"] = gate("fail", f"VIS not run (prt bytes={prt.get('bytes')}, bsp ok={comp.get('qbsp_ok')})")
        else:
            problems = []
            if not comp.get("vis_ok"):
                problems.append(f"vis exit={vis_stage.get('exit_code')} timed_out={vis_stage.get('timed_out')} "
                                f"markers={vis_stage.get('tool_failure_markers')}")
            if not prt.get("bytes"):
                problems.append("empty .prt")
            if game in ("q1", "q2") and not vis_stage.get("full_vis_reported"):
                problems.append("no 'Calculating Full Vis' line")
            if not bvis.get("visdata_nonempty"):
                problems.append(f"visdata lump {bvis.get('visdata_lump')} bytes={bvis.get('visdata_bytes')} "
                                f"clusters={bvis.get('visdata_clusters')}")
            gates["G4_full_vis"] = gate("fail" if problems else "pass", "; ".join(problems) or
                                        f"prt={prt.get('bytes')}B visdata={bvis.get('visdata_bytes')}B "
                                        f"visdatasize={vis_stage.get('visdatasize')} wall={vis_stage.get('wall_seconds')}s")
        light_stage = stages.get("light") or stages.get("q3map2-light")
        bfin = comp.get("bsp_final") or {}
        if light_stage is None:
            gates["G5_final_light"] = gate("fail", "light not run")
        else:
            problems = []
            if not comp.get("light_ok"):
                problems.append(f"light exit={light_stage.get('exit_code')} timed_out={light_stage.get('timed_out')} "
                                f"markers={light_stage.get('tool_failure_markers')}")
            if not bfin.get("lightdata_nonempty"):
                problems.append(f"lightdata lump {bfin.get('lightdata_lump')} bytes={bfin.get('lightdata_bytes')} "
                                f"nonzero={bfin.get('lightdata_nonzero_bytes')}")
            gates["G5_final_light"] = gate("fail" if problems else "pass", "; ".join(problems) or
                                           f"lightdata={bfin.get('lightdata_bytes')}B wall={light_stage.get('wall_seconds')}s")
    gates["G6_aas_valid"] = gate_g6(game, comp)
    gates["G7_mode_entities"] = gate_g7(game, target_mode, mp, classnames, entities)
    return gates


def gate_g6(game: str, comp: dict) -> dict:
    stages = {s["name"]: s for s in (comp or {}).get("stages", [])}
    if game != "q3":
        return gate("n/a", "AAS applies to Quake 3 only")
    if not comp or not comp.get("qbsp_ok"):
        return gate("fail", "no BSP for BSPC")
    problems = []
    bspc_stage = stages.get("bspc-bsp2aas") or {}
    aas = comp.get("aas") or {}
    info = comp.get("aasinfo") or {}
    metrics = info.get("metrics") or {}
    if not comp.get("bspc_ok"):
        problems.append(f"bspc exit={bspc_stage.get('exit_code')} markers={bspc_stage.get('tool_failure_markers')}")
    if not aas.get("envelope_ok"):
        problems.append(f"aas envelope magic={aas.get('magic')} version={aas.get('version')} bytes={aas.get('bytes')}")
    if not comp.get("aasinfo_ok"):
        problems.append("aasinfo failed")
    if not metrics.get("numareas") or metrics.get("numareas") != metrics.get("numareasettings"):
        problems.append(f"areas={metrics.get('numareas')} settings={metrics.get('numareasettings')}")
    if not metrics.get("reachabilitysize") or metrics.get("reachabilitysize", 0) <= 1:
        problems.append(f"reachabilitysize={metrics.get('reachabilitysize')}")
    if not info.get("reachability_consistent"):
        problems.append("reachability categories do not sum to reachabilitysize-1")
    reach = info.get("reachability") or {}
    return gate("fail" if problems else "pass", "; ".join(problems) or
                f"EAAS v5 {aas.get('bytes')}B areas={metrics.get('numareas')} "
                f"clusters={metrics.get('numclusters')} reach={metrics.get('reachabilitysize')} "
                f"walk={reach.get('walk')} barrier={reach.get('barrier jump')} "
                f"drop={reach.get('walk off ledge')} jumppad={reach.get('jump pad')} "
                f"teleport={reach.get('teleport')}")


def gate_g7(game: str, target_mode: str, mp: dict, classnames: dict, entities: list) -> dict:
    dm = classnames.get("info_player_deathmatch", 0)
    if not mp:
        return gate("fail", "no map")
    if target_mode == "sp":
        need = ["info_player_start"] + SP_EXIT[game]
        missing = [c for c in need if not classnames.get(c)]
        detail = f"info_player_start={classnames.get('info_player_start', 0)} exit={ {c: classnames.get(c, 0) for c in SP_EXIT[game]} }"
        if game == "q2" and not missing:
            names = {e["keys"].get("targetname") for e in entities if e["keys"].get("classname") == "target_changelevel"}
            linked = any(e["keys"].get("classname") == "trigger_once" and e["keys"].get("target") in names
                         for e in entities)
            if not linked:
                missing.append("trigger_once->target_changelevel link")
            detail += f" trigger_once_linked={linked}"
        return gate("fail" if missing else "pass", detail + (f"; missing {missing}" if missing else ""))
    if target_mode == "coop":
        n = classnames.get("info_player_coop", 0)
        return gate("pass" if n else "fail", f"info_player_coop={n} info_player_start={classnames.get('info_player_start', 0)}")
    if target_mode in ("dm", "ffa", "duel", "tourney", "botarena"):
        return gate("pass" if dm >= 2 else "fail", f"info_player_deathmatch={dm} (need >= 2)")
    if target_mode in ("ctf", "ctf-threewave"):
        need = FLAGS[game] + TEAM_SPAWNS[game]
        counts = {c: classnames.get(c, 0) for c in need}
        missing = [c for c, n in counts.items() if not n]
        return gate("fail" if missing else "pass", f"{counts}")
    if target_mode == "tdm":
        return gate("n/a", f"{TDM_NA_REASON[game]}; observed info_player_deathmatch={dm}")
    return gate("fail", f"unknown target mode {target_mode}")


def intent_draft(case: dict, report) -> dict:
    expected_game = GAME_NAMES[case["game"]]
    expected_mode = TARGET_MODES.get(case["mode"])
    resolved_game = report.get("game") if report else None
    resolved_mode = report.get("mode") if report else None
    if report is None:
        intent = {"intent_match": False, "reason": "no report"}
    elif expected_mode is None:
        intent = {"intent_match": False,
                  "reason": f"target mode not representable; resolved game={resolved_game} mode={resolved_mode}"}
    else:
        match = resolved_game == expected_game and resolved_mode == expected_mode
        intent = {"intent_match": match,
                  "reason": f"expected {expected_game}/{expected_mode}; resolved {resolved_game}/{resolved_mode}"}
    intent.update({"expected_game": expected_game, "expected_mode": expected_mode,
                   "target_mode_representable": expected_mode is not None,
                   "resolved_game": resolved_game, "resolved_mode": resolved_mode})
    return intent


def intent_v1(case: dict, report, extras: dict) -> dict:
    """v1 G2: game and primary mode with the D4 mapping; module and player range when the report carries them.
    With schema-v4 reports it gives the draft verdict for every target."""
    expected_game = GAME_NAMES[case["game"]]
    expected_mode = TARGET_MODES_V1.get(case["mode"])
    module = (extras or {}).get("module")
    if report is None:
        return {"intent_match": False, "reason": "no report", "expected_game": expected_game,
                "expected_mode": expected_mode}
    resolved_game, resolved_mode = report.get("game"), report.get("mode")
    problems = []
    if resolved_game != expected_game:
        problems.append(f"game {resolved_game} != {expected_game}")
    if resolved_mode != expected_mode:
        problems.append(f"mode {resolved_mode} != {expected_mode}")
    expected_module = case.get("module")
    required = MODULE_REQUIRED_MODES.get(case["mode"])
    if module is not None and expected_module and module != expected_module:
        problems.append(f"module {module} != {expected_module}")
    elif module is None and required:
        problems.append(f"module {required} required but the report declares no module")
    module_status = "reported" if module is not None else "not reported (report has no module field)"
    return {"intent_match": not problems,
            "reason": "; ".join(problems) or f"{resolved_game}/{resolved_mode}; module {module_status}",
            "expected_game": expected_game, "expected_mode": expected_mode, "expected_module": expected_module,
            "resolved_game": resolved_game, "resolved_mode": resolved_mode, "resolved_module": module,
            "player_range": {"min": (extras or {}).get("player_min"), "max": (extras or {}).get("player_max")}}


def gate_g4_v1(game: str, comp: dict) -> dict:
    """Full VIS: draft checks plus an anchored full-VIS marker and rejection of -fast output.

    Discriminator (found with vis -fast on a project-authored map, see README): ericw vis -fast still prints
    'Calculating Full Vis:' but adds the option line '"fast" was set to "1"' and reports 'c_chains: 0';
    q3map2 -vis -fast prints 'fastvis = true' and skips the PassagePortalFlow/PortalFlow pass."""
    if not comp:
        return gate("fail", "not compiled")
    stages = {s["name"]: s for s in comp.get("stages", [])}
    vis_stage = stages.get("vis") or stages.get("q3map2-vis")
    prt = comp.get("prt") or {}
    bvis = comp.get("bsp_after_vis") or {}
    if vis_stage is None:
        return gate("fail", f"VIS not run (prt bytes={prt.get('bytes')}, bsp ok={comp.get('qbsp_ok')})")
    facts = vis_stage.get("facts") or {}
    problems = []
    if not comp.get("vis_ok"):
        problems.append(f"vis exit={vis_stage.get('exit_code')} timed_out={vis_stage.get('timed_out')} "
                        f"markers={vis_stage.get('tool_failure_markers')}")
    if not prt.get("bytes"):
        problems.append("empty .prt")
    if "-fast" in (vis_stage.get("command") or []):
        problems.append("vis command line has -fast")
    if game in ("q1", "q2"):
        if not facts.get("full_vis_line"):
            problems.append("no anchored 'Calculating Full Vis:' line")
        if facts.get("fast_option_line"):
            problems.append("'\"fast\" was set to \"1\"' option line (vis -fast output)")
        if not facts.get("c_chains"):
            problems.append(f"c_chains={facts.get('c_chains')} (full portal flow not run)")
    else:
        if facts.get("fastvis_line"):
            problems.append("'fastvis = true' line (q3map2 -vis -fast output)")
        if not facts.get("portal_flow_line"):
            problems.append("no anchored '--- PassagePortalFlow/PortalFlow (N) ---' line")
    if not bvis.get("visdata_nonempty"):
        problems.append(f"visdata lump {bvis.get('visdata_lump')} bytes={bvis.get('visdata_bytes')} "
                        f"clusters={bvis.get('visdata_clusters')}")
    flow = f"c_chains={facts.get('c_chains')}" if game in ("q1", "q2") else "portal flow line present"
    return gate("fail" if problems else "pass", "; ".join(problems) or
                f"anchored full-VIS marker; no -fast; prt={prt.get('bytes')}B visdata={bvis.get('visdata_bytes')}B "
                f"{flow} wall={vis_stage.get('wall_seconds')}s")


def gate_g5_v1(game: str, comp: dict, budget: float) -> dict:
    """Final light: anchored '0 empty lightmaps' line (Q1/Q2) or a nonzero anchored 'N total lightmaps' line (Q3),
    a nonempty light lump, and a light wall time within the light budget."""
    if not comp:
        return gate("fail", "not compiled")
    stages = {s["name"]: s for s in comp.get("stages", [])}
    light_stage = stages.get("light") or stages.get("q3map2-light")
    bfin = comp.get("bsp_final") or {}
    if light_stage is None:
        return gate("fail", "light not run")
    facts = light_stage.get("facts") or {}
    problems = []
    if not comp.get("light_ok"):
        problems.append(f"light exit={light_stage.get('exit_code')} timed_out={light_stage.get('timed_out')} "
                        f"markers={light_stage.get('tool_failure_markers')}")
    if game in ("q1", "q2"):
        lines = facts.get("empty_lightmaps_lines") or []
        if lines != [0]:
            problems.append(f"anchored 'N empty lightmaps' lines={lines} (need exactly one line with N=0)")
    else:
        lines = facts.get("total_lightmaps_lines") or []
        if not lines or lines[-1] <= 0:
            problems.append(f"anchored 'N total lightmaps' lines={lines} (need a final N > 0)")
    if not bfin.get("lightdata_nonempty"):
        problems.append(f"lightdata lump {bfin.get('lightdata_lump')} bytes={bfin.get('lightdata_bytes')} "
                        f"nonzero={bfin.get('lightdata_nonzero_bytes')}")
    wall = light_stage.get("wall_seconds")
    if wall is None or wall > budget:
        problems.append(f"light wall {wall}s exceeds the light budget {budget}s")
    return gate("fail" if problems else "pass", "; ".join(problems) or
                f"anchored light line ok; lightdata={bfin.get('lightdata_bytes')}B wall={wall}s <= budget {budget}s")


def evaluate_gates_v1(case: dict, res: dict, cfg) -> dict:
    draft = evaluate_gates(case, res)
    out = dict(draft)
    out["G2_intent_match"] = gate("pass" if res["intent"]["intent_match"] else "fail", res["intent"]["reason"])
    comp = res.get("compile")
    if comp:
        out["G4_full_vis"] = gate_g4_v1(case["game"], comp)
        out["G5_final_light"] = gate_g5_v1(case["game"], comp, cfg.light_budget)
    return out


def gate_g9(requests: list, extras: dict) -> dict:
    """Request accounting: every expected request is honoured, or covered by a typed report finding."""
    covered_texts = set()
    for item in (extras.get("typed_findings") or []) + (extras.get("unmet") or []):
        if item.get("request"):
            covered_texts.add(norm_text(item["request"]))
    for item in extras.get("request_trace") or []:
        text = item.get("request") or item.get("text")
        has_finding = item.get("finding") or item.get("finding_code") or item.get("findings")
        if text and has_finding:
            covered_texts.add(norm_text(text))
    honoured, covered, drops = 0, 0, []
    for req in requests:
        if norm_text(req["request"]) == "conflict surfaced":
            continue  # accounted by G10
        if req["status"] == "honoured":
            honoured += 1
        elif norm_text(req["request"]) in covered_texts:
            covered += 1
        else:
            drops.append(f"{req['request']} [{req['status']}]")
    total = honoured + covered + len(drops)
    detail = f"{total} request(s): honoured {honoured}, covered by a typed finding {covered}, silent drops {len(drops)}"
    if drops:
        detail += ": " + "; ".join(drops[:12]) + (" ..." if len(drops) > 12 else "")
    if not extras.get("typed_findings") and not extras.get("request_trace") and not extras.get("unmet"):
        detail += " (report has no typed findings or request trace)"
    status = "pass" if not drops else "fail"
    return {"status": status, "detail": detail, "silent_drops": len(drops), "honoured": honoured,
            "covered": covered}


def _span_overlap(a: dict, b: dict) -> bool:
    try:
        return int(a["start"]) < int(b["end"]) and int(b["start"]) < int(a["end"])
    except (KeyError, TypeError, ValueError):
        return False


def gate_g10(case: dict, report_summary, extras: dict) -> dict:
    """Conflict surfaced: each expected conflict appears as a typed finding of its class covering its spans,
    and the report follows the declared resolution (primary game and mode)."""
    expected = case.get("expected_conflicts") or []
    if not expected:
        return gate("n/a", "no expected conflicts")
    findings = extras.get("typed_findings") or []
    missing = []
    for conflict in expected:
        cls = conflict["class"]
        found = False
        for f in findings:
            codes = {str(f.get("class")), str(f.get("code"))}
            if not ({cls, "prompt." + cls, "prompt.conflict." + cls} & codes):
                continue
            spans = f.get("spans") or f.get("span") or []
            if isinstance(spans, dict):
                spans = [spans]
            if all(any(_span_overlap(want, have) for have in spans) for want in conflict["span"]):
                found = True
                break
        if not found:
            missing.append(cls + " [" + ", ".join(s["text"] for s in conflict["span"]) + "]")
    resolution = case.get("expected_resolution") or {}
    primary = resolution.get("primary") or {}
    problems = []
    if missing:
        problems.append("no typed conflict finding for " + "; ".join(missing))
    if report_summary is None:
        problems.append("no report")
    elif primary and (report_summary.get("game") != primary.get("game") or
                      report_summary.get("mode") != primary.get("mode")):
        problems.append(f"resolution: report {report_summary.get('game')}/{report_summary.get('mode')} != declared "
                        f"{primary.get('game')}/{primary.get('mode')}")
    if not findings:
        problems.append("report has no typed findings")
    return gate("fail" if problems else "pass", "; ".join(problems) or
                f"{len(expected)} conflict(s) surfaced with spans; resolution {primary}")


def gate_g11(extras: dict, report_summary) -> dict:
    if report_summary is None:
        return gate("fail", "no report")
    codes = extras.get("error_findings_by_code") or {}
    total = sum(codes.values())
    result = gate("pass" if not total else "fail",
                  f"{total} error-severity analysis finding(s)" + (f": {codes}" if codes else ""))
    result["by_code"] = codes
    return result


def gate_g12(case: dict, res: dict, cfg) -> dict:
    game = case["game"]
    mp = res.get("map")
    if not mp:
        return gate("fail", "no map")
    textures = sorted(mp.get("textures") or {})
    if game == "q1":
        if not cfg.q1_wad:
            return gate("fail", "no WAD (D9 pending)")
        tool = {"clip", "skip", "hint", "hintskip", "origin"}
        missing = [t for t in textures if t.lower() not in cfg.q1_wad_names and t.lower() not in tool]
        return gate("fail" if missing else "pass", (f"missing from WAD {cfg.q1_wad_label}: {missing}" if missing
                                                    else f"{len(textures)} texture(s) in WAD {cfg.q1_wad_label}"))
    resolution = mp.get("texture_resolution") or {t: cfg.assets.resolve(game, t) for t in textures}
    problems = []
    if game == "q2":
        bad = {t: r for t, r in resolution.items() if r != "wal"}
        if bad:
            problems.append(f"no .wal in {cfg.q2_assets}: {bad}")
        comp = res.get("compile") or {}
        qbsp = next((s for s in comp.get("stages", []) if s["name"] == "qbsp"), None)
        internal = []
        if qbsp is None:
            problems_note = "qbsp not run (generate-only): resolution check only"
        else:
            lowered = {t.lower() for t in textures}
            missing = [n for n in (qbsp.get("facts") or {}).get("missing_texture_lines", []) if n.lower() in lowered]
            internal = [n for n in (qbsp.get("facts") or {}).get("missing_texture_lines", []) if n.lower() not in lowered]
            if missing:
                problems.append(f"qbsp (no -notex) could not locate {missing}")
            problems_note = f"qbsp without -notex: no missing map texture; compiler-internal lookups {internal}"
        return gate("fail" if problems else "pass", "; ".join(problems) or
                    f"{len(textures)} texture(s) resolve to .wal; {problems_note}")
    bad = {t: r for t, r in resolution.items() if r == "missing"}
    comp = res.get("compile") or {}
    bsp_stage = next((s for s in comp.get("stages", []) if s["name"] == "q3map2-bsp"), None)
    images = (bsp_stage.get("facts") or {}).get("missing_image_lines", []) if bsp_stage else []
    if bad:
        problems.append(f"unresolved shaders/textures: {sorted(bad)}")
    if images:
        problems.append(f"q3map2 could not find images for {images}")
    return gate("fail" if problems else "pass", "; ".join(problems) or
                f"{len(textures)} shader/texture name(s) resolve (shader script or image)")


def reported_gates(case: dict, res: dict, cfg) -> dict:
    out = {}
    det = res.get("determinism") or {}
    if det.get("status") in ("pass", "fail"):
        out["G8_determinism"] = gate(det["status"], det.get("detail", ""))
    else:
        out["G8_determinism"] = gate(NOT_RUN, det.get("detail") or "not run (use --repeat 2)")
    extras = res.get("report_extras") or {}
    out["G9_request_accounting"] = gate_g9(res.get("requests") or [], extras)
    out["G10_conflict_surfaced"] = gate_g10(case, res.get("report"), extras)
    out["G11_error_findings"] = gate_g11(extras, res.get("report"))
    out["G12_materials"] = gate_g12(case, res, cfg)
    probe = res.get("lighting_probe") or {}
    floor = (probe.get("floor") or {}) if probe.get("available") else {}
    out["G13_lighting_readability"] = gate("n/a", "not defined until Phase 3 (stock band from B); floor luxels: "
                                           + (f"n={floor.get('n')} mean={floor.get('mean')} <16: {floor.get('lt16')}% "
                                              f">=250: {floor.get('ge250')}%" if floor else "not measured"))
    out["G14_engine_smoke"] = gate(NOT_CONFIGURED, "no engine smoke harness is configured (H1/H2; D16-D18 pending)")
    if case["game"] == "q3":
        out["G15_q3_entity_reachability"] = gate(NOT_CONFIGURED, "q3host per-entity reachability not built (H2)")
    else:
        out["G15_q3_entity_reachability"] = gate("n/a", "Quake 3 only")
    return out


def all_hard(gates: dict):
    statuses = [gates[k]["status"] for k in HARD_GATE_KEYS if k in gates]
    if any(s in (NOT_RUN, NOT_CONFIGURED) for s in statuses):
        return None
    applicable = [s for s in statuses if s != "n/a"]
    return bool(applicable) and all(s == "pass" for s in applicable)


# --------------------------------------------------------------------------- manifests, seal, identity

def load_registry() -> dict:
    if REGISTRY_PATH.is_file():
        return json.loads(REGISTRY_PATH.read_text(encoding="utf-8"))
    return {"manifests": []}


def check_registered(path: Path, registry: dict) -> dict:
    relpath = rel(path)
    digest = sha256_file(path)
    for row in registry.get("manifests", []) + ([registry["draft"]] if registry.get("draft") else []):
        if row.get("path") == relpath:
            return {"registered": True, "set": row.get("set"), "frozen": row.get("frozen", False),
                    "sha256": digest, "registered_sha256": row.get("sha256"),
                    "matches": row.get("sha256") == digest}
    return {"registered": False, "sha256": digest}


def load_manifest(path: Path, registry: dict, allow_modified: bool) -> tuple:
    info = check_registered(path, registry)
    if info.get("registered") and info.get("frozen") and not info["matches"] and not allow_modified:
        raise SystemExit(f"frozen manifest {rel(path)} differs from tools/benchmark/manifests.json "
                         f"({info['sha256'][:16]} != {info['registered_sha256'][:16]}); refusing")
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if "cases" not in manifest and manifest.get("base_manifest"):
        base_path = REPO / manifest["base_manifest"]["path"]
        base_info = check_registered(base_path, registry)
        if manifest["base_manifest"].get("sha256") != base_info["sha256"]:
            raise SystemExit(f"{rel(path)} names base {rel(base_path)} sha256 "
                             f"{manifest['base_manifest'].get('sha256', '')[:16]}, found {base_info['sha256'][:16]}")
        base, _ = load_manifest(base_path, registry, allow_modified)
        wanted = list(manifest["ids"])
        by_id = {c["id"]: c for c in base["cases"]}
        missing = [i for i in wanted if i not in by_id]
        if missing:
            raise SystemExit(f"{rel(path)} names unknown ids {missing}")
        manifest = dict(manifest, cases=[by_id[i] for i in wanted])
    return manifest, info


def load_seal() -> dict:
    if not SEALED_PATH.is_file():
        return {"ids": set(), "prompts": set(), "sha256": None}
    data = json.loads(SEALED_PATH.read_text(encoding="utf-8"))
    return {"ids": {s["id"] for s in data.get("sealed", [])},
            "prompts": {s.get("normalized_prompt_sha256") for s in data.get("sealed", [])},
            "sha256": sha256_file(SEALED_PATH)}


def is_sealed(case: dict, seal: dict) -> bool:
    if case["id"] in seal["ids"]:
        return True
    return sha256_bytes(norm_text(case.get("prompt", "")).encode("utf-8")) in seal["prompts"]


def git_bytes(*args) -> bytes:
    completed = subprocess.run(["git", "--no-optional-locks", "-C", str(REPO)] + list(args),
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.decode("utf-8", errors="replace").strip())
    return completed.stdout


def source_snapshot() -> dict:
    """Source identity: git HEAD, sha256 of `git diff`, sha256 of the sorted untracked-file hash list.
    Read-only git commands only (--no-optional-locks: the index is not refreshed)."""
    try:
        head = git_bytes("rev-parse", "HEAD").decode().strip()
        diff = git_bytes("-c", "core.quotepath=off", "diff", "--no-color", "--no-ext-diff")
        names = [n for n in git_bytes("-c", "core.quotepath=off", "ls-files", "--others", "--exclude-standard",
                                      "-z").decode("utf-8", errors="replace").split("\0") if n]
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        return {"available": False, "error": str(error)}
    lines = []
    for name in sorted(names):
        path = REPO / name
        digest = sha256_file(path) if path.is_file() else "missing"
        lines.append(f"{digest}  {name}\n")
    listing = "".join(lines).encode("utf-8")
    return {"available": True, "git_head": head, "git_diff_sha256": sha256_bytes(diff), "git_diff_bytes": len(diff),
            "untracked_list_sha256": sha256_bytes(listing), "untracked_files": len(names),
            "rule": "git diff --no-color --no-ext-diff; untracked = git ls-files --others --exclude-standard, "
                    "lines '<sha256>  <path>' sorted by path"}


def tool_hashes(cfg) -> dict:
    tools = {"qmg": sha256_file(cfg.qmg) if cfg.qmg.is_file() else None}
    for name, path in (("qbsp", cfg.ericw / "qbsp.exe"), ("vis", cfg.ericw / "vis.exe"),
                       ("light", cfg.ericw / "light.exe"), ("q3map2", cfg.q3tools / "q3map2.exe"),
                       ("mbspc", cfg.q3tools / "mbspc.exe")):
        tools[name] = sha256_file(path) if path.is_file() else None
    return tools


def build_identity(cfg, tools: dict, manifest_info: dict) -> dict:
    return {
        "runner_sha256": RUNNER_SHA256, "runner_path": rel(RUNNER_PATH),
        "manifest_path": rel(cfg.manifest), "manifest_sha256": manifest_info["sha256"],
        "manifest_registered": manifest_info.get("registered", False),
        "manifest_set": manifest_info.get("set"),
        "qmg_path": str(cfg.qmg), "qmg_sha256": tools.get("qmg"),
        "qmg_extra": cfg.qmg_extra_list,
        "source_snapshot": cfg.snapshot,
        "tool_sha256": {k: v for k, v in tools.items() if k != "qmg"},
        "tool_pins": TOOL_PINS,
        "tool_pins_ok": all(tools.get(n) == p for n, p in TOOL_PINS.items()),
        "seal_list_sha256": cfg.seal.get("sha256"),
        "policy": cfg.policy,
        "light_budget_seconds": cfg.light_budget,
        "light_timeout_seconds": cfg.light_timeout,
        "concurrency": {"jobs": cfg.jobs, "tool_threads": TOOL_THREADS,
                        "matches_light_budget_rule": cfg.jobs == 8 and TOOL_THREADS == 2},
        "q1_wad": cfg.q1_wad_label,
    }


# --------------------------------------------------------------------------- case driver

def generate_args(case: dict, prompt_file: Path | None, extra: list) -> tuple:
    """qmg arguments (without the executable) and the map file name for a case."""
    if case.get("qmg_args"):
        args = []
        for token in case["qmg_args"]:
            token = token.replace("{prompt_file}", str(prompt_file) if prompt_file else "")
            token = token.replace("{prompt}", case.get("prompt", ""))
            token = token.replace("{seed}", str(case.get("seed")))
            args.append(token)
        out_index = args.index("--output") + 1
        map_name = args[out_index]
    else:
        map_name = f"{case['id']}.map"
        args = ["generate", "--prompt", case["prompt"], "--seed", str(case["seed"]), "--output", map_name]
    return args + list(extra), map_name


def write_prompt_file(case: dict, case_dir: Path):
    if case.get("qmg_args") and any("{prompt_file}" in t for t in case["qmg_args"]):
        path = case_dir / "prompt.txt"
        path.write_bytes(case["prompt"].encode("utf-8"))
        expected = case.get("prompt_sha256")
        if expected and sha256_file(path) != expected:
            raise RuntimeError(f"{case['id']}: prompt bytes do not match prompt_sha256")
        return path
    return None


def determinism_check(case: dict, cfg, case_dir: Path, map_name: str, prompt_file) -> dict:
    """G8: regenerate in a separate directory and process; byte-compare .map and .qmg.json."""
    stem = Path(map_name).stem
    other = case_dir / "repeat-2"
    if other.exists():
        shutil.rmtree(ensure_inside(other, cfg.output_root))
    other.mkdir(parents=True)
    args, _ = generate_args(case, prompt_file, cfg.qmg_extra_list)
    stage = run_stage("generate-repeat", [str(cfg.qmg)] + args, other, case_dir / "logs" / "generate-repeat.log")
    comparisons = {}
    for suffix in (".map", ".qmg.json"):
        a, b = case_dir / (stem + suffix), other / (stem + suffix)
        comparisons[suffix] = {"first_sha256": sha256_file(a) if a.is_file() else None,
                               "second_sha256": sha256_file(b) if b.is_file() else None}
        comparisons[suffix]["identical"] = (comparisons[suffix]["first_sha256"] is not None and
                                            comparisons[suffix]["first_sha256"] == comparisons[suffix]["second_sha256"])
    identical = all(c["identical"] for c in comparisons.values()) and stage["exit_code"] == 0
    if identical:
        shutil.rmtree(ensure_inside(other, cfg.output_root))
    return {"status": "pass" if identical else "fail", "method": "second process in a separate directory",
            "wall_seconds": stage["wall_seconds"], "comparisons": comparisons,
            "detail": ("byte-identical .map and .qmg.json across two processes" if identical else
                       f"differs: {[k for k, v in comparisons.items() if not v['identical']]} "
                       f"(second exit={stage['exit_code']}; kept in repeat-2/)")}


def parse_outputs(case: dict, cfg, case_dir: Path, map_name: str, res: dict):
    stem = Path(map_name).stem
    report_path = case_dir / f"{stem}.qmg.json"
    map_path = case_dir / map_name
    report, raw = None, None
    if report_path.is_file():
        try:
            raw = json.loads(report_path.read_text(encoding="utf-8"))
            report = summarize_report(raw)
        except (ValueError, KeyError, TypeError) as error:
            res["report_error"] = str(error)
    res["report"] = report
    res["report_extras"] = report_extras(raw) if raw is not None else {}
    mp = None
    if map_path.is_file() and map_path.stat().st_size > 0:
        try:
            mp = parse_map(map_path)
            mp["texture_resolution"] = {t: cfg.assets.resolve(case["game"], t) for t in mp["textures"]}
        except OSError as error:
            res["map_error"] = str(error)
    res["map"] = mp
    return report, mp


def case_header(case: dict, cfg) -> dict:
    keys = ["id", "target", "path", "game", "mode", "category", "prompt", "seed", "expected_requests", "notes",
            "claimed", "module", "expected_conflicts", "expected_resolution", "qmg_args", "group", "map_name"]
    out = {"schema": "qmg.benchmark.case_result", "schema_version": 2, "policy": cfg.policy,
           "runner_version": RUNNER_VERSION, "identity": cfg.identity}
    for key in keys:
        if key in case:
            out[key] = case[key]
    out.setdefault("path", case.get("target"))
    return out


def finish_case(case: dict, cfg, res: dict, started: float) -> dict:
    report, mp = res.get("report"), res.get("map")
    t0 = time.perf_counter()
    if cfg.policy == "draft":
        res["intent"] = intent_draft(case, report)
    else:
        res["intent"] = intent_v1(case, report, res.get("report_extras"))
    if res.get("compile") is not None:
        res["compile"]["total_wall_seconds"] = round(sum((s.get("wall_seconds") or 0)
                                                          for s in res["compile"]["stages"]), 3)
    draft_gates = evaluate_gates(case, dict(res, intent=intent_draft(case, report)))
    if cfg.policy == "draft":
        hard = draft_gates
    else:
        hard = evaluate_gates_v1(case, res, cfg)
    if res.get("compile") is None and cfg.generate_only:
        for key in ("G3_leak_free_compile", "G4_full_vis", "G5_final_light"):
            hard[key] = gate(NOT_RUN, "generate-only")
        if case["game"] == "q3":
            hard["G6_aas_valid"] = gate(NOT_RUN, "generate-only")
    comp = res.get("compile")
    res["gate_variants"] = {
        "draft": {k: draft_gates[k] for k in ("G4_full_vis", "G5_final_light")},
        "v1": ({"G4_full_vis": gate_g4_v1(case["game"], comp),
                "G5_final_light": gate_g5_v1(case["game"], comp, cfg.light_budget)} if comp else None),
    }
    res["lighting_probe"] = lighting_probe(res["_case_dir_for_probe"] / f"{Path(res['_map_name']).stem}.bsp",
                                           case["game"]) if comp else {"available": False, "reason": "not compiled"}
    ctx = {"game_code": case["game"], "report": report, "map": mp, "generate": res["generate"], "gates": hard,
           "report_extras": res.get("report_extras"), "surfaceparms": cfg.assets.q3_surfaceparms}
    evaluator = evaluate_request if cfg.policy == "draft" else evaluate_request_v1
    res["requests"] = [evaluator(r, ctx) for r in case.get("expected_requests", [])]
    gates = dict(hard)
    gates.update(reported_gates(case, res, cfg))
    res["gates"] = gates
    res["all_hard_gates"] = all_hard(gates)
    g10 = gates["G10_conflict_surfaced"]["status"]
    res["all_hard_gates_with_G10"] = None if res["all_hard_gates"] is None else \
        bool(res["all_hard_gates"] and g10 in ("pass", "n/a"))
    if mp is not None:
        mp.pop("_entities", None)
    res.pop("_case_dir_for_probe", None)
    res.pop("_map_name", None)
    res["timers"]["evaluate"] = round(time.perf_counter() - t0, 3)
    res["finished_at"] = now_iso()
    res["wall_seconds"] = round(time.perf_counter() - started, 3)
    res["timers"]["total"] = res["wall_seconds"]
    return res


def stage_timers(comp) -> dict:
    out = {}
    for stage in (comp or {}).get("stages", []):
        out[stage["name"]] = stage.get("wall_seconds")
    return out


def prepare_case_dir(case: dict, cfg) -> Path | None:
    case_dir = ensure_inside(cfg.output / case["id"], cfg.output_root)
    if (case_dir / "result.json").exists() and not cfg.force:
        return None
    if case_dir.exists():
        # Either --force or an interrupted earlier run (no result.json): start clean so stale
        # .pts/.lin pointfiles or BSPs can never be mistaken for this run's evidence.
        shutil.rmtree(ensure_inside(case_dir, cfg.output_root))
    case_dir.mkdir(parents=True, exist_ok=True)
    return case_dir


def run_case(case: dict, cfg) -> dict:
    case_dir = prepare_case_dir(case, cfg)
    if case_dir is None:
        return {"id": case["id"], "skipped": True}
    started_at = now_iso()
    started = time.perf_counter()
    prompt_file = write_prompt_file(case, case_dir)
    args, map_name = generate_args(case, prompt_file, cfg.qmg_extra_list)
    stage = run_stage("generate", [str(cfg.qmg)] + args, case_dir, case_dir / "logs" / "generate.log")
    map_path = case_dir / map_name
    stem = Path(map_name).stem
    gen = {
        "command": stage["command"],
        "exit_code": stage["exit_code"], "timed_out": stage["timed_out"], "wall_seconds": stage["wall_seconds"],
        "stdout_tail": tail(stage["_stdout"], 1500), "stderr_tail": tail(stage["_stderr"], 1500),
        "map_exists": map_path.is_file(), "map_bytes": map_path.stat().st_size if map_path.is_file() else 0,
        "report_exists": (case_dir / f"{stem}.qmg.json").is_file(),
    }
    res = case_header(case, cfg)
    res.update({"started_at": started_at, "generate": gen, "timers": {"generate": gen["wall_seconds"]},
                "_case_dir_for_probe": case_dir, "_map_name": map_name})
    for suffix in (".map", ".qmg.json"):
        path = case_dir / (stem + suffix)
        gen[suffix.lstrip(".").replace(".", "_") + "_sha256"] = sha256_file(path) if path.is_file() else None
    report, mp = parse_outputs(case, cfg, case_dir, map_name, res)
    if cfg.repeat >= 2 and gen["exit_code"] == 0:
        res["determinism"] = determinism_check(case, cfg, case_dir, map_name, prompt_file)
        res["timers"]["generate_repeat"] = res["determinism"]["wall_seconds"]
    else:
        res["determinism"] = {"status": NOT_RUN, "detail": "not run (use --repeat 2)"}
    res["compile"] = None
    if mp is not None and gen["exit_code"] == 0 and not cfg.generate_only:
        res["compile"] = compile_case(case, case_dir, map_name, cfg,
                                      light_timeout=cfg.light_timeout if cfg.policy == "v1" else STAGE_TIMEOUT)
        res["timers"].update(stage_timers(res["compile"]))
    finish_case(case, cfg, res, started)
    write_json(case_dir / "result.json", res)
    return {"id": case["id"], "skipped": False, "all_hard_gates": res["all_hard_gates"],
            "wall_seconds": res["wall_seconds"]}


def import_case(case: dict, cfg) -> dict:
    """Re-evaluate an earlier run's compiled outputs without recompiling. The .map and .qmg.json are regenerated
    in a fresh process with the current qmg and must be byte-identical to the imported ones (sealed cases are
    read only and not regenerated)."""
    src = cfg.import_dir / case["id"]
    src_result = src / "result.json"
    if not src_result.is_file():
        return {"id": case["id"], "error": f"no result.json in {src}"}
    old = json.loads(src_result.read_text(encoding="utf-8"))
    case_dir = prepare_case_dir(case, cfg)
    if case_dir is None:
        return {"id": case["id"], "skipped": True}
    started = time.perf_counter()
    sealed = is_sealed(case, cfg.seal)
    prompt_file = write_prompt_file(case, case_dir)
    args, map_name = generate_args(case, prompt_file, cfg.qmg_extra_list)
    stem = Path(map_name).stem
    old_args = (old.get("generate") or {}).get("command", [])[1:]
    if old_args != args:
        shutil.rmtree(ensure_inside(case_dir, cfg.output_root))
        return {"id": case["id"], "error": f"recorded invocation differs from the manifest: {old_args} vs {args}"}
    res = case_header(case, cfg)
    res.update({"started_at": now_iso(), "timers": {}, "_case_dir_for_probe": src, "_map_name": map_name})
    imported = {}
    for name in sorted(p.name for p in src.iterdir() if p.is_file() and p.name != "result.json"):
        imported[name] = sha256_file(src / name)
    for name in sorted(p.name for p in (src / "logs").glob("*") if p.is_file()):
        imported["logs/" + name] = sha256_file(src / "logs" / name)
    aas = src / "aas" / f"{stem}.aas"
    if aas.is_file():
        imported["aas/" + aas.name] = sha256_file(aas)
    identity = {}
    if sealed:
        gen = dict(old["generate"])
        gen["source"] = "imported (sealed case: not regenerated)"
        eval_dir = src
        res["determinism"] = {"status": NOT_RUN, "detail": "sealed case (D26): not regenerated"}
    else:
        stage = run_stage("generate", [str(cfg.qmg)] + args, case_dir, case_dir / "logs" / "generate.log")
        gen = {"command": stage["command"], "exit_code": stage["exit_code"], "timed_out": stage["timed_out"],
               "wall_seconds": stage["wall_seconds"], "stdout_tail": tail(stage["_stdout"], 1500),
               "stderr_tail": tail(stage["_stderr"], 1500)}
        for suffix in (".map", ".qmg.json"):
            new, prior = case_dir / (stem + suffix), src / (stem + suffix)
            identity[suffix] = {"regenerated_sha256": sha256_file(new) if new.is_file() else None,
                                "imported_sha256": sha256_file(prior) if prior.is_file() else None}
            identity[suffix]["identical"] = (identity[suffix]["regenerated_sha256"] is not None and
                                             identity[suffix]["regenerated_sha256"] == identity[suffix]["imported_sha256"])
        res["timers"]["generate"] = gen["wall_seconds"]
        eval_dir = case_dir
        same = all(v["identical"] for v in identity.values())
        res["determinism"] = {"status": "pass" if same and stage["exit_code"] == 0 else "fail",
                              "method": "import: fresh process vs the imported run's output",
                              "detail": "regenerated .map and .qmg.json byte-identical to the imported outputs"
                              if same else f"differs from the imported outputs: {identity}"}
        if not same:
            res["generate"] = gen
            res["import"] = {"from": rel(src), "identity": identity, "accepted": False}
            write_json(case_dir / "result.json.rejected", res)
            return {"id": case["id"], "error": "import identity failed (regenerated outputs differ)"}
    map_path = eval_dir / map_name
    gen["map_exists"] = map_path.is_file()
    gen["map_bytes"] = map_path.stat().st_size if map_path.is_file() else 0
    gen["report_exists"] = (eval_dir / f"{stem}.qmg.json").is_file()
    res["generate"] = gen
    parse_outputs(case, cfg, eval_dir, map_name, res)
    res["compile"] = reconstruct_compile(case["game"], src, stem)
    res["timers"].update(stage_timers(res["compile"]))
    finish_case(case, cfg, res, started)
    old_gates = {k: v.get("status") for k, v in (old.get("gates") or {}).items()}
    new_gates = {k: res["gates"][k]["status"] for k in old_gates if k in res["gates"]}
    gate_diffs = {k: [old_gates[k], new_gates.get(k)] for k in old_gates if old_gates[k] != new_gates.get(k)}
    old_requests = {(r["request"], i): r["status"] for i, r in enumerate(old.get("requests") or [])}
    request_diffs = [{"request": r["request"], "imported": old_requests.get((r["request"], i)), "now": r["status"]}
                     for i, r in enumerate(res["requests"]) if old_requests.get((r["request"], i)) != r["status"]]
    res["import"] = {"from": rel(src), "source_runner_version": old.get("runner_version"),
                     "source_result_sha256": sha256_file(src_result), "regenerated": not sealed,
                     "identity": identity, "accepted": True, "source_files_sha256": imported,
                     "check": {"gate_diffs": gate_diffs, "request_status_diffs": request_diffs,
                               "note": "differences against the imported result.json; under --policy draft both "
                                       "lists are expected to be empty"}}
    write_json(case_dir / "result.json", res)
    return {"id": case["id"], "skipped": False, "all_hard_gates": res["all_hard_gates"],
            "wall_seconds": res["wall_seconds"], "gate_diffs": len(gate_diffs), "request_diffs": len(request_diffs)}


# --------------------------------------------------------------------------- summary

def stats(values):
    if not values:
        return None
    ordered = sorted(values)
    return {"n": len(values), "min": ordered[0], "median": ordered[len(ordered) // 2], "max": ordered[-1],
            "total": round(sum(values), 3)}


def build_summary(manifest: dict, cfg) -> dict:
    cases = manifest["cases"]
    results = {}
    missing = []
    for case in cases:
        path = cfg.output / case["id"] / "result.json"
        if path.exists():
            results[case["id"]] = json.loads(path.read_text(encoding="utf-8"))
        else:
            missing.append(case["id"])
    runners = collections.Counter((r.get("identity") or {}).get("runner_sha256") or r.get("runner_version")
                                  for r in results.values())
    qmgs = collections.Counter((r.get("identity") or {}).get("qmg_sha256") for r in results.values())
    policies = collections.Counter(r.get("policy", "draft-v0") for r in results.values())
    problems = []
    if len(runners) > 1:
        problems.append(f"mixed runner hashes: {dict(runners)}")
    if len(qmgs) > 1:
        problems.append(f"mixed qmg sha256: {dict(qmgs)}")
    if len(policies) > 1:
        problems.append(f"mixed policies: {dict(policies)}")
    if problems:
        raise SummaryRefused("; ".join(problems))
    targets = []
    for case in cases:
        if case["target"] not in targets:
            targets.append(case["target"])
    per_target = []
    resolved_modes = []
    fingerprints = {}
    for target in targets:
        rows = [results[c["id"]] for c in cases if c["target"] == target and c["id"] in results]
        total = sum(1 for c in cases if c["target"] == target)
        count = lambda key: sum(1 for r in rows if r["gates"][key]["status"] == "pass")
        na = lambda key: sum(1 for r in rows if r["gates"][key]["status"] == "n/a")
        game = next(c["game"] for c in cases if c["target"] == target)
        mode = next(c["mode"] for c in cases if c["target"] == target)
        entry = {
            "target": target, "game": game, "mode": mode, "cases": total, "run": len(rows),
            "generated": count("G1_generated"), "intent_matched": count("G2_intent_match"),
            "compiled": count("G3_leak_free_compile"), "full_vis": count("G4_full_vis"),
            "final_light": count("G5_final_light"),
            "aas": count("G6_aas_valid") if game == "q3" else None,
            "mode_entities": count("G7_mode_entities"), "mode_entities_na": na("G7_mode_entities"),
            "all_hard_gates": sum(1 for r in rows if r["all_hard_gates"]),
            "resolved": dict(collections.Counter(f"{(r['report'] or {}).get('game')}/{(r['report'] or {}).get('mode')}"
                                                 for r in rows)),
        }
        per_target.append(entry)
        if TARGET_MODES.get(mode) is None:
            resolved_modes.append({"target": target, "resolved": entry["resolved"],
                                   "cases": {r["id"]: (r["report"] or {}).get("mode") for r in rows}})
        shas = collections.Counter((r["report"] or {}).get("layout_sha256") for r in rows if r["report"])
        sigs = collections.Counter(((r["report"] or {}).get("room_graph_signature") or {}).get("sha")
                                   for r in rows if r["report"])
        rooms = collections.Counter((r["report"]["counts"] or {}).get("rooms") for r in rows if r["report"])
        fingerprints[target] = {"runs_with_report": sum(1 for r in rows if r["report"]),
                                "distinct_layout_sha256": len(shas), "distinct_room_graph_signatures": len(sigs),
                                "room_counts": dict(sorted(rooms.items(), key=lambda kv: (kv[0] is None, kv[0])))}
    gate_counts = {k: dict(collections.Counter(r["gates"][k]["status"] for r in results.values() if k in r["gates"]))
                   for k in ALL_GATE_KEYS}
    failures = []
    for cid, r in results.items():
        failed = {k: v["detail"] for k, v in r["gates"].items() if v["status"] == "fail" and k in HARD_GATE_KEYS}
        if failed:
            failures.append({"id": cid, "target": r["target"], "category": r["category"], "failed_gates": failed})
    entities_by_game = collections.defaultdict(collections.Counter)
    textures_by_game = collections.defaultdict(collections.Counter)
    texture_resolution = collections.defaultdict(dict)
    findings = collections.defaultdict(collections.Counter)
    validation_codes = collections.Counter()
    request_status = collections.defaultdict(collections.Counter)
    request_status_by_target = collections.defaultdict(collections.Counter)
    stage_times = collections.defaultdict(list)
    themes = collections.defaultdict(collections.Counter)
    planners = collections.Counter()
    plan_fields = collections.defaultdict(collections.Counter)
    compile_warnings = collections.defaultdict(collections.Counter)
    gen_failures = []
    error_codes = collections.Counter()
    light_rows = []
    probes = collections.defaultdict(list)
    import_diffs = {"cases": 0, "gate_diffs": 0, "request_status_diffs": 0, "regenerated_identical": 0,
                    "not_regenerated_sealed": 0}
    for r in results.values():
        game = r["game"]
        if r.get("map"):
            entities_by_game[game].update(r["map"]["classnames"])
            textures_by_game[game].update(r["map"]["textures"])
            texture_resolution[game].update(r["map"].get("texture_resolution") or {})
        rep = r.get("report")
        if rep:
            for analyzer, codes in (rep.get("analysis_findings_by_code") or {}).items():
                if codes:
                    findings[analyzer].update(codes)
            for issue in rep.get("validation_issues") or []:
                validation_codes[f"{issue.get('severity')}:{issue.get('code')}"] += 1
            themes[r["target"]][rep.get("theme")] += 1
            planners[f"{rep['planner'].get('used_backend')}:{rep['planner'].get('status')}"] += 1
            for field in ("route_style", "symmetry", "lighting_style", "verticality", "intensity"):
                plan_fields[field][str(rep["semantic_plan"].get(field))] += 1
        if r["generate"].get("exit_code") != 0:
            gen_failures.append({"id": r["id"], "exit_code": r["generate"].get("exit_code"),
                                 "stderr_tail": (r["generate"].get("stderr_tail") or "")[-400:]})
        if r["generate"].get("wall_seconds") is not None:
            stage_times["generate"].append(r["generate"]["wall_seconds"])
        for stage in (r.get("compile") or {}).get("stages", []):
            if stage.get("wall_seconds") is not None:
                stage_times[stage["name"]].append(stage["wall_seconds"])
            if stage["name"] in ("light", "q3map2-light") and stage.get("wall_seconds") is not None:
                light_rows.append((stage["wall_seconds"], r["id"], stage.get("timed_out")))
            for item in stage.get("map_warnings_distinct") or []:
                compile_warnings[f"{game}:{stage['name']}"][item["line"][:160]] += item["count"]
        for req in r.get("requests") or []:
            request_status[r["category"]][req["status"]] += 1
            request_status_by_target[r["target"]][req["status"]] += 1
        error_codes.update(((r.get("gates") or {}).get("G11_error_findings") or {}).get("by_code") or {})
        probe = r.get("lighting_probe") or {}
        if probe.get("available") and probe.get("floor"):
            probes[game].append(probe["floor"])
        if r.get("import"):
            import_diffs["cases"] += 1
            check = r["import"].get("check") or {}
            import_diffs["gate_diffs"] += len(check.get("gate_diffs") or {})
            import_diffs["request_status_diffs"] += len(check.get("request_status_diffs") or [])
            if r["import"].get("regenerated"):
                import_diffs["regenerated_identical"] += 1
            else:
                import_diffs["not_regenerated_sealed"] += 1
    by_path = collections.OrderedDict()
    for case in cases:
        path = case.get("path") or case["target"]
        row = by_path.setdefault(path, {"path": path, "claimed": case.get("claimed"), "module": case.get("module"),
                                        "cases": 0, "run": 0, "gates": collections.defaultdict(collections.Counter),
                                        "all_hard_gates": 0, "all_hard_gates_with_G10": 0, "silent_drops": 0,
                                        "sealed": 0})
        row["cases"] += 1
        if is_sealed(case, cfg.seal):
            row["sealed"] += 1
        r = results.get(case["id"])
        if not r:
            continue
        row["run"] += 1
        for key in ALL_GATE_KEYS:
            if key in r["gates"]:
                row["gates"][key][r["gates"][key]["status"]] += 1
        row["all_hard_gates"] += 1 if r.get("all_hard_gates") else 0
        row["all_hard_gates_with_G10"] += 1 if r.get("all_hard_gates_with_G10") else 0
        row["silent_drops"] += (r["gates"].get("G9_request_accounting") or {}).get("silent_drops") or 0
    per_path = []
    for row in by_path.values():
        row["gates"] = {k: dict(v) for k, v in row["gates"].items()}
        per_path.append(row)
    light_rows.sort(reverse=True)
    over_budget = [i for w, i, _ in light_rows if w > cfg.light_budget]

    def probe_medians(rows):
        out = {}
        for key in ("mean", "lt16", "lt32", "ge250"):
            values = sorted(x[key] for x in rows if x.get(key) is not None)
            out["median_" + key] = values[len(values) // 2] if values else None
        out["maps"] = len(rows)
        return out
    identity = next(iter(results.values())).get("identity") if results else cfg.identity
    sealed_ids = [c["id"] for c in cases if is_sealed(c, cfg.seal)]
    return {
        "schema": "qmg.benchmark.summary", "schema_version": 2, "runner_version": RUNNER_VERSION,
        "policy": cfg.policy, "generated_at": now_iso(), "manifest": rel(cfg.manifest),
        "manifest_sha256": sha256_file(cfg.manifest), "identity": identity,
        "qmg": {"path": (identity or {}).get("qmg_path"), "sha256": (identity or {}).get("qmg_sha256")},
        "tools": dict(((identity or {}).get("tool_sha256") or {}), qmg=(identity or {}).get("qmg_sha256")),
        "cases_total": len(cases), "cases_run": len(results), "missing": missing, "sealed_cases": sealed_ids,
        "per_target": per_target, "per_path": per_path, "gate_counts": gate_counts,
        "hard_gates": HARD_GATE_KEYS, "reported_gates": REPORTED_GATE_KEYS,
        "all_hard_gates": sum(1 for r in results.values() if r["all_hard_gates"]),
        "all_hard_gates_with_G10_reported": sum(1 for r in results.values() if r.get("all_hard_gates_with_G10")),
        "waivers": {"count": 0, "note": "no waivers are recorded (tools/benchmark/rc-ledger.json)"},
        "failures": failures, "generation_failures": gen_failures,
        "resolved_modes_for_non_representable_targets": resolved_modes,
        "themes_by_target": {k: dict(v) for k, v in themes.items()},
        "planner_backends": dict(planners), "semantic_plan_fields": {k: dict(v) for k, v in plan_fields.items()},
        "entity_histogram_by_game": {g: dict(sorted(c.items())) for g, c in entities_by_game.items()},
        "texture_histogram_by_game": {g: dict(c.most_common()) for g, c in textures_by_game.items()},
        "texture_resolution_by_game": {g: dict(sorted(v.items())) for g, v in texture_resolution.items()},
        "analysis_findings_by_code": {k: dict(v.most_common()) for k, v in sorted(findings.items())},
        "error_findings_by_code": dict(error_codes.most_common()),
        "validation_issue_codes": dict(validation_codes.most_common()),
        "request_status_by_category": {k: dict(v) for k, v in request_status.items()},
        "request_status_by_target": {k: dict(v) for k, v in request_status_by_target.items()},
        "fingerprints_by_target": fingerprints,
        "stage_wall_seconds": {k: stats(v) for k, v in stage_times.items()},
        "light_budget": {"seconds": cfg.light_budget, "over_budget": over_budget,
                         "slowest_10": [{"id": i, "wall_seconds": w, "timed_out": t} for w, i, t in light_rows[:10]],
                         "concurrency": (identity or {}).get("concurrency")},
        "lighting_probe_floor_by_game": {g: probe_medians(v) for g, v in probes.items()},
        "import_check": import_diffs if import_diffs["cases"] else None,
        "compile_map_warnings": {k: dict(v.most_common(8)) for k, v in compile_warnings.items()},
    }


class SummaryRefused(Exception):
    pass


def write_summary_md(summary: dict, path: Path):
    lines = []
    w = lines.append
    ident = summary.get("identity") or {}
    snap = ident.get("source_snapshot") or {}
    w(f"# Quake-MapGen benchmark summary ({summary['policy']} policy)")
    w("")
    w(f"Generated {summary['generated_at']} by tools/benchmark/run.py `{summary['runner_version'][:16]}`; "
      f"manifest `{summary['manifest']}` sha256 `{summary['manifest_sha256'][:16]}`; "
      f"qmg sha256 `{(summary['qmg']['sha256'] or '')[:16]}`; source HEAD `{(snap.get('git_head') or '')[:12]}` "
      f"diff `{(snap.get('git_diff_sha256') or '')[:12]}` untracked `{(snap.get('untracked_list_sha256') or '')[:12]}`.")
    w(f"Cases run: **{summary['cases_run']} / {summary['cases_total']}**; all hard gates (G1-G7) passed: "
      f"**{summary['all_hard_gates']}**; with G10 reported: **{summary['all_hard_gates_with_G10_reported']}**. "
      f"Waivers: {summary['waivers']['count']}.")
    if summary["sealed_cases"]:
        w(f"Sealed (D26): {', '.join(summary['sealed_cases'])}.")
    if summary["missing"]:
        shown = summary["missing"][:20]
        more = len(summary["missing"]) - len(shown)
        w(f"Missing results ({len(summary['missing'])}): {', '.join(shown)}" + (f" ... (+{more})" if more else ""))
    w("")
    w("## Per-target hard gates (draft-v0 layout)")
    w("")
    w("| target | cases | G1 gen | G2 intent | G3 compile | G4 full VIS | G5 final light | G6 AAS | G7 mode ents | all gates | resolved game/mode |")
    w("|---|---|---|---|---|---|---|---|---|---|---|")
    for t in summary["per_target"]:
        g7 = f"{t['mode_entities']}" + (f" (+{t['mode_entities_na']} n/a)" if t["mode_entities_na"] else "")
        aas = "n/a" if t["aas"] is None else t["aas"]
        resolved = ", ".join(f"{k} x{v}" for k, v in sorted(t["resolved"].items()))
        w(f"| {t['target']} | {t['run']}/{t['cases']} | {t['generated']} | {t['intent_matched']} | {t['compiled']} | "
          f"{t['full_vis']} | {t['final_light']} | {aas} | {g7} | {t['all_hard_gates']} | {resolved} |")
    w("")
    w("## Per-path reported gates (G8-G15 are reported columns, not hard)")
    w("")
    w("| path | claimed | run | all hard | +G10 | G8 pass | G9 pass (silent drops) | G10 pass/fail | G11 pass | G12 pass | sealed |")
    w("|---|---|---|---|---|---|---|---|---|---|---|")
    for p in summary["per_path"]:
        g = p["gates"]
        c = lambda k, s="pass": (g.get(k) or {}).get(s, 0)
        w(f"| {p['path']} | {p['claimed']} | {p['run']}/{p['cases']} | {p['all_hard_gates']} | "
          f"{p['all_hard_gates_with_G10']} | {c('G8_determinism')} | {c('G9_request_accounting')} ({p['silent_drops']}) | "
          f"{c('G10_conflict_surfaced')}/{c('G10_conflict_surfaced', 'fail')} | {c('G11_error_findings')} | "
          f"{c('G12_materials')} | {p['sealed']} |")
    w("")
    w("## Gate totals")
    w("")
    statuses = ["pass", "fail", "n/a", NOT_RUN, NOT_CONFIGURED]
    w("| gate | " + " | ".join(statuses) + " |")
    w("|---|" + "---|" * len(statuses))
    for key, counts in summary["gate_counts"].items():
        w(f"| {key} | " + " | ".join(str(counts.get(s, 0)) for s in statuses) + " |")
    w("")
    if summary.get("import_check"):
        ic = summary["import_check"]
        w("## Import check")
        w("")
        w(f"{ic['cases']} imported case(s); regenerated byte-identical {ic['regenerated_identical']}; sealed and not "
          f"regenerated {ic['not_regenerated_sealed']}; gate differences against the imported results "
          f"{ic['gate_diffs']}; request-status differences {ic['request_status_diffs']}.")
        w("")
    w("## Expected-request status")
    w("")
    rstat = ["honoured", "partial", "not_honoured", "not_representable", "unverified"]
    w("| category | " + " | ".join(rstat) + " |")
    w("|---|" + "---|" * len(rstat))
    for cat, counts in summary["request_status_by_category"].items():
        w(f"| {cat} | " + " | ".join(str(counts.get(s, 0)) for s in rstat) + " |")
    w("")
    if summary["error_findings_by_code"]:
        w("## G11 error-severity findings by code")
        w("")
        w(", ".join(f"{k} {v}" for k, v in summary["error_findings_by_code"].items()))
        w("")
    lb = summary["light_budget"]
    w("## Light budget")
    w("")
    w(f"Budget {lb['seconds']} s; over budget: {len(lb['over_budget'])} ({', '.join(lb['over_budget'][:10])}); "
      f"concurrency {lb['concurrency']}.")
    w("")
    w("| slowest light | wall s | timed out |")
    w("|---|---|---|")
    for row in lb["slowest_10"]:
        w(f"| {row['id']} | {row['wall_seconds']} | {row['timed_out']} |")
    w("")
    if summary["lighting_probe_floor_by_game"]:
        w("## Compiled floor luxels (G13 is not defined until Phase 3; recorded only)")
        w("")
        for game, row in summary["lighting_probe_floor_by_game"].items():
            w(f"- **{game}**: {row}")
        w("")
    w("## Stage wall times (seconds)")
    w("")
    w("| stage | n | min | median | max | total |")
    w("|---|---|---|---|---|---|")
    for stage, s in summary["stage_wall_seconds"].items():
        if s:
            w(f"| {stage} | {s['n']} | {s['min']} | {s['median']} | {s['max']} | {s['total']} |")
    w("")
    w("## Tools")
    w("")
    for name, digest in summary["tools"].items():
        pin = TOOL_PINS.get(name)
        note = "" if pin is None else (" (pinned)" if digest == pin else " (PIN MISMATCH)")
        w(f"- {name}: `{digest}`{note}")
    w("")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


# --------------------------------------------------------------------------- controls

def _box_brush(x0, y0, z0, x1, y1, z1, face_suffix: str) -> str:
    planes = [
        f"( {x0} {y0} {z0} ) ( {x0} {y1} {z0} ) ( {x0} {y0} {z1} )",
        f"( {x1} {y0} {z0} ) ( {x1} {y0} {z1} ) ( {x1} {y1} {z0} )",
        f"( {x0} {y0} {z0} ) ( {x0} {y0} {z1} ) ( {x1} {y0} {z0} )",
        f"( {x0} {y1} {z0} ) ( {x1} {y1} {z0} ) ( {x0} {y1} {z1} )",
        f"( {x0} {y0} {z0} ) ( {x1} {y0} {z0} ) ( {x0} {y1} {z0} )",
        f"( {x0} {y0} {z1} ) ( {x0} {y1} {z1} ) ( {x1} {y0} {z1} )",
    ]
    return "{\n" + "\n".join(p + " " + face_suffix for p in planes) + "\n}\n"


def control_map(game: str, texture: str, leaky: bool = False, dm_spawns: int = 2) -> str:
    """A project-authored three-room box (1024 x 512 x 256 interior, two dividing walls with doorways)."""
    suffix = {"q1": f"{texture} 0 0 0 1 1", "q2": f"{texture} 0 0 0 1 1 0 0 0",
              "q3": f"{texture} 0 0 0 0.5 0.5 0 0 0"}[game]
    b = ""
    b += _box_brush(-16, -16, -16, 1040, 528, 0, suffix)
    b += _box_brush(-16, -16, 256, 1040, 528, 272, suffix)
    b += _box_brush(-16, -16, 0, 0, 528, 256, suffix)
    b += _box_brush(1024, -16, 0, 1040, 528, 256, suffix)
    b += _box_brush(0, -16, 0, 1024, 0, 256, suffix)
    b += _box_brush(0, 512, 0, 500 if leaky else 1024, 528, 256, suffix)
    for x in (336, 672):
        b += _box_brush(x, 0, 0, x + 16, 192, 256, suffix)
        b += _box_brush(x, 320, 0, x + 16, 512, 256, suffix)
        b += _box_brush(x, 192, 128, x + 16, 320, 256, suffix)
    out = '{\n"classname" "worldspawn"\n' + b + "}\n"
    start = "info_player_deathmatch" if game == "q3" else "info_player_start"
    out += '{\n"classname" "%s"\n"origin" "64 64 24"\n}\n' % start
    for i in range(dm_spawns if game != "q3" else max(dm_spawns - 1, 0)):
        out += '{\n"classname" "info_player_deathmatch"\n"origin" "%d 448 24"\n}\n' % (80 + 860 * i)
    for x in (160, 500, 850):
        out += '{\n"classname" "light"\n"origin" "%d 256 200"\n"light" "300"\n}\n' % x
    return out


def control_wad(path: Path, names: list) -> None:
    """A project-authored WAD2 with 16x16 miptex lumps (palette index 15), for the G12 Q1 positive control."""
    lumps = []
    for name in names:
        w = h = 16
        header = name.encode("latin-1")[:15].ljust(16, b"\0") + struct.pack("<II", w, h)
        offsets = []
        pos = 40
        pixels = b""
        for level in range(4):
            size = (w >> level) * (h >> level)
            offsets.append(pos)
            pixels += bytes([15]) * size
            pos += size
        lumps.append((name, header + struct.pack("<4I", *offsets) + pixels))
    body = b""
    directory = b""
    pos = 12
    for name, data in lumps:
        directory += struct.pack("<iiibbh", pos, len(data), len(data), 0x44, 0, 0) + \
            name.encode("latin-1")[:15].ljust(16, b"\0")
        body += data
        pos += len(data)
    path.write_bytes(b"WAD2" + struct.pack("<ii", len(lumps), 12 + len(body)) + body + directory)


def run_controls(cfg) -> int:
    root = cfg.controls_out
    if root.exists():
        shutil.rmtree(ensure_inside(root, cfg.controls_root))
    root.mkdir(parents=True)
    rows = []

    def add(gate_key, control, kind, verdict, level, fixture, extra=None):
        expected = "accept" if kind == "positive" else "reject"
        actual = "accept" if verdict["status"] == "pass" else "reject"
        row = {"gate": gate_key, "control": control, "kind": kind, "expected": expected,
               "status": verdict["status"], "ok": expected == actual, "evidence_level": level,
               "fixture": rel(fixture) if fixture else None, "detail": verdict.get("detail")}
        if extra:
            row.update(extra)
        rows.append(row)
        print(f"[{'ok' if row['ok'] else 'FAIL'}] {gate_key} {control} ({kind}) -> {verdict['status']}", flush=True)

    def compile_fixture(name, game, text, vis_extra=()):
        case_dir = root / name
        case_dir.mkdir(parents=True)
        (case_dir / f"{name}.map").write_text(text, encoding="utf-8", newline="\n")
        comp = compile_case({"game": game}, case_dir, f"{name}.map", cfg, vis_extra=vis_extra,
                            light_timeout=cfg.light_timeout)
        return case_dir, comp

    # --- compiler-backed controls: G3, G4, G5, G6, G12 ---
    compiled = {}
    textures = {"q1": "WALL", "q2": "e1u1/flat1_1", "q3": "base_wall/comp3text"}
    for game in ("q1", "q2", "q3"):
        d, comp = compile_fixture(f"{game}-sealed", game, control_map(game, textures[game]))
        compiled[game] = (d, comp)
        add("G3_leak_free_compile", f"{game}-sealed-box", "positive", evaluate_gates(
            {"game": game, "mode": "dm"}, {"generate": {}, "intent": {"intent_match": True, "reason": ""},
                                          "compile": comp, "map": {}})["G3_leak_free_compile"],
            "compiler output", d)
        d2, comp2 = compile_fixture(f"{game}-leaky", game, control_map(game, textures[game], leaky=True))
        add("G3_leak_free_compile", f"{game}-leaky-box", "negative", evaluate_gates(
            {"game": game, "mode": "dm"}, {"generate": {}, "intent": {"intent_match": True, "reason": ""},
                                          "compile": comp2, "map": {}})["G3_leak_free_compile"],
            "compiler output", d2)
        add("G4_full_vis", f"{game}-full-vis", "positive", gate_g4_v1(game, comp), "compiler output", d)
        d3, comp3 = compile_fixture(f"{game}-vis-fast", game, control_map(game, textures[game]), vis_extra=("-fast",))
        draft_verdict = evaluate_gates({"game": game, "mode": "dm"},
                                       {"generate": {}, "intent": {"intent_match": True, "reason": ""},
                                        "compile": comp3, "map": {}})["G4_full_vis"]["status"]
        add("G4_full_vis", f"{game}-vis-fast-bsp", "negative", gate_g4_v1(game, comp3), "compiler output", d3,
            {"draft_gate_status": draft_verdict})
        add("G5_final_light", f"{game}-final-light", "positive", gate_g5_v1(game, comp, cfg.light_budget),
            "compiler output", d)
        add("G5_final_light", f"{game}-light-over-budget", "negative", gate_g5_v1(game, comp, 0.001),
            "compiler output", d, {"budget_seconds": 0.001})
    # G5 negative: the same real compile with its light log line changed to "10 empty lightmaps".
    for game in ("q1", "q2"):
        src, _ = compiled[game]
        neg = root / f"{game}-10-empty-lightmaps"
        shutil.copytree(src, neg)
        log = neg / "logs" / "light.log"
        text = log.read_text(encoding="utf-8", errors="replace")
        if not re.search(r"(?m)^0 empty lightmaps\s*$", text):
            raise RuntimeError(f"{game} control light log has no '0 empty lightmaps' line to alter")
        log.write_text(re.sub(r"(?m)^0 empty lightmaps(\s*)$", r"10 empty lightmaps\1", text), encoding="utf-8",
                       newline="\n")
        comp = reconstruct_compile(game, neg, f"{game}-sealed")
        substring_match = "0 empty lightmaps" in log.read_text(encoding="utf-8")
        add("G5_final_light", f"{game}-10-empty-lightmaps-log", "negative", gate_g5_v1(game, comp, cfg.light_budget),
            "compiler output (log fixture)", neg, {"substring_check_would_accept": substring_match})
        comp_pos = reconstruct_compile(game, src, f"{game}-sealed")
        add("G5_final_light", f"{game}-0-empty-lightmaps-log-reread", "positive",
            gate_g5_v1(game, comp_pos, cfg.light_budget), "compiler output (log reread)", src)
    # G6: valid AAS (positive) and two corrupt AAS files (negatives).
    q3dir, q3comp = compiled["q3"]
    add("G6_aas_valid", "q3-valid-aas", "positive", gate_g6("q3", q3comp), "compiler output", q3dir)
    aas = q3dir / "aas" / "q3-sealed.aas"
    if aas.is_file():
        data = aas.read_bytes()
        for label, corrupt in (("truncated", data[:len(data) // 2]),
                               ("bad-version", data[:4] + struct.pack("<i", 4) + data[8:])):
            cdir = root / f"q3-corrupt-aas-{label}"
            (cdir / "aas").mkdir(parents=True)
            (cdir / "logs").mkdir()
            cpath = cdir / "aas" / "q3-sealed.aas"
            cpath.write_bytes(corrupt)
            comp = copy.deepcopy(q3comp)
            comp["stages"] = [s for s in comp["stages"] if s["name"] != "bspc-aasinfo"]
            comp["aas"] = inspect_aas(cpath)
            info = run_aasinfo(cpath, cdir / "aaswork", cdir / "logs", cfg, q3_env(cfg))
            text = info.pop("_stdout") + "\n" + info.pop("_stderr")
            info.update(scan_output(text, "q3", "bspc-aasinfo"))
            comp["stages"].append(info)
            comp["aasinfo"] = parse_aasinfo(text)
            comp["aasinfo_ok"] = info["exit_code"] == 0 and not info["timed_out"] and not info["tool_failure_markers"]
            add("G6_aas_valid", f"q3-corrupt-aas-{label}", "negative", gate_g6("q3", comp), "compiler output", cdir)
    else:
        rows.append({"gate": "G6_aas_valid", "control": "q3-valid-aas", "ok": False,
                     "detail": "BSPC produced no AAS for the control map"})
    # G12: Q2 real texture vs a missing texture compiled without -notex; Q3 and Q1 static checks.
    q2dir, q2comp = compiled["q2"]
    q2map = parse_map(q2dir / "q2-sealed.map")
    q2map["texture_resolution"] = {t: cfg.assets.resolve("q2", t) for t in q2map["textures"]}
    add("G12_materials", "q2-stock-wal-no-notex", "positive",
        gate_g12({"game": "q2"}, {"map": q2map, "compile": q2comp}, cfg), "compiler output", q2dir)
    mdir, mcomp = compile_fixture("q2-missing-texture", "q2", control_map("q2", "qmgctl/no_such_texture"))
    mmap = parse_map(mdir / "q2-missing-texture.map")
    mmap["texture_resolution"] = {t: cfg.assets.resolve("q2", t) for t in mmap["textures"]}
    add("G12_materials", "q2-missing-texture-no-notex", "negative",
        gate_g12({"game": "q2"}, {"map": mmap, "compile": mcomp}, cfg), "compiler output", mdir,
        {"qbsp_missing_texture_lines": next(s for s in mcomp["stages"] if s["name"] == "qbsp")["facts"]
         ["missing_texture_lines"]})
    q3map = parse_map(q3dir / "q3-sealed.map")
    add("G12_materials", "q3-stock-shader", "positive", gate_g12({"game": "q3"}, {"map": q3map, "compile": q3comp},
                                                                 cfg), "static analysis", q3dir)
    q3bad = root / "q3-missing-shader.map"
    q3bad.write_text(control_map("q3", "qmgctl/no_such_shader"), encoding="utf-8", newline="\n")
    add("G12_materials", "q3-missing-shader", "negative",
        gate_g12({"game": "q3"}, {"map": parse_map(q3bad)}, cfg), "static analysis", q3bad)
    q1dir, _ = compiled["q1"]
    q1map = parse_map(q1dir / "q1-sealed.map")
    saved = (cfg.q1_wad, cfg.q1_wad_names, cfg.q1_wad_label)
    cfg.q1_wad, cfg.q1_wad_names, cfg.q1_wad_label = None, set(), None
    add("G12_materials", "q1-no-wad", "negative", gate_g12({"game": "q1"}, {"map": q1map}, cfg), "static analysis",
        q1dir)
    wad = root / "qmg-control.wad"
    control_wad(wad, ["WALL"])
    cfg.q1_wad, cfg.q1_wad_names, cfg.q1_wad_label = wad, read_wad2_names(wad), rel(wad)
    add("G12_materials", "q1-project-wad", "positive", gate_g12({"game": "q1"}, {"map": q1map}, cfg),
        "static analysis", wad)
    cfg.q1_wad, cfg.q1_wad_names, cfg.q1_wad_label = saved

    # --- generator-backed and static controls: G1, G2, G7, G8 ---
    gdir = root / "generate"
    gdir.mkdir()
    prompt = "Quake 2 deathmatch control map with 4 rooms"
    first = run_stage("generate", [str(cfg.qmg), "generate", "--prompt", prompt, "--seed", "7", "--output",
                                   "control.map"], gdir, gdir / "logs" / "generate.log")
    gen_ok = {"exit_code": first["exit_code"], "map_bytes": (gdir / "control.map").stat().st_size
              if (gdir / "control.map").is_file() else 0}
    raw = json.loads((gdir / "control.qmg.json").read_text(encoding="utf-8")) \
        if (gdir / "control.qmg.json").is_file() else None
    summary = summarize_report(raw) if raw else None
    g1 = evaluate_gates({"game": "q2", "mode": "dm"}, {"generate": gen_ok, "report": summary,
                                                       "intent": {"intent_match": True, "reason": ""}})["G1_generated"]
    add("G1_generated", "qmg-control-prompt", "positive", g1, "static analysis", gdir / "control.map")
    bad = run_stage("generate", [str(cfg.qmg), "generate", "--prompt", prompt, "--rooms", "99", "--output",
                                 "bad.map"], gdir, gdir / "logs" / "generate-bad.log")
    gen_bad = {"exit_code": bad["exit_code"], "map_bytes": (gdir / "bad.map").stat().st_size
               if (gdir / "bad.map").is_file() else 0}
    g1n = evaluate_gates({"game": "q2", "mode": "dm"}, {"generate": gen_bad, "report": None,
                                                        "intent": {"intent_match": False, "reason": ""}})["G1_generated"]
    add("G1_generated", "qmg-invalid-arguments", "negative", g1n, "static analysis", gdir / "logs" / "generate-bad.log")
    extras = report_extras(raw) if raw else {}
    pos_case = {"game": "q2", "mode": "dm", "module": "q2-baseq2-3.19"}
    i1 = intent_v1(pos_case, summary, extras)
    add("G2_intent_match", "q2-dm-report-for-q2-dm", "positive", gate("pass" if i1["intent_match"] else "fail",
                                                                      i1["reason"]), "static analysis", gdir)
    i2 = intent_v1({"game": "q2", "mode": "duel", "module": "q2-baseq2-3.19"}, summary, extras)
    add("G2_intent_match", "q2-dm-report-for-q2-duel", "negative", gate("pass" if i2["intent_match"] else "fail",
                                                                        i2["reason"]), "static analysis", gdir)
    cmap = parse_map(gdir / "control.map")
    add("G7_mode_entities", "q2-dm-control-map", "positive",
        gate_g7("q2", "dm", cmap, cmap["classnames"], cmap["_entities"]), "static analysis", gdir / "control.map")
    one = root / "one-dm-spawn.map"
    one.write_text(control_map("q2", "e1u1/flat1_1", dm_spawns=1), encoding="utf-8", newline="\n")
    omap = parse_map(one)
    add("G7_mode_entities", "q2-one-dm-spawn", "negative",
        gate_g7("q2", "dm", omap, omap["classnames"], omap["_entities"]), "static analysis", one)
    second = gdir / "second"
    second.mkdir()
    run_stage("generate", [str(cfg.qmg), "generate", "--prompt", prompt, "--seed", "7", "--output", "control.map"],
              second, gdir / "logs" / "generate-second.log")

    def same_files(a: Path, b: Path) -> dict:
        names = ("control.map", "control.qmg.json")
        equal = all((a / n).is_file() and (b / n).is_file() and (a / n).read_bytes() == (b / n).read_bytes()
                    for n in names)
        return gate("pass" if equal else "fail", "byte-identical" if equal else "bytes differ")
    add("G8_determinism", "two-processes", "positive", same_files(gdir, second), "static analysis", second)
    mutated = gdir / "mutated"
    mutated.mkdir()
    for n in ("control.map", "control.qmg.json"):
        shutil.copy2(gdir / n, mutated / n)
    data = bytearray((mutated / "control.map").read_bytes())
    data[-2] = data[-2] ^ 0x01
    (mutated / "control.map").write_bytes(bytes(data))
    add("G8_determinism", "one-byte-mutation", "negative", same_files(gdir, mutated), "static analysis", mutated)

    # --- report-fixture controls: G9, G10, G11 ---
    fixtures = root / "report-fixtures"
    fixtures.mkdir()
    base_summary = summary or {"game": "quake2", "mode": "deathmatch"}
    ctx = {"game_code": "q2", "report": base_summary, "map": {"classnames": {"info_player_deathmatch": 4},
                                                              "_entities": [], "textures": {}},
           "generate": {"exit_code": 0}, "gates": {}, "report_extras": {}, "surfaceparms": lambda: {}}
    requests = [evaluate_request_v1(r, ctx) for r in ("game quake2", "railgun", "mode team_deathmatch")]
    covered = {"schema_version": 5, "findings": [
        {"code": "request.unsupported_item", "severity": "warning", "request": "railgun"},
        {"code": "prompt.unsupported_mode", "severity": "warning", "request": "mode team_deathmatch"}]}
    uncovered = {"schema_version": 5, "findings": [
        {"code": "request.unsupported_item", "severity": "warning", "request": "railgun"}]}
    for name, fixture, kind in (("covered", covered, "positive"), ("unreported-unsupported-request", uncovered,
                                                                    "negative")):
        path = fixtures / f"g9-{name}.json"
        path.write_text(json.dumps(fixture, indent=2) + "\n", encoding="utf-8")
        add("G9_request_accounting", name, kind, gate_g9(requests, report_extras(fixture)), "static analysis", path,
            {"request_statuses": {r["request"]: r["status"] for r in requests}})
    prompt_c = "a flat Quake 2 deathmatch map with extreme verticality"
    spans_c = [{"start": prompt_c.find(t), "end": prompt_c.find(t) + len(t), "text": t}
               for t in ("flat", "extreme verticality")]
    case_c = {"expected_conflicts": [{"class": "verticality_contradiction", "span": spans_c}],
              "expected_resolution": {"primary": {"game": "quake2", "mode": "deathmatch"}}}
    found_spans = [{"start": s["start"], "end": s["end"]} for s in spans_c]
    surfaced = {"findings": [{"code": "prompt.conflict", "class": "verticality_contradiction", "severity": "warning",
                              "spans": found_spans, "resolution": "first_term"}]}
    wrong = {"findings": [{"code": "prompt.conflict", "class": "capacity_contradiction", "severity": "warning",
                           "spans": found_spans}]}
    for name, fixture, kind in (("surfaced", surfaced, "positive"), ("silent", {}, "negative"),
                                ("wrong-class", wrong, "negative")):
        path = fixtures / f"g10-{name}.json"
        path.write_text(json.dumps(fixture, indent=2) + "\n", encoding="utf-8")
        add("G10_conflict_surfaced", name, kind, gate_g10(case_c, {"game": "quake2", "mode": "deathmatch"},
                                                          report_extras(fixture)), "static analysis", path)
    clean = {"analysis": {"quality": {"available": True, "findings": [{"code": "low_cover", "severity": "warning"}]}}}
    dirty = {"analysis": {"navigation": {"player": {"available": True, "findings": [
        {"code": "unreachable_placement", "severity": "error"}]}}}}
    for name, fixture, kind in (("warnings-only", clean, "positive"), ("error-finding", dirty, "negative")):
        path = fixtures / f"g11-{name}.json"
        path.write_text(json.dumps(fixture, indent=2) + "\n", encoding="utf-8")
        add("G11_error_findings", name, kind, gate_g11(report_extras(fixture), {"game": "quake2"}), "static analysis",
            path)
    not_covered = [{"gate": "G13_lighting_readability", "reason": "not defined until Phase 3 (no stock band yet)"},
                   {"gate": "G14_engine_smoke", "reason": "not configured (no engine harness; D16-D18 pending)"},
                   {"gate": "G15_q3_entity_reachability", "reason": "not configured (q3host not built, H2)"}]
    gates_covered = sorted({r["gate"] for r in rows})
    per_gate = {}
    for key in gates_covered:
        mine = [r for r in rows if r["gate"] == key]
        per_gate[key] = {"positives_accepted": sum(1 for r in mine if r.get("kind") == "positive" and r["ok"]),
                         "negatives_rejected": sum(1 for r in mine if r.get("kind") == "negative" and r["ok"]),
                         "controls": len(mine), "all_ok": all(r["ok"] for r in mine)}
    complete = all(v["positives_accepted"] >= 1 and v["negatives_rejected"] >= 1 and v["all_ok"]
                   for v in per_gate.values())
    out = {"schema": "qmg.benchmark.controls", "schema_version": 1, "runner_sha256": RUNNER_SHA256,
           "generated_at": now_iso(), "qmg_sha256": sha256_file(cfg.qmg) if cfg.qmg.is_file() else None,
           "tool_sha256": {k: v for k, v in tool_hashes(cfg).items() if k != "qmg"},
           "light_budget_seconds": cfg.light_budget, "all_ok": complete and all(r["ok"] for r in rows),
           "per_gate": per_gate, "no_controls": not_covered, "controls": rows,
           "note": "Fixtures are project-authored (boxes, a 16x16 WAD, report JSON). No reference geometry. "
                   "No game client or engine is run."}
    write_json(root / "controls.json", out)
    md = ["# Gate controls", "", f"runner `{RUNNER_SHA256[:16]}`; all ok: **{out['all_ok']}**", "",
          "| gate | control | kind | status | ok | evidence level |", "|---|---|---|---|---|---|"]
    for r in rows:
        md.append(f"| {r['gate']} | {r['control']} | {r.get('kind')} | {r.get('status')} | {r['ok']} | "
                  f"{r.get('evidence_level')} |")
    md += ["", "No controls (by definition): " + "; ".join(f"{n['gate']} ({n['reason']})" for n in not_covered), ""]
    (root / "controls.md").write_text("\n".join(md) + "\n", encoding="utf-8")
    print(f"controls: {sum(1 for r in rows if r['ok'])}/{len(rows)} ok; gates covered {len(per_gate)}; "
          f"all_ok={out['all_ok']} -> {rel(root / 'controls.json')}", flush=True)
    return 0 if out["all_ok"] else 1


# --------------------------------------------------------------------------- main

def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--manifest", "--cases-file", dest="manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--out", "--output", dest="output", type=Path, default=None,
                        help=f"run directory (default {rel(DEFAULT_OUTPUT)}; {rel(DEFAULT_CONTROLS)} for --controls)")
    parser.add_argument("--policy", choices=["v1", "draft"], default="v1")
    parser.add_argument("--filter", action="append", default=[], help="run cases whose id contains this substring "
                        "(repeatable; any match)")
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--force", action="store_true", help="re-run cases that already have result.json")
    parser.add_argument("--summary-only", action="store_true")
    parser.add_argument("--generate-only", action="store_true", help="generate and evaluate without compiling")
    parser.add_argument("--no-compile", action="store_true", help="alias of --generate-only")
    parser.add_argument("--repeat", type=int, default=1, choices=[1, 2], help="2: G8 determinism check")
    parser.add_argument("--qmg-extra", default="", help="extra qmg arguments, e.g. \"--switch r1s.x=on\"")
    parser.add_argument("--import", dest="import_dir", type=Path, default=None,
                        help="re-evaluate this earlier run's compiled outputs (no recompilation)")
    parser.add_argument("--controls", action="store_true", help="build and evaluate the gate controls")
    parser.add_argument("--light-budget", type=float, default=DEFAULT_LIGHT_BUDGET)
    parser.add_argument("--light-timeout", type=float, default=None,
                        help="light stage timeout under --policy v1 (default 2 x light budget)")
    parser.add_argument("--hero-run", action="store_true", help="allow sealed cases (D26 hero run only)")
    parser.add_argument("--q1-wad", type=Path, default=None, help="Q1 texture WAD for G12 (D9)")
    parser.add_argument("--allow-unpinned", action="store_true")
    parser.add_argument("--allow-modified-manifest", action="store_true")
    parser.add_argument("--qmg", type=Path, default=DEFAULT_QMG)
    parser.add_argument("--ericw", type=Path, default=DEFAULT_ERICW)
    parser.add_argument("--q3tools", type=Path, default=DEFAULT_Q3TOOLS)
    parser.add_argument("--q3-runtime", type=Path, default=DEFAULT_Q3RUNTIME)
    parser.add_argument("--q1-assets", type=Path, default=DEFAULT_Q1_ASSETS)
    parser.add_argument("--q2-assets", type=Path, default=DEFAULT_Q2_ASSETS)
    parser.add_argument("--q3-basepath", type=Path, default=DEFAULT_Q3_BASEPATH)
    parser.add_argument("--q3-game", default=DEFAULT_Q3_GAME)
    cfg = parser.parse_args(argv)
    cfg.generate_only = cfg.generate_only or cfg.no_compile
    cfg.light_timeout = cfg.light_timeout or 2 * cfg.light_budget
    cfg.qmg = cfg.qmg.resolve()
    cfg.qmg_extra_list = shlex.split(cfg.qmg_extra) if cfg.qmg_extra else []
    cfg.assets = AssetIndex(cfg.q1_assets, cfg.q2_assets, cfg.q3_basepath, cfg.q3_game)
    cfg.q1_wad_names, cfg.q1_wad_label = set(), None
    if cfg.q1_wad:
        cfg.q1_wad = cfg.q1_wad.resolve()
        cfg.q1_wad_names = read_wad2_names(cfg.q1_wad)
        cfg.q1_wad_label = f"{cfg.q1_wad} (sha256 {sha256_file(cfg.q1_wad)[:16]})"
    cfg.seal = load_seal()
    out_root = (REPO / "out").resolve()
    tools = tool_hashes(cfg)
    mismatched = [n for n, pin in TOOL_PINS.items() if tools.get(n) != pin]
    if cfg.controls:
        cfg.controls_root = out_root
        cfg.controls_out = (cfg.output or DEFAULT_CONTROLS).resolve()
        ensure_inside(cfg.controls_out, out_root)
        if mismatched and not cfg.allow_unpinned:
            print(f"tool pin mismatch: {mismatched}", file=sys.stderr)
            return 2
        return run_controls(cfg)

    cfg.output = (cfg.output or DEFAULT_OUTPUT).resolve()
    cfg.output_root = out_root
    ensure_inside(cfg.output, out_root)
    cfg.manifest = cfg.manifest.resolve()
    registry = load_registry()
    manifest, manifest_info = load_manifest(cfg.manifest, registry, cfg.allow_modified_manifest)
    cfg.output.mkdir(parents=True, exist_ok=True)
    if mismatched and not cfg.allow_unpinned and not cfg.summary_only and not cfg.generate_only \
            and not cfg.import_dir:
        print(f"tool pin mismatch: {mismatched}", file=sys.stderr)
        return 2
    cfg.snapshot = source_snapshot()
    cfg.identity = build_identity(cfg, tools, manifest_info)
    if cfg.import_dir:
        cfg.import_dir = cfg.import_dir.resolve()
        recorded = None
        for name in ("run.json", "summary.json"):
            path = cfg.import_dir / name
            if path.is_file():
                data = json.loads(path.read_text(encoding="utf-8"))
                recorded = (data.get("identity") or {}).get("qmg_sha256") or (data.get("qmg") or {}).get("sha256")
                if recorded:
                    break
        if recorded != tools.get("qmg"):
            print(f"refusing --import: {rel(cfg.import_dir)} was produced by qmg {str(recorded)[:16]}, "
                  f"--qmg is {str(tools.get('qmg'))[:16]}", file=sys.stderr)
            return 2
        if cfg.qmg_extra_list:
            print("refusing --import with --qmg-extra", file=sys.stderr)
            return 2

    selected = [c for c in manifest["cases"] if not cfg.filter or any(f in c["id"] for f in cfg.filter)]
    refusals = []
    runnable = []
    for case in selected:
        if is_sealed(case, cfg.seal) and not cfg.hero_run and not cfg.import_dir:
            refusals.append(case["id"])
        else:
            runnable.append(case)
    if refusals and not cfg.summary_only:
        with open(cfg.output / "seal-refusals.jsonl", "a", encoding="utf-8") as handle:
            for cid in refusals:
                handle.write(json.dumps({"at": now_iso(), "id": cid, "reason": "sealed (D26); --hero-run not given",
                                         "argv": sys.argv[1:]}) + "\n")
                print(f"[sealed] {cid}: refused (D26 hero seal; generation needs --hero-run)", flush=True)
    run_record = {"schema": "qmg.benchmark.run", "schema_version": 1, "at": now_iso(), "argv": sys.argv[1:],
                  "identity": cfg.identity, "selected": len(selected), "runnable": len(runnable),
                  "sealed_refused": refusals, "import_from": rel(cfg.import_dir) if cfg.import_dir else None}
    if not cfg.summary_only:
        write_json(cfg.output / "run.json", run_record)
        with open(cfg.output / "runs.jsonl", "a", encoding="utf-8") as handle:
            handle.write(json.dumps(run_record, ensure_ascii=False) + "\n")
        worker = import_case if cfg.import_dir else run_case
        print(f"{'importing' if cfg.import_dir else 'running'} {len(runnable)} case(s) with {cfg.jobs} job(s), "
              f"policy {cfg.policy} -> {rel(cfg.output)}", flush=True)
        errors = 0
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, cfg.jobs)) as pool:
            futures = {pool.submit(worker, case, cfg): case["id"] for case in runnable}
            for future in concurrent.futures.as_completed(futures):
                cid = futures[future]
                try:
                    outcome = future.result()
                except Exception as error:  # report and continue; the case stays resumable
                    errors += 1
                    print(f"[error] {cid}: {type(error).__name__}: {error}", flush=True)
                    continue
                if outcome.get("error"):
                    errors += 1
                    print(f"[error] {cid}: {outcome['error']}", flush=True)
                elif outcome.get("skipped"):
                    print(f"[skip] {cid}", flush=True)
                else:
                    extra = ""
                    if "gate_diffs" in outcome:
                        extra = f" import gate_diffs={outcome['gate_diffs']} request_diffs={outcome['request_diffs']}"
                    print(f"[done] {cid} all_hard_gates={outcome['all_hard_gates']} "
                          f"wall={outcome['wall_seconds']}s{extra}", flush=True)
    try:
        summary = build_summary(manifest, cfg)
    except SummaryRefused as refused:
        print(f"summary refused: {refused}", file=sys.stderr)
        return 3
    write_json(cfg.output / "summary.json", summary)
    write_summary_md(summary, cfg.output / "summary.md")
    print(f"summary: {summary['cases_run']}/{summary['cases_total']} cases with results; "
          f"all hard gates {summary['all_hard_gates']}; with G10 reported {summary['all_hard_gates_with_G10_reported']}",
          flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
