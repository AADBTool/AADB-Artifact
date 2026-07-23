import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Patch

from argparse import ArgumentParser
from csv import reader
from pathlib import Path

def load_csv_table(csv_path: Path) -> tuple[str, list[str], list[str], dict[str, list[float]]]:
    with csv_path.open(newline="", encoding="utf-8") as handle:
        rows = [[cell.strip() for cell in row] for row in reader(handle) if any(cell.strip() for cell in row)]

    if len(rows) < 2:
        raise ValueError(f"{csv_path} must contain a header row and at least one data row")

    header = rows[0]
    if len(header) < 2:
        raise ValueError(f"{csv_path} must contain at least one label column and one value column")

    title = header[0]
    series_names = header[1:]
    labels = []
    series_values = {name: [] for name in series_names}

    for row in rows[1:]:
        # Pad short rows with empty strings so indexing matches the header
        if len(row) < len(header):
            row = row + [""] * (len(header) - len(row))
        labels.append(row[0])
        for index, name in enumerate(series_names, start=1):
            raw = row[index]
            if raw == "":
                val = 0.0
            else:
                try:
                    val = float(raw)
                except ValueError:
                    val = 0.0
            series_values[name].append(val)

    return title, labels, series_names, series_values


def load_alias_distribution_csv(
    csv_path: Path,
) -> tuple[list[str], list[str], list[str], dict[str, dict[str, dict[str, float]]]] | None:
    with csv_path.open(newline="", encoding="utf-8") as handle:
        rows = [[cell.strip() for cell in row] for row in reader(handle) if any(cell.strip() for cell in row)]

    if len(rows) < 2:
        return None

    header = rows[0]
    index_map = {name.lower(): index for index, name in enumerate(header)}
    if "benchmark" not in index_map or "variant" not in index_map:
        return None

    desired_alias_order = ["MayAlias", "NoAlias", "MustAlias"]
    name_map = {name.lower(): name for name in header}
    alias_columns = [name_map[name.lower()] for name in desired_alias_order if name.lower() in name_map]
    if not alias_columns:
        return None

    benchmark_index = index_map["benchmark"]
    variant_index = index_map["variant"]

    benchmarks: list[str] = []
    variants: list[str] = []
    agg: dict[str, dict[str, dict[str, float]]] = {}

    for row in rows[1:]:
        if len(row) <= max(benchmark_index, variant_index):
            continue

        benchmark = row[benchmark_index].strip()
        variant = row[variant_index].strip()
        if not benchmark or not variant:
            continue

        if benchmark not in agg:
            agg[benchmark] = {}
            benchmarks.append(benchmark)
        if variant not in variants:
            variants.append(variant)
        if variant not in agg[benchmark]:
            agg[benchmark][variant] = {name: 0.0 for name in alias_columns}

        for alias_name in alias_columns:
            alias_index = index_map[alias_name.lower()]
            raw = row[alias_index] if alias_index < len(row) else ""
            try:
                value = float(raw) if raw else 0.0
            except ValueError:
                value = 0.0
            agg[benchmark][variant][alias_name] += value

    if not benchmarks or not variants:
        return None

    return benchmarks, variants, alias_columns, agg

def build_plot(csv_path: Path, output_path: Path) -> None:
    alias_data = load_alias_distribution_csv(csv_path)
    if alias_data is None:
        title, labels, series_names, series_values = load_csv_table(csv_path)
        # Keep only the alias-related series (case-insensitive) in the requested order
        desired = ["MayAlias", "NoAlias", "MustAlias"]
        name_map = {name.lower(): name for name in series_names}
        filtered_series = [name_map[d.lower()] for d in desired if d.lower() in name_map]
        if not filtered_series:
            raise ValueError(f"{csv_path} must contain at least one of MayAlias/NoAlias/MustAlias columns")

        # Use the filtered series for plotting and present labels in lowercase
        series_names = filtered_series
        legend_labels = series_names[:]

        benchmarks: list[str] = []
        variants: list[str] = []
        agg: dict[str, dict[str, dict[str, float]]] = {}

        for row_idx, label in enumerate(labels):
            base, variant = label.rsplit("_", 1) if "_" in label else (label, "")
            if base not in agg:
                agg[base] = {}
                benchmarks.append(base)
            if variant and variant not in variants:
                variants.append(variant)
            if variant not in agg[base]:
                agg[base][variant] = {s: 0.0 for s in series_names}
            for s in series_names:
                agg[base][variant][s] += series_values[s][row_idx]
    else:
        benchmarks, variants, series_names, agg = alias_data
        legend_labels = series_names[:]

    if not benchmarks:
        raise ValueError("No benchmarks found to plot after grouping")

    variants = [variant for variant in ["DefaultAA", "AADB"] if variant in variants]
    if not variants:
        raise ValueError(f"{csv_path} must contain DefaultAA and/or AADB rows")

    normalized: dict[str, dict[str, dict[str, float]]] = {}
    for benchmark in benchmarks:
        normalized[benchmark] = {}
        for variant in variants:
            row_values = agg[benchmark].get(variant)
            if row_values is None:
                raise ValueError(f"Variant {variant!r} is missing for benchmark {benchmark!r} in {csv_path}")

            total = sum(row_values.values())
            if total == 0:
                normalized[benchmark][variant] = {name: 0.0 for name in series_names}
            else:
                normalized[benchmark][variant] = {
                    name: row_values[name] / total * 100.0 for name in series_names
                }

    mean_key = "Average"
    normalized[mean_key] = {}
    for variant in variants:
        mean_values = {}
        for series_name in series_names:
            values = np.array([
                normalized[benchmark][variant][series_name]
                for benchmark in benchmarks
                if benchmark in normalized and variant in normalized[benchmark]
            ], dtype=float)
            values = values[np.isfinite(values) & (values > 0)]
            if len(values) == 0:
                mean_values[series_name] = 0.0
            else:
                mean_values[series_name] = float(np.mean(values))

        normalized[mean_key][variant] = mean_values

        base_fs = 10
        label_fs = 9
        title_fs = 11
        tick_fs = 8
        value_label_fs = 8
    plt.rcParams.update(
        {
            # "font.family": "serif",
            # "font.serif": ["Times New Roman", "Times", "DejaVu Serif"],
            "font.family": "sans-serif",
            "font.sans-serif": ["Helvetica", "Arial", "DejaVu Sans"],
            # Slightly larger than default for readability in paper figures
            "font.size": base_fs,
            "axes.labelsize": label_fs,
            "axes.titlesize": title_fs,
            "xtick.labelsize": tick_fs,
            "ytick.labelsize": tick_fs,
            "legend.fontsize": 9,
            # Embed fonts in PDF output for compatibility with Overleaf/acmart
            "pdf.fonttype": 42,
            "ps.fonttype": 42,
        }
    )

    plot_benchmarks = benchmarks + [mean_key]
    x = np.arange(len(plot_benchmarks))
    group_width = 0.72
    bar_width = group_width / max(len(variants), 1)
    offsets = np.linspace(
        -group_width / 2 + bar_width / 2,
        group_width / 2 - bar_width / 2,
        max(len(variants), 1),
    )

    fig, ax = plt.subplots(figsize=(max(7.5, len(benchmarks) * 0.45), 3.0))

    category_colors = {
        "MayAlias": "#4C78A8",
        "NoAlias": "#F58518",
        "MustAlias": "#54A24B",
    }
    variant_hatches = {
        "DefaultAA": "",
        "AADB": "///",
    }

    for variant, offset in zip(variants, offsets):
        bottoms = np.zeros(len(plot_benchmarks))
        for series_name in series_names:
            values = np.array([normalized[benchmark][variant][series_name] for benchmark in plot_benchmarks])
            ax.bar(
                x + offset,
                values,
                bar_width,
                bottom=bottoms,
                color=category_colors.get(series_name),
                edgecolor="black",
                linewidth=0.5,
                hatch=variant_hatches.get(variant, ""),
            )
            bottoms = bottoms + values

    ax.set_ylabel("Distribution of alias responses (%)")
    ax.set_xticks(x)
    ax.set_xticklabels(plot_benchmarks, rotation=45, ha="right")
    ax.tick_params(axis="x", pad=1)
    ax.set_ylim(0, 101)
    ax.set_yticks([0, 25, 50, 75, 100])
    ax.grid(axis="y", linestyle="--", linewidth=0.5, alpha=0.45)
    ax.set_axisbelow(True)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.margins(x=0.01)

    category_handles = [
        Patch(facecolor=category_colors[name], edgecolor="black", label=label)
        for name, label in zip(series_names, legend_labels)
    ]
    variant_handles = [
        Patch(facecolor="white", edgecolor="black", hatch=variant_hatches[name], label=name)
        for name in variants
    ]
    legend_handles = category_handles + variant_handles
    fig.legend(
        handles=legend_handles,
        loc="upper center",
        bbox_to_anchor=(0.5, 0.85),
        ncols=len(legend_handles),
        frameon=False,
        columnspacing=0.8,
        handlelength=1.2,
        handletextpad=0.35,
        borderaxespad=0.0,
    )
    fig.subplots_adjust(top=0.76, bottom=0.16)

    # Print average normalized percentages (arithmetic mean across benchmarks)
    if "Average" in normalized:
        print("Average normalized share (%) per category (arithmetic mean across benchmarks):")
        for variant in variants:
            parts = [f"{series}: {normalized['Average'][variant][series]:.1f}%" for series in series_names]
            print(f"  {variant}: " + ", ".join(parts))

    fig.savefig(output_path, format="pdf", bbox_inches="tight")

def main() -> None:
    parser = ArgumentParser(description="Plot a grouped bar chart from a CSV file")
    parser.add_argument("csv_path", nargs="?", default="static_numbers.csv", help="Path to the CSV file")
    parser.add_argument("-o", "--output", default="static_numbers_1.pdf", help="Path to the output PDF")
    args = parser.parse_args()

    build_plot(Path(args.csv_path), Path(args.output))

if __name__ == "__main__":
    main()