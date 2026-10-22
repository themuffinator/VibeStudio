"""Run an isolated, dedicated-only Q2 map initialization smoke check on Windows.

This driver uses an independently installed, hash-pinned Q2REPRO executable and
legacy game DLL as separate processes. No engine code is copied into this module.
It never launches a client, creates a render capture, or controls keyboard/mouse
input. The source audit and limitations are recorded with each run.
"""

from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import time

from tools.map_study.binary import Bsp


PROJECT = Path(__file__).resolve().parents[2]
ENGINE_VERSION = "Q2REPRO r4580~aeeb335f"
SOURCE_COMMIT = "aeeb335f9a4de296c73d66b39e2a1efcfe7ade0d"
PINNED_FILES = {
    "q2reproded.exe": "6a41422ad60fc3e8679192feb45a9b56da225a29e8d4d09e2a9cd87d824c3053",
    "gamex86_64.dll": "9f59a9f0c0c69e746cc0eec708920d8f5a52fcf843e74dee099af601a023a036",
}
LIMITS = [
    "Dedicated server only: no engine render target or lighting/readability evidence.",
    "No connected players: no spawn selection, pickup contact, traversal, mover activation or teleporter-use test.",
    "Checks native entity initialization and scheduled autonomous physics ticks; absence of diagnostics is a smoke result, not a balance or reachability proof.",
    "dumpents exports the authored BSP entity text, not live edict origins or mover state.",
    "The standard legacy module exposes no live entity-state dump server command; lift initial state is not established by this check.",
    "Read-only assets and a copied executable/module are local test dependencies, not bundled map content or project-linked libraries.",
]


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def project_directory(path: Path) -> Path:
    path = path.absolute()
    for ancestor in (path, *path.parents):
        if ancestor.exists() or ancestor.is_symlink():
            attributes = getattr(ancestor.lstat(), "st_file_attributes", 0)
            if ancestor.is_symlink() or attributes & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0):
                raise ValueError(f"Writable task paths cannot traverse filesystem links: {ancestor}")
    path = path.resolve()
    path.relative_to(PROJECT)
    return path


def write_json(path: Path, value) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def run(args) -> dict:
    if os.name != "nt":
        raise ValueError("This source-audited harness supports the pinned Windows dedicated build only")
    if not 2 <= args.frames <= 1000:
        raise ValueError("frames must be between 2 and 1000")
    if not 5 <= args.timeout <= 120:
        raise ValueError("timeout must be between 5 and 120 seconds")
    bsp_path = args.bsp.resolve(strict=True)
    map_name = bsp_path.stem
    if not re.fullmatch(r"[a-z][a-z0-9_]*", map_name):
        raise ValueError("BSP name must be a lowercase Quake map identifier")
    bsp = Bsp(bsp_path.read_bytes())
    if bsp.file_issues or bsp.entity_issues:
        raise ValueError("Input BSP failed the repository format reader")
    assets = args.assets.resolve(strict=True)
    if not re.fullmatch(r"[A-Za-z0-9_-]+", assets.name):
        raise ValueError("Asset folder name must be a simple Quake game directory identifier")
    if not (assets / "models").is_dir() or not (assets / "textures").is_dir():
        raise ValueError("Expected the extracted stock Q2 asset directory")
    engine_source = args.engine_root.resolve(strict=True)
    for name, expected in PINNED_FILES.items():
        if sha(engine_source / name) != expected:
            raise ValueError(f"Unreviewed engine/module build: {name}")
    runtime = project_directory(args.runtime)
    output = project_directory(args.output)
    runtime_parts = runtime.relative_to(PROJECT / ".agents/tmp").parts
    if len(runtime_parts) != 1:
        raise ValueError("Runtime must be a named task directory directly under .agents/tmp")
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    work = runtime / "runs" / f"{map_name}-{run_id}"
    game = work / assets.name
    maps = game / "maps"
    engine = runtime / "engine"
    for directory in (output, maps, engine):
        project_directory(directory).mkdir(parents=True, exist_ok=True)
    for name, expected in PINNED_FILES.items():
        destination = engine / name
        if destination.exists() and sha(destination) != expected:
            raise ValueError(f"Existing independent engine copy changed: {destination}")
        if not destination.exists():
            shutil.copy2(engine_source / name, destination)
        if sha(destination) != expected:
            raise ValueError(f"Independent copy hash mismatch: {name}")
    shutil.copy2(bsp_path, maps / bsp_path.name)
    if sha(maps / bsp_path.name) != bsp.sha256:
        raise ValueError("Input BSP changed while creating its independent runtime copy")
    # Shadow startup configs with independent empty files. The stock asset tree
    # remains a fallback for game data; no installed config is executed or changed.
    for config_root in (work / "baseq2", game):
        config_root.mkdir(exist_ok=True)
        for name in ("default.cfg", "config.cfg", "autoexec.cfg", "postexec.cfg", "postinit.cfg"):
            (config_root / name).write_text("// Isolated dedicated smoke profile.\n", encoding="ascii")
    cfg = "\n".join([
        "setmaster",
        "set public 0",
        "set sv_locked 1",
        "set sv_mvd_enable 0",
        "set allow_download 0",
        f"map {map_name}",
        "echo QMG_SMOKE_MAP_LOADED",
        f"wait {args.frames}",
        "status",
        "listmasters",
        "net_stats",
        "sv_fps",
        "path",
        "dumpents authored-entities",
        "echo QMG_SMOKE_TICKS_DONE",
        "quit",
        "",
    ])
    (game / "qmg-smoke.cfg").write_text(cfg, encoding="ascii")
    settings = {
        "basedir": assets.parent.as_posix(),
        "homedir": work.as_posix(),
        "libdir": engine.as_posix(),
        "game": assets.name,
        "com_rerelease": "-1",
        "sys_forcegamelib": (engine / "gamex86_64.dll").as_posix(),
        "sys_exitonerror": "1",
        "sys_disablecrashdump": "1",
        "net_ip": "127.0.0.1",
        "net_enable_ipv6": "0",
        "net_port": "0",
        "public": "0",
        "sv_locked": "1",
        "sv_mvd_enable": "0",
        "deathmatch": "1",
        "coop": "0",
        "maxclients": "8",
        "dmflags": "0",
        "sv_fps": "10",
        "developer": "1",
        "logfile": "1",
        "logfile_name": "engine-smoke",
        "logfile_flush": "2",
    }
    command = [str(engine / "q2reproded.exe")]
    for key, value in settings.items():
        command.extend(["+set", key, value])
    command.extend(["+exec", "qmg-smoke.cfg"])
    started = time.monotonic()
    timed_out = False
    # CREATE_NO_WINDOW and redirected handles prevent creation of an input console.
    # The dedicated executable has no client/window renderer or mouse subsystem.
    with (work / "stdout.log").open("wb") as stdout, (work / "stderr.log").open("wb") as stderr:
        child = subprocess.Popen(command, cwd=work, stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr,
                                 creationflags=subprocess.CREATE_NO_WINDOW, close_fds=True)
        try:
            returncode = child.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            child.kill()
            returncode = child.wait(timeout=5)
    elapsed = time.monotonic() - started
    engine_log = game / "logs/engine-smoke.log"
    dump = game / "entdumps/authored-entities.ent"
    for source, name in ((work / "stdout.log", "stdout.log"), (work / "stderr.log", "stderr.log"),
                         (engine_log, "engine.log"), (dump, "authored-entities.ent"), (game / "qmg-smoke.cfg", "profile.cfg")):
        if source.is_file():
            shutil.copy2(source, project_directory(output / name))
    log = engine_log.read_text(encoding="utf-8", errors="replace") if engine_log.exists() else ""
    stdout_text = (work / "stdout.log").read_text(encoding="utf-8", errors="replace")
    stderr_text = (work / "stderr.log").read_text(encoding="utf-8", errors="replace")
    all_lines = list(dict.fromkeys((stdout_text + "\n" + stderr_text).splitlines()))
    authored_match = dump.exists() and dump.read_bytes().rstrip(b"\0") == bsp.lumps[0].rstrip(b"\0")
    failure_patterns = [
        r"droptofloor:.*startsolid", r"doesn't have a spawn function", r"has no spawn function",
        r"couldn't spawn", r"couldn't load", r"couldn't open.*(?:map|server udp|dedicated server)",
        r"failed to load game library", r"no map loaded", r"no server running", r"\bfatal\b",
        r"teleporter without a target", r"couldn't find destination", r"train_find: no target",
        r"bad model", r"bad inline model", r"out of edicts", r"overflow", r"invalid game api",
        r"is cheat protected", r"is write protected",
    ]
    expected_notices = [line for line in all_lines if
        line == "Couldn't get console buffer info." or
        line.startswith('Failed to load/parse mapdb.json[unknown location]: Couldn\'t load file "mapdb.json"') or
        line == "ED_ParseEdict: mapversion is not a field"]
    diagnostics = [line for line in all_lines if line not in expected_notices and
                   any(re.search(pattern, line, re.I) for pattern in failure_patterns)]
    inhibited = re.search(r"\b(\d+) entities inhibited\b", log)
    checks = {
        "exit_zero": returncode == 0,
        "within_watchdog": not timed_out,
        "map_loaded_marker": "QMG_SMOKE_MAP_LOADED" in log,
        "ticks_complete_marker": "QMG_SMOKE_TICKS_DONE" in log,
        "correct_map_status": f"Current map: {map_name}" in log,
        "legacy_module_loaded": f"Loaded game library from {(engine / 'gamex86_64.dll').as_posix()}" in log.replace("\\", "/"),
        "legacy_game_api": "Game API version: 3" in log,
        "entities_initialized": inhibited is not None,
        "legacy_ten_hz_server": bool(re.search(r'"sv_fps" is "10"', log)),
        "no_masters": "There are no masters." in log,
        "loopback_socket_requested": "Opening UDP socket: 127.0.0.1:0" in log,
        "no_nonloopback_socket_requested": not any("Opening UDP" in line and "127.0.0.1:0" not in line for line in log.splitlines()),
        "authored_entity_dump_matches_bsp": authored_match,
        "no_matched_initialization_failures": not diagnostics,
        "no_packets_sent_or_received": "Packets sent: 0 " in log and "Packets rcvd: 0 " in log,
        "tested_bsp_unchanged": sha(maps / bsp_path.name) == bsp.sha256,
    }
    evidence = {
        "kind": "dedicated_engine_initialization_smoke", "ok": all(checks.values()),
        "map": map_name, "bsp_sha256": bsp.sha256, "input_bsp": str(bsp_path), "engine": ENGINE_VERSION,
        "source_commit": SOURCE_COMMIT, "dependency_hashes": PINNED_FILES,
        "runtime_directory": str(work), "read_only_asset_directory": str(assets),
        "command": command, "settings": settings, "exit_code": returncode, "elapsed_seconds": round(elapsed, 3),
        "scheduled_post_spawn_server_frames": args.frames, "server_frame_milliseconds": 100,
        "initial_settling_frames": 2, "checks": checks, "matched_failure_diagnostics": diagnostics,
        "native_inhibited_entity_count": int(inhibited.group(1)) if inhibited else None,
        "expected_startup_notices": expected_notices,
        "authored_classes": dict(sorted(Counter(entity.get("classname", "") for entity in bsp.entities).items())),
        "client_launched": False, "rendering_captured": False, "keyboard_mouse_input": False,
        "live_edict_state_dump": False, "mover_activation_tested": False, "teleporter_use_tested": False,
        "limits": LIMITS,
        "source_audit": {
            "window_focus_why_client_excluded": "src/windows/client.c:109-110 unconditionally sets foreground window and focus",
            "dedicated_console_isolation": "src/windows/system.c:650 Sys_ConsoleInit: no AllocConsole in dedicated build; redirected handles return before input-mode changes",
            "asset_and_write_paths": "src/common/files.c:3699 setup_base_paths and 3720 setup_game_paths: homedir overrides basedir reads and owns generated files",
            "no_auto_detection": "inc/system/system.h:71 and src/common/files.c:4036: com_rerelease -1 skips detection",
            "entity_spawn_and_settling": "src/server/init.c:203 SpawnEntities followed by two RunFrame calls",
            "scheduled_frames": "src/server/main.c:1907 dedicated command buffer advances with server frames; src/server/init.c forces legacy modules to 10 Hz",
            "entity_dump_limit": "src/server/commands.c:523 dumpents writes BSP cache entitystring; legacy src/game/g_svcmds.c exposes no live edict dump",
        },
    }
    write_json(project_directory(output / "engine-smoke.json"), evidence)
    return evidence


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bsp", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--engine-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, default=PROJECT / ".agents/tmp/counterweight-engine")
    parser.add_argument("--frames", type=int, default=40)
    parser.add_argument("--timeout", type=float, default=30)
    args = parser.parse_args()
    evidence = run(args)
    print(json.dumps({"ok":evidence["ok"], "checks":evidence["checks"], "output":str(args.output.resolve())}))
    if not evidence["ok"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
