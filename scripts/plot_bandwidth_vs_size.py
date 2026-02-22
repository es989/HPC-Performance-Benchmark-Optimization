import argparse
import json
import math
import os
import subprocess
from pathlib import Path

import matplotlib.pyplot as plt
import matplotlib.ticker as mticker
from matplotlib.lines import Line2D


# Manual cache configuration for this machine (bytes).
# Values below are derived from Win32_Processor (KB -> bytes):
#   L2CacheSize = 5120 KB  => 5 MiB
#   L3CacheSize = 12288 KB => 12 MiB
# Typical per-core L1 data cache is ~48 KiB on this CPU family.
# These are used only for visualization; they do NOT affect measurements.
MANUAL_CACHE_CONFIG = {
    "cache_l1_bytes": 48 * 1024,        # 48 KiB
    "cache_l2_bytes": 5120 * 1024,      # 5 MiB
    "cache_llc_bytes": 12288 * 1024,    # 12 MiB
}


def kernel_bytes_multiplier(kernel_name: str) -> float:
    """Return how many array-bytes are effectively touched per iteration.

    The JSON's "bytes" field is ONE array size.
    STREAM-like kernels touch multiple arrays per iteration:
      copy/scale: 2 arrays, add/triad: 3 arrays.
    """
    k = (kernel_name or "").lower()
    if "copy" in k or "scale" in k:
        return 2.0
    if "add" in k or "triad" in k:
        return 3.0
    return 1.0


def load_sweep_points(path: Path):
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)

    sweep = data.get("stats", {}).get("sweep", [])
    if not sweep:
        raise ValueError(f"No sweep data found in JSON: {path}")

    sweep = sorted(sweep, key=lambda p: p["bytes"])
    return sweep, data


def try_get_windows_cpu_model() -> str:
    """Best-effort CPU model on Windows (no hard dependency).

    Only used as a fallback when JSON metadata has "Unknown CPU".
    """
    if os.name != "nt":
        return ""

    try:
        completed = subprocess.run(
            [
                "powershell",
                "-NoProfile",
                "-Command",
                "(Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name)",
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        name = (completed.stdout or "").strip()
        return name
    except Exception:
        return ""


def bytes_to_human(x: int) -> str:
    units = ["B", "KiB", "MiB", "GiB"]
    v = float(x)
    for u in units:
        if v < 1024.0 or u == units[-1]:
            return f"{v:.0f} {u}" if v >= 10 or u == "B" else f"{v:.1f} {u}"
        v /= 1024.0
    return f"{x} B"


def get_cache_sizes_bytes(sys_meta: dict):
    platform = (sys_meta or {}).get("platform", {})
    l1 = platform.get("cache_l1_bytes") or MANUAL_CACHE_CONFIG.get("cache_l1_bytes")
    l2 = platform.get("cache_l2_bytes") or MANUAL_CACHE_CONFIG.get("cache_l2_bytes")
    llc = platform.get("cache_llc_bytes") or MANUAL_CACHE_CONFIG.get("cache_llc_bytes")
    return l1, l2, llc


def annotate_cache_text(ax, sys_meta: dict):
    l1, l2, llc = get_cache_sizes_bytes(sys_meta)

    parts = []
    if isinstance(l1, (int, float)) and l1 > 0:
        parts.append(f"L1≈{bytes_to_human(int(l1))}")
    if isinstance(l2, (int, float)) and l2 > 0:
        parts.append(f"L2≈{bytes_to_human(int(l2))}")
    if isinstance(llc, (int, float)) and llc > 0:
        parts.append(f"LLC≈{bytes_to_human(int(llc))}")

    if not parts:
        return

    ax.text(
        0.01, 0.98, ", ".join(parts),
        transform=ax.transAxes,
        va="top", ha="left",
        fontsize=9, color="dimgray",
        bbox={"facecolor": "white", "alpha": 0.75, "edgecolor": "none", "pad": 2.5},
    )


def classify_knee_region(effective_bytes: float, l1, l2, llc) -> str:
    """Classify a knee by interval (no 1.25x fuzz), to reduce duplicates."""
    if isinstance(l1, (int, float)) and l1 > 0 and effective_bytes <= l1:
        return "~L1 boundary"
    if isinstance(l2, (int, float)) and l2 > 0 and effective_bytes <= l2:
        return "~L2 boundary"
    if isinstance(llc, (int, float)) and llc > 0 and effective_bytes <= llc:
        return "~LLC boundary"
    return "~DRAM / large"


def nearest_theory_label(effective_bytes: float, l1, l2, llc, tol_frac: float = 0.15):
    """Return L1/L2/LLC if effective_bytes is within ±tol_frac of a cache size."""
    candidates = []
    for val, name in [(l1, "L1"), (l2, "L2"), (llc, "LLC")]:
        if isinstance(val, (int, float)) and val > 0:
            frac = abs(effective_bytes - val) / float(val)
            candidates.append((frac, name))
    if not candidates:
        return None
    frac, name = min(candidates, key=lambda t: t[0])
    return name if frac <= tol_frac else None


def annotate_theoretical_cache_boundaries(
    ax,
    sys_meta: dict,
    bytes_mult: float,
    *,
    include_l1: bool = False,
    show_text: bool = False,
):
    """Draw faint theoretical cache boundaries.

    Default is L2+LLC only (cleaner). Convert cache bytes -> equivalent ONE-array
    x-position by dividing by bytes_mult.
    """
    if not (bytes_mult and bytes_mult > 0):
        return

    l1, l2, llc = get_cache_sizes_bytes(sys_meta)

    markers = []
    if include_l1:
        markers.append((l1, "theory: L1"))
    markers.extend([
        (l2, "theory: L2"),
        (llc, "theory: LLC"),
    ])

    for cache_bytes, label in markers:
        if not (isinstance(cache_bytes, (int, float)) and cache_bytes > 0):
            continue
        x = cache_bytes / bytes_mult
        ax.axvline(x, color="tab:blue", linestyle=":", linewidth=1.0, alpha=0.28)
        if show_text:
            ax.text(
                x,
                0.05,
                label,
                rotation=90,
                va="bottom",
                ha="right",
                fontsize=8,
                color="tab:blue",
                alpha=0.55,
                transform=ax.get_xaxis_transform(),
            )


def _find_significant_drops(
    sizes_bytes,
    bw_gb_s,
    *,
    min_drop_ratio: float,
    abs_thresh: float,
    peak: float,
    min_pre_drop_peak_frac: float,
):
    """Return candidates: (drop_gbs, x_mid, i, b1, b2)."""
    n = len(sizes_bytes)
    cand = []
    for i in range(n - 1):
        s1, s2 = sizes_bytes[i], sizes_bytes[i + 1]
        b1, b2 = bw_gb_s[i], bw_gb_s[i + 1]
        if b1 <= 0 or b2 <= 0:
            continue

        # Guard: ignore drops that start from a very low bandwidth point
        # (often already DRAM-plateau / noise region).
        if peak > 0 and b1 < (min_pre_drop_peak_frac * peak):
            continue
        ratio = b2 / b1
        drop = b1 - b2
        if ratio < min_drop_ratio and drop >= abs_thresh:
            x_mid = math.sqrt(s1 * s2)  # geometric midpoint on log scale
            cand.append((drop, x_mid, i, b1, b2))
    return cand


def _enforce_separation(candidates, min_separation_factor: float, max_keep: int):
    """Keep largest drops, but ensure x positions aren't too close on log scale."""
    if max_keep <= 0:
        return []
    candidates = sorted(candidates, reverse=True, key=lambda t: t[0])  # largest drop first
    selected = []
    for item in candidates:
        _, x_mid, *_ = item
        too_close = any(
            (x_mid < (x_sel * min_separation_factor) and x_mid > (x_sel / min_separation_factor))
            for _, x_sel, *_ in selected
        )
        if too_close:
            continue
        selected.append(item)
        if len(selected) >= max_keep:
            break
    return selected


def annotate_switch_points(
    ax,
    sizes_bytes,
    bw_gb_s,
    sys_meta: dict,
    bytes_mult: float,
    *,
    mode: str = "clean",  # clean | research
    max_knees: int = 5,
    min_drop_ratio: float = 0.75,
    min_drop_peak_frac: float = 0.10,
    min_drop_abs_gbs: float = 1.0,
    min_separation_factor: float = 2.0,
    theory_match_tol: float = 0.15,
    min_pre_drop_peak_frac: float = 0.30,
):
    """Annotate data-driven bandwidth drops.

    clean:
      - keep at most one knee per region (~L1/~L2/~LLC/~DRAM)
      - only label as cache boundary if it's also near the corresponding theory size

    research:
      - mark up to max_knees significant drops
      - label as drop #k with magnitude; optionally add “near theory: L2/LLC”
    """
    n = len(sizes_bytes)
    if n < 2:
        return

    peak = max(bw_gb_s) if bw_gb_s else 0.0
    abs_thresh = max(min_drop_abs_gbs, min_drop_peak_frac * peak)

    l1, l2, llc = get_cache_sizes_bytes(sys_meta)

    candidates = _find_significant_drops(
        sizes_bytes,
        bw_gb_s,
        min_drop_ratio=min_drop_ratio,
        abs_thresh=abs_thresh,
        peak=peak,
        min_pre_drop_peak_frac=min_pre_drop_peak_frac,
    )
    if not candidates:
        return

    mode = (mode or "").strip().lower()
    if mode not in ("clean", "research"):
        mode = "clean"

    if mode == "research":
        selected = _enforce_separation(candidates, min_separation_factor, max_knees)
        if not selected:
            return

        print("Detected drops (research):")
        for k, (drop, x_mid, i, b1, b2) in enumerate(selected, start=1):
            rel = (drop / b1) * 100.0 if b1 > 0 else 0.0
            eff = float(bytes_mult) * float(x_mid)
            near = nearest_theory_label(eff, l1, l2, llc, tol_frac=theory_match_tol)
            extra = f", near theory: {near}" if near else ""
            label = f"drop #{k} (-{drop:.1f} GB/s, -{rel:.0f}%){extra}"
            print(f"  {label}")

            ax.axvline(x_mid, color="black", linestyle="--", linewidth=1.2, alpha=0.65)
            ax.text(
                x_mid,
                0.88,
                label,
                rotation=90,
                va="top",
                ha="right",
                fontsize=8,
                color="black",
                transform=ax.get_xaxis_transform(),
                bbox={"facecolor": "white", "alpha": 0.65, "edgecolor": "none", "pad": 1.5},
            )
        return

    # clean mode: strongest knee per (tentative) region label
    best_by_label = {}
    for drop, x_mid, i, b1, b2 in candidates:
        eff = float(bytes_mult) * float(x_mid)
        knee_label = classify_knee_region(eff, l1, l2, llc)
        prev = best_by_label.get(knee_label)
        if prev is None or drop > prev[0]:
            best_by_label[knee_label] = (drop, x_mid, i, b1, b2, eff)

    label_order = ["~L1 boundary", "~L2 boundary", "~LLC boundary", "~DRAM / large"]
    selected = [best_by_label[l] for l in label_order if l in best_by_label]

    # Enforce separation among the selected points
    selected_simple = _enforce_separation(
        [(drop, x_mid, i, b1, b2) for (drop, x_mid, i, b1, b2, eff) in selected],
        min_separation_factor,
        max_keep=len(selected),
    )
    if not selected_simple:
        return

    print("Detected drops (clean):")
    for drop, x_mid, i, b1, b2 in selected_simple:
        eff = float(bytes_mult) * float(x_mid)
        knee_label = classify_knee_region(eff, l1, l2, llc)

        # Only claim cache boundaries if also close to theory (avoid over-claiming).
        near = nearest_theory_label(eff, l1, l2, llc, tol_frac=theory_match_tol)
        if knee_label in ("~L1 boundary", "~L2 boundary", "~LLC boundary") and near is None:
            knee_label = "significant drop"
        elif knee_label == "~L1 boundary" and near != "L1":
            knee_label = "significant drop"
        elif knee_label == "~L2 boundary" and near != "L2":
            knee_label = "significant drop"
        elif knee_label == "~LLC boundary" and near != "LLC":
            knee_label = "significant drop"

        print(f"  {knee_label}")
        ax.axvline(x_mid, color="black", linestyle="--", linewidth=1.2, alpha=0.65)
        ax.text(
            x_mid,
            0.90,
            knee_label,
            rotation=90,
            va="top",
            ha="right",
            fontsize=8,
            color="black",
            transform=ax.get_xaxis_transform(),
            bbox={"facecolor": "white", "alpha": 0.65, "edgecolor": "none", "pad": 1.5},
        )


def make_plot(json_path: Path, out_dir: Path, args):
    sweep, root = load_sweep_points(json_path)

    sizes_bytes = [pt["bytes"] for pt in sweep]
    bw_gb_s = [pt["bandwidth_gb_s"] for pt in sweep]

    kernel = root.get("config", {}).get("kernel", "unknown")
    platform = root.get("metadata", {}).get("platform", {})
    cpu = (platform.get("cpu_model", "") or "").strip()
    if not cpu or cpu.startswith("Unknown CPU"):
        cpu_fallback = try_get_windows_cpu_model()
        if cpu_fallback:
            cpu = cpu_fallback

    bytes_mult = kernel_bytes_multiplier(kernel)

    plt.rcParams.update({
        "font.size": 11,
        "axes.titlesize": 13,
        "axes.labelsize": 11,
        "figure.dpi": 120,
    })

    fig, ax = plt.subplots(figsize=(9, 5.2), constrained_layout=True)

    (measured_line,) = ax.plot(
        sizes_bytes, bw_gb_s,
        marker="o", markersize=5,
        linewidth=1.8,
    )

    ax.set_xscale("log", base=2)
    ax.set_xlabel("Working-set size")
    ax.set_ylabel("Bandwidth (GB/s)")

    title = f"STREAM Bandwidth vs Working-Set Size ({kernel})"
    if cpu and not cpu.startswith("Unknown CPU"):
        title += f"\nCPU: {cpu}"
    ax.set_title(title)

    # Cache sizes block (OS/JSON or manual fallback)
    annotate_cache_text(ax, root.get("metadata", {}))

    # Faint theoretical boundaries (based on cache sizes)
    annotate_theoretical_cache_boundaries(
        ax,
        root.get("metadata", {}),
        bytes_mult,
        include_l1=bool(getattr(args, "theory_l1", False)),
        show_text=bool(getattr(args, "theory_text", False)),
    )

    # ---- Clean ticks: powers of two in range, with density control ----
    min_x, max_x = min(sizes_bytes), max(sizes_bytes)
    lo = int(math.floor(math.log2(min_x)))
    hi = int(math.ceil(math.log2(max_x)))
    ticks = [2 ** p for p in range(lo, hi + 1) if min_x <= (2 ** p) <= max_x]

    if ticks:
        # Keep at most ~10 labels
        stride = max(1, int(math.ceil(len(ticks) / 10)))
        ticks = ticks[::stride]
        ax.set_xticks(ticks)
        ax.xaxis.set_major_formatter(mticker.FuncFormatter(lambda v, pos: bytes_to_human(int(v))))

    ax.xaxis.set_minor_locator(mticker.NullLocator())

    # Subtle grid
    ax.grid(True, which="major", linestyle=":", linewidth=0.6, alpha=0.45)
    ax.set_axisbelow(True)

    # Data-driven knees / drops
    annotate_switch_points(
        ax,
        sizes_bytes,
        bw_gb_s,
        root.get("metadata", {}),
        bytes_mult,
        mode=args.mode,
        max_knees=args.max_knees,
        min_drop_ratio=args.min_drop_ratio,
        min_drop_peak_frac=args.min_drop_peak_frac,
        min_drop_abs_gbs=args.min_drop_abs_gbs,
        min_separation_factor=args.min_separation_factor,
        theory_match_tol=args.theory_match_tol,
        min_pre_drop_peak_frac=args.min_pre_drop_peak_frac,
    )

    # Small legend to explain styles
    legend_handles = [
        Line2D([0], [0], color=measured_line.get_color(), marker="o", linewidth=1.8, label="measured"),
        Line2D([0], [0], color="black", linestyle="--", linewidth=1.2, label="knees (data)"),
        Line2D([0], [0], color="tab:blue", linestyle=":", linewidth=1.0, alpha=0.28, label="theory cache (OS/manual)"),
    ]
    ax.legend(
        handles=legend_handles,
        loc="upper left",
        bbox_to_anchor=(1.02, 1.0),
        borderaxespad=0.0,
        frameon=True,
        framealpha=0.9,
    )

    out_dir.mkdir(parents=True, exist_ok=True)
    suffix = "" if args.mode == "clean" else f"_{args.mode}"
    base = out_dir / f"bandwidth_vs_size_{kernel}{suffix}"
    fig.savefig(base.with_suffix(".png"), dpi=250)
    fig.savefig(base.with_suffix(".svg"))
    print(f"Saved plot to: {base}.png and {base}.svg")


def main():
    parser = argparse.ArgumentParser(description="Plot bandwidth vs working-set size from benchmark JSON.")
    parser.add_argument("json", type=str, help="Path to results JSON file produced by bench.")
    parser.add_argument(
        "--out-dir",
        type=str,
        default=str(Path(__file__).resolve().parents[1] / "plots"),
        help="Directory to write plots (default: <repo>/plots)",
    )

    parser.add_argument(
        "--mode",
        choices=["clean", "research"],
        default="clean",
        help="clean: conservative cache-ish labels; research: drop #k labels with magnitude",
    )
    parser.add_argument("--max-knees", type=int, default=5)
    parser.add_argument(
        "--min-drop-ratio",
        type=float,
        default=0.75,
        help="Require b2/b1 < this to count as a drop",
    )
    parser.add_argument(
        "--min-drop-peak-frac",
        type=float,
        default=0.10,
        help="Require drop >= this*peak_bw (GB/s)",
    )
    parser.add_argument(
        "--min-drop-abs-gbs",
        type=float,
        default=2.0,
        help="Require drop >= this absolute GB/s",
    )
    parser.add_argument(
        "--min-separation-factor",
        type=float,
        default=2.0,
        help="Require knees to be at least this factor apart on x (log scale)",
    )
    parser.add_argument(
        "--theory-match-tol",
        type=float,
        default=0.15,
        help="Add/require 'near theory' if within this fraction",
    )
    parser.add_argument(
        "--min-pre-drop-peak-frac",
        type=float,
        default=0.30,
        help="Ignore drops that start below this fraction of peak bandwidth",
    )
    parser.add_argument(
        "--theory-l1",
        action="store_true",
        help="Also draw theory:L1 line (default off)",
    )
    parser.add_argument(
        "--theory-text",
        action="store_true",
        help="Show text labels next to theory lines (default off)",
    )
    args = parser.parse_args()

    json_path = Path(args.json).expanduser().resolve()
    out_dir = Path(args.out_dir).expanduser().resolve()

    if not json_path.is_file():
        raise SystemExit(f"JSON file not found: {json_path}")

    make_plot(json_path, out_dir, args)


if __name__ == "__main__":
    main()