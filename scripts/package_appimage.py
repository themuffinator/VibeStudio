#!/usr/bin/env python3
"""Build the Linux AppImage from a Meson build configured with --prefix=/usr.

    python scripts/package_appimage.py --build-dir builddir --docs-site build/docs-site \
        --linuxdeploy tools/linuxdeploy-x86_64.AppImage --qmake "$QT_ROOT_DIR/bin/qmake" --output dist

Steps: ``meson install`` into an AppDir (binary, catalogs, licences, desktop
entry, AppStream metadata and icons from packaging/), add the documentation,
samples and licence bundle, then let linuxdeploy and its Qt plugin copy the Qt
runtime and write VibeStudio-<version>-linux-<arch>.AppImage. The linuxdeploy
Qt plugin must sit next to linuxdeploy or on PATH.
"""
from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from package_portable import create_package
from release_meta import ROOT, Version, asset_names, read_version

APP_ID = "io.github.themuffinator.VibeStudio"


def run(command: list[str], **kwargs) -> None:
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, check=True, **kwargs)


def build(build_dir: Path, appdir: Path, docs_site: Path | None, output: Path, version: Version,
          linuxdeploy: Path, qmake: Path | None, architecture: str) -> Path:
    if appdir.exists():
        shutil.rmtree(appdir)
    run(["meson", "install", "-C", str(build_dir), "--destdir", str(appdir.resolve()), "--no-rebuild"])
    usr = appdir / "usr"
    binary = usr / "bin" / "vibestudio"
    if not binary.is_file():
        raise FileNotFoundError(f"meson install did not produce {binary}; configure with --prefix=/usr.")
    # Development files (headers, static libraries) have no place in an AppImage.
    shutil.rmtree(usr / "include", ignore_errors=True)
    for archive in list(usr.glob("lib*/**/*.a")):
        archive.unlink()
    desktop = usr / "share" / "applications" / f"{APP_ID}.desktop"
    icon = usr / "share" / "icons" / "hicolor" / "256x256" / "apps" / f"{APP_ID}.png"
    for required in (desktop, icon):
        if not required.is_file():
            raise FileNotFoundError(f"Missing desktop integration file: {required}")

    # Documentation, samples and the full licence bundle, staged the same way as
    # every other package so the AppImage carries the same notices.
    with tempfile.TemporaryDirectory(prefix="vibestudio-appimage-") as temp:
        package_dir, _ = create_package(binary, Path(temp), str(version), include_samples=True, archive=False,
                                        target_platform="linux", target_architecture=architecture,
                                        compiled_translations=build_dir / "i18n", docs_site=docs_site)
        share = usr / "share" / "vibestudio"
        for item in ("samples", "licenses"):
            target = share / item
            if target.exists():
                shutil.rmtree(target)
            shutil.copytree(package_dir / item, target)
        doc = usr / "share" / "doc" / "vibestudio"
        if doc.exists():
            shutil.rmtree(doc)
        shutil.copytree(package_dir / "docs", doc)
        for item in ("README.md", "CHANGELOG.md", "VERSION"):
            if (package_dir / item).is_file():
                shutil.copy2(package_dir / item, doc / item)

    output.mkdir(parents=True, exist_ok=True)
    name = asset_names(str(version))["linux-appimage"].replace("x86_64", architecture)
    env = dict(os.environ)
    env["LDAI_OUTPUT"] = name          # linuxdeploy-plugin-appimage's output file name
    env["OUTPUT"] = name               # older plugin releases
    env["APPIMAGE_EXTRACT_AND_RUN"] = "1"  # runners lack FUSE
    env["PATH"] = str(linuxdeploy.resolve().parent) + os.pathsep + env.get("PATH", "")
    if qmake is not None:
        env["QMAKE"] = str(qmake)
    run([str(linuxdeploy), "--appdir", str(appdir.resolve()), "--desktop-file", str(desktop.resolve()),
         "--icon-file", str(icon.resolve()), "--plugin", "qt", "--output", "appimage"],
        cwd=output, env=env)
    result = output / name
    if not result.is_file():
        raise FileNotFoundError(f"linuxdeploy finished without writing {result}")
    result.chmod(0o755)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Build the Linux AppImage.")
    parser.add_argument("--build-dir", type=Path, required=True, help="Meson build directory configured with --prefix=/usr.")
    parser.add_argument("--appdir", type=Path, default=ROOT / "build" / "AppDir")
    parser.add_argument("--docs-site", type=Path)
    parser.add_argument("--linuxdeploy", type=Path, required=True)
    parser.add_argument("--qmake", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    parser.add_argument("--version", help="Defaults to VERSION.")
    parser.add_argument("--architecture", default=platform.machine() or "x86_64")
    args = parser.parse_args()
    try:
        version = Version.parse(args.version) if args.version else read_version()
        result = build(args.build_dir, args.appdir, args.docs_site, args.output, version, args.linuxdeploy,
                       args.qmake, args.architecture)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"AppImage packaging failed: {error}", file=sys.stderr)
        return 1
    print(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
