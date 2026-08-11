#!/usr/bin/env python3
"""Verify an exact Ultimate RCC6 image, partition fit, and static budgets."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import struct
import subprocess


PARTITION = struct.Struct("<HBBII16sI")
APP_LIMIT = 3 * 1024 * 1024
STATIC_RAM_LIMIT = 210 * 1024


def partitions(path: Path) -> list[tuple[int, int, int, int, str]]:
    blob = path.read_bytes()
    result = []
    for offset in range(0, len(blob) - PARTITION.size + 1, PARTITION.size):
        magic, kind, subtype, start, size, label, _flags = PARTITION.unpack_from(blob, offset)
        if magic == 0xFFFF or magic == 0xEBEB:
            break
        if magic != 0x50AA:
            raise ValueError(f"invalid partition entry magic 0x{magic:04x} at {offset}")
        result.append((kind, subtype, start, size, label.split(b"\0", 1)[0].decode("ascii")))
    if not result:
        raise ValueError("partition table has no entries")
    return result


def size_tool() -> str:
    name = "riscv32-esp-elf-size"
    found = shutil.which(name)
    if found:
        return found
    suffix = ".exe" if __import__("os").name == "nt" else ""
    candidate = Path.home() / ".platformio" / "packages" / "toolchain-riscv32-esp" / "bin" / (name + suffix)
    if candidate.is_file():
        return str(candidate)
    raise FileNotFoundError(f"cannot locate {name}")


def static_ram(elf: Path) -> int:
    output = subprocess.check_output([size_tool(), "-A", str(elf)], text=True)
    ram_sections = {".dram0.data", ".dram0.bss", ".iram0.data", ".iram0.bss", ".noinit"}
    total = 0
    found = set()
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[0] in ram_sections:
            total += int(fields[1])
            found.add(fields[0])
    if ".dram0.data" not in found or ".dram0.bss" not in found:
        raise ValueError("unexpected GNU size section output")
    return total


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--merged", type=Path, required=True)
    parser.add_argument("--bootloader", type=Path, required=True)
    parser.add_argument("--partitions", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--sha", required=True)
    parser.add_argument("--version", default="2.1.0-rc.1")
    args = parser.parse_args()

    app = args.app.read_bytes()
    merged = args.merged.read_bytes()
    bootloader = args.bootloader.read_bytes()
    partition_blob = args.partitions.read_bytes()
    if len(app) >= APP_LIMIT:
        raise ValueError(f"application is {len(app)} bytes; limit is {APP_LIMIT - 1}")
    if len(app) < 4 or app[0] != 0xE9 or app[2] != 0x02:
        raise ValueError("application is not an ESP32-C6 DIO image")
    for marker in (b"NEONPOCKETMC", args.version.encode("ascii"), args.sha.encode("ascii")):
        if marker not in app:
            raise ValueError(f"missing application marker {marker!r}")

    if merged[:len(bootloader)] != bootloader:
        raise ValueError("merged image does not contain the exact bootloader at 0x0000")
    if merged[0x8000:0x8000 + len(partition_blob)] != partition_blob:
        raise ValueError("merged image does not contain the exact partition table at 0x8000")

    entries = partitions(args.partitions)
    ordered = sorted(entries, key=lambda entry: entry[2])
    for left, right in zip(ordered, ordered[1:]):
        if left[2] + left[3] > right[2]:
            raise ValueError(f"partition overlap: {left[4]} and {right[4]}")
    ota = sorted((entry for entry in entries if entry[0] == 0 and entry[1] in (0x10, 0x11)),
                 key=lambda entry: entry[2])
    if len(ota) != 2:
        raise ValueError("expected exactly two OTA application partitions")
    if ota[0][2] != 0x10000 or any(len(app) > entry[3] for entry in ota):
        raise ValueError("application does not fit both OTA slots at the expected offset")
    expected_merged = ota[0][2] + len(app)
    if len(merged) != expected_merged or merged[ota[0][2]:] != app:
        raise ValueError("merged image does not contain the exact app at 0x10000")

    ram = static_ram(args.elf)
    if ram >= STATIC_RAM_LIMIT:
        raise ValueError(f"static RAM is {ram} bytes; limit is {STATIC_RAM_LIMIT - 1}")
    print(f"Ultimate artifact verified: app={len(app)} static_ram={ram} ota_slot={ota[0][3]}")


if __name__ == "__main__":
    main()
