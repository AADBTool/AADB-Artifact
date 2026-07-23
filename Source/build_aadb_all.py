#!/usr/bin/env python3
"""
Python equivalent of Makefile.aadb for 657.xz_s.

Usage examples:
  ./build_aadb.py
  ./build_aadb.py build
  ./build_aadb.py -j 8 build
  ./build_aadb.py print-config
  ./build_aadb.py clean
  ./build_aadb.py deepclean

Override variables using environment variables, for example:
  TARGET_TRIPLE=riscv64-unknown-linux-gnu ./build_aadb.py
  EXTRA_COMPILE_FLAGS='-g -O2' ./build_aadb.py
  RUN_SVF=0 ./build_aadb.py
  RISCV_COMPILE_FLAGS='--sysroot=/path/to/riscv64/sysroot --gcc-toolchain=/path/to/riscv64/toolchain' ./build_aadb.py
  RISCV_BUILD=1 ./build_aadb.py #for RISCV builds
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, TextIO


def env(name: str, default: str) -> str:
    return os.environ.get(name, default)


def as_bool(value: str) -> bool:
    return str(value).strip().lower() in {"1", "true", "yes", "on"}


@dataclass
class Config:
    # Bench defaults
    exebase: str
    sources: List[Path]
    deps_map: Dict[str, List[Path]]
    # Tooling
    clang: str
    opt_bin: str
    llc_bin: str
    wpa_bin: str
    extapi_bc: str
    drop_table_script: str

    # Target config
    riscv_build: bool
    target_triple: str
    march: str
    mtune: str
    mcpu: str
    llc_mcpu: str
    aadb_opt_level: str
    riscv_compile_flags: str

    # Flags
    cppflags_common: str
    cflags_common: str
    extra_compile_flags: str
    bench_cxx_flags: str
    bench_c_flags: str

    linker: str
    link_flags: str
    link_libs: str

    # Pipeline controls
    run_svf: bool
    run_drop_table: bool
    enable_rewrite_annotation: bool
    aa_pipeline_default: str
    aa_pipeline_fallback: str
    aa_pipeline_noscev: str
    aa_default_pipeline_sources: List[str]
    aa_noscev_pipeline_sources: List[str]
    annotate_passes: str
    annotate_re_passes: str

    # Hooks
    pre_annotate_hook: str
    post_annotate_hook: str
    pre_aadb_hook: str
    post_aadb_hook: str
    post_object_hook: str

    output_bin: str
    
    # Optional fields with defaults
    skipped_scev_sources_x86: List[Path] = None
    skipped_scev_sources_riscv: List[Path] = None
    skipped_scev_sources: List[Path] = None
    skipped_sources_fallback_default_aa: List[Path] = None


@dataclass
class RuntimeOptions:
    enable_timing: bool
    log_enabled: bool
    log_file: Path

class OutputTee:
    def __init__(self, enabled: bool, log_file: Path):
        self.enabled = enabled
        self.log_file = log_file
        self._fh: TextIO | None = None

    def open(self) -> None:
        if not self.enabled:
            return
        self.log_file.parent.mkdir(parents=True, exist_ok=True)
        self._fh = self.log_file.open("w", encoding="utf-8")

    def close(self) -> None:
        if self._fh is not None:
            self._fh.close()
            self._fh = None

    def write_line(self, line: str) -> None:
        print(line, flush=True)
        if self._fh is not None:
            self._fh.write(line + "\n")
            self._fh.flush()


TEE: OutputTee | None = None


def emit(line: str) -> None:
    if TEE is None:
        print(line, flush=True)
        return
    TEE.write_line(line)


def split_words(value: str) -> List[str]:
    return [x for x in shlex.split(value) if x]


def unique_paths(paths: List[Path]) -> List[Path]:
    seen = set()
    out: List[Path] = []
    for p in paths:
        key = p.as_posix()
        if key in seen:
            continue
        seen.add(key)
        out.append(p)
    return out


def format_duration_ns(elapsed_ns: int) -> str:
    return f"{elapsed_ns / 1_000_000_000:.6f}s"


def read_makefile_var(path: Path, var_name: str) -> str | None:
    if not path.exists():
        return None

    # Accept Makefile forms like: VAR=value, VAR = value, VAR?=value, VAR+=value
    assign_re = re.compile(rf"^\s*{re.escape(var_name)}\s*(?:\?|\+|:)?=\s*(.*)$")
    with path.open("r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    i = 0
    while i < len(lines):
        raw = lines[i].rstrip("\n")
        m = assign_re.match(raw)
        if m:
            value = m.group(1).strip()

            while value.endswith("\\") and i + 1 < len(lines):
                value = value[:-1].rstrip()
                i += 1
                nxt = lines[i].rstrip("\n").strip()
                value = f"{value} {nxt}".strip()
            return value
        i += 1

    return None


def parse_makefile_deps(path: Path) -> Dict[str, List[Path]]:
    deps_map: Dict[str, List[Path]] = {}
    if not path.exists():
        return deps_map

    rule_re = re.compile(r"^\$\(addsuffix \$\(OBJ\), \$\(basename\s+(.+?)\)\):\s*(.*)$")

    with path.open("r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    i = 0
    while i < len(lines):
        line = lines[i].rstrip("\n")
        m = rule_re.match(line.strip())
        if not m:
            i += 1
            continue

        src = m.group(1).strip()
        dep_text = m.group(2).strip()

        while dep_text.endswith("\\") and i + 1 < len(lines):
            dep_text = dep_text[:-1].rstrip()
            i += 1
            dep_text = f"{dep_text} {lines[i].strip()}".strip()

        deps = [Path(p) for p in split_words(dep_text) if p]
        deps_map[src] = deps
        i += 1

    return deps_map


def load_config() -> Config:
    spec_path = Path("Makefile.spec")

    sources_raw = os.environ.get("SOURCES")
    if not sources_raw:
        sources_raw = read_makefile_var(spec_path, "SOURCES")
 
    if sources_raw:
        sources = [Path(s) for s in split_words(sources_raw)]
    else:
        sources = [Path(p) for p in sorted(glob.glob("*.c"))]
    sources = unique_paths(sources)

    name = os.environ.get("EXEBASE")
    if name is None:
        name = read_makefile_var(spec_path, "EXEBASE") or ""
        print(f"Detected EXEBASE={name}")

    if not name:
        print("Error: Executable base name not found. Set 'name' variable in Makefile.spec or provide SOURCES.", file=sys.stderr)
        sys.exit(1)

    bench_flags = os.environ.get("BENCH_FLAGS")
    if bench_flags is None:
        bench_flags = read_makefile_var(spec_path, "BENCH_FLAGS") or ""

    bench_c_flags = os.environ.get("BENCH_CFLAGS")
    if bench_c_flags is None:
        bench_c_flags = read_makefile_var(spec_path, "BENCH_CFLAGS") or ""

    extra_optimize = os.environ.get("EXTRA_OPTIMIZE")
    if extra_optimize is None:
        extra_optimize = read_makefile_var(spec_path, "EXTRA_OPTIMIZE") or ""

    extra_coptimize = os.environ.get("EXTRA_COPTIMIZE")
    if extra_coptimize is None:
        extra_coptimize = read_makefile_var(spec_path, "EXTRA_COPTIMIZE") or ""

    portability_flags = os.environ.get("PORTABILITY")
    if portability_flags is None:
        portability_flags = read_makefile_var(spec_path, "PORTABILITY") or ""

    extra_portability = os.environ.get("EXTRA_PORTABILITY")
    if extra_portability is None:
        extra_portability = read_makefile_var(spec_path, "EXTRA_PORTABILITY") or ""

    ldcflags = os.environ.get("LDCFLAGS")
    if ldcflags is None:
        ldcflags = read_makefile_var(spec_path, "LDCFLAGS") or ""

    extra_ldflags = os.environ.get("EXTRA_LDFLAGS")
    if extra_ldflags is None:
        extra_ldflags = read_makefile_var(spec_path, "EXTRA_LDFLAGS") or ""

    bench_cxx_flags = os.environ.get("BENCH_CXXFLAGS")
    if bench_cxx_flags is None:
        bench_cxx_flags = read_makefile_var(spec_path, "BENCH_CXXFLAGS") or ""

    extra_libs = env("EXTRA_LIBS", "")
    libs = env("LIBS", "")

    cppflags_common = env("CPPFLAGS_COMMON", f"-DSPEC -DNDEBUG {bench_flags}".strip())
    cflags_common = env(
        "CFLAGS_COMMON",
        f"-O3 {extra_optimize} {extra_coptimize} {extra_portability} {portability_flags}".strip(),
    )
    # The following lists of sources are skipped for SCEV alias analysis due to bug in LLVM.
    if name == "perlbench_s":
        skipped_scev_sources_x86 = ["toke.c"]
    elif name == "sgcc_s":
        skipped_scev_sources_x86 = ["lambda-mat.c", "omega.c", "regcprop.c", "tree-cfg.c", "tree-vect-slp.c", "var-tracking.c"]
    elif name == "omnetpp_s":
        skipped_scev_sources_x86 = ["model/MACAddress.cc"]
    elif name == "imagick_s":
        skipped_scev_sources_x86 = ["magick/decorate.c", "magick/distort.c", "magick/draw.c", "magick/xml-tree.c"]
    else:
        skipped_scev_sources_x86 = []

    if name == "perlbench_s":
        skipped_scev_sources_riscv = ["toke.c", "cpan/HTML-Parser/Parser.c"]
    elif name == "sgcc_s":
        skipped_scev_sources_riscv = ["lambda-mat.c", "omega.c", "regcprop.c", "tree-cfg.c", "tree-vect-slp.c", "var-tracking.c", "fold-const.c", "ira-build.c", "ira-costs.c", "ira-color.c", "lambda-code.c", "postreload-gcse.c", "profile.c", "sbitmap.c", "simplify-rtx.c"]
    elif name == "omnetpp_s":
        skipped_scev_sources_riscv = ["model/MACAddress.cc"]
    elif name == "xalancbmk_s":
        skipped_scev_sources_riscv = ["DFAContentModel.cpp", "DOMRangeImpl.cpp", "ElemNumber.cpp", "GeneralAttributeCheck.cpp", "GrammarResolver.cpp", "IGXMLScanner.cpp", "SGXMLScanner.cpp", "SchemaInfo.cpp", "TraverseSchema.cpp", "XalanXMLSerializerFactory.cpp", "XMLURL.cpp", "XPathMatcher.cpp", "XTemplateSerializer.cpp", "XercesXPath.cpp"]
    elif name == "x264_s":
        skipped_scev_sources_riscv = ["x264_src/common/mc.c", "x264_src/encoder/encoder.c"]
    elif name == "imagick_s":
        skipped_scev_sources_riscv = ["magick/decorate.c", "magick/distort.c", "magick/draw.c", "magick/xml-tree.c", "magick/attribute.c", "magick/compare.c", "magick/effect.c", "magick/matrix.c", "magick/morphology.c", "magick/quantize.c", "magick/segment.c"]
    else:
        skipped_scev_sources_riscv = []

    # The following lists of sources are skipped for SVF alias analysis due to bug in SVF.
    skipped_sources_fallback_default_aa = []
    if name == "nab_s":
        skipped_sources_fallback_default_aa = ["nblist.c"]
    elif name == "xz_s":
        skipped_sources_fallback_default_aa = ["liblzma/lzma/lzma_decoder.c"]
    elif name == "sgcc_s":
        skipped_sources_fallback_default_aa = ["insn-recog.c", "ira-lives.c", "insn-attrtab.c"]

    riscv_build = as_bool(env("RISCV_BUILD", "0"))

    if riscv_build:
        target_triple = env("TARGET_TRIPLE", "riscv64-unknown-linux-gnu")
        march = env("MARCH", "spacemit")
        mtune = env("MTUNE", "spacemit")
        mcpu = env("MCPU", "spacemit")
        llc_mcpu = env("LLC_MCPU", "spacemit")
        skipped_scev_sources = skipped_scev_sources_riscv
    else:
        #if you have Icelake server cpu replace native with "icelake-server"
        target_triple = env("TARGET_TRIPLE", "x86_64-unknown-linux-gnu")
        march = env("MARCH", "icelake-server")
        mtune = env("MTUNE", "icelake-server")
        mcpu = env("MCPU", "icelake-server")
        llc_mcpu = env("LLC_MCPU", "icelake-server")
        skipped_scev_sources = skipped_scev_sources_x86

    need_math = os.environ.get("NEED_MATH")
    if need_math is None:
        need_math = read_makefile_var(spec_path, "NEED_MATH") or "0"
    if as_bool(need_math):
        extra_libs = f"{extra_libs} -lm".strip()

    benchlang = os.environ.get("BENCHLANG")
    if benchlang is None:
        benchlang = read_makefile_var(spec_path, "BENCHLANG")
        print(f"Detected BENCHLANG={benchlang}")

    if benchlang == "C":
        clang = env("CLANG", "/artifact/llvm-project/build/bin/clang")
    else:
        clang = env("CLANG", "/artifact/llvm-project/build/bin/clang++")

    if benchlang is None:
        print("Warning: BENCHLANG not set, defaulting to C. Set BENCHLANG=C or BENCHLANG=CXX to specify.", file=sys.stderr)
        sys.exit(1)

    aadb_opt_level = env("AADB_OPT_LEVEL", "-O3")

    riscv_compile_flags = env("RISCV_COMPILE_FLAGS", "--sysroot=/artifact/riscv_toolchain_install_dir/sysroot --gcc-toolchain=/artifact/riscv_toolchain_install_dir/")

    link_flags_val = env(
        "LINK_FLAGS",
        f"-static -O3 -target {target_triple} -march={march} -mtune={mtune} {ldcflags} {extra_ldflags}".strip(),
    )


    return Config(
        exebase=name,
        sources=sources,
        deps_map=parse_makefile_deps(Path("Makefile.deps")),
        clang=clang,
        riscv_build=riscv_build,
        riscv_compile_flags=riscv_compile_flags,
        opt_bin=env("OPT_BIN", "/artifact/llvm-project/build/bin/opt"),
        llc_bin=env("LLC_BIN", "/artifact/llvm-project/build/bin/llc"),
        wpa_bin=env("WPA_BIN", "/artifact/SVF-16.0.0/SVF/Release-build/bin/wpa"),
        extapi_bc=env("EXTAPI_BC", "/artifact/SVF-16.0.0/SVF/Release-build/lib/extapi.bc"),
        drop_table_script=env("DROP_TABLE_SCRIPT", "/artifact/Sources/drop_table_postgressql.sh"),
        target_triple=target_triple,
        march=march,
        mtune=mtune,
        mcpu=mcpu,
        llc_mcpu=llc_mcpu,
        aadb_opt_level=aadb_opt_level,
        cppflags_common=cppflags_common,
        cflags_common=cflags_common,
        bench_c_flags=bench_c_flags,
        extra_compile_flags=env("EXTRA_COMPILE_FLAGS", ""),
        bench_cxx_flags=bench_cxx_flags,
        linker=env("LINKER", clang),
        link_flags=link_flags_val,

        link_libs=env("LINK_LIBS", f"{extra_libs} {libs}".strip()),
        run_svf=as_bool(env("RUN_SVF", "1")),
        run_drop_table=as_bool(env("RUN_DROP_TABLE", "1")),
        enable_rewrite_annotation=as_bool(env("ENABLE_REWRITE_ANNOTATION", "1")),
        aa_pipeline_default=env("AA_PIPELINE_DEFAULT", "caps-umu"),
        aa_pipeline_fallback=env("AA_PIPELINE_FALLBACK", "default_chained"),
        aa_pipeline_noscev=env("AA_PIPELINE_NO_SCEV", "noscev"),
        aa_default_pipeline_sources=split_words(
            env("AA_DEFAULT_PIPELINE_SOURCES", " ".join(skipped_sources_fallback_default_aa))
        ),
        aa_noscev_pipeline_sources=split_words(
            env("AA_NO_SCEV_PIPELINE_SOURCES", " ".join(skipped_scev_sources))
        ),
        annotate_passes=env("ANNOTATE_PASSES", "annotate-loads-stores,annotate-calls"),
        annotate_re_passes=env("ANNOTATE_RE_PASSES", "annotate-loads-stores-re,annotate-calls-re"),
        pre_annotate_hook=env("PRE_ANNOTATE_HOOK", ":"),
        post_annotate_hook=env("POST_ANNOTATE_HOOK", ":"),
        pre_aadb_hook=env("PRE_AADB_HOOK", ":"),
        post_aadb_hook=env("POST_AADB_HOOK", ":"),
        post_object_hook=env("POST_OBJECT_HOOK", ":"),
        output_bin=env("OUTPUT_BIN", f"{name}_aadb_py_icelake_server"),
    )


def run_cmd(args: List[str]) -> None:
    emit("+ " + " ".join(shlex.quote(a) for a in args))
    proc = subprocess.Popen(
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    assert proc.stdout is not None
    for line in proc.stdout:
        emit(line.rstrip("\n"))
    rc = proc.wait()
    if rc != 0:
        raise subprocess.CalledProcessError(rc, args)


def run_hook(hook: str) -> None:
    hook = hook.strip()
    if not hook or hook == ":":
        return
    run_cmd(["/bin/sh", "-c", hook])


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def normalize_stem(src: Path) -> str:
    """Normalize source stem by replacing special characters (. - etc) with _.
    
    For example: name1.name2.c -> name1_name2
    This ensures generated IR and obj file names are safe.
    """
    stem = src.stem
    # Replace dots, dashes, and other special chars with underscores
    normalized = re.sub(r'[.\-]', '_', stem)
    return normalized


def to_ll(src: Path) -> Path:
    normalized = normalize_stem(src)
    return src.parent / f"{normalized}.ll"


def to_aadb_ll(src: Path) -> Path:
    normalized = normalize_stem(src)
    return src.parent / f"{normalized}_aadb.ll"

def to_aadb_o3_ll(src: Path) -> Path:
    normalized = normalize_stem(src)
    return src.parent / f"{normalized}_aadb_o3.ll"

def to_obj(src: Path) -> Path:
    normalized = normalize_stem(src)
    return src.parent / f"{normalized}_aadb.o"


def normalize_rel(path: Path) -> str:
    return path.as_posix()


def source_dependencies(cfg: Config, src: Path) -> List[Path]:
    key = normalize_rel(src)
    deps = cfg.deps_map.get(key, [])
    if src not in deps:
        deps = [src] + deps
    return deps


def needs_rebuild(cfg: Config, src: Path) -> bool:
    obj = to_obj(src)
    if not obj.exists():
        return True

    try:
        obj_mtime = obj.stat().st_mtime
    except OSError:
        return True

    for dep in source_dependencies(cfg, src):
        if not dep.exists():
            # Missing dependency should force rebuild to surface the root error from the compiler.
            return True
        try:
            if dep.stat().st_mtime > obj_mtime:
                return True
        except OSError:
            return True

    return False


def compile_to_ir(cfg: Config, src: Path) -> Path:
    ir = to_ll(src)
    ensure_parent(ir)

    args = [
        cfg.clang,
        # "-O3",
        # "-static",
        "-target",
        cfg.target_triple,
    ]

    if cfg.riscv_build:
        args += [f"-mcpu={cfg.mcpu}"]
    else:
        args += [f"-march={cfg.march}"] 
        args += [f"-mtune={cfg.mtune}"]

    args += [
        "-Xclang",
        "-disable-llvm-optzns",
        "-emit-llvm",
        "-S",
        "-o",
        str(ir),
    ]
    if cfg.riscv_build:
        args += split_words(cfg.riscv_compile_flags)
    args += split_words(cfg.cppflags_common)
    args += split_words(cfg.cflags_common)
    args += split_words(cfg.extra_compile_flags)
    args += split_words(cfg.bench_cxx_flags)
    args += split_words(cfg.bench_c_flags)
    if cfg.exebase == "omnetpp_s":
        # For omnetpp_s, we need to compile with --std=c++14
        args += " --std=c++14".split()
    args.append(str(src))
    run_cmd(args)
    return ir


def aa_pipeline_for_source(cfg: Config, src: Path) -> str:
    src_text = str(src)
    if src_text in cfg.aa_default_pipeline_sources:
        return cfg.aa_pipeline_fallback
    if src_text in cfg.aa_noscev_pipeline_sources:
        return cfg.aa_pipeline_noscev
    return cfg.aa_pipeline_default


def build_aadb_ir(cfg: Config, ir: Path, src: Path) -> Path:
    aadb_ir = to_aadb_ll(src)
    aadb_o3_ir = to_aadb_o3_ll(src)
    ensure_parent(aadb_ir)
    ensure_parent(aadb_o3_ir)
    normalized = normalize_stem(src)
    annotated = ir.with_name(f"{normalized}_annotated.ll")
    annotated_re = ir.with_name(f"{normalized}_annotated_re.ll")
    annotated_backup = ir.with_name(f"{normalized}_annotated_backup.ll")
    re_o3 = ir.with_name(f"{normalized}_re_o3.ll")
    run_cmd(
        [
            cfg.opt_bin,
            f"-passes={cfg.annotate_passes}",
            f"--mtriple={cfg.target_triple}",
            "-S",
            str(ir),
            "-o",
            str(annotated),
        ]
    )

    if cfg.enable_rewrite_annotation:
        run_cmd(
            [
                cfg.opt_bin,
                f"-passes={cfg.annotate_re_passes}",
                f"--mtriple={cfg.target_triple}",
                "-S",
                str(ir),
                "-o",
                str(annotated_re),
            ]
        )

    run_hook(cfg.pre_annotate_hook)

    aa_pipeline = aa_pipeline_for_source(cfg, src)
    if cfg.run_svf and aa_pipeline != cfg.aa_pipeline_fallback:
        run_cmd(
            [
                cfg.wpa_bin,
                "-ander",
                "-svfg",
                "-print-aliases",
                f"-extapi={cfg.extapi_bc}",
                str(annotated),
            ]
        )

    if cfg.enable_rewrite_annotation:
        annotated_re.replace(annotated)

    run_hook(cfg.post_annotate_hook)


    run_hook(cfg.pre_aadb_hook)
    run_cmd(
        [
            cfg.opt_bin,
            "-O3",
            "-stats",
            f"--aa-pipeline={aa_pipeline}",
            f"-mtriple={cfg.target_triple}",
            f"-mcpu={cfg.mcpu}",
            "-S",
            str(annotated),
            "-o",
            str(aadb_ir),
        ]
    )
    run_hook(cfg.post_aadb_hook)

    return aadb_ir


def build_object(cfg: Config, aadb_ir: Path, src: Path) -> Path:
    obj = to_obj(src)
    run_cmd(
        [
            cfg.llc_bin,
            "-O3",
            "-filetype=obj",
            f"-mcpu={cfg.llc_mcpu}",
            f"-mtriple={cfg.target_triple}",
            str(aadb_ir),
            "-o",
            str(obj),
        ]
    )

    run_hook(cfg.post_object_hook)

    if cfg.run_drop_table:
        run_cmd(["bash", cfg.drop_table_script])

    return obj


def link_binary(cfg: Config, objects: Iterable[Path]) -> None:
    args = [cfg.linker]
    args += split_words(cfg.link_flags)
    args += [str(o) for o in objects]
    args += split_words(cfg.link_libs)
    args += ["-o", cfg.output_bin]
    if cfg.riscv_build:
        args += split_words(cfg.riscv_compile_flags)
    run_cmd(args)


def print_config(cfg: Config) -> None:
    print(f"CLANG={cfg.clang}")
    print(f"OPT_BIN={cfg.opt_bin}")
    print(f"LLC_BIN={cfg.llc_bin}")
    print(f"WPA_BIN={cfg.wpa_bin}")
    print(f"TARGET_TRIPLE={cfg.target_triple}")
    print(f"MARCH={cfg.march} MTUNE={cfg.mtune} MCPU={cfg.mcpu} LLC_MCPU={cfg.llc_mcpu}")
    print(f"AADB_OPT_LEVEL={cfg.aadb_opt_level}")
    print(
        "RUN_SVF={} RUN_DROP_TABLE={} ENABLE_REWRITE_ANNOTATION={}".format(
            int(cfg.run_svf), int(cfg.run_drop_table), int(cfg.enable_rewrite_annotation)
        )
    )
    print(f"AA_PIPELINE_DEFAULT={cfg.aa_pipeline_default}")
    print(f"AA_PIPELINE_FALLBACK={cfg.aa_pipeline_fallback}")
    print(f"AA_DEFAULT_PIPELINE_SOURCES={' '.join(cfg.aa_default_pipeline_sources)}")
    print(f"CPPFLAGS_COMMON={cfg.cppflags_common}")
    print(f"CFLAGS_COMMON={cfg.cflags_common}")
    print(f"OUTPUT_BIN={cfg.output_bin}")


def list_sources(cfg: Config) -> None:
    for src in cfg.sources:
        print(src)


def remove_if_exists(path: Path) -> None:
    if path.exists():
        path.unlink()


def clean(cfg: Config) -> None:
    for src in cfg.sources:
        normalized = normalize_stem(src)
        remove_if_exists(to_obj(src))
        remove_if_exists(to_aadb_ll(src))
        remove_if_exists(to_aadb_o3_ll(src))
        remove_if_exists(src.with_name(f"{normalized}_annotated.ll"))
        remove_if_exists(src.with_name(f"{normalized}_annotated_re.ll"))
        remove_if_exists(src.with_name(f"{normalized}_annotated_backup.ll"))
        remove_if_exists(src.with_name(f"{normalized}_aadb.o"))
    remove_if_exists(Path(cfg.output_bin))


def deepclean(cfg: Config) -> None:
    clean(cfg)
    for src in cfg.sources:
        remove_if_exists(to_ll(src))


def build_one(cfg: Config, src: Path) -> Path:
    t0 = time.perf_counter_ns()
    obj = to_obj(src)
    if not needs_rebuild(cfg, src):
        emit(f"= up-to-date: {src}")
        return obj

    ir = compile_to_ir(cfg, src)
    aadb_ir = build_aadb_ir(cfg, ir, src)
    out_obj = build_object(cfg, aadb_ir, src)
    if RUNTIME_OPTS is not None and RUNTIME_OPTS.enable_timing:
        emit(f"TIME {src}: {format_duration_ns(time.perf_counter_ns() - t0)}")
    return out_obj


RUNTIME_OPTS: RuntimeOptions | None = None


def build(cfg: Config, jobs: int) -> None:
    if not cfg.sources:
        print("No C sources found (set SOURCES or place .c files in current directory).", file=sys.stderr)
        sys.exit(1)

    objects: List[Path] = []

    if jobs <= 1:
        for src in cfg.sources:
            objects.append(build_one(cfg, src))
    else:
        with ThreadPoolExecutor(max_workers=jobs) as exe:
            fut_to_src = {exe.submit(build_one, cfg, src): src for src in cfg.sources}
            for fut in as_completed(fut_to_src):
                src = fut_to_src[fut]
                try:
                    objects.append(fut.result())
                except Exception as exc:
                    print(f"Build failed for {src}: {exc}", file=sys.stderr)
                    raise

    # Preserve source order for deterministic link ordering.
    # Use source-specific object paths to avoid basename collisions
    # (e.g. wand/convert.c vs utilities/convert.c).
    ordered_objects: List[Path] = []
    for src in cfg.sources:
        obj = to_obj(src)
        if not obj.exists():
            raise RuntimeError(f"Missing object for source: {src}")
        ordered_objects.append(obj)

    link_binary(cfg, ordered_objects)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Python equivalent of Makefile.aadb")
    parser.add_argument("command", nargs="?", default="build", choices=["build", "aadb", "clean", "deepclean", "print-config", "list-sources", "help"])
    parser.add_argument("-j", "--jobs", type=int, default=1, help="Parallel per-source build jobs")
    parser.add_argument("--time-build", action="store_true", help="Print timing summary for build operations")
    parser.add_argument("--log-output", action="store_true", help="Write terminal output to a log file")
    parser.add_argument("--log-file", default=os.environ.get("BUILD_LOG_FILE", "build_aadb.log"), help="Path for log file when --log-output is enabled")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    enable_timing = args.time_build or as_bool(os.environ.get("BUILD_TIME", "0"))
    log_enabled = args.log_output or as_bool(os.environ.get("BUILD_LOG_ENABLED", "0"))

    global RUNTIME_OPTS
    RUNTIME_OPTS = RuntimeOptions(
        enable_timing=enable_timing,
        log_enabled=log_enabled,
        log_file=Path(args.log_file),
    )

    global TEE
    TEE = OutputTee(log_enabled, Path(args.log_file))
    TEE.open()

    cfg = load_config()

    cmd = args.command
    build_start = time.perf_counter_ns()
    try:
        if cmd == "help":
            emit(__doc__.strip())
            return 0
        if cmd == "print-config":
            print_config(cfg)
            return 0
        if cmd == "list-sources":
            list_sources(cfg)
            return 0
        if cmd == "clean":
            clean(cfg)
            return 0
        if cmd == "deepclean":
            deepclean(cfg)
            return 0

        # build / aadb
        build(cfg, max(1, args.jobs))
        if RUNTIME_OPTS.enable_timing:
            emit(f"TIME total build: {format_duration_ns(time.perf_counter_ns() - build_start)}")
        return 0
    finally:
        if TEE is not None:
            TEE.close()


if __name__ == "__main__":
    raise SystemExit(main())
