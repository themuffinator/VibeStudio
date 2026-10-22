#!/usr/bin/env python3
"""RC2 parity rubric report from the RC ledger (stdlib only).

  python -B tools/benchmark/parity.py [--path q2-dm --path q2-duel ...] [--strict] [--tiers committed]

Prints, per path, the P1-P11 rubric items (D12 (c)) with their status and evidence pointers. Items recorded as
failing at baseline-0 (expected_at_baseline0) are listed as expected, with the exit by which they must close;
for Q2 DM these are P8, P10 and P11 (plan 9.1, rubric self-check).

--strict fails (exit 1) when any selected path in a claimed tier (default: committed) has a rubric item without
an evidence-backed pass. Without --strict the report never fails, except for the self-check below.

Self-check: the Q2 DM baseline-0 failing items recorded in the ledger must be exactly P8, P10 and P11; any other
set means the ledger and the plan disagree (exit 2).
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

BENCH = Path(__file__).resolve().parent
RUBRIC = [f"P{i}" for i in range(1, 12)]
EXPECTED_Q2DM_BASELINE0 = {"P8", "P10", "P11"}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ledger", type=Path, default=BENCH / "rc-ledger.json")
    parser.add_argument("--path", action="append", default=[])
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--tiers", default="committed", help="comma-separated claimed tiers checked by --strict")
    args = parser.parse_args(argv)
    ledger = json.loads(args.ledger.read_text(encoding="utf-8"))
    paths = ledger["paths"]
    selected = args.path or list(paths)
    unknown = [p for p in selected if p not in paths]
    if unknown:
        print(f"unknown path(s): {unknown}", file=sys.stderr)
        return 2
    tiers = {t.strip() for t in args.tiers.split(",") if t.strip()}
    strict_failures = []
    for name in selected:
        row = paths[name]
        items = row["parity"]
        backed = [k for k in RUBRIC if items[k]["status"] == "pass" and items[k].get("evidence")]
        expected = [k for k in RUBRIC if items[k].get("expected_at_baseline0")]
        failing = [k for k in RUBRIC if items[k]["status"] == "fail"]
        lacking = [k for k in RUBRIC if k not in backed]
        print(f"{name} (claimed {row['claimed']}, status {row['status']}): evidence-backed {len(backed)}/11; "
              f"failing {failing or 'none'}; lacking evidence {len(lacking)}")
        for key in RUBRIC:
            item = items[key]
            mark = " (expected at baseline-0; close by " + item.get("close_by", "?") + ")" \
                if item.get("expected_at_baseline0") else ""
            pointers = ", ".join(f"{e['path']} [{e['level']}]" for e in item.get("evidence") or []) or "-"
            print(f"  {key:<4} {item['status']:<11} {pointers}{mark}")
            if item.get("note") and item["status"] != "unassessed":
                print(f"         {item['note']}")
        if expected:
            print(f"  expected at baseline-0: {', '.join(expected)}")
        if row["claimed"] in tiers and lacking:
            strict_failures.append(f"{name}: {len(lacking)} rubric item(s) without evidence ({', '.join(lacking)})")
    status = 0
    q2dm = {k for k in RUBRIC if paths["q2-dm"]["parity"][k].get("expected_at_baseline0")}
    if q2dm != EXPECTED_Q2DM_BASELINE0:
        print(f"self-check FAILED: Q2 DM baseline-0 failing items recorded {sorted(q2dm)}, plan says "
              f"{sorted(EXPECTED_Q2DM_BASELINE0)}")
        status = 2
    else:
        print("self-check: Q2 DM baseline-0 failing items are P8, P10, P11, as the plan expects")
    if args.strict:
        for failure in strict_failures:
            print(f"STRICT: {failure}")
        print(f"strict: {len(strict_failures)} claimed path(s) ({','.join(sorted(tiers))}) lack rubric evidence")
        if strict_failures and status == 0:
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
