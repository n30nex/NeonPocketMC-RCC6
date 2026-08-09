#!/usr/bin/env python3
"""Capture exact RCC6 NeonPocket splash frames and build a looping GIF."""

import argparse
import pathlib
import struct
import time
import zlib

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

try:
    from PIL import Image
except ImportError as exc:
    raise SystemExit("Pillow is required: python -m pip install Pillow") from exc


WIDTH = 220
HEIGHT = 128
FRAME_BYTES = WIDTH * HEIGHT * 2
DURATION_MS = 3200
STEP_MS = 125


def read_to_deadline(port, length: int, deadline: float) -> bytes:
    data = bytearray()
    while len(data) < length:
        if time.monotonic() >= deadline:
            raise TimeoutError(f"timed out after {len(data)}/{length} bytes")
        chunk = port.read(length - len(data))
        if chunk:
            data.extend(chunk)
    return bytes(data)


def read_line_to_deadline(port, deadline: float, limit: int = 256) -> bytes:
    line = bytearray()
    while time.monotonic() < deadline:
        byte = port.read(1)
        if not byte:
            continue
        if byte == b"\n":
            return bytes(line).rstrip(b"\r")
        line.extend(byte)
        if len(line) > limit:
            raise RuntimeError("serial line exceeded safety limit")
    raise TimeoutError("timed out waiting for serial line")


def wait_for_line(port, prefix: bytes, deadline: float) -> bytes:
    while True:
        line = read_line_to_deadline(port, deadline)
        if line.startswith(prefix):
            return line


def parse_header(line: bytes) -> int:
    parts = line.split()
    if len(parts) != 6 or parts[:5] != [b"NPFB", b"220", b"128", b"56320", b"RGB565LE"]:
        raise RuntimeError(f"unexpected framebuffer header: {line!r}")
    return int(parts[5], 16)


def capture_frame(port, elapsed_ms: int, timeout: float) -> bytes:
    port.reset_input_buffer()
    port.write(f"NP SPLASH {elapsed_ms}\n".encode("ascii"))
    port.flush()
    deadline = time.monotonic() + timeout
    header = wait_for_line(port, b"NPFB ", deadline)
    expected_crc = parse_header(header)
    frame = read_to_deadline(port, FRAME_BYTES, deadline)
    trailer = wait_for_line(port, b"NPEND ", deadline)
    parts = trailer.split()
    if len(parts) != 3 or parts[:2] != [b"NPEND", b"56320"]:
        raise RuntimeError(f"unexpected framebuffer trailer: {trailer!r}")
    trailer_crc = int(parts[2], 16)
    actual_crc = zlib.crc32(frame) & 0xFFFFFFFF
    if expected_crc != trailer_crc or actual_crc != expected_crc:
        raise RuntimeError(
            f"CRC mismatch: header={expected_crc:08X} trailer={trailer_crc:08X} actual={actual_crc:08X}"
        )
    return frame


def rgb565le_image(frame: bytes) -> Image.Image:
    if len(frame) != FRAME_BYTES:
        raise ValueError(f"expected {FRAME_BYTES} bytes, received {len(frame)}")
    pixels = []
    for (value,) in struct.iter_unpack("<H", frame):
        pixels.append((
            ((value >> 11) & 0x1F) * 255 // 31,
            ((value >> 5) & 0x3F) * 255 // 63,
            (value & 0x1F) * 255 // 31,
        ))
    image = Image.new("RGB", (WIDTH, HEIGHT))
    image.putdata(pixels)
    return image


def elapsed_frames(step_ms: int) -> list[int]:
    frames = list(range(0, DURATION_MS + 1, step_ms))
    if frames[-1] != DURATION_MS:
        frames.append(DURATION_MS)
    return frames


def self_test() -> None:
    class Fragmented:
        def __init__(self, data: bytes):
            self.data = bytearray(data)

        def read(self, length: int) -> bytes:
            length = min(length, 3, len(self.data))
            chunk = self.data[:length]
            del self.data[:length]
            return bytes(chunk)

    assert read_to_deadline(Fragmented(b"abcdef"), 6, time.monotonic() + 1) == b"abcdef"
    assert read_line_to_deadline(Fragmented(b"hello\r\n"), time.monotonic() + 1) == b"hello"
    assert parse_header(b"NPFB 220 128 56320 RGB565LE 1234ABCD") == 0x1234ABCD
    test_frame = b"\x00\xF8" * (WIDTH * HEIGHT)
    assert rgb565le_image(test_frame).getpixel((0, 0)) == (255, 0, 0)
    assert elapsed_frames(125)[-1] == DURATION_MS
    print("self-test passed")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="verified RCC6 USB serial port")
    parser.add_argument("--output", type=pathlib.Path, help="output directory for PNGs and GIF")
    parser.add_argument("--scale", type=int, default=3, help="nearest-neighbor GIF scale (default: 3)")
    parser.add_argument("--step-ms", type=int, default=STEP_MS, help="capture interval (default: 125)")
    parser.add_argument("--timeout", type=float, default=12, help="deadline per frame in seconds")
    parser.add_argument("--final-hold-ms", type=int, default=750, help="final GIF frame hold")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        self_test()
        return
    if not args.port or args.output is None:
        parser.error("--port and --output are required unless --self-test is used")
    if args.scale < 1 or args.step_ms < 1 or args.timeout <= 0 or args.final_hold_ms < 1:
        parser.error("scale, step, timeout, and final hold must be positive")

    args.output.mkdir(parents=True, exist_ok=True)
    frames = []
    elapsed = elapsed_frames(args.step_ms)
    with serial.Serial(args.port, 115200, timeout=0.1, write_timeout=5) as port:
        port.dtr = False
        port.rts = False
        time.sleep(0.5)
        port.reset_input_buffer()
        port.write(b"NP PING\n")
        port.flush()
        handshake = wait_for_line(port, b"NPOK SPLASH_CAPTURE ", time.monotonic() + args.timeout)
        if handshake != b"NPOK SPLASH_CAPTURE 220 128 56320 RGB565LE 3200 125":
            raise RuntimeError(f"unexpected diagnostic handshake: {handshake!r}")
        nearest = getattr(Image, "Resampling", Image).NEAREST
        for index, elapsed_ms in enumerate(elapsed):
            frame = rgb565le_image(capture_frame(port, elapsed_ms, args.timeout))
            frame.save(args.output / f"frame-{index:03d}-{elapsed_ms:04d}ms.png")
            frames.append(frame.resize(
                (WIDTH * args.scale, HEIGHT * args.scale), nearest
            ))
            print(f"captured {elapsed_ms:4d} ms")

    durations = [args.step_ms] * len(frames)
    durations[-1] = args.final_hold_ms
    gif = args.output / "neonpocket-rcc6-splash.gif"
    frames[0].save(
        gif,
        save_all=True,
        append_images=frames[1:],
        duration=durations,
        loop=0,
        optimize=False,
        disposal=2,
    )
    print(gif)


if __name__ == "__main__":
    main()
