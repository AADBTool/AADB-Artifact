#!/usr/bin/env python3
"""
Python equivalent of make_all_x86.sh for building and analyzing benchmarks.

Usage:
  ./make_all_x86.py                    # Default 1 run
  ./make_all_x86.py 3                  # Run 3 times
  runs=2 ./make_all_x86.py             # Set via environment variable
"""

import os
import sys
import subprocess
from pathlib import Path

PROGRAM_FOLDER = "Sources"
SPEC2017_FOLDER = "SPEC_INSTALL/benchspec/CPU"

# The script should be run from the Sources folder
SCRIPT_ROOT = os.getcwd()

def run_command(cmd, cwd=None, env=None):
    """Run a command and return the exit code."""
    print(f"+ {' '.join(cmd)}")
    run_env = os.environ.copy()
    if env:
        run_env.update(env)
    result = subprocess.run(cmd, cwd=cwd, env=run_env, capture_output=False, text=True)
    if result.returncode != 0:
        raise subprocess.CalledProcessError(result.returncode, cmd)
    return result.returncode

def build_one(spec_name, build_dir, display_name):
    """Build a single benchmark."""
    print(f"Making {spec_name}")
    print(display_name)
    
    # Convert to absolute path if needed
    build_path = Path(build_dir)
    if not build_path.is_absolute():
        build_path = Path.cwd() / build_path

    if not os.path.isdir(build_path):
        raise ValueError(f"The spec benchmark {spec_name} has not been built before")

    dirlist = os.listdir(build_path)
    dirlist = [f for f in dirlist if f.startswith("build")]
    if len(dirlist) == 0:
        raise ValueError(f"The spec benchmark {spec_name} has not been built before")
    
    # Remove old log files
    log_files = [
        build_path / "tmp_build_svf_opt_x86_icelake_1.log",
        build_path / "tmp_build_baseline_opt_x86_icelake_1.log"
    ]
    for log_file in log_files:
        if log_file.exists():
            log_file.unlink()
    
    # Build with AADB
    print("\n=== Building with AADB ===")
    run_command(["./build_aadb_all.py", "clean"], cwd=build_path)
    run_command(
        ["./build_aadb_all.py", "--log-output", "--log-file", "tmp_build_svf_opt_x86_icelake_not_found.log", "--time-build"],
        cwd=build_path,
        env={"RISCV_BUILD": "0"}
    )
    
    # Build baseline
    # print("\n=== Building baseline ===")
    # run_command(["./build_baseline_all.py", "clean"], cwd=build_path)
    # run_command(
    #     ["./build_baseline_all.py", "--log-output", "--log-file", "tmp_build_baseline_opt_x86_icelake_not_found.log", "--time-build"],
    #     cwd=build_path,
    #     env={"RISCV_BUILD": "0"}
    # )

def count_static(spec_name, build_dir, display_name, csv_output):
    """Collect static analysis numbers for a benchmark."""
    print(f"Counting {spec_name}")
    print(display_name)
    
    # Convert to absolute path if needed
    build_path = Path(build_dir)
    if not build_path.is_absolute():
        build_path = Path.cwd() / build_path
    
    # Run count_static_aadb.py for AADB build
    aadb_log = build_path / "tmp_build_svf_opt_x86_icelake_not_found.log"
    if aadb_log.exists():
        run_command(["python3", "count_static_aadb.py", str(aadb_log), csv_output])
    else:
        print(f"Warning: {aadb_log} not found")
    
    # Run count_static_aadb.py for baseline build
    baseline_log = build_path / "tmp_build_baseline_opt_x86_icelake_not_found.log"
    if baseline_log.exists():
        run_command(["python3", "count_static_aadb.py", str(baseline_log), csv_output])
    else:
        print(f"Warning: {baseline_log} not found")

def count_scale(spec_name, build_dir, display_name, csv_output):
    """Collect database build-time scaling summary for a benchmark."""
    print(f"Scaling {spec_name}")
    print(display_name)

    build_path = Path(build_dir)
    if not build_path.is_absolute():
        build_path = Path.cwd() / build_path

    aadb_log = build_path / "tmp_build_svf_opt_x86_icelake_not_found.log"
    if aadb_log.exists():
        run_command(["python3", "count_scale.py", str(aadb_log), "--summary-csv", csv_output])
    else:
        print(f"Warning: {aadb_log} not found")

def main():
    # Get number of runs from command line or environment
    runs = int(os.environ.get("runs", 1))
    if len(sys.argv) > 1:
        runs = int(sys.argv[1])
    
    # Define benchmarks: (spec_name, build_dir, display_name)
    benchmarks = [
        ("605.mcf", "605.mcf_s/build/build_base_riscv64.0000", "MCF"),
        ("625.x264", "625.x264_s/build/build_base_riscv64.0000", "X264"),
        ("631.deepsjeng", "631.deepsjeng_s/build/build_base_riscv64.0000", "DeepSjeng"),
        ("641.leela", "641.leela_s/build/build_base_riscv64.0000", "Leela"),
        ("657.xz", "657.xz_s/build/build_base_riscv64.0000", "XZ"),
        ("623.xalancbmk", "623.xalancbmk_s/build/build_base_riscv64.0000", "XalanCBMK"),
        ("620.omnetpp", "620.omnetpp_s/build/build_base_riscv64.0000", "Omnetpp"),
        ("602.gcc", "602.gcc_s/build/build_base_riscv64.0001", "GCC"),
        ("600.Perlbench", "600.perlbench_s/build/build_base_riscv64.0000", "Perlbench"),
        ("638.imagick", "638.imagick_s/build/build_base_riscv64.0000", "Imagick"),
        ("644.nab", "644.nab_s/build/build_base_riscv64.0000", "NAB"),
        ("619.lbm", "619.lbm_s/build/build_base_riscv64.0000", "LBM"),
    ]
    
    csv_output = "benchmark_results_aadb_x86_icelake_not_found.csv"
    scale_csv_output = "benchmark_results_aadb_x86_icelake_not_found_scale.csv"
    
    # Build phase
    print("=== BUILD PHASE ===\n")
    # for run_num in range(1, runs + 1):
    #     print(f"\n{'='*60}")
    #     print(f"Run #{run_num}")
    #     print(f"{'='*60}\n")
        
    #     for spec_name, build_dir, display_name in benchmarks:
    #         # copy build_aadb_all.py and build_baseline_all.py to the benchmark directory
    #         spec_build_path = Path(SPEC2017_FOLDER) / build_dir
    #         if not spec_build_path.is_absolute():
    #             spec_build_path = Path.cwd() / spec_build_path
    #         build_aadb_script = Path(SCRIPT_ROOT) / "build_aadb_all.py"
    #         build_baseline_script = Path(SCRIPT_ROOT) / "build_baseline_all.py"
    #         if build_aadb_script.exists():
    #             run_command(["cp", str(build_aadb_script), str(spec_build_path)])
    #         if build_baseline_script.exists():
    #             run_command(["cp", str(build_baseline_script), str(spec_build_path)])
    #         build_one(spec_name, spec_build_path, display_name)
        
    #     print(f"\nRun #{run_num} - Built all binaries successfully\n")
    
    # Static analysis phase
    print("\n=== STATIC ANALYSIS PHASE ===\n")
    for spec_name, build_dir, display_name in benchmarks:
        try:
            spec_build_path = Path(SPEC2017_FOLDER) / build_dir
            if not spec_build_path.is_absolute():
                spec_build_path = Path.cwd() / spec_build_path
            count_static(spec_name, spec_build_path, display_name, csv_output)
        except Exception as e:
            print(f"Error analyzing {spec_name}: {e}")
            continue
    
    print(f"\nResults saved to: {csv_output}")

    # Scaling summary phase
    print("\n=== SCALING SUMMARY PHASE ===\n")
    for spec_name, build_dir, display_name in benchmarks:
        try:
            spec_build_path = Path(SPEC2017_FOLDER) / build_dir
            if not spec_build_path.is_absolute():
                spec_build_path = Path.cwd() / spec_build_path
            count_scale(spec_name, spec_build_path, display_name, scale_csv_output)
        except Exception as e:
            print(f"Error summarizing {spec_name}: {e}")
            continue

    print(f"\nScaling summary saved to: {scale_csv_output}")

if __name__ == "__main__":
    main()
