#!/usr/bin/env python3
import csv
import os
import sys
import re

def main():
    if len(sys.argv) not in (2, 3):
        print(f"Usage: {sys.argv[0]} <log_file_path> [csv_output_path]")
        sys.exit(1)

    log_path = sys.argv[1]
    csv_path = sys.argv[2] if len(sys.argv) == 3 else os.path.splitext(log_path)[0] + ".csv"
    benchmark_name = log_path.split("/build/")[0].split("/")[-1]
    base_log_name = os.path.basename(log_path)
    if "svf" in base_log_name:
        variant_name = "AADB"
    elif "baseline" in base_log_name:
        variant_name = "DefaultAA"
    else:
        variant_name = base_log_name
    count_gvn = 0
    total_gvn = 0
    load_gvn = 0
    total_load_gvn = 0
    total_inst_combine_deleted = 0
    total_inst_combine = 0
    total_licm_hoisted = 0
    total_slp_vector_instructions = 0
    total_loops_vectorized = 0
    total_instruction_simplified = 0
    total_NoAliasResults = 0
    total_MayAliasResults = 0
    total_MustAliasResults = 0
    total_execution_time = 0.0
    db_sizes = []
    total_svfs_queries = 0
    total_svfs_load_load_queries = 0
    total_noalias_pairs = 0
    total_svfs_not_found = 0
    total_svfs_not_found_percentage = 0.0
    utilization_values = []
    current_svfs_queries = 0
    current_svfs_load_load_queries = 0
    current_noalias_pairs = 0
    
    def parse_size_to_bytes(size_str):
        """Convert size string like '32 kB' or '14 MB' to bytes"""
        parts = size_str.strip().split()
        if len(parts) != 2:
            return None
        value = float(parts[0])
        unit = parts[1].lower()
        
        if unit == "kb":
            return int(value * 1024)
        elif unit == "mb":
            return int(value * 1024 * 1024)
        elif unit == "gb":
            return int(value * 1024 * 1024 * 1024)
        elif unit == "bytes":
            return int(value)
        return None
    
    try:
        with open(log_path, "r") as f:
            lines = f.readlines()
        
        i = 0
        while i < len(lines):
            line = lines[i]
            if "Number of instructions deleted" in line:
                parts = line.split()
                if parts and parts[0].isdigit():
                    count_gvn += 1
                    total_gvn += int(parts[0])
            elif "Number of loads deleted" in line:
                parts = line.split()
                if parts and parts[0].isdigit():
                    load_gvn += 1
                    total_load_gvn += int(parts[0])
            elif "Number of instructions simplified" in line:
                parts = line.split()
                if parts and parts[0].isdigit():
                    total_instruction_simplified += int(parts[0])
            elif "Number of dead inst eliminated" in line:
                parts = line.split()
                if parts and parts[0].isdigit():
                    total_inst_combine_deleted += int(parts[0])
            elif "Number of dead inst eliminated" in line:
                parts = line.split()
                if parts and parts[0].isdigit():
                    total_inst_combine += int(parts[0])
            elif "Number of instructions hoisted out of loop" in line:
                parts = line.split()
                if parts and parts[0].isdigit():
                    total_licm_hoisted += int(parts[0])
            elif "Number of vector instructions generated" in line:
                parts = line.split()
                if parts and parts[0].isdigit():
                    total_slp_vector_instructions += int(parts[0])
            elif "Number of loops vectorized" in line:
                parts = line.split()
                if parts and parts[0].isdigit():
                    total_loops_vectorized += int(parts[0])
            elif "Number of NoAlias results" in line:
                parts = line.split()
                if parts and parts[0].isdigit():
                    total_NoAliasResults += int(parts[0])
            elif "Number of MayAlias results" in line:
                parts = line.split()
                if parts and parts[0].isdigit():
                    total_MayAliasResults += int(parts[0])
            elif "Number of MustAlias results" in line:
                parts = line.split()
                if parts and parts[0].isdigit():
                    total_MustAliasResults += int(parts[0])
            elif "Total Alias pairs:" in line:
                # Total Alias pairs: 650
                parts = line.split()
                if len(parts) >= 4 and parts[3].isdigit():
                    pairs = int(parts[3])
                    total_noalias_pairs += pairs
                    current_noalias_pairs += pairs
                    if current_noalias_pairs > 0:
                        file_utilization = (
                            current_svfs_queries - current_svfs_load_load_queries
                        ) / current_noalias_pairs
                        utilization_values.append(file_utilization)
                    current_svfs_queries = 0
                    current_svfs_load_load_queries = 0
                    current_noalias_pairs = 0
            elif "svfs-aa" in line and "Number of SVFsAA queries that were load-load" in line:
                parts = line.split()
                if parts and parts[0].isdigit():
                    value = int(parts[0])
                    total_svfs_load_load_queries += value
                    current_svfs_load_load_queries += value
            elif re.search(r"Number of SVFsAA queries$", line.strip()):
                # Only count the exact line "Number of SVFsAA queries" without qualifiers
                parts = line.split()
                if parts and parts[0].isdigit():
                    value = int(parts[0])
                    total_svfs_queries += value
                    current_svfs_queries += value
            elif "TIME total build" in line: 
                # TIME total build: 0.123 seconds 
                match = re.search(r"TIME total build:\s*([0-9]+(?:\.[0-9]+)?)", line)
                if match:
                    total_execution_time += float(match.group(1))
            elif "Size of the table" in line:
                # Look for the size in the next few lines
                j = i + 1
                while j < len(lines) and j < i + 5:
                    size_line = lines[j].strip()
                    # Check if this line is a size (e.g., "32 kB" or "14 MB")
                    if re.match(r'^[0-9]+(?:\.[0-9]+)?\s+(bytes|kB|MB|GB)$', size_line):
                        size_bytes = parse_size_to_bytes(size_line)
                        if size_bytes is not None:
                            db_sizes.append(size_bytes)
                        break
                    j += 1
            elif "Number of Entries that are not answered by SVFsAA and default to MayAlias" in line:
                parts = line.split()
                if parts and parts[0].isdigit():
                    total_svfs_not_found += int(parts[0]) 
            i += 1

    except FileNotFoundError:
        print(f"Error: File not found: {log_path}")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

    print(f"Total GVN: {total_gvn}")
    print(f"Total loads deleted by GVN: {total_load_gvn}")
    print(f"Total instructions simplified by GVN: {total_instruction_simplified}")
    print(f"Total instructions combined by InstComb: {total_inst_combine}")
    print(f"Total Instruction combine dead instructions eliminated by InstComb: {total_inst_combine_deleted}")
    print(f"Total instructions hoisted out of loop by LICM: {total_licm_hoisted}")
    print(f"Total vector instructions generated by SLP: {total_slp_vector_instructions}")
    print(f"Total loops vectorized by LV: {total_loops_vectorized}")
    print(f"Total NoAlias results: {total_NoAliasResults}")
    print(f"Total MayAlias results: {total_MayAliasResults}")
    print(f"Total MustAlias results: {total_MustAliasResults}")
    print(f"Total execution time: {total_execution_time:.2f} seconds")
    print(f"Total Entries not answered by SVFsAA and defaulting to MayAlias: {total_svfs_not_found}")
    total_svfs_query_attempts = total_svfs_not_found + total_svfs_queries
    if total_svfs_query_attempts > 0:
        total_svfs_not_found_percentage = (
            total_svfs_not_found / total_svfs_query_attempts
        ) * 100
    print(
        "Percentage of queries not found in SVFsAA: "
        f"{total_svfs_not_found_percentage:.2f}%"
    )
    # Calculate database utilization as the mean of per-file utilization values.
    if utilization_values:
        db_utilization = sum(utilization_values) / len(utilization_values)
        print(f"\nDatabase Utilization Statistics:")
        print(f"Total SVFsAA queries: {total_svfs_queries}")
        print(f"Total SVFsAA load-load queries: {total_svfs_load_load_queries}")
        print(f"Total Alias pairs: {total_noalias_pairs}")
        print(f"Average per-file database utilization: {db_utilization:.6f}")
    else:
        print(f"Warning: No Alias pairs found in log")
    
    # Calculate database size statistics
    avg_db_size_bytes = sum(db_sizes) / len(db_sizes) if db_sizes else 0
    min_db_size_bytes = min(db_sizes) if db_sizes else 0
    max_db_size_bytes = max(db_sizes) if db_sizes else 0
    
    def format_bytes(bytes_val):
        """Convert bytes to human-readable format"""
        if bytes_val >= 1024 * 1024:
            return f"{bytes_val / (1024 * 1024):.2f} MB"
        elif bytes_val >= 1024:
            return f"{bytes_val / 1024:.2f} kB"
        else:
            return f"{bytes_val} bytes"
    
    print(f"\nDatabase sizes - Min: {format_bytes(min_db_size_bytes)}, Avg: {format_bytes(avg_db_size_bytes)}, Max: {format_bytes(max_db_size_bytes)}")

    fieldnames = [
        "benchmark",
        "variant",
        "log_path",
        "total_gvn",
        "total_load_gvn",
        "total_instruction_simplified",
        "total_inst_combine",
        "total_inst_combine_deleted",
        "total_licm_hoisted",
        "total_slp_vector_instructions",
        "total_loops_vectorized",
        "NoAlias",
        "MayAlias",
        "MustAlias",
        "total_execution_time_seconds",
        "min_db_size_bytes",
        "avg_db_size_bytes",
        "max_db_size_bytes",
        "total_svfs_queries",
        "total_svfs_load_load_queries",
        "total_noalias_pairs",
        "db_utilization",
        "total_svfs_not_found_percentage",
        "total_svfs_not_found"
    ]
    csv_exists = os.path.exists(csv_path) and os.path.getsize(csv_path) > 0
    with open(csv_path, "a", newline="") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=fieldnames)
        if not csv_exists:
            writer.writeheader()
        writer.writerow({
            "benchmark": benchmark_name,
            "variant": variant_name,
            "log_path": log_path,
            "total_gvn": total_gvn,
            "total_load_gvn": total_load_gvn,
            "total_instruction_simplified": total_instruction_simplified,
            "total_inst_combine": total_inst_combine,
            "total_inst_combine_deleted": total_inst_combine_deleted,
            "total_licm_hoisted": total_licm_hoisted,
            "total_slp_vector_instructions": total_slp_vector_instructions,
            "total_loops_vectorized": total_loops_vectorized,
            "NoAlias": total_NoAliasResults,
            "MayAlias": total_MayAliasResults,
            "MustAlias": total_MustAliasResults,
            "total_execution_time_seconds": f"{total_execution_time:.2f}",
            "min_db_size_bytes": int(min_db_size_bytes),
            "avg_db_size_bytes": int(avg_db_size_bytes),
            "max_db_size_bytes": int(max_db_size_bytes),
            "total_svfs_queries": total_svfs_queries,
            "total_svfs_load_load_queries": total_svfs_load_load_queries,
            "total_noalias_pairs": total_noalias_pairs,
            "db_utilization": f"{(total_svfs_queries) / (total_noalias_pairs - total_svfs_load_load_queries):.6f}" if total_noalias_pairs > 0 else "0",
            "total_svfs_not_found_percentage": f"{total_svfs_not_found_percentage:.2f}",
            "total_svfs_not_found": total_svfs_not_found
        })

    print(f"CSV saved to: {csv_path}")

if __name__ == "__main__":
    main()
