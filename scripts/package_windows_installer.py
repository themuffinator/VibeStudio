#!/usr/bin/env python3
"""Build the Windows installer (Inno Setup) from a staged portable package.

    python scripts/package_windows_installer.py --package-dir <staged package> --output dist

The staged package is the directory scripts/package_windows_release.py (or
package_portable.py plus windeployqt) produced: bin/vibestudio.exe with its Qt
runtime, docs, i18n, samples and licenses. This script converts the brand's
wizard art to the BMPs Inno Setup needs and runs ISCC on
packaging/windows/vibestudio.iss. The result is
VibeStudio-<version>-windows-x64-setup.exe.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from release_meta import ROOT, Version, asset_names, read_version

BRANDING = ROOT / "assets" / "branding"
SCRIPT = ROOT / "packaging" / "windows" / "vibestudio.iss"


def find_iscc(explicit: str | None) -> Path:
    candidates = [explicit] if explicit else []
    candidates += [shutil.which("ISCC"), shutil.which("iscc")]
    for base in (os.environ.get("ProgramFiles(x86)"), os.environ.get("ProgramFiles"), os.environ.get("LOCALAPPDATA")):
        if base:
            candidates.append(str(Path(base) / "Inno Setup 6" / "ISCC.exe"))
            candidates.append(str(Path(base) / "Programs" / "Inno Setup 6" / "ISCC.exe"))
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return Path(candidate)
    raise FileNotFoundError("Inno Setup 6 (ISCC.exe) was not found. Install it (choco install innosetup) or pass --iscc.")


def wizard_art(directory: Path) -> None:
    """Flatten the PNG wizard art onto white 24-bit BMPs, as Inno Setup expects."""
    from PIL import Image

    for name in ("windows-wizard-100", "windows-wizard-200", "windows-wizard-small-100", "windows-wizard-small-200"):
        image = Image.open(BRANDING / "installer" / f"{name}.png").convert("RGBA")
        flat = Image.new("RGB", image.size, (255, 255, 255))
        flat.paste(image, (0, 0), image)
        flat.save(directory / f"{name}.bmp", "BMP")
    shutil.copy2(ROOT / "packaging" / "windows" / "before-install.txt", directory / "before-install.txt")


def build(package_dir: Path, output: Path, version: Version, iscc: Path) -> Path:
    if not (package_dir / "bin" / "vibestudio.exe").is_file():
        raise FileNotFoundError(f"No bin/vibestudio.exe in the staged package: {package_dir}")
    if not (package_dir / "licenses" / "vibestudio" / "LICENSE").is_file():
        raise FileNotFoundError("The staged package has no licenses/vibestudio/LICENSE.")
    output.mkdir(parents=True, exist_ok=True)
    name = asset_names(str(version))["windows-installer"]
    with tempfile.TemporaryDirectory(prefix="vibestudio-installer-") as temp:
        art = Path(temp)
        wizard_art(art)
        command = [
            str(iscc), "/Q",
            f"/DAppVersion={version}",
            f"/DVersionInfo={version.major}.{version.minor}.{version.patch}.0",
            f"/DSourceDir={package_dir.resolve()}",
            f"/DArtDir={art}",
            f"/DIconFile={(BRANDING / 'icons' / 'vibestudio.ico').resolve()}",
            f"/DOutputDir={output.resolve()}",
            f"/DOutputBaseFilename={Path(name).stem}",
            str(SCRIPT),
        ]
        subprocess.run(command, check=True)
    installer = output / name
    if not installer.is_file():
        raise FileNotFoundError(f"ISCC finished without writing {installer}")
    return installer


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--package-dir", type=Path, required=True, help="Staged portable package with the Qt runtime.")
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    parser.add_argument("--version", help="Defaults to VERSION.")
    parser.add_argument("--iscc", help="Path to ISCC.exe.")
    args = parser.parse_args()
    try:
        version = Version.parse(args.version) if args.version else read_version()
        installer = build(args.package_dir, args.output, version, find_iscc(args.iscc))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Windows installer failed: {error}", file=sys.stderr)
        return 1
    print(installer)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
