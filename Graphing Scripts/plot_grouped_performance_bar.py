"""Plot grouped performance bars for DefaultAA vs AADB.

The script expects a CSV with these columns:

    benchmark,variant,total_execution_time_seconds

Example:

    python plot_grouped_performance_bar.py \
        --csv execution_time.csv \
        --out execution_time_grouped.pdf

The output uses two highly distinguishable colors plus a hatch pattern for the
AADB bars so the figure remains readable in grayscale prints.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd
import numpy as np


def load_execution_time(csv_path: Path) -> pd.DataFrame:
    df = pd.read_csv(csv_path)

    required = {"benchmark", "variant", "total_execution_time_seconds"}
    missing = required.difference(df.columns)
    if missing:
        missing_cols = ", ".join(sorted(missing))
        raise ValueError(f"Missing required columns: {missing_cols}")

    df = df.copy()
    df["benchmark"] = df["benchmark"].astype(str).str.strip()
    df["variant"] = df["variant"].astype(str).str.strip()
    df["total_execution_time_seconds"] = pd.to_numeric(
        df["total_execution_time_seconds"], errors="coerce"
    )
    df = df.dropna(subset=["total_execution_time_seconds"])
    return df


def pivot_for_grouped_bars(df: pd.DataFrame) -> pd.DataFrame:
    pivot = (
        df.pivot_table(
            index="benchmark",
            columns="variant",
            values="total_execution_time_seconds",
            aggfunc="mean",
        )
        .sort_index()
    )

    # Keep the benchmark order stable and easy to read.
    return pivot


def plot_grouped_bars(csv_path: Path, out_path: Path, title: str | None = None) -> None:
    df = load_execution_time(csv_path)
    pivot = pivot_for_grouped_bars(df)

    if pivot.empty:
            # Build a clean legend with only AADB and GeoMean to avoid showing the
            # baseline (DefaultAA), which is always 1.0 and omitted from the plot.
            handles = []
            if "AADB" in plot_variants:
                handles.append(Patch(facecolor=colors.get("AADB", default_color), edgecolor="black", label="AADB"))
            # GeoMean patch
            handles.append(Patch(facecolor=geo_color, edgecolor="black", label="GeoMean"))
            ax.legend(handles=handles, frameon=False, ncol=2)
    elif "baseline" in pivot.columns:
        baseline_col = "baseline"
    else:
        baseline_col = list(pivot.columns)[0]

    # Normalize all columns to the baseline column so the plot shows relative
    # performance (baseline == 1.0).
    # Guard against division by zero by replacing zeros with NaN before dividing.
    denom = pivot[baseline_col].replace({0: float("nan")})
    norm = pivot.div(denom, axis=0)

    # Use a common ordering: baseline first, then AADB if present, then others.
    variants = [c for c in [baseline_col, "AADB"] if c in norm.columns]
    for c in norm.columns:
        if c not in variants:
            variants.append(c)

    # Compute geometric mean across benchmarks for each variant and append it
    # as an extra row labeled 'GeoMean'. Ignore non-positive or NaN values when
    # computing the geometric mean.
    geom_vals = {}
    for c in norm.columns:
        col = norm[c].replace({0: np.nan}).dropna()
        col = col[col > 0]
        if len(col) == 0:
            geom_vals[c] = float("nan")
        else:
            # geometric mean = exp(mean(log(x)))
            geom_vals[c] = float(np.exp(np.log(col).mean()))

    # Append the GeoMean row to the normalized dataframe.
    norm = norm.copy()
    norm.loc["GeoMean"] = pd.Series(geom_vals)

    # Strongly distinguishable, publication-friendly colors.
    # Map colors to the actual variant names.
    colors = {
        baseline_col: "#1f77b4",  # blue for baseline
        "AADB": "#ff7f0e",       # orange for AADB (if present)
    }
    hatches = {
        baseline_col: "",
        "AADB": "///",
    }

    # Friendly display names for the legend: always show 'DefaultAA' for the
    # detected baseline column name (which may be 'baseline' in the CSV).
    display_names = {baseline_col: "DefaultAA", "AADB": "AADB"}

    # Use the normalized dataframe index (now including GeoMean) for plotting.
    benchmarks = list(norm.index)
    x = list(range(len(benchmarks)))
    width = 0.38 if len(variants) == 2 else 0.8 / max(1, len(variants))

    # Use compact, camera-ready style defaults similar to `plot_performance_bar.py`.
    # Default to single-column width so labels remain legible in Overleaf.
    width_mode = "single"
    if width_mode == "single":
        base_fs = 10
        label_fs = 9
        title_fs = 11
        tick_fs = 8
        value_label_fs = 8
        fig_size = (3.4, 2.1)
    elif width_mode == "double":
        base_fs = 9
        label_fs = 9
        title_fs = 10
        tick_fs = 8
        value_label_fs = 7
        fig_size = (7.0, 2.6)
    else:
        base_fs = 10
        label_fs = 10
        title_fs = 12
        tick_fs = 9
        value_label_fs = 8
        fig_size = (12.0, 4.8)

    plt.rcParams.update(
        {
            "font.family": "sans-serif",
            "font.sans-serif": ["Helvetica", "Arial", "DejaVu Sans"],
            "font.size": base_fs,
            "axes.labelsize": label_fs,
            "axes.titlesize": title_fs,
            "xtick.labelsize": tick_fs,
            "ytick.labelsize": tick_fs,
            "legend.fontsize": tick_fs,
        }
    )

    fig, ax = plt.subplots(figsize=fig_size)

    offsets = []
    if len(variants) == 1:
        offsets = [0.0]
    elif len(variants) == 2:
        offsets = [-width / 2, width / 2]
    else:
        center = (len(variants) - 1) / 2
        offsets = [(i - center) * width for i in range(len(variants))]

    for variant, offset in zip(variants, offsets):
        # Plot normalized values (baseline == 1.0).
        values = norm[variant].reindex(benchmarks).tolist()
        bars = ax.bar(
            [xi + offset for xi in x],
            values,
            width=width,
            label=display_names.get(variant, variant),
            color=colors.get(variant, "#4c78a8"),
            edgecolor="black",
            linewidth=0.7,
            hatch=hatches.get(variant, ""),
        )

        # Optional value labels for easy reading in papers.
        for bar, value in zip(bars, values):
            # Annotate with two decimal places since values are normalized.
            if value is None or (isinstance(value, float) and (pd.isna(value))):
                label = "-"
            else:
                label = f"{value:.2f}"
            ax.text(
                bar.get_x() + bar.get_width() / 2,
                bar.get_height(),
                label,
                ha="center",
                va="bottom",
                fontsize=value_label_fs,
                rotation=0,
                clip_on=True,
            )

    pretty_labels = [label.replace("_s", "") for label in benchmarks]
    ax.set_xticks(x)
    ax.set_xticklabels(pretty_labels, rotation=45, ha="right")
    ax.set_ylabel("Normalized execution time (DefaultAA = 1.0)", fontsize=value_label_fs)
    if title:
        ax.set_title(title)

    ax.grid(axis="y", linestyle=":", linewidth=0.7, alpha=0.6)
    ax.set_axisbelow(True)
    ax.legend(frameon=False, ncol=2)

    fig.tight_layout()
    fig.savefig(out_path, dpi=300, bbox_inches="tight")
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser(description="Plot grouped benchmark bars for DefaultAA vs AADB.")
    parser.add_argument("--csv", type=Path, default=Path("execution_time.csv"), help="Input CSV path")
    parser.add_argument("--out", type=Path, default=Path("execution_time_grouped.pdf"), help="Output figure path")
    parser.add_argument("--title", type=str, default=None, help="Optional title for the figure")
    args = parser.parse_args()

    plot_grouped_bars(args.csv, args.out, title=args.title)
    print(f"Saved plot to: {args.out}")


if __name__ == "__main__":
    main()