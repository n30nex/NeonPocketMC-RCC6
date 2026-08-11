#!/usr/bin/env python3
"""Fail-closed static contract checks for the Ultimate-only build surface."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


platform = text("variants/heltec_rcc6/platformio.ini")
display = text("src/helpers/ui/NV3001BDisplay.cpp")
service = text("examples/companion_radio/UltimateService.h")
web = text("examples/companion_radio/UltimateWebApi.cpp")
ui = text("examples/companion_radio/ui-new/UltimateUIScreen.cpp")

required = {
    "Ultimate BLE environment": "[env:heltec_rcc6_ultimate_companion_ble]" in platform,
    "Ultimate Web environment": "[env:heltec_rcc6_ultimate_companion_web]" in platform,
    "32 KiB gate": "NEONPOCKET_MEMORY_GATE_BYTES=32768" in platform,
    "indexed framebuffer": "NV3001B_USE_INDEXED_FRAMEBUFFER=1" in platform,
    "20 by 8 tiles": "framebuffer_tile_width = 20" in text("src/helpers/ui/NV3001BDisplay.h") and
        "framebuffer_tile_height = 8" in text("src/helpers/ui/NV3001BDisplay.h"),
    "256-byte history record": "static_assert(sizeof(UltimateHistoryRecord) == 256" in service,
    "32 event queue": "pending_capacity = 32" in service,
    "64 network nodes": "network_capacity = 64" in service,
    "signed OTA": "Ed25519::verify" in web and "Update.begin" in web,
    "location endpoint": '"/api/ultimate/location"' in web,
    "six-area UI": all(name in ui for name in ["HOME", "INBOX", "NETWORK", "RADIO", "TOOLS", "POWER"]),
    "15 FPS cadence": "animation_frame_millis = 66" in ui,
    "artifact verifier": (ROOT / "tools/verify_ultimate_artifact.py").is_file(),
    "portable recovery merger": (ROOT / "tools/merge_rcc6_image.py").is_file(),
}

failed = [name for name, passed in required.items() if not passed]
if failed:
    raise SystemExit("Ultimate source contract failed: " + ", ".join(failed))
print("Ultimate source contract verified")
