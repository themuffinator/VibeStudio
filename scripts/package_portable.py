#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import platform
import shutil
import sys
from datetime import datetime, timezone
from pathlib import Path


TARGETS: dict[str, dict[str, object]] = {
    "windows": {
        "displayName": "Windows",
        "archiveName": "win64",
        "launcherNotes": [
            "Launch bin/vibestudio.exe after running the package on a Windows machine with the Qt runtime staged by windeployqt.",
            "Portable packages do not modify game installs or source projects until the user chooses an explicit write/export action.",
        ],
        "deploymentTool": "windeployqt",
    },
    "macos": {
        "displayName": "macOS",
        "archiveName": "macos",
        "launcherNotes": [
            "Launch the staged binary or app bundle after macdeployqt has copied the Qt frameworks for this target.",
            "Unsigned local packages may require an explicit user-approved launch on a test machine.",
        ],
        "deploymentTool": "macdeployqt",
    },
    "linux": {
        "displayName": "Linux",
        "archiveName": "linux",
        "launcherNotes": [
            "Launch bin/vibestudio after staging the Qt runtime through the distribution package, linuxdeploy, AppImage, or another documented Linux deployment path.",
            "Run the CLI smoke commands from this README before publishing a Linux artifact.",
        ],
        "deploymentTool": "linuxdeploy/AppImage or distribution packaging",
    },
}

CANONICAL_DOCS = [
    "README.md",
    "docs/OFFLINE_USER_GUIDE.md",
    "docs/RELEASE_CANDIDATE.md",
    "docs/PACKAGING.md",
    "docs/ARCHITECTURE.md",
    "docs/STACK.md",
    "docs/DEPENDENCIES.md",
    "docs/CREDITS.md",
    "docs/SUPPORT_MATRIX.md",
    "docs/ACCESSIBILITY_LOCALIZATION.md",
    "docs/INITIAL_SETUP.md",
    "docs/ROADMAP.md",
]

COMPILER_LICENSE_SOURCES = [
    ("ericw-tools", "external/compilers/ericw-tools/COPYING"),
    ("q3map2-nrc", "external/compilers/q3map2-nrc/LICENSE"),
    ("ZDBSP", "external/compilers/zdbsp/COPYING"),
    ("ZokumBSP", "external/compilers/zokumbsp/src/COPYING"),
]


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def read_version(root: Path) -> str:
    return (root / "VERSION").read_text(encoding="utf-8").strip()


def host_target() -> str:
    system = platform.system().lower()
    if system == "windows":
        return "windows"
    if system == "darwin":
        return "macos"
    if system == "linux":
        return "linux"
    return "linux"


def host_architecture() -> str:
    return (platform.machine().lower() or "unknown").replace("amd64", "x86_64")


def copy_path(source: Path, destination: Path) -> None:
    if source.is_dir():
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(source, destination, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def safe_package_dir(output_root: Path, package_name: str) -> Path:
    resolved_output = output_root.resolve()
    package_dir = (resolved_output / package_name).resolve()
    if resolved_output == package_dir or resolved_output not in package_dir.parents:
        raise ValueError(f"Package directory escapes output root: {package_dir}")
    return package_dir


def relative_path(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def root_license_files(root: Path) -> list[Path]:
    files: list[Path] = []
    for pattern in ("LICENSE*", "COPYING*", "NOTICE*"):
        files.extend(path for path in root.glob(pattern) if path.is_file())
    return sorted(set(files))


def staged_license_files(package_dir: Path) -> list[str]:
    license_dir = package_dir / "licenses"
    if not license_dir.exists():
        return []
    return sorted(relative_path(path, package_dir) for path in license_dir.rglob("*") if path.is_file())


def write_platform_readme(package_dir: Path, target_platform: str, target_architecture: str, binary_name: str) -> str:
    target = TARGETS[target_platform]
    readme = package_dir / "platform" / "README.txt"
    readme.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        f"VibeStudio portable package target: {target['displayName']} ({target_architecture})",
        "",
        "Launch notes:",
    ]
    lines.extend(f"- {note}" for note in target["launcherNotes"])
    lines.extend(
        [
            "",
            "Clean-machine smoke checklist:",
            f"- Confirm the staged executable exists at bin/{binary_name}.",
            "- Run `vibestudio --cli --version`.",
            "- Run `vibestudio --cli package validate <fixture-or-sample-package> --json`.",
            "- Run `vibestudio --cli compiler profiles --json`.",
            "- Run `vibestudio --cli localization report --locale ar --json`.",
            "- Open the GUI, complete or skip first-run setup, and verify the Activity Center is visible.",
            "- Confirm docs/OFFLINE_USER_GUIDE.md, i18n/vibestudio_en.ts, and licenses/THIRD_PARTY_LICENSES.md are present.",
        ]
    )
    readme.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return relative_path(readme, package_dir)


def write_license_bundle(root: Path, package_dir: Path) -> tuple[str, list[str]]:
    license_dir = package_dir / "licenses"
    license_dir.mkdir(parents=True, exist_ok=True)

    copied_sources: list[str] = []
    for license_file in root_license_files(root):
        destination = license_dir / "vibestudio" / license_file.name
        copy_path(license_file, destination)
        copied_sources.append(relative_path(destination, package_dir))

    for relative in ("docs/CREDITS.md", "docs/DEPENDENCIES.md", "docs/PACKAGING.md"):
        source = root / relative
        if source.exists():
            destination = license_dir / relative
            copy_path(source, destination)
            copied_sources.append(relative_path(destination, package_dir))

    compiler_rows: list[str] = []
    for name, relative in COMPILER_LICENSE_SOURCES:
        source = root / relative
        if not source.exists():
            continue
        destination = license_dir / "external" / "compilers" / name / source.name
        copy_path(source, destination)
        copied_sources.append(relative_path(destination, package_dir))
        compiler_rows.append(f"| {name} | `{relative}` | `{relative_path(destination, package_dir)}` |")

    third_party = license_dir / "THIRD_PARTY_LICENSES.md"
    third_party.write_text(
        "\n".join(
            [
                "# Third-Party Licenses",
                "",
                "This bundle keeps license and attribution files visible in portable VibeStudio packages.",
                "Compiler sources are not bundled as executable toolchains in this package unless a future release manifest says otherwise, but their imported source licenses remain copied for review.",
                "",
                "## VibeStudio",
                "",
                "- VibeStudio root license: `licenses/vibestudio/LICENSE`.",
                "- Credits: `licenses/docs/CREDITS.md`.",
                "- Dependency notes: `licenses/docs/DEPENDENCIES.md`.",
                "- Packaging notes: `licenses/docs/PACKAGING.md`.",
                "",
                "## Imported Compiler Source Licenses",
                "",
                "| Component | Repository source | Bundled license file |",
                "| --- | --- | --- |",
                *compiler_rows,
                "",
            ]
        ),
        encoding="utf-8",
    )
    copied_sources.append(relative_path(third_party, package_dir))
    return relative_path(third_party, package_dir), sorted(set(copied_sources))


def write_checksums(package_dir: Path) -> str:
    checksum_path = package_dir / "CHECKSUMS.sha256"
    entries: list[str] = []
    for path in sorted(package_dir.rglob("*")):
        if not path.is_file() or path == checksum_path:
            continue
        entries.append(f"{sha256_file(path)}  {relative_path(path, package_dir)}")
    checksum_path.write_text("\n".join(entries) + "\n", encoding="utf-8")
    return relative_path(checksum_path, package_dir)


def build_manifest(
    root: Path,
    binary: Path,
    package_name: str,
    package_dir: Path,
    version: str,
    target_platform: str,
    target_architecture: str,
    included_samples: bool,
    archive_path: Path | None,
    platform_readme: str,
    license_bundle: str,
    license_files: list[str],
    checksums_path: str,
) -> dict:
    included_docs = [relative for relative in CANONICAL_DOCS if (root / relative).exists()]
    localization_root = root / "i18n"
    included_localization_catalogs = (
        sorted(relative_path(path, root) for path in localization_root.glob("vibestudio_*.ts") if path.is_file())
        if localization_root.exists()
        else []
    )
    target = TARGETS[target_platform]
    return {
        "schemaVersion": 2,
        "packageName": package_name,
        "version": version,
        "createdUtc": datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "targetPlatform": target_platform,
        "targetPlatformName": target["displayName"],
        "targetArchitecture": target_architecture,
        "hostPlatform": host_target(),
        "hostArchitecture": host_architecture(),
        "stagingDirectory": str(package_dir),
        "archivePath": str(archive_path) if archive_path else "",
        "binary": {
            "sourcePath": str(binary),
            "stagedPath": f"bin/{binary.name}",
            "sha256": sha256_file(binary),
        },
        "includedDocs": included_docs,
        "localizationCatalogRoot": "i18n" if localization_root.exists() else "",
        "includedLocalizationCatalogs": included_localization_catalogs,
        "offlineUserGuide": "docs/OFFLINE_USER_GUIDE.md" if (root / "docs/OFFLINE_USER_GUIDE.md").exists() else "",
        "platformReadme": platform_readme,
        "includedSamples": included_samples,
        "licenseBundle": license_bundle,
        "licenseFiles": license_files,
        "checksums": checksums_path,
        "qtDeployment": {
            "status": "planned",
            "tool": target["deploymentTool"],
            "notes": "Run the listed Qt deployment tool for this target before publishing a production-ready portable artifact.",
        },
        "externalCompilersBundled": False,
        "externalCompilerNotes": "Imported compiler source licenses are bundled for review; compiler executables remain external until a release explicitly stages them.",
    }


def create_package(
    binary: Path,
    output_root: Path,
    version: str,
    include_samples: bool,
    archive: bool,
    target_platform: str,
    target_architecture: str,
) -> tuple[Path, Path | None]:
    root = repo_root()
    binary = binary.resolve()
    if not binary.exists():
        raise FileNotFoundError(f"Binary not found: {binary}")
    if target_platform not in TARGETS:
        raise ValueError(f"Unsupported target platform: {target_platform}")

    package_name = f"vibestudio-{version}-{TARGETS[target_platform]['archiveName']}-{target_architecture}"
    package_dir = safe_package_dir(output_root, package_name)
    if package_dir.exists():
        shutil.rmtree(package_dir)
    package_dir.mkdir(parents=True)

    copy_path(binary, package_dir / "bin" / binary.name)
    for relative in ("README.md", "VERSION"):
        copy_path(root / relative, package_dir / relative)
    copy_path(root / "docs", package_dir / "docs")
    if (root / "i18n").exists():
        copy_path(root / "i18n", package_dir / "i18n")
    if include_samples:
        copy_path(root / "samples", package_dir / "samples")

    platform_readme = write_platform_readme(package_dir, target_platform, target_architecture, binary.name)
    license_bundle, license_files = write_license_bundle(root, package_dir)
    archive_path = package_dir.parent / f"{package_dir.name}.zip" if archive else None

    manifest_path = package_dir / "package-manifest.json"
    manifest_path.write_text(
        json.dumps(
            build_manifest(
                root,
                binary,
                package_name,
                package_dir,
                version,
                target_platform,
                target_architecture,
                include_samples,
                archive_path,
                platform_readme,
                license_bundle,
                license_files,
                "CHECKSUMS.sha256",
            ),
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    write_checksums(package_dir)

    if archive:
        archive_base = package_dir.parent / package_dir.name
        archive_file = shutil.make_archive(str(archive_base), "zip", package_dir.parent, package_dir.name)
        archive_path = Path(archive_file)
    return package_dir, archive_path


def requested_targets(target_platform: str) -> list[str]:
    if target_platform == "current":
        return [host_target()]
    if target_platform == "all":
        return list(TARGETS)
    return [target_platform]


def main() -> int:
    root = repo_root()
    parser = argparse.ArgumentParser(description="Stage portable VibeStudio package artifacts.")
    parser.add_argument("--binary", required=True, help="Built vibestudio executable to stage.")
    parser.add_argument("--output", default=str(root / "dist"), help="Output directory for staged packages.")
    parser.add_argument("--version", default=read_version(root), help="Package version. Defaults to VERSION.")
    parser.add_argument("--no-samples", action="store_true", help="Do not include sample projects.")
    parser.add_argument("--archive", action="store_true", help="Create zip archives next to staged directories.")
    parser.add_argument("--target-platform", choices=["current", "all", *TARGETS.keys()], default="current", help="Portable target platform to stage.")
    parser.add_argument("--target-architecture", default=host_architecture(), help="Target architecture label for the package name and manifest.")
    args = parser.parse_args()

    try:
        packages = [
            create_package(
                Path(args.binary),
                Path(args.output),
                args.version,
                include_samples=not args.no_samples,
                archive=args.archive,
                target_platform=target,
                target_architecture=args.target_architecture,
            )
            for target in requested_targets(args.target_platform)
        ]
    except Exception as exc:  # noqa: BLE001 - command-line tool reports concise failure.
        print(f"Packaging failed: {exc}", file=sys.stderr)
        return 1

    for package_dir, archive_path in packages:
        print(f"Portable package staged: {package_dir}")
        if archive_path:
            print(f"Portable package archive: {archive_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
