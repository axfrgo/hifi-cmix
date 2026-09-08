#!/usr/bin/env bash
set -euo pipefail

# Exact A/B gate for the optional causal previous-byte direct expert.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
INPUT=${1:-$ROOT/prof_input/input}
OUTDIR=${2:-$ROOT/prevbyte-direct-gate}

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
  local extra=$2
  local dir="$OUTDIR/$name"
  mkdir -p "$dir"
  EXTRA_PREVBYTE_DIRECT="$extra" PROFILE_LIST="4" \
    "$ROOT/tools/run_profile_gate.sh" "$INPUT" "$dir" \
    >"$dir/gate.stdout" 2>"$dir/gate.stderr"
}

run_one baseline 0
run_one candidate 1

base_size=$(stat -c '%s' "$OUTDIR/baseline/profile-4.comp")
candidate_size=$(stat -c '%s' "$OUTDIR/candidate/profile-4.comp")
base_sha=$(sha256sum "$OUTDIR/baseline/profile-4.comp" | awk '{print $1}')
candidate_sha=$(sha256sum "$OUTDIR/candidate/profile-4.comp" | awk '{print $1}')

echo "input bytes: $SIZE"
echo "baseline archive bytes:  $base_size"
echo "candidate archive bytes: $candidate_size"
echo "baseline archive sha256:  $base_sha"
echo "candidate archive sha256: $candidate_sha"
echo "Compare exact output SHA files and timing logs under $OUTDIR."
