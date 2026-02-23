"""Aggregate multiple benchmark JSON runs into a single JSON + CSV summary.

This complements the executable's internal statistics (median/p95 across iters)
by computing robust statistics across repeated *process-level* runs.

Output:
- Aggregated JSON with the same schema under stats.sweep
- CSV with per-bytes summary fields

Usage:
    python scripts/aggregate_runs.py --out-json results/summary/triad_agg.json \
        --out-csv results/summary/triad_agg.csv results/raw/triad_run_*.json
"""

from __future__ import annotations

import argparse
import json
from collections import defaultdict
from pathlib import Path
from statistics import median


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def safe_mean(xs: list[float]) -> float:
    xs = [float(x) for x in xs if x is not None]
    return sum(xs) / len(xs) if xs else 0.0


def group_points(docs: list[dict]):
    grouped: dict[tuple[str, int], list[dict]] = defaultdict(list)
    for doc in docs:
        sweep = (doc.get("stats") or {}).get("sweep") or []
        for pt in sweep:
            k = str(pt.get("kernel") or "unknown")
            b = int(pt.get("bytes") or 0)
            grouped[(k, b)].append(pt)
    return grouped


def aggregate(docs: list[dict]) -> tuple[dict, list[dict], list[dict]]:
    if not docs:
        raise ValueError("No input docs")

    base = docs[0]
    grouped = group_points(docs)

    sweep_rows: list[dict] = []
    csv_rows: list[dict] = []

    for (kernel, bytes_), pts in sorted(grouped.items(), key=lambda t: (t[0][0], t[0][1])):
        medians = [float(p.get("median_ns") or 0.0) for p in pts]
        p95s = [float(p.get("p95_ns") or 0.0) for p in pts]
        stddevs = [float(p.get("stddev_ns") or 0.0) for p in pts]
        bws = [float(p.get("bandwidth_gb_s") or 0.0) for p in pts]
        ns_per_access = [float(p.get("ns_per_access") or 0.0) for p in pts if "ns_per_access" in p]

        row = {
            "kernel": kernel,
            "bytes": bytes_,
            "median_ns": float(median(medians)) if medians else 0.0,
            "p95_ns": safe_mean(p95s),
            "min_ns": min(float(p.get("min_ns") or 0.0) for p in pts),
            "max_ns": max(float(p.get("max_ns") or 0.0) for p in pts),
            "stddev_ns": safe_mean(stddevs),
            "bandwidth_gb_s": float(median(bws)) if bws else 0.0,
            "checksum": float(median([float(p.get("checksum") or 0.0) for p in pts])),
            "runs": len(pts),
        }

        if ns_per_access:
            row["ns_per_access"] = float(median(ns_per_access))

        sweep_rows.append({k: v for k, v in row.items() if k != "runs"})

        csv_rows.append(row)

    out = {
        "metadata": base.get("metadata", {}),
        "config": base.get("config", {}),
        "aggregation": {
            "runs": len(docs),
            "inputs": [str(p) for p in base.get("aggregation_inputs", [])] if isinstance(base.get("aggregation_inputs"), list) else None,
        },
        "stats": {
            "performance": base.get("stats", {}).get("performance", {}),
            "sweep": sweep_rows,
        },
    }

    # prune None
    if out["aggregation"]["inputs"] is None:
        out["aggregation"].pop("inputs", None)

    return out, sweep_rows, csv_rows


def write_json(path: Path, doc: dict):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        json.dump(doc, f, indent=4)


def write_csv(path: Path, rows: list[dict]):
    import csv

    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        return

    cols = list(rows[0].keys())
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        for r in rows:
            w.writerow(r)


def main() -> int:
    ap = argparse.ArgumentParser(description="Aggregate repeated run JSONs into a single summary JSON/CSV.")
    ap.add_argument("inputs", nargs="+", type=Path, help="Input JSON files")
    ap.add_argument("--out-json", type=Path, required=True, help="Output aggregated JSON path")
    ap.add_argument("--out-csv", type=Path, required=True, help="Output CSV path")

    args = ap.parse_args()

    docs = [load_json(p) for p in args.inputs]
    out_doc, _sweep_rows, csv_rows = aggregate(docs)

    # Record inputs in the output for traceability.
    out_doc["aggregation_inputs"] = [str(p) for p in args.inputs]

    write_json(args.out_json, out_doc)
    write_csv(args.out_csv, csv_rows)

    print(f"[aggregate_runs] Wrote: {args.out_json}")
    print(f"[aggregate_runs] Wrote: {args.out_csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
