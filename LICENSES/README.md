# Third-party license bundle

These files accompany NeonPocketMC-RCC6 release builds. They preserve the license notices shipped with the resolved Arduino libraries and include the full Apache-2.0 and GNU LGPL-2.1 texts used by the framework/platform components.

The current MeshCore 1.17 application source is tagged in [`n30nex/NeonPocketMC-RCC6`](https://github.com/n30nex/NeonPocketMC-RCC6/tags). Each release page identifies its exact source tag and checksum manifest. The application is provided as source so recipients can modify it and rebuild/relink it with a modified framework. No Arduino-ESP32 or ESP32 precompiled-library source was modified for the current release line.

Framework inputs used by the current release line:

- [Arduino-ESP32 3.1.3 source](https://github.com/espressif/arduino-esp32/tree/3.1.3)
- [Arduino-ESP32 3.1.3 source archive](https://github.com/espressif/arduino-esp32/archive/refs/tags/3.1.3.zip)
- [ESP32 Arduino precompiled libraries release `idf-release_v5.3`, build `489d7a2b`](https://github.com/espressif/esp32-arduino-lib-builder/releases/tag/idf-release_v5.3)
- [Exact precompiled-libraries package used by PlatformIO](https://github.com/espressif/esp32-arduino-lib-builder/releases/download/idf-release_v5.3/esp32-arduino-libs-idf-release_v5.3-489d7a2b-v1.zip)
- [PioArduino ESP32 platform 53.03.13-1](https://github.com/pioarduino/platform-espressif32/releases/tag/53.03.13-1)

Resolved application-library versions and source links are recorded in [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md). The repository build commands are `pio run -e heltec_rcc6_companion_radio_ble` and `pio run -e heltec_rcc6_companion_radio_web_ap`.
