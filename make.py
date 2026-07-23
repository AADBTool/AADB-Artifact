#!/usr/bin/env python3
"""Build all benchmarks and generate the comparison plot.

This script runs the benchmark helper from ``Sources`` so the existing build logic
can collect the AADB/baseline results, then uses the plotting script to produce
the normalized compilation graph.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parent
SOURCES_DIR = ROOT_DIR / "Sources"
GRAPH_DIR = ROOT_DIR / "Graphing Scripts"
VENV_BIN_DIR = ROOT_DIR / "build" / "bin"

HELPER_SCRIPT = SOURCES_DIR / "make_all_x86.py"
COMPILATION_CSV = SOURCES_DIR / "benchmark_results_aadb_x86_icelake_1.csv"
COMPILATION_PLOT = GRAPH_DIR / "plot_compilation_bar.py"
ALIASES_PLOT = GRAPH_DIR / "static_number.py"
DEFAULT_COMPILATION_OUTPUT = ROOT_DIR / "aadb_compilation_normalized.pdf"
DEFAULT_ALIASES_OUTPUT = ROOT_DIR / "aadb_static_number.pdf"

def build_environment() -> dict[str, str]:
	"""Return an environment that prefers the repository's Python virtualenv."""
	env = os.environ.copy()
	if VENV_BIN_DIR.is_dir():
		env["PATH"] = f"{VENV_BIN_DIR}:{env.get('PATH', '')}"
	return env


def run_command(command: list[str], *, cwd: Path | None = None, env: dict[str, str] | None = None) -> None:
	"""Run a command and raise immediately if it fails."""
	print("+", " ".join(command))
	subprocess.run(command, cwd=cwd, env=env, check=True)


def build_benchmarks() -> None:
	if not HELPER_SCRIPT.exists():
		raise FileNotFoundError(f"Missing build helper: {HELPER_SCRIPT}")

	if not SOURCES_DIR.is_dir():
		raise FileNotFoundError(f"Missing Sources directory: {SOURCES_DIR}")

	run_command([sys.executable, str(HELPER_SCRIPT)], cwd=SOURCES_DIR, env=build_environment())


def draw_compilation_graph(output_path: Path) -> None:
	if not COMPILATION_PLOT.exists():
		raise FileNotFoundError(f"Missing plot script: {COMPILATION_PLOT}")

	if not COMPILATION_CSV.exists():
		raise FileNotFoundError(f"Missing compilation CSV: {COMPILATION_CSV}")

	run_command(
		[
			sys.executable,
			str(COMPILATION_PLOT),
			"--csv",
			str(COMPILATION_CSV),
			"--out",
			str(output_path),
		],
		cwd=ROOT_DIR,
		env=build_environment(),
	)

def draw_aliases_graph(output_path: Path) -> None:
	if not ALIASES_PLOT.exists():
		raise FileNotFoundError(f"Missing plot script: {ALIASES_PLOT}")

	if not COMPILATION_CSV.exists():
		raise FileNotFoundError(f"Missing compilation CSV: {COMPILATION_CSV}")

	run_command(
		[
			sys.executable,
			str(ALIASES_PLOT),
			str(COMPILATION_CSV),
			"-o",
			str(output_path),
		],
		cwd=ROOT_DIR,
		env=build_environment(),
	)

def parse_args() -> argparse.Namespace:
	parser = argparse.ArgumentParser(
		description="Build the benchmarks and generate the normalized compilation graph."
	)
	parser.add_argument(
		"--skip-build",
		action="store_true",
		help="Only draw the graph and skip the benchmark build step.",
	)
	parser.add_argument(
		"--skip-graph",
		action="store_true",
		help="Only build the benchmarks and skip graph generation.",
	)
	parser.add_argument(
		"--output",
		type=Path,
		default=DEFAULT_COMPILATION_OUTPUT,
		help="Output path for the compilation graph.",
	)
	parser.add_argument(
		"--aliases-output",
		type=Path,
		default=DEFAULT_ALIASES_OUTPUT,
		help="Output path for the aliases graph.",
	)
	return parser.parse_args()


def main() -> int:
	args = parse_args()

	if not args.skip_build:
		build_benchmarks()

	if not args.skip_graph:
		draw_compilation_graph(args.output)
		draw_aliases_graph(args.aliases_output)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())