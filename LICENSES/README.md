# Third-party license bundle

These files accompany the RadioCore²-RCC6 1.0 RC1 binaries. They preserve the license notices shipped with the exact resolved Arduino libraries and include the full Apache-2.0 and GNU LGPL-2.1 texts used by the framework/platform components.

The exact RC1 application source is the Git tag [`v1.0.0-rc.1`](https://github.com/n30nex/RadioCore2-RCC6/tree/v1.0.0-rc.1). The application is provided as source so recipients can modify it and rebuild/relink it with a modified framework. No Arduino-ESP32 or ESP32 precompiled-library source was modified for this release.

Framework inputs used by RC1:

- [Arduino-ESP32 3.1.3 source](https://github.com/espressif/arduino-esp32/tree/3.1.3)
- [Arduino-ESP32 3.1.3 source archive](https://github.com/espressif/arduino-esp32/archive/refs/tags/3.1.3.zip)
- [ESP32 Arduino precompiled libraries release `idf-release_v5.3`, build `489d7a2b`](https://github.com/espressif/esp32-arduino-lib-builder/releases/tag/idf-release_v5.3)
- [Exact precompiled-libraries package used by PlatformIO](https://github.com/espressif/esp32-arduino-lib-builder/releases/download/idf-release_v5.3/esp32-arduino-libs-idf-release_v5.3-489d7a2b-v1.zip)
- [PioArduino ESP32 platform 53.03.13-1](https://github.com/pioarduino/platform-espressif32/releases/tag/53.03.13-1)

Resolved application-library versions and source links are recorded in [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md). The repository build command is `pio run -e heltec_rcc6_companion_radio_ble`.
