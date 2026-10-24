"""Original paste placement, CLI, compiler and package integration fixtures.

Generated geometry and textures only. No game launch, OS input or screen capture.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import zipfile

from level_compiler_test_helpers import box, png
from level_linked_extraction_workflow import compiled_geometry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--compiler", type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    root = args.output_root.resolve()
    assert root.is_relative_to(repo / ".agents" / "tmp")
    base = root / "game" / "baseq3"
    maps = base / "maps"
    maps.mkdir(parents=True, exist_ok=True)
    (root / "home").mkdir(exist_ok=True)
    steps, variants = [], []

    def run(label, words, success=True, cli=True):
        command = ([str(args.binary.resolve()), "--cli", "--json", "--settings-file", str(root / "settings.ini")]
                   + list(map(str, words)) if cli else list(map(str, words)))
        result = subprocess.run(command, cwd=repo, capture_output=True, timeout=120)
        (root / f"{label}.stdout.txt").write_bytes(result.stdout)
        (root / f"{label}.stderr.txt").write_bytes(result.stderr)
        steps.append({"step": label, "command": command, "exitCode": result.returncode, "expectedSuccess": success})
        (root / "steps.json").write_text(json.dumps(steps, indent=2), encoding="utf-8")
        assert (result.returncode == 0) == success, (label, result.stdout[-1600:], result.stderr[-800:])
        print(label, "OK", flush=True)
        return json.loads(result.stdout) if cli else result.stdout.decode(errors="replace")

    room = [((-528, -528, -16), (528, 528, 0)), ((-528, -528, 384), (528, 528, 400)),
            ((-528, -528, 0), (-512, 528, 384)), ((512, -528, 0), (528, 528, 384)),
            ((-512, -528, 0), (512, -512, 384)), ((-512, 512, 0), (512, 528, 384))]
    png(base / "textures" / "paste" / "wall.png")
    for kind in ("classic", "valve", "primitive"):
        for face in range(6):
            png(base / "textures" / "paste" / kind / f"face{face}.png")
        source = maps / f"{kind}-source.map"
        source.write_text('{\n"classname" "worldspawn"\n'
                          + "\n".join(box(lo, hi, kind, "paste/wall") for lo, hi in room)
                          + '\n}\n{\n"classname" "info_player_deathmatch"\n"origin" "0 128 48"\n}\n'
                          + '{\n"classname" "light"\n"origin" "0 0 320"\n"light" "600"\n}\n', encoding="utf-8")
        clipboard = root / f"{kind}-clipboard.map"
        clipboard.write_text('{\n"classname" "func_door"\n"speed" "123"\n"origin" "0 0 0"\n'
                             + box((-32, -16, 0), (32, 16, 96), kind, f"paste/{kind}", True)
                             + "\n}\n", encoding="utf-8")
        original_source, original_clipboard = source.read_bytes(), clipboard.read_bytes()
        mode = {"classic": "camera", "valve": "plane", "primitive": "view-ray"}[kind]
        target = (["--camera", "160,0,80"] if mode == "camera" else
                  ["--plane-point", "160,0,0", "--plane-normal", "0,0,1", "--ray-direction", "0,0,-1"] if mode == "plane" else
                  ["--ray-origin", "160,0,200", "--view-direction", "0,0,-1", "--ray-direction", "0,0,-1"])
        delta = [160, 0, 32] if mode == "camera" else [192, 16, 0]
        direct, placed = maps / f"{kind}-direct.map", maps / f"{kind}-placed.map"
        run(f"{kind}-direct", ["map", "paste", source, "--from", clipboard, "--delta", ",".join(map(str, delta)),
                              "--output", direct, "--overwrite"])
        command = ["map", "paste", source, "--from", clipboard, "--output", placed] + target + ["--grid", "16"]
        previous = placed.read_bytes() if placed.exists() else None
        run(f"{kind}-dry-run", command + ["--dry-run", "--overwrite"])
        assert (placed.read_bytes() if placed.exists() else None) == previous
        result = run(f"{kind}-place", command + ["--overwrite"])
        assert result["placement"] == mode
        assert [result["delta"][axis] for axis in ("x", "y", "z")] == delta
        assert len(result["pasted"]) == 1
        assert source.read_bytes() == original_source and clipboard.read_bytes() == original_clipboard
        # Targeted paste and the independently calculated translation must yield
        # identical serialised geometry, owners, properties and UV values.
        assert direct.read_bytes() == placed.read_bytes(), kind
        reopened = run(f"{kind}-reopen", ["map", "inspect", placed])
        assert reopened["map"]["repeatRecipe"]["actions"] == []
        protected = placed.read_bytes()
        run(f"{kind}-no-overwrite", command, success=False)
        run(f"{kind}-dry-run-existing", command + ["--dry-run", "--overwrite"])
        assert placed.read_bytes() == protected
        if kind == "classic":
            for name, origin, expected_delta in [
                    ("front", "160,0,200", [192, 16, 400]),
                    ("miss", "800,0,200", [832, 16, 208])]:
                orbit_path, orbit_direct = root / f"orbit-{name}.map", root / f"orbit-{name}-direct.map"
                orbit = run(f"orbit-{name}", ["map", "paste", source, "--from", clipboard, "--ray-origin", origin,
                                            "--ray-direction", "0,0,-1", "--view-direction", "0,0,-1",
                                            "--orthographic", "--grid", "16", "--output", orbit_path, "--overwrite"])
                assert orbit["placement"] == "view-ray" and orbit["orthographic"]
                assert [orbit["delta"][axis] for axis in ("x", "y", "z")] == expected_delta
                run(f"orbit-{name}-direct", ["map", "paste", source, "--from", clipboard,
                                            "--delta", ",".join(map(str, expected_delta)), "--output", orbit_direct, "--overwrite"])
                assert orbit_path.read_bytes() == orbit_direct.read_bytes()
            invalid = [
                ["--camera", "1,2,3", "--delta", "1,2,3"],
                ["--camera", "nan,0,0"], ["--camera", "1,2,3", "--grid", "-1"],
                ["--plane-point", "1,2,3"], ["--plane-point", "1,2,3", "--plane-normal", "0,0,1", "--ray-direction", "0,1,0"],
                ["--ray-origin", "1,2,3", "--ray-direction", "0,0,0", "--view-direction", "0,0,1"],
                ["--ray-origin", "1,2,3", "--ray-direction", "0,0,1", "--view-direction", "0,0,-1"],
                ["--plane-normal", "0,0,1"], ["--view-direction", "0,0,1"], ["--grid", "16"],
                ["--camera", "0,0,40000"], ["--camera", "0,0,0", "--camera", "1,1,1"],
                ["--orthographic"], ["--camera", "0,0,0", "--orthographic"], ["--orthographic=false"],
                ["--dry-run=false"], ["--unknown"], ["extra-map"]]
            for index, options in enumerate(invalid):
                run(f"invalid-{index}", ["map", "paste", source, "--from", clipboard, "--output", placed, "--overwrite"] + options,
                    success=False)
                assert placed.read_bytes() == protected
            point = root / "point.map"
            point.write_text('{\n"classname" "info_paste"\n"origin" "11 23 37"\n}\n', encoding="utf-8")
            definition = root / "paste.def"
            definition.write_text("/*QUAKED info_paste (1 0 0) (-4 -8 0) (12 16 40)\nPaste test.\n*/\n", encoding="utf-8")
            point_result = run("definition-bounds", ["map", "paste", source, "--from", point, "--camera", "100,200,300",
                                                   "--definitions", definition, "--output", root / "point-placed.map", "--overwrite"])
            assert [point_result["delta"][a] for a in ("x", "y", "z")] == [85, 173, 243]
            for name, data in [("invalid-utf8", b"\xff{}"), ("nul", b"{\x00}"), ("unbalanced", b"{")]:
                bad = root / f"{name}.map"; bad.write_bytes(data)
                run(name, ["map", "paste", source, "--from", bad, "--output", placed, "--overwrite"], success=False)
                assert placed.read_bytes() == protected
            for context_kind in ("layer", "group"):
                context_map = root / f"{context_kind}-context.map"
                created = run(f"create-{context_kind}", ["editor", "scene", "create", source, "--kind", context_kind,
                                                        "--name", "Paste destination", "--output", context_map, "--overwrite"])
                context = created["createdId"]
                context_output = root / f"{context_kind}-pasted.map"
                insertion = ["map", "paste", context_map, "--from", clipboard, "--context", context,
                             "--camera", "160,0,80", "--grid", "16", "--output", context_output, "--overwrite"]
                inserted = run(f"paste-{context_kind}", insertion)
                scene = run(f"inspect-{context_kind}", ["editor", "scene", "list", context_output])["scene"]
                node = next(n for n in scene["nodes"] if n["id"] == context)
                assert set(inserted["pasted"]).issubset(set(node["objects"]))
                protected_context = context_output.read_bytes()
                locked = root / f"{context_kind}-locked.map"
                run(f"lock-{context_kind}", ["editor", "scene", "lock", context_map, "--id", context,
                                            "--locked", "true", "--output", locked, "--overwrite"])
                locked_insertion = list(insertion)
                locked_insertion[2] = locked
                run(f"refuse-locked-{context_kind}", locked_insertion, success=False)
                assert context_output.read_bytes() == protected_context
            run("unknown-context", ["map", "paste", source, "--from", clipboard, "--context", "unknown",
                                    "--output", placed, "--overwrite"], success=False)
            assert placed.read_bytes() == protected
        variant = {"dialect": kind, "placement": mode, "delta": delta}
        if args.compiler:
            prefix = [args.compiler.resolve(), "-game", "quake3", "-fs_basepath", root / "game",
                      "-fs_homepath", root / "home", "-fs_game", "baseq3", "-threads", "2"]
            for label, path in [("direct", direct), ("placed", placed)]:
                for stage, options in [("bsp", ["-meta"]), ("vis", ["-vis", "-fast"]), ("light", ["-light", "-fast"])]:
                    output = run(f"{kind}-{label}-{stage}", prefix + options + [path], cli=False)
                    assert "LEAKED" not in output and "ERROR:" not in output
            expected, expected_entities, expected_models = compiled_geometry(direct.with_suffix(".bsp").read_bytes())
            actual, actual_entities, actual_models = compiled_geometry(placed.with_suffix(".bsp").read_bytes())
            assert actual == expected and actual_entities == expected_entities and actual_models == expected_models
            subset, package = root / f"{kind}-assets.pk3", root / f"{kind}-level.pk3"
            run(f"{kind}-dependencies", ["map", "dependencies", placed, "--package", base, "--engine", "idTech3"])
            run(f"{kind}-subset", ["package", "subset", base, subset, "--map-input", placed, "--engine", "idTech3", "--overwrite"])
            run(f"{kind}-package", ["package", "save-as", subset, package, "--format", "pk3", "--overwrite",
                                   "--add-file", placed, "--as", f"maps/{kind}.map",
                                   "--add-file", placed.with_suffix(".bsp"), "--as", f"maps/{kind}.bsp"])
            validation = run(f"{kind}-validate", ["package", "validate", package])["validation"]
            assert validation["valid"] and validation["verifiedCount"] == 9
            with zipfile.ZipFile(package) as archive:
                assert archive.testzip() is None and archive.read(f"maps/{kind}.map") == placed.read_bytes()
                assert archive.read(f"maps/{kind}.bsp") == placed.with_suffix(".bsp").read_bytes()
            variant.update(identicalSurfaces=sum(actual.values()), models=actual_models, validation=validation,
                           packageSha256=hashlib.sha256(package.read_bytes()).hexdigest())
        variants.append(variant)
    report = {"stepsPassed": len(steps), "variants": variants, "applicationSha256": hashlib.sha256(args.binary.read_bytes()).hexdigest()}
    if args.compiler:
        report["compilerSha256"] = hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    (root / "verified.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"PASS: {len(steps)} paste CLI/workflow steps across three brush dialects.")


if __name__ == "__main__":
    main()
