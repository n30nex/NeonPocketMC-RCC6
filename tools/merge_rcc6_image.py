#!/usr/bin/env python3
"""Create an RCC6 recovery image without relying on a platform-specific build target."""

from __future__ import annotations

import argparse
from pathlib import Path


APP_OFFSET = 0x10000
PARTITIONS_OFFSET = 0x8000


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bootloader", type=Path, required=True)
    parser.add_argument("--partitions", type=Path, required=True)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    pieces = (
        (0, args.bootloader.read_bytes(), "bootloader"),
        (PARTITIONS_OFFSET, args.partitions.read_bytes(), "partitions"),
        (APP_OFFSET, args.app.read_bytes(), "application"),
    )
    for (offset, data, label), (next_offset, _next_data, next_label) in zip(pieces, pieces[1:]):
        if offset + len(data) > next_offset:
            raise ValueError(f"{label} overlaps {next_label}")
    if not pieces[0][1] or pieces[0][1][0] != 0xE9:
        raise ValueError("bootloader is not an ESP image")
    if not pieces[2][1] or pieces[2][1][0] != 0xE9:
        raise ValueError("application is not an ESP image")

    merged = bytearray(b"\xFF" * (APP_OFFSET + len(pieces[2][1])))
    for offset, data, _label in pieces:
        merged[offset:offset + len(data)] = data
    args.output.write_bytes(merged)
    print(f"RCC6 recovery image: {args.output} ({len(merged)} bytes)")


if __name__ == "__main__":
    main()
