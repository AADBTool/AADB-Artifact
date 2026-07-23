# Artifact Appendix
This is the supporting artifact for the paper titled "Reviving Legacy Alias Analyses in Modern Compilers Without Porting" as submitted to CGO 2027. It includes the source code for the proposed tool (AADB), along with scripts to compile and reproduce experimental results presented in the paper. We provide a Docker image containing all necessary dependencies preinstalled, enabling straightforward setup and execution of the experiments. The setup supports running the experiments on an x86 host, and it also allows cross-compilation for RISC-V ISA The reported results were obtained on Intel Xeon Silver 4314 processor ([Intel Xeon Silver 4314 Specifications](https://www.intel.com/content/www/us/en/products/sku/215269/intel-xeon-silver-4314-processor-24m-cache-2-40-ghz/specifications.html)) based on the Ice Lake server microarchitecture, and a SpacemiT K1 processor [SpacemiT K1 Datasheet](https://docs.banana-pi.org/en/BPI-F3/SpacemiT_K1_datasheet) with eight RISC-V cores supporting the RISC-V 64GCVB instruction set and the RVA22 standard.

## Artifact Check-List
- **Algorithm:** Database which stores and reuses the alias
analysis results in the optimization passes.
- **Program:** SPECCPU2017, not included in this repository, refer  to [http://www.spec.org/cpu2017] for downloading.
- **Compilation:** Publicly available and included in this artifact: LLVM v18.0 and SVF3.2
- **Transformations:** Modified LLVM pass manager which
can query the database to be used in the optimization passes
included in this artifact.
- **Binary:** Linux executables for x86 and RISC-V included in
this artifact and scripts to generate these binaries automatically.
- **Run-time environment:**
  1.  **x86** host CPU
  2. **RISC-V** host CPU
- **Hardware:** Intel and RISC-V CPU
- **Execution:** We recommend running the experiments in an isolated environment, as the results may vary if other processes are active.
- **Metrics:** Execution time and compiler reports which includes stats from passes such as LICM, LV, SLP and Compilation time.
- **Output:** CSV files containing the normalized execution times, performance improvements, and console logs reporting the performance improvements.
- **Experiments:** make.py file contains scripts to regenerate the results. Results may vary with respect to hardware parameters such as vector width, L1, L2 cache size.
- **Publicly available?:** Yes, github [AADBTool](https://github.com/AADBTool/AADB-Artifact/tree/main)

# Description
## How Delivered
This artifact is available at github [AADBTool](https://github.com/AADBTool/AADB-Artifact/tree/main)

## Hardware Dependencies
We evaluated on an Intel Xeon and SpacemiT K1 [https://docs.banana-pi.org/en/BPI-F3/SpacemiT_K1_datasheet] (RISC-V Core). AADB stores and provides precise alias analysis results for the optimization passes which may improve the performance. Performance improvement may vary with the hardware, therefore, we recommend similar platforms to reproduce the results.

## Software Dependencies. 
This has been tested on a Linux x86 host machine with Docker. For RISC-V, we recommend LINUX OS.

# Installation

Install the required dependencies by following their official installation guides:

1. **LLVM** – Follow the instructions in the [LLVM Installation Guide](https://llvm.org/docs/GettingStarted.html#getting-the-source-code-and-building-llvm$0).
2. **SVF** – Follow the instructions in the [SVF Documentation](https://github.com/SVF-tools/SVF).
3. **PostgreSQL** – Follow the instructions in the [PostgreSQL Documentation](https://www.postgresql.org/download/).
4. **RISC-V GNU Toolchain** – Follow the instructions in the [RISC-V GNU Toolchain Repository](https://github.com/riscv-collab/riscv-gnu-toolchain#risc-v-gnu-compiler-toolchain$0).
5. Clone the AADB toolchain repository:

   ```bash
   git clone <repository_url>
   ```
6. Install **SPEC CPU® 2017** by following the instructions provided with your licensed distribution from SPEC.

# Experiment Workflow
## x86 
To compile, run, and verify the results discussed in this paper (Table 4, Figure 5 and 6, on host machine (x86 Intel), navigate to the directory where AADB toolchain is installed and run the command: 

```bash
python3 make.py
```

This command will compile the benchmarks as described in section section 3, gather the execution time, static numbers as reported in Table 4 and compilation time numbers, and generate CSV files.

## RISC-V
To cross-compile and generate binaries for RISC-V, run the command: 

```bash
python3 make.py
```

This command will cross-compile all the SPEC CPU 2017 specified in the subsection 4.2 and the benchmarks are compiled as described in section 3, and the statically linked ELFs will be available on the host machine ${AADB clone dir}/Sources/SPECCPU2017 along with run_all_riscv.sh script to run the files on the RISC-V CPU.

# Evaluation and Expected Results
We expect AADB will enhance code optimization and reduce execution time, but the performance improvements may vary across different hardware platforms, since the code generation at middle are hardware-dependent. If experimental setup is as specified in the methodology section then we expect to get the same results as in the results section.

