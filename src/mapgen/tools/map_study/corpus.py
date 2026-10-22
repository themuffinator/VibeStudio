"""Local provenance registry, leakage-aware study splits, and empirical reference profiles."""

from __future__ import annotations

import json
import os
import re
import stat
from collections import Counter, defaultdict
from pathlib import Path

from . import EXTRACTOR_VERSION
from .binary import Bsp, FormatError, decode, entities, read_bytes, sha256
from .features import describe


def json_bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")


def write_json(path, value):
    path.write_bytes(json_bytes(value))


def is_link(path):
    info = path.lstat()
    return path.is_symlink() or bool(getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT)


def inventory(root):
    files, skipped = [], []
    for directory, dirs, names in os.walk(root, followlinks=False):
        for name in sorted(dirs + names):
            path = Path(directory) / name
            if is_link(path):
                skipped.append({"path": path.relative_to(root).as_posix(), "error": "link/reparse point excluded"})
                if name in dirs:
                    dirs.remove(name)
            elif name in names:
                files.append(path)
        dirs.sort()
    return sorted(files, key=lambda p: p.relative_to(root).as_posix().casefold()), skipped


def metadata(data):
    """The archive's .ref files often have a broken score_level line.

    Parse complete individual key/value lines and preserve an issue for every other
    non-comment line. This deliberately tolerant policy NEVER applies to entities.
    """
    fields, issues = {}, []
    pattern = re.compile(r'^\s*"([^"\r\n]+)"\s+"([^"\r\n]*)"\s*(?://.*)?$')
    for number, line in enumerate(decode(data).splitlines(), 1):
        if not line.strip() or line.strip() in ("{", "}") or line.lstrip().startswith("//"):
            continue
        match = pattern.fullmatch(line)
        if not match:
            issues.append(f"malformed metadata line {number}")
        elif match[1] in fields:
            issues.append(f"duplicate metadata key {match[1]} at line {number}")
        else:
            fields[match[1]] = match[2].strip()
    return fields, issues


def normalized(text):
    return re.sub(r"[^a-z0-9]+", "", text.casefold())


def provenance(path, root, package_files, bsp):
    maps_dir = path.parent
    pack_files = package_files
    relatives = [p for p in pack_files if p.parent == maps_dir]
    refs = [p for p in pack_files if p.suffix.casefold() in (".ref", ".mapref")]
    matching = [p for p in refs if p.stem.casefold() == path.stem.casefold()]
    issues = list(bsp.entity_issues) + list(bsp.file_issues)
    if not matching and len(refs) == 1:
        matching = refs
        issues.append("metadata filename differs from BSP; sole package reference used")
    if len(matching) > 1:
        issues.append("ambiguous metadata; first sorted reference used, review required")
    fields, evidence = {}, []
    for ref in matching[:1]:
        data = read_bytes(ref, 1024 * 1024)
        fields, problems = metadata(data)
        issues.extend(problems)
        evidence.append({"path": ref.relative_to(root).as_posix(), "sha256": sha256(data), "kind": "metadata"})
    docs = [p for p in pack_files if p.suffix.casefold() in (".txt", ".nfo") and p.parent.name.casefold() == "docs"]
    named_docs = [p for p in docs if p.stem.casefold() in (path.stem.casefold(), path.stem.casefold() + "-readme")]
    docs = named_docs or (docs if len(docs) == 1 else [])
    readme_author = None
    for doc in docs:
        data = read_bytes(doc, 1024 * 1024)
        text = decode(data)
        evidence.append({"path": doc.relative_to(root).as_posix(), "sha256": sha256(data), "kind": "readme"})
        match = re.search(r"^\s*Author\s*:\s*(.+)$", text, re.M | re.I)
        if match:
            readme_author = match[1].strip()[:200]
    authors = [fields[k] for k in ("original_author", "original_author2") if fields.get(k)]
    if not authors and readme_author:
        authors = [readme_author]
    if not authors:
        issues.append("author unknown")
    sidecars = []
    for item in relatives:
        if item.stem.casefold() != path.stem.casefold() or item.suffix.casefold() not in (".map", ".ent", ".aas"):
            continue
        data = read_bytes(item)
        sidecar = {"path": item.relative_to(root).as_posix(), "sha256": sha256(data), "kind": item.suffix[1:].lower()}
        if sidecar["kind"] == "map":
            sidecar["authored_source_verified"] = False
        if sidecar["kind"] == "ent":
            try:
                parsed = entities(data)
                sidecar["matches_embedded_entities"] = parsed == bsp.entities
                if parsed != bsp.entities:
                    issues.append("entity sidecar differs; embedded BSP entities remain authoritative for this run")
            except FormatError as error:
                sidecar["parse_error"] = str(error)
                issues.append("entity sidecar cannot be parsed")
        sidecars.append(sidecar)
    def integer(key):
        try:
            result = int(fields.get(key, ""))
            return result if 1 <= result <= 128 else None
        except ValueError:
            return None
    title = fields.get("map_name") or bsp.entities[0].get("message") or path.stem
    return {
        "id": path.relative_to(root).with_suffix("").as_posix(), "map": path.stem.casefold(),
        "bsp_path": path.relative_to(root).as_posix(), "bsp_sha256": bsp.sha256,
        "geometry_sha256": bsp.geometry_sha256, "title": title, "authors": authors,
        "attribution": f"{title} — {', '.join(authors) if authors else 'author unresolved'}",
        "game": "q2", "format": "IBSP38", "entity_policy": "embedded_stock_dm",
        "metadata": fields, "players_min": integer("players_min"), "players_max": integer("players_max"),
        "modes": fields.get("gametype", "").casefold().split(), "themes": fields.get("theme", "").casefold().split(),
        "evidence_files": evidence, "sidecars": sidecars, "issues": issues,
        "rights": {"status": "unreviewed_local_reference", "ml_training_allowed": None,
                   "modification_allowed": None, "redistribution_allowed": None,
                   "licence_identifier": None, "licence_evidence_url": None,
                   "canonical_source_url": None, "retrieved_at": None,
                   "training_eligible": False, "raw_retrieval_eligible": False},
    }


def assign_splits(records, curriculum):
    """Union by author, named lineage and exact geometry BEFORE assigning partitions."""
    parents = list(range(len(records)))
    def find(index):
        while parents[index] != index:
            parents[index] = parents[parents[index]]
            index = parents[index]
        return index
    def union(a, b):
        a, b = find(a), find(b)
        parents[max(a, b)] = min(a, b)
    seen = {}
    manual = {}
    for i, family in enumerate(curriculum.get("families", [])):
        for name in family:
            manual[normalized(name)] = f"curated_family_{i}"
    for i, record in enumerate(records):
        tokens = ["geometry:" + record["geometry_sha256"], "family:" + normalized(record["map"])]
        names = [record["map"], record["metadata"].get("map_base", "")]
        for name in filter(None, names):
            tokens.append("family:" + normalized(name))
            if normalized(name) in manual:
                tokens.append(manual[normalized(name)])
        for author in record["authors"]:
            tokens.append("author:" + normalized(author))
            for nickname in re.findall(r"['\"]([^'\"]+)['\"]", author):
                tokens.append("nickname:" + normalized(nickname))
        for token in tokens:
            if token in seen:
                union(i, seen[token])
            else:
                seen[token] = i
    groups = defaultdict(list)
    for i in range(len(records)):
        groups[find(i)].append(i)
    pins = {}
    for lesson in curriculum.get("lessons", []):
        for name in lesson["maps"]:
            partition = lesson.get("partition", "reference")
            if name in pins and pins[name] != partition:
                raise ValueError(f"conflicting curriculum partitions for {name}")
            pins[name] = partition
    existing = {r["map"] for r in records}
    missing = sorted(pins.keys() - existing)
    if missing:
        raise ValueError("curriculum maps missing or failed to parse: " + ", ".join(missing))
    for members in groups.values():
        group_id = sha256("\n".join(sorted(records[i]["id"] for i in members)).encode())[:16]
        partitions = {pins[records[i]["map"]] for i in members if records[i]["map"] in pins}
        if len(partitions) > 1:
            raise ValueError("author/family split conflict: " + ", ".join(records[i]["map"] for i in members))
        default = "reference" if int(group_id[:8], 16) % 10 < 8 else ("validation" if int(group_id[:8], 16) % 10 == 8 else "test")
        partition = next(iter(partitions), default)
        if any(not records[i]["authors"] for i in members):
            partition = "study_only"
        for i in members:
            records[i]["split_group"] = group_id
            records[i]["partition"] = partition
            records[i]["split_review"] = "provisional; author aliases and unrecorded remakes require human review"


def cohorts(record):
    result = []
    modes = record["modes"]
    if "duel" in modes:
        result.append("duel")
    if "ctf" in modes:
        result.append("ctf")
    if any(m in modes for m in ("ffa", "tdm", "2v2")) and record["players_max"]:
        capacity = record["players_max"]
        result.append("dm_small" if capacity <= 6 else "dm_medium" if capacity <= 12 else "dm_large")
    return result


def build_profile(records, manifest_sha):
    buckets = defaultdict(list)
    for record in records:
        if record["partition"] == "reference" and record.get("profile_eligible", True) and record.get("analysis"):
            for cohort in cohorts(record):
                buckets[cohort].append(record)
    summaries = {}
    for cohort, rows in sorted(buckets.items()):
        groups = defaultdict(list)
        for row in rows:
            groups[row["split_group"]].append(row)
        metrics = {}
        for key in sorted(rows[0]["analysis"]["metrics"]):
            medians = []
            for members in groups.values():
                values = [r["analysis"]["metrics"][key] for r in members if r["analysis"]["metrics"][key] is not None]
                if values:
                    medians.append(describe(values)["p50"])
            metrics[key] = {**describe(medians), "group_medians": sorted(medians)}
        summaries[cohort] = {"maps": len(rows), "author_family_groups": len(groups),
                             "usable_for_comparison": len(groups) >= 5, "metrics": metrics,
                             "reference_ids": sorted(r["id"] for r in rows)}
    return {"schema_version": 1, "extractor_version": EXTRACTOR_VERSION, "game": "q2",
            "entity_policy": "embedded_stock_dm", "manifest_sha256": manifest_sha,
            "purpose": "local descriptive comparison, not a learned quality model or training authorization",
            "trained_model": False, "held_out_used": False,
            "weighting": "one median per author/family component; at least five components for comparison",
            "cohorts": summaries}
