#!/usr/bin/env python3
"""
bench.py - Comprehensive Tiered Benchmark Runner for cmix-lex

Orchestrates multi-tiered compression benchmarks for the cmix-lex project,
supporting rapid smoke testing, micro/meso regime slice evaluations, and full
enwik9 pipeline validation.

Benchmark Tiers:
  - Tier 1 (Smoke, ~5s): cmix -c on prof_input/input2 (941 KB), verified roundtrip hash.
  - Tier 2 (Regime Micro, ~45s): cmix -c on 3x 1 MB slices (tier2_regime_{a,b,c}.bin).
  - Tier 3 (Meso, ~5m): cmix -c on data/slices/tier3_meso.bin (10 MB).
  - Tier 4 (Full, overnight): Full enwik9 (1 GB) pipeline run and roundtrip verification.

Usage:
  python tools/bench.py                          # Run default Tier 1 (Smoke)
  python tools/bench.py --tier 2                 # Run Tier 2 (Regime Micro)
  python tools/bench.py --tier all               # Run all tiers sequentially
  python tools/bench.py --prepare-only           # Dump ready stream without compression
  python tools/bench.py --cmix-bin ./cmix-lex/run/cmix --data-dir ./data
  python tools/bench.py --no-preprocess          # Compress with -n (no preprocessing)
  python tools/bench.py --json report.json       # Export results to JSON
"""

import argparse
from dataclasses import asdict, dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from typing import Dict, List, Optional, Tuple


# ============================================================================
# Constants & Reference Specifications
# ============================================================================

ENWIK9_TOTAL_SIZE = 1_000_000_000
ENWIK9_EXPECTED_SHA256 = (
    "159b85351e5f76e60cbe32e04c677847a9ecba3adc79addab6f4c6c7aa3744bc"
)
INPUT2_EXPECTED_SHA256 = (
    "7ca547e67ca14341250c969caf5fa9d3c21d5966579b03b111d6b80540f99048"
)


@dataclass(frozen=True)
class BenchmarkSpec:
    tier: int
    tier_name: str
    name: str
    description: str
    target_rel_path: str
    expected_size: Optional[int] = None
    offset: Optional[int] = None  # Offset within enwik9 for slice extraction
    slice_size: Optional[int] = None  # Size to extract from enwik9 if slice
    expected_sha256: Optional[str] = None
    is_full_pipeline: bool = False  # If True, invokes cmix -e pipeline


TIER_SPECS: Dict[int, List[BenchmarkSpec]] = {
    1: [
        BenchmarkSpec(
            tier=1,
            tier_name="Tier 1 (Smoke)",
            name="input2",
            description="prof_input smoke test corpus (941 KB)",
            target_rel_path="cmix-lex/prof_input/input2",
            expected_size=941_724,
            expected_sha256=INPUT2_EXPECTED_SHA256,
        ),
    ],
    2: [
        BenchmarkSpec(
            tier=2,
            tier_name="Tier 2 (Regime Micro)",
            name="tier2_regime_a.bin",
            description="Article body text region (1 MB)",
            target_rel_path="data/slices/tier2_regime_a.bin",
            expected_size=1_000_000,
            offset=50_000_000,
            slice_size=1_000_000,
        ),
        BenchmarkSpec(
            tier=2,
            tier_name="Tier 2 (Regime Micro)",
            name="tier2_regime_b.bin",
            description="XML-heavy intro region (1 MB)",
            target_rel_path="data/slices/tier2_regime_b.bin",
            expected_size=1_000_000,
            offset=5_000_000,
            slice_size=1_000_000,
        ),
        BenchmarkSpec(
            tier=2,
            tier_name="Tier 2 (Regime Micro)",
            name="tier2_regime_c.bin",
            description="Tail metadata region (1 MB)",
            target_rel_path="data/slices/tier2_regime_c.bin",
            expected_size=1_000_000,
            offset=960_000_000,
            slice_size=1_000_000,
        ),
    ],
    3: [
        BenchmarkSpec(
            tier=3,
            tier_name="Tier 3 (Meso)",
            name="tier3_meso.bin",
            description="Broad article body region (10 MB)",
            target_rel_path="data/slices/tier3_meso.bin",
            expected_size=10_000_000,
            offset=100_000_000,
            slice_size=10_000_000,
        ),
    ],
    4: [
        BenchmarkSpec(
            tier=4,
            tier_name="Tier 4 (Full)",
            name="enwik9",
            description="Full 1 GB enwik9 Hutter Prize corpus",
            target_rel_path="data/enwik9",
            expected_size=ENWIK9_TOTAL_SIZE,
            expected_sha256=ENWIK9_EXPECTED_SHA256,
            is_full_pipeline=True,
        ),
    ],
}


@dataclass
class BenchmarkResult:
    tier: int
    tier_name: str
    name: str
    description: str
    input_path: str
    input_size: int
    input_sha256: str
    compressed_size: int
    ready_stream_size: Optional[int] = None
    encode_time: float = 0.0
    decode_time: float = 0.0
    total_time: float = 0.0
    decompressed_sha256: Optional[str] = None
    hash_matched: Optional[bool] = None
    success: bool = True
    error_message: Optional[str] = None
    prepare_only: bool = False
    compression_mode: str = "-c"


# ============================================================================
# Utilities & Formatting
# ============================================================================

def compute_sha256(path: Path, chunk_size: int = 1024 * 1024) -> str:
    """Compute SHA-256 hex digest of a file using streaming reads."""
    hasher = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(chunk_size):
            hasher.update(chunk)
    return hasher.hexdigest()


def format_bytes(size: Optional[int]) -> str:
    """Format byte counts with comma separation."""
    if size is None:
        return "N/A"
    return f"{size:,} B"


def format_time(seconds: float) -> str:
    """Format duration into human-readable string."""
    if seconds < 60:
        return f"{seconds:.2f}s"
    minutes = int(seconds // 60)
    rem_seconds = seconds % 60
    if minutes < 60:
        return f"{minutes}m {rem_seconds:04.1f}s"
    hours = int(minutes // 60)
    rem_minutes = minutes % 60
    return f"{hours}h {rem_minutes:02d}m {rem_seconds:02.0f}s"


def format_ratio(orig_size: int, comp_size: int) -> str:
    """Format compression ratio and percentage."""
    if comp_size <= 0:
        return "N/A"
    ratio = orig_size / comp_size
    pct = (comp_size / orig_size) * 100.0
    return f"{ratio:.3f}x ({pct:.1f}%)"


def format_bpb(orig_size: int, comp_size: int) -> str:
    """Calculate and format bits per byte (bpb)."""
    if orig_size <= 0:
        return "N/A"
    bpb = (comp_size * 8.0) / orig_size
    return f"{bpb:.4f}"


def format_throughput(size_bytes: int, seconds: float) -> str:
    """Format data throughput in KB/s or MB/s."""
    if seconds <= 0:
        return "N/A"
    bytes_per_sec = size_bytes / seconds
    if bytes_per_sec >= 1_000_000:
        return f"{bytes_per_sec / 1_000_000:.2f} MB/s"
    return f"{bytes_per_sec / 1_000:.1f} KB/s"


def is_wsl_available() -> bool:
    """Check if WSL is available on Windows."""
    if sys.platform != "win32":
        return False
    try:
        res = subprocess.run(
            ["wsl", "--status"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        return res.returncode == 0
    except Exception:
        return False


def is_elf_binary(path: Path) -> bool:
    """Check if a file begins with the ELF magic number."""
    if not path.is_file():
        return False
    try:
        with open(path, "rb") as f:
            magic = f.read(4)
            return magic == b"\x7fELF"
    except Exception:
        return False


def to_wsl_path(path: Path) -> str:
    """Convert a Windows Path to a WSL POSIX path (e.g. C:\\dir -> /mnt/c/dir)."""
    abs_path = path.resolve()
    drive, tail = os.path.splitdrive(str(abs_path))
    if drive:
        drive_letter = drive[0].lower()
        posix_tail = tail.replace("\\", "/").lstrip("/")
        return f"/mnt/{drive_letter}/{posix_tail}"
    return str(abs_path).replace("\\", "/")


# ============================================================================
# Binary & Slice Resolution
# ============================================================================

def resolve_cmix_binary(
    custom_bin: Optional[str], workspace_root: Path
) -> Tuple[Path, bool]:
    """
    Resolve the cmix binary path and determine if WSL execution wrapper is needed.
    
    Returns:
      (resolved_binary_path, use_wsl)
    """
    candidates = []
    if custom_bin:
        candidates.append(Path(custom_bin))
        candidates.append(workspace_root / custom_bin)
    else:
        candidates.extend([
            workspace_root / "cmix-lex" / "cmix",
            workspace_root / "cmix-lex" / "run" / "cmix",
            workspace_root / "cmix-lex" / "cmix.exe",
            workspace_root / "cmix-lex" / "run" / "cmix.exe",
            Path("cmix"),
            Path("cmix.exe"),
        ])

    for cand in candidates:
        if cand.is_file():
            binary_path = cand.resolve()
            # If running on Windows and the binary is an ELF Linux executable,
            # we need to route through WSL.
            needs_wsl = (sys.platform == "win32") and is_elf_binary(binary_path)
            return binary_path, needs_wsl

    # Check if 'cmix' is in system PATH
    which_cmix = shutil.which("cmix")
    if which_cmix:
        binary_path = Path(which_cmix).resolve()
        needs_wsl = (sys.platform == "win32") and is_elf_binary(binary_path)
        return binary_path, needs_wsl

    # If on Windows and not found natively, check if cmix exists inside default WSL paths
    if sys.platform == "win32":
        for cand in [
            workspace_root / "cmix-lex" / "cmix",
            workspace_root / "cmix-lex" / "run" / "cmix",
        ]:
            if cand.exists():
                return cand.resolve(), True

    raise FileNotFoundError(
        f"cmix binary not found! Searched: {[str(c) for c in candidates]}.\n"
        f"Please build cmix-lex first (e.g. make in cmix-lex) or specify --cmix-bin PATH."
    )


def extract_slice(
    source_path: Path,
    output_path: Path,
    offset: int,
    size: int,
    chunk_size: int = 1024 * 1024,
) -> None:
    """Extract a byte slice from source_path and write to output_path."""
    output_path.parent.mkdir(parents=True, exist_ok=True)
    remaining = size
    with open(source_path, "rb") as src_f:
        src_f.seek(offset)
        with open(output_path, "wb") as dst_f:
            while remaining > 0:
                bytes_to_read = min(remaining, chunk_size)
                chunk = src_f.read(bytes_to_read)
                if not chunk:
                    raise IOError(
                        f"Unexpected EOF reading {source_path}: wanted {size} B "
                        f"from offset {offset}, reached EOF after {size - remaining} B."
                    )
                dst_f.write(chunk)
                remaining -= len(chunk)


def resolve_input_file(
    spec: BenchmarkSpec,
    workspace_root: Path,
    data_dir: Path,
    auto_extract: bool = True,
    verbose: bool = False,
) -> Path:
    """
    Locate or automatically generate the input file for a benchmark tier.
    """
    # 1. Direct path check
    direct_candidates = [
        workspace_root / spec.target_rel_path,
        data_dir / spec.name,
        data_dir / "slices" / spec.name,
        workspace_root / "cmix-lex" / "prof_input" / spec.name,
        workspace_root / "prof_input" / spec.name,
    ]

    for cand in direct_candidates:
        if cand.is_file():
            return cand.resolve()

    # 2. Check if this is a slice that can be extracted from enwik9
    if spec.offset is not None and spec.slice_size is not None:
        target_path = data_dir / "slices" / spec.name
        enwik9_candidates = [
            data_dir / "enwik9",
            workspace_root / "data" / "enwik9",
        ]
        enwik9_path = next((p for p in enwik9_candidates if p.is_file()), None)

        if enwik9_path and auto_extract:
            if verbose:
                print(
                    f"[*] Slice '{spec.name}' not found. Auto-extracting "
                    f"{format_bytes(spec.slice_size)} from {enwik9_path} at offset {spec.offset:,}..."
                )
            extract_slice(
                source_path=enwik9_path,
                output_path=target_path,
                offset=spec.offset,
                size=spec.slice_size,
            )
            return target_path.resolve()

    raise FileNotFoundError(
        f"Input file for {spec.tier_name} ({spec.name}) not found.\n"
        f"Searched paths: {[str(c) for c in direct_candidates]}.\n"
        f"For slices, ensure data/enwik9 is present so slices can be extracted."
    )


# ============================================================================
# Benchmark Runner Engine
# ============================================================================

class BenchmarkRunner:
    """Executes benchmark tiers, measures timing/ratios, and verifies hashes."""

    def __init__(
        self,
        cmix_bin: Path,
        workspace_root: Path,
        data_dir: Path,
        work_dir: Path,
        use_wsl: bool = False,
        no_preprocess: bool = False,
        prepare_only: bool = False,
        skip_verify: bool = False,
        auto_extract: bool = True,
        keep_temp: bool = False,
        verbose: bool = False,
    ):
        self.cmix_bin = cmix_bin
        self.workspace_root = workspace_root
        self.data_dir = data_dir
        self.work_dir = work_dir
        self.use_wsl = use_wsl
        self.no_preprocess = no_preprocess
        self.prepare_only = prepare_only
        self.skip_verify = skip_verify
        self.auto_extract = auto_extract
        self.keep_temp = keep_temp
        self.verbose = verbose

        self.work_dir.mkdir(parents=True, exist_ok=True)

    def _execute_cmix(
        self,
        args: List[str],
        env_extra: Optional[Dict[str, str]] = None,
        cwd: Optional[Path] = None,
    ) -> Tuple[int, str, str, float]:
        """
        Invoke the cmix binary via subprocess (or WSL if configured), measuring wall time.
        
        Returns:
          (returncode, stdout_str, stderr_str, elapsed_wall_seconds)
        """
        cmd: List[str] = []
        effective_cwd = cwd or self.workspace_root

        if self.use_wsl:
            # When routing through WSL on Windows, convert binary and arguments to WSL paths
            wsl_bin = to_wsl_path(self.cmix_bin)
            wsl_args = []
            for arg in args:
                p = Path(arg)
                # If argument is an existing path or looks like an absolute/relative file path
                if p.exists() or "\\" in arg or "/" in arg:
                    wsl_args.append(to_wsl_path(p))
                else:
                    wsl_args.append(arg)

            cmd = ["wsl", "-e"]
            if env_extra:
                for k, v in env_extra.items():
                    cmd.append(f"{k}={v}")
            cmd.append(wsl_bin)
            cmd.extend(wsl_args)
        else:
            cmd = [str(self.cmix_bin)] + args

        env = os.environ.copy()
        if env_extra:
            env.update(env_extra)

        if self.verbose:
            print(f"    [CMD] {' '.join(cmd)}")

        t0 = time.perf_counter()
        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            cwd=str(effective_cwd),
            env=env,
            check=False,
        )
        elapsed = time.perf_counter() - t0

        if self.verbose:
            if proc.stdout.strip():
                print(f"    [STDOUT] {proc.stdout.strip()}")
            if proc.stderr.strip():
                print(f"    [STDERR] {proc.stderr.strip()}")

        return proc.returncode, proc.stdout, proc.stderr, elapsed

    def run_benchmark(self, spec: BenchmarkSpec) -> BenchmarkResult:
        """Run compression and decompression roundtrip for a single benchmark spec."""
        # 1. Resolve and verify input file
        input_path = resolve_input_file(
            spec=spec,
            workspace_root=self.workspace_root,
            data_dir=self.data_dir,
            auto_extract=self.auto_extract,
            verbose=self.verbose,
        )

        input_size = input_path.stat().st_size
        input_sha256 = compute_sha256(input_path)

        # Sanity check against expected size/sha256 if defined
        if spec.expected_size is not None and input_size != spec.expected_size:
            print(
                f"[!] Notice: {spec.name} size is {input_size:,} B "
                f"(expected {spec.expected_size:,} B)"
            )
        if spec.expected_sha256 and input_sha256 != spec.expected_sha256:
            print(
                f"[!] Notice: {spec.name} SHA-256 is {input_sha256} "
                f"(expected {spec.expected_sha256})"
            )

        # Temporary artifacts
        stem = f"{spec.tier}_{spec.name}"
        comp_output = self.work_dir / f"{stem}.cmix.out"
        decomp_output = self.work_dir / f"{stem}.restored.out"

        # Clean prior leftovers
        for p in [comp_output, decomp_output, Path(f"{comp_output}.cmix.temp")]:
            if p.exists():
                try:
                    p.unlink()
                except OSError:
                    pass

        # Determine compression mode flag
        if spec.is_full_pipeline:
            comp_flag = "-e"
        elif self.no_preprocess:
            comp_flag = "-n"
        else:
            comp_flag = "-c"

        result = BenchmarkResult(
            tier=spec.tier,
            tier_name=spec.tier_name,
            name=spec.name,
            description=spec.description,
            input_path=str(input_path),
            input_size=input_size,
            input_sha256=input_sha256,
            compressed_size=0,
            prepare_only=self.prepare_only,
            compression_mode=comp_flag,
        )

        try:
            # 2. Compression Phase
            print(f"[*] Running {spec.tier_name}: {spec.name} ({format_bytes(input_size)})...")
            
            env_extra = {}
            if self.prepare_only:
                env_extra["FX_PREPARE_ONLY"] = "1"

            # Execute compression: cmix <flag> <input> <output>
            ret, stdout, stderr, enc_time = self._execute_cmix(
                args=[comp_flag, str(input_path), str(comp_output)],
                env_extra=env_extra,
            )
            result.encode_time = enc_time

            if self.prepare_only:
                # In prepare-only mode, cmix prints ready stream size and exits 0 without writing archive
                # e.g.: "prepare-only: original_input_bytes=... ready_stream_bytes=..."
                match = re.search(r"ready_stream_bytes=(\d+)", stdout + stderr)
                if match:
                    ready_size = int(match.group(1))
                    result.ready_stream_size = ready_size
                    result.compressed_size = ready_size
                else:
                    # If temp file exists or parsed from output
                    temp_file = Path(f"{comp_output}.cmix.temp")
                    if temp_file.exists():
                        result.ready_stream_size = temp_file.stat().st_size
                        result.compressed_size = result.ready_stream_size
                
                result.total_time = enc_time
                result.success = (ret == 0)
                if ret != 0:
                    result.error_message = f"cmix prepare-only exited with code {ret}"
                return result

            if ret != 0:
                result.success = False
                result.error_message = (
                    f"cmix compression failed (exit code {ret}).\n"
                    f"stderr: {stderr.strip()}"
                )
                return result

            if not comp_output.is_file():
                result.success = False
                result.error_message = f"Compressed output file {comp_output} was not created."
                return result

            compressed_size = comp_output.stat().st_size
            result.compressed_size = compressed_size

            # 3. Decompression Phase & Roundtrip Hash Verification
            if self.skip_verify:
                result.total_time = enc_time
                result.hash_matched = None
                return result

            print(f"    -> Compressed: {format_bytes(compressed_size)} ({format_ratio(input_size, compressed_size)})")
            print(f"    -> Decompressing to verify bit-exact integrity...")

            if spec.is_full_pipeline:
                # Full enwik9 archive pipeline: executing self-extracting archive or cmix -d
                # If archive9 was generated, we can execute cmix -d or archive9
                ret_dec, stdout_dec, stderr_dec, dec_time = self._execute_cmix(
                    args=["-d", str(comp_output), str(decomp_output)]
                )
            else:
                ret_dec, stdout_dec, stderr_dec, dec_time = self._execute_cmix(
                    args=["-d", str(comp_output), str(decomp_output)]
                )

            result.decode_time = dec_time
            result.total_time = enc_time + dec_time

            if ret_dec != 0:
                result.success = False
                result.error_message = (
                    f"cmix decompression failed (exit code {ret_dec}).\n"
                    f"stderr: {stderr_dec.strip()}"
                )
                return result

            if not decomp_output.is_file():
                result.success = False
                result.error_message = f"Decompressed output file {decomp_output} was not found."
                return result

            # Hash verification
            decomp_sha256 = compute_sha256(decomp_output)
            result.decompressed_sha256 = decomp_sha256
            result.hash_matched = (decomp_sha256 == input_sha256)

            if not result.hash_matched:
                result.success = False
                result.error_message = (
                    f"Roundtrip SHA-256 mismatch!\n"
                    f"  Expected: {input_sha256}\n"
                    f"  Got:      {decomp_sha256}"
                )

        except Exception as ex:
            result.success = False
            result.error_message = f"Unhandled exception: {str(ex)}"
        finally:
            # 4. Cleanup temporary files unless --keep-temp is specified
            if not self.keep_temp:
                for p in [comp_output, decomp_output, Path(f"{comp_output}.cmix.temp")]:
                    if p.exists():
                        try:
                            p.unlink()
                        except OSError:
                            pass

        return result


# ============================================================================
# Presentation & Summary Reporting
# ============================================================================

def print_banner(
    cmix_bin: Path,
    data_dir: Path,
    selected_tiers: List[int],
    runner: BenchmarkRunner,
) -> None:
    """Print configuration banner before starting benchmarks."""
    wsl_str = " (via WSL)" if runner.use_wsl else ""
    mode_str = "Prepare-Only (FX_PREPARE_ONLY=1)" if runner.prepare_only else (
        "No Preprocessing (-n)" if runner.no_preprocess else "Standard (-c / -e)"
    )

    print("=" * 105)
    print("                              CMIX-LEX TIERED BENCHMARK RUNNER")
    print("=" * 105)
    print(f" Binary:         {cmix_bin}{wsl_str}")
    print(f" Data Directory: {data_dir.resolve()}")
    print(f" Work Directory: {runner.work_dir.resolve()}")
    print(f" Target Tiers:   {', '.join(f'Tier {t}' for t in selected_tiers)}")
    print(f" Mode:           {mode_str}")
    print(f" Verify Hash:    {'Disabled (--skip-verify)' if runner.skip_verify else 'Enabled (SHA-256 roundtrip)'}")
    print("=" * 105)
    print()


def print_summary_table(results: List[BenchmarkResult], prepare_only: bool = False) -> None:
    """Print a clean, aligned summary table of benchmark results."""
    print()
    print("=" * 118)
    if prepare_only:
        print("                                   PREPROCESSING BENCHMARK SUMMARY")
        print("=" * 118)
        header = f"{'Tier':<6} {'Benchmark Input':<22} {'Orig Size':<14} {'Ready Size':<14} {'Ratio':<16} {'bpb':<8} {'Prep Time':<12} {'Status':<10}"
        print(header)
        print("-" * 118)
        for r in results:
            t_label = f"T{r.tier}"
            r_str = format_ratio(r.input_size, r.compressed_size)
            b_str = format_bpb(r.input_size, r.compressed_size)
            status_str = "OK" if r.success else "FAIL"
            print(
                f"{t_label:<6} {r.name:<22} {format_bytes(r.input_size):<14} "
                f"{format_bytes(r.compressed_size):<14} {r_str:<16} {b_str:<8} "
                f"{format_time(r.encode_time):<12} {status_str:<10}"
            )
    else:
        print("                                      COMPRESSION BENCHMARK SUMMARY")
        print("=" * 118)
        header = f"{'Tier':<6} {'Benchmark Input':<22} {'Orig Size':<14} {'Comp Size':<14} {'Ratio':<16} {'bpb':<8} {'Enc Time':<10} {'Dec Time':<10} {'Total Time':<12} {'Hash':<8}"
        print(header)
        print("-" * 118)
        for r in results:
            t_label = f"T{r.tier}"
            r_str = format_ratio(r.input_size, r.compressed_size)
            b_str = format_bpb(r.input_size, r.compressed_size)
            if r.hash_matched is True:
                hash_str = "OK"
            elif r.hash_matched is False:
                hash_str = "FAIL"
            else:
                hash_str = "SKIP" if r.success else "ERR"

            print(
                f"{t_label:<6} {r.name:<22} {format_bytes(r.input_size):<14} "
                f"{format_bytes(r.compressed_size):<14} {r_str:<16} {b_str:<8} "
                f"{format_time(r.encode_time):<10} {format_time(r.decode_time):<10} "
                f"{format_time(r.total_time):<12} {hash_str:<8}"
            )

    print("-" * 118)

    # Totals computation
    total_orig = sum(r.input_size for r in results if r.success)
    total_comp = sum(r.compressed_size for r in results if r.success)
    total_enc = sum(r.encode_time for r in results)
    total_dec = sum(r.decode_time for r in results)
    total_wall = sum(r.total_time for r in results)
    all_hash_ok = all(r.hash_matched is True for r in results if r.hash_matched is not None)
    all_success = all(r.success for r in results)

    total_r_str = format_ratio(total_orig, total_comp)
    total_b_str = format_bpb(total_orig, total_comp)
    final_status = "ALL OK" if (all_success and all_hash_ok) else "FAILED"

    if prepare_only:
        print(
            f"{'TOTAL':<6} {'':<22} {format_bytes(total_orig):<14} "
            f"{format_bytes(total_comp):<14} {total_r_str:<16} {total_b_str:<8} "
            f"{format_time(total_enc):<12} {final_status:<10}"
        )
    else:
        print(
            f"{'TOTAL':<6} {'':<22} {format_bytes(total_orig):<14} "
            f"{format_bytes(total_comp):<14} {total_r_str:<16} {total_b_str:<8} "
            f"{format_time(total_enc):<10} {format_time(total_dec):<10} "
            f"{format_time(total_wall):<12} {final_status:<8}"
        )
    print("=" * 118)
    print()


# ============================================================================
# CLI Argument Parsing & Entry Point
# ============================================================================

def parse_arguments() -> argparse.Namespace:
    """Configure and parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Comprehensive benchmark runner for cmix-lex compression engine.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Benchmark Tiers:
  Tier 1 (Smoke, ~5s):        Runs cmix -c on prof_input/input2 (941 KB), verifies SHA-256.
  Tier 2 (Regime Micro, ~45s): Runs cmix -c on 3x 1MB slices (body, XML intro, tail metadata).
  Tier 3 (Meso, ~5m):         Runs cmix -c on 10MB meso slice (broad body region).
  Tier 4 (Full, overnight):   Runs full enwik9 1GB compression and self-extracting roundtrip.

Examples:
  python tools/bench.py --tier 1
  python tools/bench.py --tier 2 --cmix-bin ./cmix-lex/run/cmix
  python tools/bench.py --tier all --json results.json
  python tools/bench.py --prepare-only
        """,
    )

    parser.add_argument(
        "--tier",
        "-t",
        default="1",
        help="Tier(s) to execute: 1, 2, 3, 4, all, smoke, micro, meso, full (default: 1)",
    )
    parser.add_argument(
        "--cmix-bin",
        type=str,
        default=None,
        help="Path to cmix binary (default: searches ./cmix-lex/cmix, ./cmix-lex/run/cmix)",
    )
    parser.add_argument(
        "--data-dir",
        type=Path,
        default=None,
        help="Path to data directory containing enwik9 or slices (default: ./data)",
    )
    parser.add_argument(
        "--prepare-only",
        action="store_true",
        help="Set FX_PREPARE_ONLY=1 to dump preprocessed stream without full compression",
    )
    parser.add_argument(
        "--no-preprocess",
        "-n",
        action="store_true",
        help="Compress using -n (no preprocessing mode)",
    )
    parser.add_argument(
        "--skip-verify",
        "--no-verify",
        action="store_true",
        help="Skip decompression and roundtrip hash verification",
    )
    parser.add_argument(
        "--no-auto-extract",
        action="store_true",
        help="Do not automatically extract missing slice files from data/enwik9",
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        default=None,
        help="Directory for temporary benchmark files (default: ./bench_work)",
    )
    parser.add_argument(
        "--wsl",
        action="store_true",
        help="Force execution through WSL on Windows",
    )
    parser.add_argument(
        "--json",
        type=Path,
        default=None,
        help="Path to export benchmark results in JSON format",
    )
    parser.add_argument(
        "--keep-temp",
        action="store_true",
        help="Do not delete temporary compressed/decompressed files",
    )
    parser.add_argument(
        "--verbose",
        "-v",
        action="store_true",
        help="Enable verbose output including subprocess commands and stdout/stderr",
    )

    return parser.parse_args()


def resolve_selected_tiers(tier_arg: str) -> List[int]:
    """Parse tier argument string into a list of tier integers."""
    normalized = tier_arg.strip().lower()
    if normalized in ("all", "0"):
        return [1, 2, 3, 4]
    if normalized in ("1", "smoke", "tier1", "t1"):
        return [1]
    if normalized in ("2", "micro", "tier2", "t2", "regime"):
        return [2]
    if normalized in ("3", "meso", "tier3", "t3"):
        return [3]
    if normalized in ("4", "full", "tier4", "t4", "enwik9"):
        return [4]

    # Handle comma-separated list like "1,2"
    tiers = []
    for item in normalized.split(","):
        item = item.strip()
        if item in ("1", "2", "3", "4"):
            tiers.append(int(item))
        else:
            raise ValueError(
                f"Unknown tier '{item}'. Valid options: 1, 2, 3, 4, all, smoke, micro, meso, full."
            )
    return sorted(list(set(tiers)))


def main() -> int:
    args = parse_arguments()

    workspace_root = Path(__file__).resolve().parent.parent
    data_dir = (args.data_dir or (workspace_root / "data")).resolve()
    work_dir = (args.work_dir or (workspace_root / "bench_work")).resolve()

    try:
        selected_tiers = resolve_selected_tiers(args.tier)
    except ValueError as e:
        print(f"[ERROR] {e}", file=sys.stderr)
        return 1

    try:
        cmix_bin, auto_wsl = resolve_cmix_binary(args.cmix_bin, workspace_root)
    except FileNotFoundError as e:
        print(f"[ERROR] {e}", file=sys.stderr)
        return 1

    use_wsl = args.wsl or auto_wsl

    runner = BenchmarkRunner(
        cmix_bin=cmix_bin,
        workspace_root=workspace_root,
        data_dir=data_dir,
        work_dir=work_dir,
        use_wsl=use_wsl,
        no_preprocess=args.no_preprocess,
        prepare_only=args.prepare_only,
        skip_verify=args.skip_verify,
        auto_extract=not args.no_auto_extract,
        keep_temp=args.keep_temp,
        verbose=args.verbose,
    )

    print_banner(
        cmix_bin=cmix_bin,
        data_dir=data_dir,
        selected_tiers=selected_tiers,
        runner=runner,
    )

    results: List[BenchmarkResult] = []
    overall_success = True

    for tier in selected_tiers:
        specs = TIER_SPECS.get(tier, [])
        for spec in specs:
            res = runner.run_benchmark(spec)
            results.append(res)
            if not res.success:
                overall_success = False
                print(f"[ERROR] Benchmark failed for {spec.name}: {res.error_message}", file=sys.stderr)

    # Print Formatted Report Table
    print_summary_table(results, prepare_only=args.prepare_only)

    # Export JSON if requested
    if args.json:
        json_path = args.json.resolve()
        json_path.parent.mkdir(parents=True, exist_ok=True)
        report_data = {
            "timestamp": time.time(),
            "cmix_binary": str(cmix_bin),
            "use_wsl": use_wsl,
            "prepare_only": args.prepare_only,
            "no_preprocess": args.no_preprocess,
            "skip_verify": args.skip_verify,
            "results": [asdict(r) for r in results],
            "overall_success": overall_success,
        }
        with open(json_path, "w", encoding="utf-8") as f:
            json.dump(report_data, f, indent=2)
        print(f"[*] Results exported to JSON: {json_path}")

    # Exit code: 0 on total success, 1 on any error or verification failure
    return 0 if overall_success else 1


if __name__ == "__main__":
    sys.exit(main())
