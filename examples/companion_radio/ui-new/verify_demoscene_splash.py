from pathlib import Path

root = Path(__file__).resolve().parents[3]
header = (Path(__file__).parent / "NeonPocketSplash.h").read_text()
ui = (Path(__file__).parent / "UITask.cpp").read_text()
main = (root / "examples/companion_radio/main.cpp").read_text()

assert "DURATION_MILLIS = 3200" in header
assert "FRAME_MILLIS = 125" in header
assert '"NEONPOCKETMC"' in header
assert all(stage in header for stage in (
    '"VECTOR BOOT"', '"RADIO LINK"', '"COMPANION SYNC"', '"MESH READY"'))
assert "new " not in header and "malloc(" not in header and "delay(" not in header
assert "NeonPocketSplash::drawFrame(display, elapsed" in ui
assert "NeonPocketSplash::drawFrame(*disp, 0" in main
assert main.index("NeonPocketSplash::drawFrame(*disp, 0") < main.index('"Loading..."')

print("NeonPocket demo-scene splash contract verified")
