#!/usr/bin/env python3
"""Docs-claims check: the qmg_docs_claims CTest (stdlib only).

  python -B tools/benchmark/docs_claims.py [--claims tools/benchmark/docs-claims.json] [--root <repo>]

Evaluates every claim in tools/benchmark/docs-claims.json (X3 truth patch and H6 protocol; plan
docs/RC_PHASE1_AUDIT_AND_PLAN.md 9.10, 9.12 and the Phase-8 exit "qmg_rc_ledger and qmg_docs_claims green")
with the semantics stated in the file's own `semantics` block:

  * `file` is read as UTF-8 with CRLF normalized to LF; `expect` 'match' needs re.search(regex) to find a match,
    'no_match' needs it to find none;
  * `count_source`: the match's named group `count` equals the length of the JSON array at `json_pointer`;
  * `controls`: every controls.match string matches the regex and every controls.no_match string does not,
    and both lists are non-empty, so each regex is shown able to pass and to fail.

The structure is checked too: schema `qmg.docs_claims` version 1, dialect python-re, unique ids, a relative path
inside the repository, `reason` and `source` present. Before the real check, a built-in self-test runs the same
evaluator on synthetic claims and must see each failure kind rejected. Exit 0 when everything holds, else 1.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path, PurePosixPath

REPO = Path(__file__).resolve().parents[2]
DEFAULT_CLAIMS = REPO / "tools" / "benchmark" / "docs-claims.json"
EXPECTS = {"match", "no_match"}


def json_pointer(document, pointer: str):
    if pointer in ("", "/"):
        return document
    for part in pointer.lstrip("/").split("/"):
        part = part.replace("~1", "/").replace("~0", "~")
        document = document[int(part)] if isinstance(document, list) else document[part]
    return document


def structure_problems(spec: dict) -> list:
    problems = []
    if spec.get("schema") != "qmg.docs_claims" or spec.get("schema_version") != 1:
        problems.append("schema must be qmg.docs_claims version 1")
    if spec.get("regex_dialect") != "python-re":
        problems.append("regex_dialect must be python-re")
    claims = spec.get("claims")
    if not isinstance(claims, list) or not claims:
        return problems + ["claims must be a non-empty array"]
    seen = set()
    for index, claim in enumerate(claims):
        where = f"claims[{index}]"
        cid = claim.get("id")
        if not isinstance(cid, str) or not cid:
            problems.append(f"{where}: missing id")
        elif cid in seen:
            problems.append(f"{where}: duplicate id {cid}")
        seen.add(cid)
        name = claim.get("file")
        if not isinstance(name, str) or not name or "\\" in name or PurePosixPath(name).is_absolute() \
                or ".." in PurePosixPath(name).parts:
            problems.append(f"{cid or where}: file must be a repository-relative POSIX path")
        if claim.get("expect") not in EXPECTS:
            problems.append(f"{cid or where}: expect must be one of {sorted(EXPECTS)}")
        for key in ("reason", "source"):
            if not isinstance(claim.get(key), str) or not claim.get(key):
                problems.append(f"{cid or where}: missing {key}")
        try:
            compiled = re.compile(claim.get("regex", ""))
        except (re.error, TypeError) as error:
            problems.append(f"{cid or where}: regex does not compile: {error}")
            compiled = None
        if not claim.get("regex"):
            problems.append(f"{cid or where}: empty regex")
        if "count_source" in claim:
            source = claim["count_source"]
            if not isinstance(source, dict) or not source.get("file") or "json_pointer" not in source:
                problems.append(f"{cid or where}: count_source needs file and json_pointer")
            if claim.get("expect") != "match":
                problems.append(f"{cid or where}: count_source needs expect 'match'")
            if compiled is not None and "count" not in compiled.groupindex:
                problems.append(f"{cid or where}: count_source needs a named group 'count'")
        controls = claim.get("controls")
        if not isinstance(controls, dict) or not isinstance(controls.get("match"), list) \
                or not isinstance(controls.get("no_match"), list):
            problems.append(f"{cid or where}: controls.match and controls.no_match must be arrays")
    return problems


def control_problems(claim: dict) -> list:
    regex = re.compile(claim["regex"])
    controls = claim.get("controls") or {}
    positives, negatives = controls.get("match") or [], controls.get("no_match") or []
    problems = []
    if not positives or not negatives:
        problems.append("needs at least one control in each of controls.match and controls.no_match")
    problems += [f"control {text!r} should match" for text in positives if not regex.search(text)]
    problems += [f"control {text!r} should not match" for text in negatives if regex.search(text)]
    return problems


def evaluate(claim: dict, read_text, read_json) -> str:
    """'' when the claim holds, else the reason."""
    try:
        text = read_text(claim["file"]).replace("\r\n", "\n")
    except (OSError, UnicodeDecodeError) as error:
        return f"cannot read {claim['file']}: {error}"
    match = re.search(claim["regex"], text)
    if claim["expect"] == "no_match":
        if match:
            line = text.count("\n", 0, match.start()) + 1
            return f"unexpected match at line {line}: {match.group(0)[:90]!r}"
        return ""
    if not match:
        return "no match"
    if "count_source" in claim:
        source = claim["count_source"]
        try:
            expected = len(json_pointer(read_json(source["file"]), source["json_pointer"]))
        except (OSError, ValueError, KeyError, IndexError, TypeError) as error:
            return f"count source {source['file']}{source['json_pointer']} unreadable: {error}"
        got = int(match.group("count"))
        if got != expected:
            return f"count {got} != {expected} (length of {source['file']}{source['json_pointer']})"
    return ""


def check(spec: dict, read_text, read_json) -> list:
    problems = structure_problems(spec)
    if problems:
        return problems
    failures = []
    for claim in spec["claims"]:
        reasons = control_problems(claim)
        why = evaluate(claim, read_text, read_json)
        if why:
            reasons.insert(0, why)
        if reasons:
            failures.append(f"{claim['id']} ({claim['file']}): " + "; ".join(reasons))
    return failures


def self_test() -> list:
    """Each synthetic spec must give the stated outcome; returns the self-test failures."""
    docs = {"doc.md": "# Title\r\nNo human playtest has been done.\nThere are 3 cards.\n",
            "cards.json": json.dumps({"cards": [1, 2, 3]})}

    def read_text(name):
        if name not in docs:
            raise OSError("missing")
        return docs[name]

    def read_json(name):
        return json.loads(read_text(name))

    def claim(**overrides):
        base = {"id": "c", "file": "doc.md", "expect": "match", "regex": r"No human playtest has been done\.",
                "reason": "r", "source": "s", "controls": {"match": ["No human playtest has been done."],
                                                           "no_match": ["Playtests were done."]}}
        base.update(overrides)
        return base

    def spec(*claims, **overrides):
        value = {"schema": "qmg.docs_claims", "schema_version": 1, "regex_dialect": "python-re",
                 "claims": list(claims)}
        value.update(overrides)
        return value

    count = claim(id="n", regex=r"There are (?P<count>\d+) cards\.",
                  count_source={"file": "cards.json", "json_pointer": "/cards"},
                  controls={"match": ["There are 7 cards."], "no_match": ["There are many cards."]})
    cases = [
        ("a holding match claim passes", spec(claim()), True),
        ("CRLF is normalized before matching", spec(claim(regex=r"(?m)^# Title$",
                                                          controls={"match": ["# Title"], "no_match": ["# T"]})), True),
        ("a holding count claim passes", spec(count), True),
        ("a missing statement fails", spec(claim(regex=r"Playtests were done\.",
                                                 controls={"match": ["Playtests were done."],
                                                           "no_match": ["x"]})), False),
        ("a forbidden statement fails", spec(claim(expect="no_match")), False),
        ("a count mismatch fails", spec(dict(count, count_source={"file": "cards.json", "json_pointer": ""},
                                             regex=r"There are (?P<count>\d+) cards\.")), False),
        ("a control that should match but does not fails",
         spec(claim(controls={"match": ["something else"], "no_match": ["x"]})), False),
        ("a control that should not match but does fails",
         spec(claim(controls={"match": ["No human playtest has been done."],
                              "no_match": ["No human playtest has been done."]})), False),
        ("an empty control list fails", spec(claim(controls={"match": [], "no_match": ["x"]})), False),
        ("a duplicate id fails", spec(claim(), claim()), False),
        ("a missing file fails", spec(claim(file="absent.md")), False),
        ("a path outside the repository fails", spec(claim(file="../doc.md")), False),
        ("a regex that does not compile fails", spec(claim(regex="(")), False),
        ("count_source without a count group fails",
         spec(claim(count_source={"file": "cards.json", "json_pointer": "/cards"})), False),
        ("a wrong schema fails", spec(claim(), schema="other"), False),
        ("an unknown expect fails", spec(claim(expect="maybe")), False),
    ]
    failures = []
    for name, value, should_pass in cases:
        try:
            passed = not check(value, read_text, read_json)
        except Exception as error:  # an exception is a self-test failure, never a pass
            failures.append(f"{name}: raised {type(error).__name__}: {error}")
            continue
        status = "ok" if passed == should_pass else "FAIL"
        print(f"[{status}] self-test: {name}")
        if passed != should_pass:
            failures.append(name)
    print(f"self-test: {len(cases) - len(failures)}/{len(cases)} checks passed")
    return failures


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--claims", type=Path, default=DEFAULT_CLAIMS)
    parser.add_argument("--root", type=Path, default=REPO)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    failures = self_test()

    def read_text(name):
        return (root / name).read_text(encoding="utf-8")

    def read_json(name):
        return json.loads(read_text(name))

    spec = json.loads(args.claims.read_text(encoding="utf-8"))
    problems = check(spec, read_text, read_json)
    for problem in problems:
        print(f"FAIL {problem}")
    claims = spec.get("claims") or []
    by_file = {}
    for claim in claims if isinstance(claims, list) else []:
        by_file[claim.get("file")] = by_file.get(claim.get("file"), 0) + 1
    files = ", ".join(f"{name} {count}" for name, count in sorted(by_file.items(), key=lambda kv: str(kv[0])))
    print(f"docs claims: {len(claims) - len(problems)}/{len(claims)} hold with their controls ({files})")
    return 1 if failures or problems else 0


if __name__ == "__main__":
    sys.exit(main())
