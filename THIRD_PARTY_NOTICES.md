# Third-party notices

RadioCore²-RCC6 is built from MeshCore and third-party embedded libraries. This file is a practical source and attribution index; the license files in each upstream project remain authoritative.

## MeshCore

- Project: [meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore)
- RC1 base commit: `fff37407652534d2077d121a7e51c920ec937bcb`
- License: MIT
- Copyright: 2025 Scott Powell / Ripple Radios
- Local license: [`license.txt`](license.txt)

## Framework and platform

| Component | RC1 version | License | Source |
|---|---|---|---|
| Arduino-ESP32 | 3.1.3 | LGPL-2.1-or-later | [espressif/arduino-esp32 3.1.3](https://github.com/espressif/arduino-esp32/tree/3.1.3) |
| ESP32 Arduino precompiled libraries | 5.3.0, build `489d7a2b3a` | LGPL-2.1-or-later and component licenses | [espressif/esp32-arduino-lib-builder](https://github.com/espressif/esp32-arduino-lib-builder) |
| PioArduino ESP32 platform | 53.03.13 | Apache-2.0 | [pioarduino/platform-espressif32](https://github.com/pioarduino/platform-espressif32) |
| ESP32 BLE library bundled with Arduino-ESP32 | Arduino-ESP32 3.1.3 | Apache-2.0 | [Arduino-ESP32 BLE library](https://github.com/espressif/arduino-esp32/tree/3.1.3/libraries/BLE) |

The GNU LGPL-2.1 text and terms are available from the [GNU license archive](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html). The complete application source and build scripts are in this repository. The pinned PlatformIO configuration retrieves the framework and library inputs used for a rebuild. Modification and reverse engineering for debugging those modifications are not prohibited by this project.

## Libraries

| Component | RC1 version | License / notice | Source |
|---|---:|---|---|
| RadioLib | 7.7.1 (`6d893483…`) | MIT | [jgromes/RadioLib](https://github.com/jgromes/RadioLib) |
| ArduinoJson | 7.4.3 | MIT | [bblanchon/ArduinoJson](https://github.com/bblanchon/ArduinoJson) |
| Crypto | 0.4.0 | Southern Storm permissive license | [rweather/arduinolibs](https://github.com/rweather/arduinolibs) |
| base64 | 1.4.0 | MIT | [densaugeo/base64_arduino](https://github.com/Densaugeo/base64_arduino) |
| CayenneLPP | 1.6.1 | MIT; retain Things Network and author notices | [ElectronicCats/CayenneLPP](https://github.com/ElectronicCats/CayenneLPP) |
| Adafruit BusIO | 1.17.4 | MIT | [adafruit/Adafruit_BusIO](https://github.com/adafruit/Adafruit_BusIO) |
| RTClib | 2.1.4 | MIT | [adafruit/RTClib](https://github.com/adafruit/RTClib) |
| Melopero RV3028 | 1.2.0 | MIT | [melopero/Melopero_RV3028](https://github.com/melopero/Melopero_RV3028) |
| ed25519 implementation bundled in MeshCore | bundled source | zlib-style; retain notice and mark altered source | [`lib/ed25519/license.txt`](lib/ed25519/license.txt) |

Additional transitive notices are retained in their source packages when fetched by PlatformIO. No endorsement by the upstream projects is implied.
