#!/usr/bin/env python3
"""
extract_slices.py - Extract stratified benchmark slices from enwik9.

This script extracts standardized benchmark slices from the 1 GB enwik9 corpus
for rapid compression testing and reproducible development iterations.

Defined Slices:
  - tier2_regime_a.bin: 1 MB (1,000,000 bytes) from offset 50,000,000 (article body text region)
  - tier2_regime_b.bin: 1 MB (1,000,000 bytes) from offset 5,000,000 (XML-heavy intro region)
  - tier2_regime_c.bin: 1 MB (1,000,000 bytes) from offset 960,000,000 (tail metadata region)
  - tier3_meso.bin:    10 MB (10,000,000 bytes) from offset 100,000,000 (broad article body region)

Usage:
  python tools/extract_slices.py
  python tools/extract_slices.py --enwik9 data/enwik9 --output-dir data/slices
"""

import argparse
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import sys
from typing import List, Optional


EXPECTED_ENWIK9_SIZE = 1_000_000_000
EXPECTED_ENWIK9_SHA256 = (
    "159b85351e5f76e60cbe32e04c677847a9ecba3adc79addab6f4c6c7aa3744bc"
)


@dataclass(frozen=True)
class SliceSpec:
    name: str
    offset: int
    size: int
    description: str


SLICES: List[SliceSpec] = [
    SliceSpec(
        name="tier2_regime_a.bin",
        offset=50_000_000,
        size=1_000_000,
        description="article body text region",
    ),
    SliceSpec(
        name="tier2_regime_b.bin",
        offset=5_000_000,
        size=1_000_000,
        description="XML-heavy intro region",
    ),
    SliceSpec(
        name="tier2_regime_c.bin",
        offset=960_000_000,
        size=1_000_000,
        description="tail metadata region",
    ),
    SliceSpec(
        name="tier3_meso.bin",
        offset=100_000_000,
        size=10_000_000,
        description="broad article body region",
    ),
]


def compute_file_sha256(path: Path, chunk_size: int = 1024 * 1024) -> str:
    """Compute SHA-256 hash of a file using streaming reads."""
    hasher = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            chunk = f.read(chunk_size)
            if not chunk:
                break
            hasher.update(chunk)
    return hasher.hexdigest()


def extract_slice(
    source_path: Path,
    output_path: Path,
    offset: int,
    size: int,
    chunk_size: int = 1024 * 1024,
) -> str:
    """Extract a byte slice from source_path and write to output_path.

    Returns the hex SHA-256 digest of the extracted slice.
    """
    hasher = hashlib.sha256()
    remaining = size

    with open(source_path, "rb") as src_f:
        src_f.seek(offset)
        with open(output_path, "wb") as dst_f:
            while remaining > 0:
                bytes_to_read = min(remaining, chunk_size)
                chunk = src_f.read(bytes_to_read)
                if not chunk:
                    raise IOError(
                        f"Unexpected EOF while reading {source_path}: "
                        f"requested {size} bytes from offset {offset}, "
                        f"got {size - remaining} bytes"
                    )
                dst_f.write(chunk)
                hasher.update(chunk)
                remaining -= len(chunk)

    return hasher.hexdigest()


def format_bytes(size: int) -> str:
    """Format byte counts into human-readable strings."""
    if size >= 1_000_000:
        return f"{size:,} bytes ({size / 1_000_000:.1f} MB)"
    if size >= 1_000:
        return f"{size:,} bytes ({size / 1_000:.1f} KB)"
    return f"{size:,} bytes"


def parse_args() -> argparse.Namespace:
    workspace_root = Path(__file__).resolve().parent.parent
    default_enwik9 = workspace_root / "data" / "enwik9"
    default_output_dir = workspace_root / "data" / "slices"

    parser = argparse.ArgumentParser(
        description="Extract stratified benchmark slices from enwik9."
    )
    parser.add_argument(
        "--enwik9",
        type=Path,
        default=default_enwik9,
        help=f"Path to enwik9 source file (default: {default_enwik9})",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=default_output_dir,
        help=f"Output directory for slices (default: {default_output_dir})",
    )
    parser.add_argument(
        "--verify-source",
        action="store_true",
        help="Compute and verify SHA256 of source enwik9 before extraction",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    enwik9_path: Path = args.enwik9
    output_dir: Path = args.output_dir

    print("=" * 80)
    print("enwik9 Stratified Benchmark Slice Extractor")
    print("=" * 80)
    print(f"Source file:      {enwik9_path}")
    print(f"Output directory: {output_dir}")

    if not enwik9_path.exists():
        print(f"\n[ERROR] Source file not found: {enwik9_path}", file=sys.stderr)
        print("Please ensure data/enwik9 is available.", file=sys.stderr)
        return 1

    file_size = enwik9_path.stat().st_size
    print(f"Source file size: {file_size:,} bytes")

    if file_size != EXPECTED_ENWIK9_SIZE:
        print(
            f"[WARNING] Expected enwik9 size {EXPECTED_ENWIK9_SIZE:,} bytes, "
            f"found {file_size:,} bytes.",
            file=sys.stderr,
        )

    if args.verify_source:
        print("\nVerifying source enwik9 SHA-256 hash (this may take a few seconds)...")
        source_hash = compute_file_sha256(enwik9_path)
        print(f"Source SHA-256: {source_hash}")
        if source_hash == EXPECTED_ENWIK9_SHA256:
            print("[OK] Source SHA-256 matches expected enwik9 hash.")
        else:
            print(
                f"[WARNING] Source hash mismatch! Expected {EXPECTED_ENWIK9_SHA256}",
                file=sys.stderr,
            )

    output_dir.mkdir(parents=True, exist_ok=True)

    print("\nExtracting slices:")
    print("-" * 80)

    results = []
    for spec in SLICES:
        out_file = output_dir / spec.name
        print(f"Extracting '{spec.name}'...")
        print(f"  Description: {spec.description}")
        print(f"  Offset:      {spec.offset:,} bytes")
        print(f"  Target size: {format_bytes(spec.size)}")
        print(f"  Destination: {out_file}")

        sha256 = extract_slice(
            source_path=enwik9_path,
            output_path=out_file,
            offset=spec.offset,
            size=spec.size,
        )

        actual_size = out_file.stat().st_size
        print(f"  Extracted:   {actual_size:,} bytes")
        print(f"  SHA-256:     {sha256}")
        print("-" * 80)

        results.append((spec, out_file, actual_size, sha256))

    print("\nSummary of Extracted Benchmark Slices:")
    print("=" * 80)
    print(f"{'Slice Name':<20} {'Size':<16} {'Offset':<14} {'SHA-256':<64}")
    print("-" * 80)
    for spec, out_file, size, sha in results:
        print(f"{spec.name:<20} {size:<16} {spec.offset:<14} {sha}")
    print("=" * 80)
    print("All slices extracted successfully.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
