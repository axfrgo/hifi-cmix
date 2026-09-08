#!/bin/bash
set -e

# Hutter Prize compression entrypoint script
# Conforms to contest rule: comp9 outputs archive9 given input enwik9
# Usage: ./comp9.sh [path/to/enwik9]

INPUT="${1:-enwik9}"

if [[ ! -f "$INPUT" ]]; then
  echo "Error: Input file '$INPUT' not found." >&2
  echo "Usage: $0 [path/to/enwik9]" >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Ensure the self-extracting cmix executable exists
if [[ ! -x "$SCRIPT_DIR/run/cmix" ]]; then
  echo "Building self-extracting cmix compressor..."
  (cd "$SCRIPT_DIR" && bash ./build_and_construct_comp.sh)
fi

echo "Compressing $INPUT -> archive9..."
cd "$SCRIPT_DIR/run"
./cmix -e "$(realpath "$INPUT")" archive9.tmp

if [[ -f "$SCRIPT_DIR/run/archive9" ]]; then
  mv "$SCRIPT_DIR/run/archive9" "$SCRIPT_DIR/archive9"
fi

echo "Compression complete. Self-extracting decompressor created at: $SCRIPT_DIR/archive9"
