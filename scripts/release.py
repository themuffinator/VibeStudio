#!/usr/bin/env python3
"""Release workflow helpers: resolve what is being released, stage the
downloads under their published names, and write the release notes.

    python scripts/release.py metadata --event push --ref refs/tags/v0.1.0-alpha.1
    python scripts/release.py stage --artifacts artifacts --output dist/release
    python scripts/release.py notes --output dist/release-notes.md

``metadata`` prints ``key=value`` lines for $GITHUB_OUTPUT. ``stage`` renames
every build job's files to the names in release_meta.ASSETS and writes
SHA256SUMS.txt and release-manifest.json. ``notes`` prefers curated notes in
docs/releases/<version>.md, otherwise the version's CHANGELOG.md section, and
adds downloads, status, verification and build details. See docs/RELEASING.md.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import changelog as changelog_tool
from release_meta import (ASSETS, CHANGELOG, CURATED_NOTES, ROOT, TAG_PREFIX, Version, asset_descriptions,
                          asset_names, read_version, release_title, repository_url, tag_for, version_from_tag)

# Which build-job artifact file becomes which published asset. Patterns are
# matched against file names inside the downloaded artifact folders.
ARTIFACT_PATTERNS = {
    "windows-installer": ["*-setup.exe"],
    "windows-portable": ["vibestudio-*-win64-x86_64.zip"],
    "windows-runtime-source": ["vibestudio-*-win64-x86_64-source.zip"],
    "macos-dmg": ["*.dmg"],
    "linux-appimage": ["*.AppImage"],
    "docs": ["*-docs.zip"],
    "source": ["*-source.tar.gz"],
}
REQUIRED_ROLES = ["windows-installer", "windows-portable", "macos-dmg", "linux-appimage", "docs", "source"]


def git(*args: str) -> str:
    result = subprocess.run(["git", *args], cwd=ROOT, text=True, capture_output=True, check=False,
                            encoding="utf-8", errors="replace")
    return result.stdout.strip() if result.returncode == 0 else ""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


# ---------------------------------------------------------------------------
# metadata
# ---------------------------------------------------------------------------
def resolve(event: str, ref: str, publish: bool, sha: str | None = None) -> dict[str, str]:
    """What this workflow run builds and whether it publishes."""
    version = read_version()
    if version.build:
        raise ValueError("VERSION must not carry build metadata.")
    tag = tag_for(version)
    commit = (sha or os.environ.get("GITHUB_SHA") or git("rev-parse", "HEAD") or "unknown")
    if event == "push" and ref.startswith("refs/tags/"):
        tagged = version_from_tag(ref)
        if str(tagged) != str(version):
            raise ValueError(f"Tag {ref.removeprefix('refs/tags/')} does not match VERSION ({version}); "
                             f"push {tag} or update VERSION first.")
        publish = True
    label = str(version) if publish else f"{version}+{dt.date.today():%Y%m%d}.{commit[:8]}"
    return {
        "version": str(version),
        "label": label,
        "file_label": label.replace("+", "-"),
        "tag": tag,
        "title": release_title(version),
        "prerelease": "true" if version.is_prerelease else "false",
        "publish": "true" if publish else "false",
        "commit": commit,
        "update_channel": "beta" if version.is_prerelease else "stable",
    }


# ---------------------------------------------------------------------------
# stage
# ---------------------------------------------------------------------------
def find_artifacts(root: Path) -> dict[str, Path]:
    found: dict[str, Path] = {}
    files = sorted(p for p in root.rglob("*") if p.is_file())
    for role, patterns in ARTIFACT_PATTERNS.items():
        matches = [p for p in files if any(p.match(pattern) for pattern in patterns)]
        if role == "windows-portable":
            matches = [p for p in matches if not p.name.endswith("-source.zip")]
        if role == "source":
            matches = [p for p in matches if "runtime" not in p.name]
        if len(matches) > 1:
            raise ValueError(f"Several files could be the {role} asset: {', '.join(m.name for m in matches)}")
        if matches:
            found[role] = matches[0]
    return found


def stage(artifacts: Path, output: Path, label: str, allow_missing: bool = False) -> dict:
    names = asset_names(label)
    found = find_artifacts(artifacts)
    missing = [role for role in REQUIRED_ROLES if role not in found]
    if missing and not allow_missing:
        raise ValueError("Missing release assets: " + ", ".join(missing))
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    descriptions = asset_descriptions()
    manifest = {"schemaVersion": 1, "project": "VibeStudio", "version": label,
                "createdUtc": dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
                "commit": os.environ.get("GITHUB_SHA") or git("rev-parse", "HEAD"), "assets": []}
    lines = []
    for role in ASSETS:
        source = found.get(role)
        if source is None:
            continue
        destination = output / names[role]
        shutil.copy2(source, destination)
        digest = sha256(destination)
        lines.append(f"{digest}  {destination.name}")
        manifest["assets"].append({"role": role, "file": destination.name, "description": descriptions[role],
                                   "bytes": destination.stat().st_size, "sha256": digest})
    (output / "SHA256SUMS.txt").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    (output / "release-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    return manifest


# ---------------------------------------------------------------------------
# notes
# ---------------------------------------------------------------------------
def previous_tag(current: str) -> str:
    tags = [t for t in git("tag", "--list", f"{TAG_PREFIX}*", "--sort=-v:refname").splitlines() if t and t != current]
    candidates = []
    target = version_from_tag(current) if current else None
    for tag in tags:
        try:
            version = version_from_tag(tag)
        except ValueError:
            continue
        if target is None or version.sort_key() < target.sort_key():
            candidates.append((version.sort_key(), tag))
    return max(candidates)[1] if candidates else ""


def commit_lines(since: str, until: str) -> list[str]:
    span = f"{since}..{until}" if since else until
    log = git("log", "--no-merges", "--pretty=format:%h%x09%s", span)
    lines = []
    for row in log.splitlines()[:200]:
        short, _, subject = row.partition("\t")
        lines.append(f"- {subject} ({short})")
    return lines


def curated_notes(version: Version) -> str:
    path = CURATED_NOTES / f"{version}.md"
    if not path.is_file():
        return ""
    text = path.read_text(encoding="utf-8").strip()
    lines = text.splitlines()
    if lines and lines[0].startswith("# "):
        lines = lines[1:]
    return "\n".join(lines).strip()


def downloads_table(label: str, manifest: dict | None) -> list[str]:
    names = asset_names(label)
    sizes = {a["file"]: a["bytes"] for a in (manifest or {}).get("assets", [])}
    rows = ["| Download | For |", "| --- | --- |"]
    for role, description in asset_descriptions().items():
        name = names[role]
        if manifest is not None and name not in sizes:
            continue
        size = f" ({sizes[name] / (1 << 20):.1f} MB)" if name in sizes else ""
        rows.append(f"| `{name}`{size} | {description} |")
    return rows


def render_notes(version: Version, label: str, commit: str, manifest: dict | None, prerelease: bool) -> str:
    tag = tag_for(version)
    body = curated_notes(version)
    if not body:
        log = changelog_tool.load(CHANGELOG)
        section = log.section(str(version)) or log.section(changelog_tool.UNRELEASED)
        entries = changelog_tool.notes_text(section).strip() if section else ""
        body = "## What's changed\n\n" + (entries.replace("### ", "#### ") if entries else "- No changelog entries were recorded.")
    out = [body, ""]
    if prerelease:
        out += [
            "> [!WARNING]",
            "> VibeStudio is pre-alpha and largely untested in real projects. Work on copies of your maps and",
            "> packages, keep backups, and please report problems. See the",
            f"> [project status]({repository_url(f'/blob/{tag}/docs/manual/status.md')}) page for known limitations.",
            "",
        ]
    out += ["## Downloads", ""] + downloads_table(label, manifest) + [""]
    out += [
        "Builds are not code-signed yet: Windows SmartScreen and macOS Gatekeeper will ask before the first launch. "
        f"The [installation guide]({repository_url(f'/blob/{tag}/docs/manual/install.md')}) explains each step.",
        "",
        "## Verify your download",
        "",
        "```sh",
        "sha256sum -c SHA256SUMS.txt --ignore-missing      # Linux",
        "shasum -a 256 -c SHA256SUMS.txt --ignore-missing  # macOS",
        "```",
        "",
        "On Windows, compare `Get-FileHash <file> -Algorithm SHA256` with the matching line in `SHA256SUMS.txt`.",
        "",
    ]
    since = previous_tag(tag)
    commits = commit_lines(since, commit if commit and commit != "unknown" else "HEAD")
    run_url = ""
    if os.environ.get("GITHUB_RUN_ID"):
        run_url = repository_url(f"/actions/runs/{os.environ['GITHUB_RUN_ID']}")
    out += ["## Build details", "",
            f"- Version: `{label}`",
            f"- Commit: `{commit}`",
            f"- Previous release: `{since}`" if since else "- Previous release: none (first release)"]
    if run_url:
        out.append(f"- Built by: [release workflow run]({run_url})")
    out.append(f"- Full changelog: [CHANGELOG.md]({repository_url(f'/blob/{tag}/CHANGELOG.md')})")
    if commits:
        out += ["", "<details>", f"<summary>Commits in this release ({len(commits)})</summary>", ""] + commits + ["", "</details>"]
    return "\n".join(out).rstrip() + "\n"


# ---------------------------------------------------------------------------
def main() -> int:
    parser = argparse.ArgumentParser(description="Release workflow helpers.")
    commands = parser.add_subparsers(dest="command", required=True)
    meta = commands.add_parser("metadata", help="Print key=value release metadata for $GITHUB_OUTPUT.")
    meta.add_argument("--event", default=os.environ.get("GITHUB_EVENT_NAME", "workflow_dispatch"))
    meta.add_argument("--ref", default=os.environ.get("GITHUB_REF", ""))
    meta.add_argument("--publish", default="false", help="'true' to publish a dispatched run.")
    meta.add_argument("--sha", default=None)
    st = commands.add_parser("stage", help="Collect build artifacts under their published names.")
    st.add_argument("--artifacts", type=Path, required=True)
    st.add_argument("--output", type=Path, required=True)
    st.add_argument("--label", help="Version label for file names (defaults to VERSION).")
    st.add_argument("--allow-missing", action="store_true", help="Stage whatever exists (dry runs).")
    nt = commands.add_parser("notes", help="Write the GitHub release notes.")
    nt.add_argument("--label", help="Version label shown in the notes (defaults to VERSION).")
    nt.add_argument("--commit", default=os.environ.get("GITHUB_SHA") or "")
    nt.add_argument("--manifest", type=Path, help="release-manifest.json from 'stage', for sizes.")
    nt.add_argument("--output", type=Path)
    args = parser.parse_args()

    try:
        if args.command == "metadata":
            info = resolve(args.event, args.ref, args.publish.strip().lower() == "true", args.sha)
            for key, value in info.items():
                print(f"{key}={value}")
            return 0
        version = read_version()
        if args.command == "stage":
            manifest = stage(args.artifacts, args.output, args.label or str(version), args.allow_missing)
            for asset in manifest["assets"]:
                print(f"{asset['file']}  {asset['bytes']} bytes")
            return 0
        if args.command == "notes":
            manifest = json.loads(args.manifest.read_text(encoding="utf-8")) if args.manifest else None
            label = args.label or str(version)
            commit = args.commit or git("rev-parse", "HEAD")
            text = render_notes(version, label, commit, manifest, version.is_prerelease)
            if args.output:
                args.output.parent.mkdir(parents=True, exist_ok=True)
                args.output.write_text(text, encoding="utf-8", newline="\n")
            else:
                sys.stdout.write(text)
            return 0
    except (ValueError, OSError) as error:
        print(f"release: {error}", file=sys.stderr)
        return 1
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
