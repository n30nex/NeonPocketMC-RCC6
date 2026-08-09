#!/usr/bin/env python3
"""Capture the live RCC6 NV3001B framebuffer over USB serial."""

import argparse
import binascii
import pathlib
import struct
import time
import zlib

import serial


WIDTH = 220
HEIGHT = 128
FRAME_BYTES = WIDTH * HEIGHT * 2
PAGES = {
    0: "home",
    1: "nearby",
    2: "radio",
    4: "advert",
    5: "quick-reply",
    6: "diagnostics",
    7: "power",
}


def png_chunk(kind: bytes, data: bytes) -> bytes:
    crc = binascii.crc32(kind + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", crc)


def rgb565le_to_png(frame: bytes) -> bytes:
    if len(frame) != FRAME_BYTES:
        raise ValueError(f"expected {FRAME_BYTES} bytes, received {len(frame)}")
    rows = bytearray()
    for y in range(HEIGHT):
        rows.append(0)
        for x in range(WIDTH):
            value = struct.unpack_from("<H", frame, (y * WIDTH + x) * 2)[0]
            red = ((value >> 11) & 0x1F) * 255 // 31
            green = ((value >> 5) & 0x3F) * 255 // 63
            blue = (value & 0x1F) * 255 // 31
            rows.extend((red, green, blue))
    header = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(rows, 9))
        + png_chunk(b"IEND", b"")
    )


def wait_line(port: serial.Serial, prefix: bytes, timeout: float = 8.0) -> bytes:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = port.readline().strip()
        if line.startswith(prefix):
            return line
    raise TimeoutError(f"timed out waiting for {prefix.decode(errors='replace')}")


def capture(port: serial.Serial, page: int) -> bytes:
    port.write(f"NP PAGE {page}\n".encode())
    port.flush()
    wait_line(port, f"NPOK PAGE {page}".encode())
    time.sleep(1.25)
    port.reset_input_buffer()
    port.write(b"NP CAPTURE\n")
    port.flush()
    header = wait_line(port, b"NPFB ")
    if header != b"NPFB 220 128 56320 LE_RGB565":
        raise RuntimeError(f"unexpected framebuffer header: {header!r}")
    frame = port.read(FRAME_BYTES)
    if len(frame) != FRAME_BYTES:
        raise RuntimeError(f"short framebuffer: {len(frame)} bytes")
    trailer = wait_line(port, b"NPEND ")
    if trailer != b"NPEND 56320":
        raise RuntimeError(f"unexpected framebuffer trailer: {trailer!r}")
    return frame


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM21", help="RCC6 serial port (default: COM21)")
    parser.add_argument("--output", type=pathlib.Path, required=True, help="directory for PNG files")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    with serial.Serial(args.port, 115200, timeout=2, write_timeout=5) as port:
        port.dtr = False
        port.rts = False
        time.sleep(0.5)
        port.reset_input_buffer()
        port.write(b"NP PING\n")
        port.flush()
        wait_line(port, b"NPOK PONG")
        for page, name in PAGES.items():
            output = args.output / f"rcc6-{name}.png"
            output.write_bytes(rgb565le_to_png(capture(port, page)))
            print(output)


if __name__ == "__main__":
    main()
