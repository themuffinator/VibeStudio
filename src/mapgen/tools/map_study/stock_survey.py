"""Local-only coverage survey of Quake (BSP29/BSP2) and Quake III (IBSP46) maps.

Writes one descriptive JSON record per map plus a coverage table (parsed /
rejected with reason). Records hold counts, bounds, histograms and hashes only:
never geometry, entity lumps, lightmaps or texture pixels. Every record carries
its provenance class and training_eligible=false. Output is deterministic (no
timestamps, sorted keys and paths) and belongs under out/, which is not tracked.

    python -B -m tools.map_study.stock_survey --game q1 \\
        --source stock-id/q1/id1=E:/_SOURCE/_ASSETS/Q1/maps --out out/map-study-q1
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import struct
import sys
from collections import Counter
from pathlib import Path

from .binary import FormatError, read_bytes, sha256
from .bsp29 import Q1Bsp
from .corpus import is_link
from .ibsp46 import Q3Bsp

SCHEMA = "qmg-map-study-q1q3-survey-v1"
READERS = {"q1": Q1Bsp, "q3": Q3Bsp}
MODULES = ("binary.py", "bsp29.py", "ibsp46.py", "stock_survey.py")
PROVENANCE = {
    "stock-id": {"publisher": "id Software", "designer": None, "designer_documented": False,
                 "licence": "commercial EULA, local reference only"},
    "project-fixture": {"publisher": "Quake-MapGen project", "designer": None, "designer_documented": False,
                        "licence": "project-authored synthetic fixture"},
    "project-generated": {"publisher": "Quake-MapGen project", "designer": None, "designer_documented": False,
                          "licence": "project generator output"},
}


def script_hashes():
    here = Path(__file__).resolve().parent
    return {name: sha256((here / name).read_bytes()) for name in MODULES}


def provenance(kind, group):
    record = {"class": kind, "group": group, **PROVENANCE[kind]}
    record.update({"training_eligible": False, "raw_retrieval_eligible": False})
    return record


def lit_sidecar(path, lighting_bytes):
    """Header-only check of a coloured-light .lit sidecar: 'QLIT', version 1, 3 bytes per sample."""
    lit = path.with_suffix(".lit")
    if not lit.is_file() or is_link(lit):
        return {"present": False}
    size = lit.stat().st_size
    with lit.open("rb") as stream:
        header = stream.read(8)
    magic, version = (header[:4], struct.unpack_from("<i", header, 4)[0]) if len(header) == 8 else (b"", None)
    return {"present": True, "bytes": size, "magic_ok": magic == b"QLIT", "version": version,
            "consistent": magic == b"QLIT" and version == 1 and size == 8 + 3 * lighting_bytes}


def discover(root, include):
    files = []
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root).as_posix()
        if path.suffix.lower() != ".bsp" or not fnmatch.fnmatch(relative, include):
            continue
        if is_link(path) or not path.is_file():
            continue
        files.append((relative, path))
    return files


def survey_file(game, kind, label, relative, path):
    parent = relative.rsplit("/", 1)[0] if "/" in relative else ""
    group = f"{label}/{parent}" if parent else label
    stem = relative.rsplit("/", 1)[-1][:-4]
    row = {"map_id": f"{group}/{stem}", "group": group, "relative_path": relative, "class": kind}
    try:
        data = read_bytes(path)
    except (OSError, FormatError) as error:
        row.update({"status": "rejected", "reason": f"read: {error}", "sha256": None, "bytes": None, "format": None})
        return row, None
    row.update({"sha256": sha256(data), "bytes": len(data)})
    try:
        bsp = READERS[game](data)
        summary = bsp.summary()
    except FormatError as error:
        row.update({"status": "rejected", "reason": str(error), "format": None})
        return row, None
    except Exception as error:  # reader defect: surfaced separately from format rejections
        row.update({"status": "error", "reason": f"{type(error).__name__}: {error}", "format": None})
        return row, None
    if game == "q1":
        summary["lighting"]["lit_sidecar"] = lit_sidecar(path, summary["lighting"]["bytes"])
    row.update({"status": "parsed", "reason": None, "format": summary["format"],
                "issues": sum(summary["issue_counts"].values())})
    record = {"schema": SCHEMA, "map_id": row["map_id"], "game": game,
              "source": {"group": group, "relative_path": relative},
              "sha256": row["sha256"], "bytes": row["bytes"], "provenance": provenance(kind, group),
              "summary": summary}
    return row, record


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8",
                    newline="\n")


def markdown(game, totals, groups, rows):
    lines = [f"# Map study coverage: {game}", "",
             "Local-only output. Counts and hashes only; no geometry, entity lumps, lightmaps or pixels.",
             "Every record: training_eligible=false. Evidence level: static analysis.", "",
             f"Total {totals['files']}: parsed {totals['parsed']}, rejected {totals['rejected']}, "
             f"reader errors {totals['error']}.", "",
             "| Group | Class | Files | Parsed | Rejected | Errors | Formats |", "|---|---|---|---|---|---|---|"]
    for name, group in groups.items():
        formats = ", ".join(f"{k} {v}" for k, v in group["formats"].items()) or "-"
        lines.append(f"| {name} | {group['class']} | {group['files']} | {group['parsed']} | {group['rejected']} | "
                     f"{group['error']} | {formats} |")
    lines += ["", "| Map | Format | SHA-256 (12) | Status | Issues | Reason |", "|---|---|---|---|---|---|"]
    for row in rows:
        digest = (row["sha256"] or "")[:12]
        lines.append(f"| {row['map_id']} | {row['format'] or '-'} | {digest} | {row['status']} | "
                     f"{row.get('issues', '-')} | {row['reason'] or ''} |")
    return "\n".join(lines) + "\n"


def survey(game, sources, out_dir, kind="stock-id", include="*.bsp"):
    """sources: list of (label, root). Returns the coverage document (also written to out_dir)."""
    if game not in READERS:
        raise ValueError(f"unknown game {game!r}")
    if kind not in PROVENANCE:
        raise ValueError(f"unknown provenance class {kind!r}")
    rows = []
    source_rows = []
    for label, root in sources:
        if not label or any(part in ("", ".", "..") for part in label.split("/")):
            raise ValueError(f"invalid source label {label!r}")
        files = discover(Path(root), include)
        source_rows.append({"label": label, "root": Path(root).as_posix(), "files": len(files)})
        for relative, path in files:
            row, record = survey_file(game, kind, label, relative, path)
            rows.append(row)
            if record is not None:
                write_json(Path(out_dir) / "maps" / f"{row['map_id']}.json", record)
    rows.sort(key=lambda r: r["map_id"])
    statuses = Counter(r["status"] for r in rows)
    totals = {"files": len(rows), "parsed": statuses["parsed"], "rejected": statuses["rejected"],
              "error": statuses["error"]}
    groups = {}
    for row in rows:
        group = groups.setdefault(row["group"], {"class": row["class"], "files": 0, "parsed": 0, "rejected": 0,
                                                 "error": 0, "formats": Counter()})
        group["files"] += 1
        group[row["status"]] += 1
        if row["format"]:
            group["formats"][row["format"]] += 1
    groups = {name: {**g, "formats": dict(sorted(g["formats"].items()))} for name, g in sorted(groups.items())}
    coverage = {"schema": SCHEMA, "game": game, "provenance_class": kind, "training_eligible": False,
                "evidence_level": "static analysis", "script_sha256": script_hashes(), "sources": source_rows,
                "include": include, "totals": totals, "groups": groups, "rows": rows}
    write_json(Path(out_dir) / "coverage.json", coverage)
    (Path(out_dir) / "coverage.md").write_text(markdown(game, totals, groups, rows), encoding="utf-8", newline="\n")
    return coverage


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game", choices=sorted(READERS), required=True)
    parser.add_argument("--source", action="append", required=True, metavar="LABEL=PATH",
                        help="group label and directory scanned recursively for .bsp files")
    parser.add_argument("--class", dest="kind", choices=sorted(PROVENANCE), default="stock-id")
    parser.add_argument("--include", default="*.bsp", help="fnmatch pattern on the path relative to each source")
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)
    sources = []
    for value in args.source:
        label, separator, root = value.partition("=")
        if not separator or not label or not Path(root).is_dir():
            parser.error(f"--source expects LABEL=DIRECTORY: {value}")
        sources.append((label.strip("/"), root))
    coverage = survey(args.game, sources, args.out, args.kind, args.include)
    print(json.dumps(coverage["totals"], sort_keys=True))
    return 0 if coverage["totals"]["error"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
