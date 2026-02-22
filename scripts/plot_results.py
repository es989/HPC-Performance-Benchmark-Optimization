"""Plot benchmark JSON results using pandas + matplotlib.

Primary output: Plot #1 (Bandwidth vs Working-Set Size) that highlights the
memory-hierarchy "waterfall".

Usage (PowerShell):
    python scripts/plot_results.py ./results/triad_aff1.json

This script intentionally stays simple (single plot) to match the portfolio
requirements and keep the workflow reproducible.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker


def kernel_bytes_multiplier(kernel_name: str) -> float:
    """How many array-bytes are effectively touched per iteration.

    In the JSON, `bytes` is ONE array size.
    STREAM-like kernels touch multiple arrays per iteration.
    """

    k = (kernel_name or "").lower()
    if "copy" in k or "scale" in k:
        return 2.0
    if "add" in k or "triad" in k:
        return 3.0
    return 1.0


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
    if "bytes" not in df.columns:
        raise ValueError("Sweep points are missing required field: bytes")

    # Normalize / validate expected fields.
    if "bandwidth_gb_s" not in df.columns:
        raise ValueError("Sweep points are missing required field: bandwidth_gb_s")

    if "kernel" not in df.columns:
        df["kernel"] = "unknown"

    df = df.sort_values("bytes").reset_index(drop=True)
    return df


def plot_bandwidth_vs_size(
    df: pd.DataFrame,
    *,
    use_effective_workingset: bool,
    out_path: Path,
    title: str | None,
    show: bool,
    dpi: int,
):
    kernel = str(df["kernel"].iloc[0]) if len(df) else "unknown"
    mult = kernel_bytes_multiplier(kernel)

    x_bytes = df["bytes"].astype(float)
    if use_effective_workingset:
        x_bytes = x_bytes * mult
        x_label = "Working-set size (effective bytes)"
    else:
        x_label = "Working-set size (bytes)"

    y = df["bandwidth_gb_s"].astype(float)

    fig, ax = plt.subplots(figsize=(8.0, 4.8))
    ax.plot(x_bytes, y, marker="o", linewidth=1.8, markersize=4)

    ax.set_xscale("log", base=2)
    ax.set_xlabel(x_label)
    ax.set_ylabel("Bandwidth (GB/s)")

    if title is None:
        title = f"Bandwidth vs Working-Set Size ({kernel})"
    ax.set_title(title)

    ax.grid(True, which="both", linestyle="--", linewidth=0.6, alpha=0.5)

    def fmt(x, _pos=None):
        return bytes_to_human(x)

    ax.xaxis.set_major_formatter(mticker.FuncFormatter(fmt))

    fig.tight_layout()

    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path, dpi=dpi)

    if show:
        plt.show()

    plt.close(fig)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Plot Plot#1: Bandwidth vs Working-Set Size from benchmark JSON (pandas + matplotlib)."
    )
    parser.add_argument("json_path", type=Path, help="Path to results JSON (stats.sweep)")
    parser.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Output image path (default: plots/bandwidth_vs_size_<kernel>.png)",
    )
    parser.add_argument(
        "--no-effective-ws",
        action="store_true",
        help="Use raw `bytes` on X axis (do not apply STREAM array-multiplier)",
    )
    parser.add_argument("--title", type=str, default=None, help="Plot title override")
    parser.add_argument("--show", action="store_true", help="Display the plot window")
    parser.add_argument("--dpi", type=int, default=200, help="Output DPI (default: 200)")

    args = parser.parse_args()

    doc = load_json(args.json_path)
    df = build_dataframe(doc)

    kernel = str(df["kernel"].iloc[0]) if len(df) else "unknown"

    if args.out is None:
        out_path = Path("plots") / f"bandwidth_vs_size_{kernel}.png"
    else:
        out_path = args.out

    plot_bandwidth_vs_size(
        df,
        use_effective_workingset=(not args.no_effective_ws),
        out_path=out_path,
        title=args.title,
        show=args.show,
        dpi=args.dpi,
    )

    print(f"[plot_results] Wrote: {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
