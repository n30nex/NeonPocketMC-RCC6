# Third-party notices

NeonPocketMC-RCC6 is built from MeshCore and third-party embedded libraries. This file is a practical source and attribution index; the license files in each upstream project remain authoritative.

## MeshCore

- Project: [meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore)
- MeshCore 1.17 base commit: `727fc0512ce08bfd7b499e46daa7fca6eeec730d`
- License: MIT
- Copyright: 2025 Scott Powell / Ripple Radios
- Local license: [`license.txt`](license.txt)

## Framework and platform

| Component | Resolved version | License | Source |
|---|---|---|---|
| Arduino-ESP32 | 3.1.3 | LGPL-2.1-or-later | [source](https://github.com/espressif/arduino-esp32/tree/3.1.3); [local license](LICENSES/LGPL-2.1.txt) |
| ESP32 Arduino precompiled libraries | 5.3.0, build `489d7a2b3a` | LGPL-2.1-or-later and component licenses | [release/source](https://github.com/espressif/esp32-arduino-lib-builder/releases/tag/idf-release_v5.3); [exact package](https://github.com/espressif/esp32-arduino-lib-builder/releases/download/idf-release_v5.3/esp32-arduino-libs-idf-release_v5.3-489d7a2b-v1.zip); [local LGPL text](LICENSES/LGPL-2.1.txt) |
| PioArduino ESP32 platform | 53.03.13-1 | Apache-2.0 | [release/source](https://github.com/pioarduino/platform-espressif32/releases/tag/53.03.13-1); [local license](LICENSES/Apache-2.0.txt) |
| ESP32 BLE library bundled with Arduino-ESP32 | Arduino-ESP32 3.1.3 | Apache-2.0 | [source](https://github.com/espressif/arduino-esp32/tree/3.1.3/libraries/BLE); [local license](LICENSES/Apache-2.0.txt) |

The complete application source and build scripts are in this repository. Framework inputs, source archives, relinking information, and local license copies are indexed in [`LICENSES/README.md`](LICENSES/README.md). The resolved library versions are recorded below; some dependency requirements in `platformio.ini` use compatible-version ranges, so reproduce a release from its exact Git tag and checksum manifest. Modification and reverse engineering for debugging those modifications are not prohibited by this project.

## Libraries

| Component | Resolved version | License / notice | Source |
|---|---:|---|---|
| RadioLib | 7.7.1 (`6d893483…`) | MIT | [source](https://github.com/jgromes/RadioLib/tree/6d8934836678d8894e3d556550475b37dce3e2b6); [local license](LICENSES/RadioLib.txt) |
| ArduinoJson | 7.4.3 | MIT | [source](https://github.com/bblanchon/ArduinoJson/tree/v7.4.3); [local license](LICENSES/ArduinoJson.txt) |
| Crypto | 0.4.0 | Southern Storm permissive license | [versioned source package](https://registry.platformio.org/libraries/rweather/Crypto/versions/0.4.0); [local license](LICENSES/Crypto.txt) |
| base64 | 1.4.0 | MIT | [versioned source package](https://registry.platformio.org/libraries/densaugeo/base64/versions/1.4.0); [local license](LICENSES/base64.txt) |
| CayenneLPP | 1.6.1 | MIT; retain Things Network and author notices | [source](https://github.com/ElectronicCats/CayenneLPP/tree/1.6.1); [local license](LICENSES/CayenneLPP.txt) |
| Adafruit BusIO | 1.17.4 | MIT | [source](https://github.com/adafruit/Adafruit_BusIO/tree/1.17.4); [local license](LICENSES/Adafruit-BusIO.txt) |
| RTClib | 2.1.4 | MIT | [source](https://github.com/adafruit/RTClib/tree/2.1.4); [local license](LICENSES/RTClib.txt) |
| Melopero RV3028 | 1.2.0 | MIT | [source](https://github.com/melopero/Melopero_RV-3028_Arduino_Library/tree/1.2.0); [local license](LICENSES/Melopero-RV3028.txt) |
| ed25519 implementation bundled in MeshCore | bundled source | zlib-style; retain notice and mark altered source | [`lib/ed25519/license.txt`](lib/ed25519/license.txt) |

Additional transitive notices are retained in their source packages when fetched by PlatformIO. No endorsement by the upstream projects is implied.
