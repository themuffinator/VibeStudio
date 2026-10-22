"""CTest asset_admission: registry tool and admission checks, contract cases plus the recorded control list.

usage:
  python -B tools/asset_registry/test_asset_admission.py [--q2 DIR] [--q3 DIR] [--q1-wad PATH] [--controls PATH]

1. Contract cases (always run, no game data): synthetic Quake II, Quake III and Quake trees built in a temporary
   directory from project-authored header bytes (no pixels from any game), passed through registry.py as a separate
   process and through admission.admit. They cover header decoding, flag decoding, the shader parser, the
   .pak/.pk3 refusal, the out/assets write guard and every reason path the recorded controls rely on.
2. Recorded controls (tools/asset_registry/controls.json) against registries built from --q2 and --q3 by
   registry.py --stdout. The registries are read from the pipe and never written to disk here.
3. When --q1-wad is given, the WAD registry is built and WALL1_1/FLOOR0_1 verdicts against it are printed
   (informational: the no-WAD controls still run with no Q1 registry loaded).

Exit 0: everything ran and passed. Exit 1: any failure. Exit 77 with a QMG-NOT-CONFIGURED line: everything that ran
passed, but recorded controls were skipped because an asset directory is absent.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
import admission  # noqa: E402

REGISTRY_TOOL = HERE / "registry.py"
NOT_CONFIGURED = 77

failures: list[str] = []
passes = 0


def check(condition: bool, label: str) -> None:
    global passes
    if condition:
        passes += 1
    else:
        failures.append(label)
        print(f"FAIL {label}")


def run_registry(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, "-B", str(REGISTRY_TOOL), *args], capture_output=True)


def build_registry(*args: str) -> dict:
    result = run_registry(*args, "--stdout")
    if result.returncode != 0:
        raise RuntimeError(f"registry.py {' '.join(args)} failed ({result.returncode}): {result.stderr.decode(errors='replace')}")
    return json.loads(result.stdout.decode("ascii"))


def codes(result: dict) -> set[str]:
    return {reason["code"] for reason in result["reasons"]}


def expect(index: admission.Index, game: str, name: str, role: str, verdict: str, need: set[str] | None = None,
           label: str = "") -> dict:
    result = admission.admit(index, game, name, role)
    tag = label or f"{game} {name!r} as {role}"
    unknown = codes(result) - set(admission.REASON_CODES)
    check(not unknown, f"{tag}: reason codes outside the closed list {sorted(unknown)}")
    check(result["verdict"] == verdict, f"{tag}: expected {verdict}, got {result['verdict']} {sorted(codes(result))}")
    if need:
        check(need <= codes(result), f"{tag}: expected reason codes {sorted(need)}, got {sorted(codes(result))}")
    if verdict == "reject":
        check(all(reason["detail"] for reason in result["reasons"]), f"{tag}: every rejection reason needs a detail")
    return result


# --------------------------------------------------------------------------- synthetic headers (project-authored)

def wal_bytes(name: str, width: int, height: int, flags: int = 0, contents: int = 0, value: int = 0,
              animname: str = "", break_offsets: bool = False) -> bytes:
    offsets, offset = [], 100
    for level in range(4):
        offsets.append(offset)
        offset += (width >> level) * (height >> level)
    if break_offsets:
        offsets[1] += 4
    header = (name.encode("ascii").ljust(32, b"\0") + struct.pack("<II", width, height) + struct.pack("<4I", *offsets)
              + animname.encode("ascii").ljust(32, b"\0") + struct.pack("<iii", flags, contents, value))
    return header + bytes(offset - 100)


def tga_bytes(width: int, height: int, depth: int = 24) -> bytes:
    header = bytes([0, 0, 2]) + bytes(5) + struct.pack("<HHHH", 0, 0, width, height) + bytes([depth, 8 if depth == 32 else 0])
    return header + bytes(width * height * depth // 8)


def jpeg_bytes(width: int, height: int) -> bytes:
    app0 = b"\xff\xe0" + struct.pack(">H", 16) + b"JFIF\0\x01\x01\0\0\x01\0\x01\0\0"
    sof0 = b"\xff\xc0" + struct.pack(">HBHHB", 17, 8, height, width, 3) + bytes(9)
    return b"\xff\xd8" + app0 + sof0 + b"\xff\xd9"


def wad_bytes(lumps: list[tuple[str, int, bytes]]) -> bytes:
    body, directory, offset = b"", b"", 12
    for name, lump_type, data in lumps:
        directory += struct.pack("<iiibbbb", offset, len(data), len(data), lump_type, 0, 0, 0) + name.encode("latin-1").ljust(16, b"\0")
        body += data
        offset += len(data)
    return b"WAD2" + struct.pack("<ii", len(lumps), offset) + body + directory


def miptex_bytes(name: str, width: int, height: int) -> bytes:
    offsets, offset = [], 40
    for level in range(4):
        offsets.append(offset)
        offset += (width >> level) * (height >> level)
    return name.encode("latin-1").ljust(16, b"\0") + struct.pack("<II4I", width, height, *offsets) + bytes(offset - 40)


Q3_TEST_SHADERS = """// project-authored test shaders
textures/qmgtest/glow
{
\tsurfaceparm nomarks
\tq3map_surfacelight 500
\t{
\t\tmap $lightmap
\t}
\t{
\t\tmap textures/qmgtest/plainbase.tga
\t\tblendFunc GL_DST_COLOR GL_ZERO
\t}
}
textures/qmgtest/screen
{
\t{
\t\tmap textures/qmgtest/layer.tga
\t\tblendfunc add
\t\ttcMod scroll 3 1
\t}
\t/* a block comment
\t   spanning lines { } */
\t{
\t\tmap $lightmap
\t\tblendfunc filter
\t}
}
textures/qmgtest/tight{
\tsurfaceparm metalsteps
\t{ map "textures/qmgtest/plainbase.tga" }
}
textures/qmgtest/sky
{
\tsurfaceparm sky
\tsurfaceparm noimpact
\tq3map_surfacelight 80
\tskyparms - 512 -
\t{
\t\tmap textures/qmgtest/layer.tga
\t\ttcMod scroll 0.1 0.1
\t}
}
textures/qmgtest/lava
{
\tsurfaceparm lava
\tsurfaceparm nolightmap
\t{
\t\tmap textures/qmgtest/plainbase.tga
\t}
}
textures/qmgtest/missing
{
\t{
\t\tmap textures/qmgtest/nothere.tga
\t}
}
textures/qmgtest/dup
{
\tsurfaceparm nodraw
}
textures/qmgtest/dup
{
\t{
\t\tmap textures/qmgtest/plainbase.tga
\t}
}
textures/qmgtest/unclosed
{
\tsurfaceparm nodraw
"""


def contract_cases(tmp: Path) -> None:
    # ---- Quake II tree
    q2 = tmp / "q2"
    (q2 / "textures" / "qmgtest").mkdir(parents=True)
    (q2 / "env").mkdir()
    walls = {
        "plain": wal_bytes("qmgtest/plain", 16, 16),
        "lamp": wal_bytes("qmgtest/lamp", 16, 16, flags=0x1, value=300),
        "goo": wal_bytes("qmgtest/goo", 16, 16, flags=0x8, contents=0x10),
        "anim0": wal_bytes("qmgtest/anim0", 16, 16, animname="qmgtest/anim1"),
        "anim1": wal_bytes("qmgtest/anim1", 16, 16, animname="qmgtest/anim0"),
        "bad": wal_bytes("qmgtest/bad", 16, 16, break_offsets=True),
        "odd": wal_bytes("qmgtest/elsewhere", 32, 16, contents=0x8000000 | 0x40000000),
    }
    for stem, data in walls.items():
        (q2 / "textures" / "qmgtest" / f"{stem}.wal").write_bytes(data)
    (q2 / "textures" / "qmgtest" / "hires.jpg").write_bytes(jpeg_bytes(256, 128))
    for face in ("rt", "bk", "lf", "ft", "up", "dn"):
        (q2 / "env" / f"qmgsky{face}.tga").write_bytes(tga_bytes(8, 8))
    registry = build_registry("--game", "q2", "--root", str(q2))
    again = run_registry("--game", "q2", "--root", str(q2), "--stdout").stdout
    check(json.dumps(registry, sort_keys=True, indent=1, ensure_ascii=True).encode("ascii") + b"\n" == again,
          "q2 registry output is byte-identical across two processes and in canonical form")
    by_key = {entry["key"]: entry for entry in registry["textures"]}
    check(registry["counts"] == {"texture_names": 8, "with_wal": 7, "image_only": 1, "wal_header_invalid": 1,
                                 "wal_internal_name_mismatch": 1, "images": 1, "skies": 1, "other_files": 0},
          f"q2 registry counts {registry['counts']}")
    lamp = by_key["qmgtest/lamp"]["wal"]
    check(lamp["surf"] == ["SURF_LIGHT"] and lamp["value"] == 300 and lamp["width"] == 16, "q2 WAL flags and value decode")
    goo = by_key["qmgtest/goo"]["wal"]
    check(goo["surf"] == ["SURF_WARP"] and goo["contents_flags"] == ["CONTENTS_SLIME"], "q2 WAL contents decode")
    odd = by_key["qmgtest/odd"]["wal"]
    check(odd["contents_flags"] == ["CONTENTS_DETAIL"] and odd["contents_unknown_bits"] == ["0x40000000"]
          and odd.get("internal_name_mismatch") is True, "q2 unknown contents bits and internal-name mismatch are kept")
    hires = by_key["qmgtest/hires"]
    check(hires["wal"] is None and hires["images"][0]["width"] == 256 and hires["images"][0]["height"] == 128,
          "q2 JPEG-only name decodes its SOF dimensions")
    check(registry["skies"][0]["complete_tga"] and not registry["skies"][0]["complete_pcx"], "q2 sky set completeness")
    check(all(("origin" in entry and "licence_status" in entry and "source" in entry) for entry in registry["textures"]),
          "every q2 entry has source, origin and licence_status")
    index = admission.Index({"q2": registry})
    expect(index, "q2", "qmgtest/plain", "wall", "accept")
    expect(index, "q2", "QMGTEST/PLAIN", "floor", "accept", label="q2 lookup is case-insensitive")
    expect(index, "q2", "qmgtest/lamp", "ceiling", "reject", {"emitter"})
    expect(index, "q2", "qmgtest/lamp", "light_emitter", "accept")
    expect(index, "q2", "qmgtest/plain", "light_emitter", "reject", {"role_requirement"})
    expect(index, "q2", "qmgtest/goo", "floor", "reject", {"liquid", "warp"})
    expect(index, "q2", "qmgtest/goo", "slime", "accept")
    expect(index, "q2", "qmgtest/goo", "water", "reject", {"role_requirement"})
    expect(index, "q2", "qmgtest/anim0", "wall", "reject", {"animated"})
    expect(index, "q2", "qmgtest/bad", "wall", "reject", {"header_invalid"})
    expect(index, "q2", "qmgtest/odd", "wall", "reject", {"non_structural"})
    expect(index, "q2", "qmgtest/hires", "wall", "reject", {"no_wal"})
    expect(index, "q2", "qmgtest/nothere", "wall", "reject", {"absent"})
    expect(index, "q2", "", "wall", "reject", {"name_missing"})
    expect(index, "q2", "qmgtest/plain", "banister", "reject", {"unknown_role"})
    expect(index, "q2", "qmgtest/plain", "fog", "reject", {"unsupported_role"})
    expect(admission.Index(), "q2", "qmgtest/plain", "wall", "reject", {"registry_unavailable"})

    # ---- refusal and write guard
    archive_root = tmp / "q2pak"
    (archive_root / "textures").mkdir(parents=True)
    (archive_root / "pak0.pak").write_bytes(b"")
    check(run_registry("--game", "q2", "--root", str(archive_root), "--stdout").returncode == 2,
          "registry.py refuses a tree that holds .pak archives")
    guard_target = REPO / "data" / "assets" / "q2" / "qmgtest-guard.json"
    guard = run_registry("--game", "q2", "--root", str(q2), "--out", str(guard_target))
    check(guard.returncode == 2 and not guard_target.exists(), "registry.py refuses to write outside out/assets (D9 pending)")
    check(run_registry("--game", "q1", "--stdout").returncode == 2, "registry.py --game q1 without --q1-wad is an error")

    # ---- Quake III tree
    q3 = tmp / "q3"
    (q3 / "scripts").mkdir(parents=True)
    (q3 / "textures" / "qmgtest").mkdir(parents=True)
    (q3 / "scripts" / "qmgtest.shader").write_text(Q3_TEST_SHADERS, encoding="ascii", newline="\n")
    (q3 / "scripts" / "notes.txt").write_text("not a shader", encoding="ascii")
    (q3 / "textures" / "qmgtest" / "plainbase.tga").write_bytes(tga_bytes(8, 8))
    (q3 / "textures" / "qmgtest" / "layer.jpg").write_bytes(jpeg_bytes(16, 16))
    (q3 / "textures" / "qmgtest" / "alpha.tga").write_bytes(tga_bytes(4, 4, 32))
    registry3 = build_registry("--game", "q3", "--root", str(q3))
    shaders = {}
    for shader in registry3["shaders"]:
        shaders.setdefault(shader["key"], []).append(shader)
    check(registry3["counts"]["shader_files"] == 1 and registry3["counts"]["shader_names"] == 7
          and registry3["counts"]["duplicate_names"] == 1, f"q3 shader counts {registry3['counts']}")
    check(len(registry3["parse_errors"]) == 1 and "is not closed" in registry3["parse_errors"][0]["error"],
          f"q3 unclosed shader is a recorded parse error {registry3['parse_errors']}")
    glow = shaders["textures/qmgtest/glow"][0]
    check(glow["line"] == 2 and glow["source"] == "scripts/qmgtest.shader:2" and glow["q3map_surfacelight"] == 500
          and glow["surfaceparms"] == ["nomarks"], "q3 shader file:line, surfaceparms and q3map_surfacelight")
    check(shaders["textures/qmgtest/sky"][0]["skyparms"] == {"farbox": "-", "cloudheight": "512", "nearbox": "-"},
          "q3 skyparms")
    tight = shaders["textures/qmgtest/tight"][0]
    check(tight["stages"][0]["maps"] == ["textures/qmgtest/plainbase.tga"], "q3 tokenizer splits braces and reads quotes")
    screen = shaders["textures/qmgtest/screen"][0]
    check(len(screen["stages"]) == 2 and screen["stages"][0]["blendfunc"] == ["GL_ONE", "GL_ONE"]
          and screen["stages"][0]["tcmods"] == ["scroll"], "q3 block comments are skipped and blend shorthands expand")
    check([d["definition_index"] for d in shaders["textures/qmgtest/dup"]] == [0, 1], "q3 duplicate definitions are kept in order")
    alpha = [image for image in registry3["images"] if image["key"] == "textures/qmgtest/alpha"][0]
    check(alpha["tga_alpha_bits"] == 8 and alpha["tga_pixel_depth"] == 32, "q3 TGA alpha bits decode")
    index3 = admission.Index({"q3": registry3})
    expect(index3, "q3", "qmgtest/plainbase", "wall", "accept", label="q3 implicit image with a modulating stage use is plain")
    expect(index3, "q3", "qmgtest/layer", "wall", "reject", {"layer_image"})
    expect(index3, "q3", "qmgtest/alpha", "wall", "accept", label="q3 implicit image with no stage use")
    expect(index3, "q3", "qmgtest/glow", "ceiling", "reject", {"emitter"})
    expect(index3, "q3", "qmgtest/glow", "light_emitter", "accept")
    expect(index3, "q3", "qmgtest/screen", "wall", "reject", {"animated"})
    expect(index3, "q3", "qmgtest/tight", "floor", "accept")
    expect(index3, "q3", "qmgtest/sky", "sky", "accept")
    expect(index3, "q3", "qmgtest/sky", "ceiling", "reject", {"sky", "emitter", "animated"})
    expect(index3, "q3", "qmgtest/sky", "light_emitter", "reject", {"sky"})
    expect(index3, "q3", "qmgtest/lava", "lava", "accept")
    expect(index3, "q3", "qmgtest/lava", "floor", "reject", {"liquid"})
    expect(index3, "q3", "qmgtest/missing", "wall", "reject", {"missing_stage_image"})
    expect(index3, "q3", "qmgtest/dup", "wall", "reject", {"tool_surface"}, label="q3 first duplicate definition wins")
    expect(index3, "q3", "qmgtest/dup", "hidden", "accept")
    expect(index3, "q3", "qmgtest/plainbase", "lava", "reject", {"role_requirement"})
    expect(index3, "q3", "qmg/lava", "lava", "reject", {"absent"})
    expect(index3, "q3", "qmgtest/plainbase", "window", "reject", {"unsupported_role"})
    pk3_root = tmp / "q3pk3"
    (pk3_root / "scripts").mkdir(parents=True)
    (pk3_root / "pak0.pk3").write_bytes(b"")
    check(run_registry("--game", "q3", "--root", str(pk3_root), "--stdout").returncode == 2,
          "registry.py refuses a tree that holds .pk3 archives")

    # ---- Quake WAD2
    wad_path = tmp / "qmgtest.wad"
    wad_path.write_bytes(wad_bytes([
        ("QMGWALL", 68, miptex_bytes("QMGWALL", 16, 16)),
        ("*lavaqmg", 68, miptex_bytes("*lavaqmg", 16, 16)),
        ("*qmgdrink", 68, miptex_bytes("*qmgdrink", 16, 16)),
        ("skyqmg", 68, miptex_bytes("skyqmg", 32, 16)),
        ("+0qmgbtn", 68, miptex_bytes("+0qmgbtn", 16, 16)),
        ("trigger", 68, miptex_bytes("trigger", 16, 16)),
        ("conqmg", 66, struct.pack("<ii", 2, 2) + bytes(4)),
    ]))
    registry1 = build_registry("--game", "q1", "--q1-wad", str(wad_path))
    check(registry1["counts"] == {"miptex": 6, "other_lumps": 1, "errors": 0}, f"q1 WAD counts {registry1['counts']}")
    check(registry1["source"]["wad_sha256"] == hashlib.sha256(wad_path.read_bytes()).hexdigest(), "q1 WAD sha256 recorded")
    sky = [entry for entry in registry1["textures"] if entry["key"] == "skyqmg"][0]
    check(sky["width"] == 32 and sky["height"] == 16 and sky["header_valid"], "q1 miptex dimensions")
    index1 = admission.Index({"q1": registry1})
    expect(index1, "q1", "qmgwall", "wall", "accept", label="q1 lookup is case-insensitive")
    expect(index1, "q1", "*lavaqmg", "wall", "reject", {"liquid"})
    expect(index1, "q1", "*lavaqmg", "lava", "accept")
    expect(index1, "q1", "*lavaqmg", "water", "reject", {"role_requirement"})
    expect(index1, "q1", "*qmgdrink", "water", "accept")
    expect(index1, "q1", "skyqmg", "ceiling", "reject", {"sky"})
    expect(index1, "q1", "skyqmg", "sky", "accept")
    expect(index1, "q1", "+0qmgbtn", "wall", "reject", {"animated"})
    expect(index1, "q1", "trigger", "trigger", "accept")
    expect(index1, "q1", "trigger", "wall", "reject", {"tool_surface"})
    expect(index1, "q1", "conqmg", "wall", "reject", {"absent"}, label="q1 non-miptex lumps are not textures")
    expect(index1, "q1", "WALL1_1", "wall", "reject", {"absent"})
    expect(index1, "q1", "QMGWALL", "light_emitter", "reject", {"unsupported_role"})
    expect(admission.Index(), "q1", "QMGWALL", "wall", "reject", {"no_q1_wad"})


# --------------------------------------------------------------------------- recorded controls

def recorded_controls(controls_path: Path, registries: dict[str, dict]) -> list[str]:
    spec = json.loads(controls_path.read_text(encoding="utf-8"))
    skipped = []
    plan_ids = [control["id"] for control in spec["controls"] if control.get("plan")]
    check(len(plan_ids) == 7, f"controls.json lists the plan's seven negative controls (got {len(plan_ids)})")
    seen = set()
    for control in spec["controls"]:
        check(control["id"] not in seen, f"duplicate control id {control['id']}")
        seen.add(control["id"])
        missing = [game for game in control["requires"] if game not in registries]
        if missing:
            skipped.append(control["id"])
            print(f"SKIP {control['id']}: no {', '.join(missing)} registry")
            continue
        index = admission.Index({game: registries[game] for game in control["requires"]})
        before = len(failures)
        result = expect(index, control["game"], control["name"], control["role"], control["expect"],
                        set(control.get("expect_codes", [])), label=control["id"])
        for key, value in control.get("expect_facts", {}).items():
            check(result["facts"].get(key) == value,
                  f"{control['id']}: fact {key} expected {value!r}, registry has {result['facts'].get(key)!r}")
        status = "ok  " if len(failures) == before else "BAD "
        facts = {key: result["facts"][key] for key in ("flags", "surf", "contents", "contents_flags", "value",
                                                        "resolution", "image", "shader", "surfaceparms",
                                                        "q3map_surfacelight") if key in result["facts"]}
        print(f"{status}{control['id']}: {result['verdict']} {sorted(codes(result))} {json.dumps(facts, sort_keys=True)}")
    return skipped


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--q2", default="", help="extracted Quake II baseq2 directory (empty: not configured)")
    parser.add_argument("--q3", default="", help="extracted Quake III baseq3 directory (empty: not configured)")
    parser.add_argument("--q1-wad", default="", help="user-supplied Quake texture WAD2 (optional)")
    parser.add_argument("--controls", type=Path, default=HERE / "controls.json")
    args = parser.parse_args(argv)

    with tempfile.TemporaryDirectory(prefix="qmg-asset-admission-") as tmp:
        contract_cases(Path(tmp))
    print(f"contract cases: {passes} checks, {len(failures)} failed")

    registries: dict[str, dict] = {}
    absent: list[str] = []
    for game, root in (("q2", args.q2), ("q3", args.q3)):
        if not root or not Path(root).is_dir():
            absent.append(f"{game} asset directory ({root or 'unset'})")
            continue
        try:
            registries[game] = build_registry("--game", game, "--root", root)
        except RuntimeError as error:
            check(False, f"{game} registry: {error}")
            continue
        payload = (json.dumps(registries[game], indent=1, sort_keys=True, ensure_ascii=True) + "\n").encode("ascii")
        digest = hashlib.sha256(payload).hexdigest()
        recorded = REPO / "out" / "assets" / game / "registry.json"
        state = "absent"
        if recorded.is_file():
            state = "identical" if hashlib.sha256(recorded.read_bytes()).hexdigest() == digest else "different"
        print(f"{game} registry from {root}: sha256 {digest}, counts {json.dumps(registries[game]['counts'], sort_keys=True)}; "
              f"out/assets/{game}/registry.json {state}")
    skipped = recorded_controls(args.controls, registries)

    if args.q1_wad:
        wad = Path(args.q1_wad)
        if wad.is_file():
            try:
                index1 = admission.Index({"q1": build_registry("--game", "q1", "--q1-wad", str(wad))})
                for name, role in (("WALL1_1", "wall"), ("FLOOR0_1", "floor"), ("CEIL1_1", "ceiling")):
                    result = admission.admit(index1, "q1", name, role)
                    print(f"info q1 WAD {wad.name}: {name} as {role}: {result['verdict']} {sorted(codes(result))}")
            except RuntimeError as error:
                check(False, f"q1 WAD registry: {error}")
        else:
            absent.append(f"q1 WAD ({wad})")

    total = passes + len(failures)
    print(f"asset_admission: {passes}/{total} checks passed, {len(skipped)} recorded controls skipped")
    if failures:
        return 1
    if skipped or absent:
        print(f"QMG-NOT-CONFIGURED: {len(skipped)} recorded controls did not run because inputs are absent: "
              f"{'; '.join(absent)}")
        return NOT_CONFIGURED
    return 0


if __name__ == "__main__":
    sys.exit(main())
