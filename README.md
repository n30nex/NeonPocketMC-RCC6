# RadioCore²-RCC6

MeshCore Companion firmware for the **Heltec RadioCore RCC6** with its native **220×128 NV3001B TFT**. Two firmware modes are provided: secure BLE companion mode, and Wi-Fi Web/AP mode with an offline WebUI plus raw TCP/5000 access for trusted private networks.

[![Release](https://img.shields.io/github/v/release/n30nex/RadioCore2-RCC6?include_prereleases&label=firmware)](https://github.com/n30nex/RadioCore2-RCC6/releases)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](license.txt)
[![Target: ESP32-C6](https://img.shields.io/badge/target-ESP32--C6-00bcd4)](#supported-hardware)

> [!WARNING]
> **1.0 RC1 downloads are temporarily withdrawn.** Two independent users reported a boot loop or blank device after flashing the published RC1 files. Do not flash RC1 while the recovery path and exact replacement candidate are being requalified. If you already flashed it, do not erase the device or overwrite SPIFFS; put the RCC6 into download mode and open an issue with the file used, flash offset, hardware revision, and boot log if available.

## On-device UI

<p align="center">
  <img src="docs/images/neon-pocket-on-device.jpg" alt="RadioCore RCC6 running the Neon Pocket MeshCore dashboard from battery power" width="760">
</p>

<p align="center"><em>Neon Pocket dashboard running on a battery-powered RadioCore RCC6.</em></p>

<details>
<summary>RadioCore hardware</summary>

<p align="center">
  <img src="docs/images/radiocore-hardware.jpg" alt="Two RadioCore units with attached TFT boards" width="620">
</p>

</details>

## What RC1 adds

- Native landscape **220×128** phone-style dashboard for the RCC6 TFT.
- Home cards for unread messages and recent adverts.
- BLE, battery, RF, unread, RX/TX, RSSI, and SNR status at a glance.
- Recent nodes, radio settings, Bluetooth, Advert, Power, and message views.
- Color-coded visual notifications for messages, nearby nodes, BLE state, adverts, and low battery.
- One-button press/double-press/hold controls and screen-off/wake behavior.
- Subtle page transitions and notification motion.
- RGB565 framebuffer with 8-row delta flushing; measured RC1 delta frame: **121 ms** versus **342–360 ms** for a full frame.
- Static BLE frame queues sized for companion synchronization bursts.
- Responsive iPhone-dark WebUI for phones and desktops, served directly by the RCC6.
- 2.4 GHz setup AP, local Wi-Fi setup wizard, DHCP address on the TFT, and automatic setup-AP fallback.
- One exclusive WebUI or raw TCP/5000 companion session at a time.
- DIO flash mode for RCC6 compatibility.

## Controls

| Action | Result |
|---|---|
| Press while screen is off | Wake the display; the press is consumed |
| Press while screen is on | Next page or next message page |
| Double-press | Run the action shown on the current page |
| Hold | Open the Power confirmation page |
| Hold again within eight seconds | Power off after the button is released |

Wait at least eight seconds after boot before using Hold; the early-boot hold remains MeshCore's CLI rescue gesture.

## Supported hardware

- Heltec **RadioCore RCC6** / ESP32-C6
- RCC6 LoRa module configuration used by the upstream `heltec_rcc6` target
- Attached Heltec NV3001B **220×128 TFT**
- BLE companion firmware mode
- Wi-Fi Web/AP companion firmware mode with HTTP/80 and TCP/5000

> [!CAUTION]
> This build targets **RCC6 only**. Do not flash it onto RadioCore RC32, RC52, or unrelated ESP32 boards.

Attach a suitable antenna before transmitting. For battery use, use a protected single-cell 3.7 V Li-ion/LiPo pack and verify that the pack permits the board's configured charging current before charging it from USB.

## Release candidates

The original RC1 binary was withdrawn after startup-failure reports. **RC2 replaces it** with independently built BLE and Web/AP images that were app-only flashed and smoke-tested on the exact RCC6 hardware. Do not use old RC1 binaries copied from caches or chat attachments.

## Build from source

This repository is based on MeshCore `v1.16.0` development source at upstream commit `fff37407652534d2077d121a7e51c920ec937bcb`.

```shell
git clone https://github.com/n30nex/RadioCore2-RCC6.git
cd RadioCore2-RCC6
pio run -e heltec_rcc6_companion_radio_ble
pio run -e heltec_rcc6_companion_radio_web_ap
```

The application binary is produced under:

```text
.pio/build/heltec_rcc6_companion_radio_ble/firmware.bin
.pio/build/heltec_rcc6_companion_radio_web_ap/firmware.bin
```

The Web/AP image starts a WPA-protected setup network and serves its WebUI at `http://192.168.4.1`. After the Home-page setup wizard joins a local 2.4 GHz network, the TFT shows the DHCP address. Station-mode HTTP uses username `meshcore` and the eight-letter device key shown on the TFT. Raw TCP/5000 exposes the full MeshCore companion/admin protocol without application authentication, so use station mode only on a trusted private LAN.

## RC2 validation

Validated on physical hardware:

- Exact ESP32-C6 USB identity and DIO firmware header.
- App-only flash at `0x10000` with byte-for-byte `verify-flash` success.
- Framebuffer allocation: 56,320 bytes.
- Full TFT flush: 342–360 ms; five-band delta flush: 121 ms.
- Stable USB enumeration across repeated observation windows and manual resets.
- BLE advertisement as `MeshCore-<node name>` with Nordic UART service `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`.
- Secure six-digit PIN authentication, node load in the companion app, disconnect, re-advertise, and reconnect.
- Native landscape UI and one-button navigation on the device.
- Battery-powered boot and display operation.
- Web/AP station join, authenticated WebUI load, and DHCP address shown on the TFT.
- Native TCP/5000 `DEVICE_INFO` response from a desktop client.
- Direct BLE-mode and WebUI-mode Public LoRa transmissions independently received by a second MeshCore companion radio at 11.75 and 12.0 dB SNR.

Still to qualify before final 1.0:

- Full bidirectional LoRa soak and failure-path qualification.
- Complete one-button action matrix.
- Every notification color and animation path.
- Controlled low-battery and transmit-failure paths.
- Measured battery runtime and charging qualification.

## Upstream and license

RadioCore²-RCC6 is a community firmware build, not an official Heltec or MeshCore release.

It is built on [MeshCore](https://github.com/meshcore-dev/MeshCore) and retains the upstream MIT license and copyright notice in [`license.txt`](license.txt). Dependency licenses and exact source/relinking pointers are listed in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) and [`LICENSES/README.md`](LICENSES/README.md). Thanks to the MeshCore contributors and Heltec RadioCore beta community.

## Contributing

Please include the exact RCC6 hardware revision, firmware release, radio region/settings, and reproduction steps with bug reports. Never post private keys, pairing PINs, channel secrets, or full device backups.
