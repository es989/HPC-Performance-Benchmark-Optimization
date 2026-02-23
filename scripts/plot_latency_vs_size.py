"""Plot pointer-chasing latency JSON results using pandas + matplotlib.

Primary output: Plot #2 (Latency vs Working-Set Size).

Usage:
    python scripts/plot_latency_vs_size.py results/summary/latency_ptr_chase_agg.json

Input JSON contract:
- stats.sweep[*].bytes
- stats.sweep[*].ns_per_access

The benchmark records ns_per_access as median_ns / steps.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker


def bytes_to_human(x: float) -> str:
    units = ["B", "KiB", "MiB", "GiB"]
    v = float(x)
    for u in units:
        if v < 1024.0 or u == units[-1]:
            if u == "B":
                return f"{v:.0f} {u}"
            return f"{v:.0f} {u}" if v >= 10 else f"{v:.1f} {u}"
        v /= 1024.0
    return f"{x:.0f} B"


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def build_dataframe(doc: dict) -> pd.DataFrame:
    sweep = (doc.get("stats") or {}).get("sweep") or []
    if not sweep:
        raise ValueError("No sweep data found under stats.sweep")

    df = pd.DataFrame(sweep)
    if "bytes" not in df.columns or "ns_per_access" not in df.columns:
        raise ValueError("Sweep points must include 'bytes' and 'ns_per_access'")

    df = df.sort_values("bytes").reset_index(drop=True)
    return df


def plot_latency_vs_size(df: pd.DataFrame, *, out_path: Path, title: str | None, show: bool, dpi: int):
    x = df["bytes"].astype(float)
    y = df["ns_per_access"].astype(float)

    fig, ax = plt.subplots(figsize=(8.0, 4.8))
    ax.plot(x, y, marker="o", linewidth=1.8, markersize=4)

    ax.set_xscale("log", base=2)
    ax.set_xlabel("Working-set size (bytes)")
    ax.set_ylabel("Latency (ns per dependent load)")

    if title is None:
        title = "Pointer-Chasing Latency vs Working-Set Size"
    ax.set_title(title)

    ax.grid(True, which="both", linestyle="--", linewidth=0.6, alpha=0.5)

    ax.xaxis.set_major_formatter(mticker.FuncFormatter(lambda v, _pos=None: bytes_to_human(v)))

    fig.tight_layout()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path, dpi=dpi)

    if show:
        plt.show()

    plt.close(fig)


def main() -> int:
    p = argparse.ArgumentParser(description="Plot Plot#2: latency vs working-set size from JSON.")
    p.add_argument("json_path", type=Path, help="Path to results JSON (stats.sweep)")
    p.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Output image path (default: plots/latency_vs_size_ptr_chase.png)",
    )
    p.add_argument("--title", type=str, default=None, help="Plot title override")
    p.add_argument("--show", action="store_true", help="Display the plot window")
    p.add_argument("--dpi", type=int, default=200, help="Output DPI (default: 200)")

    args = p.parse_args()

    doc = load_json(args.json_path)
    df = build_dataframe(doc)

    out_path = args.out or (Path("plots") / "latency_vs_size_ptr_chase.png")

    plot_latency_vs_size(df, out_path=out_path, title=args.title, show=args.show, dpi=args.dpi)

    print(f"[plot_latency_vs_size] Wrote: {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
