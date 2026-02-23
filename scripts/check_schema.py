"""Sanity-check aggregated summary JSON schema.

Checks the newest *_agg_*.json files under results/summary and validates:
- top-level keys: metadata/config/stats
- required sweep fields
- latency summaries include ns_per_access

Usage:
    python scripts/check_schema.py
"""

from __future__ import annotations

import json
from pathlib import Path


def main() -> int:
    base = Path("results") / "summary"
    files = sorted(base.glob("*_agg_*.json"), key=lambda p: p.stat().st_mtime, reverse=True)[:6]

    if not files:
        print(f"No aggregated summaries found under: {base}")
        return 2

    print("Newest summary files:")
    for p in files[:3]:
        print(f" - {p}")

    req_common = {
        "kernel",
        "bytes",
        "median_ns",
        "p95_ns",
        "min_ns",
        "max_ns",
        "stddev_ns",
        "bandwidth_gb_s",
        "checksum",
    }

    ok = True

    for p in files[:3]:
        doc = json.loads(p.read_text(encoding="utf-8"))

        missing_top = [k for k in ("metadata", "config", "stats") if k not in doc]
        if missing_top:
            print(f"WARN {p.name}: missing top-level keys: {missing_top}")

        sweep = ((doc.get("stats") or {}).get("sweep") or [])
        if not sweep:
            print(f"FAIL {p.name}: missing or empty stats.sweep")
            ok = False
            continue

        missing = sorted(req_common - set(sweep[0].keys()))
        if missing:
            print(f"FAIL {p.name}: sweep[0] missing keys: {missing}")
            ok = False

        if "latency_ptr_chase" in p.name:
            if "ns_per_access" not in sweep[0]:
                print(f"FAIL {p.name}: latency summary missing ns_per_access")
                ok = False

        print(f"OK {p.name}: rows={len(sweep)}")

    print("SCHEMA_OK" if ok else "SCHEMA_ISSUES")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
