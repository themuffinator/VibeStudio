#!/usr/bin/env python3
"""Assemble VibeStudio.app and its disk image on macOS.

    python scripts/package_macos_app.py --binary builddir/src/vibestudio \
        --compiled-translations builddir/i18n --docs-site build/docs-site \
        --qt-prefix "$QT_ROOT_DIR" --output dist

Steps: stage the portable layout (scripts/package_portable.py), move it into a
bundle (Contents/MacOS/vibestudio, everything else under Contents/Resources),
write Info.plist from packaging/macos/Info.plist.in, add the brand icon, copy
the Qt frameworks with macdeployqt, sign ad hoc, and build
VibeStudio-<version>-macos-<arch>.dmg with the branded Finder background
(create-dmg when installed, plain hdiutil otherwise).

Builds are ad-hoc signed only, not notarised: Gatekeeper asks before the first
launch (docs/manual/install.md explains how to open it).
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

BRANDING = ROOT / "assets" / "branding"
MINIMUM_MACOS = "13.0"
RESOURCE_ITEMS = ("docs", "i18n", "samples", "licenses", "README.md", "CHANGELOG.md", "VERSION")


def run(command: list[str], **kwargs) -> None:
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, check=True, **kwargs)


def info_plist(version: Version) -> str:
    text = (ROOT / "packaging" / "macos" / "Info.plist.in").read_text(encoding="utf-8")
    # CFBundleVersion and CFBundleShortVersionString take numbers only; the full
    # label (with any pre-release) is shown in the studio's About page.
    return (text.replace("@SHORT_VERSION@", version.core)
                .replace("@BUNDLE_VERSION@", version.core)
                .replace("@MINIMUM_SYSTEM_VERSION@", MINIMUM_MACOS))


def assemble(binary: Path, translations: Path | None, docs_site: Path | None, work: Path, version: Version,
             architecture: str) -> Path:
    package_dir, _ = create_package(binary, work / "staging", str(version), include_samples=True, archive=False,
                                    target_platform="macos", target_architecture=architecture,
                                    compiled_translations=translations, docs_site=docs_site)
    app = work / "VibeStudio.app"
    if app.exists():
        shutil.rmtree(app)
    macos = app / "Contents" / "MacOS"
    resources = app / "Contents" / "Resources"
    macos.mkdir(parents=True)
    resources.mkdir(parents=True)
    shutil.copy2(package_dir / "bin" / binary.name, macos / "vibestudio")
    os.chmod(macos / "vibestudio", 0o755)
    for item in RESOURCE_ITEMS:
        source = package_dir / item
        if source.is_dir():
            shutil.copytree(source, resources / item)
        elif source.is_file():
            shutil.copy2(source, resources / item)
    shutil.copy2(BRANDING / "icons" / "vibestudio.icns", resources / "vibestudio.icns")
    (app / "Contents" / "Info.plist").write_text(info_plist(version), encoding="utf-8")
    (app / "Contents" / "PkgInfo").write_text("APPL????", encoding="ascii")
    return app


def deploy_and_sign(app: Path, qt_prefix: Path | None, identity: str) -> None:
    tool = shutil.which("macdeployqt")
    if qt_prefix is not None and (qt_prefix / "bin" / "macdeployqt").is_file():
        tool = str(qt_prefix / "bin" / "macdeployqt")
    if not tool:
        raise FileNotFoundError("macdeployqt was not found; pass --qt-prefix.")
    run([tool, str(app), "-always-overwrite", "-verbose=1"])
    run(["codesign", "--force", "--deep", "--sign", identity, str(app)])
    run(["codesign", "--verify", "--deep", "--strict", "--verbose=2", str(app)])


def background_image(work: Path) -> Path:
    """A HiDPI-aware background (TIFF with both resolutions) when tiffutil is available."""
    one = BRANDING / "installer" / "macos-dmg-background.png"
    two = BRANDING / "installer" / "macos-dmg-background@2x.png"
    if shutil.which("tiffutil"):
        tiff = work / "background.tiff"
        run(["tiffutil", "-cathidpicheck", str(one), str(two), "-out", str(tiff)])
        return tiff
    return one


def make_dmg(app: Path, dmg: Path, volume: str, work: Path) -> None:
    if dmg.exists():
        dmg.unlink()
    source = work / "dmg-source"
    if source.exists():
        shutil.rmtree(source)
    source.mkdir()
    shutil.copytree(app, source / app.name, symlinks=True)
    if shutil.which("create-dmg"):
        try:
            run(["create-dmg", "--volname", volume, "--background", str(background_image(work)),
                 "--window-pos", "200", "120", "--window-size", "660", "400", "--icon-size", "128",
                 "--icon", app.name, "165", "190", "--hide-extension", app.name,
                 "--app-drop-link", "495", "190", str(dmg), str(source)])
            return
        except subprocess.CalledProcessError:
            print("create-dmg could not lay out the window; falling back to a plain disk image.", flush=True)
            if dmg.exists():
                dmg.unlink()
    (source / "Applications").symlink_to("/Applications")
    run(["hdiutil", "create", "-volname", volume, "-srcfolder", str(source), "-ov", "-format", "UDZO", str(dmg)])


def main() -> int:
    parser = argparse.ArgumentParser(description="Assemble VibeStudio.app and its disk image.")
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--compiled-translations", type=Path)
    parser.add_argument("--docs-site", type=Path)
    parser.add_argument("--qt-prefix", type=Path, default=Path(os.environ["QT_ROOT_DIR"]) if os.environ.get("QT_ROOT_DIR") else None)
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    parser.add_argument("--version", help="Defaults to VERSION.")
    parser.add_argument("--architecture", default=platform.machine() or "arm64")
    parser.add_argument("--sign-identity", default="-", help="codesign identity; '-' signs ad hoc.")
    parser.add_argument("--no-dmg", action="store_true", help="Stop after the signed .app.")
    args = parser.parse_args()
    if sys.platform != "darwin":
        print("The macOS bundle can only be assembled on macOS.", file=sys.stderr)
        return 1
    try:
        version = Version.parse(args.version) if args.version else read_version()
        args.output.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix="vibestudio-macos-"))
        app = assemble(args.binary, args.compiled_translations, args.docs_site, work, version, args.architecture)
        deploy_and_sign(app, args.qt_prefix, args.sign_identity)
        final_app = args.output / app.name
        if final_app.exists():
            shutil.rmtree(final_app)
        shutil.copytree(app, final_app, symlinks=True)
        print(final_app)
        if not args.no_dmg:
            name = asset_names(str(version))["macos-dmg"].replace("-arm64", f"-{args.architecture}")
            dmg = args.output / name
            make_dmg(app, dmg, f"VibeStudio {version}", work)
            print(dmg)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"macOS packaging failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
