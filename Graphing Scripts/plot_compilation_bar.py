import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt
# no path effects — use plain text for value labels
from typing import Optional
import shutil
import subprocess
import matplotlib.colors as mcolors
import pandas as pd


def get_figure_size(width_mode: str) -> tuple[float, float]:
    # Common paper figure sizes in inches.
    if width_mode == "single":
        return 3.4, 2.1
    if width_mode == "double":
        return 7.0, 2.6
    # Fallback for slides/analysis.
    return 12.0, 4.8


def load_two_column_csv(csv_path: Path) -> pd.DataFrame:
    lines = [line.strip() for line in csv_path.read_text().splitlines() if line.strip()]
    if not lines:
        raise ValueError(f"Input CSV is empty: {csv_path}")

    # Some files use a single-text header line (e.g., "Norm. Performance")
    # while data rows are still comma-separated.
    first_line = lines[0]
    if "," in first_line:
        raw_header = [p.strip() for p in first_line.split(",", 1)]
    else:
        raw_header = [first_line.strip(), "Value"]

    rows: list[tuple[str, str]] = []
    for line in lines[1:]:
        parts = line.split(",", 1)
        if len(parts) < 2:
            continue
        rows.append((parts[0].strip(), parts[1].strip()))

    if not rows:
        raise ValueError(f"No comma-separated data rows found in: {csv_path}")

    c0 = raw_header[0] if raw_header[0] else "Benchmark"
    c1 = raw_header[1] if raw_header[1] else "Value"
    return pd.DataFrame(rows, columns=[c0, c1])


def load_compilation_csv(csv_path: Path) -> pd.DataFrame:
    with csv_path.open(newline="", encoding="utf-8") as handle:
        rows = [row for row in csv.reader(handle) if any(cell.strip() for cell in row)]

    if not rows:
        raise ValueError(f"Input CSV is empty: {csv_path}")

    header = [column.strip() for column in rows[0]]
    expected_columns = len(header)

    normalized_rows: list[list[str]] = []
    for row in rows[1:]:
        if len(row) >= 2 and row[0].strip().lower() == header[0].lower() and row[1].strip().lower() == header[1].lower():
            continue

        if len(row) < expected_columns:
            row = row + [""] * (expected_columns - len(row))
        normalized_rows.append(row[:expected_columns])

    df = pd.DataFrame(normalized_rows, columns=header)

    lowered = {c.lower(): c for c in df.columns}
    required = {"benchmark", "variant", "total_execution_time_seconds"}
    if required.issubset(lowered):
        benchmark_col = lowered["benchmark"]
        variant_col = lowered["variant"]
        metric_col = lowered["total_execution_time_seconds"]

        normalized = df[[benchmark_col, variant_col, metric_col]].copy()
        normalized[benchmark_col] = normalized[benchmark_col].astype(str).str.strip()
        normalized[variant_col] = normalized[variant_col].astype(str).str.strip().str.lower()
        normalized[metric_col] = pd.to_numeric(normalized[metric_col], errors="coerce")
        normalized = normalized.dropna(subset=[metric_col])

        filtered = normalized[normalized[variant_col].isin({"aadb", "defaultaa"})]
        if filtered.empty:
            raise ValueError(f"{csv_path} does not contain AADB/DefaultAA rows")

        benchmark_order = list(dict.fromkeys(filtered[benchmark_col].tolist()))
        pivot = filtered.pivot_table(
            index=benchmark_col,
            columns=variant_col,
            values=metric_col,
            aggfunc="first",
        ).reindex(benchmark_order)

        missing_variants = [name for name in ("aadb", "defaultaa") if name not in pivot.columns]
        if missing_variants:
            raise ValueError(
                f"{csv_path} is missing required variants: {', '.join(missing_variants)}"
            )

        ratio = pivot["aadb"] / pivot["defaultaa"]
        result = pd.DataFrame({"Benchmark": ratio.index.tolist(), "Value": ratio.to_list()})

        finite_values = ratio[pd.notna(ratio) & (ratio > 0)]
        if len(finite_values) > 0:
            geomean = float(pd.Series(finite_values).prod() ** (1.0 / len(finite_values)))
            result = pd.concat(
                [result, pd.DataFrame([["geomean", geomean]], columns=["Benchmark", "Value"])],
                ignore_index=True,
            )
        return result

    return load_two_column_csv(csv_path)


def plot_performance(
    csv_path: Path,
    output_path: Path,
    width_mode: str,
    bar_width: float = 0.85,
    bar_gap: Optional[float] = None,
    crop_pdf: bool = False,
) -> None:
    df = load_compilation_csv(csv_path)

    # Normalize/clean expected columns from the provided CSV.
    df.columns = [c.strip() for c in df.columns]
    benchmark_col = df.columns[0]
    value_col = df.columns[1]

    df[benchmark_col] = df[benchmark_col].astype(str).str.strip()
    df[value_col] = pd.to_numeric(df[value_col], errors="coerce")
    df = df.dropna(subset=[value_col])

    labels = df[benchmark_col].tolist()
    values = df[value_col].tolist()

    # Paper-friendly palette: muted blue for most bars, vermillion highlight for geomean.
    base_color = "#4C78A8"
    highlight_color = "#E45756"
    colors = [highlight_color if l.lower() == "geomean" else base_color for l in labels]

    # Use compact, camera-ready style defaults.
    # Prefer a clean sans-serif font (Helvetica/Arial) similar to the project's
    # legacy `plot-bars.py` output. Increase sizes for single-column figures so
    # text remains legible.
    if width_mode == "single":
        base_fs = 10
        label_fs = 9
        title_fs = 11
        tick_fs = 8
        value_label_fs = 8
    elif width_mode == "double":
        base_fs = 9
        label_fs = 9
        title_fs = 10
        tick_fs = 8
        value_label_fs = 7
    else:
        base_fs = 10
        label_fs = 10
        title_fs = 12
        tick_fs = 9
        value_label_fs = 8

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

    # bar_width is expected to be a fraction of the category width (0-1)
    # If the user provided a bar gap, compute width as (1 - gap), clamped.
    n = len(labels)
    # For single-column figures, default to a visible gap of 0.2 if not set.
    if width_mode == "single" and bar_gap is None:
        bar_gap = 0.2

    if bar_gap is not None:
        # clamp reasonable gap values between 0 and 0.9
        bar_gap = max(0.0, min(0.9, float(bar_gap)))
        bar_width = max(0.05, 1.0 - bar_gap)

    fig, ax = plt.subplots(figsize=get_figure_size(width_mode))

    # First pass: draw bars at initial size and test if label text fits at
    # the chosen font size. If not, increase figure width to provide more
    # pixels per category so labels can remain at fixed size while keeping
    # the requested gap fraction.
    bars = ax.bar(labels, values, color=colors, edgecolor="black", linewidth=0.5, width=bar_width)
    fig.canvas.draw()
    renderer = fig.canvas.get_renderer()

    ax_bbox = ax.get_window_extent(renderer=renderer)
    pixels_per_cat = ax_bbox.width / max(1, n)

    pad_px = 6
    desired_fs = value_label_fs

    max_needed_frac = 0.0
    temp_texts = []
    for bar, value in zip(bars, values):
        x = bar.get_x() + bar.get_width() / 2
        # create invisible text to measure
        t = ax.text(x, 0.5, f"{value:.2f}", ha="center", va="center", fontsize=desired_fs, visible=False)
        temp_texts.append(t)
        bbox = t.get_window_extent(renderer=renderer)
        needed_px = bbox.width + pad_px
        needed_frac = needed_px / pixels_per_cat
        max_needed_frac = max(max_needed_frac, needed_frac)

    # remove temporary texts
    for t in temp_texts:
        t.remove()

    # If any label requires a larger fraction than current bar_width, scale
    # the figure width so bar_width remains (1-gap) but pixels_per_cat
    # increases to satisfy needed sizes.
    # Only auto-scale in single-column mode to avoid changing larger layouts.
    if width_mode == "single" and max_needed_frac > bar_width:
        scale = max_needed_frac / bar_width
        # avoid excessive scaling by capping to 1.25x for single-column
        scale = min(scale, 1.25)
        cur_size = fig.get_size_inches()
        fig.set_size_inches(cur_size[0] * scale, cur_size[1])
        # re-render bars after resizing
        ax.clear()
        bars = ax.bar(labels, values, color=colors, edgecolor="black", linewidth=0.5, width=bar_width)
        fig.canvas.draw()
        renderer = fig.canvas.get_renderer()

    # # Final pass: add labels centered vertically with fixed font size
    # for bar, value, bar_color in zip(bars, values, colors):
    #     x = bar.get_x() + bar.get_width() / 2
    #     y = value * 0.5
    #     txt = ax.text(x, y, f"{value:.2f}", ha="center", va="center", fontsize=desired_fs)
    #     try:
    #         rgb = mcolors.to_rgb(bar_color)
    #         lum = 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2]
    #         txt.set_color("white" if lum < 0.6 else "black")
    #     except Exception:
    #         txt.set_color("black")

    ax.set_ylabel("Normalized Compilation Time (AADB / DefaultAA)", fontsize=value_label_fs)
    # ax.set_xlabel("Benchmark")
    # ax.set_title("Performance on x86", pad=6)
    ax.yaxis.set_label_coords(-0.125, 0.3)

    ax.axhline(1.0, color="gray", linestyle="--", linewidth=1.0, alpha=0.8)
    ax.grid(axis="y", linestyle=":", linewidth=0.7, alpha=0.6)
    ax.set_axisbelow(True)

    ax.tick_params(axis="x", labelrotation=45)
    for tick in ax.get_xticklabels():
        tick.set_horizontalalignment("right")

    max_value = max(values) if values else 1.0
    upper_tick = max(2, int(max_value) + 1)
    ax.set_ylim(0.9, upper_tick + 0.1)
    yticks = [tick for tick in range(1, upper_tick + 1)]
    ax.set_yticks(yticks)
    ax.set_yticklabels([f"{tick}x" for tick in yticks])

    plt.tight_layout()
    # Save with minimal padding so bbox_inches='tight' can crop whitespace.
    fig.savefig(output_path, dpi=300, bbox_inches="tight", pad_inches=0.01)
    # Optionally run external pdfcrop if requested and available.
    if crop_pdf and str(output_path).lower().endswith(".pdf"):
        pdfcrop_path = shutil.which("pdfcrop")
        if pdfcrop_path:
            try:
                subprocess.run([pdfcrop_path, str(output_path), str(output_path)], check=True)
            except subprocess.CalledProcessError:
                print("pdfcrop failed; output left uncropped.")
        else:
            print("pdfcrop not found; saved with matplotlib tight bbox instead.")
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Create a publication-ready bar plot from a 2-column performance CSV."
    )
    parser.add_argument(
        "--csv",
        type=Path,
        default=Path("performance_x86.csv"),
        help="Path to input CSV file (default: performance_x86.csv)",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("performance_x86_barplot.pdf"),
        help="Path to output figure file, e.g., .pdf or .png (default: performance_x86_barplot.pdf)",
    )
    parser.add_argument(
        "--width",
        choices=["single", "double", "wide"],
        default="double",
        help=(
            "Figure width preset: single (3.4in), double (7.0in), "
            "or wide (12in). Default: double"
        ),
    )
    parser.add_argument(
        "--bar-width",
        type=float,
        default=0.85,
        help="Bar width as fraction of category width (0.0-1.0). Default: 0.85",
    )
    parser.add_argument(
        "--bar-gap",
        type=float,
        default=None,
        help=(
            "Space between bars as fraction of category width (0.0-0.9). "
            "If set, overrides --bar-width. Example: --bar-gap 0.2"
        ),
    )
    parser.add_argument(
        "--crop",
        action="store_true",
        help="If set and output is PDF, try to run `pdfcrop` to trim margins.",
    )
    args = parser.parse_args()

    # Pass bar width and crop flag into the plotting function.
    plot_performance(
        args.csv, args.out, args.width, bar_width=args.bar_width, bar_gap=args.bar_gap, crop_pdf=args.crop
    )
    print(f"Saved plot to: {args.out}")


if __name__ == "__main__":
    main()