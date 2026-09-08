#!/usr/bin/env bash
set -euo pipefail

# Matched causal A/B gate for the E1 checksum scan.  The candidate changes
# only the lookup implementation; both builds must produce byte-identical
# archives and exact decoded SHA-256 before any speed claim is accepted.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
INPUT=${1:-$ROOT/prof_input/input2}
OUTDIR=${2:-$ROOT/e1-simd-gate}

if [[ ! -f "$INPUT" ]]; then
  echo "missing input: $INPUT" >&2
  exit 2
fi
SIZE=$(stat -c '%s' "$INPUT")
if (( SIZE > 20000000 )); then
  echo "refusing input larger than 20,000,000 bytes: $SIZE" >&2
  exit 2
fi

mkdir -p "$OUTDIR"

run_one() {
  local name=$1
  local simd=$2
  local dir="$OUTDIR/$name"
  mkdir -p "$dir"
  # Profile 4 is the currently measured fast/balanced portfolio.  Keep all
  # other compile-time choices fixed so this is a single-variable comparison.
  E1_SIMD_SCAN="$simd" PROFILE_LIST="4" \
    "$ROOT/tools/run_profile_gate.sh" "$INPUT" "$dir" \
    >"$dir/gate.stdout" 2>"$dir/gate.stderr"
}

run_one scalar 0
run_one simd 1

scalar_archive=$(stat -c '%s' "$OUTDIR/scalar/profile-4.comp")
simd_archive=$(stat -c '%s' "$OUTDIR/simd/profile-4.comp")
scalar_sha=$(sha256sum "$OUTDIR/scalar/profile-4.comp" | awk '{print $1}')
simd_sha=$(sha256sum "$OUTDIR/simd/profile-4.comp" | awk '{print $1}')

echo "input bytes: $SIZE"
echo "scalar archive bytes: $scalar_archive"
echo "simd archive bytes:   $simd_archive"
echo "scalar archive sha256: $scalar_sha"
echo "simd archive sha256:   $simd_sha"

if [[ "$scalar_archive" != "$simd_archive" || "$scalar_sha" != "$simd_sha" ]]; then
  echo "REJECT: SIMD path changed the encoded archive" >&2
  exit 1
fi

echo "PASS: archive bytes are identical; compare encode/decode times in"
echo "      $OUTDIR/scalar/profile-4.*.time and $OUTDIR/simd/profile-4.*.time"
