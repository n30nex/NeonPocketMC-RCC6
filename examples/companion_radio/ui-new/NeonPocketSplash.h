#pragma once

#include <helpers/ui/DisplayDriver.h>

namespace NeonPocketSplash {

static constexpr unsigned long DURATION_MILLIS = 3200;
static constexpr unsigned long FRAME_MILLIS = 125;

static constexpr ColorVal BLACK = 0x0000;
static constexpr ColorVal CYAN = 0x07FF;
static constexpr ColorVal COBALT = 0x001F;
static constexpr ColorVal GRID_BLUE = 0x0008;
static constexpr ColorVal LIME = 0x07E0;
static constexpr ColorVal MAGENTA = 0xF81F;
static constexpr ColorVal WHITE = 0xFFFF;

inline void shortVersion(char* dest, size_t size, const char* version) {
  if (size == 0) return;
  size_t i = 0;
  while (version[i] && version[i] != '-' && i + 1 < size) {
    dest[i] = version[i];
    i++;
  }
  dest[i] = 0;
}

inline void line(DisplayDriver& display, int x0, int y0, int x1, int y1) {
  const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  const int sx = x0 < x1 ? 1 : -1;
  const int raw_dy = y1 > y0 ? y1 - y0 : y0 - y1;
  const int dy = -raw_dy;
  const int sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;
  while (true) {
    display.fillRect(x0, y0, 1, 1);
    if (x0 == x1 && y0 == y1) break;
    const int twice_error = error * 2;
    if (twice_error >= dy) {
      error += dy;
      x0 += sx;
    }
    if (twice_error <= dx) {
      error += dx;
      y0 += sy;
    }
  }
}

inline void drawBackdrop(DisplayDriver& display, uint8_t frame) {
  const int width = display.width();
  const uint8_t stars = frame < 4 ? 6 + frame * 3 : 18;
  for (uint8_t i = 0; i < stars; i++) {
    const int x = 3 + (i * 47 + i * i * 3) % (width - 6);
    const int y = 2 + (i * 13 + i * i * 5) % 72;
    display.setColor((i % 5) == 0 ? CYAN : ((i % 3) == 0 ? COBALT : 0x0208));
    display.fillRect(x, y, (i % 7) == 0 ? 2 : 1, 1);
  }

  if (frame < 4) return;
  const int horizon = 78;
  display.setColor(GRID_BLUE);
  display.fillRect(0, horizon, width, 1);
  for (int i = 0; i <= 6; i++) {
    line(display, width / 2, horizon, i * width / 6, 100);
  }
  display.fillRect(0, 83, width, 1);
  display.fillRect(0, 90, width, 1);
  display.fillRect(0, 99, width, 1);
}

inline void drawPocket(DisplayDriver& display, uint8_t frame) {
  const int x = (display.width() - 50) / 2;
  const int y = 3;

  display.setColor(CYAN);
  display.fillRect(x + 18, y, 14, 2);
  if (frame >= 1) {
    display.fillRect(x, y + 5, 50, 2);
    display.fillRect(x, y + 5, 2, 25);
    display.fillRect(x + 48, y + 5, 2, 25);
  }
  if (frame >= 2) {
    display.fillRect(x, y + 28, 50, 2);
    display.setColor(COBALT);
    display.drawRect(x + 5, y + 10, 40, 15);
  }
  if (frame >= 3) {
    display.setColor(COBALT);
    line(display, x + 10, y + 23, x + 25, y + 12);
    line(display, x + 25, y + 12, x + 40, y + 23);
    line(display, x + 10, y + 23, x + 40, y + 23);
  }
  if (frame >= 4) {
    display.setColor(LIME);
    display.fillRect(x + 8, y + 21, 5, 5);
    display.fillRect(x + 23, y + 10, 5, 5);
    display.fillRect(x + 38, y + 21, 5, 5);
  }
  if (frame >= 5) {
    display.setColor(CYAN);
    display.fillRect(x + 5, y, 4, 1);
    display.fillRect(x + 39, y + 1, 5, 1);
    display.fillRect(x + 24, y - 2, 2, 3);
  }
}

inline void drawTitle(DisplayDriver& display, uint8_t frame) {
  display.setTextSize(2);
  if (frame < 4) {
    const int offset = (frame & 1) ? 2 : -2;
    display.setColor(MAGENTA);
    display.drawTextCentered(display.width() / 2 - offset, 40, "NEONPOCKETMC");
    display.setColor(COBALT);
    display.drawTextCentered(display.width() / 2 + offset, 38, "NEONPOCKETMC");
  } else {
    display.setColor(COBALT);
    display.drawTextCentered(display.width() / 2 + 1, 40, "NEONPOCKETMC");
  }
  display.setColor(LIME);
  display.drawTextCentered(display.width() / 2, 39, "NEONPOCKETMC");

  if (frame >= 4) {
    display.setTextSize(1);
    display.setColor(CYAN);
    display.drawTextCentered(display.width() / 2, 64, "MESHCORE COMPANION");
  }
}

inline const char* statusFor(unsigned long elapsed) {
  if (elapsed < 800) return "VECTOR BOOT";
  if (elapsed < 1600) return "RADIO LINK";
  if (elapsed < 2400) return "COMPANION SYNC";
  return "MESH READY";
}

inline void drawFrame(DisplayDriver& display, unsigned long elapsed,
    const char* version, const char* build_date) {
  if (elapsed > DURATION_MILLIS) elapsed = DURATION_MILLIS;
  const uint8_t frame = elapsed / FRAME_MILLIS;

  drawBackdrop(display, frame);
  drawPocket(display, frame);
  drawTitle(display, frame);

  display.setTextSize(1);
  display.setColor(WHITE);
  display.setCursor(10, 84);
  display.print(version);
  display.drawTextRightAlign(display.width() - 10, 84, build_date);

  const int bar_x = 14;
  const int bar_width = display.width() - bar_x * 2;
  display.setColor(COBALT);
  display.drawRect(bar_x, 102, bar_width, 8);
  display.setColor(CYAN);
  display.fillRect(bar_x + 2, 104,
      (bar_width - 4) * elapsed / DURATION_MILLIS, 4);

  display.setColor(elapsed >= 2400 ? LIME : CYAN);
  display.drawTextCentered(display.width() / 2, 113, statusFor(elapsed));

  const int scan_y = 2 + (frame * 5) % 94;
  const int scan_x = (frame * 19) % (display.width() + 36) - 36;
  display.setColor(GRID_BLUE);
  display.fillRect(0, scan_y + 2, display.width(), 1);
  display.setColor(COBALT);
  display.fillRect(0, scan_y, display.width(), 1);
  display.setColor(CYAN);
  display.fillRect(scan_x, scan_y, 36, 1);

  if (frame >= 10 && frame <= 18) {
    const int sweep_x = (display.width() - 50) / 2 + (frame - 10) * 6;
    display.setColor(CYAN);
    line(display, sweep_x - 4, 5, sweep_x - 10, 32);
    display.setColor(WHITE);
    line(display, sweep_x, 5, sweep_x - 6, 32);
  }
}

}  // namespace NeonPocketSplash
