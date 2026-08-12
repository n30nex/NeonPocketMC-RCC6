#!/usr/bin/env python3
"""Capture real Ultimate UI frames from the RCC6 diagnostic firmware."""

from __future__ import annotations

import argparse
import binascii
import re
import time
from pathlib import Path

from PIL import Image, ImageDraw
import serial

WIDTH = 220
HEIGHT = 128
BYTE_COUNT = WIDTH * HEIGHT * 2
PAGES = ("home", "inbox", "network", "radio", "tools", "power")


def open_port(name: str) -> serial.Serial:
    port = serial.Serial()
    port.port = name
    port.baudrate = 115200
    port.timeout = 0.1
    port.write_timeout = 5
    port.dtr = False
    port.rts = False
    port.open()
    port.reset_input_buffer()
    return port


def read_until(port: serial.Serial, pattern: bytes, timeout: float) -> bytes:
    deadline = time.monotonic() + timeout
    last_lines: list[bytes] = []
    while time.monotonic() < deadline:
        line = port.readline()
        if not line:
            continue
        last_lines.append(line)
        del last_lines[:-8]
        match = re.search(pattern, line)
        if match:
            return match.group(0)
    raise TimeoutError(f"timed out waiting for {pattern!r}; tail={b''.join(last_lines)!r}")


def command(port: serial.Serial, text: str, reply: bytes, timeout: float = 5.0) -> None:
    port.write((text + "\r\n").encode("ascii"))
    port.flush()
    read_until(port, reply, timeout)


def capture(port: serial.Serial, timeout: float = 12.0) -> Image.Image:
    port.write(b"NP FRAME\r\n")
    port.flush()
    header = read_until(
        port,
        rb"NPFB 220 128 56320 RGB565LE [0-9A-F]{8}\n",
        timeout,
    )
    expected_crc = int(header.rstrip().split()[-1], 16)
    payload = bytearray()
    deadline = time.monotonic() + timeout
    while len(payload) < BYTE_COUNT and time.monotonic() < deadline:
        payload.extend(port.read(BYTE_COUNT - len(payload)))
    if len(payload) != BYTE_COUNT:
        raise TimeoutError(f"received {len(payload)}/{BYTE_COUNT} framebuffer bytes")
    actual_crc = binascii.crc32(payload) & 0xFFFFFFFF
    if actual_crc != expected_crc:
        raise RuntimeError(f"frame CRC mismatch: {actual_crc:08X} != {expected_crc:08X}")
    read_until(port, rb"NPEND 56320 [0-9A-F]{8}\n", timeout)

    image = Image.new("RGB", (WIDTH, HEIGHT))
    pixels = image.load()
    for index in range(WIDTH * HEIGHT):
        value = payload[index * 2] | (payload[index * 2 + 1] << 8)
        red = ((value >> 11) & 0x1F) * 255 // 31
        green = ((value >> 5) & 0x3F) * 255 // 63
        blue = (value & 0x1F) * 255 // 31
        pixels[index % WIDTH, index // WIDTH] = (red, green, blue)
    return image


def contact_sheet(frames: list[tuple[str, Image.Image]]) -> Image.Image:
    scale = 3
    tile_width = WIDTH * scale
    tile_height = HEIGHT * scale + 34
    rows = (len(frames) + 1) // 2
    sheet = Image.new("RGB", (tile_width * 2 + 36, tile_height * rows + 24), "#05070d")
    draw = ImageDraw.Draw(sheet)
    for index, (label, image) in enumerate(frames):
        column = index % 2
        row = index // 2
        x = 12 + column * (tile_width + 12)
        y = 12 + row * (tile_height + 12)
        scaled = image.resize((tile_width, HEIGHT * scale), Image.Resampling.NEAREST)
        sheet.paste(scaled, (x, y))
        draw.text((x, y + HEIGHT * scale + 8), label.upper(), fill="#56f6ff")
    return sheet


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--settle", type=float, default=0.75)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    frames: list[tuple[str, Image.Image]] = []
    with open_port(args.port) as port:
        command(port, "NP PING", rb"NPOK ULTIMATE_CAPTURE")
        command(port, "NP DEMO", rb"NPOK DEMO", 10.0)
        time.sleep(args.settle)
        for index, page in enumerate(PAGES):
            frame = capture(port)
            frame.save(args.output / f"rcc6-ultimate-{page}.png", optimize=True)
            frame.resize((WIDTH * 4, HEIGHT * 4), Image.Resampling.NEAREST).save(
                args.output / f"rcc6-ultimate-{page}-4x.png", optimize=True
            )
            frames.append((page, frame))
            if index + 1 < len(PAGES):
                command(port, "NP NEXT", rb"NPOK NEXT")
                time.sleep(args.settle)

        # Return to Tools and capture the refined v2.1 composer flow.
        for _ in range(5):
            command(port, "NP NEXT", rb"NPOK NEXT")
            time.sleep(0.15)
        command(port, "NP ACTION", rb"NPOK ACTION")
        time.sleep(args.settle)
        command(port, "NP NEXT", rb"NPOK NEXT")
        command(port, "NP ACTION", rb"NPOK ACTION")
        time.sleep(args.settle)
        for label in ("composer-targets", "composer-phrases"):
            frame = capture(port)
            frame.save(args.output / f"rcc6-ultimate-{label}.png", optimize=True)
            frame.resize((WIDTH * 4, HEIGHT * 4), Image.Resampling.NEAREST).save(
                args.output / f"rcc6-ultimate-{label}-4x.png", optimize=True
            )
            frames.append((label, frame))
            command(port, "NP ACTION", rb"NPOK ACTION")
            time.sleep(args.settle)
        command(port, "NP CLEARDEMO", rb"NPOK CLEARDEMO", 10.0)

    contact_sheet(frames).save(args.output / "rcc6-ultimate-gallery.png", optimize=True)
    print(f"captured {len(frames)} pages to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
