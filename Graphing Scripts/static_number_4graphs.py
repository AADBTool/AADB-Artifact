import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import MaxNLocator

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
        if len(row) < len(header):
            raise ValueError(f"Row {row!r} in {csv_path} has fewer columns than the header")
        labels.append(row[0])
        for index, name in enumerate(series_names, start=1):
            series_values[name].append(float(row[index]))

    return title, labels, series_names, series_values


def y_axis_limits(values: list[float]) -> tuple[float, float]:
    data_min = min(values)
    data_max = max(values)
    spread = data_max - data_min
    padding = max(spread * 0.12, data_max * 0.08 if data_max > 0 else 1)

    lower = 0 if data_min >= 0 else data_min - padding
    upper = data_max + padding

    if upper <= lower:
        upper = lower + 1

    return lower, upper


def split_label(label: str) -> tuple[str, str]:
    if "_" not in label:
        return label, ""

    benchmark, variant = label.rsplit("_", 1)
    return benchmark, variant


def build_plot(csv_path: Path, output_path: Path) -> None:
    title, labels, series_names, series_values = load_csv_table(csv_path)
    requested_series = ["GVN", "LICM", "LV", "SLP"]
    plot_series = [name for name in requested_series if name in series_values]

    if not plot_series:
        raise ValueError(f"None of {requested_series!r} were found in {csv_path}")

    grouped_labels: list[str] = []
    variant_names: list[str] = []
    grouped_indices: dict[str, dict[str, int]] = {}

    for index, label in enumerate(labels):
        benchmark, variant = split_label(label)
        if benchmark not in grouped_indices:
            grouped_indices[benchmark] = {}
            grouped_labels.append(benchmark)
        if variant not in variant_names:
            variant_names.append(variant)
        grouped_indices[benchmark][variant] = index

    x = np.arange(len(grouped_labels))
    group_width = 0.9
    bar_width = group_width / max(len(variant_names), 1)
    variant_offsets = np.linspace(-group_width / 2 + bar_width / 2, group_width / 2 - bar_width / 2, max(len(variant_names), 1))
    palette = ["#4C78A8", "#F58518", "#54A24B", "#E45756"]
    variant_colors = {variant: palette[index % len(palette)] for index, variant in enumerate(variant_names)}

    fig, axes = plt.subplots(
        2,
        2,
        layout="constrained",
        figsize=(18, 12),
    )

    axes = axes.flatten()

    for axis, name in zip(axes, plot_series):
        for offset, variant in zip(variant_offsets, variant_names):
            values = []
            for benchmark in grouped_labels:
                row_index = grouped_indices[benchmark].get(variant)
                if row_index is None:
                    raise ValueError(f"Variant {variant!r} is missing for benchmark {benchmark!r} in {csv_path}")
                values.append(series_values[name][row_index])

            rects = axis.bar(x + offset, values, bar_width, label=variant, color=variant_colors.get(variant))
            axis.bar_label(rects, padding=3, fontsize=6)

        axis.set_title(name)
        axis.set_xticks(x)
        axis.set_xticklabels(grouped_labels, rotation=45, ha="right")
        axis.grid(axis="y", linestyle=":", alpha=0.35)
        metric_values = [series_values[name][grouped_indices[benchmark][variant]] for benchmark in grouped_labels for variant in variant_names]
        axis.set_ylim(*y_axis_limits(metric_values))
        axis.yaxis.set_major_locator(MaxNLocator(nbins=5))

    for axis in axes[len(plot_series):]:
        axis.axis("off")

    axes[0].set_ylabel("Value")
    fig.suptitle(title)
    handles, legend_labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, legend_labels, loc="upper center", ncols=max(len(variant_names), 1), frameon=False)

    fig.savefig(output_path, format="pdf", bbox_inches="tight")


def main() -> None:
    parser = ArgumentParser(description="Plot a grouped bar chart from a CSV file")
    parser.add_argument("csv_path", nargs="?", default="static_numbers.csv", help="Path to the CSV file")
    parser.add_argument("-o", "--output", default="static_numbers.pdf", help="Path to the output PDF")
    args = parser.parse_args()

    build_plot(Path(args.csv_path), Path(args.output))

if __name__ == "__main__":
    main()