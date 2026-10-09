#!/usr/bin/env python3
"""Give a Windows machine without a GPU a software Vulkan driver.

VibeStudio's 3D views draw with OpenGL or Vulkan, and most tests run on Qt's
offscreen platform, where only Vulkan draws on Windows. Hosted Windows CI
runners have no graphics driver, so without help their drawing tests skip.
This script puts what they need into one folder:

- the Vulkan loader (vulkan-1.dll, with vulkaninfo.exe) from LunarG's Vulkan
  Runtime components; VibeStudio opens the loader at run time;
- Mesa's lavapipe driver (vulkan_lvp.dll and its manifest) from the
  mesa-dist-win MSVC build.

Both downloads are pinned by version and SHA-256 below. The script then runs
vulkaninfo with VK_DRIVER_FILES naming lavapipe's manifest, so the loader sees
lavapipe and nothing else, and fails unless lavapipe's llvmpipe device shows.

Under GitHub Actions it adds the folder to PATH and sets VK_DRIVER_FILES for
the job's later steps; elsewhere it prints the two settings to use:

    python scripts/install_software_vulkan.py
    python scripts/install_software_vulkan.py --dest "$env:RUNNER_TEMP/software-vulkan"

Nothing is installed system-wide and none of it ships with VibeStudio: the
folder only serves tests. Unpacking Mesa's archive needs 7-Zip (7z on PATH,
or SEVEN_ZIP naming 7z.exe). Linux CI takes lavapipe from its distribution
(mesa-vulkan-drivers) instead.
"""
from __future__ import annotations

import argparse
import hashlib
import http.client
import json
import os
import platform
import shutil
import subprocess
import sys
import time
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import NoReturn

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DEST = ROOT / "build" / "software-vulkan"


@dataclass(frozen=True)
class Download:
    name: str
    url: str
    sha256: str
    filename: str


# The Khronos Vulkan loader as LunarG builds and signs it for the Vulkan SDK.
VULKAN_RUNTIME_VERSION = "1.4.363.0"
VULKAN_RUNTIME = Download(
    name=f"LunarG Vulkan Runtime {VULKAN_RUNTIME_VERSION}",
    url=f"https://sdk.lunarg.com/sdk/download/{VULKAN_RUNTIME_VERSION}/windows/vulkan-runtime-components.zip",
    sha256="a25a927aa8b9f0371048f1861cf88ac3b9bc9b1fb332c42d897c8ab32695769a",
    filename=f"VulkanRT-X64-{VULKAN_RUNTIME_VERSION}-Components.zip",
)

# pal1000's Windows builds of Mesa; lavapipe is Mesa's Vulkan driver that
# runs on the processor (llvmpipe).
MESA_VERSION = "26.2.4"
MESA = Download(
    name=f"Mesa {MESA_VERSION} from mesa-dist-win (MSVC build)",
    url=f"https://github.com/pal1000/mesa-dist-win/releases/download/{MESA_VERSION}/mesa3d-{MESA_VERSION}-release-msvc.7z",
    sha256="351fc8c8b695878ffb3eaa044b3ead08672a48b1a045e3c3e3975811df0f6695",
    filename=f"mesa3d-{MESA_VERSION}-release-msvc.7z",
)

# Inside the runtime's top folder, VulkanRT-X64-<version>-Components/.
LOADER_FILES = ("x64/vulkan-1.dll", "x64/vulkaninfo.exe", "VulkanRT-License.txt")
LAVAPIPE_LIBRARY = "vulkan_lvp.dll"
LAVAPIPE_MANIFEST = "lvp_icd.x86_64.json"
DOWNLOAD_ATTEMPTS = 3


def fail(message: str) -> NoReturn:
    raise SystemExit(f"install_software_vulkan: {message}")


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(1 << 20):
            digest.update(chunk)
    return digest.hexdigest()


def fetch(url: str, path: Path) -> str:
    """Write ``url`` to ``path`` and return the SHA-256 of what arrived."""
    digest = hashlib.sha256()
    request = urllib.request.Request(url, headers={"User-Agent": "VibeStudio-CI"})
    with urllib.request.urlopen(request, timeout=120) as response, path.open("wb") as out:
        while chunk := response.read(1 << 20):
            digest.update(chunk)
            out.write(chunk)
    return digest.hexdigest()


def download(item: Download, folder: Path) -> Path:
    """Download ``item`` into ``folder`` once, refusing any other bytes."""
    target = folder / item.filename
    if target.is_file() and sha256_of(target) == item.sha256:
        print(f"{item.name}: already downloaded")
        return target
    folder.mkdir(parents=True, exist_ok=True)
    partial = target.with_name(target.name + ".part")
    actual = ""
    for attempt in range(1, DOWNLOAD_ATTEMPTS + 1):
        try:
            actual = fetch(item.url, partial)
            break
        except (OSError, http.client.HTTPException) as error:
            partial.unlink(missing_ok=True)
            if attempt == DOWNLOAD_ATTEMPTS:
                fail(f"could not download {item.name} from {item.url}: {error}")
            print(f"{item.name}: download failed ({error}); trying again", file=sys.stderr, flush=True)
            time.sleep(10 * attempt)
    if actual != item.sha256:
        partial.unlink(missing_ok=True)
        fail(f"{item.url} has SHA-256 {actual}, but {item.sha256} is pinned. "
             "Check the new file before updating the pin in this script.")
    partial.replace(target)
    print(f"{item.name}: downloaded, SHA-256 {actual}")
    return target


def extract_loader(archive: Path, dest: Path) -> None:
    with zipfile.ZipFile(archive) as bundle:
        names = bundle.namelist()
        for wanted in LOADER_FILES:
            matches = [name for name in names if name.endswith("/" + wanted)]
            if len(matches) != 1:
                fail(f"{archive.name} has no single {wanted}")
            with bundle.open(matches[0]) as source, (dest / Path(wanted).name).open("wb") as out:
                shutil.copyfileobj(source, out)


def seven_zip() -> str:
    candidates = [os.environ.get("SEVEN_ZIP"), shutil.which("7z"), shutil.which("7za"),
                  str(Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "7-Zip" / "7z.exe")]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return candidate
    fail("7-Zip is needed to unpack Mesa's .7z archive; install it or set SEVEN_ZIP to 7z.exe")


def extract_lavapipe(archive: Path, dest: Path) -> None:
    files = (LAVAPIPE_LIBRARY, LAVAPIPE_MANIFEST)
    for name in files:
        (dest / name).unlink(missing_ok=True)
    result = subprocess.run([seven_zip(), "e", "-y", "-bso0", "-bsp0", f"-o{dest}", str(archive),
                             *[f"x64\\{name}" for name in files]],
                            capture_output=True, text=True, errors="replace")
    missing = [name for name in files if not (dest / name).is_file()]
    if result.returncode != 0 or missing:
        fail(f"7-Zip could not unpack {', '.join(missing) or 'lavapipe'} from {archive.name}:\n"
             f"{result.stdout}{result.stderr}")
    manifest = json.loads((dest / LAVAPIPE_MANIFEST).read_text(encoding="utf-8"))
    library = str(manifest.get("ICD", {}).get("library_path", ""))
    if Path(library.replace("\\", "/")).name != LAVAPIPE_LIBRARY:
        fail(f"{LAVAPIPE_MANIFEST} names {library!r}, not {LAVAPIPE_LIBRARY} beside it")


def check_lavapipe(dest: Path) -> str:
    """Run vulkaninfo against lavapipe alone; return the device it reports."""
    env = dict(os.environ)
    env["VK_DRIVER_FILES"] = str(dest / LAVAPIPE_MANIFEST)
    env["PATH"] = str(dest) + os.pathsep + env.get("PATH", "")
    try:
        result = subprocess.run([str(dest / "vulkaninfo.exe"), "--summary"], env=env, capture_output=True,
                                text=True, errors="replace", timeout=180)
    except (OSError, subprocess.TimeoutExpired) as error:
        fail(f"vulkaninfo did not run: {error}")
    devices = result.stdout.partition("Devices:")[2]
    names = [line.split("=", 1)[1].strip() for line in devices.splitlines()
             if line.strip().startswith("deviceName") and "=" in line]
    if result.returncode != 0 or not any("llvmpipe" in name for name in names):
        fail("vulkaninfo does not list lavapipe's llvmpipe device:\n" + result.stdout + result.stderr)
    return names[0]


def export(dest: Path) -> None:
    manifest = dest / LAVAPIPE_MANIFEST
    path_file = os.environ.get("GITHUB_PATH")
    env_file = os.environ.get("GITHUB_ENV")
    if path_file and env_file:
        with open(path_file, "a", encoding="utf-8") as handle:
            handle.write(f"{dest}\n")
        with open(env_file, "a", encoding="utf-8") as handle:
            handle.write(f"VK_DRIVER_FILES={manifest}\n")
        print(f"Later steps find the loader on PATH ({dest}) and lavapipe through VK_DRIVER_FILES.")
        return
    print("To draw with it, set in PowerShell:")
    print(f'  $env:PATH = "{dest};" + $env:PATH')
    print(f'  $env:VK_DRIVER_FILES = "{manifest}"')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n", 1)[0])
    parser.add_argument("--dest", type=Path, default=DEFAULT_DEST,
                        help=f"folder for the loader and lavapipe (default: {DEFAULT_DEST.relative_to(ROOT)})")
    args = parser.parse_args()
    if sys.platform != "win32" or platform.machine().upper() not in ("AMD64", "X86_64"):
        fail("this installs 64-bit Windows binaries; on Linux install mesa-vulkan-drivers instead")

    dest = args.dest.resolve()
    dest.mkdir(parents=True, exist_ok=True)
    downloads = dest / "downloads"
    extract_loader(download(VULKAN_RUNTIME, downloads), dest)
    extract_lavapipe(download(MESA, downloads), dest)
    device = check_lavapipe(dest)
    print(f"Vulkan loader {VULKAN_RUNTIME_VERSION} and Mesa {MESA_VERSION} lavapipe in {dest}: {device}")
    system_loader = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "vulkan-1.dll"
    if system_loader.is_file():
        print(f"Note: programs load {system_loader} before the copy here; it reads VK_DRIVER_FILES too.")
    export(dest)
    return 0


if __name__ == "__main__":
    sys.exit(main())
