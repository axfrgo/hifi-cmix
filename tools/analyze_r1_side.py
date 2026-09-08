"""Read-only diagnostic for the R1ORD3 permutation side blob."""

import collections
import math
import pathlib
import sys


def read_varint(blob, pos):
    value = 0
    shift = 0
    while True:
        byte = blob[pos]
        pos += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, pos
        shift += 7


def main(path):
    blob = pathlib.Path(path).read_bytes()
    if not blob.startswith(b"R1ORD3\n"):
        raise SystemExit("not an R1ORD3 side blob")
    pos = 7
    metadata = []
    for _ in range(7):
        value, pos = read_varint(blob, pos)
        metadata.append(value)

    widths = []
    while pos < len(blob):
        start = pos
        _, pos = read_varint(blob, pos)
        widths.append(pos - start)

    byte_counts = collections.Counter(blob)
    entropy = -sum((count / len(blob)) * math.log2(count / len(blob))
                   for count in byte_counts.values())
    print(f"metadata={metadata}")
    print(f"side_bytes={len(blob)} header_bytes={len(blob) - sum(widths)}")
    print(f"rank_count={len(widths)} mean_rank_bytes={sum(widths) / len(widths):.6f}")
    print(f"rank_varint_widths={dict(sorted(collections.Counter(widths).items()))}")
    print(f"zero_order_byte_entropy={entropy:.6f} bits/byte")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: analyze_r1_side.py SIDE_PATH")
    main(sys.argv[1])
